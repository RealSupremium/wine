/* WinRT Windows.Data.Json.JsonObject Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
 * Copyright (C) 2026 Olivia Ryan
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

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_object
{
    IJsonObject IJsonObject_iface;
    IJsonValue IJsonValue_iface;
    IMap_HSTRING_IJsonValue IMap_HSTRING_IJsonValue_iface;
    LONG ref;
    IMap_HSTRING_IInspectable *members;
};

static inline struct json_object *impl_from_IJsonObject( IJsonObject *iface )
{
    return CONTAINING_RECORD( iface, struct json_object, IJsonObject_iface );
}

static HRESULT WINAPI json_object_QueryInterface( IJsonObject *iface, REFIID iid, void **out )
{
    struct json_object *impl = impl_from_IJsonObject( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IJsonObject ))
    {
        *out = &impl->IJsonObject_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IJsonValue ))
    {
        *out = &impl->IJsonValue_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMap_HSTRING_IJsonValue ))
    {
        *out = &impl->IMap_HSTRING_IJsonValue_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* IIterable<IKeyValuePair<HSTRING, IJsonValue*>> -> forward to PropertySet's
     * IIterable<IKeyValuePair<HSTRING, IInspectable*>>. The vtable layouts are
     * ABI-compatible since IJsonValue* and IInspectable* are pointer types. */
    {
        static const GUID IID_IIterable_IKeyValuePair_HSTRING_IJsonValue =
            {0xdfabb6e1, 0x0411, 0x5a8f, {0xaa, 0x87, 0x35, 0x4e, 0x71, 0x10, 0xf0, 0x99}};

        if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue ))
        {
            return IMap_HSTRING_IInspectable_QueryInterface( impl->members,
                &IID_IIterable_IKeyValuePair_HSTRING_IInspectable, out );
        }
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI json_object_AddRef( IJsonObject *iface )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI json_object_Release( IJsonObject *iface )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        IMap_HSTRING_IInspectable_Release( impl->members );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI json_object_GetIids( IJsonObject *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetRuntimeClassName( IJsonObject *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetTrustLevel( IJsonObject *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetNamedValue( IJsonObject *iface, HSTRING name, IJsonValue **value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    boolean exists;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;

    hr = IMap_HSTRING_IInspectable_HasKey( impl->members, name, &exists );
    if (FAILED(hr) || !exists) return WEB_E_JSON_VALUE_NOT_FOUND;

    return IMap_HSTRING_IInspectable_Lookup( impl->members, name, (IInspectable **)value );
}

static HRESULT WINAPI json_object_SetNamedValue( IJsonObject *iface, HSTRING name, IJsonValue *value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    boolean dummy;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    return IMap_HSTRING_IInspectable_Insert( impl->members, name, (IInspectable *)value, &dummy );
}

static HRESULT WINAPI json_object_GetNamedObject( IJsonObject *iface, HSTRING name, IJsonObject **value )
{
    IJsonValue *internal_value;
    JsonValueType value_type;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (FAILED(hr = IJsonObject_GetNamedValue( iface, name, &internal_value )))
        return hr;

    IJsonValue_Release( internal_value );
    IJsonValue_get_ValueType( internal_value, &value_type );
    if (value_type != JsonValueType_Object) return E_ILLEGAL_METHOD_CALL;

    return IJsonValue_GetObject( internal_value, value );
}

static HRESULT WINAPI json_object_GetNamedArray( IJsonObject *iface, HSTRING name, IJsonArray **value )
{
    IJsonValue *internal_value;
    JsonValueType value_type;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (FAILED(hr = IJsonObject_GetNamedValue( iface, name, &internal_value )))
        return hr;

    IJsonValue_Release( internal_value );
    IJsonValue_get_ValueType( internal_value, &value_type );
    if (value_type != JsonValueType_Array) return E_ILLEGAL_METHOD_CALL;

    return IJsonValue_GetArray( internal_value, value );
}

static HRESULT WINAPI json_object_GetNamedString( IJsonObject *iface, HSTRING name, HSTRING *value )
{
    IJsonValue *internal_value;
    JsonValueType value_type;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (FAILED(hr = IJsonObject_GetNamedValue( iface, name, &internal_value )))
        return hr;

    IJsonValue_Release( internal_value );
    IJsonValue_get_ValueType( internal_value, &value_type );
    if (value_type != JsonValueType_String) return E_ILLEGAL_METHOD_CALL;

    return IJsonValue_GetString( internal_value, value );
}

static HRESULT WINAPI json_object_GetNamedNumber( IJsonObject *iface, HSTRING name, DOUBLE *value )
{
    IJsonValue *internal_value;
    JsonValueType value_type;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (FAILED(hr = IJsonObject_GetNamedValue( iface, name, &internal_value )))
        return hr;

    IJsonValue_Release( internal_value );
    IJsonValue_get_ValueType( internal_value, &value_type );
    if (value_type != JsonValueType_Number) return E_ILLEGAL_METHOD_CALL;

    return IJsonValue_GetNumber( internal_value, value );
}

static HRESULT WINAPI json_object_GetNamedBoolean( IJsonObject *iface, HSTRING name, boolean *value )
{
    IJsonValue *internal_value;
    JsonValueType value_type;
    HRESULT hr;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (FAILED(hr = IJsonObject_GetNamedValue( iface, name, &internal_value )))
        return hr;

    IJsonValue_Release( internal_value );
    IJsonValue_get_ValueType( internal_value, &value_type );
    if (value_type != JsonValueType_Boolean) return E_ILLEGAL_METHOD_CALL;

    return IJsonValue_GetBoolean( internal_value, value );
}

DEFINE_IINSPECTABLE( json_object_value, IJsonValue, struct json_object, IJsonObject_iface )

static HRESULT WINAPI json_object_value_get_ValueType( IJsonValue *iface, JsonValueType *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = JsonValueType_Object;
    return S_OK;
}

static HRESULT WINAPI json_object_value_Stringify( IJsonValue *iface, HSTRING *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_value_GetString( IJsonValue *iface, HSTRING *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_object_value_GetNumber( IJsonValue *iface, DOUBLE *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_object_value_GetBoolean( IJsonValue *iface, boolean *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_object_value_GetArray( IJsonValue *iface, IJsonArray **value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_object_value_GetObject( IJsonValue *iface, IJsonObject **value )
{
    struct json_object *impl = impl_from_IJsonValue( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &impl->IJsonObject_iface;
    IJsonObject_AddRef( *value );
    return S_OK;
}

static const struct IJsonValueVtbl json_object_value_vtbl =
{
    json_object_value_QueryInterface,
    json_object_value_AddRef,
    json_object_value_Release,
    /* IInspectable methods */
    json_object_value_GetIids,
    json_object_value_GetRuntimeClassName,
    json_object_value_GetTrustLevel,
    /* IJsonValue methods */
    json_object_value_get_ValueType,
    json_object_value_Stringify,
    json_object_value_GetString,
    json_object_value_GetNumber,
    json_object_value_GetBoolean,
    json_object_value_GetArray,
    json_object_value_GetObject,
};

DEFINE_IINSPECTABLE( json_object_map, IMap_HSTRING_IJsonValue, struct json_object, IJsonObject_iface )

static HRESULT WINAPI json_object_map_Lookup( IMap_HSTRING_IJsonValue *iface, HSTRING key, IJsonValue **value )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p, key %s, value %p\n", iface, debugstr_hstring( key ), value );
    return IMap_HSTRING_IInspectable_Lookup( impl->members, key, (IInspectable **)value );
}

static HRESULT WINAPI json_object_map_get_Size( IMap_HSTRING_IJsonValue *iface, unsigned int *size )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p, size %p\n", iface, size );
    return IMap_HSTRING_IInspectable_get_Size( impl->members, size );
}

static HRESULT WINAPI json_object_map_HasKey( IMap_HSTRING_IJsonValue *iface, HSTRING key, boolean *found )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p, key %s, found %p\n", iface, debugstr_hstring( key ), found );
    return IMap_HSTRING_IInspectable_HasKey( impl->members, key, found );
}

static HRESULT WINAPI json_object_map_GetView( IMap_HSTRING_IJsonValue *iface, IMapView_HSTRING_IJsonValue **view )
{
    FIXME( "iface %p, view %p stub!\n", iface, view );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_map_Insert( IMap_HSTRING_IJsonValue *iface, HSTRING key, IJsonValue *value, boolean *replaced )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p, key %s, value %p, replaced %p\n", iface, debugstr_hstring( key ), value, replaced );
    return IMap_HSTRING_IInspectable_Insert( impl->members, key, (IInspectable *)value, replaced );
}

static HRESULT WINAPI json_object_map_Remove( IMap_HSTRING_IJsonValue *iface, HSTRING key )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p, key %s\n", iface, debugstr_hstring( key ) );
    return IMap_HSTRING_IInspectable_Remove( impl->members, key );
}

static HRESULT WINAPI json_object_map_Clear( IMap_HSTRING_IJsonValue *iface )
{
    struct json_object *impl = impl_from_IMap_HSTRING_IJsonValue( iface );
    TRACE( "iface %p\n", iface );
    return IMap_HSTRING_IInspectable_Clear( impl->members );
}

static const struct IMap_HSTRING_IJsonValueVtbl json_object_map_vtbl =
{
    json_object_map_QueryInterface,
    json_object_map_AddRef,
    json_object_map_Release,
    /* IInspectable methods */
    json_object_map_GetIids,
    json_object_map_GetRuntimeClassName,
    json_object_map_GetTrustLevel,
    /* IMap<HSTRING, IJsonValue*> methods */
    json_object_map_Lookup,
    json_object_map_get_Size,
    json_object_map_HasKey,
    json_object_map_GetView,
    json_object_map_Insert,
    json_object_map_Remove,
    json_object_map_Clear,
};

static const struct IJsonObjectVtbl json_object_vtbl =
{
    json_object_QueryInterface,
    json_object_AddRef,
    json_object_Release,
    /* IInspectable methods */
    json_object_GetIids,
    json_object_GetRuntimeClassName,
    json_object_GetTrustLevel,
    /* IJsonObject methods */
    json_object_GetNamedValue,
    json_object_SetNamedValue,
    json_object_GetNamedObject,
    json_object_GetNamedArray,
    json_object_GetNamedString,
    json_object_GetNamedNumber,
    json_object_GetNamedBoolean,
};

struct json_object_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct json_object_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct json_object_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IPropertySet *property_set;
    HSTRING property_set_class;
    struct json_object *impl;
    HSTRING_HEADER header;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IJsonObject_iface.lpVtbl = &json_object_vtbl;
    impl->IJsonValue_iface.lpVtbl = &json_object_value_vtbl;
    impl->IMap_HSTRING_IJsonValue_iface.lpVtbl = &json_object_map_vtbl;
    impl->ref = 1;

    WindowsCreateStringReference( RuntimeClass_Windows_Foundation_Collections_PropertySet, wcslen( RuntimeClass_Windows_Foundation_Collections_PropertySet ),
                                  &header, &property_set_class );
    if (FAILED(hr = RoActivateInstance( property_set_class, (IInspectable **)&property_set ))) goto failed;

    hr = IPropertySet_QueryInterface( property_set, &IID_IMap_HSTRING_IInspectable, (void **)&impl->members );
    IPropertySet_Release( property_set );
    if (FAILED(hr)) goto failed;

    *instance = (IInspectable *)&impl->IJsonObject_iface;
    return S_OK;

failed:
    free( impl );
    return hr;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct json_object_statics json_object_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *json_object_factory = &json_object_statics.IActivationFactory_iface;
