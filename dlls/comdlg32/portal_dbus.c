/*
 * XDG Desktop Portal File Chooser D-Bus integration
 *
 * Copyright 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <dbus/dbus.h>

#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#undef WIN32_NO_STATUS
#include "ntstatus.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(comdlg32);

/* We dlopen libdbus at runtime, so don't hard-require the development package.
 * When configure can't determine the SONAME (e.g. missing libdbus-devel for
 * 32-bit builds), fall back to the common Linux SONAME.
 */
#ifndef SONAME_LIBDBUS_1
#define SONAME_LIBDBUS_1 "libdbus-1.so.3"
#endif

/* D-Bus function pointers */
#define DBUS_FUNCS \
    DO_FUNC(dbus_bus_add_match); \
    DO_FUNC(dbus_bus_remove_match); \
    DO_FUNC(dbus_bus_get); \
    DO_FUNC(dbus_connection_add_filter); \
    DO_FUNC(dbus_connection_remove_filter); \
    DO_FUNC(dbus_connection_read_write_dispatch); \
    DO_FUNC(dbus_connection_send_with_reply_and_block); \
    DO_FUNC(dbus_connection_unref); \
    DO_FUNC(dbus_error_free); \
    DO_FUNC(dbus_error_init); \
    DO_FUNC(dbus_error_is_set); \
    DO_FUNC(dbus_message_get_args); \
    DO_FUNC(dbus_message_get_interface); \
    DO_FUNC(dbus_message_get_member); \
    DO_FUNC(dbus_message_get_path); \
    DO_FUNC(dbus_message_get_type); \
    DO_FUNC(dbus_message_is_signal); \
    DO_FUNC(dbus_message_iter_append_basic); \
    DO_FUNC(dbus_message_iter_close_container); \
    DO_FUNC(dbus_message_iter_get_arg_type); \
    DO_FUNC(dbus_message_iter_get_basic); \
    DO_FUNC(dbus_message_iter_get_fixed_array); \
    DO_FUNC(dbus_message_iter_init); \
    DO_FUNC(dbus_message_iter_init_append); \
    DO_FUNC(dbus_message_iter_next); \
    DO_FUNC(dbus_message_iter_open_container); \
    DO_FUNC(dbus_message_iter_recurse); \
    DO_FUNC(dbus_message_new_method_call); \
    DO_FUNC(dbus_message_iter_append_fixed_array); \
    DO_FUNC(dbus_message_unref);

#define DO_FUNC(f) static typeof(f) * p_##f
DBUS_FUNCS;
#undef DO_FUNC

static BOOL load_dbus_functions(void)
{
    void *handle;

    handle = dlopen(SONAME_LIBDBUS_1, RTLD_NOW);
    if (!handle)
    {
        WARN("Failed to load %s: %s\n", SONAME_LIBDBUS_1, dlerror());
        return FALSE;
    }

#define DO_FUNC(f) \
    if (!(p_##f = dlsym(handle, #f))) \
    { \
        WARN("Failed to load symbol %s: %s\n", #f, dlerror()); \
        return FALSE; \
    }
    DBUS_FUNCS;
#undef DO_FUNC

    return TRUE;
}

/* Portal context for tracking async request */
struct portal_context
{
    DBusConnection *connection;
    char *request_path;         /* Portal request object path */
    BOOL filter_added;
    BOOL response_received;
    UINT response_code;         /* 0=success, 1=cancelled, 2=error */
    char **result_uris;         /* file:// URIs from portal */
    UINT result_uri_count;
};

/* Helper: Split pattern string by semicolons (e.g., "*.txt;*.doc" -> ["*.txt", "*.doc"]) */
static char **split_pattern(const char *pattern, UINT *count)
{
    char *copy, *token, *saveptr;
    char **result = NULL;
    UINT capacity = 4, size = 0;

    *count = 0;
    if (!pattern || !*pattern) return NULL;

    copy = strdup(pattern);
    result = malloc(capacity * sizeof(char*));

    token = strtok_r(copy, ";", &saveptr);
    while (token)
    {
        /* Trim spaces */
        while (*token == ' ') token++;

        if (*token)
        {
            if (size >= capacity)
            {
                capacity *= 2;
                result = realloc(result, capacity * sizeof(char*));
            }
            result[size++] = strdup(token);
        }
        token = strtok_r(NULL, ";", &saveptr);
    }

    free(copy);
    *count = size;
    return result;
}

/* Helper: Append a single filter to D-Bus message */
static void append_single_filter(DBusMessageIter *filters_array, const char *name, const char *pattern)
{
    DBusMessageIter filter_struct, patterns_array, pattern_struct;
    char **patterns;
    UINT pattern_count, i;
    dbus_uint32_t type = 0; /* 0 = glob pattern */

    /* Open filter struct: (sa(us)) */
    p_dbus_message_iter_open_container(filters_array, DBUS_TYPE_STRUCT, NULL, &filter_struct);

    /* Append filter name */
    p_dbus_message_iter_append_basic(&filter_struct, DBUS_TYPE_STRING, &name);

    /* Open patterns array: a(us) */
    p_dbus_message_iter_open_container(&filter_struct, DBUS_TYPE_ARRAY, "(us)", &patterns_array);

    /* Split pattern by semicolons */
    patterns = split_pattern(pattern, &pattern_count);
    if (patterns)
    {
        for (i = 0; i < pattern_count; i++)
        {
            /* Open pattern struct: (us) */
            p_dbus_message_iter_open_container(&patterns_array, DBUS_TYPE_STRUCT, NULL, &pattern_struct);
            p_dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_UINT32, &type);
            p_dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_STRING, &patterns[i]);
            p_dbus_message_iter_close_container(&patterns_array, &pattern_struct);

            free(patterns[i]);
        }
        free(patterns);
    }

    p_dbus_message_iter_close_container(&filter_struct, &patterns_array);
    p_dbus_message_iter_close_container(filters_array, &filter_struct);
}

/* Build filter array option for D-Bus: a(sa(us)) */
static BOOL get_next_filter_pair(const char **cursor, const char *end,
                                 const char **name, const char **pattern)
{
    const char *p = *cursor;
    const char *q;

    if (!p || p >= end || !*p) return FALSE;

    /* name */
    q = p;
    while (q < end && *q) q++;
    if (q >= end) return FALSE;
    *name = p;
    p = q + 1;

    if (p >= end || !*p) return FALSE;

    /* pattern */
    q = p;
    while (q < end && *q) q++;
    if (q >= end) return FALSE;
    *pattern = p;
    p = q + 1;

    *cursor = p;
    return TRUE;
}

static void append_current_filter_option(DBusMessageIter *options_dict, const char *name, const char *pattern)
{
    DBusMessageIter entry, variant, filter_struct, patterns_array, pattern_struct;
    const char *key = "current_filter";
    char **patterns;
    UINT pattern_count, i;
    dbus_uint32_t type = 0; /* 0 = glob pattern */

    if (!name || !pattern) return;

    /* Open dict entry: {sv} */
    p_dbus_message_iter_open_container(options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    /* Open variant containing (sa(us)) */
    p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "(sa(us))", &variant);

    /* Open filter struct: (sa(us)) */
    p_dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &filter_struct);
    p_dbus_message_iter_append_basic(&filter_struct, DBUS_TYPE_STRING, &name);

    p_dbus_message_iter_open_container(&filter_struct, DBUS_TYPE_ARRAY, "(us)", &patterns_array);
    patterns = split_pattern(pattern, &pattern_count);
    if (patterns)
    {
        for (i = 0; i < pattern_count; i++)
        {
            p_dbus_message_iter_open_container(&patterns_array, DBUS_TYPE_STRUCT, NULL, &pattern_struct);
            p_dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_UINT32, &type);
            p_dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_STRING, &patterns[i]);
            p_dbus_message_iter_close_container(&patterns_array, &pattern_struct);
            free(patterns[i]);
        }
        free(patterns);
    }

    p_dbus_message_iter_close_container(&filter_struct, &patterns_array);
    p_dbus_message_iter_close_container(&variant, &filter_struct);
    p_dbus_message_iter_close_container(&entry, &variant);
    p_dbus_message_iter_close_container(options_dict, &entry);
}

static void append_filters_option(DBusMessageIter *options_dict, const char *filters_blob,
                                  UINT filters_len, UINT filter_count, UINT current_filter_index)
{
    DBusMessageIter entry, variant, filters_array;
    const char *key = "filters";
    const char *cursor, *end;
    const char *name = NULL, *pattern = NULL;
    const char *sel_name = NULL, *sel_pattern = NULL;
    UINT i;

    if (!filters_blob || !filters_len || !filter_count) return;

    /* Open dict entry: {sv} */
    p_dbus_message_iter_open_container(options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    /* Open variant containing a(sa(us)) */
    p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
    p_dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &filters_array);

    cursor = filters_blob;
    end = filters_blob + filters_len;

    /* Add each filter pair (name, pattern) */
    for (i = 1; i <= filter_count; i++)
    {
        if (!get_next_filter_pair(&cursor, end, &name, &pattern))
            break;

        append_single_filter(&filters_array, name ? name : "", pattern ? pattern : "");

        if (current_filter_index && i == current_filter_index)
        {
            sel_name = name;
            sel_pattern = pattern;
        }
    }

    p_dbus_message_iter_close_container(&variant, &filters_array);
    p_dbus_message_iter_close_container(&entry, &variant);
    p_dbus_message_iter_close_container(options_dict, &entry);

    if (sel_name && sel_pattern)
        append_current_filter_option(options_dict, sel_name, sel_pattern);
}

/* Append boolean option to options dictionary */
static void append_boolean_option(DBusMessageIter *options_dict, const char *key, dbus_bool_t value)
{
    DBusMessageIter entry, variant;

    p_dbus_message_iter_open_container(options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    p_dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    p_dbus_message_iter_close_container(&entry, &variant);
    p_dbus_message_iter_close_container(options_dict, &entry);
}

/* Append string option to options dictionary */
static void append_string_option(DBusMessageIter *options_dict, const char *key, const char *value)
{
    DBusMessageIter entry, variant;

    if (!value) return;

    p_dbus_message_iter_open_container(options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    p_dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    p_dbus_message_iter_close_container(&entry, &variant);
    p_dbus_message_iter_close_container(options_dict, &entry);
}

/* Signal filter to capture Response from portal */
static DBusHandlerResult portal_response_filter(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    struct portal_context *ctx = user_data;
    const char *path, *interface, *member;
    DBusMessageIter args, dict, entry, variant, array;
    dbus_uint32_t response;
    const char *key;

    if (p_dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    interface = p_dbus_message_get_interface(msg);
    member = p_dbus_message_get_member(msg);
    path = p_dbus_message_get_path(msg);

    if (!interface || !member || !path)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* Check if this is a Response signal on org.freedesktop.portal.Request */
    if (strcmp(interface, "org.freedesktop.portal.Request") != 0 ||
        strcmp(member, "Response") != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    /* Check if this is our request */
    if (!ctx->request_path || strcmp(path, ctx->request_path) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    TRACE("portal_response_filter: Received Response signal for %s\n", path);

    /* Parse response: (uint32 response_code, a{sv} results) */
    if (!p_dbus_message_iter_init(msg, &args))
    {
        WARN("Failed to init message iterator\n");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    /* Get response code */
    if (p_dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32)
    {
        WARN("Expected UINT32 for response code\n");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    p_dbus_message_iter_get_basic(&args, &response);
    ctx->response_code = response;

    TRACE("portal_response_filter: Response code: %u\n", response);

    /* If success, parse results dict */
    if (response == 0 && p_dbus_message_iter_next(&args))
    {
        if (p_dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY)
        {
            WARN("Expected ARRAY for results dict\n");
            goto done;
        }

        p_dbus_message_iter_recurse(&args, &dict);

        /* Iterate over dict entries looking for result URIs */
        while (p_dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
        {
            p_dbus_message_iter_recurse(&dict, &entry);

            if (p_dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            {
                p_dbus_message_iter_next(&dict);
                continue;
            }

            p_dbus_message_iter_get_basic(&entry, &key);

            TRACE("portal_response_filter: key=%s\n", key);

            if (strcmp(key, "uris") == 0)
            {
                DBusMessageIter count_iter;
                UINT i;

                /* Get variant containing array of strings */
                if (!p_dbus_message_iter_next(&entry))
                    break;

                if (p_dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
                    break;

                p_dbus_message_iter_recurse(&entry, &variant);

                TRACE("portal_response_filter: uris variant type=%c\n",
                    p_dbus_message_iter_get_arg_type(&variant));

                if (p_dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
                {
                    int elem_type;

                    p_dbus_message_iter_recurse(&variant, &array);
                    elem_type = p_dbus_message_iter_get_arg_type(&array);

                    TRACE("portal_response_filter: uris array elem type=%c\n", elem_type);

                    if (elem_type == DBUS_TYPE_STRING || elem_type == DBUS_TYPE_OBJECT_PATH)
                    {
                        /* Count URIs first */
                        count_iter = array;
                        ctx->result_uri_count = 0;
                        while (p_dbus_message_iter_get_arg_type(&count_iter) == elem_type)
                        {
                            ctx->result_uri_count++;
                            p_dbus_message_iter_next(&count_iter);
                        }

                        TRACE("portal_response_filter: Found %u URIs\n", ctx->result_uri_count);

                        /* Allocate and copy URIs */
                        if (ctx->result_uri_count > 0)
                        {
                            ctx->result_uris = calloc(ctx->result_uri_count + 1, sizeof(char*));
                            for (i = 0; i < ctx->result_uri_count; i++)
                            {
                                const char *uri;
                                p_dbus_message_iter_get_basic(&array, &uri);
                                ctx->result_uris[i] = strdup(uri);
                                TRACE("portal_response_filter: URI[%u]: %s\n", i, uri);
                                p_dbus_message_iter_next(&array);
                            }
                        }
                        break;
                    }
                    else if (elem_type == DBUS_TYPE_BYTE)
                    {
                        const unsigned char *bytes = NULL;
                        int len = 0;

                        p_dbus_message_iter_get_fixed_array(&array, &bytes, &len);
                        if (bytes && len > 0)
                        {
                            ctx->result_uri_count = 1;
                            ctx->result_uris = calloc(2, sizeof(char*));
                            if (ctx->result_uris)
                                ctx->result_uris[0] = strndup((const char*)bytes, len);
                        }
                        break;
                    }
                }
                else if (p_dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING ||
                         p_dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_OBJECT_PATH)
                {
                    const char *uri = NULL;
                    p_dbus_message_iter_get_basic(&variant, &uri);
                    if (uri && *uri)
                    {
                        ctx->result_uri_count = 1;
                        ctx->result_uris = calloc(2, sizeof(char*));
                        if (ctx->result_uris)
                            ctx->result_uris[0] = strdup(uri);
                    }
                    break;
                }
            }
            else if (strcmp(key, "uri") == 0 || strcmp(key, "current_file") == 0)
            {
                const char *uri = NULL;

                /* Get variant containing a single string */
                if (!p_dbus_message_iter_next(&entry))
                    break;

                if (p_dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
                    break;

                p_dbus_message_iter_recurse(&entry, &variant);

                TRACE("portal_response_filter: %s variant type=%c\n",
                    key, p_dbus_message_iter_get_arg_type(&variant));

                if (p_dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
                {
                    const unsigned char *bytes = NULL;
                    int len = 0;

                    p_dbus_message_iter_recurse(&variant, &array);
                    if (p_dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_BYTE)
                    {
                        p_dbus_message_iter_get_fixed_array(&array, &bytes, &len);
                        if (bytes && len > 0)
                        {
                            ctx->result_uri_count = 1;
                            ctx->result_uris = calloc(2, sizeof(char*));
                            if (ctx->result_uris)
                                ctx->result_uris[0] = strndup((const char*)bytes, len);
                        }
                    }
                    break;
                }

                if (p_dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRING)
                    break;

                p_dbus_message_iter_get_basic(&variant, &uri);
                if (uri && *uri)
                {
                    ctx->result_uri_count = 1;
                    ctx->result_uris = calloc(2, sizeof(char*));
                    if (ctx->result_uris)
                        ctx->result_uris[0] = strdup(uri);
                }
                break;
            }
            else
            {
                /* Unknown key, skip it */
            }

            p_dbus_message_iter_next(&dict);
        }
    }
    else if (response != 0)
    {
        TRACE("portal_response_filter: response code=%u (non-success)\n", response);
    }

done:
    ctx->response_received = TRUE;
    if (!ctx->result_uris || !ctx->result_uri_count)
        TRACE("portal_response_filter: no result URIs found\n");
    return DBUS_HANDLER_RESULT_HANDLED;
}

/* Initialize portal connection */
static NTSTATUS portal_init(struct portal_context *ctx)
{
    DBusError error;

    memset(ctx, 0, sizeof(*ctx));

    TRACE("portal_init: Loading D-Bus functions...\n");
    if (!load_dbus_functions())
    {
        WARN("portal_init: Failed to load D-Bus functions\n");
        return STATUS_NOT_SUPPORTED;
    }

    TRACE("portal_init: Connecting to session bus...\n");
    p_dbus_error_init(&error);
    ctx->connection = p_dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!ctx->connection)
    {
        WARN("portal_init: Failed to connect to session bus: %s\n", error.message);
        p_dbus_error_free(&error);
        return STATUS_NOT_SUPPORTED;
    }
    p_dbus_error_free(&error);

    TRACE("Successfully connected to D-Bus session bus\n");
    return STATUS_SUCCESS;
}

/* Cleanup portal context */
static void portal_cleanup(struct portal_context *ctx)
{
    UINT i;
    char match_rule[512];
    DBusError error;

    if (ctx->request_path)
    {
        if (ctx->connection)
        {
            p_dbus_error_init(&error);
            snprintf(match_rule, sizeof(match_rule),
                     "type='signal',interface='org.freedesktop.portal.Request',path='%s'",
                     ctx->request_path);
            p_dbus_bus_remove_match(ctx->connection, match_rule, &error);
            if (p_dbus_error_is_set(&error))
            {
                WARN("portal_cleanup: bus_remove_match failed for '%s': %s (%s)\n",
                    match_rule,
                    error.message ? error.message : "(no message)",
                    error.name ? error.name : "(no name)");
                p_dbus_error_free(&error);
            }
        }

        free(ctx->request_path);
        ctx->request_path = NULL;
    }

    if (ctx->result_uris)
    {
        for (i = 0; i < ctx->result_uri_count; i++)
            free(ctx->result_uris[i]);
        free(ctx->result_uris);
        ctx->result_uris = NULL;
    }

    if (ctx->connection)
    {
        if (ctx->filter_added)
        {
            p_dbus_connection_remove_filter(ctx->connection, portal_response_filter, ctx);
            ctx->filter_added = FALSE;
        }
        p_dbus_connection_unref(ctx->connection);
        ctx->connection = NULL;
    }
}

/* Call portal method and wait for response */
static NTSTATUS portal_call_and_wait(struct portal_context *ctx,
                                      const char *method,
                                      const char *title_utf8,
                                      const char *initial_dir_utf8,
                                      const char *initial_filename_utf8,
                                      const char *current_file_unix,
                                      const char *filters_blob,
                                      UINT filters_len,
                                      UINT filter_count,
                                      UINT current_filter_index,
                                      DWORD flags)
{
    DBusMessage *request, *reply;
    DBusMessageIter args, options_dict;
    DBusError error;
    const char *parent_window = "";
    const char *request_handle;
    char match_rule[512];
    char token[64];
    NTSTATUS status = STATUS_INTERNAL_ERROR;
    static unsigned int token_seq;

    const char *title_ptr = title_utf8 ? title_utf8 : "";

	TRACE("portal_call_and_wait: %s title=%s dir=%s file=%s\n", method,
          title_ptr,
          initial_dir_utf8 ? initial_dir_utf8 : "(null)",
          initial_filename_utf8 ? initial_filename_utf8 : "(null)");

	/* Build method call */
	request = p_dbus_message_new_method_call(
		"org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.FileChooser",
		method);

	if (!request)
	{
		WARN("portal_call_and_wait: dbus_message_new_method_call failed\n");
		return STATUS_NO_MEMORY;
	}

	p_dbus_message_iter_init_append(request, &args);
	p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent_window);
	p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &title_ptr);

	/* Open Options Dictionary */
	p_dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options_dict);

    /* 1. handle_token */
    snprintf(token, sizeof(token), "wine%u_%u", (UINT)getpid(), __sync_add_and_fetch(&token_seq, 1));
    append_string_option(&options_dict, "handle_token", token);

	/* 2. Open dialog mode flags */
	if (strcmp(method, "OpenFile") == 0)
    {
		if (flags & PORTAL_OPEN_FLAG_MULTIPLE)
			append_boolean_option(&options_dict, "multiple", TRUE);
		if (flags & PORTAL_OPEN_FLAG_DIRECTORY)
			append_boolean_option(&options_dict, "directory", TRUE);
    }

	/* 3. Directory (current_folder expects 'ay' - array of bytes) */
		if (initial_dir_utf8 && initial_dir_utf8[0])
		{
			DBusMessageIter entry, variant, array;
			const char *folder_key = "current_folder";
	        int len = strlen(initial_dir_utf8) + 1;
	        const unsigned char *bytes = (const unsigned char *)initial_dir_utf8;

			p_dbus_message_iter_open_container(&options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
			p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &folder_key);
			p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
			p_dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array);
			p_dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, len);
			p_dbus_message_iter_close_container(&variant, &array);
			p_dbus_message_iter_close_container(&entry, &variant);
			p_dbus_message_iter_close_container(&options_dict, &entry);
		}

	/* 4. Initial filename (Save dialog only) */
	if (initial_filename_utf8 && strcmp(method, "SaveFile") == 0)
	{
		/* Strip path if application passed one, portal just wants the name */
		const char *name_only = strrchr(initial_filename_utf8, '/');
		if (!name_only) name_only = strrchr(initial_filename_utf8, '\\');
		name_only = name_only ? name_only + 1 : initial_filename_utf8;

		/* Skip wildcard patterns like "*.txt" */
		if (name_only[0] && !strpbrk(name_only, "*?"))
        {
			append_string_option(&options_dict, "current_name", name_only);
        }
	}

    /* 5. Current file (Save dialog only) */
	    if (current_file_unix && current_file_unix[0] && strcmp(method, "SaveFile") == 0)
	    {
	        DBusMessageIter entry, variant, array;
	        const char *file_key = "current_file";
	        int len = strlen(current_file_unix) + 1;
	        const unsigned char *bytes = (const unsigned char *)current_file_unix;

	        p_dbus_message_iter_open_container(&options_dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	        p_dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &file_key);
	        p_dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
	        p_dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array);
	        p_dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, len);
	        p_dbus_message_iter_close_container(&variant, &array);
	        p_dbus_message_iter_close_container(&entry, &variant);
	        p_dbus_message_iter_close_container(&options_dict, &entry);
	    }

	/* 6. Filters */
	if (filters_blob && filters_len && filter_count)
    {
		append_filters_option(&options_dict, filters_blob, filters_len, filter_count, current_filter_index);
    }

	p_dbus_message_iter_close_container(&args, &options_dict);

	/* Send method call */
	p_dbus_error_init(&error);
    /* Avoid a fixed timeout here: some portal backends may take longer than a few seconds
     * to create the request object. If we time out, the dialog may still appear and we'd
     * fall back to Wine's dialog, resulting in two dialogs.
     */
    reply = p_dbus_connection_send_with_reply_and_block(ctx->connection, request, -1, &error);
	p_dbus_message_unref(request);

	if (!reply)
	{
		WARN("Portal method call failed: %s (%s)\n",
             error.message ? error.message : "(no message)",
             error.name ? error.name : "(no name)");
		p_dbus_error_free(&error);
		return STATUS_NOT_SUPPORTED;
	}
	if (!p_dbus_message_get_args(reply, NULL, DBUS_TYPE_OBJECT_PATH, &request_handle, DBUS_TYPE_INVALID))
	{
        WARN("portal_call_and_wait: dbus_message_get_args failed for request_handle\n");
		p_dbus_message_unref(reply);
		return STATUS_INTERNAL_ERROR;
	}

	if (ctx->request_path) free(ctx->request_path);
	ctx->request_path = strdup(request_handle);
	p_dbus_message_unref(reply);

    /* Signal filter setup */
    p_dbus_connection_add_filter(ctx->connection, portal_response_filter, ctx, NULL);
    ctx->filter_added = TRUE;

    p_dbus_error_init(&error);
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='org.freedesktop.portal.Request',path='%s'",
             ctx->request_path);

    p_dbus_bus_add_match(ctx->connection, match_rule, &error);
	if (p_dbus_error_is_set(&error))
	{
        WARN("portal_call_and_wait: bus_add_match failed for '%s': %s (%s)\n",
            match_rule,
            error.message ? error.message : "(no message)",
            error.name ? error.name : "(no name)");
		p_dbus_error_free(&error);
		return STATUS_INTERNAL_ERROR;
	}

    /* Wait for response */
    ctx->response_received = FALSE;

    while (!ctx->response_received)
    {
        if (!p_dbus_connection_read_write_dispatch(ctx->connection, -1))
        {
            WARN("portal_call_and_wait: read_write_dispatch failed\n");
            return STATUS_INTERNAL_ERROR;
        }
    }

    TRACE("portal_call_and_wait: response_received=%d response_code=%u\n",
          ctx->response_received, ctx->response_code);
	status = (ctx->response_code == 0) ? STATUS_SUCCESS :
             (ctx->response_code == 1) ? STATUS_CANCELLED : STATUS_INTERNAL_ERROR;
    return status;
}

/* Convert file:// URI to Unix path, then to Win32 path */
static NTSTATUS uri_to_win32_path(const char *uri, WCHAR **dos_path, BOOL allow_missing)
{
    const char *unix_path;
    char *decoded_path;
    WCHAR *result = NULL;
    NTSTATUS status;
    UINT disposition = allow_missing ? FILE_OPEN_IF : FILE_OPEN;
    const char *p;
    char *q;

    TRACE("uri_to_win32_path: Converting URI: %s\n", uri);

    /* Strip file:// prefix */
    if (strncmp(uri, "file://", 7) == 0)
        unix_path = uri + 7;
    else
        unix_path = uri;

    TRACE("uri_to_win32_path: Unix path: %s\n", unix_path);

    /* URL-decode the path (handle %20 etc.) */
    decoded_path = malloc(strlen(unix_path) + 1);
    if (!decoded_path)
        return STATUS_NO_MEMORY;

    p = unix_path;
    q = decoded_path;

    while (*p)
    {
        if (*p == '%' && p[1] && p[2])
        {
            int value;
            if (sscanf(p + 1, "%2x", &value) == 1)
            {
                *q++ = (char)value;
                p += 3;
                continue;
            }
        }
        *q++ = *p++;
    }
    *q = '\0';

    TRACE("uri_to_win32_path: Decoded path: %s\n", decoded_path);

    /* Convert Unix path to DOS path using Wine's proper conversion function */
    status = ntdll_get_dos_file_name(decoded_path, &result, disposition);

    TRACE("uri_to_win32_path: ntdll_get_dos_file_name returned: 0x%08x\n", status);
    if (status == STATUS_SUCCESS && result)
    {
        TRACE("uri_to_win32_path: DOS path: %s\n", debugstr_w(result));
    }

    free(decoded_path);

    if (status != STATUS_SUCCESS)
    {
        if (allow_missing && status == STATUS_NO_SUCH_FILE && result)
        {
            TRACE("uri_to_win32_path: allowing STATUS_NO_SUCH_FILE for save, DOS path: %s\n",
                debugstr_w(result));
            *dos_path = result;
            return STATUS_SUCCESS;
        }
        if (result) free(result);
        return status;
    }

    *dos_path = result;
    return STATUS_SUCCESS;
}

static UINT split_dir_and_name(const WCHAR *path, WCHAR *dir, UINT dir_cap, const WCHAR **name_out)
{
    const WCHAR *slash;
    UINT dir_len;

    if (name_out) *name_out = path;
    if (!path || !*path) return 0;

    slash = wcsrchr(path, '\\');
    if (!slash) slash = wcsrchr(path, '/');
    if (!slash || slash == path) return 0;

    if (name_out) *name_out = slash + 1;
    dir_len = (UINT)(slash - path);
    if (dir && dir_cap)
    {
        if (dir_len >= dir_cap) dir_len = dir_cap - 1;
        memcpy(dir, path, dir_len * sizeof(WCHAR));
        dir[dir_len] = 0;
    }
    return dir_len;
}

/* Public entry point: OpenFile */
NTSTATUS CDECL portal_open_file(void *args)
{
    struct portal_open_file_params *params = args;
    struct portal_context ctx = {0};
    NTSTATUS status;
    UINT i, count, out_count;
    BOOL want_multi;

    TRACE("portal_open_file: flags=0x%08x max_results=%u\n",
        (unsigned int)params->flags, params->max_results);

    status = portal_init(&ctx);
    if (status != STATUS_SUCCESS)
        return status;

    status = portal_call_and_wait(&ctx, "OpenFile",
                                  params->title_utf8[0] ? params->title_utf8 : NULL,
                                  params->initial_dir_utf8[0] ? params->initial_dir_utf8 : NULL,
                                  NULL,
                                  NULL,
                                  params->filters_blob,
                                  params->filters_blob_len,
                                  params->filter_count,
                                  params->current_filter_index,
                                  params->flags);
    TRACE("portal_open_file: portal_call_and_wait returned 0x%08x\n", (unsigned int)status);

    params->result_count = 0;
    params->result_grouped = 0;
    params->result_buffer_len = 0;
    params->result_buffer[0] = 0;

    want_multi = (params->flags & PORTAL_OPEN_FLAG_MULTIPLE) != 0;

    if (status == STATUS_SUCCESS && ctx.result_uris && ctx.result_uri_count)
    {
        WCHAR **dos_paths = NULL;
        WCHAR dir[PORTAL_PATH_MAX];
        const WCHAR *first_name = NULL;
        UINT dir_len = 0;
        UINT required = 0;
        WCHAR *out = params->result_buffer;
        UINT out_cap = ARRAY_SIZE(params->result_buffer);
        UINT pos = 0;
        BOOL same_dir = TRUE;

        count = ctx.result_uri_count;
        if (params->max_results && params->max_results < count) count = params->max_results;
        if (!count) goto open_done;

        dos_paths = calloc(count, sizeof(*dos_paths));
        if (!dos_paths)
        {
            status = STATUS_NO_MEMORY;
            goto open_done;
        }

        out_count = 0;
        for (i = 0; i < count; i++)
        {
            if (uri_to_win32_path(ctx.result_uris[i], &dos_paths[out_count], FALSE) == STATUS_SUCCESS && dos_paths[out_count])
                out_count++;
        }
        if (!out_count) goto open_done;

        if (want_multi && out_count > 1)
        {
            dir_len = split_dir_and_name(dos_paths[0], dir, ARRAY_SIZE(dir), &first_name);
            if (!dir_len || !first_name) same_dir = FALSE;

            if (same_dir)
            {
                for (i = 1; i < out_count; i++)
                {
                    const WCHAR *name;
                    WCHAR other_dir[PORTAL_PATH_MAX];
                    UINT other_len = split_dir_and_name(dos_paths[i], other_dir, ARRAY_SIZE(other_dir), &name);
                    if (other_len != dir_len || wcscmp(other_dir, dir) || !name)
                    {
                        same_dir = FALSE;
                        break;
                    }
                }
            }
        }

        /* Compute required length in WCHARs, including final NUL(s). */
        if (!want_multi || out_count == 1)
        {
            required = (UINT)wcslen(dos_paths[0]) + 1;
            if (want_multi) required += 1; /* extra NUL termination */
        }
        else if (same_dir)
        {
            required = dir_len + 1; /* dir + NUL */
            for (i = 0; i < out_count; i++)
            {
                const WCHAR *name;
                split_dir_and_name(dos_paths[i], NULL, 0, &name);
                required += (UINT)wcslen(name) + 1;
            }
            required += 1; /* extra NUL */
        }
        else
        {
            /* Fallback: pack full paths as NUL-separated strings. */
            required = 0;
            for (i = 0; i < out_count; i++) required += (UINT)wcslen(dos_paths[i]) + 1;
            required += 1; /* extra NUL */
        }

        params->result_count = out_count;
        params->result_grouped = (want_multi && out_count > 1 && same_dir);
        params->result_buffer_len = required;

        if (required > out_cap)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            goto open_done;
        }

        /* Write packed result. */
        if (!want_multi || out_count == 1)
        {
            UINT len = (UINT)wcslen(dos_paths[0]);
            memcpy(out, dos_paths[0], len * sizeof(WCHAR));
            pos = len;
            out[pos++] = 0;
            if (want_multi) out[pos++] = 0;
        }
        else if (same_dir)
        {
            memcpy(out + pos, dir, dir_len * sizeof(WCHAR));
            pos += dir_len;
            out[pos++] = 0;
            for (i = 0; i < out_count; i++)
            {
                const WCHAR *name;
                UINT len;
                split_dir_and_name(dos_paths[i], NULL, 0, &name);
                len = (UINT)wcslen(name);
                memcpy(out + pos, name, len * sizeof(WCHAR));
                pos += len;
                out[pos++] = 0;
            }
            out[pos++] = 0;
        }
        else
        {
            for (i = 0; i < out_count; i++)
            {
                UINT len = (UINT)wcslen(dos_paths[i]);
                memcpy(out + pos, dos_paths[i], len * sizeof(WCHAR));
                pos += len;
                out[pos++] = 0;
            }
            out[pos++] = 0;
        }

        status = STATUS_SUCCESS;

open_done:
        if (dos_paths)
        {
            for (i = 0; i < count; i++) free(dos_paths[i]);
            free(dos_paths);
        }
    }

    portal_cleanup(&ctx);
    return status;
}

/* Public entry point: SaveFile */
NTSTATUS CDECL portal_save_file(void *args)
{
    struct portal_save_file_params *params = args;
    struct portal_context ctx = {0};
    NTSTATUS status;

    TRACE("portal_save_file: flags=0x%08x\n", (unsigned int)params->flags);

    status = portal_init(&ctx);
    if (status != STATUS_SUCCESS)
        return status;

    status = portal_call_and_wait(&ctx, "SaveFile",
                                  params->title_utf8[0] ? params->title_utf8 : NULL,
                                  params->initial_dir_utf8[0] ? params->initial_dir_utf8 : NULL,
                                  params->initial_filename_utf8[0] ? params->initial_filename_utf8 : NULL,
                                  params->current_file_unix[0] ? params->current_file_unix : NULL,
                                  params->filters_blob,
                                  params->filters_blob_len,
                                  params->filter_count,
                                  params->current_filter_index,
                                  params->flags);
    TRACE("portal_save_file: portal_call_and_wait returned 0x%08x\n", (unsigned int)status);

    if (status == STATUS_SUCCESS && ctx.result_uris && ctx.result_uris[0])
    {
        WCHAR *dos_path = NULL;
        if (uri_to_win32_path(ctx.result_uris[0], &dos_path, TRUE) == STATUS_SUCCESS)
        {
            UINT len = wcslen(dos_path);
            if (len < PORTAL_PATH_MAX)
            {
                memcpy(params->result_path, dos_path, (len + 1) * sizeof(WCHAR));
                params->result_path_len = len;
            }
            else
            {
                params->result_path[0] = 0;
                params->result_path_len = len;
                status = STATUS_BUFFER_TOO_SMALL;
            }
            free(dos_path);
        }
    }

    portal_cleanup(&ctx);
    return status;
}

/* Public entry point: availability probe */
NTSTATUS CDECL portal_is_available(void *args)
{
    struct portal_is_available_params *params = args;
    struct portal_context ctx = {0};
    NTSTATUS status;

    if (params) params->reserved = 0;
    status = portal_init(&ctx);
    portal_cleanup(&ctx);
    return status;
}
