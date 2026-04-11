/*
 * Unit test for winscard functions
 *
 * Copyright 2022 Hans Leidekker for CodeWeavers
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

#include <wchar.h>
#include <windows.h>
#include <winscard.h>

#include <winnt.h>
#include <winreg.h>

#include "minwindef.h"
#include "wine/test.h"

static void test_SCardEstablishContext(void)
{
    const BYTE cmd[] = {0x00, 0xca, 0x01, 0x86, 0x00};
    SCARDCONTEXT context;
    SCARDHANDLE connect;
    SCARD_READERSTATEA states[2];
    SCARD_IO_REQUEST send_pci = {SCARD_PROTOCOL_T1, 8}, recv_pci = {SCARD_PROTOCOL_T1, 8};
    char *readers, *groups, *ptr;
    WCHAR *names, *ptrW;
    BYTE buf[32], recv_buf[264], *atr, *attr;
    DWORD len, atrlen, state, protocol;
    LONG ret;

    ret = SCardEstablishContext( 0, NULL, NULL, NULL );
    ok( ret == SCARD_E_INVALID_PARAMETER, "got %lx\n", ret );

    ret = SCardEstablishContext( 0, NULL, NULL, &context );
    if (ret == SCARD_E_NO_SERVICE)
    {
        skip( "can't establish context, make sure pcscd is running\n" );
        return;
    }
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardIsValidContext( context );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    len = 0;
    ret = SCardListReadersA( context, NULL, NULL, &len );
    if (ret == SCARD_E_NO_READERS_AVAILABLE)
    {
        skip( "connect a smart card device to run more tests\n" );
        return;
    }
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( len, "got zero length\n" );

    readers = calloc( 1, len );
    ret = SCardListReadersA( context, NULL, readers, NULL );
    ok( ret == SCARD_E_INVALID_PARAMETER, "got %lx\n", ret );

    len -= 1;
    ret = SCardListReadersA( context, NULL, readers, &len );
    ok( ret == SCARD_E_INSUFFICIENT_BUFFER, "got %lx\n", ret );

    len += 1;
    ret = SCardListReadersA( context, NULL, readers, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    free( readers );

    readers = NULL;
    len = SCARD_AUTOALLOCATE;
    ret = SCardListReadersA( context, NULL, (char *)&readers, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( readers != NULL, "got NULL readers" );
    ok( len != SCARD_AUTOALLOCATE, "got %lu", len );
    if (!*readers)
    {
        skip( "connect a smart card device to run more tests\n" );
        return;
    }
    ptr = readers;
    while (*ptr)
    {
        trace( "found reader: %s\n", wine_dbgstr_a(ptr) );
        ptr += strlen( ptr ) + 1;
    }

    len = 0;
    ret = SCardListReaderGroupsA( context, NULL, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( len, "got zero length\n" );

    groups = calloc( 1, len );
    ret = SCardListReaderGroupsA( context, groups, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ptr = groups;
    while (*ptr)
    {
        trace( "found group: %s\n", wine_dbgstr_a(ptr) );
        ptr += strlen( ptr ) + 1;
    }
    free( groups );

    ret = SCardGetStatusChangeW( context, 1000, NULL, 0 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    memset( states, 0, sizeof(states) );
    states[0].szReader = "\\\\?PnP?\\Notification";
    states[1].szReader = readers;
    states[1].cbAtr = sizeof(states[1].rgbAtr) + 1;
    ret = SCardGetStatusChangeA( context, 1000, states, 2 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( states[1].cbAtr <= sizeof(states[1].rgbAtr), "got %lu\n", states[1].cbAtr );

    states[1].dwCurrentState = states[1].dwEventState & ~SCARD_STATE_CHANGED;
    ret = SCardGetStatusChangeA( context, 1000, states, 2 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardConnectA( context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &connect, NULL );
    if (ret == SCARD_E_READER_UNAVAILABLE)
    {
        skip( "can't connect to reader %s (in use by other application?)\n", wine_dbgstr_a(readers) );
        SCardFreeMemory( context, readers );
        return;
    }
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    connect = 0xdeadbeef;
    protocol = 0xdeadbeef;
    ret = SCardConnectA( context, readers, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &connect, &protocol );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( connect != 0xdeadbeef, "connect not set\n" );
    ok( protocol == SCARD_PROTOCOL_T1, "got %lx\n", protocol );
    SCardFreeMemory( context, readers );

    len = atrlen = 0;
    state = 0xdeadbeef;
    protocol = 0xdeadbeef;
    ret = SCardStatusW( connect, NULL, &len, &state, &protocol, NULL, &atrlen );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( len, "got zero length\n" );
    ok( state != 0xdeadbeef, "state not set\n" );
    ok( protocol == SCARD_PROTOCOL_T1, "got %lx\n", protocol );
    ok( atrlen, "got zero length\n" );

    names = calloc( 1, len * sizeof(WCHAR) );
    atr = calloc( 1, atrlen );
    state = protocol = 0xdeadbeef;
    ret = SCardStatusW( connect, names, &len, &state, &protocol, atr, &atrlen );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( state != 0xdeadbeef, "state not set\n" );
    ok( protocol == SCARD_PROTOCOL_T1, "got %lx\n", protocol );
    ptrW = names;
    while (*ptrW)
    {
        trace( "found name: %s\n", wine_dbgstr_w(ptrW) );
        ptrW += wcslen( ptrW ) + 1;
    }
    ret = SCardStatusW( connect, names, &len, &state, &protocol, NULL, NULL );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ret = SCardStatusW( connect, NULL, NULL, &state, &protocol, NULL, NULL );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ret = SCardStatusW( connect, NULL, NULL, NULL, NULL, NULL, NULL );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    free( names );
    free( atr );

    ret = SCardBeginTransaction( connect );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardEndTransaction( connect, 0 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardReconnect( connect, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, SCARD_LEAVE_CARD, NULL );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    protocol = 0xdeadbeef;
    ret = SCardReconnect( connect, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, SCARD_LEAVE_CARD, &protocol );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( protocol == SCARD_PROTOCOL_T1, "got %lx\n", protocol );

    len = 0;
    ret = SCardGetAttrib( connect, SCARD_ATTR_VENDOR_NAME, NULL, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    ok( len, "got zero length\n" );

    attr = calloc( 1, len );
    ret = SCardGetAttrib( connect, SCARD_ATTR_VENDOR_NAME, attr, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardGetAttrib( connect, SCARD_ATTR_VENDOR_NAME, attr, NULL );
    ok( ret == SCARD_E_INVALID_PARAMETER, "got %lx\n", ret );
    free( attr );

    ret = SCardBeginTransaction( connect );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    send_pci.dwProtocol = recv_pci.dwProtocol = protocol;
    memset( recv_buf, 0, sizeof(recv_buf) );
    ret = SCardTransmit( connect, &send_pci, cmd, sizeof(cmd), &recv_pci, recv_buf, NULL );
    ok( ret == SCARD_E_INVALID_PARAMETER, "got %lx (%lu)\n", ret, ret );

    ret = SCardTransmit( connect, NULL, cmd, sizeof(cmd), &recv_pci, recv_buf, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx (%lu)\n", ret, ret );

    len = sizeof(recv_buf);
    ret = SCardTransmit( connect, &send_pci, cmd, sizeof(cmd), &recv_pci, recv_buf, &len );
    ok( ret == SCARD_S_SUCCESS, "got %lx (%lu)\n", ret, ret );

    ret = SCardEndTransaction( connect, 0 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    buf[0] = 0x02;
    ret = SCardControl( connect, SCARD_CTL_CODE(1), buf, 1, buf, sizeof(buf), NULL );
    ok( ret == SCARD_E_INVALID_PARAMETER, "got %lx\n", ret );

    len = sizeof(buf);
    ret = SCardControl( connect, SCARD_CTL_CODE(1), buf, 1, buf, sizeof(buf), &len );
    todo_wine ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );
    todo_wine ok( !len, "got %lu\n", len );

    ret = SCardDisconnect( connect, 0 );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardDisconnect( 0, 0 );
    ok( ret == ERROR_INVALID_HANDLE, "got %lx\n", ret );

    ret = SCardCancel( context );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardCancel( 0 );
    ok( ret == ERROR_INVALID_HANDLE, "got %lx\n", ret );

    ret = SCardIsValidContext( context );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardReleaseContext( 0 );
    ok( ret == ERROR_INVALID_HANDLE, "got %lx\n", ret );

    ret = SCardReleaseContext( context );
    ok( ret == SCARD_S_SUCCESS, "got %lx\n", ret );

    ret = SCardIsValidContext( context );
    ok( ret == ERROR_INVALID_HANDLE, "got %lx\n", ret );

    ret = SCardIsValidContext( 0 );
    ok( ret == ERROR_INVALID_HANDLE, "got %lx\n", ret );
}

static const char *subkey_smartcards_database = "SOFTWARE\\Microsoft\\Cryptography\\Calais\\SmartCards";
static const char *card_name_1 = "PKI-test-1";
static const char *card_name_2 = "PKI-test-2";
static const WCHAR *card_name_1W = L"PKI-test-1";
static const WCHAR *card_name_2W = L"PKI-test-2";
static const BYTE card_atr_1[] = {
    0x3b, 0xda, 0x13, 0xff, 0x81, 0x31, 0xfb, 0x46, 0x80, 0x12, 0x39, 0x2f, 0x31, 0xc1, 0x73, 0xc6, 0x01, 0xc0, 0x3b
};
static const BYTE card_atr_2[] = { 0x3b, 0xd2, 0x18, 0x00, 0x81, 0x31, 0xfe, 0x58, 0xC9, 0x03, 0x16 };

static LONG populate_smartcard_db(void)
{
    HKEY key_db, key_card1, key_card2;
    LONG ret;

    const char *crypto_provider_1 = "Microsoft Base Smart Card Crypto Provider";
    const char *crypto_provider_2 = "Test Crypto Provider";
    const char *storage_provider_1 = "Microsoft Smart Card Key Storage Provider";
    const char *storage_provider_2 = "Test Key Storage Provider";
    const char *dll_1 = "opensc-driver.dll";
    const char *dll_2 = "cardoscm64.dll";
    const BYTE atr_mask_2[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16 };

    /* open the smartcard db, create it if needed */
    ret = RegCreateKeyExA(
            HKEY_LOCAL_MACHINE, subkey_smartcards_database, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key_db, NULL);
    ok(ret == ERROR_SUCCESS, "failed to open HKLM\\%s: %ld\n", subkey_smartcards_database, ret);
    if (ret) return ret;

    /* delete the test keys, in case they have not been deleted properly by the previous test run */
    RegDeleteTreeA(key_db, card_name_1);
    RegDeleteTreeA(key_db, card_name_2);

    /* create the test keys */
    ret = RegCreateKeyExA(key_db, card_name_1, 0, NULL, 0, KEY_WRITE, NULL, &key_card1, NULL);
    if (ret) return ret;
    ret = RegCreateKeyExA(key_db, card_name_2, 0, NULL, 0, KEY_WRITE, NULL, &key_card2, NULL);
    if (ret) return ret;

    ret = RegSetValueExA(
            key_card1, "Crypto Provider", 0, REG_SZ, (const BYTE *)crypto_provider_1, strlen(crypto_provider_1));
    if (ret) return ret;
    ret = RegSetValueExA(
            key_card2, "Crypto Provider", 0, REG_SZ, (const BYTE *)crypto_provider_2, strlen(crypto_provider_2));
    if (ret) return ret;

    ret = RegSetValueExA(key_card1,
            "Smart Card Key Storage Provider",
            0,
            REG_SZ,
            (const BYTE *)storage_provider_1,
            strlen(storage_provider_1));
    if (ret) return ret;
    ret = RegSetValueExA(key_card2,
            "Smart Card Key Storage Provider",
            0,
            REG_SZ,
            (const BYTE *)storage_provider_2,
            strlen(storage_provider_2));
    if (ret) return ret;

    ret = RegSetValueExA(key_card1, "80000001", 0, REG_SZ, (const BYTE *)dll_1, strlen(dll_1));
    if (ret) return ret;
    ret = RegSetValueExA(key_card2, "80000001", 0, REG_SZ, (const BYTE *)dll_2, strlen(dll_2));
    if (ret) return ret;

    ret = RegSetValueExA(key_card1, "ATR", 0, REG_BINARY, (const BYTE *)card_atr_1, sizeof(card_atr_1));
    if (ret) return ret;
    ret = RegSetValueExA(key_card2, "ATR", 0, REG_BINARY, (const BYTE *)card_atr_2, sizeof(card_atr_2));
    if (ret) return ret;

    /* don't create ATRMask for the first card, it will default to a mask full of ones */
    ret = RegSetValueExA(key_card2, "ATRMask", 0, REG_BINARY, (const BYTE *)atr_mask_2, sizeof(atr_mask_2));
    if (ret) return ret;

    ret = RegCloseKey(key_card1);
    if (ret) return ret;
    ret = RegCloseKey(key_card2);
    if (ret) return ret;
    ret = RegCloseKey(key_db);
    if (ret) return ret;

    return 0;
}

static void test_SCardGetCardTypeProviderName(void)
{
    LONG ret;
    SCARDCONTEXT ctx;
    DWORD len = 0;
    WCHAR *provider;

    /* test basic error conditions */
    provider = malloc(sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, NULL, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == SCARD_E_INVALID_PARAMETER, "should fail when card_type is null\n");

    ret = SCardGetCardTypeProviderNameW(0, card_name_1W, SCARD_PROVIDER_CARD_MODULE, provider, NULL);
    ok(ret == SCARD_E_INVALID_PARAMETER, "should fail when length is null\n");
    free(provider);

    /* test get length and allocate */
    ret = SCardGetCardTypeProviderNameW(0, card_name_1W, SCARD_PROVIDER_CARD_MODULE, NULL, &len);
    ok(len == 18, "invalid length: got %lu, expected %lu\n", len, 18L);
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, card_name_1W, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(len == 18, "invalid length: got %lu, expected %lu\n", len, 18L);
    ok(wcscmp(provider, L"opensc-driver.dll") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    free(provider);

    /* test lookup with a pre-allocated space */
    len = 4; /* too small */
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, card_name_1W, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == SCARD_E_INSUFFICIENT_BUFFER,
            "should have failed with SCARD_E_INSUFFICIENT_BUFFER but returned %#lx\n",
            ret);
    ok(len == 4, "the length should not have been set, but was %ld\n", len);
    ok(provider[0] == 0, "the provider should not have been set, but was %s\n", debugstr_w(provider));
    free(provider);

    len = 32; /* ok */
    provider = malloc(len * sizeof(WCHAR));
    for (int i = 0; i < 32; i++) { provider[i] = 0xcafe; }
    ret = SCardGetCardTypeProviderNameW(0, card_name_1W, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"opensc-driver.dll") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    for (int i = len; i < 32; i++)
    {
        ok(provider[i] == 0xcafe, "memory corruption: provider[%d] = %u\n", i, provider[i]);
    }
    free(provider);

    len = 32;
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, card_name_2W, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"cardoscm64.dll") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    free(provider);

    len = 32;
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, card_name_2W, SCARD_PROVIDER_KSP, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"Test Key Storage Provider") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    free(provider);

    len = 32;
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(0, card_name_2W, SCARD_PROVIDER_CSP, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"Test Crypto Provider") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    free(provider);

    /* test with a context */
    ret = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx);
    ok(ret == ERROR_SUCCESS, "failed to establish context: error %ld\n", ret);
    if (ret) return;

    len = 32;
    provider = calloc(len, sizeof(WCHAR));
    ret = SCardGetCardTypeProviderNameW(ctx, card_name_1W, SCARD_PROVIDER_CARD_MODULE, provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"opensc-driver.dll") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    free(provider);

    /* test with auto alloc */
    len = SCARD_AUTOALLOCATE;
    provider = NULL;
    ret = SCardGetCardTypeProviderNameW(ctx, card_name_1W, SCARD_PROVIDER_CARD_MODULE, (LPWSTR)&provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"opensc-driver.dll") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    ok(len == wcslen(provider) + 1,
            "bad length from SCardGetCardTypeProviderNameW: got %lu, expected %Iu\n",
            len,
            wcslen(provider) + 1);
    SCardFreeMemory(0, provider);

    len = SCARD_AUTOALLOCATE;
    provider = NULL;
    ret = SCardGetCardTypeProviderNameW(ctx, card_name_1W, SCARD_PROVIDER_CSP, (LPWSTR)&provider, &len);
    ok(ret == ERROR_SUCCESS, "SCardGetCardTypeProviderNameW returned an error: %#lx\n", ret);
    ok(wcscmp(provider, L"Microsoft Base Smart Card Crypto Provider") == 0,
            "bad output of SCardGetCardTypeProviderNameW: '%ls' (len %ld)\n",
            provider,
            len);
    ok(len == wcslen(provider) + 1,
            "bad length from SCardGetCardTypeProviderNameW: got %lu, expected %Iu\n",
            len,
            wcslen(provider) + 1);
    SCardFreeMemory(0, provider);

    ret = SCardReleaseContext(ctx);
    ok(ret == ERROR_SUCCESS, "failed to release context: error %ld\n", ret);
}

static void test_smartcard_db(void)
{
    LONG ret;
    HKEY key;

    /* prepare the database */
    ret = populate_smartcard_db();
    ok(ret == ERROR_SUCCESS, "failed to populate database: error %ld\n", ret);
    if (ret) return;

    /* run the tests */
    test_SCardGetCardTypeProviderName();

    /* delete the test keys */
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey_smartcards_database, 0, KEY_READ | KEY_WRITE, &key);
    ok(ret == ERROR_SUCCESS, "failed to open the smartcard database: error %ld\n", ret);
    ret = RegDeleteTreeA(key, card_name_1);
    ok(ret == ERROR_SUCCESS,
            "failed to delete HKLM\\%s\\%s: error %ld\n",
            subkey_smartcards_database,
            card_name_1,
            ret);
    ret = RegDeleteTreeA(key, card_name_2);
    ok(ret == ERROR_SUCCESS,
            "failed to delete HKLM\\%s\\%s: error %ld\n",
            subkey_smartcards_database,
            card_name_1,
            ret);
}

START_TEST(winscard)
{
    test_SCardEstablishContext();
    test_smartcard_db();
}
