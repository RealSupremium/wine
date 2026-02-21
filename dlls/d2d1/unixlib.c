/*
 * Copyright 2025 Jakub Woźniak
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* This file is compiled as NATIVE LINUX CODE (not Windows PE code).
 * The #pragma makedep unix tells the Wine build system to compile
 * this file without -mabi=ms and other Windows ABI flags.
 * This means dlopen/dlsym work correctly here. */
#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <stdarg.h>
#include <dlfcn.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

/* Opaque librsvg types - we don't need the real headers */
typedef void RsvgHandle;
typedef void GError;

/* librsvg function pointer types */
typedef RsvgHandle *(*rsvg_handle_new_from_data_t)(const unsigned char *data,
        size_t data_len, GError **error);
typedef void (*g_object_unref_t)(void *object);
typedef void (*g_error_free_t)(GError *error);
typedef void (*rsvg_handle_render_document_t)(RsvgHandle*, void*, void*, void*);
typedef void*(*cairo_image_surface_create_for_data_t)(unsigned char*, int, int, int, int);
typedef void*(*cairo_create_t)(void*);
typedef void (*cairo_destroy_t)(void*);
typedef void (*cairo_surface_destroy_t)(void*);
typedef void (*cairo_scale_t)(void*, double, double);
typedef struct{
    double x;
    double y;
    double width;
    double height;
} RsvgRectangle;
/* Loaded library and function pointers */
static void *libcairo = NULL;
static cairo_image_surface_create_for_data_t p_cairo_image_surface_create_for_data;
static cairo_create_t p_cairo_create;
static cairo_destroy_t p_cairo_destroy;
static cairo_surface_destroy_t p_cairo_surface_destroy;
static cairo_scale_t p_cairo_scale;

static void *librsvg_so = NULL;
static rsvg_handle_new_from_data_t p_rsvg_handle_new_from_data = NULL;
static rsvg_handle_render_document_t p_rsvg_handle_render_document;
static g_object_unref_t            p_g_object_unref            = NULL;
static g_error_free_t              p_g_error_free              = NULL;

static BOOL load_librsvg(void)
{
    if (librsvg_so) return TRUE;

    /* dlopen works here because this file is compiled as native Linux code */
    librsvg_so = dlopen("librsvg-2.so.2", RTLD_NOW);
    if (!librsvg_so)
    {
        ERR("Failed to load librsvg-2.so.2: %s\n", dlerror());
        return FALSE;
    }

    p_rsvg_handle_new_from_data = dlsym(librsvg_so, "rsvg_handle_new_from_data");
    p_g_object_unref            = dlsym(librsvg_so, "g_object_unref");
    p_g_error_free              = dlsym(librsvg_so, "g_error_free");
    p_rsvg_handle_render_document = dlsym(librsvg_so, "rsvg_handle_render_document");

    if (!p_rsvg_handle_new_from_data || !p_g_object_unref || !p_g_error_free || !p_rsvg_handle_render_document)
    {
        ERR("Failed to find required librsvg symbols\n");
        dlclose(librsvg_so);
        librsvg_so = NULL;
        return FALSE;
    }

    TRACE("librsvg loaded successfully!\n");
    return TRUE;
}

static BOOL load_cairo(void){

    if(libcairo) return TRUE;

    libcairo = dlopen("libcairo.so.2", RTLD_NOW);

    if(!libcairo)
    {
        ERR("Failed to load libcairo: %s\n", dlerror());
        return FALSE;
    }

    p_cairo_image_surface_create_for_data = dlsym(libcairo, "cairo_image_surface_create_for_data");
    p_cairo_create = dlsym(libcairo, "cairo_create");
    p_cairo_destroy = dlsym(libcairo, "cairo_destroy");
    p_cairo_surface_destroy = dlsym(libcairo, "cairo_surface_destroy");
    p_cairo_scale = dlsym(libcairo, "cairo_scale");

    if(!p_cairo_image_surface_create_for_data || !p_cairo_create)
    {
        ERR("Failed to find Cairo symbols\n");
        dlclose(libcairo);
        libcairo = NULL;
        return FALSE;
    }

    TRACE("Cairo loaded successfully!\n");
    return TRUE;
}

/* Called from PE side via UNIX_CALL(rsvg_create_handle, &params) */
static NTSTATUS rsvg_create_handle(void *args)
{
    struct rsvg_create_params *params = args;
    void *error = NULL;

    params->handle = NULL;

    if (!load_librsvg())
        return STATUS_NOT_SUPPORTED;

    params->handle = p_rsvg_handle_new_from_data(params->data, params->size, &error);

    if (!params->handle)
    {
        ERR("librsvg failed to parse SVG data\n");
        if (error) p_g_error_free(error);
        return STATUS_UNSUCCESSFUL;
    }

    TRACE("Created rsvg handle %p\n", params->handle);
    return STATUS_SUCCESS;
}

/* Called from PE side via UNIX_CALL(rsvg_free_handle, &params) */
static NTSTATUS rsvg_free_handle(void *args)
{
    struct rsvg_free_params *params = args;

    if (params->handle && p_g_object_unref)
        p_g_object_unref(params->handle);

    return STATUS_SUCCESS;
}

static NTSTATUS rsvg_render(void *args){
    struct rsvg_render_params *params = args;
    void *surface = NULL, *cr = NULL;
    RsvgRectangle viewport;
    double scale_x, scale_y;
    NTSTATUS ret = STATUS_SUCCESS;

    TRACE("rsvg_render called: handle=%p, pixels:%p, %ux%u stride=%u\n",
          params->handle, params->pixels, params->width, params->height, params->stride);

    if(!params->handle || !params->pixels)
    {
        ERR("Invalid parameters: handle:%p pixels:%p\n", params->handle, params->pixels);
        ret = STATUS_INVALID_PARAMETER;
        goto done;
    }

    if(params->svg_width <= 0.01 && params->svg_height <= 0.01)
    {
        ERR("Invalid viewport: %fx%f\n", params->svg_width, params->svg_height);
        ret = STATUS_INVALID_PARAMETER;
        goto done;
    }

    if(params->width == 0 || params->height == 0 || params->stride == 0)
    {
        ERR("Invalid dimensions: %ux%u stride=%u\n",params->width, params->height, params->stride);
        ret = STATUS_INVALID_PARAMETER;
        goto done;
    }

    if(!load_librsvg() || !load_cairo())
    {
        ret = STATUS_NOT_SUPPORTED;
        goto done;
    }

    if(!p_rsvg_handle_render_document)
    {
        ERR("rsvg_handle_render_document symbol not found\n");
        ret = STATUS_NOT_SUPPORTED;
        goto done;
    }

    TRACE("Creating cairo surface...\n");

    /* Create Cairo surface wrapping D2D bitmap pixels in CAIRO_FORMAT_ARGB32 */
    surface = p_cairo_image_surface_create_for_data(
        params->pixels,
        0,  /* CAIRO_FORMAT_ARGB32 */
        params->width,
        params->height,
        params->stride
    );

    if(!surface)
    {
        ERR("Failed to create Cairo surface\n");
        ret = STATUS_UNSUCCESSFUL;
        goto done;
    }

    TRACE("Creating cairo context...\n");

    cr = p_cairo_create(surface);
    if(!cr){
        p_cairo_surface_destroy(surface);
        ret = STATUS_UNSUCCESSFUL;
        goto done;
    }

    /* explicit double cast for precision*/
    scale_x = (double)params->width / params->svg_width;
    scale_y = (double)params->height / params->svg_height;

    if(scale_x <= 0.0 || scale_x > 1000.0 || scale_y <= 0.0 || scale_y > 1000.0)
    {
        ERR("Invalid scale values: %fx%f\n",scale_x,scale_y);
        ret = STATUS_INVALID_PARAMETER;
        goto done;
    }

    TRACE("Scaling and rendering...\n");

    p_cairo_scale(cr, scale_x, scale_y);


    TRACE("Preparing Rsvg viewport...\n");

    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (double)params->svg_width;
    viewport.height = (double)params->svg_height;


    TRACE("Calling rsvg_handle_render_document...\n");

    p_rsvg_handle_render_document(params->handle, cr, &viewport, NULL);

done:
    TRACE("Cleaning up...\n");

    if(cr) p_cairo_destroy(cr);
    if(surface) p_cairo_surface_destroy(surface);

    /* Reset x87 FPU state after Cairo rendering. Cairo's fsin leaves the PE
     * (Precision Exception) flag set, which causes SIGFPE when inherited by
     * new threads running unmasked exceptions (CW=0x0040).*/
    __asm__ volatile ("fninit");
    return ret;
}

/* This table MUST match the order of enum d2d1_unix_funcs in unixlib.h */
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    rsvg_create_handle,  /* unix_rsvg_create_handle */
    rsvg_free_handle,    /* unix_rsvg_free_handle   */
    rsvg_render,         /* unix_rsvg_render        */
};