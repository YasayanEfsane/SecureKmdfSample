#pragma once

/*
 * Shared kernel/user ABI for the Secure KMDF sample.
 *
 * Security properties:
 *   - The IOCTL transfer method is METHOD_BUFFERED.
 *   - The wire structures contain no pointers, handles, flexible arrays,
 *     or architecture-sized fields.
 *   - Natural Windows ABI alignment is used and verified at compile time.
 */

#if defined(_KERNEL_MODE)
#include <ntddk.h>
#include <wdf.h>
#else
#include <Windows.h>
#include <winioctl.h>
#endif

#define MY_PROTOCOL_VERSION        1UL
#define MY_MAX_PAYLOAD_SIZE        256UL
#define MY_PACKET_WIRE_SIZE        288UL

#define MY_OPERATION_ECHO             1UL
#define MY_OPERATION_ASCII_UPPERCASE  2UL

#define MY_RESULT_SUCCESS          0UL

#define MY_DEVICE_NT_NAME          L"\\Device\\SecureKmdfSample"
#define MY_DEVICE_DOS_NAME         L"\\DosDevices\\Global\\SecureKmdfSample"
/*
 * Win32 first searches the caller's local DosDevices namespace, then falls
 * back to the global link created above.
 */
#define MY_DEVICE_WIN32_PATH       L"\\\\.\\SecureKmdfSample"

/*
 * 0x800 is in the vendor-reserved function range. Requiring both read and
 * write access means a handle opened without GENERIC_READ | GENERIC_WRITE
 * cannot issue this bidirectional request.
 *
 * METHOD_BUFFERED is the security boundary here: the I/O manager gives KMDF
 * a kernel-owned intermediate buffer instead of an untrusted user pointer.
 */
#define IOCTL_MY_PROCESS_PACKET \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, \
             (FILE_READ_ACCESS | FILE_WRITE_ACCESS))

typedef struct _MY_PACKET_REQUEST {
    ULONG StructSize;
    ULONG Version;
    ULONG Operation;
    ULONG Flags;
    ULONGLONG CorrelationId;
    ULONG PayloadLength;
    ULONG Reserved;
    UCHAR Payload[MY_MAX_PAYLOAD_SIZE];
} MY_PACKET_REQUEST, *PMY_PACKET_REQUEST;

typedef struct _MY_PACKET_RESPONSE {
    ULONG StructSize;
    ULONG Version;
    ULONG Operation;
    ULONG Result;
    ULONGLONG CorrelationId;
    ULONG PayloadLength;
    ULONG Reserved;
    UCHAR Payload[MY_MAX_PAYLOAD_SIZE];
} MY_PACKET_RESPONSE, *PMY_PACKET_RESPONSE;

#if defined(__cplusplus)
static_assert(sizeof(MY_PACKET_REQUEST) == MY_PACKET_WIRE_SIZE,
              "MY_PACKET_REQUEST ABI size changed");
static_assert(sizeof(MY_PACKET_RESPONSE) == MY_PACKET_WIRE_SIZE,
              "MY_PACKET_RESPONSE ABI size changed");
static_assert((IOCTL_MY_PROCESS_PACKET & 0x3UL) == METHOD_BUFFERED,
              "IOCTL must remain METHOD_BUFFERED");
#else
C_ASSERT(sizeof(MY_PACKET_REQUEST) == MY_PACKET_WIRE_SIZE);
C_ASSERT(sizeof(MY_PACKET_RESPONSE) == MY_PACKET_WIRE_SIZE);
C_ASSERT((IOCTL_MY_PROCESS_PACKET & 0x3UL) == METHOD_BUFFERED);
#endif

#if defined(_KERNEL_MODE)

typedef struct _MY_DRIVER_CONTEXT {
    WDFDEVICE ControlDevice;
} MY_DRIVER_CONTEXT, *PMY_DRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MY_DRIVER_CONTEXT, MyGetDriverContext)

typedef struct _MY_DEVICE_CONTEXT {
    volatile LONG64 CompletedRequestCount;
} MY_DEVICE_CONTEXT, *PMY_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MY_DEVICE_CONTEXT, MyGetDeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD MyEvtDriverUnload;
EVT_WDF_OBJECT_CONTEXT_CLEANUP MyEvtDeviceContextCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL MyEvtIoDeviceControl;

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MyCreateControlDevice(
    _In_ WDFDRIVER Driver,
    _Out_ WDFDEVICE* DeviceOut
    );

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
MyCreateQueue(
    _In_ WDFDEVICE Device
    );

#endif /* _KERNEL_MODE */
