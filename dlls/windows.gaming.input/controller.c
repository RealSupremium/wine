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
#include "provider.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(input);

#define WIDL_impl_raw_controller
#define WIDL_impl_raw_controller_statics
#include "classes_impl.h"

static CRITICAL_SECTION controller_cs;
static CRITICAL_SECTION_DEBUG controller_cs_debug =
{
    0, 0, &controller_cs,
    { &controller_cs_debug.ProcessLocksList, &controller_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": controller_cs") }
};
static CRITICAL_SECTION controller_cs = { &controller_cs_debug, -1, 0, 0, 0, 0 };

static IVector_RawGameController *controllers;
static struct list controller_added_handlers = LIST_INIT( controller_added_handlers );
static struct list controller_removed_handlers = LIST_INIT( controller_removed_handlers );

static HRESULT init_controllers(void)
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_RawGameController,
        .view = &IID_IVectorView_RawGameController,
        .iterable = &IID_IIterable_RawGameController,
        .iterator = &IID_IIterator_RawGameController,
    };
    HRESULT hr;

    EnterCriticalSection( &controller_cs );
    if (controllers) hr = S_OK;
    else hr = vector_create( &iids, (void **)&controllers );
    LeaveCriticalSection( &controller_cs );

    return hr;
}

struct raw_controller
{
    struct raw_controller_klass klass;
    IGameControllerProvider *provider;
    IWineGameControllerProvider *wine_provider;
};

static struct raw_controller *raw_controller_from_klass( struct raw_controller_klass *klass )
{
    return CONTAINING_RECORD( klass, struct raw_controller, klass );
}

static HRESULT raw_controller_missing_interface( struct raw_controller *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void raw_controller_destroy( struct raw_controller *impl )
{
    if (impl->wine_provider) IWineGameControllerProvider_Release( impl->wine_provider );
    IGameControllerProvider_Release( impl->provider );
    free( impl );
}

static HRESULT raw_controller_Initialize( struct raw_controller *impl, IGameController *outer, IGameControllerProvider *provider )
{
    HRESULT hr;

    TRACE( "controller %p, outer %p, provider %p.\n", impl, outer, provider );

    impl->klass.IGameController_outer = outer;
    IGameControllerProvider_AddRef( (impl->provider = provider) );

    hr = IGameControllerProvider_QueryInterface( provider, &IID_IWineGameControllerProvider,
                                                 (void **)&impl->wine_provider );
    if (FAILED(hr)) return hr;

    EnterCriticalSection( &controller_cs );
    if (SUCCEEDED(hr = init_controllers()))
        hr = IVector_RawGameController_Append( controllers, &impl->klass.IRawGameController_iface );
    LeaveCriticalSection( &controller_cs );

    return hr;
}

static HRESULT raw_controller_OnInputResumed( struct raw_controller *impl, UINT64 timestamp )
{
    FIXME( "controller %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT raw_controller_OnInputSuspended( struct raw_controller *impl, UINT64 timestamp )
{
    FIXME( "controller %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT raw_controller_get_AxisCount( struct raw_controller *impl, INT32 *value )
{
    return IWineGameControllerProvider_get_AxisCount( impl->wine_provider, value );
}

static HRESULT raw_controller_get_ButtonCount( struct raw_controller *impl, INT32 *value )
{
    return IWineGameControllerProvider_get_ButtonCount( impl->wine_provider, value );
}

static HRESULT raw_controller_get_ForceFeedbackMotors( struct raw_controller *impl, IVectorView_ForceFeedbackMotor **value )
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_ForceFeedbackMotor,
        .view = &IID_IVectorView_ForceFeedbackMotor,
        .iterable = &IID_IIterable_ForceFeedbackMotor,
        .iterator = &IID_IIterator_ForceFeedbackMotor,
    };
    IVector_ForceFeedbackMotor *vector;
    IForceFeedbackMotor *motor;
    HRESULT hr;

    TRACE( "controller %p, value %p\n", impl, value );

    if (FAILED(hr = vector_create( &iids, (void **)&vector ))) return hr;

    if (SUCCEEDED(IWineGameControllerProvider_get_ForceFeedbackMotor( impl->wine_provider, &motor )) && motor)
    {
        hr = IVector_ForceFeedbackMotor_Append( vector, motor );
        IForceFeedbackMotor_Release( motor );
    }

    if (SUCCEEDED(hr)) hr = IVector_ForceFeedbackMotor_GetView( vector, value );
    IVector_ForceFeedbackMotor_Release( vector );

    return hr;
}

static HRESULT raw_controller_get_HardwareProductId( struct raw_controller *impl, UINT16 *value )
{
    return IGameControllerProvider_get_HardwareProductId( impl->provider, value );
}

static HRESULT raw_controller_get_HardwareVendorId( struct raw_controller *impl, UINT16 *value )
{
    return IGameControllerProvider_get_HardwareVendorId( impl->provider, value );
}

static HRESULT raw_controller_get_SwitchCount( struct raw_controller *impl, INT32 *value )
{
    return IWineGameControllerProvider_get_SwitchCount( impl->wine_provider, value );
}

static HRESULT raw_controller_GetButtonLabel( struct raw_controller *impl, INT32 index, enum GameControllerButtonLabel *value )
{
    FIXME( "controller %p, index %d, value %p stub!\n", impl, index, value );
    return E_NOTIMPL;
}

static HRESULT raw_controller_GetCurrentReading( struct raw_controller *impl, UINT32 buttons_size, BOOLEAN *buttons,
                                                 UINT32 switches_size, enum GameControllerSwitchPosition *switches,
                                                 UINT32 axes_size, DOUBLE *axes, UINT64 *timestamp )
{
    WineGameControllerState state;
    HRESULT hr;

    TRACE( "controller %p, buttons_size %u, buttons %p, switches_size %u, switches %p, axes_size %u, axes %p, timestamp %p.\n",
           impl, buttons_size, buttons, switches_size, switches, axes_size, axes, timestamp );

    if (FAILED(hr = IWineGameControllerProvider_get_State( impl->wine_provider, &state ))) return hr;

    memcpy( axes, state.axes, axes_size * sizeof(*axes) );
    memcpy( buttons, state.buttons, buttons_size * sizeof(*buttons) );
    memcpy( switches, state.switches, switches_size * sizeof(*switches) );
    *timestamp = state.timestamp;

    return hr;
}

static HRESULT raw_controller_GetSwitchKind( struct raw_controller *impl, INT32 index, enum GameControllerSwitchKind *value )
{
    FIXME( "controller %p, index %d, value %p stub!\n", impl, index, value );
    return E_NOTIMPL;
}

static HRESULT raw_controller_get_SimpleHapticsControllers( struct raw_controller *impl, IVectorView_SimpleHapticsController **value )
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_SimpleHapticsController,
        .view = &IID_IVectorView_SimpleHapticsController,
        .iterable = &IID_IIterable_SimpleHapticsController,
        .iterator = &IID_IIterator_SimpleHapticsController,
    };
    IVector_SimpleHapticsController *vector;
    HRESULT hr;

    FIXME( "controller %p, value %p stub!\n", impl, value );

    if (SUCCEEDED(hr = vector_create( &iids, (void **)&vector )))
    {
        hr = IVector_SimpleHapticsController_GetView( vector, value );
        IVector_SimpleHapticsController_Release( vector );
    }

    return hr;
}

static HRESULT raw_controller_get_NonRoamableId( struct raw_controller *impl, HSTRING *value )
{
    FIXME( "controller %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT raw_controller_get_DisplayName( struct raw_controller *impl, HSTRING *value )
{
    FIXME( "controller %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static const struct raw_controller_funcs raw_controller_funcs = RAW_CONTROLLER_FUNCS_INIT;

static HRESULT raw_controller_statics_missing_interface( struct raw_controller_statics *statics, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void raw_controller_statics_destroy( struct raw_controller_statics *statics )
{
}

static HRESULT raw_controller_statics_ActivateInstance( struct raw_controller_statics *statics, IInspectable **instance )
{
    FIXME( "statics %p, instance %p stub!\n", statics, instance );
    return E_NOTIMPL;
}

static HRESULT raw_controller_statics_add_RawGameControllerAdded( struct raw_controller_statics *statics,
                                                                  IEventHandler_RawGameController *handler,
                                                                  EventRegistrationToken *token )
{
    TRACE( "statics %p, handler %p, token %p.\n", statics, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &controller_added_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT raw_controller_statics_remove_RawGameControllerAdded( struct raw_controller_statics *statics, EventRegistrationToken token )
{
    TRACE( "statics %p, token %#I64x.\n", statics, token.value );
    return event_handlers_remove( &controller_added_handlers, &token );
}

static HRESULT raw_controller_statics_add_RawGameControllerRemoved( struct raw_controller_statics *statics,
                                                                    IEventHandler_RawGameController *handler,
                                                                    EventRegistrationToken *token )
{
    TRACE( "statics %p, handler %p, token %p.\n", statics, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &controller_removed_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT raw_controller_statics_remove_RawGameControllerRemoved( struct raw_controller_statics *statics, EventRegistrationToken token )
{
    TRACE( "statics %p, token %#I64x.\n", statics, token.value );
    return event_handlers_remove( &controller_removed_handlers, &token );
}

static HRESULT raw_controller_statics_get_RawGameControllers( struct raw_controller_statics *statics,
                                                              IVectorView_RawGameController **value )
{
    HRESULT hr;

    TRACE( "statics %p, value %p.\n", statics, value );

    EnterCriticalSection( &controller_cs );
    if (SUCCEEDED(hr = init_controllers()))
        hr = IVector_RawGameController_GetView( controllers, value );
    LeaveCriticalSection( &controller_cs );

    return hr;
}

static HRESULT raw_controller_statics_FromGameController( struct raw_controller_statics *statics, IGameController *game_controller, IRawGameController **value )
{
    IGameController *controller;
    HRESULT hr;

    TRACE( "statics %p, game_controller %p, value %p.\n", statics, game_controller, value );

    *value = NULL;
    hr = IGameControllerFactoryManagerStatics2_TryGetFactoryControllerFromGameController( manager_factory, &statics->ICustomGameControllerFactory_iface,
                                                                                          game_controller, &controller );
    if (FAILED(hr) || !controller) return hr;

    hr = IGameController_QueryInterface( controller, &IID_IRawGameController, (void **)value );
    IGameController_Release( controller );

    return hr;
}

static HRESULT raw_controller_statics_CreateGameController( struct raw_controller_statics *statics, IGameControllerProvider *provider, IInspectable **value )
{
    struct raw_controller *impl;

    TRACE( "statics %p, provider %p, value %p.\n", statics, provider, value );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    raw_controller_klass_init( &impl->klass, RuntimeClass_Windows_Gaming_Input_RawGameController );

    TRACE( "created RawGameController %p\n", statics );

    *value = (IInspectable *)&impl->klass.IGameControllerImpl_iface;
    return S_OK;
}

static HRESULT raw_controller_statics_OnGameControllerAdded( struct raw_controller_statics *statics, IGameController *value )
{
    IRawGameController *controller;
    HRESULT hr;

    TRACE( "statics %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IRawGameController, (void **)&controller )))
        return hr;
    event_handlers_notify( &controller_added_handlers, (IInspectable *)controller );
    IRawGameController_Release( controller );

    return S_OK;
}

static HRESULT raw_controller_statics_OnGameControllerRemoved( struct raw_controller_statics *statics, IGameController *value )
{
    IRawGameController *controller;
    BOOLEAN found;
    UINT32 index;
    HRESULT hr;

    TRACE( "statics %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IRawGameController, (void **)&controller )))
        return hr;

    EnterCriticalSection( &controller_cs );
    if (SUCCEEDED(hr = init_controllers()))
    {
        if (FAILED(hr = IVector_RawGameController_IndexOf( controllers, controller, &index, &found )) || !found)
            WARN( "Could not find controller %p, hr %#lx!\n", controller, hr );
        else
            hr = IVector_RawGameController_RemoveAt( controllers, index );
    }
    LeaveCriticalSection( &controller_cs );

    if (FAILED(hr))
        WARN( "Failed to remove controller %p, hr %#lx!\n", controller, hr );
    else if (found)
    {
        TRACE( "Removed statics %p.\n", controller );
        event_handlers_notify( &controller_removed_handlers, (IInspectable *)controller );
    }
    IRawGameController_Release( controller );

    return S_OK;
}

static const struct raw_controller_statics_funcs raw_controller_statics_funcs = RAW_CONTROLLER_STATICS_FUNCS_INIT;
static struct raw_controller_statics raw_controller_statics = raw_controller_statics_default;
ICustomGameControllerFactory *controller_factory = &raw_controller_statics.ICustomGameControllerFactory_iface;
