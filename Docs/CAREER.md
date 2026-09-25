# Career implementation scope

The career catalog implements an eight-chapter challenge series on **eight distinct circuits** across the connected development road network, with three physical opponents per event. This is a bounded progression slice, not the complete Nova City campaign. The fictional crew names, leaders and dialogue in `Content/Data/Career/career.json` are original. They describe invitations and relationships; the named leaders are not additional spawned characters or bespoke AI drivers.

## Catalog and progression contract

`UADCareerSubsystem` owns immutable chapter and reputation-rank definitions. It loads `Content/Data/Career/career.json` once when the game instance initializes. The catalog has a version, ordered `chapters` and ordered `ranks`. Chapter IDs are stable save identifiers; new chapters must be appended or accompanied by an explicit save migration. Reordering or removing completed chapters intentionally makes progression unavailable instead of silently unlocking unrelated content.

Each chapter supplies a title, crew, leader, briefing, victory line, existing race ID, difficulty, winning prize, finishing stipend and winning REP. Every chapter resolves its own event through `Content/Data/Races/catalog.json`: Dockside, Foundry Shift, Iron Quay, Night Survey, Northfield Run, Glass Coast, Sable Ring and Afterdark Final. Difficulty follows the existing Easy/Normal/Hard race settings, with no new opponent stat multipliers or rubber-band assistance. The events use different routes and checkpoint layouts. They remain circuit events; there are no bespoke crew vehicles or hidden challenge conditions.

The current chapter is the next entry after the saved `CompletedChapters` prefix. A first-place finish awards that chapter's `WinCredits` and `RepReward`, then appends its ID to that prefix. Places 2-4 award `FinishCredits`, grant no REP and leave the chapter available to retry. A DNF, aborted race or unfinished classification is ineligible. Finishing every chapter awards 8,000 REP in total, enough for all eight rank names from Unknown through Afterdark Champion. Completing the series leaves no active chapter; free driving and ordinary races remain separate from career rewards.

`ComputeReward(Profile, ChapterId, Place, OutReward, OutError)` only computes a proposed reward. It rejects unavailable catalogs, unknown or already completed chapters, completion holes or duplicates, invalid REP, places outside 1-4 and REP overflow. Its output is cleared on every failure. It does not trust or inspect a live race and does not write a profile.

## Integration responsibilities

The race/ownership integration must perform all of the following before committing a payment:

1. Capture the active chapter ID when the race starts, match its `RaceId` to the loaded race, and use the chapter's prescribed difficulty.
2. Derive the player's place from the authoritative race manager's finished result, excluding DNF and abandoned races. Never take a HUD-supplied place as proof of a finish.
3. Assign a unique run receipt, compute the reward against the current profile, and persist credits, REP, completed chapter, counters and receipt in one ownership transaction.
4. Reject a repeated receipt and preserve the old in-memory profile if saving fails. Keep results available for a save retry without starting a new race or awarding twice.
5. Display story and rank text as local series progress. Do not imply that the named crew leader has a unique spawned model or AI personality.

Career decisions are local single-player rules. Checksums and receipts are corruption/duplicate-payment protections, not online anti-cheat. Future competitive services must validate race results and currency on a server.

## Data error handling

The parser rejects missing or oversized catalogs, malformed JSON, unsupported versions, wrong JSON types, fractional or non-finite numeric values, unsupported difficulty, blank/oversized text, control characters, malformed/duplicate IDs, finish stipends larger than winning prizes, and unreachable highest ranks. The first rank must start at zero and later thresholds must increase strictly. Arrays are limited to 64 ranks or chapters and files to 128 KB. A failed reload preserves the previous valid catalog; a failed initial load reports an error and leaves career unavailable.

## Validation scope

The integration batch is now being built and tested. Catalog and ordered-progression automation plus seven physical regional race variants are prepared. This document is not a runtime pass claim. The batch should include malformed catalog fixtures; rank boundaries; all eight sequential wins; a paid non-winning finish without advancement; mismatched chapter/route/difficulty; DNF and cancellation; repeated receipt; failed-save rollback and retry; save/relaunch progression; invalid completion order; and final-series completion. Runtime checks should also confirm ordinary races do not accidentally award a career result.

## Remaining campaign work

District territories and unlocks, sprint/touge event rules, modeled and voiced characters, bespoke crew AI, rival memory, authored dealership locations, skill REP combos, cinematics and the full narrative championship remain unfinished. The separate garage now provides a three-car ownership and purchasing loop; the finale is currently a catalog-defined circuit event. The separate exploration director now connects eight one-time discoveries to the shared ownership profile; these are not additional career races. The catalog structure can carry additional authored chapters when those playable systems and locations exist.
