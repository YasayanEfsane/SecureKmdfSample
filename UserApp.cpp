#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <climits>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "Public.h"

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : value_(value) {}

    ~UniqueHandle() {
        if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) {
            CloseHandle(value_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept {
        return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
    }

private:
    HANDLE value_;
};

static std::wstring
GetErrorText(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);

    std::wstring result = (length != 0 && buffer != nullptr)
        ? std::wstring(buffer, length)
        : L"Unknown error";

    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return result;
}

static bool
WideToUtf8(std::wstring_view input, std::string& output)
{
    output.clear();
    if (input.empty()) {
        return true;
    }
    if (input.size() > static_cast<size_t>(INT_MAX)) {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return false;
    }

    const int inputLength = static_cast<int>(input.size());
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input.data(),
        inputLength,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return false;
    }

    output.resize(static_cast<size_t>(required));
    return WideCharToMultiByte(
               CP_UTF8,
               WC_ERR_INVALID_CHARS,
               input.data(),
               inputLength,
               output.data(),
               required,
               nullptr,
               nullptr) == required;
}

static bool
Utf8ToWide(const UCHAR* input, ULONG inputLength, std::wstring& output)
{
    output.clear();
    if (inputLength == 0) {
        return true;
    }
    if (input == nullptr || inputLength > static_cast<ULONG>(INT_MAX)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    const char* bytes = reinterpret_cast<const char*>(input);
    const int byteCount = static_cast<int>(inputLength);
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        bytes,
        byteCount,
        nullptr,
        0);
    if (required <= 0) {
        return false;
    }

    output.resize(static_cast<size_t>(required));
    return MultiByteToWideChar(
               CP_UTF8,
               MB_ERR_INVALID_CHARS,
               bytes,
               byteCount,
               output.data(),
               required) == required;
}

static ULONGLONG
CreateCorrelationId()
{
    FILETIME fileTime{};
    ULARGE_INTEGER value{};

    GetSystemTimePreciseAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;

    /* Zero is reserved by the protocol. */
    return value.QuadPart != 0 ? value.QuadPart : 1ULL;
}

static bool
ValidateResponse(
    const MY_PACKET_REQUEST& request,
    const MY_PACKET_RESPONSE& response,
    DWORD bytesReturned
    )
{
    if (bytesReturned != sizeof(MY_PACKET_RESPONSE) ||
        response.StructSize != sizeof(MY_PACKET_RESPONSE) ||
        response.Version != MY_PROTOCOL_VERSION ||
        response.Operation != request.Operation ||
        response.Result != MY_RESULT_SUCCESS ||
        response.CorrelationId != request.CorrelationId ||
        response.PayloadLength != request.PayloadLength ||
        response.PayloadLength > MY_MAX_PAYLOAD_SIZE ||
        response.Reserved != 0) {
        return false;
    }

    /* Enforce the same canonical zero-tail rule on kernel output. */
    for (ULONG index = response.PayloadLength;
         index < MY_MAX_PAYLOAD_SIZE;
         ++index) {
        if (response.Payload[index] != 0) {
            return false;
        }
    }

    return true;
}

int wmain(int argc, wchar_t* argv[])
{
    const std::wstring inputText =
        (argc >= 2) ? argv[1] : L"Hello KMDF, secure packet";
    std::string inputUtf8;

    if (!WideToUtf8(inputText, inputUtf8)) {
        std::wcerr << L"Input could not be converted to UTF-8. Error: "
                   << GetLastError() << L"\n";
        return 1;
    }

    if (inputUtf8.size() > MY_MAX_PAYLOAD_SIZE) {
        std::wcerr << L"Input may contain at most " << MY_MAX_PAYLOAD_SIZE
                   << L" UTF-8 bytes.\n";
        return 1;
    }

    const UniqueHandle device(CreateFileW(
        MY_DEVICE_WIN32_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));

    if (!device.valid()) {
        const DWORD error = GetLastError();
        std::wcerr << L"Could not open the device (" << error << L"): "
                   << GetErrorText(error);
        if (error == ERROR_ACCESS_DENIED) {
            std::wcerr << L"Run the application from an elevated Administrator session.\n";
        }
        return 1;
    }

    /* Value initialization zeroes every reserved and unused payload byte. */
    MY_PACKET_REQUEST request{};
    request.StructSize = static_cast<ULONG>(sizeof(request));
    request.Version = MY_PROTOCOL_VERSION;
    request.Operation = MY_OPERATION_ASCII_UPPERCASE;
    request.Flags = 0;
    request.CorrelationId = CreateCorrelationId();
    request.PayloadLength = static_cast<ULONG>(inputUtf8.size());
    request.Reserved = 0;
    if (!inputUtf8.empty()) {
        std::memcpy(request.Payload, inputUtf8.data(), inputUtf8.size());
    }

    MY_PACKET_RESPONSE response{};
    DWORD bytesReturned = 0;

    const BOOL ok = DeviceIoControl(
        device.get(),
        IOCTL_MY_PROCESS_PACKET,
        &request,
        static_cast<DWORD>(sizeof(request)),
        &response,
        static_cast<DWORD>(sizeof(response)),
        &bytesReturned,
        nullptr);
    const DWORD ioctlError = ok ? ERROR_SUCCESS : GetLastError();

    if (!inputUtf8.empty()) {
        SecureZeroMemory(inputUtf8.data(), inputUtf8.size());
    }

    if (!ok) {
        std::wcerr << L"DeviceIoControl failed (" << ioctlError << L"): "
                   << GetErrorText(ioctlError);
        SecureZeroMemory(&request, sizeof(request));
        SecureZeroMemory(&response, sizeof(response));
        return 1;
    }

    if (!ValidateResponse(request, response, bytesReturned)) {
        std::wcerr << L"The driver response failed protocol validation.\n";
        SecureZeroMemory(&request, sizeof(request));
        SecureZeroMemory(&response, sizeof(response));
        return 1;
    }

    std::wstring outputText;
    if (!Utf8ToWide(response.Payload, response.PayloadLength, outputText)) {
        std::wcerr << L"The response is not valid UTF-8.\n";
        SecureZeroMemory(&request, sizeof(request));
        SecureZeroMemory(&response, sizeof(response));
        return 1;
    }

    std::wcout << L"Input    : " << inputText << L"\n"
               << L"Response : " << outputText << L"\n";

    SecureZeroMemory(&request, sizeof(request));
    SecureZeroMemory(&response, sizeof(response));
    return 0;
}
