// ForcePalmBoostByBambozu v2 - forcepalm_v2.cpp
// Enhanced force palm jump boost (R3 / Right Joystick Press)
// Based on the original working version with DualSense support added
//
// CONTROLLER SUPPORT:
// - Xbox controllers (XInput) - plug and play
// - PS5 DualSense (native HID) - works via USB or Bluetooth, no extra software needed!
//
// INSTALL: drop .asi + .ini into Crimson Desert\bin64\
// Loader: CDUMM can load ASI; Ultimate ASI Loader works too

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <thread>
#include <fstream>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <psapi.h>
#include <Xinput.h>
#include <atomic>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <timeapi.h>
}
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Xinput.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winmm.lib")

// Shared memory for cross-mod compatibility
#include "shared_player_base.h"
static SharedPlayerBase g_sharedPlayerBase;

// Named mutex: guards against two DLL images loaded from different paths.
static HANDLE s_instanceMutex = NULL;

// SafetyHook for proper hook chaining (compatibility with other mods)
#include "safetyhook.hpp"
static safetyhook::MidHook g_posHook;

template <typename T>
static inline T Clamp(T value, T lo, T hi) {
    return (value < lo) ? lo : (value > hi) ? hi : value;
}

// ============================================================
//  OFFSETS
// ============================================================
constexpr auto VEL_Z_OFFSET = 0x1B4;

// ============================================================
//  XBOX BUTTON CONSTANTS
// ============================================================
constexpr auto BTN_R3 = XINPUT_GAMEPAD_RIGHT_THUMB;   // 128 (0x0080)

// ============================================================
//  PS5 DUALSENSE CONSTANTS & STRUCTURES
// ============================================================
constexpr auto DUALSENSE_VID = 0x054C;
constexpr auto DUALSENSE_PID = 0x0CE6;  // DualSense (original)
constexpr auto DUALSENSE_EDGE_PID = 0x0DF2;  // DualSense Edge

// PS5 Button bit masks
constexpr auto DS_BTN_SQUARE = 0x0010;
constexpr auto DS_BTN_CROSS = 0x0020;
constexpr auto DS_BTN_CIRCLE = 0x0040;
constexpr auto DS_BTN_TRIANGLE = 0x0080;
constexpr auto DS_BTN_L1 = 0x0100;
constexpr auto DS_BTN_R1 = 0x0200;
constexpr auto DS_BTN_L2 = 0x0400;
constexpr auto DS_BTN_R2 = 0x0800;
constexpr auto DS_BTN_CREATE = 0x1000;
constexpr auto DS_BTN_OPTIONS = 0x2000;
constexpr auto DS_BTN_L3 = 0x4000;
constexpr auto DS_BTN_R3 = 0x8000;
constexpr auto DS_BTN_PS = 0x10000;

#pragma pack(push, 1)
struct DualSenseInputUSB {
    uint8_t reportId;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    uint8_t sequence;
    uint8_t buttons[4];
    uint8_t reserved;
};

struct DualSenseInputBT {
    uint8_t reportId;
    uint8_t unk1;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    uint8_t sequence;
    uint8_t buttons[4];
    uint8_t reserved;
};
#pragma pack(pop)

struct DualSenseState {
    HANDLE deviceHandle;
    bool isConnected;
    bool isBluetooth;
    uint32_t buttons;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
};

static DualSenseState g_dualSense = { INVALID_HANDLE_VALUE, false, false, 0, 0, 0, 128, 128, 128, 128 };

// ============================================================
//  FLIGHT STATE DETECTION TUNING
// ============================================================
static float g_flightVelThreshold = 3.0f;
static float g_groundedVelEpsilon = 0.05f;
static float g_airborneVelThreshold = 0.25f;
static int   g_airborneGraceMs = 300;
static int   g_groundedTimeThreshold = 100;
static int   g_flightTimeThreshold = 250;
static int   g_flightConfirmMs = 120;

// ============================================================
//  CONFIG (loaded from ForcePalmBoostByBambozu.ini)
// ============================================================
static const char* kModBaseName = "ForcePalmBoostByBambozu";
static const char* kLogFileName = "ForcePalmBoostByBambozu.log";

static bool  g_forcePalmBoostEnabled = true;
static float g_forcePalmBoostValue = 28.0f;
static int   g_forcePalmBoostWindowMs = 180;
static int   g_forcePalmButton = XINPUT_GAMEPAD_X;  // 16384 (X on Xbox / Square on PS5)
static int   g_forcePalmKey = 0x20;              // Spacebar

// ============================================================
//  SHARED STATE
// ============================================================
static uintptr_t          g_playerBase = 0;
static std::atomic<bool>  g_forcePalmActive{ false };
static ULONGLONG          g_forcePalmEndTime = 0;
static std::string        g_iniPath;
static ULONGLONG          g_lastIniModTime = 0;
static float              g_boostBaselineVel = 0.0f;

// Flight state detection
static ULONGLONG          g_lastAirborneTime = 0;
static std::atomic<bool>  g_isInFlight{ false };
static bool               g_wasAirborne = false;
static ULONGLONG          g_groundedSinceMs = 0;
static ULONGLONG          g_lastAirborneMotionMs = 0;
static ULONGLONG          g_flightCandidateSinceMs = 0;

// ============================================================
//  LOGGING
// ============================================================
static void Log(const std::string& msg) {
    std::ofstream f(kLogFileName, std::ios::app);
    f << msg << "\n";
}

// ============================================================
//  DUALSENSE HID FUNCTIONS
// ============================================================
static const struct { uint32_t ds; WORD xi; } kBtnMap[] = {
    { DS_BTN_CROSS,    XINPUT_GAMEPAD_A },
    { DS_BTN_CIRCLE,   XINPUT_GAMEPAD_B },
    { DS_BTN_SQUARE,   XINPUT_GAMEPAD_X },
    { DS_BTN_TRIANGLE, XINPUT_GAMEPAD_Y },
    { DS_BTN_R1,       XINPUT_GAMEPAD_RIGHT_SHOULDER },
    { DS_BTN_L1,       XINPUT_GAMEPAD_LEFT_SHOULDER },
    { DS_BTN_OPTIONS,  XINPUT_GAMEPAD_START },
    { DS_BTN_CREATE,   XINPUT_GAMEPAD_BACK },
    { DS_BTN_L3,       XINPUT_GAMEPAD_LEFT_THUMB },
    { DS_BTN_R3,       XINPUT_GAMEPAD_RIGHT_THUMB },
};

static bool DualSense_Initialize() {
    if (g_dualSense.deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dualSense.deviceHandle);
        g_dualSense.deviceHandle = INVALID_HANDLE_VALUE;
        g_dualSense.isConnected = false;
    }

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData{};
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

        auto* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(requiredSize);
        if (!detailData) continue;

        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, detailData, requiredSize, NULL, NULL)) {
            HANDLE hDevice = CreateFileA(
                detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

            if (hDevice != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrib{};
                attrib.Size = sizeof(HIDD_ATTRIBUTES);

                if (HidD_GetAttributes(hDevice, &attrib)) {
                    if (attrib.VendorID == DUALSENSE_VID &&
                        (attrib.ProductID == DUALSENSE_PID || attrib.ProductID == DUALSENSE_EDGE_PID)) {

                        PHIDP_PREPARSED_DATA preparsedData;
                        if (HidD_GetPreparsedData(hDevice, &preparsedData)) {
                            HIDP_CAPS caps;
                            if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
                                g_dualSense.isBluetooth = (caps.InputReportByteLength == 78);
                            }
                            HidD_FreePreparsedData(preparsedData);
                        }

                        g_dualSense.deviceHandle = hDevice;
                        g_dualSense.isConnected = true;
                        free(detailData);
                        SetupDiDestroyDeviceInfoList(deviceInfoSet);

                        Log(std::string(kModBaseName) + ": DualSense controller detected (" +
                            (g_dualSense.isBluetooth ? "Bluetooth" : "USB") + ")");
                        return true;
                    }
                }
                CloseHandle(hDevice);
            }
        }
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return false;
}

static uint32_t DualSense_ParseButtons(const uint8_t* buttonData) {
    uint32_t raw = buttonData[0] | (buttonData[1] << 8) | (buttonData[2] << 16);
    // Bits already align to DS_BTN_* masks — return directly
    return raw & (DS_BTN_SQUARE | DS_BTN_CROSS | DS_BTN_CIRCLE | DS_BTN_TRIANGLE | DS_BTN_L1 | DS_BTN_R1 | DS_BTN_L2 | DS_BTN_R2 | DS_BTN_CREATE | DS_BTN_OPTIONS | DS_BTN_L3 | DS_BTN_R3 | DS_BTN_PS);

}

static bool DualSense_ReadInput() {
    if (g_dualSense.deviceHandle == INVALID_HANDLE_VALUE) return false;

    uint8_t buffer[128]{};
    DWORD bytesRead = 0;

    if (!ReadFile(g_dualSense.deviceHandle, buffer, sizeof(buffer), &bytesRead, NULL)) {
        return false;
    }

    if (bytesRead < 10) return false;

    if (!g_dualSense.isBluetooth && buffer[0] == 0x01) {
        auto* report = (DualSenseInputUSB*)buffer;
        g_dualSense.leftStickX = report->leftStickX;
        g_dualSense.leftStickY = report->leftStickY;
        g_dualSense.rightStickX = report->rightStickX;
        g_dualSense.rightStickY = report->rightStickY;
        g_dualSense.leftTrigger = report->leftTrigger;
        g_dualSense.rightTrigger = report->rightTrigger;
        g_dualSense.buttons = DualSense_ParseButtons(report->buttons);
        return true;
    }
    else if (g_dualSense.isBluetooth && buffer[0] == 0x31) {
        auto* report = (DualSenseInputBT*)buffer;
        g_dualSense.leftStickX = report->leftStickX;
        g_dualSense.leftStickY = report->leftStickY;
        g_dualSense.rightStickX = report->rightStickX;
        g_dualSense.rightStickY = report->rightStickY;
        g_dualSense.leftTrigger = report->leftTrigger;
        g_dualSense.rightTrigger = report->rightTrigger;
        g_dualSense.buttons = DualSense_ParseButtons(report->buttons);
        return true;
    }

    return false;
}

static void DualSense_Update() {
    if (!g_dualSense.isConnected) {
        static ULONGLONG lastReconnectAttempt = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastReconnectAttempt > 2000) {
            lastReconnectAttempt = now;
            DualSense_Initialize();
        }
    }

    if (g_dualSense.isConnected) {
        if (!DualSense_ReadInput()) {
            g_dualSense.isConnected = false;
            if (g_dualSense.deviceHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(g_dualSense.deviceHandle);
                g_dualSense.deviceHandle = INVALID_HANDLE_VALUE;
            }
        }
    }
}

// ============================================================
//  INI READER
// ============================================================
static float ReadIniFloat(const char* file, const char* section, const char* key, float def) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), file);
    return buf[0] ? (float)atof(buf) : def;
}
static int ReadIniInt(const char* file, const char* section, const char* key, int def) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), file);
    if (!buf[0]) return def;
    char* end = nullptr;
    long value = strtol(buf, &end, 0);
    if (end == buf) return def;
    return (int)value;
}
static bool ReadIniBool(const char* file, const char* section, const char* key, bool def) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), file);
    if (!buf[0]) return def;
    if (_stricmp(buf, "true") == 0 || _stricmp(buf, "1") == 0 ||
        _stricmp(buf, "yes") == 0 || _stricmp(buf, "on") == 0) return true;
    if (_stricmp(buf, "false") == 0 || _stricmp(buf, "0") == 0 ||
        _stricmp(buf, "no") == 0 || _stricmp(buf, "off") == 0) return false;
    return def;
}

static bool GetFileModTime(const std::string& path, ULONGLONG* outTime) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) return false;
    ULARGE_INTEGER uli{};
    uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
    *outTime = uli.QuadPart;
    return true;
}

static void ReloadIniValues() {
    g_forcePalmBoostEnabled = ReadIniBool(g_iniPath.c_str(), "ForcePalm", "Enabled", true);
    g_forcePalmBoostValue = ReadIniFloat(g_iniPath.c_str(), "ForcePalm", "BoostValue", 28.0f);
    g_forcePalmBoostWindowMs = ReadIniInt(g_iniPath.c_str(), "ForcePalm", "WindowMs", 180);
    g_forcePalmButton = ReadIniInt(g_iniPath.c_str(), "ForcePalm", "Button", XINPUT_GAMEPAD_X);  // 16384
    g_forcePalmKey = ReadIniInt(g_iniPath.c_str(), "ForcePalm", "Key", 0x20);              // Spacebar

    Log(std::string(kModBaseName) + ": INI reloaded - ForcePalmBoost=" +
        (g_forcePalmBoostEnabled ? "ON" : "OFF"));
}

// ============================================================
//  AOB PATTERN SCAN
// ============================================================
static uint8_t* PatternScan(const uint8_t* pattern, const char* mask, size_t len) {
    HMODULE hMod = GetModuleHandleA(nullptr);
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi));

    auto* base = reinterpret_cast<uint8_t*>(mi.lpBaseOfDll);
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
//  FLIGHT STATE DETECTION
// ============================================================
static bool IsPlayerInFlight(ULONGLONG nowMs) {
    if (g_playerBase == 0) return false;

    float velZ = *reinterpret_cast<float*>(g_playerBase + VEL_Z_OFFSET);
    float absVelZ = std::fabs(velZ);

    if (absVelZ >= g_airborneVelThreshold) {
        g_lastAirborneMotionMs = nowMs;
        if (!g_wasAirborne) {
            g_lastAirborneTime = nowMs;
            g_wasAirborne = true;
            g_isInFlight.store(false);
            g_flightCandidateSinceMs = 0;
        }
    }

    bool airborneGrace = (g_lastAirborneMotionMs != 0) &&
        ((nowMs - g_lastAirborneMotionMs) <= (ULONGLONG)g_airborneGraceMs);
    if (!g_wasAirborne && airborneGrace) {
        g_wasAirborne = true;
        if (g_lastAirborneTime == 0) g_lastAirborneTime = nowMs;
    }

    bool groundedLike = (absVelZ <= g_groundedVelEpsilon) && !airborneGrace;
    if (groundedLike) {
        if (g_groundedSinceMs == 0) g_groundedSinceMs = nowMs;
        if ((nowMs - g_groundedSinceMs) >= (ULONGLONG)g_groundedTimeThreshold) {
            g_isInFlight.store(false);
            g_wasAirborne = false;
            g_lastAirborneTime = 0;
            g_lastAirborneMotionMs = 0;
            g_flightCandidateSinceMs = 0;
        }
        return g_isInFlight.load();
    }
    g_groundedSinceMs = 0;

    if (g_wasAirborne) {
        ULONGLONG airborneTime = (g_lastAirborneTime != 0) ? (nowMs - g_lastAirborneTime) : 0;
        if (absVelZ <= g_flightVelThreshold) {
            if (g_flightCandidateSinceMs == 0) g_flightCandidateSinceMs = nowMs;
        }
        else {
            g_flightCandidateSinceMs = 0;
        }

        ULONGLONG controlledTime = (g_flightCandidateSinceMs != 0) ?
            (nowMs - g_flightCandidateSinceMs) : 0;
        if (airborneTime >= (ULONGLONG)g_flightTimeThreshold &&
            controlledTime >= (ULONGLONG)g_flightConfirmMs) {
            g_isInFlight.store(true);
        }
    }

    if (!g_isInFlight.load() && !airborneGrace) {
        g_wasAirborne = false;
        g_lastAirborneTime = 0;
    }

    return g_isInFlight.load();
}

// ============================================================
//  POSITION DELTA HOOK — applies force palm impulse via xmm0.f32[1]
//  Direct velocity writes to entity+0x1B4 are overwritten by the physics
//  engine within the same tick. Modifying xmm0 at integration time is the
//  only reliable way to inject vertical movement (same mechanism as EnhancedFlight).
// ============================================================
static void PosHookCallback(safetyhook::Context& ctx) {
    if (ctx.rbx > 0x100000ULL)
        g_playerBase = static_cast<uintptr_t>(ctx.rbx);

    if (g_forcePalmActive.load()) {
        float currentVelY = ctx.xmm0.f32[1];

        // Define a "Soft Cap" for vertical speed to prevent physics snapping
        // If the game thinks you are going too fast vertically, it teleports you to correct it.
        const float maxSafeVerticalSpeed = 35.0f;

        // Target is absolute, not relative to current velocity
        float absoluteTarget = g_forcePalmBoostValue;

        // Only boost if we haven't reached the target yet
        if (currentVelY < absoluteTarget) {
            float smoothFactor = 0.15f;
            ctx.xmm0.f32[1] = std::min(
                currentVelY + (absoluteTarget - currentVelY) * smoothFactor,
                maxSafeVerticalSpeed
            );
        }
    }
}

static bool InstallPosHook(uint8_t* location) {
    auto result = safetyhook::MidHook::create(reinterpret_cast<void*>(location), PosHookCallback);
    if (!result) {
        Log(std::string(kModBaseName) + ": ERROR - boost hook unavailable");
        return false;
    }
    g_posHook = std::move(*result);
    Log(std::string(kModBaseName) + ": boost active");
    return true;
}

// ============================================================
//  INPUT LOOP
// ============================================================
static void InputLoop() {

    WORD     prevButtons = 0;
    uint32_t prevDSButtons = 0;
    bool     prevKeyDown = false;
    ULONGLONG lastIniPollMs = GetTickCount64();
    ULONGLONG lastPressTimeMs = 0; // triple-press tracking

    while (true) {
        Sleep(1);

        ULONGLONG nowMs = GetTickCount64();

        // Always pull player base from shared memory.
        // EnhancedFlight captures it via PosHookCallback (ctx.rbx) and publishes it here.
        // ForcePalm has no working hook of its own post-1.04.01 patch, so shared memory
        // is the only reliable source regardless of which mod loaded first.
        {
            uintptr_t sharedBase = g_sharedPlayerBase.GetPlayerBase();
            if (sharedBase != 0)
                g_playerBase = sharedBase;
        }

        // Live-reload INI every second
        if (nowMs - lastIniPollMs >= 1000) {
            lastIniPollMs = nowMs;
            ULONGLONG currentModTime = 0;
            if (GetFileModTime(g_iniPath, &currentModTime)) {
                if (currentModTime != g_lastIniModTime) {
                    Sleep(100);
                    g_lastIniModTime = currentModTime;
                    ReloadIniValues();
                }
            }
        }

        // Update DualSense
        DualSense_Update();

        // Read XInput
        XINPUT_STATE state{};
        bool hasXInput = (XInputGetState(0, &state) == ERROR_SUCCESS);
        WORD xinputButtons = hasXInput ? state.Gamepad.wButtons : 0;
        WORD xinputJustPressed = xinputButtons & ~prevButtons;

        // Read DualSense
        bool hasDualSense = g_dualSense.isConnected;
        uint32_t dsButtons = hasDualSense ? g_dualSense.buttons : 0;
        uint32_t dsJustPressed = dsButtons & ~prevDSButtons;

        // Combine (prefer DualSense if both connected)
        bool hasController = hasDualSense || hasXInput;
        WORD buttons = 0;
        WORD justPressed = 0;

        if (hasDualSense) {
            // Map DualSense buttons to XInput-compatible format so a single Button= INI value
            // works for both Xbox and PS5 (Cross->A, Circle->B, Square->X, Triangle->Y, etc.)
            for (auto& m : kBtnMap) {
                if (dsButtons & m.ds) buttons |= m.xi;
                if (dsJustPressed & m.ds) justPressed |= m.xi;
            }
        }
        else if (hasXInput) {
            buttons = xinputButtons;
            justPressed = xinputJustPressed;
        }

        bool inFlight = IsPlayerInFlight(nowMs);

        // ---- FORCE PALM BOOST (R3) logic ----
        if (g_forcePalmBoostEnabled && !inFlight) {
            bool kbDown = g_forcePalmKey != 0 && (GetAsyncKeyState(g_forcePalmKey) & 0x8000) != 0;
            bool kbPressed = kbDown && !prevKeyDown;
            bool btnPressed = hasController && (justPressed & g_forcePalmButton);

            if (btnPressed || kbPressed) {
                if (nowMs - lastPressTimeMs < 250ULL) {
                    // Capture velocity baseline at activation time
                    if (g_playerBase != 0)
                        g_boostBaselineVel = *reinterpret_cast<float*>(g_playerBase + VEL_Z_OFFSET);
                    g_forcePalmActive.store(true);
                    g_forcePalmEndTime = nowMs + (ULONGLONG)g_forcePalmBoostWindowMs;

                    Log(std::string(kModBaseName) + ": force palm boost fired!");

                    lastPressTimeMs = 0;
                }
                else {
                    lastPressTimeMs = nowMs;
                }
            }

            prevKeyDown = kbDown;
        }
        else {
            prevKeyDown = false;
        }

        // ---- BOOST WINDOW MANAGEMENT ----
        // PosHookCallback applies the actual vertical impulse via xmm0.f32[1].
        // InputLoop just manages the active window and clears the flag when done.
        if (g_forcePalmActive.load()) {
            if (nowMs >= g_forcePalmEndTime || inFlight) {
                g_forcePalmActive.store(false);
                g_forcePalmEndTime = 0;
            }
        }

        prevButtons = xinputButtons;
        prevDSButtons = dsButtons;
    }
}

// ============================================================
//  ENTRY POINT
// ============================================================
static void ModMain() {
    Log(std::string(kModBaseName) + ": waiting for game to load...");
    Sleep(15000);

    // Initialize shared memory
    if (!g_sharedPlayerBase.Initialize(kModBaseName)) {
        Log(std::string(kModBaseName) + ": ERROR - Failed to initialize shared memory");
        return;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    g_iniPath = std::string(exePath);
    g_iniPath = g_iniPath.substr(0, g_iniPath.rfind('\\')) + "\\" + kModBaseName + ".ini";

    ReloadIniValues();
    GetFileModTime(g_iniPath, &g_lastIniModTime);

    Log(std::string(kModBaseName) + ": config loaded (live reload enabled)");
    Log(std::string("  INI = ") + g_iniPath);
    Log("  ForcePalm: Enabled=" + std::string(g_forcePalmBoostEnabled ? "YES" : "NO") +
        " BoostValue=" + std::to_string(g_forcePalmBoostValue) +
        " WindowMs=" + std::to_string(g_forcePalmBoostWindowMs));

    // Pos hook: use ClaimPosHook() so only one mod instance patches the bytes.
    // If we win: scan, install, publish the address via shared memory.
    // If we lose: wait for the winner to publish the address, then chain there.
    // This prevents the race where both mods scan the same bytes, the first patches
    // them into a JMP trampoline, and the second scan fails on the modified bytes.
    if (g_sharedPlayerBase.ClaimPosHook()) {
        static const uint8_t posPrimaryPattern[] = {
            0x49, 0x3B, 0xF7,
            0x0F, 0x8C, 0xFF, 0xFF, 0xFF, 0xFF,
            0x0F, 0x28, 0xC6,
            0xF3, 0x45, 0x0F, 0x5C, 0xC8,
            0x41, 0x0F, 0x58, 0x45, 0x00,
            0x41, 0x0F, 0x11, 0x45, 0x00
        };
        static const char posPrimaryMask[] = "xxxxx????xxxxxxxxxxxxxxxxxx";
        uint8_t* posLoc = PatternScan(posPrimaryPattern, posPrimaryMask, sizeof(posPrimaryPattern));
        if (posLoc) {
            posLoc += 17;
            if (InstallPosHook(posLoc)) {
                g_sharedPlayerBase.SetPosHookAddress(reinterpret_cast<uintptr_t>(posLoc));
            }
        }
        else {
            Log(std::string(kModBaseName) + ": WARNING - boost unavailable, game version may not be supported");
        }
    }
    else {
        uintptr_t posAddr = 0;
        for (int i = 0; i < 50 && posAddr == 0; i++) {
            Sleep(100);
            posAddr = g_sharedPlayerBase.GetPosHookAddress();
        }
        if (posAddr) {
            InstallPosHook(reinterpret_cast<uint8_t*>(posAddr));
        }
        else {
            Log(std::string(kModBaseName) + ": WARNING - boost unavailable, game version may not be supported");
        }
    }

    // Initialize DualSense
    DualSense_Initialize();

    Log(std::string(kModBaseName) + ": Ready!");
    timeBeginPeriod(1);
    InputLoop();
    timeEndPeriod(1);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        s_instanceMutex = CreateMutexA(NULL, TRUE, "CrimsonDesert_ForcePalmBoost_Singleton");
        if (s_instanceMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (s_instanceMutex) { CloseHandle(s_instanceMutex); s_instanceMutex = NULL; }
            return TRUE;
        }
        DisableThreadLibraryCalls(hModule);
        std::thread(ModMain).detach();
    }
    return TRUE;
}
