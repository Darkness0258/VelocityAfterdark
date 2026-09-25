"""Import the flattened CC0 city car as one static background asset."""

import hashlib
from pathlib import Path

import unreal


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SOURCE_GLB = PROJECT_ROOT / "Content/ExternalAssets/3DAssetsDev/CityCar/city_car.glb"
SOURCE_OBJ = PROJECT_ROOT / "Content/ExternalAssets/3DAssetsDev/CityCar/static/city_car_static.obj"
SOURCE_MTL = SOURCE_OBJ.with_suffix(".mtl")
EXPECTED_SHA256 = "6246ac14c89d41c31714cd3524c3e07d5270ad63038bfc38a8d7fbd6435c4877"
DESTINATION = "/Game/Velocity/External/CityCarStaticFinal"
ASSET_PATH = DESTINATION + "/SM_CC0_CityCar.SM_CC0_CityCar"
GENERATED_LEGACY_DESTINATIONS = (
    "/Game/Velocity/External/CityCar",
    "/Game/Velocity/External/CityCarStatic",
)


def main():
    if not SOURCE_GLB.is_file() or not SOURCE_OBJ.is_file() or not SOURCE_MTL.is_file():
        raise RuntimeError("Expected source GLB plus flattened OBJ/MTL. Run flatten_cc0_city_car.py first.")
    actual_hash = hashlib.sha256(SOURCE_GLB.read_bytes()).hexdigest()
    if actual_hash != EXPECTED_SHA256:
        raise RuntimeError(f"City-car source hash changed unexpectedly: {actual_hash}")
    if SOURCE_OBJ.stat().st_size < 1_000_000:
        raise RuntimeError(f"Flattened static OBJ is unexpectedly small: {SOURCE_OBJ.stat().st_size} bytes")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save the current level before importing the CC0 car.")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
        raise RuntimeError("Save edited content before importing the CC0 car.")

    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(SOURCE_OBJ))
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", "SM_CC0_CityCar")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported_paths = list(task.get_editor_property("imported_object_paths"))
    imported_assets = [(path, unreal.EditorAssetLibrary.load_asset(path)) for path in imported_paths]
    imported_meshes = [(path, asset) for path, asset in imported_assets if asset and asset.get_class().get_name() == "StaticMesh"]
    asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if not asset or len(imported_meshes) != 1 or imported_meshes[0][0] != ASSET_PATH:
        raise RuntimeError(f"Expected exactly one static mesh at {ASSET_PATH}; meshes: {[path for path, _ in imported_meshes]}")

    bounds = asset.get_bounds()
    extents = bounds.box_extent
    if not (170.0 <= extents.x <= 190.0 and 85.0 <= extents.y <= 100.0 and 70.0 <= extents.z <= 85.0):
        raise RuntimeError(f"Imported static mesh bounds look wrong: {extents}")
    unreal.EditorAssetLibrary.save_loaded_asset(asset)

    # The earlier GLB test import created only generated assets under this exact
    # project-owned folder. Retire those unused animation/rig assets after the
    # replacement static asset has been imported, saved, and validated.
    for legacy_destination in GENERATED_LEGACY_DESTINATIONS:
        if unreal.EditorAssetLibrary.does_directory_exist(legacy_destination):
            legacy_assets = unreal.EditorAssetLibrary.list_assets(legacy_destination, recursive=True, include_folder=False)
            unreal.log(f"AFTERDARK_CC0_RETIRING_LEGACY_ASSETS: {legacy_destination} contains {len(legacy_assets)} assets")
            if not unreal.EditorAssetLibrary.delete_directory(legacy_destination):
                raise RuntimeError(f"Could not delete generated legacy asset directory {legacy_destination}")
            if unreal.EditorAssetLibrary.does_directory_exist(legacy_destination):
                raise RuntimeError(f"Legacy imported assets remain under {legacy_destination}")

    mesh_count = len(imported_paths)
    unreal.log(f"AFTERDARK_CC0_IMPORT_ASSET: StaticMesh {ASSET_PATH}")
    unreal.log(
        "AFTERDARK_CC0_IMPORT_OK: one flattened static mesh; "
        f"bounds {extents.x * 2:.1f}x{extents.y * 2:.1f}x{extents.z * 2:.1f} cm; "
        f"source SHA-256 {actual_hash}; importer outputs {mesh_count}"
    )


if __name__ == "__main__":
    main()
