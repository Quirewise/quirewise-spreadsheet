#ifndef QUIREWISE_STATUS_H
#define QUIREWISE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(QW_STATUS_BUILD_SHARED)
#    if defined(QW_STATUS_EXPORTS)
#      define QW_API __declspec(dllexport)
#    else
#      define QW_API __declspec(dllimport)
#    endif
#  else
#    define QW_API
#  endif
#elif defined(QW_STATUS_BUILD_SHARED)
#  define QW_API __attribute__((visibility("default")))
#else
#  define QW_API
#endif

typedef enum QwStatus {
    QW_STATUS_OK = 0,
    QW_STATUS_ERR_INVALID_ARGUMENT = -1,
    QW_STATUS_ERR_OUT_OF_MEMORY = -2,
    QW_STATUS_ERR_IO = -3,
    QW_STATUS_ERR_FORMAT = -4,
    QW_STATUS_ERR_UNSUPPORTED = -5,
    QW_STATUS_ERR_DECODE = -6,
    QW_STATUS_ERR_EMPTY = -8,
    QW_STATUS_ERR_EXTERNAL_DECODER_REQUIRED = -9,
    QW_STATUS_ERR_RESOURCE_LIMIT = -11
} QwStatus;

#ifdef __cplusplus
}
#endif

#endif
