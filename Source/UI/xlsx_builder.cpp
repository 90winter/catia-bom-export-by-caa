#define NOMINMAX
#define UNICODE
#define _UNICODE

#include "xlsx_builder.h"

#include <windows.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// ===========================================================================
//  纯原生 OpenXML .xlsx 生成器
//  Pure native OpenXML .xlsx writer (MSVC 2012 / Windows 7 compatible).
//  不依赖 Node.js、不依赖 MSXML、不依赖任何第三方库。
// ===========================================================================

namespace {

typedef std::vector<std::string> Row;
typedef std::vector<Row> Table;

// ---------------------------------------------------------------------------
// 文本 / 文件 基础工具
// ---------------------------------------------------------------------------

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static bool readAll(const std::wstring& path, std::string& out) {
    out.clear();
    FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) != 0) {
        out.append(buf, n);
    }
    bool ok = (ferror(f) == 0);
    fclose(f);
    return ok;
}

static bool writeAll(const std::wstring& path, const std::string& data) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    size_t put = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    bool ok = (put == data.size());
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring dirOf(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    if (k == std::wstring::npos) return L".";
    if (k == 2 && p.size() > 1 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, k);
}

static std::wstring baseOf(const std::wstring& p) {
    size_t k = p.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? p : p.substr(k + 1);
}

// XML 文本转义（输入输出均为 UTF-8）
static std::string esc(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '&':  r += "&amp;";  break;
            case '<':  r += "&lt;";   break;
            case '>':  r += "&gt;";   break;
            case '"':  r += "&quot;"; break;
            case '\'': r += "&apos;"; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                    // 丢弃 XML 1.0 不允许的控制字符
                } else {
                    r += (char)c;
                }
                break;
        }
    }
    return r;
}

// 数值格式化：固定小数位后去掉多余的 0（不依赖 locale 的小数点）
static std::string fmt(double v, int decimals) {
    if (!_finite(v)) return "0";
    if (v == 0.0) return "0";
    if (decimals < 0) decimals = 0;
    if (decimals > 15) decimals = 15;
    char buf[192];
    sprintf_s(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s(buf);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == ',') s[i] = '.';   // 某些区域设置小数点为逗号
    }
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s[s.size() - 1] == '0') s.erase(s.size() - 1);
        if (!s.empty() && s[s.size() - 1] == '.') s.erase(s.size() - 1);
    }
    if (s.empty() || s == "-" || s == "-0") s = "0";
    return s;
}

static std::string fmtInt(long long v) {
    char buf[48];
    sprintf_s(buf, sizeof(buf), "%lld", v);
    return std::string(buf);
}

// 视觉宽度（中文按 2 个字符宽计）
static int visualWidth(const std::string& u8) {
    int w = 0;
    for (size_t i = 0; i < u8.size(); ) {
        unsigned char c = (unsigned char)u8[i];
        int len = 1;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + (size_t)len > u8.size()) len = 1;
        w += (len == 1) ? 1 : 2;
        i += (size_t)len;
    }
    return w;
}

// ---------------------------------------------------------------------------
// CSV 解析（与 CAA worker 输出的 RFC4180 变体保持一致）
// ---------------------------------------------------------------------------

static Table parseCsv(const std::string& data) {
    Table rows;
    Row row;
    std::string cell;
    bool quoted = false;
    size_t start = (data.size() >= 3 &&
                    (unsigned char)data[0] == 0xEF &&
                    (unsigned char)data[1] == 0xBB &&
                    (unsigned char)data[2] == 0xBF) ? 3 : 0;

    for (size_t i = start; i < data.size(); i++) {
        char c = data[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < data.size() && data[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            row.push_back(cell);
            cell.clear();
        } else if (c == '\n') {
            if (!cell.empty() && cell[cell.size() - 1] == '\r') cell.erase(cell.size() - 1);
            row.push_back(cell);
            cell.clear();
            rows.push_back(row);
            row.clear();
        } else {
            cell += c;
        }
    }
    if (!cell.empty() || !row.empty()) {
        if (!cell.empty() && cell[cell.size() - 1] == '\r') cell.erase(cell.size() - 1);
        row.push_back(cell);
        rows.push_back(row);
    }
    return rows;
}

static int columnOf(const Row& head, const char* name) {
    for (size_t i = 0; i < head.size(); i++) {
        if (head[i] == name) return (int)i;
    }
    return -1;
}

static std::string cellOf(const Row& r, int idx) {
    if (idx < 0 || (size_t)idx >= r.size()) return std::string();
    return r[idx];
}

static bool toNumber(const std::string& s, double& out) {
    std::string t;
    t.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != ' ' && s[i] != '\t') t += s[i];
    }
    if (t.empty()) return false;
    char* end = NULL;
    double v = strtod(t.c_str(), &end);
    if (end == t.c_str() || (end && *end != 0)) return false;
    if (!_finite(v)) return false;
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// 取消 / 超时 检查（原生写入器全程可中断）
// ---------------------------------------------------------------------------

// 返回 true 表示应当立即中止生成；errMsg 已被填写。
static bool checkAbort(HANDLE cancelEvent, DWORD deadlineTick, bool hasDeadline,
                       std::wstring& errMsg, bool* outCanceled, bool* outTimedOut) {
    if (cancelEvent != NULL && cancelEvent != INVALID_HANDLE_VALUE &&
        WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
        if (outCanceled) *outCanceled = true;
        errMsg = L"任务已由用户取消。";
        return true;
    }
    if (hasDeadline && deadlineTick != 0 && (LONG)(GetTickCount() - deadlineTick) >= 0) {
        if (outTimedOut) *outTimedOut = true;
        errMsg = L"Excel 工作簿生成超时中止。";
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 状态 / 异常说明（与 CAA worker 的 status 字段保持一致）
// ---------------------------------------------------------------------------

// 分组 CSV 与实例 CSV 中都可能出现的合法（非致命）状态
static bool isKnownStatus(const std::string& s) {
    static const char* const kStatuses[] = {
        "ok",
        "area_warning",
        "render_warning",
        "inertia_warning",
        "area_render_warning",
        "area_inertia_warning",
        "render_inertia_warning",
        "area_render_inertia_warning"
    };
    for (size_t i = 0; i < sizeof(kStatuses) / sizeof(kStatuses[0]); i++) {
        if (s == kStatuses[i]) return true;
    }
    return false;
}

// 把 "area_render_inertia_warning" 之类的状态翻译成中文工程说明
static std::string statusNote(const std::string& status) {
    if (status == "empty_body") {
        return utf8(L"空几何体（无三维实体），未生成体积与质心数据，已跳过");
    }
    std::vector<std::string> parts;
    if (status.find("area") != std::string::npos) {
        parts.push_back(utf8(L"表面积测量异常（已按保守值处理）"));
    }
    if (status.find("render") != std::string::npos) {
        parts.push_back(utf8(L"预览图渲染失败（已使用空白占位图）"));
    }
    if (status.find("inertia") != std::string::npos) {
        parts.push_back(utf8(L"主惯性矩不可用（已保守单列，未参与归并）"));
    }
    if (parts.empty()) {
        parts.push_back(utf8(L"未归类的异常状态"));
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += utf8(L"；");
        out += parts[i];
    }
    return out;
}

// 把 "area_render_inertia_warning" 之类的状态码翻译成简短的中文标签
static std::string statusLabel(const std::string& status) {
    if (status == "empty_body") {
        return utf8(L"空几何体");
    }
    std::string out;
    if (status.find("area") != std::string::npos) {
        out += utf8(L"表面积异常");
    }
    if (status.find("render") != std::string::npos) {
        if (!out.empty()) out += utf8(L" + ");
        out += utf8(L"预览渲染异常");
    }
    if (status.find("inertia") != std::string::npos) {
        if (!out.empty()) out += utf8(L" + ");
        out += utf8(L"惯性矩异常");
    }
    if (out.empty()) {
        out = utf8(L"未归类异常");
    }
    return out;
}

// ---------------------------------------------------------------------------
// CRC32 (IEEE 802.3) + Deflate “stored” 压缩块
// ---------------------------------------------------------------------------

static unsigned int g_crc[256];
static bool g_crcReady = false;

static void crcInit() {
    if (g_crcReady) return;
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc[i] = c;
    }
    g_crcReady = true;
}

static unsigned int crc32(const std::string& d) {
    crcInit();
    unsigned int c = 0xFFFFFFFFu;
    for (size_t i = 0; i < d.size(); i++) {
        c = g_crc[(c ^ (unsigned char)d[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// RFC1951: BFINAL/BTYPE=00 非压缩块，单块最大 65535 字节
static void deflateStore(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size() + (in.size() / 65535 + 1) * 5 + 8);
    size_t pos = 0;
    for (;;) {
        size_t n = in.size() - pos;
        if (n > 65535) n = 65535;
        bool final = (pos + n >= in.size());
        unsigned int nn = (unsigned int)(~n) & 0xFFFFu;
        out.push_back((char)(final ? 1 : 0));
        out.push_back((char)(n & 0xFF));
        out.push_back((char)((n >> 8) & 0xFF));
        out.push_back((char)(nn & 0xFF));
        out.push_back((char)((nn >> 8) & 0xFF));
        if (n) out.append(in, pos, n);
        pos += n;
        if (final) break;
    }
}

static void put16(std::string& s, unsigned int v) {
    s.push_back((char)(v & 0xFF));
    s.push_back((char)((v >> 8) & 0xFF));
}

static void put32(std::string& s, unsigned long v) {
    s.push_back((char)(v & 0xFF));
    s.push_back((char)((v >> 8) & 0xFF));
    s.push_back((char)((v >> 16) & 0xFF));
    s.push_back((char)((v >> 24) & 0xFF));
}

struct ZipEntry {
    std::string name;
    std::string data;
};

static std::string buildZip(const std::vector<ZipEntry>& entries,
                            HANDLE cancelEvent,
                            DWORD deadlineTick,
                            bool hasDeadline,
                            std::wstring& errMsg,
                            bool* outCanceled,
                            bool* outTimedOut) {
    std::string out;
    std::string central;

    SYSTEMTIME st;
    GetLocalTime(&st);
    unsigned int dosTime = (unsigned int)(((st.wHour & 0x1F) << 11) |
                                          ((st.wMinute & 0x3F) << 5) |
                                          (st.wSecond / 2));
    unsigned int dosDate = (unsigned int)((((st.wYear >= 1980 ? st.wYear - 1980 : 0) & 0x7F) << 9) |
                                          ((st.wMonth & 0x0F) << 5) |
                                          (st.wDay & 0x1F));

    for (size_t i = 0; i < entries.size(); i++) {
        if (checkAbort(cancelEvent, deadlineTick, hasDeadline, errMsg, outCanceled, outTimedOut)) {
            out.clear();
            return out;
        }
        const ZipEntry& e = entries[i];
        std::string comp;
        deflateStore(e.data, comp);
        unsigned int crc = crc32(e.data);
        unsigned long offset = (unsigned long)out.size();

        // ---- local file header ----
        put32(out, 0x04034B50ul);
        put16(out, 20);                       // version needed
        put16(out, 0);                        // flags
        put16(out, 8);                        // method = deflate
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, crc);
        put32(out, (unsigned long)comp.size());
        put32(out, (unsigned long)e.data.size());
        put16(out, (unsigned int)e.name.size());
        put16(out, 0);                        // extra length
        out += e.name;
        out += comp;

        // ---- central directory record ----
        put32(central, 0x02014B50ul);
        put16(central, 20);                   // version made by
        put16(central, 20);                   // version needed
        put16(central, 0);                    // flags
        put16(central, 8);                    // method
        put16(central, dosTime);
        put16(central, dosDate);
        put32(central, crc);
        put32(central, (unsigned long)comp.size());
        put32(central, (unsigned long)e.data.size());
        put16(central, (unsigned int)e.name.size());
        put16(central, 0);                    // extra
        put16(central, 0);                    // comment
        put16(central, 0);                    // disk number
        put16(central, 0);                    // internal attributes
        put32(central, 0);                    // external attributes
        put32(central, offset);
        central += e.name;
    }

    unsigned long cdOffset = (unsigned long)out.size();
    unsigned long cdSize = (unsigned long)central.size();
    out += central;

    put32(out, 0x06054B50ul);
    put16(out, 0);
    put16(out, 0);
    put16(out, (unsigned int)entries.size());
    put16(out, (unsigned int)entries.size());
    put32(out, cdSize);
    put32(out, cdOffset);
    put16(out, 0);

    return out;
}

// ---------------------------------------------------------------------------
// 工作簿 XML 片段拼装
// ---------------------------------------------------------------------------

static void cellNum(std::string& x, const char* ref, int style, const std::string& val) {
    char b[160];
    sprintf_s(b, sizeof(b), "<x:c r=\"%s\" s=\"%d\"><x:v>", ref, style);
    x += b;
    x += val;
    x += "</x:v></x:c>";
}

static void cellText(std::string& x, const char* ref, int style, const std::string& u8text) {
    char b[160];
    sprintf_s(b, sizeof(b),
             "<x:c r=\"%s\" s=\"%d\" t=\"inlineStr\"><x:is><x:t xml:space=\"preserve\">",
             ref, style);
    x += b;
    x += esc(u8text);
    x += "</x:t></x:is></x:c>";
}

static void cellEmpty(std::string& x, const char* ref, int style) {
    char b[160];
    sprintf_s(b, sizeof(b), "<x:c r=\"%s\" s=\"%d\" />", ref, style);
    x += b;
}

static void cellFormula(std::string& x, const char* ref, int style,
                        const std::string& formula, const std::string& cached, bool hasCached) {
    char b[160];
    sprintf_s(b, sizeof(b), "<x:c r=\"%s\" s=\"%d\"><x:f>", ref, style);
    x += b;
    x += esc(formula);
    x += "</x:f>";
    if (hasCached) {
        x += "<x:v>";
        x += cached;
        x += "</x:v>";
    }
    x += "</x:c>";
}

static void cellRef(char* buf, size_t n, int col, int row) {
    sprintf_s(buf, n, "%c%d", (char)('A' + col), row);
}

// 从模板 sheet2.xml 中读取「材料牌号 → 密度」对照表
static void parseMaterials(const std::string& xml, std::map<std::string, double>& out) {
    size_t pos = 0;
    for (;;) {
        size_t rowStart = xml.find("<x:row ", pos);
        if (rowStart == std::string::npos) break;
        size_t rowEnd = xml.find("</x:row>", rowStart);
        if (rowEnd == std::string::npos) break;
        std::string row = xml.substr(rowStart, rowEnd - rowStart);
        pos = rowEnd + 8;

        std::string name, number;
        size_t a = row.find("r=\"A");
        if (a != std::string::npos) {
            size_t v = row.find("<x:v>", a);
            size_t ve = (v == std::string::npos) ? std::string::npos : row.find("</x:v>", v);
            if (v != std::string::npos && ve != std::string::npos && ve > v + 5) {
                name = row.substr(v + 5, ve - v - 5);
            }
        }
        size_t b = row.find("r=\"B");
        if (b != std::string::npos) {
            size_t v = row.find("<x:v>", b);
            size_t ve = (v == std::string::npos) ? std::string::npos : row.find("</x:v>", v);
            if (v != std::string::npos && ve != std::string::npos && ve > v + 5) {
                number = row.substr(v + 5, ve - v - 5);
            }
        }
        if (!name.empty() && !number.empty()) {
            double d = 0.0;
            if (toNumber(number, d) && d > 0.0) out[name] = d;
        }
    }
}

struct GroupRow {
    long long index;
    std::string name;
    long long quantity;
    double volume;
    double totalVolume;
    double cx, cy, cz;
    std::string image;      // 图片文件名（UTF-8），可为空
    int mediaIndex;         // 1-based，0 表示无图片
};

} // namespace

// ===========================================================================
//  主入口
// ===========================================================================

bool BuildFilteredBomXlsxNative(const std::wstring& csvPath,
                                const std::wstring& outXlsxPath,
                                const std::wstring& appRoot,
                                std::wstring& errMsg,
                                double customDensity,
                                HANDLE cancelEvent,
                                DWORD deadlineTick,
                                bool hasDeadline,
                                bool* outCanceled,
                                bool* outTimedOut) {
    errMsg.clear();
    if (outCanceled) *outCanceled = false;
    if (outTimedOut) *outTimedOut = false;

    // 进入即检查一次取消 / 超时（"0" 号检查点）
    if (checkAbort(cancelEvent, deadlineTick, hasDeadline, errMsg, outCanceled, outTimedOut)) {
        return false;
    }

    // -----------------------------------------------------------------
    // 1. 读取 group CSV
    // -----------------------------------------------------------------
    std::string csvText;
    if (!readAll(csvPath, csvText)) {
        errMsg = L"无法读取分组测量数据 (Entity_Analysis_*.csv)。";
        return false;
    }
    Table table = parseCsv(csvText);
    if (table.size() < 1) {
        errMsg = L"分组测量数据为空。";
        return false;
    }

    const Row& head = table[0];
    int ixIndex   = columnOf(head, "index");
    int ixName    = columnOf(head, "display_name");
    int ixQty     = columnOf(head, "quantity");
    int ixVolume  = columnOf(head, "volume_mm3");
    int ixTotal   = columnOf(head, "total_volume_mm3");
    int ixCx      = columnOf(head, "cx_mm");
    int ixCy      = columnOf(head, "cy_mm");
    int ixCz      = columnOf(head, "cz_mm");
    int ixImage   = columnOf(head, "image");
    int ixStatus  = columnOf(head, "status");

    if (ixIndex < 0 || ixName < 0 || ixQty < 0 || ixVolume < 0 ||
        ixTotal < 0 || ixCx < 0 || ixCy < 0 || ixCz < 0 || ixImage < 0 || ixStatus < 0) {
        errMsg = L"分组测量数据缺少必要字段。";
        return false;
    }

    std::wstring csvDir = dirOf(csvPath);

    std::vector<GroupRow> groups;
    std::vector<std::wstring> mediaFiles;

    for (size_t r = 1; r < table.size(); r++) {
        if (checkAbort(cancelEvent, deadlineTick, hasDeadline, errMsg, outCanceled, outTimedOut)) {
            return false;
        }
        const Row& row = table[r];
        if (row.size() <= 1) continue;

        std::string status = cellOf(row, ixStatus);
        if (!isKnownStatus(status)) {
            errMsg = L"分组测量数据状态异常 (status=" + wide(status) + L")。";
            return false;
        }

        GroupRow g;
        g.index = 0;
        g.quantity = 0;
        g.volume = 0.0;
        g.totalVolume = 0.0;
        g.cx = 0.0;
        g.cy = 0.0;
        g.cz = 0.0;
        g.mediaIndex = 0;

        double v = 0.0;
        if (!toNumber(cellOf(row, ixQty), v) || v < 1.0) {
            errMsg = L"分组测量数据中的单台用量无效。";
            return false;
        }
        g.quantity = (long long)(v + 0.5);

        double idxVal = 0.0;
        if (toNumber(cellOf(row, ixIndex), idxVal)) g.index = (long long)(idxVal + 0.5);
        else g.index = (long long)(groups.size() + 1);

        if (!toNumber(cellOf(row, ixVolume), g.volume) ||
            !toNumber(cellOf(row, ixTotal), g.totalVolume) ||
            !toNumber(cellOf(row, ixCx), g.cx) ||
            !toNumber(cellOf(row, ixCy), g.cy) ||
            !toNumber(cellOf(row, ixCz), g.cz)) {
            errMsg = L"分组测量数据中的体积/质心数值无效。";
            return false;
        }

        g.name = cellOf(row, ixName);
        g.image = cellOf(row, ixImage);

        if (!g.image.empty()) {
            std::wstring imgPath = csvDir + L"\\" + wide(g.image);
            if (exists(imgPath)) {
                mediaFiles.push_back(imgPath);
                g.mediaIndex = (int)mediaFiles.size();
            }
        }
        groups.push_back(g);
    }

    size_t n = groups.size();

    // -----------------------------------------------------------------
    // 2. 读取静态 OpenXML 模板
    // -----------------------------------------------------------------
    std::wstring tplDir = appRoot + L"\\Runtime\\Export\\templates";
    std::string stylesXml, themeXml, materialsXml;
    if (!readAll(tplDir + L"\\styles.xml", stylesXml) || stylesXml.empty()) {
        errMsg = L"缺少 Excel 样式模板 (Runtime\\Export\\templates\\styles.xml)。";
        return false;
    }
    if (!readAll(tplDir + L"\\theme1.xml", themeXml) || themeXml.empty()) {
        errMsg = L"缺少 Excel 主题模板 (Runtime\\Export\\templates\\theme1.xml)。";
        return false;
    }
    if (!readAll(tplDir + L"\\sheet2.xml", materialsXml) || materialsXml.empty()) {
        errMsg = L"缺少材料密度模板 (Runtime\\Export\\templates\\sheet2.xml)。";
        return false;
    }

    std::map<std::string, double> densityTable;
    parseMaterials(materialsXml, densityTable);

    // ---------------------------------------------------------------
    // 2b. 自定义密度传播：命令行 / 外部指定的密度作为第 21 行材料写入
    // ---------------------------------------------------------------
    int matLastRow = 20;                                  // <x:sheetData> 有效材料末行
    std::string activeMaterial = "PP-TD20";               // 列 F 使用的材料牌号
    double defaultDensity = 1010.0;
    {
        std::map<std::string, double>::const_iterator it = densityTable.find(activeMaterial);
        if (it != densityTable.end()) defaultDensity = it->second;
    }
    double activeDensity = defaultDensity;

    const bool useCustomDensity = (customDensity > 0.0 && _finite(customDensity) != 0);
    if (useCustomDensity) {
        matLastRow = 21;
        activeMaterial = utf8(L"自定义密度");
        activeDensity = customDensity;

        std::string customRow = "<x:row r=\"21\">";
        customRow += "<x:c r=\"A21\" s=\"10\" t=\"str\"><x:v>";
        customRow += esc(activeMaterial);
        customRow += "</x:v></x:c>";
        customRow += "<x:c r=\"B21\" s=\"11\" t=\"n\"><x:v>";
        customRow += fmt(customDensity, 6);
        customRow += "</x:v></x:c>";
        customRow += "<x:c r=\"C21\" s=\"12\" t=\"str\"><x:v>";
        customRow += esc(utf8(L"自定义材料"));
        customRow += "</x:v></x:c>";
        customRow += "<x:c r=\"D21\" s=\"12\" t=\"str\"><x:v>";
        customRow += esc(utf8(L"命令行/外部指定密度"));
        customRow += "</x:v></x:c>";
        customRow += "</x:row>";

        // 保持 sheetData 内行号升序：模板在第 21 行留空、备注位于第 22 行，
        // 因此优先插入到 r="22" 之前，找不到时退化为 </x:sheetData> 之前。
        size_t sheetDataEnd = materialsXml.find("</x:sheetData>");
        size_t noteRowPos = materialsXml.find("<x:row r=\"22\"");
        if (noteRowPos != std::string::npos &&
            (sheetDataEnd == std::string::npos || noteRowPos < sheetDataEnd)) {
            materialsXml.insert(noteRowPos, customRow);
        } else if (sheetDataEnd != std::string::npos) {
            materialsXml.insert(sheetDataEnd, customRow);
        } else {
            errMsg = L"材料密度模板结构异常，无法写入自定义密度行。";
            return false;
        }
    }

    // -----------------------------------------------------------------
    // 3. 组装 sheet1 (实体BOM)
    // -----------------------------------------------------------------
    const std::string u8Title =
        utf8(L"零部件三维实体重量与质心 BOM 明细表");
    const std::string u8Note =
        utf8(L"说明：1. 相同几何规格（同体积、同表面积）的标准件与对称件已自动归并汇总；"
             L"2. 质心坐标基于 CATPart 零件设计坐标系（绝对坐标系）；"
             L"3. 可在「材料牌号」列下拉切换材质，质量与总成质心自动重算。");
    const wchar_t* headerWide[15] = {
        L"序号", L"零件 / 实体名称", L"单台用量", L"单件体积 (mm³)", L"单台份体积 (mm³)",
        L"材料牌号", L"材料密度 (kg/m³)", L"单台克重", L"组质心 X (mm)", L"组质心 Y (mm)",
        L"组质心 Z (mm)", L"质量矩 X (g·mm)", L"质量矩 Y (g·mm)", L"质量矩 Z (g·mm)",
        L"实体预览图"
    };
    int firstRow = 6;
    int lastRow = (n > 0) ? (int)n + 5 : 5;
    int totalRow = lastRow + 1;
    int centerRow = lastRow + 2;

    std::string s1;
    s1 += "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    s1 += "<x:worksheet xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"";
    s1 += " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    s1 += "<x:sheetViews><x:sheetView showGridLines=\"0\" workbookViewId=\"0\">";
    s1 += "<x:pane ySplit=\"5\" topLeftCell=\"A6\" activePane=\"bottomLeft\" state=\"frozen\" />";
    s1 += "</x:sheetView></x:sheetViews>";
    s1 += "<x:sheetFormatPr defaultRowHeight=\"15\" />";

    int widthB = 30;
    for (size_t i = 0; i < groups.size(); i++) {
        int w = visualWidth(groups[i].name) + 4;
        if (w > widthB) widthB = w;
    }
    if (widthB > 55) widthB = 55;

    const int colW[15] = { 9, widthB, 13, 19, 22, 22, 22, 18, 18, 18, 18, 21, 21, 21, 18 };
    const int colHidden[15] = { 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0 };

    s1 += "<x:cols>";
    for (int c = 0; c < 15; c++) {
        char b[160];
        sprintf_s(b, sizeof(b),
                 "<x:col min=\"%d\" max=\"%d\" width=\"%d\" hidden=\"%d\" customWidth=\"1\" />",
                 c + 1, c + 1, colW[c], colHidden[c]);
        s1 += b;
    }
    s1 += "</x:cols>";

    s1 += "<x:sheetData>";

    // 标题行 (row 2)
    {
        char ref[16];
        s1 += "<x:row r=\"2\">";
        cellRef(ref, sizeof(ref), 0, 2);
        cellText(s1, "A2", 14, u8Title);
        for (int c = 1; c < 15; c++) {
            cellRef(ref, sizeof(ref), c, 2);
            cellEmpty(s1, ref, 14);
        }
        s1 += "</x:row>";
    }
    // 说明行 (row 3)
    {
        char ref[16];
        s1 += "<x:row r=\"3\">";
        cellText(s1, "A3", 15, u8Note);
        for (int c = 1; c < 15; c++) {
            cellRef(ref, sizeof(ref), c, 3);
            cellEmpty(s1, ref, 15);
        }
        s1 += "</x:row>";
    }
    // 表头行 (row 5)
    {
        char ref[16];
        s1 += "<x:row r=\"5\" ht=\"35\" customHeight=\"1\">";
        for (int c = 0; c < 15; c++) {
            cellRef(ref, sizeof(ref), c, 5);
            cellText(s1, ref, 9, utf8(headerWide[c]));
        }
        s1 += "</x:row>";
    }

    // 数据行
    double sumQuantity = 0.0, sumTotalVolume = 0.0, sumWeight = 0.0;
    double sumMx = 0.0, sumMy = 0.0, sumMz = 0.0;

    for (size_t i = 0; i < n; i++) {
        // 逐行检查取消 / 超时，保证大模型下也能立即响应
        if (checkAbort(cancelEvent, deadlineTick, hasDeadline, errMsg, outCanceled, outTimedOut)) {
            return false;
        }
        const GroupRow& g = groups[i];
        int r = firstRow + (int)i;
        char rowOpen[64];
        sprintf_s(rowOpen, sizeof(rowOpen), "<x:row r=\"%d\" ht=\"87\" customHeight=\"1\">", r);
        s1 += rowOpen;

        char ref[16];

        cellRef(ref, sizeof(ref), 0, r);
        cellNum(s1, ref, 12, fmtInt(g.index));

        cellRef(ref, sizeof(ref), 1, r);
        cellText(s1, ref, 12, g.name);

        cellRef(ref, sizeof(ref), 2, r);
        cellNum(s1, ref, 27, fmtInt(g.quantity));

        cellRef(ref, sizeof(ref), 3, r);
        cellNum(s1, ref, 28, fmt(g.volume, 4));

        cellRef(ref, sizeof(ref), 4, r);
        cellNum(s1, ref, 28, fmt(g.totalVolume, 4));

        cellRef(ref, sizeof(ref), 5, r);
        cellText(s1, ref, 10, activeMaterial);

        // 公式内含中文，需先转成 UTF-8
        std::string vlookup = utf8(L"VLOOKUP(F") + fmtInt(r) +
                              utf8(L",'材料密度'!$A$2:$B$") + fmtInt(matLastRow) +
                              utf8(L",2,FALSE)");
        cellRef(ref, sizeof(ref), 6, r);
        cellFormula(s1, ref, 27, vlookup, fmt(activeDensity, 4), true);

        double weight = g.totalVolume * activeDensity / 1000000.0;
        std::string weightFormula = std::string("E") + fmtInt(r) + "*G" + fmtInt(r) + "/1000000";
        cellRef(ref, sizeof(ref), 7, r);
        cellFormula(s1, ref, 29, weightFormula, fmt(weight, 12), true);

        cellRef(ref, sizeof(ref), 8, r);
        cellNum(s1, ref, 29, fmt(g.cx, 4));
        cellRef(ref, sizeof(ref), 9, r);
        cellNum(s1, ref, 29, fmt(g.cy, 4));
        cellRef(ref, sizeof(ref), 10, r);
        cellNum(s1, ref, 29, fmt(g.cz, 4));

        double mx = weight * g.cx;
        double my = weight * g.cy;
        double mz = weight * g.cz;
        cellRef(ref, sizeof(ref), 11, r);
        cellFormula(s1, ref, 29, std::string("H") + fmtInt(r) + "*I" + fmtInt(r), fmt(mx, 12), true);
        cellRef(ref, sizeof(ref), 12, r);
        cellFormula(s1, ref, 29, std::string("H") + fmtInt(r) + "*J" + fmtInt(r), fmt(my, 12), true);
        cellRef(ref, sizeof(ref), 13, r);
        cellFormula(s1, ref, 29, std::string("H") + fmtInt(r) + "*K" + fmtInt(r), fmt(mz, 12), true);

        cellRef(ref, sizeof(ref), 14, r);
        cellEmpty(s1, ref, 12);

        s1 += "</x:row>";

        sumQuantity += (double)g.quantity;
        sumTotalVolume += g.totalVolume;
        sumWeight += weight;
        sumMx += mx;
        sumMy += my;
        sumMz += mz;
    }

    // 合计行
    {
        char rowOpen[64];
        sprintf_s(rowOpen, sizeof(rowOpen), "<x:row r=\"%d\" ht=\"25\" customHeight=\"1\">", totalRow);
        s1 += rowOpen;
        char ref[16];

        cellRef(ref, sizeof(ref), 0, totalRow);
        cellEmpty(s1, ref, 30);
        cellRef(ref, sizeof(ref), 1, totalRow);
        cellText(s1, ref, 30, utf8(L"合计"));

        cellRef(ref, sizeof(ref), 2, totalRow);
        if (n > 0) {
            cellFormula(s1, ref, 31,
                        std::string("SUM(C") + fmtInt(firstRow) + ":C" + fmtInt(lastRow) + ")",
                        fmtInt((long long)(sumQuantity + 0.5)), true);
        } else {
            cellNum(s1, ref, 31, "0");
        }

        cellRef(ref, sizeof(ref), 3, totalRow);
        cellEmpty(s1, ref, 30);
        cellRef(ref, sizeof(ref), 4, totalRow);
        if (n > 0) {
            cellFormula(s1, ref, 32,
                        std::string("SUM(E") + fmtInt(firstRow) + ":E" + fmtInt(lastRow) + ")",
                        fmt(sumTotalVolume, 12), true);
        } else {
            cellEmpty(s1, ref, 32);
        }
        cellRef(ref, sizeof(ref), 5, totalRow);
        cellEmpty(s1, ref, 32);
        cellRef(ref, sizeof(ref), 6, totalRow);
        cellEmpty(s1, ref, 32);
        cellRef(ref, sizeof(ref), 7, totalRow);
        if (n > 0) {
            cellFormula(s1, ref, 32,
                        std::string("SUM(H") + fmtInt(firstRow) + ":H" + fmtInt(lastRow) + ")",
                        fmt(sumWeight, 12), true);
        } else {
            cellEmpty(s1, ref, 32);
        }
        for (int c = 8; c <= 10; c++) {
            cellRef(ref, sizeof(ref), c, totalRow);
            cellEmpty(s1, ref, 32);
        }
        const double sums[3] = { sumMx, sumMy, sumMz };
        const char cols3[3] = { 'L', 'M', 'N' };
        for (int k = 0; k < 3; k++) {
            cellRef(ref, sizeof(ref), 11 + k, totalRow);
            if (n > 0) {
                cellFormula(s1, ref, 32,
                            std::string("SUM(") + cols3[k] + fmtInt(firstRow) + ":" +
                                cols3[k] + fmtInt(lastRow) + ")",
                            fmt(sums[k], 12), true);
            } else {
                cellEmpty(s1, ref, 32);
            }
        }
        cellRef(ref, sizeof(ref), 14, totalRow);
        cellEmpty(s1, ref, 30);
        s1 += "</x:row>";
    }

    // 核算总成质心行
    {
        char rowOpen[64];
        sprintf_s(rowOpen, sizeof(rowOpen), "<x:row r=\"%d\" ht=\"28\" customHeight=\"1\">", centerRow);
        s1 += rowOpen;
        char ref[16];
        const int stylesTop[7] = { 42, 43, 42, 42, 44, 44, 44 };
        for (int c = 0; c < 7; c++) {
            cellRef(ref, sizeof(ref), c, centerRow);
            if (c == 1) {
                cellText(s1, ref, stylesTop[c], utf8(L"核算总成质心坐标 (mm)"));
            } else {
                cellEmpty(s1, ref, stylesTop[c]);
            }
        }
        cellRef(ref, sizeof(ref), 7, centerRow);
        cellText(s1, ref, 45, utf8(L"X / Y / Z :"));

        const double denom = sumWeight;
        const double moments[3] = { sumMx, sumMy, sumMz };
        const char mcol[3] = { 'L', 'M', 'N' };
        for (int k = 0; k < 3; k++) {
            cellRef(ref, sizeof(ref), 8 + k, centerRow);
            if (n > 0 && denom > 0.0) {
                cellFormula(s1, ref, 46,
                            std::string(1, mcol[k]) + fmtInt(totalRow) + "/H" + fmtInt(totalRow),
                            fmt(moments[k] / denom, 12), true);
            } else {
                cellEmpty(s1, ref, 46);
            }
        }
        for (int c = 11; c <= 13; c++) {
            cellRef(ref, sizeof(ref), c, centerRow);
            cellEmpty(s1, ref, 44);
        }
        cellRef(ref, sizeof(ref), 14, centerRow);
        cellEmpty(s1, ref, 42);
        s1 += "</x:row>";
    }

    s1 += "</x:sheetData>";

    // 合并单元格
    s1 += "<x:mergeCells><x:mergeCell ref=\"A2:O2\" /><x:mergeCell ref=\"A3:O3\" /></x:mergeCells>";

    // 材料牌号下拉
    if (n > 0) {
        char b[512];
        sprintf_s(b, sizeof(b), "<x:dataValidations count=\"1\"><x:dataValidation type=\"list\" sqref=\"F%d:F%d\"><x:formula1>", firstRow, lastRow);
        s1 += b;
        s1 += esc(utf8(L"INDIRECT(\"'材料密度'!$A$2:$A$") + fmtInt(matLastRow) + utf8(L"\")"));
        s1 += "</x:formula1></x:dataValidation></x:dataValidations>";
    }

    s1 += "<x:pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\" />";
    if (!mediaFiles.empty()) {
        s1 += "<x:drawing r:id=\"rIdDrawing1\" />";
    }
    s1 += "</x:worksheet>";

    // -----------------------------------------------------------------
    // 4. drawing1.xml / sheet1.xml.rels
    // -----------------------------------------------------------------
    std::string drawingXml;
    std::string drawingRels;
    std::string sheet1Rels;

    if (!mediaFiles.empty()) {
        drawingXml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        drawingXml += "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\">";
        drawingRels = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        drawingRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";

        int picId = 0;
        for (size_t i = 0; i < n; i++) {
            if (checkAbort(cancelEvent, deadlineTick, hasDeadline, errMsg, outCanceled, outTimedOut)) {
                return false;
            }
            const GroupRow& g = groups[i];
            if (g.mediaIndex <= 0) continue;
            int excelRow = firstRow + (int)i;
            int zeroRow = excelRow - 1;
            char rid[32];
            sprintf_s(rid, sizeof(rid), "RImg%d", g.mediaIndex);
            char pic[160];
            sprintf_s(pic, sizeof(pic), "%d", ++picId);

            drawingXml += "<xdr:oneCellAnchor><xdr:from><xdr:col>14</xdr:col><xdr:colOff>57150</xdr:colOff>";
            char b[256];
            sprintf_s(b, sizeof(b), "<xdr:row>%d</xdr:row><xdr:rowOff>57150</xdr:rowOff></xdr:from>", zeroRow);
            drawingXml += b;
            drawingXml += "<xdr:ext cx=\"990600\" cy=\"990600\" /><xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"";
            drawingXml += pic;
            drawingXml += "\" name=\"Preview ";
            drawingXml += pic;
            drawingXml += "\" /><xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" /></xdr:cNvPicPr></xdr:nvPicPr><xdr:blipFill><a:blip r:embed=\"";
            drawingXml += rid;
            drawingXml += "\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" /><a:stretch xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:fillRect /></a:stretch></xdr:blipFill><xdr:spPr><a:prstGeom prst=\"rect\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" /></xdr:spPr></xdr:pic><xdr:clientData /></xdr:oneCellAnchor>";

            drawingRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"/xl/media/image";
            drawingRels += fmtInt((long long)g.mediaIndex);
            drawingRels += ".png\" Id=\"";
            drawingRels += rid;
            drawingRels += "\" />";
        }
        drawingXml += "</xdr:wsDr>";
        drawingRels += "</Relationships>";

        sheet1Rels = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        sheet1Rels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
        sheet1Rels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"/xl/drawings/drawing1.xml\" Id=\"rIdDrawing1\" />";
        sheet1Rels += "</Relationships>";
    }

    // -----------------------------------------------------------------
    // 5. 微小几何体筛除记录 (sheet3)
    // -----------------------------------------------------------------
    std::string sheet3Xml;
    bool hasSheet3 = false;
    // 异常与警告明细 (empty_body / area_warning / render_warning / inertia_warning ...)
    std::string anomalySheetXml;
    bool hasAnomalySheet = false;
    int anomalySheetNum = 0;
    {
        // baseOf(csv) = "Entity_Analysis_<id>.csv"  →  "Instances_<id>.csv"
        std::wstring csvBase = baseOf(csvPath);
        std::wstring instancesName;
        const std::wstring entityPrefix = L"Entity_Analysis_";
        if (csvBase.size() > entityPrefix.size() &&
            _wcsnicmp(csvBase.c_str(), entityPrefix.c_str(), entityPrefix.size()) == 0) {
            instancesName = L"Instances_" + csvBase.substr(entityPrefix.size());
        }
        std::wstring instancesPath = csvDir + L"\\" + instancesName;
        std::string insText;
        if (!instancesName.empty() && readAll(instancesPath, insText) && !insText.empty()) {
            Table ins = parseCsv(insText);
            if (ins.size() > 0) {
                const Row& ih = ins[0];
                int iIndex = columnOf(ih, "index");
                int iName  = columnOf(ih, "display_name");
                int iFeat  = columnOf(ih, "feature_name");
                int iTag   = columnOf(ih, "tag");
                int iVol   = columnOf(ih, "volume_mm3");
                int iCx    = columnOf(ih, "cx_mm");
                int iCy    = columnOf(ih, "cy_mm");
                int iCz    = columnOf(ih, "cz_mm");
                int iStat  = columnOf(ih, "status");

                if (iIndex >= 0 && iName >= 0 && iFeat >= 0 && iTag >= 0 &&
                    iVol >= 0 && iCx >= 0 && iCy >= 0 && iCz >= 0 && iStat >= 0) {
                    std::vector<Row> tiny;
                    std::vector<Row> anomalies;
                    for (size_t r = 1; r < ins.size(); r++) {
                        if (checkAbort(cancelEvent, deadlineTick, hasDeadline,
                                       errMsg, outCanceled, outTimedOut)) {
                            return false;
                        }
                        if (ins[r].size() <= 1) continue;
                        std::string st = cellOf(ins[r], iStat);
                        if (st == "tiny_volume") {
                            tiny.push_back(ins[r]);
                        } else if (!st.empty() && st != "ok") {
                            anomalies.push_back(ins[r]);
                        }
                    }

                    if (!tiny.empty()) {
                        hasSheet3 = true;
                        const wchar_t* auditHeader[11] = {
                            L"实例序号", L"实体名称", L"特征名称", L"体标签", L"体积 (mm³)",
                            L"质心 X (mm)", L"质心 Y (mm)", L"质心 Z (mm)", L"筛除原因",
                            L"工程处理说明", L"精确测量体积 (mm³)"
                        };
                        const int auditW[11] = { 13, 28, 20, 14, 16, 16, 16, 16, 24, 70, 24 };

                        sheet3Xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
                        sheet3Xml += "<x:worksheet xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
                        sheet3Xml += "<x:sheetViews><x:sheetView showGridLines=\"0\" workbookViewId=\"0\">";
                        sheet3Xml += "<x:pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\" />";
                        sheet3Xml += "</x:sheetView></x:sheetViews>";
                        sheet3Xml += "<x:sheetFormatPr defaultRowHeight=\"15\" />";
                        sheet3Xml += "<x:cols>";
                        for (int c = 0; c < 11; c++) {
                            char b[160];
                            sprintf_s(b, sizeof(b),
                                     "<x:col min=\"%d\" max=\"%d\" width=\"%d\" hidden=\"0\" customWidth=\"1\" />",
                                     c + 1, c + 1, auditW[c]);
                            sheet3Xml += b;
                        }
                        sheet3Xml += "</x:cols>";
                        sheet3Xml += "<x:sheetData>";

                        sheet3Xml += "<x:row r=\"1\" ht=\"32\" customHeight=\"1\">";
                        char ref[16];
                        for (int c = 0; c < 11; c++) {
                            cellRef(ref, sizeof(ref), c, 1);
                            cellText(sheet3Xml, ref, 47, utf8(auditHeader[c]));
                        }
                        sheet3Xml += "</x:row>";

                        for (size_t k = 0; k < tiny.size(); k++) {
                            const Row& row = tiny[k];
                            int r = (int)k + 2;
                            char rowOpen[64];
                            sprintf_s(rowOpen, sizeof(rowOpen), "<x:row r=\"%d\">", r);
                            sheet3Xml += rowOpen;

                            double idxVal = 0.0, tagVal = 0.0, volVal = 0.0;
                            double cxVal = 0.0, cyVal = 0.0, czVal = 0.0;
                            bool okIdx = toNumber(cellOf(row, iIndex), idxVal);
                            bool okTag = toNumber(cellOf(row, iTag), tagVal);
                            bool okVol = toNumber(cellOf(row, iVol), volVal);
                            bool okCx  = toNumber(cellOf(row, iCx), cxVal);
                            bool okCy  = toNumber(cellOf(row, iCy), cyVal);
                            bool okCz  = toNumber(cellOf(row, iCz), czVal);

                            cellRef(ref, sizeof(ref), 0, r);
                            if (okIdx) cellNum(sheet3Xml, ref, 48, fmt(idxVal, 0));
                            else cellEmpty(sheet3Xml, ref, 48);

                            cellRef(ref, sizeof(ref), 1, r);
                            cellText(sheet3Xml, ref, 48, cellOf(row, iName));
                            cellRef(ref, sizeof(ref), 2, r);
                            cellText(sheet3Xml, ref, 48, cellOf(row, iFeat));

                            cellRef(ref, sizeof(ref), 3, r);
                            if (okTag) cellNum(sheet3Xml, ref, 48, fmt(tagVal, 0));
                            else cellEmpty(sheet3Xml, ref, 48);

                            cellRef(ref, sizeof(ref), 4, r);
                            if (okVol) cellNum(sheet3Xml, ref, 49, fmt(volVal, 4));
                            else cellEmpty(sheet3Xml, ref, 49);

                            cellRef(ref, sizeof(ref), 5, r);
                            if (okCx) cellNum(sheet3Xml, ref, 49, fmt(cxVal, 4));
                            else cellEmpty(sheet3Xml, ref, 49);
                            cellRef(ref, sizeof(ref), 6, r);
                            if (okCy) cellNum(sheet3Xml, ref, 49, fmt(cyVal, 4));
                            else cellEmpty(sheet3Xml, ref, 49);
                            cellRef(ref, sizeof(ref), 7, r);
                            if (okCz) cellNum(sheet3Xml, ref, 49, fmt(czVal, 4));
                            else cellEmpty(sheet3Xml, ref, 49);

                            cellRef(ref, sizeof(ref), 8, r);
                            cellText(sheet3Xml, ref, 48, utf8(L"体积小于 1.0 mm³"));
                            cellRef(ref, sizeof(ref), 9, r);
                            cellText(sheet3Xml, ref, 48,
                                     utf8(L"微小几何元素（如建模碎屑或参考微元），已自动从正式 BOM 与总质量测算中剔除"));
                            cellRef(ref, sizeof(ref), 10, r);
                            if (okVol) cellText(sheet3Xml, ref, 48, fmt(volVal, 4));
                            else cellEmpty(sheet3Xml, ref, 48);

                            sheet3Xml += "</x:row>";
                        }

                        sheet3Xml += "</x:sheetData>";
                        sheet3Xml += "<x:pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\" />";
                        sheet3Xml += "</x:worksheet>";
                    }

                    // ---- 异常与警告明细 (empty_body / *_warning) ----
                    if (!anomalies.empty()) {
                        hasAnomalySheet = true;
                        anomalySheetNum = hasSheet3 ? 4 : 3;

                        const wchar_t* anomalyHeader[6] = {
                            L"实例序号", L"实体名称", L"特征名称", L"体标签",
                            L"异常状态", L"说明"
                        };
                        const int anomalyW[6] = { 13, 30, 22, 14, 26, 72 };

                        anomalySheetXml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
                        anomalySheetXml += "<x:worksheet xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
                        anomalySheetXml += "<x:sheetViews><x:sheetView showGridLines=\"0\" workbookViewId=\"0\">";
                        anomalySheetXml += "<x:pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\" />";
                        anomalySheetXml += "</x:sheetView></x:sheetViews>";
                        anomalySheetXml += "<x:sheetFormatPr defaultRowHeight=\"15\" />";
                        anomalySheetXml += "<x:cols>";
                        for (int c = 0; c < 6; c++) {
                            char b[160];
                            sprintf_s(b, sizeof(b),
                                     "<x:col min=\"%d\" max=\"%d\" width=\"%d\" hidden=\"0\" customWidth=\"1\" />",
                                     c + 1, c + 1, anomalyW[c]);
                            anomalySheetXml += b;
                        }
                        anomalySheetXml += "</x:cols>";
                        anomalySheetXml += "<x:sheetData>";

                        anomalySheetXml += "<x:row r=\"1\" ht=\"32\" customHeight=\"1\">";
                        char aref[16];
                        for (int c = 0; c < 6; c++) {
                            cellRef(aref, sizeof(aref), c, 1);
                            cellText(anomalySheetXml, aref, 47, utf8(anomalyHeader[c]));
                        }
                        anomalySheetXml += "</x:row>";

                        for (size_t k = 0; k < anomalies.size(); k++) {
                            if (checkAbort(cancelEvent, deadlineTick, hasDeadline,
                                           errMsg, outCanceled, outTimedOut)) {
                                return false;
                            }
                            const Row& row = anomalies[k];
                            int r = (int)k + 2;
                            char rowOpen[64];
                            sprintf_s(rowOpen, sizeof(rowOpen), "<x:row r=\"%d\">", r);
                            anomalySheetXml += rowOpen;

                            double idxVal = 0.0, tagVal = 0.0;
                            bool okIdx = toNumber(cellOf(row, iIndex), idxVal);
                            bool okTag = toNumber(cellOf(row, iTag), tagVal);

                            cellRef(aref, sizeof(aref), 0, r);
                            if (okIdx) cellNum(anomalySheetXml, aref, 48, fmt(idxVal, 0));
                            else cellEmpty(anomalySheetXml, aref, 48);

                            cellRef(aref, sizeof(aref), 1, r);
                            cellText(anomalySheetXml, aref, 48, cellOf(row, iName));
                            cellRef(aref, sizeof(aref), 2, r);
                            cellText(anomalySheetXml, aref, 48, cellOf(row, iFeat));

                            cellRef(aref, sizeof(aref), 3, r);
                            if (okTag) cellNum(anomalySheetXml, aref, 48, fmt(tagVal, 0));
                            else cellEmpty(anomalySheetXml, aref, 48);

                            std::string st = cellOf(row, iStat);
                            cellRef(aref, sizeof(aref), 4, r);
                            cellText(anomalySheetXml, aref, 48, statusLabel(st));
                            cellRef(aref, sizeof(aref), 5, r);
                            cellText(anomalySheetXml, aref, 48, statusNote(st));

                            anomalySheetXml += "</x:row>";
                        }

                        anomalySheetXml += "</x:sheetData>";
                        anomalySheetXml += "<x:pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\" />";
                        anomalySheetXml += "</x:worksheet>";
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // 6. 包级 XML
    // -----------------------------------------------------------------
    std::string contentTypes = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    contentTypes += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">";
    contentTypes += "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\" />";
    contentTypes += "<Default Extension=\"xml\" ContentType=\"application/xml\" />";
    contentTypes += "<Default Extension=\"png\" ContentType=\"image/png\" />";
    contentTypes += "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\" />";
    contentTypes += "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\" />";
    contentTypes += "<Override PartName=\"/xl/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\" />";
    contentTypes += "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\" />";
    contentTypes += "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\" />";
    contentTypes += "<Override PartName=\"/xl/worksheets/sheet2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\" />";
    if (hasSheet3) {
        contentTypes += "<Override PartName=\"/xl/worksheets/sheet3.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\" />";
    }
    if (hasAnomalySheet) {
        char b[256];
        sprintf_s(b, sizeof(b),
                 "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\" />",
                 anomalySheetNum);
        contentTypes += b;
    }
    if (!mediaFiles.empty()) {
        contentTypes += "<Override PartName=\"/xl/drawings/drawing1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\" />";
    }
    contentTypes += "</Types>";

    std::string rootRels = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    rootRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    rootRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"/xl/workbook.xml\" Id=\"rId1\" />";
    rootRels += "</Relationships>";

    std::string workbook = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    workbook += "<x:workbook xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"";
    workbook += " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    workbook += "<x:sheets>";
    workbook += "<x:sheet name=\"";
    workbook += esc(utf8(L"实体BOM"));
    workbook += "\" sheetId=\"1\" r:id=\"rId4\" />";
    workbook += "<x:sheet name=\"";
    workbook += esc(utf8(L"材料密度"));
    workbook += "\" sheetId=\"2\" r:id=\"rId5\" />";
    if (hasSheet3) {
        workbook += "<x:sheet name=\"";
        workbook += esc(utf8(L"微小几何体筛除记录"));
        workbook += "\" sheetId=\"3\" r:id=\"rId6\" />";
    }
    if (hasAnomalySheet) {
        char b[256];
        sprintf_s(b, sizeof(b), "\" sheetId=\"%d\" r:id=\"%s\" />",
                 anomalySheetNum, hasSheet3 ? "rId7" : "rId6");
        workbook += "<x:sheet name=\"";
        workbook += esc(utf8(L"异常与警告明细"));
        workbook += b;
    }
    workbook += "</x:sheets></x:workbook>";

    std::string workbookRels = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    workbookRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"/xl/styles.xml\" Id=\"rId1\" />";
    workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"/xl/theme/theme1.xml\" Id=\"rId2\" />";
    workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"/xl/sharedStrings.xml\" Id=\"rId3\" />";
    workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"/xl/worksheets/sheet1.xml\" Id=\"rId4\" />";
    workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"/xl/worksheets/sheet2.xml\" Id=\"rId5\" />";
    if (hasSheet3) {
        workbookRels += "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"/xl/worksheets/sheet3.xml\" Id=\"rId6\" />";
    }
    if (hasAnomalySheet) {
        char b[320];
        sprintf_s(b, sizeof(b),
                 "<Relationship Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"/xl/worksheets/sheet%d.xml\" Id=\"%s\" />",
                 anomalySheetNum, hasSheet3 ? "rId7" : "rId6");
        workbookRels += b;
    }
    workbookRels += "</Relationships>";

    std::string sharedStrings = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    sharedStrings += "<x:sst xmlns:x=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" />";

    // -----------------------------------------------------------------
    // 7. 打包
    // -----------------------------------------------------------------
    std::vector<ZipEntry> entries;
    ZipEntry e;

    e.name = "[Content_Types].xml"; e.data = contentTypes; entries.push_back(e);
    e.name = "_rels/.rels"; e.data = rootRels; entries.push_back(e);
    e.name = "xl/workbook.xml"; e.data = workbook; entries.push_back(e);
    e.name = "xl/_rels/workbook.xml.rels"; e.data = workbookRels; entries.push_back(e);
    e.name = "xl/sharedStrings.xml"; e.data = sharedStrings; entries.push_back(e);
    e.name = "xl/styles.xml"; e.data = stylesXml; entries.push_back(e);
    e.name = "xl/theme/theme1.xml"; e.data = themeXml; entries.push_back(e);
    e.name = "xl/worksheets/sheet1.xml"; e.data = s1; entries.push_back(e);
    if (!mediaFiles.empty()) {
        e.name = "xl/worksheets/_rels/sheet1.xml.rels"; e.data = sheet1Rels; entries.push_back(e);
    }
    e.name = "xl/worksheets/sheet2.xml"; e.data = materialsXml; entries.push_back(e);
    if (hasSheet3) {
        e.name = "xl/worksheets/sheet3.xml"; e.data = sheet3Xml; entries.push_back(e);
    }
    if (hasAnomalySheet) {
        e.name = "xl/worksheets/sheet" + fmtInt((long long)anomalySheetNum) + ".xml";
        e.data = anomalySheetXml;
        entries.push_back(e);
    }
    if (!mediaFiles.empty()) {
        e.name = "xl/drawings/drawing1.xml"; e.data = drawingXml; entries.push_back(e);
        e.name = "xl/drawings/_rels/drawing1.xml.rels"; e.data = drawingRels; entries.push_back(e);
        for (size_t i = 0; i < mediaFiles.size(); i++) {
            if (checkAbort(cancelEvent, deadlineTick, hasDeadline,
                           errMsg, outCanceled, outTimedOut)) {
                return false;
            }
            std::string png;
            if (!readAll(mediaFiles[i], png) || png.empty()) {
                errMsg = L"无法读取实体预览图：" + baseOf(mediaFiles[i]);
                return false;
            }
            e.name = "xl/media/image" + fmtInt((long long)(i + 1)) + ".png";
            e.data = png;
            entries.push_back(e);
        }
    }

    std::string zip = buildZip(entries, cancelEvent, deadlineTick, hasDeadline,
                               errMsg, outCanceled, outTimedOut);
    if (zip.empty()) {
        if (errMsg.empty()) errMsg = L"生成 OpenXML 压缩包失败。";
        return false;
    }
    if (!writeAll(outXlsxPath, zip)) {
        errMsg = L"无法写入 Excel 文件：" + outXlsxPath;
        return false;
    }

    return true;
}
