/* CSV writer honors the sheet_index conversion option. */

#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef QUIREWISE_MULTI_SHEET_XLSX
#error "QUIREWISE_MULTI_SHEET_XLSX must be defined"
#endif

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

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *data;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    data = (uint8_t *)malloc((size_t)size);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1u, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return data;
}

int main(void)
{
    QwSpreadsheetDocument *document = NULL;
    QwConvertReport report;
    QwConvertOptions options;
    uint8_t *data;
    size_t data_size = 0u;

    data = read_file(QUIREWISE_MULTI_SHEET_XLSX, &data_size);
    if (!data || !data_size) fail("cannot read the multi-sheet fixture");
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, data, data_size,
                             &document) != QW_SPREADSHEET_OK ||
        !document) {
        free(data);
        fail("XLSX parse failed");
    }
    free(data);
    if (qw_spreadsheet_sheet_count(document) != 2u)
        fail("expected two worksheets");

    memset(&report, 0, sizeof(report));
    options = (QwConvertOptions)QW_CONVERT_OPTIONS_INIT;
    {
        uint8_t *output = NULL;
        size_t output_size = 0u;
        if (qw_spreadsheet_write_document(document, QW_FORMAT_CSV, &options,
                                          &output, &output_size, &report) !=
                QW_SPREADSHEET_OK ||
            !output || !output_size)
            fail("default CSV write failed");
        if (!contains(output, output_size, "alpha") ||
            contains(output, output_size, "Second header"))
            fail("default CSV write did not use the first worksheet");
        qw_spreadsheet_buffer_free(output);
    }
    options = (QwConvertOptions)QW_CONVERT_OPTIONS_INIT;
    options.sheet_index = 1u;
    memset(&report, 0, sizeof(report));
    {
        uint8_t *output = NULL;
        size_t output_size = 0u;
        if (qw_spreadsheet_write_document(document, QW_FORMAT_CSV, &options,
                                          &output, &output_size, &report) !=
                QW_SPREADSHEET_OK ||
            !output || !output_size)
            fail("second-sheet CSV write failed");
        if (!contains(output, output_size, "Second header") ||
            contains(output, output_size, "alpha"))
            fail("sheet_index=1 did not select the second worksheet");
        if (!strstr(report.note, "2 of 2"))
            fail("report note does not identify the selected worksheet");
        qw_spreadsheet_buffer_free(output);
    }
    options.sheet_index = 2u;
    {
        uint8_t *output = (uint8_t *)1;
        size_t output_size = 7u;
        if (qw_spreadsheet_write_document(document, QW_FORMAT_CSV, &options,
                                          &output, &output_size, &report) !=
            QW_SPREADSHEET_ERR_INVALID_ARGUMENT)
            fail("out-of-range sheet index was accepted");
        if (output || output_size)
            fail("rejected sheet index produced output");
    }
    qw_spreadsheet_document_free(document);
    puts("Quirewise sheet selection tests passed");
    return 0;
}
