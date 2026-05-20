#ifndef __WINE_COMDLG32_UNIXLIB_H
#define __WINE_COMDLG32_UNIXLIB_H

#include "windef.h"
#include "winternl.h"
#include "wine/unixlib.h"

#define PORTAL_STR_MAX     4096
#define PORTAL_PATH_MAX    4096
#define PORTAL_FILTERS_MAX 8192
/* Packed OPENFILENAMEW multi-select results can be large. */
#define PORTAL_RESULT_MAX  65536

/* Internal portal request flags carried in portal_open_file_params.flags. */
#define PORTAL_OPEN_FLAG_MULTIPLE   0x00000001
#define PORTAL_OPEN_FLAG_DIRECTORY  0x00000002

struct portal_open_file_params
{
    char title_utf8[PORTAL_STR_MAX];
    char initial_dir_utf8[PORTAL_PATH_MAX];
    char filters_blob[PORTAL_FILTERS_MAX];
    UINT filters_blob_len;
    UINT filter_count;         /* number of (name, pattern) pairs */
    UINT current_filter_index; /* 1-based index, 0 if unset */
    DWORD flags;
    UINT max_results;
    UINT result_count; /* 0 if none */
    UINT result_grouped; /* nonzero if buffer is dir + filenames */
    UINT result_buffer_len; /* WCHARs, including final NUL(s); may be required length on overflow */
    WCHAR result_buffer[PORTAL_RESULT_MAX]; /* Out: packed OPENFILENAMEW result */
};

struct portal_save_file_params
{
    char title_utf8[PORTAL_STR_MAX];
    char initial_dir_utf8[PORTAL_PATH_MAX];
    char initial_filename_utf8[PORTAL_PATH_MAX];
    char current_file_unix[PORTAL_PATH_MAX];
    char filters_blob[PORTAL_FILTERS_MAX];
    UINT filters_blob_len;
    UINT filter_count;         /* number of (name, pattern) pairs */
    UINT current_filter_index; /* 1-based index, 0 if unset */
    DWORD flags;
    WCHAR result_path[PORTAL_PATH_MAX]; /* Out: selected path */
    UINT result_path_len; /* characters, excluding NUL; 0 if none */
};

struct portal_is_available_params
{
    UINT reserved;
};

enum comdlg32_unix_funcs
{
    unix_portal_open_file,
    unix_portal_save_file,
    unix_portal_is_available,
};

#endif /* __WINE_COMDLG32_UNIXLIB_H */
