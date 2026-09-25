#pragma once

#include "CoreMinimal.h"

/** Serializable simulation state, independent of actors and renderer resources. */
struct VELOCITYAFTERDARK_API FADWorldSnapshot
{
    bool bRecorded = false;
    double Hour = 23.;
    double WeatherElapsed = 0.;
    int32 WeatherIndex = 0;
    uint8 Weather = 0;
    float Wetness = 0.f;
    float RainAmount = 0.f;
    float FogAmount = 0.f;

    bool IsValid() const;
};

struct VELOCITYAFTERDARK_API FADDiscoveryDefinition
{
    FString Id;
    FString Name;
    FString Description;
    FVector2D Position = FVector2D::ZeroVector;
    float RadiusCm = 2200.f;
    float DwellSeconds = 1.5f;
    int64 Credits = 0;
    int64 Reputation = 0;

    // On failure Out remains unchanged, so an invalid reload cannot erase a catalog.
    static bool LoadCatalog(const FString& Path, TArray<FADDiscoveryDefinition>& Out, FString& Error);
};
