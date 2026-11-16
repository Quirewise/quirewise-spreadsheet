/*
 * CSV/TSV writer tests: parse comma-separated text (including quoting and a
 * comma inside a field), write it back as CSV and TSV, and re-read the TSV
 * output to confirm the values survive.
 */

#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static int contains(const uint8_t *data, size_t size, const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t index;

    if (needle_size > size) return 0;
    for (index = 0u; index + needle_size <= size; ++index) {
        if (memcmp(data + index, needle, needle_size) == 0) return 1;
    }
    return 0;
}

int main(void)
{
    static const char csv_text[] =
        "a,b,c\n"
        "1,\"two, with comma\",3\n"
        "4,\"quote \"\"inside\"\"\",\"line\nbreak\"\n";
    QwSpreadsheetDocument *document = NULL;
    QwConvertReport report;
    uint8_t *output = NULL;
    size_t output_size = 0u;

    memset(&report, 0, sizeof(report));
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_CSV, csv_text,
                             strlen(csv_text), &document) !=
            QW_SPREADSHEET_OK ||
        !document)
        fail("CSV parse failed");

    if (qw_spreadsheet_write_document(document, QW_FORMAT_CSV, NULL, &output,
                                      &output_size, &report) !=
            QW_SPREADSHEET_OK ||
        !output || !output_size)
        fail("CSV write failed");
    if (!contains(output, output_size, "a,b,c") ||
        !contains(output, output_size, "\"two, with comma\"") ||
        !contains(output, output_size, "\"quote \"\"inside\"\"\""))
        fail("CSV writer lost quoting");
    qw_spreadsheet_buffer_free(output);

    output = NULL;
    output_size = 0u;
    if (qw_spreadsheet_write_document(document, QW_FORMAT_TSV, NULL, &output,
                                      &output_size, &report) !=
            QW_SPREADSHEET_OK ||
        !output || !output_size)
        fail("TSV write failed");
    if (!contains(output, output_size, "a\tb\tc") ||
        !contains(output, output_size, "two, with comma"))
        fail("TSV writer lost values");
    {
        QwSpreadsheetDocument *round = NULL;
        QwSpreadsheetSheet *sheet;
        QwSpreadsheetCellView cell;
        int found = 0;
        size_t index;

        if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_TSV, output,
                                 output_size, &round) != QW_SPREADSHEET_OK ||
            !round)
            fail("TSV writer output did not parse");
        if (qw_spreadsheet_sheet_count(round) != 1u)
            fail("TSV round trip lost the sheet");
        sheet = (QwSpreadsheetSheet *)qw_spreadsheet_sheet_at(round, 0u);
        if (!sheet) fail("TSV round trip lost the sheet");
        for (index = 0u; index < qw_spreadsheet_cell_count(sheet); ++index) {
            if (qw_spreadsheet_cell_at(sheet, index, &cell) !=
                QW_SPREADSHEET_OK)
                continue;
            if (cell.row == 1u && cell.column == 1u && cell.value_size == 15u &&
                memcmp(cell.value, "two, with comma", 15u) == 0)
                found = 1;
        }
        if (!found) fail("TSV round trip lost a quoted field");
        qw_spreadsheet_document_free(round);
    }
    qw_spreadsheet_buffer_free(output);

    {
        QwConvertReport unsupported_report;
        uint8_t *unsupported = (uint8_t *)1;
        size_t unsupported_size = 7u;

        memset(&unsupported_report, 0, sizeof(unsupported_report));
        if (qw_spreadsheet_write_document(document, QW_FORMAT_PDF, NULL,
                                          &unsupported, &unsupported_size,
                                          &unsupported_report) !=
            QW_SPREADSHEET_ERR_UNSUPPORTED)
            fail("PDF target was accepted");
        if (unsupported || unsupported_size)
            fail("unsupported target produced output");
    }

    if (qw_spreadsheet_write_document(NULL, QW_FORMAT_CSV, NULL, NULL, NULL,
                                      NULL) !=
        QW_SPREADSHEET_ERR_INVALID_ARGUMENT)
        fail("NULL arguments were accepted");
    qw_spreadsheet_buffer_free(NULL);
    qw_spreadsheet_document_free(document);
    puts("Quirewise spreadsheet writer tests passed");
    return 0;
}
