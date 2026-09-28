#pragma once

#include "CoreMinimal.h"

class UPhysicalMaterial;

/** Compact physical profiles for collidable Nova City surfaces. */
struct FADPhysicalSurfaceProfile
{
    float Friction = 0.68f;
    float Restitution = 0.035f;
};

namespace ADSurfacePhysics
{
    FADPhysicalSurfaceProfile ResolveProfile(FName MaterialName);
    UPhysicalMaterial* CreatePhysicalMaterial(UObject* Outer, FName ObjectName, FName MaterialName);
}
