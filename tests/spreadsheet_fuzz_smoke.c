#include <quirewise/spreadsheet.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED UINT32_C(0x51f05511)
#define FUZZ_RUNS 250u

static uint32_t rng_state = FUZZ_SEED;

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    rng_state = value;
    return value;
}

static size_t mutate(const char *seed, char *buffer, size_t capacity)
{
    size_t length = strlen(seed);
    size_t index;
    unsigned int flips = 1u + next_random() % 4u;
    if (length + 1u > capacity) length = capacity - 1u;
    memcpy(buffer, seed, length);
    buffer[length] = '\0';
    if (next_random() % 8u == 0u && length > 1u)
        length = 1u + next_random() % length;
    for (index = 0u; index < flips && length; ++index) {
        size_t position = next_random() % length;
        buffer[position] = (char)(buffer[position] ^
                                  (uint8_t)(1u << (next_random() % 8u)));
    }
    return length;
}

int main(void)
{
    static const struct {
        QwSpreadsheetFormat format;
        const char *text;
    } seeds[] = {
        {QW_SPREADSHEET_FORMAT_CSV, "name,value\r\nalpha,1\r\nbeta,2\r\n"},
        {QW_SPREADSHEET_FORMAT_TSV, "name\tvalue\nalpha\t1\nbeta\t2\n"},
        {QW_SPREADSHEET_FORMAT_CSV, "\"quoted,cell\",1\r\n\"multi\nline\",2\r\n"},
    };
    char buffer[1024];
    size_t index;
    size_t run;
    size_t ok = 0u;
    size_t rejected = 0u;
    for (index = 0u; index < sizeof(seeds) / sizeof(seeds[0]); ++index) {
        for (run = 0u; run < FUZZ_RUNS; ++run) {
            QwSpreadsheetDocument *document = NULL;
            size_t size = mutate(seeds[index].text, buffer, sizeof(buffer));
            QwSpreadsheetStatus status = qw_spreadsheet_parse(
                seeds[index].format, buffer, size, &document);
            if (status == QW_SPREADSHEET_OK && document) {
                ++ok;
                qw_spreadsheet_document_free(document);
            } else {
                ++rejected;
            }
        }
    }
    printf("Spreadsheet fuzz smoke: %zu ok, %zu rejected, seed=0x%08x runs=%u\n",
           ok, rejected, FUZZ_SEED, FUZZ_RUNS);
    return 0;
}
