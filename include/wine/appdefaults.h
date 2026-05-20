#ifndef __WINE_APPDEFAULTS_H
#define __WINE_APPDEFAULTS_H

static inline BOOL wine_get_appdefaults_reg_sz(const WCHAR *section, const WCHAR *name,
                                               WCHAR *buffer, DWORD size)
{
    static const WCHAR appdefaultsW[] = L"Software\\Wine\\AppDefaults";
    static const WCHAR wineW[] = L"Software\\Wine\\";
    HKEY defkey = 0, appkey = 0, tmpkey;
    WCHAR keypath[MAX_PATH + 64];
    WCHAR appname[MAX_PATH + 1], *p1, *p2, *p;
    DWORD len, type;
    BOOL found = FALSE;

    if (!section || !name || !buffer || size < sizeof(WCHAR)) return FALSE;
    buffer[0] = 0;

    lstrcpyW(keypath, wineW);
    if (lstrlenW(keypath) + lstrlenW(section) < ARRAY_SIZE(keypath))
    {
        lstrcatW(keypath, section);
        if (RegOpenKeyW(HKEY_CURRENT_USER, keypath, &defkey)) defkey = 0;
    }

    if (!RegOpenKeyW(HKEY_CURRENT_USER, appdefaultsW, &tmpkey))
    {
        len = GetModuleFileNameW(NULL, appname, ARRAY_SIZE(appname));
        if (len && len < ARRAY_SIZE(appname))
        {
            p1 = wcsrchr(appname, '\\');
            p2 = wcsrchr(appname, '/');
            p = p1;
            if (p2 && (!p || p2 > p)) p = p2;
            if (p) p++;
            else p = appname;

            if (lstrlenW(p) + 1 + lstrlenW(section) < ARRAY_SIZE(keypath))
            {
                lstrcpyW(keypath, p);
                lstrcatW(keypath, L"\\");
                lstrcatW(keypath, section);
                if (RegOpenKeyW(tmpkey, keypath, &appkey)) appkey = 0;
            }
        }
        RegCloseKey(tmpkey);
    }

    if (appkey)
    {
        len = size;
        if (!RegQueryValueExW(appkey, name, 0, &type, (BYTE *)buffer, &len) &&
            (type == REG_SZ || type == REG_EXPAND_SZ))
            found = TRUE;
    }

    if (!found && defkey)
    {
        len = size;
        if (!RegQueryValueExW(defkey, name, 0, &type, (BYTE *)buffer, &len) &&
            (type == REG_SZ || type == REG_EXPAND_SZ))
            found = TRUE;
    }

    if (found)
    {
        if (len > size - sizeof(WCHAR)) len = size - sizeof(WCHAR);
        buffer[len / sizeof(WCHAR)] = 0;
    }
    else
        buffer[0] = 0;

    if (appkey) RegCloseKey(appkey);
    if (defkey) RegCloseKey(defkey);
    return found;
}

#endif /* __WINE_APPDEFAULTS_H */
