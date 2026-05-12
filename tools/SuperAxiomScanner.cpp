#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Region {
    uintptr_t base;
    size_t size;
    DWORD protect;
};

static DWORD FindProcessId(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool IsReadable(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    protect &= 0xff;
    return protect == PAGE_READONLY || protect == PAGE_READWRITE ||
           protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

static bool IsWritable(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    protect &= 0xff;
    return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

int main() {
    DWORD pid = FindProcessId(L"CrimsonDesert.exe");
    if (!pid) {
        std::puts("CrimsonDesert.exe is not running.");
        return 2;
    }

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) {
        std::printf("OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    HMODULE mods[1024]{};
    DWORD needed = 0;
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (EnumProcessModules(proc, mods, sizeof(mods), &needed) && needed >= sizeof(HMODULE)) {
        MODULEINFO mi{};
        GetModuleInformation(proc, mods[0], &mi, sizeof(mi));
        moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        moduleSize = static_cast<size_t>(mi.SizeOfImage);
    }

    std::printf("PID: %lu\n", pid);
    std::printf("Module base: 0x%llX size: 0x%zX\n",
                static_cast<unsigned long long>(moduleBase), moduleSize);

    std::vector<Region> regions;
    uintptr_t p = moduleBase;
    uintptr_t end = moduleBase + moduleSize;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi))) break;
        uintptr_t rbase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t rend = rbase + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && IsReadable(mbi.Protect)) {
            uintptr_t clippedBase = std::max(rbase, moduleBase);
            uintptr_t clippedEnd = std::min(rend, end);
            if (clippedEnd > clippedBase) {
                regions.push_back({ clippedBase, static_cast<size_t>(clippedEnd - clippedBase), mbi.Protect });
            }
        }
        p = rend;
    }

    const uint32_t f20 = 0x41A00000;  // 20.0f
    const uint32_t f40 = 0x42200000;  // 40.0f
    const size_t gap = 0xB40;
    int hits = 0;

    for (const auto& r : regions) {
        if (r.size <= gap + sizeof(float)) continue;
        std::vector<uint8_t> buf(r.size);
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(r.base), buf.data(), buf.size(), &got)) continue;
        if (got <= gap + sizeof(float)) continue;

        for (size_t i = 0; i + gap + 4 <= got; i += 4) {
            uint32_t a = 0, b = 0;
            std::memcpy(&a, buf.data() + i, 4);
            if (a != f20) continue;
            std::memcpy(&b, buf.data() + i + gap, 4);
            if (b != f40) continue;

            uintptr_t rangeAddr = r.base + i;
            uintptr_t pullAddr = rangeAddr + gap;
            std::printf("candidate range RVA=0x%llX pull RVA=0x%llX writable=%s protect=0x%lX\n",
                        static_cast<unsigned long long>(rangeAddr - moduleBase),
                        static_cast<unsigned long long>(pullAddr - moduleBase),
                        IsWritable(r.protect) ? "yes" : "no",
                        r.protect);
            hits++;
        }
    }

    std::printf("Candidates: %d\n", hits);
    CloseHandle(proc);
    return hits ? 0 : 3;
}
