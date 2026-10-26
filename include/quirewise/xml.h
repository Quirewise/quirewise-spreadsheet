#ifndef QUIREWISE_XML_H
#define QUIREWISE_XML_H

#include <stddef.h>
#include <stdint.h>

#define QW_XML_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QwXmlNode QwXmlNode;

typedef enum QwXmlNodeKind {
    QW_XML_NODE_INVALID = 0,
    QW_XML_NODE_DOCUMENT,
    QW_XML_NODE_ELEMENT,
    QW_XML_NODE_TEXT,
    QW_XML_NODE_COMMENT,
    QW_XML_NODE_PROCESSING_INSTRUCTION
} QwXmlNodeKind;

typedef enum QwXmlErrorCode {
    QW_XML_ERROR_NONE = 0,
    QW_XML_ERROR_ARGUMENT,
    QW_XML_ERROR_FORMAT,
    QW_XML_ERROR_RESOURCE_LIMIT,
    QW_XML_ERROR_OUT_OF_MEMORY,
    QW_XML_ERROR_ENCODING,
    QW_XML_ERROR_UNSUPPORTED,
    QW_XML_ERROR_NAMESPACE,
    QW_XML_ERROR_VALIDITY
} QwXmlErrorCode;

typedef struct QwXmlError {
    QwXmlErrorCode code;
    size_t byte_offset;
    char message[160];
} QwXmlError;

typedef int (*QwXmlResolveEntityFn)(void *context, const char *public_id,
                                    const char *system_id,
                                    const char *base_uri,
                                    const uint8_t **out_data,
                                    size_t *out_size,
                                    const char **out_resolved_base_uri);
typedef void (*QwXmlReleaseEntityFn)(void *context, const uint8_t *data,
                                     size_t size);

typedef struct QwXmlParseOptions {
    int validate_dtd; /* Validate XML 1.0 DTD constraints in addition to syntax. */
    const char *base_uri; /* Base URI used to resolve relative external IDs. */
    void *resolver_context;
    /* External resources are fetched only through this caller-supplied resolver.
     * On success it returns bytes, their size, and optionally the resolved base
     * URI. Bytes stay valid until release_entity is called, or until parsing
     * returns when no release callback is provided. */
    QwXmlResolveEntityFn resolve_entity;
    QwXmlReleaseEntityFn release_entity;
} QwXmlParseOptions;

int qw_xml_abi_version(void);

/* Parse namespace-aware XML 1.0. External resources are accessed only through
 * the caller-supplied resolver. */
QwXmlNode *qw_xml_parse(const void *data, size_t size, QwXmlError *error);
QwXmlNode *qw_xml_parse_with_options(const void *data, size_t size,
                                     const QwXmlParseOptions *options,
                                     QwXmlError *error);
void qw_xml_drop(QwXmlNode *root);

/* The returned UTF-8 buffer is malloc-owned and must be released with free(). */
int qw_xml_serialize(const QwXmlNode *node, uint8_t **out_data,
                     size_t *out_size, QwXmlError *error);

QwXmlNodeKind qw_xml_node_kind(const QwXmlNode *node);
QwXmlNode *qw_xml_root(QwXmlNode *node);
QwXmlNode *qw_xml_down(QwXmlNode *node);
QwXmlNode *qw_xml_next(QwXmlNode *node);
QwXmlNode *qw_xml_prev(QwXmlNode *node);
QwXmlNode *qw_xml_up(QwXmlNode *node);
int qw_xml_is_tag(const QwXmlNode *node, const char *name);
const char *qw_xml_tag(const QwXmlNode *node);
const char *qw_xml_qualified_tag(const QwXmlNode *node);
const char *qw_xml_local_name(const QwXmlNode *node);
const char *qw_xml_namespace_uri(const QwXmlNode *node);
const char *qw_xml_text(const QwXmlNode *node);
const char *qw_xml_comment(const QwXmlNode *node);
const char *qw_xml_pi_target(const QwXmlNode *node);
const char *qw_xml_pi_data(const QwXmlNode *node);
const char *qw_xml_att(const QwXmlNode *node, const char *qualified_name);
size_t qw_xml_attribute_count(const QwXmlNode *node);
const char *qw_xml_attribute_name(const QwXmlNode *node, size_t index);
const char *qw_xml_attribute_local_name(const QwXmlNode *node, size_t index);
const char *qw_xml_attribute_namespace_uri(const QwXmlNode *node,
                                           size_t index);
const char *qw_xml_attribute_value(const QwXmlNode *node, size_t index);
const char *qw_xml_att_ns(const QwXmlNode *node, const char *namespace_uri,
                          const char *local_name);
const char *qw_xml_att_alt(const QwXmlNode *node, const char *first_name,
                           const char *second_name);
int qw_xml_att_eq(const QwXmlNode *node, const char *name,
                  const char *value);

QwXmlNode *qw_xml_find(QwXmlNode *item, const char *tag);
QwXmlNode *qw_xml_find_next(QwXmlNode *item, const char *tag);
QwXmlNode *qw_xml_find_down(QwXmlNode *item, const char *tag);
QwXmlNode *qw_xml_find_match(QwXmlNode *item, const char *tag,
                             const char *attribute, const char *value);
QwXmlNode *qw_xml_find_next_match(QwXmlNode *item, const char *tag,
                                  const char *attribute, const char *value);
QwXmlNode *qw_xml_find_down_match(QwXmlNode *item, const char *tag,
                                  const char *attribute, const char *value);
QwXmlNode *qw_xml_find_dfs(QwXmlNode *item, const char *tag,
                           const char *attribute, const char *value);
QwXmlNode *qw_xml_find_dfs_top(QwXmlNode *item, const char *tag,
                               const char *attribute, const char *value,
                               QwXmlNode *top);
QwXmlNode *qw_xml_find_next_dfs(QwXmlNode *item, const char *tag,
                                const char *attribute, const char *value);
QwXmlNode *qw_xml_find_next_dfs_top(QwXmlNode *item, const char *tag,
                                    const char *attribute, const char *value,
                                    QwXmlNode *top);

#ifdef __cplusplus
}
#endif

#endif
