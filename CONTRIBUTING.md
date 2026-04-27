# Contributing to Enhanced Flight & Mobility Mods

Thanks for your interest in contributing. These mods are C++ ASI plugins for Crimson Desert. Contributions of all kinds are welcome — bug fixes, offset updates after game patches, new features, and documentation improvements.

---

## Getting Started

### Prerequisites

- A C++ compiler targeting x64 Windows (Visual Studio 2022 recommended)
- Windows SDK
- Crimson Desert installed (for in-game testing)
- Basic familiarity with C++, memory manipulation, and/or game modding

### Setup

```bash
git clone https://github.com/Ronibuilds/CrimsonDesertMods.git
```

Set up your own C++ DLL project, add the relevant source files, and link against the required libraries. Rename the output `.dll` to `.asi`, place it in `Crimson Desert\bin64\` alongside the `.ini`, and test in-game.

---

## Project Structure

```
src/
  dllmain.cpp               # Enhanced Flight
  forcepalm_v2.cpp          # Enhanced Force Palm
  shared_player_base.h      # Shared memory interface (Enhanced Flight + Enhanced Force Palm only)
  SuperAxiomForce/
    SuperAxiomForce.cpp     # Super Axiom Force (standalone, no shared state)
```

Enhanced Flight and Enhanced Force Palm coordinate through `shared_player_base.h`. Super Axiom Force is completely independent.

---

## Types of Contributions

### Offset Updates (Most Common)

Game patches frequently change the memory layout, breaking the pattern scans used to locate hook points. If a mod logs "pattern not found" after an update, the AOB patterns or offsets need updating.

To investigate:
1. Use a disassembler (Ghidra, x64dbg, IDA) to locate the new function
2. Compare against the patterns in `dllmain.cpp` (`g_velHookPattern`, `g_posHookPattern`) or the relevant source file
3. Update the pattern bytes and/or hook offsets
4. Test in-game and confirm the log shows "hook installed" with no errors

### Bug Fixes

Check the issue tracker for open bugs. Attach the relevant `.log` file from `bin64` when reporting a bug — it contains detailed diagnostics.

### New Features

Open an issue first to discuss the idea before writing code. This avoids duplicate work and lets us agree on design before implementation.

### Documentation

Fixing typos, clarifying config options, adding examples, and keeping changelogs up to date are all appreciated.

---

## Code Guidelines

- **C++17**, no exceptions in hot paths (use return codes or flags instead)
- The input loop runs on a background thread — all shared state must be accessed atomically or under a lock
- Pattern scanning is the only way to locate hook addresses — never hardcode absolute addresses
- Always check `g_playerBase` for null before dereferencing
- Add a log line (`WriteLog`) for any new hook installation or critical state change — this is how users diagnose issues
- Keep INI keys backward-compatible where possible; if a key must change, document the migration in the changelog

---

## Pull Request Process

1. Fork the repository and create a branch from `main`:
   ```bash
   git checkout -b fix/axiom-offset-update
   ```
2. Make your changes and test in-game
3. Update the relevant `docs/` README if behavior changed
4. Open a pull request with:
   - A clear description of what changed and why
   - The game version you tested against
   - The relevant log snippet confirming the fix works

PRs that include updated offsets should note the game build version (visible in the launcher or `bin64` EXE properties).

---

## Reporting Issues

Use the [GitHub Issues](https://github.com/Ronibuilds/CrimsonDesertMods/issues) tab. Please include:

- Which mod is affected
- Game version (patch number)
- The full contents of the relevant `.log` file from `bin64`
- What you expected vs. what happened

---

## Architecture Notes

### Shared Memory (Enhanced Flight + Enhanced Force Palm only)

Enhanced Flight and Enhanced Force Palm share a named memory segment defined in `shared_player_base.h`. Enhanced Flight installs the hooks and publishes the player base pointer; Enhanced Force Palm reads it. Use `ClaimVelHook` / `ClaimPosHook` from `shared_player_base.h` to coordinate hook ownership — do not manually install hooks at an address already owned by the other mod.

Super Axiom Force does not participate in this system and has no shared state with the other mods.

### Pattern Scanning

`dllmain.cpp` contains `ScanPattern()`. Patterns are byte arrays with `0xFF` as a wildcard. When updating patterns after a game patch, keep both the primary and fallback patterns if possible for wider compatibility.

### Physics Timing

`QueryPerformanceCounter` is used for sub-millisecond delta time. The position hook fires once per physics substep per frame — ensure any injected velocity or position delta is scaled correctly by `dt`.
