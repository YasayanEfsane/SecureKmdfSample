#include "Public.h"

static
NTSTATUS
MyValidateRequest(
    _In_ const MY_PACKET_REQUEST* Request
    );

static
VOID
MyBuildResponse(
    _In_ const MY_PACKET_REQUEST* Request,
    _Out_ MY_PACKET_RESPONSE* Response
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MyCreateQueue)
#pragma alloc_text(PAGE, MyEvtIoDeviceControl)
#pragma alloc_text(PAGE, MyValidateRequest)
#pragma alloc_text(PAGE, MyBuildResponse)
#endif

NTSTATUS
MyCreateQueue(
    _In_ WDFDEVICE Device
    )
{
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    NTSTATUS status;

    PAGED_CODE();

    /* Sequential dispatch removes shared-state races for this small endpoint. */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = MyEvtIoDeviceControl;

    /* Control-device queues must not participate in PnP power management. */
    queueConfig.PowerManaged = WdfFalse;

    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        &queueAttributes,
        WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
MyEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    PMY_PACKET_REQUEST inputBuffer = NULL;
    PMY_PACKET_RESPONSE outputBuffer = NULL;
    MY_PACKET_REQUEST requestCopy;
    MY_PACKET_RESPONSE responseCopy;
    PMY_DEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    size_t retrievedInputLength = 0;
    size_t retrievedOutputLength = 0;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    PAGED_CODE(); /* Queue ExecutionLevel forces this callback to PASSIVE_LEVEL. */

    RtlZeroMemory(&requestCopy, sizeof(requestCopy));
    RtlZeroMemory(&responseCopy, sizeof(responseCopy));

    if (IoControlCode != IOCTL_MY_PROCESS_PACKET) {
        goto Complete;
    }

    /*
     * WdfRequestRetrieve*Buffer takes a minimum, not an exact size. Therefore
     * exact lengths are checked first to reject both truncated and oversized
     * protocol messages (version confusion / request smuggling defense).
     */
    if (InputBufferLength != sizeof(MY_PACKET_REQUEST)) {
        status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    if (OutputBufferLength < sizeof(MY_PACKET_RESPONSE)) {
        status = STATUS_BUFFER_TOO_SMALL;
        goto Complete;
    }

    if (OutputBufferLength != sizeof(MY_PACKET_RESPONSE)) {
        status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(MY_PACKET_REQUEST),
        (PVOID*)&inputBuffer,
        &retrievedInputLength);
    if (!NT_SUCCESS(status)) {
        goto Complete;
    }

    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(MY_PACKET_RESPONSE),
        (PVOID*)&outputBuffer,
        &retrievedOutputLength);
    if (!NT_SUCCESS(status)) {
        goto Complete;
    }

    /* Defense in depth: verify the lengths returned by KMDF as well. */
    if (retrievedInputLength != sizeof(MY_PACKET_REQUEST)) {
        status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (retrievedOutputLength < sizeof(MY_PACKET_RESPONSE)) {
        status = STATUS_BUFFER_TOO_SMALL;
        goto Complete;
    }
    if (retrievedOutputLength != sizeof(MY_PACKET_RESPONSE)) {
        status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    /*
     * METHOD_BUFFERED can use one shared SystemBuffer for input and output.
     * Copy the complete, already size-checked input exactly once before any
     * output write. This prevents overlap bugs and double-fetch patterns.
     */
    RtlCopyMemory(&requestCopy, inputBuffer, sizeof(requestCopy));

    status = MyValidateRequest(&requestCopy);
    if (!NT_SUCCESS(status)) {
        goto Complete;
    }

    MyBuildResponse(&requestCopy, &responseCopy);

    /* responseCopy was fully zeroed, preventing uninitialized kernel leaks. */
    RtlCopyMemory(outputBuffer, &responseCopy, sizeof(responseCopy));
    information = sizeof(responseCopy);

    device = WdfIoQueueGetDevice(Queue);
    deviceContext = MyGetDeviceContext(device);
    InterlockedIncrement64(&deviceContext->CompletedRequestCount);

    status = STATUS_SUCCESS;

Complete:
    /* The local copy might contain plaintext or ciphertext; wipe it reliably. */
    RtlSecureZeroMemory(&requestCopy, sizeof(requestCopy));
    RtlSecureZeroMemory(&responseCopy, sizeof(responseCopy));

    /* On every error, Information remains zero, so no stale bytes are returned. */
    WdfRequestCompleteWithInformation(Request, status, information);
}

static
NTSTATUS
MyValidateRequest(
    _In_ const MY_PACKET_REQUEST* Request
    )
{
    ULONG index;

    PAGED_CODE();

    if (Request == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->StructSize != sizeof(MY_PACKET_REQUEST) ||
        Request->Version != MY_PROTOCOL_VERSION ||
        Request->Flags != 0 ||
        Request->Reserved != 0 ||
        Request->CorrelationId == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Operation != MY_OPERATION_ECHO &&
        Request->Operation != MY_OPERATION_ASCII_UPPERCASE) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->PayloadLength > MY_MAX_PAYLOAD_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Canonical encoding: unused bytes must be zero; hidden trailing data fails. */
    for (index = Request->PayloadLength;
         index < MY_MAX_PAYLOAD_SIZE;
         ++index) {
        if (Request->Payload[index] != 0) {
            return STATUS_INVALID_PARAMETER;
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
MyBuildResponse(
    _In_ const MY_PACKET_REQUEST* Request,
    _Out_ MY_PACKET_RESPONSE* Response
    )
{
    ULONG index;
    UCHAR value;

    PAGED_CODE();

    RtlZeroMemory(Response, sizeof(*Response));
    Response->StructSize = sizeof(*Response);
    Response->Version = MY_PROTOCOL_VERSION;
    Response->Operation = Request->Operation;
    Response->Result = MY_RESULT_SUCCESS;
    Response->CorrelationId = Request->CorrelationId;
    Response->PayloadLength = Request->PayloadLength;

    for (index = 0; index < Request->PayloadLength; ++index) {
        value = Request->Payload[index];

        if (Request->Operation == MY_OPERATION_ASCII_UPPERCASE &&
            value >= (UCHAR)'a' && value <= (UCHAR)'z') {
            value = (UCHAR)(value - ((UCHAR)'a' - (UCHAR)'A'));
        }

        Response->Payload[index] = value;
    }
}
