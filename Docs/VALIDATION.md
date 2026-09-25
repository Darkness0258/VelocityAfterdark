# Validation evidence

## Visual presentation package — 2026-09-26

The current Windows package was rebuilt after the HUD, camera, lighting and district-material changes. `Artifacts/build-proof.json` records the current executable SHA-256 and package container hashes. Its acceptance scope is C++/UHT build, cook/stage/archive, and runtime asset inclusion; the earlier consolidated gameplay report below belongs to the prior executable.

`Scripts/Smoke-Package.ps1 -PackageDirectory Artifacts/Package/Windows -OutputName VisualPolishPackageSmoke` launched the cooked build, loaded the district data, instantiated three parked background cars, requested fresh title/chase/hood/cockpit captures, logged `AFTERDARK_RENDER_SMOKE_COMPLETE`, and exited cleanly. See `Artifacts/VisualPolishPackageSmoke/runtime.log` and the four PNG captures in that directory. The capture inspection confirms the updated HUD and camera composition, but the procedural city and player car remain blockout art. This smoke does not repeat the full gameplay, multiplayer, device or performance matrix.

The cooked UFS manifest includes the three district PBR materials and all six generated textures with their bulk data. The repeatable Package action now checks these assets before recording build proof. Detailed change notes and limitations are in `VISUAL_PRESENTATION.md`.

## Consolidated gameplay run — 2026-09-25 (prior visual build)

UE 5.8.3 (changelist 58210709) built and packaged the current Windows Development game. Source validation passed 146 project text files and the data/contracts; portable vehicle math passed 963 checks and portable race rules passed 374. The consolidated run at `Artifacts/Consolidated/20260925T143116Z-a87e22b8/report.json` passed every stage: source validation, both portable suites, **45 Unreal automation cases**, packaged camera smoke, a four-size viewport matrix, a two-lap physical race, garage persistence across process relaunch, loopback multiplayer driving/telemetry/disconnect, and a 120-second driving capture. The report ties the prepared run to the packaged executable and runtime source manifest.

At the time of that consolidated run, the tested executable SHA-256 was `D293028A6A009289044752C0CCC63F79A8E7E8DE5E197220329AFFBAB007C4F0`. The current `Artifacts/build-proof.json` has since been refreshed for the visual presentation package above; the measurements in this section must not be attributed to its newer executable. That earlier package's UFS manifest contained runtime data and the imported `SM_CC0_CityCar` mesh. Its smoke log confirms the district created three non-colliding instances and activated 1,024 rain streaks at 83% intensity. The model's provenance, CC0 license, conversion and asset hashes are in `ASSET_PROVENANCE.md`.

`Artifacts/DisplayMatrix/report.json` records four requested window sizes. Captures matched Unreal's reported render resolution and retained each requested aspect ratio: 800×600 produced 800×600; 1280×720 produced 1280×720; 1920×1080 and 2560×1440 both rendered at 888×500 on this offscreen setup. The latter pair is not evidence of native 1080p/1440p output or a physical display test.

The packaged race finished all four racers across two laps without DNF, recovery or penalty; the player placed fourth. Its 120-second uncapped DX11/720p capture averaged **86.97 FPS**, p95 **13.648 ms**, p99 **15.9 ms**, worst frame **40.678 ms**, with two frames over 33.33 ms. The 120-second straight-line drive sample covered 2,297.4 m, averaged **93.22 FPS**, p95 **12.604 ms**, p99 **14.023 ms**, worst frame **26.075 ms**, and had no frames over 33.33 ms. Reported GPU-memory counters peaked near 252–253 MB; process working set peaked at 501 MB during that drive. The loopback multiplayer run verified host listen, client join/map load, client throttle, authoritative server movement, returned speed telemetry and disconnect notification. The harness terminated both instances after observing disconnect; its report records exit code -1 for each process. None of these short samples establishes a sustained 60 FPS target, full-world memory behavior, external-network reliability, or native high-resolution support.

Rendered captures remain a procedural city/car blockout with a dark starter vehicle and simple building facades. The imported CC0 prop adds static background detail, not a production vehicle model. The full game roadmap is not accepted; final automotive/environment art, physical controller/wheel testing, a native monitor matrix, audio review, broad-world soak, and wider multiplayer fault cases remain open.

## Previous integrated slice measurements — superseded binary

The following report entries describe earlier builds unless explicitly identified as historical. They must not be attributed to the executable hash above.

## Historical build records

**2026-09-24 recovery/integration:** UE 5.8.3 and source recovery were verified. The source/data and portable test results are superseded by the current evidence above; recovered gameplay at that point had not yet received a fresh Unreal report.

**2026-09-20 integrated build:** Editor and Windows Game C++/UHT compilation succeeded, followed by a successful Windows cook/stage/archive (466.52 seconds). The cook reported zero errors and zero warnings; all 12 runtime JSON files appear in the staged UFS manifest. `Artifacts/build-proof.json` records the current executable and containers, and `Artifacts/integrated-source-manifest.json` records 113 runtime source/content files. This is build evidence only. The user requested completing implementation before a consolidated gameplay test pass; no new gameplay, package smoke, multiplayer session or performance capture has run.

2026-09-20 previous Windows executable SHA-256: `AB680ED248248532CD32D23EF11621F4883BFDA192D63006B193B844F8DC0461`.

The earlier Phase 2 record is preserved as `Artifacts/phase2-build-proof.json`. **The PASS rows and runtime measurements in the historical tables below apply to previous executables, not the current source checkpoint.** Prepared engine cases have since grown to the 45-case suite reported above. See `IMPLEMENTATION_QUEUE.md`, `INTEGRATED_SYSTEMS.md` and `CONSOLIDATED_TEST_PLAN.md` for current scope and remaining gates.

Historical table below, updated 2026-09-19 (local time). Phase 2 extended the tested Phase 1 foundation at that time. This file records actual checks; it is not a release certificate.

| Gate | Result so far | Evidence |
|---|---|---|
| Target engine | UE 5.8.2 installed and used | Engine Build.version; changelist 56702186 |
| C++ / UnrealHeaderTool | PASS | `Scripts/Invoke-Afterdark.ps1 -Action Build`; generated module DLL in Binaries/Win64 |
| Source/data contracts | PASS | `python Scripts/validate_source.py`; 3.90 km connected roads, no building-zone road overlap |
| Portable vehicle math | PASS | `Scripts/Test-Core.ps1`; 963 numerical assertions compiled and executed with g++ 6.3 |
| Portable race rules | PASS | `Scripts/Test-RaceRules.ps1`; 374 assertions, zero failures |
| Race data validation | PASS | `Afterdark.Data.RaceDefinition`; production route plus malformed-content rejection and safe failed reloads |
| Physical races | PASS | Four cars complete two laps at all three difficulties without managed recovery or DNF; 21 total Unreal tests pass |
| Editor content generation | PASS | Bootstrap saved 16 materials and L_Dockside.umap; commandlet reported 0 errors, 0 warnings |
| Runtime data validation | PASS | `Afterdark.Data.VehicleDefinition`; real JSON loads; malformed schema rejected |
| Chaos settling/drive/brake | PASS | Three real PIE runs at fixed 30, 60, 120 Hz; 4 wheels grounded, positive acceleration, brake slows chassis |
| Handling regression fixtures | PASS | Twelve additional cases: 200 km/h braking and thin-wall impacts, 60 km/h steering and curb crossings, recovery at 30/60/120 Hz |
| Enhanced Input integration | PASS | Real keyboard/analog-command routing, camera switch, throttle release and focus-flush automation |
| Rendered inspection | Startup/camera smoke PASS; final art pending | Four final cooked captures in `Artifacts/PackageSmoke`; lamp, camera, menu and speed-unit spacing fixes inspected |
| Windows package | PASS | Four runtime JSON files in UFS manifest; successful archive; packaged four-car race finishes and exits cleanly |
| Device and handling acceptance | Pending | Physical controller, wheel, subjective handling, glancing/diagonal impacts, low-FPS hitches and full resolution matrix |
| Rendered frame times | Four-car capture completed; occasional hitches remain | 120 seconds, 83.17 FPS average at 720p DX11; p95 13.751 ms; seven frames above 33.33 ms |

## Measured physics smoke results

These are straight-line tests in the real generated district using the actual Chaos vehicle component. Input commands are applied directly to the component for this physics test. They do not validate physical controller input, tire thermodynamics or complete handling.

| Simulation rate | Speed after 4 s throttle | Forward distance | Speed after following 2 s brake |
|---|---:|---:|---:|
| 30 Hz | 73.44 km/h | 44.09 m | 0.12 km/h |
| 60 Hz | 73.67 km/h | 44.02 m | 0.18 km/h |
| 120 Hz | 73.92 km/h | 43.99 m | 0.17 km/h |

Acceleration samples agree within 0.7% across these three runs (gate: 5%). This is one straight-line maneuver; it does not establish frame-rate invariance for drifting, impacts or suspension over rough ground. Tire contacts still update once per game frame while Chaos integrates substeps.

At the time this historical 21-case report was recorded, the input test used Unreal's simulated key events through the actual player controller and Enhanced Input mappings; it did not certify a physical gamepad. The current 45-case export is listed at the top of this document. Logs: `Artifacts/build-editor.log`, `Artifacts/bootstrap.log`, `Artifacts/automation-engine.log`.

## Phase 2 race completion

One 2,047.96 m circuit, two laps, seven directed gates and three physical opponents are connected to the existing vehicle model. Countdown, race clock, ordered swept gates, wrong-way guidance, route minimap, recovery penalties, provisional/final results, restart and free-driving return are implemented. All four cars use the same forces, gears and collision rules. There are no extra AI forces or position catch-up teleports.

Full-race Unreal automation uses fixed 30 Hz simulation and ordinary throttle/brake/steering, including for the QA player. It verifies pause timing, a penalized recovery without progress gain, restart cleanup, continuous physical motion, valid lap sums, unique placement and two legal laps for every car. It does not certify racing enjoyment or all blocking/ramming scenarios.

| Difficulty | QA player finish / place | Rhea finish | Mako finish | Tess finish |
|---|---:|---:|---:|---:|
| Relaxed | 213.593 s / 1 | 252.103 s | 269.125 s | 270.209 s |
| Street | 216.127 s / 1 | 217.440 s | 224.706 s | 231.925 s |
| Expert | 217.119 s / 4 | 196.224 s | 208.277 s | 210.508 s |

Every measured competition completed with zero managed recoveries, penalties and DNFs. A separate explicit-recovery fixture checks the five-second penalty and then restarts cleanly before these measurements. The tests exposed and led to fixes for converging grid lanes, inconsistent following-distance reference points, and finished cars parking too close to the timing line. Finished opponents now drive a bounded run-out before braking; scored results remain fixed.

The race-definition test covers unsupported schema, wrong numeric types, fractional laps, impossible deadlines, degenerate route segments, backward/misplaced gates, overlapping grid slots, duplicate opponents and pace/lane settings outside the driver envelope. Portable rules tests cover skips, wrong direction, finite gate bounds, repeated finishes, timing interpolation, motion discontinuities and invalid best-lap candidates after recovery. These test the actual runtime parser and scoring code.

## Handling fixture measurements

The detailed maneuver values below were recorded in Phase 1. The complete handling suite also passes in the Phase 2 automation report.

`ADHandlingTests.cpp` seeds velocity once after settling the chassis. All subsequent contact, steering, braking and impact behavior uses the production component and Chaos. Fixed frame counts control input durations; floating-point world-time comparisons previously added an extra steering frame at some rates. These are bounded safety/regression cases, not a natural acceleration or subjective driving test.

| Rate | Speed entering high-speed braking | Braking distance | Heading after steering/braking | Peak chassis rise over 15 cm curb |
|---|---:|---:|---:|---:|
| 30 Hz | 187.00 km/h | 117.53 m | 12.82° | 9.47 cm |
| 60 Hz | 187.00 km/h | 117.51 m | 12.50° | 8.43 cm |
| 120 Hz | 187.02 km/h | 117.56 m | 12.36° | 8.09 cm |

Every high-speed case stopped below 5 km/h within seven seconds, remained upright and had no measured lateral drift. The steering fixture applied 0.25 right input for 0.5 seconds from 60 km/h, then braked to rest. The 10 cm wall stopped the 200 km/h chassis without tunneling; peak speed did not exceed its seeded 200 km/h. Curb crossings stayed upright and continued beyond the obstacle. All obstacle/steering recoveries restored four contacts and propulsion. Curb response still varies with contact sampling rate; these results do not establish full suspension invariance or resolve the remaining game-thread contact limitation.

## Packaged Phase 2 race and performance

`Scripts/Smoke-Race.ps1` passed on 2026-09-19 using the Windows Development executable. The rendered Street race finished in 231.961 simulation seconds with four valid two-lap results, zero penalties, zero managed resets, zero physical recovery attempts and zero DNFs. Player/Rhea/Mako/Tess finish times were 216.135 / 219.777 / 224.705 / 231.960 seconds. All cars covered over 4 km through normal controls; reported distance includes travel after the finish. The result report reconciles lap times and placement. Race metrics recorded real avoidance and passing; they do not certify sophisticated racing tactics.

Evidence: `Artifacts/RaceSmoke/{report.json,frames.csv,frame-summary.json,run-environment.json,runtime.log,AfterdarkRaceGrid.png,AfterdarkRaceResults.png}`. Grid and results screenshots were inspected for readable timers, places, laps, route map and action hints. The game exited with code 0; its log has no `Error:` entries. Screenshots occur outside the performance window.

| Four-car cooked metric | Measured value |
|---|---:|
| CSV frames / duration | 9,981 / 120.009 s |
| Average frame rate | 83.17 FPS |
| Median / p95 / p99 frame time | 12.069 / 13.751 / 14.715 ms |
| Worst frame / frames above 33.33 ms | 43.656 ms / 7 |
| Median game / render / RHI / GPU time | 3.238 / 8.791 / 3.055 / 12.056 ms |
| Reported local GPU-memory counter peak | 250.582 MB |
| Process working-set high-water, including startup | 521.95 MB |

Hardware: i5-6440HQ, 16 GB RAM, GeForce 940MX 2 GB, driver 582.66. Output is 1280×720 with 85% screen percentage, DX11/SM5, uncapped FPS and VSync off. The test warms up before capturing and checks that CSV duration/frame count agree with the race report (a one-frame profiler-boundary difference is allowed). Most frames fit the 60 FPS budget, but the measured hitches prevent an every-frame 60 FPS claim. RenderOffscreen does not measure display latency. The memory counters are scoped measurements, not whole-system memory or a long soak. Expanded art, traffic and weather require fresh profiling.

The tested native game binary SHA-256 is `FE2B3F95A5BD2CAE302A4E29611D3FF0F620DBF92EBE5202CF58C3E2C55ED63D`. `Artifacts/build-proof.json` ties this build to its validation artifacts. UAT completed build/cook/stage/archive with exit code 0; the full engine-owned log is preserved in `Artifacts/package-engine.log` because the initial console transcript was interrupted during the build.

## Phase 1 baseline driving capture

The following single-car captures predate Phase 2. Their binary hashes and dates are retained in their own run-environment reports; they are not measurements of the new four-car build.

The final cooked executable passed `Scripts/Benchmark-Afterdark.ps1`: 120.006 seconds, 2,297.37 m, maximum speed 83.51 km/h, maximum route deviation 0.936 m. It drove continuously through normal controls with no transform reset, road-contact loss or route failure. Evidence is in `Artifacts/DrivingBenchmarkPackage`: `drive-report.json`, `frames.csv`, `frame-summary.json`, `run-environment.json` and `runtime.log`. Both the driving report and CSV must be fresh and their duration/frame counts must agree; the process exited with code 0 and the log has no `Error:` entries.

| Final cooked driving metric | Measured value |
|---|---:|
| CSV frames / duration | 11,419 / 120.005 s |
| Average frame rate | 95.15 FPS |
| Median / p95 / p99 frame time | 10.368 / 11.913 / 12.593 ms |
| Worst frame / frames above 33.33 ms | 24.104 ms / 0 |
| Median game / render / RHI / GPU time | 2.277 / 6.212 / 2.689 / 10.311 ms |
| Reported local GPU-memory counter peak | 247.551 MB |
| Process working-set high-water, including startup | 499.51 MB |

The Phase 1 single-car workload had headroom over most of this static-district route. The worst frame exceeded 16.67 ms, so it was not an every-frame 60 FPS guarantee. The memory counters do not measure whole-system RAM/VRAM or a repeated-route memory soak. `Artifacts/phase1-build-proof.json` preserves the earlier build record; use the Phase 2 race measurements above for the current executable.

`Scripts/Benchmark-Afterdark.ps1 -Editor` completed a 20-second driving warmup followed by 120.008 seconds of measured driving. It used normal controls on `dockside_north_loop_v1`, covered 2,297.46 m, reached 83.51 km/h and stayed within 0.937 m of the route during capture. No transform reset, road-contact loss, overturning or route failure occurred. This is a QA driver, not racing AI.

Editor trial evidence: `Artifacts/DrivingBenchmarkEditor/{drive-report.json,frames.csv,frame-summary.json,run-environment.json,runtime.log}`. 11,390 CSV frames: median 10.371 ms, p95 12.021 ms, p99 13.096 ms, worst 36.872 ms, average 94.91 FPS. Two frames exceeded 33.33 ms. Median game/render/RHI/GPU times: 2.916 / 6.741 / 2.669 / 10.296 ms. Reported local GPU-memory counter peaked at 268.891 MB; process working-set high-water was 1,901.90 MB including startup. These counters are not total system RAM/VRAM or a memory-soak result. The old editor report's lap field counted start-line crossings, including initial entry; the final helper labels that metric as start-line crossings.

Hardware: i5-6440HQ, 16 GB RAM, GeForce 940MX 2 GB, driver 582.66. Settings: 1280×720 output, 85% screen percentage, DX11/SM5, initial project scalability, uncapped FPS and VSync off. The runner explicitly sets resolution scale, cap and VSync for subsequent runs. Rendering is offscreen; the run measures rendered workload, not display latency. There are no screenshot captures during measurement. The sample has useful 60 FPS headroom for this static district, with occasional hitches; it does not establish performance for traffic, expanded art, weather or the entire hardware matrix.

## Rendered smoke and profiling

`Scripts/Invoke-Afterdark.ps1 -Action Render` completed with a clean helper exit and four fresh screenshots. The scene shows the original car, connected roads, lit windows, reflections and HUD; chase/hood/cockpit viewpoints render. Captures caught and led to fixes for inward car triangle winding, unreadable font rasterization, title/speedometer overlap, missing material instancing support and exposure. This is functional blockout inspection, not AAA art acceptance.

The Phase 2 cooked build passed `Scripts/Smoke-Package.ps1` on 2026-09-19: native Windows runtime, real GPU rendering, successful data loads, four fresh captures and exit code 0. `Artifacts/PackageSmoke/runtime.log` contains no `Error:` entries. All four screenshots were inspected: title race entry/difficulty hints are readable, chase/hood/cockpit cameras leave the road visible, the menu car clears the controls footer, and KM/H has space beside the speed digits. Profiler overlays are hidden. The corrected `DirectoriesToAlwaysStageAsUFS` setting is guarded by manifest checks and real runtime data loads.

The separate stationary camera sequence is diagnostic, with a 60 FPS cap and screenshot readback stalls. Its refreshed `Artifacts/PackageSmoke/frame-summary.json` reports 1,175 frames over 21.029 seconds after discarding five seconds: median 16.671 ms, p95 19.287 ms, p99 46.963 ms, worst 208.530 ms, average 55.88 FPS. Twenty frames exceed 33.33 ms. Use the uncapped, screenshot-free race capture above for sustained performance; these workloads are not directly comparable. The earlier camera artifacts were preserved in `Artifacts/Phase1PackageSmoke`.

Previously tested playable artifact: `Artifacts/Package/Windows/VelocityAfterdark.exe`, with its adjacent Engine and VelocityAfterdark directories. Do not move only the executable out of that folder. The directly tested game binary is `VelocityAfterdark/Binaries/Win64/VelocityAfterdark.exe` inside that archive. Source entry point remains the root `VelocityAfterdark.uproject`; a fresh package is being prepared for the current source.

## Environment and limitations

Windows host: Intel i5-6440HQ, 16 GB RAM, NVIDIA GeForce 940MX with 2 GB VRAM. Initial graphics use DX11/SM5 and conventional lighting/shadows. UnrealBuildTool accepts installed MSVC 14.51.36256 but reports that it is newer than its preferred 14.50.35717. Presentation color literals were corrected to avoid float-conversion warnings. No compiler/toolchain installation or host configuration change was performed.

The current source includes five data-driven fictional vehicles, an interactive garage and upgrade/save loop, eight route events, three physical opponents, environmental time/weather controls, regional roads, limited traffic and police pursuit/search, camera/input/settings options, and photo/replay tooling. A generic licensed CC0 city car appears only as background dressing. These source-backed systems have passed the bounded checks listed above; this does not imply a complete AAA implementation. Cars and districts are original generated blockouts with simple chassis collision, opaque exterior glass and basic cabin shapes. The fresh captures visibly confirm that the vehicles, roads, buildings and lighting still need substantial art production. Synthesized sound is connected but is not final recorded automotive audio. Regional scenery is a compact procedural test world loaded as a single map rather than a production-scale seamless World Partition city. Weather/road response, traffic behaviors, police tactics, rival memory and race generation are intentionally limited. One packaged loopback join/drive/disconnect path passed, but player-cap, packet fault, external network and host-failure cases remain untested.

The project is a functioning Unreal prototype, **not a 100%-complete production-quality game**. Final car/world art, hands-on handling and collision evaluation, physical controller/wheel and disconnect checks, audio listening, display/device matrix, adversarial AI blocking/ramming, packaged gameplay, whole-system performance and memory-soak acceptance remain open. Automated passes certify only the specific assertions each test makes.
