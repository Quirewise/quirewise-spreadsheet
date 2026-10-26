#ifndef QW_ZIP_NATIVE_H
#define QW_ZIP_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#include "../../core/architecture_metadata.h"

#define QW_ZIP_MAX_ARCHIVE_BYTES QW_INPUT_RESOURCE_MAX_ARCHIVE_INPUT_BYTES
#define QW_ZIP_MAX_ARCHIVE_ENTRIES QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRIES
#define QW_ZIP_MAX_SINGLE_EXPANDED_BYTES \
    QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRY_BYTES
#define QW_ZIP_MAX_TOTAL_EXPANDED_BYTES \
    QW_INPUT_RESOURCE_MAX_ARCHIVE_TOTAL_BYTES

typedef struct QwZipEntry {
    char *name;
    size_t name_size;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t crc32;
    uint32_t external_attributes;
    uint16_t flags;
    uint16_t method;
    uint16_t dos_time;
    uint16_t dos_date;
    int is_directory;
} QwZipEntry;

typedef struct QwZipArchive {
    const uint8_t *data;
    size_t size;
    QwZipEntry *entries;
    size_t count;
    uint64_t base_offset;
    size_t max_archive_input_bytes;
    size_t max_archive_entries;
    uint64_t max_single_expanded_bytes;
    uint64_t max_total_expanded_bytes;
    const char *error;
} QwZipArchive;

typedef struct QwZipEntryInfo {
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint16_t flags;
    uint16_t method;
} QwZipEntryInfo;

typedef struct QwZipWriterEntry {
    char *name;
    size_t name_size;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t crc32;
    uint16_t method;
} QwZipWriterEntry;

typedef struct QwZipWriter {
    uint8_t *data;
    size_t size;
    size_t capacity;
    QwZipWriterEntry *entries;
    size_t count;
    size_t entry_capacity;
    int finalized;
    int failed;
} QwZipWriter;

enum {
    QW_ZIP_CASE_SENSITIVE = 1,
    QW_ZIP_NO_COMPRESSION = 0,
    QW_ZIP_DEFAULT_COMPRESSION = -1
};

int qw_zip_reader_init_mem(QwZipArchive *archive, const void *data,
                           size_t size, unsigned flags);
int qw_zip_reader_init_mem_with_profile(
    QwZipArchive *archive, const void *data, size_t size, unsigned flags,
    const QwFormatInputResourceProfile *resource_profile);
void qw_zip_reader_end(QwZipArchive *archive);
size_t qw_zip_reader_get_num_files(const QwZipArchive *archive);
size_t qw_zip_reader_get_filename(const QwZipArchive *archive, size_t index,
                                  char *buffer, size_t buffer_size);
int qw_zip_reader_file_stat(const QwZipArchive *archive, size_t index,
                            QwZipEntryInfo *info);
int qw_zip_reader_is_file_supported(const QwZipArchive *archive,
                                    size_t index);
int qw_zip_reader_is_file_a_directory(const QwZipArchive *archive,
                                      size_t index);
int qw_zip_reader_locate_file(const QwZipArchive *archive, const char *name,
                              const char *file_to_match, unsigned flags);
void *qw_zip_reader_extract_to_heap(QwZipArchive *archive, size_t index,
                                    size_t *out_size, unsigned flags);
void *qw_zip_reader_extract_to_heap_with_password(QwZipArchive *archive,
                                                  size_t index,
                                                  size_t *out_size,
                                                  const char *password);
const char *qw_zip_archive_error(const QwZipArchive *archive);

int qw_zip_writer_init_heap(QwZipWriter *writer,
                            size_t size_to_reserve_at_beginning,
                            size_t initial_allocation_size);
int qw_zip_writer_add_mem(QwZipWriter *writer, const char *archive_name,
                          const void *data, size_t size, int compression_level);
int qw_zip_writer_finalize_heap_archive(QwZipWriter *writer,
                                        void **out_data, size_t *out_size);
void qw_zip_writer_end(QwZipWriter *writer);

#endif
