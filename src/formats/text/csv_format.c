#include "../../core/format_registry.h"
#include "../../core/text_buffer.h"
#include "../spreadsheet/spreadsheet_parser_internal.h"

#ifndef QUIREWISE_STANDALONE_SPREADSHEET
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    QW_CSV_PROBE_LIMIT = 65536,
    QW_CSV_MAX_INPUT = 256 * 1024 * 1024,
    QW_CSV_HTML_LIMIT = 512 * 1024 * 1024,
    QW_CSV_TYPED_FIELD_LIMIT = 1024 * 1024,
    QW_CSV_TYPED_CELL_LIMIT = 1000000
};

typedef struct QwCsvProbeStats {
    size_t rows;
    size_t expected_columns;
    size_t current_columns;
    int in_quotes;
    int after_quote;
    int field_start;
    int row_has_content;
    int inconsistent;
} QwCsvProbeStats;

static int csv_literal(QwTextBuffer *buffer, const char *value)
{
    return qw_text_buffer_append(buffer, value, strlen(value));
}

static int csv_utf8_valid(const uint8_t *data, size_t size)
{
    size_t i = 0;
    while (i < size) {
        unsigned char first = data[i];
        size_t count;
        if (first < 0x80u) {
            if (first == 0u || (first < 0x20u && first != '\r' &&
                               first != '\n' && first != '\t')) return 0;
            count = 1u;
        } else if (first >= 0xc2u && first <= 0xdfu) count = 2u;
        else if (first >= 0xe0u && first <= 0xefu) count = 3u;
        else if (first >= 0xf0u && first <= 0xf4u) count = 4u;
        else return 0;
        if (count > size - i) return 0;
        for (size_t j = 1u; j < count; ++j) {
            if ((data[i + j] & 0xc0u) != 0x80u) return 0;
        }
        if ((first == 0xe0u && data[i + 1u] < 0xa0u) ||
            (first == 0xedu && data[i + 1u] >= 0xa0u) ||
            (first == 0xf0u && data[i + 1u] < 0x90u) ||
            (first == 0xf4u && data[i + 1u] >= 0x90u)) return 0;
        i += count;
    }
    return 1;
}

static void csv_probe_finish_row(QwCsvProbeStats *stats)
{
    if (stats->row_has_content || stats->current_columns > 1u) {
        if (!stats->expected_columns) stats->expected_columns = stats->current_columns;
        else if (stats->expected_columns != stats->current_columns)
            stats->inconsistent = 1;
        ++stats->rows;
    }
    stats->current_columns = 1u;
    stats->field_start = 1;
    stats->after_quote = 0;
    stats->row_has_content = 0;
}

static int csv_probe_delimiter(const uint8_t *data, size_t size,
                               unsigned char delimiter, size_t *rows_out,
                               size_t *columns_out)
{
    size_t scan_size = size < QW_CSV_PROBE_LIMIT ? size : QW_CSV_PROBE_LIMIT;
    QwCsvProbeStats stats;
    size_t i = 0;
    memset(&stats, 0, sizeof(stats));
    stats.current_columns = 1u;
    stats.field_start = 1;

    while (i < scan_size) {
        unsigned char ch = data[i++];
        if (stats.in_quotes) {
            if (ch == '"') {
                if (i < scan_size && data[i] == '"') {
                    ++i;
                } else {
                    stats.in_quotes = 0;
                    stats.after_quote = 1;
                }
            }
            continue;
        }
        if (stats.after_quote && ch != delimiter && ch != '\r' && ch != '\n' &&
            ch != ' ' && ch != '\t') return 0;
        if (stats.field_start && ch == '"') {
            stats.in_quotes = 1;
            stats.field_start = 0;
            stats.row_has_content = 1;
        } else if (ch == '"') {
            return 0;
        } else if (ch == delimiter) {
            ++stats.current_columns;
            stats.field_start = 1;
            stats.after_quote = 0;
            stats.row_has_content = 1;
        } else if (ch == '\r' || ch == '\n') {
            csv_probe_finish_row(&stats);
            if (ch == '\r' && i < scan_size && data[i] == '\n') ++i;
        } else {
            stats.field_start = 0;
            stats.after_quote = 0;
            stats.row_has_content = 1;
        }
    }
    if (stats.in_quotes) {
        if (scan_size == size) return 0;
        /* Ignore the incomplete sample tail, but keep complete earlier rows. */
    } else if (stats.row_has_content || stats.current_columns > 1u) {
        csv_probe_finish_row(&stats);
    }
    if (stats.inconsistent || stats.rows < 2u || stats.expected_columns < 2u) return 0;
    *rows_out = stats.rows;
    *columns_out = stats.expected_columns;
    return 1;
}

static int csv_choose_delimiter(const uint8_t *data, size_t size,
                                unsigned char *delimiter,
                                size_t *rows, size_t *columns)
{
    static const unsigned char candidates[] = { ',', '\t', ';' };
    size_t best_score = 0u;
    int found = 0;
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        size_t candidate_rows = 0u;
        size_t candidate_columns = 0u;
        size_t score;
        if (!csv_probe_delimiter(data, size, candidates[i],
                                 &candidate_rows, &candidate_columns)) continue;
        score = candidate_rows * candidate_columns;
        if (!found || score > best_score) {
            found = 1;
            best_score = score;
            *delimiter = candidates[i];
            *rows = candidate_rows;
            *columns = candidate_columns;
        }
    }
    return found;
}

static int csv_probe(const void *input, size_t size)
{
    const uint8_t *data = (const uint8_t *)input;
    size_t offset = 0u;
    size_t sample_size;
    unsigned char delimiter;
    size_t rows, columns;
    if (!data || !size || size > QW_CSV_MAX_INPUT) return 0;
    if (size >= 3u && data[0] == 0xefu && data[1] == 0xbbu && data[2] == 0xbfu)
        offset = 3u;
    sample_size = size - offset;
    if (sample_size > QW_CSV_PROBE_LIMIT) sample_size = QW_CSV_PROBE_LIMIT;
    if (!csv_utf8_valid(data + offset, sample_size)) {
        size_t trim;
        int valid_prefix = 0;
        for (trim = 1u; trim <= 3u && trim < sample_size; ++trim) {
            if (csv_utf8_valid(data + offset, sample_size - trim)) {
                sample_size -= trim;
                valid_prefix = 1;
                break;
            }
        }
        if (!valid_prefix) return 0;
    }
    return csv_choose_delimiter(data + offset, size - offset,
                                &delimiter, &rows, &columns);
}

static int csv_html_append_text(QwTextBuffer *html,
                               const uint8_t *data,
                               size_t size)
{
    size_t start = 0u;
    for (size_t i = 0u; i < size; ++i) {
        const char *escaped = NULL;
        if (data[i] == '&') escaped = "&amp;";
        else if (data[i] == '<') escaped = "&lt;";
        else if (data[i] == '>') escaped = "&gt;";
        else if (data[i] == '"') escaped = "&quot;";
        if (escaped) {
            if (i > start && !qw_text_buffer_append(html, (const char *)data + start, i - start))
                return 0;
            if (!csv_literal(html, escaped)) return 0;
            start = i + 1u;
        }
    }
    return start == size ||
           qw_text_buffer_append(html, (const char *)data + start, size - start);
}

static int csv_field_append(QwTextBuffer *field, const uint8_t *data,
                            size_t size)
{
    if (!field || (!data && size)) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size > QW_CSV_TYPED_FIELD_LIMIT - field->size)
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    return qw_text_buffer_append(field, (const char *)data, size)
        ? QW_STATUS_OK : QW_STATUS_ERR_OUT_OF_MEMORY;
}

static int csv_commit_typed_cell(QwSpreadsheetDocument *spreadsheet,
                                 QwTextBuffer *field,
                                 size_t row, size_t column)
{
    if (!spreadsheet) return QW_STATUS_OK;
    if (!qw_spreadsheet_document_add_cell(spreadsheet, 0u, row, column,
            QW_SPREADSHEET_VALUE_STRING, (const uint8_t *)field->data,
            field->size, NULL, 0u)) {
        return spreadsheet->total_cell_count >= QW_CSV_TYPED_CELL_LIMIT
            ? QW_STATUS_ERR_RESOURCE_LIMIT : QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    field->size = 0u;
    if (field->data) field->data[0] = '\0';
    return QW_STATUS_OK;
}

static int csv_process_grid(const void *input,
                          size_t size,
                          QwTextBuffer *out_html,
                          unsigned char *out_delimiter,
                          size_t *out_rows,
                          size_t *out_columns,
                          QwSpreadsheetDocument *spreadsheet,
                          int capture_html)
{
    static const char prefix[] =
        "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
        "@page{size:A4 landscape;margin:12mm}body{font-family:sans-serif;font-size:9pt}"
        "table{border-collapse:collapse;table-layout:auto;width:100%}"
        "td{border:0.5pt solid #777;padding:3pt;vertical-align:top;"
        "white-space:pre-wrap;overflow-wrap:anywhere}</style></head><body><table><tbody>";
    static const char suffix[] = "</tbody></table></body></html>";
    const uint8_t *data = (const uint8_t *)input;
    QwTextBuffer html;
    QwTextBuffer field;
    unsigned char delimiter;
    size_t rows_hint, columns_hint;
    size_t offset = 0u;
    size_t rows_written = 0u;
    size_t columns_written = 0u;
    size_t row_index = 0u;
    size_t i = 0u;
    size_t row_columns = 1u;
    int in_quotes = 0;
    int after_quote = 0;
    int field_start = 1;
    int row_open = 0;
    int row_has_content = 0;
    int rc = QW_STATUS_OK;

    if (!input || !size || (capture_html && !out_html) || !out_delimiter ||
        !out_rows || !out_columns) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (out_html) memset(out_html, 0, sizeof(*out_html));
    *out_delimiter = 0u;
    *out_rows = 0u;
    *out_columns = 0u;
    if (size > QW_CSV_MAX_INPUT) return QW_STATUS_ERR_UNSUPPORTED;
    if (!csv_probe(input, size)) return QW_STATUS_ERR_FORMAT;
    if (size >= 3u && data[0] == 0xefu && data[1] == 0xbbu && data[2] == 0xbfu)
        offset = 3u;
    data += offset;
    size -= offset;
    if (!csv_utf8_valid(data, size)) return QW_STATUS_ERR_DECODE;
    if (!csv_choose_delimiter(data, size, &delimiter, &rows_hint, &columns_hint))
        return QW_STATUS_ERR_FORMAT;

    memset(&html, 0, sizeof(html));
    memset(&field, 0, sizeof(field));
    if (spreadsheet) {
        size_t sheet_index;
        field.limit = QW_CSV_TYPED_FIELD_LIMIT;
        if (!qw_spreadsheet_document_add_sheet(spreadsheet,
                (const uint8_t *)"Sheet1", 6u, &sheet_index) ||
            sheet_index != 0u) return QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    if (capture_html) html.limit = QW_CSV_HTML_LIMIT;
    if (capture_html &&
        !qw_text_buffer_append(&html, prefix, sizeof(prefix) - 1u)) {
        free(html.data);
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    while (i < size) {
        unsigned char ch = data[i++];
        if (!row_open) {
            if (capture_html && !csv_literal(&html, "<tr><td>")) {
                rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                break;
            }
            row_open = 1;
        }
        if (in_quotes) {
            if (ch == '"') {
                if (i < size && data[i] == '"') {
                    ++i;
                    if (spreadsheet &&
                        (rc = csv_field_append(&field,
                            (const uint8_t *)"\"", 1u)) != QW_STATUS_OK) break;
                    if (capture_html &&
                        !csv_html_append_text(&html, (const uint8_t *)"\"", 1u)) {
                        rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                        break;
                    }
                    row_has_content = 1;
                } else {
                    in_quotes = 0;
                    after_quote = 1;
                }
            } else {
                if (spreadsheet && (rc = csv_field_append(&field, &ch, 1u)) != QW_STATUS_OK)
                    break;
                if (capture_html && !csv_html_append_text(&html, &ch, 1u)) {
                    rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                    break;
                }
                row_has_content = 1;
            }
            continue;
        }
        if (after_quote && ch != delimiter && ch != '\r' && ch != '\n' &&
            ch != ' ' && ch != '\t') {
            rc = QW_STATUS_ERR_FORMAT;
            break;
        }
        if (field_start && ch == '"') {
            in_quotes = 1;
            field_start = 0;
            row_has_content = 1;
        } else if (ch == '"') {
            rc = QW_STATUS_ERR_FORMAT;
            break;
        } else if (ch == delimiter) {
            if (spreadsheet &&
                (rc = csv_commit_typed_cell(spreadsheet, &field,
                    row_index, row_columns - 1u)) != QW_STATUS_OK) break;
            if (capture_html && !csv_literal(&html, "</td><td>")) {
                rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                break;
            }
            ++row_columns;
            columns_written = row_columns > columns_written ? row_columns : columns_written;
            field_start = 1;
            after_quote = 0;
            row_has_content = 1;
        } else if (ch == '\r' || ch == '\n') {
            if (spreadsheet &&
                (rc = csv_commit_typed_cell(spreadsheet, &field,
                    row_index, row_columns - 1u)) != QW_STATUS_OK) break;
            if (capture_html && !csv_literal(&html, "</td></tr>")) {
                rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                break;
            }
            ++rows_written;
            ++row_index;
            row_open = 0;
            row_columns = 1u;
            field_start = 1;
            after_quote = 0;
            row_has_content = 0;
            if (ch == '\r' && i < size && data[i] == '\n') ++i;
        } else if (after_quote && (ch == ' ' || ch == '\t')) {
            /* Tolerate padding after a quoted field. */
        } else {
            if (spreadsheet && (rc = csv_field_append(&field, &ch, 1u)) != QW_STATUS_OK)
                break;
            if (capture_html && !csv_html_append_text(&html, &ch, 1u)) {
                rc = QW_STATUS_ERR_OUT_OF_MEMORY;
                break;
            }
            field_start = 0;
            after_quote = 0;
            row_has_content = 1;
        }
    }
    if (rc == QW_STATUS_OK && in_quotes) rc = QW_STATUS_ERR_FORMAT;
    if (rc == QW_STATUS_OK && row_open && (row_has_content || row_columns > 1u)) {
        if (spreadsheet)
            rc = csv_commit_typed_cell(spreadsheet, &field,
                                       row_index, row_columns - 1u);
        if (rc == QW_STATUS_OK && capture_html &&
            !csv_literal(&html, "</td></tr>")) rc = QW_STATUS_ERR_OUT_OF_MEMORY;
        if (rc == QW_STATUS_OK) ++rows_written;
    }
    if (rc == QW_STATUS_OK && capture_html &&
        !qw_text_buffer_append(&html, suffix, sizeof(suffix) - 1u))
        rc = QW_STATUS_ERR_OUT_OF_MEMORY;
    if (rc == QW_STATUS_OK) {
        if (out_html && capture_html) {
            *out_html = html;
            memset(&html, 0, sizeof(html));
        }
        *out_delimiter = delimiter;
        *out_rows = rows_written;
        *out_columns = columns_written ? columns_written : columns_hint;
    }
    free(html.data);
    free(field.data);
    (void)rows_hint;
    return rc;
}

typedef struct QwCsvLayoutOwner {
    char *html;
} QwCsvLayoutOwner;

static void csv_layout_dispose(void *context)
{
    QwCsvLayoutOwner *owner = (QwCsvLayoutOwner *)context;
    if (!owner) return;
    free(owner->html);
    free(owner);
}

static int csv_open_layout(const void *input,
                           size_t size,
                           QwInputFormat input_format,
                           const QwConvertOptions *options,
                           QwLayoutDocument *out_document,
                           QwConvertReport *report)
{
    QwTextBuffer html = {0};
    unsigned char delimiter;
    size_t rows;
    size_t columns;
    QwCsvLayoutOwner *owner;
    int rc;
    (void)input_format;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(out_document, 0, sizeof(*out_document));
    rc = csv_process_grid(input, size, &html, &delimiter, &rows, &columns,
                          NULL, 1);
    if (rc != QW_STATUS_OK) return rc;
    owner = (QwCsvLayoutOwner *)calloc(1u, sizeof(*owner));
    if (!owner) {
        free(html.data);
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    owner->html = html.data;
    out_document->html_utf8 = (const uint8_t *)owner->html;
    out_document->html_size = html.size;
    out_document->context = owner;
    out_document->dispose = csv_layout_dispose;
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
            "%s table opened as an HTML-backed layout (%zu rows, up to %zu columns); cell values remain text",
            delimiter == '\t' ? "TSV" : "CSV", rows, columns);
    }
    return QW_STATUS_OK;
}

static int csv_open_spreadsheet(const void *input, size_t size,
                                QwInputFormat input_format,
                                const QwConvertOptions *options,
                                QwSpreadsheetDocument *out_document,
                                QwConvertReport *report)
{
    unsigned char delimiter;
    size_t rows;
    size_t columns;
    int rc;
    (void)options;
    if (!out_document) return QW_STATUS_ERR_INVALID_ARGUMENT;
    qw_spreadsheet_document_init(out_document, input_format);
    if (input_format != QW_FORMAT_CSV && input_format != QW_FORMAT_TSV)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = csv_process_grid(input, size, NULL, &delimiter, &rows, &columns,
                          out_document, 0);
    if (rc != QW_STATUS_OK) {
        qw_spreadsheet_document_dispose(out_document);
        return rc;
    }
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
            "%s parsed as one typed workbook with string-valued cells (%zu rows, up to %zu columns); field types are not inferred",
            delimiter == '\t' ? "TSV" : "CSV", rows, columns);
    }
    return QW_STATUS_OK;
}

#ifndef QUIREWISE_STANDALONE_SPREADSHEET
static int csv_convert_to_pdf(const void *input,
                              size_t size,
                              const QwConvertOptions *options,
                              uint8_t **out_pdf,
                              size_t *out_size,
                              QwConvertReport *report)
{
    QwTextBuffer html = {0};
    unsigned char delimiter;
    size_t rows;
    size_t columns;
    int rc;
    (void)options;
    if (!out_pdf || !out_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *out_pdf = NULL;
    *out_size = 0u;
    rc = csv_process_grid(input, size, &html, &delimiter, &rows, &columns,
                          NULL, 1);
    if (rc == QW_STATUS_OK) {
        /* The CSV grid is validated and converted to HTML, but emitting PDF is
         * the job of the internal Quirewise layout/PDF renderer, which this
         * package does not provide yet. */
        rc = QW_STATUS_ERR_UNSUPPORTED;
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                "%s table parsed (%zu rows, up to %zu columns); PDF rendering requires the internal Quirewise layout/PDF renderer",
                delimiter == '\t' ? "TSV" : "CSV", rows, columns);
        }
    }
    free(html.data);
    return rc;
}

const QwFormatDescriptor qw_format_csv = {
    .id = QW_FORMAT_CSV,
    .name = "CSV/TSV",
    .capabilities = (QW_FORMAT_CAP_MULTI_PAGE) | (0u),
    .probe = csv_probe,
    .open_memory = NULL,
    .open_layout = csv_open_layout,
    .open_audio = NULL,
    .max_input_bytes = QW_FORMAT_INPUT_LIMIT_TEXT,
    .resource_profile = QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT,
    .convert_to_pdf = csv_convert_to_pdf,
    .open_spreadsheet = csv_open_spreadsheet
};
#endif

int qw_csv_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_document)
{
    return csv_open_spreadsheet(data, size, input_format, NULL, out_document,
                                NULL);
}
