#define UNICODE
#define _UNICODE

#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string>
#include <float.h>
#include "pipeline.h"

static int result = 1;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APP + 1) {
        if (w == 1) {
            printf("progress=%d\n", (int)l);
        } else if (w == 0 || w == 2) {
            printf("stage=%d\n", (int)w);
        } else if (w == 3 || w == 4 || w == 5) {
            std::wstring* x = (std::wstring*)l;
            wprintf(L"done=%d path=%s\n", (int)w, x ? x->c_str() : L"");
            delete x;
            if (w == 3) result = 0;
            PostQuitMessage(0);
        }
        fflush(stdout);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int wmain(int argc, wchar_t** argv) {
    _wsetlocale(LC_ALL, L"");
    if (argc < 3 || argc > 5) {
        wprintf(L"Usage: pipeline_test.exe <input.CATPart> <output-directory> [600|800|1200] [density_kg_m3]\n");
        return 2;
    }

    int size = 800;
    if (argc >= 4) {
        size = _wtoi(argv[3]);
        if (size != 600 && size != 800 && size != 1200) {
            wprintf(L"Invalid size %d (must be 600, 800, or 1200)\n", size);
            return 2;
        }
    }

    double density = 0.0;
    if (argc == 5) {
        wchar_t* end = NULL;
        density = wcstod(argv[4], &end);
        if (end == argv[4] || *end != 0 || !_finite(density) || density <= 0.0) {
            wprintf(L"Invalid density %s (must be positive finite number)\n", argv[4]);
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
    j->density = density;
    j->notify = h;
    j->outExitCode = &result;

    uintptr_t t = _beginthreadex(NULL, 0, RunBomJob, j, 0, NULL);
    if (!t) return 3;

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    WaitForSingleObject((HANDLE)t, INFINITE);
    CloseHandle((HANDLE)t);

    return result;
}
