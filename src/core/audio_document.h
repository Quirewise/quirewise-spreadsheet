#ifndef QUIREWISE_AUDIO_DOCUMENT_INTERNAL_H
#define QUIREWISE_AUDIO_DOCUMENT_INTERNAL_H

#include "quirewise/convert.h"

typedef struct QwAudioTag {
    const char *name;
    const char *value;
} QwAudioTag;

typedef struct QwAudioChapter {
    int64_t start_time_us;
    int64_t end_time_us;
    const char *title;
} QwAudioChapter;

typedef struct QwAudioDocument {
    QwInputFormat input_format;
    const uint8_t *stream_data;
    size_t stream_size;
    const char *source_path;
    size_t selected_track;
    unsigned sample_rate;
    unsigned channels;
    unsigned bitrate;
    const char *codec_name;
    int64_t duration_us;
    const QwAudioTag *tags;
    size_t tag_count;
    const uint8_t *cover_data;
    size_t cover_size;
    const QwAudioChapter *chapters;
    size_t chapter_count;
    const char *qmc_ekey;
    const char *qmc_ekey_db_path;
    void *context;
    void (*dispose)(void *context);
    /* Clear stream format when an encrypted opener decrypts before inspection. */
    QwInputFormat decoded_format;
} QwAudioDocument;

struct QwSource;
int qw_audio_document_from_source(const struct QwSource *source,
                                  QwInputFormat input_format,
                                  const QwConvertOptions *options,
                                  QwAudioDocument *out_document);
int qw_audio_document_validate(const QwAudioDocument *document);
void qw_audio_document_dispose(QwAudioDocument *document);

#endif
