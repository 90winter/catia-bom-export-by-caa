#pragma once
#include <windows.h>
#include <string>

struct BomJob {
    std::wstring input;
    std::wstring outputDir;
    int size;
    HWND notify;
    HANDLE cancelEvent;
    int* outExitCode;
    double density;

    BomJob() : size(800), notify(NULL), cancelEvent(NULL), outExitCode(NULL), density(0.0) {}
};

// Takes ownership of a heap-allocated BomJob.
// WM_APP+1 notification stages:
//   0 = Start
//   1 = Progress (lParam = percentage)
//   2 = Excel generation phase
//   3 = Success (lParam = heap std::wstring* owned by receiver)
//   4 = Failure (lParam = heap std::wstring* owned by receiver)
//   5 = Canceled (lParam = heap std::wstring* owned by receiver)
unsigned __stdcall RunBomJob(void* raw);
