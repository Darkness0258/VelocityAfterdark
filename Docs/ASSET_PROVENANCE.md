# External asset provenance

## City car

- Source page: [City car, Car Park and Road Vehicle Fleet](https://3dassets.dev/assets/car-park-and-road-vehicle-fleet-city-car-033e3215)
- Source file: `Content/ExternalAssets/3DAssetsDev/CityCar/city_car.glb`
- License stated by the publisher: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)
- Publisher-provided source hash: SHA-256 `6246AC14C89D41C31714CD3524C3E07D5270AD63038BFC38A8D7FBD6435C4877`
- Publisher specifications: generic three-door city-car design, about 3.59 m long, 31,000 triangles, eight materials and rigid-transform door/bonnet/boot/steering/wheel clips.
- The publisher marks this model as AI-assisted. Its shape is a generic background vehicle and it does not replace the game's original named race cars.
- Runtime representation: one flattened, UV-complete static mesh at `/Game/Velocity/External/CityCarStaticFinal/SM_CC0_CityCar`, instanced three times as non-colliding avenue dressing. The source animation rig was intentionally removed; the model is not a player or traffic vehicle.
- Conversion: `Scripts/Editor/flatten_cc0_city_car.py` applies the default scene transforms, converts glTF axes/metres to Unreal axes/centimetres, retains eight material groups with base-color/roughness/metallic approximations, writes UVs and strips animation data.
- Imported assets: `Content/Velocity/External/CityCarStaticFinal/SM_CC0_CityCar.uasset` plus eight generated material assets. The previous animated import trees were deleted after the flattened mesh passed bounds validation.
- Flattened OBJ SHA-256: `a91c6a1df5b9fc8e62a27776af23b3a94e382cccc249df73372426d545f700ae` (31,000 triangles; 359.2 × 182.8 × 153.3 cm).
- Importer: `Scripts/Editor/import_cc0_city_car.py`, using UE 5.8 Interchange. Runtime package staging and a three-instance district marker are required for packaged smoke acceptance.

The license and source attribution were checked against the publisher page on 2026-09-25. Keep this record with any distributed source or cooked project content.

## Asphalt Track surface

- Source page: [Asphalt Track](https://polyhaven.com/a/asphalt_track)
- Source files: `Content/ExternalAssets/PolyHaven/AsphaltTrack/asphalt_track_diff_2k.jpg`, `asphalt_track_nor_dx_2k.jpg`, and `asphalt_track_rough_2k.jpg`
- License stated by the publisher: [CC0 1.0](https://polyhaven.com/license)
- Publisher-provided MD5 hashes: diffuse `faec159f1f29d231581c32bd256b5ede`; DirectX normal `81fb6473062c9a2de8b59746723a345c`; roughness `213b9f13c03772e24a9cd55c4e43f391`
- Publisher specifications: 2K JPG PBR maps; weathered outdoor asphalt-track surface, 2 m wide.
- Runtime representation: `/Game/Velocity/External/AsphaltTrack/` texture assets and `/Game/Velocity/Materials/M_Asphalt_PolyHaven`.
- Conversion: `Scripts/Editor/import_polyhaven_asphalt.py` imports the 2K color, DirectX normal and roughness maps, creates an instanced-mesh-compatible PBR material, and keeps the roughness and base-color controls used by the dynamic weather system.
- Imported assets: `/Game/Velocity/External/AsphaltTrack/T_AsphaltTrack_Diffuse_2K`, `T_AsphaltTrack_NormalDX_2K`, and `T_AsphaltTrack_Roughness_2K`; `/Game/Velocity/Materials/M_Asphalt_PolyHaven`.
- Source file SHA-256 hashes: diffuse `742d1d79560bd661d5871afc857e981e25dcd563bfd24385cbd063ebcaeef06c`; DirectX normal `33e5f2d8af1e23653b7849cf3e45830cd36e81fb3614c9065c2708ea1ff846e2`; roughness `b934fb6fbe05770df95fa94fee3a12d45eb121621ce96e9b262f403d49ee493e`.
- Importer: Unreal Engine 5.8 Editor Python via `Scripts/Editor/import_polyhaven_asphalt.py`.

The asset page and CC0 terms were checked against the publisher on 2026-09-26. Keep this record with any distributed source or cooked project content.
