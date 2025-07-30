/* WinRT Windows.Gaming.Input implementation
 *
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

#include "private.h"
#include "provider.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(input);

#define WIDL_impl_controller
#define WIDL_impl_manager_statics
#include "classes_impl.h"

static CRITICAL_SECTION manager_cs;
static CRITICAL_SECTION_DEBUG manager_cs_debug =
{
    0, 0, &manager_cs,
    { &manager_cs_debug.ProcessLocksList, &manager_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": manager_cs") }
};
static CRITICAL_SECTION manager_cs = { &manager_cs_debug, -1, 0, 0, 0, 0 };

static struct list controller_list = LIST_INIT( controller_list );

struct controller
{
    struct controller_klass klass;
    IInspectable *IInspectable_inner;

    struct list entry;
    IGameControllerProvider *provider;
    ICustomGameControllerFactory *factory;
};

static struct controller *controller_from_klass( struct controller_klass *klass )
{
    return CONTAINING_RECORD( klass, struct controller, klass );
}

static HRESULT controller_missing_interface( struct controller *impl, REFIID iid, void **out )
{
    return IInspectable_QueryInterface( impl->IInspectable_inner, iid, out );
}

static void controller_destroy( struct controller *impl )
{
    IInspectable_Release( impl->IInspectable_inner );
    ICustomGameControllerFactory_Release( impl->factory );
    IGameControllerProvider_Release( impl->provider );
    free( impl );
}

static HRESULT controller_add_HeadsetConnected( struct controller *impl, ITypedEventHandler_IGameController_Headset *handler,
                                                EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", impl, handler, token );
    return E_NOTIMPL;
}

static HRESULT controller_remove_HeadsetConnected( struct controller *impl, EventRegistrationToken token )
{
    FIXME( "iface %p, token %I64x stub!\n", impl, token.value );
    return E_NOTIMPL;
}

static HRESULT controller_add_HeadsetDisconnected( struct controller *impl, ITypedEventHandler_IGameController_Headset *handler,
                                                   EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", impl, handler, token );
    return E_NOTIMPL;
}

static HRESULT controller_remove_HeadsetDisconnected( struct controller *impl, EventRegistrationToken token )
{
    FIXME( "iface %p, token %I64x stub!\n", impl, token.value );
    return E_NOTIMPL;
}

static HRESULT controller_add_UserChanged( struct controller *impl,
                                           ITypedEventHandler_IGameController_UserChangedEventArgs *handler,
                                           EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", impl, handler, token );
    return E_NOTIMPL;
}

static HRESULT controller_remove_UserChanged( struct controller *impl, EventRegistrationToken token )
{
    FIXME( "iface %p, token %I64x stub!\n", impl, token.value );
    return E_NOTIMPL;
}

static HRESULT controller_get_Headset( struct controller *impl, IHeadset **value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT controller_get_IsWireless( struct controller *impl, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT controller_get_User( struct controller *impl, __x_ABI_CWindows_CSystem_CIUser **value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT controller_TryGetBatteryReport( struct controller *impl, IBatteryReport **value )
{
    FIXME( "iface %p, value %p stub!\n", impl, value );
    return E_NOTIMPL;
}

static HRESULT manager_statics_missing_interface( struct manager_statics *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void manager_statics_destroy( struct manager_statics *impl )
{
}

static HRESULT manager_statics_ActivateInstance( struct manager_statics *impl, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", impl, instance );
    return E_NOTIMPL;
}

static HRESULT manager_statics_RegisterCustomFactoryForGipInterface( struct manager_statics *impl,
                                                                     ICustomGameControllerFactory *factory, GUID interface_id )
{
    FIXME( "iface %p, factory %p, interface_id %s stub!\n", impl, factory, debugstr_guid( &interface_id ) );
    return E_NOTIMPL;
}

static HRESULT manager_statics_RegisterCustomFactoryForHardwareId( struct manager_statics *impl,
                                                                   ICustomGameControllerFactory *factory,
                                                                   UINT16 vendor_id, UINT16 product_id )
{
    FIXME( "iface %p, factory %p, vendor_id %u, product_id %u stub!\n", impl, factory, vendor_id, product_id );
    return E_NOTIMPL;
}

static HRESULT manager_statics_RegisterCustomFactoryForXusbType( struct manager_statics *impl,
                                                                 ICustomGameControllerFactory *factory,
                                                                 XusbDeviceType type, XusbDeviceSubtype subtype )
{
    FIXME( "iface %p, factory %p, type %d, subtype %d stub!\n", impl, factory, type, subtype );
    return E_NOTIMPL;
}

static HRESULT manager_statics_TryGetFactoryControllerFromGameController( struct manager_statics *impl,
                                                                          ICustomGameControllerFactory *factory,
                                                                          IGameController *controller, IGameController **value )
{
    struct controller *entry, *other;
    IGameController *tmp_controller;
    BOOL found = FALSE;

    TRACE( "iface %p, factory %p, controller %p, value %p.\n", impl, factory, controller, value );

    /* Spider Man Remastered passes a IRawGameController instead of IGameController, query the iface again */
    if (FAILED(IGameController_QueryInterface( controller, &IID_IGameController, (void **)&tmp_controller ))) goto done;

    EnterCriticalSection( &manager_cs );

    LIST_FOR_EACH_ENTRY( entry, &controller_list, struct controller, entry )
        if ((found = &entry->klass.IGameController_iface == tmp_controller)) break;

    if (!found) WARN( "Failed to find controller %p\n", controller );
    else
    {
        LIST_FOR_EACH_ENTRY( other, &controller_list, struct controller, entry )
            if ((found = entry->provider == other->provider && other->factory == factory)) break;
        if (!found) WARN( "Failed to find controller %p, factory %p\n", controller, factory );
        else IGameController_AddRef( (*value = &other->klass.IGameController_iface) );
    }

    LeaveCriticalSection( &manager_cs );

    IGameController_Release( tmp_controller );

done:
    if (!found) *value = NULL;
    return S_OK;
}

static const struct controller_funcs controller_funcs = CONTROLLER_FUNCS_INIT;

static const struct manager_statics_funcs manager_statics_funcs = MANAGER_STATICS_FUNCS_INIT;
static struct manager_statics manager_statics = manager_statics_default;
IGameControllerFactoryManagerStatics2 *manager_factory = &manager_statics.IGameControllerFactoryManagerStatics2_iface;

static HRESULT controller_create( ICustomGameControllerFactory *factory, IGameControllerProvider *provider,
                                  struct controller **out )
{
    IGameControllerImpl *inner_impl;
    struct controller *impl;
    HRESULT hr;

    if (!(impl = malloc(sizeof(*impl)))) return E_OUTOFMEMORY;
    controller_klass_init( &impl->klass );

    if (FAILED(hr = ICustomGameControllerFactory_CreateGameController( factory, provider, &impl->IInspectable_inner )))
        WARN( "Failed to create game controller, hr %#lx\n", hr );
    else if (FAILED(hr = IInspectable_QueryInterface( impl->IInspectable_inner, &IID_IGameControllerImpl, (void **)&inner_impl )))
        WARN( "Failed to find IGameControllerImpl iface, hr %#lx\n", hr );
    else
    {
        if (FAILED(hr = IGameControllerImpl_Initialize( inner_impl, &impl->klass.IGameController_iface, provider )))
            WARN( "Failed to initialize game controller, hr %#lx\n", hr );
        IGameControllerImpl_Release( inner_impl );
    }

    if (FAILED(hr))
    {
        if (impl->IInspectable_inner) IInspectable_Release( impl->IInspectable_inner );
        free( impl );
        return hr;
    }

    ICustomGameControllerFactory_AddRef( (impl->factory = factory) );
    IGameControllerProvider_AddRef( (impl->provider = provider) );

    *out = impl;
    return S_OK;
}

void manager_on_provider_created( IGameControllerProvider *provider )
{
    IWineGameControllerProvider *wine_provider;
    struct list *entry, *next, *list;
    struct controller *controller;
    WineGameControllerType type;
    HRESULT hr;

    TRACE( "provider %p\n", provider );

    if (FAILED(IGameControllerProvider_QueryInterface( provider, &IID_IWineGameControllerProvider,
                                                       (void **)&wine_provider )))
    {
        FIXME( "IWineGameControllerProvider isn't implemented by provider %p\n", provider );
        return;
    }
    if (FAILED(hr = IWineGameControllerProvider_get_Type( wine_provider, &type )))
    {
        WARN( "Failed to get controller type, hr %#lx\n", hr );
        type = WineGameControllerType_Joystick;
    }
    IWineGameControllerProvider_Release( wine_provider );

    EnterCriticalSection( &manager_cs );

    if (list_empty( &controller_list )) list = &controller_list;
    else list = list_tail( &controller_list );

    if (SUCCEEDED(controller_create( controller_factory, provider, &controller )))
        list_add_tail( &controller_list, &controller->entry );

    switch (type)
    {
    case WineGameControllerType_Joystick: break;
    case WineGameControllerType_Gamepad:
        if (SUCCEEDED(controller_create( gamepad_factory, provider, &controller )))
            list_add_tail( &controller_list, &controller->entry );
        break;
    case WineGameControllerType_RacingWheel:
        if (SUCCEEDED(controller_create( racing_wheel_factory, provider, &controller )))
            list_add_tail( &controller_list, &controller->entry );
        break;
    }

    LIST_FOR_EACH_SAFE( entry, next, list )
    {
        controller = LIST_ENTRY( entry, struct controller, entry );
        hr = ICustomGameControllerFactory_OnGameControllerAdded( controller->factory,
                                                                 &controller->klass.IGameController_iface );
        if (FAILED(hr)) WARN( "OnGameControllerAdded failed, hr %#lx\n", hr );
        if (next == &controller_list) break;
    }

    LeaveCriticalSection( &manager_cs );
}

void manager_on_provider_removed( IGameControllerProvider *provider )
{
    struct controller *controller, *next;

    TRACE( "provider %p\n", provider );

    EnterCriticalSection( &manager_cs );

    LIST_FOR_EACH_ENTRY( controller, &controller_list, struct controller, entry )
    {
        if (controller->provider != provider) continue;
        ICustomGameControllerFactory_OnGameControllerRemoved( controller->factory,
                                                              &controller->klass.IGameController_iface );
    }

    LIST_FOR_EACH_ENTRY_SAFE( controller, next, &controller_list, struct controller, entry )
    {
        if (controller->provider != provider) continue;
        list_remove( &controller->entry );
        IGameController_Release( &controller->klass.IGameController_iface );
    }

    LeaveCriticalSection( &manager_cs );
}
