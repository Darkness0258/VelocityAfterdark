# Validation strategy

## Layers of evidence

1. Portable C++ tests verify numerical vehicle math and race rules. They do not compile Unreal classes or exercise Chaos.
2. Source/data checks parse project text, JSON, editor scripts and wiring. They do not prove Unreal APIs compile or gameplay feels correct.
3. UnrealBuildTool/UHT and Editor automation verify compilation, data loading and the runtime behaviors covered by each test.
4. Cooked-game runs verify packaged assets and rendering. Human handling/art/audio review, physical devices, network fault testing and performance profiling remain separate acceptance gates.

The latest workspace evidence at this audit is `Artifacts/UnrealTests/index.json`: 56 Unreal Editor cases passed, with 0 warnings, failures or not-run cases. The suite includes a live career race reward-retry path through actual keyboard Enter input and reload, in addition to its isolated transaction and race fixtures. The corresponding source checks passed 157 project text files, `Test-Core.ps1` passed 971 numerical checks, and `Test-RaceRules.ps1` passed 401 race-rule checks. Read `Docs/VALIDATION.md` for the matching cooked-package report and limits; do not combine reports from different builds as if they were one test run.

## Phase 1 manual matrix

| Test | Procedure | Pass condition |
|---|---|---|
| Cold boot | Build, bootstrap, open the root project, Play, then start driving | Car, city and HUD appear without missing-content or data errors |
| Launch | Full throttle on a clear straight, five repetitions | Car moves forward; gears/RPM respond; no oscillation or airborne launch |
| Braking | Brake from 100 km/h, then hold at rest | Speed falls without powered reverse or persistent drift |
| Steering | Slalom at 30, 80 and 150 km/h, then release steering | Responsive at low speed; bounded yaw at high speed; recoverable traction |
| Frame rate | Repeat fixed inputs at 30, 60 and 120 Hz | 0–100 and stopping distance within 5%; no unstable suspension |
| Low FPS | Test at 15 FPS and after a 100 ms hitch | No NaN/explosion/stuck input; document any supported-envelope deviation |
| Suspension | Hit a curb and diagonal bump; settle after a drop | Stable wheel contact and recovery; no persistent bounce or tunneling |
| Collision | Wall, glancing barrier and head-on impacts at several speeds | Stable response; recovery works; no map escape exploit |
| Recovery | Flip or drop below the district, then request recovery | Safe upright spawn, zeroed velocities, cleared controls and wheel state |
| Manual/reverse | Shift at limits; request reverse while moving and at rest | Gear bounds hold; no sudden direction reversal at speed |
| Camera | Chase near obstacles, hood and cockpit views | Usable framing with no persistent clipping or excessive FOV |
| Inputs | Keyboard, analog triggers/stick, and device switching | Progressive analog values, no stale input, both keyboard directions work |
| Disconnect | Hold accelerator, unplug and reconnect a pad | Throttle clears and can be reacquired safely |
| Focus/pause | Hold controls, Alt-Tab, pause and resume | No retained throttle/steer; audio pauses and resumes correctly |
| Audio | Idle, full load, shifts, coast and repeated pause | Continuous RPM/load response; no clicks, clipping or orphan audio |
| Display | 1280×720, 1920×1080, ultrawide and resize | HUD stays in safe areas; speed/gear remain readable |
| Performance | Warm route on named baseline hardware/preset | Capture CPU/GPU frame times and evaluate the 60 FPS target honestly |
| Package | Cook Windows Development and launch outside the editor | JSON, generated/imported content and runtime meshes load correctly |
| Content error | Corrupt or remove required vehicle/district JSON | Visible diagnostic; no uncontrolled simulation or silent success |

## Cross-system regressions

Phase 2 race automation checks directed ordered gates, skipped-gate rejection, timing, restart and physical AI finishes. Later tests cover ownership transactions and migrations, map projection and waypoint persistence, discovery rewards, career progression, effective upgrades, input/settings changes, world systems, photo/replay return state, and regional races. The precise assertion list belongs in `Docs/CONSOLIDATED_TEST_PLAN.md` and the actual results belong in the run report.

Manual or extended acceptance still includes steering feel, suspension across frame rates, player-obstructed traffic queues, police tactics beyond pursuit/search, weather persistence and visibility, safe full-speed streaming, complete career save/relaunch and reward retry paths, every settings option, visual map layout at device resolutions, physical controller/wheel disconnects, full-scene replay quality, external-network faults, audio listening, and long-run memory/performance behavior.

Store fresh output under `Artifacts/`. A validation record must name the tested source/package, command, exit state, report path, and exact limits. A clean source scan or editor test is not packaged-device acceptance; a package-only smoke is not full feature acceptance. Never call the whole roadmap complete while any delivery gate in `Docs/MILESTONES.md` remains open.
