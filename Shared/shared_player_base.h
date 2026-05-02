// shared_player_base.h
// Shared memory interface for player base pointer
// Allows multiple mods to share the same velocity hook without conflicts

#pragma once
#include <windows.h>
#include <cstdint>

// Shared memory name (unique identifier for this game)
constexpr auto SHARED_MEMORY_NAME = "CrimsonDesert_PlayerBase_SharedMem_Bambozu";

// Shared data structure
#pragma pack(push, 1)
struct SharedPlayerData {
    uintptr_t playerBase;           // Player object base pointer
    ULONGLONG lastUpdateTick;       // GetTickCount64() when last updated
    char      hookOwnerName[64];    // Name of mod that installed the hook
    DWORD     version;              // Structure version (for compatibility)
    uintptr_t velHookAddress;       // Address where the vel hook was installed
    // Companion mods read this to chain their own callback
    LONG      posHookInstalled;     // Interlocked flag: 1 once pos hook is installed by any instance
    uintptr_t posHookAddress;       // Address where the pos hook was installed (published by winner of ClaimPosHook)
    LONG      velHookInstalled;     // Interlocked flag: 1 once vel hook is installed (EnhancedFlight always wins)
};
#pragma pack(pop)

// Helper class to manage shared memory
class SharedPlayerBase {
private:
    HANDLE hMapFile;
    SharedPlayerData* pData;
    bool isOwner;

public:
    SharedPlayerBase() : hMapFile(NULL), pData(nullptr), isOwner(false) {}

    ~SharedPlayerBase() {
        if (pData) {
            UnmapViewOfFile(pData);
            pData = nullptr;
        }
        if (hMapFile) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
        }
    }

    // Initialize shared memory (returns true if we created it, false if already exists)
    bool Initialize(const char* modName) {
        // Try to open existing shared memory first
        hMapFile = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            SHARED_MEMORY_NAME
        );

        if (hMapFile == NULL) {
            // Doesn't exist yet — try to create it.
            hMapFile = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0,
                sizeof(SharedPlayerData),
                SHARED_MEMORY_NAME
            );

            if (hMapFile == NULL) {
                return false;
            }

            // IMPORTANT: capture GetLastError() IMMEDIATELY after CreateFileMappingA.
            // If two mods race through the OpenFileMappingA == NULL path and both call
            // CreateFileMappingA, Windows still returns a valid handle to the loser but
            // also sets ERROR_ALREADY_EXISTS.  Without this check both mods would think
            // they are the hook owner.
            isOwner = (GetLastError() != ERROR_ALREADY_EXISTS);
        }
        else {
            // Already exists (another mod installed the hook)
            isOwner = false;
        }

        // Map the shared memory
        pData = (SharedPlayerData*)MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(SharedPlayerData)
        );

        if (pData == NULL) {
            CloseHandle(hMapFile);
            hMapFile = NULL;
            return false;
        }

        // If we're the owner, initialize the structure
        if (isOwner) {
            memset(pData, 0, sizeof(SharedPlayerData));
            pData->version = 1;
            strncpy_s(pData->hookOwnerName, modName, sizeof(pData->hookOwnerName) - 1);
        }

        return true;
    }

    // Update player base (should only be called by hook owner)
    void UpdatePlayerBase(uintptr_t playerBase) {
        if (pData) {
            pData->playerBase = playerBase;
            pData->lastUpdateTick = GetTickCount64();
        }
    }

    // Get player base (can be called by any mod)
    uintptr_t GetPlayerBase() {
        if (pData) {
            // Check if data is fresh (updated within last 100ms)
            ULONGLONG now = GetTickCount64();
            if ((now - pData->lastUpdateTick) < 100) {
                return pData->playerBase;
            }
        }
        return 0;
    }

    // Store the address where the vel hook was installed (call after InstallVelHook succeeds)
    void SetHookAddress(uintptr_t addr) {
        if (pData) {
            pData->velHookAddress = addr;
        }
    }

    // Read the vel hook address written by the owner (returns 0 while owner hasn't set it yet)
    uintptr_t GetHookAddress() const {
        return pData ? pData->velHookAddress : 0;
    }

    // Atomically claim the pos hook install slot.
    // Returns true if THIS call is the one that should install (first caller wins).
    bool ClaimPosHook() {
        if (!pData) return false;
        return InterlockedCompareExchange(&pData->posHookInstalled, 1L, 0L) == 0L;
    }

    // Atomically claim the vel hook install slot.
    // Independent of shared memory ownership — EnhancedFlight always wins since ForcePalm never calls this.
    bool ClaimVelHook() {
        if (!pData) return false;
        return InterlockedCompareExchange(&pData->velHookInstalled, 1L, 0L) == 0L;
    }

    // Publish the address where the pos hook was installed (call after ClaimPosHook + install).
    void SetPosHookAddress(uintptr_t addr) {
        if (pData) pData->posHookAddress = addr;
    }

    // Read the pos hook address written by the installer (returns 0 while not yet set).
    uintptr_t GetPosHookAddress() const {
        return pData ? pData->posHookAddress : 0;
    }

    // Check if this mod is the hook owner
    bool IsHookOwner() const {
        return isOwner;
    }

    // Get the name of the mod that owns the hook
    const char* GetHookOwnerName() const {
        if (pData) {
            return pData->hookOwnerName;
        }
        return "Unknown";
    }
};
