#include "internal.h"

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	NTSTATUS status;
	WDF_DRIVER_CONFIG driverConfig;

	WDF_DRIVER_CONFIG_INIT(&driverConfig, EvtDeviceAdd);

	status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &driverConfig, WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfDriverCreate failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	status = GlobalDeviceListInit(WdfGetDriver());
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"GlobalDeviceListInit failed 0x%08X", status);
		OutputDebugString(msg);
		return status;
	}

	return status;
}

NTSTATUS EvtDeviceAdd( _In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit) {
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;

	status = DeviceCreate(Driver, DeviceInit, &deviceContext); if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"EvtDeviceAdd: DeviceCreate returned 0x%08X", status);
		OutputDebugString(msg);

		return status;
	}

	status = DeviceConfigure(deviceContext);
	{
		wchar_t msg[128];
		swprintf(msg, 128, L"EvtDeviceAdd: DeviceConfigure returned 0x%08X", status);
		OutputDebugString(msg);
	}
	if (!NT_SUCCESS(status)) {
		return status;
	}

	return status;
}