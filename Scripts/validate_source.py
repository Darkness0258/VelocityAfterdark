"""Validate source content and contracts without claiming to build Unreal C++."""
from __future__ import annotations

import ast
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_json(path: Path):
    def reject_constant(value):
        raise ValueError(f"Non-finite JSON value: {value} in {path}")
    return json.loads(path.read_text(encoding="utf-8-sig"), parse_constant=reject_constant)


def validate_integrity() -> None:
    """Reject damaged text and missing project includes before invoking UHT.

    This is an integrity guard, not a replacement for the compiler or asset loader.
    Generated Unreal binary packages are intentionally outside this text scan.
    """
    extensions = {".cpp", ".h", ".cs", ".ini", ".json", ".py", ".ps1", ".md"}
    paths = [ROOT / "VelocityAfterdark.uproject"]
    for directory in ("Source", "Config", "Content/Data", "Scripts", "Tests", "Docs"):
        paths.extend(p for p in (ROOT / directory).rglob("*")
                     if p.is_file() and p.suffix in extensions)
    include_roots = [ROOT / "Source/VelocityAfterdark/Public",
                     ROOT / "Source/VelocityAfterdark/Private"]
    for path in paths:
        data = path.read_bytes()
        relative = path.relative_to(ROOT)
        require(data, f"Empty project text file: {relative}")
        require(not any(byte < 32 and byte not in (9, 10, 13) for byte in data),
                f"Damaged project text (unexpected control bytes): {relative}")
        text = data.decode("utf-8-sig", errors="strict")
        if path.suffix == ".json":
            load_json(path)
        if path.suffix == ".py":
            for native_type in re.findall(r'\bunreal\.(AD[A-Z]\w*)\b', text):
                require(any((base / "Core" / f"{native_type}.h").is_file() for base in include_roots),
                        f"Missing native Python bridge {native_type} used by {relative}")
        if path.suffix in {".h", ".cpp"}:
            for name in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
                if Path(name).name.startswith("AD") and not name.endswith(".generated.h"):
                    require(any((base / name).is_file() for base in [path.parent, *include_roots]),
                            f"Missing project include {name} in {relative}")
    print(f"PASS: {len(paths)} project text files passed integrity checks.")


def main() -> None:
    validate_integrity()
    project = load_json(ROOT / "VelocityAfterdark.uproject")
    require(project["EngineAssociation"] == "5.8", "Unexpected target engine")
    require(project["Modules"][0]["Name"] == "VelocityAfterdark", "Module mismatch")
    plugins = {p["Name"] for p in project["Plugins"] if p["Enabled"]}
    require({"EnhancedInput", "ProceduralMeshComponent", "PythonScriptPlugin"} <= plugins,
            "Required engine plugins are absent")

    car = load_json(ROOT / "Content/Data/Vehicles/aster_s6.json")
    require(car["schemaVersion"] == 1, "Vehicle schema is unsupported")
    require(car["vehicleId"] == "aster_s6", "Vehicle ID mismatch")
    require(car["bodyLengthCm"] == 456 and car["bodyWidthCm"] == 176, "Starter coachwork dimensions are invalid")
    require(car["drivetrain"] in {"RWD", "FWD", "AWD"}, "Invalid drivetrain")
    require(500 <= car["massKg"] <= 5000, "Unsafe car mass")
    require(0 < car["idleRpm"] < car["downshiftRpm"] < car["upshiftRpm"] < car["redlineRpm"],
            "RPM and shift thresholds are inconsistent")
    curve = car["torqueCurve"]
    require(len(curve) >= 2, "Torque curve requires at least two samples")
    require(all(a["rpm"] < b["rpm"] for a, b in zip(curve, curve[1:])), "Unordered torque curve")
    require(curve[0]["rpm"] <= car["idleRpm"] and curve[-1]["rpm"] >= car["redlineRpm"],
            "Torque curve does not cover operating RPM")
    require(all(0 <= p["torqueNm"] <= 5000 for p in curve), "Unsafe torque curve")
    ratios = car["gearRatios"]
    require(1 <= len(ratios) <= 10 and all(0 < g <= 8 for g in ratios), "Invalid gears")
    require(all(a > b for a, b in zip(ratios, ratios[1:])), "Gears must decrease monotonically")
    require(len(car["wheelAnchorsCm"]) == 4, "Four wheel anchors required")
    require(0 < car["drivetrainEfficiency"] <= 1, "Invalid efficiency")
    require(0 < car["frontBrakeBias"] < 1, "Invalid brake bias")
    require(car["wheelRadiusM"] > 0 and car["springRateNPerM"] > 0, "Invalid wheel/spring")
    compression = car["massKg"] * 9.81 / (4 * car["springRateNPerM"])
    require(compression < car["suspensionTravelM"], "Car bottoms out under its static load")
    require(car["suspensionRestLengthM"] - compression + car["wheelRadiusM"] > 0.24,
            "Chassis intersects road at estimated static equilibrium")
    horsepower = max(p["torqueNm"] * p["rpm"] / 7127.0 for p in curve)

    dealer = load_json(ROOT / "Content/Data/Vehicles/dealership.json")
    listings = dealer["vehicles"]
    vehicle_ids = {"aster_s6", "kestrel_xr", "meridian_gt", "ironwake_v8", "vespera_r9"}
    require(dealer["schemaVersion"] == 1 and len(listings) == len(vehicle_ids),
            "Vehicle dealer roster is incomplete or has an unsupported schema")
    require({entry["vehicleId"] for entry in listings} == vehicle_ids,
            "Dealer IDs must match the complete data-driven vehicle roster")
    require(len({entry["vehicleId"] for entry in listings}) == len(listings), "Duplicate dealer vehicle ID")
    for entry in listings:
        definition = load_json(ROOT / f"Content/Data/Vehicles/{entry['vehicleId']}.json")
        require(definition["vehicleId"] == entry["vehicleId"], "Dealer ID and vehicle file do not match")
        require(definition["bodyStyle"] in {"coupe", "hatchback", "sedan", "muscle", "hypercar"},
                f"Unsupported presentation silhouette for {entry['vehicleId']}")
        require(320 <= definition["bodyLengthCm"] <= 650 and 140 <= definition["bodyWidthCm"] <= 260,
                f"Invalid visual/collision dimensions for {entry['vehicleId']}")
        require(500 <= definition["massKg"] <= 5000 and definition["drivetrain"] in {"FWD", "RWD", "AWD"},
                f"Invalid mass or drivetrain for {entry['vehicleId']}")
        require(0 <= entry["price"] <= 10000000 and entry["description"].strip(),
                f"Invalid dealer listing for {entry['vehicleId']}")
        samples = definition["torqueCurve"]
        require(samples[0]["rpm"] <= definition["idleRpm"] < definition["redlineRpm"] <= samples[-1]["rpm"],
                f"Torque curve does not span the full RPM range for {entry['vehicleId']}")
        gear_set = definition["gearRatios"]
        require(2 <= len(gear_set) <= 10 and all(a > b > 0 for a, b in zip(gear_set, gear_set[1:])),
                f"Invalid gear progression for {entry['vehicleId']}")

    world = load_json(ROOT / "Content/Data/World/dockside.json")
    require(world["schemaVersion"] == 1 and world["units"] == "centimeters", "Invalid world schema")
    roads = world["roads"]
    require(len({r["id"] for r in roads}) == len(roads), "Duplicate road IDs")
    half = world["groundHalfExtent"]
    width = world["roadWidth"]
    require(500 <= width <= 4000, "Unexpected drivable road width")
    length_cm = 0.0
    for road in roads:
        start, end = road["start"], road["end"]
        require(len(start) == len(end) == 2, "Invalid road endpoint")
        require((start[0] == end[0]) != (start[1] == end[1]), "Road must be nonzero and axis-aligned")
        for point in (start, end):
            require(all(abs(point[i]) + width / 2 < half[i] for i in (0, 1)), "Road leaves ground collision")
        length_cm += math.dist(start, end)

    def intersects(a, b):
        return all(max(min(a["start"][i], a["end"][i]), min(b["start"][i], b["end"][i])) <=
                   min(max(a["start"][i], a["end"][i]), max(b["start"][i], b["end"][i]))
                   for i in (0, 1))

    visited, pending = {0}, [0]
    while pending:
        current = pending.pop()
        for index, road in enumerate(roads):
            if index not in visited and intersects(roads[current], road):
                visited.add(index)
                pending.append(index)
    require(len(visited) == len(roads), "Disconnected road network")
    require(300000 <= length_cm <= 500000, "Road network outside 3–5 km target")

    atmosphere = load_json(ROOT / "Content/Data/World/atmosphere.json")
    require(atmosphere["schemaVersion"] == 1 and atmosphere["initialHour"] == 23,
            "Opening atmosphere must start at night")
    require(atmosphere["initialWeather"] == "Rain" and "Rain" in atmosphere["weatherSequence"],
            "The opening drive must start in supported rain weather")
    require(0.8 <= atmosphere["initialRainAmount"] <= 1 and
            0.4 <= atmosphere["initialWetness"] <= 1,
            "The opening rain should be visible and influence grip immediately")
    require(512 <= atmosphere["rainCount"] <= 2048 and
            500 <= atmosphere["rainRadiusCm"] <= 5000 and
            500 <= atmosphere["rainHeightCm"] <= 3000,
            "Opening rain emitter density and bounds must remain in the supported performance range")
    for block in world["blocks"]:
        require(all(a < b for a, b in zip(block["min"], block["max"])), "Invalid block bounds")
        for road in roads:
            corridor_min = [min(road["start"][i], road["end"][i]) - width / 2 for i in (0, 1)]
            corridor_max = [max(road["start"][i], road["end"][i]) + width / 2 for i in (0, 1)]
            overlap = all(block["min"][i] < corridor_max[i] and block["max"][i] > corridor_min[i]
                          for i in (0, 1))
            require(not overlap, f"Building block {block['id']} obstructs road {road['id']}")

    benchmark = load_json(ROOT / "Content/Data/Tests/dockside_benchmark.json")
    require(benchmark["schemaVersion"] == 1 and benchmark["id"], "Invalid benchmark schema")
    require(10 <= benchmark["warmupSeconds"] <= 60 and 120 <= benchmark["durationSeconds"] <= 600,
            "Benchmark must include warmup and at least 120 seconds of driving")
    require(40 <= benchmark["cruiseSpeedKmh"] <= 120 and 1 <= benchmark["brakeDecelerationMps2"] <= 6,
            "Unsafe benchmark control settings")
    points = benchmark["points"]
    require(8 <= len(points) <= 128, "Invalid benchmark waypoint count")
    for point in points:
        require(len(point) == 3 and all(math.isfinite(v) for v in point), "Invalid benchmark waypoint")
        require(abs(point[0]) <= 45000 and abs(point[1]) <= 30000 and 20 <= point[2] <= 120,
                "Benchmark waypoint exceeds supported bounds")
    for start, end in zip(points, points[1:] + points[:1]):
        require(100 <= math.dist(start[:2], end[:2]) <= 100000, "Degenerate benchmark segment")
        # Sample each chord against the union of real road corridors, including junctions.
        for step in range(101):
            sample = [start[i] + (end[i] - start[i]) * step / 100 for i in (0, 1)]
            require(any(all(min(r["start"][i], r["end"][i]) - width / 2 + 100 <= sample[i] <=
                                max(r["start"][i], r["end"][i]) + width / 2 - 100 for i in (0, 1))
                        for r in roads), "Benchmark route leaves roads or has less than 1 m edge clearance")

    # This checks race-to-world authoring, separate from the production C++
    # definition parser and portable gate tests. The road union exists only here.
    race = load_json(ROOT / "Content/Data/Races/dockside_circuit.json")
    require(race["schemaVersion"] == 1 and race["laps"] == 2 and len(race["opponents"]) == 3,
            "Dockside Phase 2 requires two laps and three opponents")
    route = race["routePoints"]
    def on_road(point, clearance=0):
        return any(all(min(r["start"][i], r["end"][i]) - width / 2 + clearance <= point[i] <=
                           max(r["start"][i], r["end"][i]) + width / 2 - clearance for i in (0, 1))
                   for r in roads)
    for start, end in zip(route, route[1:] + route[:1]):
        for step in range(101):
            point = [start[i] + (end[i] - start[i]) * step / 100 for i in (0, 1)]
            require(on_road(point, 100), "Race route leaves road collision or has less than 1 m clearance")
    for slot in race["grid"]:
        require(on_road(slot, 300), "Race grid does not have enough road clearance for the car")
    for gate in race["checkpoints"]:
        for side in (-1, 1):
            right = [-gate["forward"][1], gate["forward"][0]]
            edge = [gate["location"][i] + side * right[i] * gate["halfWidthCm"] for i in (0, 1)]
            require(on_road(edge), "Checkpoint gate extends outside road collision")
    race_length_m = sum(math.dist(a[:2], b[:2]) for a, b in zip(route, route[1:] + route[:1])) / 100

    python_files = list((ROOT / "Scripts").rglob("*.py"))
    for path in python_files:
        ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    for path in (ROOT / "Source").rglob("*.h"):
        includes = [line.strip() for line in path.read_text(encoding="utf-8-sig").splitlines()
                    if line.lstrip().startswith("#include")]
        generated = [line for line in includes if ".generated.h" in line]
        if generated:
            require(includes[-1] == generated[-1], f"UHT generated include must be last: {path}")

    packaging = (ROOT / "Config/DefaultGame.ini").read_text()
    # This is the reflected UE property name; the editor's display label is
    # "Additional Non-Asset Directories to Package" and is not an INI key.
    require('DirectoriesToAlwaysStageAsUFS=(Path="Data")' in packaging, "Runtime JSON staging configuration is missing")
    require('AdditionalNonAssetDirectoriesToPackage=' not in packaging, "Unsupported packaging key would silently omit JSON")

    # The source model is flattened to one UV-complete static mesh before UE
    # import. Check provenance and topology without opening the binary uasset.
    model_dir = ROOT / "Content/ExternalAssets/3DAssetsDev/CityCar"
    glb = model_dir / "city_car.glb"
    obj = model_dir / "static/city_car_static.obj"
    mtl = model_dir / "static/city_car_static.mtl"
    mesh_asset = ROOT / "Content/Velocity/External/CityCarStaticFinal/SM_CC0_CityCar.uasset"
    require(glb.is_file() and obj.is_file() and mtl.is_file() and mesh_asset.is_file(),
            "The documented city-car source, flattened mesh, or imported runtime asset is missing")
    require(hashlib.sha256(glb.read_bytes()).hexdigest() ==
            "6246ac14c89d41c31714cd3524c3e07d5270ad63038bfc38a8d7fbd6435c4877",
            "The CC0 city-car source does not match the reviewed provenance record")
    obj_lines = obj.read_text(encoding="ascii").splitlines()
    require(any(line == "o city_car_static" for line in obj_lines), "Flattened OBJ must be one object")
    require(any(line.startswith("vt ") for line in obj_lines), "Flattened OBJ must provide valid UVs")
    require(sum(line.startswith("f ") for line in obj_lines) == 31000,
            "Flattened OBJ topology differs from the reviewed 31,000 triangle model")
    require(any(line == "usemtl citycar_0_paintC" for line in obj_lines), "Flattened OBJ lost material groups")
    district_cpp = (ROOT / "Source/VelocityAfterdark/Private/World/ADDistrict.cpp").read_text(encoding="utf-8")
    require("CityCarStaticFinal/SM_CC0_CityCar.SM_CC0_CityCar" in district_cpp and
            "AFTERDARK_PARKED_ART_READY" in district_cpp and "SetCollisionProfileName(TEXT(\"NoCollision\"))" in district_cpp,
            "District dressing must hard-reference the imported mesh and remain collision-free")
    package_script = (ROOT / "Scripts/Invoke-Afterdark.ps1").read_text(encoding="utf-8")
    require("SM_CC0_CityCar.uasset" in package_script, "Windows package must assert inclusion of the imported mesh")
    print(f"PASS: project/data/source contracts; {length_cm / 100000:.2f} km connected roads; "
          f"{horsepower:.1f} hp sampled peak; static compression {compression * 100:.2f} cm; "
          f"{race_length_m:.2f} m circuit on actual roads; {len(python_files)} Python files parsed; "
          "one UV-complete 31,000-triangle CC0 city car verified.")
    print("Unreal compilation, rendering, physics integration and device acceptance require separate checks.")


if __name__ == "__main__":
    main()
