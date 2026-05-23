// SuperAxiomForce - SuperAxiomForce.cpp
// Enhanced Axiom Force mod for Crimson Desert
//
// v2.0: Improved stability and new features
//
// INSTALL: drop SuperAxiomForce.asi + SuperAxiomForce.ini into Crimson Desert\bin64\

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <thread>
#include <fstream>
#include <string>
#include <cstdlib>
#include <atomic>
#include <vector>
#include <sstream>

#pragma comment(lib, "psapi.lib")


// ============================================================
//  CONSTANTS
// ============================================================
static const char* kModName = "SuperAxiomForce";
static const char* kLogFile = "SuperAxiomForce.log";
static const char* kIniSection = "AxiomForce";

// Fallback offsets (used if pattern scanning fails)
//static const uintptr_t kAxiomRangeOffset_Fallback        = 0x5E1EFA8;  // found 1.06.00 runtime pair
//static const uintptr_t kAxiomPullSpeedOffset_Fallback     = 0x5E1FAE8;  // found 1.06.00 runtime pair
//static const uintptr_t kAerialManeuverOffset_Fallback     = 0xE6D19D;   // likely 1.06.00 straight-pull call
//static const uintptr_t kReelDurationOffset_Fallback       = 0x446B9C;   // found 1.06.00 paired jbe
//static const uintptr_t kPropellDurationOffset_Fallback    = 0x446E3E;   // found 1.06.00 paired jbe
// Default values
static const float kDefaultRange = 20.0f;
static const float kFallbackRange = 500.0f;
static const float kDefaultPullSpeed = 40.0f;
static const float kFallbackPullSpeed = 1000.0f;

// ============================================================
//  STATE
// ============================================================
static std::string        g_iniPath;
static ULONGLONG          g_lastIniModTime = 0;
static std::atomic<bool>  g_isInitialized{ false };

// Dynamically discovered addresses (nullptr = not found yet)
static float* g_pAxiomRange = nullptr;
static float* g_pAxiomPullSpeed = nullptr;
static uint8_t* g_pAerialManeuver = nullptr;
static uint8_t* g_pReelDuration = nullptr;
static uint8_t* g_pPropellDuration = nullptr;
static uint8_t g_OriginalAerialManeuverBytes[5];

// ============================================================
//  LOGGING
// ============================================================
static void Log(const char* msg) {
    static bool is_new_session = true;
    auto mode = std::ios::app;
    if (is_new_session) {
        mode = std::ios::trunc; // Wipe old log
        is_new_session = false;
    }
    std::ofstream f(kLogFile, mode);
    f << "[" << kModName << "] " << msg << "\n";
}

static void Log(const std::string& msg) {
    Log(msg.c_str());
}

// ============================================================
//  INI READING
// ============================================================
static float ReadIniFloat(const char* section, const char* key, float def) {
    char buf[64] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath.c_str());
    return buf[0] ? static_cast<float>(atof(buf)) : def;
}

static bool ReadIniBool(const char* section, const char* key, bool def) {
    char buf[64] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath.c_str());
    if (!buf[0]) return def;
    return (_stricmp(buf, "true") == 0 || _stricmp(buf, "1") == 0 ||
        _stricmp(buf, "yes") == 0 || _stricmp(buf, "on") == 0);
}

static bool GetFileModTime(ULONGLONG* outTime) {
    WIN32_FILE_ATTRIBUTE_DATA fi;
    if (!GetFileAttributesExA(g_iniPath.c_str(), GetFileExInfoStandard, &fi)) return false;
    ULARGE_INTEGER uli;
    uli.LowPart = fi.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fi.ftLastWriteTime.dwHighDateTime;
    *outTime = uli.QuadPart;
    return true;
}

// ============================================================
//  PATTERN SCANNING
// ============================================================
//static uint8_t* PatternScan(const uint8_t* pattern, const char* mask, size_t len) {
//    MODULEINFO mi{};
//    GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &mi, sizeof(mi));
//
//    uint8_t* base = reinterpret_cast<uint8_t*>(mi.lpBaseOfDll);
//    size_t size = mi.SizeOfImage;
//
//    for (size_t i = 0; i < size - len; i++) {
//        bool found = true;
//        for (size_t j = 0; j < len; j++) {
//            if (mask[j] == 'x' && base[i + j] != pattern[j]) {
//                found = false;
//                break;
//            }
//        }
//        if (found) return base + i;
//    }
//    return nullptr;
//}

// ============================================================
//  Pattern scan definitions and address candidates
// ============================================================
enum class ResolveMode : std::uint8_t
{
    Direct,
    RipRelative
};

struct AddressCandidate
{
    const char* name;
    const char* pattern;
    ResolveMode mode = ResolveMode::Direct;
    std::ptrdiff_t disp_offset = 0;
    std::ptrdiff_t instr_end_offset = 0;
};

constexpr AddressCandidate kAxiomRangeCandidate[] = {
    {
        "AxiomRange_C1",
        "00 68 9B CE 6D 00 00 00 00 00 00",
        ResolveMode::Direct, 0x11, 0
    },
    {
        "AxiomRange_C2",
        "00 00 FA 43 00 00 A0 ?? ?? ?? ?? ??",
        ResolveMode::Direct, 0, 0
    },
    {
        "AxiomRange_C3",
        "C5 7A 5C ?? 24 ?? C5 FA 10 ?? ?? C5 ?? 59 ?? C5 7A 10 ?? ?? C4 ?? ?? 59 ?? C5 ?? 58 ??",
        ResolveMode::RipRelative, 215, 4
    },
};
constexpr AddressCandidate kAxiomPullSpeedCandidate[] = {
    {
        "AxiomPullSpeed_C1",
        "00 80 71 0F 2B 00 ?? ?? ?? ?? 00",
        ResolveMode::Direct, 0x11, 0
    },
    {
        "AxiomPullSpeed_C3",
        "71 0F 2B 00 00 00 00 00 00 00 00 00 00 00 00 00 00", //?
        ResolveMode::Direct, 0xF, 0
    }
};
constexpr AddressCandidate kAxiomStraightPullCandidate[] = {
    {
        "AxiomStraightPull_C1",
        "48 8B ?? B2 ?? FF ?? ?? C5 48 C6 D6 55 C5",
        ResolveMode::Direct, 0x24C, 0
    },
    {
        "AxiomStraightPull_C2",
        "C5 F8 28 CF 48 8B CF E8 ?? ?? ?? ?? 4C 89 74 24 ?? 4C 89 7C 24 ?? 4C 89 64 24 ?? 4C 8D 4F ?? 4C 8D 87 ?? ?? ?? ?? 48 8B D7 C5 F8 28 C6 E8",
        ResolveMode::Direct, 0x2D, 0
    }
};
constexpr AddressCandidate kAxiomReelDurationCandidate[] = {
    {
        "AxiomReelDuration_C1",
        "C5 EA 58 D9 C5 FA 10 35 ?? ?? ?? ?? C5 F8 2F",
        ResolveMode::Direct, 16, 0
    },
    {
        "AxiomReelDuration_C2",
        "89 AC 3E ?? ?? 00 00 C5 F8 77 4C 8D 9C 24",
        ResolveMode::Direct, -0x4E, 0
    },
    {
        "AxiomReelDuration_C3",
        "C5 F8 57 C0 C5 FA 2A C0 C5 FA 59 D6 C5 E0 C6 CB 55 C5 EA 58 D9 C5 FA 10 35 ?? ?? ?? ?? C5 F8 2F F7",
        ResolveMode::Direct, 33, 0
    },
};
constexpr AddressCandidate kAxiomPropellDurationCandidate[] = {
    {
        "AxiomPropellDuration_C1",
        "C5 F8 2F FE 76 ?? C5 FA 5E",
        ResolveMode::Direct, 4, 0
    },
    {
        "AxiomPropellDuration_C2",
        "C5 C8 57 F6 C5 F8 2F FE 76 ?? C5 FA",
        ResolveMode::Direct, 8, 0
    },
    {
        "AxiomPropellDuration_C3",
        "C5 DA 59 C4 C5 E2 58 C0 C5 F0 57 C9 C5 F8 2E C8 77 06 C5 FA 51 C0 EB 05 ?? ?? ?? ?? ?? C5 C8 57 F6 C5 F8 2F FE",
        ResolveMode::Direct, 37, 0
    },
};

struct MemoryGuard {
    void* addr;
    size_t size;
    DWORD oldProtect;
    bool success;

    MemoryGuard(void* a, size_t s) : addr(a), size(s) {
        success = VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    }
    ~MemoryGuard() {
        if (success) VirtualProtect(addr, size, oldProtect, &oldProtect);
    }
};

static uintptr_t find_pattern(const std::byte* base, std::size_t size, const char* pattern) {
    auto patternToByte = [](const char* pattern) {
        auto bytes = std::vector<int>{};
        auto start = const_cast<char*>(pattern);
        auto end = const_cast<char*>(pattern) + strlen(pattern);
        for (auto current = start; current < end; ++current) {
            if (*current == '?') {
                ++current;
                if (*current == '?') ++current;
                bytes.push_back(-1);
            }
            else {
                bytes.push_back(strtoul(current, &current, 16));
            }
        }
        return bytes;
        };

    auto regionStart = reinterpret_cast<const std::uint8_t*>(base);
    auto regionSize = size;
    auto patternBytes = patternToByte(pattern);
    auto s = patternBytes.size();
    auto d = patternBytes.data();

    for (auto i = 0ul; i < regionSize - s; ++i) {
        bool found = true;
        for (auto j = 0ul; j < s; ++j) {
            if (regionStart[i + j] != d[j] && d[j] != -1) {
                found = false;
                break;
            }
        }
        if (found) return (uintptr_t)&regionStart[i];
    }
    return 0;
}

static uintptr_t resolve_addresses(const AddressCandidate* candidates, size_t csize, const std::byte* base, std::size_t size) {
    for (size_t i = 0; i < csize; ++i)
    {
        const auto& c = candidates[i];
        const uintptr_t hit = find_pattern(base, size, c.pattern);
        if (hit) {
            return hit + c.disp_offset;
        }
        else {
            std::stringstream ss;
            ss << "WARNING: Failed to resolve address for candidate " << c.name;
            Log(ss.str());
        }
    }
    return 0;
}

// ============================================================
//  ADDRESS DISCOVERY
// ============================================================
    static bool resolve_axiom_range(std::byte* startAddress, DWORD regionSize) {
        const auto axiomRangeAddress = resolve_addresses(kAxiomRangeCandidate, std::size(kAxiomRangeCandidate), startAddress, regionSize);
        if (!axiomRangeAddress) {
            Log("ERROR: AxiomRange address not resolved");
        }
        else {
            MemoryGuard dataGuard((LPVOID)axiomRangeAddress, sizeof(float));
            if (dataGuard.success) {
                float* axiomRangePtr = reinterpret_cast<float*>(axiomRangeAddress);
                std::stringstream ss;
                ss << "AxiomRange address resolved: 0x" << std::hex << axiomRangeAddress << " (Value: " << *axiomRangePtr << ")";
                Log(ss.str());
                if (*axiomRangePtr > 100000.0f || *axiomRangePtr < -100000.0f || std::isnan(*axiomRangePtr)) {
                    ss.clear();
                    ss << "AxiomRange safety check failed. Value: " << *axiomRangePtr;
                    Log(ss.str());
                    return false;
                }
                g_pAxiomRange = axiomRangePtr;
                Log("AxiomRange pointer set.");
                return true;
            }
            else {
                Log("Failed to set memory protection for AxiomRange address.");
            }
        }

        return false;
    }

static bool resolve_axiom_pull_speed(std::byte* startAddress, DWORD regionSize) {
    const auto pullSpeedAddress = resolve_addresses(kAxiomPullSpeedCandidate, std::size(kAxiomPullSpeedCandidate), startAddress, regionSize);
    if (!pullSpeedAddress) {
        Log("AxiomPullSpeed address not resolved");
    }
    else {
        MemoryGuard guard((LPVOID)pullSpeedAddress, sizeof(float));
        if (guard.success) {
            float* axiomPullSpeedPtr = reinterpret_cast<float*>(pullSpeedAddress);
            std::stringstream ss;
            ss << "AxiomPullSpeed address resolved: 0x" << std::hex << pullSpeedAddress << " (Value: " << *axiomPullSpeedPtr << ")";
            Log(ss.str());
            if (*axiomPullSpeedPtr < -100000.0f || *axiomPullSpeedPtr > 100000.0f || std::isnan(*axiomPullSpeedPtr)) {
                ss.clear();
                ss << "AxiomPullSpeed safety check failed. Value: " << *axiomPullSpeedPtr;
                Log(ss.str());
                return false;
            }
            g_pAxiomPullSpeed = axiomPullSpeedPtr;
            Log("AxiomPullSpeed pointer set.");
            return true;
        }
        else {
            Log("Failed to set memory protection for AxiomPullSpeed.");
        }
    }
    return false;
}

static bool resolve_straight_pull(std::byte* startAddress, DWORD regionSize) {
    const auto straightPullAddress = resolve_addresses(kAxiomStraightPullCandidate, std::size(kAxiomStraightPullCandidate), startAddress, regionSize);
    if (!straightPullAddress) {
        Log("StraightPull address not resolved");
    }
    else {
        MemoryGuard guard((LPVOID)straightPullAddress, 5);
        if (guard.success && *(uint8_t*)straightPullAddress == 0xE8) {
            g_pAerialManeuver = reinterpret_cast<uint8_t*>(straightPullAddress);
            std::stringstream ss;
            ss << "StraightPull address resolved: 0x" << std::hex << straightPullAddress;
            Log(ss.str());
            memcpy(g_OriginalAerialManeuverBytes, g_pAerialManeuver, 5);
            return true;
        }
        else {
            Log("StraightPull address validation failed.");
        }
    }
    return false;
}

static bool resolve_reel_duration(std::byte* startAddress, DWORD regionSize) {
    const auto reelAddress = resolve_addresses(kAxiomReelDurationCandidate, std::size(kAxiomReelDurationCandidate), startAddress, regionSize);
    if (!reelAddress) {
        Log("ReelDuration address not resolved");
    }
    else {
        MemoryGuard gReel((LPVOID)reelAddress, 2);
        if (gReel.success) {
            std::stringstream ss;
            if (*(uint8_t*)reelAddress != 0x76 || *(uint8_t*)(reelAddress + 1) != 0x43) {
                ss.clear();
                ss << "ReelDuration pattern found but byte check failed. Expected 0x76 0x43, found 0x" << std::hex << *(uint8_t*)reelAddress << " 0x" << *(uint8_t*)(reelAddress + 1) << ". Address may be incorrect.";
                Log(ss.str());
            }
            g_pReelDuration = reinterpret_cast<uint8_t*>(reelAddress);
            ss.clear();
            ss << "ReelDuration address resolved: 0x" << std::hex << reelAddress;
            Log(ss.str());
            return true;
        }
        else {
            Log("ReelDuration address validation failed! Check skipped.");
        }
    }
    return false;
}

static bool resolve_propell_duration(std::byte* startAddress, DWORD regionSize) {
    const auto propellAddress = resolve_addresses(kAxiomPropellDurationCandidate, std::size(kAxiomPropellDurationCandidate), startAddress, regionSize);
    if (!propellAddress) {
        Log("PropellDuration address not resolved");
    }
    else {
        MemoryGuard gProp((LPVOID)propellAddress, 2);
        if (gProp.success) {
            if (*(uint8_t*)propellAddress != 0x76 || *(uint8_t*)(propellAddress + 1) != 0x06) {
                std::stringstream ss;
                ss << "PropellDuration pattern found but byte check failed. Expected 0x76 0x06, found 0x" << std::hex << *(uint8_t*)propellAddress << " 0x" << *(uint8_t*)(propellAddress + 1) << ". Address may be incorrect.";
                Log(ss.str());
            }
            g_pPropellDuration = reinterpret_cast<uint8_t*>(propellAddress);
            std::stringstream ss;
            ss << "PropellDuration address resolved: 0x" << std::hex << propellAddress;
            Log(ss.str());
            return true;
        }
        else {
            Log("PropellDuration address validation failed! Check skipped.");
        }
    }
    return false;
}

static void DiscoverAddresses() {
    Log("Initializing mod features...");
    MODULEINFO mi = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &mi, sizeof(mi));
    auto regionSize = mi.SizeOfImage;
    auto baseAddress = reinterpret_cast<std::byte*>(mi.lpBaseOfDll);
    auto range = resolve_axiom_range(baseAddress, regionSize);
    auto pullSpeed = resolve_axiom_pull_speed(baseAddress, regionSize);
    auto straightPull = resolve_straight_pull(baseAddress, regionSize);
    auto reelDuration = resolve_reel_duration(baseAddress, regionSize);
    auto propellDuration = resolve_propell_duration(baseAddress, regionSize);
    if (range) Log("AxiomRange: Ready");
    if (pullSpeed) Log("AxiomPullSpeed: Ready");
    if (straightPull) Log("StraightPull: Ready");
    if (reelDuration) Log("ReelDuration: Ready");
    if (propellDuration) Log("PropellDuration: Ready");
    Log("All features initialized successfully");
}

// ============================================================
//  CORE: APPLY PATCHES
// ============================================================

// Apply straight pull trajectory modification
static bool ApplyStraightPull(bool enabled) {
    static bool s_isPatched = false;
    const size_t patchSize = 5;
    uint8_t nops[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    if (!g_pAerialManeuver) {
        Log("StraightPull feature not available");
        return false;
    }
    bool isCurrentlyNoped = (memcmp(g_pAerialManeuver, nops, patchSize) == 0);
    bool isCurrentlyOriginal = (memcmp(g_pAerialManeuver, g_OriginalAerialManeuverBytes, patchSize) == 0);

    if (enabled) {
        if (isCurrentlyNoped) {
            Log("StraightPull already patched");
            return true;
        }

        if (!isCurrentlyOriginal) {
            Log("StraightPull memory does not match expected bytes. Aborting to prevent crash.");
            return false;
        }

        MemoryGuard guard(g_pAerialManeuver, patchSize);
        if (!guard.success) {
            Log("StraightPull memory protection failed");
            return false;
        }

        memcpy(g_pAerialManeuver, nops, patchSize);
        s_isPatched = true;
        Log("StraightPull ENABLED");
    }
    // 3. Handle the "Disable" request
    else {
        if (isCurrentlyOriginal) {
            s_isPatched = false;
            Log("StraightPull already disabled");
            return true;
        }

        if (isCurrentlyNoped && !s_isPatched) {
            Log("StraightPull patch detected from external mod. Skipping restore to avoid conflict.");
            return true;
        }

        if (isCurrentlyNoped && s_isPatched) {
            MemoryGuard guard(g_pAerialManeuver, patchSize);
            if (!guard.success) return false;

            memcpy(g_pAerialManeuver, g_OriginalAerialManeuverBytes, patchSize);
            s_isPatched = false;
            Log("StraightPull DISABLED");
        }
    }

    return true;
}

// Apply instant axiom force activation
// 1.04.01: patches jbe->jmp at two locations to skip duration checks
static bool ApplyInstantAxiom(bool enabled) {
    static bool s_isPatched = false;

    if (enabled && s_isPatched)   return true;
    if (!enabled && !s_isPatched) return true;

    if (!g_pReelDuration || !g_pPropellDuration) {
        Log("ERROR: InstantAxiom feature not available");
        return false;
    }

    // Reel:   jbe +0x43 -> jmp +0x43
    uint8_t reelOrig[2] = { 0x76, 0x43 };
    uint8_t reelPatch[2] = { 0xEB, 0x43 };
    // Propell: jbe +0x06 -> jmp +0x06
    uint8_t propOrig[2] = { 0x76, 0x06 };
    uint8_t propPatch[2] = { 0xEB, 0x06 };

    DWORD oldProtect = 0;

    if (enabled) {
        if (memcmp(g_pReelDuration, reelPatch, 2) == 0) {
            s_isPatched = true;
            Log("InstantAxiom already active");
            return true;
        }
        if (memcmp(g_pReelDuration, reelOrig, 2) != 0 || memcmp(g_pPropellDuration, propOrig, 2) != 0) {
            Log("WARNING: InstantAxiom feature unavailable");
            return false;
        }
        MemoryGuard g1(g_pReelDuration, 2);
        memcpy(g_pReelDuration, reelPatch, 2);
        MemoryGuard g2(g_pPropellDuration, 2);
        memcpy(g_pPropellDuration, propPatch, 2);
        s_isPatched = true;
        Log("InstantAxiom ENABLED");
    }
    else {
        if (memcmp(g_pReelDuration, reelOrig, 2) == 0) {
            s_isPatched = false;
            Log("InstantAxiom already disabled");
            return true;
        }
        if (memcmp(g_pReelDuration, reelPatch, 2) != 0) {
            Log("WARNING: InstantAxiom state uncertain - reset to default");
            s_isPatched = false;
            return true;
        }
        MemoryGuard g1(g_pReelDuration, 2);
        memcpy(g_pReelDuration, reelOrig, 2);
        MemoryGuard g2(g_pPropellDuration, 2);
        memcpy(g_pPropellDuration, propOrig, 2);
        s_isPatched = false;
        Log("InstantAxiom DISABLED - Restored to default");
    }
    return true;
}

// ============================================================
//  CORE: APPLY RANGE
// ============================================================
static bool ApplyAxiomRange(float range) {
    if (!g_pAxiomRange) {
        Log("ERROR: Range feature not available");
        return false;
    }

    MemoryGuard guard(g_pAxiomRange, sizeof(float));
    if (!guard.success) {
        Log("ERROR: Memory protection failed");
        return false;
    }
    float currentValue = *g_pAxiomRange;
    if (currentValue > 10000.0f || currentValue < -10000.0f || std::isnan(currentValue)) {
        std::stringstream ss;
        ss << "ERROR: Current AxiomRange value is out of expected bounds. Value: " << currentValue;
        Log(ss.str());
        return false;
    }
    *g_pAxiomRange = range;
    Log("Range applied");
    return true;
}

static bool ApplyAxiomPullSpeed(float value) {
    if (!g_pAxiomPullSpeed) {
        Log("ERROR: PullSpeed feature not available");
        return false;
    }

    MemoryGuard guard(g_pAxiomPullSpeed, sizeof(float));
    if (!guard.success) {
        Log("ERROR: Memory protection failed");
        return false;
    }
    float currentValue = *g_pAxiomPullSpeed;
    if (currentValue > 10000.0f || currentValue < -10000.0f || std::isnan(currentValue)) {
        std::stringstream ss;
        ss << "ERROR: Current AxiomPullSpeed value is out of expected bounds. Value: " << currentValue;
        Log(ss.str());
        return false;
    }
    *g_pAxiomPullSpeed = value;

    Log("PullSpeed applied");
    return true;
}

// ============================================================
//  CONFIG LOAD + APPLY
// ============================================================
static void LoadAndApply() {
    bool  enabled = ReadIniBool(kIniSection, "Enabled", true);
    float range = ReadIniFloat(kIniSection, "Range", kFallbackRange);
    float pullSpeed = ReadIniFloat(kIniSection, "PullSpeed", kFallbackPullSpeed);
    bool  straightPull = ReadIniBool(kIniSection, "StraightPull", true);
    bool  instantAxiom = ReadIniBool(kIniSection, "InstantAxiom", false);

    char buf[256];
    sprintf_s(buf, "Config loaded: Enabled=%s StraightPull=%s InstantAxiom=%s",
        enabled ? "true" : "false",
        straightPull ? "true" : "false", instantAxiom ? "true" : "false");
    Log(buf);

    if (!enabled) {
        ApplyAxiomRange(kDefaultRange);
        ApplyAxiomPullSpeed(kDefaultPullSpeed);
        ApplyStraightPull(false);
        ApplyInstantAxiom(false);
        Log("Mod disabled - defaults restored");
        return;
    }

    // Apply all enhancements
    ApplyAxiomRange(range);
    ApplyAxiomPullSpeed(pullSpeed);
    ApplyStraightPull(straightPull);
    ApplyInstantAxiom(instantAxiom);
}

// ============================================================
//  MAIN MOD THREAD
// ============================================================
static void ModMain() {
    //Fix error make plugin loading 2 times in some cases, causing double logging with 1 instance failed.
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\CD_SuperAxiomForce");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return;
    }

    Log("Starting up...");

    // Wait for the game to finish loading before touching memory.
    Sleep(15000);

    // Resolve INI path: same directory as the .asi
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = std::string(exePath);
    dir = dir.substr(0, dir.rfind('\\'));
    g_iniPath = dir + "\\" + kModName + ".ini";

    // Discover addresses via pattern scanning
    DiscoverAddresses();

    // Initial apply
    LoadAndApply();
    GetFileModTime(&g_lastIniModTime);

    // Live-reload loop: re-apply whenever the INI changes
    while (true) {
        Sleep(1000);

        ULONGLONG currentModTime = 0;
        if (GetFileModTime(&currentModTime) && currentModTime != g_lastIniModTime) {
            Sleep(100); // small settle delay
            g_lastIniModTime = currentModTime;
            Log("INI changed - reloading.");
            LoadAndApply();
        }
    }
}

// ============================================================
//  DLL ENTRY POINT
// ============================================================
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        bool expected = false;
        if (!g_isInitialized.compare_exchange_strong(expected, true))
            return TRUE; // already initialised

        std::thread(ModMain).detach();
    }
    return TRUE;
}
