# Delivery gates

No phase is accepted by having files or a passing source scan. Each gate needs an engine build, reproducible behavior and recorded acceptance.

The project has prototype source implementations across Phases 1–9, but that does not mean all phases meet their delivery gates. The 2026-09-27 prepared package suite passed its ten bounded stages, including 56 Unreal automation cases; its exact scope and measured performance are in `VALIDATION.md`. Production art/audio, physical-device evaluation, broader world content, whole-world performance, and online acceptance remain open. No phase is production-complete solely because its prototype systems compile or pass selected automated cases.

Phase 2 extends the tested Phase 1 driving foundation. Phase 1 art, physical-device and broad handling acceptance remain open and are not reclassified as complete by later feature work.

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
| 1 identity; 53 art; 60–62 opening/story/characters; 71–72 priority/experience | Original eight-chapter text and a real-time multi-shot opening sequence are implemented; full campaign, character performances and final art remain open |
| 2–4 world/streaming/rendering; 54–55 detail/surfaces | Bounded Phase 1 sector; full world/streaming Phase 6, art Phase 8 |
| 5–6 time/weather; 57 wetness/dirt | Phase 4; snow/dust regional expansion Phase 6 |
| 7–9 physics/tires/suspension | Simplified Phase 1 force model; advanced tire thermals/differentials pending measured need |
| 10 damage; 31–33 speed/nitrous/exhaust; 56 particles; 58 lights; 59 interiors | Basic lights/interior Phase 1; required first-playable damage/nitrous before integrated gate; art/VFX Phase 8 |
| 11 vehicles; 12–15 customization/upgrades/dyno/tuning; 25 garage | Five data-driven cars and transactional garage/upgrades/tuning presets; full body-part/livery pipeline, dyno and broader roster remain open |
| 16–18 race types/routes/AI; 65 generated races | One valid race Phase 2, broader modes and procedural validation after route graph stabilizes |
| 19–20 police/escape | Phase 5 |
| 21–24 crews/career/REP/economy; 26 dealers; 27 meets; 63–64 events/rivals | Phase 7; meets/events depend on Phase 4 activity scheduling |
| 28 traffic; 29 protected background pedestrians | Phase 4 and regional atmosphere pass |
| 30 cameras; 37–40 HUD/speed/map/menu; 45 input | Driving HUD/cameras, rebinding basics and offline landmark map with zoom/pan, discovered fast travel and one waypoint; full navigation/UI/input coverage remains open |
| 34–36 vehicle/environment/music | Synth proof Phase 1, surface/weather audio Phase 4, authored mixer/radio Phase 8 |
| 41–42 photo/replay | Photo mode and bounded player-vehicle replay are implemented; full-scene replay and production photo controls remain open |
| 43–44 networking/social | Architecture only until Phase 9 |
| 46–48 accessibility/graphics/scalability; 70 profiling | Legible scalable Phase 1 HUD, standard engine scalability; full custom settings/device matrix Phase 8; profiling every gate |
| 49–50 save/statistics | Phase 3, expanded alongside actual systems |
| 51–52 modular/data design; 66–69 phases/first playable/quality/testing | Applied throughout; first playable remains separate from Phase 1 |

## Next gate priorities

See `VALIDATION.md` for the latest package-matched report, metrics and scope boundaries. A passing prepared suite accepts only the assertions it ran; it does not accept every phase or the full roadmap.

Before treating the slice as production-quality accepted, complete hands-on handling/racing, physical controller/wheel and disconnect checks, audio listening, full display matrix, adversarial AI checks, and replace development art with authored assets. Substantial content and runtime acceptance remain outstanding; see `IMPLEMENTATION_QUEUE.md`.
