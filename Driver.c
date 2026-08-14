#include "Public.h"

/* Four-byte literal; on little-endian pool tools this is displayed as MydT. */
#define MY_POOL_TAG ((ULONG)'TdyM')

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MyEvtDriverUnload)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDRIVER driver = NULL;
    WDFDEVICE controlDevice = NULL;
    PMY_DRIVER_CONTEXT driverContext;
    NTSTATUS status;

    PAGED_CODE(); /* DriverEntry is called at PASSIVE_LEVEL. */

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    config.EvtDriverUnload = MyEvtDriverUnload;
    config.DriverPoolTag = MY_POOL_TAG;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, MY_DRIVER_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        &driver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    driverContext = MyGetDriverContext(driver);
    driverContext->ControlDevice = NULL;

    status = MyCreateControlDevice(driver, &controlDevice);
    if (!NT_SUCCESS(status)) {
        /* MyCreateControlDevice releases every partially created object. */
        return status;
    }

    driverContext->ControlDevice = controlDevice;
    return STATUS_SUCCESS;
}

VOID
MyEvtDriverUnload(
    _In_ WDFDRIVER Driver
    )
{
    PMY_DRIVER_CONTEXT driverContext;
    WDFDEVICE controlDevice;

    PAGED_CODE(); /* Driver ExecutionLevel is explicitly PASSIVE_LEVEL. */

    driverContext = MyGetDriverContext(Driver);
    controlDevice = driverContext->ControlDevice;
    driverContext->ControlDevice = NULL;

    if (controlDevice != NULL) {
        /* Deletes child queues, removes the symbolic link, and runs cleanup. */
        WdfObjectDelete(controlDevice);
    }
}
