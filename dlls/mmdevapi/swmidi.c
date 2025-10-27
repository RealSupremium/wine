/*
 * MIDI driver for software synthesizer
 *
 * Copyright 2025 Anton Baskanov
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

#include <stdarg.h>

#include "windef.h"
#include "mmddk.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(midi);

static CRITICAL_SECTION cs;
static CRITICAL_SECTION_DEBUG cs_debug = 
{
    0, 0, &cs,
    { &cs_debug.ProcessLocksList, &cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": cs") }
};
static CRITICAL_SECTION cs = { &cs_debug, -1, 0, 0, 0, 0 };

static BOOL open;
static MIDIOPENDESC open_desc;
static WORD open_flags;

static DWORD swmidi_get_dev_caps(MIDIOUTCAPSW *out_caps, DWORD_PTR size)
{
    MIDIOUTCAPSW caps = {
        MM_MICROSOFT, MM_MSFT_WDMAUDIO_MIDIOUT, 0x050a,
        L"Wine GS Wavetable SW Synth",
        MOD_SWSYNTH, 48, 48, 0xffff,
        MIDICAPS_VOLUME | MIDICAPS_LRVOLUME,
    };

    TRACE("out_caps %p, size %Iu.\n", out_caps, size);

    if (!out_caps)
        return MMSYSERR_INVALPARAM;

    memcpy(out_caps, &caps, min(size, sizeof(caps)));

    return MMSYSERR_NOERROR;
}

static DWORD swmidi_open(MIDIOPENDESC *desc, UINT flags)
{
    TRACE("desc %p, flags %x.\n", desc, flags);

    EnterCriticalSection(&cs);

    if (open)
    {
        LeaveCriticalSection(&cs);
        return MMSYSERR_ALLOCATED;
    }

    open = TRUE;
    open_desc = *desc;
    open_flags = HIWORD(flags & CALLBACK_TYPEMASK);

    LeaveCriticalSection(&cs);

    DriverCallback(open_desc.dwCallback, open_flags, (HDRVR)open_desc.hMidi, MOM_OPEN,
            open_desc.dwInstance, 0, 0);

    return MMSYSERR_NOERROR;
}

static DWORD swmidi_close(void)
{
    TRACE("\n");

    EnterCriticalSection(&cs);

    open = FALSE;

    LeaveCriticalSection(&cs);

    DriverCallback(open_desc.dwCallback, open_flags, (HDRVR)open_desc.hMidi, MOM_CLOSE,
            open_desc.dwInstance, 0, 0);

    return MMSYSERR_NOERROR;
}

DWORD swmidi_mod_message(UINT dev_id, UINT msg, DWORD_PTR user, DWORD_PTR param1, DWORD_PTR param2)
{
    TRACE("dev_id %u, msg %x, user %#Ix, param1 %#Ix, param2 %#Ix.\n", dev_id, msg, user, param1,
            param2);

    switch (msg)
    {
    case DRVM_INIT:
    case DRVM_EXIT:
        return MMSYSERR_NOERROR;
    case MODM_GETNUMDEVS:
        return 1;
    case MODM_GETDEVCAPS:
        return swmidi_get_dev_caps((MIDIOUTCAPSW *)param1, param2);
    case MODM_OPEN:
        return swmidi_open((MIDIOPENDESC *)param1, param2);
    case MODM_CLOSE:
        return swmidi_close();
    }

    return MMSYSERR_NOTSUPPORTED;
}
