#ifndef QUIREWISE_MUPDF_BACKEND_H
#define QUIREWISE_MUPDF_BACKEND_H

#include "quirewise/convert.h"
#include "quirewise/document.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

typedef struct QwMuPdfReader QwMuPdfReader;

typedef struct QwMuPdfOutlineItem {
    const char *title;
    size_t page_index;
    size_t depth;
} QwMuPdfOutlineItem;

int qw_mupdf_convert_memory(const uint8_t *data,
                            size_t size,
                            const char *input_extension,
                            int reflow_epub,
                            uint8_t **out_pdf,
                            size_t *out_size,
                            QwConvertReport *report);
int qw_mupdf_convert_memory_with_outline(const uint8_t *data,
                                         size_t size,
                                         const char *input_extension,
                                         const QwMuPdfOutlineItem *outline,
                                         size_t outline_count,
                                         uint8_t **out_pdf,
                                         size_t *out_size,
                                         QwConvertReport *report);

int qw_mupdf_convert_html(const char *html,
                          size_t html_size,
                          uint8_t **out_pdf,
                          size_t *out_size,
                          QwConvertReport *report);
int qw_mupdf_convert_html_to_stream(const char *html,
                                   size_t html_size,
                                   FILE *stream,
                                   size_t max_output_bytes,
                                   size_t *out_size,
                                   QwConvertReport *report);
int qw_mupdf_convert_html_with_page_size(const char *html,
                                        size_t html_size,
                                        float page_width_points,
                                        float page_height_points,
                                        uint8_t **out_pdf,
                                        size_t *out_size,
                                        QwConvertReport *report);
int qw_mupdf_convert_html_with_page_size_to_stream(
    const char *html,
    size_t html_size,
    float page_width_points,
    float page_height_points,
    FILE *stream,
    size_t max_output_bytes,
    size_t *out_size,
    QwConvertReport *report);
int qw_mupdf_convert_html_file(const char *path,
                               uint8_t **out_pdf,
                               size_t *out_size,
                               QwConvertReport *report);
int qw_mupdf_convert_html_file_to_stream(const char *path,
                                         FILE *stream,
                                         size_t max_output_bytes,
                                         size_t *out_size,
                                         QwConvertReport *report);
int qw_mupdf_decode_caj_jp2(const uint8_t *data,
                            size_t size,
                            QwPageImage *out_page,
                            QwConvertReport *report);

int qw_mupdf_render_pdf_page(const uint8_t *data,
                             size_t size,
                             size_t page_index,
                             unsigned int dpi,
                             QwPageImage *out_page,
                             QwConvertReport *report);

int qw_mupdf_pdf_reader_open(const uint8_t *data,
                             size_t size,
                             QwMuPdfReader **out_reader,
                             size_t *out_page_count,
                             QwConvertReport *report);
int qw_mupdf_pdf_reader_render_page(QwMuPdfReader *reader,
                                    size_t page_index,
                                    unsigned int dpi,
                                    QwPageImage *out_page,
                                    QwConvertReport *report);
int qw_mupdf_pdf_reader_extract_text(QwMuPdfReader *reader,
                                     uint8_t **out_text,
                                     size_t *out_size,
                                     QwConvertReport *report);
int qw_mupdf_pdf_reader_extract_text_to_stream(
    QwMuPdfReader *reader,
    FILE *stream,
    size_t max_output_bytes,
    size_t *out_size,
    QwConvertReport *report);
void qw_mupdf_pdf_reader_close(QwMuPdfReader *reader);

int qw_mupdf_merge_pdf_documents(const uint8_t *const *pdfs,
                                 const size_t *sizes,
                                 size_t count,
                                 uint8_t **out_pdf,
                                 size_t *out_size,
                                 size_t *out_pages,
                                 QwConvertReport *report);

#endif
