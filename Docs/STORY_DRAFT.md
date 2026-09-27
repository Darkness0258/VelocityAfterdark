# Career story draft

The authored chapter text below has been integrated into `Content/Data/Career/career.json` and is used by the eight-chapter offline career. It remains editable draft dialogue; integration does not mean that every crew, character, or story system in the product specification exists.

| Chapter | Crew | Leader | Briefing | Victory line |
|---|---|---|---|---|
| Dockside | Iron Wolves | Dax Kerr | Rain on the viaduct, engines under it. Mara Venn walks you in — Dax doesn't take strangers seriously until they've won something. | Dax nods once. That's the whole conversation. It's enough. |
| Foundry Shift | Night Serpents | Ivo Renn | Ivo Renn doesn't do trash talk. He runs a clean line and expects you to keep up or get out of the way. | Ivo says nothing. He's already recalculating your line for next time. |
| Iron Quay | Night Serpents | Ivo Renn | Same driver, same question: was Foundry Shift luck? Ivo wants the real answer. | First real word from Ivo Renn: "Again." From him, that's respect. |
| Night Survey | Signal Red | Sel Arden | Sel Arden's vouching for you with Signal Red, staking her own name on it. Don't make her regret it. | Sel doesn't celebrate. She just tells Signal Red you're theirs now. |
| Northfield Run | Signal Red | Sel Arden | Open road, open throttle — and word's out. Someone's asking who's climbing the ranks this fast. | Sel clocks the time and says it flat: "You're not quiet anymore." |
| Glass Coast | Obsidian Run | Reya Voss | Reya Voss doesn't race for REP. She races because losing in public is the one thing money can't fix. | Reya loses gracefully in front of everyone who matters to her — and never forgives you for making her do it there. |
| Sable Ring | Ghost Circuit | Wren Ashby | Ivo Renn's watching from the ring's edge tonight. Wren doesn't like being anyone's undercard. | Wren steps back. Ivo steps forward. Some things were always going to end up between the two of you. |
| Afterdark Final | All five crews present | Ivo Renn | Sel Arden gets you through the door. Every crew you've raced is watching. Ivo Renn is waiting. | Afterdark Champion. Ivo doesn't say "again" this time — he says your name. |

## Implementation notes

- The chapter IDs point to the authored Dockside, Foundry Shift, Iron Quay, Night Survey, Northfield Run, Glass Coast, Sable Ring, and Afterdark Final races. Ivo and Sel recur as stable AI rival IDs, so the saved rival-memory system can change their briefing and victory context after previous encounters. Mara is mentioned as the mechanic; she is not a race opponent.
- The opening arrival is an in-engine real-time camera sequence with multiple authored shots. It holds player driving controls and the chassis safely while the city continues simulating. It is not a prerecorded video, motion-captured character scene, or voiced performance.
- Crew territories, bespoke crew vehicles/AI, modeled and voiced character performances, and a broader branching story are not part of this implemented eight-race career slice. Review original names for release clearance before shipping.
