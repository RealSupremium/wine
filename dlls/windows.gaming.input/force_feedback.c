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

#include "math.h"

#include "ddk/hidsdi.h"
#include "dinput.h"
#include "hidusage.h"
#include "provider.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(input);

#define WIDL_impl_effect
#define WIDL_impl_motor
#include "classes_impl.h"

struct effect
{
    struct effect_klass klass;

    CRITICAL_SECTION cs;
    IDirectInputEffect *effect;

    GUID type;
    DWORD axes[3];
    LONG directions[3];
    ULONG repeat_count;
    DICONSTANTFORCE constant_force;
    DIRAMPFORCE ramp_force;
    DICONDITION condition;
    DIPERIODIC periodic;
    DIENVELOPE envelope;
    DIEFFECT params;
};

static struct effect *effect_from_klass( struct effect_klass *klass )
{
    return CONTAINING_RECORD( klass, struct effect, klass );
}

static HRESULT effect_missing_interface( struct effect *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void effect_destroy( struct effect *impl )
{
    if (impl->effect) IDirectInputEffect_Release( impl->effect );
    impl->cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection( &impl->cs );
    free( impl );
}

static int effect_reorient_direction( const WineForceFeedbackEffectParameters *params, Vector3 *direction )
{
    int sign = +1;

    switch (params->type)
    {
    case WineForceFeedbackEffectType_Constant:
        *direction = params->constant.direction;
        sign = params->constant.direction.X < 0 ? -1 : +1;
        break;

    case WineForceFeedbackEffectType_Ramp:
        *direction = params->ramp.start_vector;
        sign = params->ramp.start_vector.X < 0 ? -1 : +1;
        break;

    case WineForceFeedbackEffectType_Periodic_SineWave:
    case WineForceFeedbackEffectType_Periodic_TriangleWave:
    case WineForceFeedbackEffectType_Periodic_SquareWave:
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveDown:
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveUp:
        *direction = params->periodic.direction;
        break;

    case WineForceFeedbackEffectType_Condition_Spring:
    case WineForceFeedbackEffectType_Condition_Damper:
    case WineForceFeedbackEffectType_Condition_Inertia:
    case WineForceFeedbackEffectType_Condition_Friction:
        *direction = params->condition.direction;
        sign = -1;
        break;
    }

    direction->X *= -sign;
    direction->Y *= -sign;
    direction->Z *= -sign;

    return sign;
}

static HRESULT effect_put_Parameters( struct effect *impl, WineForceFeedbackEffectParameters params,
                                      WineForceFeedbackEffectEnvelope *envelope )
{
    Vector3 direction = {0};
    double magnitude = 0;
    DWORD count = 0;
    HRESULT hr;
    int sign;

    TRACE( "iface %p, params %p, envelope %p.\n", impl, &params, envelope );

    EnterCriticalSection( &impl->cs );

    sign = effect_reorient_direction( &params, &direction );
    /* Y and Z axes seems to be always ignored, is it really the case? */
    magnitude += direction.X * direction.X;

    switch (params.type)
    {
    case WineForceFeedbackEffectType_Constant:
        impl->repeat_count = params.constant.repeat_count;
        impl->constant_force.lMagnitude = sign * round( params.constant.gain * sqrt( magnitude ) * 10000 );
        impl->params.dwDuration = min( max( params.constant.duration.Duration / 10, 0 ), INFINITE );
        impl->params.dwStartDelay = min( max( params.constant.start_delay.Duration / 10, 0 ), INFINITE );
        break;

    case WineForceFeedbackEffectType_Ramp:
        impl->repeat_count = params.ramp.repeat_count;
        impl->ramp_force.lStart = sign * round( params.ramp.gain * sqrt( magnitude ) * 10000 );
        impl->ramp_force.lEnd = round( params.ramp.gain * params.ramp.end_vector.X * 10000 );
        impl->params.dwDuration = min( max( params.ramp.duration.Duration / 10, 0 ), INFINITE );
        impl->params.dwStartDelay = min( max( params.ramp.start_delay.Duration / 10, 0 ), INFINITE );
        break;

    case WineForceFeedbackEffectType_Periodic_SineWave:
    case WineForceFeedbackEffectType_Periodic_TriangleWave:
    case WineForceFeedbackEffectType_Periodic_SquareWave:
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveDown:
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveUp:
        impl->repeat_count = params.periodic.repeat_count;
        impl->periodic.dwMagnitude = round( params.periodic.gain * 10000 );
        impl->periodic.dwPeriod = 1000000 / params.periodic.frequency;
        impl->periodic.dwPhase = round( params.periodic.phase * 36000 );
        impl->periodic.lOffset = round( params.periodic.bias * 10000 );
        impl->params.dwDuration = min( max( params.periodic.duration.Duration / 10, 0 ), INFINITE );
        impl->params.dwStartDelay = min( max( params.periodic.start_delay.Duration / 10, 0 ), INFINITE );
        break;

    case WineForceFeedbackEffectType_Condition_Spring:
    case WineForceFeedbackEffectType_Condition_Damper:
    case WineForceFeedbackEffectType_Condition_Inertia:
    case WineForceFeedbackEffectType_Condition_Friction:
        impl->repeat_count = 1;
        impl->condition.lPositiveCoefficient = round( atan( params.condition.positive_coeff ) / M_PI_2 * 10000 );
        impl->condition.lNegativeCoefficient = round( atan( params.condition.negative_coeff ) / M_PI_2 * 10000 );
        impl->condition.dwPositiveSaturation = round( params.condition.max_positive_magnitude * 10000 );
        impl->condition.dwNegativeSaturation = round( params.condition.max_negative_magnitude * 10000 );
        impl->condition.lDeadBand = round( params.condition.deadzone * 10000 );
        impl->condition.lOffset = round( params.condition.bias * 10000 );
        impl->params.dwDuration = -1;
        impl->params.dwStartDelay = 0;
        break;
    }

    if (impl->axes[count] == DIJOFS_X) impl->directions[count++] = round( direction.X * 10000 );
    if (impl->axes[count] == DIJOFS_Y) impl->directions[count++] = round( direction.Y * 10000 );
    if (impl->axes[count] == DIJOFS_Z) impl->directions[count++] = round( direction.Z * 10000 );

    if (!envelope) impl->params.lpEnvelope = NULL;
    else
    {
        impl->envelope.dwAttackTime = min( max( envelope->attack_duration.Duration / 10, 0 ), INFINITE );
        impl->envelope.dwAttackLevel = round( envelope->attack_gain * 10000 );
        impl->envelope.dwFadeTime = impl->params.dwDuration - min( max( envelope->release_duration.Duration / 10, 0 ), INFINITE );
        impl->envelope.dwFadeLevel = round( envelope->release_gain * 10000 );
        impl->params.lpEnvelope = &impl->envelope;
    }

    if (!impl->effect) hr = S_OK;
    else hr = IDirectInputEffect_SetParameters( impl->effect, &impl->params, DIEP_ALLPARAMS & ~DIEP_AXES );
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT effect_get_Gain( struct effect *impl, DOUBLE *value )
{
    TRACE( "iface %p, value %p.\n", impl, value );

    EnterCriticalSection( &impl->cs );
    *value = impl->params.dwGain / 10000.;
    LeaveCriticalSection( &impl->cs );

    return S_OK;
}

static HRESULT effect_put_Gain( struct effect *impl, DOUBLE value )
{
    HRESULT hr;

    TRACE( "iface %p, value %f.\n", impl, value );

    EnterCriticalSection( &impl->cs );
    impl->params.dwGain = round( value * 10000 );
    if (!impl->effect) hr = S_FALSE;
    else hr = IDirectInputEffect_SetParameters( impl->effect, &impl->params, DIEP_GAIN );
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT effect_get_State( struct effect *impl, ForceFeedbackEffectState *value )
{
    DWORD status;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", impl, value );

    EnterCriticalSection( &impl->cs );
    if (!impl->effect)
        *value = ForceFeedbackEffectState_Stopped;
    else if (FAILED(hr = IDirectInputEffect_GetEffectStatus( impl->effect, &status )))
        *value = ForceFeedbackEffectState_Faulted;
    else
    {
        if (status == DIEGES_PLAYING) *value = ForceFeedbackEffectState_Running;
        else *value = ForceFeedbackEffectState_Stopped;
    }
    LeaveCriticalSection( &impl->cs );

    return S_OK;
}

static HRESULT effect_Start( struct effect *impl )
{
    HRESULT hr = E_UNEXPECTED;
    DWORD flags = 0;

    TRACE( "iface %p.\n", impl );

    EnterCriticalSection( &impl->cs );
    if (impl->effect) hr = IDirectInputEffect_Start( impl->effect, impl->repeat_count, flags );
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT effect_Stop( struct effect *impl )
{
    HRESULT hr = E_UNEXPECTED;

    TRACE( "iface %p.\n", impl );

    EnterCriticalSection( &impl->cs );
    if (impl->effect) hr = IDirectInputEffect_Stop( impl->effect );
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static const struct effect_funcs effect_funcs = EFFECT_FUNCS_INIT;

HRESULT force_feedback_effect_create( enum WineForceFeedbackEffectType type, IInspectable *outer, IWineForceFeedbackEffectImpl **out )
{
    struct effect *impl;

    TRACE( "outer %p, out %p\n", outer, out );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    effect_klass_init( &impl->klass );

    switch (type)
    {
    case WineForceFeedbackEffectType_Constant:
        impl->type = GUID_ConstantForce;
        impl->params.lpvTypeSpecificParams = &impl->constant_force;
        impl->params.cbTypeSpecificParams = sizeof(impl->constant_force);
        break;

    case WineForceFeedbackEffectType_Ramp:
        impl->type = GUID_RampForce;
        impl->params.lpvTypeSpecificParams = &impl->ramp_force;
        impl->params.cbTypeSpecificParams = sizeof(impl->ramp_force);
        break;

    case WineForceFeedbackEffectType_Periodic_SineWave:
        impl->type = GUID_Sine;
        goto WineForceFeedbackEffectType_Periodic;
    case WineForceFeedbackEffectType_Periodic_TriangleWave:
        impl->type = GUID_Triangle;
        goto WineForceFeedbackEffectType_Periodic;
    case WineForceFeedbackEffectType_Periodic_SquareWave:
        impl->type = GUID_Square;
        goto WineForceFeedbackEffectType_Periodic;
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveDown:
        impl->type = GUID_SawtoothDown;
        goto WineForceFeedbackEffectType_Periodic;
    case WineForceFeedbackEffectType_Periodic_SawtoothWaveUp:
        impl->type = GUID_SawtoothUp;
        goto WineForceFeedbackEffectType_Periodic;
    WineForceFeedbackEffectType_Periodic:
        impl->params.lpvTypeSpecificParams = &impl->periodic;
        impl->params.cbTypeSpecificParams = sizeof(impl->periodic);
        break;

    case WineForceFeedbackEffectType_Condition_Spring:
        impl->type = GUID_Spring;
        goto WineForceFeedbackEffectType_Condition;
    case WineForceFeedbackEffectType_Condition_Damper:
        impl->type = GUID_Damper;
        goto WineForceFeedbackEffectType_Condition;
    case WineForceFeedbackEffectType_Condition_Inertia:
        impl->type = GUID_Inertia;
        goto WineForceFeedbackEffectType_Condition;
    case WineForceFeedbackEffectType_Condition_Friction:
        impl->type = GUID_Friction;
        goto WineForceFeedbackEffectType_Condition;
    WineForceFeedbackEffectType_Condition:
        impl->params.lpvTypeSpecificParams = &impl->condition;
        impl->params.cbTypeSpecificParams = sizeof(impl->condition);
        break;
    }

    impl->envelope.dwSize = sizeof(DIENVELOPE);
    impl->params.dwSize = sizeof(DIEFFECT);
    impl->params.rgdwAxes = impl->axes;
    impl->params.rglDirection = impl->directions;
    impl->params.dwTriggerButton = -1;
    impl->params.dwGain = 10000;
    impl->params.dwFlags = DIEFF_CARTESIAN|DIEFF_OBJECTOFFSETS;
    impl->params.cAxes = -1;
    impl->axes[0] = DIJOFS_X;
    impl->axes[1] = DIJOFS_Y;
    impl->axes[2] = DIJOFS_Z;

    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)( __FILE__ ": effect.cs" );

    *out = &impl->klass.IWineForceFeedbackEffectImpl_iface;
    TRACE( "created ForceFeedbackEffect %p\n", *out );
    return S_OK;
}

struct motor
{
    struct motor_klass klass;
    IDirectInputDevice8W *device;
};

static struct motor *motor_from_klass( struct motor_klass *klass )
{
    return CONTAINING_RECORD( klass, struct motor, klass );
}

static HRESULT motor_missing_interface( struct motor *impl, REFIID iid, void **out )
{
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static void motor_destroy( struct motor *motor )
{
    IDirectInputDevice8_Release( motor->device );
    free( motor );
}

static HRESULT motor_get_AreEffectsPaused( struct motor *motor, BOOLEAN *value )
{
    DWORD state;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", motor, value );

    if (FAILED(hr = IDirectInputDevice8_GetForceFeedbackState( motor->device, &state ))) *value = FALSE;
    else *value = (state & DIGFFS_PAUSED);

    return hr;
}

static HRESULT motor_get_MasterGain( struct motor *motor, double *value )
{
    DIPROPDWORD gain =
    {
        .diph =
        {
            .dwSize = sizeof(DIPROPDWORD),
            .dwHeaderSize = sizeof(DIPROPHEADER),
            .dwHow = DIPH_DEVICE,
        },
    };
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", motor, value );

    if (FAILED(hr = IDirectInputDevice8_GetProperty( motor->device, DIPROP_FFGAIN, &gain.diph ))) *value = 1.;
    else *value = gain.dwData / 10000.;

    return hr;
}

static HRESULT motor_put_MasterGain( struct motor *motor, double value )
{
    DIPROPDWORD gain =
    {
        .diph =
        {
            .dwSize = sizeof(DIPROPDWORD),
            .dwHeaderSize = sizeof(DIPROPHEADER),
            .dwHow = DIPH_DEVICE,
        },
    };

    TRACE( "iface %p, value %f.\n", motor, value );

    gain.dwData = 10000 * value;
    return IDirectInputDevice8_SetProperty( motor->device, DIPROP_FFGAIN, &gain.diph );
}

static HRESULT motor_get_IsEnabled( struct motor *motor, BOOLEAN *value )
{
    DWORD state;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", motor, value );

    if (FAILED(hr = IDirectInputDevice8_GetForceFeedbackState( motor->device, &state ))) *value = FALSE;
    else *value = !(state & DIGFFS_ACTUATORSOFF);

    return hr;
}

static BOOL CALLBACK check_ffb_axes( const DIDEVICEOBJECTINSTANCEW *obj, void *args )
{
    ForceFeedbackEffectAxes *value = args;

    if (obj->dwType & DIDFT_FFACTUATOR)
    {
        if (IsEqualIID( &obj->guidType, &GUID_XAxis )) *value |= ForceFeedbackEffectAxes_X;
        else if (IsEqualIID( &obj->guidType, &GUID_YAxis )) *value |= ForceFeedbackEffectAxes_Y;
        else if (IsEqualIID( &obj->guidType, &GUID_ZAxis )) *value |= ForceFeedbackEffectAxes_Z;
    }

    return DIENUM_CONTINUE;
}

static HRESULT motor_get_SupportedAxes( struct motor *motor, enum ForceFeedbackEffectAxes *value )
{
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", motor, value );

    *value = ForceFeedbackEffectAxes_None;
    if (FAILED(hr = IDirectInputDevice8_EnumObjects( motor->device, check_ffb_axes, value, DIDFT_AXIS )))
        *value = ForceFeedbackEffectAxes_None;

    return hr;
}

static HRESULT motor_load_effect_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    struct effect *effect = effect_from_IForceFeedbackEffect( (IForceFeedbackEffect *)param );
    IForceFeedbackMotor *iface = (IForceFeedbackMotor *)invoker;
    struct motor *motor = motor_from_IForceFeedbackMotor( iface );
    ForceFeedbackEffectAxes supported_axes = 0;
    IDirectInputEffect *dinput_effect;
    HRESULT hr;

    EnterCriticalSection( &effect->cs );

    if (FAILED(hr = IForceFeedbackMotor_get_SupportedAxes( iface, &supported_axes )))
    {
        WARN( "get_SupportedAxes for motor %p returned %#lx\n", motor, hr );
        effect->params.cAxes = 0;
    }
    else if (effect->params.cAxes == -1)
    {
        DWORD count = 0;

        /* initialize axis mapping and re-map directions that were set with the initial mapping */
        if (supported_axes & ForceFeedbackEffectAxes_X)
        {
            effect->directions[count] = effect->directions[0];
            effect->axes[count++] = DIJOFS_X;
        }
        if (supported_axes & ForceFeedbackEffectAxes_Y)
        {
            effect->directions[count] = effect->directions[1];
            effect->axes[count++] = DIJOFS_Y;
        }
        if (supported_axes & ForceFeedbackEffectAxes_Z)
        {
            effect->directions[count] = effect->directions[2];
            effect->axes[count++] = DIJOFS_Z;
        }

        effect->params.cAxes = count;
    }

    if (SUCCEEDED(hr = IDirectInputDevice8_CreateEffect( motor->device, &effect->type, &effect->params,
                                                         &dinput_effect, NULL )))
    {
        if (effect->effect) IDirectInputEffect_Release( effect->effect );
        effect->effect = dinput_effect;
    }

    LeaveCriticalSection( &effect->cs );

    result->vt = VT_UI4;
    if (SUCCEEDED(hr)) result->ulVal = ForceFeedbackLoadEffectResult_Succeeded;
    else if (hr == DIERR_DEVICEFULL) result->ulVal = ForceFeedbackLoadEffectResult_EffectStorageFull;
    else result->ulVal = ForceFeedbackLoadEffectResult_EffectNotSupported;

    return hr;
}

static HRESULT motor_LoadEffectAsync( struct motor *motor, IForceFeedbackEffect *effect,
                                      IAsyncOperation_ForceFeedbackLoadEffectResult **async_op )
{
    TRACE( "iface %p, effect %p, async_op %p.\n", motor, effect, async_op );
    return async_operation_effect_result_create( (IUnknown *)&motor->klass.IForceFeedbackMotor_iface,
                                                 (IUnknown *)effect, motor_load_effect_async, async_op );
}

static HRESULT motor_PauseAllEffects( struct motor *motor )
{
    TRACE( "iface %p.\n", motor );

    return IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_PAUSE );
}

static HRESULT motor_ResumeAllEffects( struct motor *motor )
{
    TRACE( "iface %p.\n", motor );

    return IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_CONTINUE );
}

static HRESULT motor_StopAllEffects( struct motor *motor )
{
    TRACE( "iface %p.\n", motor );

    return IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_STOPALL );
}

static HRESULT motor_try_disable_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    struct motor *motor = motor_from_IForceFeedbackMotor( (IForceFeedbackMotor *)invoker );
    HRESULT hr;

    hr = IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_SETACTUATORSOFF );
    result->vt = VT_BOOL;
    result->boolVal = SUCCEEDED(hr);

    return hr;
}

static HRESULT motor_TryDisableAsync( struct motor *motor, IAsyncOperation_boolean **async_op )
{
    TRACE( "iface %p, async_op %p.\n", motor, async_op );
    return async_operation_boolean_create( (IUnknown *)&motor->klass.IForceFeedbackMotor_iface,
                                           NULL, motor_try_disable_async, async_op );
}

static HRESULT motor_try_enable_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    struct motor *motor = motor_from_IForceFeedbackMotor( (IForceFeedbackMotor *)invoker );
    HRESULT hr;

    hr = IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_SETACTUATORSON );
    result->vt = VT_BOOL;
    result->boolVal = SUCCEEDED(hr);

    return hr;
}

static HRESULT motor_TryEnableAsync( struct motor *motor, IAsyncOperation_boolean **async_op )
{
    TRACE( "iface %p, async_op %p.\n", motor, async_op );
    return async_operation_boolean_create( (IUnknown *)&motor->klass.IForceFeedbackMotor_iface,
                                           NULL, motor_try_enable_async, async_op );
}

static HRESULT motor_try_reset_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    struct motor *motor = motor_from_IForceFeedbackMotor( (IForceFeedbackMotor *)invoker );
    HRESULT hr;

    hr = IDirectInputDevice8_SendForceFeedbackCommand( motor->device, DISFFC_RESET );
    result->vt = VT_BOOL;
    result->boolVal = SUCCEEDED(hr);

    return hr;
}

static HRESULT motor_TryResetAsync( struct motor *motor, IAsyncOperation_boolean **async_op )
{
    TRACE( "iface %p, async_op %p.\n", motor, async_op );
    return async_operation_boolean_create( (IUnknown *)&motor->klass.IForceFeedbackMotor_iface,
                                           NULL, motor_try_reset_async, async_op );
}

static HRESULT motor_unload_effect_async( IUnknown *iface, IUnknown *param, PROPVARIANT *result )
{
    struct effect *effect = effect_from_IForceFeedbackEffect( (IForceFeedbackEffect *)param );
    IDirectInputEffect *dinput_effect;
    HRESULT hr;

    EnterCriticalSection( &effect->cs );
    dinput_effect = effect->effect;
    effect->effect = NULL;
    LeaveCriticalSection( &effect->cs );

    if (!dinput_effect) hr = S_OK;
    else
    {
        hr = IDirectInputEffect_Unload( dinput_effect );
        IDirectInputEffect_Release( dinput_effect );
    }

    result->vt = VT_BOOL;
    result->boolVal = SUCCEEDED(hr);
    return hr;
}

static HRESULT motor_TryUnloadEffectAsync( struct motor *motor, IForceFeedbackEffect *effect,
                                           IAsyncOperation_boolean **async_op )
{
    struct effect *impl = effect_from_IForceFeedbackEffect( (IForceFeedbackEffect *)effect );
    HRESULT hr = S_OK;

    TRACE( "iface %p, effect %p, async_op %p.\n", motor, effect, async_op );

    EnterCriticalSection( &impl->cs );
    if (!impl->effect) hr = E_FAIL;
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;

    return async_operation_boolean_create( (IUnknown *)&motor->klass.IForceFeedbackMotor_iface,
                                           (IUnknown *)effect, motor_unload_effect_async, async_op );
}

static const struct motor_funcs motor_funcs = MOTOR_FUNCS_INIT;

HRESULT force_feedback_motor_create( IDirectInputDevice8W *device, IForceFeedbackMotor **out )
{
    struct motor *motor;
    HRESULT hr;

    TRACE( "device %p, out %p\n", device, out );

    if (FAILED(hr = IDirectInputDevice8_Unacquire( device ))) goto failed;
    if (FAILED(hr = IDirectInputDevice8_SetCooperativeLevel( device, GetDesktopWindow(), DISCL_BACKGROUND | DISCL_EXCLUSIVE ))) goto failed;
    if (FAILED(hr = IDirectInputDevice8_Acquire( device ))) goto failed;

    if (!(motor = calloc( 1, sizeof(*motor) ))) return E_OUTOFMEMORY;
    motor_klass_init( &motor->klass, RuntimeClass_Windows_Gaming_Input_ForceFeedback_ForceFeedbackMotor );

    IDirectInputDevice_AddRef( device );
    motor->device = device;

    *out = &motor->klass.IForceFeedbackMotor_iface;
    TRACE( "created ForceFeedbackMotor %p\n", *out );
    return S_OK;

failed:
    IDirectInputDevice8_SetCooperativeLevel( device, 0, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE );
    IDirectInputDevice8_Acquire( device );
    WARN( "Failed to acquire device exclusively, hr %#lx\n", hr );
    return hr;
}
