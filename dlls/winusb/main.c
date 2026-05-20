/*
 * Copyright (C) 2022 Mohamad Al-Jaf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winusb.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winusb);

// TODO: should be in a common header between driver and user mode dll
#define IOCTL_WINUSB_GET_DESCRIPTOR             0x350c004
#define IOCTL_WINUSB_SET_CURRENT_ALT_SETTING    0x350c008
#define IOCTL_WINUSB_SET_PIPE_POLICY            0x350c00c
#define IOCTL_WINUSB_RESET_PIPE                 0x350c024
#define IOCTL_WINUSB_ABORT_PIPE                 0x350c028
#define IOCTL_WINUSB_SET_POWER_POLICY           0x350c02c
#define IOCTL_WINUSB_CONTROL_TRANSFER           0x350c03a
#define IOCTL_WINUSB_QUERY_DEVICE_INFO          0x350c04c
#define IOCTL_WINUSB_GET_PIPE_POLICY            0x350c050
#define IOCTL_WINUSB_GET_POWER_POLICY           0x350c058
#define IOCTL_WINUSB_GET_CURRENT_ALT_SETTING    0x350c05c
#define IOCTL_WINUSB_READ_PIPE                  0x350401e
#define IOCTL_WINUSB_WRITE_PIPE                 0x3508021
#define IOCTL_WINUSB_FLUSH_PIPE                 0x3504048
#define IOCTL_WINUSB_INITIALIZE_1               0x350c068
#define IOCTL_WINUSB_INITIALIZE_2               0x350c03c

// This has a size of 0x298, but these are the two important ones for us.
typedef struct _WINUSB_INTERNAL_HANDLE {
    HANDLE FileHandle;
    UCHAR  InterfaceIndex;
} WINUSB_INTERNAL_HANDLE, *PWINUSB_INTERNAL_HANDLE;

typedef enum _WINUSB_DEVICE_INFO_TYPE {
    DeviceSpeed = 0x01
    // TODO: find the others
} WINUSB_DEVICE_INFO_TYPE;

#pragma pack(push, 1)

// Derived from decompilation of GetPipePolicy
typedef struct _WINUSB_GET_PIPE_POLICY_PACKET {
    UCHAR InterfaceIndex;
    UCHAR PipeAddress;
    USHORT Reserved; 
    ULONG PolicyType;
    ULONG Reserved2; // To pad to 12 bytes 
} WINUSB_GET_PIPE_POLICY_PACKET;
#pragma pack(pop)

static UCHAR GetDriverInterfaceIndex(UCHAR interfaceIndex) {
    if (interfaceIndex > 0) {
        return interfaceIndex + 1;
    }
    return 0;
}

static BOOL SyncIoControl(HANDLE hFile, DWORD dwIoControlCode, 
                                 LPVOID lpInBuffer, DWORD nInBufferSize,
                                 LPVOID lpOutBuffer, DWORD nOutBufferSize,
                                 LPDWORD lpBytesReturned) 
{
    OVERLAPPED overlapped = {0};
    DWORD bytes = 0;
    BOOL result;

    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent) return FALSE;

    // Set bit 1 for internal WinUSB signaling (from decompilation)
    // TODO: I'm unsure if this is needed
    overlapped.hEvent = (HANDLE)((uintptr_t)overlapped.hEvent | 1);
    
    result = DeviceIoControl(hFile, dwIoControlCode, 
                                  lpInBuffer, nInBufferSize, 
                                  lpOutBuffer, nOutBufferSize, 
                                  &bytes, &overlapped);

    // If it returned FALSE, it might just be pending.
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        result = GetOverlappedResult(hFile, &overlapped, &bytes, TRUE);
    }

    if (lpBytesReturned) *lpBytesReturned = bytes;
    
    // Clear the bit 1 before closing, just to be safe (though CloseHandle ignores it usually)
    overlapped.hEvent = (HANDLE)((uintptr_t)overlapped.hEvent & ~1ULL);
    CloseHandle(overlapped.hEvent);
    
    return result;
}


BOOL WINAPI WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_PIPE_IO_PACKET packet;
    DWORD bytes = 0;

    if (!handle) return FALSE;

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;

    return SyncIoControl(handle->FileHandle, IOCTL_WINUSB_ABORT_PIPE,
                           &packet, sizeof(packet),
                           NULL, 0,
                           &bytes);
}

BOOL WINAPI WinUsb_AbortPipeAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    // TODO: figure out what this should do differently as "async"
    return WinUsb_AbortPipe(InterfaceHandle, PipeID);
}

BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_CONTROL_PACKET packet;
    DWORD bytesReturned = 0;
    BOOL result;
    
    if (!handle) { 
        SetLastError(ERROR_INVALID_HANDLE); 
        return FALSE; 
    }

    if (LengthTransferred) {
        *LengthTransferred = 0;
    }

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.SetupPacket = SetupPacket; // Copy 8 bytes

    if (Overlapped) {
        result = DeviceIoControl(handle->FileHandle, 
                                  IOCTL_WINUSB_CONTROL_TRANSFER, 
                                  &packet, sizeof(packet), // Size is 9
                                  Buffer, BufferLength, 
                                  &bytesReturned, Overlapped);
    } else {
        result = SyncIoControl(handle->FileHandle, 
                                      IOCTL_WINUSB_CONTROL_TRANSFER, 
                                      &packet, sizeof(packet), 
                                      Buffer, BufferLength, 
                                      &bytesReturned);
    }

    if (LengthTransferred && result) {
         *LengthTransferred = bytesReturned;
    }

    return result;
}

BOOL WINAPI WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_PIPE_IO_PACKET packet;
    DWORD bytes = 0;

    if (!handle) return FALSE;

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;

    return SyncIoControl(handle->FileHandle, IOCTL_WINUSB_FLUSH_PIPE,
                           &packet, sizeof(packet),
                           NULL, 0,
                           &bytes);
}

BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE InterfaceHandle) {
    if (InterfaceHandle) {
        HeapFree(GetProcessHeap(), 0, InterfaceHandle);
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI WinUsb_GetAdjustedFrameNumber(PULONG CurrentFrameNumber, PVOID TimeStamp) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AssociatedInterfaceIndex, PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle) {
    SetLastError(ERROR_NOT_SUPPORTED); // Complex: Requires creating a new context for same file handle but different interface index
    return FALSE;
}

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, PUCHAR SettingNumber) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    UCHAR driverIndex;
    UCHAR resultSetting = 0;
    DWORD bytesReturned = 0;
    BOOL result;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    driverIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    
    result = SyncIoControl(handle->FileHandle,
                                  IOCTL_WINUSB_GET_CURRENT_ALT_SETTING,
                                  &driverIndex, sizeof(driverIndex), // Input
                                  &resultSetting, sizeof(resultSetting), // Output
                                  &bytesReturned);

    if (result && SettingNumber) {
        *SettingNumber = resultSetting;
    }

    return result;
}

BOOL WINAPI WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE InterfaceHandle, PULONG CurrentFrameNumber, PULONG TimeStamp) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID FrameQpcInfo) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR DescriptorType, UCHAR Index, USHORT LanguageID, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_DESCRIPTOR_PACKET packet;
    DWORD bytes = 0;
    BOOL result;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    packet.DescriptorType = DescriptorType;
    packet.Index = Index;
    packet.LanguageId = LanguageID;

    result = SyncIoControl(handle->FileHandle, IOCTL_WINUSB_GET_DESCRIPTOR,
                                  &packet, sizeof(packet),
                                  Buffer, BufferLength,
                                  &bytes);

    if (LengthTransferred) *LengthTransferred = bytes;
    return result;
}

BOOL WINAPI WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    if (!handle) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    
    return GetOverlappedResult(handle->FileHandle, lpOverlapped, lpNumberOfBytesTransferred, bWait);
}

BOOL WINAPI WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, PULONG ValueLength, PVOID Value) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_GET_PIPE_POLICY_PACKET packet = {0};
    DWORD bytesReturned = 0;
    BOOL result;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (!ValueLength || !Value) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Decompilation sends 12 bytes
    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;
    packet.PolicyType = PolicyType;
    
    
    result = SyncIoControl(handle->FileHandle,
                                  IOCTL_WINUSB_GET_PIPE_POLICY,
                                  &packet, 12, // Explicit 12 bytes as per decompilation 0xc
                                  Value, *ValueLength,
                                  &bytesReturned);

    if (result) {
        *ValueLength = bytesReturned;
    }

    return result;
}

BOOL WINAPI WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, PULONG ValueLength, PVOID Value) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_POLICY_PACKET packet = {0};
    DWORD bytes = 0;
    BOOL result;

    if (!handle) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PolicyType = PolicyType;

    result = SyncIoControl(handle->FileHandle, IOCTL_WINUSB_GET_POWER_POLICY, &packet, sizeof(packet), Value, *ValueLength, &bytes);
    if (result) *ValueLength = bytes;
    return result;
}

BOOL WINAPI WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle) {
    PWINUSB_INTERNAL_HANDLE handle;
    DWORD bytes = 0;
    USHORT initInput[1] = { 0x100 }; 
    USHORT initOutput[1] = { 0 };

    if (!InterfaceHandle) { 
        SetLastError(ERROR_INVALID_PARAMETER); 
        return FALSE; 
    }
    *InterfaceHandle = NULL;

    handle = (PWINUSB_INTERNAL_HANDLE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WINUSB_INTERNAL_HANDLE));
    if (!handle) { 
        SetLastError(ERROR_NOT_ENOUGH_MEMORY); 
        return FALSE; 
    }

    handle->FileHandle = DeviceHandle;
    handle->InterfaceIndex = 0;

    if (!SyncIoControl(DeviceHandle, 
                              IOCTL_WINUSB_INITIALIZE_1, 
                              initInput, sizeof(initInput), 
                              initOutput, sizeof(initOutput), 
                              &bytes)) 
    {
        HeapFree(GetProcessHeap(), 0, handle);
        return FALSE;
    }

    // This might be configuration stuff
    if (!SyncIoControl(DeviceHandle, 
                              IOCTL_WINUSB_INITIALIZE_2, 
                              NULL, 0, 
                              NULL, 0, 
                              &bytes)) 
    {
        HeapFree(GetProcessHeap(), 0, handle);
        return FALSE;
    }

    *InterfaceHandle = (WINUSB_INTERFACE_HANDLE)handle;
    return TRUE;
}

PVOID WINAPI WinUsb_ParseConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor, PVOID StartPosition, LONG InterfaceNumber, LONG AlternateSetting, LONG InterfaceClass, LONG InterfaceSubClass, LONG InterfaceProtocol) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

PVOID WINAPI WinUsb_ParseDescriptors(PVOID DescriptorBuffer, ULONG TotalLength, PVOID StartPosition, LONG DescriptorType) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

BOOL WINAPI WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG InformationType, PULONG BufferLength, PVOID Buffer) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    DWORD type = InformationType;
    DWORD bytesReturned = 0;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return SyncIoControl(handle->FileHandle, IOCTL_WINUSB_QUERY_DEVICE_INFO,
                           &type, sizeof(DWORD),
                           Buffer, *BufferLength,
                           &bytesReturned);
}

BOOL WINAPI WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateSettingNumber, PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor) {
    SetLastError(ERROR_NOT_SUPPORTED); // Requires parsing config descriptor usually
    return FALSE;
}

BOOL WINAPI WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION PipeInformation) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION_EX PipeInformationEx) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_ReadIsochPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_ReadIsochPipeAsap(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    DWORD bytesRead = 0;
    BOOL result;
    WINUSB_PIPE_IO_PACKET packet;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (LengthTransferred) {
        *LengthTransferred = 0;
    }

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;

    if (Overlapped) {
        result = DeviceIoControl(handle->FileHandle, 
                             IOCTL_WINUSB_READ_PIPE,
                             &packet, sizeof(packet),
                             Buffer, BufferLength,
                             &bytesRead, Overlapped);
    } else {
        result = SyncIoControl(handle->FileHandle, 
                             IOCTL_WINUSB_READ_PIPE,
                             &packet, sizeof(packet),
                             Buffer, BufferLength,
                             &bytesRead);
    }

    if (result && LengthTransferred) {
        *LengthTransferred = bytesRead;
    }

    return result;
}

BOOL WINAPI WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_PIPE_IO_PACKET packet;
    DWORD bytes = 0;

    if (!handle) return FALSE;

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;

    return SyncIoControl(handle->FileHandle, IOCTL_WINUSB_RESET_PIPE,
                           &packet, sizeof(packet),
                           NULL, 0,
                           &bytes);
}

BOOL WINAPI WinUsb_ResetPipeAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    // TODO: figure out what this should do differently as "async"
    return WinUsb_ResetPipe(InterfaceHandle, PipeID);
}

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    WINUSB_SET_ALT_SETTING_PACKET packet;
    DWORD bytesReturned = 0;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.AlternateSetting = SettingNumber;

    return SyncIoControl(handle->FileHandle,
                                  IOCTL_WINUSB_SET_CURRENT_ALT_SETTING,
                                  &packet, sizeof(packet),
                                  NULL, 0,
                                  &bytesReturned);
}

BOOL WINAPI WinUsb_SetCurrentAlternateSettingAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber) {
    // TODO: figure out what this should do differently as "async"
    return WinUsb_SetCurrentAlternateSetting(InterfaceHandle, SettingNumber);
}

BOOL WINAPI WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, ULONG ValueLength, PVOID Value) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    DWORD inputSize = sizeof(WINUSB_POLICY_PACKET) + ValueLength;
    PUCHAR rawBuf;
    WINUSB_POLICY_PACKET *packet;
    DWORD bytes = 0;
    BOOL result;

    if (!handle) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

    rawBuf = (PUCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, inputSize);
    if (!rawBuf) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }

    packet = (WINUSB_POLICY_PACKET*)rawBuf;
    packet->InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet->PipeAddress = PipeID;
    packet->PolicyType = PolicyType;
    
    if (Value && ValueLength > 0) {
        memcpy(rawBuf + sizeof(WINUSB_POLICY_PACKET), Value, ValueLength);
    }

    result = SyncIoControl(handle->FileHandle, IOCTL_WINUSB_SET_PIPE_POLICY, rawBuf, inputSize, NULL, 0, &bytes);
    
    HeapFree(GetProcessHeap(), 0, rawBuf);
    return result;
}

BOOL WINAPI WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, ULONG ValueLength, PVOID Value) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    DWORD inputSize = sizeof(WINUSB_POLICY_PACKET) + ValueLength;
    PUCHAR rawBuf;
    WINUSB_POLICY_PACKET *packet;
    DWORD bytes = 0;
    BOOL result;

    if (!handle) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

    rawBuf = (PUCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, inputSize);
    if (!rawBuf) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }

    packet = (WINUSB_POLICY_PACKET*)rawBuf;
    packet->InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet->PipeAddress = 0; // Unused?
    packet->PolicyType = PolicyType;
    
    if (Value && ValueLength > 0) {
        memcpy(rawBuf + sizeof(WINUSB_POLICY_PACKET), Value, ValueLength);
    }

    result = SyncIoControl(handle->FileHandle, IOCTL_WINUSB_SET_POWER_POLICY, rawBuf, inputSize, NULL, 0, &bytes);

    HeapFree(GetProcessHeap(), 0, rawBuf);
    return result;
}

BOOL WINAPI WinUsb_StartTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID StartTrackingInfo) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_StopTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID StopTrackingInfo) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_UnregisterIsochBuffer(WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_WriteIsochPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_WriteIsochPipeAsap(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    PWINUSB_INTERNAL_HANDLE handle = (PWINUSB_INTERNAL_HANDLE)InterfaceHandle;
    DWORD bytesWritten = 0;
    BOOL result;
    WINUSB_PIPE_IO_PACKET packet;

    if (!handle) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (LengthTransferred) {
        *LengthTransferred = 0;
    }

    packet.InterfaceIndex = GetDriverInterfaceIndex(handle->InterfaceIndex);
    packet.PipeAddress = PipeID;

    if (Overlapped) {
        result = DeviceIoControl(handle->FileHandle, 
                             IOCTL_WINUSB_WRITE_PIPE,
                             &packet, sizeof(packet),
                             Buffer, BufferLength,
                             &bytesWritten, Overlapped);
    } else {
        result = SyncIoControl(handle->FileHandle, 
                             IOCTL_WINUSB_WRITE_PIPE,
                             &packet, sizeof(packet),
                             Buffer, BufferLength,
                             &bytesWritten);
    }

    if (result && LengthTransferred) {
        *LengthTransferred = bytesWritten;
    }

    return result;
}
