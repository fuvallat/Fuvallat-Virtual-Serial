#include <string.h>

#include "settings.h"

DRIVER_CONFIG g_Config = { 0 };

static char* ReadRegistryValueStringUtf8(const wchar_t* path, const wchar_t* name) {
    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey);
    if (lRes != ERROR_SUCCESS) {
        return NULL;
    }

    WCHAR value[256];
    DWORD valueLength = sizeof(value);
    LONG queryRes = RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)value, &valueLength);
    RegCloseKey(hKey);

    if (queryRes != ERROR_SUCCESS) {
        return NULL;
    }

    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (utf8Length <= 0) {
        return NULL;
    }

    char* utf8Value = (char*)malloc(utf8Length);
    if (!utf8Value) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8Value, utf8Length, NULL, NULL) == 0) {
        free(utf8Value);
        return NULL;
    }

    return utf8Value;
}

static BOOL ReadRegistryValueDword(const wchar_t* path, const wchar_t* name, DWORD* outValue) {
    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey);
    if (lRes != ERROR_SUCCESS) {
        return FALSE;
    }

    DWORD type = 0;
    DWORD data = 0;
    DWORD dataSize = sizeof(data);
    LONG queryRes = RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&data, &dataSize);
    RegCloseKey(hKey);

    if (queryRes != ERROR_SUCCESS || type != REG_DWORD) {
        return FALSE;
    }

    *outValue = data;
    return TRUE;
}

static void ReadStringSetting(const wchar_t* path, const wchar_t* name, char* target, size_t targetSize, const char* defaultValue) {
    char* value = ReadRegistryValueStringUtf8(path, name);
    if (value) {
        strncpy(target, value, targetSize - 1);
        target[targetSize - 1] = '\0';
        free(value);
    } else {
        strncpy(target, defaultValue, targetSize - 1);
        target[targetSize - 1] = '\0';
    }
}

static void ReadIntSetting(const wchar_t* path, const wchar_t* name, int* target, int defaultValue) {
    DWORD val = 0;
    if (ReadRegistryValueDword(path, name, &val)) {
        *target = (int)val;
    } else {
        *target = defaultValue;
    }
}

static void WriteRegistryDefaultString(HKEY hKey, const wchar_t* name, const char* defaultValueUtf8) {
    DWORD type;
    DWORD dataSize = 0;
    LONG result = RegQueryValueExW(hKey, name, NULL, &type, NULL, &dataSize);

    if (result != ERROR_SUCCESS) {
        wchar_t buffer[256];
        int len = MultiByteToWideChar(CP_UTF8, 0, defaultValueUtf8, -1, buffer, _countof(buffer));
        if (len <= 0) {
            buffer[0] = L'\0';
        }

        RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)buffer, (DWORD)((wcslen(buffer) + 1) * sizeof(wchar_t)));
    }
}

static void WriteRegistryDefaultDword(HKEY hKey, const wchar_t* name, DWORD defaultValue) {
    DWORD type;
    DWORD dataSize = 0;
    LONG result = RegQueryValueExW(hKey, name, NULL, &type, NULL, &dataSize);

    if (result != ERROR_SUCCESS) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&defaultValue, sizeof(defaultValue));
    }
}

void CreateDefaultSettings(const char* port) {
    wchar_t keyPath[256];
    swprintf(keyPath, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%hs", port);

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, NULL, 0, KEY_WRITE | KEY_READ, NULL, &hKey, NULL) != ERROR_SUCCESS) return;

    // Port defaults
    WriteRegistryDefaultDword(hKey, L"Enable", 1);
    WriteRegistryDefaultDword(hKey, L"Mode", 0);
    WriteRegistryDefaultDword(hKey, L"Operation", 0);
    WriteRegistryDefaultDword(hKey, L"Device", 0);

    WriteRegistryDefaultDword(hKey, L"ForwardType", 0);
    WriteRegistryDefaultString(hKey, L"TargetPort", port);
    WriteRegistryDefaultString(hKey, L"TargetNetAddress", "");
    WriteRegistryDefaultDword(hKey, L"TargetNetPort", 0);
    WriteRegistryDefaultString(hKey, L"TargetPath", "");

    RegCloseKey(hKey);

    // -------------------------
    // DEVICE 0 – EMX100
    // -------------------------
    wchar_t devKey[256];
    swprintf(devKey, 256, L"%ws\\0", keyPath);

    HKEY hDev;
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, devKey, 0, NULL, 0, KEY_WRITE | KEY_READ, NULL, &hDev, NULL);

    WriteRegistryDefaultString(hDev, NULL, "Metrisoft EMX100"); // (Default)
    WriteRegistryDefaultString(hDev, L"LineEnd", "CRLF");
    WriteRegistryDefaultString(hDev, L"ErrorDefault", "AX/NACK");

    WriteRegistryDefaultString(hDev, L"Command0", "XA/VER?");
    WriteRegistryDefaultString(hDev, L"Answer0", "AX/VER:F250310");

    WriteRegistryDefaultString(hDev, L"Command1", "XA/N?");
    WriteRegistryDefaultString(hDev, L"Answer1", "AX/N=420F");

    WriteRegistryDefaultString(hDev, L"Command2", "XA/B?");
    WriteRegistryDefaultString(hDev, L"Answer2", "AX/B=420S");

    WriteRegistryDefaultString(hDev, L"Command3", "XA/T?");
    WriteRegistryDefaultString(hDev, L"Answer3", "AX/T=420X");

    RegCloseKey(hDev);

    // -------------------------
    // DEVICE 1 – Bilanciai D410
    // -------------------------
    swprintf(devKey, 256, L"%ws\\1", keyPath);
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, devKey, 0, NULL, 0, KEY_WRITE | KEY_READ, NULL, &hDev, NULL);

    WriteRegistryDefaultString(hDev, NULL, "Bilanciai D410");
    WriteRegistryDefaultString(hDev, L"LineEnd", "CRLF");
    WriteRegistryDefaultString(hDev, L"ErrorDefault", "??");

    WriteRegistryDefaultString(hDev, L"Command0", "XB");
    WriteRegistryDefaultString(hDev, L"Answer0", "420 kg B");

    WriteRegistryDefaultString(hDev, L"Command1", "XN");
    WriteRegistryDefaultString(hDev, L"Answer1", "420 kg NT");

    RegCloseKey(hDev);

    // -------------------------
    // DEVICE 2 – Vishay VT300
    // -------------------------
    swprintf(devKey, 256, L"%ws\\2", keyPath);
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, devKey, 0, NULL, 0, KEY_WRITE | KEY_READ, NULL, &hDev, NULL);

    WriteRegistryDefaultString(hDev, NULL, "Vishay VT300");
    WriteRegistryDefaultString(hDev, L"LineEnd", "CRLF");
    WriteRegistryDefaultString(hDev, L"ErrorDefault", "??");

    WriteRegistryDefaultString(hDev, L"Command0", "?");
    WriteRegistryDefaultString(hDev, L"Answer0", "P420 kg B");

    WriteRegistryDefaultString(hDev, L"Command1", "0");
    WriteRegistryDefaultString(hDev, L"Answer1", "420 kg NT");

    RegCloseKey(hDev);
}

void ReloadAllSettings(const char* port) {
    wchar_t baseKey[256];
    swprintf(baseKey, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%hs", port);

    LoadPortSettings(&g_Config.Port, baseKey);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        swprintf(baseKey, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Defaults\\");
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return;
        }
    }

    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyLen;

    int count = 0;
    while (TRUE) {
        subKeyLen = 256;
        LONG res = RegEnumKeyExW(hKey, index, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL);
        if (res != ERROR_SUCCESS) break;

        if (iswdigit(subKeyName[0])) count++;

        index++;
    }

    if (g_Config.Devices) {
        OutputDebugString(L"ReloadAllSettings: g_Config.Devices");
        for (int i = 0; i < g_Config.DeviceCount; i++) {
            DEVICE_PROFILE* d = &g_Config.Devices[i];
            OutputDebugString(L"ReloadAllSettings: DEVICE_PROFILE");

            free(d->Name);
            free(d->ErrorDefault);

            for (int j = 0; j < d->CommandCount; j++) {
                free(d->Commands[j].Command);
                free(d->Commands[j].Answer);
            }

            free(d->Commands);
        }
        free(g_Config.Devices);
    }

    g_Config.DeviceCount = count;
    g_Config.Devices = (DEVICE_PROFILE*)calloc(count, sizeof(DEVICE_PROFILE));

    index = 0;
    int deviceIndex = 0;

    while (TRUE) {
        subKeyLen = 256;
        LONG res = RegEnumKeyExW(hKey, index, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL);
        if (res != ERROR_SUCCESS) break;

        OutputDebugString(L"ReloadAllSettings: while TRUE");
        if (iswdigit(subKeyName[0])) {
            int deviceID = _wtoi(subKeyName);

            wchar_t fullPath[256];
            swprintf(fullPath, 256, L"%ws\\%ws", baseKey, subKeyName);
            OutputDebugString(L"ReloadAllSettings: load device");

            LoadDeviceProfile(&g_Config.Devices[deviceIndex], fullPath, deviceID);
            deviceIndex++;
        }

        index++;
    }

    RegCloseKey(hKey);
}

void LoadPortSettings(PORT_SETTINGS* port, const wchar_t* keyPath) {
    ReadIntSetting(keyPath, L"Enable", &port->Enable, 1);
    ReadIntSetting(keyPath, L"Mode", &port->Mode, 0);
    ReadIntSetting(keyPath, L"Operation", &port->Operation, 0);
    ReadIntSetting(keyPath, L"Device", &port->Device, 0);
    ReadIntSetting(keyPath, L"ForwardType", &port->ForwardType, 0);

    ReadStringSetting(keyPath, L"TargetPort", port->TargetPort, sizeof(port->TargetPort), "");
    ReadStringSetting(keyPath, L"TargetNetAddress", port->TargetNetAddress, sizeof(port->TargetNetAddress), "");

    DWORD portVal = 0;
    if (ReadRegistryValueDword(keyPath, L"TargetNetPort", &portVal))
        port->TargetNetPort = (int)portVal;
    else
        port->TargetNetPort = 0;
    ReadStringSetting(keyPath, L"TargetPath", port->TargetPath, sizeof(port->TargetPath), "");
}

static void NormalizeLineEnd(char* lineEnd) {
    if (!lineEnd || !lineEnd[0]) {
        strcpy(lineEnd, "\r\n");
        return;
    }

    char tmp[16];
    strncpy(tmp, lineEnd, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') *p = '\0';
    }

    if (_stricmp(tmp, "CR") == 0) {
        strcpy(lineEnd, "\r");
    } else if (_stricmp(tmp, "LF") == 0) {
        strcpy(lineEnd, "\n");
    } else if (_stricmp(tmp, "CRLF") == 0 || _stricmp(tmp, "CR+LF") == 0) {
        strcpy(lineEnd, "\r\n");
    } else if (_stricmp(tmp, "LFCR") == 0) {
        strcpy(lineEnd, "\n\r");
    } else {
        strcpy(lineEnd, "\r\n");
    }
}

void LoadDeviceProfile(DEVICE_PROFILE* dev, const wchar_t* keyPath, int deviceID) {
    memset(dev, 0, sizeof(DEVICE_PROFILE));
    dev->DeviceID = deviceID;

    dev->Name = ReadRegistryValueStringUtf8(keyPath, NULL);
    if (!dev->Name) dev->Name = _strdup("Unknown Device");

    char* le = ReadRegistryValueStringUtf8(keyPath, L"LineEnd");
    if (le) {
        strncpy(dev->LineEnd, le, sizeof(dev->LineEnd));
        free(le);
    } else {
        strcpy(dev->LineEnd, "\r\n");
    }
    NormalizeLineEnd(dev->LineEnd);

    dev->ErrorDefault = ReadRegistryValueStringUtf8(keyPath, L"ErrorDefault");
    if (!dev->ErrorDefault) dev->ErrorDefault = _strdup("NOK");

    dev->CommandCount = 0;
    dev->Commands = NULL;

    for (int i = 0; i < 200; i++) {
        wchar_t cmdKey[32], ansKey[32];
        swprintf(cmdKey, 32, L"Command%d", i);
        swprintf(ansKey, 32, L"Answer%d", i);
        OutputDebugString(L"LoadDeviceProfile: inside for");

        char* cmd = ReadRegistryValueStringUtf8(keyPath, cmdKey);
        if (!cmd) break;

        char* ans = ReadRegistryValueStringUtf8(keyPath, ansKey);

        dev->Commands = realloc(dev->Commands, sizeof(COMMAND_ENTRY) * (dev->CommandCount + 1));
        dev->Commands[dev->CommandCount].Command = cmd;
        dev->Commands[dev->CommandCount].Answer = ans ? ans : _strdup("");

        dev->CommandCount++;
    }
}