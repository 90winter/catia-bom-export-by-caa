#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <process.h>
#include <string>
#include <stdio.h>
#include <float.h>
#include "pipeline.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define ID_INPUT   101
#define ID_FILE    102
#define ID_OUTPUT  103
#define ID_FOLDER  104
#define ID_SIZE    105
#define ID_RUN     106
#define ID_STATUS  107
#define ID_TITLE   108
#define ID_SUB     109
#define ID_INFO    110

#define IDI_APPICON 200

#define WM_PROGRESS (WM_APP + 1)

static HWND wnd, editInput, editOutput, combo, runButton, statusText;
static HFONT bodyFont, titleFont, smallFont, boldFont;
static HBRUSH bgBrush, whiteBrush;
static bool busy = false;
static int progress = -1;
static HANDLE cancelEvent = NULL;
static HANDLE workerThread = NULL;

static std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

static std::wstring defaultOutput(const std::wstring &in) {
    size_t slash = in.find_last_of(L"\\/");
    size_t dot = in.find_last_of(L'.');
    if (slash == std::wstring::npos || dot == std::wstring::npos ||
        dot <= slash + 1 || _wcsicmp(in.c_str() + dot, L".CATPart") != 0) {
        return L"";
    }
    return in.substr(0, dot) + L"_BOM";
}

static HWND add(const wchar_t* cls, const wchar_t* value, DWORD style,
                int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, value,
                            WS_CHILD | WS_VISIBLE | ((wcscmp(cls, L"STATIC") == 0) ? 0 : WS_TABSTOP) | style,
                            x, y, w, h, wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void status(const wchar_t *v) {
    SetWindowTextW(statusText, v);
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        wnd = h;
        bgBrush = CreateSolidBrush(RGB(246, 248, 251));
        whiteBrush = CreateSolidBrush(RGB(255, 255, 255));

        titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        bodyFont  = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        boldFont  = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

        add(L"STATIC", L"CATPart 实体 BOM 生成工具", 0, 34, 22, 600, 32, ID_TITLE, titleFont);
        add(L"STATIC", L"自动归并相同几何规格实体（同体积/同表面积），智能核算质量与总成质心", 0, 36, 60, 680, 22, ID_SUB, smallFont);

        add(L"STATIC", L"CATPart 模型文件", 0, 36, 108, 220, 22, 0, boldFont);
        editInput = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 34, 136, 570, 32, ID_INPUT, bodyFont);
        add(L"BUTTON", L"浏览文件...", BS_OWNERDRAW, 616, 136, 105, 32, ID_FILE, bodyFont);

        add(L"STATIC", L"Excel 导出目录", 0, 36, 188, 220, 22, 0, boldFont);
        editOutput = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 34, 216, 570, 32, ID_OUTPUT, bodyFont);
        add(L"BUTTON", L"选择目录...", BS_OWNERDRAW, 616, 216, 105, 32, ID_FOLDER, bodyFont);

        add(L"STATIC", L"实体预览图分辨率", 0, 36, 268, 220, 22, 0, boldFont);
        combo = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 34, 296, 230, 160, ID_SIZE, bodyFont);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"快速  ·  600 × 600");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"标准  ·  800 × 800");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"高清  ·  1200 × 1200");
        SendMessageW(combo, CB_SETCURSEL, 1, 0); // Default to 800x800

        add(L"STATIC", L"预览图将自动嵌入 Excel，材料牌号支持下拉切换并实时动态重算。", 0, 280, 301, 440, 22, ID_INFO, smallFont);

        runButton = add(L"BUTTON", L"开始生成实体 BOM", BS_OWNERDRAW | BS_DEFPUSHBUTTON, 34, 360, 168, 42, ID_RUN, boldFont);
        statusText = add(L"STATIC", L"就绪。请选择 CATPart 模型文件，点击“开始生成实体 BOM”开始执行。", 0, 36, 468, 680, 26, ID_STATUS, smallFont);

        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkMode(dc, TRANSPARENT);
        int id = GetDlgCtrlID((HWND)l);
        SetTextColor(dc, (id == ID_TITLE) ? RGB(22, 40, 56) :
                         (id == ID_SUB || id == ID_INFO || id == ID_STATUS) ? RGB(91, 108, 123) :
                         RGB(40, 56, 70));
        return (INT_PTR)bgBrush;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(24, 41, 57));
        return (INT_PTR)whiteBrush;
    }

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, bgBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rail = { 34, 432, 721, 441 };
        HBRUSH g = CreateSolidBrush(RGB(221, 229, 236));
        FillRect(dc, &rail, g);
        DeleteObject(g);

        if (progress >= 0) {
            RECT fill = rail;
            fill.right = rail.left + (rail.right - rail.left) * progress / 100;
            HBRUSH b = CreateSolidBrush(RGB(40, 113, 168));
            FillRect(dc, &fill, b);
            DeleteObject(b);
        }
        EndPaint(h, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)l;
        if (d->CtlType != ODT_BUTTON) break;
        bool primary = (d->CtlID == ID_RUN);
        bool disabled = (d->itemState & ODS_DISABLED) != 0;

        COLORREF fill = disabled ? RGB(187, 198, 208) :
                        primary ? (busy ? RGB(180, 60, 60) : RGB(35, 103, 156)) : RGB(255, 255, 255);
        COLORREF edge = primary ? fill : RGB(192, 204, 214);

        HBRUSH b = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldb = SelectObject(d->hDC, b);
        HGDIOBJ oldp = SelectObject(d->hDC, pen);

        RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom, 8, 8);

        SelectObject(d->hDC, oldb);
        SelectObject(d->hDC, oldp);
        DeleteObject(b);
        DeleteObject(pen);

        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, primary ? RGB(255, 255, 255) : RGB(35, 74, 103));
        SelectObject(d->hDC, primary ? boldFont : bodyFont);

        wchar_t text[80];
        GetWindowTextW(d->hwndItem, text, 80);
        DrawTextW(d->hDC, text, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(w) == ID_INPUT && HIWORD(w) == EN_CHANGE && editOutput) {
            std::wstring d = defaultOutput(getText(editInput));
            if (!d.empty()) {
                SetWindowTextW(editOutput, d.c_str());
            }
            return 0;
        }

        switch (LOWORD(w)) {
        case ID_FILE: {
            wchar_t path[32768] = L"";
            OPENFILENAMEW of = { sizeof(of) };
            of.hwndOwner = h;
            of.lpstrFilter = L"CATIA 零件文件 (*.CATPart)\0*.CATPart\0所有文件 (*.*)\0*.*\0\0";
            of.lpstrFile = path;
            of.nMaxFile = 32768;
            of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&of)) {
                SetWindowTextW(editInput, path);
            }
            return 0;
        }

        case ID_FOLDER: {
            BROWSEINFOW bi = {};
            bi.hwndOwner = h;
            bi.lpszTitle = L"请选择 Excel 导出目录";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
            if (pid) {
                wchar_t p[MAX_PATH];
                if (SHGetPathFromIDListW(pid, p)) {
                    SetWindowTextW(editOutput, p);
                }
                CoTaskMemFree(pid);
            }
            return 0;
        }

        case ID_RUN: {
            if (busy) {
                // User requested cancellation
                if (cancelEvent) {
                    SetEvent(cancelEvent);
                }
                EnableWindow(runButton, FALSE);
                SetWindowTextW(runButton, L"正在取消...");
                status(L"正在中止后台分析任务并清理资源，请稍候…");
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }

            std::wstring in = getText(editInput);
            std::wstring out = getText(editOutput);

            DWORD a = GetFileAttributesW(in.c_str());
            if (in.empty() || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxW(h, L"请选择有效的 CATPart 文件。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }

            if (out.empty()) {
                MessageBoxW(h, L"请选择 Excel 导出目录。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }

            DWORD b = GetFileAttributesW(out.c_str());
            if (b == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(out.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    MessageBoxW(h, L"无法创建导出目录，请检查路径及写入权限。", L"提示", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                b = GetFileAttributesW(out.c_str());
            }

            if (b == INVALID_FILE_ATTRIBUTES || !(b & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxW(h, L"指定的导出路径不是有效文件夹。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }

            cancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

            BomJob* job = new BomJob;
            job->input = in;
            job->outputDir = out;

            int n = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
            job->size = (n == 0) ? 600 : ((n == 2) ? 1200 : 800);
            job->notify = h;
            job->cancelEvent = cancelEvent;

            busy = true;
            progress = 0;
            SetWindowTextW(runButton, L"取消生成");
            EnableWindow(runButton, TRUE);
            EnableWindow(combo, FALSE);
            EnableWindow(editInput, FALSE);
            EnableWindow(editOutput, FALSE);
            status(L"正在启动后台分析服务并读取 CATPart 模型…");
            InvalidateRect(h, NULL, FALSE);

            uintptr_t th = _beginthreadex(NULL, 0, RunBomJob, job, 0, NULL);
            if (th) {
                workerThread = (HANDLE)th;
            } else {
                delete job;
                if (cancelEvent) {
                    CloseHandle(cancelEvent);
                    cancelEvent = NULL;
                }
                busy = false;
                SetWindowTextW(runButton, L"开始生成实体 BOM");
                EnableWindow(runButton, TRUE);
                EnableWindow(combo, TRUE);
                EnableWindow(editInput, TRUE);
                EnableWindow(editOutput, TRUE);
                status(L"无法启动任务处理线程。");
            }
            return 0;
        }
        }
        break;
    }

    case WM_PROGRESS: {
        if (w == 0) {
            progress = -1;
            status(L"正在启动并读取 CATPart 模型…");
        } else if (w == 1) {
            progress = (int)l * 80 / 100;
            wchar_t t[100];
            swprintf_s(t, L"正在分析测量并智能归并实体，进度 %d%%", (int)l);
            status(t);
        } else if (w == 2) {
            progress = 85;
            status(L"实体分析完成，正在生成 Excel 工作簿与嵌入预览图…");
        } else if (w == 3 || w == 4 || w == 5) {
            std::wstring* result = (std::wstring*)l;

            if (workerThread) {
                WaitForSingleObject(workerThread, 1000);
                CloseHandle(workerThread);
                workerThread = NULL;
            }
            if (cancelEvent) {
                CloseHandle(cancelEvent);
                cancelEvent = NULL;
            }

            busy = false;
            SetWindowTextW(runButton, L"开始生成实体 BOM");
            EnableWindow(runButton, TRUE);
            EnableWindow(combo, TRUE);
            EnableWindow(editInput, TRUE);
            EnableWindow(editOutput, TRUE);

            if (w == 5) {
                progress = 0;
                status(L"任务已取消。");
                delete result;
            } else {
                progress = (w == 3) ? 100 : 0;
                status((w == 3) ? L"Excel BOM 已生成完成。" : L"导出已中止或生成失败，请查看提示信息。");

                MessageBoxW(h, result ? result->c_str() : L"任务结束。",
                            (w == 3) ? L"生成完成" : L"提示",
                            MB_OK | ((w == 3) ? MB_ICONINFORMATION : MB_ICONWARNING));
                delete result;
            }
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_CLOSE: {
        if (busy) {
            if (MessageBoxW(h, L"BOM 正在生成中，是否中止当前任务并退出程序？", L"确认退出", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return 0;
            }
            if (cancelEvent) {
                SetEvent(cancelEvent);
            }
            if (workerThread) {
                WaitForSingleObject(workerThread, 3000);
                CloseHandle(workerThread);
                workerThread = NULL;
            }
            if (cancelEvent) {
                CloseHandle(cancelEvent);
                cancelEvent = NULL;
            }
            busy = false;
            DestroyWindow(h);
            return 0;
        }
        DestroyWindow(h);
        return 0;
    }

    case WM_DESTROY: {
        DeleteObject(titleFont);
        DeleteObject(bodyFont);
        DeleteObject(boldFont);
        DeleteObject(smallFont);
        DeleteObject(bgBrush);
        DeleteObject(whiteBrush);
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK CliProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PROGRESS) {
        if (w == 3 || w == 4 || w == 5) {
            std::wstring* p = (std::wstring*)l;
            delete p;
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && (_wcsicmp(argv[1], L"/c") == 0 || _wcsicmp(argv[1], L"-c") == 0)) {
        if (argc < 4 || argc > 6) {
            LocalFree(argv);
            CoUninitialize();
            return 2;
        }
        int sz = 800;
        if (argc >= 5) {
            sz = _wtoi(argv[4]);
            if (sz != 600 && sz != 800 && sz != 1200) {
                LocalFree(argv);
                CoUninitialize();
                return 2;
            }
        }
        double density = 0.0;
        if (argc == 6) {
            wchar_t* end = NULL;
            density = wcstod(argv[5], &end);
            if (end == argv[5] || *end != 0 || !_finite(density) || density <= 0.0) {
                LocalFree(argv);
                CoUninitialize();
                return 2;
            }
        }
        std::wstring inPath = argv[2];
        std::wstring outDir = argv[3];

        DWORD a = GetFileAttributesW(inPath.c_str());
        if (inPath.empty() || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
            LocalFree(argv);
            CoUninitialize();
            return 1;
        }

        DWORD b = GetFileAttributesW(outDir.c_str());
        if (b == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(outDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                LocalFree(argv);
                CoUninitialize();
                return 1;
            }
            b = GetFileAttributesW(outDir.c_str());
        }
        if (b == INVALID_FILE_ATTRIBUTES || !(b & FILE_ATTRIBUTE_DIRECTORY)) {
            LocalFree(argv);
            CoUninitialize();
            return 1;
        }

        WNDCLASSW wcCli = {};
        wcCli.lpfnWndProc = CliProc;
        wcCli.hInstance = inst;
        wcCli.lpszClassName = L"CAABomCliHiddenWindow";
        RegisterClassW(&wcCli);

        HWND hCli = CreateWindowW(wcCli.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
        int cliExitCode = 1;
        BomJob* job = new BomJob;
        job->input = inPath;
        job->outputDir = outDir;
        job->size = sz;
        job->density = density;
        job->notify = hCli;
        job->outExitCode = &cliExitCode;

        uintptr_t th = _beginthreadex(NULL, 0, RunBomJob, job, 0, NULL);
        if (!th) {
            delete job;
            LocalFree(argv);
            CoUninitialize();
            return 1;
        }

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        WaitForSingleObject((HANDLE)th, INFINITE);
        CloseHandle((HANDLE)th);

        CoUninitialize();
        LocalFree(argv);
        return cliExitCode;
    }
    if (argv) {
        LocalFree(argv);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CAABomModernWindow";
    RegisterClassExW(&wc);

    HWND h = CreateWindowW(wc.lpszClassName, L"CATPart 实体 BOM 生成工具",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 770, 550, NULL, NULL, inst, NULL);

    if (wc.hIcon)   SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    if (wc.hIconSm) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    ShowWindow(h, show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
