#include "quirewise/spreadsheet.h"

#include "quirewise/status.h"
#include "spreadsheet_parser_internal.h"

#include <stdlib.h>
#include <string.h>

static QwSpreadsheetStatus spreadsheet_status_map(int status)
{
    switch (status) {
    case QW_STATUS_OK: return QW_SPREADSHEET_OK;
    case QW_STATUS_ERR_INVALID_ARGUMENT: return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    case QW_STATUS_ERR_OUT_OF_MEMORY: return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
    case QW_STATUS_ERR_UNSUPPORTED: return QW_SPREADSHEET_ERR_UNSUPPORTED;
    case QW_STATUS_ERR_RESOURCE_LIMIT: return QW_SPREADSHEET_ERR_RESOURCE_LIMIT;
    case QW_STATUS_ERR_DECODE: return QW_SPREADSHEET_ERR_DECODE;
    default: return QW_SPREADSHEET_ERR_FORMAT;
    }
}

QwSpreadsheetStatus qw_spreadsheet_parse(QwSpreadsheetFormat format,
                                         const void *data, size_t size,
                                         QwSpreadsheetDocument **out_document)
{
    QwSpreadsheetDocument *document;
    int status;
    QwInputFormat internal_format;
    if (!out_document) return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    *out_document = NULL;
    if (!data || !size) return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    switch (format) {
    case QW_SPREADSHEET_FORMAT_CSV: internal_format = QW_FORMAT_CSV; break;
    case QW_SPREADSHEET_FORMAT_TSV: internal_format = QW_FORMAT_TSV; break;
    case QW_SPREADSHEET_FORMAT_XLS: internal_format = QW_FORMAT_XLS; break;
    case QW_SPREADSHEET_FORMAT_XLT: internal_format = QW_FORMAT_XLT; break;
    case QW_SPREADSHEET_FORMAT_XLSX: internal_format = QW_FORMAT_XLSX; break;
    case QW_SPREADSHEET_FORMAT_XLSM: internal_format = QW_FORMAT_XLSM; break;
    case QW_SPREADSHEET_FORMAT_XLTX: internal_format = QW_FORMAT_XLTX; break;
    case QW_SPREADSHEET_FORMAT_ODS: internal_format = QW_FORMAT_ODS; break;
    case QW_SPREADSHEET_FORMAT_OTS: internal_format = QW_FORMAT_OTS; break;
    case QW_SPREADSHEET_FORMAT_FODS: internal_format = QW_FORMAT_FODS; break;
    default: return QW_SPREADSHEET_ERR_UNSUPPORTED;
    }
    document = (QwSpreadsheetDocument *)calloc(1u, sizeof(*document));
    if (!document) return QW_SPREADSHEET_ERR_OUT_OF_MEMORY;
    if (format == QW_SPREADSHEET_FORMAT_CSV ||
        format == QW_SPREADSHEET_FORMAT_TSV) {
        status = qw_csv_parse_spreadsheet(data, size, internal_format, document);
    } else if (format == QW_SPREADSHEET_FORMAT_XLS ||
               format == QW_SPREADSHEET_FORMAT_XLT) {
        status = qw_xls_parse_spreadsheet(data, size, internal_format, document);
    } else if (format == QW_SPREADSHEET_FORMAT_ODS ||
               format == QW_SPREADSHEET_FORMAT_OTS ||
               format == QW_SPREADSHEET_FORMAT_FODS) {
        status = qw_ods_parse_spreadsheet(data, size, internal_format, document);
    } else {
        status = qw_xlsx_parse_spreadsheet(data, size, internal_format, document);
    }
    if (status != QW_STATUS_OK) {
        qw_spreadsheet_document_dispose(document);
        free(document);
        return spreadsheet_status_map(status);
    }
    *out_document = document;
    return QW_SPREADSHEET_OK;
}

void qw_spreadsheet_document_free(QwSpreadsheetDocument *document)
{
    if (!document) return;
    qw_spreadsheet_document_dispose(document);
    free(document);
}

QwSpreadsheetFormat qw_spreadsheet_document_format(
    const QwSpreadsheetDocument *document)
{
    if (!document) return QW_SPREADSHEET_FORMAT_INVALID;
    switch (document->input_format) {
    case QW_FORMAT_CSV: return QW_SPREADSHEET_FORMAT_CSV;
    case QW_FORMAT_TSV: return QW_SPREADSHEET_FORMAT_TSV;
    case QW_FORMAT_XLS: return QW_SPREADSHEET_FORMAT_XLS;
    case QW_FORMAT_XLT: return QW_SPREADSHEET_FORMAT_XLT;
    case QW_FORMAT_XLSX: return QW_SPREADSHEET_FORMAT_XLSX;
    case QW_FORMAT_XLSM: return QW_SPREADSHEET_FORMAT_XLSM;
    case QW_FORMAT_XLTX: return QW_SPREADSHEET_FORMAT_XLTX;
    case QW_FORMAT_ODS: return QW_SPREADSHEET_FORMAT_ODS;
    case QW_FORMAT_OTS: return QW_SPREADSHEET_FORMAT_OTS;
    case QW_FORMAT_FODS: return QW_SPREADSHEET_FORMAT_FODS;
    default: return QW_SPREADSHEET_FORMAT_INVALID;
    }
}

size_t qw_spreadsheet_sheet_count(const QwSpreadsheetDocument *document)
{
    return document ? document->sheet_count : 0u;
}

const QwSpreadsheetSheet *qw_spreadsheet_sheet_at(
    const QwSpreadsheetDocument *document, size_t index)
{
    if (!document || index >= document->sheet_count) return NULL;
    return &document->sheets[index];
}

const char *qw_spreadsheet_sheet_name(const QwSpreadsheetSheet *sheet,
                                      size_t *out_size)
{
    if (out_size) *out_size = 0u;
    if (!sheet) return NULL;
    if (out_size) *out_size = strlen(sheet->name);
    return sheet->name;
}

size_t qw_spreadsheet_cell_count(const QwSpreadsheetSheet *sheet)
{
    return sheet ? sheet->cell_count : 0u;
}

QwSpreadsheetStatus qw_spreadsheet_cell_at(const QwSpreadsheetSheet *sheet,
                                           size_t index,
                                           QwSpreadsheetCellView *out_cell)
{
    const QwSpreadsheetCell *cell;
    if (!out_cell) return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    *out_cell = (QwSpreadsheetCellView){0};
    if (!sheet) return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    if (index >= sheet->cell_count) return QW_SPREADSHEET_ERR_INVALID_ARGUMENT;
    cell = &sheet->cells[index];
    out_cell->row = cell->row;
    out_cell->column = cell->column;
    out_cell->kind = (QwSpreadsheetCellKind)cell->value_kind;
    out_cell->value = cell->value_utf8;
    out_cell->value_size = cell->value_size;
    out_cell->formula = cell->formula_utf8;
    out_cell->formula_size = cell->formula_size;
    return QW_SPREADSHEET_OK;
}
