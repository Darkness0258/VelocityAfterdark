# Map and post-race grades

This document now records the implemented prototype behavior. It is not a claim that every map or ranking item in the full game specification is complete.

## Exploration map

The offline map pauses free driving and draws the validated road graph in a pannable, zoomable 2D view. Players can select a catalog landmark, filter discovered/undiscovered locations, and route to the selection. It also marks career race starts and the home-garage spawn.

Fast travel is limited to already-discovered landmarks, outside a race or active pursuit. The arrival path uses a safe vehicle placement and clears the driving state; the pursuit gate cannot be bypassed through the map. A single custom waypoint can be placed on the road graph and is stored in the version 7 profile. The eight authored discoveries continue to pay once, only after a grounded, slow arrival.

The editor automation covers road-graph map projection/zoom-pan, waypoint persistence, a full map open/filter/discover/fast-travel flow, safe placement, and pursuit restrictions. This does not establish usability across every resolution or physical controller. Dealer pins, multiple saved custom pins, online map state, and streaming destinations are not implemented.

## Post-race grade

Each classified race result receives a cosmetic D/C/B/A/S/SS/SSS grade from the route's authored par time, total finish time including penalties, recovery count, and final place. A DNF receives D. SSS requires a recovery-free win at 95% of par or faster. The grade is not a career rank and does not change credits, REP, or unlocks.

The result label can be included in photo/replay presentation, and a photo captured from results writes a JSON sidecar with event, place, time, grade, and recovery count. Grade boundary tests cover clean pace, recovery, place, DNF, and invalid inputs. Subjective par-time balance and the visual quality of every grade surface still need hands-on review.
