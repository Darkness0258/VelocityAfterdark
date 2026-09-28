#include "World/ADPhysicalSurface.h"

#include "PhysicalMaterials/PhysicalMaterial.h"

FADPhysicalSurfaceProfile ADSurfacePhysics::ResolveProfile(FName MaterialName)
{
    const FString Name = MaterialName.ToString();
    if (Name.Contains(TEXT("Asphalt"), ESearchCase::IgnoreCase)) return {0.82f, 0.025f};
    if (Name.Contains(TEXT("Concrete"), ESearchCase::IgnoreCase)
        || Name.Contains(TEXT("Building"), ESearchCase::IgnoreCase)) return {0.76f, 0.045f};
    if (Name.Contains(TEXT("RoadYellow"), ESearchCase::IgnoreCase)
        || Name.Contains(TEXT("RoadMarking"), ESearchCase::IgnoreCase)) return {0.68f, 0.035f};
    if (Name.Contains(TEXT("Metal"), ESearchCase::IgnoreCase)
        || Name.Contains(TEXT("Rust"), ESearchCase::IgnoreCase)
        || Name.Contains(TEXT("Container"), ESearchCase::IgnoreCase)) return {0.52f, 0.08f};
    if (Name.Contains(TEXT("Water"), ESearchCase::IgnoreCase)) return {0.28f, 0.015f};
    if (Name.Contains(TEXT("Sand"), ESearchCase::IgnoreCase)
        || Name.Contains(TEXT("Foliage"), ESearchCase::IgnoreCase)) return {0.48f, 0.025f};
    if (Name.Contains(TEXT("Rock"), ESearchCase::IgnoreCase)) return {0.73f, 0.035f};
    if (Name.Contains(TEXT("Wood"), ESearchCase::IgnoreCase)) return {0.60f, 0.055f};
    if (Name.Contains(TEXT("Rubber"), ESearchCase::IgnoreCase)) return {0.96f, 0.12f};
    if (Name.Contains(TEXT("Glass"), ESearchCase::IgnoreCase)) return {0.42f, 0.025f};
    return {};
}

UPhysicalMaterial* ADSurfacePhysics::CreatePhysicalMaterial(UObject* Outer, FName ObjectName, FName MaterialName)
{
    if (!IsValid(Outer) || ObjectName.IsNone()) return nullptr;
    const FADPhysicalSurfaceProfile Profile = ResolveProfile(MaterialName);
    UPhysicalMaterial* PhysicalMaterial = NewObject<UPhysicalMaterial>(Outer, ObjectName);
    if (!PhysicalMaterial) return nullptr;
    PhysicalMaterial->Friction = Profile.Friction;
    PhysicalMaterial->StaticFriction = Profile.Friction;
    PhysicalMaterial->Restitution = Profile.Restitution;
    return PhysicalMaterial;
}
