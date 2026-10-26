#ifndef QUIREWISE_DOCUMENT_H
#define QUIREWISE_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public page document contract shared by decoders and writers.
 * Audio and layout models use separate typed artifacts.
 */
typedef enum QwPageImageKind {
    QW_PAGE_IMAGE_JPEG = 0,
    QW_PAGE_IMAGE_CCITT = 1,
    QW_PAGE_IMAGE_GRAY8 = 2,
    QW_PAGE_IMAGE_RGB24 = 3,
    QW_PAGE_IMAGE_JP2 = 4,
    QW_PAGE_IMAGE_JP2_ALPHA = 5,
    QW_PAGE_IMAGE_JP2_ALPHA_PREMULTIPLIED = 6
} QwPageImageKind;

typedef struct QwPageImage {
    const uint8_t *data;
    size_t size;
    int kind; /* QwPageImageKind */
    uint32_t width;  /* required unless kind is JPEG and dimensions can be read */
    uint32_t height; /* required unless kind is JPEG and dimensions can be read */
} QwPageImage;

/* A provider may decode lazily so large books need not retain every raster. */
typedef int (*QwPageAcquireFn)(void *context,
                               size_t index,
                               QwPageImage *out_page);
typedef void (*QwPageReleaseFn)(void *context, QwPageImage *page);
/* Page acquisition returns 0 on success; any nonzero code aborts conversion. */

typedef enum QwDocumentKind {
    QW_DOCUMENT_PDF_BYTES = 1,
    QW_DOCUMENT_PAGE_SOURCE = 2
} QwDocumentKind;

#define QW_DOCUMENT_REQUIRE_PDF_1_5 (1u << 0)
typedef void (*QwDocumentDisposeFn)(void *context);

/*
 * Borrowed page facade consumed synchronously by a converter. PDF bytes remain
 * owned by the caller. For page sources, acquire/release callbacks and context
 * remain valid until conversion returns. release_page is called once after
 * every acquire_page call, including calls that return an error.
 */
typedef struct QwDocument {
    QwDocumentKind kind;
    unsigned flags;
    const uint8_t *pdf_data;
    size_t pdf_size;
    size_t page_count;
    QwPageAcquireFn acquire_page;
    QwPageReleaseFn release_page;
    void *context;
    QwDocumentDisposeFn dispose;
} QwDocument;

#ifdef __cplusplus
}
#endif

#endif
