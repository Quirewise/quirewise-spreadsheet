#ifndef QUIREWISE_CONVERT_H
#define QUIREWISE_CONVERT_H

#include "quirewise/status.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QwFormatId {
    QW_FORMAT_UNKNOWN = 0,
    QW_FORMAT_PDF,
    QW_FORMAT_UVZ,
    QW_FORMAT_CAJ,
    QW_FORMAT_CEB,
    QW_FORMAT_DJVU,
    QW_FORMAT_EPUB,
    QW_FORMAT_MOBI,
    QW_FORMAT_AZW3,
    QW_FORMAT_TXT,
    QW_FORMAT_DOCX,
    QW_FORMAT_IMAGE,
    QW_FORMAT_IMAGE_FOLDER,
    QW_FORMAT_COMIC,
    QW_FORMAT_NCM,
    QW_FORMAT_QMC,
    QW_FORMAT_KGM,
    QW_FORMAT_KWM,
    QW_FORMAT_MP3,
    QW_FORMAT_FLAC,
    QW_FORMAT_M4A,
    QW_FORMAT_WAV,
    QW_FORMAT_OGG,
    QW_FORMAT_TIFF,
    QW_FORMAT_AAC,
    /* Extended audio IDs; input and output capabilities register independently. */
    QW_FORMAT_APE,
    QW_FORMAT_WMA,
    QW_FORMAT_AIFF,
    QW_FORMAT_CAF,
    QW_FORMAT_MATROSKA,
    QW_FORMAT_AC3,
    QW_FORMAT_DTS,
    QW_FORMAT_WAVPACK,
    QW_FORMAT_TTA,
    QW_FORMAT_TAK,
    QW_FORMAT_MPC,
    QW_FORMAT_ALAC,
    QW_FORMAT_OPUS,
    QW_FORMAT_HTML,
    QW_FORMAT_XPS,
    QW_FORMAT_WEBM,
    QW_FORMAT_MARKDOWN,
    QW_FORMAT_OGG_FLAC,
    QW_FORMAT_MP2,
    QW_FORMAT_EAC3,
    QW_FORMAT_SPEEX,
    QW_FORMAT_WAV24,
    QW_FORMAT_WAV32,
    QW_FORMAT_AIFF24,
    QW_FORMAT_AIFF32,
    QW_FORMAT_CAF24,
    QW_FORMAT_CAF32,
    /* Appended to preserve the numeric IDs of existing public formats. */
    QW_FORMAT_ODT,
    QW_FORMAT_FB2,
    QW_FORMAT_RTF,
    QW_FORMAT_CSV,
    QW_FORMAT_JSON,
    /* Added as an output target without changing existing public IDs. */
    QW_FORMAT_WEBP,
    QW_FORMAT_PNG,
    QW_FORMAT_BMP,
    QW_FORMAT_QOI,
    QW_FORMAT_WBMP,
    QW_FORMAT_PPM,
    QW_FORMAT_PAM,
    QW_FORMAT_XBM,
    QW_FORMAT_ICO,
    QW_FORMAT_CUR,
    QW_FORMAT_ICNS,
    QW_FORMAT_TGA,
    QW_FORMAT_PCX,
    QW_FORMAT_JPG,
    QW_FORMAT_RAS,
    QW_FORMAT_SGI,
    QW_FORMAT_XPM,
    QW_FORMAT_MIFF,
    QW_FORMAT_GIF,
    QW_FORMAT_APNG,
    QW_FORMAT_MNG,
    QW_FORMAT_PSD,
    QW_FORMAT_JP2,
    QW_FORMAT_DDS,
    QW_FORMAT_PFM,
    QW_FORMAT_PSB,
    /* Appended archive ebook wrappers; existing IDs stay stable. */
    QW_FORMAT_HTMLZ,
    QW_FORMAT_TXTZ,
    QW_FORMAT_PMLZ,
    /* Appended vector and audio inputs; existing format IDs stay stable. */
    QW_FORMAT_SVG,
    QW_FORMAT_SVGZ,
    QW_FORMAT_AMR,
    QW_FORMAT_AU,
    QW_FORMAT_VOC,
    QW_FORMAT_W64,
    QW_FORMAT_DSF,
    QW_FORMAT_DFF,
    QW_FORMAT_DSS,
    /* Output-only aliases sharing the existing MP4 and WebM audio containers. */
    QW_FORMAT_M4B,
    QW_FORMAT_WEBA,
    /* AVIF is a single-image input and output format. */
    QW_FORMAT_AVIF,
    /* JPEG XL single-image input and output. */
    QW_FORMAT_JXL,
    /* PostScript and EPS use the statically linked Ghostscript adapter. */
    QW_FORMAT_PS,
    QW_FORMAT_EPS,
    /* Legacy PostScript-based Illustrator documents use Ghostscript to PDF. */
    QW_FORMAT_AI,
    /* HEIC/HEIF single-image input and lossy HEVC output. */
    QW_FORMAT_HEIC,
    /* Camera RAW inputs decoded through the LibRaw C API. */
    QW_FORMAT_RAW,
    /* GIMP XCF layered images, flattened through the native page decoder. */
    QW_FORMAT_XCF,
    /* Kodak Photo CD still images decoded through FFmpeg libavcodec. */
    QW_FORMAT_PCD,
    /* WordprocessingML variants share the native DOCX layout reader. */
    QW_FORMAT_DOCM,
    QW_FORMAT_DOTX,
    QW_FORMAT_DOTM,
    /* OpenDocument text templates and single-file XML documents. */
    QW_FORMAT_OTT,
    QW_FORMAT_FODT,
    /* EML messages expose MIME text and attachment metadata through layout. */
    QW_FORMAT_EML,
    /* HWPX OWPML text is extracted through the shared layout document. */
    QW_FORMAT_HWPX,
    /* JSON Lines contains one independent JSON value per physical line. */
    QW_FORMAT_JSONL,
    /* OOXML spreadsheet inputs share the bounded XLSX layout reader. */
    QW_FORMAT_XLSX,
    QW_FORMAT_XLSM,
    QW_FORMAT_XLTX,
    /* OpenDocument spreadsheet packages and flat XML share the layout reader. */
    QW_FORMAT_ODS,
    QW_FORMAT_OTS,
    QW_FORMAT_FODS,
    /* OpenDocument presentation packages and flat XML share a slide reader. */
    QW_FORMAT_ODP,
    QW_FORMAT_OTP,
    QW_FORMAT_FODP,
    /* PowerPoint Open XML families share a bounded slide-text reader. */
    QW_FORMAT_PPTX,
    QW_FORMAT_PPTM,
    QW_FORMAT_POTX,
    QW_FORMAT_POTM,
    QW_FORMAT_PPSX,
    QW_FORMAT_PPSM,
    /* Legacy BIFF8 workbooks and templates use the bounded CFB reader. */
    QW_FORMAT_XLT,
    QW_FORMAT_XLS,
    /* TSV is a separate output target; existing CSV/TSV input stays shared. */
    QW_FORMAT_TSV,
    /* Subtitle interchange formats use a timed-cue document model. */
    QW_FORMAT_SRT,
    QW_FORMAT_VTT,
    QW_FORMAT_LRC,
    /* YAML structured data uses the shared ordered value document. */
    QW_FORMAT_YAML,
    /* TOML structured data preserves native date/time scalar values. */
    QW_FORMAT_TOML,
    /* XML preserves ordered element content and string attributes. */
    QW_FORMAT_XML,
    /* vCard preserves ordered properties, groups, parameters and raw values. */
    QW_FORMAT_VCF,
    /* MicroDVD subtitles use frame-addressed cues and a source frame rate. */
    QW_FORMAT_SUB,
    /* Timed Text Markup Language cues use the shared subtitle document. */
    QW_FORMAT_TTML,
    /* ASS and SSA subtitle scripts retain timed text in a shared cue model. */
    QW_FORMAT_ASS,
    QW_FORMAT_SSA,
    QW_FORMAT_SAMI,
    /* Scenarist SCC uses a bounded caption input and output subset. */
    QW_FORMAT_SCC,
    /* Man pages use a bounded UTF-8 roff-to-layout reader. */
    QW_FORMAT_MAN,
    /* OPML outlines use the shared XML parser and layout document route. */
    QW_FORMAT_OPML,
    /* Jupyter Notebook cells share the JSON tree and Markdown layout reader. */
    QW_FORMAT_IPYNB,
    /* reStructuredText opens through a bounded CommonMark-backed reader. */
    QW_FORMAT_RST,
    /* Textile uses the bounded shared CommonMark layout adapter. */
    QW_FORMAT_TEXTILE,
    /* Org mode uses a bounded CommonMark-backed layout reader. */
    QW_FORMAT_ORG,
    /* MediaWiki uses a bounded subset reader through the shared layout. */
    QW_FORMAT_MEDIAWIKI,
    /* DocBook XML uses the shared ordered XML tree and layout writers. */
    QW_FORMAT_DOCBOOK,
    /* AsciiDoc uses a bounded reader through CommonMark and shared layouts. */
    QW_FORMAT_ADOC,
    /* Typst markup uses a bounded subset reader and shared layout writer. */
    QW_FORMAT_TYP,
    /* LaTeX uses a bounded source reader and shared layout writer. */
    QW_FORMAT_TEX,
    /* Radiance RGBE is a single-page image output target. */
    QW_FORMAT_HDR,
    /* Comic Book ZIP packages one PNG image per page. */
    QW_FORMAT_CBZ,
    /* Comic Book TAR packages one PNG image per page. */
    QW_FORMAT_CBT,
    QW_FORMAT_COUNT
} QwFormatId;

/* Shared IDs cover inputs and outputs; capability is registered per direction. */
typedef QwFormatId QwInputFormat;
typedef QwFormatId QwOutputFormat;

typedef enum QwPdfaLevel {
    QW_PDFA_NONE = 0,
    QW_PDFA_1B = 1,
    QW_PDFA_2B = 2,
    QW_PDFA_3B = 3
} QwPdfaLevel;

typedef enum QwScanBinarizeMode {
    QW_SCAN_BINARIZE_NONE = 0,
    QW_SCAN_BINARIZE_OTSU = 1,
    QW_SCAN_BINARIZE_WOLF = 2
} QwScanBinarizeMode;

typedef enum QwScanSplitMode {
    QW_SCAN_SPLIT_NONE = 0,
    QW_SCAN_SPLIT_DOUBLE = 1
} QwScanSplitMode;

typedef struct QwConvertOptions {
    int page_mode;             /* ContainerPdfPageMode; defaults to A4 */
    int pdfa_level;            /* QwPdfaLevel; defaults to NONE */
    int optimize;              /* Apply /ebook during PDF/A conversion */
    const char *qmc_ekey;      /* Optional EKey for QMC STag/MusicEx or KGM v5 */
    size_t audio_track;        /* Audio output: 0 selects best; otherwise 1-based */
    const char *zip_password;  /* Optional archive password; ZIP/CBZ and 7z/CB7 */
    int scan_binarize;         /* QwScanBinarizeMode; optional PDF page processing */
    unsigned scan_wolf_window; /* Odd local window size; 0 selects the default */
    unsigned scan_despeckle_diameter; /* Remove smaller isolated components; 0 disables */
    int scan_deskew;           /* Estimate and correct text-line skew */
    int scan_crop;             /* Crop detected page margins after geometry correction */
    unsigned scan_crop_padding;/* Pixels retained around detected content; 0 selects default */
    int scan_illumination;     /* Normalize uneven paper illumination */
    int scan_split_mode;       /* QwScanSplitMode; DOUBLE emits left then right page */
    int scan_ccitt_g4;         /* Encode the final bilevel page as CCITT Group 4 */
    int scan_perspective;      /* Correct a confidently detected page quadrilateral */
    int scan_dewarp;           /* Flatten text-line curvature when profile confidence is high */
    const char *qmc_ekey_db_path; /* Optional QQ player_process_db for QMC */
} QwConvertOptions;

typedef enum QwConvertStage {
    QW_CONVERT_STAGE_NONE = 0,
    QW_CONVERT_STAGE_PROBE,
    QW_CONVERT_STAGE_OPEN,
    QW_CONVERT_STAGE_PLAN,
    QW_CONVERT_STAGE_EXECUTE,
    QW_CONVERT_STAGE_PUBLISH
} QwConvertStage;

#define QW_CONVERT_OPTIONS_INIT \
    { 0, QW_PDFA_NONE, 0, NULL, 0, NULL, \
      QW_SCAN_BINARIZE_NONE, 0, 0, 0, 0, 0, 0, QW_SCAN_SPLIT_NONE, 0, 0, 0, NULL }

typedef struct QwConvertReport {
    QwInputFormat format;
    size_t pages_written;
    size_t output_size;
    size_t skipped_pages;
    char note[192];
    QwOutputFormat output_format;
    /* Appended ABI field identifying the last active phase. */
    QwConvertStage stage;
} QwConvertReport;

/* Detect a known input format from its contents, including container structure. */
QW_API QwInputFormat qw_detect_format(const void *data, size_t size);
QW_API const char *qw_format_name(QwInputFormat format);

/*
 * Convert a memory buffer to a registered output format. On success,
 * *out_data is owned by the caller and must be released with qw_buffer_free.
 */
QW_API int qw_convert_to_format_memory(const void *data,
                                        size_t size,
                                        QwOutputFormat output_format,
                                        const QwConvertOptions *options,
                                        uint8_t **out_data,
                                        size_t *out_size,
                                        QwConvertReport *report);
QW_API void qw_buffer_free(void *buffer);

/*
 * Convert a memory buffer to PDF. On success, *out_pdf is malloc-owned by the
 * caller and must be released with qw_buffer_free().
 */
QW_API int qw_convert_to_pdf_memory(const void *data,
                                     size_t size,
                                     const QwConvertOptions *options,
                                     uint8_t **out_pdf,
                                     size_t *out_size,
                                     QwConvertReport *report);

/*
 * Convert one file to a new PDF file. Existing output paths are never
 * overwritten. A failed write may leave a partial output for manual review.
 */
QW_API int qw_convert_file(const char *input_path,
                            const char *output_path,
                            const QwConvertOptions *options,
                            QwConvertReport *report);

/* Generic target-format counterpart; the output extension must match target. */
QW_API int qw_convert_file_to_format(const char *input_path,
                                      const char *output_path,
                                      QwOutputFormat output_format,
                                      const QwConvertOptions *options,
                                      QwConvertReport *report);

/*
 * Check whether a file can be read through its registered Quirewise route.
 * PDF inputs are opened and every page is rendered; other non-audio inputs
 * use the PDF conversion route, while audio inputs use FLAC. Any generated
 * bytes are discarded without creating an output file. Memory use can
 * therefore be similar to a normal in-memory conversion. Only input keys,
 * archive passwords, and audio_track are used from options; output processing
 * and page-selection options are ignored so the check covers all pages.
 */
QW_API int qw_check_file(const char *input_path,
                          const QwConvertOptions *options,
                          QwConvertReport *report);

#ifdef __cplusplus
}
#endif

#endif
