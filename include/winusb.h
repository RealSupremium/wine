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

#ifndef _WINUSB_H_
#define _WINUSB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <usb.h>

typedef struct _WINUSB_SETUP_PACKET {
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

typedef struct _WINUSB_PIPE_INFORMATION {
    USBD_PIPE_TYPE PipeType;
    UCHAR          PipeId;
    USHORT         MaximumPacketSize;
    UCHAR          Interval;
} WINUSB_PIPE_INFORMATION, *PWINUSB_PIPE_INFORMATION;

typedef struct _WINUSB_PIPE_INFORMATION_EX {
    USBD_PIPE_TYPE PipeType;
    UCHAR          PipeId;
    USHORT         MaximumPacketSize;
    UCHAR          Interval;
    ULONG          MaximumBytesPerInterval;
} WINUSB_PIPE_INFORMATION_EX, *PWINUSB_PIPE_INFORMATION_EX;

typedef struct _WINUSB_PIPE_IO_PACKET {
    UCHAR InterfaceIndex;
    UCHAR PipeAddress;
} WINUSB_PIPE_IO_PACKET;

typedef struct _WINUSB_SET_ALT_SETTING_PACKET {
    UCHAR InterfaceIndex;
    UCHAR AlternateSetting;
} WINUSB_SET_ALT_SETTING_PACKET;

typedef struct _WINUSB_POLICY_PACKET {
    UCHAR InterfaceIndex;
    UCHAR PipeAddress;
    USHORT Reserved;
    ULONG PolicyType;
    ULONG Reserved2;
} WINUSB_POLICY_PACKET;

typedef struct _WINUSB_CONTROL_PACKET {
    UCHAR InterfaceIndex;
    WINUSB_SETUP_PACKET SetupPacket; 
} WINUSB_CONTROL_PACKET;

typedef struct _WINUSB_DESCRIPTOR_PACKET {
    UCHAR DescriptorType;
    UCHAR Index;
    USHORT LanguageId;
} WINUSB_DESCRIPTOR_PACKET;

typedef PVOID WINUSB_INTERFACE_HANDLE, *PWINUSB_INTERFACE_HANDLE;
typedef PVOID WINUSB_ISOCH_BUFFER_HANDLE, *PWINUSB_ISOCH_BUFFER_HANDLE;

BOOL WINAPI WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL WINAPI WinUsb_AbortPipeAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE InterfaceHandle);
BOOL WINAPI WinUsb_GetAdjustedFrameNumber(PULONG CurrentFrameNumber, PVOID TimeStamp);
BOOL WINAPI WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AssociatedInterfaceIndex, PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle);
BOOL WINAPI WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, PUCHAR SettingNumber);
BOOL WINAPI WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE InterfaceHandle, PULONG CurrentFrameNumber, PULONG TimeStamp);
BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID FrameQpcInfo);
BOOL WINAPI WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR DescriptorType, UCHAR Index, USHORT LanguageID, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred);
BOOL WINAPI WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait);
BOOL WINAPI WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, PULONG ValueLength, PVOID Value);
BOOL WINAPI WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, PULONG ValueLength, PVOID Value);
BOOL WINAPI WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle);
PVOID WINAPI WinUsb_ParseConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor, PVOID StartPosition, LONG InterfaceNumber, LONG AlternateSetting, LONG InterfaceClass, LONG InterfaceSubClass, LONG InterfaceProtocol);
PVOID WINAPI WinUsb_ParseDescriptors(PVOID DescriptorBuffer, ULONG TotalLength, PVOID StartPosition, LONG DescriptorType);
BOOL WINAPI WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG InformationType, PULONG BufferLength, PVOID Buffer);
BOOL WINAPI WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateSettingNumber, PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor);
BOOL WINAPI WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION PipeInformation);
BOOL WINAPI WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION_EX PipeInformationEx);
BOOL WINAPI WinUsb_ReadIsochPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_ReadIsochPipeAsap(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle);
BOOL WINAPI WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL WINAPI WinUsb_ResetPipeAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL WINAPI WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber);
BOOL WINAPI WinUsb_SetCurrentAlternateSettingAsync(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber);
BOOL WINAPI WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, ULONG ValueLength, PVOID Value);
BOOL WINAPI WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, ULONG ValueLength, PVOID Value);
BOOL WINAPI WinUsb_StartTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID StartTrackingInfo);
BOOL WINAPI WinUsb_StopTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle, PVOID StopTrackingInfo);
BOOL WINAPI WinUsb_UnregisterIsochBuffer(WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle);
BOOL WINAPI WinUsb_WriteIsochPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_WriteIsochPipeAsap(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL WINAPI WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);

#ifdef __cplusplus
}
#endif

#endif /* _WINUSB_H_ */
