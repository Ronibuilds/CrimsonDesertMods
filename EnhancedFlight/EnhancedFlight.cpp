// EnhancedFlight - dllmain.cpp
// Enhanced flight mechanics for Crimson Desert
// - Ascend flight speed boost (RB/R1 button, hold)
// - Descend flight speed boost (RT/R2 button, hold)
// - Horizontal flight speed boost (A/Cross button, hold/toggle)
// - Aerial roll speed boost (B/Circle button)
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
}
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Xinput.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

// Shared memory for cross-mod compatibility
#include "shared_player_base.h"
static SharedPlayerBase g_sharedPlayerBase;

// Global initialization guard to prevent double-initialization
static std::atomic<bool> g_isInitialized{ false };

// SafetyHook for proper hook chaining (compatibility with other mods)
#include "safetyhook.hpp"
static safetyhook::MidHook g_velHook;
static safetyhook::MidHook g_posHook;

template <typename T>
static inline T Clamp(T value, T lo, T hi) {
    return (value < lo) ? lo : (value > hi) ? hi : value;
}

// ============================================================
//  XBOX BUTTON CONSTANTS
// ============================================================
#define BTN_A   XINPUT_GAMEPAD_A                // 4096
#define BTN_B   XINPUT_GAMEPAD_B                // 8192
#define BTN_RB  XINPUT_GAMEPAD_RIGHT_SHOULDER   // 512
#define BTN_LT  0x0800                          // Left Trigger (custom flag)
#define BTN_RT  0x1000                          // Right Trigger (custom flag)

// ============================================================
//  PS5 DUALSENSE CONSTANTS & STRUCTURES
// ============================================================
// DualSense VID/PID
#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID 0x0CE6  // DualSense (original)
#define DUALSENSE_EDGE_PID 0x0DF2  // DualSense Edge

// PS5 Button bit masks (matching DualSense HID report format)
#define DS_BTN_SQUARE    0x0010   // 16
#define DS_BTN_CROSS     0x0020   // 32
#define DS_BTN_CIRCLE    0x0040   // 64
#define DS_BTN_TRIANGLE  0x0080   // 128
#define DS_BTN_L1        0x0100   // 256
#define DS_BTN_R1        0x0200   // 512
#define DS_BTN_L2        0x0400   // 1024
#define DS_BTN_R2        0x0800   // 2048
#define DS_BTN_CREATE    0x1000   // 4096 (Share/Create button)
#define DS_BTN_OPTIONS   0x2000   // 8192
#define DS_BTN_L3        0x4000   // 16384
#define DS_BTN_R3        0x8000   // 32768
#define DS_BTN_PS        0x10000  // PS button (in separate byte)

// DualSense input report structure (USB mode - Report ID 0x01)
#pragma pack(push, 1)
struct DualSenseInputUSB {
    uint8_t reportId;         // 0x01
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftTrigger;      // L2 analog
    uint8_t rightTrigger;     // R2 analog
    uint8_t sequence;
    uint8_t buttons[4];       // Button states packed
    uint8_t reserved;
    // Additional data follows but we only need basics for button input
};

// DualSense input report structure (Bluetooth mode - Report ID 0x31)
struct DualSenseInputBT {
    uint8_t reportId;         // 0x31
    uint8_t unk1;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t leftTrigger;      // L2 analog
    uint8_t rightTrigger;     // R2 analog
    uint8_t sequence;
    uint8_t buttons[4];       // Button states packed
    uint8_t reserved;
    // Additional data follows
};
#pragma pack(pop)

// DualSense controller state
struct DualSenseState {
    HANDLE deviceHandle;
    bool isConnected;
    bool isBluetooth;
    uint32_t buttons;         // Combined button state
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
};

static DualSenseState g_dualSense = { INVALID_HANDLE_VALUE, false, false, 0, 0, 0, 128, 128, 128, 128 };

// ============================================================
//  VELOCITY OFFSETS
// ============================================================
// 1.04.01: velocity is now a packed float4 {X, Z_height, Y, W} at entity+0x1B0
#define VEL_Z_OFFSET   0x1B4  // vertical velocity (second float of packed vector at entity+0x1B0)

// ============================================================
//  FLIGHT STATE DETECTION
// ============================================================
// Heuristic: we can't read a true "wings deployed" flag, so we gate boosts
// behind a conservative "likely-flight" state to avoid activating during
// jumps/combat airtime.
//
// Notes:
// - Ground detection is based on vertical velocity being ~0 for a short time.
// - Flight entry requires being airborne for a minimum time and moving fast enough.
static float g_flightVelThreshold = 3.0f;          // (Legacy) vertical velocity threshold (kept for INI compatibility)
static float g_groundedVelEpsilon = 0.05f;         // |velZ| below this for some time => grounded
static float g_airborneVelThreshold = 0.25f;        // |velZ| above this => definitely airborne (jump/fall)
static int   g_airborneGraceMs = 650;               // keep "airborne" true briefly when velZ ~= 0 (jump apex / wing deploy)
static int   g_groundedTimeThreshold = 500;         // ms grounded-like before clearing flight (raised: combat sub-states are ~200-400ms)
static int   g_flightTimeThreshold = 250;           // ms airborne before considering "flight"
static int   g_flightConfirmMs = 120;               // ms of "controlled" vertical speed before we call it flight
static int   g_recentFlightWindowMs = 3000;         // within this ms of a flight exit, re-confirm instantly if airborne+controlled

// ============================================================
//  CONFIG (loaded from EnhancedFlight.ini)
// ============================================================
static const char* kModBaseName = "EnhancedFlight";
static const char* kLogFileName = "EnhancedFlight.log";

// Ascend (upward flight)
static bool  g_ascendEnabled = true;
static float g_ascendBoostValue = 8.0f;        // Ascend boost (SETS velocity)
static int   g_ascendButton = BTN_RB;          // Xbox RB
static int   g_ascendKey = 0x14;               // Caps Lock (keyboard fallback)
static int   g_ascendRampUpMs = 300;           // Time to ramp up to full speed (ms)
static int   g_ascendRampDownMs = 200;         // Time to ramp down to normal speed (ms)

// Descend (downward flight)
static bool  g_descendEnabled = true;
static float g_descendBoostValue = -8.0f;      // Descend boost (SETS velocity)
static int   g_descendButton = BTN_RT;         // Xbox RT (right trigger)
static int   g_descendKey = 0x11;              // Ctrl (keyboard fallback)
static int   g_descendRampUpMs = 300;          // Time to ramp up to full speed (ms)
static int   g_descendRampDownMs = 200;        // Time to ramp down to normal speed (ms)

// Wing retract detection (gates ascend/descend when wings are retracted)
static int   g_wingRetractButton = 0;          // 0 = not bound
static int   g_wingRetractKey = 0x20;          // Space (VK_SPACE)

// Horizontal flight
static bool  g_horizontalEnabled = true;
static bool  g_horizontalUseToggle = false;    // false = hold mode (default), true = toggle mode
static float g_horizontalBoostValue = 4.0f;    // Horizontal multiplier
static int   g_horizontalButton = BTN_A;       // Xbox A
static int   g_horizontalKey = 0xA0;           // Left Shift (keyboard)
static int   g_horizontalRampUpMs = 500;       // Time to ramp up to full speed (ms)
static int   g_horizontalRampDownMs = 300;     // Time to ramp down to normal speed (ms)

// Aerial roll speed boost
static bool  g_aerialRollBoostEnabled = true;
static float g_aerialRollBoostValue = 4.5f;    // Speed multiplier during boost
static float g_aerialRollBoostDuration = 3.0f; // Duration in seconds
static int   g_aerialRollButton = BTN_B;       // Xbox B
static int   g_aerialRollKey = 0xA4;           // Left Alt (VK_LMENU)
static int   g_aerialRollRampUpMs = 400;       // Time to ramp up to full speed (ms)
static int   g_aerialRollRampDownMs = 600;     // Time to ramp down (ms)

// ============================================================
//  SHARED STATE
// ============================================================
static uintptr_t                g_playerBase = 0;
// Captured from PosHookCallback (ctx.rbx = player entity, confirmed by diagnostic)
static std::atomic<uintptr_t>  g_capturedPlayerBase{ 0 };
static std::atomic<bool>  g_ascendBoostActive{ false };
static std::atomic<bool>  g_descendBoostActive{ false };
static std::atomic<bool>  g_horizontalBoostActive{ false };
static std::atomic<float> g_aerialRollBoostMultiplier{ 1.0f }; // Current boost multiplier
static ULONGLONG          g_aerialRollBoostEndTime = 0;        // When boost ends
static std::string        g_iniPath;
static ULONGLONG          g_lastIniModTime = 0;

// Smooth acceleration/deceleration state (horizontal and aerial only)
static std::atomic<float> g_currentHorizontalMult{ 1.0f };     // Current horizontal multiplier (smoothed)
static std::atomic<float> g_currentAerialMult{ 1.0f };         // Current aerial multiplier (smoothed)
static ULONGLONG          g_horizontalRampStartMs = 0;         // When horizontal ramp started
static ULONGLONG          g_aerialRampStartMs = 0;             // When aerial ramp started
static bool               g_horizontalRamping = false;         // True if ramping up (false = ramping down)
static bool               g_aerialRamping = false;
static float              g_horizontalRampStartValue = 1.0f;   // Value when ramp-down started
static float              g_aerialRampStartValue = 1.0f;       // Value when ramp-down started

// Ascend ramp state
static std::atomic<float> g_currentAscendVel{ 0.0f };
static ULONGLONG          g_ascendRampStartMs = 0;
static bool               g_ascendRamping = false;
static float              g_ascendRampStartValue = 0.0f;

// Descend ramp state
static std::atomic<float> g_currentDescendVel{ 0.0f };
static ULONGLONG          g_descendRampStartMs = 0;
static bool               g_descendRamping = false;
static float              g_descendRampStartValue = 0.0f;

// Wing retract state
static bool               g_wingsRetracted = false;

// Flight state detection
static ULONGLONG          g_lastAirborneTime = 0;     // When we first went airborne
static std::atomic<bool>  g_isInFlight{ false };      // True when wings are likely deployed
static bool               g_wasAirborne = false;      // Previous frame airborne state
static ULONGLONG          g_groundedSinceMs = 0;      // When we first observed grounded-like motion
static ULONGLONG          g_lastAirborneMotionMs = 0; // Last time |velZ| was large enough to confirm airborne
static ULONGLONG          g_flightCandidateSinceMs = 0; // When we first observed "controlled" vertical speed
static ULONGLONG          g_vertBoostLastActiveMs = 0;   // last tick our mod had non-zero vertical boost
static ULONGLONG          g_lastFlightConfirmedMs = 0;   // last time g_isInFlight was set to true
// Bug 3: playerBase stability tracking (prevents spurious flight confirm after save/load)
static uintptr_t          g_lastSeenPlayerBase = 0;    // previous playerBase value
static ULONGLONG          g_playerBaseChangeMs = 0;    // when playerBase last changed

// Natural (game-computed) vertical velocity — captured from xmm6 in VelHookCallback
// before InputLoop can overwrite 0x2C8/0x2B4. Immune to our own ascend boost injection.
static std::atomic<float> g_naturalVelZ{ 0.0f };

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

// Find and open DualSense controller
static bool DualSense_Initialize() {
    // Close existing handle if open
    if (g_dualSense.deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dualSense.deviceHandle);
        g_dualSense.deviceHandle = INVALID_HANDLE_VALUE;
    }

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, nullptr, 0, &requiredSize, nullptr);

        auto* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(requiredSize);
        if (!detailData) continue;

        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, detailData, requiredSize, nullptr, nullptr)) {
            HANDLE hDevice = CreateFileA(detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);

            if (hDevice != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrib;
                attrib.Size = sizeof(HIDD_ATTRIBUTES);

                if (HidD_GetAttributes(hDevice, &attrib)) {
                    // Check if this is a DualSense or DualSense Edge
                    if (attrib.VendorID == DUALSENSE_VID &&
                        (attrib.ProductID == DUALSENSE_PID || attrib.ProductID == DUALSENSE_EDGE_PID)) {

                        g_dualSense.deviceHandle = hDevice;
                        g_dualSense.isConnected = true;

                        // Determine connection type by checking preparsed data
                        PHIDP_PREPARSED_DATA preparsedData;
                        if (HidD_GetPreparsedData(hDevice, &preparsedData)) {
                            HIDP_CAPS caps;
                            if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
                                // Bluetooth uses larger input report (0x31), USB uses 0x01
                                g_dualSense.isBluetooth = (caps.InputReportByteLength > 64);
                            }
                            HidD_FreePreparsedData(preparsedData);
                        }

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

// Parse button data from HID report
static uint32_t DualSense_ParseButtons(const uint8_t* buttonData) {
    uint32_t buttons = 0;

    // Button byte layout:
    // buttons[0]: bits 0-3 = D-pad, bit 4 = Square, bit 5 = Cross, bit 6 = Circle, bit 7 = Triangle
    // buttons[1]: bit 0 = L1, bit 1 = R1, bit 2 = L2, bit 3 = R2, bit 4 = Create, bit 5 = Options, bit 6 = L3, bit 7 = R3
    // buttons[2]: bit 0 = PS button, bit 1 = Touchpad button

    if (buttonData[0] & 0x10) buttons |= DS_BTN_SQUARE;
    if (buttonData[0] & 0x20) buttons |= DS_BTN_CROSS;
    if (buttonData[0] & 0x40) buttons |= DS_BTN_CIRCLE;
    if (buttonData[0] & 0x80) buttons |= DS_BTN_TRIANGLE;

    if (buttonData[1] & 0x01) buttons |= DS_BTN_L1;
    if (buttonData[1] & 0x02) buttons |= DS_BTN_R1;
    if (buttonData[1] & 0x04) buttons |= DS_BTN_L2;
    if (buttonData[1] & 0x08) buttons |= DS_BTN_R2;
    if (buttonData[1] & 0x10) buttons |= DS_BTN_CREATE;
    if (buttonData[1] & 0x20) buttons |= DS_BTN_OPTIONS;
    if (buttonData[1] & 0x40) buttons |= DS_BTN_L3;
    if (buttonData[1] & 0x80) buttons |= DS_BTN_R3;

    if (buttonData[2] & 0x01) buttons |= DS_BTN_PS;

    return buttons;
}

// Read DualSense input state
static bool DualSense_ReadInput() {
    if (g_dualSense.deviceHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    uint8_t buffer[128];
    memset(buffer, 0, sizeof(buffer));

    DWORD bytesRead = 0;
    if (!ReadFile(g_dualSense.deviceHandle, buffer, sizeof(buffer), &bytesRead, nullptr)) {
        // Read failed - controller may be disconnected
        g_dualSense.isConnected = false;
        return false;
    }

    if (bytesRead == 0) {
        return false;
    }

    // Parse based on connection type
    if (!g_dualSense.isBluetooth && buffer[0] == 0x01) {
        // USB mode (Report ID 0x01)
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
        // Bluetooth mode (Report ID 0x31)
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

// Update DualSense state (call every frame)
static void DualSense_Update() {
    // Try to reconnect if disconnected
    if (!g_dualSense.isConnected) {
        static ULONGLONG lastReconnectAttempt = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastReconnectAttempt > 2000) { // Try every 2 seconds
            lastReconnectAttempt = now;
            DualSense_Initialize();
        }
    }

    if (g_dualSense.isConnected) {
        if (!DualSense_ReadInput()) {
            // Read failed, mark as disconnected
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
    long value = strtol(buf, &end, 0); // base 0 supports 0x.. hex and decimal
    if (end == buf) return def;
    return (int)value;
}
static bool ReadIniBool(const char* file, const char* section, const char* key, bool def) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), file);
    if (!buf[0]) return def;

    // Accept: true/false, 1/0, yes/no, on/off
    if (_stricmp(buf, "true") == 0 || _stricmp(buf, "1") == 0 ||
        _stricmp(buf, "yes") == 0 || _stricmp(buf, "on") == 0) return true;
    if (_stricmp(buf, "false") == 0 || _stricmp(buf, "0") == 0 ||
        _stricmp(buf, "no") == 0 || _stricmp(buf, "off") == 0) return false;
    return def;
}

// Get file last modified time
static bool GetFileModTime(const std::string& path, ULONGLONG* outTime) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return false;
    }
    ULARGE_INTEGER uli;
    uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
    *outTime = uli.QuadPart;
    return true;
}

// Reload INI values
static void ReloadIniValues() {
    // Ascend settings
    g_ascendEnabled = ReadIniBool(g_iniPath.c_str(), "Ascend", "Enabled", true);
    g_ascendBoostValue = ReadIniFloat(g_iniPath.c_str(), "Ascend", "BoostValue", 8.0f);
    g_ascendButton = ReadIniInt(g_iniPath.c_str(), "Ascend", "Button", BTN_RB);
    g_ascendKey = ReadIniInt(g_iniPath.c_str(), "Ascend", "Key", 0x14);
    g_ascendRampUpMs = ReadIniInt(g_iniPath.c_str(), "Ascend", "RampUpMs", 300);
    g_ascendRampDownMs = ReadIniInt(g_iniPath.c_str(), "Ascend", "RampDownMs", 200);

    // Descend settings
    g_descendEnabled = ReadIniBool(g_iniPath.c_str(), "Descend", "Enabled", true);
    g_descendBoostValue = ReadIniFloat(g_iniPath.c_str(), "Descend", "BoostValue", -8.0f);
    g_descendButton = ReadIniInt(g_iniPath.c_str(), "Descend", "Button", BTN_RT);
    g_descendKey = ReadIniInt(g_iniPath.c_str(), "Descend", "Key", 0x11);
    g_descendRampUpMs = ReadIniInt(g_iniPath.c_str(), "Descend", "RampUpMs", 300);
    g_descendRampDownMs = ReadIniInt(g_iniPath.c_str(), "Descend", "RampDownMs", 200);

    // Horizontal settings
    g_horizontalEnabled = ReadIniBool(g_iniPath.c_str(), "Horizontal", "Enabled", true);
    g_horizontalUseToggle = ReadIniBool(g_iniPath.c_str(), "Horizontal", "UseToggle", false);
    g_horizontalBoostValue = ReadIniFloat(g_iniPath.c_str(), "Horizontal", "BoostValue", 4.0f);
    g_horizontalButton = ReadIniInt(g_iniPath.c_str(), "Horizontal", "Button", BTN_A);
    g_horizontalKey = ReadIniInt(g_iniPath.c_str(), "Horizontal", "Key", 0xA0);
    g_horizontalRampUpMs = ReadIniInt(g_iniPath.c_str(), "Horizontal", "RampUpMs", 500);
    g_horizontalRampDownMs = ReadIniInt(g_iniPath.c_str(), "Horizontal", "RampDownMs", 300);

    // Aerial roll boost settings
    g_aerialRollBoostEnabled = ReadIniBool(g_iniPath.c_str(), "AerialRoll", "Enabled", true);
    g_aerialRollBoostValue = ReadIniFloat(g_iniPath.c_str(), "AerialRoll", "BoostValue", 6.0f);
    g_aerialRollBoostDuration = ReadIniFloat(g_iniPath.c_str(), "AerialRoll", "Duration", 3.0f);
    g_aerialRollButton = ReadIniInt(g_iniPath.c_str(), "AerialRoll", "Button", BTN_B);
    g_aerialRollKey = ReadIniInt(g_iniPath.c_str(), "AerialRoll", "Key", 0xA4);
    g_aerialRollRampUpMs = ReadIniInt(g_iniPath.c_str(), "AerialRoll", "RampUpMs", 400);
    g_aerialRollRampDownMs = ReadIniInt(g_iniPath.c_str(), "AerialRoll", "RampDownMs", 600);

    // Wing retract settings
    g_wingRetractButton = ReadIniInt(g_iniPath.c_str(), "WingRetract", "Button", 0);
    g_wingRetractKey = ReadIniInt(g_iniPath.c_str(), "WingRetract", "Key", 0x20);

    Log(std::string(kModBaseName) + ": INI reloaded - " +
        "Ascend=" + (g_ascendEnabled ? "ON" : "OFF") +
        " Descend=" + (g_descendEnabled ? "ON" : "OFF") +
        " Horizontal=" + (g_horizontalEnabled ? "ON" : "OFF") +
        " (Toggle=" + (g_horizontalUseToggle ? "YES" : "NO") + ")" +
        " AerialRollBoost=" + (g_aerialRollBoostEnabled ? "ON" : "OFF"));
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
//  HOOK 1: VERTICAL VELOCITY HOOK (SafetyHook)
//  Captures playerBase (RSI) every physics frame
//  Uses SafetyHook for compatibility with other mods
// ============================================================
static void VelHookCallback(safetyhook::Context& ctx) {
    uintptr_t playerBase = ctx.rax;

    if (playerBase > 0x100000) {
        // Bug 3: detect playerBase change (happens on save/load — new entity address).
        // Reset all flight state so the stabilization period starts fresh.
        if (playerBase != g_lastSeenPlayerBase && g_lastSeenPlayerBase != 0) {
            g_playerBaseChangeMs = GetTickCount64();
            g_isInFlight.store(false);
            g_wasAirborne = false;
            g_lastAirborneTime = 0;
            g_lastAirborneMotionMs = 0;
            g_flightCandidateSinceMs = 0;
            g_groundedSinceMs = 0;
        }
        g_lastSeenPlayerBase = playerBase;
        g_playerBase = playerBase;
        g_sharedPlayerBase.UpdatePlayerBase(playerBase);
    }
}

static bool InstallVelHook(uint8_t* location) {
    // Use safetyhook::MidHook for mid-function hooking
    // This automatically handles hook chaining if another mod already hooked here
    auto hook_result = safetyhook::MidHook::create(
        reinterpret_cast<void*>(location),
        VelHookCallback
    );

    if (!hook_result) {
        Log(std::string(kModBaseName) + ": WARNING - secondary detection unavailable");
        return false;
    }

    g_velHook = std::move(*hook_result);
    Log(std::string(kModBaseName) + ": detection active");
    return true;
}

// ============================================================
//  HOOK 2: HORIZONTAL POSITION DELTA HOOK (SafetyHook)
//  Uses SafetyHook for compatibility with other mods
// ============================================================
static void PosHookCallback(safetyhook::Context& ctx) {
    if (ctx.rbx > 0x100000ULL)
        g_capturedPlayerBase.store(ctx.rbx);

    __try {
        // r13 contains position pointer
        uintptr_t r13_ptr = ctx.r13;

        if (r13_ptr < 0x100000) return;
        if (g_playerBase == 0) return;

        // Check if we have any active multipliers (including ramp-down)
        float horizontalMult = g_currentHorizontalMult.load();
        float aerialMult = g_currentAerialMult.load();
        float ascendVel = g_currentAscendVel.load();
        float descendVel = g_currentDescendVel.load();
        float vertBoost = ascendVel + descendVel; // ascendVel > 0, descendVel < 0

        bool inFlight = g_isInFlight.load();

        // Allow processing if we're ramping down, in flight with any boost active
        bool isRampingDown = (horizontalMult > 1.0f) || (aerialMult > 1.0f);
        bool hasVertBoost = (vertBoost != 0.0f) && inFlight;
        bool inFlightWithBoost = inFlight && (g_horizontalBoostActive.load() ||
            g_aerialRollBoostMultiplier.load() > 1.0f ||
            hasVertBoost);

        if (!isRampingDown && !inFlightWithBoost) return;

        // Hard gate: never apply speed changes unless in true flight.
        if (!inFlight) return;

        // xmm0 contains position delta [deltaX, deltaY, deltaZ, deltaW]
        // Y (f32[1]) is vertical — writing to entity+velZOffset doesn't work because
        // the engine overwrites it within the same physics tick. Modifying the delta
        // here (at integration time) is the only reliable way to control vertical movement.
        float delta_x = ctx.xmm0.f32[0];
        float delta_z = ctx.xmm0.f32[2];

        if (delta_x != delta_x || delta_z != delta_z) return;
        if (delta_x > 50.0f || delta_x < -50.0f) return;
        if (delta_z > 50.0f || delta_z < -50.0f) return;

        // Horizontal / aerial roll boost (XZ plane)
        float totalMultiplier = std::max(horizontalMult, aerialMult);
        totalMultiplier = Clamp(totalMultiplier, 1.0f, 25.0f);

        if (totalMultiplier != 1.0f) {
            float new_dx = Clamp(delta_x * totalMultiplier, -50.0f, 50.0f);
            float new_dz = Clamp(delta_z * totalMultiplier, -50.0f, 50.0f);
            ctx.xmm0.f32[0] = new_dx;
            ctx.xmm0.f32[2] = new_dz;
        }

        // Vertical boost (Y axis).
        // The hook fires N times per frame (one per physics substep). We use QPC for sub-ms
        // precision: the first substep of each frame has a large elapsed time (~frame_dt),
        // subsequent substeps in the same frame have elapsed ~0. Applying vertBoost*dt on
        // every call means the first substep carries the full frame's boost and the rest
        // contribute nothing — which sums correctly and is smooth every rendered frame.
        if (hasVertBoost) {
            static LARGE_INTEGER s_qpcFreq = { 0 };
            static LARGE_INTEGER s_lastQPC = { 0 };
            if (s_qpcFreq.QuadPart == 0)
                QueryPerformanceFrequency(&s_qpcFreq);
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float vertDt = 0.0f;
            if (s_lastQPC.QuadPart != 0 && s_qpcFreq.QuadPart != 0) {
                double elapsed = (double)(now.QuadPart - s_lastQPC.QuadPart) / (double)s_qpcFreq.QuadPart;
                vertDt = (float)Clamp(elapsed, 0.0, 0.05);
            }
            s_lastQPC = now;
            if (vertDt > 0.00001f) {
                float deltaY = ctx.xmm0.f32[1];
                ctx.xmm0.f32[1] = Clamp(deltaY + vertBoost * vertDt, -50.0f, 50.0f);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silently catch exceptions
    }
}

static bool InstallPosHook(uint8_t* location) {
    // Use safetyhook::MidHook for mid-function hooking
    auto hook_result = safetyhook::MidHook::create(
        reinterpret_cast<void*>(location),
        PosHookCallback
    );

    if (!hook_result) {
        Log(std::string(kModBaseName) + ": ERROR - speed boost unavailable");
        return false;
    }

    g_posHook = std::move(*hook_result);
    Log(std::string(kModBaseName) + ": speed boost active");
    return true;
}

// ============================================================
//  FLIGHT STATE DETECTION FUNCTION
// ============================================================
static bool IsPlayerInFlight(ULONGLONG nowMs) {
    if (g_playerBase == 0) return false;

    // Bug 3: refuse to confirm flight during the 2s stabilization window after
    // a playerBase change (save/load gives a new player entity pointer).
    if (g_playerBaseChangeMs != 0 && (nowMs - g_playerBaseChangeMs) < 2000ULL) {
        return false;
    }
    if (g_playerBaseChangeMs != 0 && (nowMs - g_playerBaseChangeMs) >= 2000ULL) {
        g_playerBaseChangeMs = 0;  // stabilized — clear the gate
    }

    __try {
        float velZ = *reinterpret_cast<float*>(g_playerBase + VEL_Z_OFFSET);

        // Sanity-check the float before using it.
        if (velZ != velZ || velZ > 500.0f || velZ < -500.0f) return g_isInFlight.load();

        float absVelZ = std::fabs(velZ);

        // Confirmed airborne when vertical velocity is clearly non-zero.
        if (absVelZ >= g_airborneVelThreshold) {
            g_lastAirborneMotionMs = nowMs;
            if (!g_wasAirborne) {
                g_lastAirborneTime = nowMs;
                g_wasAirborne = true;
                // Don't de-confirm flight if the mod itself is driving this velZ spike.
                // A game position-correction followed by our boost re-kicking is NOT a landing.
                bool modDrivingVelZ = (std::fabs(g_currentAscendVel.load()) > 0.5f ||
                    std::fabs(g_currentDescendVel.load()) > 0.5f);
                if (!modDrivingVelZ) {
                    g_isInFlight.store(false);
                }
                g_flightCandidateSinceMs = 0;
            }
        }

        // Airborne grace: treat brief velZ==0 windows as still airborne (jump apex / wing deploy)
        bool airborneGrace = (g_lastAirborneMotionMs != 0) && ((nowMs - g_lastAirborneMotionMs) <= (ULONGLONG)g_airborneGraceMs);
        if (!g_wasAirborne && airborneGrace) {
            g_wasAirborne = true;
            if (g_lastAirborneTime == 0) g_lastAirborneTime = nowMs;
        }

        // Grounded-like: only when velZ is very close to 0 AND we have no recent airborne motion.
        // (Avoids killing flight at jump apex / wing deploy.)
        // Also protected when vertical boost was recently active — the ramp-down naturally passes
        // through velZ≈0 and should not be misread as landing.
        bool vertBoostRecent = (g_vertBoostLastActiveMs != 0) &&
            ((nowMs - g_vertBoostLastActiveMs) < 300ULL);
        bool groundedLike = (absVelZ <= g_groundedVelEpsilon) && !airborneGrace && !vertBoostRecent;

        if (groundedLike) {
            if (g_groundedSinceMs == 0) {
                g_groundedSinceMs = nowMs;
            }
            if ((nowMs - g_groundedSinceMs) >= (ULONGLONG)g_groundedTimeThreshold) {
                g_isInFlight.store(false);
                g_wasAirborne = false;
                g_lastAirborneTime = 0;
                g_lastAirborneMotionMs = 0;
                g_flightCandidateSinceMs = 0;
                g_lastFlightConfirmedMs = 0;
            }
            return g_isInFlight.load();
        }
        g_groundedSinceMs = 0;

        // Flight entry: must have been airborne for a bit AND have "controlled" vertical speed.
        // This is a heuristic for "wings deployed" (stable glide) vs. ballistic jump/fall.
        if (g_wasAirborne) {
            ULONGLONG airborneTime = (g_lastAirborneTime != 0) ? (nowMs - g_lastAirborneTime) : 0;
            // Allow flight re-confirm even at high velZ if our mod is providing the boost.
            bool modDrivingVelZ = (std::fabs(g_currentAscendVel.load()) > 0.5f ||
                std::fabs(g_currentDescendVel.load()) > 0.5f);
            if (absVelZ <= g_flightVelThreshold || modDrivingVelZ) {
                if (g_flightCandidateSinceMs == 0) g_flightCandidateSinceMs = nowMs;
            }
            else {
                g_flightCandidateSinceMs = 0;
            }

            ULONGLONG controlledTime = (g_flightCandidateSinceMs != 0) ? (nowMs - g_flightCandidateSinceMs) : 0;

            bool recentFlight = (g_lastFlightConfirmedMs != 0) &&
                ((nowMs - g_lastFlightConfirmedMs) <= (ULONGLONG)g_recentFlightWindowMs);
            bool fastConfirm = recentFlight && controlledTime >= (ULONGLONG)g_flightConfirmMs;

            if (fastConfirm ||
                (airborneTime >= (ULONGLONG)g_flightTimeThreshold && controlledTime >= (ULONGLONG)g_flightConfirmMs)) {
                g_isInFlight.store(true);
                g_lastFlightConfirmedMs = nowMs;
            }
        }

        // If we're not in flight and we have no recent airborne motion, clear airborne state.
        if (!g_isInFlight.load() && !airborneGrace) {
            g_wasAirborne = false;
            g_lastAirborneTime = 0;
        }

        return g_isInFlight.load();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static float ReadVelZSafe(uintptr_t playerBase) {
    __try {
        return *reinterpret_cast<float*>(playerBase + VEL_Z_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

// ============================================================
//  INPUT LOOP
// ============================================================
static void InputLoop() {

    WORD prevButtons = 0;
    uint32_t prevDSButtons = 0;
    bool prevHorizKeyDown = false;
    bool prevAerialKeyDown = false;
    bool prevWingRetractDown = false;
    ULONGLONG fastFallSinceMs = 0;
    ULONGLONG lastIniPollMs = GetTickCount64();


    while (true) {
        Sleep(1);

        // Capture natural velZ before any writes this tick (immune to our own boost injection).
        if (g_playerBase != 0) {
            float nv = ReadVelZSafe(g_playerBase);
            if (nv == nv && nv > -500.0f && nv < 500.0f) g_naturalVelZ.store(nv);
        }

        // Update player base — prefer PosHook capture (ctx.rbx), fall back to shared memory.
        {
            uintptr_t captured = g_capturedPlayerBase.load();
            if (captured != 0) {
                if (g_lastSeenPlayerBase != 0 && captured != g_lastSeenPlayerBase) {
                    // Player entity changed (save/load) — reset all flight state.
                    g_playerBaseChangeMs = GetTickCount64();
                    g_isInFlight.store(false);
                    g_wasAirborne = false;
                    g_lastAirborneTime = 0;
                    g_lastAirborneMotionMs = 0;
                    g_flightCandidateSinceMs = 0;
                    g_groundedSinceMs = 0;
                }
                g_lastSeenPlayerBase = captured;
                g_playerBase = captured;
                g_sharedPlayerBase.UpdatePlayerBase(captured);
            }
            else if (!g_sharedPlayerBase.IsHookOwner()) {
                uintptr_t sharedBase = g_sharedPlayerBase.GetPlayerBase();
                if (sharedBase != 0 && sharedBase != g_playerBase)
                    g_playerBase = sharedBase;
            }
        }

        // Check INI for changes every 1000ms
        ULONGLONG nowMs = GetTickCount64();
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

        // Update DualSense state
        DualSense_Update();

        // Read XInput (Xbox controller) - try all 4 slots to find connected controller
        XINPUT_STATE state{};
        bool hasXInput = false;
        for (DWORD i = 0; i < 4; i++) {
            if (XInputGetState(i, &state) == ERROR_SUCCESS) {
                hasXInput = true;
                break;
            }
        }
        WORD xinputButtons = hasXInput ? state.Gamepad.wButtons : 0;
        WORD xinputJustPressed = xinputButtons & ~prevButtons;
        BYTE xinputLeftTrigger = hasXInput ? state.Gamepad.bLeftTrigger : 0;
        BYTE xinputRightTrigger = hasXInput ? state.Gamepad.bRightTrigger : 0;

        // Read DualSense (PS5 controller)
        bool hasDualSense = g_dualSense.isConnected;
        uint32_t dsButtons = hasDualSense ? g_dualSense.buttons : 0;
        uint32_t dsJustPressed = dsButtons & ~prevDSButtons;
        BYTE dsLeftTrigger = hasDualSense ? g_dualSense.leftTrigger : 0;
        BYTE dsRightTrigger = hasDualSense ? g_dualSense.rightTrigger : 0;

        // Combine controller states (prefer DualSense if both connected)
        bool hasController = hasDualSense || hasXInput;
        WORD buttons = 0;
        WORD justPressed = 0;
        BYTE leftTrigger = 0;
        BYTE rightTrigger = 0;

        if (hasDualSense) {
            // Map DualSense buttons to XInput-compatible format for compatibility with existing button checks
            // Cross -> A, Circle -> B, Square -> X, Triangle -> Y
            // R1 -> RB, L1 -> LB, etc.
            if (dsButtons & DS_BTN_CROSS)    buttons |= XINPUT_GAMEPAD_A;
            if (dsButtons & DS_BTN_CIRCLE)   buttons |= XINPUT_GAMEPAD_B;
            if (dsButtons & DS_BTN_SQUARE)   buttons |= XINPUT_GAMEPAD_X;
            if (dsButtons & DS_BTN_TRIANGLE) buttons |= XINPUT_GAMEPAD_Y;
            if (dsButtons & DS_BTN_R1)       buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (dsButtons & DS_BTN_L1)       buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (dsButtons & DS_BTN_OPTIONS)  buttons |= XINPUT_GAMEPAD_START;
            if (dsButtons & DS_BTN_CREATE)   buttons |= XINPUT_GAMEPAD_BACK;
            if (dsButtons & DS_BTN_L3)       buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
            if (dsButtons & DS_BTN_R3)       buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;

            if (dsJustPressed & DS_BTN_CROSS)    justPressed |= XINPUT_GAMEPAD_A;
            if (dsJustPressed & DS_BTN_CIRCLE)   justPressed |= XINPUT_GAMEPAD_B;
            if (dsJustPressed & DS_BTN_SQUARE)   justPressed |= XINPUT_GAMEPAD_X;
            if (dsJustPressed & DS_BTN_TRIANGLE) justPressed |= XINPUT_GAMEPAD_Y;
            if (dsJustPressed & DS_BTN_R1)       justPressed |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (dsJustPressed & DS_BTN_L1)       justPressed |= XINPUT_GAMEPAD_LEFT_SHOULDER;

            leftTrigger = dsLeftTrigger;
            rightTrigger = dsRightTrigger;
        }
        else if (hasXInput) {
            buttons = xinputButtons;
            justPressed = xinputJustPressed;
            leftTrigger = xinputLeftTrigger;
            rightTrigger = xinputRightTrigger;
        }

        // ---- CHECK FLIGHT STATE ----
        bool inFlight = IsPlayerInFlight(nowMs);
        // inAirborneGrace is intentionally NOT included in allowVertical (Bug 1 fix).
        // Grace period is internal to the flight state machine only — it must not bootstrap
        // boost activation during a jump, even when the player holds ascend/descend buttons.

        // ---- FAST-FALL DETECTION ----
        // If the game's natural velZ (xmm6, immune to our boost injection) has been more negative
        // than 8.0 m/s for 50ms straight, the player is in a wingless dive — block RB/LB.
        // We only suppress allowVertical here; we do NOT touch g_isInFlight.
        bool isFastFalling = false;
        {
            float naturalVelZ = g_naturalVelZ.load();
            bool modDrivingDescend = std::fabs(g_currentDescendVel.load()) > 0.5f;
            if (inFlight && !modDrivingDescend && naturalVelZ < -8.0f) {
                if (fastFallSinceMs == 0) fastFallSinceMs = nowMs;
                if ((nowMs - fastFallSinceMs) >= 50ULL) isFastFalling = true;
            }
            else {
                fastFallSinceMs = 0;
            }
        }

        // ---- WING RETRACT DETECTION ----
        {
            bool wingRetractDown = (g_wingRetractKey != 0 && (GetAsyncKeyState(g_wingRetractKey) & 0x8000) != 0) ||
                (g_wingRetractButton != 0 && hasController && (buttons & g_wingRetractButton) != 0);
            if (wingRetractDown && !prevWingRetractDown && inFlight) {
                g_wingsRetracted = !g_wingsRetracted;
            }
            if (!inFlight) {
                g_wingsRetracted = false;
            }
            prevWingRetractDown = wingRetractDown;
        }

        // notActuallyGrounded: true as soon as the player is airborne (g_groundedSinceMs resets
        // the moment velZ is non-zero). This is a direct ground-contact signal, unlike the old
        // recentlyGrounded timer (700ms) which blocked vertical boost for 330ms AFTER flight
        // confirmed — causing the "works sometimes, not others" reports. g_groundedSinceMs is
        // also used to cut horizontal boost immediately on landing (see below).
        bool notActuallyGrounded = (g_groundedSinceMs == 0);
        bool allowVertical = inFlight && notActuallyGrounded && !g_wingsRetracted && !isFastFalling;

        // ---- ASCEND BOOST (RB) - Hold to activate ----
        if (g_ascendEnabled && allowVertical) {
            bool kbDown = (GetAsyncKeyState(g_ascendKey) & 0x8000) != 0;
            // Support analog triggers (LT/RT) for ascend
            bool btnDown = (g_ascendButton == BTN_RT) ? (rightTrigger > 128) :
                (g_ascendButton == BTN_LT) ? (leftTrigger > 128) :
                (hasController && (buttons & g_ascendButton) != 0);
            g_ascendBoostActive.store(kbDown || btnDown);
        }
        else {
            g_ascendBoostActive.store(false);
        }

        // ---- DESCEND BOOST (RT) - Hold to activate ----
        if (g_descendEnabled && allowVertical) {
            bool kbDown = (GetAsyncKeyState(g_descendKey) & 0x8000) != 0;
            // Support analog triggers (LT/RT) for descend
            bool btnDown = (g_descendButton == BTN_RT) ? (rightTrigger > 128) :
                (g_descendButton == BTN_LT) ? (leftTrigger > 128) :
                (hasController && (buttons & g_descendButton) != 0);
            g_descendBoostActive.store(kbDown || btnDown);
        }
        else {
            g_descendBoostActive.store(false);
        }

        // ---- HORIZONTAL BOOST (A) - Hold or Toggle ----
        if (g_horizontalEnabled && inFlight && notActuallyGrounded) {
            if (g_horizontalUseToggle) {
                // Toggle mode
                bool toggled = false;
                if (hasController && (justPressed & g_horizontalButton)) {
                    toggled = true;
                }

                if (g_horizontalKey != 0) {
                    bool horizKeyDown = (GetAsyncKeyState(g_horizontalKey) & 0x8000) != 0;
                    if (horizKeyDown && !prevHorizKeyDown) {
                        toggled = true;
                    }
                    prevHorizKeyDown = horizKeyDown;
                }

                if (toggled) {
                    bool newState = !g_horizontalBoostActive.load();
                    g_horizontalBoostActive.store(newState);
                    Log(std::string(kModBaseName) + ": horizontal boost " + std::string(newState ? "ON" : "OFF"));
                }
            }
            else {
                // Hold mode (default)
                bool kbDown = g_horizontalKey != 0 && (GetAsyncKeyState(g_horizontalKey) & 0x8000) != 0;
                bool btnDown = hasController && (buttons & g_horizontalButton) != 0;

                bool horizBoost = kbDown || btnDown;
                g_horizontalBoostActive.store(horizBoost);
                prevHorizKeyDown = kbDown;
            }
        }
        else {
            g_horizontalBoostActive.store(false);
        }

        // Smooth ramping for horizontal boost (ramp-down only applies while still in flight and airborne)
        {
            bool wantHorizontalBoost = g_horizontalBoostActive.load() && g_horizontalEnabled && inFlight && notActuallyGrounded;
            float targetMult = wantHorizontalBoost ? std::max(1.0f, g_horizontalBoostValue) : 1.0f;
            float currentMult = g_currentHorizontalMult.load();

            if (wantHorizontalBoost && currentMult < targetMult) {
                // Ramp up
                if (!g_horizontalRamping) {
                    g_horizontalRamping = true;
                    g_horizontalRampStartMs = nowMs;
                }
                ULONGLONG elapsed = nowMs - g_horizontalRampStartMs;
                if (g_horizontalRampUpMs > 0 && elapsed < (ULONGLONG)g_horizontalRampUpMs) {
                    float progress = (float)elapsed / (float)g_horizontalRampUpMs;
                    currentMult = 1.0f + progress * (targetMult - 1.0f);
                }
                else {
                    currentMult = targetMult;
                }
                g_currentHorizontalMult.store(currentMult);
            }
            else if (wantHorizontalBoost) {
                // Holding at max — keep ramp state clear so ramp-down starts fresh on release
                g_currentHorizontalMult.store(targetMult);
                g_horizontalRamping = false;
                g_horizontalRampStartMs = 0;
            }
            else if (currentMult > 1.0f) {
                if (!inFlight || !notActuallyGrounded) {
                    // Flight ended or player touched ground — stop immediately
                    currentMult = 1.0f;
                    g_horizontalRampStartMs = 0;
                    g_horizontalRamping = false;
                }
                else {
                    // Ramp down (triggered on release, while still airborne in flight)
                    if (g_horizontalRamping || g_horizontalRampStartMs == 0) {
                        // Transition from ramp-up or hold — capture current value as start
                        g_horizontalRampStartMs = nowMs;
                        g_horizontalRampStartValue = currentMult;
                        g_horizontalRamping = false;
                    }
                    ULONGLONG elapsed = nowMs - g_horizontalRampStartMs;
                    if (g_horizontalRampDownMs > 0 && elapsed < (ULONGLONG)g_horizontalRampDownMs) {
                        float progress = (float)elapsed / (float)g_horizontalRampDownMs;
                        currentMult = g_horizontalRampStartValue - (g_horizontalRampStartValue - 1.0f) * progress;
                    }
                    else {
                        currentMult = 1.0f;
                        g_horizontalRampStartMs = 0;
                    }
                }
                g_currentHorizontalMult.store(currentMult);
            }
            else {
                // Reset state when at 1.0
                g_currentHorizontalMult.store(1.0f);
                g_horizontalRampStartMs = 0;
            }
        }

        // Smooth ramp for ascend boost
        {
            bool wantAscend = g_ascendBoostActive.load() && g_ascendEnabled;
            float targetVel = wantAscend ? g_ascendBoostValue : 0.0f;
            float currentVel = g_currentAscendVel.load();

            if (!allowVertical) {
                // Not in flight / wings retracted — cut off immediately
                currentVel = 0.0f;
                g_ascendRampStartMs = 0;
                g_ascendRamping = false;
            }
            else if (wantAscend && currentVel < targetVel) {
                // Ramp up toward target
                if (!g_ascendRamping) {
                    g_ascendRamping = true;
                    g_ascendRampStartMs = nowMs;
                    g_ascendRampStartValue = currentVel;
                }
                ULONGLONG elapsed = nowMs - g_ascendRampStartMs;
                if (g_ascendRampUpMs > 0 && elapsed < (ULONGLONG)g_ascendRampUpMs) {
                    float progress = (float)elapsed / (float)g_ascendRampUpMs;
                    currentVel = g_ascendRampStartValue + progress * (targetVel - g_ascendRampStartValue);
                }
                else {
                    currentVel = targetVel;
                }
            }
            else if (wantAscend) {
                // Holding at target
                currentVel = targetVel;
                g_ascendRamping = false;
                g_ascendRampStartMs = 0;
            }
            else if (currentVel != 0.0f) {
                // Ramp down toward 0
                if (g_ascendRamping || g_ascendRampStartMs == 0) {
                    g_ascendRampStartMs = nowMs;
                    g_ascendRampStartValue = currentVel;
                    g_ascendRamping = false;
                }
                ULONGLONG elapsed = nowMs - g_ascendRampStartMs;
                if (g_ascendRampDownMs > 0 && elapsed < (ULONGLONG)g_ascendRampDownMs) {
                    float progress = (float)elapsed / (float)g_ascendRampDownMs;
                    currentVel = g_ascendRampStartValue * (1.0f - progress);
                }
                else {
                    currentVel = 0.0f;
                    g_ascendRampStartMs = 0;
                }
            }
            else {
                g_ascendRampStartMs = 0;
            }

            if (std::fabs(currentVel) > 0.1f) g_vertBoostLastActiveMs = nowMs;
            g_currentAscendVel.store(currentVel);
        }

        // Smooth ramp for descend boost
        {
            bool wantDescend = g_descendBoostActive.load() && g_descendEnabled;
            float targetVel = wantDescend ? g_descendBoostValue : 0.0f;
            float currentVel = g_currentDescendVel.load();

            if (!allowVertical) {
                // Not in flight / wings retracted — cut off immediately
                currentVel = 0.0f;
                g_descendRampStartMs = 0;
                g_descendRamping = false;
            }
            else if (wantDescend && std::fabs(currentVel) < std::fabs(targetVel)) {
                // Ramp up toward target (negative direction)
                if (!g_descendRamping) {
                    g_descendRamping = true;
                    g_descendRampStartMs = nowMs;
                    g_descendRampStartValue = currentVel;
                }
                ULONGLONG elapsed = nowMs - g_descendRampStartMs;
                if (g_descendRampUpMs > 0 && elapsed < (ULONGLONG)g_descendRampUpMs) {
                    float progress = (float)elapsed / (float)g_descendRampUpMs;
                    currentVel = g_descendRampStartValue + progress * (targetVel - g_descendRampStartValue);
                }
                else {
                    currentVel = targetVel;
                }
            }
            else if (wantDescend) {
                // Holding at target
                currentVel = targetVel;
                g_descendRamping = false;
                g_descendRampStartMs = 0;
            }
            else if (currentVel != 0.0f) {
                // Ramp down toward 0
                if (g_descendRamping || g_descendRampStartMs == 0) {
                    g_descendRampStartMs = nowMs;
                    g_descendRampStartValue = currentVel;
                    g_descendRamping = false;
                }
                ULONGLONG elapsed = nowMs - g_descendRampStartMs;
                if (g_descendRampDownMs > 0 && elapsed < (ULONGLONG)g_descendRampDownMs) {
                    float progress = (float)elapsed / (float)g_descendRampDownMs;
                    currentVel = g_descendRampStartValue * (1.0f - progress);
                }
                else {
                    currentVel = 0.0f;
                    g_descendRampStartMs = 0;
                }
            }
            else {
                g_descendRampStartMs = 0;
            }

            if (std::fabs(currentVel) > 0.1f) g_vertBoostLastActiveMs = nowMs;
            g_currentDescendVel.store(currentVel);
        }

        // ---- AERIAL ROLL BOOST (B) - Activate on press ----
        if (g_aerialRollBoostEnabled && inFlight) {
            bool kbDown = g_aerialRollKey != 0 && (GetAsyncKeyState(g_aerialRollKey) & 0x8000) != 0;
            bool kbPressed = kbDown && !prevAerialKeyDown;
            bool btnPressed = hasController && (justPressed & g_aerialRollButton);

            if (btnPressed || kbPressed) {
                float prevMult = g_currentAerialMult.load();
                float targetMult = std::max(1.0f, g_aerialRollBoostValue);
                g_aerialRollBoostMultiplier.store(targetMult);
                g_aerialRollBoostEndTime = nowMs + (ULONGLONG)(g_aerialRollBoostDuration * 1000.0f);
                g_aerialRamping = true;
                // If already boosting, back-date the ramp start so progress continues from
                // currentMult rather than jumping back to 1.0 and ramping up from scratch.
                if (prevMult > 1.0f && targetMult > 1.0f) {
                    float progress = Clamp((prevMult - 1.0f) / (targetMult - 1.0f), 0.0f, 1.0f);
                    g_aerialRampStartMs = nowMs - (ULONGLONG)(progress * (float)g_aerialRollRampUpMs);
                }
                else {
                    g_aerialRampStartMs = nowMs;
                }
                Log(std::string(kModBaseName) + ": aerial roll boost activated!");
            }

            prevAerialKeyDown = kbDown;
        }
        else {
            prevAerialKeyDown = false;
        }

        // Smooth ramping for aerial roll boost
        // Always decay/clear aerial roll boost, even after landing.
        if (!inFlight) {
            g_aerialRollBoostMultiplier.store(1.0f);
            g_currentAerialMult.store(1.0f);
            g_aerialRollBoostEndTime = 0;
            g_aerialRamping = false;
        }
        else if (g_aerialRollBoostMultiplier.load() > 1.0f) {
            float targetMult = g_aerialRollBoostValue;
            float currentMult = g_currentAerialMult.load();

            // Check if boost duration expired
            if (g_aerialRollBoostEndTime == 0 || nowMs >= g_aerialRollBoostEndTime) {
                // Boost expired - ramp down
                if (g_aerialRamping) {
                    // Switching from ramp-up to ramp-down - capture current value as starting point
                    g_aerialRampStartMs = nowMs;
                    g_aerialRampStartValue = currentMult;
                    g_aerialRamping = false;
                }
                ULONGLONG elapsed = nowMs - g_aerialRampStartMs;
                if (g_aerialRollRampDownMs > 0 && elapsed < (ULONGLONG)g_aerialRollRampDownMs && currentMult > 1.0f) {
                    float progress = (float)elapsed / (float)g_aerialRollRampDownMs;
                    // Smoothly interpolate from START value down to 1.0
                    currentMult = g_aerialRampStartValue - (g_aerialRampStartValue - 1.0f) * progress;
                }
                else {
                    currentMult = 1.0f;
                    g_aerialRollBoostMultiplier.store(1.0f);
                    g_aerialRollBoostEndTime = 0;
                }
                g_currentAerialMult.store(currentMult);
            }
            else {
                // Boost active - ramp up to target
                ULONGLONG elapsed = nowMs - g_aerialRampStartMs;
                if (g_aerialRamping && g_aerialRollRampUpMs > 0 && elapsed < (ULONGLONG)g_aerialRollRampUpMs) {
                    float progress = (float)elapsed / (float)g_aerialRollRampUpMs;
                    currentMult = 1.0f + progress * (targetMult - 1.0f);
                }
                else {
                    currentMult = targetMult;
                }
                g_currentAerialMult.store(currentMult);
            }
        }
        else {
            // No boost active
            g_currentAerialMult.store(1.0f);
        }

        prevButtons = hasXInput ? xinputButtons : 0;
        prevDSButtons = hasDualSense ? dsButtons : 0;
    }
}

// ============================================================
//  ENTRY POINT
// ============================================================
static void ModMain() {
    Log(std::string(kModBaseName) + ": waiting for game to load...");
    Sleep(15000);

    // Initialize shared memory (we'll install the hook since we're flight mod)
    if (!g_sharedPlayerBase.Initialize(kModBaseName)) {
        Log(std::string(kModBaseName) + ": WARNING - cross-mod sync unavailable");
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    g_iniPath = std::string(exePath);
    g_iniPath = g_iniPath.substr(0, g_iniPath.rfind('\\')) + std::string("\\") + kModBaseName + ".ini";

    // Load all configuration
    ReloadIniValues();

    GetFileModTime(g_iniPath, &g_lastIniModTime);

    Log(std::string(kModBaseName) + ": config loaded (live reload enabled)");

    // Install vel hook.
    // Owner:    scan for the pattern, install, save address to shared memory.
    // Companion: wait for the owner to publish the address, then chain our own
    //            VelHookCallback at the same address via SafetyHook (which supports
    //            multiple callbacks at the same location).  This gives us a direct,
    //            always-fresh g_playerBase without relying on shared-memory polling.
    // ClaimVelHook() is independent of shared memory ownership — EnhancedFlight always wins
    // since ForcePalm never calls it. This prevents the race where ForcePalm loads first,
    // becomes the shared memory owner, and then EnhancedFlight (as companion) waits for a
    // velHookAddress that nobody will ever publish.
    if (g_sharedPlayerBase.ClaimVelHook()) {
        // 1.04.01: Hook a player pointer capture site instead of the velZ write.
        // At hook_offset +17 from primary match (or +13 from fallback), rax = player entity.
        static const uint8_t velPrimaryPattern[] = {
            0x49, 0x8B, 0x7D, 0x18,
            0x49, 0x8B, 0x44, 0x24, 0x40,
            0x48, 0x8B, 0x40, 0x68,
            0x48, 0x8B, 0x70, 0x20
        };
        static const char velPrimaryMask[] = "xxxxxxxxxxxxxxxxx";
        static const uint8_t velFallbackPattern[] = {
            0x49, 0x8B, 0x44, 0x24, 0x40,
            0x48, 0x8B, 0x40, 0x68,
            0x48, 0x8B, 0x70, 0x20
        };
        static const char velFallbackMask[] = "xxxxxxxxxxxxx";

        uint8_t* velLoc = PatternScan(velPrimaryPattern, velPrimaryMask, sizeof(velPrimaryPattern));
        int hookOffset = 17;
        if (!velLoc) {
            velLoc = PatternScan(velFallbackPattern, velFallbackMask, sizeof(velFallbackPattern));
            hookOffset = 13;
        }

        if (!velLoc) {
            Log(std::string(kModBaseName) + ": ERROR - game version not supported, flight detection inactive.");
        }
        else {
            velLoc += hookOffset;
            if (InstallVelHook(velLoc)) {
                g_sharedPlayerBase.SetHookAddress(reinterpret_cast<uintptr_t>(velLoc));
            }
        }
    }
    else {
        // Another EnhancedFlight instance already claimed the vel hook — chain at its address.
        uintptr_t hookAddr = 0;
        for (int i = 0; i < 100 && hookAddr == 0; i++) {
            Sleep(100);
            hookAddr = g_sharedPlayerBase.GetHookAddress();
        }

        if (hookAddr) {
            InstallVelHook(reinterpret_cast<uint8_t*>(hookAddr));
        }
        else {
            Log(std::string(kModBaseName) + ": WARNING - secondary detection unavailable");
        }
    }

    // Pos hook (horizontal / aerial / vertical speed).
    // ClaimPosHook() uses an interlocked flag in shared memory so only ONE mod instance
    // across ALL loaded DLLs (EnhancedFlight + ForcePalm) scans and patches the bytes.
    // The winner publishes the hook address; every other instance chains at that address.
    // This prevents the race where both mods scan the same bytes, one patches them first,
    // and the other's scan fails because it now sees a JMP trampoline instead of the pattern.
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
            Log(std::string(kModBaseName) + ": WARNING - speed boost unavailable, game version may not be supported.");
        }
    }
    else {
        // Another instance already installed the pos hook — chain our callback at the
        // shared address instead of scanning (bytes are now a JMP trampoline, not the pattern).
        uintptr_t posAddr = 0;
        for (int i = 0; i < 50 && posAddr == 0; i++) {
            Sleep(100);
            posAddr = g_sharedPlayerBase.GetPosHookAddress();
        }
        if (posAddr) {
            InstallPosHook(reinterpret_cast<uint8_t*>(posAddr));
        }
        else {
            Log(std::string(kModBaseName) + ": WARNING - speed boost unavailable, game version may not be supported.");
        }
    }

    // Initialize DualSense controller support
    DualSense_Initialize();

    Log(std::string(kModBaseName) + ": Ready!");
    InputLoop();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // Prevent double-initialization (DLL can be loaded multiple times)
        bool expected = false;
        if (!g_isInitialized.compare_exchange_strong(expected, true)) {
            // Already initialized - this is a subsequent DLL_PROCESS_ATTACH event, ignore it
            return TRUE;
        }

        DisableThreadLibraryCalls(hModule);
        std::thread(ModMain).detach();
    }
    return TRUE;
}