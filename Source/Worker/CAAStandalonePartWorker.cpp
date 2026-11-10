#include <stdio.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <vector>
#include <string>
#include <math.h>
#include <float.h>
#include <wctype.h>

// CATIA / CAA Session & Document Services
#include "CATSession.h"
#include "CATSessionServices.h"
#include "CATDocument.h"
#include "CATDocumentServices.h"
#include "CATInit.h"
#include "CATIPrtContainer.h"
#include "CATIPrtPart.h"
#include "CATIPartRequest.h"
#include "CATIDescendants.h"
#include "CATISpecObject.h"
#include "CATLISTV_CATISpecObject.h"
#include "CATIAlias.h"

// CATIA Visualization & Offscreen Rendering
#include "CATVisManager.h"
#include "CATI3DGeoVisu.h"
#include "CATPathElement.h"
#include "CAT3DViewpoint.h"
#include "CAT3DRep.h"
#include "CAT3DBoundingSphere.h"
#include "CATPrint3DRepImage.h"
#include "CATPixelImage.h"
#include "CATSupport.h"
#include "CATMathDirectionf.h"

// CATIA Topology, Geometry & Mass Properties
#include "CATIBodyRequest.h"
#include "CATIGeometricalElement.h"
#include "CATLISTV_CATBaseUnknown.h"
#include "CATBody.h"
#include "CATCell.h"
#include "CATFace.h"
#include "CATListOfCATCells.h"
#include "CATTopData.h"
#include "CATICGMDynMassProperties3D.h"
#include "CATMathPoint.h"
#include "CATICGMObject.h"

// Visibility & Show/NoShow Detection
#include "CATIVisProperties.h"
#include "CATVisPropertiesValues.h"
#include "CATShowAttribut.h"

// Convert Ansi string to UTF-8
static std::string utf8(const char* s) {
    if (!s) return "";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (!n) return "";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
    int k = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string out(k, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], k, NULL, NULL);
    out.resize(k - 1);
    return out;
}

// Write quoted CSV field escaping quotation marks
static void csvField(FILE* f, const std::string& s) {
    fputc('"', f);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"') fputc('"', f);
        fputc(s[i], f);
    }
    fputc('"', f);
}

// Convert CATUnicodeString to std::wstring
static std::wstring catWide(const CATUnicodeString& value) {
    const CATUC2Bytes* p = value.ConvertToUCChar();
    std::wstring w;
    if (p) {
        while (*p) {
            w.push_back((wchar_t)*p);
            ++p;
        }
    }
    return w;
}

// Convert std::wstring to UTF-8 std::string
static std::string wideUtf8(const std::wstring& value) {
    if (value.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &s[0], n, NULL, NULL);
    s.resize(n - 1);
    return s;
}

// Check if an object is hidden (in NoShow / Invisible state)
static bool isObjectHidden(CATBaseUnknown* unk) {
    if (!unk) return false;
    CATIVisProperties* visProps = NULL;
    bool hidden = false;
    if (SUCCEEDED(unk->QueryInterface(IID_CATIVisProperties, (void**)&visProps)) && visProps) {
        CATVisPropertiesValues vals;
        if (SUCCEEDED(visProps->GetPropertiesAtt(vals, CATVPShow, CATVPGlobalType))) {
            CATShowAttribut show = CATShowAttr;
            if (SUCCEEDED(vals.GetShowAttr(show)) && (show == CATNoShowAttr || show == CATInvisibleAttr)) {
                hidden = true;
            }
        }
        visProps->Release();
    }
    return hidden;
}

// Geometry measurement results
struct Measurement {
    bool ok;
    bool volumeOk;
    bool areaWarning;
    bool areaFailed;
    double volume;
    double area;
    double x;
    double y;
    double z;
    double negativeAbs;
    unsigned long tag;
    int positiveCount;
    int negativeCount;

    Measurement()
        : ok(false), volumeOk(false), areaWarning(false), areaFailed(false),
          volume(0), area(0), x(0), y(0), z(0),
          negativeAbs(0), tag(0), positiveCount(0), negativeCount(0) {}
};

// Measure 3D solid body volume, surface area, and center of gravity
static Measurement measureBody(CATISpecObject_var obj, FILE* f) {
    Measurement m;
    CATIBodyRequest* req = NULL;
    if (FAILED(obj->QueryInterface(IID_CATIBodyRequest, (void**)&req)) || !req) {
        return m;
    }

    CATLISTV(CATBaseUnknown_var) results;
    HRESULT hr = req->GetResults(CATUnicodeString("MfDefault3DView"), results);
    bool failed = false;

    if (SUCCEEDED(hr)) {
        for (int j = 1; j <= results.Size(); j++) {
            CATIGeometricalElement* geo = NULL;
            if (FAILED(results[j]->QueryInterface(IID_CATIGeometricalElement, (void**)&geo)) || !geo) {
                failed = true;
                continue;
            }

            CATBody_var body = geo->GetBodyResult();
            if (NULL_var != body) {
                CATTopData td;
                CATICGMDynMassProperties3D* op = CATCGMDynCreateMassProperties3D(&td, (CATBody*)body);
                if (op) {
                    double vol = op->GetVolume();
                    if (_finite(vol) && vol > 0) {
                        CATMathPoint pt = op->GetCenterOfGravity();
                        if (_finite(pt.GetX()) && _finite(pt.GetY()) && _finite(pt.GetZ())) {
                            // Direct, fast, native CGM surface area calculation
                            double area = op->GetWetArea();

                            // Fallback to cell iteration only if direct wet area is invalid or non-positive
                            if (!_finite(area) || area <= 0.0) {
                                CATLISTP(CATCell) faces;
                                body->GetAllCells(faces, 2);
                                area = 0.0;
                                for (int k = 1; k <= faces.Size(); k++) {
                                    CATICGMDynMassProperties3D* faceOp = CATCGMDynCreateMassProperties3D(&td, (CATFace*)faces[k]);
                                    if (faceOp) {
                                        double fa = faceOp->GetWetArea();
                                        faceOp->Release();
                                        if (_finite(fa)) {
                                            area += fabs(fa);
                                        }
                                    }
                                }
                            }

                            if (!_finite(area) || area <= 0.0) {
                                m.areaFailed = true;
                            }

                            m.positiveCount++;
                            m.volume = vol;
                            m.area = area;
                            m.x = pt.GetX();
                            m.y = pt.GetY();
                            m.z = pt.GetZ();

                            CATICGMObject* co = NULL;
                            if (SUCCEEDED(body->QueryInterface(IID_CATICGMObject, (void**)&co)) && co) {
                                m.tag = (unsigned long)co->GetPersistentTag();
                                co->Release();
                            }
                        } else {
                            failed = true;
                        }
                    } else if (!_finite(vol) || vol < 0) {
                        failed = true;
                    }
                    op->Release();
                } else {
                    failed = true;
                }
            }
            geo->Release();
        }
    } else {
        failed = true;
    }

    fprintf(f, "measurement results=%d solids=%d invalid=%d area=%.17g vol=%.17g\n",
            results.Size(), m.positiveCount, failed ? 1 : 0, m.area, m.volume);
    req->Release();

    // Body is valid if exactly 1 solid result was measured with valid volume and surface area
    m.volumeOk = !failed && m.positiveCount == 1;
    m.areaFailed = !(_finite(m.area) && m.area > 0);
    m.areaWarning = false;
    m.ok = m.volumeOk && !m.areaFailed;

    return m;
}

// Instance representing one solid body in the model
struct Instance {
    CATISpecObject_var obj;
    std::wstring displayWide;
    std::string display;
    std::string feature;
    Measurement m;
    int group;
    bool tiny;

    Instance() : group(-1), tiny(false) {}
};

// Group representing identical parts grouped by volume and surface area
struct Group {
    int rep;
    std::vector<int> members;
    double volumeSum;
    double momentX;
    double momentY;
    double momentZ;
    std::string image;
    bool imageOk;

    Group() : rep(-1), volumeSum(0), momentX(0), momentY(0), momentZ(0), imageOk(false) {}
};

static bool shouldFilterTiny(bool volumeOk, double volume) {
    return volumeOk && volume < 1.0;
}

static bool nearVal(double a, double b, double absTol) {
    return fabs(a - b) <= absTol + 1e-6 * ((fabs(a) > fabs(b)) ? fabs(a) : fabs(b));
}

static bool existing(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Sanitize filename to ensure Windows path safety
static std::wstring safeName(const std::wstring& value) {
    std::wstring result;
    for (size_t i = 0; i < value.size() && result.size() < 150; i++) {
        wchar_t c = value[i];
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            c = L'_';
        }
        result.push_back(c);
    }
    if (!result.empty() && result[result.size() - 1] >= 0xD800 && result[result.size() - 1] <= 0xDBFF) {
        result.erase(result.size() - 1);
    }
    while (!result.empty() && (result[result.size() - 1] == L'.' || result[result.size() - 1] == L' ')) {
        result.erase(result.size() - 1);
    }
    if (result.empty() || result == L"." || result == L"..") {
        result = L"Entity";
    }
    std::wstring key = result.substr(0, result.find(L'.'));
    while (!key.empty() && (key[key.size() - 1] == L' ' || key[key.size() - 1] == L'.')) {
        key.erase(key.size() - 1);
    }
    for (size_t i = 0; i < key.size(); i++) {
        key[i] = (wchar_t)towupper(key[i]);
    }
    bool reserved = (key == L"CON" || key == L"PRN" || key == L"AUX" || key == L"NUL" ||
                     (key.size() == 4 && (key.substr(0, 3) == L"COM" || key.substr(0, 3) == L"LPT") &&
                      ((key[3] >= L'1' && key[3] <= L'9') || key[3] == 0x00B9 || key[3] == 0x00B2 || key[3] == 0x00B3)));
    if (reserved) {
        result = L"_" + result;
    }
    return result;
}

static std::wstring uniqueName(const std::wstring& out, const std::wstring& name) {
    std::wstring base = safeName(name);
    std::wstring filename = base + L".png";
    for (unsigned i = 2; existing(out + filename); i++) {
        wchar_t suffix[32];
        swprintf_s(suffix, L" (%u)", i);
        filename = base + suffix + L".png";
    }
    return filename;
}

// Offscreen render body representation with 2x supersampling and composite onto white RGB
static int testRep(CATISpecObject_var obj, FILE* f, const std::string& temporary, const std::wstring& target, int size) {
    int success = 0;
    int renderSize = size * 2; // Always 2x supersampling

    CATPathElement* path = new CATPathElement(obj);
    CATVisManager* mgr = CATVisManager::GetVisManager();
    CAT3DViewpoint* vp = new CAT3DViewpoint();

    list<IID> ids;
    IID* iid = new IID(IID_CATI3DGeoVisu);
    ids.fastadd(iid);

    HRESULT r = mgr ? mgr->AttachTo(path, vp, ids) : E_FAIL;
    fprintf(f, "attach=%08lx\n", r);
    fflush(f);

    delete iid;
    ids.empty();
    path->Release();

    if (SUCCEEDED(r)) {
        CATI3DGeoVisu* vis = NULL;
        HRESULT q = obj->QueryInterface(IID_CATI3DGeoVisu, (void**)&vis);
        fprintf(f, "vis_qi=%08lx\n", q);

        if (vis) {
            CATRep* rep = vis->GiveRep();
            CAT3DRep* r3 = (CAT3DRep*)rep;
            if (r3) {
                CATPrint3DRepImage image(r3, renderSize, renderSize);
                CATSupport* support = image.GetSupport();
                CAT3DViewpoint* camera = image.GetViewpoint();
                if (support && camera) {
                    support->SetBackgroundColor(1.0f, 1.0f, 1.0f);
                    support->SetAntiAliasingMode(1);
                    camera->SetProjectionType(CYLINDRIC);
                    camera->SetSightDirection(CATMathDirectionf(-1.0f, -1.0f, -1.0f));
                    camera->SetUpDirection(CATMathDirectionf(0.0f, 0.0f, 1.0f));

                    CAT3DBoundingSphere sphere = r3->GetBoundingElement();
                    CAT3DBoundingSphere padded(sphere.GetCenter(), sphere.GetRadius() * 1.15f);
                    camera->Reframe((float)renderSize, (float)renderSize,
                                   support->GetMMInSupportUnit(), support->GetRatioWH(), padded);

                    CATPixelImage* pix = image.Rasterize();
                    if (pix) {
                        int w = 0, h = 0;
                        pix->GetSize(w, h);
                        int wr = -999;
                        if (w == renderSize && h == renderSize && pix->GetFormat() == RGBA) {
                            const unsigned char* rgba = pix->GetPixelsToRead();
                            unsigned char* rgb = new unsigned char[(size_t)w * h * 3];
                            for (size_t z = 0; z < (size_t)w * h; z++) {
                                unsigned int alpha = rgba[4 * z + 3];
                                for (int c = 0; c < 3; c++) {
                                    rgb[3 * z + c] = (unsigned char)((rgba[4 * z + c] * alpha + 255 * (255 - alpha) + 127) / 255);
                                }
                            }

                            {
                                CATPixelImage white(w, h, RGB, rgb, PixDeallocateNO);
                                CATPixelImage* down = white.CreateZoomedImage(size, size, 3);
                                if (down) {
                                    int dw = 0, dh = 0;
                                    down->GetSize(dw, dh);
                                    if (dw == size && dh == size) {
                                        wr = down->WriteToFile("PNG", temporary.c_str());
                                    }
                                    down->Release();
                                }
                            }
                            delete[] rgb;
                        }

                        if (wr == 0) {
                            int nw = MultiByteToWideChar(CP_ACP, 0, temporary.c_str(), -1, NULL, 0);
                            std::wstring tw(nw, L'\0');
                            if (nw) {
                                MultiByteToWideChar(CP_ACP, 0, temporary.c_str(), -1, &tw[0], nw);
                            }
                            if (nw && MoveFileW(tw.c_str(), target.c_str())) {
                                success = 1;
                            } else {
                                fprintf(f, "move_error=%lu\n", (unsigned long)GetLastError());
                                DeleteFileA(temporary.c_str());
                            }
                        }

                        if (wr != 0 && GetFileAttributesA(temporary.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            DeleteFileA(temporary.c_str());
                        }

                        fprintf(f, "raster=%dx%d target=%d write=%d moved=%d\n", w, h, size, wr, success);
                        fflush(f);
                        pix->Release();
                    } else {
                        fprintf(f, "raster=null\n");
                    }
                }
            }
            vis->Release();
        }

        r = mgr->DetachFrom(vp, 0);
        fprintf(f, "detach=%08lx\n", r);
        if (FAILED(r)) success = 0;
    }

    vp->Release();
    return success;
}

static bool isAsciiPath(const std::wstring& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] > 127) return false;
    }
    return true;
}

static std::wstring asciiScratchRoot() {
    wchar_t path[32768];
    DWORD n = GetTempPathW(32768, path);
    if (n && n < 32768) {
        std::wstring p(path);
        if (isAsciiPath(p)) return p;
        wchar_t shortPath[32768];
        DWORD k = GetShortPathNameW(path, shortPath, 32768);
        if (k && k < 32768 && isAsciiPath(shortPath)) return shortPath;
    }
    n = GetEnvironmentVariableW(L"PUBLIC", path, 32768);
    if (n && n < 32768 && isAsciiPath(path)) return path;
    return L"";
}

int main(int argc, char** argv) {
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || (wargc != 4 && wargc != 5)) {
        fprintf(stderr, "Usage: CAAStandalonePartWorker.exe <input.CATPart> <output-directory> <600|800|1200> [density_kg_m3]\n");
        return 2;
    }

    std::wstring inputArg = wargv[1];
    std::wstring outputArg = wargv[2];

    if (inputArg == L"@env" || outputArg == L"@env") {
        wchar_t value[32768];
        if (inputArg == L"@env") {
            DWORD n = GetEnvironmentVariableW(L"CAA_INPUT_PATH", value, 32768);
            if (n == 0 || n >= 32768) {
                fprintf(stderr, "CAA_INPUT_PATH missing\n");
                return 2;
            }
            inputArg = value;
        }
        if (outputArg == L"@env") {
            DWORD n = GetEnvironmentVariableW(L"CAA_OUTPUT_DIR", value, 32768);
            if (n == 0 || n >= 32768) {
                fprintf(stderr, "CAA_OUTPUT_DIR missing\n");
                return 2;
            }
            outputArg = value;
        }
    }

    double density = 1010.0;
    if (wargc == 5) {
        wchar_t* end = NULL;
        density = wcstod(wargv[4], &end);
        if (end == wargv[4] || *end != 0 || !_finite(density) || density <= 0) {
            fprintf(stderr, "Invalid density\n");
            return 2;
        }
    }

    // Strictly validate image resolution tiers (600, 800, 1200)
    int size = _wtoi(wargv[3]);
    if (size != 600 && size != 800 && size != 1200) {
        fprintf(stderr, "Invalid image size: %d. Expected 600, 800, or 1200\n", size);
        return 2;
    }

    DWORD attr = GetFileAttributesW(outputArg.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "Output directory does not exist\n");
        return 2;
    }

    std::wstring out = outputArg;
    if (out.empty()) return 2;
    if (out[out.size() - 1] != L'\\' && out[out.size() - 1] != L'/') out += L'\\';

    char runid[80];
    SYSTEMTIME now;
    GetLocalTime(&now);
    sprintf_s(runid, "%04d%02d%02d_%02d%02d%02d_%03d_%lu",
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
              now.wMilliseconds, (unsigned long)GetCurrentProcessId());

    int ridLen = MultiByteToWideChar(CP_ACP, 0, runid, -1, NULL, 0);
    std::wstring wid(ridLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, runid, -1, &wid[0], ridLen);
    wid.resize(ridLen - 1);

    std::wstring logPath = out + L"capture_" + wid + L".log";
    std::wstring csvPath = out + L"Entity_Analysis_" + wid + L".csv";

    if (existing(logPath) || existing(csvPath)) {
        fprintf(stderr, "Run output already exists\n");
        return 2;
    }

    FILE* f = _wfopen(logPath.c_str(), L"wb");
    if (!f) {
        fprintf(stderr, "Cannot create log\n");
        return 2;
    }

    std::wstring scratch = asciiScratchRoot();
    if (scratch.empty()) {
        fprintf(f, "scratch_ascii_root_unavailable\nfailures=1\n");
        fclose(f);
        LocalFree(wargv);
        return 1;
    }
    if (scratch[scratch.size() - 1] != L'\\') scratch += L'\\';
    scratch += L"CAA_BOM_" + wid;

    if (!CreateDirectoryW(scratch.c_str(), NULL)) {
        fprintf(f, "scratch_create_failed=%lu\nfailures=1\n", (unsigned long)GetLastError());
        fclose(f);
        LocalFree(wargv);
        return 1;
    }

    std::string scratchA(scratch.begin(), scratch.end());
    fprintf(f, "scratch_created=1\n");
    fflush(f);

    HRESULT c = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    char sessionName[] = "CAAEntitySession";
    CATSession* session = NULL;
    HRESULT hr = Create_Session(sessionName, session);
    fprintf(f, "Create_Session=%08lx size=%d density_kg_m3=%.15g\n", hr, size, density);
    fflush(f);

    if (FAILED(hr) || !session) {
        RemoveDirectoryW(scratch.c_str());
        fclose(f);
        if (SUCCEEDED(c)) CoUninitialize();
        LocalFree(wargv);
        return 1;
    }

    CATDocument* doc = NULL;
    CATUnicodeString input;
    input.BuildFromWChar(inputArg.c_str());
    hr = CATDocumentServices::OpenDocument(input, doc, TRUE);
    std::wstring tempInput;

    if (FAILED(hr) && !doc && GetFileAttributesW(inputArg.c_str()) != INVALID_FILE_ATTRIBUTES) {
        tempInput = scratch + L"\\input.CATPart";
        if (CopyFileW(inputArg.c_str(), tempInput.c_str(), TRUE)) {
            CATUnicodeString safeInput;
            safeInput.BuildFromWChar(tempInput.c_str());
            hr = CATDocumentServices::OpenDocument(safeInput, doc, TRUE);
            fprintf(f, "fallback_copy_open=%08lx; external references may differ\n", hr);
        } else {
            fprintf(f, "fallback_copy_failed=%lu\n", (unsigned long)GetLastError());
            tempInput.clear();
        }
    }

    fprintf(f, "OpenDocument=%08lx doc=%p (read-only)\n", hr, doc);
    fflush(f);

    int failures = 0, warnings = 0;

    if (SUCCEEDED(hr) && doc) {
        CATInit* init = NULL;
        hr = doc->QueryInterface(IID_CATInit, (void**)&init);
        CATIPrtContainer* cont = init ? (CATIPrtContainer*)init->GetRootContainer("CATIPrtContainer") : NULL;

        if (cont) {
            CATIPrtPart_var part(cont->GetPart());
            CATIPartRequest* request = NULL;
            hr = part->QueryInterface(IID_CATIPartRequest, (void**)&request);

            if (SUCCEEDED(hr) && request) {
                CATLISTV(CATBaseUnknown_var) bodies;
                hr = request->GetSolidBodies(CATUnicodeString("MfDefault3DView"), bodies);
                fprintf(f, "GetSolidBodies=%08lx count=%d\n", hr, bodies.Size());
                fflush(f);

                if (FAILED(hr) || bodies.Size() == 0) {
                    fprintf(f, "no root bodies or retrieval failed\n");
                    failures++;
                }

                // --- STRICT HIDDEN ENTITY (NoShow) SCAN ---
                int hiddenCount = 0;
                std::string joinedNames;

                for (int i = 1; SUCCEEDED(hr) && i <= bodies.Size(); i++) {
                    bool hidden = isObjectHidden((CATBaseUnknown*)bodies[i]);
                    CATIBodyRequest* bReq = NULL;
                    if (!hidden && SUCCEEDED(bodies[i]->QueryInterface(IID_CATIBodyRequest, (void**)&bReq)) && bReq) {
                        CATLISTV(CATBaseUnknown_var) res;
                        if (SUCCEEDED(bReq->GetResults(CATUnicodeString("MfDefault3DView"), res))) {
                            for (int j = 1; j <= res.Size(); ++j) {
                                if (isObjectHidden((CATBaseUnknown*)res[j])) {
                                    hidden = true;
                                    break;
                                }
                            }
                        }
                        bReq->Release();
                    }

                    if (hidden) {
                        std::string bname;
                        CATIAlias* alias = NULL;
                        if (SUCCEEDED(bodies[i]->QueryInterface(IID_CATIAlias, (void**)&alias)) && alias) {
                            bname = wideUtf8(catWide(alias->GetAlias()));
                            alias->Release();
                        }
                        if (bname.empty()) {
                            char bbuf[32];
                            sprintf_s(bbuf, "Body_%d", i);
                            bname = bbuf;
                        }
                        fprintf(f, "hidden_body index=%d name=%s\n", i, bname.c_str());
                        hiddenCount++;
                        if (!joinedNames.empty()) joinedNames += ", ";
                        joinedNames += bname;
                    }
                }

                if (hiddenCount > 0) {
                    fprintf(f, "hidden_body_count=%d names=%s\nfailures=1\n", hiddenCount, joinedNames.c_str());
                    fflush(f);

                    request->Release();
                    cont->Release();
                    if (init) init->Release();
                    CATDocumentServices::Remove(*doc);
                    Delete_Session(sessionName);

                    if (!tempInput.empty()) {
                        if (DeleteFileW(tempInput.c_str())) fprintf(f, "temporary_copy_removed=1\n");
                        else fprintf(f, "temporary_copy_removed=0 error=%lu\n", (unsigned long)GetLastError());
                    }
                    if (RemoveDirectoryW(scratch.c_str())) fprintf(f, "scratch_removed=1\n");
                    else fprintf(f, "scratch_removed=0 error=%lu\n", (unsigned long)GetLastError());

                    fclose(f);
                    if (SUCCEEDED(c)) CoUninitialize();
                    LocalFree(wargv);
                    return 3; // Exit code 3: Model contains hidden (NoShow) entities
                }

                // --- GEOMETRIC MEASUREMENT & GROUPING ---
                std::vector<Instance> instances;
                std::vector<Group> groups;
                int filtered = 0;

                for (int i = 1; SUCCEEDED(hr) && i <= bodies.Size(); i++) {
                    Instance item;
                    item.obj = CATISpecObject_var(bodies[i]);
                    if (NULL_var == item.obj) {
                        fprintf(f, "body %d no spec\n", i);
                        failures++;
                        continue;
                    }

                    CATIAlias_var alias(item.obj);
                    CATUnicodeString aliasString = (NULL_var != alias) ? alias->GetAlias() : CATUnicodeString("");
                    item.displayWide = catWide(aliasString);
                    item.display = wideUtf8(item.displayWide);
                    item.feature = wideUtf8(catWide(item.obj->GetName()));

                    fprintf(f, "measure_begin=%d/%d\n", i, bodies.Size());
                    fflush(f);

                    item.m = measureBody(item.obj, f);
                    int pos = (int)instances.size();

                    if (shouldFilterTiny(item.m.volumeOk, item.m.volume)) {
                        item.tiny = true;
                        filtered++;
                        warnings++;
                        fprintf(f, "tiny_volume body=%d volume_mm3=%.17g\n", i, item.m.volume);
                    } else if (item.m.ok) {
                        for (size_t j = 0; j < groups.size(); j++) {
                            const Measurement& ref = instances[groups[j].rep].m;
                            if (ref.ok &&
                                nearVal(item.m.volume, ref.volume, 1e-6) &&
                                nearVal(item.m.area, ref.area, 1e-4)) {
                                item.group = (int)j;
                                break;
                            }
                        }
                        if (item.group < 0) {
                            Group g;
                            g.rep = pos;
                            groups.push_back(g);
                            item.group = (int)groups.size() - 1;
                        }
                        Group& g = groups[item.group];
                        g.members.push_back(pos);
                        g.volumeSum += item.m.volume;
                        g.momentX += item.m.volume * item.m.x;
                        g.momentY += item.m.volume * item.m.y;
                        g.momentZ += item.m.volume * item.m.z;
                    } else {
                        failures++;
                    }

                    instances.push_back(item);
                    fprintf(f, "measure_done=%d/%d group=%d status=%s\n",
                            i, bodies.Size(), item.group + 1,
                            item.tiny ? "tiny_volume" : (item.m.ok ? "ok" : "failed"));
                    fflush(f);
                }

                int groupedCount = 0;
                for (size_t z = 0; z < groups.size(); z++) {
                    groupedCount += (int)groups[z].members.size();
                }
                fprintf(f, "filter_summary total=%d filtered=%d grouped=%d ungrouped=%d\n",
                        bodies.Size(), filtered, groupedCount, bodies.Size() - filtered - groupedCount);
                fprintf(f, "group_count=%d\n", (int)groups.size());
                fflush(f);

                // --- RENDERING PREVIEW IMAGES FOR UNIQUE GROUPS ---
                for (size_t k = 0; k < groups.size(); k++) {
                    Group& g = groups[k];
                    Instance& item = instances[g.rep];
                    std::wstring wfilename = uniqueName(out, item.displayWide);
                    g.image = wideUtf8(wfilename);
                    std::wstring target = out + wfilename;

                    char tempName[160];
                    sprintf_s(tempName, "group_%02d_%lu_%s.png", (int)k + 1, item.m.tag, runid);
                    std::string temporary = scratchA + "\\" + tempName;

                    if (existing(target) || GetFileAttributesA(temporary.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        fprintf(f, "output collision group=%d\n", (int)k + 1);
                    } else {
                        fprintf(f, "render_group_begin=%d/%d\n", (int)k + 1, (int)groups.size());
                        fflush(f);
                        g.imageOk = testRep(item.obj, f, temporary, target, size) != 0;
                    }
                    if (!g.imageOk) failures++;
                    fprintf(f, "render_group_done=%d/%d status=%s\n",
                            (int)k + 1, (int)groups.size(), g.imageOk ? "ok" : "failed");
                    fflush(f);
                }

                // --- OUTPUT GROUP CSV ---
                FILE* csv = _wfopen(csvPath.c_str(), L"wb");
                if (!csv) {
                    fprintf(f, "group CSV open failed\n");
                    failures++;
                } else {
                    fwrite("\xEF\xBB\xBF", 1, 3, csv); // UTF-8 BOM
                    fprintf(csv, "index,display_name,quantity,area_mm2,volume_mm3,total_volume_mm3,cx_mm,cy_mm,cz_mm,image,status,volume_moment_x_mm4,volume_moment_y_mm4,volume_moment_z_mm4,member_indices\r\n");
                    for (size_t k = 0; k < groups.size(); k++) {
                        Group& g = groups[k];
                        Instance& item = instances[g.rep];
                        fprintf(csv, "%d,", (int)k + 1);
                        csvField(csv, item.display);
                        fprintf(csv, ",%d,", (int)g.members.size());
                        if (item.m.ok) fprintf(csv, "%.15g", item.m.area);
                        fprintf(csv, ",%.15g,%.15g,%.15g,%.15g,%.15g,",
                                item.m.volume, g.volumeSum,
                                g.momentX / g.volumeSum, g.momentY / g.volumeSum, g.momentZ / g.volumeSum);
                        csvField(csv, g.image);
                        fprintf(csv, ",%s,%.15g,%.15g,%.15g,\"",
                                g.imageOk ? "ok" : "image_failed",
                                g.momentX, g.momentY, g.momentZ);
                        for (size_t j = 0; j < g.members.size(); j++) {
                            if (j) fputc(';', csv);
                            fprintf(csv, "%d", g.members[j] + 1);
                        }
                        fprintf(csv, "\"\r\n");
                    }
                    fclose(csv);
                }

                // --- OUTPUT INSTANCES CSV ---
                std::wstring instancePath = out + L"Instances_" + wid + L".csv";
                if (existing(instancePath)) {
                    fprintf(f, "instance CSV exists\n");
                    failures++;
                } else {
                    FILE* ins = _wfopen(instancePath.c_str(), L"wb");
                    if (!ins) {
                        fprintf(f, "instance CSV open failed\n");
                        failures++;
                    } else {
                        fwrite("\xEF\xBB\xBF", 1, 3, ins); // UTF-8 BOM
                        fprintf(ins, "index,group_index,display_name,feature_name,tag,area_mm2,volume_mm3,cx_mm,cy_mm,cz_mm,mass_g,image,status,area_raw_signed_mm2,negative_face_count,negative_area_abs_mm2,area_condition\r\n");
                        for (size_t i = 0; i < instances.size(); i++) {
                            Instance& item = instances[i];
                            fprintf(ins, "%d,%d,", (int)i + 1, item.group + 1);
                            csvField(ins, item.display);
                            fputc(',', ins);
                            csvField(ins, item.feature);
                            fprintf(ins, ",%lu,", item.m.tag);
                            if (item.m.ok) fprintf(ins, "%.15g,", item.m.area);
                            else fprintf(ins, ",");

                            if (item.m.volumeOk) {
                                fprintf(ins, "%.15g,%.15g,%.15g,%.15g,%.15g,",
                                        item.m.volume, item.m.x, item.m.y, item.m.z,
                                        item.m.volume * density * 1e-6);
                            } else {
                                fprintf(ins, ",,,,,");
                            }

                            csvField(ins, item.group >= 0 ? groups[item.group].image : "");
                            fprintf(ins, ",%s,",
                                    item.tiny ? "tiny_volume" :
                                    (item.m.ok ? (item.group >= 0 && groups[item.group].imageOk ? "ok" : "image_failed") :
                                     "measurement_failed"));
                            fprintf(ins, ",,,ok\r\n");
                        }
                        fclose(ins);
                    }
                }
                request->Release();
            } else {
                fprintf(f, "CATIPartRequest failed=%08lx\n", hr);
                failures++;
            }
            cont->Release();
        } else {
            fprintf(f, "CATIPrtContainer unavailable\n");
            failures++;
        }
        if (init) init->Release();
        CATDocumentServices::Remove(*doc);
    } else {
        failures++;
    }

    Delete_Session(sessionName);

    if (!tempInput.empty()) {
        if (DeleteFileW(tempInput.c_str())) fprintf(f, "temporary_copy_removed=1\n");
        else {
            fprintf(f, "temporary_copy_removed=0 error=%lu\n", (unsigned long)GetLastError());
            failures++;
        }
    }

    if (RemoveDirectoryW(scratch.c_str())) fprintf(f, "scratch_removed=1\n");
    else {
        fprintf(f, "scratch_removed=0 error=%lu\n", (unsigned long)GetLastError());
        failures++;
    }

    fprintf(f, "warnings=%d\n", warnings);
    fprintf(f, "failures=%d\n", failures);
    fclose(f);

    if (SUCCEEDED(c)) CoUninitialize();
    LocalFree(wargv);

    return failures ? 1 : 0;
}
