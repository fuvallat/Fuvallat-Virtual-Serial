#pragma once

#include "internal.h"

#define SYMBOLIC_LINK_NAME_LENGTH 32
#define SYMBOLIC_LINK_NAME_PREFIX L"\\DosDevices\\Global\\"
#define REG_PATH_DEVICEMAP L"HARDWARE\\DEVICEMAP"
#define SERIAL_DEVICE_MAP L"SERIALCOMM"
#define REG_VALUENAME_PORTNAME L"PortName"
#define REG_PATH_SERIALCOMM REG_PATH_DEVICEMAP L"\\" SERIAL_DEVICE_MAP

typedef enum _PORT_MODE {
    Emulator = 0,
    Forwarder = 1,
    Echo = 2,
    Fake = 3
} PORT_MODE;

typedef enum _FORWARD_MODE {
	Serial = 0,
	Network = 1,
	File = 2,
    None = 3
} FORWARD_MODE;

typedef struct _QUEUE_CONTEXT QUEUE_CONTEXT;
typedef struct _DEVICE_CONTEXT DEVICE_CONTEXT, * PDEVICE_CONTEXT;

struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    ULONG BaudRate;
    BOOLEAN DtrState;
    BOOLEAN RtsState;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_CHARS SpecialChars;
    SERIAL_HANDFLOW HandFlow;
    ULONG ModemControlRegister;
    ULONG FifoControlRegister;
    ULONG LineControlRegister;
    UCHAR ValidDataMask;
    SERIAL_TIMEOUTS Timeouts;
    ULONG CurrentWaitMask;
    ULONG PendingEvents;
    WDFREQUEST PendingWaitMaskRequest;
    BOOLEAN CreatedLegacyHardwareKey;
    PWSTR PdoName;

    BOOLEAN Enable;
    PORT_MODE Mode;

    PDEVICE_CONTEXT LinkedPort;
    BOOLEAN IsLinked;
    CRITICAL_SECTION LinkedPortLock;

    BOOLEAN IsForwarder;
    FORWARD_MODE ForwardMode;
    BOOLEAN ForwardConnected;
    BOOL StopForwardThread;
    HANDLE ForwardThreadHandle;

    WCHAR TargetPort[32];
    WCHAR TargetNetAddress[64];
    USHORT TargetNetPort;
    WCHAR TargetPath[260];
    HANDLE TargetHandle;

    CHAR port[32];

    HANDLE RegistryMonitorThread;

    QUEUE_CONTEXT* QueueContext;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

NTSTATUS DeviceCreate(_In_ WDFDRIVER Driver, _In_ PWDFDEVICE_INIT DeviceInit, _Out_ PDEVICE_CONTEXT* DeviceContext);
NTSTATUS DeviceConfigure(_In_ PDEVICE_CONTEXT DeviceContext);
NTSTATUS DeviceGetPdoName(_In_ PDEVICE_CONTEXT DeviceContext);
NTSTATUS DeviceWriteLegacyHardwareKey(_In_ PWSTR PdoName, _In_ PWSTR ComPort, _In_ WDFDEVICE Device);
void DeviceDeleteLegacyHardwareKey(PCWSTR pdoName, PCWSTR port);
EVT_WDF_DEVICE_CONTEXT_CLEANUP EvtDeviceCleanup;
VOID EvtDeviceSelfManagedIoCleanup(_In_ WDFDEVICE Device);
NTSTATUS EvtDeviceD0Entry(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE PreviousState);
NTSTATUS EvtDeviceD0Exit(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE TargetState);
ULONG GetBaudRate(_In_ PDEVICE_CONTEXT DeviceContext);
VOID SetBaudRate(_In_ PDEVICE_CONTEXT DeviceContext, _In_ ULONG BaudRate);
ULONG* GetModemControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext);
ULONG* GetFifoControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext);
ULONG* GetLineControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext);
VOID SetValidDataMask(_In_ PDEVICE_CONTEXT DeviceContext, _In_ UCHAR Mask);
VOID SetTimeouts(_In_ PDEVICE_CONTEXT DeviceContext, _In_ SERIAL_TIMEOUTS Timeouts);
VOID GetTimeouts(_In_ PDEVICE_CONTEXT DeviceContext, _Out_ SERIAL_TIMEOUTS* Timeouts);

DWORD WINAPI RegistryMonitorThread(LPVOID param);