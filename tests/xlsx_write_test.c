/* SpreadsheetML writer keeps all worksheets, cell values and merged ranges. */

#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef QUIREWISE_XLSX_WRITE_SRC
#error "QUIREWISE_XLSX_WRITE_SRC must be defined"
#endif

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
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

static int sheet_contains(const QwSpreadsheetSheet *sheet, size_t row,
                          size_t column, const char *value)
{
    size_t index;
    size_t size = strlen(value);
    for (index = 0u; index < qw_spreadsheet_cell_count(sheet); ++index) {
        QwSpreadsheetCellView cell;
        if (qw_spreadsheet_cell_at(sheet, index, &cell) != QW_SPREADSHEET_OK)
            continue;
        if (cell.row == row && cell.column == column &&
            cell.value_size == size && memcmp(cell.value, value, size) == 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    uint8_t *data;
    size_t data_size = 0u;
    QwSpreadsheetDocument *document = NULL;
    QwSpreadsheetDocument *round = NULL;
    QwConvertReport report;
    uint8_t *output = NULL;
    size_t output_size = 0u;

    data = read_file(QUIREWISE_XLSX_WRITE_SRC, &data_size);
    if (!data || !data_size) fail("cannot read the workbook fixture");
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, data, data_size,
                             &document) != QW_SPREADSHEET_OK ||
        !document) {
        free(data);
        fail("input workbook did not parse");
    }
    free(data);
    memset(&report, 0, sizeof(report));
    if (qw_spreadsheet_write_document(document, QW_FORMAT_XLSX, NULL, &output,
                                      &output_size, &report) !=
            QW_SPREADSHEET_OK ||
        !output || !output_size)
        fail("XLSX write failed");
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, output, output_size,
                             &round) != QW_SPREADSHEET_OK ||
        !round)
        fail("written workbook did not parse back");
    if (qw_spreadsheet_sheet_count(round) != qw_spreadsheet_sheet_count(document))
        fail("worksheet count changed");
    {
        const QwSpreadsheetSheet *sheet = qw_spreadsheet_sheet_at(round, 0u);
        const char *name = qw_spreadsheet_sheet_name(sheet, NULL);
        if (!name || strcmp(name, "First") != 0)
            fail("first worksheet name changed");
        if (!sheet_contains(sheet, 0u, 0u, "Header") ||
            !sheet_contains(sheet, 1u, 0u, "alpha"))
            fail("first worksheet values changed");
    }
    {
        const QwSpreadsheetSheet *sheet = qw_spreadsheet_sheet_at(round, 1u);
        const char *name = qw_spreadsheet_sheet_name(sheet, NULL);
        if (!name || strcmp(name, "Second") != 0)
            fail("second worksheet name changed");
        if (!sheet_contains(sheet, 0u, 0u, "Second header") ||
            !sheet_contains(sheet, 1u, 0u, "beta"))
            fail("second worksheet values changed");
    }
    qw_spreadsheet_document_free(round);
    qw_spreadsheet_document_free(document);
    qw_spreadsheet_buffer_free(output);

    /* The merged fixture round-trips its merge rectangle. */
    data = read_file(QUIREWISE_MERGED_XLSX, &data_size);
    if (!data || !data_size) fail("cannot read the merged fixture");
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, data, data_size,
                             &document) != QW_SPREADSHEET_OK ||
        !document) {
        free(data);
        fail("merged workbook did not parse");
    }
    free(data);
    if (qw_spreadsheet_sheet_merge_count(
            qw_spreadsheet_sheet_at(document, 0u)) != 1u)
        fail("merged fixture lost its merge on input");
    output = NULL;
    output_size = 0u;
    if (qw_spreadsheet_write_document(document, QW_FORMAT_XLSX, NULL, &output,
                                      &output_size, &report) !=
            QW_SPREADSHEET_OK ||
        !output || !output_size)
        fail("merged XLSX write failed");
    qw_spreadsheet_document_free(document);
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLSX, output, output_size,
                             &round) != QW_SPREADSHEET_OK ||
        !round)
        fail("merged output did not parse back");
    if (qw_spreadsheet_sheet_merge_count(
            qw_spreadsheet_sheet_at(round, 0u)) != 1u)
        fail("merged range did not survive the XLSX round trip");
    qw_spreadsheet_document_free(round);
    qw_spreadsheet_buffer_free(output);
    puts("Quirewise XLSX write tests passed");
    return 0;
}
