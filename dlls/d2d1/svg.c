/*
 * Copyright 2025 Jakub Woźniak
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* This is the PE (Windows) side of the SVG implementation.
 * It does NOT call dlopen directly - instead it calls into
 * unixlib.c via the Wine unix call mechanism. */

#include "d2d1_private.h"
#include "unixlib.h"

/* Shorthand macro for calling into our Unix lib */
#define UNIX_CALL(func, params) WINE_UNIX_CALL(unix_ ## func, params)

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

/* Heap helpers */
static inline void *heap_alloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline void *heap_alloc_zero(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static inline BOOL heap_free(void *ptr)
{
    return HeapFree(GetProcessHeap(), 0, ptr);
}

/* COM interface implementation */

static HRESULT STDMETHODCALLTYPE d2d_svg_document_QueryInterface(ID2D1Resource *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1Resource_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_svg_document_AddRef(ID2D1Resource *iface)
{
    struct d2d_svg_document *document = impl_from_ID2D1Resource(iface);
    ULONG refcount = InterlockedIncrement(&document->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_svg_document_Release(ID2D1Resource *iface)
{
    struct d2d_svg_document *document = impl_from_ID2D1Resource(iface);
    ULONG refcount = InterlockedDecrement(&document->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        /* Free the librsvg handle via the unix lib */
        if (document->rsvg_handle && __wine_unixlib_handle)
        {
            struct rsvg_free_params params;
            params.handle = document->rsvg_handle;
            UNIX_CALL(rsvg_free_handle, &params);
        }
        if (document->factory)
            ID2D1Factory_Release(document->factory);
        heap_free(document);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_svg_document_GetFactory(ID2D1Resource *iface,
        ID2D1Factory **factory)
{
    struct d2d_svg_document *document = impl_from_ID2D1Resource(iface);
    TRACE("iface %p, factory %p.\n", iface, factory);
    *factory = document->factory;
    ID2D1Factory_AddRef(*factory);
}

static const ID2D1ResourceVtbl d2d_svg_document_vtbl =
{
    d2d_svg_document_QueryInterface,
    d2d_svg_document_AddRef,
    d2d_svg_document_Release,
    d2d_svg_document_GetFactory,
};

HRESULT d2d_svg_document_create(struct d2d_device_context *context, IStream *stream,
        D2D1_SIZE_F viewport_size, ID2D1SvgDocument **document)
{
    struct rsvg_create_params params;
    struct d2d_svg_document *object;
    STATSTG stat;
    void *buffer;
    ULONG read_len;
    NTSTATUS status;

    TRACE("context %p, stream %p, viewport_size {%.8e, %.8e}, document %p.\n",
            context, stream, viewport_size.width, viewport_size.height, document);

    /* Check unix lib is available */
    if (!__wine_unixlib_handle)
    {
        WARN("Unix lib not available, SVG not supported\n");
        return E_NOTIMPL;
    }

    /* Read the SVG stream into a buffer */
    if (FAILED(IStream_Stat(stream, &stat, STATFLAG_NONAME)))
    {
        ERR("Failed to stat stream\n");
        return E_FAIL;
    }

    buffer = heap_alloc(stat.cbSize.QuadPart);
    if (!buffer)
        return E_OUTOFMEMORY;

    if (FAILED(IStream_Read(stream, buffer, stat.cbSize.QuadPart, &read_len)))
    {
        ERR("Failed to read stream\n");
        heap_free(buffer);
        return E_FAIL;
    }

    /* Ask the Unix side to parse the SVG with librsvg */
    params.data   = buffer;
    params.size   = read_len;
    params.handle = NULL;

    status = UNIX_CALL(rsvg_create_handle, &params);
    heap_free(buffer);

    if (status)
    {
        ERR("Unix lib failed to create rsvg handle: %08lx\n", status);
        return E_FAIL;
    }

    /* Allocate our COM object */
    if (!(object = heap_alloc_zero(sizeof(*object))))
    {
        /* Free the rsvg handle we just created */
        struct rsvg_free_params free_params;
        free_params.handle = params.handle;
        UNIX_CALL(rsvg_free_handle, &free_params);
        return E_OUTOFMEMORY;
    }

    object->ID2D1Resource_iface.lpVtbl = &d2d_svg_document_vtbl;
    object->refcount       = 1;
    object->viewport_size  = viewport_size;
    object->rsvg_handle    = params.handle;
    object->factory        = context->factory;
    ID2D1Factory_AddRef(object->factory);

    TRACE("Created SVG document %p with rsvg handle %p.\n", object, params.handle);
    *document = (ID2D1SvgDocument *)&object->ID2D1Resource_iface;
    return S_OK;
}