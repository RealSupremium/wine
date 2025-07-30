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

#define WIDL_impl_constant_effect
#define WIDL_impl_constant_factory
#include "classes_impl.h"

struct constant_effect
{
    struct constant_effect_klass klass;
    IWineForceFeedbackEffectImpl *IWineForceFeedbackEffectImpl_inner;
    LONG ref;
};

static struct constant_effect *constant_effect_from_klass( struct constant_effect_klass *klass )
{
    return CONTAINING_RECORD( klass, struct constant_effect, klass );
}

static HRESULT constant_effect_missing_interface( struct constant_effect *impl, REFIID iid, void **out )
{
    return IWineForceFeedbackEffectImpl_QueryInterface( impl->IWineForceFeedbackEffectImpl_inner, iid, out );
}

static void constant_effect_destroy( struct constant_effect *impl )
{
    IWineForceFeedbackEffectImpl_Release( impl->IWineForceFeedbackEffectImpl_inner );
    free( impl );
}

static HRESULT constant_effect_SetParameters( struct constant_effect *impl, Vector3 direction, TimeSpan duration )
{
    WineForceFeedbackEffectParameters params =
    {
        .constant =
        {
            .type = WineForceFeedbackEffectType_Constant,
            .direction = direction,
            .duration = duration,
            .repeat_count = 1,
            .gain = 1.,
        },
    };

    TRACE( "iface %p, direction %s, duration %I64u.\n", impl, debugstr_vector3( &direction ), duration.Duration );

    return IWineForceFeedbackEffectImpl_SetParameters( impl->IWineForceFeedbackEffectImpl_inner, params, NULL );
}

static HRESULT constant_effect_SetParametersWithEnvelope( struct constant_effect *impl, Vector3 direction, FLOAT attack_gain,
                                                          FLOAT sustain_gain, FLOAT release_gain, TimeSpan start_delay,
                                                          TimeSpan attack_duration, TimeSpan sustain_duration,
                                                          TimeSpan release_duration, UINT32 repeat_count )
{
    WineForceFeedbackEffectParameters params =
    {
        .constant =
        {
            .type = WineForceFeedbackEffectType_Constant,
            .direction = direction,
            .duration = {attack_duration.Duration + sustain_duration.Duration + release_duration.Duration},
            .start_delay = start_delay,
            .repeat_count = repeat_count,
            .gain = sustain_gain,
        },
    };
    WineForceFeedbackEffectEnvelope envelope =
    {
        .attack_gain = attack_gain,
        .release_gain = release_gain,
        .attack_duration = attack_duration,
        .release_duration = release_duration,
    };

    TRACE( "iface %p, direction %s, attack_gain %f, sustain_gain %f, release_gain %f, start_delay %I64u, attack_duration %I64u, "
           "sustain_duration %I64u, release_duration %I64u, repeat_count %u.\n", impl, debugstr_vector3( &direction ),
           attack_gain, sustain_gain, release_gain, start_delay.Duration, attack_duration.Duration, sustain_duration.Duration,
           release_duration.Duration, repeat_count );

    return IWineForceFeedbackEffectImpl_SetParameters( impl->IWineForceFeedbackEffectImpl_inner, params, &envelope );
}

static const struct constant_effect_funcs constant_effect_funcs = CONSTANT_EFFECT_FUNCS_INIT;

static HRESULT constant_factory_missing_interface( struct constant_factory *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void constant_factory_destroy( struct constant_factory *impl )
{
}

static HRESULT constant_factory_ActivateInstance( struct constant_factory *factory, IInspectable **instance )
{
    struct constant_effect *impl;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", factory, instance );

    if (!(impl = calloc( 1, sizeof(struct constant_effect) ))) return E_OUTOFMEMORY;
    constant_effect_klass_init( &impl->klass );

    if (FAILED(hr = force_feedback_effect_create( WineForceFeedbackEffectType_Constant, (IInspectable *)&impl->klass.IConstantForceEffect_iface,
                                                  &impl->IWineForceFeedbackEffectImpl_inner )))
    {
        free( impl );
        return hr;
    }

    *instance = (IInspectable *)&impl->klass.IConstantForceEffect_iface;
    TRACE( "created ConstantForceEffect %p\n", *instance );
    return S_OK;
}

static const struct constant_factory_funcs constant_factory_funcs = CONSTANT_FACTORY_FUNCS_INIT;
static struct constant_factory constant_statics = constant_factory_default;
IInspectable *constant_effect_factory = (IInspectable *)&constant_statics.IActivationFactory_iface;
