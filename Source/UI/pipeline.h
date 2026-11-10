#pragma once
#include <windows.h>
#include <string>

struct BomJob {
    std::wstring input;
    std::wstring outputDir;
    int size;
    HWND notify;
};

// Takes ownership of a heap-allocated BomJob.
// WM_APP+1 notification stages:
//   0 = Start
//   1 = Progress (lParam = percentage)
//   2 = Excel generation phase
//   3 = Success (lParam = heap std::wstring* owned by receiver)
//   4 = Failure (lParam = heap std::wstring* owned by receiver)
unsigned __stdcall RunBomJob(void* raw);
