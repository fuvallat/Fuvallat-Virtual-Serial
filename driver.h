#pragma once

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;

NTSTATUS GlobalDeviceListInit(_In_ WDFDRIVER Driver);