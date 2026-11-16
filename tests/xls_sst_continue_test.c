/* BIFF8 SST strings that span CONTINUE records decode completely. */

#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef QUIREWISE_LARGE_SST_XLS
#error "QUIREWISE_LARGE_SST_XLS must be defined"
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

int main(void)
{
    QwSpreadsheetDocument *document = NULL;
    QwSpreadsheetSheet *sheet;
    uint8_t *data;
    size_t data_size = 0u;
    size_t index;
    int found_first = 0;
    int found_last = 0;
    int found_last_cjk = 0;

    data = read_file(QUIREWISE_LARGE_SST_XLS, &data_size);
    if (!data || !data_size) fail("cannot read the large SST fixture");
    if (qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLS, data, data_size,
                             &document) != QW_SPREADSHEET_OK ||
        !document) {
        free(data);
        fail("BIFF8 workbook with CONTINUE records did not parse");
    }
    free(data);
    if (qw_spreadsheet_sheet_count(document) != 1u)
        fail("expected one worksheet");
    sheet = (QwSpreadsheetSheet *)qw_spreadsheet_sheet_at(document, 0u);
    if (!sheet) fail("worksheet is missing");
    for (index = 0u; index < qw_spreadsheet_cell_count(sheet); ++index) {
        QwSpreadsheetCellView cell;
        if (qw_spreadsheet_cell_at(sheet, index, &cell) != QW_SPREADSHEET_OK)
            continue;
        if (cell.row == 0u && cell.column == 0u && cell.value_size >= 10u &&
            memcmp(cell.value, "String0000", 10u) == 0)
            found_first = 1;
        if (cell.row == 1499u && cell.column == 0u && cell.value_size >= 10u &&
            memcmp(cell.value, "String1499", 10u) == 0)
            found_last = 1;
        if (cell.row == 1499u && cell.column == 1u && cell.value_size >= 7u &&
            memcmp(cell.value, "值1499", 7u) == 0)
            found_last_cjk = 1;
    }
    if (!found_first || !found_last || !found_last_cjk) {
        fprintf(stderr,
                "FAIL: shared strings across CONTINUE (first %d, last %d, "
                "last CJK %d)\n",
                found_first, found_last, found_last_cjk);
        qw_spreadsheet_document_free(document);
        return 1;
    }
    qw_spreadsheet_document_free(document);
    puts("Quirewise BIFF8 SST continuation tests passed");
    return 0;
}
