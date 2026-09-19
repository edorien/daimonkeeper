/******************************************************************************/
// The OS-native process entry point (main() / WinMain()) -- the one thing the
// OS itself calls before any KeeperFX code runs. This used to live in
// kfx_platform (PlatformLinux.cpp / PlatformWindows.cpp), the one documented
// case of a lower-ranked library calling up into app_entry's kfxmain() -- see
// docs/refactor/todo/remove-kfxmain-symbol-residual.md. It belongs here
// instead: only app_entry is allowed to depend on every layer, and the
// native entry point's only real job is to hand off to kfxmain(), the actual
// composition-root entry point defined in main.cpp.
/******************************************************************************/
#include "pre_inc.h"
#include "kfxmain.h"

#ifdef _WIN32

#include "bflib_basics.h" // LbJustLog, used by the crash-parachute handler below

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr DWORD MSVC_CXX_EXCEPTION = 0xe06d7363; // 0xE0 + 'msc', a throw from a MSVC-built module
constexpr DWORD GCC_CXX_EXCEPTION = 0x20474343;  // 'GCC' + 1, a throw from a GCC-built module

const char * exception_name(DWORD exception_code)
{
    switch (exception_code) {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case MSVC_CXX_EXCEPTION: return "C++ exception (MSVC runtime)";
        case GCC_CXX_EXCEPTION: return "C++ exception (GCC runtime)";
    }
    return "Unknown";
}

// Names the module which owns an address, without the directory part, and how far into it the
// address sits. Returns false when the address belongs to no loaded module.
bool module_name_of(const void * address, char * name, size_t name_size, uintptr_t * offset)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)address, &module) || (module == nullptr)) {
        return false;
    }
    char module_path[MAX_PATH];
    if (GetModuleFileNameA(module, module_path, sizeof(module_path)) == 0) {
        return false;
    }
    const char * base_name = strrchr(module_path, '\\');
    snprintf(name, name_size, "%s", (base_name != nullptr) ? (base_name + 1) : module_path);
    *offset = (uintptr_t)address - (uintptr_t)module;
    return true;
}

LONG __stdcall Vex_handler(_EXCEPTION_POINTERS *ExceptionInfo)
{
    const auto exception_code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    if (exception_code == DBG_PRINTEXCEPTION_WIDE_C) {
        return EXCEPTION_CONTINUE_EXECUTION; // Thrown by OutputDebugStringW, intended for debugger
    } else if (exception_code == DBG_PRINTEXCEPTION_C) {
        return EXCEPTION_CONTINUE_EXECUTION; // Thrown by OutputDebugStringA, intended for debugger
    } else if (exception_code == 0xe24c4a02) {
        return EXCEPTION_EXECUTE_HANDLER; // Thrown by luaJIT for some reason
    } else if (exception_code == 0x406d1388) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Software exceptions are raised from inside RaiseException(), so their exception address
    // always lands in KERNELBASE and never names the module at fault. A MSVC C++ throw does
    // carry one usable pointer: its third parameter is the ThrowInfo block, which is static
    // data of the module that compiled the throw.
    const auto record = ExceptionInfo->ExceptionRecord;
    char module[MAX_PATH];
    uintptr_t offset = 0;
    if ((exception_code == MSVC_CXX_EXCEPTION) && (record->NumberParameters >= 3)
     && module_name_of((const void *)record->ExceptionInformation[2], module, sizeof(module), &offset)) {
        LbJustLog("Exception 0x%08lx thrown: %s from %s\n", exception_code,
            exception_name(exception_code), module);
    } else if (module_name_of(record->ExceptionAddress, module, sizeof(module), &offset)) {
        LbJustLog("Exception 0x%08lx thrown: %s at %s+0x%lx\n", exception_code,
            exception_name(exception_code), module, (unsigned long)offset);
    } else {
        LbJustLog("Exception 0x%08lx thrown: %s at %p\n", exception_code,
            exception_name(exception_code), record->ExceptionAddress);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    AddVectoredExceptionHandler(0, &Vex_handler);
    // Construct argc/argv from Unicode command line
    int argc = 0;
    auto szArglist = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<char *> argv(argc);
    std::vector<std::vector<char>> args(argc);
    for (int i = 0; i < argc; ++i) {
        const auto arg_size = WideCharToMultiByte(CP_UTF8, 0, szArglist[i], -1, nullptr, 0, nullptr, nullptr);
        if (arg_size > 0) {
            args[i] = std::vector<char>(arg_size);
            WideCharToMultiByte(CP_UTF8, 0, szArglist[i], -1, args[i].data(), arg_size, nullptr, nullptr);
        } else {
            args[i] = std::vector<char>(1);
        }
        argv[i] = args[i].data();
    }
    LocalFree(szArglist);
    return kfxmain(argc, argv.data());
}

#else // !_WIN32

extern "C" int main(int argc, char *argv[])
{
    return kfxmain(argc, argv);
}

#endif
#include "post_inc.h"
