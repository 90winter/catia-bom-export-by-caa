#pragma once
#include <string>

// ---------------------------------------------------------------------------
// Pure native (no Node.js / no external runtime) OpenXML .xlsx workbook writer
// for the filtered BOM.  Compatible with MSVC 2012 (cl.exe 17.00) and with
// Windows 7, because it only uses kernel32 / the C runtime that ship with the
// operating system.
//
//   csvPath      : "Entity_Analysis_<id>.csv" produced by the CAA worker
//                  (the matching "Instances_<id>.csv" and the part preview
//                  *.png files are looked up in the same directory)
//   outXlsxPath  : destination .xlsx file (overwritten)
//   appRoot      : application root, used to locate
//                  Runtime\Export\templates\styles.xml | theme1.xml | sheet2.xml
//   errMsg       : human readable failure reason (already localised)
//
// Returns true on success.
// ---------------------------------------------------------------------------
bool BuildFilteredBomXlsxNative(const std::wstring& csvPath,
                                const std::wstring& outXlsxPath,
                                const std::wstring& appRoot,
                                std::wstring& errMsg);
