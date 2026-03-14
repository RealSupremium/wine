/*
 * Copyright 2026 Rémi Bernon for CodeWeavers
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

#include <stddef.h>
#include <stdarg.h>

#include <poll.h>

#include "unix_private.h"

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>

void sched_run(void)
{
/*
    CFRunLoopSourceRef source = CFRunLoopSourceCreate( NULL, 0, NULL );
    CFRunLoopAddSource( CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes );
    CFRelease( source );
*/

    CFRunLoopRun(); /* Should never return, except on error. */
    assert( 0 );
}

#else

void sched_run(void)
{
    for (;;) poll( NULL, 0, -1 );
}

#endif
