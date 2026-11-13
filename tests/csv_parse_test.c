/*
 * CSV parsing through the public spreadsheet API: one sheet, string cells and
 * quoted fields with an embedded comma.
 */

#include <quirewise/spreadsheet.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", #condition, __LINE__); \
        return 1; \
    } \
} while (0)

static int find_cell(const QwSpreadsheetSheet *sheet, unsigned long row,
                     unsigned long column, const char *expected)
{
    size_t count = qw_spreadsheet_cell_count(sheet);
    size_t index;
    for (index = 0u; index < count; ++index) {
        QwSpreadsheetCellView cell;
        memset(&cell, 0, sizeof(cell));
        if (qw_spreadsheet_cell_at(sheet, index, &cell) != QW_SPREADSHEET_OK)
            continue;
        if (cell.row != row || cell.column != column) continue;
        if (cell.value_size != strlen(expected)) continue;
        if (cell.value && memcmp(cell.value, expected, cell.value_size) == 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    static const char csv[] =
        "name,qty\n"
        "apple,3\n"
        "\"pear, green\",5\n";
    QwSpreadsheetDocument *document = NULL;
    const QwSpreadsheetSheet *sheet;

    CHECK(qw_spreadsheet_parse(QW_SPREADSHEET_FORMAT_CSV, csv,
                               sizeof(csv) - 1u, &document) ==
          QW_SPREADSHEET_OK);
    CHECK(document != NULL);
    CHECK(qw_spreadsheet_document_format(document) ==
          QW_SPREADSHEET_FORMAT_CSV);
    CHECK(qw_spreadsheet_sheet_count(document) == 1u);
    sheet = qw_spreadsheet_sheet_at(document, 0u);
    CHECK(sheet != NULL);
    CHECK(qw_spreadsheet_cell_count(sheet) == 6u);
    CHECK(find_cell(sheet, 0u, 0u, "name"));
    CHECK(find_cell(sheet, 0u, 1u, "qty"));
    CHECK(find_cell(sheet, 1u, 0u, "apple"));
    CHECK(find_cell(sheet, 1u, 1u, "3"));
    CHECK(find_cell(sheet, 2u, 0u, "pear, green"));
    CHECK(find_cell(sheet, 2u, 1u, "5"));
    qw_spreadsheet_document_free(document);
    return 0;
}
