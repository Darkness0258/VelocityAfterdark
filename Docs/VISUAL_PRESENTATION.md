# Visual presentation pass — 2026-09-26

This pass improves the visible UE 5.8 district and driving interface while keeping the current DX11/SM5 path and low-memory target. It is a presentation polish pass over the existing vertical slice, not a replacement for production vehicle and environment art.

## What changed

- Driving HUD content now sits in a compact control rail; the career card is out of the road's center, and the speed, body condition, and nitrous readout use one panel. The persistent generic garage notice was removed from the drive view.
- The chase camera is farther back and slightly higher with a shallower pitch. The cockpit camera was moved clear of the procedural roof mesh, and the headlight output was reduced to control the large white hotspot.
- Building windows now use narrow, vertical proportions. Night sky fill was raised for improved readability while retaining the cold/warm street-light palette.
- Added a deterministic Python texture generator for six seamless 512-square surface/normal maps, plus an Unreal editor bootstrap that imports them and creates asphalt, facade, and concrete PBR materials. The imported city-prop materials are enabled for HISM rendering. The package script now guards the material UAssets and all six texture assets with their bulk data in the cooked manifest.

## Validation

- `python Scripts/validate_source.py` passed after the source changes: 149 project text files and the configured source/data contracts.
- Unreal editor build, generated texture import, and material compilation passed through `Scripts/Invoke-Afterdark.ps1 -Action UpgradeVisuals`.
- Four editor captures were produced and visually inspected. The fresh cooked Windows package passed `Scripts/Smoke-Package.ps1 -PackageDirectory Artifacts/Package/Windows -OutputName VisualPolishPackageSmoke`; the runtime log reports all four capture requests, `AFTERDARK_RENDER_SMOKE_COMPLETE`, and a clean exit. The harness also confirmed the cooked data loads and three background city cars are instantiated.
- The current cooked manifest contains the three PBR materials and all six generated texture assets with their bulk data. The repeatable package check now guards those files. This visual-only package smoke does not repeat the broader race, garage persistence, multiplayer, device, or performance matrix.

Captured package views are in `Artifacts/VisualPolishPackageSmoke/`: `AfterdarkTitle.png`, `AfterdarkChase.png`, `AfterdarkHood.png`, and `AfterdarkCockpit.png`. The smoke log and frame capture are `runtime.log` and `frames.csv` in that directory. The package is `Artifacts/Package/Windows/` and its current executable/container hashes are recorded in `Artifacts/build-proof.json`.

## Remaining art limits

The fresh images still show the project's generated blockout city and procedural player car. Surface maps add material response but do not provide authored facade geometry, detailed road markings, hero-car bodywork/interiors, or a complete wet-reflection solution. This project uses its conventional DX11/SM5 rendering path; these changes do not add Lumen or hardware ray tracing. Production-quality realism needs original high-detail car and district assets, authored lighting/reflection captures, and further measured tuning. No claim of AAA visual acceptance or full-game completion is made.
