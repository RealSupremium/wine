/*
 * Unit tests for power management functions
 *
 * Copyright (c) 2019 Alex Henrie
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

#include "wine/test.h"

static HANDLE (WINAPI *pPowerCreateRequest)(REASON_CONTEXT *);
static BOOL   (WINAPI *pPowerSetRequest)(HANDLE, POWER_REQUEST_TYPE);
static BOOL   (WINAPI *pPowerClearRequest)(HANDLE, POWER_REQUEST_TYPE);

void test_GetSystemPowerStatus(void)
{
    SYSTEM_POWER_STATUS ps;
    BOOL ret;
    BYTE capacity_flags, expected_capacity_flags;

    if (0) /* crashes */
        GetSystemPowerStatus(NULL);

    memset(&ps, 0x23, sizeof(ps));
    ret = GetSystemPowerStatus(&ps);
    ok(ret == TRUE, "expected TRUE\n");

    if (ps.BatteryFlag == BATTERY_FLAG_UNKNOWN)
    {
        skip("GetSystemPowerStatus not implemented or not working\n");
        return;
    }
    else if (ps.BatteryFlag != BATTERY_FLAG_NO_BATTERY)
    {
        trace("battery detected\n");
        expected_capacity_flags = 0;
        if (ps.BatteryLifePercent > 66)
            expected_capacity_flags |= BATTERY_FLAG_HIGH;
        if (ps.BatteryLifePercent < 33)
            expected_capacity_flags |= BATTERY_FLAG_LOW;
        if (ps.BatteryLifePercent < 5)
            expected_capacity_flags |= BATTERY_FLAG_CRITICAL;
        capacity_flags = (ps.BatteryFlag & ~BATTERY_FLAG_CHARGING);
        ok(capacity_flags == expected_capacity_flags,
           "expected %u%%-charged battery to have capacity flags 0x%02x, got 0x%02x\n",
           ps.BatteryLifePercent, expected_capacity_flags, capacity_flags);
        ok(ps.BatteryLifeTime <= ps.BatteryFullLifeTime,
           "expected BatteryLifeTime %lu to be less than or equal to BatteryFullLifeTime %lu\n",
           ps.BatteryLifeTime, ps.BatteryFullLifeTime);
        if (ps.BatteryFlag & BATTERY_FLAG_CHARGING)
        {
            ok(ps.BatteryLifeTime == BATTERY_LIFE_UNKNOWN,
               "expected BatteryLifeTime to be -1 when charging, got %lu\n", ps.BatteryLifeTime);
            ok(ps.BatteryFullLifeTime == BATTERY_LIFE_UNKNOWN,
               "expected BatteryFullLifeTime to be -1 when charging, got %lu\n", ps.BatteryFullLifeTime);
        }
    }
    else
    {
        trace("no battery detected\n");
        ok(ps.ACLineStatus == AC_LINE_ONLINE,
           "expected ACLineStatus to be 1, got %u\n", ps.ACLineStatus);
        ok(ps.BatteryLifePercent == BATTERY_PERCENTAGE_UNKNOWN,
           "expected BatteryLifePercent to be -1, got %u\n", ps.BatteryLifePercent);
        ok(ps.BatteryLifeTime == BATTERY_LIFE_UNKNOWN,
           "expected BatteryLifeTime to be -1, got %lu\n", ps.BatteryLifeTime);
        ok(ps.BatteryFullLifeTime == BATTERY_LIFE_UNKNOWN,
           "expected BatteryFullLifeTime to be -1, got %lu\n", ps.BatteryFullLifeTime);
    }
}

static void test_PowerCreateRequest(void)
{
    static WCHAR reason[] = {'W','i','n','e',' ','c','o','n','f','o','r','m','a','n','c','e',' ','t','e','s','t',0};
    REASON_CONTEXT ctx;
    HANDLE request;
    DWORD err;
    BOOL ret;

    if (!pPowerCreateRequest)
    {
        win_skip("PowerCreateRequest not available\n");
        return;
    }

    /* valid simple string context */
    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString = reason;
    SetLastError(0xdeadbeef);
    request = pPowerCreateRequest(&ctx);
    ok(request != INVALID_HANDLE_VALUE, "expected valid handle, got INVALID_HANDLE_VALUE\n");
    ok(request != NULL, "expected valid handle, got NULL\n");
    if (request != INVALID_HANDLE_VALUE && request != NULL)
    {
        ret = CloseHandle(request);
        ok(ret == TRUE, "CloseHandle failed, error %lu\n", GetLastError());
    }

    /* valid detailed context with NULL module and zero strings */
    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = POWER_REQUEST_CONTEXT_DETAILED_STRING;
    ctx.Reason.Detailed.LocalizedReasonModule = NULL;
    ctx.Reason.Detailed.LocalizedReasonId     = 0;
    ctx.Reason.Detailed.ReasonStringCount     = 0;
    ctx.Reason.Detailed.ReasonStrings         = NULL;
    SetLastError(0xdeadbeef);
    request = pPowerCreateRequest(&ctx);
    ok(request != INVALID_HANDLE_VALUE, "expected valid handle, got INVALID_HANDLE_VALUE\n");
    ok(request != NULL, "expected valid handle, got NULL\n");
    if (request != INVALID_HANDLE_VALUE && request != NULL)
    {
        ret = CloseHandle(request);
        ok(ret == TRUE, "CloseHandle failed, error %lu\n", GetLastError());
    }

    /* invalid version */
    ctx.Version = 999;
    ctx.Flags   = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString = reason;
    SetLastError(0xdeadbeef);
    request = pPowerCreateRequest(&ctx);
    err = GetLastError();
    todo_wine ok(request == INVALID_HANDLE_VALUE, "expected INVALID_HANDLE_VALUE, got %p\n", request);
    todo_wine ok(err == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %lu\n", err);

    /* invalid flags (0) */
    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = 0;
    ctx.Reason.SimpleReasonString = reason;
    SetLastError(0xdeadbeef);
    request = pPowerCreateRequest(&ctx);
    err = GetLastError();
    todo_wine ok(request == INVALID_HANDLE_VALUE, "expected INVALID_HANDLE_VALUE, got %p\n", request);
    todo_wine ok(err == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %lu\n", err);

    /* invalid flags (0xFFFF) */
    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = 0xFFFF;
    ctx.Reason.SimpleReasonString = reason;
    SetLastError(0xdeadbeef);
    request = pPowerCreateRequest(&ctx);
    err = GetLastError();
    todo_wine ok(request == INVALID_HANDLE_VALUE, "expected INVALID_HANDLE_VALUE, got %p\n", request);
    todo_wine ok(err == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %lu\n", err);
}

static void test_PowerSetRequest(void)
{
    static WCHAR reason[] = {'W','i','n','e',' ','c','o','n','f','o','r','m','a','n','c','e',' ','t','e','s','t',0};
    REASON_CONTEXT ctx;
    HANDLE request;
    DWORD err;
    BOOL ret;

    if (!pPowerCreateRequest || !pPowerSetRequest)
    {
        win_skip("PowerSetRequest not available\n");
        return;
    }

    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString = reason;
    request = pPowerCreateRequest(&ctx);
    if (request == INVALID_HANDLE_VALUE || request == NULL)
    {
        skip("PowerCreateRequest failed, cannot test PowerSetRequest\n");
        return;
    }

    /* valid handle + each valid request type (Windows 7 and later) */
    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(request, PowerRequestDisplayRequired);
    ok(ret, "PowerSetRequest(DisplayRequired) failed, error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(request, PowerRequestSystemRequired);
    ok(ret, "PowerSetRequest(SystemRequired) failed, error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(request, PowerRequestAwayModeRequired);
    ok(ret, "PowerSetRequest(AwayModeRequired) failed, error %lu\n", GetLastError());

    /* PowerRequestExecutionRequired was added in Windows 8, not tested here */

    /* invalid handle */
    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(INVALID_HANDLE_VALUE, PowerRequestDisplayRequired);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", err);

    /* NULL handle */
    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(NULL, PowerRequestDisplayRequired);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", err);

    /* invalid request type */
    SetLastError(0xdeadbeef);
    ret = pPowerSetRequest(request, (POWER_REQUEST_TYPE)0xFFFF);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_NOT_SUPPORTED, "expected ERROR_NOT_SUPPORTED, got %lu\n", err);

    CloseHandle(request);
}

static void test_PowerClearRequest(void)
{
    static WCHAR reason[] = {'W','i','n','e',' ','c','o','n','f','o','r','m','a','n','c','e',' ','t','e','s','t',0};
    REASON_CONTEXT ctx;
    HANDLE request;
    DWORD err;
    BOOL ret;

    if (!pPowerCreateRequest || !pPowerSetRequest || !pPowerClearRequest)
    {
        win_skip("PowerClearRequest not available\n");
        return;
    }

    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags   = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString = reason;
    request = pPowerCreateRequest(&ctx);
    if (request == INVALID_HANDLE_VALUE || request == NULL)
    {
        skip("PowerCreateRequest failed, cannot test PowerClearRequest\n");
        return;
    }

    /* clear without a prior set (count is 0) */
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(request, PowerRequestDisplayRequired);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_NOT_SUPPORTED, "expected ERROR_NOT_SUPPORTED, got %lu\n", err);

    /* set then clear (normal flow) for each valid type (Windows 7 and later) */
    pPowerSetRequest(request, PowerRequestDisplayRequired);
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(request, PowerRequestDisplayRequired);
    ok(ret, "PowerClearRequest(DisplayRequired) failed, error %lu\n", GetLastError());

    pPowerSetRequest(request, PowerRequestSystemRequired);
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(request, PowerRequestSystemRequired);
    ok(ret, "PowerClearRequest(SystemRequired) failed, error %lu\n", GetLastError());

    pPowerSetRequest(request, PowerRequestAwayModeRequired);
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(request, PowerRequestAwayModeRequired);
    ok(ret, "PowerClearRequest(AwayModeRequired) failed, error %lu\n", GetLastError());

    /* PowerRequestExecutionRequired was added in Windows 8, not tested here */

    /* invalid handle */
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(INVALID_HANDLE_VALUE, PowerRequestDisplayRequired);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", err);

    /* NULL handle */
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(NULL, PowerRequestDisplayRequired);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", err);

    /* invalid request type */
    SetLastError(0xdeadbeef);
    ret = pPowerClearRequest(request, (POWER_REQUEST_TYPE)0xFFFF);
    err = GetLastError();
    todo_wine ok(!ret, "expected failure, got %d\n", ret);
    todo_wine ok(err == ERROR_NOT_SUPPORTED, "expected ERROR_NOT_SUPPORTED, got %lu\n", err);

    CloseHandle(request);
}

START_TEST(power)
{
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");

    pPowerCreateRequest  = (void *)GetProcAddress(kernel32, "PowerCreateRequest");
    pPowerSetRequest     = (void *)GetProcAddress(kernel32, "PowerSetRequest");
    pPowerClearRequest   = (void *)GetProcAddress(kernel32, "PowerClearRequest");

    test_GetSystemPowerStatus();
    test_PowerCreateRequest();
    test_PowerSetRequest();
    test_PowerClearRequest();
}
