#pragma once
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ---------------------------------------------------------------------------
// Pure native (no Node.js / no external runtime) OpenXML .xlsx workbook writer
// for the filtered BOM.  Compatible with MSVC 2012 (cl.exe 17.00) and with
// Windows 7, because it only uses kernel32 / the C runtime that ship with the
// operating system.
//
//   csvPath       : "Entity_Analysis_<id>.csv" produced by the CAA worker
//                   (the matching "Instances_<id>.csv" and the part preview
//                   *.png files are looked up in the same directory)
//   outXlsxPath   : destination .xlsx file (overwritten)
//   appRoot       : application root, used to locate
//                   Runtime\Export\templates\styles.xml | theme1.xml | sheet2.xml
//   errMsg        : human readable failure reason (already localised)
//   customDensity : > 0.0 -> a 21st material row "自定义密度" is appended to the
//                   《材料密度》 sheet and used as the default material of every
//                   BOM row (mass / centre-of-gravity are recomputed with it);
//                   <= 0.0 -> "PP-TD20" and its template density are used.
//   cancelEvent   : optional manual-reset event; when signalled the writer
//                   aborts as fast as possible and reports outCanceled.
//   deadlineTick  : absolute GetTickCount() deadline (only used when
//                   hasDeadline == true); when expired the writer reports
//                   outTimedOut.
//   outCanceled   : set to true when the user cancelled the task.
//   outTimedOut   : set to true when the global deadline was exceeded.
//
// Returns true on success.
// ---------------------------------------------------------------------------
bool BuildFilteredBomXlsxNative(const std::wstring& csvPath,
                                const std::wstring& outXlsxPath,
                                const std::wstring& appRoot,
                                std::wstring& errMsg,
                                double customDensity = 0.0,
                                HANDLE cancelEvent = NULL,
                                DWORD deadlineTick = 0,
                                bool hasDeadline = false,
                                bool* outCanceled = NULL,
                                bool* outTimedOut = NULL);
