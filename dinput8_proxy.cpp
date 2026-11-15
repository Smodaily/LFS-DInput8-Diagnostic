#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <strsafe.h>

using DirectInput8CreateFn = HRESULT (WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static HMODULE g_realDinput8 = nullptr;
static DirectInput8CreateFn g_realDirectInput8Create = nullptr;
static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
static wchar_t g_logPath[MAX_PATH] = {};

static void InitLogPath() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&InitLogPath), &self)) return;
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(self, modulePath, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(modulePath, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';
    StringCchPrintfW(g_logPath, MAX_PATH, L"%sLFS_Assist.log", modulePath);
}

static void Log(const wchar_t* message) {
    if (!g_logPath[0]) InitLogPath();
    OutputDebugStringW(message);
    OutputDebugStringW(L"\r\n");
    if (!g_logPath[0]) return;
    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[1024] = {};
    StringCchPrintfW(line, 1024, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds, message);
    DWORD bytes = 0;
    WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &bytes, nullptr);
    CloseHandle(file);
}

static BOOL CALLBACK InitializeProxy(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t systemDir[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        Log(L"ERROR: GetSystemDirectoryW failed");
        return TRUE;
    }
    wchar_t realPath[MAX_PATH] = {};
    if (FAILED(StringCchPrintfW(realPath, MAX_PATH, L"%s\\dinput8.dll", systemDir))) {
        Log(L"ERROR: system DLL path too long");
        return TRUE;
    }
    g_realDinput8 = LoadLibraryW(realPath);
    if (!g_realDinput8) {
        wchar_t msg[256] = {};
        StringCchPrintfW(msg, 256, L"ERROR: cannot load system dinput8.dll, Win32=%lu", GetLastError());
        Log(msg);
        return TRUE;
    }
    g_realDirectInput8Create = reinterpret_cast<DirectInput8CreateFn>(
        GetProcAddress(g_realDinput8, "DirectInput8Create"));
    if (!g_realDirectInput8Create) {
        Log(L"ERROR: DirectInput8Create export not found in system DLL");
        return TRUE;
    }
    Log(L"Proxy initialized; system dinput8.dll loaded successfully");
    return TRUE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid,
                                 LPVOID* out, LPUNKNOWN outer) {
    InitOnceExecuteOnce(&g_initOnce, InitializeProxy, nullptr, nullptr);
    if (!g_realDirectInput8Create) {
        if (out) *out = nullptr;
        Log(L"DirectInput8Create called, but forwarding target is unavailable");
        return DIERR_NOTINITIALIZED;
    }
    Log(L"LFS (or another process) called DirectInput8Create");
    HRESULT hr = g_realDirectInput8Create(instance, version, riid, out, outer);
    wchar_t msg[128] = {};
    StringCchPrintfW(msg, 128, L"DirectInput8Create returned HRESULT=0x%08lX", static_cast<unsigned long>(hr));
    Log(msg);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
