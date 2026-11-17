#pragma once

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int Enable;
    int Mode;
    int Operation;
    int Device;
    int ForwardType;
    char TargetPort[32];
    char TargetNetAddress[128];
    int TargetNetPort;
    char TargetPath[260];
} PORT_SETTINGS;

typedef struct {
    char* Command;
    char* Answer;
} COMMAND_ENTRY;

typedef struct {
    int DeviceID;
    char* Name;
    char LineEnd[8];
    char* ErrorDefault;
    int CommandCount;
    COMMAND_ENTRY* Commands;
} DEVICE_PROFILE;

typedef struct {
    PORT_SETTINGS Port;
    int DeviceCount;
    DEVICE_PROFILE* Devices;
} DRIVER_CONFIG;

extern DRIVER_CONFIG g_Config;

#ifdef __cplusplus
extern "C" {
#endif
    void CreateDefaultSettings(const char* port);
    void ReloadAllSettings(const char* port);
    void LoadPortSettings(PORT_SETTINGS* port, const wchar_t* keyPath);
    void LoadDeviceProfile(DEVICE_PROFILE* dev, const wchar_t* keyPath, int deviceID);
#ifdef __cplusplus
}
#endif