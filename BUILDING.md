# Building OuterWildsECS

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.25 or newer
- A bootstrapped vcpkg checkout

Set `VCPKG_ROOT` to the vcpkg checkout before configuring:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

The dependency versions are described by `vcpkg.json`. Assimp and ImGui are
pinned to the versions used by the existing game code. CMake installs missing
packages into the preset build tree during the first configuration.

## Configure And Build

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -Configuration Release
```

To discard only generated files under `out/build/windows-x64` and configure
again:

```powershell
.\scripts\build.ps1 -Clean
```

To configure without compiling:

```powershell
.\scripts\build.ps1 -ConfigureOnly
```

## Run

The executable locates the project root from `OUTERWILDS_ROOT`, the current
working directory, or its own location. The run script also verifies the
required local asset set before launch:

```powershell
.\scripts\run.ps1
.\scripts\run.ps1 -Configuration Release
```

Use `-Diagnostics` to open the in-game diagnostics panel, `-PhysXDebug` to open
the independent virtual physics-space viewport, `-Pvd` to request a PhysX
Visual Debugger connection at startup, and `-Console` to enable the legacy
console. See `DEBUGGING.md` for the intended development workflow.

See `ASSETS.md` when the asset check reports missing files. To run a diagnostics
executable that does not need the main scene assets, pass `-SkipAssetCheck`.

Visual Studio generators also receive the repository root as the debugger
working directory through CMake target properties.

## Build Targets

- `OuterWildsEngine`: reusable static library containing engine and game systems
- `OuterWildsECS`: current game executable and scene bootstrap

New diagnostics and tests should link `OuterWilds::Engine` instead of compiling
engine sources again. This is the intended boundary for the upcoming PhysicsLab.
