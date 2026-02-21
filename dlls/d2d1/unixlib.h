/*
 * Copyright 2025 Jakub Woźniak
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __D2D1_UNIXLIB_H
#define __D2D1_UNIXLIB_H

#include "wine/unixlib.h"

/* Commands that the PE side can call into the Unix side */
enum d2d1_unix_funcs
{
    unix_rsvg_create_handle,
    unix_rsvg_free_handle,
    unix_rsvg_render,
};

/* Parameters for creating an SVG handle from raw data */
struct rsvg_create_params
{
    const unsigned char *data;  /* in:  SVG XML data */
    ULONG               size;   /* in:  size of data */
    void               *handle; /* out: RsvgHandle* on success */
};

/* Parameters for freeing an SVG handle */
struct rsvg_free_params
{
    void *handle; /* in: RsvgHandle* to free */
};

struct rsvg_render_params
{
    void *handle;       // in: RsvgHandle*
    void *pixels;       // in: pointer to bitmap pixels
    double svg_width;   // in: viewport width
    double svg_height;  // in: viewport height
    UINT32 width;       // in: bitmap width
    UINT32 height;      // in: bitmap height
    UINT32 stride;      // in: bytes per row
    UINT32 padding;
};
#endif /* __D2D1_UNIXLIB_H */