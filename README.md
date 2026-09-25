# VELOCITY: AFTERDARK

An Unreal Engine 5.8 C++ racing prototype set in Nova City. The current source includes five data-driven original vehicles, eight career circuits, a transactional garage, regional exploration, weather, traffic and police pursuits. A separately licensed static background car dresses the playable avenue. Its fresh Win64 package passed 45 Unreal automation cases, startup/race/garage checks, a four-size viewport matrix and a loopback host/client driving smoke. This is not the complete AAA game; see the dated validation record for evidence and limits.

The main project is **`VelocityAfterdark.uproject` in this directory**. A separate `MyProject/` folder, if present, is preserved and is not used by these scripts.

## Start

Requires Unreal Engine 5.8 with C++ development components and a supported Windows C++ compiler/SDK. Use PowerShell in this directory:

```powershell
.\Scripts\Invoke-Afterdark.ps1 -Action Check
.\Scripts\Invoke-Afterdark.ps1 -Action Bootstrap
.\Scripts\Invoke-Afterdark.ps1 -Action Open
```

Pass `-EngineRoot 'C:\path\to\UE_5.8'` if needed, or set `UE_ROOT`. Bootstrap compiles the editor module, creates missing materials and the Dockside map, and saves them. It also repairs required material instancing flags while preserving existing art properties. Do not skip Bootstrap on a fresh checkout. In the editor, open `/Game/Velocity/Maps/L_Dockside`, press **Play**, then **Enter** to drive.

## Controls

| Action | Keyboard |
|---|---|
| Throttle / brake | W / S |
| Steer | A / D |
| Handbrake | Space |
| Chase / hood / cockpit | C |
| Recover upright at start | R |
| Automatic / manual | M |
| Shift down / up | Q / E |
| Select reverse / drive at rest | V |
| Start / resume | Enter |
| Pause / resume | Escape |
| Start / restart Dockside Circuit | F / gamepad B |
| Race difficulty (outside an active race) | Tab / D-pad right |
| Restart from results | Enter / gamepad A |
| Leave race for free driving | Backspace / D-pad left |
| Garage (stop first; finish/leave any race or pursuit) | G / left-stick click |
| Career chapter | N / LB+B |
| Pursuit challenge | P / right-stick click |
| Nitrous | Left Shift / gamepad A |
| Settings | F10 |
| Photo mode / vehicle replay | F2 / F4 |
| Exploration map (offline free drive) | F5 / D-pad left |

In the garage, use Up/Down to select, Left/Right or Q/E to change, and Enter to confirm. Save Changes purchases and fits the draft; Escape discards unsaved edits. J/L orbit, I/K adjust elevation, and the mouse wheel zooms. Purchased parts remain owned when removed; re-equipping is free. Saving also repairs basic collision damage. The VEHICLE row previews the Aster S6 coupe (RWD), Kestrel XR hatchback (FWD, 8,500 CR) and Meridian GT sedan (AWD, 24,000 CR). Save buys and equips an unowned car; each owned car retains its own paint, upgrades and tune. These are procedural development models with different physics configurations. Controller navigation uses D-pad Up/Down, LB/RB, A and B; right stick orbits.

Photo mode uses WASD to move, Q/E vertically, arrows to look, Z/X for focal length, and [ / ] for exposure. Controller sticks move/look and triggers move vertically. F8 requests a PNG in `Saved/Photos`; H hides the HUD. Replay stores up to 60 seconds of the player's latest continuous driving segment, with three camera views, scrubbing, pause and slow playback. Recovery, garage entry, invalid motion and long gaps cut the recording. It does not record the whole traffic/race scene or recorded engine audio. Escape restores the exact live vehicle pose and velocity.

The offline exploration map pauses free driving. Up/Down selects a landmark, Left/Right filters all/undiscovered/discovered, Enter/A sets its road route, and Escape/F5 returns. Clicking a marker or list item also sets a route; Backspace returns to nearest-landmark guidance. D-pad left opens/closes the map in free drive and remains the leave-race control during a race. Eight catalog locations grant credits and REP once after a grounded arrival below 40 km/h; returning to a discovered location does not pay again. Routes follow a cached planar road graph. Fast travel, arbitrary map pins, zoom/pan and online map support remain future work.

F10 opens settings. DRIVING CONTROLS supports keyboard and controller rebinding for the main driving actions. Select a control, press its replacement input, then return to APPLY AND SAVE. Compatible conflicts swap; contextual menu/activity shortcuts remain reserved. Escape discards unapplied changes.

Gamepad mappings appear in the in-game controls. Physical device acceptance is tracked separately from code support.

Dockside Circuit has two laps, seven directed gates and three physical opponents. Cross the green gate in the indicated direction; the minimap tracks the route and all four cars. Recovery during a race returns to the last validated section, adds five seconds and invalidates that lap for best-lap records. Results remain provisional while opponents are finishing. Difficulty changes opponent pace within the same vehicle physics.

## Build and verification

```powershell
python Scripts/validate_source.py
.\Scripts\Test-Core.ps1
.\Scripts\Test-RaceRules.ps1
.\Scripts\Invoke-Afterdark.ps1 -Action Build
.\Scripts\Invoke-Afterdark.ps1 -Action Test
.\Scripts\Invoke-Afterdark.ps1 -Action Render
.\Scripts\Invoke-Afterdark.ps1 -Action Package
.\Scripts\Smoke-Package.ps1
.\Scripts\Smoke-Race.ps1
.\Scripts\Smoke-Garage.ps1
.\Scripts\Benchmark-Afterdark.ps1
```

Reports and build logs go to `Artifacts/`. See [validation evidence](Docs/VALIDATION.md) for current pass results and remaining acceptance, and [consolidated test plan](Docs/CONSOLIDATED_TEST_PLAN.md) for uncovered tests. Packaging records source/content/package hashes in `Artifacts/build-proof.json` and `Artifacts/integrated-source-manifest.json`; `phase2-build-proof.json` preserves older evidence. A build record does not certify driving quality or full-world GPU performance.

`Render` runs a hidden DX11 game instance, lets the scene warm up, captures the title and three driving cameras, then exits. Inspect `Artifacts/Render/`; the existence of images is not an art-quality pass. The sequence also records a short stationary CSV sample under `Saved/Profiling/CSV`. `Package` writes a Windows Development build under `Artifacts/Package/Windows`; launch its `VelocityAfterdark.exe` after a successful cook. `Smoke-Package.ps1` starts the living world with an isolated profile, copies four fresh captures to `Artifacts/PackageSmoke`, and requires cooked district/vehicle startup, active rain, and a clean exit.

`Benchmark-Afterdark.ps1` drives a data-defined Dockside loop through normal throttle, brake and steering controls. It warms up for 20 seconds, captures 120 seconds without screenshots, and exports driving and frame-time reports to `Artifacts/DrivingBenchmarkPackage`. Add `-IncludeEnvironment` to run with a fresh profile and the living world/rain enabled; this mode fails if the rain emitter does not activate. Use `-Editor` to test a freshly built editor module before cooking. This is a QA driver, not opponent racing AI; a successful capture does not itself mean the 60 FPS performance target passed.

`Smoke-Race.ps1` runs a rendered two-lap race with three opponents and an automated QA player using the same physical driver. It checks legal finishes, results and lap timings, captures the grid/results, and measures 120 seconds with all four cars. Use `-Editor` before cooking. Reports are in `Artifacts/RaceSmoke` (cooked) or `Artifacts/RaceSmokeEditor`. Normal play keeps the player under your control.

## Design and scope

- [Architecture and full-system design](Docs/ARCHITECTURE.md)
- [Connected systems and current implementation contracts](Docs/INTEGRATED_SYSTEMS.md)
- [Milestones and all 72 specification sections](Docs/MILESTONES.md)
- [Asset requirements and performance strategy](Docs/ASSETS_AND_PERFORMANCE.md)
- [Testing strategy](Docs/TESTING.md)

The integrated prototype adds a rendered garage, six paint colors, four effective upgrades, three gearing presets, version 5 per-vehicle ownership/world saves, time/weather/wetness, bounded physical traffic/signals, Dockside police/search, streamed regional development scenery, an exploration map and eight discoveries, an eight-chapter career on eight distinct circuits, settings, nitrous, basic damage, photo mode, vehicle replay and optional direct-connect free roam. See [current implementation status](Docs/IMPLEMENTATION_QUEUE.md) for exact scope and remaining work. Automated tests cover selected behavior; source presence and compilation alone do not establish gameplay or performance acceptance.

The regional roads are flat development geometry, including the foothills. Production car/environment art, recorded audio, advanced vehicle systems, a full dealership world and starter-selection sequence, deep customization, complete accessibility/remapping, advanced police tactics and competitive online races remain unfinished.

## Saves

Normal offline play stores `Saved/Profiles/driver.json`, with a last-good `.bak` and verified temporary replacement. Version 1–4 profiles migrate on their next save, retaining the original starter car build. Version 5 stores the owned vehicle collection and each car's configuration. Invalid/future-version files are preserved and saving is blocked; corruption can recover from a valid backup. Windows uses an exclusive write lock and atomic replacement. Checksums detect accidental corruption and are not anti-cheat. Editor/automation profiles are session-only unless `-AfterdarkProfile="C:\absolute\path\driver.json"` is supplied. Automated garage smoke uses a unique isolated path.

Garage, career and discovery transactions also save the latest offline clock, weather sequence/timer, rain/fog transition and road wetness. World autosaves wait for a stopped free-drive car and at least 60 seconds; normal world teardown attempts an exit save. These writes are currently synchronous and require later hitch/failure profiling. Abrupt termination can lose world changes since the last successful save. This is not a full world/entity snapshot or a saved player position.

## Optional direct-connect free roam

Development console commands: `ADHost`, `ADJoin 127.0.0.1:7777`, and `ADDisconnect`. A host accepts up to four players. Vehicle inputs are sent to authority; clients do not set force, money, unlocks or race results. Input loss applies braking after a timeout. The mode uses stock cars and disables career rewards, garage purchases and competitive race entry. It has no matchmaking, public lobby directory, account service or competitive result backend. Network runtime, latency and disconnect validation remain deferred.
