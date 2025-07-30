/* WinRT IAsync* implementation
 *
 * Copyright 2022 Bernhard Kölbl for CodeWeavers
 * Copyright 2022 Rémi Bernon for CodeWeavers
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

#define WIDL_using_Wine_Internal
#include "private.h"
#include "initguid.h"
#include "async_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(combase);

#define WIDL_impl_async_info
#define WIDL_impl_async_bool
#define WIDL_impl_async_result
#include "async_private_impl.h"

#define Closed 4
#define HANDLER_NOT_SET ((void *)~(ULONG_PTR)0)

struct async_info
{
    struct async_info_klass klass;

    async_operation_callback callback;
    TP_WORK *async_run_work;
    IUnknown *invoker;
    IUnknown *param;

    CRITICAL_SECTION cs;
    IAsyncOperationCompletedHandlerImpl *handler;
    PROPVARIANT result;
    AsyncStatus status;
    HRESULT hr;
};

static struct async_info *async_info_from_klass( struct async_info_klass *klass )
{
    return CONTAINING_RECORD( klass, struct async_info, klass );
}

static HRESULT async_info_missing_interface( struct async_info *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void async_info_destroy( struct async_info *impl )
{
    if (impl->handler && impl->handler != HANDLER_NOT_SET) IAsyncOperationCompletedHandlerImpl_Release( impl->handler );
    IAsyncInfo_Close( &impl->klass.IAsyncInfo_iface );
    if (impl->param) IUnknown_Release( impl->param );
    if (impl->invoker) IUnknown_Release( impl->invoker );
    PropVariantClear( &impl->result );
    impl->cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection( &impl->cs );
    free( impl );
}

static HRESULT async_info_put_Completed( struct async_info *impl, IAsyncOperationCompletedHandlerImpl *handler )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p, handler %p.\n", impl, handler );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->handler != HANDLER_NOT_SET) hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else if ((impl->handler = handler))
    {
        IAsyncOperationCompletedHandlerImpl_AddRef( impl->handler );

        if (impl->status > Started)
        {
            IInspectable *operation = impl->klass.IInspectable_outer;
            AsyncStatus status = impl->status;
            impl->handler = NULL; /* Prevent concurrent invoke. */
            LeaveCriticalSection( &impl->cs );

            IAsyncOperationCompletedHandlerImpl_Invoke( handler, operation, status );
            IAsyncOperationCompletedHandlerImpl_Release( handler );

            return S_OK;
        }
    }
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_get_Completed( struct async_info *impl, IAsyncOperationCompletedHandlerImpl **handler )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p, handler %p.\n", impl, handler );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) hr = E_ILLEGAL_METHOD_CALL;
    if (impl->handler == NULL || impl->handler == HANDLER_NOT_SET) *handler = NULL;
    else IAsyncOperationCompletedHandlerImpl_AddRef( (*handler = impl->handler) );
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_get_Result( struct async_info *impl, PROPVARIANT *result )
{
    HRESULT hr = E_ILLEGAL_METHOD_CALL;

    TRACE( "iface %p, result %p.\n", impl, result );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Completed || impl->status == Error)
    {
        PropVariantCopy( result, &impl->result );
        hr = impl->hr;
    }
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_Start( struct async_info *impl )
{
    TRACE( "iface %p.\n", impl );

    /* keep the async alive in the callback */
    IInspectable_AddRef( impl->klass.IInspectable_outer );
    SubmitThreadpoolWork( impl->async_run_work );

    return S_OK;
}

static HRESULT async_info_get_Id( struct async_info *impl, UINT32 *id )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p, id %p.\n", impl, id );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) hr = E_ILLEGAL_METHOD_CALL;
    *id = 1;
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_get_Status( struct async_info *impl, AsyncStatus *status )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p, status %p.\n", impl, status );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) hr = E_ILLEGAL_METHOD_CALL;
    *status = impl->status;
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_get_ErrorCode( struct async_info *impl, HRESULT *error_code )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p, error_code %p.\n", impl, error_code );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) *error_code = hr = E_ILLEGAL_METHOD_CALL;
    else *error_code = impl->hr;
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_Cancel( struct async_info *impl )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p.\n", impl );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->status == Started) impl->status = Canceled;
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT async_info_Close( struct async_info *impl )
{
    HRESULT hr = S_OK;

    TRACE( "iface %p.\n", impl );

    EnterCriticalSection( &impl->cs );
    if (impl->status == Started)
        hr = E_ILLEGAL_STATE_CHANGE;
    else if (impl->status != Closed)
    {
        CloseThreadpoolWork( impl->async_run_work );
        impl->async_run_work = NULL;
        impl->status = Closed;
    }
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static void CALLBACK async_info_callback( TP_CALLBACK_INSTANCE *instance, void *iface, TP_WORK *work )
{
    struct async_info *impl = async_info_from_IAsyncInfoImpl( iface );
    IInspectable *operation = impl->klass.IInspectable_outer;
    PROPVARIANT result = {0};
    HRESULT hr;

    hr = impl->callback( impl->invoker, impl->param, &result );

    EnterCriticalSection( &impl->cs );
    if (impl->status != Closed) impl->status = FAILED(hr) ? Error : Completed;
    PropVariantCopy( &impl->result, &result );
    impl->hr = hr;

    if (impl->handler != NULL && impl->handler != HANDLER_NOT_SET)
    {
        IAsyncOperationCompletedHandlerImpl *handler = impl->handler;
        AsyncStatus status = impl->status;
        impl->handler = NULL; /* Prevent concurrent invoke. */
        LeaveCriticalSection( &impl->cs );

        IAsyncOperationCompletedHandlerImpl_Invoke( handler, operation, status );
        IAsyncOperationCompletedHandlerImpl_Release( handler );
    }
    else LeaveCriticalSection( &impl->cs );

    /* release refcount acquired in Start */
    IInspectable_Release( operation );

    PropVariantClear( &result );
}

static const struct async_info_funcs async_info_funcs = ASYNC_INFO_FUNCS_INIT;

static HRESULT async_info_create( IUnknown *invoker, IUnknown *param, async_operation_callback callback,
                                  IInspectable *outer, IAsyncInfoImpl **out )
{
    struct async_info *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(struct async_info) ))) return E_OUTOFMEMORY;
    async_info_klass_init( &impl->klass );

    impl->callback = callback;
    impl->handler = HANDLER_NOT_SET;
    impl->status = Started;
    if (!(impl->async_run_work = CreateThreadpoolWork( async_info_callback, &impl->klass.IAsyncInfoImpl_iface, NULL )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        free( impl );
        return hr;
    }

    if ((impl->invoker = invoker)) IUnknown_AddRef( impl->invoker );
    if ((impl->param = param)) IUnknown_AddRef( impl->param );

    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)( __FILE__ ": async_info.cs" );

    *out = &impl->klass.IAsyncInfoImpl_iface;
    return S_OK;
}

struct async_bool
{
    struct async_bool_klass klass;
    IAsyncInfoImpl *IAsyncInfoImpl_inner;
};

static struct async_bool *async_bool_from_klass( struct async_bool_klass *klass )
{
    return CONTAINING_RECORD( klass, struct async_bool, klass );
}

static HRESULT async_bool_missing_interface( struct async_bool *impl, REFIID iid, void **out )
{
    return IAsyncInfoImpl_QueryInterface( impl->IAsyncInfoImpl_inner, iid, out );
}

static void async_bool_destroy( struct async_bool *impl )
{
    IAsyncInfoImpl_Release( impl->IAsyncInfoImpl_inner );
    free( impl );
}

static HRESULT async_bool_put_Completed( struct async_bool *impl, IAsyncOperationCompletedHandler_boolean *bool_handler )
{
    IAsyncOperationCompletedHandlerImpl *handler = (IAsyncOperationCompletedHandlerImpl *)bool_handler;
    TRACE( "iface %p, handler %p.\n", impl, handler );
    return IAsyncInfoImpl_put_Completed( impl->IAsyncInfoImpl_inner, handler );
}

static HRESULT async_bool_get_Completed( struct async_bool *impl, IAsyncOperationCompletedHandler_boolean **bool_handler )
{
    IAsyncOperationCompletedHandlerImpl **handler = (IAsyncOperationCompletedHandlerImpl **)bool_handler;
    TRACE( "iface %p, handler %p.\n", impl, handler );
    return IAsyncInfoImpl_get_Completed( impl->IAsyncInfoImpl_inner, handler );
}

static HRESULT async_bool_GetResults( struct async_bool *impl, BOOLEAN *results )
{
    PROPVARIANT result = {.vt = VT_BOOL};
    HRESULT hr;

    TRACE( "iface %p, results %p.\n", impl, results );

    hr = IAsyncInfoImpl_get_Result( impl->IAsyncInfoImpl_inner, &result );

    *results = result.boolVal;
    PropVariantClear( &result );
    return hr;
}

static const struct async_bool_funcs async_bool_funcs = ASYNC_BOOL_FUNCS_INIT;

HRESULT async_operation_boolean_create( IUnknown *invoker, IUnknown *param, async_operation_callback callback,
                                        IAsyncOperation_boolean **out )
{
    struct async_bool *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    async_bool_klass_init( &impl->klass, L"Windows.Foundation.IAsyncOperation`1<Boolean>" );

    if (FAILED(hr = async_info_create( invoker, param, callback, (IInspectable *)&impl->klass.IAsyncOperation_boolean_iface, &impl->IAsyncInfoImpl_inner )) ||
        FAILED(hr = IAsyncInfoImpl_Start( impl->IAsyncInfoImpl_inner )))
    {
        if (impl->IAsyncInfoImpl_inner) IAsyncInfoImpl_Release( impl->IAsyncInfoImpl_inner );
        free( impl );
        return hr;
    }

    *out = &impl->klass.IAsyncOperation_boolean_iface;
    TRACE( "created IAsyncOperation_boolean %p\n", *out );
    return S_OK;
}

struct async_result
{
    struct async_result_klass klass;
    IAsyncInfoImpl *IAsyncInfoImpl_inner;
};

static struct async_result *async_result_from_klass( struct async_result_klass *klass )
{
    return CONTAINING_RECORD( klass, struct async_result, klass );
}

static HRESULT async_result_missing_interface( struct async_result *impl, REFIID iid, void **out )
{
    return IAsyncInfoImpl_QueryInterface( impl->IAsyncInfoImpl_inner, iid, out );
}

static void async_result_destroy( struct async_result *impl )
{
    IAsyncInfoImpl_Release( impl->IAsyncInfoImpl_inner );
    free( impl );
}

static HRESULT async_result_put_Completed( struct async_result *impl, IAsyncOperationCompletedHandler_ForceFeedbackLoadEffectResult *handler )
{
    TRACE( "iface %p, handler %p.\n", impl, handler );
    return IAsyncInfoImpl_put_Completed( impl->IAsyncInfoImpl_inner, (IAsyncOperationCompletedHandlerImpl *)handler );
}

static HRESULT async_result_get_Completed( struct async_result *impl, IAsyncOperationCompletedHandler_ForceFeedbackLoadEffectResult **handler )
{
    TRACE( "iface %p, handler %p.\n", impl, handler );
    return IAsyncInfoImpl_get_Completed( impl->IAsyncInfoImpl_inner, (IAsyncOperationCompletedHandlerImpl **)handler );
}

static HRESULT async_result_GetResults( struct async_result *impl, ForceFeedbackLoadEffectResult *results )
{
    PROPVARIANT result = {.vt = VT_UI4};
    HRESULT hr;

    TRACE( "iface %p, results %p.\n", impl, results );

    hr = IAsyncInfoImpl_get_Result( impl->IAsyncInfoImpl_inner, &result );

    *results = result.ulVal;
    PropVariantClear( &result );
    return hr;
}

static const struct async_result_funcs async_result_funcs = ASYNC_RESULT_FUNCS_INIT;

HRESULT async_operation_effect_result_create( IUnknown *invoker, IUnknown *param, async_operation_callback callback,
                                              IAsyncOperation_ForceFeedbackLoadEffectResult **out )
{
    struct async_result *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    async_result_klass_init( &impl->klass, L"Windows.Foundation.IAsyncOperation`1<Windows.Gaming.Input.ForceFeedback.ForceFeedbackLoadEffectResult>" );

    if (FAILED(hr = async_info_create( invoker, param, callback, (IInspectable *)&impl->klass.IAsyncOperation_ForceFeedbackLoadEffectResult_iface, &impl->IAsyncInfoImpl_inner )) ||
        FAILED(hr = IAsyncInfoImpl_Start( impl->IAsyncInfoImpl_inner )))
    {
        if (impl->IAsyncInfoImpl_inner) IAsyncInfoImpl_Release( impl->IAsyncInfoImpl_inner );
        free( impl );
        return hr;
    }

    *out = &impl->klass.IAsyncOperation_ForceFeedbackLoadEffectResult_iface;
    TRACE( "created IAsyncOperation_ForceFeedbackLoadEffectResult %p\n", *out );
    return S_OK;
}
