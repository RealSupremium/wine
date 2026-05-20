/*
 * Unix library entry point for comdlg32
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"

#include "unixlib.h"

/* Forward declarations from portal_dbus.c */
NTSTATUS CDECL portal_open_file(void *args);
NTSTATUS CDECL portal_save_file(void *args);
NTSTATUS CDECL portal_is_available(void *args);

/******************************************************************************
 * Unix library entry points - function table approach
 */

static NTSTATUS CDECL unix_portal_open_file_wrapper(void *args)
{
    return portal_open_file(args);
}

static NTSTATUS CDECL unix_portal_save_file_wrapper(void *args)
{
    return portal_save_file(args);
}

static NTSTATUS CDECL unix_portal_is_available_wrapper(void *args)
{
    return portal_is_available(args);
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    unix_portal_open_file_wrapper,
    unix_portal_save_file_wrapper,
    unix_portal_is_available_wrapper,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_portal_is_available + 1 );
