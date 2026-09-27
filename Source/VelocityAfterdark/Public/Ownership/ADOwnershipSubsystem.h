#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Vehicle/ADVehicleDefinition.h"
#include "World/ADWorldProgress.h"
#include "ADOwnershipSubsystem.generated.h"

struct VELOCITYAFTERDARK_API FADGaragePaint
{
    FString Id;
    FString Name;
    FLinearColor Color = FLinearColor::White;
};

struct VELOCITYAFTERDARK_API FADGarageUpgrade
{
    FString Id;
    FString Name;
    FString Description;
    int64 Price = 0;
    float TorqueMultiplier = 1.f;
    float GripMultiplier = 1.f;
    float BrakeMultiplier = 1.f;
    float MassMultiplier = 1.f;
};

struct VELOCITYAFTERDARK_API FADGarageTune
{
    FString Id;
    FString Name;
    FString Description;
    float FinalDriveMultiplier = 1.f;
};

struct VELOCITYAFTERDARK_API FADDealerVehicle
{
    FString Id;
    FString Description;
    int64 Price = 0;
    FADVehicleDefinition Definition;
};

/** Parts belong to an individual car, not to every car in the driver's collection. */
struct VELOCITYAFTERDARK_API FADOwnedVehicle
{
    FString VehicleId = TEXT("aster_s6");
    FString PaintId = TEXT("mint");
    TArray<FString> OwnedUpgrades;
    TArray<FString> EquippedUpgrades;
    FString TuneId = TEXT("street");
};

/** Bounded, profile-owned history used to vary future rivalry dialogue. */
struct VELOCITYAFTERDARK_API FADRivalMemory
{
    FString RivalId;
    FString LastChapterId;
    int32 Encounters = 0;
    int32 PlayerWins = 0;
    int32 RivalWins = 0;
    float Respect = 0.f;
    float Grudge = 0.f;
};

/** A finalized career classification entry from one named opponent. */
struct VELOCITYAFTERDARK_API FADRivalRaceResult
{
    FString RivalId;
    int32 Place = 0;
};

struct VELOCITYAFTERDARK_API FADGarageProfile
{
    FString ActiveVehicleId = TEXT("aster_s6");
    TArray<FADOwnedVehicle> Vehicles = {FADOwnedVehicle()};
    int64 Credits = 12000;
    // Active-car view retained for garage callers. Durable records must agree with this view.
    FString PaintId = TEXT("mint");
    TArray<FString> OwnedUpgrades;
    TArray<FString> EquippedUpgrades;
    FString TuneId = TEXT("street");
    int64 Reputation = 0;
    int64 RaceWins = 0;
    int64 RacesFinished = 0;
    TArray<FString> CompletedChapters;
    TArray<FString> AwardedRaceIds;
    TArray<FADRivalMemory> RivalMemories;
    TArray<FString> DiscoveredLocations;
    FADWorldSnapshot World;
};

/** Local ownership service. Definitions remain immutable; only validated commits change ownership.
 *  File checksums detect corruption, not cheating. Competitive currency needs a future server authority.
 */
UCLASS()
class VELOCITYAFTERDARK_API UADOwnershipSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    const FString& GetStatus() const { return Status; }
    const FADGarageProfile& GetProfile() const { return Profile; }
    const TArray<FADGaragePaint>& GetPaints() const { return Paints; }
    const TArray<FADGarageUpgrade>& GetUpgrades() const { return Upgrades; }
    const TArray<FADGarageTune>& GetTunes() const { return Tunes; }
    const TArray<FADDiscoveryDefinition>& GetDiscoveries() const { return Discoveries; }
    const TArray<FADDealerVehicle>& GetVehicles() const { return Vehicles; }
    const FADDealerVehicle* FindVehicle(const FString& VehicleId) const;
    bool IsVehicleOwned(const FString& VehicleId) const;
    const FADVehicleDefinition& GetStockDefinition() const { return GetStockDefinition(Profile); }
    const FADVehicleDefinition& GetStockDefinition(const FADGarageProfile& Desired) const;
    // Starts a fresh preview from this car's saved build (or its factory build if unowned).
    bool MakeVehicleDraft(const FString& VehicleId, FADGarageProfile& OutDraft, FString& OutError) const;
    bool GetPurchaseCost(const FADGarageProfile& Desired, int64& OutCost, FString& OutError) const;

    // Safe for a garage preview: never mutates the stock definition or persistent state.
    FADVehicleDefinition BuildDefinition(const FADGarageProfile& Desired) const;
    // Desired.Credits is ignored. New purchases are priced here; ownership cannot be sold or removed.
    bool Commit(const FADGarageProfile& Desired, FString& OutError);
    // The authoritative race manager submits classification, never a payout.
    bool CommitRaceResult(const FString& ReceiptId, const FString& ChapterId, int32 Place, FString& OutError);
    bool CommitRaceResult(const FString& ReceiptId, const FString& ChapterId, int32 Place,
        const TArray<FADRivalRaceResult>& Rivals, FString& OutError);
    // Only the offline world director calls this after checking real vehicle arrival.
    bool CommitDiscovery(const FString& LocationId, FString& OutError);
    // A snapshot is pending until a successful transaction; failed saves never advance the durable state.
    void StageWorldSnapshot(const FADWorldSnapshot& Snapshot);
    bool SaveWorldSnapshot(FString& OutError);
    bool SetCustomWaypoint(bool bRecorded, FVector2D Position, FString& OutError);
    // Explicit absolute paths isolate test profiles. An empty path selects a memory-only profile.
    bool InitializeProfile(const FString& AbsoluteSavePath, FString& OutError);

private:
    bool LoadData(FString& OutError);
    bool ValidateProfile(const FADGarageProfile& Candidate, FString& OutError) const;
    bool WriteProfile(const FADGarageProfile& Candidate, FString& OutError);

    FADVehicleDefinition StockDefinition;
    FADGarageProfile Profile;
    TArray<FADGaragePaint> Paints;
    TArray<FADGarageUpgrade> Upgrades;
    TArray<FADGarageTune> Tunes;
    TArray<FADDealerVehicle> Vehicles;
    TArray<FADDiscoveryDefinition> Discoveries;
    FADWorldSnapshot PendingWorld;
    FString SavePath;
    FString PrimarySnapshot;
    bool bPrimaryExisted = false;
    FString Error;
    FString Status;
    bool bDataReady = false;
    bool bReady = false;
};
