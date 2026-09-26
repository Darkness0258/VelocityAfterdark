#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Racing/ADRaceDefinition.h"
#include "ADPoliceDirector.generated.h"

class AADVehiclePawn;
class UADRaceDriverComponent;
class UPointLightComponent;
class UStaticMeshComponent;

UENUM()
enum class EADPoliceState : uint8 { Patrol, Pursuit, Search, Cooldown, Busted };

DECLARE_MULTICAST_DELEGATE(FADPoliceEscaped);

/** Bounded Dockside police prototype. Pursuit motion uses ordinary vehicle controls;
 *  PIT contact uses one gated, cooldown-limited impulse and roadblocks use route data.
 */
UCLASS()
class VELOCITYAFTERDARK_API AADPoliceDirector : public AActor
{
    GENERATED_BODY()
public:
    AADPoliceDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    void RegisterPlayer(AADVehiclePawn* InPlayer);
    void SetEnabled(bool bInEnabled);
    bool StartPursuit();
    int32 GetHeat() const { return Heat; }
    EADPoliceState GetState() const { return State; }
    FString GetStateLabel() const;
    bool IsActive() const { return State == EADPoliceState::Pursuit || State == EADPoliceState::Search || State == EADPoliceState::Busted; }
    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    float GetSearchRemainingSeconds() const;
    int32 GetUnitCount() const;
    int32 GetPITCount() const { return PITCount; }
    int32 GetRoadblockCount() const;
    FADPoliceEscaped OnEscaped;

private:
    struct FPoliceUnit
    {
        TWeakObjectPtr<AADVehiclePawn> Car;
        TWeakObjectPtr<UADRaceDriverComponent> Driver;
        TWeakObjectPtr<UPointLightComponent> RedLight;
        TWeakObjectPtr<UPointLightComponent> BlueLight;
        TWeakObjectPtr<UStaticMeshComponent> RedLens;
        TWeakObjectPtr<UStaticMeshComponent> BlueLens;
        bool bSeesPlayer = false;
        bool bDirectControl = false;
        bool bRoadblock = false;
        bool bRetired = false;
        float RetirementSeconds = 0.f;
        float ClearanceM = 100.f;
        float RoadblockSeconds = 0.f;
        float PITCooldownSeconds = 0.f;
    };
    struct FPoliceSettings
    {
        double PatrolSpeedScale = .6;
        TArray<double> PursuitSpeedScales;
        TArray<double> HeatThresholdSeconds;
        double DetectionDistanceM = 140.;
        double SpeedingKmh = 105.;
        double DetectionGraceSeconds = 3.;
        double LostSightGraceSeconds = 3.;
        double SearchSeconds = 30.;
        double CooldownSeconds = 12.;
        double BustSeconds = 8.;
        double BustDistanceM = 8.;
        double BustMaxSpeedKmh = 5.;
        double SpawnMinDistanceM = 120.;
        double SpawnMaxDistanceM = 220.;
        double SenseIntervalSeconds = .2;
        int32 MaxUnits = 5;
    } Settings;

    bool LoadSettings(FString& OutError);
    bool SpawnUnit(bool bRoadblock = false, int32 RoadblockSlot = 0);
    void RetireUnit(FPoliceUnit& Unit);
    void RemoveRetiredUnits(float DeltaSeconds);
    void ClearUnits();
    void RefreshDrivers();
    void Sense();
    void DriveUnit(FPoliceUnit& Unit, int32 Index, float DeltaSeconds);
    void EnterState(EADPoliceState NewState);
    double SpeedScale() const;
    void AddIdentification(FPoliceUnit& Unit);

    FADRaceDefinition Route;
    TWeakObjectPtr<AADVehiclePawn> Player;
    TArray<FPoliceUnit> Units;
    FCollisionQueryParams SightQuery;
    FString Error;
    FVector LastSeenPosition = FVector::ZeroVector;
    FVector LastSeenVelocity = FVector::ZeroVector;
    EADPoliceState State = EADPoliceState::Patrol;
    double StateSeconds = 0.;
    double PursuitSeconds = 0.;
    double MissingSightSeconds = 0.;
    double ViolationSeconds = 0.;
    double BustProgressSeconds = 0.;
    double SenseCountdown = 0.;
    double SpawnRetrySeconds = 0.;
    double RoadblockRetrySeconds = 0.;
    int32 Heat = 0;
    // A failed unit cannot be replaced repeatedly to keep an ongoing chase alive.
    int32 PursuitUnitsDispatched = 0;
    int32 PITCount = 0;
    bool bRoadblockDispatched = false;
    bool bReady = false;
    bool bEnabled = true;
    bool bAnySeesPlayer = false;
};
