/* Spreadsheet text writers (CSV and TSV).
 *
 * A worksheet is a sparse cell list, so the writer sorts one sheet's cells by
 * (row, column) and emits delimiters for the gaps instead of expanding the
 * grid. Only the first worksheet is written: comma- and tab-separated text has
 * no sheet concept, and the report says how many sheets were skipped.
 */

#include "quirewise/spreadsheet.h"

#include "spreadsheet_document.h"

#include "../archive/zip_native.h"

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


static int output_text(SpreadsheetOutput *out, const char *text)
{
    return text && text[0] ? output_append(out, text, strlen(text)) : 1;
}

static int output_escape_xml(SpreadsheetOutput *out, const uint8_t *text,
                             size_t size)
{
    size_t index;
    size_t start = 0u;
    for (index = 0u; index < size; ++index) {
        const char *replacement = NULL;
        switch (text[index]) {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        case '"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        default: break;
        }
        if (!replacement) continue;
        if (!output_append(out, text + start, index - start) ||
            !output_text(out, replacement))
            return 0;
        start = index + 1u;
    }
    return output_append(out, text + start, size - start);
}

static int xlsx_append_column(char *buffer, size_t capacity, size_t column)
{
    char reversed[8];
    size_t length = 0u;
    size_t index;
    size_t value = column + 1u;
    while (value && length < sizeof(reversed)) {
        --value;
        reversed[length++] = (char)('A' + (value % 26u));
        value /= 26u;
    }
    if (length + 1u > capacity) return 0;
    for (index = 0u; index < length; ++index)
        buffer[index] = reversed[length - 1u - index];
    buffer[length] = '\0';
    return 1;
}

static int xlsx_append_cell(SpreadsheetOutput *out, const CellRef *ref,
                            const QwSpreadsheetCell *cell)
{
    char column[8];
    char reference[24];
    char number[64];
    int length;
    if (!xlsx_append_column(column, sizeof(column), ref->column)) return 0;
    length = snprintf(reference, sizeof(reference), "%s%zu", column,
                      ref->row + 1u);
    if (length <= 0 || (size_t)length >= sizeof(reference)) return 0;
    if (!output_text(out, "<c r=\"") || !output_text(out, reference)) return 0;
    if (cell->value_kind == QW_SPREADSHEET_VALUE_NUMBER) {
        size_t copy = cell->value_size < sizeof(number) - 1u
                          ? cell->value_size
                          : sizeof(number) - 1u;
        memcpy(number, cell->value_utf8, copy);
        number[copy] = '\0';
        return output_text(out, "\"><v>") && output_text(out, number) &&
               output_text(out, "</v></c>");
    }
    if (cell->value_kind == QW_SPREADSHEET_VALUE_BOOLEAN) {
        int truth = cell->value_size && cell->value_utf8[0] == '1';
        return output_text(out, "\" t=\"b\"><v>") &&
               output_text(out, truth ? "1" : "0") &&
               output_text(out, "</v></c>");
    }
    if (cell->value_kind == QW_SPREADSHEET_VALUE_EMPTY) return 1;
    return output_text(out, "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">") &&
           output_escape_xml(out, cell->value_utf8, cell->value_size) &&
           output_text(out, "</t></is></c>");
}

static int xlsx_build_sheet(const QwSpreadsheetSheet *sheet,
                            SpreadsheetOutput *out)
{
    CellRef *refs = NULL;
    size_t ref_count = sheet->cell_count;
    size_t index;
    if (!output_text(out,
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/"
            "spreadsheetml/2006/main\"><sheetData>")) return 0;
    if (ref_count) {
        refs = (CellRef *)malloc(ref_count * sizeof(*refs));
        if (!refs) return 0;
        for (index = 0u; index < ref_count; ++index) {
            refs[index].row = sheet->cells[index].row;
            refs[index].column = sheet->cells[index].column;
            refs[index].index = index;
        }
        qsort(refs, ref_count, sizeof(*refs), cell_ref_compare);
        for (index = 0u; index < ref_count; ++index) {
            const QwSpreadsheetCell *cell = &sheet->cells[refs[index].index];
            if (cell->value_kind == QW_SPREADSHEET_VALUE_EMPTY) continue;
            if (index == 0u || refs[index].row != refs[index - 1u].row) {
                char row[32];
                int length = snprintf(row, sizeof(row), "<row r=\"%zu\">",
                                      refs[index].row + 1u);
                if (length <= 0 || (size_t)length >= sizeof(row) ||
                    !output_text(out, row)) {
                    free(refs);
                    return 0;
                }
            }
            if (!xlsx_append_cell(out, &refs[index], cell)) {
                free(refs);
                return 0;
            }
            if (index + 1u == ref_count ||
                refs[index + 1u].row != refs[index].row) {
                if (!output_text(out, "</row>")) {
                    free(refs);
                    return 0;
                }
            }
        }
    }
    free(refs);
    if (!output_text(out, "</sheetData>")) return 0;
    if (sheet->merge_count) {
        char header[64];
        size_t merge_index;
        int length = snprintf(header, sizeof(header),
                              "<mergeCells count=\"%zu\">", sheet->merge_count);
        if (length <= 0 || (size_t)length >= sizeof(header) ||
            !output_text(out, header))
            return 0;
        for (merge_index = 0u; merge_index < sheet->merge_count;
             ++merge_index) {
            char first[8];
            char last[8];
            char range[32];
            if (!xlsx_append_column(first, sizeof(first),
                                    sheet->merges[merge_index].column1) ||
                !xlsx_append_column(last, sizeof(last),
                                    sheet->merges[merge_index].column2))
                return 0;
            length = snprintf(range, sizeof(range), "%s%zu:%s%zu", first,
                              sheet->merges[merge_index].row1 + 1u, last,
                              sheet->merges[merge_index].row2 + 1u);
            if (length <= 0 || (size_t)length >= sizeof(range) ||
                !output_text(out, "<mergeCell ref=\"") ||
                !output_text(out, range) ||
                !output_text(out, "\"/>"))
                return 0;
        }
        if (!output_text(out, "</mergeCells>")) return 0;
    }
    return output_text(out, "</worksheet>");
}

static QwSpreadsheetStatus xlsx_write_package(const QwSpreadsheetDocument *document,
                                              uint8_t **out_data,
                                              size_t *out_size,
                                              QwConvertReport *report)
{
    SpreadsheetOutput sheet_buffers[64];
    size_t sheet_count = document->sheet_count;
    size_t index;
    QwZipWriter writer;
    int zip_ok = 0;
    if (sheet_count > sizeof(sheet_buffers) / sizeof(sheet_buffers[0]))
        sheet_count = sizeof(sheet_buffers) / sizeof(sheet_buffers[0]);
    memset(sheet_buffers, 0, sizeof(sheet_buffers));
    if (!qw_zip_writer_init_heap(&writer, 0u, 0u))
        return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
    zip_ok = 1;
    for (index = 0u; index < sheet_count; ++index) {
        if (!xlsx_build_sheet(&document->sheets[index], &sheet_buffers[index])) {
            qw_zip_writer_end(&writer);
            return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
        }
    }
    if (!zip_ok) { qw_zip_writer_end(&writer); return QW_SPREADSHEET_ERR_OUT_OF_MEMORY; }
    {
        SpreadsheetOutput content_types;
        SpreadsheetOutput root_rels;
        SpreadsheetOutput workbook;
        SpreadsheetOutput workbook_rels;
        memset(&content_types, 0, sizeof(content_types));
        memset(&root_rels, 0, sizeof(root_rels));
        memset(&workbook, 0, sizeof(workbook));
        memset(&workbook_rels, 0, sizeof(workbook_rels));
        if (!output_text(&content_types,
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/"
                "2006/content-types\"><Default Extension=\"rels\" "
                "ContentType=\"application/vnd.openxmlformats-package."
                "relationships+xml\"/><Default Extension=\"xml\" "
                "ContentType=\"application/xml\"/><Override "
                "PartName=\"/xl/workbook.xml\" ContentType=\"application/"
                "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>") ||
            !output_text(&root_rels,
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                "package/2006/relationships\"><Relationship Id=\"rId1\" "
                "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                "relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
                "</Relationships>") ||
            !output_text(&workbook,
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<workbook xmlns=\"http://schemas.openxmlformats.org/"
                "spreadsheetml/2006/main\" xmlns:r=\"http://schemas."
                "openxmlformats.org/officeDocument/2006/relationships\">"
                "<sheets>") ||
            !output_text(&workbook_rels,
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                "package/2006/relationships\">")) {
            qw_zip_writer_end(&writer);
            return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
        }
        for (index = 0u; index < sheet_count; ++index) {
            char path[128];
            char relationship[320];
            char item[256];
            int length = snprintf(path, sizeof(path),
                                  "xl/worksheets/sheet%lu.xml",
                                  (unsigned long)(index + 1u));
            if (length <= 0 || (size_t)length >= sizeof(path)) {
                qw_zip_writer_end(&writer);
                return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
            }
            length = snprintf(item, sizeof(item),
                              "<Override PartName=\"/%s\" ContentType=\"application/"
                              "vnd.openxmlformats-officedocument.spreadsheetml."
                              "worksheet+xml\"/>", path);
            if (length <= 0 || (size_t)length >= sizeof(item) ||
                !output_text(&content_types, item)) {
                qw_zip_writer_end(&writer);
                return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
            }
            length = snprintf(relationship, sizeof(relationship),
                              "<Relationship Id=\"rId%lu\" Type=\"http://schemas."
                              "openxmlformats.org/officeDocument/2006/"
                              "relationships/worksheet\" Target=\"worksheets/"
                              "sheet%lu.xml\"/>", (unsigned long)(index + 1u),
                              (unsigned long)(index + 1u));
            if (length <= 0 || (size_t)length >= sizeof(relationship) ||
                !output_text(&workbook_rels, relationship)) {
                qw_zip_writer_end(&writer);
                return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
            }
            {
                const char *name = qw_spreadsheet_sheet_name(
                    &document->sheets[index], NULL);
                size_t name_size = 0u;
                (void)qw_spreadsheet_sheet_name(&document->sheets[index],
                                                &name_size);
                if (name_size > 31u) name_size = 31u;
                if (!output_text(&workbook, "<sheet name=\"") ||
                    !output_escape_xml(&workbook, (const uint8_t *)name,
                                       name_size) ||
                    !output_text(&workbook, "\" sheetId=\"")) {
                    qw_zip_writer_end(&writer);
                    return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
                }
                length = snprintf(item, sizeof(item), "%lu\" r:id=\"rId%lu\"/>",
                                  (unsigned long)(index + 1u),
                                  (unsigned long)(index + 1u));
                if (length <= 0 || (size_t)length >= sizeof(item) ||
                    !output_text(&workbook, item)) {
                    qw_zip_writer_end(&writer);
                    return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
                }
            }
        }
        if (!output_text(&workbook, "</sheets></workbook>") ||
            !output_text(&workbook_rels, "</Relationships>") ||
            !output_text(&content_types, "</Types>") ||
            !qw_zip_writer_add_mem(&writer, "[Content_Types].xml",
                                   content_types.data, content_types.size,
                                   QW_ZIP_NO_COMPRESSION) ||
            !qw_zip_writer_add_mem(&writer, "_rels/.rels", root_rels.data,
                                   root_rels.size, QW_ZIP_NO_COMPRESSION) ||
            !qw_zip_writer_add_mem(&writer, "xl/workbook.xml", workbook.data,
                                   workbook.size, QW_ZIP_NO_COMPRESSION) ||
            !qw_zip_writer_add_mem(&writer, "xl/_rels/workbook.xml.rels",
                                   workbook_rels.data, workbook_rels.size,
                                   QW_ZIP_NO_COMPRESSION)) {
            qw_zip_writer_end(&writer);
            return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
        }
        for (index = 0u; index < sheet_count; ++index) {
            char path[128];
            int length = snprintf(path, sizeof(path),
                                  "xl/worksheets/sheet%lu.xml",
                                  (unsigned long)(index + 1u));
            if (length <= 0 || (size_t)length >= sizeof(path) ||
                !qw_zip_writer_add_mem(&writer, path, sheet_buffers[index].data,
                                       sheet_buffers[index].size,
                                       QW_ZIP_NO_COMPRESSION)) {
                qw_zip_writer_end(&writer);
                return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
            }
        }
        free(content_types.data);
        free(root_rels.data);
        free(workbook.data);
        free(workbook_rels.data);
    }
    for (index = 0u; index < sheet_count; ++index) free(sheet_buffers[index].data);
    {
        void *archive = NULL;
        size_t archive_size = 0u;
        if (!qw_zip_writer_finalize_heap_archive(&writer, &archive,
                                                 &archive_size) ||
            !archive) {
            qw_zip_writer_end(&writer);
            return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
        }
        qw_zip_writer_end(&writer);
        *out_data = (uint8_t *)archive;
        *out_size = archive_size;
    }
    if (report) {
        report->format = document->input_format;
        report->output_format = QW_FORMAT_XLSX;
        report->output_size = *out_size;
        snprintf(report->note, sizeof(report->note),
                 "minimal SpreadsheetML package with %zu worksheet(s); "
                 "inline strings and numeric values only", sheet_count);
    }
    return QW_SPREADSHEET_OK;
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
    size_t selected = 0u;
    int row_open = 0;
    uint8_t delimiter;

    if (!document || !out_data || !out_size)
        return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    *out_data = NULL;
    *out_size = 0u;
    if (output_format == QW_FORMAT_XLSX)
        return xlsx_write_package(document, out_data, out_size, report);
    if (output_format == QW_FORMAT_CSV) delimiter = (uint8_t)',';
    else if (output_format == QW_FORMAT_TSV) delimiter = (uint8_t)'\t';
    else return QW_SPREADSHEET_ERR_UNSUPPORTED;
    if (!document->sheet_count) return QW_SPREADSHEET_ERR_FORMAT;

    {
        selected = options ? (size_t)options->sheet_index : 0u;
        if (selected >= document->sheet_count) {
            if (report)
                snprintf(report->note, sizeof(report->note),
                         "worksheet index %zu is outside the %zu sheet(s)",
                         selected, document->sheet_count);
            return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
        }
        sheet = &document->sheets[selected];
    }
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
                       "worksheet %zu of %zu written as %s-separated text; "
                       "other sheets are not representable and skipped",
                       selected + 1u, document->sheet_count,
                       output_format == QW_FORMAT_CSV ? "comma" : "tab");
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
