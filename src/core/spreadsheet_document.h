#ifndef QUIREWISE_SPREADSHEET_DOCUMENT_INTERNAL_H
#define QUIREWISE_SPREADSHEET_DOCUMENT_INTERNAL_H

#include "quirewise/convert.h"

typedef enum QwSpreadsheetValueKind {
    QW_SPREADSHEET_VALUE_EMPTY = 0,
    QW_SPREADSHEET_VALUE_STRING,
    QW_SPREADSHEET_VALUE_NUMBER,
    QW_SPREADSHEET_VALUE_BOOLEAN,
    QW_SPREADSHEET_VALUE_ERROR,
    QW_SPREADSHEET_VALUE_DATE_TIME
} QwSpreadsheetValueKind;

typedef struct QwSpreadsheetCell {
    size_t row;
    size_t column;
    QwSpreadsheetValueKind value_kind;
    uint8_t *value_utf8;
    size_t value_size;
    uint8_t *formula_utf8;
    size_t formula_size;
} QwSpreadsheetCell;

typedef struct QwSpreadsheetSheet {
    char *name;
    QwSpreadsheetCell *cells;
    size_t cell_count;
    size_t cell_capacity;
} QwSpreadsheetSheet;

typedef struct QwSpreadsheetDocument {
    QwInputFormat input_format;
    QwSpreadsheetSheet *sheets;
    size_t sheet_count;
    size_t sheet_capacity;
    size_t total_cell_count;
    size_t content_bytes;
} QwSpreadsheetDocument;

/* The document owns every name, value and formula inserted through these APIs. */
void qw_spreadsheet_document_init(QwSpreadsheetDocument *document,
                                 QwInputFormat input_format);
int qw_spreadsheet_document_add_sheet(QwSpreadsheetDocument *document,
                                      const uint8_t *name, size_t name_size,
                                      size_t *out_sheet_index);
int qw_spreadsheet_document_add_cell(QwSpreadsheetDocument *document,
                                     size_t sheet_index,
                                     size_t row, size_t column,
                                     QwSpreadsheetValueKind value_kind,
                                     const uint8_t *value, size_t value_size,
                                     const uint8_t *formula,
                                     size_t formula_size);
int qw_spreadsheet_document_validate(const QwSpreadsheetDocument *document);
void qw_spreadsheet_document_dispose(QwSpreadsheetDocument *document);

#endif
