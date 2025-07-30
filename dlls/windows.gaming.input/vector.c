/* WinRT Windows.Gaming.Input implementation
 *
 * Copyright 2021 Rémi Bernon for CodeWeavers
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

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(combase);

#define WIDL_impl_iterator
#define WIDL_impl_vector_view
#define WIDL_impl_vector
#include "vector_private_impl.h"

struct iterator
{
    struct iterator_klass klass;
    IVectorView_IInspectable *view;
    UINT32 index;
    UINT32 size;
};

static struct iterator *iterator_from_klass( struct iterator_klass *klass )
{
    return CONTAINING_RECORD( klass, struct iterator, klass );
}

static HRESULT iterator_missing_interface( struct iterator *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void iterator_destroy( struct iterator *impl )
{
    IVectorView_IInspectable_Release( impl->view );
    free( impl );
}

static HRESULT iterator_get_Current( struct iterator *impl, IInspectable **value )
{
    TRACE( "iterator %p, value %p.\n", impl, value );
    return IVectorView_IInspectable_GetAt( impl->view, impl->index, value );
}

static HRESULT iterator_get_HasCurrent( struct iterator *impl, boolean *value )
{
    TRACE( "iterator %p, value %p.\n", impl, value );
    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT iterator_MoveNext( struct iterator *impl, boolean *value )
{
    TRACE( "iterator %p, value %p.\n", impl, value );
    if (impl->index < impl->size) impl->index++;
    return IIterator_IInspectable_get_HasCurrent( &impl->klass.IIterator_IInspectable_iface, value );
}

static HRESULT iterator_GetMany( struct iterator *impl, UINT32 items_size,
                                        IInspectable **items, UINT *count )
{
    TRACE( "iterator %p, items_size %u, items %p, count %p.\n", impl, items_size, items, count );
    return IVectorView_IInspectable_GetMany( impl->view, impl->index, items_size, items, count );
}

static const struct iterator_funcs iterator_funcs = ITERATOR_FUNCS_INIT;

struct vector_view
{
    struct vector_view_klass klass;
    UINT32 size;
    IInspectable *elements[1];
};

static struct vector_view *vector_view_from_klass( struct vector_view_klass *klass )
{
    return CONTAINING_RECORD( klass, struct vector_view, klass );
}

static HRESULT vector_view_missing_interface( struct vector_view *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void vector_view_destroy( struct vector_view *impl )
{
    for (UINT32 i = 0; i < impl->size; ++i) IInspectable_Release( impl->elements[i] );
    free( impl );
}

static HRESULT vector_view_GetAt( struct vector_view *impl, UINT32 index, IInspectable **value )
{
    TRACE( "vector_view %p, index %u, value %p.\n", impl, index, value );

    *value = NULL;
    if (index >= impl->size) return E_BOUNDS;

    IInspectable_AddRef( (*value = impl->elements[index]) );
    return S_OK;
}

static HRESULT vector_view_get_Size( struct vector_view *impl, UINT32 *value )
{
    TRACE( "vector_view %p, value %p.\n", impl, value );
    *value = impl->size;
    return S_OK;
}

static HRESULT vector_view_IndexOf( struct vector_view *impl, IInspectable *element,
                                    UINT32 *index, BOOLEAN *found )
{
    ULONG i;

    TRACE( "vector_view %p, element %p, index %p, found %p.\n", impl, element, index, found );

    for (i = 0; i < impl->size; ++i) if (impl->elements[i] == element) break;
    if ((*found = (i < impl->size))) *index = i;
    else *index = 0;

    return S_OK;
}

static HRESULT vector_view_GetMany( struct vector_view *impl, UINT32 start_index,
                                    UINT32 items_size, IInspectable **items, UINT *count )
{
    UINT32 i;

    TRACE( "vector_view %p, start_index %u, items_size %u, items %p, count %p.\n",
           impl, start_index, items_size, items, count );

    if (start_index >= impl->size) return E_BOUNDS;

    for (i = start_index; i < impl->size; ++i)
    {
        if (i - start_index >= items_size) break;
        IInspectable_AddRef( (items[i - start_index] = impl->elements[i]) );
    }
    *count = i - start_index;

    return S_OK;
}

static HRESULT vector_view_First( struct vector_view *impl, IIterator_IInspectable **value )
{
    struct iterator *iter;

    TRACE( "vector_view %p, value %p.\n", impl, value );

    if (!(iter = calloc( 1, sizeof(struct iterator) ))) return E_OUTOFMEMORY;
    iterator_klass_init( &iter->klass );
    iter->klass.iid = impl->klass.iids.iterator;

    IVectorView_IInspectable_AddRef( (iter->view = &impl->klass.IVectorView_IInspectable_iface) );
    iter->size = impl->size;

    *value = &iter->klass.IIterator_IInspectable_iface;
    return S_OK;
}

static const struct vector_view_funcs vector_view_funcs = VECTOR_VIEW_FUNCS_INIT;

struct vector
{
    struct vector_klass klass;
    UINT32 size;
    UINT32 capacity;
    IInspectable **elements;
};

static struct vector *vector_from_klass( struct vector_klass *klass )
{
    return CONTAINING_RECORD( klass, struct vector, klass );
}

static HRESULT vector_missing_interface( struct vector *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void vector_destroy( struct vector *impl )
{
    IVector_IInspectable_Clear( &impl->klass.IVector_IInspectable_iface );
    free( impl );
}

static HRESULT vector_GetAt( struct vector *impl, UINT32 index, IInspectable **value )
{
    TRACE( "vector %p, index %u, value %p.\n", impl, index, value );

    *value = NULL;
    if (index >= impl->size) return E_BOUNDS;

    IInspectable_AddRef( (*value = impl->elements[index]) );
    return S_OK;
}

static HRESULT vector_get_Size( struct vector *impl, UINT32 *value )
{
    TRACE( "vector %p, value %p.\n", impl, value );
    *value = impl->size;
    return S_OK;
}

static HRESULT vector_GetView( struct vector *impl, IVectorView_IInspectable **value )
{
    struct vector_view *view;
    ULONG i;

    TRACE( "vector %p, value %p.\n", impl, value );

    if (!(view = calloc( 1, offsetof( struct vector_view, elements[impl->size] ) ))) return E_OUTOFMEMORY;
    vector_view_klass_init( &view->klass );
    view->klass.iids = impl->klass.iids;

    for (i = 0; i < impl->size; ++i) IInspectable_AddRef( (view->elements[view->size++] = impl->elements[i]) );

    *value = &view->klass.IVectorView_IInspectable_iface;
    return S_OK;
}

static HRESULT vector_IndexOf( struct vector *impl, IInspectable *element, UINT32 *index, BOOLEAN *found )
{
    ULONG i;

    TRACE( "vector %p, element %p, index %p, found %p.\n", impl, element, index, found );

    for (i = 0; i < impl->size; ++i) if (impl->elements[i] == element) break;
    if ((*found = (i < impl->size))) *index = i;
    else *index = 0;

    return S_OK;
}

static HRESULT vector_SetAt( struct vector *impl, UINT32 index, IInspectable *value )
{
    TRACE( "vector %p, index %u, value %p.\n", impl, index, value );

    if (index >= impl->size) return E_BOUNDS;
    IInspectable_Release( impl->elements[index] );
    IInspectable_AddRef( (impl->elements[index] = value) );
    return S_OK;
}

static HRESULT vector_InsertAt( struct vector *impl, UINT32 index, IInspectable *value )
{
    IInspectable **tmp = impl->elements;

    TRACE( "vector %p, index %u, value %p.\n", impl, index, value );

    if (impl->size == impl->capacity)
    {
        impl->capacity = max( 32, impl->capacity * 3 / 2 );
        if (!(impl->elements = realloc( impl->elements, impl->capacity * sizeof(*impl->elements) )))
        {
            impl->elements = tmp;
            return E_OUTOFMEMORY;
        }
    }

    memmove( impl->elements + index + 1, impl->elements + index, (impl->size++ - index) * sizeof(*impl->elements) );
    IInspectable_AddRef( (impl->elements[index] = value) );
    return S_OK;
}

static HRESULT vector_RemoveAt( struct vector *impl, UINT32 index )
{
    TRACE( "vector %p, index %u.\n", impl, index );

    if (index >= impl->size) return E_BOUNDS;
    IInspectable_Release( impl->elements[index] );
    memmove( impl->elements + index, impl->elements + index + 1, (--impl->size - index) * sizeof(*impl->elements) );
    return S_OK;
}

static HRESULT vector_Append( struct vector *impl, IInspectable *value )
{
    TRACE( "vector %p, value %p.\n", impl, value );
    return IVector_IInspectable_InsertAt( &impl->klass.IVector_IInspectable_iface, impl->size, value );
}

static HRESULT vector_RemoveAtEnd( struct vector *impl )
{
    TRACE( "vector %p.\n", impl );
    if (impl->size) IInspectable_Release( impl->elements[--impl->size] );
    return S_OK;
}

static HRESULT vector_Clear( struct vector *impl )
{
    TRACE( "vector %p.\n", impl );

    while (impl->size) IVector_IInspectable_RemoveAtEnd( &impl->klass.IVector_IInspectable_iface );
    free( impl->elements );
    impl->capacity = 0;
    impl->elements = NULL;

    return S_OK;
}

static HRESULT vector_GetMany( struct vector *impl, UINT32 start_index,
                               UINT32 items_size, IInspectable **items, UINT *count )
{
    UINT32 i;

    TRACE( "vector %p, start_index %u, items_size %u, items %p, count %p.\n",
           impl, start_index, items_size, items, count );

    if (start_index >= impl->size) return E_BOUNDS;

    for (i = start_index; i < impl->size; ++i)
    {
        if (i - start_index >= items_size) break;
        IInspectable_AddRef( (items[i - start_index] = impl->elements[i]) );
    }
    *count = i - start_index;

    return S_OK;
}

static HRESULT vector_ReplaceAll( struct vector *impl, UINT32 count, IInspectable **items )
{
    IVector_IInspectable *iface = &impl->klass.IVector_IInspectable_iface;
    HRESULT hr;
    ULONG i;

    TRACE( "vector %p, count %u, items %p.\n", impl, count, items );

    hr = IVector_IInspectable_Clear( iface );
    for (i = 0; i < count && SUCCEEDED(hr); ++i) hr = IVector_IInspectable_Append( iface, items[i] );
    return hr;
}

static HRESULT vector_First( struct vector *impl, IIterator_IInspectable **value )
{
    IIterable_IInspectable *iterable;
    IVectorView_IInspectable *view;
    HRESULT hr;

    TRACE( "vector %p, value %p.\n", impl, value );

    if (FAILED(hr = IVector_IInspectable_GetView( &impl->klass.IVector_IInspectable_iface, &view ))) return hr;

    hr = IVectorView_IInspectable_QueryInterface( view, impl->klass.iids.iterable, (void **)&iterable );
    IVectorView_IInspectable_Release( view );
    if (FAILED(hr)) return hr;

    hr = IIterable_IInspectable_First( iterable, value );
    IIterable_IInspectable_Release( iterable );
    return hr;
}

static const struct vector_funcs vector_funcs = VECTOR_FUNCS_INIT;

HRESULT vector_create( const struct vector_iids *iids, void **out )
{
    struct vector *impl;

    TRACE( "iid %s, out %p.\n", debugstr_guid( iids->vector ), out );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    vector_klass_init( &impl->klass );
    impl->klass.iids = *iids;

    *out = &impl->klass.IVector_IInspectable_iface;
    TRACE( "created %p\n", *out );
    return S_OK;
}
