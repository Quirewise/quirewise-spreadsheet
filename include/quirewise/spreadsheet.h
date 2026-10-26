#ifndef QUIREWISE_SPREADSHEET_H
#define QUIREWISE_SPREADSHEET_H

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

#ifdef __cplusplus
}
#endif

#endif
