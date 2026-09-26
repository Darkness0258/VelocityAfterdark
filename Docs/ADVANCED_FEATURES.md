# Advanced features — design notes

Design-level specs only — no C++ here. Each one extends a subsystem that already exists rather than adding new architecture, so the actual implementation belongs somewhere that can compile and test against your real project (see "Building these" below). Ordered by value-per-hour, not dependency order.

## 1. Rival memory

Already specified, never built: `ARCHITECTURE.md`'s Driver data contract lists "personality weights, reaction delay, risk... Rival memories store meaningful encounters and bounded tune evolution." This is the direct continuation of last review's story gap.

- **State to add**: per-rival record in the profile — races against this rival, wins/losses, last chapter faced, a small bounded respect/grudge scalar.
- **Owner**: bump the profile to schema 6 the same way `UADOwnershipSubsystem` already migrated 1→5. Same atomic write, backup, and readback — no new persistence mechanism.
- **Where it surfaces**: the `briefing`/`victory line` text fields `UADCareerSubsystem` already reads per chapter. A rival who's lost twice gets different pre-race text than one who just beat you. Zero new UI.
- **Effort**: small — a data field and text branching, not new gameplay.

## 2. AI driver personalities

Right now Rhea/Mako/Tess differ only by difficulty-scaled pace ("Difficulty scales planned pace, without extra torque or player-relative catch-up" — `ARCHITECTURE.md`). The Driver contract already reserves "personality weights, reaction delay, risk"; unused.

- **What's new**: 2–3 named weights per opponent (overtake aggression, braking-point conservatism, mistake frequency under pressure) read by `UADRaceDriverComponent`'s existing pure-pursuit/braking logic as multipliers on decisions, not new systems.
- **Hard constraint to keep**: identical physics for every car — your docs already guarantee this. Personality changes *decisions*, never grip or torque.
- **Effort**: medium — mostly tuning; the hook points already exist.

## 3. Police PIT / roadblocks

`AADPoliceDirector` already runs patrol → pursuit → search → cooldown → busted. PIT and roadblocks are the two "future police systems" items your own architecture doc names as unbuilt.

- **PIT**: heat-gated — an interceptor within a lateral/speed window applies one scripted force impulse to the player's rear quarter. Reuses existing collision/force application; no new vehicle-vehicle physics.
- **Roadblocks**: spawn 1–2 units ahead on the player's road-graph position at high heat, using the same distant-road refill logic `AADTrafficManager` already has for traffic.
- **Effort**: PIT is small (one scripted force + cooldown); roadblocks are medium (needs road-graph lookahead, which `FADRoadNetwork` already exposes).

## 4. Sprint / point-to-point race type

Listed as open in `MILESTONES.md`. Doesn't need a new race manager — `AADRaceManager` already handles route + gates + classification generically.

- **What's new**: a race JSON with zero laps (A to B) instead of a closed loop. `ADRaceRules::Progress` already works on directed gates, not specifically on laps, so this is mostly new catalog entries plus a route, not new code.
- **Effort**: small.

## 5. Civilian traffic reacting to pursuits

`AADTrafficManager` and `AADPoliceDirector` don't talk to each other yet. Good payoff for little new state.

- **What's new**: traffic cars within a radius of an active pursuit brake/pull to the shoulder instead of running normal route logic — one new state on the existing pooled traffic actor, checked at the traffic manager's existing update rate.
- **Effort**: small–medium — no new actors, a state flag and a radius check.

## 6. Photo mode: depth of field + filters

Cheapest item here. `UADCinematicComponent` already exposes translation/rotation/FOV/exposure.

- **What's new**: a post-process material parameter collection driven by the same photo-mode input handler — DOF focal distance/aperture, 3–4 LUT filters.
- **Effort**: small, no gameplay/save implications.

## Sequencing

Do 1 and 2 first. They're cheap and they're what makes eight chapters feel different from each other instead of "same race, new paint" — the exact gap flagged last time.

## Building these

This is behavior and data design, not implementation. For the actual C++: point **Claude Code** at your real repo. It can read the real `AADPoliceDirector.cpp`, run `Scripts/Test-Core.ps1` and `Invoke-Afterdark.ps1 -Action Build` after every change, and catch a broken build immediately — none of which is possible from a chat window with no Unreal Engine or MSVC toolchain behind it. Feed it one feature at a time, review each diff, commit it. That also fixes the one-commit-total problem from last time.
