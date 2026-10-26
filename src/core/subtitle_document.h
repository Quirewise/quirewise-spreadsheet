#ifndef QUIREWISE_SUBTITLE_DOCUMENT_INTERNAL_H
#define QUIREWISE_SUBTITLE_DOCUMENT_INTERNAL_H

#include "quirewise/convert.h"

enum {
    QW_SUBTITLE_MAX_CUES = 1000000u,
    QW_SUBTITLE_MAX_CONTENT_BYTES = 64u * 1024u * 1024u,
    QW_SUBTITLE_MAX_IDENTIFIER_BYTES = 1024u,
    QW_SUBTITLE_MAX_SETTINGS_BYTES = 4096u
};

typedef struct QwSubtitleCue {
    uint64_t start_milliseconds;
    uint64_t end_milliseconds;
    int end_time_explicit;
    uint8_t *identifier_utf8;
    size_t identifier_size;
    uint8_t *settings_utf8;
    size_t settings_size;
    uint8_t *text_utf8;
    size_t text_size;
} QwSubtitleCue;

typedef struct QwSubtitleDocument {
    QwInputFormat input_format;
    /* MicroDVD frame rate as a reduced rational; zero for other formats. */
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
    QwSubtitleCue *cues;
    size_t cue_count;
    size_t cue_capacity;
    size_t content_bytes;
} QwSubtitleDocument;

void qw_subtitle_document_init(QwSubtitleDocument *document,
                               QwInputFormat input_format);
int qw_subtitle_document_add_cue(QwSubtitleDocument *document,
                                 uint64_t start_milliseconds,
                                 uint64_t end_milliseconds,
                                 int end_time_explicit,
                                 const uint8_t *identifier,
                                 size_t identifier_size,
                                 const uint8_t *settings,
                                 size_t settings_size,
                                 const uint8_t *text,
                                 size_t text_size);
int qw_subtitle_document_validate(const QwSubtitleDocument *document);
void qw_subtitle_document_dispose(QwSubtitleDocument *document);

#endif
