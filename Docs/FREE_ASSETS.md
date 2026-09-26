# Free asset sources

Verified, currently-live sources to replace generated blockout content with real production assets. Follow the same discipline already used for `SM_CC0_CityCar`: record every import in `ASSET_PROVENANCE.md` before it reaches a package, using the template at the bottom of this file. Links and licenses below were checked live before this doc was written — re-verify the license on the actual download page before you import anything, since packs occasionally change terms.

## Where everything goes

Raw downloads (zips, source files, license text) land in `Content/ExternalAssets/<Source>/<AssetName>/` — never imported directly. After an editor import/conversion script (same pattern as `Scripts/Editor/import_cc0_city_car.py`), converted `.uasset`s live in `Content/Velocity/External/<AssetName>/`. Every asset that reaches a package gets one `ASSET_PROVENANCE.md` entry.

## Sources by need

| Need (from `ASSETS_AND_PERFORMANCE.md`) | Source | License | What to grab | Notes |
|---|---|---|---|---|
| District trim kit — curbs, corners, junctions, signage, barriers | [Kenney City Kit (Roads)](https://kenney.nl/assets/city-kit-roads) | CC0 1.0 | Road segments, curbs, barriers, signs, traffic lights (2.1 added signage) | FBX/OBJ/glTF, documented as Unreal-compatible |
| District identity variety — Iron Quay / Glass Coast / Downtown | Kenney's broader City Kit line (commercial, suburban, industrial variants) at [kenney.nl/assets](https://kenney.nl/assets) | CC0 1.0 | The variant matching each district | Same modular grid as Roads, so kits combine cleanly |
| Track dressing — fences, barriers, billboards, pit props | [Kenney Racing Kit](https://opengameart.org/content/racing-kit) | CC0 | 70+ objects, FBX/OBJ/glTF | Explicitly documented as working in Unreal |
| PBR materials — asphalt, concrete, facade, metal | [Poly Haven](https://polyhaven.com/textures) and [ambientCG](https://ambientcg.com) | CC0 | Diffuse/normal/roughness/AO/displacement sets | Can directly replace the six Python-generated textures from `VISUAL_PRESENTATION.md` — same PBR channel layout your bootstrap script already imports |
| One-off high-fidelity environment kits | [Fab](https://www.fab.com) — filter License: Free, check "Limited Time Free" | Varies — some rotate free every 2 weeks, some are permanently free | Whatever currently fits Nova City's look — check what's live now rather than trusting an old pack name | Claim ("buy" for $0) while free and it's yours permanently, even after the window closes |
| UI — buttons, panels, sliders, 2 fonts, 6 UI SFX | [Kenney UI Pack](https://opengameart.org/content/ui-pack) | CC0 | Full sprite set + fonts + sound in one download | Matches your "engine fonts and canvas HUD" gap directly |
| Ambient/impact/environment SFX | [Freesound.org](https://freesound.org) | **Per-file — filter to CC0 only** | Rain, wind, impacts, crowd ambience | Freesound mixes CC0/CC-BY/CC-BY-SA on one site; check every file's own license tag, same as any other external source |
| Engine/exhaust recordings | — | — | — | No good CC0 option exists for a *specific* fictional car's tone. Your synthesized layers stay the right call until you record your own or license a pack — a generic freesound clip won't match a bespoke torque curve anyway |

## What NOT to source externally

`ARCHITECTURE.md` is explicit that the five hero vehicles are original and fictional — that identity (Aster S6, Kestrel XR, Meridian GT) is a real asset, not a placeholder gap. Swapping a generic CC0 mesh in as one of these hero bodies would undercut the one thing meant to be yours. Keep free assets for background/environment dressing — exactly how the CC0 city car is already used — and for generic reusable interior parts (wheels, generic seats) you reskin, not for the five named cars' bodies.

## Provenance entry template

Copy into `ASSET_PROVENANCE.md` for every asset that reaches a package — same fields as the existing `SM_CC0_CityCar` entry:

```
- Source page: [name](url)
- Source file: `Content/ExternalAssets/<Source>/<AssetName>/<file>`
- License stated by publisher: [license name](license url)
- Publisher-provided hash (if any):
- Publisher specifications:
- Runtime representation: `/Game/Velocity/External/<AssetName>/<AssetName>`
- Conversion: `Scripts/Editor/<script_name>.py` — describe what it does
- Imported assets:
- Flattened/converted file hash:
- Importer:

The license and source attribution were checked against the publisher page on <date>.
```
