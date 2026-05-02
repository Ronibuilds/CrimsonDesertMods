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

#pragma comment(lib, "psapi.lib")

// ============================================================
//  CONSTANTS
// ============================================================
static const char* kModName = "SuperAxiomForce";
static const char* kLogFile = "SuperAxiomForce.log";
static const char* kIniSection = "AxiomForce";

// Fallback offsets (used if pattern scanning fails)
static const uintptr_t kAxiomRangeOffset_Fallback = 0x5E05128;  // confirmed latest
static const uintptr_t kAxiomPullSpeedOffset_Fallback = 0x5E05C68;  // confirmed latest
static const uintptr_t kAerialManeuverOffset_Fallback = 0x0EA5DEE;  // confirmed 1.04.01
static const uintptr_t kReelDurationOffset_Fallback = 0x420705;   // confirmed 1.04.01
static const uintptr_t kPropellDurationOffset_Fallback = 0x4209A0;   // confirmed 1.04.01

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

// ============================================================
//  LOGGING
// ============================================================
static void Log(const char* msg) {
    std::ofstream f(kLogFile, std::ios::app);
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
static uint8_t* PatternScan(const uint8_t* pattern, const char* mask, size_t len) {
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &mi, sizeof(mi));

    uint8_t* base = reinterpret_cast<uint8_t*>(mi.lpBaseOfDll);
    size_t size = mi.SizeOfImage;

    for (size_t i = 0; i < size - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] == 'x' && base[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return nullptr;
}

// ============================================================
//  ADDRESS DISCOVERY
// ============================================================
static void DiscoverAddresses() {
    HMODULE hMod = GetModuleHandleA(nullptr);
    uintptr_t base = reinterpret_cast<uintptr_t>(hMod);

    Log("Initializing mod features...");

    // Pattern 1: Straight pull feature (updated bytes for 1.04.01)
    {
        static const uint8_t pattern[] = { 0xE8, 0x4D, 0xD0, 0xFF, 0xFF };
        static const char mask[] = "xxxxx";

        uint8_t* loc = PatternScan(pattern, mask, sizeof(pattern));
        if (loc) {
            g_pAerialManeuver = loc;
            Log("StraightPull: Ready");
        }
        else {
            g_pAerialManeuver = reinterpret_cast<uint8_t*>(base + kAerialManeuverOffset_Fallback);
            Log("StraightPull: Ready");
        }
    }

    // InstantAxiom - 1.04.01 uses jbe->jmp patch at new offsets (movss approach no longer valid)
    g_pReelDuration = reinterpret_cast<uint8_t*>(base + kReelDurationOffset_Fallback);
    g_pPropellDuration = reinterpret_cast<uint8_t*>(base + kPropellDurationOffset_Fallback);
    Log("InstantAxiom: Ready");

    // Range
    g_pAxiomRange = reinterpret_cast<float*>(base + kAxiomRangeOffset_Fallback);

    // PullSpeed
    g_pAxiomPullSpeed = reinterpret_cast<float*>(base + kAxiomPullSpeedOffset_Fallback);

    Log("All features initialized successfully");
}

// ============================================================
//  CORE: APPLY PATCHES
// ============================================================

// Apply straight pull trajectory modification
static bool ApplyStraightPull(bool enabled) {
    static bool s_isPatched = false;

    // Skip if already in desired state
    if (enabled && s_isPatched) {
        return true;
    }
    if (!enabled && !s_isPatched) {
        return true;
    }

    if (!g_pAerialManeuver) {
        Log("ERROR: StraightPull feature not available");
        return false;
    }

    uint8_t* pCall = g_pAerialManeuver;

    // Expected original bytes: E8 4D D0 FF FF (call instruction, updated 1.04.01)
    uint8_t expectedBytes[5] = { 0xe8, 0x4d, 0xd0, 0xff, 0xff };
    // NOPs to disable the call
    uint8_t nops[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };

    DWORD oldProtect = 0;
    if (!VirtualProtect(pCall, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("ERROR: StraightPull memory protection failed");
        return false;
    }

    if (enabled) {
        // Check if bytes match expected pattern (safety check)
        bool isOriginal = (memcmp(pCall, expectedBytes, 5) == 0);
        bool isAlreadyPatched = (memcmp(pCall, nops, 5) == 0);

        if (isOriginal) {
            memcpy(pCall, nops, 5);
            s_isPatched = true;
            Log("StraightPull ENABLED");
        }
        else if (isAlreadyPatched) {
            // Bytes are already NOP'd by another mod (e.g. ImprovedAxiom).
            // Do NOT claim ownership — if StraightPull is later disabled we must not
            // restore bytes we didn't write, or we'd undo the other mod's patch.
            // s_isPatched stays false intentionally.
            Log("StraightPull already active");
        }
        else {
            Log("WARNING: StraightPull feature unavailable");
            VirtualProtect(pCall, 5, oldProtect, &oldProtect);
            return false;
        }
    }
    else {
        // Disable: Restore original call instruction
        bool currentlyPatched = (memcmp(pCall, nops, 5) == 0);
        bool currentlyOriginal = (memcmp(pCall, expectedBytes, 5) == 0);

        if (currentlyPatched) {
            memcpy(pCall, expectedBytes, 5);
            s_isPatched = false;
            Log("StraightPull DISABLED - Restored to default");
        }
        else if (currentlyOriginal) {
            s_isPatched = false;
            Log("StraightPull already disabled");
        }
        else {
            Log("WARNING: StraightPull state uncertain - reset to default");
            s_isPatched = false;
        }
    }

    VirtualProtect(pCall, 5, oldProtect, &oldProtect);
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
        VirtualProtect(g_pReelDuration, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_pReelDuration, reelPatch, 2);
        VirtualProtect(g_pReelDuration, 2, oldProtect, &oldProtect);
        VirtualProtect(g_pPropellDuration, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_pPropellDuration, propPatch, 2);
        VirtualProtect(g_pPropellDuration, 2, oldProtect, &oldProtect);
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
        VirtualProtect(g_pReelDuration, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_pReelDuration, reelOrig, 2);
        VirtualProtect(g_pReelDuration, 2, oldProtect, &oldProtect);
        VirtualProtect(g_pPropellDuration, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_pPropellDuration, propOrig, 2);
        VirtualProtect(g_pPropellDuration, 2, oldProtect, &oldProtect);
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

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_pAxiomRange, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("ERROR: Memory protection failed");
        return false;
    }

    *g_pAxiomRange = range;

    VirtualProtect(g_pAxiomRange, sizeof(float), oldProtect, &oldProtect);

    Log("Range applied");
    return true;
}

static bool ApplyAxiomPullSpeed(float value) {
    if (!g_pAxiomPullSpeed) {
        Log("ERROR: PullSpeed feature not available");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_pAxiomPullSpeed, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("ERROR: Memory protection failed");
        return false;
    }

    *g_pAxiomPullSpeed = value;

    VirtualProtect(g_pAxiomPullSpeed, sizeof(float), oldProtect, &oldProtect);

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