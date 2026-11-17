#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "serial.h"
#include "internal.h"
#include "ringbuffer.h"
#include "queue.h"
#include "forward_network.h"

extern DRIVER_CONFIG g_Config;

static const DEVICE_PROFILE* GetActiveProfile(void) {
	int activeId = g_Config.Port.Device;

	for (int i = 0; i < g_Config.DeviceCount; ++i) {
		if (g_Config.Devices[i].DeviceID == activeId) {
			OutputDebugString(L"GetActiveProfile: End");
			return &g_Config.Devices[i];
		}
	}

	return NULL;
}

void ForcedDisconnect(PDEVICE_CONTEXT dev) {
	PQUEUE_CONTEXT qc = dev->QueueContext;

	WdfIoQueuePurge(qc->Queue, WDF_NO_EVENT_CALLBACK, WDF_NO_CONTEXT);
	WdfIoQueuePurge(qc->ReadQueue, WDF_NO_EVENT_CALLBACK, WDF_NO_CONTEXT);
	WdfIoQueuePurge(qc->WaitMaskQueue, WDF_NO_EVENT_CALLBACK, WDF_NO_CONTEXT);

	WdfIoQueueStart(qc->Queue);
	WdfIoQueueStart(qc->ReadQueue);
	WdfIoQueueStart(qc->WaitMaskQueue);

	dev->CurrentWaitMask = 0;

	RingBufferClear(&qc->RingBuffer);
}

void ApplySettingsToDeviceContext(PDEVICE_CONTEXT dev) {
	dev->Enable = g_Config.Port.Enable;
	dev->Mode = g_Config.Port.Mode;

	if (dev->Mode == Forwarder) {
		switch (g_Config.Port.ForwardType) {
			case 0: dev->ForwardMode = Serial; break;
			case 1: dev->ForwardMode = Network; break;
			case 2: dev->ForwardMode = File; break;
			default: dev->ForwardMode = Serial; break;
		}
	} else {
		dev->ForwardMode = None;
	}

	mbstowcs(dev->TargetPort, g_Config.Port.TargetPort, ARRAYSIZE(dev->TargetPort));
	mbstowcs(dev->TargetNetAddress, g_Config.Port.TargetNetAddress, ARRAYSIZE(dev->TargetNetAddress));
	dev->TargetNetPort = g_Config.Port.TargetNetPort;
	mbstowcs(dev->TargetPath, g_Config.Port.TargetPath, ARRAYSIZE(dev->TargetPath));

	if (dev->TargetHandle) {
		CloseHandle(dev->TargetHandle);
		dev->TargetHandle = NULL;
	}
	dev->ForwardConnected = FALSE;
}

DWORD WINAPI RegistryMonitorThread(LPVOID param) {
	PDEVICE_CONTEXT dev = (PDEVICE_CONTEXT)param;

	wchar_t keyPath[256];
	swprintf(keyPath, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%hs", dev->port);

	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_NOTIFY, &hKey) != ERROR_SUCCESS) {
		OutputDebugString(L"RegistryMonitor: Cannot open key");
		return 0;
	}

	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!hEvent) {
		RegCloseKey(hKey);
		return 0;
	}

	while (!dev->StopForwardThread) {
		LONG status = RegNotifyChangeKeyValue(hKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_NAME, hEvent, TRUE);

		if (status != ERROR_SUCCESS) {
			Sleep(500);
			continue;
		}

		WaitForSingleObject(hEvent, INFINITE);
		ResetEvent(hEvent);

		ReloadAllSettings(dev->port);
		ApplySettingsToDeviceContext(dev);

		ForcedDisconnect(dev);
//		WdfDeviceSetFailed(dev->Device, WdfDeviceFailedAttemptRestart);
//		WdfDeviceCreateDeviceInterface(dev->Device, &GUID_DEVINTERFACE_COMPORT, NULL);
	}

	CloseHandle(hEvent);
	RegCloseKey(hKey);
	return 0;
}

NTSTATUS QueueCreate(_In_ PDEVICE_CONTEXT DeviceContext) {
	NTSTATUS status;
	WDFDEVICE dev = DeviceContext->Device;
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDF_OBJECT_ATTRIBUTES queueAttributes;
	WDFQUEUE defaultQueue;
	PQUEUE_CONTEXT qc;

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoRead = EvtIoRead;
	queueConfig.EvtIoWrite = EvtIoWrite;
	queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);

	status = WdfIoQueueCreate(dev, &queueConfig, &queueAttributes, &defaultQueue);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfIoQueueCreate default queue failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	qc = GetQueueContext(defaultQueue);
	qc->Queue = defaultQueue;
	qc->DeviceContext = DeviceContext;

	DeviceContext->QueueContext = qc;

	RingBufferInitialize(&qc->RingBuffer, qc->Buffer, sizeof(qc->Buffer));
	qc->CommandMatchState = COMMAND_MATCH_STATE_IDLE;
	qc->ConnectCommand = FALSE;
	qc->IgnoreNextChar = FALSE;
	qc->ConnectionStateChanged = FALSE;
	qc->CurrentlyConnected = FALSE;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	status = WdfIoQueueCreate(dev, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &qc->ReadQueue);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfIoQueueCreate ReadQueue failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	status = WdfIoQueueCreate(dev, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &qc->WaitMaskQueue);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfIoQueueCreate WaitMaskQueue failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	if (!DeviceContext->IsLinked && DeviceContext->Mode == Forwarder && DeviceContext->ForwardMode != None) {
		HANDLE hThread = CreateThread(NULL, 0, ForwardReaderThread, qc, 0, NULL);
		if (hThread != NULL) {
			DeviceContext->ForwardThreadHandle = hThread;
			OutputDebugString(L"ForwardReaderThread: Started successfully");
		} else {
			OutputDebugString(L"ForwardReaderThread: CreateThread failed");
		}
	}

	DeviceContext->RegistryMonitorThread = CreateThread(
		NULL, 0, RegistryMonitorThread, DeviceContext, 0, NULL
	);

	DeviceContext->ForwardConnected = FALSE;

	return STATUS_SUCCESS;
}

NTSTATUS RequestCopyFromBuffer(_In_ WDFREQUEST Request, _In_ PVOID SourceBuffer, _In_ size_t NumBytesToCopyFrom) {
	NTSTATUS status;
	WDFMEMORY memory;

	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfRequestRetrieveOutputMemory failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory, 0, SourceBuffer, NumBytesToCopyFrom);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfMemoryCopyFromBuffer failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	WdfRequestSetInformation(Request, NumBytesToCopyFrom);

	return status;
}

NTSTATUS RequestCopyToBuffer(_In_ WDFREQUEST Request, _In_ PVOID DestinationBuffer, _In_ size_t NumBytesToCopyTo) {
	NTSTATUS status;
	WDFMEMORY memory;

	status = WdfRequestRetrieveInputMemory(Request, &memory);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfRequestRetrieveInputMemory failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	status = WdfMemoryCopyToBuffer(memory, 0, DestinationBuffer, NumBytesToCopyTo);
	if (!NT_SUCCESS(status)) {
		wchar_t msg[128];
		swprintf(msg, 128, L"Error: WdfMemoryCopyToBuffer failed 0x%x", status);
		OutputDebugString(msg);

		return status;
	}

	return status;
}

VOID EvtCancelWaitMask(WDFREQUEST Request) {
	WdfRequestComplete(Request, STATUS_CANCELLED);
}

VOID EvtIoDeviceControl( _In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t OutputBufferLength, _In_ size_t InputBufferLength, _In_ ULONG IoControlCode) {
	NTSTATUS status;
	PQUEUE_CONTEXT qc = GetQueueContext(Queue);
	PDEVICE_CONTEXT dev = GetDeviceContext(WdfIoQueueGetDevice(Queue));
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	switch (IoControlCode) {
		case IOCTL_SERIAL_SET_BAUD_RATE: {
			PSERIAL_BAUD_RATE baud;
			size_t length = 0;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_BAUD_RATE), (PVOID*)&baud, &length);
			if (!NT_SUCCESS(status)) break;

			dev->BaudRate = baud->BaudRate ? baud->BaudRate : 9600;

			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_GET_BAUD_RATE: {
			PSERIAL_BAUD_RATE out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_BAUD_RATE), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			out->BaudRate = dev->BaudRate;

			WdfRequestSetInformation(Request, sizeof(SERIAL_BAUD_RATE));
			break;
		}
		case IOCTL_SERIAL_SET_MODEM_CONTROL: {
			ULONG* modemControlRegister = GetModemControlRegisterPtr(dev);

			ASSERT(modemControlRegister);

			status = RequestCopyToBuffer(Request, modemControlRegister, sizeof(ULONG));
			break;
		}
		case IOCTL_SERIAL_GET_MODEM_CONTROL: {
			ULONG* modemControlRegister = GetModemControlRegisterPtr(dev);

			ASSERT(modemControlRegister);

			status = RequestCopyFromBuffer(Request, modemControlRegister, sizeof(ULONG));
			break;
		}
		case IOCTL_SERIAL_SET_FIFO_CONTROL: {
			ULONG* fifoControlRegister = GetFifoControlRegisterPtr(dev);
			ASSERT(fifoControlRegister);
			status = RequestCopyToBuffer(Request, fifoControlRegister, sizeof(ULONG));
			break;
		}
		case IOCTL_SERIAL_GET_LINE_CONTROL: {
			PSERIAL_LINE_CONTROL out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			out->StopBits = STOP_BIT_1;
			out->Parity = NO_PARITY;
			out->WordLength = 8;

			WdfRequestSetInformation(Request, sizeof(SERIAL_LINE_CONTROL));
			break;
		}
		case IOCTL_SERIAL_SET_LINE_CONTROL: {
			PSERIAL_LINE_CONTROL in;
			size_t len;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), (PVOID*)&in, &len);
			if (!NT_SUCCESS(status)) break;

			dev->LineControl = *in;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_GET_TIMEOUTS: {
			PSERIAL_TIMEOUTS out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_TIMEOUTS), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			*out = dev->Timeouts;

			WdfRequestSetInformation(Request, sizeof(SERIAL_TIMEOUTS));
			break;
		}
		case IOCTL_SERIAL_SET_TIMEOUTS: {
			PSERIAL_TIMEOUTS in;
			size_t len;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_TIMEOUTS), (PVOID*)&in, &len);
			if (!NT_SUCCESS(status)) break;

			dev->Timeouts = *in;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_CONFIG_SIZE: {
			PULONG out;
			size_t length = 0;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID*)&out, &length);
			if (!NT_SUCCESS(status)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			*out = 0;
			WdfRequestSetInformation(Request, sizeof(ULONG));
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_GET_WAIT_MASK: {
			PULONG out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			*out = dev->CurrentWaitMask;

			WdfRequestSetInformation(Request, sizeof(ULONG));
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_WAIT_ON_MASK: {
			if (dev->PendingWaitMaskRequest) {
				WdfRequestComplete(dev->PendingWaitMaskRequest, STATUS_CANCELLED);
			}

			dev->PendingWaitMaskRequest = Request;

			ULONG signaled = dev->PendingEvents & dev->CurrentWaitMask;
			if (signaled) {
				ServicePendingWaitMask(qc);
			}

			return;
		}
		case IOCTL_SERIAL_SET_WAIT_MASK: {
			PULONG mask;
			size_t length = 0;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&mask, &length);
			if (!NT_SUCCESS(status) || length < sizeof(ULONG)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			qc->DeviceContext->CurrentWaitMask = *mask;

			if (dev->PendingWaitMaskRequest) {
				WdfRequestComplete(dev->PendingWaitMaskRequest, STATUS_CANCELLED);
				dev->PendingWaitMaskRequest = NULL;
			}

			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_GET_COMMSTATUS: {
			PSERIAL_STATUS out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_STATUS), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			RtlZeroMemory(out, sizeof(SERIAL_STATUS));

			out->AmountInInQueue = RingBufferGetUsed(&qc->RingBuffer);

			WdfRequestSetInformation(Request, sizeof(SERIAL_STATUS));
			break;
		}
		case IOCTL_SERIAL_GET_MODEMSTATUS: {
			PULONG out;
			size_t len;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID*)&out, &len);
			if (!NT_SUCCESS(status)) break;

			ULONG modem = SERIAL_CTS_STATE | SERIAL_DSR_STATE | SERIAL_RLSD_STATE;

			if (dev->PendingEvents & SERIAL_EV_RXCHAR) {
				modem |= SERIAL_RING_STATE;
			}

			*out = modem;

			WdfRequestSetInformation(Request, sizeof(ULONG));
			break;
		}
		case IOCTL_SERIAL_GET_PROPERTIES: {
			PSERIAL_COMMPROP properties;
			size_t length = 0;

			status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_COMMPROP), (PVOID*)&properties, &length);
			if (!NT_SUCCESS(status) || length < sizeof(SERIAL_COMMPROP)) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			RtlZeroMemory(properties, sizeof(SERIAL_COMMPROP));
			properties->PacketLength = sizeof(SERIAL_COMMPROP);
			properties->PacketVersion = 2;
			properties->ServiceMask = SERIAL_SP_SERIALCOMM;
			properties->MaxTxQueue = 4096;
			properties->MaxRxQueue = 4096;
			properties->MaxBaud = 921600;
			properties->ProvSubType = SERIAL_SP_RS232;
			properties->ProvCapabilities = SERIAL_PCF_DTRDSR | SERIAL_PCF_RTSCTS | SERIAL_PCF_CD | SERIAL_PCF_PARITY_CHECK | SERIAL_PCF_TOTALTIMEOUTS | SERIAL_PCF_INTTIMEOUTS | SERIAL_PCF_XONXOFF;
			properties->SettableParams = SERIAL_SP_PARITY | SERIAL_SP_BAUD | SERIAL_SP_DATABITS | SERIAL_SP_STOPBITS | SERIAL_SP_HANDSHAKING | SERIAL_SP_PARITY_CHECK | SERIAL_SP_CARRIER_DETECT;
			properties->SettableBaud = SERIAL_BAUD_075 | SERIAL_BAUD_110 | SERIAL_BAUD_134_5 | SERIAL_BAUD_150 | SERIAL_BAUD_300 | SERIAL_BAUD_600 | SERIAL_BAUD_1200 | SERIAL_BAUD_1800 | SERIAL_BAUD_2400 | SERIAL_BAUD_4800 | SERIAL_BAUD_7200 | SERIAL_BAUD_9600 | SERIAL_BAUD_14400 | SERIAL_BAUD_19200 | SERIAL_BAUD_38400 | SERIAL_BAUD_56K | SERIAL_BAUD_128K | SERIAL_BAUD_115200 | SERIAL_BAUD_57600 | SERIAL_BAUD_USER;
			properties->SettableData = SERIAL_DATABITS_5 | SERIAL_DATABITS_6 | SERIAL_DATABITS_7 | SERIAL_DATABITS_8;
			properties->SettableStopParity = SERIAL_STOPBITS_10 | SERIAL_STOPBITS_15 | SERIAL_STOPBITS_20 | SERIAL_PARITY_NONE | SERIAL_PARITY_ODD | SERIAL_PARITY_EVEN | SERIAL_PARITY_MARK | SERIAL_PARITY_SPACE;

			status = STATUS_SUCCESS;
			WdfRequestSetInformation(Request, sizeof(SERIAL_COMMPROP));
			break;
		}
		case IOCTL_SERIAL_SET_QUEUE_SIZE: {
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_PURGE: {
			PULONG flags;
			size_t length = 0;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&flags, &length);
			if (!NT_SUCCESS(status) || length < sizeof(ULONG)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			ULONG f = *flags;

			if (f & (SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_RXABORT)) {
				RingBufferClear(&qc->RingBuffer);

				WDFREQUEST req;
				while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(qc->ReadQueue, &req))) {
					WdfRequestComplete(req, STATUS_CANCELLED);
				}
			}

			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_DTR: {
			dev->DtrState = TRUE;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_CLR_DTR: {
			dev->DtrState = FALSE;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_RTS: {
			dev->RtsState = TRUE;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_CLR_RTS: {
			dev->RtsState = FALSE;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_XON:
		case IOCTL_SERIAL_SET_XOFF:
		case IOCTL_SERIAL_GET_CHARS: {
			SERIAL_CHARS out = dev->SpecialChars;
			status = RequestCopyFromBuffer(Request, &out, sizeof(out));
			WdfRequestSetInformation(Request, sizeof(out));
			break;
		}
		case IOCTL_SERIAL_SET_CHARS: {
			PSERIAL_CHARS in;
			size_t length = 0;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_CHARS), (PVOID*)&in, &length);
			if (!NT_SUCCESS(status) || length < sizeof(SERIAL_CHARS)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			dev->SpecialChars = *in;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_HANDFLOW: {
			PSERIAL_HANDFLOW hf;
			size_t length = 0;

			status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_HANDFLOW), (PVOID*)&hf, &length);
			if (!NT_SUCCESS(status)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			dev->HandFlow = *hf;
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_GET_HANDFLOW: {
			SERIAL_HANDFLOW out = dev->HandFlow;
			status = RequestCopyFromBuffer(Request, &out, sizeof(out));
			WdfRequestSetInformation(Request, sizeof(out));
			break;
		}
		case IOCTL_SERIAL_RESET_DEVICE:
			status = STATUS_SUCCESS;
			break;
		default: {
			wchar_t msg2[128];
			swprintf(msg2, 128, L"EvtIoDeviceControl DEFAULT hit, IOCTL=0x%08X", IoControlCode);
			OutputDebugString(msg2);
			status = STATUS_NOT_SUPPORTED;
			break;
		}
	}

	WdfRequestComplete(Request, status);
}

void DebugDumpBytes(PDEVICE_CONTEXT dev, const char* prefix, const BYTE* data, size_t len) {
	wchar_t head[128];
	swprintf(head, 128, L"[Port %hs] %hs len=%Iu", dev->port, prefix, len);
	OutputDebugString(head);

	wchar_t line[256];
	line[0] = 0;

	for (size_t i = 0; i < len; ++i) {
		wchar_t chunk[8];
		swprintf(chunk, 8, L"%02X ", data[i]);
		wcscat_s(line, 256, chunk);
	}

	OutputDebugString(line);
}

HANDLE OpenFileTarget(PCWSTR path) {
	HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	wchar_t msg[128];
	swprintf(msg, 128, L"OpenFileTarget: %ws finished.", path);
	OutputDebugString(msg);

	return h;
}

NTSTATUS EnsureForwardTargetConnected(PDEVICE_CONTEXT dev) {
	if (dev->ForwardConnected) return STATUS_SUCCESS;

	switch (dev->ForwardMode) {
		case Serial:
			OutputDebugString(L"ForwardMode=Serial");
			dev->ForwardConnected = TRUE;
			return STATUS_SUCCESS;
		case Network:
			OutputDebugString(L"ForwardMode=Network");
			dev->TargetHandle = OpenNetworkConnection(dev->TargetNetAddress, dev->TargetNetPort, NETPROTO_TCP);
			break;
		case File:
			OutputDebugString(L"ForwardMode=File");
			dev->TargetHandle = OpenFileTarget(dev->TargetPath);
			break;
		case None:
			OutputDebugString(L"ForwardMode=None");
			return STATUS_INVALID_DEVICE_STATE;
		default:
			OutputDebugString(L"ForwardMode=Invalid");
			return STATUS_INVALID_DEVICE_STATE;
	}

	if (dev->TargetHandle == INVALID_HANDLE_VALUE || dev->TargetHandle == NULL) {
		OutputDebugString(L"TargetHandle: invalid");
		dev->ForwardConnected = FALSE;
		return STATUS_IO_DEVICE_ERROR;
	}

	dev->ForwardConnected = TRUE;
	OutputDebugString(L"ForwardConnected=true");

	return STATUS_SUCCESS;
}

NTSTATUS LinkedPortWrite(PDEVICE_CONTEXT src, PDEVICE_CONTEXT dst, PUCHAR buf, size_t len) {
	PQUEUE_CONTEXT targetQc = dst->QueueContext;

	if (dst->Mode == Emulator) {
		QueueProcessWriteBytes(targetQc, buf, len);
	} else {
		NTSTATUS status = RingBufferWrite(&targetQc->RingBuffer, buf, len);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	ServicePendingReads(targetQc);
	targetQc->DeviceContext->PendingEvents |= SERIAL_EV_RXCHAR;
	ServicePendingWaitMask(targetQc);

	return STATUS_SUCCESS;
}

NTSTATUS EmulatorWrite(PQUEUE_CONTEXT qc, PUCHAR buf, size_t len) {
	return QueueProcessWriteBytes(qc, buf, len);
}

NTSTATUS ForwarderWrite(PDEVICE_CONTEXT dev, PUCHAR buf, size_t len) {
	NTSTATUS status;

	status = EnsureForwardTargetConnected(dev);
	if (!NT_SUCCESS(status)) return status;

	DWORD written = 0;
	BOOL ok = WriteFile(dev->TargetHandle, buf, (DWORD)len, &written, NULL);

	if (!ok || written == 0) {
		dev->ForwardConnected = FALSE;
		if (dev->TargetHandle && dev->TargetHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(dev->TargetHandle);
			dev->TargetHandle = NULL;
		}
		return STATUS_IO_DEVICE_ERROR;
	}

	return STATUS_SUCCESS;
}

NTSTATUS EchoWrite(PQUEUE_CONTEXT qc, PUCHAR buf, size_t len) {
	NTSTATUS st = RingBufferWrite(&qc->RingBuffer, buf, len);
	if (NT_SUCCESS(st)) {
		qc->DeviceContext->PendingEvents |= SERIAL_EV_RXCHAR;
		ServicePendingWaitMask(qc);
	}
	return st;
}

NTSTATUS FakeWrite() {
	return STATUS_SUCCESS;
}

VOID EvtIoWrite(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t Length) {
	PQUEUE_CONTEXT qc = GetQueueContext(Queue);
	PDEVICE_CONTEXT dev = qc->DeviceContext;
	NTSTATUS status;
	WDFMEMORY memory;
	PUCHAR buf;

	if (!dev->Enable) {
		WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
		return;
	}

	status = WdfRequestRetrieveInputMemory(Request, &memory);
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
		return;
	}

	buf = (PUCHAR)WdfMemoryGetBuffer(memory, NULL);

	DebugDumpBytes(dev, "WRITE FROM COM", buf, Length);

	EnterCriticalSection(&dev->LinkedPortLock);
	BOOLEAN isLinked = dev->IsLinked;
	PDEVICE_CONTEXT linked = dev->LinkedPort;
	LeaveCriticalSection(&dev->LinkedPortLock);

	if (isLinked && linked && linked->Enable && linked->QueueContext) {
		status = LinkedPortWrite(dev, linked, buf, Length);
		WdfRequestCompleteWithInformation(Request, status, Length);
		return;
	}

	switch (dev->Mode) {
		case Emulator:
			status = EmulatorWrite(qc, buf, Length);
			WdfRequestCompleteWithInformation(Request, status, Length);
			return;
		case Forwarder:
			status = ForwarderWrite(dev, buf, Length);
			WdfRequestCompleteWithInformation(Request, status, Length);
			return;
		case Echo:
			status = EchoWrite(qc, buf, Length);
			WdfRequestCompleteWithInformation(Request, status, Length);
			return;
		case Fake:
			status = FakeWrite();
			WdfRequestCompleteWithInformation(Request, status, Length);
			return;
		default:
			WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
			return;
	}
}

VOID EvtIoRead(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t Length) {
	NTSTATUS status;
	PQUEUE_CONTEXT qc = GetQueueContext(Queue);
	PDEVICE_CONTEXT dev = GetDeviceContext(WdfIoQueueGetDevice(Queue));
	WDFMEMORY memory;
	size_t bytesCopied = 0;
	BYTE* buffer;
//	DebugLog(qc->DeviceContext, L"EvtIoRead: requested Length = %Iu", Length);

	if (!dev->Enable) {
//		DebugLog(qc->DeviceContext, L"EvtIoRead: Enable=FALSE, returning 0 bytes");
		WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
		return;
	}

	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status)) {
//		DebugLog(qc->DeviceContext, L"EvtIoRead: RetrieveOutputMemory failed 0x%08X", status);
		WdfRequestComplete(Request, status);
		return;
	}

	buffer = (BYTE*)WdfMemoryGetBuffer(memory, NULL);

	status = RingBufferRead(&qc->RingBuffer, buffer, Length, &bytesCopied);
	if (!NT_SUCCESS(status)) {
//		DebugLog(qc->DeviceContext, L"EvtIoRead: RingBufferRead failed 0x%08X", status);
		WdfRequestComplete(Request, status);
		return;
	}

	if (bytesCopied > 0) {
		DebugDumpBytes(dev, "READ TO COM", buffer, bytesCopied);
//		DebugLog(qc->DeviceContext, L"EvtIoRead: completing with %Iu bytes", bytesCopied);
		WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, bytesCopied);
	} else {
		DebugLog(qc->DeviceContext, L"EvtIoRead: no data, forwarding to ReadQueue");
		status = WdfRequestForwardToIoQueue(Request, qc->ReadQueue);
		if (!NT_SUCCESS(status)) {
			DebugLog(qc->DeviceContext, L"EvtIoRead: ForwardToIoQueue failed 0x%08X", status);
			WdfRequestComplete(Request, status);
		} else {
//			DebugLog(qc->DeviceContext, L"EvtIoRead: Request successfully forwarded to ReadQueue");
		}
	}
}

VOID ServicePendingReads(PQUEUE_CONTEXT qc) {
	WDFREQUEST request;
	NTSTATUS status;
	size_t bytesCopied = 0;
	DebugLog(qc->DeviceContext, L"ServicePendingReads: called");
	int requestCount = 0;
	while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(qc->ReadQueue, &request))) {
		requestCount++;
		DebugLog(qc->DeviceContext, L"ServicePendingReads: Found pending request #%d", requestCount);
		WDFMEMORY memory;
		bytesCopied = 0;
		status = WdfRequestRetrieveOutputMemory(request, &memory);
		if (!NT_SUCCESS(status)) {
			DebugLog(qc->DeviceContext, L"ServicePendingReads: WdfRequestRetrieveOutputMemory failed 0x%08X", status);
			WdfRequestComplete(request, status);
			continue;
		}

		BYTE* buffer = (BYTE*)WdfMemoryGetBuffer(memory, NULL);

		status = RingBufferRead(&qc->RingBuffer, buffer, DATA_BUFFER_SIZE, &bytesCopied);
		DebugLog(qc->DeviceContext, L"ServicePendingReads: RingBufferRead -> %Iu bytes, status=0x%08X", bytesCopied, status);
		if (!NT_SUCCESS(status) || bytesCopied == 0) {
			DebugLog(qc->DeviceContext, L"ServicePendingReads: No data or error, returning request to queue");
			WdfRequestForwardToIoQueue(request, qc->ReadQueue);
			return;
		}

		WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, bytesCopied);
	}
	DebugLog(qc->DeviceContext, L"ServicePendingReads: No pending requests found (checked %d)", requestCount);
}

VOID ServicePendingWaitMask(PQUEUE_CONTEXT qc) {
	PDEVICE_CONTEXT dev = qc->DeviceContext;

	if (!dev->PendingWaitMaskRequest) return;

	ULONG signaled = dev->PendingEvents & dev->CurrentWaitMask;
	if (!signaled) return;

	WDFREQUEST req = dev->PendingWaitMaskRequest;
	dev->PendingWaitMaskRequest = NULL;

	PULONG outMask;
	size_t len;

	NTSTATUS status = WdfRequestRetrieveOutputBuffer(req, sizeof(ULONG), (PVOID*)&outMask, &len);

	if (NT_SUCCESS(status)) {
		*outMask = signaled;
		WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, sizeof(ULONG));
	} else {
		WdfRequestComplete(req, status);
	}

	dev->PendingEvents &= ~signaled;
}

DWORD WINAPI ForwardReaderThread(LPVOID param) {
	PQUEUE_CONTEXT qc = (PQUEUE_CONTEXT)param;
	PDEVICE_CONTEXT dev = qc->DeviceContext;
	BYTE buf[4096];

	DWORD enable = 0;
	wchar_t keyPath[256];
	swprintf(keyPath, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%hs", dev->port);
	dev->Enable = (enable != 0);

	for (;;) {
		if (dev->StopForwardThread) {
			OutputDebugString(L"StopForwardThread=true");
			break;
		}

		if (dev->IsLinked) {
			Sleep(50);
			continue;
		}

		if (dev->TargetPort[0] == L'\0' && dev->TargetNetAddress[0] == L'\0' && dev->TargetPath[0] == L'\0') {
			OutputDebugString(L"No port, No host, No file");
			Sleep(100);
			continue;
		}

		if (dev->TargetHandle == NULL || dev->TargetHandle == INVALID_HANDLE_VALUE) {
//			OutputDebugString(L"TargetHandle is invalid, attempting to reconnect...");
			NTSTATUS reconnectStatus = EnsureForwardTargetConnected(dev);
			if (!NT_SUCCESS(reconnectStatus)) {
				wchar_t errMsg[128];
				swprintf(errMsg, 128, L"ForwardReaderThread: EnsureForwardTargetConnected failed - status=0x%08X", reconnectStatus);
				OutputDebugString(errMsg);
				Sleep(500);
			}
			continue;
		}

		if (!dev->ForwardConnected) {
			OutputDebugString(L"Not ForwardConnected, attempting to reconnect...");
			NTSTATUS ensureStatus = EnsureForwardTargetConnected(dev);
			if (!NT_SUCCESS(ensureStatus)) {
				wchar_t errMsg[128];
				swprintf(errMsg, 128, L"ForwardReaderThread: Failed to establish connection - status=0x%08X", ensureStatus);
				OutputDebugString(errMsg);
				Sleep(500);
			}
			continue;
		}

		DWORD read = 0;
		BOOL ok = ReadFile(dev->TargetHandle, buf, sizeof(buf), &read, NULL);
		if (!ok) {
			wchar_t errMsg[128];
			swprintf(errMsg, 128, L"ForwardReaderThread: ReadFile failed, error=%u", GetLastError());
			OutputDebugString(errMsg);
			dev->ForwardConnected = FALSE;
			if (dev->TargetHandle && dev->TargetHandle != INVALID_HANDLE_VALUE) {
				CloseHandle(dev->TargetHandle);
				dev->TargetHandle = NULL;
			}
			Sleep(100);
			continue;
		}
		if (read == 0) {
			OutputDebugString(L"ForwardReaderThread: ReadFile returned 0 bytes");
			Sleep(10);
			continue;
		}

		DebugDumpBytes(dev, "RECV FROM TARGET", buf, read);
		
		NTSTATUS writeStatus = RingBufferWrite(&qc->RingBuffer, buf, read);
		if (!NT_SUCCESS(writeStatus)) {
			wchar_t errMsg[128];
			swprintf(errMsg, 128, L"ForwardReaderThread: RingBufferWrite failed - status=0x%08X, data loss detected!", writeStatus);
			OutputDebugString(errMsg);
		}
		
		ServicePendingReads(qc);
		dev->PendingEvents |= SERIAL_EV_RXCHAR;
		ServicePendingWaitMask(qc);
	}

	return 0;
}

void InitializeQueueContext(PQUEUE_CONTEXT QueueContext) {
}

NTSTATUS QueueProcessWriteBytes(_In_ PQUEUE_CONTEXT QueueContext, _In_reads_bytes_(Length) PUCHAR Characters, _In_ size_t Length) {
	NTSTATUS status = STATUS_SUCCESS;

	wchar_t msg[128];
	swprintf(msg, 128, L"[Port %S] Emulator", QueueContext->DeviceContext->port);
	OutputDebugString(msg);

	UCHAR receivedString[64] = { 0 };
	size_t receivedStringLength = 0;

	while (Length != 0 && receivedStringLength < sizeof(receivedString) - 1) {
		UCHAR currentCharacter = *(Characters++);
		receivedString[receivedStringLength++] = currentCharacter;
		Length--;
	}

	receivedString[receivedStringLength] = '\0';

	const DEVICE_PROFILE* profile = GetActiveProfile();
	if (!profile) {
		DebugLog(QueueContext->DeviceContext, L"Emulator: no active profile, ignoring");
		return STATUS_SUCCESS;
	}

	PRING_BUFFER targetBuffer = &QueueContext->RingBuffer;
	PQUEUE_CONTEXT targetQueueContext = QueueContext;
	PDEVICE_CONTEXT linkedPort = NULL;

	EnterCriticalSection(&QueueContext->DeviceContext->LinkedPortLock);
	if (QueueContext->DeviceContext->IsLinked && QueueContext->DeviceContext->LinkedPort && QueueContext->DeviceContext->LinkedPort->QueueContext && QueueContext->DeviceContext->LinkedPort->Enable) {
		linkedPort = QueueContext->DeviceContext->LinkedPort;
		targetBuffer = &linkedPort->QueueContext->RingBuffer;
		targetQueueContext = linkedPort->QueueContext;
	}
	LeaveCriticalSection(&QueueContext->DeviceContext->LinkedPortLock);

	if (g_Config.Port.Operation == 0) {
		UCHAR responseString[64];
		srand((unsigned int)time(0));
		int randomNumber = rand() % 900 + 100;
		snprintf((char*)responseString, sizeof(responseString), "AX/B=%d%hs", randomNumber, profile->LineEnd);

		size_t respLen = strlen((char*)responseString);
		DebugLogVisible(QueueContext->DeviceContext, responseString, respLen);

		status = RingBufferWrite(targetBuffer, responseString, respLen);
		if (!NT_SUCCESS(status)) return status;

		ServicePendingReads(targetQueueContext);
		targetQueueContext->DeviceContext->PendingEvents |= SERIAL_EV_RXCHAR;
		ServicePendingWaitMask(targetQueueContext);
		return STATUS_SUCCESS;
	}

	DebugLog(QueueContext->DeviceContext, L"Emulator: MANUAL mode - Received: %S", receivedString);
	DebugLogVisible(QueueContext->DeviceContext, receivedString, receivedStringLength);

	const char* responseString = NULL;
	char buffer[256];

	snprintf(buffer, sizeof(buffer), "%hs%hs", profile->ErrorDefault, profile->LineEnd);
	responseString = buffer;

//	RingBufferWrite(targetBuffer, receivedString, receivedStringLength);

	for (int i = 0; i < profile->CommandCount; ++i) {
		char fullCmd[256];
		snprintf(fullCmd, sizeof(fullCmd), "%hs%hs", profile->Commands[i].Command, profile->LineEnd);

		if (strcmp((char*)receivedString, fullCmd) == 0) {
			snprintf(buffer, sizeof(buffer), "%hs%hs", profile->Commands[i].Answer, profile->LineEnd);
			responseString = buffer;
			break;
		}
	}

	size_t respLen = strlen(responseString);
	status = RingBufferWrite(targetBuffer, (PUCHAR)responseString, respLen);
	if (!NT_SUCCESS(status)) return status;
	DebugLogVisible(QueueContext->DeviceContext, responseString, respLen);

	ServicePendingReads(targetQueueContext);
	targetQueueContext->DeviceContext->PendingEvents |= SERIAL_EV_RXCHAR;
	ServicePendingWaitMask(targetQueueContext);

	return STATUS_SUCCESS;
}

NTSTATUS QueueProcessGetLineControl( _In_ PQUEUE_CONTEXT QueueContext, _In_ WDFREQUEST Request) {
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	SERIAL_LINE_CONTROL lineControl = { 0 };
	ULONG lineControlSnapshot;
	ULONG* lineControlRegister;

	deviceContext = QueueContext->DeviceContext;
	lineControlRegister = GetLineControlRegisterPtr(deviceContext);

	ASSERT(lineControlRegister);

	lineControlSnapshot = *lineControlRegister;

	if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_5_DATA) {
		lineControl.WordLength = 5;
	} else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_6_DATA) {
		lineControl.WordLength = 6;
	} else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_7_DATA) {
		lineControl.WordLength = 7;
	} else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_8_DATA) {
		lineControl.WordLength = 8;
	}

	if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_NONE_PARITY) {
		lineControl.Parity = NO_PARITY;
	} else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_ODD_PARITY) {
		lineControl.Parity = ODD_PARITY;
	} else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_EVEN_PARITY) {
		lineControl.Parity = EVEN_PARITY;
	} else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_MARK_PARITY) {
		lineControl.Parity = MARK_PARITY;
	} else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_SPACE_PARITY) {
		lineControl.Parity = SPACE_PARITY;
	}

	if (lineControlSnapshot & SERIAL_2_STOP) {
		if (lineControl.WordLength == 5) {
			lineControl.StopBits = STOP_BITS_1_5;
		} else {
			lineControl.StopBits = STOP_BITS_2;
		}
	} else {
		lineControl.StopBits = STOP_BIT_1;
	}

	status = RequestCopyFromBuffer(Request, (void*)&lineControl, sizeof(lineControl));
	return status;
}

NTSTATUS QueueProcessSetLineControl( _In_ PQUEUE_CONTEXT QueueContext, _In_ WDFREQUEST Request) {
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	SERIAL_LINE_CONTROL lineControl = { 0 };
	ULONG* lineControlRegister;
	UCHAR lineControlData = 0;
	UCHAR lineControlStop = 0;
	UCHAR lineControlParity = 0;
	ULONG lineControlSnapshot;
	ULONG lineControlNew;
	ULONG lineControlPrevious;
	ULONG i;

	deviceContext = QueueContext->DeviceContext;
	lineControlRegister = GetLineControlRegisterPtr(deviceContext);

	ASSERT(lineControlRegister);

	status = RequestCopyToBuffer(Request, (void*)&lineControl, sizeof(lineControl));

	if (NT_SUCCESS(status)) {
		switch (lineControl.WordLength) {
			case 5:
				lineControlData = SERIAL_5_DATA;
				SetValidDataMask(deviceContext, 0x1f);
				break;
			case 6:
				lineControlData = SERIAL_6_DATA;
				SetValidDataMask(deviceContext, 0x3f);
				break;
			case 7:
				lineControlData = SERIAL_7_DATA;
				SetValidDataMask(deviceContext, 0x7f);
				break;
			case 8:
				lineControlData = SERIAL_8_DATA;
				SetValidDataMask(deviceContext, 0xff);
				break;
			default:
				status = STATUS_INVALID_PARAMETER;
				break;
		}
	}

	if (NT_SUCCESS(status)) {
		switch (lineControl.StopBits) {
			case STOP_BIT_1:
				lineControlStop = SERIAL_1_STOP;
				break;
			case STOP_BITS_1_5:
				if (lineControlData != SERIAL_5_DATA) {
					status = STATUS_INVALID_PARAMETER;
					break;
				}
				lineControlStop = SERIAL_1_5_STOP;
				break;
			case STOP_BITS_2:
				if (lineControlData == SERIAL_5_DATA) {
					status = STATUS_INVALID_PARAMETER;
					break;
				}
				lineControlStop = SERIAL_2_STOP;
				break;
			default:
				status = STATUS_INVALID_PARAMETER;
				break;
		}
	}

	if (NT_SUCCESS(status)) {
		switch (lineControl.Parity) {
			case NO_PARITY:
				lineControlParity = SERIAL_NONE_PARITY;
				break;
			case EVEN_PARITY:
				lineControlParity = SERIAL_EVEN_PARITY;
				break;
			case ODD_PARITY:
				lineControlParity = SERIAL_ODD_PARITY;
				break;
			case SPACE_PARITY:
				lineControlParity = SERIAL_SPACE_PARITY;
				break;
			case MARK_PARITY:
				lineControlParity = SERIAL_MARK_PARITY;
				break;
			default:
				status = STATUS_INVALID_PARAMETER;
				break;
		}
	}

	i = 0;
	do {
		i++;
		if ((i & 0xf) == 0) {
			SwitchToThread();
		}

		lineControlSnapshot = *lineControlRegister;
		lineControlNew = (lineControlSnapshot & SERIAL_LCR_BREAK) | (lineControlData | lineControlParity | lineControlStop);
		lineControlPrevious = InterlockedCompareExchange( (LONG*)lineControlRegister, lineControlNew, lineControlSnapshot);
	} while (lineControlPrevious != lineControlSnapshot);

	return status;
}