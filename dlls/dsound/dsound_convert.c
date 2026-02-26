/* DirectSound format conversion and mixing routines
 *
 * Copyright 2007 Maarten Lankhorst
 * Copyright 2011 Owen Rudge for CodeWeavers
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

/* 8 bits is unsigned, the rest is signed.
 * First I tried to reuse existing stuff from alsa-lib, after that
 * didn't work, I gave up and just went for individual hacks.
 *
 * 24 bit is expensive to do, due to unaligned access.
 * In dlls/winex11.drv/dib_convert.c convert_888_to_0888_asis there is a way
 * around it, but I'm happy current code works, maybe something for later.
 *
 * The ^ 0x80 flips the signed bit, this is the conversion from
 * signed (-128.. 0.. 127) to unsigned (0...255)
 * This is only temporary: All 8 bit data should be converted to signed.
 * then when fed to the sound card, it should be converted to unsigned again.
 *
 * Sound is LITTLE endian
 */


#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "mmsystem.h"
#include "wine/debug.h"
#include "dsound.h"
#include "dsound_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsound);

#ifdef WORDS_BIGENDIAN
#define le16(x) RtlUshortByteSwap((x))
#define le32(x) RtlUlongByteSwap((x))
#else
#define le16(x) (x)
#define le32(x) (x)
#endif

static float get8(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    const BYTE *buf = base + channel;
    return (buf[0] - 0x80) / (float)0x80;
}

static float get16(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    const BYTE *buf = base + 2 * channel;
    const SHORT *sbuf = (const SHORT*)(buf);
    SHORT sample = (SHORT)le16(*sbuf);
    return sample / (float)0x8000;
}

static float get24(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    LONG sample;
    const BYTE *buf = base + 3 * channel;

    /* The next expression deliberately has an overflow for buf[2] >= 0x80,
       this is how negative values are made.
     */
    sample = (buf[0] << 8) | (buf[1] << 16) | (buf[2] << 24);
    return sample / (float)0x80000000U;
}

static float get32(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    const BYTE *buf = base + 4 * channel;
    const LONG *sbuf = (const LONG*)(buf);
    LONG sample = le32(*sbuf);
    return sample / (float)0x80000000U;
}

static float getieee32(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    const BYTE *buf = base + 4 * channel;
    const float *sbuf = (const float*)(buf);
    /* The value will be clipped later, when put into some non-float buffer */
    return *sbuf;
}

const bitsgetfunc getbpp[5] = {get8, get16, get24, get32, getieee32};

static float get8_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    float val = 0;
    /* XXX: does Windows include LFE into the mix? */
    for (c = 0; c < channels; c++)
        val += get8(dsb, base, c);
    val /= channels;
    return val;
}

static float get16_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    float val = 0;
    /* XXX: does Windows include LFE into the mix? */
    for (c = 0; c < channels; c++)
        val += get16(dsb, base, c);
    val /= channels;
    return val;
}

static float get24_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    float val = 0;
    /* XXX: does Windows include LFE into the mix? */
    for (c = 0; c < channels; c++)
        val += get24(dsb, base, c);
    val /= channels;
    return val;
}

static float get32_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    float val = 0;
    /* XXX: does Windows include LFE into the mix? */
    for (c = 0; c < channels; c++)
        val += get32(dsb, base, c);
    val /= channels;
    return val;
}

static float getieee32_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    float val = 0;
    /* XXX: does Windows include LFE into the mix? */
    for (c = 0; c < channels; c++)
        val += getieee32(dsb, base, c);
    val /= channels;
    return val;
}

const bitsgetfunc getbpp_mono[5] = {get8_mono, get16_mono, get24_mono, get32_mono, getieee32_mono};

static inline unsigned char f_to_8(float value)
{
    if(value <= -1.f)
        return 0;
    if(value >= 1.f * 0x7f / 0x80)
        return 0xFF;
    return lrintf((value + 1.f) * 0x80);
}

static inline SHORT f_to_16(float value)
{
    if(value <= -1.f)
        return 0x8000;
    if(value >= 1.f * 0x7FFF / 0x8000)
        return 0x7FFF;
    return le16(lrintf(value * 0x8000));
}

static LONG f_to_24(float value)
{
    if(value <= -1.f)
        return 0x80000000;
    if(value >= 1.f * 0x7FFFFF / 0x800000)
        return 0x7FFFFF00;
    return lrintf(value * 0x80000000U);
}

static inline LONG f_to_32(float value)
{
    if(value <= -1.f)
        return 0x80000000;
    if(value >= 1.f)
        return 0x7FFFFFFF;
    return le32(lrintf(value * 0x80000000U));
}

static void putieee32(const IDirectSoundBufferImpl *dsb, DWORD pos, DWORD channel, float value)
{
    BYTE *buf = (BYTE *)dsb->device->tmp_buffer;
    float *fbuf = (float*)(buf + pos + sizeof(float) * channel);
    *fbuf = value;
}

static void putieee32_sum(const IDirectSoundBufferImpl *dsb, DWORD pos, DWORD channel, float value)
{
    BYTE *buf = (BYTE *)dsb->device->tmp_buffer;
    float *fbuf = (float*)(buf + pos + sizeof(float) * channel);
    *fbuf += value;
}

void putsamples(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    DWORD channels = dsb->device->pwfx->nChannels;
    int i;
    for (i = 0; i < count; ++i)
        putieee32(dsb, i * channels * sizeof(float), channel, values[i]);
}

void putsamples_mono2stereo(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, i * 2 * sizeof(float), 0, values[i]);
        putieee32(dsb, i * 2 * sizeof(float), 1, values[i]);
    }
}

void putsamples_mono2quad(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, i * 4 * sizeof(float), 0, values[i]);
        putieee32(dsb, i * 4 * sizeof(float), 1, values[i]);
        putieee32(dsb, i * 4 * sizeof(float), 2, values[i]);
        putieee32(dsb, i * 4 * sizeof(float), 3, values[i]);
    }
}

void putsamples_stereo2quad(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    if (channel == 0) { /* Left */
        for (i = 0; i < count; ++i)
        {
            putieee32(dsb, i * 2 * sizeof(float), 0, values[i]); /* Front left */
            putieee32(dsb, i * 2 * sizeof(float), 2, values[i]); /* Back left */
        }
    } else if (channel == 1) { /* Right */
        for (i = 0; i < count; ++i)
        {
            putieee32(dsb, i * 2 * sizeof(float), 1, values[i]); /* Front right */
            putieee32(dsb, i * 2 * sizeof(float), 3, values[i]); /* Back right */
        }
    }
}

void putsamples_mono2surround51(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, i * 6 * sizeof(float), 0, values[i]);
        putieee32(dsb, i * 6 * sizeof(float), 1, values[i]);
        putieee32(dsb, i * 6 * sizeof(float), 2, values[i]);
        putieee32(dsb, i * 6 * sizeof(float), 3, values[i]);
        putieee32(dsb, i * 6 * sizeof(float), 4, values[i]);
        putieee32(dsb, i * 6 * sizeof(float), 5, values[i]);
    }
}

void putsamples_stereo2surround51(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    if (channel == 0) { /* Left */
        for (i = 0; i < count; ++i)
        {
            putieee32(dsb, i * 6 * sizeof(float), 0, values[i]); /* Front left */
            putieee32(dsb, i * 6 * sizeof(float), 4, values[i]); /* Back left */

            putieee32(dsb, i * 6 * sizeof(float), 2, 0.0f); /* Mute front centre */
            putieee32(dsb, i * 6 * sizeof(float), 3, 0.0f); /* Mute LFE */
        }
    } else if (channel == 1) { /* Right */
        for (i = 0; i < count; ++i)
        {
            putieee32(dsb, i * 6 * sizeof(float), 1, values[i]); /* Front right */
            putieee32(dsb, i * 6 * sizeof(float), 5, values[i]); /* Back right */
        }
    }
}

void putsamples_surround512stereo(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    /* based on analyzing a recording of a dsound downmix */
    switch(channel){

    case 4: /* surround left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 0: /* front left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 1.0f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 5: /* surround right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 1: /* front right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 1.0f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 2: /* centre */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.7;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 3:
        /* LFE is totally ignored in dsound when downmixing to 2 channels */
        break;
    }
}

void putsamples_surround712stereo(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    /* based on analyzing a recording of a dsound downmix */
    switch(channel){

    case 6: /* back left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 4: /* surround left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 0: /* front left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 1.0f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 7: /* back right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 5: /* surround right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.24f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 1: /* front right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 1.0f;
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 2: /* centre */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.7;
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 3:
        /* LFE is totally ignored in dsound when downmixing to 2 channels */
        break;
    }
}

void putsamples_quad2stereo(const IDirectSoundBufferImpl *dsb, DWORD channel, DWORD count, float *values)
{
    int i;
    /* based on pulseaudio's downmix algorithm */
    switch(channel){

    case 2: /* back left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.1f; /* (1/9) / (sum of left volumes) */
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 0: /* front left */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.9f; /* 1 / (sum of left volumes) */
            putieee32_sum(dsb, i * 2 * sizeof(float), 0, value);
        }
        break;

    case 3: /* back right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.1f; /* (1/9) / (sum of right volumes) */
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;

    case 1: /* front right */
        for (i = 0; i < count; ++i)
        {
            float value = values[i] * 0.9f; /* 1 / (sum of right volumes) */
            putieee32_sum(dsb, i * 2 * sizeof(float), 1, value);
        }
        break;
    }
}

void mixieee32(float *src, float *dst, unsigned samples)
{
    TRACE("%p - %p %d\n", src, dst, samples);
    while (samples--)
        *(dst++) += *(src++);
}

static void norm8(float *src, unsigned char *dst, unsigned samples)
{
    TRACE("%p - %p %d\n", src, dst, samples);
    while (samples--)
    {
        *dst = f_to_8(*src);
        ++dst;
        ++src;
    }
}

static void norm16(float *src, SHORT *dst, unsigned samples)
{
    TRACE("%p - %p %d\n", src, dst, samples);
    while (samples--)
    {
        *dst = f_to_16(*src);
        ++dst;
        ++src;
    }
}

static void norm24(float *src, BYTE *dst, unsigned samples)
{
    TRACE("%p - %p %d\n", src, dst, samples);
    while (samples--)
    {
        LONG t = f_to_24(*src);
        dst[0] = (t >> 8) & 0xFF;
        dst[1] = (t >> 16) & 0xFF;
        dst[2] = t >> 24;
        dst += 3;
        ++src;
    }
}

static void norm32(float *src, INT *dst, unsigned samples)
{
    TRACE("%p - %p %d\n", src, dst, samples);
    while (samples--)
    {
        *dst = f_to_32(*src);
        ++dst;
        ++src;
    }
}

const normfunc normfunctions[4] = {
    (normfunc)norm8,
    (normfunc)norm16,
    (normfunc)norm24,
    (normfunc)norm32,
};
