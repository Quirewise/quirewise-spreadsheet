# quirewise-spreadsheet usage guide

Task-level guide to `Quirewise::Spreadsheet`. The public API is declared in
`quirewise/spreadsheet.h` and works on one uniform workbook model for all
supported formats.

## 1. Build and integrate

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/quirewise/prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(QuirewiseSpreadsheet CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Quirewise::Spreadsheet)
```

The exported config calls `find_dependency` for `QuirewiseRuntime`,
`QuirewiseArchive` and `QuirewiseData`; install those packages in a visible
prefix. Add `-DQUIREWISE_BUILD_SHARED=ON` for shared libraries.

## 2. Core model

### 2.1 Documents, sheets and cells

`qw_spreadsheet_parse(format, data, size, &document)` selects the parser from
the enum; there is no content sniffing. On success the document owns a `sheet`
array, each sheet owns an ordered cell array:

```c
typedef struct QwSpreadsheetCellView {
    size_t row;                 /* zero-based */
    size_t column;              /* zero-based */
    QwSpreadsheetCellKind kind;
    const uint8_t *value;       /* UTF-8, borrowed from the document */
    size_t value_size;
    const uint8_t *formula;     /* cached formula text or NULL */
    size_t formula_size;
} QwSpreadsheetCellView;
```

`qw_spreadsheet_sheet_count` and `qw_spreadsheet_sheet_at` walk sheets in
workbook order. `qw_spreadsheet_sheet_name` returns a NUL-terminated name and
its length. `qw_spreadsheet_cell_count` and `qw_spreadsheet_cell_at` walk a
sheet's cells.

### 2.2 Cell kinds

| Kind | `value` contents |
|---|---|
| `QW_SPREADSHEET_CELL_EMPTY` | empty; `value_size` is 0 |
| `QW_SPREADSHEET_CELL_STRING` | UTF-8 text |
| `QW_SPREADSHEET_CELL_NUMBER` | numeric lexeme, e.g. `3`, `-12.5`, `1e+06` |
| `QW_SPREADSHEET_CELL_BOOLEAN` | `"1"` or `"0"` |
| `QW_SPREADSHEET_CELL_ERROR` | spreadsheet error text such as `#DIV/0!` |
| `QW_SPREADSHEET_CELL_DATE_TIME` | date/time text as stored in the file |

Every value is text. The library never converts numbers to `double`; parse the
lexeme yourself when you need arithmetic. Dates are only classified as
`DATE_TIME` when the source marks them as such (XLSX `t="d"`, ODS `date`).
XLSX numeric cells with a date number format stay `NUMBER`, because number
formats are not mapped.

### 2.3 Ordering and sparseness

Within a sheet, cells are stored in strictly increasing `(row, column)` order.
Gaps are implicit: a sheet with cells at `(0,0)` and `(5,2)` has two entries,
not eighteen. An explicit empty cell may appear as `EMPTY` when the source
stored one.

There is no random-access index. Either scan linearly, or exploit the ordering
to stop as soon as you pass the requested coordinate. Build a hash map if you
need many lookups.

### 2.4 Formulas

`formula` carries the formula text stored in the file, when the format and cell
support it:

| Format | Formula text |
|---|---|
| XLSX/XLSM/XLTX | normal formulas (no `t` attribute or `t="normal"`); shared and array formulas make the typed parse fail with `UNSUPPORTED` |
| ODS/OTS/FODS | `table:formula` values beginning with `of:=`; other formula namespaces fail with `UNSUPPORTED` |
| XLS/XLT | not retained (`formula` is `NULL`) |
| CSV/TSV | not applicable |

The `value` beside a formula is the cached result from the file. Formulas are
never evaluated and no recalculation is offered.

### 2.5 Ownership

Input bytes are borrowed for the parse call only. The document owns sheet
names, values and formulas; the pointers in a `QwSpreadsheetCellView` stay valid
until `qw_spreadsheet_document_free`. Cells are read-only views - there is no
API to modify or add a cell.

## 3. Workflows
Recipes are excerpts; place them inside a function and supply the inputs.

### 3.1 Parse CSV and print the grid

```c
#include <quirewise/spreadsheet.h>
#include <stdio.h>

static const char csv[] = "name,qty\napple,3\n\"pear, green\",5\n";
QwSpreadsheetDocument *document = NULL;
if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_CSV, csv, sizeof(csv) - 1u,
                         &document) == QW_SPREADSHEET_OK) {
    const QwSpreadsheetSheet *sheet = qw_spreadsheet_sheet_at(document, 0u);
    size_t index;
    for (index = 0u; sheet && index < qw_spreadsheet_cell_count(sheet);
         ++index) {
        QwSpreadsheetCellView cell;
        if (qw_spreadsheet_cell_at(sheet, index, &cell) == QW_SPREADSHEET_OK)
            printf("(%zu,%zu) %.*s\n", cell.row, cell.column,
                   (int)cell.value_size,
                   cell.value ? (const char *)cell.value : "");
    }
    qw_spreadsheet_document_free(document);
}
```

CSV and TSV always produce one sheet named `Sheet1` with string cells. The
delimiter is sniffed, so the same call handles comma-, tab- and
semicolon-separated files; a quoted field may contain commas and newlines, and
`""` inside a quoted field is an escaped quote.

### 3.2 Dump an XLSX workbook

```c
static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long length;
    unsigned char *data = NULL;

    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 &&
        fseek(file, 0, SEEK_SET) == 0 &&
        (data = malloc((size_t)length)) != NULL &&
        fread(data, 1u, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (data) *out_size = (size_t)length;
    return data;
}

size_t size = 0u;
unsigned char *bytes = read_file("workbook.xlsx", &size);
QwSpreadsheetDocument *document = NULL;

if (bytes && qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, bytes, size,
                                  &document) == QW_SPREADSHEET_OK) {
    size_t sheet_index;
    for (sheet_index = 0u; sheet_index < qw_spreadsheet_sheet_count(document);
         ++sheet_index) {
        const QwSpreadsheetSheet *sheet =
            qw_spreadsheet_sheet_at(document, sheet_index);
        size_t name_size = 0u, cell_index;
        const char *name = qw_spreadsheet_sheet_name(sheet, &name_size);
        printf("sheet %zu: %.*s\n", sheet_index, (int)name_size, name);
        for (cell_index = 0u; cell_index < qw_spreadsheet_cell_count(sheet);
             ++cell_index) {
            QwSpreadsheetCellView cell;
            if (qw_spreadsheet_cell_at(sheet, cell_index, &cell) !=
                QW_SPREADSHEET_OK)
                continue;
            printf("  (%zu,%zu) %.*s\n", cell.row, cell.column,
                   (int)cell.value_size,
                   cell.value ? (const char *)cell.value : "");
        }
    }
    qw_spreadsheet_document_free(document);
}
free(bytes);
```

XLSM, XLTX, XLS, XLT, ODS, OTS and FODS follow the same shape with their
respective `QwSpreadsheetFormat` value. XLSX and ODS containers are ZIP/XML
packages, so always pass the complete file bytes, never a single component.

### 3.3 Inspect formulas and cached results

```c
size_t index;
for (index = 0u; index < qw_spreadsheet_cell_count(sheet); ++index) {
    QwSpreadsheetCellView cell;
    if (qw_spreadsheet_cell_at(sheet, index, &cell) != QW_SPREADSHEET_OK)
        continue;
    if (cell.formula && cell.formula_size)
        printf("(%zu,%zu) =%.*s cached=%.*s\n", cell.row, cell.column,
               (int)cell.formula_size, (const char *)cell.formula,
               (int)cell.value_size,
               cell.value ? (const char *)cell.value : "");
}
```

Do not expect a formula on every numeric cell: XLS does not expose formula text,
XLSX only keeps normal formulas, and ODS only keeps `of:=` formulas. The cached
value can be absent for an unevaluated formula; in that case `value_size` is 0.

### 3.4 Look up a cell by address

Because cells are ordered, a lookup can stop as soon as it passes the target
instead of scanning the whole sheet: iterate `qw_spreadsheet_cell_at`, compare
`row`/`column`, and break when `cell.row > row || (cell.row == row &&
cell.column > column)`. A1 notation maps to row 0, column 0. A "not found"
result is also the correct answer for an implicitly empty cell. For many
lookups, build a hash map once rather than repeating the scan.

## 4. Error handling

| Value | Meaning |
|---|---|
| `QW_SPREADSHEET_OK` (0) | success |
| `QW_SPREADSHEET_ERR_INVALID_ARGUMENT` (-1) | null pointer, empty input or bad cell index |
| `QW_SPREADSHEET_ERR_OUT_OF_MEMORY` (-2) | allocation failure |
| `QW_SPREADSHEET_ERR_FORMAT` (-3) | malformed file or content the format parser cannot accept |
| `QW_SPREADSHEET_ERR_UNSUPPORTED` (-4) | valid file outside the supported subset (encrypted XLS, shared/array XLSX formulas, ODS time cells) |
| `QW_SPREADSHEET_ERR_RESOURCE_LIMIT` (-5) | sheet, cell, value or input limit exceeded |
| `QW_SPREADSHEET_ERR_DECODE` (-6) | container or text decoding failed |

On any failure `*out_document` is set to `NULL`, so there is nothing to free.
Typical causes:

- CSV/TSV: fewer than two rows or columns in the sniffed prefix, an
  inconsistent column count there, or bytes that are not valid UTF-8
  (`DECODE`).
- XLSX/XLSM/XLTX: a damaged ZIP package, a missing workbook or worksheet
  relationship (`FORMAT`/`DECODE`), a shared/array formula or unknown cell type
  (`UNSUPPORTED`).
- XLS/XLT: a non-BIFF8 or truncated CFB stream (`FORMAT`/`DECODE`), encryption
  (`UNSUPPORTED`), a workbook stream above 128 MiB (`RESOURCE_LIMIT`).
- ODS/OTS/FODS: a missing `content.xml` (`FORMAT`), an unsupported value type
  or a formula outside `of:=` (`UNSUPPORTED`).

## 4.1 Write a workbook

`qw_spreadsheet_write_document(document, format, options, &data, &size, &report)`
writes XLSX (`QW_FORMAT_XLSX`) or ODS (`QW_FORMAT_ODS`) with every worksheet,
cell value and merged range. `QW_FORMAT_CSV`/`QW_FORMAT_TSV` write one
worksheet; pass `options->sheet_index` (zero-based; the CLI `--sheet N` is
one-based) to choose it. Release the result with
`qw_spreadsheet_buffer_free`. Other target formats return
`QW_SPREADSHEET_ERR_UNSUPPORTED`.

## 5. Memory, lifetime and threading

- Input buffers are borrowed for the parse call; free your file buffer
  immediately afterwards. The document owns names, values and formulas and is
  released once with `qw_spreadsheet_document_free`, which accepts `NULL`.
- `QwSpreadsheetCellView` points into the document and becomes invalid when the
  document is freed; `qw_spreadsheet_cell_at` zeroes the view first, so a failed
  call leaves a safe empty view.
- There is no global mutable state and no initialization step. Separate
  documents may be parsed on different threads; do not free one while another
  thread reads it.

## 6. Limits and format notes

| Scope | Limit |
|---|---|
| Sheets per document | 256 |
| Cells per document | 1,000,000 |
| Cell value | 1 MiB |
| Formula text | 64 KiB |
| Document text content | 256 MiB |
| Sheet name | 4 KiB |
| CSV/TSV input | 256 MiB |
| XLS workbook stream | 128 MiB, 65,536 rows x 256 columns |
| XLSX worksheet | 1,048,576 rows x 16,384 columns, 32 MiB per XML part |
| ODS sheet | 1,000,000 rows x 16,384 columns |

Format notes:

- CSV/TSV: UTF-8 (optional BOM), `\n` or `\r\n` line endings. Fields are
  always strings; the separator is chosen from `,`, tab and `;` by a
  consistency probe over the first 64 KiB. Fields above 1 MiB report a resource
  limit, as does a document above 1,000,000 cells.
- XLSX: values come from shared strings, inline strings, boolean/error/date
  markers and numeric text. Merged ranges are exposed through
  `qw_spreadsheet_sheet_merge_count`/`_at` and preserved by the writer; number
  formats, styles, charts, images and macros are ignored; sheet order comes from
  `workbook.xml`.
- XLS: BIFF8 records only. Out-of-order worksheet cells are skipped, encrypted
  workbooks are rejected, and formulas are not retained; dates are number
  lexemes.
- ODS: repeated rows and columns are expanded subject to the limits; covered
  cells are skipped; string, float, percentage, currency, boolean and date
  value types are mapped; time values are unsupported.
- XLSX and ODS writers preserve values and merged ranges, not styles, charts,
  images or recomputed formulas. Legacy XLS/XLT is read-only. Formulas are never
  evaluated in any format.

## 7. Related packages

- `Quirewise::Archive` opens the ZIP and CFB containers used by XLSX/XLSM/XLTX
  and XLS/XLT; it is a private link dependency found by the package config.
- `Quirewise::Data` parses the XML parts of XLSX and ODS workbooks.
- `Quirewise::Runtime` supplies shared status and utility definitions.
- `Quirewise::Document` consumes `Quirewise::Spreadsheet` when a document
  contains a spreadsheet attachment.

## 8. License

MIT. See `LICENSE`.
