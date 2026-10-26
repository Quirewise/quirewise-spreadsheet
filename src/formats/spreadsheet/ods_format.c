#include "format_registry.h"
#include "../../core/xml_parser.h"
#include "../archive/zip_native.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ODS_FLAT_PROBE_BYTES = 8192,
    ODS_MAX_XML_PART_BYTES = 32 * 1024 * 1024,
    ODS_MAX_TOTAL_XML_BYTES = 128 * 1024 * 1024,
    ODS_MAX_FLAT_XML_BYTES = 64 * 1024 * 1024,
    ODS_MAX_HTML_BYTES = 64 * 1024 * 1024,
    ODS_MAX_SHEETS = 256,
    ODS_MAX_ROWS = 1000000,
    ODS_MAX_COLUMNS = 16384,
    ODS_MAX_CELLS = 1000000,
    ODS_MAX_XML_NODES = 4000000,
    ODS_MAX_XML_DEPTH = 128
};

static int ods_bytes_contain(const uint8_t *data, size_t size,
                             const char *pattern, size_t pattern_size)
{
    size_t i;
    if (!data || !pattern || !pattern_size || pattern_size > size) return 0;
    for (i = 0u; i <= size - pattern_size; ++i) {
        if (memcmp(data + i, pattern, pattern_size) == 0) return 1;
    }
    return 0;
}

static int ods_flat_probe(const void *data, size_t size)
{
    static const char office_ns[] =
        "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
    static const char mime_attr[] = "office:mimetype";
    static const char spreadsheet_mime[] =
        "application/vnd.oasis.opendocument.spreadsheet";
    static const char template_mime[] =
        "application/vnd.oasis.opendocument.spreadsheet-template";
    const uint8_t *bytes = (const uint8_t *)data;
    size_t probe_size = size < ODS_FLAT_PROBE_BYTES ? size : ODS_FLAT_PROBE_BYTES;
    size_t i;
    size_t root_end = 0u;
    if (!bytes || !size || size > ODS_MAX_FLAT_XML_BYTES ||
        (size >= 4u && bytes[0] == 'P' && bytes[1] == 'K' &&
         ((bytes[2] == 3u && bytes[3] == 4u) ||
          (bytes[2] == 5u && bytes[3] == 6u) ||
          (bytes[2] == 7u && bytes[3] == 8u)))) return 0;
    for (i = 0u; i < probe_size; ++i) {
        if (bytes[i] == '<' && i + 1u < probe_size &&
            bytes[i + 1u] != '?' && bytes[i + 1u] != '!' &&
            bytes[i + 1u] != '/') {
            size_t j;
            for (j = i + 1u; j < probe_size; ++j) {
                if (bytes[j] == '>') {
                    root_end = j + 1u;
                    break;
                }
            }
            break;
        }
    }
    if (!root_end) return 0;
    return ods_bytes_contain(bytes, root_end, office_ns, sizeof(office_ns) - 1u) &&
           ods_bytes_contain(bytes, root_end, mime_attr, sizeof(mime_attr) - 1u) &&
           (ods_bytes_contain(bytes, root_end, spreadsheet_mime,
                              sizeof(spreadsheet_mime) - 1u) ||
            ods_bytes_contain(bytes, root_end, template_mime,
                              sizeof(template_mime) - 1u));
}

#include "../../core/html_output.h"
#include "../../core/spreadsheet_document.h"
#include "../../core/text_buffer.h"

typedef struct OdsReader {
    QwZipArchive *zip;
    QwTextBuffer html;
    QwSpreadsheetDocument *workbook;
    size_t current_sheet_index;
    size_t total_xml_bytes;
    size_t visited_nodes;
    size_t row_count;
    size_t current_sheet_row_count;
    size_t cell_count;
    size_t sheet_count;
    int capture_html;
    int typed_model_unsupported;
    int typed_model_resource_limit;
} OdsReader;

static void ods_set_error(QwConvertReport *report, const char *message)
{
    if (report && message)
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
}

static int ods_html_append(OdsReader *reader, const char *text, size_t size)
{
    return reader && (!reader->capture_html ||
        qw_html_output_append(&reader->html, text, size,
                              ODS_MAX_HTML_BYTES, NULL));
}

static int ods_html_literal(OdsReader *reader, const char *text)
{
    return text && ods_html_append(reader, text, strlen(text));
}

static int ods_html_escape(OdsReader *reader, const char *text)
{
    return reader && (!reader->capture_html ||
        qw_html_output_escape(&reader->html, text, ODS_MAX_HTML_BYTES, NULL));
}

static const char *ods_local_name(const char *qualified_name)
{
    const char *colon;
    if (!qualified_name) return "";
    colon = strrchr(qualified_name, ':');
    return colon ? colon + 1u : qualified_name;
}

static int ods_is_tag(QwXmlNode *node, const char *local_name)
{
    const char *tag = qw_xml_tag(node);
    return tag && strcmp(ods_local_name(tag), local_name) == 0;
}

static void *ods_extract_xml(OdsReader *reader, const char *name,
                             size_t *out_size)
{
    size_t index;
    size_t extracted_size = 0u;
    QwZipEntryInfo stat;
    void *contents;
    if (out_size) *out_size = 0u;
    if (!reader || !reader->zip || !out_size) return NULL;
    index = qw_zip_reader_locate_file(reader->zip, name, NULL,
                                      QW_ZIP_CASE_SENSITIVE);
    if (index == (size_t)-1 ||
        !qw_zip_reader_file_stat(reader->zip, index, &stat) ||
        !stat.uncompressed_size ||
        stat.uncompressed_size > ODS_MAX_XML_PART_BYTES ||
        stat.uncompressed_size > ODS_MAX_TOTAL_XML_BYTES -
            reader->total_xml_bytes ||
        !qw_zip_reader_is_file_supported(reader->zip, index)) return NULL;
    contents = qw_zip_reader_extract_to_heap(reader->zip, index,
                                              &extracted_size, 0u);
    if (!contents || extracted_size != stat.uncompressed_size ||
        extracted_size > ODS_MAX_XML_PART_BYTES ||
        extracted_size > ODS_MAX_TOTAL_XML_BYTES - reader->total_xml_bytes) {
        free(contents);
        return NULL;
    }
    reader->total_xml_bytes += extracted_size;
    *out_size = extracted_size;
    return contents;
}

static QwXmlNode *ods_parse_xml(const uint8_t *data, size_t size)
{
    return qw_xml_parse(data, size, NULL);
}

static QwXmlNode *ods_find_descendant(OdsReader *reader, QwXmlNode *node,
                                   const char *local_name, size_t depth)
{
    QwXmlNode *found;
    if (!reader || depth > ODS_MAX_XML_DEPTH) return NULL;
    for (; node; node = qw_xml_next(node)) {
        if (++reader->visited_nodes > ODS_MAX_XML_NODES) return NULL;
        if (ods_is_tag(node, local_name)) return node;
        found = ods_find_descendant(reader, qw_xml_down(node), local_name,
                                    depth + 1u);
        if (found || reader->visited_nodes > ODS_MAX_XML_NODES) return found;
    }
    return NULL;
}

static int ods_append_text_nodes(OdsReader *reader, QwXmlNode *node, size_t depth)
{
    if (!reader || depth > ODS_MAX_XML_DEPTH) return 0;
    for (; node; node = qw_xml_next(node)) {
        const char *text;
        if (++reader->visited_nodes > ODS_MAX_XML_NODES) return 0;
        text = qw_xml_text(node);
        if (text) {
            if (!ods_html_escape(reader, text)) return 0;
        } else if (!ods_append_text_nodes(reader, qw_xml_down(node), depth + 1u)) {
            return 0;
        }
    }
    return 1;
}

static int ods_parse_repeat(const char *text, size_t max_value, size_t *out_value)
{
    unsigned long value;
    char *end = NULL;
    if (!out_value) return 0;
    *out_value = 1u;
    if (!text || !*text) return 1;
    if (*text < '0' || *text > '9') return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end || value == 0u || value > max_value) return 0;
    *out_value = (size_t)value;
    return 1;
}

static int ods_append_paragraphs(OdsReader *reader, QwXmlNode *cell)
{
    QwXmlNode *child;
    int found = 0;
    for (child = qw_xml_down(cell); child; child = qw_xml_next(child)) {
        if (ods_is_tag(child, "p") || ods_is_tag(child, "h")) {
            if ((found && !ods_html_literal(reader, "<br>")) ||
                !ods_append_text_nodes(reader, qw_xml_down(child), 0u)) return -1;
            found = 1;
        }
    }
    return found ? 1 : 0;
}

static int ods_append_cell_value(OdsReader *reader, QwXmlNode *cell)
{
    const char *type = qw_xml_att(cell, "office:value-type");
    const char *value;
    int paragraph_result;
    if (ods_is_tag(cell, "covered-table-cell")) return 1;
    if (!type || strcmp(type, "string") == 0) {
        paragraph_result = ods_append_paragraphs(reader, cell);
        if (paragraph_result > 0) return 1;
        if (paragraph_result < 0) return 0;
        value = qw_xml_att(cell, "office:string-value");
        return value ? ods_html_escape(reader, value) : 1;
    }
    if (strcmp(type, "boolean") == 0) {
        value = qw_xml_att(cell, "office:boolean-value");
        if (!value) return 1;
        return ods_html_literal(reader,
            (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)
                ? "TRUE" : "FALSE");
    }
    if (strcmp(type, "date") == 0) value = qw_xml_att(cell, "office:date-value");
    else if (strcmp(type, "time") == 0) value = qw_xml_att(cell, "office:time-value");
    else value = qw_xml_att(cell, "office:value");
    return value ? ods_html_escape(reader, value) : 1;
}

static int ods_collect_text_nodes(OdsReader *reader, QwXmlNode *node,
                                 QwTextBuffer *text, size_t depth)
{
    if (!reader || !text || depth > ODS_MAX_XML_DEPTH) return 0;
    for (; node; node = qw_xml_next(node)) {
        const char *value;
        if (++reader->visited_nodes > ODS_MAX_XML_NODES) return 0;
        if (ods_is_tag(node, "s") || ods_is_tag(node, "tab") ||
            ods_is_tag(node, "line-break")) {
            reader->typed_model_unsupported = 1;
            return 0;
        }
        value = qw_xml_text(node);
        if (value) {
            if (!qw_text_buffer_append(text, value, strlen(value))) return 0;
        } else if (!ods_collect_text_nodes(reader, qw_xml_down(node), text,
                                          depth + 1u)) {
            return 0;
        }
    }
    return 1;
}

static int ods_collect_cell_text(OdsReader *reader, QwXmlNode *cell,
                                 QwTextBuffer *text, int *out_found)
{
    QwXmlNode *child;
    int found = 0;
    if (!reader || !cell || !text || !out_found) return 0;
    for (child = qw_xml_down(cell); child; child = qw_xml_next(child)) {
        if (ods_is_tag(child, "p") || ods_is_tag(child, "h")) {
            if ((found && !qw_text_buffer_append(text, "\n", 1u)) ||
                !ods_collect_text_nodes(reader, qw_xml_down(child), text, 0u))
                return 0;
            found = 1;
        }
    }
    *out_found = found;
    return 1;
}

static int ods_read_cell_model(OdsReader *reader, QwXmlNode *cell,
                               size_t row, size_t column)
{
    const char *type;
    const char *raw = NULL;
    const char *formula;
    QwSpreadsheetValueKind kind = QW_SPREADSHEET_VALUE_EMPTY;
    QwTextBuffer text = {0};
    const uint8_t *value = NULL;
    size_t value_size = 0u;
    int has_value = 0;
    int has_paragraphs = 0;
    int added;
    if (!reader || !reader->workbook || !cell ||
        ods_is_tag(cell, "covered-table-cell")) return 1;
    type = qw_xml_att(cell, "office:value-type");
    formula = qw_xml_att(cell, "table:formula");
    text.limit = (size_t)ODS_MAX_XML_PART_BYTES + 1u;
    if (formula && formula[0] && strncmp(formula, "of:=", 4u) != 0) {
        reader->typed_model_unsupported = 1;
        goto cleanup;
    }
    if (type && strcmp(type, "string") == 0) {
        raw = qw_xml_att(cell, "office:string-value");
        if (raw) {
            value = (const uint8_t *)raw;
            value_size = strlen(raw);
            has_value = 1;
        } else {
            if (!ods_collect_cell_text(reader, cell, &text, &has_paragraphs))
                goto unsupported_or_error;
            if (has_paragraphs) {
                value = (const uint8_t *)text.data;
                value_size = text.size;
                has_value = 1;
            }
        }
        kind = QW_SPREADSHEET_VALUE_STRING;
    } else if (!type) {
        if (!ods_collect_cell_text(reader, cell, &text, &has_paragraphs))
            goto unsupported_or_error;
        if (has_paragraphs) {
            value = (const uint8_t *)text.data;
            value_size = text.size;
            has_value = 1;
            kind = QW_SPREADSHEET_VALUE_STRING;
        }
    } else if (strcmp(type, "float") == 0 || strcmp(type, "percentage") == 0 ||
               strcmp(type, "currency") == 0) {
        raw = qw_xml_att(cell, "office:value");
        if (raw) {
            value = (const uint8_t *)raw;
            value_size = strlen(raw);
            has_value = 1;
            kind = QW_SPREADSHEET_VALUE_NUMBER;
        }
    } else if (strcmp(type, "boolean") == 0) {
        raw = qw_xml_att(cell, "office:boolean-value");
        if (raw) {
            if (strcmp(raw, "true") == 0 || strcmp(raw, "1") == 0) raw = "1";
            else if (strcmp(raw, "false") == 0 || strcmp(raw, "0") == 0) raw = "0";
            else goto unsupported_or_error;
            value = (const uint8_t *)raw;
            value_size = 1u;
            has_value = 1;
            kind = QW_SPREADSHEET_VALUE_BOOLEAN;
        }
    } else if (strcmp(type, "date") == 0) {
        raw = qw_xml_att(cell, "office:date-value");
        if (raw) {
            value = (const uint8_t *)raw;
            value_size = strlen(raw);
            has_value = 1;
            kind = QW_SPREADSHEET_VALUE_DATE_TIME;
        }
    } else if (strcmp(type, "time") == 0) {
        reader->typed_model_unsupported = 1;
        goto cleanup;
    } else {
        reader->typed_model_unsupported = 1;
        goto cleanup;
    }
    if (!has_value && (!formula || !formula[0])) goto cleanup;
    added = qw_spreadsheet_document_add_cell(reader->workbook,
        reader->current_sheet_index, row, column, kind, value, value_size,
        formula ? (const uint8_t *)formula : NULL,
        formula ? strlen(formula) : 0u);
    if (!added) {
        reader->typed_model_resource_limit = 1;
        goto cleanup;
    }
    free(text.data);
    return 1;

unsupported_or_error:
    if (!reader->typed_model_unsupported) reader->typed_model_resource_limit = 1;
cleanup:
    free(text.data);
    return 1;
}

static int ods_render_row(OdsReader *reader, QwXmlNode *row)
{
    size_t row_repeat;
    size_t row_index;
    const char *repeat_text = qw_xml_att(row, "table:number-rows-repeated");
    if (!ods_parse_repeat(repeat_text, ODS_MAX_ROWS - reader->row_count,
                          &row_repeat)) return 0;
    if (reader->workbook && row_repeat > 1u) {
        QwXmlNode *cell;
        for (cell = qw_xml_down(row); cell; cell = qw_xml_next(cell)) {
            const char *formula = qw_xml_att(cell, "table:formula");
            if (formula && formula[0]) {
                reader->typed_model_unsupported = 1;
                break;
            }
        }
    }
    for (row_index = 0u; row_index < row_repeat; ++row_index) {
        QwXmlNode *cell;
        size_t column_count = 0u;
        size_t model_row = reader->current_sheet_row_count++;
        if (reader->row_count >= ODS_MAX_ROWS ||
            !ods_html_literal(reader, "<tr>")) return 0;
        ++reader->row_count;
        for (cell = qw_xml_down(row); cell; cell = qw_xml_next(cell)) {
            size_t cell_repeat;
            size_t repeat_index;
            const char *repeat = qw_xml_att(cell,
                                             "table:number-columns-repeated");
            if (!ods_is_tag(cell, "table-cell") &&
                !ods_is_tag(cell, "covered-table-cell")) continue;
            if (++reader->visited_nodes > ODS_MAX_XML_NODES ||
                !ods_parse_repeat(repeat, ODS_MAX_COLUMNS - column_count,
                                  &cell_repeat) ||
                cell_repeat > ODS_MAX_CELLS - reader->cell_count) return 0;
            if (reader->workbook && cell_repeat > 1u) {
                const char *formula = qw_xml_att(cell, "table:formula");
                if (formula && formula[0])
                    reader->typed_model_unsupported = 1;
            }
            for (repeat_index = 0u; repeat_index < cell_repeat; ++repeat_index) {
                if (column_count >= ODS_MAX_COLUMNS ||
                    reader->cell_count >= ODS_MAX_CELLS) return 0;
                if (reader->capture_html &&
                    (!ods_html_literal(reader, "<td>") ||
                     !ods_append_cell_value(reader, cell) ||
                     !ods_html_literal(reader, "</td>"))) return 0;
                if (!ods_read_cell_model(reader, cell, model_row,
                                         column_count)) return 0;
                ++column_count;
                ++reader->cell_count;
            }
        }
        if (!ods_html_literal(reader, "</tr>")) return 0;
    }
    return 1;
}

static int ods_render_rows(OdsReader *reader, QwXmlNode *node, size_t depth)
{
    if (depth > ODS_MAX_XML_DEPTH) return 0;
    for (; node; node = qw_xml_next(node)) {
        if (++reader->visited_nodes > ODS_MAX_XML_NODES) return 0;
        if (ods_is_tag(node, "table-row")) {
            if (!ods_render_row(reader, node)) return 0;
        } else if (ods_is_tag(node, "table-header-rows") ||
                   ods_is_tag(node, "table-rows") ||
                   ods_is_tag(node, "table-row-group")) {
            if (!ods_render_rows(reader, qw_xml_down(node), depth + 1u)) return 0;
        }
    }
    return 1;
}

static int ods_render_sheet(OdsReader *reader, QwXmlNode *sheet)
{
    const char *name = qw_xml_att(sheet, "table:name");
    char generated_name[32];
    int generated_name_size;
    if (++reader->sheet_count > ODS_MAX_SHEETS ||
        !ods_html_literal(reader, "<section><h2>") ||
        !ods_html_escape(reader, name ? name : "Sheet") ||
        !ods_html_literal(reader, "</h2><table><tbody>")) return 0;
    reader->current_sheet_row_count = 0u;
    if (reader->workbook) {
        if (!name || !name[0]) {
            generated_name_size = snprintf(generated_name,
                sizeof(generated_name), "Sheet%zu", reader->sheet_count);
            if (generated_name_size < 0 ||
                (size_t)generated_name_size >= sizeof(generated_name)) {
                reader->typed_model_resource_limit = 1;
                return 0;
            }
            name = generated_name;
        }
        if (!qw_spreadsheet_document_add_sheet(reader->workbook,
                (const uint8_t *)name, strlen(name),
                &reader->current_sheet_index)) {
            reader->typed_model_resource_limit = 1;
            return 0;
        }
    }
    if (!ods_render_rows(reader, qw_xml_down(sheet), 0u)) return 0;
    return ods_html_literal(reader, "</tbody></table></section>");
}

static int ods_build_layout_html(const void *data, size_t size,
                                 QwInputFormat input_format,
                                 QwSpreadsheetDocument *workbook,
                                 int capture_html,
                                 OdsReader *reader,
                                 QwConvertReport *report)
{
    QwZipArchive zip;
    void *content_data = NULL;
    size_t content_size = 0u;
    QwXmlNode *xml = NULL;
    QwXmlNode *spreadsheet = NULL;
    QwXmlNode *sheet;
    int archive_open = 0;
    int rc = QW_STATUS_ERR_FORMAT;
    if (!reader) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(&zip, 0, sizeof(zip));
    memset(reader, 0, sizeof(*reader));
    reader->workbook = workbook;
    reader->capture_html = capture_html;
    if (!data || !size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (input_format == QW_FORMAT_FODS) {
        if (size > ODS_MAX_FLAT_XML_BYTES || !ods_flat_probe(data, size)) {
            ods_set_error(report, "FODS is not a recognized flat spreadsheet XML file");
            return QW_STATUS_ERR_FORMAT;
        }
        content_data = malloc(size);
        if (!content_data) return QW_STATUS_ERR_OUT_OF_MEMORY;
        memcpy(content_data, data, size);
        content_size = size;
    } else {
#ifdef QUIREWISE_STANDALONE_SPREADSHEET
        if (!qw_zip_reader_init_mem_with_profile(
                &zip, data, size, 0u,
                QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE)) {
#else
        if (!qw_zip_reader_init_mem_with_profile(
                &zip, data, size, 0u,
                qw_format_resource_profile_get(input_format))) {
#endif
            ods_set_error(report, "OpenDocument spreadsheet package could not be opened as ZIP");
            return QW_STATUS_ERR_FORMAT;
        }
        archive_open = 1;
        reader->zip = &zip;
        content_data = ods_extract_xml(reader, "content.xml", &content_size);
        if (!content_data) {
            ods_set_error(report, "OpenDocument spreadsheet content.xml is missing, unsupported, or too large");
            goto cleanup;
        }
    }

    xml = ods_parse_xml((const uint8_t *)content_data, content_size);
    if (!xml) {
        ods_set_error(report,
                      "OpenDocument spreadsheet content XML could not be parsed");
        rc = QW_STATUS_ERR_DECODE;
        goto cleanup;
    }
    spreadsheet = ods_find_descendant(reader, qw_xml_root(xml),
                                      "spreadsheet", 0u);
    if (!spreadsheet) {
        ods_set_error(report, "OpenDocument content has no spreadsheet body");
        rc = QW_STATUS_ERR_DECODE;
        goto cleanup;
    }
    if (reader->capture_html)
        reader->html.limit = (size_t)ODS_MAX_HTML_BYTES + 1u;
    if (!ods_html_literal(reader,
            "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
            "@page{size:landscape;margin:12mm}body{font-family:sans-serif;"
            "font-size:10pt;color:#111}h2{font-size:14pt;margin:18pt 0 6pt}"
            "table{border-collapse:collapse;width:100%;table-layout:auto}"
            "td{border:1px solid #888;padding:3pt;vertical-align:top;"
            "overflow-wrap:anywhere}section{break-after:page}"
            "</style></head><body>")) {
        ods_set_error(report,
                      "OpenDocument spreadsheet parse: HTML header exceeds output limits");
        rc = QW_STATUS_ERR_DECODE;
        goto cleanup;
    }
    for (sheet = qw_xml_down(spreadsheet); sheet; sheet = qw_xml_next(sheet)) {
        if (ods_is_tag(sheet, "table") && !ods_render_sheet(reader, sheet)) {
            ods_set_error(report,
                "OpenDocument spreadsheet exceeds conversion resource limits");
            rc = QW_STATUS_ERR_DECODE;
            goto cleanup;
        }
    }
    if (!reader->sheet_count || !ods_html_literal(reader, "</body></html>")) {
        ods_set_error(report,
            "OpenDocument spreadsheet has no sheets or exceeds output limits");
        rc = QW_STATUS_ERR_DECODE;
        goto cleanup;
    }
    rc = QW_STATUS_OK;

cleanup:
    qw_xml_drop(xml);
    free(content_data);
    if (archive_open) qw_zip_reader_end(&zip);
    return rc;
}

static void ods_layout_dispose(void *context)
{
    free(context);
}

static int ods_open_layout(const void *data, size_t size,
                           QwInputFormat input_format,
                           const QwConvertOptions *options,
                           QwLayoutDocument *out_document,
                           QwConvertReport *report)
{
    OdsReader reader;
    int rc;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(out_document, 0, sizeof(*out_document));
    memset(&reader, 0, sizeof(reader));
    if (input_format != QW_FORMAT_ODS && input_format != QW_FORMAT_OTS &&
        input_format != QW_FORMAT_FODS) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = ods_build_layout_html(data, size, input_format, NULL, 1,
                               &reader, report);
    if (rc != QW_STATUS_OK) {
        free(reader.html.data);
        return rc;
    }
    out_document->html_utf8 = (const uint8_t *)reader.html.data;
    out_document->html_size = reader.html.size;
    out_document->context = reader.html.data;
    out_document->dispose = ods_layout_dispose;
    if (report) {
        if (input_format == QW_FORMAT_OTS) {
            (void)snprintf(report->note, sizeof(report->note),
                "OTS template sheets opened as layout HTML; template behavior, styles, formulas, charts, and macros are not preserved");
        } else if (input_format == QW_FORMAT_FODS) {
            (void)snprintf(report->note, sizeof(report->note),
                "FODS flat spreadsheet XML and cached cell values opened as layout HTML; styles, formula calculation, charts, and macros are not preserved");
        } else {
            (void)snprintf(report->note, sizeof(report->note),
                "ODS sheets and cached cell values opened as layout HTML; styles, formula calculation, charts, and macros are not preserved");
        }
    }
    return QW_STATUS_OK;
}

static int ods_open_spreadsheet(const void *data, size_t size,
                                QwInputFormat input_format,
                                const QwConvertOptions *options,
                                QwSpreadsheetDocument *out_workbook,
                                QwConvertReport *report)
{
    OdsReader reader;
    int rc;
    (void)options;
    if (!out_workbook) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (input_format != QW_FORMAT_ODS && input_format != QW_FORMAT_OTS &&
        input_format != QW_FORMAT_FODS) return QW_STATUS_ERR_INVALID_ARGUMENT;
    qw_spreadsheet_document_init(out_workbook, input_format);
    memset(&reader, 0, sizeof(reader));
    rc = ods_build_layout_html(data, size, input_format, out_workbook, 0,
                               &reader, report);
    if (rc == QW_STATUS_OK && reader.typed_model_resource_limit)
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
    if (rc == QW_STATUS_OK && reader.typed_model_unsupported)
        rc = QW_STATUS_ERR_UNSUPPORTED;
    if (rc == QW_STATUS_OK && !qw_spreadsheet_document_validate(out_workbook))
        rc = QW_STATUS_ERR_FORMAT;
    if (rc != QW_STATUS_OK) {
        qw_spreadsheet_document_dispose(out_workbook);
        if (report && rc == QW_STATUS_ERR_UNSUPPORTED)
            ods_set_error(report,
                "OpenDocument workbook uses cell content outside the typed subset");
        return rc;
    }
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
            "%s typed workbook opened with sheet names, scalar cell types, formulas, and cached values; formulas are not recalculated",
            input_format == QW_FORMAT_OTS ? "OTS" :
            input_format == QW_FORMAT_FODS ? "FODS" : "ODS");
    }
    return QW_STATUS_OK;
}

int qw_ods_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_workbook)
{
    return ods_open_spreadsheet(data, size, input_format, NULL, out_workbook,
                                NULL);
}

const QwFormatDescriptor qw_format_ods = {
    .id = QW_FORMAT_ODS,
    .name = "ODS",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE |
                    QW_FORMAT_CAP_ARCHIVE_PROBE,
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = ods_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE,
    .convert_to_pdf = NULL,
    .open_spreadsheet = ods_open_spreadsheet
};

const QwFormatDescriptor qw_format_ots = {
    .id = QW_FORMAT_OTS,
    .name = "OTS",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE |
                    QW_FORMAT_CAP_ARCHIVE_PROBE,
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = ods_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE,
    .convert_to_pdf = NULL,
    .open_spreadsheet = ods_open_spreadsheet
};

const QwFormatDescriptor qw_format_fods = {
    .id = QW_FORMAT_FODS,
    .name = "FODS",
    .capabilities = QW_FORMAT_CAP_MULTI_PAGE,
    .probe = ods_flat_probe,
    .open_memory = NULL,
    .open_layout = ods_open_layout,
    .open_audio = NULL,
    .max_input_bytes = ODS_MAX_FLAT_XML_BYTES,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL,
    .open_spreadsheet = ods_open_spreadsheet
};
