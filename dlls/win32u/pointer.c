/*
 * USER Pointer Message processing
 *
 * Copyright 2025 Hecheng Yu
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "win32u_private.h"
#include <pthread.h>

WINE_DEFAULT_DEBUG_CHANNEL(pointer);

#define MAX_ACTIVE_POINTERS 10

struct pointer_info_entry
{
    BOOL active;
    UINT32 pointerId;
    POINTER_INPUT_TYPE pointerType;
    HWND hwndTarget;
    POINT ptPixelLocation;
    DWORD pointerFlags;
    DWORD dwTime;
    DWORD releaseTime;
    UINT message;
};

static struct pointer_info_entry pointer_cache[MAX_ACTIVE_POINTERS];
static pthread_mutex_t pointer_cache_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Find a cache entry by pointer ID */
static struct pointer_info_entry *find_pointer_entry( UINT32 id )
{
    int i;
    for (i = 0; i < MAX_ACTIVE_POINTERS; i++)
    {
        if (pointer_cache[i].active && pointer_cache[i].pointerId == id)
            return &pointer_cache[i];
    }
    return NULL;
}

/* Find an empty cache slot */
static struct pointer_info_entry *get_free_pointer_entry( void )
{
    int i;
    for (i = 0; i < MAX_ACTIVE_POINTERS; i++)
    {
        if (!pointer_cache[i].active)
            return &pointer_cache[i];
    }
    WARN( "Pointer cache full!\n" );
    return NULL;
}

/* Store pointer information from hardware input */
void store_pointer_info( HWND hwnd, const INPUT *input, LPARAM lparam )
{
    struct pointer_info_entry *entry;
    UINT32 id;
    DWORD flags;
    DWORD msg;
    POINT pos;

    if (input->type != INPUT_HARDWARE) return;

    msg = input->hi.uMsg;

    /* Only handle WM_POINTER* messages */
    if (msg != WM_POINTERDOWN &&
        msg != WM_POINTERUPDATE &&
        msg != WM_POINTERUP)
        return;

    id = input->hi.wParamL;

    /* Reconstruct full 32-bit flags: add high bits based on message type */
    flags = input->hi.wParamH;
    if (msg == WM_POINTERDOWN)
        flags |= POINTER_FLAG_DOWN;
    else if (msg == WM_POINTERUPDATE)
        flags |= POINTER_FLAG_UPDATE;
    else if (msg == WM_POINTERUP)
        flags |= POINTER_FLAG_UP;

    /* lparam contains normalized position (0-65535) */
    pos.x = LOWORD(lparam);
    pos.y = HIWORD(lparam);

    TRACE("Storing pointer %u, msg=%u, flags=%u, pos=(%d,%d)\n",
          id, msg, flags, pos.x, pos.y);

    pthread_mutex_lock( &pointer_cache_mutex );

    entry = find_pointer_entry( id );

    if (msg == WM_POINTERDOWN)
    {
        /* New touch - allocate entry */
        if (!entry) entry = get_free_pointer_entry();
        if (entry)
        {
            entry->active = TRUE;
            entry->pointerId = id;
            entry->pointerType = PT_TOUCH; // Since currently only XI_TOUCH* triggers WM_POINTER*, it's okay to use hardcoded PT_TOUCH here.
            entry->hwndTarget = hwnd;
        }
    }

    if (entry)
    {
        entry->message = msg;
        entry->pointerFlags = flags;
        entry->ptPixelLocation.x = pos.x;
        entry->ptPixelLocation.y = pos.y;
        entry->dwTime = NtGetTickCount();

        if (msg == WM_POINTERUP)
        {
            entry->releaseTime = NtGetTickCount();
        }
    }

    pthread_mutex_unlock( &pointer_cache_mutex );
}

BOOL get_pointer_type( UINT32 id, POINTER_INPUT_TYPE *type )
{
    struct pointer_info_entry *entry;
    BOOL ret = FALSE;

    if (id == 1)
    {
        *type = PT_MOUSE;
        TRACE( "Pointer 1 is mouse\n" );
        return TRUE;
    }

    pthread_mutex_lock( &pointer_cache_mutex );

    entry = find_pointer_entry( id );
    if (entry && entry->active)
    {
        *type = entry->pointerType;
        TRACE( "Pointer %u type: %d\n", id, *type );
        ret = TRUE;
    }
    else
    {
        WARN( "Pointer %u not found\n", id );
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
    }

    pthread_mutex_unlock( &pointer_cache_mutex );
    return ret;
}

/**********************************************************************
 * NtUserGetPointerType (win32u.@)
 */
BOOL WINAPI NtUserGetPointerType( UINT32 id, POINTER_INPUT_TYPE *type )
{
    TRACE( "id %u, type %p\n", id, type );
    if (!type)
    {
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    return get_pointer_type( id, type );
}
