#ifndef QUIREWISE_ARCHITECTURE_METADATA_INTERNAL_H
#define QUIREWISE_ARCHITECTURE_METADATA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* Shared finite resource bounds for format adapters and their readers. */
#define QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRIES ((size_t)50000u)
#define QW_INPUT_RESOURCE_MAX_ARCHIVE_INPUT_BYTES \
    ((size_t)1024u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRY_BYTES \
    ((uint64_t)256u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_ARCHIVE_TOTAL_BYTES \
    ((uint64_t)1024u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_IMAGE_DIMENSION ((size_t)32768u)
#define QW_INPUT_RESOURCE_MAX_DECODED_PIXELS ((size_t)20000000u)

enum QwInputBackendPolicy {
    QW_INPUT_BACKEND_POLICY_FORMAT_METADATA = 1u
};

enum QwInputLifecyclePolicy {
    QW_INPUT_LIFECYCLE_TYPED_DOCUMENT_DISPOSE = 1u
};

typedef struct QwFormatInputResourceProfile {
    size_t max_archive_input_bytes;
    size_t max_archive_entries;
    uint64_t max_archive_entry_bytes;
    uint64_t max_archive_total_bytes;
    size_t max_image_dimension;
    size_t max_decoded_pixels;
    unsigned backend_policy;
    unsigned lifecycle_policy;
} QwFormatInputResourceProfile;

#define QW_FORMAT_INPUT_RESOURCE_PROFILE_DEFAULT \
    (&(QwFormatInputResourceProfile){ \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_INPUT_BYTES, \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRIES, \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRY_BYTES, \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_TOTAL_BYTES, \
        QW_INPUT_RESOURCE_MAX_IMAGE_DIMENSION, \
        QW_INPUT_RESOURCE_MAX_DECODED_PIXELS, \
        QW_INPUT_BACKEND_POLICY_FORMAT_METADATA, \
        QW_INPUT_LIFECYCLE_TYPED_DOCUMENT_DISPOSE \
    })

#define QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_ENTRIES ((size_t)10000u)
#define QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_ENTRY_BYTES \
    ((uint64_t)128u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_TOTAL_BYTES \
    ((uint64_t)512u * 1024u * 1024u)
#define QW_FORMAT_INPUT_RESOURCE_PROFILE_DOCUMENT_ARCHIVE \
    (&(QwFormatInputResourceProfile){ \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_INPUT_BYTES, \
        QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_ENTRIES, \
        QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_ENTRY_BYTES, \
        QW_INPUT_RESOURCE_MAX_DOCUMENT_ARCHIVE_TOTAL_BYTES, \
        QW_INPUT_RESOURCE_MAX_IMAGE_DIMENSION, \
        QW_INPUT_RESOURCE_MAX_DECODED_PIXELS, \
        QW_INPUT_BACKEND_POLICY_FORMAT_METADATA, \
        QW_INPUT_LIFECYCLE_TYPED_DOCUMENT_DISPOSE \
    })

#define QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_ENTRIES ((size_t)10000u)
#define QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_ENTRY_BYTES \
    QW_INPUT_RESOURCE_MAX_ARCHIVE_ENTRY_BYTES
#define QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_TOTAL_BYTES \
    ((uint64_t)512u * 1024u * 1024u)
#define QW_FORMAT_INPUT_RESOURCE_PROFILE_COMIC_ARCHIVE \
    (&(QwFormatInputResourceProfile){ \
        QW_INPUT_RESOURCE_MAX_ARCHIVE_INPUT_BYTES, \
        QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_ENTRIES, \
        QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_ENTRY_BYTES, \
        QW_INPUT_RESOURCE_MAX_COMIC_ARCHIVE_TOTAL_BYTES, \
        QW_INPUT_RESOURCE_MAX_IMAGE_DIMENSION, \
        QW_INPUT_RESOURCE_MAX_DECODED_PIXELS, \
        QW_INPUT_BACKEND_POLICY_FORMAT_METADATA, \
        QW_INPUT_LIFECYCLE_TYPED_DOCUMENT_DISPOSE \
    })

#define QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_INPUT_BYTES \
    ((size_t)256u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRIES ((size_t)10000u)
#define QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRY_BYTES \
    ((uint64_t)64u * 1024u * 1024u)
#define QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_TOTAL_BYTES \
    ((uint64_t)256u * 1024u * 1024u)
#define QW_FORMAT_INPUT_RESOURCE_PROFILE_TEXT_ARCHIVE \
    (&(QwFormatInputResourceProfile){ \
        QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_INPUT_BYTES, \
        QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRIES, \
        QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_ENTRY_BYTES, \
        QW_INPUT_RESOURCE_MAX_TEXT_ARCHIVE_TOTAL_BYTES, \
        QW_INPUT_RESOURCE_MAX_IMAGE_DIMENSION, \
        QW_INPUT_RESOURCE_MAX_DECODED_PIXELS, \
        QW_INPUT_BACKEND_POLICY_FORMAT_METADATA, \
        QW_INPUT_LIFECYCLE_TYPED_DOCUMENT_DISPOSE \
    })

typedef enum QwMediaKind {
    QW_MEDIA_KIND_UNKNOWN = 0,
    QW_MEDIA_KIND_FORMAT_DOCUMENT,
    QW_MEDIA_KIND_PDF_BYTES,
    QW_MEDIA_KIND_PAGE_DOCUMENT,
    QW_MEDIA_KIND_LAYOUT_DOCUMENT,
    QW_MEDIA_KIND_AUDIO_DOCUMENT,
    QW_MEDIA_KIND_SPREADSHEET_DOCUMENT,
    QW_MEDIA_KIND_SUBTITLE_DOCUMENT,
    QW_MEDIA_KIND_DATA_DOCUMENT,
    QW_MEDIA_KIND_ATTACHMENT_SET,
    QW_MEDIA_KIND_TARGET_BYTES,
    QW_MEDIA_KIND_COUNT
} QwMediaKind;

#define QW_MEDIA_KIND_BIT(kind) (1u << ((unsigned)(kind) - 1u))

typedef enum QwBackendCapability {
    QW_BACKEND_CAP_NONE = 0,
    QW_BACKEND_CAP_PDF_LAYOUT = 1u << 0,
    QW_BACKEND_CAP_PDF_RASTER = 1u << 1,
    QW_BACKEND_CAP_IMAGE_DECODE = 1u << 2,
    QW_BACKEND_CAP_AUDIO_DECODE = 1u << 3,
    QW_BACKEND_CAP_AUDIO_ENCODE = 1u << 4,
    QW_BACKEND_CAP_PDFA_REWRITE = 1u << 5,
    QW_BACKEND_CAP_FORMAT_ADAPTER = 1u << 6,
    QW_BACKEND_CAP_IMAGE_ENCODE = 1u << 7,
    QW_BACKEND_CAP_PDFA_VERIFY = 1u << 8,
    QW_BACKEND_CAP_GHOSTSCRIPT = 1u << 9
} QwBackendCapability;

enum QwRouteQualityLossFlags {
    QW_ROUTE_LOSS_RASTERIZATION = 1u << 0,
    QW_ROUTE_LOSS_TEXT_LAYOUT = 1u << 1,
    QW_ROUTE_LOSS_METADATA = 1u << 2,
    QW_ROUTE_LOSS_SEMANTICS_UNVERIFIED = 1u << 3,
    QW_ROUTE_LOSS_CODEC_DEPENDENT = 1u << 4,
    QW_ROUTE_LOSS_FORMAT_DEPENDENT = 1u << 5
};

#define QW_ROUTE_LOSS_ALL \
    (QW_ROUTE_LOSS_RASTERIZATION | QW_ROUTE_LOSS_TEXT_LAYOUT | \
     QW_ROUTE_LOSS_METADATA | QW_ROUTE_LOSS_SEMANTICS_UNVERIFIED | \
     QW_ROUTE_LOSS_CODEC_DEPENDENT | QW_ROUTE_LOSS_FORMAT_DEPENDENT)

#define QW_BACKEND_CAP_ALL \
    (QW_BACKEND_CAP_PDF_LAYOUT | QW_BACKEND_CAP_PDF_RASTER | \
     QW_BACKEND_CAP_IMAGE_DECODE | QW_BACKEND_CAP_AUDIO_DECODE | \
     QW_BACKEND_CAP_AUDIO_ENCODE | QW_BACKEND_CAP_PDFA_REWRITE | \
     QW_BACKEND_CAP_FORMAT_ADAPTER | QW_BACKEND_CAP_IMAGE_ENCODE | \
     QW_BACKEND_CAP_PDFA_VERIFY | QW_BACKEND_CAP_GHOSTSCRIPT)

enum QwRegistryEntryFlags {
    QW_REGISTRY_ENTRY_HAS_PROBE = 1u << 0,
    QW_REGISTRY_ENTRY_HAS_OPEN = 1u << 1,
    QW_REGISTRY_ENTRY_HAS_EDGE = 1u << 2
};

enum QwRegistryValidationRequirements {
    QW_REGISTRY_REQUIRE_UNIQUE_IDS = 1u << 0,
    QW_REGISTRY_REQUIRE_NAMES = 1u << 1,
    QW_REGISTRY_REQUIRE_PROBES = 1u << 2,
    QW_REGISTRY_REQUIRE_OPENERS = 1u << 3,
    QW_REGISTRY_REQUIRE_EDGES = 1u << 4
};

typedef struct QwRegistryValidationEntry {
    int id;
    const char *name;
    unsigned flags;
} QwRegistryValidationEntry;

typedef struct QwRouteValidationEntry {
    const char *name;
    QwMediaKind input_kind;
    QwMediaKind output_kind;
    unsigned required_backends;
    unsigned quality_loss_flags;
    int has_matcher;
    int has_executor;
} QwRouteValidationEntry;

int qw_registry_entries_validate(const QwRegistryValidationEntry *entries,
                                 size_t count,
                                 unsigned requirements,
                                 char *error,
                                 size_t error_capacity);
int qw_route_metadata_validate(const QwRouteValidationEntry *routes,
                               size_t count,
                               char *error,
                               size_t error_capacity);

#endif
