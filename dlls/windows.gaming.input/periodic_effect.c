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

#define WIDL_impl_periodic_effect
#define WIDL_impl_periodic_factory
#include "classes_impl.h"

struct periodic_effect
{
    struct periodic_effect_klass klass;
    IWineForceFeedbackEffectImpl *IWineForceFeedbackEffectImpl_inner;
    PeriodicForceEffectKind kind;
};

static struct periodic_effect *periodic_effect_from_klass( struct periodic_effect_klass *klass )
{
    return CONTAINING_RECORD( klass, struct periodic_effect, klass );
}

static HRESULT periodic_effect_missing_interface( struct periodic_effect *impl, REFIID iid, void **out )
{
    return IWineForceFeedbackEffectImpl_QueryInterface( impl->IWineForceFeedbackEffectImpl_inner, iid, out );
}

static void periodic_effect_destroy( struct periodic_effect *impl )
{
    IWineForceFeedbackEffectImpl_Release( impl->IWineForceFeedbackEffectImpl_inner );
    free( impl );
}

static HRESULT periodic_effect_get_Kind( struct periodic_effect *impl, PeriodicForceEffectKind *kind )
{
    TRACE( "iface %p, kind %p.\n", impl, kind );
    *kind = impl->kind;
    return S_OK;
}

static HRESULT periodic_effect_SetParameters( struct periodic_effect *impl, Vector3 direction,
                                              FLOAT frequency, FLOAT phase, FLOAT bias, TimeSpan duration )
{
    WineForceFeedbackEffectParameters params =
    {
        .periodic =
        {
            .type = WineForceFeedbackEffectType_Periodic_SquareWave + impl->kind,
            .direction = direction,
            .frequency = frequency,
            .phase = phase,
            .bias = bias,
            .duration = duration,
            .repeat_count = 1,
            .gain = 1.,
        },
    };

    TRACE( "iface %p, direction %s, frequency %f, phase %f, bias %f, duration %I64u.\n", impl,
           debugstr_vector3( &direction ), frequency, phase, bias, duration.Duration );

    return IWineForceFeedbackEffectImpl_SetParameters( impl->IWineForceFeedbackEffectImpl_inner, params, NULL );
}

static HRESULT periodic_effect_SetParametersWithEnvelope( struct periodic_effect *impl, Vector3 direction, FLOAT frequency, FLOAT phase, FLOAT bias,
                                                        FLOAT attack_gain, FLOAT sustain_gain, FLOAT release_gain, TimeSpan start_delay,
                                                        TimeSpan attack_duration, TimeSpan sustain_duration,
                                                        TimeSpan release_duration, UINT32 repeat_count )
{
    WineForceFeedbackEffectParameters params =
    {
        .periodic =
        {
            .type = WineForceFeedbackEffectType_Periodic_SquareWave + impl->kind,
            .direction = direction,
            .frequency = frequency,
            .phase = phase,
            .bias = bias,
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

    TRACE( "iface %p, direction %s, frequency %f, phase %f, bias %f, attack_gain %f, sustain_gain %f, release_gain %f, start_delay %I64u, "
           "attack_duration %I64u, sustain_duration %I64u, release_duration %I64u, repeat_count %u.\n", impl, debugstr_vector3( &direction ),
           frequency, phase, bias, attack_gain, sustain_gain, release_gain, start_delay.Duration, attack_duration.Duration, sustain_duration.Duration,
           release_duration.Duration, repeat_count );

    return IWineForceFeedbackEffectImpl_SetParameters( impl->IWineForceFeedbackEffectImpl_inner, params, &envelope );
}

static const struct periodic_effect_funcs periodic_effect_funcs = PERIODIC_EFFECT_FUNCS_INIT;

static HRESULT periodic_factory_missing_interface( struct periodic_factory *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void periodic_factory_destroy( struct periodic_factory *impl )
{
}

static HRESULT periodic_factory_ActivateInstance( struct periodic_factory *impl, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", impl, instance );
    return E_NOTIMPL;
}

static HRESULT periodic_factory_CreateInstance( struct periodic_factory *factory, enum PeriodicForceEffectKind kind, IForceFeedbackEffect **out )
{
    enum WineForceFeedbackEffectType type = WineForceFeedbackEffectType_Periodic + kind;
    struct periodic_effect *impl;
    HRESULT hr;

    TRACE( "iface %p, kind %u, out %p.\n", factory, kind, out );

    if (!(impl = calloc( 1, sizeof(struct periodic_effect) ))) return E_OUTOFMEMORY;
    periodic_effect_klass_init( &impl->klass );
    impl->kind = kind;

    if (FAILED(hr = force_feedback_effect_create( type, (IInspectable *)&impl->klass.IPeriodicForceEffect_iface, &impl->IWineForceFeedbackEffectImpl_inner )) ||
        FAILED(hr = IPeriodicForceEffect_QueryInterface( &impl->klass.IPeriodicForceEffect_iface, &IID_IForceFeedbackEffect, (void **)out )))
    {
        if (impl->IWineForceFeedbackEffectImpl_inner) IWineForceFeedbackEffectImpl_Release( impl->IWineForceFeedbackEffectImpl_inner );
        free( impl );
        return hr;
    }

    IPeriodicForceEffect_Release( &impl->klass.IPeriodicForceEffect_iface );
    TRACE( "created PeriodicForceEffect %p\n", *out );
    return S_OK;
}

static const struct periodic_factory_funcs periodic_factory_funcs = PERIODIC_FACTORY_FUNCS_INIT;
static struct periodic_factory periodic_statics = periodic_factory_default;
IInspectable *periodic_effect_factory = (IInspectable *)&periodic_statics.IActivationFactory_iface;
