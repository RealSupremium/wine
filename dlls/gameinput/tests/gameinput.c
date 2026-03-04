/*
 * Copyright 2026 Alistair Leslie-Hughes
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
#define COBJMACROS

#include <stdio.h>

#include "windows.h"
#include "initguid.h"
#include "gameinput.h"

#include "wine/test.h"

static void test_CreateGameInput(void)
{
    HRESULT hr;
    v0_IGameInput *gameinputV0, *gameinputV0_2;
    v1_IGameInput *gameinputV1 = NULL;
    v2_IGameInput *gameinputV2 = NULL;
    IAgileObject *agile;

    hr = GameInputCreate( &gameinputV0 );
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    /* Calling GameInputCreate multiple times returns same pointer */
    hr = GameInputCreate( &gameinputV0_2 );
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(gameinputV0_2 == gameinputV0, "Wrong pointer\n");
    v0_IGameInput_Release(gameinputV0_2);

    hr = v0_IGameInput_QueryInterface( gameinputV0, &IID_v1_IGameInput, (void **)&gameinputV1 );
    if (hr != S_OK)
        win_skip("iface IID_v1_IGameInput not supported\n");

    hr = v0_IGameInput_QueryInterface( gameinputV0, &IID_v2_IGameInput, (void **)&gameinputV2 );
    if (hr != S_OK)
        win_skip("iface IID_v2_IGameInput not supported\n");

    hr = v0_IGameInput_QueryInterface( gameinputV0, &IID_IAgileObject, (void **)&agile );
    ok(hr == E_NOINTERFACE, "Unexpected hr %#lx.\n", hr);

    if (gameinputV1)
        v1_IGameInput_Release(gameinputV1);
    if (gameinputV2)
        v2_IGameInput_Release(gameinputV2);
    v0_IGameInput_Release(gameinputV0);
}

START_TEST(gameinput)
{
    test_CreateGameInput();
}