# quirewise-spreadsheet

Workbook parser and writer for XLSX/XLSM/XLTX, XLS/XLT, ODS/OTS/FODS and
CSV/TSV, exposing sheets, cells, cell kinds and cached formula text.

`Quirewise::Spreadsheet` is an independent, MIT-licensed C11 library. It is
installed as a CMake package and can be consumed without the Quirewise
application or any other Quirewise package.

## What it does

- Parses XLSX, XLSM and XLTX workbooks: workbook relationships, sheet order,
  shared and inline strings, cached cell values, date-typed cells and normal
  formula text.
- Parses BIFF8 XLS and XLT workbooks from the CFB `Workbook`/`Book` stream,
  including shared strings, `LABEL`, `RK`, `NUMBER`, `MULRK`, `BOOLERR` and
  cached formula results, plus CFB v3 stream sizes whose high half is
  uninitialized.
- Parses ODS, OTS and FODS spreadsheets from the packaged `content.xml` or the
  flat XML form, including repeated rows/columns, scalar value types and
  `table:formula` text.
- Parses CSV and TSV with quoted fields, escaped quotes and multi-line cells.
  The separator is detected from comma, tab and semicolon; all CSV/TSV fields
  become string cells and no type inference is performed.
- Exposes one uniform model: ordered sheets, and per sheet the cells in strictly
  increasing `(row, column)` order, each with a kind, a UTF-8 value and optional
  cached formula text.
- Writes XLSX and ODS packages with every worksheet, cell value and merged
  range through `qw_spreadsheet_write_document`; CSV/TSV output selects the
  first worksheet by default and honors `QwConvertOptions::sheet_index`.

The format is selected by the caller through `QwSpreadsheetFormat`; there is no
content probing function.

**Deliberate boundaries.** XLSX and ODS are read and written; legacy XLS/XLT is
read-only but can be converted to XLSX/ODS. CSV/TSV output writes the selected
worksheet only - text targets have no sheet concept - and quotes fields
containing the delimiter, a quote or a line break. Formulas are never evaluated:
only the cached result stored in the file is returned, and writer output keeps
values plus merged ranges, not formula results. Styles, number formats, charts,
images, macros and template behavior are not modeled.
Encrypted XLS workbooks are unsupported, and an XLSX cell that uses a shared or
array formula makes the typed parse fail with
`QW_SPREADSHEET_ERR_UNSUPPORTED`. ODS time-typed cells are outside the
supported typed subset.

## Requirements

- C11 compiler and CMake 3.21 or newer.
- The `QuirewiseRuntime`, `QuirewiseArchive` and `QuirewiseData` CMake packages
  must be installed; the exported `QuirewiseSpreadsheet` package config finds
  them. `Archive` provides ZIP/CFB container access and `Data` provides the XML
  reader.
- CTest is used for the test suite.

## Build, test and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/quirewise/prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Shared builds are available with `-DQUIREWISE_BUILD_SHARED=ON`.

## Use it from CMake

```cmake
find_package(QuirewiseSpreadsheet CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Spreadsheet)
```

## Quick start

Parse CSV and print every cell:

```c
#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char csv[] = "name,qty\napple,3\n\"pear, green\",5\n";
    QwSpreadsheetDocument *document = NULL;
    const QwSpreadsheetSheet *sheet;
    size_t index;

    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_CSV, csv,
                             sizeof(csv) - 1u, &document) !=
        QW_SPREADSHEET_OK) {
        fputs("CSV parse failed\n", stderr);
        return 1;
    }
    sheet = qw_spreadsheet_sheet_at(document, 0u);
    for (index = 0u; sheet && index < qw_spreadsheet_cell_count(sheet);
         ++index) {
        QwSpreadsheetCellView cell;
        if (qw_spreadsheet_cell_at(sheet, index, &cell) != QW_SPREADSHEET_OK)
            continue;
        printf("(%zu,%zu) kind=%d %.*s\n", cell.row, cell.column,
               (int)cell.kind, (int)cell.value_size,
               cell.value ? (const char *)cell.value : "");
    }
    qw_spreadsheet_document_free(document);
    return 0;
}
```

## API overview

| Header | Purpose |
|---|---|
| `quirewise/spreadsheet.h` | all public entry points |

| Task | Functions |
|---|---|
| Parse | `qw_spreadsheet_parse` |
| Write | `qw_spreadsheet_write_document`, `qw_spreadsheet_buffer_free` |
| Document lifetime | `qw_spreadsheet_document_free` |
| Document info | `qw_spreadsheet_document_format`, `qw_spreadsheet_sheet_count` |
| Sheets | `qw_spreadsheet_sheet_at`, `qw_spreadsheet_sheet_name`, `qw_spreadsheet_cell_count` |
| Cells | `qw_spreadsheet_cell_at` |

The public cell type is `QwSpreadsheetCellView` with `row`, `column`, `kind`,
`value`/`value_size` and `formula`/`formula_size`. `QwSpreadsheetCellKind` is
one of `EMPTY`, `STRING`, `NUMBER`, `BOOLEAN`, `ERROR` or `DATE_TIME`.

## Behavior and limits

- `qw_spreadsheet_parse` returns `QW_SPREADSHEET_OK` (0) or a negative
  `QwSpreadsheetStatus`. Input bytes are borrowed for the call; the document
  owns all names, values and formulas.
- Cells are stored per sheet in strictly increasing `(row, column)` order and
  gaps are not materialized; an entry may still carry `EMPTY` when the source
  stored an explicit empty cell. There is no random-access index; scan the
  ordered cells and build your own map if you need lookups.
- `cell.value` is UTF-8 text for every kind, including numbers (the source
  lexeme, e.g. `3` or `12.5`). Boolean cells hold `"1"` or `"0"`. Error cells
  hold spreadsheet error text such as `#DIV/0!`.
- `cell.formula` is present for XLSX normal formulas and ODS `of:=` formulas.
  XLS formulas are not retained. Formula text never includes a recomputed
  value; `value` is the cached result from the file.
- `qw_spreadsheet_cell_at` zeroes the view, returns
  `QW_SPREADSHEET_ERR_INVALID_ARGUMENT` for a null sheet or out-of-range index,
  and otherwise returns `QW_SPREADSHEET_OK`.
- Structural limits: 256 sheets, 1,000,000 cells per document, 1 MiB per cell
  value, 64 KiB per formula, 256 MiB of text content, 4 KiB per sheet name.
  Format limits: XLS 65,536 rows x 256 columns and a 128 MiB workbook stream;
  XLSX 1,048,576 rows x 16,384 columns and 32 MiB per XML part; ODS 1,000,000
  rows x 16,384 columns; CSV/TSV 256 MiB.
- CSV/TSV requires at least two rows and two columns and a consistent column
  count in the sniffed prefix; a single-column file is rejected with
  `QW_SPREADSHEET_ERR_FORMAT`.
- XLS worksheets whose cells are not row-major are accepted, but out-of-order
  cells are skipped to preserve the ordered model.

## Testing

`ctest` covers CSV/TSV parsing (quoted field with an embedded comma), XLS
parsing against two CC0 fixtures (`valid.xls`: 10 sheets/3301 cells;
`MonteCarlo.xls`: 7 sheets/3950 cells, including truncated-file rejection), and
a deterministic fuzz smoke over CSV/TSV seeds.

## Documentation

See [docs/usage.md](docs/usage.md) for task-oriented recipes, the cell model,
formula handling, status values and limits.

## License

MIT. See [LICENSE](LICENSE).
