#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include "internal.h"

#define SYMBOLIC_LINK_NAME_LENGTH 128

static WDFSPINLOCK g_DeviceListLock;
static PDEVICE_CONTEXT g_DeviceList[32];
static ULONG g_DeviceCount = 0;

extern DRIVER_CONFIG g_Config;

NTSTATUS GlobalDeviceListInit(_In_ WDFDRIVER Driver) {
    UNREFERENCED_PARAMETER(Driver);
    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    return WdfSpinLockCreate(&attrs, &g_DeviceListLock);
}

VOID GlobalDeviceListAdd(_In_ PDEVICE_CONTEXT Dev) {
    WdfSpinLockAcquire(g_DeviceListLock);
    if (g_DeviceCount < ARRAYSIZE(g_DeviceList)) {
        g_DeviceList[g_DeviceCount++] = Dev;
    }
    WdfSpinLockRelease(g_DeviceListLock);
}

VOID GlobalDeviceListRemove(_In_ PDEVICE_CONTEXT Dev) {
    WdfSpinLockAcquire(g_DeviceListLock);
    for (ULONG i = 0; i < g_DeviceCount; ++i) {
        if (g_DeviceList[i] == Dev) {
            g_DeviceList[i] = g_DeviceList[g_DeviceCount - 1];
            g_DeviceCount--;
            break;
        }
    }
    WdfSpinLockRelease(g_DeviceListLock);
}

PDEVICE_CONTEXT FindDeviceContextByComName(_In_ PCWSTR PortName) {
    PDEVICE_CONTEXT result = NULL;

    if (!PortName || !PortName[0]) return NULL;
    
    WdfSpinLockAcquire(g_DeviceListLock);

    for (ULONG i = 0; i < g_DeviceCount; ++i) {
        PDEVICE_CONTEXT dev = g_DeviceList[i];
        if (dev && dev->port[0]) {
            WCHAR wname[32] = { 0 };
            int len = MultiByteToWideChar(CP_UTF8, 0, dev->port, -1, wname, ARRAYSIZE(wname));
            
            if (len > 0 && _wcsicmp(wname, PortName) == 0) {
                result = dev;
                break;
            }
        }
    }

    WdfSpinLockRelease(g_DeviceListLock);
    return result;
}

BOOLEAN IsVirtualPortName(_In_ PCWSTR PortName) {
    if (!PortName || !PortName[0]) return FALSE;
    
    if (PortName[0] != L'C' && PortName[0] != L'c') return FALSE;
    if (PortName[1] != L'O' && PortName[1] != L'o') return FALSE;
    if (PortName[2] != L'M' && PortName[2] != L'm') return FALSE;
    
    return (PortName[3] >= L'0' && PortName[3] <= L'9');
}

NTSTATUS DeviceCreate(_In_ WDFDRIVER Driver, _In_ PWDFDEVICE_INIT DeviceInit, _Out_ PDEVICE_CONTEXT* DeviceContext) {
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    deviceAttributes.SynchronizationScope = WdfSynchronizationScopeDevice;
    deviceAttributes.EvtCleanupCallback = EvtDeviceCleanup;

    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDeviceSelfManagedIoCleanup = EvtDeviceSelfManagedIoCleanup;
    pnpCallbacks.EvtDeviceD0Entry = EvtDeviceD0Entry;
    pnpCallbacks.EvtDeviceD0Exit = EvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Error: WdfDeviceCreate failed 0x%x", status);
        OutputDebugString(msg);

        return status;
    }

    deviceContext = GetDeviceContext(device);
    ZeroMemory(deviceContext, sizeof(*deviceContext));

    deviceContext->Device = device;
    deviceContext->QueueContext = NULL;
    deviceContext->LinkedPort = NULL;
    deviceContext->IsLinked = FALSE;
    deviceContext->Enable = TRUE;
    deviceContext->StopForwardThread = FALSE;
    deviceContext->ForwardThreadHandle = NULL;
    deviceContext->BaudRate = 9600;
    deviceContext->CurrentWaitMask = 0;
    deviceContext->PendingEvents = 0;
    deviceContext->PendingWaitMaskRequest = NULL;
    deviceContext->LineControl.StopBits = STOP_BIT_1;
    deviceContext->LineControl.Parity = NO_PARITY;
    deviceContext->LineControl.WordLength = 8;
    deviceContext->SpecialChars.XonChar = 17;
    deviceContext->SpecialChars.XonChar = 19;
    deviceContext->HandFlow.ControlHandShake = SERIAL_DTR_CONTROL;
    deviceContext->HandFlow.FlowReplace = SERIAL_RTS_CONTROL;
    deviceContext->HandFlow.XonLimit = 2048;
    deviceContext->HandFlow.XoffLimit = 512;
    InitializeCriticalSection(&deviceContext->LinkedPortLock);

    GlobalDeviceListAdd(deviceContext);

    *DeviceContext = deviceContext;

    return status;
}

NTSTATUS DeviceConfigure(_In_ PDEVICE_CONTEXT DeviceContext) {
    NTSTATUS status;
    WDFDEVICE device = DeviceContext->Device;
    WDFKEY key;
    LPGUID guid;
    errno_t errorNo;

    DECLARE_CONST_UNICODE_STRING(portName, REG_VALUENAME_PORTNAME);
    DECLARE_UNICODE_STRING_SIZE(port, 10);
    DECLARE_UNICODE_STRING_SIZE(symbolicLinkName, SYMBOLIC_LINK_NAME_LENGTH);

    guid = (LPGUID)&GUID_DEVINTERFACE_COMPORT;

    status = WdfDeviceCreateDeviceInterface(device, guid, NULL);
    if (!NT_SUCCESS(status)) {
        OutputDebugString(L"Error: Cannot create device interface");
        goto exit;
    }

    status = WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status)) {
        OutputDebugString(L"Error: Failed to retrieve device hardware key root");
        goto exit;
    }

    status = WdfRegistryQueryUnicodeString(key, &portName, NULL, &port);
    if (!NT_SUCCESS(status)) {
        OutputDebugString(L"Error: Failed to read PortName");
        goto exit;
    }
    OutputDebugString(L"Driver loaded successfully\n");

    char portUtf8[32] = {0};
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, port.Buffer, -1, portUtf8, sizeof(portUtf8), NULL, NULL);
    if (utf8Length <= 0) {
        OutputDebugString(L"Error: Failed to convert COM port name to UTF-8");
        status = STATUS_NOT_SUPPORTED;
        goto exit;
    }
    strncpy(DeviceContext->port, portUtf8, sizeof(DeviceContext->port));
    DeviceContext->port[sizeof(DeviceContext->port) - 1] = '\0';

    ReloadAllSettings(portUtf8);

    DeviceContext->Mode = g_Config.Port.Mode;

    if (DeviceContext->Mode == Forwarder) {
        switch (g_Config.Port.ForwardType) {
            case 0: DeviceContext->ForwardMode = Serial;   break;
            case 1: DeviceContext->ForwardMode = Network;    break;
            case 2: DeviceContext->ForwardMode = File;   break;
            default: DeviceContext->ForwardMode = Serial;  break;
        }
    } else {
        DeviceContext->ForwardMode = None;
    }
    mbstowcs(DeviceContext->TargetPort, g_Config.Port.TargetPort, ARRAYSIZE(DeviceContext->TargetPort));
    mbstowcs(DeviceContext->TargetNetAddress, g_Config.Port.TargetNetAddress, ARRAYSIZE(DeviceContext->TargetNetAddress));
    DeviceContext->TargetNetPort = g_Config.Port.TargetNetPort;
    mbstowcs(DeviceContext->TargetPath, g_Config.Port.TargetPath, ARRAYSIZE(DeviceContext->TargetPath));
    DeviceContext->Enable = g_Config.Port.Enable;

    DeviceContext->IsLinked = FALSE;
    DeviceContext->LinkedPort = NULL;

    if (IsVirtualPortName(DeviceContext->TargetPort)) {
        PDEVICE_CONTEXT other = FindDeviceContextByComName(DeviceContext->TargetPort);

        if (other) {
            EnterCriticalSection(&DeviceContext->LinkedPortLock);
            DeviceContext->IsLinked = TRUE;
            DeviceContext->LinkedPort = other;
            LeaveCriticalSection(&DeviceContext->LinkedPortLock);

            EnterCriticalSection(&other->LinkedPortLock);
            other->IsLinked = TRUE;
            other->LinkedPort = DeviceContext;
            LeaveCriticalSection(&other->LinkedPortLock);
            
            wchar_t msg[128];
            swprintf(msg, 128, L"Linked ports successfully connected: %hs <-> %hs", DeviceContext->port, other->port);
            OutputDebugString(msg);
        } else {
            wchar_t msg[128];
            swprintf(msg, 128, L"Target virtual port '%ws' not found! (TargetPort setting for %hs)", DeviceContext->TargetPort, DeviceContext->port);
            OutputDebugString(msg);
        }
    } else {
        wchar_t msg[128];
        swprintf(msg, 128, L"TargetPort '%ws' is not a virtual port name (not COM format)", DeviceContext->TargetPort);
        OutputDebugString(msg);
    }

#pragma warning(suppress: 6054 26035)
    symbolicLinkName.Length = (USHORT)((wcslen(SYMBOLIC_LINK_NAME_PREFIX) + wcslen(port.Buffer)) * sizeof(wchar_t));
    if (symbolicLinkName.Length >= symbolicLinkName.MaximumLength) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Error: Buffer overflow when creating COM port name. Size is %d, buffer length is %d", symbolicLinkName.Length, symbolicLinkName.MaximumLength);
        OutputDebugString(msg);

        status = STATUS_BUFFER_OVERFLOW;
        goto exit;
    }

    errorNo = wcscpy_s(symbolicLinkName.Buffer, SYMBOLIC_LINK_NAME_LENGTH, SYMBOLIC_LINK_NAME_PREFIX);
    if (errorNo != 0) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Failed to copy %ws to buffer with error %d", SYMBOLIC_LINK_NAME_PREFIX, errorNo);
        OutputDebugString(msg);

        status = STATUS_NOT_SUPPORTED;
        goto exit;
    }

    errorNo = wcscat_s(symbolicLinkName.Buffer, SYMBOLIC_LINK_NAME_LENGTH, port.Buffer);
    if (errorNo != 0) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Failed to copy %ws to buffer with error %d", port.Buffer, errorNo);
        OutputDebugString(msg);

        status = STATUS_NOT_SUPPORTED;
        goto exit;
    }

    status = WdfDeviceCreateSymbolicLink(device, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Error: Cannot create symbolic link %ws", symbolicLinkName.Buffer);
        OutputDebugString(msg);

        goto exit;
    }

    status = DeviceGetPdoName(DeviceContext);
    if (!NT_SUCCESS(status)) {
        goto exit;
    }

    status = DeviceWriteLegacyHardwareKey(DeviceContext->PdoName, port.Buffer, DeviceContext->Device);
    if (NT_SUCCESS(status)) {
        DeviceContext->CreatedLegacyHardwareKey = TRUE;
    }

    DeviceContext->TargetHandle = NULL;
    DeviceContext->ForwardConnected = FALSE;

    status = QueueCreate(DeviceContext);
    if (!NT_SUCCESS(status)) {
        goto exit;
    }

exit:
 {
        wchar_t msg[128];
        swprintf(msg, 128, L"DeviceConfigure: exiting with status=0x%08X", status);
        OutputDebugString(msg);
    }

    DeviceContext->BaudRate = 9600;
    DeviceContext->DtrState = FALSE;
    DeviceContext->RtsState = FALSE;
    DeviceContext->ValidDataMask = 0xFF;
    DeviceContext->LineControlRegister = 0;

    RtlZeroMemory(&DeviceContext->HandFlow, sizeof(DeviceContext->HandFlow));
    RtlZeroMemory(&DeviceContext->SpecialChars, sizeof(DeviceContext->SpecialChars));
    RtlZeroMemory(&DeviceContext->Timeouts, sizeof(DeviceContext->Timeouts));

    DeviceContext->Timeouts.ReadIntervalTimeout = MAXULONG;
    DeviceContext->Timeouts.ReadTotalTimeoutConstant = 0;
    DeviceContext->Timeouts.ReadTotalTimeoutMultiplier = 0;
    DeviceContext->Timeouts.WriteTotalTimeoutMultiplier = 0;
    DeviceContext->Timeouts.WriteTotalTimeoutConstant = 0;

    DeviceContext->CurrentWaitMask = 0;
    DeviceContext->PendingEvents = 0;
    DeviceContext->PendingWaitMaskRequest = NULL;

    DeviceContext->LineControl.StopBits = STOP_BIT_1;
    DeviceContext->LineControl.Parity = NO_PARITY;
    DeviceContext->LineControl.WordLength = 8;

    DeviceContext->SpecialChars.XonChar = 17;
    DeviceContext->SpecialChars.XonChar = 19;

    DeviceContext->HandFlow.ControlHandShake = SERIAL_DTR_CONTROL;
    DeviceContext->HandFlow.FlowReplace = SERIAL_RTS_CONTROL;
    DeviceContext->HandFlow.XonLimit = 2048;
    DeviceContext->HandFlow.XoffLimit = 512;

    return status;
}

NTSTATUS DeviceGetPdoName(_In_ PDEVICE_CONTEXT DeviceContext) {
    NTSTATUS status;
	WDFDEVICE device = DeviceContext->Device;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDFMEMORY memory;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = device;

	status = WdfDeviceAllocAndQueryProperty( device, DevicePropertyPhysicalDeviceObjectName, NonPagedPoolNx, &attributes, &memory);
	if (!NT_SUCCESS(status)) {
        OutputDebugString(L"Error: Failed to query PDO name");

        goto exit;
	}

	DeviceContext->PdoName = (PWCHAR)WdfMemoryGetBuffer(memory, NULL);
    wchar_t msg[128];
    swprintf(msg, 128, L"PDO Name is %ws", DeviceContext->PdoName);
    OutputDebugString(msg);

exit:
    return status;
}

NTSTATUS DeviceWriteLegacyHardwareKey(_In_ PWSTR PdoName, _In_ PWSTR ComPort, _In_ WDFDEVICE Device) {
    WDFKEY key = NULL;
	NTSTATUS status;
	UNICODE_STRING pdoString = { 0 };
	UNICODE_STRING port = { 0 };

	DECLARE_CONST_UNICODE_STRING(deviceSubkey, SERIAL_DEVICE_MAP);

	RtlInitUnicodeString(&pdoString, PdoName);
	RtlInitUnicodeString(&port, ComPort);

	status = WdfDeviceOpenDevicemapKey(Device, &deviceSubkey, KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key);
	if (!NT_SUCCESS(status)) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Error: Failed to open DEVICEMAP\\SERIALCOMM key 0x%x", status);
        OutputDebugString(msg);

        goto exit;
	}

	status = WdfRegistryAssignUnicodeString(key, &pdoString, &port);
	if (!NT_SUCCESS(status)) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Error: Failed to write to DEVICEMAP\\SERIALCOMM key 0x%x", status);
        OutputDebugString(msg);

        goto exit;
	}

exit:
	if (key != NULL) {
		WdfRegistryClose(key);
		key = NULL;
	}

	return status;
}

VOID EvtDeviceCleanup(_In_ WDFOBJECT Object) {
    WDFDEVICE device = (WDFDEVICE)Object;
    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);

    wchar_t msg[128];
    swprintf(msg, 128, L"EvtDeviceCleanup: Starting cleanup for port %hs", deviceContext->port);
    OutputDebugString(msg);

    if (deviceContext->ForwardThreadHandle && deviceContext->ForwardThreadHandle != INVALID_HANDLE_VALUE) {
        OutputDebugString(L"EvtDeviceCleanup: Signaling ForwardReaderThread to stop");
        deviceContext->StopForwardThread = TRUE;
        
        DWORD waitResult = WaitForSingleObject(deviceContext->ForwardThreadHandle, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            OutputDebugString(L"EvtDeviceCleanup: WARNING - ForwardReaderThread did not exit within 5 seconds!");
        } else if (waitResult == WAIT_OBJECT_0) {
            OutputDebugString(L"EvtDeviceCleanup: ForwardReaderThread exited successfully");
        }
        
        CloseHandle(deviceContext->ForwardThreadHandle);
        deviceContext->ForwardThreadHandle = NULL;
    }

    EnterCriticalSection(&deviceContext->LinkedPortLock);
    PDEVICE_CONTEXT linkedPort = deviceContext->LinkedPort;
    deviceContext->IsLinked = FALSE;
    deviceContext->LinkedPort = NULL;
    LeaveCriticalSection(&deviceContext->LinkedPortLock);

    GlobalDeviceListRemove(deviceContext);

    if (linkedPort) {
        EnterCriticalSection(&linkedPort->LinkedPortLock);
        if (linkedPort->LinkedPort == deviceContext) {
            linkedPort->IsLinked = FALSE;
            linkedPort->LinkedPort = NULL;
            wchar_t warningMsg[128];
            swprintf(warningMsg, 128, L"EvtDeviceCleanup: Unlinked %hs from its paired port", deviceContext->port);
            OutputDebugString(warningMsg);
        }
        LeaveCriticalSection(&linkedPort->LinkedPortLock);
    }

    if (deviceContext->PendingWaitMaskRequest) {
        WDFREQUEST req = deviceContext->PendingWaitMaskRequest;
        deviceContext->PendingWaitMaskRequest = NULL;
        WdfRequestComplete(req, STATUS_CANCELLED);
    }

    DeleteCriticalSection(&deviceContext->LinkedPortLock);

    if (deviceContext->TargetHandle) {
        CloseHandle(deviceContext->TargetHandle);
        deviceContext->TargetHandle = NULL;
    }

    if (deviceContext->RegistryMonitorThread) {
        deviceContext->StopForwardThread = TRUE;
        WaitForSingleObject(deviceContext->RegistryMonitorThread, 2000);
        CloseHandle(deviceContext->RegistryMonitorThread);
        deviceContext->RegistryMonitorThread = NULL;
    }
}

VOID EvtDeviceSelfManagedIoCleanup(_In_ WDFDEVICE Device) {
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);

    wchar_t msg[128];
    swprintf(msg, 128, L"Cleanup: deleting legacy PortName for %ws", ctx->PdoName);
    OutputDebugString(msg);

    WCHAR portW[32];
    mbstowcs_s(NULL, portW, ARRAYSIZE(portW), ctx->port, _TRUNCATE);
    DeviceDeleteLegacyHardwareKey(ctx->PdoName, portW);
}

NTSTATUS EvtDeviceD0Entry(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE PreviousState) {
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);

    wchar_t msg[128];
    swprintf(msg, 128, L"EvtDeviceD0Entry: Device %hs entering D0", ctx->port);
    OutputDebugString(msg);

    return STATUS_SUCCESS;
}

NTSTATUS EvtDeviceD0Exit(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE TargetState) {
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);

    wchar_t msg[128];
    swprintf(msg, 128, L"EvtDeviceD0Exit: Device %hs leaving D0", ctx->port);
    OutputDebugString(msg);

    return STATUS_SUCCESS;
}

void DeviceDeleteLegacyHardwareKey(PCWSTR pdoName, PCWSTR port) {
    WCHAR devParamsPath[512];
    swprintf(devParamsPath, ARRAYSIZE(devParamsPath), L"SYSTEM\\CurrentControlSet\\Enum\\%ws\\Device Parameters", pdoName);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, devParamsPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"PortName");
        RegCloseKey(hKey);
    }

    WCHAR settingsPath[512];
    swprintf(settingsPath, ARRAYSIZE(settingsPath), L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%ws", port);

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, settingsPath);
}

ULONG GetBaudRate(_In_ PDEVICE_CONTEXT DeviceContext) {
	return DeviceContext->BaudRate;
}

VOID SetBaudRate(_In_ PDEVICE_CONTEXT DeviceContext, _In_ ULONG BaudRate) {
	DeviceContext->BaudRate = BaudRate;
}

ULONG* GetModemControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext) {
	return &DeviceContext->ModemControlRegister;
}

ULONG* GetFifoControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext) {
	return &DeviceContext->FifoControlRegister;
}

ULONG* GetLineControlRegisterPtr(_In_ PDEVICE_CONTEXT DeviceContext) {
	return &DeviceContext->LineControlRegister;
}

VOID SetValidDataMask(_In_ PDEVICE_CONTEXT DeviceContext, _In_ UCHAR Mask) {
	DeviceContext->ValidDataMask = Mask;
}

VOID SetTimeouts(_In_ PDEVICE_CONTEXT DeviceContext, _In_ SERIAL_TIMEOUTS Timeouts) {
	DeviceContext->Timeouts = Timeouts;
}

VOID GetTimeouts(_In_ PDEVICE_CONTEXT DeviceContext, _Out_ SERIAL_TIMEOUTS* Timeouts) {
	*Timeouts = DeviceContext->Timeouts;
}
