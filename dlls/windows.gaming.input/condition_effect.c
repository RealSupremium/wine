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

WINE_DEFAULT_DEBUG_CHANNEL(input);

#define WIDL_impl_condition_effect
#define WIDL_impl_condition_factory
#include "classes_impl.h"

struct condition_effect
{
    struct condition_effect_klass klass;
    IWineForceFeedbackEffectImpl *IWineForceFeedbackEffectImpl_inner;
    ConditionForceEffectKind kind;
};

static struct condition_effect *condition_effect_from_klass( struct condition_effect_klass *klass )
{
    return CONTAINING_RECORD( klass, struct condition_effect, klass );
}

static HRESULT condition_effect_missing_interface( struct condition_effect *impl, REFIID iid, void **out )
{
    return IWineForceFeedbackEffectImpl_QueryInterface( impl->IWineForceFeedbackEffectImpl_inner, iid, out );
}

static void condition_effect_destroy( struct condition_effect *impl )
{
    IWineForceFeedbackEffectImpl_Release( impl->IWineForceFeedbackEffectImpl_inner );
    free( impl );
}

static HRESULT condition_effect_get_Kind( struct condition_effect *impl, ConditionForceEffectKind *kind )
{
    TRACE( "iface %p, kind %p.\n", impl, kind );
    *kind = impl->kind;
    return S_OK;
}

static HRESULT condition_effect_SetParameters( struct condition_effect *impl, Vector3 direction, FLOAT positive_coeff,
                                               FLOAT negative_coeff, FLOAT max_positive_magnitude,
                                               FLOAT max_negative_magnitude, FLOAT deadzone, FLOAT bias )
{
    WineForceFeedbackEffectParameters params =
    {
        .condition =
        {
            .type = WineForceFeedbackEffectType_Condition + impl->kind,
            .direction = direction,
            .positive_coeff = positive_coeff,
            .negative_coeff = negative_coeff,
            .max_positive_magnitude = max_positive_magnitude,
            .max_negative_magnitude = max_negative_magnitude,
            .deadzone = deadzone,
            .bias = bias,
        },
    };

    TRACE( "iface %p, direction %s, positive_coeff %f, negative_coeff %f, max_positive_magnitude %f, max_negative_magnitude %f, deadzone %f, bias %f.\n",
           impl, debugstr_vector3( &direction ), positive_coeff, negative_coeff, max_positive_magnitude, max_negative_magnitude, deadzone, bias );

    return IWineForceFeedbackEffectImpl_SetParameters( impl->IWineForceFeedbackEffectImpl_inner, params, NULL );
}

static const struct condition_effect_funcs condition_effect_funcs = CONDITION_EFFECT_FUNCS_INIT;

static HRESULT condition_factory_missing_interface( struct condition_factory *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void condition_factory_destroy( struct condition_factory *impl )
{
}

static HRESULT condition_factory_ActivateInstance( struct condition_factory *impl, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", impl, instance );
    return E_NOTIMPL;
}

static HRESULT condition_factory_CreateInstance( struct condition_factory *factory, enum ConditionForceEffectKind kind, IForceFeedbackEffect **out )
{
    enum WineForceFeedbackEffectType type = WineForceFeedbackEffectType_Condition + kind;
    struct condition_effect *impl;
    HRESULT hr;

    TRACE( "iface %p, kind %u, out %p.\n", factory, kind, out );

    if (!(impl = calloc( 1, sizeof(struct condition_effect) ))) return E_OUTOFMEMORY;
    condition_effect_klass_init( &impl->klass );
    impl->kind = kind;

    if (FAILED(hr = force_feedback_effect_create( type, (IInspectable *)&impl->klass.IConditionForceEffect_iface, &impl->IWineForceFeedbackEffectImpl_inner )) ||
        FAILED(hr = IConditionForceEffect_QueryInterface( &impl->klass.IConditionForceEffect_iface, &IID_IForceFeedbackEffect, (void **)out )))
    {
        if (impl->IWineForceFeedbackEffectImpl_inner) IWineForceFeedbackEffectImpl_Release( impl->IWineForceFeedbackEffectImpl_inner );
        free( impl );
        return hr;
    }

    IConditionForceEffect_Release( &impl->klass.IConditionForceEffect_iface );
    TRACE( "created ConditionForceEffect %p\n", *out );
    return S_OK;
}

static const struct condition_factory_funcs condition_factory_funcs = CONDITION_FACTORY_FUNCS_INIT;
static struct condition_factory condition_statics = condition_factory_default;
IInspectable *condition_effect_factory = (IInspectable *)&condition_statics.IActivationFactory_iface;
