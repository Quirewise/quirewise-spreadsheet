#include "spreadsheet_document.h"

#include <stdlib.h>
#include <string.h>

enum {
    QW_SPREADSHEET_MAX_SHEETS = 256,
    QW_SPREADSHEET_MAX_CELLS = 1000000,
    QW_SPREADSHEET_MAX_NAME_BYTES = 4096,
    QW_SPREADSHEET_MAX_VALUE_BYTES = 1024 * 1024,
    QW_SPREADSHEET_MAX_FORMULA_BYTES = 64 * 1024,
    QW_SPREADSHEET_MAX_CONTENT_BYTES = 256 * 1024 * 1024
};

static uint8_t *spreadsheet_copy_bytes(const uint8_t *source, size_t size)
{
    uint8_t *copy;
    if (!source && size) return NULL;
    if (size == SIZE_MAX) return NULL;
    copy = (uint8_t *)malloc(size + 1u);
    if (!copy) return NULL;
    if (size) memcpy(copy, source, size);
    copy[size] = 0u;
    return copy;
}

void qw_spreadsheet_document_init(QwSpreadsheetDocument *document,
                                 QwInputFormat input_format)
{
    if (!document) return;
    memset(document, 0, sizeof(*document));
    document->input_format = input_format;
}

int qw_spreadsheet_document_add_sheet(QwSpreadsheetDocument *document,
                                      const uint8_t *name, size_t name_size,
                                      size_t *out_sheet_index)
{
    QwSpreadsheetSheet *grown;
    QwSpreadsheetSheet *sheet;
    char *copy;
    size_t capacity;
    if (!document || !name || !name_size || !out_sheet_index ||
        name_size > QW_SPREADSHEET_MAX_NAME_BYTES ||
        memchr(name, 0, name_size) ||
        document->sheet_count >= QW_SPREADSHEET_MAX_SHEETS) return 0;
    if (document->sheet_count == document->sheet_capacity) {
        capacity = document->sheet_capacity ? document->sheet_capacity * 2u : 4u;
        if (capacity > QW_SPREADSHEET_MAX_SHEETS)
            capacity = QW_SPREADSHEET_MAX_SHEETS;
        if (capacity <= document->sheet_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return 0;
        grown = (QwSpreadsheetSheet *)realloc(document->sheets,
            capacity * sizeof(*grown));
        if (!grown) return 0;
        document->sheets = grown;
        document->sheet_capacity = capacity;
    }
    copy = (char *)spreadsheet_copy_bytes(name, name_size);
    if (!copy) return 0;
    sheet = &document->sheets[document->sheet_count];
    memset(sheet, 0, sizeof(*sheet));
    sheet->name = copy;
    *out_sheet_index = document->sheet_count++;
    if (name_size > QW_SPREADSHEET_MAX_CONTENT_BYTES - document->content_bytes) {
        free(sheet->name);
        --document->sheet_count;
        memset(sheet, 0, sizeof(*sheet));
        return 0;
    }
    document->content_bytes += name_size;
    return 1;
}

int qw_spreadsheet_document_add_cell(QwSpreadsheetDocument *document,
                                     size_t sheet_index,
                                     size_t row, size_t column,
                                     QwSpreadsheetValueKind value_kind,
                                     const uint8_t *value, size_t value_size,
                                     const uint8_t *formula,
                                     size_t formula_size)
{
    QwSpreadsheetSheet *sheet;
    QwSpreadsheetCell *grown;
    QwSpreadsheetCell *cell;
    uint8_t *value_copy = NULL;
    uint8_t *formula_copy = NULL;
    size_t capacity;
    size_t added_bytes;
    if (!document || sheet_index >= document->sheet_count ||
        value_kind < QW_SPREADSHEET_VALUE_EMPTY ||
        value_kind > QW_SPREADSHEET_VALUE_DATE_TIME ||
        (!value && value_size) || (!formula && formula_size) ||
        value_size > QW_SPREADSHEET_MAX_VALUE_BYTES ||
        formula_size > QW_SPREADSHEET_MAX_FORMULA_BYTES ||
        ((value_kind == QW_SPREADSHEET_VALUE_EMPTY) && value_size) ||
        (document->content_bytes > QW_SPREADSHEET_MAX_CONTENT_BYTES) ||
        value_size > QW_SPREADSHEET_MAX_CONTENT_BYTES - document->content_bytes)
        return 0;
    added_bytes = value_size;
    if (formula_size > QW_SPREADSHEET_MAX_CONTENT_BYTES -
                           document->content_bytes - added_bytes) return 0;
    added_bytes += formula_size;
    if (added_bytes > QW_SPREADSHEET_MAX_CONTENT_BYTES -
                          document->content_bytes) return 0;
    sheet = &document->sheets[sheet_index];
    if (sheet->cell_count >= QW_SPREADSHEET_MAX_CELLS ||
        document->total_cell_count >= QW_SPREADSHEET_MAX_CELLS) return 0;
    if (sheet->cell_count) {
        const QwSpreadsheetCell *previous = &sheet->cells[sheet->cell_count - 1u];
        if (row < previous->row ||
            (row == previous->row && column <= previous->column)) return 0;
    }
    if (value_size) {
        value_copy = spreadsheet_copy_bytes(value, value_size);
        if (!value_copy) return 0;
    }
    if (formula_size) {
        formula_copy = spreadsheet_copy_bytes(formula, formula_size);
        if (!formula_copy) {
            free(value_copy);
            return 0;
        }
    }
    if (sheet->cell_count == sheet->cell_capacity) {
        capacity = sheet->cell_capacity ? sheet->cell_capacity * 2u : 64u;
        if (capacity > QW_SPREADSHEET_MAX_CELLS)
            capacity = QW_SPREADSHEET_MAX_CELLS;
        if (capacity <= sheet->cell_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) {
            free(value_copy);
            free(formula_copy);
            return 0;
        }
        grown = (QwSpreadsheetCell *)realloc(sheet->cells,
            capacity * sizeof(*grown));
        if (!grown) {
            free(value_copy);
            free(formula_copy);
            return 0;
        }
        sheet->cells = grown;
        sheet->cell_capacity = capacity;
    }
    cell = &sheet->cells[sheet->cell_count++];
    cell->row = row;
    cell->column = column;
    cell->value_kind = value_kind;
    cell->value_utf8 = value_copy;
    cell->value_size = value_size;
    cell->formula_utf8 = formula_copy;
    cell->formula_size = formula_size;
    ++document->total_cell_count;
    document->content_bytes += added_bytes;
    return 1;
}

int qw_spreadsheet_document_validate(const QwSpreadsheetDocument *document)
{
    size_t i;
    size_t total_cell_count = 0u;
    if (!document || document->input_format <= QW_FORMAT_UNKNOWN ||
        document->input_format >= QW_FORMAT_COUNT ||
        !document->sheets || !document->sheet_count ||
        document->sheet_count > QW_SPREADSHEET_MAX_SHEETS ||
        document->content_bytes > QW_SPREADSHEET_MAX_CONTENT_BYTES) return 0;
    for (i = 0u; i < document->sheet_count; ++i) {
        const QwSpreadsheetSheet *sheet = &document->sheets[i];
        size_t j;
        if (!sheet->name || !sheet->name[0] ||
            (!sheet->cells && sheet->cell_count) ||
            sheet->cell_count > QW_SPREADSHEET_MAX_CELLS) return 0;
        for (j = 0u; j < sheet->cell_count; ++j) {
            const QwSpreadsheetCell *cell = &sheet->cells[j];
            if (cell->value_kind < QW_SPREADSHEET_VALUE_EMPTY ||
                cell->value_kind > QW_SPREADSHEET_VALUE_DATE_TIME ||
                (!cell->value_utf8 && cell->value_size) ||
                (!cell->formula_utf8 && cell->formula_size) ||
                (cell->value_kind == QW_SPREADSHEET_VALUE_EMPTY &&
                 cell->value_size)) return 0;
            if (j && (cell->row < sheet->cells[j - 1u].row ||
                (cell->row == sheet->cells[j - 1u].row &&
                 cell->column <= sheet->cells[j - 1u].column))) return 0;
            ++total_cell_count;
        }
    }
    return total_cell_count == document->total_cell_count &&
           total_cell_count <= QW_SPREADSHEET_MAX_CELLS;
}

void qw_spreadsheet_document_dispose(QwSpreadsheetDocument *document)
{
    size_t i;
    if (!document) return;
    for (i = 0u; i < document->sheet_count; ++i) {
        QwSpreadsheetSheet *sheet = &document->sheets[i];
        size_t j;
        for (j = 0u; j < sheet->cell_count; ++j) {
            free(sheet->cells[j].value_utf8);
            free(sheet->cells[j].formula_utf8);
        }
        free(sheet->cells);
        free(sheet->name);
    }
    free(document->sheets);
    memset(document, 0, sizeof(*document));
}
