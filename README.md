# quirewise-spreadsheet

Standalone C11 workbook parser.

CMake target: `Quirewise::Spreadsheet`. Public header: `quirewise/spreadsheet.h`.

## Capabilities

- CSV/TSV: comma, tab or semicolon separators; quoted fields, escaped quotes and
  multi-line cells. Fields are text; no type inference.
- BIFF8 XLS/XLT: reads the CFB Workbook/Book stream, shared strings, cells and
  cached formula results.
- XLSX/XLSM/XLTX: reads workbook relationships, sheets and shared strings.
- ODS/OTS/FODS: reads the ODF package `content.xml` or flat XML.

The parser exposes sheets, cells, cell types and cached formula values.

## Limits

This is a **read-only** parser: there is no XLSX/XLS/ODS/CSV writer. Formulas are
never evaluated; only cached results are returned. Styles, merged regions,
charts, images, macros and template behaviour are not mapped. Limits: 256
sheets, 1,000,000 cells, 16,384 columns; BIFF8 only (no encrypted workbooks,
SST strings cannot span BIFF records).

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```
