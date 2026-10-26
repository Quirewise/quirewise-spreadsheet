#ifndef QUIREWISE_FORMAT_REGISTRY_INTERNAL_H
#define QUIREWISE_FORMAT_REGISTRY_INTERNAL_H

#include "quirewise/convert.h"
#include "data_document.h"
#include "architecture_metadata.h"
#include "audio_document.h"
#include "layout_document.h"
#include "spreadsheet_document.h"
#include "subtitle_document.h"

struct QwSource;

typedef int (*QwFormatProbeFn)(const void *data, size_t size);
typedef int (*QwFormatDecodeDocumentFn)(const void *data,
                                        size_t size,
                                        const QwConvertOptions *options,
                                        QwDocument *out_document,
                                        QwConvertReport *report);
typedef int (*QwFormatDecodeLayoutFn)(const void *data,
                                      size_t size,
                                      QwInputFormat input_format,
                                      const QwConvertOptions *options,
                                      QwLayoutDocument *out_document,
                                      QwConvertReport *report);
typedef int (*QwFormatDecodeAudioFn)(const struct QwSource *source,
                                     QwInputFormat input_format,
                                     const QwConvertOptions *options,
                                     QwAudioDocument *out_document,
                                     QwConvertReport *report);
typedef int (*QwFormatDecodeSpreadsheetFn)(const void *data,
                                           size_t size,
                                           QwInputFormat input_format,
                                           const QwConvertOptions *options,
                                           struct QwSpreadsheetDocument *out_document,
                                           QwConvertReport *report);
typedef int (*QwFormatDecodeSubtitleFn)(const void *data,
                                        size_t size,
                                        QwInputFormat input_format,
                                        const QwConvertOptions *options,
                                        QwSubtitleDocument *out_document,
                                        QwConvertReport *report);
typedef int (*QwFormatDecodeDataFn)(const void *data,
                                    size_t size,
                                    QwInputFormat input_format,
                                    const QwConvertOptions *options,
                                    QwDataDocument *out_document,
                                    QwConvertReport *report);
typedef int (*QwFormatPdfTransformFn)(const void *data,
                                      size_t size,
                                      const QwConvertOptions *options,
                                      uint8_t **out_pdf,
                                      size_t *out_size,
                                      QwConvertReport *report);
typedef int (*QwFormatPathProbeFn)(const char *path);
typedef int (*QwFormatDecodeDocumentPathFn)(const char *path,
                                           const QwConvertOptions *options,
                                           QwDocument *out_document,
                                           QwConvertReport *report);

enum QwFormatCapabilities {
    QW_FORMAT_CAP_PDF_DOCUMENT = 1u << 0,
    QW_FORMAT_CAP_PAGE_IMAGES = 1u << 1,
    QW_FORMAT_CAP_CONTAINER = 1u << 2,
    QW_FORMAT_CAP_MULTI_PAGE = 1u << 3,
    QW_FORMAT_CAP_ARCHIVE_PROBE = 1u << 4
};

typedef struct QwFormatDescriptor {
    QwInputFormat id;
    const char *name;
    unsigned capabilities;
    QwFormatProbeFn probe;
    QwFormatDecodeDocumentFn open_memory;
    QwFormatDecodeLayoutFn open_layout;
    QwFormatDecodeAudioFn open_audio;
    /* Finite source byte budget enforced before planning or decoding. */
    size_t max_input_bytes;
    /* Shared archive, raster, backend, and ownership policy for this input. */
    const QwFormatInputResourceProfile *resource_profile;
    /* Native PDF transform edge, when the format exposes one directly. */
    QwFormatPdfTransformFn convert_to_pdf;
    /* Typed workbook adapter; layout rendering remains available separately. */
    QwFormatDecodeSpreadsheetFn open_spreadsheet;
    /* Timed-cue adapter; layout rendering remains available separately. */
    QwFormatDecodeSubtitleFn open_subtitle;
    /* Ordered typed values shared by structured text format adapters. */
    QwFormatDecodeDataFn open_data;
} QwFormatDescriptor;

#define QW_FORMAT_INPUT_LIMIT_DEFAULT \
    ((size_t)1024u * 1024u * 1024u)
#define QW_FORMAT_INPUT_LIMIT_IMAGE \
    ((size_t)512u * 1024u * 1024u)
#define QW_FORMAT_INPUT_LIMIT_TEXT \
    ((size_t)256u * 1024u * 1024u)

typedef struct QwPathFormatDescriptor {
    QwInputFormat id;
    QwFormatPathProbeFn probe_path;
    QwFormatDecodeDocumentPathFn decode_document_path;
    const struct QwPathResourceProfile *resource_profile;
} QwPathFormatDescriptor;

typedef struct QwPathResourceProfile {
    size_t max_input_files;
    size_t max_total_input_bytes;
    size_t max_single_input_bytes;
    size_t max_pages;
} QwPathResourceProfile;

typedef struct QwFormatMetadata {
    QwMediaKind media_kind;
    unsigned document_kinds;
    unsigned format_capabilities;
    unsigned adapter_capabilities;
    unsigned backend_capabilities;
    unsigned input_capabilities;
    unsigned output_capabilities;
} QwFormatMetadata;

enum QwFormatInputCapabilities {
    QW_FORMAT_INPUT_CAP_AUDIO = 1u << 0,
    QW_FORMAT_INPUT_CAP_ENCRYPTED_AUDIO = 1u << 1
};

enum QwFormatOutputCapabilities {
    QW_FORMAT_OUTPUT_CAP_AUDIO = 1u << 0
};

typedef struct QwFormatDetection {
    QwFormatDescriptor input;
    const QwPathFormatDescriptor *path_format;
    unsigned priority;
    int has_input;
    int requires_path;
} QwFormatDetection;

enum QwFormatDetectionPriority {
    QW_FORMAT_DETECTION_NONE = 0,
    QW_FORMAT_DETECTION_PATH_CONTEXT = 1,
    QW_FORMAT_DETECTION_CONTENT = 2,
    QW_FORMAT_DETECTION_DECLARED_EXTENSION = 3
};

enum QwFormatAdapterCapabilities {
    QW_FORMAT_ADAPTER_CAP_CONTENT_PROBE = 1u << 0,
    QW_FORMAT_ADAPTER_CAP_PATH_PROBE = 1u << 1,
    QW_FORMAT_ADAPTER_CAP_MEMORY_OPEN = 1u << 2,
    QW_FORMAT_ADAPTER_CAP_PATH_OPEN = 1u << 3,
    QW_FORMAT_ADAPTER_CAP_PDF_TRANSFORM = 1u << 4,
    QW_FORMAT_ADAPTER_CAP_AUDIO_INPUT = 1u << 5,
    QW_FORMAT_ADAPTER_CAP_LAYOUT_OPEN = 1u << 6,
    QW_FORMAT_ADAPTER_CAP_SPREADSHEET_OPEN = 1u << 7,
    QW_FORMAT_ADAPTER_CAP_SUBTITLE_OPEN = 1u << 8,
    QW_FORMAT_ADAPTER_CAP_DATA_OPEN = 1u << 9
};

extern const QwFormatDescriptor qw_format_pdf;
extern const QwFormatDescriptor qw_format_rst;
extern const QwPathFormatDescriptor qw_path_format_rst;
extern const QwFormatDescriptor qw_format_textile;
extern const QwPathFormatDescriptor qw_path_format_textile;
extern const QwFormatDescriptor qw_format_org;
extern const QwPathFormatDescriptor qw_path_format_org;
extern const QwFormatDescriptor qw_format_mediawiki;
extern const QwPathFormatDescriptor qw_path_format_mediawiki;
extern const QwFormatDescriptor qw_format_docbook;
extern const QwPathFormatDescriptor qw_path_format_docbook;
extern const QwFormatDescriptor qw_format_adoc;
extern const QwPathFormatDescriptor qw_path_format_adoc;
extern const QwFormatDescriptor qw_format_typ;
extern const QwPathFormatDescriptor qw_path_format_typ;
extern const QwFormatDescriptor qw_format_tex;
extern const QwPathFormatDescriptor qw_path_format_tex;
extern const QwFormatDescriptor qw_format_svg;
extern const QwFormatDescriptor qw_format_svgz;
extern const QwFormatDescriptor qw_format_amr;
extern const QwFormatDescriptor qw_format_au;
extern const QwFormatDescriptor qw_format_voc;
extern const QwFormatDescriptor qw_format_w64;
extern const QwFormatDescriptor qw_format_dsf;
extern const QwFormatDescriptor qw_format_dff;
extern const QwFormatDescriptor qw_format_dss;
extern const QwFormatDescriptor qw_format_djvu;
extern const QwFormatDescriptor qw_format_tiff;
extern const QwFormatDescriptor qw_format_avif;
extern const QwFormatDescriptor qw_format_jxl;
extern const QwFormatDescriptor qw_format_heic;
extern const QwFormatDescriptor qw_format_raw;
extern const QwFormatDescriptor qw_format_xcf;
extern const QwFormatDescriptor qw_format_pcd;
extern const QwFormatDescriptor qw_format_ps;
extern const QwFormatDescriptor qw_format_eps;
extern const QwFormatDescriptor qw_format_ai;
extern const QwFormatDescriptor qw_format_qoi;
extern const QwFormatDescriptor qw_format_wbmp;
extern const QwFormatDescriptor qw_format_ppm;
extern const QwFormatDescriptor qw_format_pam;
extern const QwFormatDescriptor qw_format_xbm;
extern const QwFormatDescriptor qw_format_ico;
extern const QwFormatDescriptor qw_format_cur;
extern const QwFormatDescriptor qw_format_icns;
extern const QwFormatDescriptor qw_format_uvz;
extern const QwFormatDescriptor qw_format_image;
extern const QwFormatDescriptor qw_format_image_folder;
extern const QwFormatDescriptor qw_format_comic;
extern const QwFormatDescriptor qw_format_epub;
extern const QwFormatDescriptor qw_format_docx;
extern const QwFormatDescriptor qw_format_docm;
extern const QwFormatDescriptor qw_format_dotx;
extern const QwFormatDescriptor qw_format_dotm;
extern const QwFormatDescriptor qw_format_odt;
extern const QwFormatDescriptor qw_format_ott;
extern const QwFormatDescriptor qw_format_fodt;
extern const QwFormatDescriptor qw_format_eml;
extern const QwFormatDescriptor qw_format_hwpx;
extern const QwFormatDescriptor qw_format_fb2;
extern const QwFormatDescriptor qw_format_txt;
extern const QwFormatDescriptor qw_format_rtf;
extern const QwFormatDescriptor qw_format_csv;
extern const QwFormatDescriptor qw_format_json;
extern const QwFormatDescriptor qw_format_jsonl;
extern const QwFormatDescriptor qw_format_yaml;
extern const QwFormatDescriptor qw_format_toml;
extern const QwFormatDescriptor qw_format_xml;
extern const QwFormatDescriptor qw_format_vcard;
extern const QwFormatDescriptor qw_format_scc;
extern const QwFormatDescriptor qw_format_man;
extern const QwFormatDescriptor qw_format_opml;
extern const QwPathFormatDescriptor qw_path_format_yaml;
extern const QwPathFormatDescriptor qw_path_format_toml;
extern const QwPathFormatDescriptor qw_path_format_xml;
extern const QwPathFormatDescriptor qw_path_format_vcard;
extern const QwFormatDescriptor qw_format_xlsx;
extern const QwFormatDescriptor qw_format_xlsm;
extern const QwFormatDescriptor qw_format_xltx;
extern const QwFormatDescriptor qw_format_xlt;
extern const QwFormatDescriptor qw_format_xls;
extern const QwFormatDescriptor qw_format_ods;
extern const QwFormatDescriptor qw_format_ots;
extern const QwFormatDescriptor qw_format_fods;
extern const QwFormatDescriptor qw_format_odp;
extern const QwFormatDescriptor qw_format_otp;
extern const QwFormatDescriptor qw_format_fodp;
extern const QwFormatDescriptor qw_format_srt;
extern const QwFormatDescriptor qw_format_vtt;
extern const QwFormatDescriptor qw_format_lrc;
extern const QwFormatDescriptor qw_format_sub;
extern const QwFormatDescriptor qw_format_ttml;
extern const QwFormatDescriptor qw_format_ass;
extern const QwFormatDescriptor qw_format_ssa;
extern const QwFormatDescriptor qw_format_sami;
extern const QwFormatDescriptor qw_format_pptx;
extern const QwFormatDescriptor qw_format_pptm;
extern const QwFormatDescriptor qw_format_potx;
extern const QwFormatDescriptor qw_format_potm;
extern const QwFormatDescriptor qw_format_ppsx;
extern const QwFormatDescriptor qw_format_ppsm;
extern const QwFormatDescriptor qw_format_html;
extern const QwFormatDescriptor qw_format_htmlz;
extern const QwFormatDescriptor qw_format_txtz;
extern const QwFormatDescriptor qw_format_pmlz;
extern const QwFormatDescriptor qw_format_markdown;
extern const QwFormatDescriptor qw_format_ipynb;
extern const QwFormatDescriptor qw_format_xps;
extern const QwFormatDescriptor qw_format_ceb;
extern const QwFormatDescriptor qw_format_caj;
extern const QwFormatDescriptor qw_format_mobi;
extern const QwFormatDescriptor qw_format_azw3;
extern const QwFormatDescriptor qw_format_mp2;
extern const QwFormatDescriptor qw_format_mp3;
extern const QwFormatDescriptor qw_format_flac;
extern const QwFormatDescriptor qw_format_m4a;
extern const QwFormatDescriptor qw_format_wav;
extern const QwFormatDescriptor qw_format_ogg;
extern const QwFormatDescriptor qw_format_aac;
extern const QwFormatDescriptor qw_format_ape;
extern const QwFormatDescriptor qw_format_wma;
extern const QwFormatDescriptor qw_format_aiff;
extern const QwFormatDescriptor qw_format_caf;
extern const QwFormatDescriptor qw_format_matroska;
extern const QwFormatDescriptor qw_format_ac3;
extern const QwFormatDescriptor qw_format_dts;
extern const QwFormatDescriptor qw_format_wavpack;
extern const QwFormatDescriptor qw_format_tta;
extern const QwFormatDescriptor qw_format_tak;
extern const QwFormatDescriptor qw_format_mpc;
extern const QwFormatDescriptor qw_format_ncm;
extern const QwFormatDescriptor qw_format_qmc;
extern const QwFormatDescriptor qw_format_kgm;
extern const QwFormatDescriptor qw_format_kwm;
extern const QwPathFormatDescriptor qw_path_format_image_folder;
extern const QwPathFormatDescriptor qw_path_format_ncm;
extern const QwPathFormatDescriptor qw_path_format_qmc;
extern const QwPathFormatDescriptor qw_path_format_kgm;
extern const QwPathFormatDescriptor qw_path_format_kwm;

const QwFormatDescriptor *qw_format_find(QwInputFormat id);
const QwFormatDescriptor *qw_format_detect(const void *data, size_t size);
const QwPathFormatDescriptor *qw_format_detect_path(const char *path);
size_t qw_format_registry_count(void);
const QwFormatDescriptor *qw_format_registry_at(size_t index);
size_t qw_path_format_registry_count(void);
const QwPathFormatDescriptor *qw_path_format_registry_at(size_t index);
int qw_format_metadata_get(QwInputFormat id, QwFormatMetadata *out_metadata);
int qw_format_supports(QwInputFormat id, unsigned capability);
int qw_format_output_supports(QwOutputFormat id, unsigned capability);
size_t qw_format_max_bytes(QwInputFormat id);
int qw_format_size_within_limit(QwInputFormat id, size_t input_bytes);
const QwFormatInputResourceProfile *
qw_format_resource_profile_get(QwInputFormat id);
int qw_format_registry_validate(char *error, size_t error_capacity);
int qw_format_detect_source(const struct QwSource *source,
                            QwFormatDetection *out_detection);
int qw_format_detection_from_descriptor(const QwFormatDescriptor *format,
                                        const QwPathFormatDescriptor *path,
                                        unsigned priority,
                                        QwFormatDetection *out_detection);

/* Native text-only PDF route used by simple packaged text formats. */
int qw_native_text_pdf_from_utf8(const uint8_t *text, size_t text_size,
                                uint8_t **out_pdf, size_t *out_size);

#endif
