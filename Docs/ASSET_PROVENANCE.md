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
