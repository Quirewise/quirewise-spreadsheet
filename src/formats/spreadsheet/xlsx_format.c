#include "format_registry.h"
#include "../../core/html_output.h"
#include "../../core/xml_parser.h"
#include "../archive/zip_native.h"

#include "quirewise/status.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    XLSX_MAX_XML_PART_BYTES = 32 * 1024 * 1024,
    XLSX_MAX_TOTAL_XML_BYTES = 128 * 1024 * 1024,
    XLSX_MAX_HTML_BYTES = 64 * 1024 * 1024,
    XLSX_MAX_SHEETS = 256,
    XLSX_MAX_RELATIONSHIPS = 512,
    XLSX_MAX_SHARED_STRINGS = 250000,
    XLSX_MAX_XML_DEPTH = 128,
    XLSX_MAX_XML_NODES = 1000000,
    XLSX_MAX_CELLS = 1000000,
    XLSX_MAX_ROWS = 1048576,
    XLSX_MAX_COLUMNS = 16384,
    XLSX_MAX_PATH_BYTES = 1024,
    XLSX_MAX_RELATIONSHIP_ID_BYTES = 128
};

typedef struct XlsxRelationship {
    char id[XLSX_MAX_RELATIONSHIP_ID_BYTES];
    char target[XLSX_MAX_PATH_BYTES];
    unsigned kind;
} XlsxRelationship;

enum {
    XLSX_RELATIONSHIP_WORKBOOK = 1u,
    XLSX_RELATIONSHIP_WORKSHEET,
    XLSX_RELATIONSHIP_SHARED_STRINGS
};

typedef struct XlsxReader {
    QwZipArchive *zip;
    QwTextBuffer html;
    QwSpreadsheetDocument *workbook;
    size_t current_sheet_index;
    int capture_html;
    char **shared_strings;
    size_t shared_string_count;
    size_t shared_string_capacity;
    size_t total_xml_bytes;
    size_t visited_nodes;
    size_t cell_count;
    size_t sheet_count;
    int output_limit_hit;
    int typed_model_unsupported;
    int typed_model_resource_limit;
} XlsxReader;

static void xlsx_set_error(QwConvertReport *report, int code,
                           const char *message)
{
    (void)code;
    if (report && message)
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
}

static int xlsx_html_append(XlsxReader *reader, const char *text, size_t size)
{
    return reader && (!reader->capture_html || qw_spreadsheet_html_output_append(
        &reader->html, text, size, XLSX_MAX_HTML_BYTES,
        &reader->output_limit_hit));
}

static int xlsx_html_literal(XlsxReader *reader, const char *text)
{
    return text && xlsx_html_append(reader, text, strlen(text));
}

static int xlsx_html_escape(XlsxReader *reader, const char *text)
{
    return reader && (!reader->capture_html || qw_spreadsheet_html_output_escape(
        &reader->html, text, XLSX_MAX_HTML_BYTES,
        &reader->output_limit_hit));
}

static const char *xlsx_local_name(const char *qualified_name)
{
    const char *colon;
    if (!qualified_name) return "";
    colon = strrchr(qualified_name, ':');
    return colon ? colon + 1u : qualified_name;
}

static int xlsx_is_tag(QwXmlNode *node, const char *local_name)
{
    const char *tag = qw_xml_tag(node);
    return tag && strcmp(xlsx_local_name(tag), local_name) == 0;
}

static int xlsx_copy_bounded(char *destination, size_t capacity,
                             const char *source)
{
    size_t size;
    if (!destination || !capacity || !source) return 0;
    size = strlen(source);
    if (size >= capacity) return 0;
    memcpy(destination, source, size + 1u);
    return 1;
}

static int xlsx_safe_package_path(const char *path)
{
    const unsigned char *cursor = (const unsigned char *)path;
    const char *segment;
    if (!path || !*path || *path == '/' || *path == '\\') return 0;
    segment = path;
    for (;;) {
        unsigned char ch = *cursor;
        if (ch == '\0' || ch == '/') {
            size_t segment_size = (size_t)((const char *)cursor - segment);
            if (!segment_size ||
                (segment_size == 1u && segment[0] == '.') ||
                (segment_size == 2u && segment[0] == '.' &&
                 segment[1] == '.')) return 0;
            if (ch == '\0') return 1;
            segment = (const char *)cursor + 1u;
        } else if (ch == '\\' || ch == '%' || ch == '?' || ch == '#' ||
                   ch == ':' || ch < 0x20u || ch == 0x7fu) {
            return 0;
        }
        ++cursor;
    }
}

static int xlsx_resolve_target(const char *source_part, const char *target,
                               char *out_path, size_t out_capacity)
{
    size_t directory_size = 0u;
    size_t target_size;
    const char *target_start = target;
    const char *slash;
    if (!target || !out_path || !out_capacity) return 0;
    if (*target_start == '/') {
        ++target_start;
    } else if (source_part && *source_part) {
        slash = strrchr(source_part, '/');
        if (slash) directory_size = (size_t)(slash - source_part + 1u);
    }
    target_size = strlen(target_start);
    if (!target_size || directory_size > out_capacity - 1u ||
        target_size > out_capacity - directory_size - 1u) return 0;
    if (directory_size) memcpy(out_path, source_part, directory_size);
    memcpy(out_path + directory_size, target_start, target_size + 1u);
    return xlsx_safe_package_path(out_path);
}

static char *xlsx_relationships_path(const char *part_path)
{
    const char *basename;
    const char *slash;
    size_t directory_size;
    size_t basename_size;
    size_t needed;
    char *path;
    if (!part_path || !xlsx_safe_package_path(part_path)) return NULL;
    slash = strrchr(part_path, '/');
    directory_size = slash ? (size_t)(slash - part_path + 1u) : 0u;
    basename = slash ? slash + 1u : part_path;
    basename_size = strlen(basename);
    if (directory_size > SIZE_MAX - basename_size - 12u) return NULL;
    needed = directory_size + 7u + basename_size + 5u + 1u;
    if (needed > XLSX_MAX_PATH_BYTES) return NULL;
    path = (char *)malloc(needed);
    if (!path) return NULL;
    if (directory_size) memcpy(path, part_path, directory_size);
    memcpy(path + directory_size, "_rels/", 6u);
    memcpy(path + directory_size + 6u, basename, basename_size);
    memcpy(path + directory_size + 6u + basename_size, ".rels", 6u);
    return path;
}

static void *xlsx_extract_xml(XlsxReader *reader, const char *path,
                              size_t *out_size)
{
    size_t index;
    size_t extracted_size = 0u;
    QwZipEntryInfo stat;
    void *contents;
    if (out_size) *out_size = 0u;
    if (!reader || !reader->zip || !out_size ||
        !xlsx_safe_package_path(path)) return NULL;
    index = qw_zip_reader_locate_file(reader->zip, path, NULL,
                                      QW_ZIP_CASE_SENSITIVE);
    if (index == (size_t)-1 ||
        !qw_zip_reader_file_stat(reader->zip, index, &stat) ||
        !stat.uncompressed_size ||
        stat.uncompressed_size > XLSX_MAX_XML_PART_BYTES ||
        stat.uncompressed_size > XLSX_MAX_TOTAL_XML_BYTES -
            reader->total_xml_bytes ||
        !qw_zip_reader_is_file_supported(reader->zip, index)) return NULL;
    contents = qw_zip_reader_extract_to_heap(reader->zip, index,
                                              &extracted_size, 0u);
    if (!contents || extracted_size != stat.uncompressed_size ||
        extracted_size > XLSX_MAX_XML_PART_BYTES ||
        extracted_size > XLSX_MAX_TOTAL_XML_BYTES -
            reader->total_xml_bytes) {
        free(contents);
        return NULL;
    }
    reader->total_xml_bytes += extracted_size;
    *out_size = extracted_size;
    return contents;
}

static QwXmlNode *xlsx_parse_xml(const uint8_t *data, size_t size)
{
    return qw_xml_parse(data, size, NULL);
}

static QwXmlNode *xlsx_find_descendant(XlsxReader *reader, QwXmlNode *node,
                                    const char *local_name, size_t depth)
{
    QwXmlNode *found;
    if (!reader || depth > XLSX_MAX_XML_DEPTH) return NULL;
    for (; node; node = qw_xml_next(node)) {
        if (++reader->visited_nodes > XLSX_MAX_XML_NODES) return NULL;
        if (xlsx_is_tag(node, local_name)) return node;
        found = xlsx_find_descendant(reader, qw_xml_down(node), local_name,
                                     depth + 1u);
        if (found || reader->visited_nodes > XLSX_MAX_XML_NODES) return found;
    }
    return NULL;
}

static int xlsx_append_xml_text(XlsxReader *reader, QwXmlNode *node,
                                size_t depth)
{
    if (!reader || depth > XLSX_MAX_XML_DEPTH) return 0;
    for (; node; node = qw_xml_next(node)) {
        const char *text;
        if (++reader->visited_nodes > XLSX_MAX_XML_NODES) return 0;
        text = qw_xml_text(node);
        if (text) {
            if (!xlsx_html_escape(reader, text)) return 0;
        } else if (!xlsx_append_xml_text(reader, qw_xml_down(node),
                                         depth + 1u)) {
            return 0;
        }
    }
    return 1;
}

static int xlsx_collect_plain_text(XlsxReader *reader, QwXmlNode *node,
                                  size_t depth, QwTextBuffer *buffer)
{
    if (!reader || !buffer || depth > XLSX_MAX_XML_DEPTH) return 0;
    for (; node; node = qw_xml_next(node)) {
        const char *text;
        if (++reader->visited_nodes > XLSX_MAX_XML_NODES) return 0;
        text = qw_xml_text(node);
        if (text) {
            size_t size = strlen(text);
            if (size > XLSX_MAX_XML_PART_BYTES ||
                buffer->size > XLSX_MAX_XML_PART_BYTES - size) return 0;
            buffer->limit = (size_t)XLSX_MAX_XML_PART_BYTES + 1u;
            if (!qw_text_buffer_append(buffer, text, size)) return 0;
        } else if (!xlsx_collect_plain_text(reader, qw_xml_down(node),
                                            depth + 1u, buffer)) {
            return 0;
        }
    }
    return 1;
}

static unsigned xlsx_relationship_kind(const char *type)
{
    const char *kind = type ? strrchr(type, '/') : NULL;
    if (!kind) return 0u;
    if (strcmp(kind, "/officeDocument") == 0)
        return XLSX_RELATIONSHIP_WORKBOOK;
    if (strcmp(kind, "/worksheet") == 0)
        return XLSX_RELATIONSHIP_WORKSHEET;
    if (strcmp(kind, "/sharedStrings") == 0)
        return XLSX_RELATIONSHIP_SHARED_STRINGS;
    return 0u;
}

static XlsxRelationship *xlsx_read_relationships(
    XlsxReader *reader, QwXmlNode *xml, const char *source_part,
    size_t *out_count)
{
    XlsxRelationship *relationships;
    QwXmlNode *root;
    QwXmlNode *node;
    size_t count = 0u;
    if (out_count) *out_count = 0u;
    if (!reader || !xml || !out_count) return NULL;
    relationships = (XlsxRelationship *)calloc(
        XLSX_MAX_RELATIONSHIPS, sizeof(*relationships));
    if (!relationships) return NULL;
    root = qw_xml_root(xml);
    for (node = qw_xml_down(root); node; node = qw_xml_next(node)) {
        const char *id;
        const char *type;
        const char *target;
        const char *target_mode;
        unsigned kind;
        size_t i;
        XlsxRelationship relation;
        if (!xlsx_is_tag(node, "Relationship")) continue;
        if (++reader->visited_nodes > XLSX_MAX_XML_NODES) {
            free(relationships);
            return NULL;
        }
        id = qw_xml_att(node, "Id");
        type = qw_xml_att(node, "Type");
        target = qw_xml_att(node, "Target");
        target_mode = qw_xml_att(node, "TargetMode");
        kind = xlsx_relationship_kind(type);
        if (!kind || (target_mode && strcmp(target_mode, "External") == 0))
            continue;
        memset(&relation, 0, sizeof(relation));
        if (!id || !target || count == XLSX_MAX_RELATIONSHIPS ||
            !xlsx_copy_bounded(relation.id, sizeof(relation.id), id) ||
            !xlsx_resolve_target(source_part, target, relation.target,
                                 sizeof(relation.target))) {
            free(relationships);
            return NULL;
        }
        relation.kind = kind;
        for (i = 0u; i < count; ++i) {
            if (strcmp(relationships[i].id, relation.id) == 0) {
                free(relationships);
                return NULL;
            }
        }
        relationships[count++] = relation;
    }
    *out_count = count;
    return relationships;
}

static const XlsxRelationship *xlsx_find_relationship(
    const XlsxRelationship *relationships, size_t count, const char *id,
    unsigned kind)
{
    size_t i;
    if (!relationships || !id) return NULL;
    for (i = 0u; i < count; ++i) {
        if (relationships[i].kind == kind &&
            strcmp(relationships[i].id, id) == 0) return &relationships[i];
    }
    return NULL;
}

static int xlsx_shared_string_add(XlsxReader *reader, QwXmlNode *item)
{
    QwTextBuffer value = {0};
    char **grown;
    size_t capacity;
    char *empty;
    if (reader->shared_string_count >= XLSX_MAX_SHARED_STRINGS ||
        !xlsx_collect_plain_text(reader, qw_xml_down(item), 0u, &value)) {
        free(value.data);
        return 0;
    }
    if (!value.data) {
        empty = (char *)calloc(1u, 1u);
        if (!empty) return 0;
        value.data = empty;
    }
    if (reader->shared_string_count == reader->shared_string_capacity) {
        capacity = reader->shared_string_capacity
            ? reader->shared_string_capacity * 2u : 256u;
        if (capacity > XLSX_MAX_SHARED_STRINGS)
            capacity = XLSX_MAX_SHARED_STRINGS;
        grown = (char **)realloc(reader->shared_strings,
                                 capacity * sizeof(*grown));
        if (!grown) {
            free(value.data);
            return 0;
        }
        reader->shared_strings = grown;
        reader->shared_string_capacity = capacity;
    }
    reader->shared_strings[reader->shared_string_count++] = value.data;
    return 1;
}

static int xlsx_read_shared_strings(XlsxReader *reader, QwXmlNode *xml)
{
    QwXmlNode *root;
    QwXmlNode *node;
    if (!reader || !xml) return 0;
    root = xlsx_find_descendant(reader, qw_xml_root(xml), "sst", 0u);
    if (!root) return 0;
    for (node = qw_xml_down(root); node; node = qw_xml_next(node)) {
        if (xlsx_is_tag(node, "si") && !xlsx_shared_string_add(reader, node))
            return 0;
    }
    return 1;
}

static int xlsx_parse_column(const char *cell_reference, size_t *out_column)
{
    size_t column = 0u;
    size_t i = 0u;
    if (!cell_reference || !out_column) return 0;
    while (cell_reference[i] >= 'A' && cell_reference[i] <= 'Z') {
        unsigned digit = (unsigned)(cell_reference[i] - 'A' + 1);
        if (column > (XLSX_MAX_COLUMNS - digit) / 26u) return 0;
        column = column * 26u + digit;
        ++i;
    }
    while (cell_reference[i] >= 'a' && cell_reference[i] <= 'z') {
        unsigned digit = (unsigned)(cell_reference[i] - 'a' + 1);
        if (column > (XLSX_MAX_COLUMNS - digit) / 26u) return 0;
        column = column * 26u + digit;
        ++i;
    }
    if (!i || column == 0u || column > XLSX_MAX_COLUMNS ||
        cell_reference[i] < '0' || cell_reference[i] > '9') return 0;
    *out_column = column;
    return 1;
}

static int xlsx_parse_cell_reference(const char *reference,
                                     size_t *out_row, size_t *out_column)
{
    size_t column;
    size_t row = 0u;
    size_t i = 0u;
    if (!reference || !out_row || !out_column ||
        !xlsx_parse_column(reference, &column)) return 0;
    while ((reference[i] >= 'A' && reference[i] <= 'Z') ||
           (reference[i] >= 'a' && reference[i] <= 'z')) ++i;
    if (reference[i] < '0' || reference[i] > '9') return 0;
    while (reference[i] >= '0' && reference[i] <= '9') {
        unsigned digit = (unsigned)(reference[i] - '0');
        if (row > (XLSX_MAX_ROWS - digit) / 10u) return 0;
        row = row * 10u + digit;
        ++i;
    }
    if (reference[i] || !row || row > XLSX_MAX_ROWS) return 0;
    *out_row = row - 1u;
    *out_column = column - 1u;
    return 1;
}

static int xlsx_read_cell_model(XlsxReader *reader, QwXmlNode *cell,
                                size_t row, size_t column)
{
    const char *type = qw_xml_att(cell, "t");
    QwXmlNode *value_node = NULL;
    QwXmlNode *formula_node;
    QwTextBuffer value = {0};
    QwTextBuffer formula = {0};
    QwSpreadsheetValueKind value_kind = QW_SPREADSHEET_VALUE_EMPTY;
    const uint8_t *value_data = NULL;
    size_t value_size = 0u;
    const char *raw;
    int ok = 0;

    if (!reader || !reader->workbook || !cell) return 1;
    if (type && strcmp(type, "s") != 0 && strcmp(type, "b") != 0 &&
        strcmp(type, "e") != 0 && strcmp(type, "d") != 0 &&
        strcmp(type, "str") != 0 && strcmp(type, "inlineStr") != 0 &&
        strcmp(type, "n") != 0) {
        reader->typed_model_unsupported = 1;
        goto cleanup;
    }
    value.limit = (size_t)XLSX_MAX_XML_PART_BYTES + 1u;
    formula.limit = (size_t)XLSX_MAX_XML_PART_BYTES + 1u;
    formula_node = xlsx_find_descendant(reader, qw_xml_down(cell), "f", 0u);
    if (formula_node) {
        const char *formula_type = qw_xml_att(formula_node, "t");
        if ((formula_type && strcmp(formula_type, "normal") != 0) ||
            !qw_xml_down(formula_node)) {
            reader->typed_model_unsupported = 1;
            goto cleanup;
        }
    }
    if (formula_node &&
        !xlsx_collect_plain_text(reader, qw_xml_down(formula_node), 0u,
                                 &formula)) goto cleanup;
    if (type && strcmp(type, "inlineStr") == 0) {
        value_node = xlsx_find_descendant(reader, qw_xml_down(cell), "is", 0u);
        if (value_node &&
            !xlsx_collect_plain_text(reader, qw_xml_down(value_node), 0u,
                                     &value)) goto cleanup;
        value_kind = QW_SPREADSHEET_VALUE_STRING;
        value_data = (const uint8_t *)value.data;
        value_size = value.size;
    } else {
        value_node = xlsx_find_descendant(reader, qw_xml_down(cell), "v", 0u);
        raw = value_node ? qw_xml_text(qw_xml_down(value_node)) : NULL;
        if (raw) {
            if ((!type || strcmp(type, "n") == 0) && !*raw) {
                value_kind = QW_SPREADSHEET_VALUE_EMPTY;
                value_data = NULL;
                value_size = 0u;
                goto value_ready;
            }
            if (type && strcmp(type, "s") == 0) {
                char *end = NULL;
                unsigned long index;
                errno = 0;
                if (!*raw) {
                    value_kind = QW_SPREADSHEET_VALUE_STRING;
                    value_data = (const uint8_t *)"";
                    value_size = 0u;
                    goto value_ready;
                }
                index = strtoul(raw, &end, 10);
                if (errno || !end || *end || index >= reader->shared_string_count)
                    goto cleanup;
                value_data = (const uint8_t *)reader->shared_strings[index];
                value_size = strlen(reader->shared_strings[index]);
                value_kind = QW_SPREADSHEET_VALUE_STRING;
            } else {
                value_data = (const uint8_t *)raw;
                value_size = strlen(raw);
                if (type && strcmp(type, "b") == 0) {
                    if (!(value_size == 1u && (raw[0] == '0' || raw[0] == '1')))
                        goto cleanup;
                    value_kind = QW_SPREADSHEET_VALUE_BOOLEAN;
                } else if (type && strcmp(type, "e") == 0) {
                    value_kind = QW_SPREADSHEET_VALUE_ERROR;
                } else if (type && strcmp(type, "d") == 0) {
                    value_kind = QW_SPREADSHEET_VALUE_DATE_TIME;
                } else if (type && (strcmp(type, "str") == 0 ||
                                    strcmp(type, "inlineStr") == 0)) {
                    value_kind = QW_SPREADSHEET_VALUE_STRING;
                } else if (!type || strcmp(type, "n") == 0) {
                    value_kind = QW_SPREADSHEET_VALUE_NUMBER;
                } else {
                    reader->typed_model_unsupported = 1;
                    goto cleanup;
                }
            }
        }
    }
value_ready:
    if (!qw_spreadsheet_document_add_cell(reader->workbook,
            reader->current_sheet_index, row, column, value_kind,
            value_data, value_size,
            (const uint8_t *)formula.data, formula.size)) {
        reader->typed_model_resource_limit = 1;
        goto cleanup;
    }
    ok = 1;

cleanup:
    free(value.data);
    free(formula.data);
    return ok;
}

static int xlsx_append_cell_value(XlsxReader *reader, QwXmlNode *cell)
{
    const char *type = qw_xml_att(cell, "t");
    QwXmlNode *value = NULL;
    if (type && strcmp(type, "inlineStr") == 0) {
        value = xlsx_find_descendant(reader, qw_xml_down(cell), "is", 0u);
        return value
            ? xlsx_append_xml_text(reader, qw_xml_down(value), 0u) : 1;
    }
    value = xlsx_find_descendant(reader, qw_xml_down(cell), "v", 0u);
    if (!value) return 1;
    if (type && strcmp(type, "s") == 0) {
        const char *raw;
        char *end = NULL;
        unsigned long index;
        raw = qw_xml_text(qw_xml_down(value));
        if (!raw || !*raw) return 1;
        errno = 0;
        index = strtoul(raw, &end, 10);
        if (errno || !end || *end || index >= reader->shared_string_count)
            return 0;
        return xlsx_html_escape(reader, reader->shared_strings[index]);
    }
    if (type && strcmp(type, "b") == 0) {
        const char *raw = qw_xml_text(qw_xml_down(value));
        if (!raw) return 1;
        return xlsx_html_literal(reader, strcmp(raw, "0") == 0
            ? "FALSE" : "TRUE");
    }
    return xlsx_append_xml_text(reader, qw_xml_down(value), 0u);
}

static int xlsx_render_worksheet(XlsxReader *reader, QwXmlNode *xml)
{
    QwXmlNode *sheet_data;
    QwXmlNode *row;
    size_t fallback_row = 0u;
    sheet_data = xlsx_find_descendant(reader, qw_xml_root(xml),
                                      "sheetData", 0u);
    if (!sheet_data) return 0;
    if (!xlsx_html_literal(reader, "<table><tbody>")) return 0;
    for (row = qw_xml_down(sheet_data); row; row = qw_xml_next(row)) {
        QwXmlNode *cell;
        size_t previous_column = 0u;
        size_t row_index = fallback_row;
        const char *row_reference;
        if (!xlsx_is_tag(row, "row")) continue;
        row_reference = qw_xml_att(row, "r");
        if (reader->workbook && row_reference) {
            char *end = NULL;
            unsigned long row_number;
            errno = 0;
            row_number = strtoul(row_reference, &end, 10);
            if (errno || !end || *end || !row_number ||
                row_number > XLSX_MAX_ROWS) return 0;
            row_index = (size_t)row_number - 1u;
        }
        if (++reader->visited_nodes > XLSX_MAX_XML_NODES ||
            !xlsx_html_literal(reader, "<tr>")) return 0;
        for (cell = qw_xml_down(row); cell; cell = qw_xml_next(cell)) {
            const char *reference;
            size_t column = 0u;
            size_t cell_row = row_index;
            size_t cell_column;
            size_t blank;
            if (!xlsx_is_tag(cell, "c")) continue;
            if (++reader->visited_nodes > XLSX_MAX_XML_NODES ||
                ++reader->cell_count > XLSX_MAX_CELLS) return 0;
            reference = qw_xml_att(cell, "r");
            if (reader->workbook && reference) {
                if (!xlsx_parse_cell_reference(reference, &cell_row,
                                               &cell_column)) return 0;
                column = cell_column + 1u;
            } else if (reference && !xlsx_parse_column(reference, &column)) {
                return 0;
            }
            if (!column || column <= previous_column)
                column = previous_column + 1u;
            if (column > XLSX_MAX_COLUMNS) return 0;
            if (!reference || !reader->workbook)
                cell_column = column - 1u;
            if (reader->capture_html) {
                for (blank = previous_column + 1u; blank < column; ++blank) {
                    if (!xlsx_html_literal(reader, "<td></td>")) return 0;
                }
            }
            if (reader->workbook && !xlsx_read_cell_model(
                    reader, cell, cell_row, cell_column)) return 0;
            if (reader->capture_html &&
                (!xlsx_html_literal(reader, "<td>") ||
                 !xlsx_append_cell_value(reader, cell) ||
                 !xlsx_html_literal(reader, "</td>"))) return 0;
            previous_column = column;
        }
        if (!xlsx_html_literal(reader, "</tr>")) return 0;
        if (row_index == SIZE_MAX) return 0;
        fallback_row = row_index + 1u;
    }
    return xlsx_html_literal(reader, "</tbody></table>");
}

static void xlsx_free_shared_strings(XlsxReader *reader)
{
    size_t i;
    if (!reader) return;
    for (i = 0u; i < reader->shared_string_count; ++i)
        free(reader->shared_strings[i]);
    free(reader->shared_strings);
    reader->shared_strings = NULL;
    reader->shared_string_count = 0u;
    reader->shared_string_capacity = 0u;
}

static int xlsx_build_document(const void *data, size_t size,
                               QwInputFormat input_format,
                               QwTextBuffer *out_html,
                               QwSpreadsheetDocument *out_workbook,
                               QwConvertReport *report)
{
    QwZipArchive zip;
    XlsxReader reader;
    QwXmlNode *root_rels_xml = NULL;
    QwXmlNode *workbook_xml = NULL;
    QwXmlNode *workbook_rels_xml = NULL;
    QwXmlNode *shared_strings_xml = NULL;
    QwXmlNode *sheet_xml = NULL;
    XlsxRelationship *root_relationships = NULL;
    XlsxRelationship *workbook_relationships = NULL;
    size_t root_relationship_count = 0u;
    size_t workbook_relationship_count = 0u;
    void *root_rels_data = NULL;
    void *workbook_data = NULL;
    void *workbook_rels_data = NULL;
    void *shared_strings_data = NULL;
    void *sheet_data = NULL;
    size_t root_rels_size = 0u;
    size_t workbook_size = 0u;
    size_t workbook_rels_size = 0u;
    size_t shared_strings_size = 0u;
    size_t sheet_size = 0u;
    char workbook_path[XLSX_MAX_PATH_BYTES] = "xl/workbook.xml";
    char *workbook_rels_path = NULL;
    QwXmlNode *sheets_node;
    QwXmlNode *sheet_node;
    size_t parsed_sheets = 0u;
    int archive_open = 0;
    int rc = QW_STATUS_ERR_FORMAT;

#define XLSX_FAIL(code, message) do { \
    rc = (code); \
    xlsx_set_error(report, rc, (message)); \
    goto cleanup; \
} while (0)

    if (!out_html && !out_workbook) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (out_html) memset(out_html, 0, sizeof(*out_html));
    if (!data || !size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(&zip, 0, sizeof(zip));
    memset(&reader, 0, sizeof(reader));
    if (!qw_zip_reader_init_mem_with_profile(
            &zip, data, size, 0u,
            QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE)) {
        xlsx_set_error(report, QW_STATUS_ERR_FORMAT,
            "OOXML spreadsheet package could not be opened as ZIP");
        qw_zip_reader_end(&zip);
        return QW_STATUS_ERR_FORMAT;
    }
    archive_open = 1;
    reader.zip = &zip;
    reader.workbook = out_workbook;
    reader.capture_html = out_html != NULL;
    if (out_workbook) {
        qw_spreadsheet_document_init(out_workbook, input_format);
    }
    if (reader.capture_html)
        reader.html.limit = (size_t)XLSX_MAX_HTML_BYTES + 1u;
        root_rels_data = xlsx_extract_xml(&reader, "_rels/.rels",
                                          &root_rels_size);
        if (root_rels_data) {
            root_rels_xml = xlsx_parse_xml(
                (const uint8_t *)root_rels_data, root_rels_size);
            free(root_rels_data);
            root_rels_data = NULL;
            root_relationships = xlsx_read_relationships(
                &reader, root_rels_xml, "", &root_relationship_count);
            if (root_relationships) {
                for (size_t i = 0u; i < root_relationship_count; ++i) {
                    if (root_relationships[i].kind ==
                            XLSX_RELATIONSHIP_WORKBOOK) {
                        (void)xlsx_copy_bounded(workbook_path,
                            sizeof(workbook_path), root_relationships[i].target);
                        break;
                    }
                }
            }
            qw_xml_drop(root_rels_xml);
            root_rels_xml = NULL;
        }
        if (!xlsx_safe_package_path(workbook_path)) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook relationship has an unsafe target");
        }
        workbook_data = xlsx_extract_xml(&reader, workbook_path,
                                         &workbook_size);
        if (!workbook_data) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook XML is missing, unsupported, or too large");
        }
        workbook_xml = xlsx_parse_xml((const uint8_t *)workbook_data,
                                      workbook_size);
        free(workbook_data);
        workbook_data = NULL;
        workbook_rels_path = xlsx_relationships_path(workbook_path);
        if (!workbook_rels_path) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook relationship path is invalid");
        }
        workbook_rels_data = xlsx_extract_xml(&reader, workbook_rels_path,
                                               &workbook_rels_size);
        if (!workbook_rels_data) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook relationships are missing or unsupported");
        }
        workbook_rels_xml = xlsx_parse_xml(
            (const uint8_t *)workbook_rels_data, workbook_rels_size);
        free(workbook_rels_data);
        workbook_rels_data = NULL;
        workbook_relationships = xlsx_read_relationships(
            &reader, workbook_rels_xml, workbook_path,
            &workbook_relationship_count);
        if (!workbook_relationships) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook relationships are invalid or too numerous");
        }
        for (size_t i = 0u; i < workbook_relationship_count; ++i) {
            if (workbook_relationships[i].kind ==
                    XLSX_RELATIONSHIP_SHARED_STRINGS) {
                shared_strings_data = xlsx_extract_xml(&reader,
                    workbook_relationships[i].target, &shared_strings_size);
                if (!shared_strings_data) {
                    XLSX_FAIL(QW_STATUS_ERR_DECODE,
                              "spreadsheet shared string table is unsupported or too large");
                }
                shared_strings_xml = xlsx_parse_xml(
                    (const uint8_t *)shared_strings_data,
                    shared_strings_size);
                free(shared_strings_data);
                shared_strings_data = NULL;
                if (!xlsx_read_shared_strings(&reader, shared_strings_xml)) {
                    XLSX_FAIL(QW_STATUS_ERR_DECODE,
                              "spreadsheet shared string table is invalid or too large");
                }
                break;
            }
        }
        if (reader.capture_html && !xlsx_html_literal(&reader,
                "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
                "@page{size:landscape;margin:12mm}body{font-family:sans-serif;"
                "font-size:10pt;color:#111}h2{font-size:14pt;margin:18pt 0 6pt}"
                "table{border-collapse:collapse;margin:0 0 16pt}"
                "td{border:1px solid #888;padding:3pt 6pt;vertical-align:top;"
                "white-space:pre-wrap}</style></head><body>")) {
            XLSX_FAIL(QW_STATUS_ERR_OUT_OF_MEMORY,
                      "Could not allocate spreadsheet HTML output");
        }
        sheets_node = xlsx_find_descendant(&reader, qw_xml_root(workbook_xml),
                                            "sheets", 0u);
        if (!sheets_node) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook contains no sheet list");
        }
        for (sheet_node = qw_xml_down(sheets_node); sheet_node;
             sheet_node = qw_xml_next(sheet_node)) {
            const char *sheet_name;
            const char *relationship_id;
            const XlsxRelationship *relation;
            if (!xlsx_is_tag(sheet_node, "sheet")) continue;
            if (++parsed_sheets > XLSX_MAX_SHEETS) {
                XLSX_FAIL(QW_STATUS_ERR_RESOURCE_LIMIT,
                          "spreadsheet workbook exceeds the 256 sheet limit");
            }
            sheet_name = qw_xml_att(sheet_node, "name");
            relationship_id = qw_xml_att(sheet_node, "r:id");
            relation = xlsx_find_relationship(workbook_relationships,
                workbook_relationship_count, relationship_id,
                XLSX_RELATIONSHIP_WORKSHEET);
            if (!sheet_name || !relation) {
                XLSX_FAIL(QW_STATUS_ERR_DECODE,
                          "spreadsheet sheet has no valid worksheet relationship");
            }
            if (out_workbook &&
                !qw_spreadsheet_document_add_sheet(out_workbook,
                    (const uint8_t *)sheet_name, strlen(sheet_name),
                    &reader.current_sheet_index)) {
                reader.typed_model_resource_limit = 1;
                XLSX_FAIL(QW_STATUS_ERR_RESOURCE_LIMIT,
                          "spreadsheet workbook model exceeds its sheet or memory limit");
            }
            sheet_data = xlsx_extract_xml(&reader, relation->target,
                                          &sheet_size);
            if (!sheet_data) {
                XLSX_FAIL(QW_STATUS_ERR_DECODE,
                          "spreadsheet worksheet is missing, unsupported, or too large");
            }
            sheet_xml = xlsx_parse_xml((const uint8_t *)sheet_data,
                                       sheet_size);
            free(sheet_data);
            sheet_data = NULL;
            if (!xlsx_html_literal(&reader, "<h2>") ||
                !xlsx_html_escape(&reader, sheet_name) ||
                !xlsx_html_literal(&reader, "</h2>") ||
                !xlsx_render_worksheet(&reader, sheet_xml)) {
                XLSX_FAIL(QW_STATUS_ERR_DECODE,
                          "spreadsheet worksheet exceeds conversion limits");
            }
            qw_xml_drop(sheet_xml);
            sheet_xml = NULL;
            sheet_size = 0u;
            ++reader.sheet_count;
        }
        if (!reader.sheet_count ||
            (reader.capture_html &&
             !xlsx_html_literal(&reader, "</body></html>"))) {
            XLSX_FAIL(QW_STATUS_ERR_DECODE,
                      "spreadsheet workbook has no readable worksheets");
        }
    if (out_html) {
        *out_html = reader.html;
        memset(&reader.html, 0, sizeof(reader.html));
    }
    rc = QW_STATUS_OK;
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
            out_workbook
                ? "%s typed workbook opened with cached cell values, cell types, and formula text; formulas are not recalculated and styles, number formats, merged cells, charts, macros and template behavior are not preserved"
                : "%s worksheets and cached cell values opened as HTML layout; formulas are not recalculated, and styles, number formats, merged cells, charts, macros and template behavior are not preserved",
            input_format == QW_FORMAT_XLSM ? "XLSM" :
            input_format == QW_FORMAT_XLTX ? "XLTX" : "XLSX");
    }

cleanup:
    qw_xml_drop(root_rels_xml);
    qw_xml_drop(workbook_xml);
    qw_xml_drop(workbook_rels_xml);
    qw_xml_drop(shared_strings_xml);
    qw_xml_drop(sheet_xml);
    free(root_rels_data);
    free(workbook_data);
    free(workbook_rels_data);
    free(shared_strings_data);
    free(sheet_data);
    free(workbook_rels_path);
    free(root_relationships);
    free(workbook_relationships);
    xlsx_free_shared_strings(&reader);
    free(reader.html.data);
    if (archive_open) qw_zip_reader_end(&zip);
    if (reader.typed_model_unsupported) {
        rc = QW_STATUS_ERR_UNSUPPORTED;
        xlsx_set_error(report, rc,
            "typed XLSX conversion does not support shared/array formulas or this cell type");
    } else if (reader.typed_model_resource_limit) {
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
        xlsx_set_error(report, rc,
            "typed XLSX workbook exceeds its cell or memory limit");
    } else if (reader.output_limit_hit) {
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
        xlsx_set_error(report, rc,
                       "OOXML spreadsheet layout exceeds the 64 MiB HTML limit");
    }
    if (rc != QW_STATUS_OK && out_workbook)
        qw_spreadsheet_document_dispose(out_workbook);
#undef XLSX_FAIL
    return rc;
}

static void xlsx_layout_dispose(void *context)
{
    free(context);
}

static int xlsx_open_layout(const void *data, size_t size,
                            QwInputFormat input_format,
                            const QwConvertOptions *options,
                            QwLayoutDocument *out_document,
                            QwConvertReport *report)
{
    QwTextBuffer html = {0};
    int rc;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(out_document, 0, sizeof(*out_document));
    if (input_format != QW_FORMAT_XLSX &&
        input_format != QW_FORMAT_XLSM &&
        input_format != QW_FORMAT_XLTX) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = xlsx_build_document(data, size, input_format, &html, NULL, report);
    if (rc != QW_STATUS_OK) {
        free(html.data);
        return rc;
    }
    out_document->html_utf8 = (const uint8_t *)html.data;
    out_document->html_size = html.size;
    out_document->context = html.data;
    out_document->dispose = xlsx_layout_dispose;
    return QW_STATUS_OK;
}

static int xlsx_open_spreadsheet(const void *data, size_t size,
                                 QwInputFormat input_format,
                                 const QwConvertOptions *options,
                                 QwSpreadsheetDocument *out_document,
                                 QwConvertReport *report)
{
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    qw_spreadsheet_document_init(out_document, input_format);
    if (input_format != QW_FORMAT_XLSX &&
        input_format != QW_FORMAT_XLSM &&
        input_format != QW_FORMAT_XLTX) return QW_STATUS_ERR_INVALID_ARGUMENT;
    return xlsx_build_document(data, size, input_format, NULL, out_document,
                               report);
}

int qw_xlsx_parse_spreadsheet(const void *data, size_t size,
                              QwInputFormat input_format,
                              QwSpreadsheetDocument *out_document)
{
    return xlsx_open_spreadsheet(data, size, input_format, NULL,
                                 out_document, NULL);
}

const QwFormatDescriptor qw_format_xlsx = {
    .id = QW_FORMAT_XLSX,
    .name = "XLSX",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE |
                    QW_FORMAT_CAP_ARCHIVE_PROBE,
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = xlsx_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE,
    .convert_to_pdf = NULL,
    .open_spreadsheet = xlsx_open_spreadsheet
};

const QwFormatDescriptor qw_format_xlsm = {
    .id = QW_FORMAT_XLSM,
    .name = "XLSM",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE |
                    QW_FORMAT_CAP_ARCHIVE_PROBE,
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = xlsx_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE,
    .convert_to_pdf = NULL,
    .open_spreadsheet = xlsx_open_spreadsheet
};

const QwFormatDescriptor qw_format_xltx = {
    .id = QW_FORMAT_XLTX,
    .name = "XLTX",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE |
                    QW_FORMAT_CAP_ARCHIVE_PROBE,
    .probe = NULL,
    .open_memory = NULL,
    .open_layout = xlsx_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE,
    .convert_to_pdf = NULL,
    .open_spreadsheet = xlsx_open_spreadsheet
};
