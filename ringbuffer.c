#include "internal.h"

VOID RingBufferInitialize(_In_ PRING_BUFFER Self, _In_reads_bytes_(BufferSize) BYTE* Buffer, _In_ size_t BufferSize) {
	Self->Size = BufferSize;
	Self->Base = Buffer;
	Self->End = Buffer + BufferSize;
	Self->Head = Buffer;
	Self->Tail = Buffer;
	InitializeCriticalSection(&Self->Lock);
}

VOID RingBufferGetAvailableSpace(_In_ PRING_BUFFER Self, _Out_ size_t* AvailableSpace) {
	BYTE* headSnapshot = NULL;
	BYTE* tailSnapshot = NULL;
	BYTE* tailPlusOne = NULL;

	ASSERT(AvailableSpace);

	headSnapshot = Self->Head;
	tailSnapshot = Self->Tail;

	tailPlusOne = ((tailSnapshot + 1) == Self->End) ? Self->Base : (tailSnapshot + 1);

	if (tailPlusOne == headSnapshot) {
		*AvailableSpace = 0;
	} else if (tailSnapshot == headSnapshot) {
		*AvailableSpace = Self->Size - 1;
	} else {
		if (tailSnapshot > headSnapshot) {
			*AvailableSpace = Self->Size - (tailSnapshot - headSnapshot) - 1;
		} else {
			*AvailableSpace = (headSnapshot - tailSnapshot) - 1;
		}
	}
}

VOID RingBufferGetAvailableData(_In_ PRING_BUFFER Self, _Out_ size_t* AvailableData) {
	size_t availableSpace;

	ASSERT(AvailableData);

	RingBufferGetAvailableSpace(Self, &availableSpace);

	*AvailableData = Self->Size - availableSpace - 1;
}

NTSTATUS RingBufferWrite(_In_ PRING_BUFFER Self, _In_reads_bytes_(DataSize) BYTE* Data, _In_ size_t DataSize) {
	size_t availableSpace;
	size_t bytesToCopy;
	size_t spaceFromCurrToEnd;
	NTSTATUS result;

	ASSERT(Data && (0 != DataSize));

	EnterCriticalSection(&Self->Lock);

	if (Self->Tail >= Self->End) {
		LeaveCriticalSection(&Self->Lock);
		return STATUS_INTERNAL_ERROR;
	}

	RingBufferGetAvailableSpace(Self, &availableSpace);

	if (availableSpace < DataSize) {
		wchar_t warnMsg[128];
		swprintf(warnMsg, 128, L"WARNING: RingBuffer overflow - requested %Iu bytes, available %Iu (data loss!)", DataSize, availableSpace);
		OutputDebugString(warnMsg);
		bytesToCopy = availableSpace;
	} else {
		bytesToCopy = DataSize;
	}

	if (bytesToCopy) {
		if ((Self->Tail + bytesToCopy) > Self->End) {
			spaceFromCurrToEnd = Self->End - Self->Tail;

			RtlCopyMemory(Self->Tail, Data, spaceFromCurrToEnd);

			Data += spaceFromCurrToEnd;

			bytesToCopy -= spaceFromCurrToEnd;

			RtlCopyMemory(Self->Base, Data, bytesToCopy);

			Self->Tail = Self->Base + bytesToCopy;
		} else {
			RtlCopyMemory(Self->Tail, Data, bytesToCopy);

			Self->Tail += bytesToCopy;
			if (Self->Tail == Self->End) {
				Self->Tail = Self->Base;
			}
		}

		ASSERT(Self->Tail < Self->End);
	}

	result = STATUS_SUCCESS;
	LeaveCriticalSection(&Self->Lock);

	return result;
}

NTSTATUS RingBufferRead(_In_ PRING_BUFFER Self, _Out_writes_bytes_to_(DataSize, *BytesCopied) BYTE* Data, _In_ size_t DataSize, _Out_ size_t* BytesCopied) {
	size_t availableData;
	size_t dataFromCurrToEnd;
	NTSTATUS result;

	ASSERT(Data && (DataSize != 0));

	EnterCriticalSection(&Self->Lock);

	if (Self->Head >= Self->End) {
		LeaveCriticalSection(&Self->Lock);
		return STATUS_INTERNAL_ERROR;
	}

	RingBufferGetAvailableData(Self, &availableData);

	if (availableData == 0) {
		*BytesCopied = 0;
		LeaveCriticalSection(&Self->Lock);
		return STATUS_SUCCESS;
	}

	if (DataSize > availableData) {
		DataSize = availableData;
	}

	*BytesCopied = DataSize;

	if ((Self->Head + DataSize) > Self->End) {
		dataFromCurrToEnd = Self->End - Self->Head;
		RtlCopyMemory(Data, Self->Head, dataFromCurrToEnd);
		Data += dataFromCurrToEnd;
		DataSize -= dataFromCurrToEnd;

		RtlCopyMemory(Data, Self->Base, DataSize);

		Self->Head = Self->Base + DataSize;
	} else {
		RtlCopyMemory(Data, Self->Head, DataSize);

		Self->Head += DataSize;
		if (Self->Head == Self->End) {
			Self->Head = Self->Base;
		}
	}

	ASSERT(Self->Head < Self->End);

	result = STATUS_SUCCESS;
	LeaveCriticalSection(&Self->Lock);

	return result;
}

size_t RingBufferGetUsed(_In_ PRING_BUFFER Self) {
	size_t availableData = 0;
	RingBufferGetAvailableData(Self, &availableData);
	return availableData;
}

VOID RingBufferClear(_Inout_ PRING_BUFFER Self) {
	EnterCriticalSection(&Self->Lock);
	Self->Head = Self->Base;
	Self->Tail = Self->Base;
	LeaveCriticalSection(&Self->Lock);
}