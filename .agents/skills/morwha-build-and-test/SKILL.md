---
name: morwha-build-and-test
description: >-
  Build MORWHA.dll and compile/run morwha_tests.exe headless C++ unit tests without path-hunting.
  Activate whenever compiling C++, running C++ unit tests, or editing SKSE_Source/CMakeLists.txt.
---

# M.O.R.W.H.A Build & Test Runbook

Never search the filesystem for `cmake.exe`, Visual Studio, `vcpkg`, or `CommonLibSSE`. Run from the workspace root (`E:\Skyrim\development\mods\MORWHA Dev`):

```powershell
# Full verification: header-hygiene check + build morwha_tests + build MORWHA.dll + run all tests
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all

# Fast inner loop (~1-2s): pure-module header check + build & run morwha_tests.exe ONLY
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target tests

# Filtered unit test run
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target tests -Filter "CoSaveCodec"

# Force CMake reconfigure (required after editing SKSE_Source/CMakeLists.txt or adding .cpp files)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all -Reconfigure
```

## Locked Target DLL Behavior

When `SkyrimSE.exe` is running, Windows locks `SKSE/Plugins/MORWHA.dll`:
- `MORWHA.dll` still compiles and links cleanly at `SKSE_Source/build/release/Release/MORWHA.dll`.
- Only the `POST_BUILD` copy step fails; `build-and-test.ps1` catches this automatically, emits `[WARN]`, and exits `0`. Never retry in a loop or kill `SkyrimSE.exe`.
