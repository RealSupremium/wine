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
#include "mmdeviceapi.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(midi);

struct swmidi
{
    CRITICAL_SECTION cs;

    BOOL open;
    MIDIOPENDESC open_desc;
    WORD flags;
};

static struct swmidi swmidi;

static DWORD swmidi_init(void)
{
    TRACE("\n");

    InitializeCriticalSection(&swmidi.cs);

    return MMSYSERR_NOERROR;
}

static DWORD swmidi_exit(void)
{
    TRACE("\n");

    DeleteCriticalSection(&swmidi.cs);

    return MMSYSERR_NOERROR;
}

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

    EnterCriticalSection(&swmidi.cs);

    if (swmidi.open)
    {
        LeaveCriticalSection(&swmidi.cs);
        return MMSYSERR_ALLOCATED;
    }

    swmidi.open = TRUE;
    swmidi.open_desc = *desc;
    swmidi.flags = HIWORD(flags & CALLBACK_TYPEMASK);

    LeaveCriticalSection(&swmidi.cs);

    DriverCallback(swmidi.open_desc.dwCallback, swmidi.flags, (HDRVR)swmidi.open_desc.hMidi,
            MOM_OPEN, swmidi.open_desc.dwInstance, 0, 0);

    return MMSYSERR_NOERROR;
}

static DWORD swmidi_close(void)
{
    TRACE("\n");

    EnterCriticalSection(&swmidi.cs);

    swmidi.open = FALSE;

    LeaveCriticalSection(&swmidi.cs);

    DriverCallback(swmidi.open_desc.dwCallback, swmidi.flags, (HDRVR)swmidi.open_desc.hMidi,
            MOM_CLOSE, swmidi.open_desc.dwInstance, 0, 0);

    return MMSYSERR_NOERROR;
}

DWORD WINAPI swmidi_modMessage(UINT dev_id, UINT msg, DWORD_PTR user, DWORD_PTR param1,
        DWORD_PTR param2)
{
    TRACE("dev_id %u, msg %x, user %#Ix, param1 %#Ix, param2 %#Ix.\n", dev_id, msg, user, param1,
            param2);

    switch (msg)
    {
    case DRVM_INIT:
        return swmidi_init();
    case DRVM_EXIT:
        return swmidi_exit();
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

LRESULT CALLBACK swmidi_DriverProc(DWORD_PTR dev_id, HDRVR drvr, UINT msg, LPARAM param1,
        LPARAM param2)
{
    TRACE("dev_id %Iu, drvr %p, msg %x, param1 %#Ix, param2 %#Ix.\n", dev_id, drvr, msg, param1,
            param2);

    switch(msg)
    {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_OPEN:
    case DRV_CLOSE:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_QUERYCONFIGURE:
        return DRV_SUCCESS;
    case DRV_CONFIGURE:
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_OK;
    }

    return DRV_FAILURE;
}
