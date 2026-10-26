#ifndef QUIREWISE_LAYOUT_DOCUMENT_INTERNAL_H
#define QUIREWISE_LAYOUT_DOCUMENT_INTERNAL_H

#include "quirewise/convert.h"

typedef struct QwLayoutRun {
    const uint8_t *utf8_text;
    size_t text_size;
    unsigned style_flags;
    const char *link_target;
} QwLayoutRun;

enum QwLayoutStyleFlags {
    QW_LAYOUT_STYLE_BOLD = 1u << 0,
    QW_LAYOUT_STYLE_ITALIC = 1u << 1,
    QW_LAYOUT_STYLE_CODE = 1u << 2,
    QW_LAYOUT_STYLE_HEADING = 1u << 3,
    QW_LAYOUT_STYLE_QUOTE = 1u << 4
};

typedef struct QwLayoutParagraph {
    const QwLayoutRun *runs;
    size_t run_count;
    unsigned heading_level;
    unsigned page_break_before;
} QwLayoutParagraph;

typedef struct QwLayoutSection {
    const char *title;
    const QwLayoutParagraph *paragraphs;
    size_t paragraph_count;
} QwLayoutSection;

typedef struct QwAttachment {
    const char *name;
    const char *media_type;
    const uint8_t *data;
    size_t size;
} QwAttachment;

typedef struct QwAttachmentSet {
    const QwAttachment *items;
    size_t count;
    void *context;
    void (*dispose)(void *context);
} QwAttachmentSet;

typedef struct QwLayoutDocument {
    const QwLayoutSection *sections;
    size_t section_count;
    const uint8_t *html_utf8;
    size_t html_size;
    const uint8_t *pdf_data;
    size_t pdf_size;
    const char *resource_base_path;
    const QwAttachmentSet *attachments;
    void *context;
    void (*dispose)(void *context);
    float page_width_points;
    float page_height_points;
} QwLayoutDocument;

struct QwSource;
int qw_layout_document_validate(const QwLayoutDocument *document);
void qw_layout_document_dispose(QwLayoutDocument *document);

#endif
