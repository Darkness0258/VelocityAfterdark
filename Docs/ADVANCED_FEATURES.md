# Advanced features — design notes

> **Current status audited 2026-09-27:** These notes began as implementation proposals. The table below describes source behavior and its test boundary; passing source tests do not mean every production gate is closed. The latest matching package/test evidence is in `VALIDATION.md`, with open roadmap work in `IMPLEMENTATION_QUEUE.md`.

| Feature | Current status | Verification boundary |
|---|---|---|
| Rival memory | Bounded profile records, race-result updates, and rival-specific briefing/victory text are implemented. | Career route automation checks Ivo/Sel context changes; no focused profile save/reload test yet isolates rival-memory round-trip. |
| AI driver personalities | Data-defined overtaking, braking and pressure-mistake weights are implemented and applied to driving decisions. | Catalog validation and full physical races pass; no A/B test asserts that each weight produces a measured behavior difference. |
| Police PIT / roadblocks | Not implemented. Coordinated PIT, roadblocks, spike strips and helicopter support remain open. | Current living-world tests cover pursuit/search integration, not these tactics. |
| Sprint / point-to-point | Implemented as zero-lap races with ordered directed gates and an endpoint finish. | Portable rules, race catalog validation and a full physical regional sprint pass. |
| Traffic reacting to pursuits | Implemented: nearby traffic receives emergency-yield instructions during an active pursuit. | Living-world pursuit integration passes; traffic-response distances/lanes still need a dedicated behavior test. |
| Photo depth of field and filters | Implemented with focal distance, aperture, exposure and four color looks. | Photo/replay mode state and return behavior pass; visual effect quality and screenshot capture still need human review. |

The design details below remain useful as intended behavior, but the status table above is authoritative for what is implemented and tested.

Each feature extends an existing subsystem rather than adding a separate architecture.

## 1. Rival memory

Implemented: `FADRivalMemory` is bounded and profile-owned; race results update encounter/win/loss counts, respect and grudge; the career subsystem uses the record to vary rival briefing and victory text. Career route automation checks those context variants. A focused serialization/save-reload regression for rival memory remains open.

- Profile capacity is bounded to 32 rivals and 128 encounters per rival.
- Serialization uses the existing atomic profile/backup path.
- Generic profile migration/corruption tests do not isolate rival-memory serialization and save/reload.

## 2. AI driver personalities

Implemented: race definitions load three bounded personality weights, and the AI applies them to pass decisions, braking margins and deterministic pressure mistakes. Vehicle forces and grip remain shared.

- Catalog validation rejects out-of-range weights; the full regional race fixtures exercise the physical AI.
- A dedicated A/B test measuring distinct behavior for each weight remains open.

## 3. Police PIT / roadblocks

`AADPoliceDirector` already runs patrol → pursuit → search → cooldown → busted. PIT and roadblocks are the two "future police systems" items your own architecture doc names as unbuilt.

- **PIT**: heat-gated — an interceptor within a lateral/speed window applies one scripted force impulse to the player's rear quarter. Reuses existing collision/force application; no new vehicle-vehicle physics.
- **Roadblocks**: spawn 1–2 units ahead on the player's road-graph position at high heat, using the same distant-road refill logic `AADTrafficManager` already has for traffic.
- **Effort**: PIT is small (one scripted force + cooldown); roadblocks are medium (needs road-graph lookahead, which `FADRoadNetwork` already exposes).

## 4. Sprint / point-to-point race type

Implemented: `laps: 0` selects a directed A-to-B route while the existing race manager owns gates, timing and classification.

- The catalog validates that the final directed gate is the route endpoint. Portable and full physical sprint tests pass.

## 5. Civilian traffic reacting to pursuits

Implemented: `AADTrafficManager` checks the authoritative police pursuit state and directs nearby civilian AI to yield to the shoulder.

- The response is limited to a 35 m player radius. The living-world test exercises pursuit transitions; a dedicated traffic braking/shoulder assertion remains open.

## 6. Photo mode: depth of field + filters

Implemented: photo mode controls focal distance, aperture and exposure, and offers four built-in color looks.

- `Afterdark.Runtime.PhotoReplay` verifies paused-mode and return-state behavior, but not visual DOF/filter quality or capture success.

## Sequencing

Outstanding checks: add an isolated rival-memory save/reload test, A/B behavior tests for personality weights, and a traffic shoulder/braking assertion during pursuit. PIT and roadblocks remain unimplemented; keep future tactics separate from evidence that already passes.

## Building these

For future implementation, work in the actual repository, keep new behavior data-driven, and run the portable tests, Unreal automation, and packaged checks after changing runtime code. The consolidated report records tested behavior separately from remaining feature work.
