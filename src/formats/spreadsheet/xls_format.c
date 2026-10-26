#include "../../core/format_registry.h"
#include "../../core/html_output.h"
#include "../../core/text_buffer.h"
#include "../archive/ole_compound.h"
#include "spreadsheet_parser_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    XLS_MAX_WORKBOOK_BYTES = 128u * 1024u * 1024u,
    XLS_PROBE_PREFIX_BYTES = 1024u * 1024u,
    XLS_MAX_HTML_BYTES = 64u * 1024u * 1024u,
    XLS_MAX_SHARED_STRINGS = 1000000u,
    XLS_MAX_SHARED_STRING_BYTES = 64u * 1024u * 1024u,
    XLS_MAX_STRING_BYTES = 256u * 1024u,
    XLS_MAX_SHEETS = 256u,
    XLS_MAX_CELLS = 1000000u,
    XLS_MAX_ROWS = 65536u,
    XLS_MAX_COLUMNS = 256u,
    XLS_MAX_RECORD_BYTES = 8224u
};

enum {
    XLS_RECORD_BOF = 0x0809u,
    XLS_RECORD_EOF = 0x000au,
    XLS_RECORD_FILEPASS = 0x002fu,
    XLS_RECORD_TEMPLATE = 0x0060u,
    XLS_RECORD_BOUNDSHEET8 = 0x0085u,
    XLS_RECORD_SST = 0x00fcu,
    XLS_RECORD_CONTINUE = 0x003cu,
    XLS_RECORD_LABELSST = 0x00fdu,
    XLS_RECORD_NUMBER = 0x0203u,
    XLS_RECORD_LABEL = 0x0204u,
    XLS_RECORD_BOOLERR = 0x0205u,
    XLS_RECORD_FORMULA = 0x0006u,
    XLS_RECORD_STRING = 0x0207u,
    XLS_RECORD_RK = 0x027eu,
    XLS_RECORD_MULRK = 0x00bdu,
    XLS_WORKBOOK_SUBSTREAM = 0x0005u,
    XLS_WORKSHEET_SUBSTREAM = 0x0010u
};

typedef struct XlsString {
    char *data;
    size_t size;
} XlsString;

typedef struct XlsSheet {
    uint32_t stream_offset;
    char *name;
    size_t name_size;
    uint8_t hidden_state;
    uint8_t sheet_type;
} XlsSheet;

typedef struct XlsReader {
    QwTextBuffer html;
    QwSpreadsheetDocument *spreadsheet;
    size_t spreadsheet_sheet_index;
    XlsString *shared_strings;
    size_t shared_string_count;
    size_t shared_string_bytes;
    size_t sheet_count;
    size_t cell_count;
    size_t visited_records;
    int row_open;
    uint32_t current_row;
    uint32_t previous_row;
    uint32_t next_column;
    int has_previous_row;
    int output_limit_hit;
    int capture_html;
    int spreadsheet_error;
} XlsReader;

static uint16_t xls_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t xls_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xls_u64(const uint8_t *p)
{
    return (uint64_t)xls_u32(p) | ((uint64_t)xls_u32(p + 4u) << 32);
}

static int xls_html_literal(XlsReader *reader, const char *text)
{
    return reader && (!reader->capture_html || qw_html_output_literal(
        &reader->html, text, XLS_MAX_HTML_BYTES, &reader->output_limit_hit));
}

static int xls_html_escape(XlsReader *reader, const char *text)
{
    return reader && (!reader->capture_html || qw_html_output_escape(
        &reader->html, text, XLS_MAX_HTML_BYTES, &reader->output_limit_hit));
}

static void xls_string_drop(XlsString *string)
{
    if (!string) return;
    free(string->data);
    string->data = NULL;
    string->size = 0u;
}

static void xls_reader_drop(XlsReader *reader)
{
    size_t i;
    if (!reader) return;
    for (i = 0u; i < reader->shared_string_count; ++i)
        xls_string_drop(&reader->shared_strings[i]);
    free(reader->shared_strings);
    free(reader->html.data);
    memset(reader, 0, sizeof(*reader));
}

static int xls_append_codepoint(QwTextBuffer *buffer, uint32_t codepoint)
{
    char encoded[4];
    size_t size;
    if (codepoint == 0u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = 0xfffdu;
    }
    if (codepoint < 0x80u) {
        encoded[0] = (char)codepoint;
        size = 1u;
    } else if (codepoint < 0x800u) {
        encoded[0] = (char)(0xc0u | (codepoint >> 6));
        encoded[1] = (char)(0x80u | (codepoint & 0x3fu));
        size = 2u;
    } else if (codepoint < 0x10000u) {
        encoded[0] = (char)(0xe0u | (codepoint >> 12));
        encoded[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[2] = (char)(0x80u | (codepoint & 0x3fu));
        size = 3u;
    } else {
        encoded[0] = (char)(0xf0u | (codepoint >> 18));
        encoded[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        encoded[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[3] = (char)(0x80u | (codepoint & 0x3fu));
        size = 4u;
    }
    return qw_text_buffer_append(buffer, encoded, size);
}

static int xls_decode_characters(const uint8_t *data, size_t data_size,
                                size_t character_count, int high_byte,
                                QwTextBuffer *output, size_t *out_consumed)
{
    size_t width = high_byte ? 2u : 1u;
    size_t bytes;
    size_t i;
    if (character_count > SIZE_MAX / width) return 0;
    bytes = character_count * width;
    if (bytes > data_size) return 0;
    output->limit = XLS_MAX_STRING_BYTES + 1u;
    for (i = 0u; i < character_count; ++i) {
        uint32_t codepoint;
        if (high_byte) {
            uint16_t first = xls_u16(data + i * 2u);
            if (first >= 0xd800u && first <= 0xdbffu &&
                i + 1u < character_count) {
                uint16_t second = xls_u16(data + (i + 1u) * 2u);
                if (second >= 0xdc00u && second <= 0xdfffu) {
                    codepoint = 0x10000u +
                        (((uint32_t)first - 0xd800u) << 10) +
                        ((uint32_t)second - 0xdc00u);
                    ++i;
                } else {
                    codepoint = 0xfffdu;
                }
            } else if (first >= 0xdc00u && first <= 0xdfffu) {
                codepoint = 0xfffdu;
            } else {
                codepoint = first;
            }
        } else {
            codepoint = data[i];
        }
        if (!xls_append_codepoint(output, codepoint)) return 0;
    }
    if (out_consumed) *out_consumed = bytes;
    return 1;
}

static int xls_decode_simple_unicode(const uint8_t *data, size_t data_size,
                                    size_t count_bytes, XlsString *out,
                                    size_t *out_consumed)
{
    size_t character_count;
    size_t cursor;
    size_t char_bytes = 0u;
    int high_byte;
    QwTextBuffer buffer;
    if (!data || !out || (count_bytes != 1u && count_bytes != 2u) ||
        data_size < count_bytes + 1u) return 0;
    character_count = count_bytes == 1u ? data[0] : xls_u16(data);
    cursor = count_bytes + 1u;
    high_byte = (data[count_bytes] & 0x01u) != 0u;
    memset(&buffer, 0, sizeof(buffer));
    if (!xls_decode_characters(data + cursor, data_size - cursor,
                               character_count, high_byte,
                               &buffer, &char_bytes)) {
        free(buffer.data);
        return 0;
    }
    cursor += char_bytes;
    out->data = buffer.data;
    out->size = buffer.size;
    if (out_consumed) *out_consumed = cursor;
    return 1;
}

static int xls_decode_rich_unicode(const uint8_t *data, size_t data_size,
                                   size_t count_bytes, XlsString *out,
                                   size_t *out_consumed)
{
    size_t character_count;
    size_t cursor;
    size_t char_bytes = 0u;
    size_t run_bytes = 0u;
    size_t extension_bytes = 0u;
    uint8_t flags;
    int high_byte;
    int rich;
    int extended;
    QwTextBuffer buffer;
    if (!data || !out || (count_bytes != 1u && count_bytes != 2u) ||
        data_size < count_bytes + 1u) return 0;
    character_count = count_bytes == 1u ? data[0] : xls_u16(data);
    flags = data[count_bytes];
    high_byte = (flags & 0x01u) != 0u;
    rich = (flags & 0x08u) != 0u;
    extended = (flags & 0x04u) != 0u;
    cursor = count_bytes + 1u;
    if (rich) {
        if (cursor + 2u > data_size) return 0;
        run_bytes = (size_t)xls_u16(data + cursor) * 4u;
        cursor += 2u;
    }
    if (extended) {
        uint32_t extension_size;
        if (cursor + 4u > data_size) return 0;
        extension_size = xls_u32(data + cursor);
        cursor += 4u;
#if SIZE_MAX <= UINT32_MAX
        if (extension_size > SIZE_MAX) return 0;
#endif
        extension_bytes = (size_t)extension_size;
    }
    memset(&buffer, 0, sizeof(buffer));
    if (!xls_decode_characters(data + cursor, data_size - cursor,
                               character_count, high_byte,
                               &buffer, &char_bytes)) {
        free(buffer.data);
        return 0;
    }
    cursor += char_bytes;
    if (run_bytes > data_size - cursor) {
        free(buffer.data);
        return 0;
    }
    cursor += run_bytes;
    if (extension_bytes > data_size - cursor) {
        free(buffer.data);
        return 0;
    }
    cursor += extension_bytes;
    out->data = buffer.data;
    out->size = buffer.size;
    if (out_consumed) *out_consumed = cursor;
    return 1;
}

static int xls_add_shared_string(XlsReader *reader, const uint8_t *data,
                                 size_t data_size, size_t *offset)
{
    XlsString string = {0};
    size_t consumed = 0u;
    size_t remaining;
    XlsString *slot;
    if (!reader || !offset || *offset > data_size) return 0;
    remaining = data_size - *offset;
    if (!xls_decode_rich_unicode(data + *offset, remaining, 2u,
                                 &string, &consumed) ||
        consumed > remaining ||
        string.size > XLS_MAX_SHARED_STRING_BYTES -
            reader->shared_string_bytes) {
        xls_string_drop(&string);
        return 0;
    }
    slot = &reader->shared_strings[reader->shared_string_count++];
    *slot = string;
    reader->shared_string_bytes += string.size;
    *offset += consumed;
    return 1;
}

static int xls_parse_sst(XlsReader *reader, const uint8_t *data, size_t size)
{
    uint32_t total_count;
    uint32_t unique_count;
    size_t offset = 8u;
    uint32_t i;
    if (!reader || !data || size < 8u || reader->shared_strings)
        return 0;
    total_count = xls_u32(data);
    unique_count = xls_u32(data + 4u);
    if (unique_count > XLS_MAX_SHARED_STRINGS || unique_count > total_count)
        return 0;
#if SIZE_MAX <= UINT32_MAX
    if (unique_count > SIZE_MAX / sizeof(*reader->shared_strings)) return 0;
#endif
    reader->shared_strings = (XlsString *)calloc(
        unique_count ? unique_count : 1u, sizeof(*reader->shared_strings));
    if (!reader->shared_strings) return 0;
    reader->shared_string_count = 0u;
    for (i = 0u; i < unique_count; ++i) {
        if (!xls_add_shared_string(reader, data, size, &offset)) return 0;
    }
    return 1;
}

static int xls_read_record(const uint8_t *data, size_t size, size_t offset,
                           uint16_t *out_type, const uint8_t **out_payload,
                           size_t *out_payload_size, size_t *out_next)
{
    uint16_t payload_size;
    if (!data || offset > size || size - offset < 4u) return 0;
    payload_size = xls_u16(data + offset + 2u);
    if (payload_size > XLS_MAX_RECORD_BYTES ||
        (size_t)payload_size > size - offset - 4u) return 0;
    if (out_type) *out_type = xls_u16(data + offset);
    if (out_payload) *out_payload = data + offset + 4u;
    if (out_payload_size) *out_payload_size = payload_size;
    if (out_next) *out_next = offset + 4u + payload_size;
    return 1;
}

static int xls_probe_template_flag(const uint8_t *data, size_t size,
                                  int *out_is_template)
{
    static const char *const stream_names[] = {"Workbook", "Book"};
    uint8_t *workbook = NULL;
    size_t workbook_size = 0u;
    uint64_t stream_size = 0u;
    size_t i;
    int valid = 0;
    if (out_is_template) *out_is_template = 0;
    if (!out_is_template || !qw_ole_compound_has_signature(data, size))
        return 0;
    for (i = 0u; i < sizeof(stream_names) / sizeof(stream_names[0]); ++i) {
        if (qw_ole_compound_read_stream(
                data, size, stream_names[i], XLS_PROBE_PREFIX_BYTES,
                &workbook, &workbook_size, &stream_size)) break;
    }
    if (!workbook) return 0;
    {
        uint16_t type;
        const uint8_t *payload;
        size_t payload_size;
        size_t next;
        size_t offset;
        if (!xls_read_record(workbook, workbook_size, 0u, &type, &payload,
                             &payload_size, &next) ||
            type != XLS_RECORD_BOF || payload_size < 4u ||
            xls_u16(payload) != 0x0600u ||
            xls_u16(payload + 2u) != XLS_WORKBOOK_SUBSTREAM) goto cleanup;
        valid = 1;
        offset = next;
        while (offset < workbook_size) {
            if (!xls_read_record(workbook, workbook_size, offset, &type,
                                 &payload, &payload_size, &next)) break;
            if (type == XLS_RECORD_TEMPLATE) *out_is_template = 1;
            if (type == XLS_RECORD_EOF) break;
            offset = next;
        }
    }
cleanup:
    free(workbook);
    (void)stream_size;
    return valid;
}

static int xls_probe(const void *data, size_t size)
{
    int is_template = 0;
    return xls_probe_template_flag((const uint8_t *)data, size,
                                   &is_template) && !is_template;
}

static int xlt_probe(const void *data, size_t size)
{
    int is_template = 0;
    return xls_probe_template_flag((const uint8_t *)data, size,
                                   &is_template) && is_template;
}

static void xls_sheets_drop(XlsSheet *sheets, size_t count)
{
    size_t i;
    for (i = 0u; i < count; ++i) free(sheets[i].name);
    free(sheets);
}

static int xls_add_sheet(XlsReader *reader, XlsSheet *sheets,
                         size_t *sheet_count, const uint8_t *payload,
                         size_t payload_size)
{
    XlsString name = {0};
    size_t consumed = 0u;
    XlsSheet *sheet;
    if (!reader || !sheets || !sheet_count || payload_size < 8u ||
        *sheet_count >= XLS_MAX_SHEETS) return 0;
    if (!xls_decode_simple_unicode(payload + 6u, payload_size - 6u, 1u,
                                   &name, &consumed) ||
        consumed != payload_size - 6u || !name.data || !name.size) {
        xls_string_drop(&name);
        return 0;
    }
    sheet = &sheets[*sheet_count];
    memset(sheet, 0, sizeof(*sheet));
    sheet->stream_offset = xls_u32(payload);
    sheet->hidden_state = payload[4u] & 0x03u;
    sheet->sheet_type = payload[5u];
    sheet->name = name.data;
    sheet->name_size = name.size;
    if (sheet->sheet_type == 0u) ++*sheet_count;
    else free(sheet->name);
    return 1;
}

static int xls_parse_globals(XlsReader *reader, const uint8_t *workbook,
                             size_t workbook_size, XlsSheet *sheets,
                             size_t *out_sheet_count, int *out_encrypted)
{
    uint16_t type;
    const uint8_t *payload;
    size_t payload_size;
    size_t offset = 0u;
    size_t next;
    size_t sheet_count = 0u;
    int saw_eof = 0;
    if (out_sheet_count) *out_sheet_count = 0u;
    if (out_encrypted) *out_encrypted = 0;
    if (!reader || !workbook || !sheets || !out_sheet_count ||
        !out_encrypted ||
        !xls_read_record(workbook, workbook_size, 0u, &type, &payload,
                         &payload_size, &next) ||
        type != XLS_RECORD_BOF || payload_size < 4u ||
        xls_u16(payload) != 0x0600u ||
        xls_u16(payload + 2u) != XLS_WORKBOOK_SUBSTREAM) return 0;
    offset = next;
    while (offset < workbook_size) {
        if (++reader->visited_records > XLS_MAX_CELLS + XLS_MAX_SHEETS * 32u ||
            !xls_read_record(workbook, workbook_size, offset, &type, &payload,
                             &payload_size, &next)) return 0;
        if (type == XLS_RECORD_FILEPASS) {
            *out_encrypted = 1;
            return 0;
        }
        if (type == XLS_RECORD_BOUNDSHEET8) {
            if (!xls_add_sheet(reader, sheets, &sheet_count, payload,
                               payload_size)) return 0;
        } else if (type == XLS_RECORD_SST) {
            QwTextBuffer joined_sst;
            memset(&joined_sst, 0, sizeof(joined_sst));
            joined_sst.limit = (size_t)XLS_MAX_WORKBOOK_BYTES + 1u;
            if (!qw_text_buffer_append(&joined_sst, (const char *)payload,
                                       payload_size)) {
                free(joined_sst.data);
                return 0;
            }
            offset = next;
            while (offset < workbook_size) {
                uint16_t continuation_type;
                const uint8_t *continuation_payload;
                size_t continuation_size;
                size_t continuation_next;
                if (!xls_read_record(workbook, workbook_size, offset,
                                     &continuation_type,
                                     &continuation_payload,
                                     &continuation_size,
                                     &continuation_next) ||
                    continuation_type != XLS_RECORD_CONTINUE) break;
                if (!continuation_size || !qw_text_buffer_append(
                        &joined_sst, (const char *)continuation_payload,
                        continuation_size)) {
                    free(joined_sst.data);
                    return 0;
                }
                offset = continuation_next;
            }
            if (!xls_parse_sst(reader, (const uint8_t *)joined_sst.data,
                               joined_sst.size)) {
                free(joined_sst.data);
                return 0;
            }
            free(joined_sst.data);
            continue;
        } else if (type == XLS_RECORD_EOF) {
            saw_eof = 1;
            break;
        }
        offset = next;
    }
    if (!saw_eof || !sheet_count) return 0;
    reader->sheet_count = sheet_count;
    *out_sheet_count = sheet_count;
    return 1;
}

static int xls_finish_row(XlsReader *reader)
{
    if (!reader || !reader->row_open) return 1;
    reader->row_open = 0;
    return xls_html_literal(reader, "</tr>");
}

static int xls_append_cell(XlsReader *reader, uint32_t row, uint32_t column,
                           const char *value)
{
    char row_label[16];
    uint32_t blank_column;
    if (!reader || row >= XLS_MAX_ROWS ||
        column >= XLS_MAX_COLUMNS || ++reader->cell_count > XLS_MAX_CELLS)
        return 0;
    if (!value) value = "";
    if (!reader->row_open || row != reader->current_row) {
        if (reader->has_previous_row && row <= reader->previous_row) return 0;
        if (!xls_finish_row(reader)) return 0;
        (void)snprintf(row_label, sizeof(row_label), "%u", row + 1u);
        if (reader->capture_html &&
            (!xls_html_literal(reader, "<tr><th scope=\"row\">") ||
             !xls_html_escape(reader, row_label) ||
             !xls_html_literal(reader, "</th>"))) return 0;
        reader->current_row = row;
        reader->previous_row = row;
        reader->has_previous_row = 1;
        reader->next_column = 0u;
        reader->row_open = 1;
    }
    if (column < reader->next_column) return 0;
    if (reader->capture_html) {
        for (blank_column = reader->next_column; blank_column < column;
             ++blank_column) {
            if (!xls_html_literal(reader, "<td></td>")) return 0;
        }
        if (!xls_html_literal(reader, "<td>") ||
            !xls_html_escape(reader, value) ||
            !xls_html_literal(reader, "</td>")) return 0;
    }
    reader->next_column = column + 1u;
    return 1;
}

static int xls_append_value(XlsReader *reader, uint32_t row, uint32_t column,
                            const char *display_value,
                            QwSpreadsheetValueKind value_kind,
                            const uint8_t *value, size_t value_size)
{
    if (!reader || !xls_append_cell(reader, row, column, display_value))
        return 0;
    if (!reader->spreadsheet) return 1;
    if (!qw_spreadsheet_document_add_cell(reader->spreadsheet,
            reader->spreadsheet_sheet_index, row, column, value_kind,
            value, value_size, NULL, 0u)) {
        reader->spreadsheet_error = QW_STATUS_ERR_OUT_OF_MEMORY;
        return 0;
    }
    return 1;
}

static int xls_append_shared_cell(XlsReader *reader, uint32_t row,
                                  uint32_t column, uint32_t string_index)
{
    const XlsString *value;
    if (!reader || string_index >= reader->shared_string_count) return 0;
    value = &reader->shared_strings[string_index];
    return xls_append_value(reader, row, column, value->data,
        QW_SPREADSHEET_VALUE_STRING, (const uint8_t *)value->data,
        value->size);
}

static int xls_format_double(double value, char *output, size_t capacity)
{
    int length;
    if (!output || !capacity) return 0;
    if (value == 0.0) value = 0.0;
    length = snprintf(output, capacity, "%.15g", value);
    return length >= 0 && (size_t)length < capacity;
}

static double xls_decode_rk(uint32_t value)
{
    double number;
    if (value & 0x02u) {
        int32_t integer = (int32_t)value >> 2;
        number = (double)integer;
    } else {
        uint64_t bits = (uint64_t)(value & 0xfffffffcu) << 32;
        memcpy(&number, &bits, sizeof(number));
    }
    if (value & 0x01u) number /= 100.0;
    return number;
}

static int xls_append_numeric(XlsReader *reader, uint32_t row,
                              uint32_t column, double value)
{
    char text[64];
    if (!xls_format_double(value, text, sizeof(text))) return 0;
    return xls_append_value(reader, row, column, text,
        QW_SPREADSHEET_VALUE_NUMBER, (const uint8_t *)text, strlen(text));
}

static const char *xls_error_text(uint8_t error)
{
    switch (error) {
    case 0x00u: return "#NULL!";
    case 0x07u: return "#DIV/0!";
    case 0x0fu: return "#VALUE!";
    case 0x17u: return "#REF!";
    case 0x1du: return "#NAME?";
    case 0x24u: return "#NUM!";
    case 0x2au: return "#N/A";
    default: return "#ERROR!";
    }
}

static int xls_parse_sheet_record(XlsReader *reader, uint16_t type,
                                  const uint8_t *payload, size_t size,
                                  uint32_t *pending_row,
                                  uint32_t *pending_column,
                                  int *pending_string)
{
    uint32_t row;
    uint32_t column;
    if (!reader || !payload || !pending_row || !pending_column ||
        !pending_string) return 0;
    if (*pending_string && type != XLS_RECORD_STRING) return 0;
    switch (type) {
    case XLS_RECORD_LABELSST:
        if (size < 10u) return 0;
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        return xls_append_shared_cell(reader, row, column,
                                      xls_u32(payload + 6u));
    case XLS_RECORD_NUMBER: {
        uint64_t bits;
        double value;
        if (size < 14u) return 0;
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        bits = xls_u64(payload + 6u);
        memcpy(&value, &bits, sizeof(value));
        return xls_append_numeric(reader, row, column, value);
    }
    case XLS_RECORD_RK:
        if (size < 10u) return 0;
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        return xls_append_numeric(reader, row, column,
                                  xls_decode_rk(xls_u32(payload + 6u)));
    case XLS_RECORD_MULRK: {
        uint16_t first_column;
        uint16_t last_column;
        size_t pair_count;
        size_t i;
        if (size < 12u || (size - 6u - 2u) % 6u != 0u) return 0;
        row = xls_u16(payload);
        first_column = xls_u16(payload + 2u);
        last_column = xls_u16(payload + size - 2u);
        pair_count = (size - 8u) / 6u;
        if (!pair_count || first_column >= XLS_MAX_COLUMNS ||
            last_column != first_column + pair_count - 1u) return 0;
        for (i = 0u; i < pair_count; ++i) {
            uint32_t rk = xls_u32(payload + 8u + i * 6u);
            if (!xls_append_numeric(reader, row,
                                    (uint32_t)first_column + (uint32_t)i,
                                    xls_decode_rk(rk))) return 0;
        }
        return 1;
    }
    case XLS_RECORD_BOOLERR:
        if (size < 8u) return 0;
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        if (payload[7u]) {
            const char *error = xls_error_text(payload[6u]);
            return xls_append_value(reader, row, column, error,
                QW_SPREADSHEET_VALUE_ERROR, (const uint8_t *)error,
                strlen(error));
        }
        return xls_append_value(reader, row, column,
            payload[6u] ? "TRUE" : "FALSE", QW_SPREADSHEET_VALUE_BOOLEAN,
            (const uint8_t *)(payload[6u] ? "1" : "0"), 1u);
    case XLS_RECORD_LABEL: {
        XlsString value = {0};
        size_t consumed = 0u;
        int ok;
        if (size < 9u || !xls_decode_simple_unicode(payload + 6u,
                size - 6u, 2u, &value, &consumed) ||
            consumed != size - 6u) {
            xls_string_drop(&value);
            return 0;
        }
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        ok = xls_append_value(reader, row, column, value.data,
            QW_SPREADSHEET_VALUE_STRING, (const uint8_t *)value.data,
            value.size);
        xls_string_drop(&value);
        return ok;
    }
    case XLS_RECORD_FORMULA: {
        const uint8_t *result;
        uint16_t formula_size;
        if (size < 22u) return 0;
        row = xls_u16(payload);
        column = xls_u16(payload + 2u);
        result = payload + 6u;
        formula_size = xls_u16(payload + 20u);
        if ((size_t)formula_size > size - 22u) return 0;
        if (xls_u16(result + 6u) == 0xffffu) {
            switch (result[0]) {
            case 0u:
                *pending_row = row;
                *pending_column = column;
                *pending_string = 1;
                return 1;
            case 1u:
                return xls_append_value(reader, row, column,
                    result[2] ? "TRUE" : "FALSE",
                    QW_SPREADSHEET_VALUE_BOOLEAN,
                    (const uint8_t *)(result[2] ? "1" : "0"), 1u);
            case 2u: {
                const char *error = xls_error_text(result[2]);
                return xls_append_value(reader, row, column, error,
                    QW_SPREADSHEET_VALUE_ERROR, (const uint8_t *)error,
                    strlen(error));
            }
            case 3u:
                return xls_append_value(reader, row, column, "",
                    QW_SPREADSHEET_VALUE_EMPTY, NULL, 0u);
            default:
                return 0;
            }
        } else {
            uint64_t bits = xls_u64(result);
            double value;
            memcpy(&value, &bits, sizeof(value));
            return xls_append_numeric(reader, row, column, value);
        }
    }
    case XLS_RECORD_STRING: {
        XlsString value = {0};
        size_t consumed = 0u;
        int ok;
        if (!*pending_string || !xls_decode_simple_unicode(
                payload, size, 2u, &value, &consumed) ||
            consumed != size) {
            xls_string_drop(&value);
            return 0;
        }
        ok = xls_append_value(reader, *pending_row, *pending_column,
            value.data, QW_SPREADSHEET_VALUE_STRING,
            (const uint8_t *)value.data, value.size);
        xls_string_drop(&value);
        *pending_string = 0;
        return ok;
    }
    default:
        return 1;
    }
}

static int xls_render_sheet(XlsReader *reader, const uint8_t *workbook,
                            size_t workbook_size, const XlsSheet *sheet)
{
    size_t offset;
    uint16_t type;
    const uint8_t *payload;
    size_t payload_size;
    size_t next;
    uint32_t pending_row = 0u;
    uint32_t pending_column = 0u;
    int pending_string = 0;
    int saw_eof = 0;
    if (!reader || !workbook || !sheet ||
        sheet->stream_offset >= workbook_size ||
        !xls_read_record(workbook, workbook_size, sheet->stream_offset,
                         &type, &payload, &payload_size, &next) ||
        type != XLS_RECORD_BOF || payload_size < 4u ||
        xls_u16(payload) != 0x0600u ||
        xls_u16(payload + 2u) != XLS_WORKSHEET_SUBSTREAM) return 0;
    reader->row_open = 0;
    reader->has_previous_row = 0;
    reader->current_row = 0u;
    reader->previous_row = 0u;
    reader->next_column = 0u;
    if (!xls_html_literal(reader, "<section><h2>") ||
        !xls_html_escape(reader, sheet->name) ||
        !xls_html_literal(reader, "</h2><table><tbody>")) return 0;
    offset = next;
    while (offset < workbook_size) {
        if (++reader->visited_records > XLS_MAX_CELLS * 2u ||
            !xls_read_record(workbook, workbook_size, offset, &type,
                             &payload, &payload_size, &next)) return 0;
        if (type == XLS_RECORD_EOF) {
            saw_eof = 1;
            break;
        }
        if (!xls_parse_sheet_record(reader, type, payload, payload_size,
                                    &pending_row, &pending_column,
                                    &pending_string)) return 0;
        offset = next;
    }
    if (!saw_eof || pending_string || !xls_finish_row(reader))
        return 0;
    return xls_html_literal(reader, "</tbody></table></section>");
}

static int xls_build_workbook(const void *data, size_t size,
                              QwInputFormat input_format,
                              XlsReader *reader,
                              QwSpreadsheetDocument *spreadsheet,
                              int capture_html,
                              QwConvertReport *report)
{
    static const char *const stream_names[] = {"Workbook", "Book"};
    uint8_t *workbook = NULL;
    size_t workbook_size = 0u;
    uint64_t stream_size = 0u;
    XlsSheet *sheets = NULL;
    size_t sheet_count = 0u;
    size_t i;
    int encrypted = 0;
    int rc = QW_STATUS_ERR_FORMAT;
    if (!reader || !data || !size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(reader, 0, sizeof(*reader));
    reader->spreadsheet = spreadsheet;
    reader->capture_html = capture_html;
    for (i = 0u; i < sizeof(stream_names) / sizeof(stream_names[0]); ++i) {
        if (qw_ole_compound_read_stream((const uint8_t *)data, size,
                stream_names[i], XLS_MAX_WORKBOOK_BYTES, &workbook,
                &workbook_size, &stream_size)) break;
    }
    if (!workbook) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
            "XLS/XLT compound file has no supported Workbook stream");
        return QW_STATUS_ERR_FORMAT;
    }
    if (stream_size > XLS_MAX_WORKBOOK_BYTES ||
        workbook_size != (size_t)stream_size) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
            "XLS/XLT Workbook stream exceeds the 128 MiB processing limit");
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
        goto cleanup;
    }
    sheets = (XlsSheet *)calloc(XLS_MAX_SHEETS, sizeof(*sheets));
    if (!sheets) {
        rc = QW_STATUS_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (!xls_parse_globals(reader, workbook, workbook_size, sheets,
                           &sheet_count, &encrypted)) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
            encrypted ? "Encrypted XLS/XLT workbooks are not supported"
                      : "XLS/XLT BIFF8 workbook globals are invalid or exceed the supported subset");
        rc = encrypted ? QW_STATUS_ERR_UNSUPPORTED : QW_STATUS_ERR_DECODE;
        goto cleanup;
    }
    if (capture_html) reader->html.limit = (size_t)XLS_MAX_HTML_BYTES + 1u;
    if (capture_html && !xls_html_literal(reader,
            "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
            "@page{size:landscape;margin:12mm}body{font-family:sans-serif;"
            "font-size:10pt;color:#111}h2{font-size:14pt;margin:18pt 0 6pt}"
            "table{border-collapse:collapse;margin:0 0 16pt}td,th{border:1px solid #888;"
            "padding:3pt 6pt;vertical-align:top;white-space:pre-wrap}"
            "th{background:#eee;text-align:right;font-weight:400}</style></head><body>")) {
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
        goto cleanup;
    }
    for (i = 0u; i < sheet_count; ++i) {
        if (spreadsheet) {
            size_t model_sheet_index;
            if (!qw_spreadsheet_document_add_sheet(spreadsheet,
                    (const uint8_t *)sheets[i].name, sheets[i].name_size,
                    &model_sheet_index)) {
                if (report) (void)snprintf(report->note, sizeof(report->note),
                    "XLS/XLT workbook sheet metadata exceeds available resources");
                rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                goto cleanup;
            }
            reader->spreadsheet_sheet_index = model_sheet_index;
        }
        if (!xls_render_sheet(reader, workbook, workbook_size, &sheets[i])) {
            if (report) (void)snprintf(report->note, sizeof(report->note),
                reader->spreadsheet_error
                    ? "XLS/XLT typed workbook exceeds available memory"
                    : reader->output_limit_hit
                    ? "XLS/XLT HTML layout exceeds the 64 MiB output limit"
                    : "XLS/XLT worksheet contains an unsupported or invalid BIFF8 record sequence");
            rc = reader->spreadsheet_error ? reader->spreadsheet_error
                : reader->output_limit_hit ? QW_STATUS_ERR_RESOURCE_LIMIT
                                           : QW_STATUS_ERR_DECODE;
            goto cleanup;
        }
    }
    if (capture_html && !xls_html_literal(reader, "</body></html>")) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
            "XLS/XLT HTML layout exceeds the 64 MiB output limit");
        rc = QW_STATUS_ERR_RESOURCE_LIMIT;
        goto cleanup;
    }
    if (spreadsheet && !qw_spreadsheet_document_validate(spreadsheet)) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
            "XLS/XLT typed workbook model validation failed");
        rc = QW_STATUS_ERR_FORMAT;
        goto cleanup;
    }
    rc = QW_STATUS_OK;
    if (report) {
        if (spreadsheet) {
            (void)snprintf(report->note, sizeof(report->note),
                "%s BIFF8 scalar cells and cached formula results opened as a typed workbook; formulas are not retained or recalculated, and formatting, number formats, merged cells, charts, macros, objects and template behavior are not preserved",
                input_format == QW_FORMAT_XLT ? "XLT" : "XLS");
        } else {
            (void)snprintf(report->note, sizeof(report->note),
                "%s BIFF8 worksheet values and cached formula results opened as HTML layout; encryption, split shared-string continuations, formatting, number formats, merged cells, charts, macros, objects and template behavior are not preserved",
                input_format == QW_FORMAT_XLT ? "XLT" : "XLS");
        }
    }

cleanup:
    free(workbook);
    xls_sheets_drop(sheets, sheets ? XLS_MAX_SHEETS : 0u);
    (void)sheet_count;
    return rc;
}

static int xls_open_layout(const void *data, size_t size,
                           QwInputFormat input_format,
                           const QwConvertOptions *options,
                           QwLayoutDocument *out_document,
                           QwConvertReport *report)
{
    XlsReader reader;
    int rc;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(out_document, 0, sizeof(*out_document));
    memset(&reader, 0, sizeof(reader));
    if (input_format != QW_FORMAT_XLS && input_format != QW_FORMAT_XLT)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = xls_build_workbook(data, size, input_format, &reader, NULL, 1,
                            report);
    if (rc != QW_STATUS_OK) {
        xls_reader_drop(&reader);
        return rc;
    }
    out_document->html_utf8 = (const uint8_t *)reader.html.data;
    out_document->html_size = reader.html.size;
    out_document->context = reader.html.data;
    out_document->dispose = free;
    reader.html.data = NULL;
    xls_reader_drop(&reader);
    return QW_STATUS_OK;
}

static int xls_open_spreadsheet(const void *data, size_t size,
                                QwInputFormat input_format,
                                const QwConvertOptions *options,
                                QwSpreadsheetDocument *out_document,
                                QwConvertReport *report)
{
    XlsReader reader;
    int rc;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    qw_spreadsheet_document_init(out_document, input_format);
    if (input_format != QW_FORMAT_XLS && input_format != QW_FORMAT_XLT)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(&reader, 0, sizeof(reader));
    rc = xls_build_workbook(data, size, input_format, &reader, out_document,
                            0, report);
    xls_reader_drop(&reader);
    if (rc != QW_STATUS_OK) qw_spreadsheet_document_dispose(out_document);
    return rc;
}

#ifndef QUIREWISE_STANDALONE_SPREADSHEET
const QwFormatDescriptor qw_format_xls = {
    .id = QW_FORMAT_XLS,
    .name = "XLS",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE,
    .probe = xls_probe,
    .open_memory = NULL,
    .open_layout = xls_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL,
    .open_spreadsheet = xls_open_spreadsheet
};
#endif

int qw_xls_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_document)
{
    return xls_open_spreadsheet(data, size, input_format, NULL, out_document,
                                NULL);
}

const QwFormatDescriptor qw_format_xlt = {
    .id = QW_FORMAT_XLT,
    .name = "XLT",
    .capabilities = QW_FORMAT_CAP_CONTAINER | QW_FORMAT_CAP_MULTI_PAGE,
    .probe = xlt_probe,
    .open_memory = NULL,
    .open_layout = xls_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_DEFAULT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = NULL,
    .open_spreadsheet = xls_open_spreadsheet
};
