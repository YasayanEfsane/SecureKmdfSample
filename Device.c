#include "Public.h"
#include <wdmsec.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MyCreateControlDevice)
#pragma alloc_text(PAGE, MyEvtDeviceContextCleanup)
#endif

NTSTATUS
MyCreateControlDevice(
    _In_ WDFDRIVER Driver,
    _Out_ WDFDEVICE* DeviceOut
    )
{
    DECLARE_CONST_UNICODE_STRING(deviceName, MY_DEVICE_NT_NAME);
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, MY_DEVICE_DOS_NAME);
    DECLARE_CONST_UNICODE_STRING(deviceSddl, SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PWDFDEVICE_INIT deviceInit = NULL;
    WDFDEVICE device = NULL;
    PMY_DEVICE_CONTEXT deviceContext;
    NTSTATUS status;

    PAGED_CODE();

    if (DeviceOut == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *DeviceOut = NULL;

    /*
     * This SDDL grants Generic-All only to LocalSystem (SY) and built-in
     * Administrators (BA). Passing it at allocation applies the ACL before
     * the named control device becomes reachable.
     */
    deviceInit = WdfControlDeviceInitAllocate(Driver, &deviceSddl);
    if (deviceInit == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, TRUE);

    /*
     * This selects buffered I/O for read/write requests. IOCTL buffering is
     * selected independently by METHOD_BUFFERED in Public.h.
     */
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoBuffered);

    /* WDF also sets FILE_DEVICE_SECURE_OPEN; this makes the intent explicit. */
    WdfDeviceInitSetCharacteristics(deviceInit, FILE_DEVICE_SECURE_OPEN, TRUE);

    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        goto Exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &deviceAttributes,
        MY_DEVICE_CONTEXT);
    deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    deviceAttributes.EvtCleanupCallback = MyEvtDeviceContextCleanup;

    status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        goto Exit;
    }

    deviceContext = MyGetDeviceContext(device);
    deviceContext->CompletedRequestCount = 0;

    status = MyCreateQueue(device);
    if (!NT_SUCCESS(status)) {
        goto Exit;
    }

    status = WdfDeviceCreateSymbolicLink(device, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        goto Exit;
    }

    /* A control device rejects I/O until this mandatory call is made. */
    WdfControlFinishInitializing(device);

    *DeviceOut = device;
    device = NULL; /* Ownership is transferred to the driver context. */
    status = STATUS_SUCCESS;

Exit:
    if (deviceInit != NULL) {
        /* Required when WdfControlDeviceInitAllocate succeeded but create did not. */
        WdfDeviceInitFree(deviceInit);
    }

    if (device != NULL) {
        /* Also deletes any queue/link already parented to the partial device. */
        WdfObjectDelete(device);
    }

    return status;
}

VOID
MyEvtDeviceContextCleanup(
    _In_ WDFOBJECT DeviceObject
    )
{
    PMY_DEVICE_CONTEXT deviceContext;

    PAGED_CODE(); /* WDFDEVICE cleanup callbacks run at PASSIVE_LEVEL. */

    deviceContext = MyGetDeviceContext((WDFDEVICE)DeviceObject);
    RtlSecureZeroMemory(deviceContext, sizeof(*deviceContext));
}
