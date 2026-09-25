# Delivery gates

No phase is accepted by having files or a passing source scan. Each gate needs an engine build, reproducible behavior and recorded acceptance.

On 2026-09-20 the user changed the delivery order: implement the remaining phases first, then run the consolidated gameplay test suite. The 2026-09-25 integrated Unreal run passed 45/45 cases. A fresh UE 5.8.3 Win64 package then passed packaged startup, physical race and garage persistence smoke checks. These results accept the tested prototype behaviors only; the production roadmap, original art/audio, device evaluation, broader world, performance and online acceptance remain open.

On 2026-09-14 the user explicitly authorized Phase 2 after the Phase 1 build, handling regressions and packaged driving benchmark passed. Phase 2 implementation proceeds with the existing driving foundation; Phase 1 art, physical-device and broader handling acceptance remain open and are not reclassified as completed.

| Phase | Deliverable | Exit evidence |
|---|---|---|
| 1 — driving | One Aster S6, Dockside, physics, chase/cockpit, keyboard/gamepad, HUD, engine audio | Clean UHT/compile, launch/cook, handling matrix, device checks, performance capture and art review |
| 2 — racing | One circuit, checkpoint gates, three physical AI cars, laps and results | Forward/reverse/skipped-gate exploit tests; AI recovery and race completion at three difficulties |
| 3 — ownership | Rendered garage, paint and meaningful upgrades, tuning presets, versioned save | Immediate visible/performance changes, transaction rollback, interrupted-write and migration tests |
| 4 — living road | Traffic lanes/signals, time clock, gradual wetness, rain | Traffic queues and collisions; weather/time persistence; measured braking changes |
| 5 — pursuit | Heat 0–5, pursuit and search, escape, bounded tactics | Observability/state transition tests, valid hide/escape, no unfair instant spawns |
| First playable | Consolidated 1–5: 3–5 km, 1 race/3 rivals, nitrous/damage, graphics UI and save | All 18 requested first-playable items work together in a packaged executable |
| 6 — city | All biomes progressively authored, streaming, discoveries | Max-speed cross-city streaming soak with no missing road collision or memory growth |
| 7 — career | Crews, story beats, economy, dealers, starter selection | Full career graph reachable; no duplicated rewards, dead ends or excessive grind |
| 8 — polish | Authored car/world/audio/VFX, accessibility, photo/replay, settings | Hardware matrix, content audit, save compatibility, localization and device acceptance |
| 9 — online | Dedicated authority, lobbies, racing; optional social | Latency/loss/cheat tests, server load, recovery and service failure handling |

The 10–15 car roster begins after handling and asset production standards are accepted. Data architecture supports more entries; 100 cars are a content and performance workload, not a class-count target.

## Specification coverage map

All requested systems remain part of the design. “Planned” does not mean implemented.

| Specification sections | Delivery responsibility |
|---|---|
| 1 identity; 53 art; 60–62 opening/story/characters; 71–72 priority/experience | Direction in architecture; authored story Phase 7, final art Phase 8 |
| 2–4 world/streaming/rendering; 54–55 detail/surfaces | Bounded Phase 1 sector; full world/streaming Phase 6, art Phase 8 |
| 5–6 time/weather; 57 wetness/dirt | Phase 4; snow/dust regional expansion Phase 6 |
| 7–9 physics/tires/suspension | Simplified Phase 1 force model; advanced tire thermals/differentials pending measured need |
| 10 damage; 31–33 speed/nitrous/exhaust; 56 particles; 58 lights; 59 interiors | Basic lights/interior Phase 1; required first-playable damage/nitrous before integrated gate; art/VFX Phase 8 |
| 11 vehicles; 12–15 customization/upgrades/dyno/tuning; 25 garage | One car Phase 1, working ownership pipeline Phase 3, dyno and deep cosmetic pipeline Phase 8 |
| 16–18 race types/routes/AI; 65 generated races | One valid race Phase 2, broader modes and procedural validation after route graph stabilizes |
| 19–20 police/escape | Phase 5 |
| 21–24 crews/career/REP/economy; 26 dealers; 27 meets; 63–64 events/rivals | Phase 7; meets/events depend on Phase 4 activity scheduling |
| 28 traffic; 29 protected background pedestrians | Phase 4 and regional atmosphere pass |
| 30 cameras; 37–40 HUD/speed/map/menu; 45 input | Basic driving HUD/cameras/controls Phase 1; remaining navigation, rebinding and full UI later |
| 34–36 vehicle/environment/music | Synth proof Phase 1, surface/weather audio Phase 4, authored mixer/radio Phase 8 |
| 41–42 photo/replay | Phase 8 after record/replay data contract; race snapshots designed in Phase 2 |
| 43–44 networking/social | Architecture only until Phase 9 |
| 46–48 accessibility/graphics/scalability; 70 profiling | Legible scalable Phase 1 HUD, standard engine scalability; full custom settings/device matrix Phase 8; profiling every gate |
| 49–50 save/statistics | Phase 3, expanded alongside actual systems |
| 51–52 modular/data design; 66–69 phases/first playable/quality/testing | Applied throughout; first playable remains separate from Phase 1 |

## Next gate priorities

The fresh integrated package is recorded in `Artifacts/build-proof.json`; its source/content identity is recorded in `Artifacts/integrated-source-manifest.json`. The current report has 45/45 Unreal passes. Packaged smoke visibly renders the rainy opening and completes four camera captures; the physical two-lap race finishes four cars without recovery, DNF or penalty; and the garage purchase/configuration persists across two launches. A rainy, traffic-enabled 120-second driving sample averaged 84.22 FPS with p95 13.862 ms; a separate race sample averaged 84.42 FPS with p95 13.69 ms. See `VALIDATION.md` for exact evidence, artifacts and scope boundaries.

Before treating the slice as production-quality accepted, complete hands-on handling/racing, controller/disconnect, audio listening, display-matrix and adversarial AI checks, and replace generated blockout art with authored assets. Phase 3–9 source work is now tracked in `IMPLEMENTATION_QUEUE.md`, with substantial content and runtime acceptance still outstanding. The user-requested consolidated testing order does not turn untested source into an accepted phase.
