#ifndef QUIREWISE_HTML_OUTPUT_INTERNAL_H
#define QUIREWISE_HTML_OUTPUT_INTERNAL_H

#include "text_buffer.h"

#include <stddef.h>

int qw_html_output_append(QwTextBuffer *html, const char *text, size_t size,
                          size_t max_bytes, int *out_limit_hit);
int qw_html_output_literal(QwTextBuffer *html, const char *text,
                           size_t max_bytes, int *out_limit_hit);
int qw_html_output_escape(QwTextBuffer *html, const char *text,
                          size_t max_bytes, int *out_limit_hit);

#endif
