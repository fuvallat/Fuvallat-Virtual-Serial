#pragma once

#include "wdf.h"
#include "Wdfobject.h"

#include "internal.h"

#define DATA_BUFFER_SIZE 32768

#define COMMAND_MATCH_STATE_IDLE 0
#define COMMAND_MATCH_STATE_GOT_A 1
#define COMMAND_MATCH_STATE_GOT_T 2
#define COMMAND_MATCH_STATE_GOT_X 3
#define COMMAND_MATCH_STATE_GOT_XA 4
#define COMMAND_MATCH_STATE_GOT_XA_SLASH 5
#define COMMAND_MATCH_STATE_GOT_XA_SLASH_B 6

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define MAXULONG 0xffffffff

typedef struct _QUEUE_CONTEXT {
	UCHAR CommandMatchState;
	BOOLEAN ConnectCommand;
	BOOLEAN IgnoreNextChar;
	BOOLEAN ConnectionStateChanged;
	BOOLEAN CurrentlyConnected;
	RING_BUFFER RingBuffer;
	BYTE Buffer[DATA_BUFFER_SIZE];
	WDFQUEUE Queue;
	WDFQUEUE ReadQueue;
	WDFQUEUE WaitMaskQueue;
	PDEVICE_CONTEXT DeviceContext;
} QUEUE_CONTEXT, * PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, GetQueueContext);

EVT_WDF_IO_QUEUE_IO_READ EvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE EvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;

NTSTATUS QueueCreate(_In_ PDEVICE_CONTEXT DeviceContext);
NTSTATUS QueueProcessWriteBytes(_In_ PQUEUE_CONTEXT QueueContext, _In_reads_bytes_(Length) PUCHAR Characters, _In_ size_t Length);
NTSTATUS QueueProcessGetLineControl(_In_ PQUEUE_CONTEXT QueueContext, _In_ WDFREQUEST Request);
NTSTATUS QueueProcessSetLineControl(_In_ PQUEUE_CONTEXT QueueContext, _In_ WDFREQUEST Request);
NTSTATUS RequestCopyFromBuffer(_In_ WDFREQUEST Request, _In_ PVOID SourceBuffer, _In_ size_t NumBytesToCopyFrom);
NTSTATUS RequestCopyToBuffer(_In_ WDFREQUEST Request, _In_ PVOID DestinationBuffer, _In_ size_t NumBytesToCopyTo);
DWORD WINAPI ForwardReaderThread(LPVOID param);
VOID ServicePendingReads(PQUEUE_CONTEXT qc);
VOID ServicePendingWaitMask(PQUEUE_CONTEXT qc);