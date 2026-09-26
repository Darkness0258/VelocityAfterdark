# Remaining implementation and consolidated validation

The user requested on 2026-09-20 that the remaining roadmap phases be built before the combined gameplay test pass. That implementation batch and the combined runtime checks are now recorded below. The current packaged slice is not the completed production roadmap.

## Build order

1. Finish the ownership loop: rendered garage, preview/cancel, real upgrade effects, tuning, transactional saves, corruption recovery and migration.
2. Connect the living road: clock, weather, gradual wetness and tire friction, bounded physical traffic and signals.
3. Connect police pursuit, heat escalation, line-of-sight search and escape to driving and garage restrictions.
4. Expand the road/world architecture with bounded streaming of regional scenery and persistent road collision.
5. Connect career events, crews, narrative beats, rewards and discoveries to one persistent ownership record. Rewards must be idempotent.
6. Add the missing first-playable systems and polish: nitrous, scalable damage, settings/accessibility, photo/replay and performance budgets.
7. Add optional online support behind a separate mode. Competitive ownership and results must remain authority-owned; unsupported online modes must be explicit.
8. Run the consolidated source, engine, transaction, physics, race, environment, pursuit, streaming and packaged smoke checks; fix failures and rerun affected checks.

## Acceptance boundaries

Generated cars and scenery remain development art. Regional blockouts are not the final authored Nova City. Synthesized audio is not final recorded engine audio. No quantity of source files substitutes for the original asset, device, performance, multiplayer and quality requirements. Record the actual supported scope and remaining work for each phase before declaring it complete.

## Current state

- Driving/racing and the added Phase 3–9 prototype systems have selected runtime validation recorded in `VALIDATION.md`; old Phase 1/2 reports are explicitly marked historical.
- Phase 3 garage, catalog, effective physics configuration and transactional saves are connected. Garage flow, acceleration, ownership, migration/rollback, physical configuration and two-process relaunch checks passed in the 46-case/editor and packaged garage reports.
- Phase 4 source includes a 24-hour clock, clear/rain/fog transitions, wetness and road-grip changes, four physical traffic cars and two timed signals. The opening rainy night now renders 1,024 bounded camera-local streaks; the cooked startup capture verifies the emitter activates. Schema 5 saves retain the clock, weather sequence position, transition intensity and wetness. Full road-network traffic, windshield/roof interaction and environmental audio remain incomplete. Failed traffic cars freeze, retire beyond player proximity, and can be replaced through bounded road spawn checks.
- Phase 5 has bounded Dockside patrol/pursuit/search/cooldown/busted states, heat 0–5, physical interceptors and garage/recovery restrictions. Coordinated PIT, roadblocks, spikes and helicopters are not implemented.
- Phase 6 adds connected resident road collision and seven themed scenery regions with load/unload hysteresis, a cell cap and a per-update work budget. A cached road graph splits crossings/T-junctions and overlapping segments; the offline map filters landmarks and sets road-following discovery routes. Scenery is primitive development art; roads are flat, not authored mountain hairpins or a finished city. Scenery props are not yet colliding; road collision remains resident.
- Phase 7 has eight original chapters with eight distinct catalog-defined circuits, reputation ranks, persistent progression and idempotent result rewards. Eight road-accessible discoveries pay catalog-defined credits and REP once, in the same transaction as their discovered state. Five data-driven car definitions now support per-car purchases and builds. The full multi-district narrative campaign, starter sequence, authored dealerships, broader vehicle roster and dynamic crew world remain incomplete.
- Phase 8 work includes supported graphics/presentation settings, nitrous, basic collision damage, a photo camera with exposure adjustment and up to 60 seconds of vehicle-only replay. Custom graphics values are preserved; settings persistence is read back before reporting success. Replay resets at discontinuities. Main driving inputs can be rebound with conflict swaps and apply/cancel behavior; fitted UI scale is adjustable from 70–100%. Full art/audio/VFX, accessibility, all-action rebinding, professional photo controls and full-scene replay remain unfinished.
- Phase 9 has source for an optional four-player direct-connect stock-car free roam mode with authority-owned vehicle input, nitrous/damage and environment snapshots. Login reserves stable player slots; stale input brakes, recovery requests are throttled, and remote cars receive wet-road grip. A packaged loopback smoke passed one client join/drive/telemetry/disconnect path; player-slot limits, internet conditions, competitive races, matchmaking, account services and social features remain unvalidated or unimplemented.
- The UE 5.8.3 Editor and Win64 Game targets compiled, cooked, staged and archived on 2026-09-26. `Test-PreparedSuite.ps1 -IncludeDrivingBenchmark` passed with source/package hashes matched, 963 portable vehicle-math checks, 393 portable race-rule checks, all 46 Unreal cases, cooked startup, the four-requested-size viewport matrix, two-lap physical race, two-process garage relaunch, loopback network smoke and 120-second drive capture. Physical controllers/wheels, full-art acceptance, native high-resolution output, whole-world performance and production-roadmap acceptance remain open. See `Artifacts/Consolidated/20260926T183252Z-ac3fce9e/report.json`.

## 2026-09-24 integration batch

Recovered damaged source/data and missing build inputs from recorded successful edits, preserving damaged copies under `Artifacts/Recovery`; verified ZIP checkpoints precede the final test batch. The installed engine is 5.8.3. On 2026-09-26, source integrity/data contracts, 963 portable vehicle-math checks, 393 portable race-rule checks and all 46 required Unreal cases passed. The subsequent Win64 package passed cooked startup/data/rendering, a two-lap race with four physical finishers, garage purchase/configuration persistence through two launches, loopback network smoke and a 120-second driving capture. This is prototype evidence, not full-AAA or full-roadmap acceptance.
