"""Import a curated, instancing-friendly subset of the verified Kenney CC0 kits."""

import hashlib
import json
from pathlib import Path

import unreal


PROJECT_ROOT = Path(__file__).resolve().parents[2]
CONTENT = PROJECT_ROOT / "Content/ExternalAssets/Kenney"
DESTINATIONS = {
    "Commercial": "/Game/Velocity/External/KenneyCityKitCommercial",
    "Industrial": "/Game/Velocity/External/KenneyCityKitIndustrial",
    "Roads": "/Game/Velocity/External/KenneyCityKitRoads",
}


def models(kit, names):
    relative_folder = "Models/FBX format" if kit == "Roads" else "Source/Models/FBX format"
    folder = CONTENT / f"CityKit{kit}/{relative_folder}"
    return [
        {
            "kit": kit,
            "source": folder / f"{filename}.fbx",
            "name": f"SM_Kenney_{kit}_{asset_name}",
            "package": f"{DESTINATIONS[kit]}/SM_Kenney_{kit}_{asset_name}",
        }
        for filename, asset_name in names
    ]


ASSETS = (
    models("Commercial", [
        ("building-skyscraper-a", "SkyscraperA"),
        ("building-skyscraper-b", "SkyscraperB"),
        ("building-skyscraper-c", "SkyscraperC"),
        ("building-skyscraper-d", "SkyscraperD"),
        ("building-skyscraper-e", "SkyscraperE"),
        ("building-a", "BuildingA"),
        ("building-d", "BuildingD"),
        ("building-h", "BuildingH"),
        ("building-n", "BuildingN"),
    ])
    + models("Industrial", [
        ("building-a", "BuildingA"),
        ("building-d", "BuildingD"),
        ("building-h", "BuildingH"),
        ("building-m", "BuildingM"),
        ("building-q", "BuildingQ"),
        ("building-t", "BuildingT"),
        ("shipping-container-a", "ContainerA"),
        ("shipping-container-c", "ContainerC"),
        ("chimney-large", "ChimneyLarge"),
        ("detail-tank-large", "TankLarge"),
        ("water-tower", "WaterTower"),
    ])
    + models("Roads", [
        ("light-curved", "LampCurved"),
        ("light-square", "LampSquare"),
        ("traffic-light", "TrafficLight"),
        ("traffic-light-object-vertical", "TrafficLightObjectVertical"),
        ("traffic-light-hanging", "TrafficLightHanging"),
        ("construction-barrier", "ConstructionBarrier"),
    ])
)


def checked(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest().upper()


def main():
    for asset in ASSETS:
        checked(asset["source"].is_file(), f"Missing curated source file: {asset['source']}")
    for kit in DESTINATIONS:
        source_license = CONTENT / f"CityKit{kit}/Source/License.txt" if kit != "Roads" else CONTENT / "CityKitRoads/License.txt"
        checked(source_license.is_file(), f"Missing bundled license: {source_license}")
        checked("Creative Commons Zero" in source_license.read_text(errors="replace"),
                f"The bundled {kit} license is no longer CC0.")
    checked(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),
            "Save the current level before importing the Kenney city kit.")
    checked(not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(),
            "Save edited content before importing the Kenney city kit.")

    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    static_data = options.get_editor_property("static_mesh_import_data")
    if static_data:
        static_data.set_editor_property("combine_meshes", True)
        options.set_editor_property("static_mesh_import_data", static_data)

    tasks = []
    source_hashes = {}
    for asset in ASSETS:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(asset["source"]))
        task.set_editor_property("destination_path", DESTINATIONS[asset["kit"]])
        task.set_editor_property("destination_name", asset["name"])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        task.set_editor_property("options", options)
        tasks.append(task)
        source_hashes[str(asset["source"].relative_to(PROJECT_ROOT)).replace("\\", "/")] = digest(asset["source"])

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()

    manifest = []
    for item, task in zip(ASSETS, tasks):
        asset = unreal.EditorAssetLibrary.load_asset(item["package"])
        checked(asset is not None and asset.get_class().get_name() == "StaticMesh",
                f"Expected a static mesh at {item['package']}; importer returned {task.get_editor_property('imported_object_paths')}")
        bounds = asset.get_bounds()
        extents = bounds.box_extent
        dimensions = (float(extents.x * 2), float(extents.y * 2), float(extents.z * 2))
        checked(all(1.0 <= value <= 200000.0 for value in dimensions),
                f"Invalid imported mesh bounds for {item['package']}: {dimensions}")
        checked(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
                f"Could not save imported mesh: {item['package']}")
        package_file = PROJECT_ROOT / "Content" / item["package"].replace("/Game/", "").replace("/", "\\")
        package_file = package_file.with_suffix(".uasset")
        checked(package_file.is_file(), f"Unreal reported an import but did not write {package_file}")
        entry = {
            "kit": item["kit"],
            "source": str(item["source"].relative_to(PROJECT_ROOT)).replace("\\", "/"),
            "source_sha256": source_hashes[str(item["source"].relative_to(PROJECT_ROOT)).replace("\\", "/")],
            "package": item["package"],
            "uasset": str(package_file.relative_to(PROJECT_ROOT)).replace("\\", "/"),
            "uasset_sha256": digest(package_file),
            "bounds_cm": [round(value, 2) for value in dimensions],
            "imported_objects": [str(path) for path in task.get_editor_property("imported_object_paths")],
        }
        manifest.append(entry)
        unreal.log(f"AFTERDARK_KENNEY_MESH: {item['package']} {dimensions[0]:.1f}x{dimensions[1]:.1f}x{dimensions[2]:.1f} cm")

    report = PROJECT_ROOT / "Artifacts/KenneyImport/manifest.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    unreal.log(f"AFTERDARK_KENNEY_IMPORT_OK: {len(manifest)} static meshes; report {report}")


if __name__ == "__main__":
    main()
