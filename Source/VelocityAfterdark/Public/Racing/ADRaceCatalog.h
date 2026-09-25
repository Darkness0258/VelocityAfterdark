#pragma once

#include "CoreMinimal.h"
#include "Racing/ADRaceDefinition.h"

/** Immutable, bounded set of authored events on the currently resident road network.
 *  Definitions and road coverage are validated before a reload replaces usable content.
 *  A race ID is an identity, never a filename supplied by the player or profile.
 */
class VELOCITYAFTERDARK_API FADRaceCatalog
{
public:
    bool LoadDefault(FString& OutError);
    bool LoadFromJson(const FString& CatalogPath, const FString& DistrictPath,
        const FString& RegionsPath, FString& OutError);
    const FADRaceDefinition* Find(const FString& RaceId) const;
    const TArray<FADRaceDefinition>& GetRaces() const { return Races; }

private:
    TArray<FADRaceDefinition> Races;
};
