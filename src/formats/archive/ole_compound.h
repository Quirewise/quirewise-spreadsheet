#ifndef QUIREWISE_OLE_COMPOUND_H
#define QUIREWISE_OLE_COMPOUND_H

#include <stddef.h>
#include <stdint.h>

/* Reads a root-level CFB stream. A shorter max_prefix returns the stream prefix. */
int qw_ole_compound_read_stream(const uint8_t *data,
                                size_t size,
                                const char *stream_name,
                                size_t max_prefix,
                                uint8_t **out_data,
                                size_t *out_size,
                                uint64_t *out_stream_size);

int qw_ole_compound_has_signature(const uint8_t *data, size_t size);

#endif
