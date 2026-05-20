/*
 * WinUSB driver
 *
 * Copyright (C) 2026 Supremium (realsupremium@outlook.com)
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
#include <assert.h>
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"
#include "ddk/usb.h"
#include "ddk/usbdlib.h"
#include "ddk/usbioctl.h"
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

#define PIPE_TRANSFER_TIMEOUT 0x03
#define SHORT_PACKET_TERMINATE 0x01
#define AUTO_CLEAR_STALL 0x02
#define IGNORE_SHORT_PACKETS 0x04
#define ALLOW_PARTIAL_READS 0x05
#define AUTO_FLUSH 0x06
#define RAW_IO 0x07

typedef struct _WINUSB_SETUP_PACKET {
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

typedef struct _WINUSB_CONTROL_PACKET {
    UCHAR InterfaceIndex;
    WINUSB_SETUP_PACKET SetupPacket;
} WINUSB_CONTROL_PACKET;

typedef struct _WINUSB_PIPE_IO_PACKET {
    UCHAR InterfaceIndex;
    UCHAR PipeAddress;
} WINUSB_PIPE_IO_PACKET;

typedef struct _WINUSB_SET_ALT_SETTING_PACKET {
    UCHAR InterfaceIndex;
    UCHAR AlternateSetting;
} WINUSB_SET_ALT_SETTING_PACKET;

typedef struct _WINUSB_DESCRIPTOR_PACKET {
    UCHAR DescriptorType;
    UCHAR Index;
    USHORT LanguageId;
} WINUSB_DESCRIPTOR_PACKET;

typedef struct _WINUSB_GET_PIPE_POLICY_PACKET {
    UCHAR InterfaceIndex;
    UCHAR PipeAddress;
    USHORT Reserved;
    ULONG PolicyType;
} WINUSB_GET_PIPE_POLICY_PACKET;

typedef struct _DEVICE_EXTENSION {
    DEVICE_OBJECT *DeviceObject;
    DEVICE_OBJECT *NextDeviceObject;
    USBD_INTERFACE_INFORMATION *Interface;
    UCHAR InterfaceNumber;
    DEVICE_OBJECT *Pdo;
    UNICODE_STRING *InterfaceLinks;
    ULONG InterfaceLinkCount;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#ifndef GET_USBD_INTERFACE_SIZE
#define GET_USBD_INTERFACE_SIZE(num_endpoints) (sizeof(USBD_INTERFACE_INFORMATION) + (sizeof(USBD_PIPE_INFORMATION) * (num_endpoints)) - sizeof(USBD_PIPE_INFORMATION))
#endif

#ifndef GET_SELECT_INTERFACE_REQUEST_SIZE
#define GET_SELECT_INTERFACE_REQUEST_SIZE(num_endpoints) (sizeof(struct _URB_SELECT_INTERFACE) + (sizeof(USBD_PIPE_INFORMATION) * (num_endpoints)) - sizeof(USBD_PIPE_INFORMATION))
#endif

static inline void UsbBuildGetDescriptorRequest(URB *urb, USHORT length, UCHAR descriptor_type,
        UCHAR index, USHORT language_id, void *transfer_buffer, PMDL transfer_buffer_mdl,
        ULONG transfer_buffer_length, URB *link)
{
    urb->UrbHeader.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    urb->UrbHeader.Length = length;
    urb->UrbControlDescriptorRequest.TransferBufferLength = transfer_buffer_length;
    urb->UrbControlDescriptorRequest.TransferBuffer = transfer_buffer;
    urb->UrbControlDescriptorRequest.TransferBufferMDL = transfer_buffer_mdl;
    urb->UrbControlDescriptorRequest.DescriptorType = descriptor_type;
    urb->UrbControlDescriptorRequest.Index = index;
    urb->UrbControlDescriptorRequest.LanguageId = language_id;
    urb->UrbControlDescriptorRequest.UrbLink = link;
}

static void register_interfaces(DEVICE_EXTENSION *ext)
{
    KEY_VALUE_PARTIAL_INFORMATION *info;
    UNICODE_STRING value_name;
    HANDLE key;
    ULONG size;
    WCHAR *p;

    RtlInitUnicodeString(&value_name, L"DeviceInterfaceGUIDs");

    if (IoOpenDeviceRegistryKey(ext->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &key))
        return;


    if (ZwQueryValueKey(key, &value_name, KeyValuePartialInformation, NULL, 0, &size) == STATUS_BUFFER_TOO_SMALL)
    {
        if ((info = ExAllocatePool(PagedPool, size)))
        {
            if (!ZwQueryValueKey(key, &value_name, KeyValuePartialInformation, info, size, &size) &&
                info->Type == REG_MULTI_SZ)
            {
                for (p = (WCHAR *)info->Data; *p; p += p[0] ? (wcslen(p) + 1) : 1)
                {
                    UNICODE_STRING guid_str, link;
                    GUID guid;

                    RtlInitUnicodeString(&guid_str, p);
                    if (!RtlGUIDFromString(&guid_str, &guid) &&
                        !IoRegisterDeviceInterface(ext->Pdo, &guid, NULL, &link))
                    {
                        UNICODE_STRING *new_links;

                        IoSetDeviceInterfaceState(&link, TRUE);

                        if (ext->InterfaceLinks)
                            new_links = ExAllocatePool(PagedPool, (ext->InterfaceLinkCount + 1) * sizeof(UNICODE_STRING));
                        else
                            new_links = ExAllocatePool(PagedPool, sizeof(UNICODE_STRING));

                        if (new_links)
                        {
                            if (ext->InterfaceLinks)
                            {
                                RtlCopyMemory(new_links, ext->InterfaceLinks, ext->InterfaceLinkCount * sizeof(UNICODE_STRING));
                                ExFreePool(ext->InterfaceLinks);
                            }
                            ext->InterfaceLinks = new_links;
                            ext->InterfaceLinks[ext->InterfaceLinkCount++] = link;
                        }
                        else
                        {
                            RtlFreeUnicodeString(&link);
                        }
                    }
                }
            }
            ExFreePool(info);
        }
    }
    ZwClose(key);
}

static void unregister_interfaces(DEVICE_EXTENSION *ext)
{
    ULONG i;

    for (i = 0; i < ext->InterfaceLinkCount; ++i)
    {
        IoSetDeviceInterfaceState(&ext->InterfaceLinks[i], FALSE);
        RtlFreeUnicodeString(&ext->InterfaceLinks[i]);
    }
    if (ext->InterfaceLinks)
        ExFreePool(ext->InterfaceLinks);
    ext->InterfaceLinks = NULL;
    ext->InterfaceLinkCount = 0;
}

struct completion_ctx
{
    PIRP original_irp;
    URB *urb;
};

static NTSTATUS WINAPI urb_completion(DEVICE_OBJECT *device, IRP *irp, void *context)
{
    struct completion_ctx *ctx = context;

    ctx->original_irp->IoStatus.Status = irp->IoStatus.Status;
    ctx->original_irp->IoStatus.Information = 0;

    if (NT_SUCCESS(irp->IoStatus.Status))
    {
        switch (ctx->urb->UrbHeader.Function)
        {
            case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
                ctx->original_irp->IoStatus.Information = ctx->urb->UrbControlDescriptorRequest.TransferBufferLength;
                break;
            case URB_FUNCTION_CONTROL_TRANSFER:
                ctx->original_irp->IoStatus.Information = ctx->urb->UrbControlTransfer.TransferBufferLength;
                break;
            case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
                ctx->original_irp->IoStatus.Information = ctx->urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                break;
        }
    }

    IoCompleteRequest(ctx->original_irp, IO_NO_INCREMENT);
    ExFreePool(ctx->urb);
    ExFreePool(ctx);
    IoFreeIrp(irp);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS submit_urb(DEVICE_EXTENSION *ext, URB *urb, PIRP original_irp)
{
    IO_STATUS_BLOCK io_status;
    PIRP irp;
    KEVENT event;
    NTSTATUS status;

    if (original_irp)
    {
        struct completion_ctx *ctx;

        if (!(ctx = ExAllocatePool(NonPagedPool, sizeof(*ctx)))) return STATUS_NO_MEMORY;
        ctx->original_irp = original_irp;
        ctx->urb = urb;

        if (!(irp = IoAllocateIrp(ext->NextDeviceObject->StackSize, FALSE)))
        {
            ExFreePool(ctx);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        IoSetCompletionRoutine(irp, urb_completion, ctx, TRUE, TRUE, TRUE);
        IoGetNextIrpStackLocation(irp)->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        IoGetNextIrpStackLocation(irp)->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
        IoGetNextIrpStackLocation(irp)->Parameters.Others.Argument1 = urb;

        IoMarkIrpPending(original_irp);
        IoCallDriver(ext->NextDeviceObject, irp);
        return STATUS_PENDING;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        ext->NextDeviceObject,
                                        NULL, 0, NULL, 0,
                                        TRUE, &event, &io_status);
    if (!irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoGetNextIrpStackLocation(irp)->Parameters.Others.Argument1 = urb;

    status = IoCallDriver(ext->NextDeviceObject, irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = io_status.Status;
    }

    return status;
}

static NTSTATUS select_interface(DEVICE_EXTENSION *ext, UCHAR alt_setting)
{
    URB *urb;
    USB_CONFIGURATION_DESCRIPTOR *config_desc;
    USB_INTERFACE_DESCRIPTOR *iface_desc;
    NTSTATUS status;
    ULONG size;
    int i;

    urb = ExAllocatePool(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));
    if (!urb) return STATUS_NO_MEMORY;

    config_desc = ExAllocatePool(PagedPool, sizeof(USB_CONFIGURATION_DESCRIPTOR));
    if (!config_desc)
    {
        ExFreePool(urb);
        return STATUS_NO_MEMORY;
    }

    UsbBuildGetDescriptorRequest(urb, (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                                 config_desc, NULL, sizeof(USB_CONFIGURATION_DESCRIPTOR), NULL);

    status = submit_urb(ext, urb, NULL);
    if (!NT_SUCCESS(status))
    {
        ExFreePool(config_desc);
        ExFreePool(urb);
        return status;
    }

    size = config_desc->wTotalLength;
    ExFreePool(config_desc);

    config_desc = ExAllocatePool(PagedPool, size);
    if (!config_desc)
    {
        ExFreePool(urb);
        return STATUS_NO_MEMORY;
    }

    UsbBuildGetDescriptorRequest(urb, (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                                 config_desc, NULL, size, NULL);

            status = submit_urb(ext, urb, NULL);
    ExFreePool(urb);
    if (!NT_SUCCESS(status))
    {
        ExFreePool(config_desc);
        return status;
    }

    iface_desc = USBD_ParseConfigurationDescriptorEx(config_desc, config_desc,
                                                     ext->InterfaceNumber, alt_setting,
                                                     -1, -1, -1);
    if (!iface_desc)
    {
        ERR("Could not find interface descriptor for interface %u, alt setting %u.\n",
            ext->InterfaceNumber, alt_setting);
        ExFreePool(config_desc);
        return STATUS_UNSUCCESSFUL;
    }

    size = GET_SELECT_INTERFACE_REQUEST_SIZE(iface_desc->bNumEndpoints);
    urb = ExAllocatePool(NonPagedPool, size);
    if (!urb)
    {
        ExFreePool(config_desc);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(urb, size);
    urb->UrbHeader.Length = (USHORT)size;
    urb->UrbHeader.Function = URB_FUNCTION_SELECT_INTERFACE;
    urb->UrbSelectInterface.ConfigurationHandle = NULL;
    urb->UrbSelectInterface.Interface.Length = GET_USBD_INTERFACE_SIZE(iface_desc->bNumEndpoints);
    urb->UrbSelectInterface.Interface.InterfaceNumber = ext->InterfaceNumber;
    urb->UrbSelectInterface.Interface.AlternateSetting = alt_setting;
    urb->UrbSelectInterface.Interface.NumberOfPipes = iface_desc->bNumEndpoints;

    for (i = 0; i < iface_desc->bNumEndpoints; i++)
    {
        urb->UrbSelectInterface.Interface.Pipes[i].MaximumPacketSize = 0;
        urb->UrbSelectInterface.Interface.Pipes[i].EndpointAddress = 0;
    }

    status = submit_urb(ext, urb, NULL);
    if (NT_SUCCESS(status))
    {
        if (ext->Interface) ExFreePool(ext->Interface);
        ext->Interface = ExAllocatePool(NonPagedPool, urb->UrbSelectInterface.Interface.Length);
        if (ext->Interface)
            RtlCopyMemory(ext->Interface, &urb->UrbSelectInterface.Interface, urb->UrbSelectInterface.Interface.Length);
        else
            status = STATUS_NO_MEMORY;
    }

    ExFreePool(urb);
    ExFreePool(config_desc);
    return status;
}

static USBD_PIPE_HANDLE get_pipe_handle(DEVICE_EXTENSION *ext, UCHAR pipe_address)
{
    ULONG i;
    if (!ext->Interface) return NULL;
    for (i = 0; i < ext->Interface->NumberOfPipes; i++)
    {
        if (ext->Interface->Pipes[i].EndpointAddress == pipe_address)
            return ext->Interface->Pipes[i].PipeHandle;
    }
    return NULL;
}

static NTSTATUS WINAPI dispatch_ioctl(DEVICE_OBJECT *device, IRP *irp)
{
    DEVICE_EXTENSION *ext = device->DeviceExtension;
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG len = 0;

    TRACE("code %#lx\n", code);

    switch (code)
    {
        case IOCTL_WINUSB_INITIALIZE_1:
            break;

        case IOCTL_WINUSB_INITIALIZE_2:
            if (!ext->Interface)
                status = select_interface(ext, 0);
            break;

        case IOCTL_WINUSB_GET_DESCRIPTOR:
        {
            WINUSB_DESCRIPTOR_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            URB *urb;
            ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;

            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            urb = ExAllocatePool(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));
            if (!urb)
            {
                status = STATUS_NO_MEMORY;
                break;
            }

            UsbBuildGetDescriptorRequest(urb, (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                         packet->DescriptorType, packet->Index, packet->LanguageId,
                                         irp->AssociatedIrp.SystemBuffer, NULL, out_len, NULL);

            status = submit_urb(ext, urb, irp);
            if (status != STATUS_PENDING && NT_SUCCESS(status))
                len = urb->UrbControlDescriptorRequest.TransferBufferLength;

            if (status != STATUS_PENDING)
                ExFreePool(urb);
            break;
        }

        case IOCTL_WINUSB_CONTROL_TRANSFER:
        {
            WINUSB_CONTROL_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            URB *urb;

            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            urb = ExAllocatePool(NonPagedPool, sizeof(struct _URB_CONTROL_TRANSFER));
            if (!urb)
            {
                status = STATUS_NO_MEMORY;
                break;
            }

            RtlZeroMemory(urb, sizeof(struct _URB_CONTROL_TRANSFER));
            urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_TRANSFER);
            urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
            urb->UrbControlTransfer.PipeHandle = NULL;
            urb->UrbControlTransfer.TransferFlags = USBD_SHORT_TRANSFER_OK;
            if (packet->SetupPacket.RequestType & 0x80)
                urb->UrbControlTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;

            urb->UrbControlTransfer.TransferBufferMDL = irp->MdlAddress;
            urb->UrbControlTransfer.TransferBufferLength = packet->SetupPacket.Length;
            RtlCopyMemory(urb->UrbControlTransfer.SetupPacket, &packet->SetupPacket, 8);

            status = submit_urb(ext, urb, irp);
            if (status != STATUS_PENDING && NT_SUCCESS(status))
                len = urb->UrbControlTransfer.TransferBufferLength;

            if (status != STATUS_PENDING)
                ExFreePool(urb);
            break;
        }

        case IOCTL_WINUSB_READ_PIPE:
        case IOCTL_WINUSB_WRITE_PIPE:
        {
            WINUSB_PIPE_IO_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            URB *urb;
            USBD_PIPE_HANDLE pipe_handle;

            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            pipe_handle = get_pipe_handle(ext, packet->PipeAddress);
            if (!pipe_handle)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            urb = ExAllocatePool(NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
            if (!urb)
            {
                status = STATUS_NO_MEMORY;
                break;
            }

            RtlZeroMemory(urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
            urb->UrbHeader.Length = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
            urb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
            urb->UrbBulkOrInterruptTransfer.PipeHandle = pipe_handle;
            urb->UrbBulkOrInterruptTransfer.TransferFlags = USBD_SHORT_TRANSFER_OK;
            if (code == IOCTL_WINUSB_READ_PIPE)
                urb->UrbBulkOrInterruptTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;

            urb->UrbBulkOrInterruptTransfer.TransferBufferMDL = irp->MdlAddress;
            urb->UrbBulkOrInterruptTransfer.TransferBufferLength = stack->Parameters.DeviceIoControl.OutputBufferLength;

            status = submit_urb(ext, urb, irp);
            
            if (status != STATUS_PENDING && NT_SUCCESS(status))
                len = urb->UrbBulkOrInterruptTransfer.TransferBufferLength;

            if (status != STATUS_PENDING)
                ExFreePool(urb);
            break;
        }

        case IOCTL_WINUSB_SET_CURRENT_ALT_SETTING:
        {
            WINUSB_SET_ALT_SETTING_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            status = select_interface(ext, packet->AlternateSetting);
            break;
        }

        case IOCTL_WINUSB_GET_CURRENT_ALT_SETTING:
        {
            UCHAR *alt = irp->AssociatedIrp.SystemBuffer;
            if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(UCHAR))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (ext->Interface)
                *alt = ext->Interface->AlternateSetting;
            else
                *alt = 0;
            len = sizeof(UCHAR);
            break;
        }

        case IOCTL_WINUSB_GET_PIPE_POLICY:
        {
            WINUSB_GET_PIPE_POLICY_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
            void *value = irp->AssociatedIrp.SystemBuffer;

            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            switch (packet->PolicyType)
            {
                case PIPE_TRANSFER_TIMEOUT:
                    if (out_len < sizeof(ULONG))
                    {
                        status = STATUS_BUFFER_TOO_SMALL;
                        break;
                    }
                    *(PULONG)value = 5000; /* 5 seconds */
                    len = sizeof(ULONG);
                    break;
                case SHORT_PACKET_TERMINATE:
                case AUTO_CLEAR_STALL:
                case IGNORE_SHORT_PACKETS:
                case AUTO_FLUSH:
                case RAW_IO:
                    if (out_len < sizeof(UCHAR))
                    {
                        status = STATUS_BUFFER_TOO_SMALL;
                        break;
                    }
                    *(PUCHAR)value = FALSE;
                    len = sizeof(UCHAR);
                    break;
                case ALLOW_PARTIAL_READS:
                    if (out_len < sizeof(UCHAR))
                    {
                        status = STATUS_BUFFER_TOO_SMALL;
                        break;
                    }
                    *(PUCHAR)value = TRUE;
                    len = sizeof(UCHAR);
                    break;
                default:
                    FIXME("Unhandled policy type %#lx\n", packet->PolicyType);
                    status = STATUS_INVALID_PARAMETER;
            }
            break;
        }

        case IOCTL_WINUSB_RESET_PIPE:
        case IOCTL_WINUSB_ABORT_PIPE:
        case IOCTL_WINUSB_FLUSH_PIPE:
        {
            WINUSB_PIPE_IO_PACKET *packet = irp->AssociatedIrp.SystemBuffer;
            URB *urb;
            USBD_PIPE_HANDLE pipe_handle;

            if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*packet))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            pipe_handle = get_pipe_handle(ext, packet->PipeAddress);
            if (!pipe_handle)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            urb = ExAllocatePool(NonPagedPool, sizeof(struct _URB_PIPE_REQUEST));
            if (!urb)
            {
                status = STATUS_NO_MEMORY;
                break;
            }
            RtlZeroMemory(urb, sizeof(struct _URB_PIPE_REQUEST));
            urb->UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
            if (code == IOCTL_WINUSB_ABORT_PIPE)
                urb->UrbHeader.Function = URB_FUNCTION_ABORT_PIPE;
            else if (code == IOCTL_WINUSB_RESET_PIPE)
                urb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
            else
                urb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE;

            urb->UrbPipeRequest.PipeHandle = pipe_handle;

            status = submit_urb(ext, urb, NULL);
            ExFreePool(urb);
            break;
        }

        default:
            FIXME("Unhandled ioctl %#lx\n", code);
            status = STATUS_NOT_IMPLEMENTED;
    }

    if (status != STATUS_PENDING)
    {
        irp->IoStatus.Status = status;
        irp->IoStatus.Information = len;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
    }

    return status;
}

static UCHAR query_interface_number(DEVICE_OBJECT *pdo)
{
    IO_STATUS_BLOCK io_status;
    KEVENT event;
    PIRP irp;
    NTSTATUS status;
    UCHAR interface_number = 0;
    WCHAR *ids, *ptr;

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    irp = IoAllocateIrp(pdo->StackSize, FALSE);
    if (!irp) return 0;

    irp->UserEvent = &event;
    irp->UserIosb = &io_status;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = IRP_MJ_PNP;
    stack->MinorFunction = IRP_MN_QUERY_ID;
    stack->Parameters.QueryId.IdType = BusQueryHardwareIDs;

    status = IoCallDriver(pdo, irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = io_status.Status;
    }

    if (NT_SUCCESS(status))
    {
        ids = (WCHAR *)io_status.Information;
        if (!ids) return 0;
        for (ptr = ids; *ptr; ptr += wcslen(ptr) + 1)
        {
            WCHAR *mi = wcsstr(ptr, L"&MI_");
            if (mi)
            {
                interface_number = wcstol(mi + 4, NULL, 16);
                break;
            }
        }
        ExFreePool(ids);
    }
    return interface_number;
}

static NTSTATUS WINAPI irp_completion(DEVICE_OBJECT *device, IRP *irp, void *context)
{
    KeSetEvent(context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS WINAPI dispatch_pnp(DEVICE_OBJECT *device, IRP *irp)
{
    DEVICE_EXTENSION *ext = device->DeviceExtension;
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    switch (stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            KEVENT event;

            KeInitializeEvent(&event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(irp);
            IoSetCompletionRoutine(irp, irp_completion, &event, TRUE, TRUE, TRUE);
            status = IoCallDriver(ext->NextDeviceObject, irp);

            if (NT_SUCCESS(status))
            {
                ext->InterfaceNumber = query_interface_number(ext->Pdo);
                select_interface(ext, 0);
                register_interfaces(ext);
            }
            
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
        }

        case IRP_MN_REMOVE_DEVICE:
            unregister_interfaces(ext);
            if (ext->Interface) ExFreePool(ext->Interface);
            IoSkipCurrentIrpStackLocation(irp);
            status = IoCallDriver(ext->NextDeviceObject, irp);
            IoDetachDevice(ext->NextDeviceObject);
            IoDeleteDevice(device);
            return status;

        default:
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(ext->NextDeviceObject, irp);
    }
}

static NTSTATUS WINAPI dispatch_create(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI dispatch_close(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_add_device(DRIVER_OBJECT *driver, DEVICE_OBJECT *pdo)
{
    DEVICE_EXTENSION *ext;
    DEVICE_OBJECT *device;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(DEVICE_EXTENSION), NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &device);
    if (!NT_SUCCESS(status)) return status;

    ext = device->DeviceExtension;
    ext->DeviceObject = device;
    ext->NextDeviceObject = IoAttachDeviceToDeviceStack(device, pdo);
    ext->Pdo = pdo;

    device->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    device->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

static void WINAPI driver_unload(DRIVER_OBJECT *driver)
{
}

NTSTATUS WINAPI DriverEntry(DRIVER_OBJECT *driver, UNICODE_STRING *path)
{
    driver->DriverExtension->AddDevice = driver_add_device;
    driver->DriverUnload = driver_unload;
    driver->MajorFunction[IRP_MJ_PNP] = dispatch_pnp;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatch_ioctl;
    driver->MajorFunction[IRP_MJ_CREATE] = dispatch_create;
    driver->MajorFunction[IRP_MJ_CLOSE] = dispatch_close;

    return STATUS_SUCCESS;
}