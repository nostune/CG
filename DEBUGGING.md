# Debugging OuterWildsECS

The diagnostics stack separates three kinds of information:

- Logs describe discrete events and failures. They are retained in a bounded
  in-memory buffer and written to a timestamped file under `logs/`.
- Metrics describe values that change continuously, such as frame time and
  PhysX actor counts. Systems should publish these instead of logging every
  frame.
- Spatial diagnostics show geometry and relationships. PVD is the external
  PhysX view; built-in PhysX debug geometry is collected separately for a
  future in-game viewport renderer.

## Diagnostics Panel

Press `F1` at runtime or launch with:

```powershell
.\scripts\run.ps1 -Diagnostics
```

The panel provides Overview, Logs, and Physics tabs. Logs can be filtered by
severity and text without changing the session log file.

Open Diagnostics, enable PhysX debug geometry, and show the independent virtual
physics-space viewport in one command:

```powershell
.\scripts\run.ps1 -PhysXDebug
```

Add `-SkipWelcome` when a reproduction should enter the simulation immediately.

For the normal development loop, double-click `START_DEBUG.cmd` in the project
root. It performs an incremental Debug build, skips the welcome screen, opens
Diagnostics, and enables throttled contact samples. PhysX Space stays closed
until it is explicitly enabled from the Physics tab.

## Automated Smoke Test

Use the real engine without UI automation. The test hides the window, skips the
welcome screen, enables PhysX debug collection, runs three simulation seconds,
shuts down normally, and validates the session log:

```powershell
.\scripts\test-diagnostics.ps1
.\scripts\test-diagnostics.ps1 -SkipBuild -ProbePvd
```

The test fails when the main loop, non-empty PhysX debug geometry, clean
shutdown, or log file is missing, or when an Error/Critical entry is present.

Sector transitions use the same real-scene test path. The scenarios verify
coordinate and velocity frame changes as well as collision-shape activation:

```powershell
.\scripts\test-sector-transition.ps1 -Scenario EarthExit
.\scripts\test-sector-transition.ps1 -Scenario MoonExit
.\scripts\test-sector-transition.ps1 -Scenario SaturnArrival
.\scripts\test-sector-transition.ps1 -Scenario MarsArrival
```

High-speed landing has its own deterministic regression. It drops the
spacecraft radially toward Earth at 25 m/s and requires a real contact event,
landing-assist engagement, stable sleep, and clean shutdown:

```powershell
.\scripts\test-landing.ps1
.\scripts\test-landing.ps1 -SkipBuild
```

The landing path has three phases. PhysX and CCD own the initial impact. Once
contact speed is manageable, the assist uses acceleration-mode tangent damping
and orientation PD torque without overriding pilot input. Sustained low-speed
contact with no input performs one sleep transition; custom gravity does not
wake sleeping actors.

## Console

The console is disabled by default because legacy systems still emit noisy
`stdout` messages. Enable it only when needed:

```powershell
.\scripts\run.ps1 -Console
```

New code should use `DebugManager` and publish repeated numeric state as a
metric. Direct `std::cout`, `printf`, and per-frame logging are migration debt.

## Contact Diagnostics

The Physics tab can enable detailed contact samples. Contact metrics are always
available in Overview: pair count, point count, maximum impulse, and minimum
separation. Detailed samples are limited to four per second and include actor
names, a representative point, its normal, impulse, and separation.

When built-in visualization is active, PhysX Space also renders contact points
and normals. Run the automated contact path with:

```powershell
.\scripts\test-diagnostics.ps1 -SkipBuild -ContactDebug
```

## PhysX Visual Debugger

The current integration exposes the legacy `PxPvd` socket path. Its viewer is a
separate NVIDIA application listening on `127.0.0.1:5425`. Start the receiver
before enabling the connection in the Physics tab, or request a connection
during PhysX startup:

```powershell
.\scripts\run.ps1 -Diagnostics -Pvd
```

Failure to find a receiver is non-fatal and is recorded as a warning. The local
machine currently has the PhysX PVD SDK library but not the external viewer.

For PhysX 5, NVIDIA recommends OmniPVD recordings for deep offline inspection.
For fast iteration inside this game, the preferred next step is rendering the
already collected `PxRenderBuffer` primitives over the DX11 game viewport.
