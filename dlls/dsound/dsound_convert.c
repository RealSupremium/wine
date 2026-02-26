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

static float get8(const IDirectSoundBufferImpl *dsb, BYTE *buf)
{
    return (buf[0] - 0x80) / (float)0x80;
}

static float get16(const IDirectSoundBufferImpl *dsb, BYTE *buf)
{
    const SHORT *sbuf = (const SHORT*)(buf);
    SHORT sample = (SHORT)le16(*sbuf);
    return sample / (float)0x8000;
}

static float get24(const IDirectSoundBufferImpl *dsb, BYTE *buf)
{
    LONG sample;

    /* The next expression deliberately has an overflow for buf[2] >= 0x80,
       this is how negative values are made.
     */
    sample = (buf[0] << 8) | (buf[1] << 16) | (buf[2] << 24);
    return sample / (float)0x80000000U;
}

static float get32(const IDirectSoundBufferImpl *dsb, BYTE *buf)
{
    const LONG *sbuf = (const LONG*)(buf);
    LONG sample = le32(*sbuf);
    return sample / (float)0x80000000U;
}

static float getieee32(const IDirectSoundBufferImpl *dsb, BYTE *buf)
{
    const float *sbuf = (const float*)(buf);
    /* The value will be clipped later, when put into some non-float buffer */
    return *sbuf;
}

static void getsamples8(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    int i;
    for (i = 0; i < count; ++i)
        dst[i] = get8(dsb, base + i);
}

static void getsamples16(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    int i;
    for (i = 0; i < count; ++i)
        dst[i] = get16(dsb, base + i * 2);
}

static void getsamples24(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    int i;
    for (i = 0; i < count; ++i)
        dst[i] = get24(dsb, base + i * 3);
}

static void getsamples32(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    int i;
    for (i = 0; i < count; ++i)
        dst[i] = get32(dsb, base + i * 4);
}

static void getsamplesieee32(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    int i;
    for (i = 0; i < count; ++i)
        dst[i] = getieee32(dsb, base + i * 4);
}

const bitsgetfunc getbpp[5] = {getsamples8, getsamples16, getsamples24, getsamples32, getsamplesieee32};

static void getsamples8_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    int i;
    for (i = 0; i < count; ++i)
    {
        float val = 0;
        /* XXX: does Windows include LFE into the mix? */
        for (c = 0; c < channels; c++)
            val += get8(dsb, base + i * channels + c);
        val /= channels;
        dst[i] = val;
    }
}

static void getsamples16_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    int i;
    for (i = 0; i < count; ++i)
    {
        float val = 0;
        /* XXX: does Windows include LFE into the mix? */
        for (c = 0; c < channels; c++)
            val += get16(dsb, base + (i * channels + c) * 2);
        val /= channels;
        dst[i] = val;
    }
}

static void getsamples24_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    int i;
    for (i = 0; i < count; ++i)
    {
        float val = 0;
        /* XXX: does Windows include LFE into the mix? */
        for (c = 0; c < channels; c++)
            val += get24(dsb, base + (i * channels + c) * 3);
        val /= channels;
        dst[i] = val;
    }
}

static void getsamples32_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    int i;
    for (i = 0; i < count; ++i)
    {
        float val = 0;
        /* XXX: does Windows include LFE into the mix? */
        for (c = 0; c < channels; c++)
            val += get32(dsb, base + (i * channels + c) * 4);
        val /= channels;
        dst[i] = val;
    }
}

static void getsamplesieee32_mono(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD count, float *dst)
{
    DWORD channels = dsb->pwfx->nChannels;
    DWORD c;
    int i;
    for (i = 0; i < count; ++i)
    {
        float val = 0;
        /* XXX: does Windows include LFE into the mix? */
        for (c = 0; c < channels; c++)
            val += getieee32(dsb, base + (i * channels + c) * 4);
        val /= channels;
        dst[i] = val;
    }
}

const bitsgetfunc getbpp_mono[5] = {getsamples8_mono, getsamples16_mono, getsamples24_mono, getsamples32_mono, getsamplesieee32_mono};

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

static void putieee32(const IDirectSoundBufferImpl *dsb, BYTE *buf, DWORD pos, DWORD channel, float value)
{
    float *fbuf = (float*)(buf + pos + sizeof(float) * channel);
    *fbuf += value;
}

void putsamples(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    DWORD channels = dsb->mix_channels;
    DWORD c;
    int i;
    for (i = 0; i < count / channels; ++i)
        for (c = 0; c < channels; ++c)
            putieee32(dsb, buf, i * channels * sizeof(float), c, values[i] * volumes[c]);
}

void putsamples_mono(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    int i;
    for (i = 0; i < count; ++i)
        putieee32(dsb, buf, i * sizeof(float), 0, values[i] * volume0);
}

void putsamples_stereo(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    int i;
    for (i = 0; i < count / 2; ++i) {
        putieee32(dsb, buf, i * 2 * sizeof(float), 0, values[i * 2 + 0] * volume0);
        putieee32(dsb, buf, i * 2 * sizeof(float), 1, values[i * 2 + 1] * volume1);
    }
}

void putsamples_quad(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    int i;
    for (i = 0; i < count / 4; ++i) {
        putieee32(dsb, buf, i * 4 * sizeof(float), 0, values[i * 4 + 0] * volume0);
        putieee32(dsb, buf, i * 4 * sizeof(float), 1, values[i * 4 + 1] * volume1);
        putieee32(dsb, buf, i * 4 * sizeof(float), 2, values[i * 4 + 2] * volume2);
        putieee32(dsb, buf, i * 4 * sizeof(float), 3, values[i * 4 + 3] * volume3);
    }
}

void putsamples_surround51(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    float volume4 = volumes[4];
    float volume5 = volumes[5];
    int i;
    for (i = 0; i < count / 6; ++i) {
        putieee32(dsb, buf, i * 6 * sizeof(float), 0, values[i * 6 + 0] * volume0);
        putieee32(dsb, buf, i * 6 * sizeof(float), 1, values[i * 6 + 1] * volume1);
        putieee32(dsb, buf, i * 6 * sizeof(float), 2, values[i * 6 + 2] * volume2);
        putieee32(dsb, buf, i * 6 * sizeof(float), 3, values[i * 6 + 3] * volume3);
        putieee32(dsb, buf, i * 6 * sizeof(float), 4, values[i * 6 + 4] * volume4);
        putieee32(dsb, buf, i * 6 * sizeof(float), 5, values[i * 6 + 5] * volume5);
    }
}

void putsamples_surround71(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    float volume4 = volumes[4];
    float volume5 = volumes[5];
    float volume6 = volumes[6];
    float volume7 = volumes[7];
    int i;
    for (i = 0; i < count / 8; ++i) {
        putieee32(dsb, buf, i * sizeof(float), 0, values[i * 8 + 0] * volume0);
        putieee32(dsb, buf, i * sizeof(float), 1, values[i * 8 + 1] * volume1);
        putieee32(dsb, buf, i * sizeof(float), 2, values[i * 8 + 2] * volume2);
        putieee32(dsb, buf, i * sizeof(float), 3, values[i * 8 + 3] * volume3);
        putieee32(dsb, buf, i * sizeof(float), 4, values[i * 8 + 4] * volume4);
        putieee32(dsb, buf, i * sizeof(float), 5, values[i * 8 + 5] * volume5);
        putieee32(dsb, buf, i * sizeof(float), 6, values[i * 8 + 6] * volume6);
        putieee32(dsb, buf, i * sizeof(float), 7, values[i * 8 + 7] * volume7);
    }
}

void putsamples_mono2stereo(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, buf, i * 2 * sizeof(float), 0, values[i] * volume0);
        putieee32(dsb, buf, i * 2 * sizeof(float), 1, values[i] * volume1);
    }
}

void putsamples_mono2quad(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, buf, i * 4 * sizeof(float), 0, values[i] * volume0);
        putieee32(dsb, buf, i * 4 * sizeof(float), 1, values[i] * volume1);
        putieee32(dsb, buf, i * 4 * sizeof(float), 2, values[i] * volume2);
        putieee32(dsb, buf, i * 4 * sizeof(float), 3, values[i] * volume3);
    }
}

void putsamples_stereo2quad(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    int i;
    for (i = 0; i < count / 2; ++i)
    {
        putieee32(dsb, buf, i * 4 * sizeof(float), 0, values[i * 2 + 0] * volume0); /* Front left */
        putieee32(dsb, buf, i * 4 * sizeof(float), 1, values[i * 2 + 1] * volume1); /* Front right */
        putieee32(dsb, buf, i * 4 * sizeof(float), 2, values[i * 2 + 0] * volume2); /* Back left */
        putieee32(dsb, buf, i * 4 * sizeof(float), 3, values[i * 2 + 1] * volume3); /* Back right */
    }
}

void putsamples_mono2surround51(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume2 = volumes[2];
    float volume3 = volumes[3];
    float volume4 = volumes[4];
    float volume5 = volumes[5];
    int i;
    for (i = 0; i < count; ++i)
    {
        putieee32(dsb, buf, i * 6 * sizeof(float), 0, values[i] * volume0);
        putieee32(dsb, buf, i * 6 * sizeof(float), 1, values[i] * volume1);
        putieee32(dsb, buf, i * 6 * sizeof(float), 2, values[i] * volume2);
        putieee32(dsb, buf, i * 6 * sizeof(float), 3, values[i] * volume3);
        putieee32(dsb, buf, i * 6 * sizeof(float), 4, values[i] * volume4);
        putieee32(dsb, buf, i * 6 * sizeof(float), 5, values[i] * volume5);
    }
}

void putsamples_stereo2surround51(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    float volume4 = volumes[4];
    float volume5 = volumes[5];
    int i;
    for (i = 0; i < count / 2; ++i)
    {
        putieee32(dsb, buf, i * 6 * sizeof(float), 0, values[i * 2 + 0] * volume0); /* Front left */
        putieee32(dsb, buf, i * 6 * sizeof(float), 1, values[i * 2 + 1] * volume1); /* Front right */
        putieee32(dsb, buf, i * 6 * sizeof(float), 4, values[i * 2 + 0] * volume4); /* Back left */
        putieee32(dsb, buf, i * 6 * sizeof(float), 5, values[i * 2 + 1] * volume5); /* Back right */
    }
}

void putsamples_surround512stereo(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    int i;
    /* based on analyzing a recording of a dsound downmix */
    for (i = 0; i < count / 6; ++i)
    {
        float left = values[i * 6 + 0] + values[i * 6 + 2] * 0.7f + values[i * 6 + 4] * 0.24f;
        float right = values[i * 6 + 1] + values[i * 6 + 2] * 0.7f + values[i * 6 + 5] * 0.24f;
        putieee32(dsb, buf, i * 2 * sizeof(float), 0, left * volume0);
        putieee32(dsb, buf, i * 2 * sizeof(float), 1, right * volume1);
    }
}

void putsamples_surround712stereo(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    int i;
    /* based on analyzing a recording of a dsound downmix */
    for (i = 0; i < count / 8; ++i)
    {
        float left = values[i * 8 + 0] + values[i * 8 + 2] * 0.7f + values[i * 8 + 4] * 0.24f + values[i * 8 + 6] * 0.24f;
        float right = values[i * 8 + 1] + values[i * 8 + 2] * 0.7f + values[i * 8 + 5] * 0.24f + values[i * 8 + 7] * 0.24f;
        putieee32(dsb, buf, i * 2 * sizeof(float), 0, left * volume0);
        putieee32(dsb, buf, i * 2 * sizeof(float), 1, right * volume1);
    }
}

void putsamples_quad2stereo(const IDirectSoundBufferImpl *dsb, BYTE *buf, float *volumes, DWORD count, float *values)
{
    float volume0 = volumes[0];
    float volume1 = volumes[1];
    int i;
    /* based on pulseaudio's downmix algorithm */
    for (i = 0; i < count / 4; ++i)
    {
        float left = values[i * 4 + 0] * 0.9f + values[i * 4 + 2] * 0.1f;
        float right = values[i * 4 + 1] * 0.9f + values[i * 4 + 3] * 0.1f;
        putieee32(dsb, buf, i * 2 * sizeof(float), 0, left * volume0);
        putieee32(dsb, buf, i * 2 * sizeof(float), 1, right * volume1);
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
