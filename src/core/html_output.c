#include "html_output.h"

#include <string.h>

int qw_html_output_append(QwTextBuffer *html, const char *text, size_t size,
                          size_t max_bytes, int *out_limit_hit)
{
    if (!html || (!text && size) || max_bytes == (size_t)-1) return 0;
    if (size > max_bytes || html->size > max_bytes - size) {
        if (out_limit_hit) *out_limit_hit = 1;
        return 0;
    }
    html->limit = max_bytes + 1u;
    return qw_text_buffer_append(html, text, size);
}

int qw_html_output_literal(QwTextBuffer *html, const char *text,
                           size_t max_bytes, int *out_limit_hit)
{
    return text && qw_html_output_append(html, text, strlen(text), max_bytes,
                                         out_limit_hit);
}

int qw_html_output_escape(QwTextBuffer *html, const char *text,
                          size_t max_bytes, int *out_limit_hit)
{
    const unsigned char *cursor = (const unsigned char *)text;
    const unsigned char *span = cursor;
    if (!text) return 1;
    while (*cursor) {
        const char *replacement = NULL;
        switch (*cursor) {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        default: break;
        }
        if (replacement) {
            if (!qw_html_output_append(html, (const char *)span,
                    (size_t)(cursor - span), max_bytes, out_limit_hit) ||
                !qw_html_output_literal(html, replacement, max_bytes,
                                        out_limit_hit)) return 0;
            span = cursor + 1u;
        }
        ++cursor;
    }
    return qw_html_output_append(html, (const char *)span,
                                 (size_t)(cursor - span), max_bytes,
                                 out_limit_hit);
}
