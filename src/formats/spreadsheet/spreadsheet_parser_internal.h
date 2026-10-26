#ifndef QUIREWISE_SPREADSHEET_PARSER_INTERNAL_H
#define QUIREWISE_SPREADSHEET_PARSER_INTERNAL_H

#include "../../core/spreadsheet_document.h"

int qw_csv_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_document);
int qw_xls_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_document);
int qw_xlsx_parse_spreadsheet(const void *data, size_t size,
                              QwInputFormat input_format,
                              QwSpreadsheetDocument *out_document);
int qw_ods_parse_spreadsheet(const void *data, size_t size,
                             QwInputFormat input_format,
                             QwSpreadsheetDocument *out_document);

#endif
