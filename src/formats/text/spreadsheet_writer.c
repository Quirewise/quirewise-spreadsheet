/* Spreadsheet text writers (CSV and TSV).
 *
 * A worksheet is a sparse cell list, so the writer sorts one sheet's cells by
 * (row, column) and emits delimiters for the gaps instead of expanding the
 * grid. Only the first worksheet is written: comma- and tab-separated text has
 * no sheet concept, and the report says how many sheets were skipped.
 */

#include "quirewise/spreadsheet.h"

#include "spreadsheet_document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QW_SPREADSHEET_OUTPUT_LIMIT ((size_t)256u * 1024u * 1024u)

typedef struct SpreadsheetOutput {
    uint8_t *data;
    size_t size;
    size_t capacity;
} SpreadsheetOutput;

typedef struct CellRef {
    size_t row;
    size_t column;
    size_t index;
} CellRef;

static int output_reserve(SpreadsheetOutput *out, size_t additional)
{
    size_t needed;
    size_t capacity;
    uint8_t *grown;

    if (out->size > QW_SPREADSHEET_OUTPUT_LIMIT ||
        additional > QW_SPREADSHEET_OUTPUT_LIMIT - out->size)
        return 0;
    needed = out->size + additional;
    if (needed <= out->capacity) return 1;
    capacity = out->capacity ? out->capacity : 4096u;
    while (capacity < needed) {
        if (capacity > QW_SPREADSHEET_OUTPUT_LIMIT / 2u) {
            capacity = QW_SPREADSHEET_OUTPUT_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < needed) return 0;
    grown = (uint8_t *)realloc(out->data, capacity);
    if (!grown) return 0;
    out->data = grown;
    out->capacity = capacity;
    return 1;
}

static int output_append(SpreadsheetOutput *out, const void *data, size_t size)
{
    if (!size) return 1;
    if (!output_reserve(out, size)) return 0;
    memcpy(out->data + out->size, data, size);
    out->size += size;
    return 1;
}

static int output_byte(SpreadsheetOutput *out, uint8_t value)
{
    return output_append(out, &value, 1u);
}

static int output_field(SpreadsheetOutput *out, const uint8_t *value,
                        size_t size, uint8_t delimiter)
{
    size_t index;
    int needs_quotes = 0;

    for (index = 0u; index < size; ++index) {
        if (value[index] == delimiter || value[index] == '"' ||
            value[index] == '\n' || value[index] == '\r') {
            needs_quotes = 1;
            break;
        }
    }
    if (!needs_quotes) return output_append(out, value, size);
    if (!output_byte(out, '"')) return 0;
    for (index = 0u; index < size; ++index) {
        if (value[index] == '"' && !output_byte(out, '"')) return 0;
        if (!output_byte(out, value[index])) return 0;
    }
    return output_byte(out, '"');
}

static int cell_ref_compare(const void *left, const void *right)
{
    const CellRef *a = (const CellRef *)left;
    const CellRef *b = (const CellRef *)right;

    if (a->row != b->row) return a->row < b->row ? -1 : 1;
    if (a->column != b->column) return a->column < b->column ? -1 : 1;
    return 0;
}

QwSpreadsheetStatus qw_spreadsheet_write_document(
    const QwSpreadsheetDocument *document, QwOutputFormat output_format,
    const QwConvertOptions *options, uint8_t **out_data, size_t *out_size,
    QwConvertReport *report)
{
    const QwSpreadsheetSheet *sheet;
    SpreadsheetOutput out;
    CellRef *refs = NULL;
    size_t ref_count;
    size_t index;
    size_t current_row = 0u;
    size_t current_column = 0u;
    int row_open = 0;
    uint8_t delimiter;

    (void)options;
    if (!document || !out_data || !out_size)
        return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    *out_data = NULL;
    *out_size = 0u;
    if (output_format == QW_FORMAT_CSV) delimiter = (uint8_t)',';
    else if (output_format == QW_FORMAT_TSV) delimiter = (uint8_t)'\t';
    else return QW_SPREADSHEET_ERR_UNSUPPORTED;
    if (!document->sheet_count) return QW_SPREADSHEET_ERR_FORMAT;

    sheet = &document->sheets[0];
    memset(&out, 0, sizeof(out));
    ref_count = sheet->cell_count;
    if (ref_count) {
        refs = (CellRef *)malloc(ref_count * sizeof(*refs));
        if (!refs) return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
        for (index = 0u; index < ref_count; ++index) {
            refs[index].row = sheet->cells[index].row;
            refs[index].column = sheet->cells[index].column;
            refs[index].index = index;
        }
        qsort(refs, ref_count, sizeof(*refs), cell_ref_compare);
    }
    for (index = 0u; index < ref_count; ++index) {
        const QwSpreadsheetCell *cell = &sheet->cells[refs[index].index];

        while (current_row < cell->row) {
            if (!output_byte(&out, '\n')) goto out_of_memory;
            ++current_row;
            current_column = 0u;
            row_open = 0;
        }
        while (current_column < cell->column) {
            if (!output_byte(&out, delimiter)) goto out_of_memory;
            ++current_column;
        }
        if (row_open) {
            if (!output_byte(&out, delimiter)) goto out_of_memory;
        }
        row_open = 1;
        current_column = cell->column + 1u;
        if (cell->value_kind != QW_SPREADSHEET_VALUE_EMPTY &&
            !output_field(&out, cell->value_utf8, cell->value_size,
                          delimiter))
            goto out_of_memory;
    }
    if (row_open && !output_byte(&out, '\n')) goto out_of_memory;
    free(refs);
    if (!out.data) {
        out.data = (uint8_t *)malloc(1u);
        if (!out.data) return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
    }
    *out_data = out.data;
    *out_size = out.size;
    if (report) {
        report->format = document->input_format;
        report->output_format = output_format;
        report->output_size = out.size;
        report->pages_written = 0u;
        (void)snprintf(report->note, sizeof(report->note),
                       output_format == QW_FORMAT_CSV
                           ? "first worksheet written as comma-separated text"
                           : "first worksheet written as tab-separated text");
        if (document->sheet_count > 1u) {
            size_t used = strlen(report->note);
            if (used < sizeof(report->note)) {
                (void)snprintf(report->note + used,
                               sizeof(report->note) - used,
                               "; %zu further sheet(s) not representable in "
                               "text and skipped",
                               document->sheet_count - 1u);
            }
        }
    }
    return QW_SPREADSHEET_OK;

out_of_memory:
    free(refs);
    free(out.data);
    return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
}

void qw_spreadsheet_buffer_free(void *buffer)
{
    free(buffer);
}
