# Gameplay Architecture

## Frame phases

Gameplay updates use explicit phases in `Engine::Update`:

1. Orbiting body transforms
2. Player, spacecraft, camera, and audio input
3. Sector selection and gravity forces
4. PhysX simulation
5. Sector and spacecraft post-physics synchronization
6. Orbit analysis and objective state transitions
7. UI snapshot, diagnostics, and rendering

Systems that inspect movement outcomes belong after physics. UI code must not
mutate gameplay components or evaluate mission rules.

## Scene state

`components::GameState` is stored in `entt::registry::ctx()`. It owns state that
has exactly one value per scene, including the active objective and the bounded
objective event history. Do not create a synthetic singleton entity for this
data.

Objectives are normal ECS entities with `ObjectiveComponent`. Feature systems
create objectives and write progress; `ObjectiveSystem` clamps values, performs
status transitions, selects the active HUD objective, publishes events, and
records diagnostics. Objective IDs are scene-unique; creating an existing ID
returns its current entity.

Mission-specific runtime data belongs in a separate component. For example,
`OrbitNavigationComponent` owns trajectory samples and accumulated orbital
angle. It does not add orbit fields to the generic objective component.

## Gravity and navigation

`GravityEvaluator` is the shared central-field implementation. Runtime force
application and trajectory prediction must both call it so a displayed path is
based on the same gravity law as PhysX.

`OrbitNavigationSystem` runs after physics. It calculates altitude, radial and
tangential speed, numerically predicts 60 seconds of flight, classifies the path
as impact, escape, ballistic, or stable, and advances the orbit objective only
while the predicted path remains inside the local sector without intersecting
the body.

`SolarMapState` is the scene-level read model for the navigation map. It holds
live celestial-body positions, sampled body orbits, the spacecraft position,
and the predicted spacecraft path in world space. The UI copies this state and
never queries PhysX or advances objective progress.

Press `M` to toggle the map. Drag with the left mouse button to rotate, use the
wheel to zoom, and drag with the right mouse button to pan. Clicking selects a
body; double-clicking focuses it. Double-clicking the right mouse button resets
the view to the whole solar system. Press `Tab` to switch between Sun-centered
and spacecraft-centered framing. Predicted flight is rendered as a spatial
dashed line using the same world-space samples as orbit analysis.

`SolarMapCameraSystem` owns a separate camera entity. Player, spacecraft, and
free-camera systems continue synchronizing their normal cameras while the map
is open. `RenderSystem` temporarily selects the map camera during the animated
transition, then returns to the still-current gameplay camera. The map system
must never overwrite a gameplay camera's pose or lens settings.

`NavigationTargetState` is the scene-level owner of the current reticle
candidate, locked celestial body, target distance, and relative velocity.
`NavigationTargetSystem` selects candidates near the gameplay camera center,
locks or clears them with the left mouse button, and converts target world velocity into the
spacecraft's current sector frame. A map click submits a lock request to this
same state; UI code must not maintain an independent gameplay target.

While piloting, hold `Space` to reduce velocity relative to the locked body to
zero, matching the original game's behavior. This is not circular-orbit
insertion: gravity begins creating relative velocity again as soon as matching
stops. Matching uses a capped PhysX acceleration rather than setting rigid-body
velocity directly.
The target HUD renders a single bracket for the current candidate and double
brackets for the locked body. Child-body world velocity must include its
parent body's world velocity, as with the Moon orbiting Earth.

## Adding an objective

1. Put feature-specific measurements in a dedicated component and system.
2. Create the generic objective with `ObjectiveSystem::CreateObjective`.
3. Activate it with `ObjectiveSystem::Activate`.
4. Report numeric progress with `ObjectiveSystem::SetProgress`, or use
   `Complete` and `Fail` for discrete outcomes.
5. React to `GameState::objectiveEvents` from a game-flow system when chained
   objectives or rewards are needed.

Do not place objective conditions in `main.cpp` or `UISystem`.

## Regression tests

```powershell
.\scripts\test-orbit.ps1 -SkipBuild
.\scripts\test-map-camera.ps1 -SkipBuild
.\scripts\test-velocity-match.ps1 -SkipBuild
.\scripts\test-landing.ps1 -SkipBuild
.\scripts\test-sector-transition.ps1 -Scenario EarthExit -SkipBuild
.\scripts\test-sector-transition.ps1 -Scenario MoonExit -SkipBuild
.\scripts\test-sector-transition.ps1 -Scenario MarsArrival -SkipBuild
.\scripts\test-sector-transition.ps1 -Scenario SaturnArrival -SkipBuild
.\scripts\test-diagnostics.ps1 -SkipBuild
```
