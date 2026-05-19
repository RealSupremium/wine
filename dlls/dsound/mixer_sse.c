/* SSE versions of DirectSound mixing routines
 *
 * Copyright 2026 Anton Baskanov
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

#include <xmmintrin.h>

#include "windef.h"
#include "mmsystem.h"
#include "dsound.h"
#include "dsound_private.h"
#include "fir.h"

/**
 * Note that this function will overwrite up to FIR_WIDTH - 1 frames before and
 * after output[].
 */
void downsample_sse(LONG64 opos_num, DWORD opos_num_step, float rem_float, float rem_step_float,
        float firgain_float, UINT required_input, float *input, float *output)
{
    __m128 rem = _mm_set1_ps(rem_float);
    __m128 rem_step = _mm_set1_ps(rem_step_float);
    __m128 firgain = _mm_set_ss(firgain_float);
    __m128 one = _mm_set1_ps(1.0f);
    int j;

    for (j = 0; j < required_input; ++j) {
        /* opos is in the range [-(fir_width - 1), count) */
        int opos = (int)(opos_num >> FREQ_ADJUST_SHIFT) - FIR_WIDTH;
        UINT idx = ~(DWORD)opos_num >> (FREQ_ADJUST_SHIFT - FIR_STEP_SHIFT) << FIR_WIDTH_SHIFT;
        __m128 rem_inv = _mm_sub_ps(one, rem);

        __m128 input_value_ss = _mm_mul_ss(_mm_load_ss(&input[j]), firgain);
        __m128 input_value = _mm_shuffle_ps(input_value_ss, input_value_ss, 0);
        __m128 input_value0 = _mm_mul_ps(rem_inv, input_value);
        __m128 input_value1 = _mm_mul_ps(rem, input_value);

        int i;
        C_ASSERT(!(FIR_WIDTH % 4));
        for (i = 0; i < FIR_WIDTH; i += 4) {
            __m128 value0 = _mm_mul_ps(_mm_load_ps(&fir[idx + i]), input_value0);
            __m128 value1 = _mm_mul_ps(_mm_load_ps(&fir[idx + FIR_WIDTH + i]), input_value1);
            __m128 value = _mm_add_ps(value0, value1);
            _mm_storeu_ps(&output[opos + i], _mm_add_ps(_mm_loadu_ps(&output[opos + i]), value));
        }

        rem = _mm_add_ps(rem, rem_step);
        rem = _mm_sub_ps(rem, _mm_and_ps(one, _mm_cmple_ps(one, rem)));

        opos_num += opos_num_step;
    }
}

void upsample_sse(LONG64 ipos_num, DWORD ipos_num_step, float rem_inv_float,
        float rem_inv_step_float, UINT count, float *input, float *output)
{
    __m128 rem_inv = _mm_set1_ps(rem_inv_float);
    __m128 rem_inv_step = _mm_set1_ps(rem_inv_step_float);
    __m128 one = _mm_set1_ps(1.0f);

    UINT i;

    for(i = 0; i < count; ++i) {
        UINT ipos = ipos_num >> FREQ_ADJUST_SHIFT;
        UINT idx = ~(DWORD)ipos_num >> (FREQ_ADJUST_SHIFT - FIR_STEP_SHIFT) << FIR_WIDTH_SHIFT;
        __m128 rem = _mm_sub_ps(one, rem_inv);

        int j;
        __m128 sum = _mm_set1_ps(0.0f);
        float* cache = &input[ipos];

        C_ASSERT(!(FIR_WIDTH % 4));
        for (j = 0; j < FIR_WIDTH; j += 4) {
            __m128 fir_value0 = _mm_mul_ps(_mm_load_ps(&fir[idx + j]), rem_inv);
            __m128 fir_value1 = _mm_mul_ps(_mm_load_ps(&fir[idx + j + FIR_WIDTH]), rem);
            __m128 fir_value = _mm_add_ps(fir_value0, fir_value1);
            __m128 input_value = _mm_loadu_ps(&cache[j]);
            sum = _mm_add_ps(sum, _mm_mul_ps(fir_value, input_value));
        }

        /* Add the even-numbered sums to the odd-numbered ones. */
        sum = _mm_add_ps(sum, _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0, 3, 0, 1)));
        /* Calculate the final sum and store it to the output array. */
        sum = _mm_add_ss(sum, _mm_movehl_ps(sum, sum));
        _mm_store_ss(&output[i], sum);

        rem_inv = _mm_add_ps(rem_inv, rem_inv_step);
        rem_inv = _mm_sub_ps(rem_inv, _mm_and_ps(one, _mm_cmple_ps(one, rem_inv)));

        ipos_num += ipos_num_step;
    }
}
