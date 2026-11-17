#pragma once

typedef struct _RING_BUFFER {
	size_t Size;
	BYTE* Base;
	BYTE* End;
	BYTE* Head;
	BYTE* Tail;
	CRITICAL_SECTION Lock;
} RING_BUFFER, * PRING_BUFFER;

VOID RingBufferInitialize(_In_ PRING_BUFFER Self, _In_reads_bytes_(BufferSize) BYTE* Buffer, _In_ size_t BufferSize);
VOID RingBufferGetAvailableSpace(_In_ PRING_BUFFER Self, _Out_ size_t* AvailableSpace);
VOID RingBufferGetAvailableData(_In_ PRING_BUFFER Self, _Out_ size_t* AvailableData);
NTSTATUS RingBufferWrite(_In_ PRING_BUFFER Self, _In_reads_bytes_(DataSize) BYTE* Data, _In_ size_t DataSize);
NTSTATUS RingBufferRead(_In_ PRING_BUFFER Self, _Out_writes_bytes_to_(DataSize, *BytesCopied) BYTE* Data, _In_ size_t DataSize, _Out_ size_t* BytesCopied);
size_t RingBufferGetUsed(_In_ PRING_BUFFER Self);
VOID RingBufferClear(_Inout_ PRING_BUFFER Self);