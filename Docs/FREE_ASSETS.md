# Free asset sources

Candidate sources for replacing generated blockout content. Selected Kenney city kits and the Poly Haven asphalt texture are already imported and documented; the other packs below are suggestions, not project dependencies. Record every new import in the root or `Docs/ASSET_PROVENANCE.md` before it reaches a package, using the template at the bottom. Listing pages and stated licenses were rechecked on 2026-09-27 for the named packs below, but always verify the exact product/license/EULA shown when downloading.

## Where everything goes

Raw downloads (zips, source files, license text) land in `Content/ExternalAssets/<Source>/<AssetName>/` — never imported directly. After an editor import/conversion script (same pattern as `Scripts/Editor/import_cc0_city_car.py`), converted `.uasset`s live in `Content/Velocity/External/<AssetName>/`. Every asset that reaches a package gets one `ASSET_PROVENANCE.md` entry.

## Sources by need

| Need (from `ASSETS_AND_PERFORMANCE.md`) | Source | License | What to grab | Notes |
|---|---|---|---|---|
| District trim kit — curbs, corners, junctions, signage, barriers | [Kenney City Kit (Roads)](https://kenney.nl/assets/city-kit-roads) | CC0 1.0 | Road segments, curbs, barriers, signs, traffic lights | The current 2.1 official listing includes road signs and traffic lights; a selected subset is already imported. |
| District identity variety — Iron Quay / Glass Coast / Downtown | [Kenney City Kit (Commercial)](https://kenney.nl/assets/city-kit-commercial) and [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | CC0 1.0 | Skyscrapers, industrial buildings and props | Selected subsets are already imported; verify scale, pivots and materials before placing more. |
| Track dressing — fences, barriers, billboards, pit props | [Kenney Racing Kit](https://opengameart.org/content/racing-kit) | CC0 | 70+ objects; OBJ, FBX and glTF formats | The listing says it works in Unreal; still test import scale, materials and collision in this project. |
| PBR materials — asphalt, concrete, facade, metal | [Poly Haven](https://polyhaven.com/textures) and [ambientCG](https://ambientcg.com) | CC0 per their asset terms | Diffuse/base-color, normal, roughness, AO and displacement where available | The Asphalt Track texture is already imported. Check channel conventions and resolution before adding maps; generated project textures remain separate. |
| One-off environment kits | [Fab](https://www.fab.com) — browse Free and Limited-Time Free offers | Varies by product and selected license | Select only assets that fit Nova City's look and target hardware | Fab's official guide says free products still require accepting the applicable EULA; a free offer is not a blanket license. Preserve the listing, license tier and acquisition record. |
| UI — buttons, panels, sliders and icons | [Kenney UI Pack](https://kenney.nl/assets/ui-pack) | CC0 | Review the 430-file pack for suitable sprites | The listing identifies UI elements; do not assume it contains fonts or sound until the downloaded archive is checked. |
| Ambient/impact/environment SFX | [Freesound.org](https://freesound.org) | Per-file Creative Commons license | Rain, wind, impacts and crowd ambience | Filter to CC0 where practical and record each selected sound's own license and attribution conditions. |
| Engine/exhaust recordings | — | — | — | No good CC0 option exists for a *specific* fictional car's tone. Your synthesized layers stay the right call until you record your own or license a pack — a generic freesound clip won't match a bespoke torque curve anyway |

## What NOT to source externally

`ARCHITECTURE.md` is explicit that the five hero vehicles are original and fictional. Do not use generic downloaded meshes as their bodies. Keep third-party content to documented environment/background dressing and generic reusable interior parts that are appropriately adapted; the imported city car remains non-colliding scenery, not a player or traffic car.

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

The 2026-09-27 live check confirmed CC0 on the official Kenney City Kit Roads, Commercial, Industrial and UI Pack pages; the Kenney Racing Kit listing also states CC0; Poly Haven states all its assets use CC0; and ambientCG's current asset listing states its assets are CC0. Fab product licenses differ, so inspect each product's selected license and EULA. Freesound licenses are attached to individual sound uploads. [Fab download/license guidance](https://dev.epicgames.com/documentation/fab/purchasing-and-downloading-assets-in-fab), [Freesound licensing FAQ](https://freesound.org/help/faq/).
