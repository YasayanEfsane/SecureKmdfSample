#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

#include "../Public.h"

namespace {

int g_failures = 0;

void Check(bool condition, std::string_view name)
{
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failures;
    }
}

MY_PACKET_REQUEST MakeRequest(ULONG operation, const char* payload)
{
    MY_PACKET_REQUEST request{};
    request.StructSize = sizeof(request);
    request.Version = MY_PROTOCOL_VERSION;
    request.Operation = operation;
    request.CorrelationId = 0x1122334455667788ULL;
    request.PayloadLength = static_cast<ULONG>(std::strlen(payload));
    std::memcpy(request.Payload, payload, request.PayloadLength);
    return request;
}

bool SendValid(HANDLE device, MY_PACKET_REQUEST& request, MY_PACKET_RESPONSE& response)
{
    DWORD returned = 0;
    SetLastError(ERROR_SUCCESS);
    return DeviceIoControl(device, IOCTL_MY_PROCESS_PACKET,
                           &request, sizeof(request), &response, sizeof(response),
                           &returned, nullptr) != FALSE && returned == sizeof(response);
}

void ExpectRejected(HANDLE device, DWORD ioctl, MY_PACKET_REQUEST& request,
                    DWORD inputSize, DWORD outputSize, std::string_view name)
{
    MY_PACKET_RESPONSE response{};
    DWORD returned = 0;
    SetLastError(ERROR_SUCCESS);
    const BOOL ok = DeviceIoControl(device, ioctl, &request, inputSize,
                                    &response, outputSize, &returned, nullptr);
    Check(ok == FALSE && returned == 0, name);
}

void RunOfflineTests()
{
    Check(sizeof(MY_PACKET_REQUEST) == 288, "request wire size is 288 bytes");
    Check(sizeof(MY_PACKET_RESPONSE) == 288, "response wire size is 288 bytes");
    Check(offsetof(MY_PACKET_REQUEST, StructSize) == 0, "StructSize offset");
    Check(offsetof(MY_PACKET_REQUEST, Version) == 4, "Version offset");
    Check(offsetof(MY_PACKET_REQUEST, Operation) == 8, "Operation offset");
    Check(offsetof(MY_PACKET_REQUEST, Flags) == 12, "Flags offset");
    Check(offsetof(MY_PACKET_REQUEST, CorrelationId) == 16, "CorrelationId offset");
    Check(offsetof(MY_PACKET_REQUEST, PayloadLength) == 24, "PayloadLength offset");
    Check(offsetof(MY_PACKET_REQUEST, Reserved) == 28, "Reserved offset");
    Check(offsetof(MY_PACKET_REQUEST, Payload) == 32, "Payload offset");
    Check((IOCTL_MY_PROCESS_PACKET & 3UL) == METHOD_BUFFERED,
          "IOCTL transfer method is METHOD_BUFFERED");
    Check(((IOCTL_MY_PROCESS_PACKET >> 14) & 3UL) ==
              (FILE_READ_ACCESS | FILE_WRITE_ACCESS),
          "IOCTL requires read and write access");

    MY_PACKET_REQUEST boundary{};
    boundary.StructSize = sizeof(boundary);
    boundary.Version = MY_PROTOCOL_VERSION;
    boundary.Operation = MY_OPERATION_ECHO;
    boundary.CorrelationId = 1;
    boundary.PayloadLength = MY_MAX_PAYLOAD_SIZE;
    std::memset(boundary.Payload, 0xA5, sizeof(boundary.Payload));
    Check(boundary.PayloadLength == sizeof(boundary.Payload),
          "maximum payload fits the fixed packet");
}

void RunIntegrationTests()
{
    HANDLE device = CreateFileW(MY_DEVICE_WIN32_PATH,
                                GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        std::cerr << "[FAIL] open device (Win32 error " << GetLastError() << ")\n";
        ++g_failures;
        return;
    }

    MY_PACKET_RESPONSE response{};
    auto request = MakeRequest(MY_OPERATION_ECHO, "CipherText-123");
    Check(SendValid(device, request, response) &&
              response.Result == MY_RESULT_SUCCESS &&
              response.CorrelationId == request.CorrelationId &&
              response.PayloadLength == request.PayloadLength &&
              std::memcmp(response.Payload, request.Payload, request.PayloadLength) == 0,
          "valid echo request");

    request = MakeRequest(MY_OPERATION_ASCII_UPPERCASE, "Hello-kmdf-123");
    response = {};
    constexpr char expected[] = "HELLO-KMDF-123";
    Check(SendValid(device, request, response) &&
              response.PayloadLength == sizeof(expected) - 1 &&
              std::memcmp(response.Payload, expected, sizeof(expected) - 1) == 0,
          "valid uppercase request");

    request = MakeRequest(MY_OPERATION_ECHO, "x");
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request,
                   sizeof(request) - 1, sizeof(response), "reject short input buffer");
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request,
                   sizeof(request), sizeof(response) - 1, "reject short output buffer");
    ExpectRejected(device, CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED,
                   FILE_READ_ACCESS | FILE_WRITE_ACCESS), request,
                   sizeof(request), sizeof(response), "reject unknown IOCTL");

    request.Version = MY_PROTOCOL_VERSION + 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject unsupported version");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.StructSize = sizeof(request) - 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject invalid structure size");
    request = MakeRequest(99, "x");
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject unknown operation");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.Flags = 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject nonzero flags");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.CorrelationId = 0;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject zero correlation ID");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.Reserved = 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject nonzero reserved field");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.PayloadLength = MY_MAX_PAYLOAD_SIZE + 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject oversized payload length");
    request = MakeRequest(MY_OPERATION_ECHO, "x");
    request.Payload[request.PayloadLength] = 1;
    ExpectRejected(device, IOCTL_MY_PROCESS_PACKET, request, sizeof(request),
                   sizeof(response), "reject nonzero payload tail");

    CloseHandle(device);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    RunOfflineTests();
    if (argc == 2 && std::wstring_view(argv[1]) == L"--integration") {
        RunIntegrationTests();
    } else if (argc != 1) {
        std::cerr << "Usage: ProtocolTests.exe [--integration]\n";
        return 2;
    }

    std::cout << (g_failures == 0 ? "All tests passed.\n" : "Tests failed.\n");
    return g_failures == 0 ? 0 : 1;
}
