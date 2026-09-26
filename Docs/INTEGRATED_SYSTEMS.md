# Integrated development implementation

This describes the source built after Phase 2, not full-roadmap acceptance. On 2026-09-26, the fresh UE 5.8.3 Win64 package passed 46 Unreal cases, 963 vehicle-math checks, 393 race-rule checks, cooked startup/race/garage checks, the four-requested-size render matrix, loopback listen-server/client driving smoke and a 120-second driving benchmark. The fresh report is linked in `VALIDATION.md`. The rendered city and vehicles are still development art; physical-device, wider-network and full-roadmap acceptance remain open in `IMPLEMENTATION_QUEUE.md`.

## Ownership and world continuity

`UADOwnershipSubsystem` loads immutable car, garage and discovery definitions. Garage previews derive an effective definition without changing stock; commit applies it only to a stopped/non-simulating chassis and rolls back if persistence fails.

The schema 5 profile contains credits, an active vehicle ID, an owned vehicle collection with per-car paint/parts/tuning, career counters/chapters/receipts, discovered IDs and an offline world snapshot. Versions 1–4 migrate the original starter build into its owned record. The immutable dealer catalog prices the RWD Aster S6, FWD Kestrel XR and AWD Meridian GT. Preview changes body/paint; commit changes actual engine, gearing, drivetrain and mass atomically with the purchase. A newer schema is preserved and prevents writes. Each transaction validates the candidate, stages and flushes a checksum envelope, verifies readback, replaces the previous-good backup, then atomically replaces the primary. A Windows exclusive lock and primary snapshot comparison reject concurrent/stale writers. Only success changes the live committed profile. Session-only editor/QA profiles run the same validation without disk writes.

Garage requests cannot change credits, progression, discoveries or world state. Race rewards take a receipt and final classification from the race manager; discovery rewards take a catalog ID from the arrival director. Prices/rewards come from catalogs. These are local authority boundaries, not anti-cheat against a modified executable.

`AADAtmosphere` stages hour, weather, sequence index/timer, rain/fog blend and wetness. Garage/career/discovery transactions include the latest snapshot. The atmosphere also saves after at least 60 seconds when stopped outside a race/pursuit, and on normal world teardown. Writes remain synchronous and need later hitch profiling. No player pose, traffic entities or active pursuits are restored. Abrupt termination can lose world changes since the last successful save.

## Living road and regional world

`AADAtmosphere` owns clear/rain/fog transitions, day/night lighting and gradual wetness. Registered cars receive the same wetness input. `AADGameMode` registers all server player pawns; race/traffic/police owners register their cars. Clients consume the replicated environment snapshot. Rain is a bounded 1,024-instance, non-shadowing local streak field verified in a packaged night capture; full roof/windshield interaction is still pending.

`AADTrafficManager` runs physical drivers on the existing circuit loop with two timed signals. Failed cars freeze and cannot restart; after five seconds and 150 m of separation they can retire. Bounded distant-road candidates refill traffic with ground/occupancy checks. This is not citywide lane/junction traffic or a civilian vehicle roster.

`AADPoliceDirector` owns patrol/pursuit/search/cooldown/busted states and heat 0–5. Retired units cannot observe or arrest. An active pursuit has bounded dispatches instead of endless replacement; increased heat can authorize another unit. Full PIT/boxing, roadblocks, spikes, helicopters, arrest economics and regional coordination remain open.

`AADRegionalWorld` keeps road collision resident and streams scenery with hysteresis, a cell cap and update budget. Approximately 7.1 km of new centerlines connect to the original 3.9 km. Roads are flat; scenery uses non-colliding development primitives. This is not authored World Partition production content.

## Exploration and navigation

`FADRoadNetwork` has no actor/tick dependencies. It loads both catalogs once, validates bounds/IDs/geometry, splits axis-aligned roads at crossings, T-junctions and overlap endpoints, and builds bounded adjacency. Route queries project endpoints within 150 m onto split edges, then run Dijkstra with temporary endpoint attachments. Invalid rebuilds preserve the previous graph; failed queries clear their outputs.

`AADExplorationDirector` validates discoveries against the network. Grounded arrival below 40 km/h with a continuous dwell triggers a one-time transaction. Discontinuous motion, garage, non-driving, racing and pursuit states cannot accumulate dwell. Photo/replay pause the world. Failed writes grant nothing and leave a bounded retry. Navigation refreshes once per second toward a chosen or nearest undiscovered target.

`UADMapComponent` owns offline modal pause/input restoration, paged selection, discovered/undiscovered filtering and route selection; HUD observes it. The current map supports catalog landmarks. Arbitrary pins, zoom/pan, fast travel, race/dealer layers and online map support remain open. Rendered UI acceptance is pending.

## Presentation and effects

`UADVehicleEffectsComponent` implements a bounded nitrous tank/recharge/torque multiplier, collision health, scratch strips and exhaust illumination. Boost feeds the existing tire-grip calculation. Cosmetic damage is default; simulation damage reduces engine output only. Clients display server tank/health/boost state and keep button requests separate from replicated results.

`UADSettingsSubsystem` stages supported changes until Apply. Mixed quality, arbitrary frame caps and unchanged resolution scale are preserved. Independent config readback is required before reporting saved settings. Main keyboard/controller driving actions support validated rebinding with compatible-conflict swaps; context/menu shortcuts stay reserved. A shared canvas transform aligns rendering and pointer input at a fitted 70–100% UI scale. The complete resolution/audio/accessibility/all-action remapping matrix remains open.

`UADCinematicComponent` records a bounded continuous vehicle segment and pauses offline simulation for photo/replay. Recovery, garage/non-driving, timestamp gaps and incompatible motion cut the recording. Replay temporarily moves the player car, then restores pose, velocities and simulation flags. Photo controls include translation, rotation, FOV, exposure and screenshot requests. Other actors/audio/wheel histories are not replayed; professional focus/DOF/filter/weather controls remain open.

The vehicle catalog contains five data-driven definitions. Two recently added car silhouettes are procedural development geometry. The district also uses three instances of a flattened, licensed CC0 static city-car mesh as non-colliding scenery. Its source, conversion, license, hash and package location are recorded in `ASSET_PROVENANCE.md`; it is not used as a drivable or traffic car.

## Optional networking

`UADOnlineSubsystem` guards direct host/join/disconnect transitions, validates addresses/ports and reports failures. Game-mode login reserves a stable slot before initialization, rejecting a fifth login even if several PreLogin checks passed. Logout releases the slot. Spawn selection checks nearby collision; stale input brakes and action/recovery requests are throttled.

The server simulates forces, nitrous and damage. Clients receive movement, telemetry, paint, effects and environment; all server player pawns receive wet-road grip. Stock-car free roam is the supported source scope. Career, purchases and competitive races are blocked online. Prediction, matchmaking, accounts, competitive results and social services remain open.

This uses Unreal's documented [login and player lifecycle hooks](https://dev.epicgames.com/documentation/unreal-engine/game-mode-and-game-state-in-unreal-engine). The packaged loopback check validates one client join, driving RPC, authoritative movement, telemetry and disconnect. It does not validate simultaneous joins, player-slot limits, packet loss, latency, NAT, host migration or unclean host failure.

## Delivery evidence

`Invoke-Afterdark.ps1 -Action Package` builds Editor and Game targets, cooks/stages Windows, checks every runtime JSON in the UFS manifest and records build-only hashes. `phase2-build-proof.json` preserves the earlier evidence. The current `build-proof.json` explicitly excludes old runtime reports.

The `Test-PreparedSuite.ps1 -IncludeDrivingBenchmark` one-command run passed on 2026-09-25. It records source-manifest/package hash agreement, 45 Unreal automation cases, four camera captures at each of four requested sizes, a two-lap packaged race, garage persistence across relaunch, loopback client throttle/authority movement/replicated telemetry/disconnect, and a 120-second rendered drive sample. Read `Artifacts/Consolidated/20260925T143116Z-a87e22b8/report.json` for stage results. This does not close physical-device, external-network, world-scale, art, audio or full-roadmap gates.
