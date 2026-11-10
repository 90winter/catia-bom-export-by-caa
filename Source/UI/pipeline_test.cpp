#define UNICODE
#define _UNICODE

#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string>
#include "pipeline.h"

static int result = 1;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APP + 1) {
        if (w == 1) {
            printf("progress=%d\n", (int)l);
        } else if (w == 0 || w == 2) {
            printf("stage=%d\n", (int)w);
        } else if (w == 3 || w == 4) {
            std::wstring* x = (std::wstring*)l;
            wprintf(L"done=%d path=%s\n", (int)w, x ? x->c_str() : L"");
            delete x;
            result = (w == 3) ? 0 : 1;
            PostQuitMessage(0);
        }
        fflush(stdout);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int wmain(int argc, wchar_t** argv) {
    _wsetlocale(LC_ALL, L"");
    if (argc < 3 || argc > 4) {
        wprintf(L"Usage: pipeline_test.exe <input.CATPart> <output-directory> [600|800|1200]\n");
        return 2;
    }

    int size = 800;
    if (argc == 4) {
        size = _wtoi(argv[3]);
        if (size != 600 && size != 800 && size != 1200) {
            wprintf(L"Invalid size %d (must be 600, 800, or 1200)\n", size);
            return 2;
        }
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"BOMTestHarness";
    RegisterClassW(&wc);

    HWND h = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    BomJob* j = new BomJob;
    j->input = argv[1];
    j->outputDir = argv[2];
    j->size = size;
    j->notify = h;

    uintptr_t t = _beginthreadex(NULL, 0, RunBomJob, j, 0, NULL);
    if (!t) return 3;
    CloseHandle((HANDLE)t);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return result;
}
