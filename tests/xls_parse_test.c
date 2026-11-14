#include "quirewise/spreadsheet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Fixtures are CC0 workbooks from app/testfile/spreadsheet/xls (see
 * FORMAT_SAMPLE_MATRIX.csv). valid.xls exercises out-of-order worksheet rows
 * and MULRK records; MonteCarlo.xls is a CFB v3 file whose stream-size high
 * half is uninitialised (the reader must mask it).
 */
#ifndef QUIREWISE_XLS_VALID_FIXTURE
#define QUIREWISE_XLS_VALID_FIXTURE "tests/data/valid.xls"
#endif
#ifndef QUIREWISE_XLS_MONTECARLO_FIXTURE
#define QUIREWISE_XLS_MONTECARLO_FIXTURE "tests/data/MonteCarlo.xls"
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *file;
    long length;
    unsigned char *data;
    if (out_size) *out_size = 0u;
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open fixture: %s\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (!data) { fclose(file); return NULL; }
    if (fread(data, 1u, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    if (out_size) *out_size = (size_t)length;
    return data;
}

static size_t total_cells(const QwSpreadsheetDocument *document)
{
    size_t total = 0u;
    size_t index;
    for (index = 0u; index < qw_spreadsheet_sheet_count(document); ++index) {
        const QwSpreadsheetSheet *sheet =
            qw_spreadsheet_sheet_at(document, index);
        total += qw_spreadsheet_cell_count(sheet);
    }
    return total;
}

static int check_valid(void)
{
    size_t size = 0u;
    unsigned char *data = read_file(QUIREWISE_XLS_VALID_FIXTURE, &size);
    QwSpreadsheetDocument *document = NULL;
    const QwSpreadsheetSheet *sheet;
    QwSpreadsheetCellView cell;
    size_t name_size = 0u;
    const char *name;
    CHECK(data != NULL);
    CHECK(qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLS, data, size,
                               &document) == QW_SPREADSHEET_OK);
    CHECK(document != NULL);
    CHECK(qw_spreadsheet_sheet_count(document) == 10u);
    CHECK(total_cells(document) == 3301u);
    sheet = qw_spreadsheet_sheet_at(document, 2u);
    CHECK(sheet != NULL);
    name = qw_spreadsheet_sheet_name(sheet, &name_size);
    CHECK(name != NULL && name_size == 1u && name[0] == 'b');
    memset(&cell, 0, sizeof(cell));
    CHECK(qw_spreadsheet_cell_at(sheet, 0u, &cell) == QW_SPREADSHEET_OK);
    CHECK(cell.value_size == 6u && memcmp(cell.value, "Enash:", 6u) == 0);
    qw_spreadsheet_document_free(document);
    free(data);
    return 0;
}

static int check_montecarlo(void)
{
    size_t size = 0u;
    unsigned char *data = read_file(QUIREWISE_XLS_MONTECARLO_FIXTURE, &size);
    QwSpreadsheetDocument *document = NULL;
    const QwSpreadsheetSheet *sheet;
    QwSpreadsheetCellView cell;
    size_t name_size = 0u;
    const char *name;
    CHECK(data != NULL);
    CHECK(qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLS, data, size,
                               &document) == QW_SPREADSHEET_OK);
    CHECK(document != NULL);
    CHECK(qw_spreadsheet_sheet_count(document) == 7u);
    CHECK(total_cells(document) == 3950u);
    sheet = qw_spreadsheet_sheet_at(document, 0u);
    CHECK(sheet != NULL);
    name = qw_spreadsheet_sheet_name(sheet, &name_size);
    CHECK(name != NULL && name_size == 10u &&
          memcmp(name, "RangeTable", 10u) == 0);
    memset(&cell, 0, sizeof(cell));
    CHECK(qw_spreadsheet_cell_at(sheet, 0u, &cell) == QW_SPREADSHEET_OK);
    CHECK(cell.value_size == strlen("Monte Carlo 1: PF simulation") &&
          memcmp(cell.value, "Monte Carlo 1: PF simulation",
                 cell.value_size) == 0);

    /* A truncated file must be rejected, not accepted as a partial sheet. */
    {
        QwSpreadsheetDocument *broken = NULL;
        CHECK(qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_XLS, data, 512u,
                                   &broken) != QW_SPREADSHEET_OK);
        CHECK(broken == NULL);
    }
    qw_spreadsheet_document_free(document);
    free(data);
    return 0;
}

int main(void)
{
    if (check_valid() != 0) return 1;
    if (check_montecarlo() != 0) return 1;
    printf("XLS parse: valid.xls 10 sheets/3301 cells, "
           "MonteCarlo.xls 7 sheets/3950 cells\n");
    return 0;
}
