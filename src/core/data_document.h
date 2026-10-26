#ifndef QUIREWISE_DATA_DOCUMENT_INTERNAL_H
#define QUIREWISE_DATA_DOCUMENT_INTERNAL_H

#include "quirewise/convert.h"

#include <stddef.h>

#define QW_DATA_DOCUMENT_MAX_DEPTH 256u
#define QW_DATA_DOCUMENT_MAX_NODES 1000000u
#define QW_DATA_DOCUMENT_MAX_MEMORY ((size_t)256u * 1024u * 1024u)

typedef enum QwDataValueKind {
    QW_DATA_VALUE_NULL = 0,
    QW_DATA_VALUE_BOOLEAN,
    QW_DATA_VALUE_NUMBER,
    QW_DATA_VALUE_STRING,
    QW_DATA_VALUE_ARRAY,
    QW_DATA_VALUE_OBJECT,
    /* TOML date/time scalars retain their lexical type in the shared tree. */
    QW_DATA_VALUE_TEMPORAL
} QwDataValueKind;

typedef struct QwDataValue QwDataValue;
typedef struct QwDataMember QwDataMember;

struct QwDataValue {
    QwDataValueKind kind;
    union {
        int boolean;
        struct {
            char *data;
            size_t size;
        } text;
        struct {
            QwDataValue *items;
            size_t count;
            size_t capacity;
        } array;
        struct {
            QwDataMember *items;
            size_t count;
            size_t capacity;
        } object;
    } value;
};

struct QwDataMember {
    char *key;
    size_t key_size;
    QwDataValue value;
};

typedef struct QwDataDocument {
    QwInputFormat input_format;
    QwDataValue root;
    size_t node_count;
    size_t allocation_bytes;
} QwDataDocument;

void qw_data_document_init(QwDataDocument *document,
                           QwInputFormat input_format);
void qw_data_document_dispose(QwDataDocument *document);
int qw_data_document_validate(const QwDataDocument *document);
void qw_data_value_dispose(QwDataValue *value);

#endif
