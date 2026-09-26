# Consolidated acceptance

The latest prepared integration batch passed on 2026-09-26 against the package recorded in `Artifacts/build-proof.json`. Its report is `Artifacts/Consolidated/20260926T183252Z-ac3fce9e/report.json`; all ten prepared stages passed, but the report does not mark the full game roadmap accepted. The sections below list remaining validation and production work. Repeat packaged checks after changing runtime source or content.

## Automated cases already prepared

The existing 21 engine cases and portable vehicle/race assertions remain required. The initial seven additional cases were:

- Ownership.Transactions: priced purchases, duplicate purchase, immutable stock, equip/unequip and invalid ownership.
- Ownership.CorruptionRecovery: interrupted staging, corrupt primary, backup recovery/repair and preservation of a future schema.
- Ownership.MigrationAndRollback: v1 migration, stale-session rejection and real write failure without a charge.
- Runtime.GarageFlow: actual input, entry guards, paint preview/cancel, purchase, equip/tune and return pose.
- Runtime.GarageAcceleration: repeated physical stock/ECU acceleration from identical initial conditions.
- Data.RoadNetwork: crossings, T-junctions, overlapping roads, same-edge projections, disconnected/far queries, rejected-reload preservation and routes to every production discovery.
- Ownership.WorldProgress: world-state round trip, one-time discovery rewards, unknown IDs, garage preservation of discoveries/world state, invalid snapshots and failed-write rollback.

The current suite requires **46 engine cases**. The portable race suite now includes zero-lap sprint progression and passes 393 assertions. Unreal coverage includes race catalog validation and ordered career progression; per-vehicle ownership, schema migration and rollback; a purchased vehicle driven with normal keyboard input; input rebinding, settings apply/cancel and UI coordinate scaling; living-world weather/traffic/pursuit integration; photo/replay return state; and eight full physical regional races with three opponents, directed gates, no DNF/recovery, legal results and cleanup. These bounded fixtures do not replace the remaining visual/device/network/soak checks below.

`Smoke-Garage.ps1` launches two isolated processes, checks persistence and actual physical configuration on reload, captures stock/custom/reloaded garage images and records a bounded garage CSV. The test never uses the normal player profile.

`Scripts/Test-PreparedSuite.ps1` is the single-invocation entry point for these fixtures. It requires matching package/source hashes, runs prerequisites sequentially, stops and records the first failure, and produces `Artifacts/Consolidated/<run>/report.json`; `-IncludeDrivingBenchmark` adds the cooked drive capture. The passed run also includes a four-requested-size packaged viewport matrix and a two-process multiplayer drive/telemetry/disconnect smoke. These checks remain bounded; neither substitutes for a physical display/gamepad or internet-hosted session.

## New cross-system cases still required

| Area | Acceptance work |
|---|---|
| Environment | Full day wrap, sunset/day garage exposure, clear/rain/fog transitions, drying, visibility, road material changes, grip parity for all players/AI, parked/exit saves and restored phase/wetness |
| Traffic | Red/green stops, queue following, player obstruction, collision/recovery, race teardown and repopulation; no car spawned near a player |
| Police | Actual physical arrival, escalation, line-of-sight loss/reacquisition, search/escape, bounded spawn counts, arrest control lock, garage/race/recovery restrictions |
| Regions/map | Drive every connector, road overlap/curb checks, full-speed streaming, repeated cell load/unload, prop bounds, memory soak, absence of missing road collision; map filtering, controller/mouse selection, route following, discovery dwell/recovery exclusions, failed-save retry and relaunch |
| Career | Current-only event selection, legal classification before rewards, repeat receipt, failed-save retry, all chapters reachable, ordered progress migration |
| Nitrous/damage | Measured acceleration surge at traction-free speed, tank depletion/recharge at multiple frame rates, pause/focus release, collision damage, cosmetic/simulation modes, garage repair |
| Settings | Apply/cancel behavior, persistence failure, custom engine presets, every supported setting, readability and focus handling |
| Photo/replay | Paused world scoring, timeline rollover/rewind, all views, return pose/velocities, capture success, camera clipping, no post-playback checkpoint gain |
| Online | Smoke verifies listen/join/map load, client throttle causing authority-side movement, returned speed telemetry and server-observed disconnect. Remaining: nitrous/damage/environment replication, simultaneous pending joins/four-player cap, slot reuse, input timeout, recovery rate/occupancy, unauthorized-owner RPC rejection, competitive/currency paths, latency/loss and host failure |
| Package/performance | Fresh cook, all runtime JSON and imported mesh staged, four consistent aspect-preserving captures at requested sizes from 800x600 through 2560x1440 (the offscreen renderer may use a smaller internal resolution), separate no-QA-feature-suppression driving captures, sustained frame/memory measurements, physical controller and monitor matrix |

Development smoke flags suppress the living-world managers to keep the original driving/race fixtures deterministic. New world acceptance must explicitly use `-AfterdarkEnvironment`; a successful isolated garage/race smoke is not living-world validation. The integrated living-world automation injects disconnect notifications through the actual player-controller callback and checks pause/recovery behavior; it does not substitute for removing and reconnecting a physical gamepad.

## Scope that source/tests cannot waive

The five vehicle definitions are data-driven, but two newer silhouettes use generated blockout geometry. A generic CC0 city car now appears as three non-colliding background props; its materials are approximated from source PBR values, and it is not a drivable or traffic vehicle. Neither addition is final authored automotive/environment art. Recorded audio, complete terrain/biomes, authored dealerships/starter selection, advanced police tactics, full accessibility/remapping, professional photo/full-scene replay, competitive online authority and broader career content remain production work. Keep those gates open even if all prepared automated cases pass.
