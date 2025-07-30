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

#define WIDL_impl_gamepad
#define WIDL_impl_gamepad_statics
#include "classes_impl.h"

static CRITICAL_SECTION gamepad_cs;
static CRITICAL_SECTION_DEBUG gamepad_cs_debug =
{
    0, 0, &gamepad_cs,
    { &gamepad_cs_debug.ProcessLocksList, &gamepad_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": gamepad_cs") }
};
static CRITICAL_SECTION gamepad_cs = { &gamepad_cs_debug, -1, 0, 0, 0, 0 };

static IVector_Gamepad *gamepads;
static struct list gamepad_added_handlers = LIST_INIT( gamepad_added_handlers );
static struct list gamepad_removed_handlers = LIST_INIT( gamepad_removed_handlers );

static HRESULT init_gamepads(void)
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_Gamepad,
        .view = &IID_IVectorView_Gamepad,
        .iterable = &IID_IIterable_Gamepad,
        .iterator = &IID_IIterator_Gamepad,
    };
    HRESULT hr;

    EnterCriticalSection( &gamepad_cs );
    if (gamepads) hr = S_OK;
    else hr = vector_create( &iids, (void **)&gamepads );
    LeaveCriticalSection( &gamepad_cs );

    return hr;
}

struct gamepad
{
    struct gamepad_klass klass;
    IGameControllerProvider *provider;
    IWineGameControllerProvider *wine_provider;

    struct WineGameControllerState initial_state;
    BOOL state_changed;
};

static struct gamepad *gamepad_from_klass( struct gamepad_klass *klass )
{
    return CONTAINING_RECORD( klass, struct gamepad, klass );
}

static HRESULT gamepad_missing_interface( struct gamepad *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void gamepad_destroy( struct gamepad *impl )
{
    if (impl->wine_provider) IWineGameControllerProvider_Release( impl->wine_provider );
    IGameControllerProvider_Release( impl->provider );
    free( impl );
}

static HRESULT gamepad_Initialize( struct gamepad *impl, IGameController *outer, IGameControllerProvider *provider )
{
    HRESULT hr;

    TRACE( "iface %p, outer %p, provider %p.\n", impl, outer, provider );

    impl->klass.IGameController_outer = outer;
    IGameControllerProvider_AddRef( (impl->provider = provider) );

    hr = IGameControllerProvider_QueryInterface( provider, &IID_IWineGameControllerProvider,
                                                 (void **)&impl->wine_provider );

    if (SUCCEEDED(hr))
        hr = IWineGameControllerProvider_get_State( impl->wine_provider, &impl->initial_state );

    if (FAILED(hr)) return hr;

    EnterCriticalSection( &gamepad_cs );
    if (SUCCEEDED(hr = init_gamepads())) hr = IVector_Gamepad_Append( gamepads, &impl->klass.IGamepad_iface );
    LeaveCriticalSection( &gamepad_cs );

    return hr;
}

static HRESULT gamepad_OnInputResumed( struct gamepad *impl, UINT64 timestamp )
{
    FIXME( "iface %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT gamepad_OnInputSuspended( struct gamepad *impl, UINT64 timestamp )
{
    FIXME( "iface %p, timestamp %I64u stub!\n", impl, timestamp );
    return E_NOTIMPL;
}

static HRESULT gamepad_get_Vibration( struct gamepad *impl, struct GamepadVibration *value )
{
    struct WineGameControllerVibration vibration;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", impl, value );

    if (FAILED(hr = IWineGameControllerProvider_get_Vibration( impl->wine_provider, &vibration ))) return hr;

    value->LeftMotor = vibration.rumble / 65535.;
    value->RightMotor = vibration.buzz / 65535.;
    value->LeftTrigger = vibration.left / 65535.;
    value->RightTrigger = vibration.right / 65535.;

    return S_OK;
}

static HRESULT gamepad_put_Vibration( struct gamepad *impl, struct GamepadVibration value )
{
    struct WineGameControllerVibration vibration =
    {
        .rumble = value.LeftMotor * 65535.,
        .buzz = value.RightMotor * 65535.,
        .left = value.LeftTrigger * 65535.,
        .right = value.RightTrigger * 65535.,
    };

    TRACE( "iface %p, value %p.\n", impl, &value );

    return IWineGameControllerProvider_put_Vibration( impl->wine_provider, vibration );
}

static HRESULT gamepad_GetCurrentReading( struct gamepad *impl, struct GamepadReading *value )
{
    struct WineGameControllerState state;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", impl, value );

    if (FAILED(hr = IWineGameControllerProvider_get_State( impl->wine_provider, &state ))) return hr;

    memset(value, 0, sizeof(*value));
    if (impl->state_changed ||
        memcmp( impl->initial_state.axes, state.axes, sizeof(state) - offsetof(struct WineGameControllerState, axes)) )
    {
        impl->state_changed = TRUE;
        if (state.buttons[0]) value->Buttons |= GamepadButtons_A;
        if (state.buttons[1]) value->Buttons |= GamepadButtons_B;
        if (state.buttons[2]) value->Buttons |= GamepadButtons_X;
        if (state.buttons[3]) value->Buttons |= GamepadButtons_Y;
        if (state.buttons[4]) value->Buttons |= GamepadButtons_LeftShoulder;
        if (state.buttons[5]) value->Buttons |= GamepadButtons_RightShoulder;
        if (state.buttons[6]) value->Buttons |= GamepadButtons_View;
        if (state.buttons[7]) value->Buttons |= GamepadButtons_Menu;
        if (state.buttons[8]) value->Buttons |= GamepadButtons_LeftThumbstick;
        if (state.buttons[9]) value->Buttons |= GamepadButtons_RightThumbstick;

        switch (state.switches[0])
        {
        case GameControllerSwitchPosition_Up:
        case GameControllerSwitchPosition_UpRight:
        case GameControllerSwitchPosition_UpLeft:
            value->Buttons |= GamepadButtons_DPadUp;
            break;
        case GameControllerSwitchPosition_Down:
        case GameControllerSwitchPosition_DownRight:
        case GameControllerSwitchPosition_DownLeft:
            value->Buttons |= GamepadButtons_DPadDown;
            break;
        default:
            break;
        }

        switch (state.switches[0])
        {
        case GameControllerSwitchPosition_Right:
        case GameControllerSwitchPosition_UpRight:
        case GameControllerSwitchPosition_DownRight:
            value->Buttons |= GamepadButtons_DPadRight;
            break;
        case GameControllerSwitchPosition_Left:
        case GameControllerSwitchPosition_UpLeft:
        case GameControllerSwitchPosition_DownLeft:
            value->Buttons |= GamepadButtons_DPadLeft;
            break;
        default:
            break;
        }

        value->LeftThumbstickX = 2. * state.axes[0] - 1.;
        value->LeftThumbstickY = 1. - 2. * state.axes[1];
        value->LeftTrigger = state.axes[2];
        value->RightThumbstickX = 2. * state.axes[3] - 1.;
        value->RightThumbstickY = 1. - 2. * state.axes[4];
        value->RightTrigger = state.axes[5];

        value->Timestamp = state.timestamp;
    }

    return hr;
}

static HRESULT gamepad_GetButtonLabel( struct gamepad *impl, GamepadButtons button, GameControllerButtonLabel *value )
{
    FIXME( "iface %p, button %#x, value %p stub!\n", impl, button, value );
    *value = GameControllerButtonLabel_None;
    return S_OK;
}

static const struct gamepad_funcs gamepad_funcs = GAMEPAD_FUNCS_INIT;

static HRESULT gamepad_statics_missing_interface( struct gamepad_statics *iface, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void gamepad_statics_destroy( struct gamepad_statics *impl )
{
}

static HRESULT gamepad_statics_ActivateInstance( struct gamepad_statics *impl, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", impl, instance );
    return E_NOTIMPL;
}

static HRESULT gamepad_statics_add_GamepadAdded( struct gamepad_statics *impl, IEventHandler_Gamepad *handler,
                                                 EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", impl, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &gamepad_added_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT gamepad_statics_remove_GamepadAdded( struct gamepad_statics *impl, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", impl, token.value );
    return event_handlers_remove( &gamepad_added_handlers, &token );
}

static HRESULT gamepad_statics_add_GamepadRemoved( struct gamepad_statics *impl, IEventHandler_Gamepad *handler,
                                                   EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", impl, handler, token );
    if (!handler) return E_INVALIDARG;
    return event_handlers_append( &gamepad_removed_handlers, (IEventHandler_IInspectable *)handler, token );
}

static HRESULT gamepad_statics_remove_GamepadRemoved( struct gamepad_statics *impl, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", impl, token.value );
    return event_handlers_remove( &gamepad_removed_handlers, &token );
}

static HRESULT gamepad_statics_get_Gamepads( struct gamepad_statics *impl, IVectorView_Gamepad **value )
{
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", impl, value );

    EnterCriticalSection( &gamepad_cs );
    if (SUCCEEDED(hr = init_gamepads()))
        hr = IVector_Gamepad_GetView( gamepads, value );
    LeaveCriticalSection( &gamepad_cs );

    return hr;
}

static HRESULT gamepad_statics_FromGameController( struct gamepad_statics *impl, IGameController *game_controller, IGamepad **value )
{
    IGameController *controller;
    HRESULT hr;

    TRACE( "iface %p, game_controller %p, value %p.\n", impl, game_controller, value );

    *value = NULL;
    hr = IGameControllerFactoryManagerStatics2_TryGetFactoryControllerFromGameController( manager_factory, &impl->ICustomGameControllerFactory_iface,
                                                                                          game_controller, &controller );
    if (FAILED(hr) || !controller) return hr;

    hr = IGameController_QueryInterface( controller, &IID_IGamepad, (void **)value );
    IGameController_Release( controller );
    return hr;
}

static HRESULT gamepad_statics_CreateGameController( struct gamepad_statics *statics, IGameControllerProvider *provider,
                                                     IInspectable **value )
{
    struct gamepad *impl;

    TRACE( "iface %p, provider %p, value %p.\n", statics, provider, value );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    gamepad_klass_init( &impl->klass, RuntimeClass_Windows_Gaming_Input_Gamepad );

    TRACE( "created Gamepad %p\n", impl );

    *value = (IInspectable *)&impl->klass.IGameControllerImpl_iface;
    return S_OK;
}

static HRESULT gamepad_statics_OnGameControllerAdded( struct gamepad_statics *statics, IGameController *value )
{
    IGamepad *gamepad;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IGamepad, (void **)&gamepad )))
        return hr;
    event_handlers_notify( &gamepad_added_handlers, (IInspectable *)gamepad );
    IGamepad_Release( gamepad );

    return S_OK;
}

static HRESULT gamepad_statics_OnGameControllerRemoved( struct gamepad_statics *statics, IGameController *value )
{
    IGamepad *gamepad;
    BOOLEAN found;
    UINT32 index;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", statics, value );

    if (FAILED(hr = IGameController_QueryInterface( value, &IID_IGamepad, (void **)&gamepad )))
        return hr;

    EnterCriticalSection( &gamepad_cs );
    if (SUCCEEDED(hr = init_gamepads()))
    {
        if (FAILED(hr = IVector_Gamepad_IndexOf( gamepads, gamepad, &index, &found )) || !found)
            WARN( "Could not find gamepad %p, hr %#lx!\n", gamepad, hr );
        else
            hr = IVector_Gamepad_RemoveAt( gamepads, index );
    }
    LeaveCriticalSection( &gamepad_cs );

    if (FAILED(hr))
        WARN( "Failed to remove gamepad %p, hr %#lx!\n", gamepad, hr );
    else if (found)
    {
        TRACE( "Removed gamepad %p.\n", gamepad );
        event_handlers_notify( &gamepad_removed_handlers, (IInspectable *)gamepad );
    }
    IGamepad_Release( gamepad );

    return S_OK;
}

static const struct gamepad_statics_funcs gamepad_statics_funcs = GAMEPAD_STATICS_FUNCS_INIT;
static struct gamepad_statics gamepad_statics = gamepad_statics_default;
ICustomGameControllerFactory *gamepad_factory = &gamepad_statics.ICustomGameControllerFactory_iface;
