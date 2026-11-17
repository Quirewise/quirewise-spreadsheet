#ifndef QUIREWISE_SPREADSHEET_H
#define QUIREWISE_SPREADSHEET_H

#include "quirewise/format.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QwSpreadsheetStatus {
    QW_SPREADSHEET_OK = 0,
    QW_SPREADSHEET_ERR_INVALID_ARGUMENT = -1,
    QW_SPREADSHEET_ERR_OUT_OF_MEMORY = -2,
    QW_SPREADSHEET_ERR_FORMAT = -3,
    QW_SPREADSHEET_ERR_UNSUPPORTED = -4,
    QW_SPREADSHEET_ERR_RESOURCE_LIMIT = -5,
    QW_SPREADSHEET_ERR_DECODE = -6
} QwSpreadsheetStatus;

typedef enum QwSpreadsheetFormat {
    QW_SPREADSHEET_FORMAT_INVALID = -1,
    QW_SPREADSHEET_FORMAT_CSV = 0,
    QW_SPREADSHEET_FORMAT_TSV,
    QW_SPREADSHEET_FORMAT_XLS,
    QW_SPREADSHEET_FORMAT_XLT,
    QW_SPREADSHEET_FORMAT_XLSX,
    QW_SPREADSHEET_FORMAT_XLSM,
    QW_SPREADSHEET_FORMAT_XLTX,
    QW_SPREADSHEET_FORMAT_ODS,
    QW_SPREADSHEET_FORMAT_OTS,
    QW_SPREADSHEET_FORMAT_FODS
} QwSpreadsheetFormat;

typedef enum QwSpreadsheetCellKind {
    QW_SPREADSHEET_CELL_EMPTY = 0,
    QW_SPREADSHEET_CELL_STRING,
    QW_SPREADSHEET_CELL_NUMBER,
    QW_SPREADSHEET_CELL_BOOLEAN,
    QW_SPREADSHEET_CELL_ERROR,
    QW_SPREADSHEET_CELL_DATE_TIME
} QwSpreadsheetCellKind;

typedef struct QwSpreadsheetDocument QwSpreadsheetDocument;
typedef struct QwSpreadsheetSheet QwSpreadsheetSheet;

typedef struct QwSpreadsheetCellView {
    size_t row;
    size_t column;
    QwSpreadsheetCellKind kind;
    const uint8_t *value;
    size_t value_size;
    const uint8_t *formula;
    size_t formula_size;
} QwSpreadsheetCellView;

/* Input bytes are borrowed only while parsing. Cell data is owned by document. */
QwSpreadsheetStatus qw_spreadsheet_parse(QwSpreadsheetFormat format,
                                         const void *data, size_t size,
                                         QwSpreadsheetDocument **out_document);
void qw_spreadsheet_document_free(QwSpreadsheetDocument *document);
QwSpreadsheetFormat qw_spreadsheet_document_format(
    const QwSpreadsheetDocument *document);
size_t qw_spreadsheet_sheet_count(const QwSpreadsheetDocument *document);
const QwSpreadsheetSheet *qw_spreadsheet_sheet_at(
    const QwSpreadsheetDocument *document, size_t index);
const char *qw_spreadsheet_sheet_name(const QwSpreadsheetSheet *sheet,
                                      size_t *out_size);
size_t qw_spreadsheet_cell_count(const QwSpreadsheetSheet *sheet);
QwSpreadsheetStatus qw_spreadsheet_cell_at(const QwSpreadsheetSheet *sheet,
                                           size_t index,
                                           QwSpreadsheetCellView *out_cell);

/* Merged cell rectangles (row/column are zero-based, inclusive). */
size_t qw_spreadsheet_sheet_merge_count(const QwSpreadsheetSheet *sheet);
QwSpreadsheetStatus qw_spreadsheet_sheet_merge_at(
    const QwSpreadsheetSheet *sheet, size_t index, size_t *out_row1,
    size_t *out_column1, size_t *out_row2, size_t *out_column2);

/*
 * Write XLSX (QW_FORMAT_XLSX) or ODS (QW_FORMAT_ODS) with all worksheets,
 * values and merged ranges, or one worksheet as CSV (QW_FORMAT_CSV) or TSV
 * (QW_FORMAT_TSV). Text targets have no sheet concept: options->sheet_index
 * selects the worksheet (zero-based) and the report names any skipped sheets.
 * CSV/TSV fields are quoted when they contain the delimiter, a quote or a line
 * break. Other output formats return QW_SPREADSHEET_ERR_UNSUPPORTED. The
 * result is malloc-owned and must be released with qw_spreadsheet_buffer_free();
 * it is not NUL-terminated.
 */
QwSpreadsheetStatus qw_spreadsheet_write_document(
    const QwSpreadsheetDocument *document, QwOutputFormat output_format,
    const QwConvertOptions *options, uint8_t **out_data, size_t *out_size,
    QwConvertReport *report);

/* Release a buffer returned by qw_spreadsheet_write_document(). */
void qw_spreadsheet_buffer_free(void *buffer);

#ifdef __cplusplus
}
#endif

#endif
