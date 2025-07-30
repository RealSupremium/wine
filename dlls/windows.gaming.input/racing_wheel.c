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

#define WIDL_impl_racing_wheel
#define WIDL_impl_racing_wheel_statics
#include "classes_impl.h"

static CRITICAL_SECTION racing_wheel_cs;
static CRITICAL_SECTION_DEBUG racing_wheel_cs_debug =
{
    0, 0, &racing_wheel_cs,
    { &racing_wheel_cs_debug.ProcessLocksList, &racing_wheel_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": racing_wheel_cs") }
};
static CRITICAL_SECTION racing_wheel_cs = { &racing_wheel_cs_debug, -1, 0, 0, 0, 0 };

static IVector_RacingWheel *racing_wheels;
static struct list racing_wheel_added_handlers = LIST_INIT( racing_wheel_added_handlers );
static struct list racing_wheel_removed_handlers = LIST_INIT( racing_wheel_removed_handlers );

static HRESULT init_racing_wheels(void)
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_RacingWheel,
        .view = &IID_IVectorView_RacingWheel,
        .iterable = &IID_IIterable_RacingWheel,
        .iterator = &IID_IIterator_RacingWheel,
    };
    HRESULT hr;

    EnterCriticalSection( &racing_wheel_cs );
    if (racing_wheels) hr = S_OK;
    else hr = vector_create( &iids, (void **)&racing_wheels );
    LeaveCriticalSection( &racing_wheel_cs );

    return hr;
}

struct racing_wheel
{
    struct racing_wheel_klass klass;
    IGameControllerProvider *provider;
    IWineGameControllerProvider *wine_provider;
};

static struct racing_wheel *racing_wheel_from_klass( struct racing_wheel_klass *klass )
{
    return CONTAINING_RECORD( klass, struct racing_wheel, klass );
}

static HRESULT racing_wheel_missing_interface( struct racing_wheel *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void racing_wheel_destroy( struct racing_wheel *impl )
{
    if (impl->wine_provider) IWineGameControllerProvider_Release( impl->wine_provider );
    IGameControllerProvider_Release( impl->provider );
    free( impl );
}

static HRESULT racing_wheel_Initialize( struct racing_wheel *impl, IGameController *outer, IGameControllerProvider *provider )
{
    HRESULT hr;

    TRACE( "iface %p, outer %p, provider %p.\n", impl, outer, provider );

    impl->klass.IGameController_outer = outer;
    IGameControllerProvider_AddRef( (impl->provider = provider) );

    hr = IGameControllerProvider_QueryInterface( provider, &IID_IWineGameControllerProvider,
                                                 (void **)&impl->wine_provider );
    if (FAILED(hr)) return hr;

    EnterCriticalSection( &racing_wheel_cs );
    if (SUCCEEDED(hr = init_racing_wheels()))
        hr = IVector_RacingWheel_Append( racing_wheels, &impl->klass.IRacingWheel_iface );
    LeaveCriticalSection( &racing_wheel_cs );

    return hr;
}

static HRESULT racing_wheel_OnInputResumed( struct racing_wheel *impl, UINT64 timestamp )
{
    FIXME( "iface %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_OnInputSuspended( struct racing_wheel *impl, UINT64 timestamp )
{
    FIXME( "iface %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_HasClutch( struct racing_wheel *impl, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_HasHandbrake( struct racing_wheel *impl, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_HasPatternShifter( struct racing_wheel *impl, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_MaxPatternShifterGear( struct racing_wheel *impl, INT32 *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_MaxWheelAngle( struct racing_wheel *impl, DOUBLE *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_get_WheelMotor( struct racing_wheel *impl, IForceFeedbackMotor **value )
{
    TRACE( "iface %p, value %p\n", impl, value );

    return IWineGameControllerProvider_get_ForceFeedbackMotor( impl->wine_provider, value );
}

static HRESULT racing_wheel_GetButtonLabel( struct racing_wheel *impl, enum RacingWheelButtons button,
                                            enum GameControllerButtonLabel *value )
{
    FIXME( "iface %p, button %d, value %p stub!\n", impl, button, value );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_GetCurrentReading( struct racing_wheel *impl, struct RacingWheelReading *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static const struct racing_wheel_funcs racing_wheel_funcs = RACING_WHEEL_FUNCS_INIT;

static HRESULT racing_wheel_statics_missing_interface( struct racing_wheel_statics *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void racing_wheel_statics_destroy( struct racing_wheel_statics *impl )
{
}

static HRESULT racing_wheel_statics_ActivateInstance( struct racing_wheel_statics *impl, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", impl, instance );
    return E_NOTIMPL;
}

static HRESULT racing_wheel_statics_add_RacingWheelAdded( struct racing_wheel_statics *impl, IEventHandler_RacingWheel *handler,
                                                          EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", impl, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &racing_wheel_added_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT racing_wheel_statics_remove_RacingWheelAdded( struct racing_wheel_statics *impl, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", impl, token.value );
    return event_handlers_remove( &racing_wheel_added_handlers, &token );
}

static HRESULT racing_wheel_statics_add_RacingWheelRemoved( struct racing_wheel_statics *impl, IEventHandler_RacingWheel *handler,
                                                            EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", impl, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &racing_wheel_removed_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT racing_wheel_statics_remove_RacingWheelRemoved( struct racing_wheel_statics *impl, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", impl, token.value );
    return event_handlers_remove( &racing_wheel_removed_handlers, &token );
}

static HRESULT racing_wheel_statics_get_RacingWheels( struct racing_wheel_statics *impl, IVectorView_RacingWheel **value )
{
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", impl, value );

    EnterCriticalSection( &racing_wheel_cs );
    if (SUCCEEDED(hr = init_racing_wheels())) hr = IVector_RacingWheel_GetView( racing_wheels, value );
    LeaveCriticalSection( &racing_wheel_cs );

    return hr;
}

static HRESULT racing_wheel_statics_FromGameController( struct racing_wheel_statics *impl, IGameController *game_controller, IRacingWheel **value )
{
    IGameController *controller;
    HRESULT hr;

    TRACE( "iface %p, game_controller %p, value %p.\n", impl, game_controller, value );

    *value = NULL;
    hr = IGameControllerFactoryManagerStatics2_TryGetFactoryControllerFromGameController( manager_factory, &impl->ICustomGameControllerFactory_iface,
                                                                                          game_controller, &controller );
    if (FAILED(hr) || !controller) return hr;

    hr = IGameController_QueryInterface( controller, &IID_IRacingWheel, (void **)value );
    IGameController_Release( controller );
    return hr;
}

static HRESULT racing_wheel_statics_CreateGameController( struct racing_wheel_statics *statics, IGameControllerProvider *provider,
                                                          IInspectable **value )
{
    struct racing_wheel *impl;

    TRACE( "iface %p, provider %p, value %p.\n", statics, provider, value );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    racing_wheel_klass_init( &impl->klass, RuntimeClass_Windows_Gaming_Input_RacingWheel );

    TRACE( "created RacingWheel %p\n", impl );

    *value = (IInspectable *)&impl->klass.IGameControllerImpl_iface;
    return S_OK;
}

static HRESULT racing_wheel_statics_OnGameControllerAdded( struct racing_wheel_statics *statics, IGameController *value )
{
    IRacingWheel *racing_wheel;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IRacingWheel, (void **)&racing_wheel )))
        return hr;
    event_handlers_notify( &racing_wheel_added_handlers, (IInspectable *)racing_wheel );
    IRacingWheel_Release( racing_wheel );

    return S_OK;
}

static HRESULT racing_wheel_statics_OnGameControllerRemoved( struct racing_wheel_statics *statics, IGameController *value )
{
    IRacingWheel *racing_wheel;
    BOOLEAN found;
    UINT32 index;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IRacingWheel, (void **)&racing_wheel )))
        return hr;

    EnterCriticalSection( &racing_wheel_cs );
    if (SUCCEEDED(hr = init_racing_wheels()))
    {
        if (FAILED(hr = IVector_RacingWheel_IndexOf( racing_wheels, racing_wheel, &index, &found )) || !found)
            WARN( "Could not find RacingWheel %p, hr %#lx!\n", racing_wheel, hr );
        else
            hr = IVector_RacingWheel_RemoveAt( racing_wheels, index );
    }
    LeaveCriticalSection( &racing_wheel_cs );

    if (FAILED(hr))
        WARN( "Failed to remove RacingWheel %p, hr %#lx!\n", racing_wheel, hr );
    else if (found)
    {
        TRACE( "Removed RacingWheel %p.\n", racing_wheel );
        event_handlers_notify( &racing_wheel_removed_handlers, (IInspectable *)racing_wheel );
    }
    IRacingWheel_Release( racing_wheel );

    return S_OK;
}

static const struct racing_wheel_statics_funcs racing_wheel_statics_funcs = RACING_WHEEL_STATICS_FUNCS_INIT;
static struct racing_wheel_statics racing_wheel_statics = racing_wheel_statics_default;
ICustomGameControllerFactory *racing_wheel_factory = &racing_wheel_statics.ICustomGameControllerFactory_iface;
