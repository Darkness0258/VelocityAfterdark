#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ADRaceRules.h"
#include "Racing/ADRaceDefinition.h"
#include "Racing/ADRaceCatalog.h"
#include "ADRaceManager.generated.h"

class AADVehiclePawn;
class UADRaceDriverComponent;
class UInstancedStaticMeshComponent;

UENUM()
enum class EADRaceState : uint8 { Idle, Countdown, Racing, Results };

struct FADRacerState
{
    FString Name;
    FLinearColor Color = FLinearColor::White;
    TWeakObjectPtr<AADVehiclePawn> Car;
    TWeakObjectPtr<UADRaceDriverComponent> Driver;
    ADRaceRules::Progress Progress;
    FVector LastPosition = FVector::ZeroVector;
    double PenaltySeconds = 0.;
    double RankedDistanceM = 0.;
    double LastRecoverySeconds = -10.;
    int32 RecoveryCount = 0;
    int32 Place = 1;
    bool bDNF = false;
    bool bWrongWay = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FADRaceStateChanged, EADRaceState);

/** Authority owns scoring; input, AI and HUD only issue commands/read snapshots. */
UCLASS()
class VELOCITYAFTERDARK_API AADRaceManager : public AActor
{
    GENERATED_BODY()
public:
    AADRaceManager();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    bool StartRace(AADVehiclePawn* Player, int32 DifficultyIndex = 1);
    bool StartRaceById(AADVehiclePawn* Player, const FString& RaceId, int32 DifficultyIndex = 1);
    bool StartCareerRace(AADVehiclePawn* Player);
    bool RetryCareerReward();
    bool HasPendingCareerReward() const;
    const FString& GetCareerMessage() const { return CareerMessage; }
    void LeaveRace();
    bool RecoverRacer(int32 Index);
    bool IsReady() const { return bReady; }
    const FString& GetLoadError() const { return LoadError; }
    EADRaceState GetState() const { return State; }
    bool IsClassificationFinal() const;
    bool AllowsPlayerInput() const;
    double GetElapsedSeconds() const { return ElapsedSeconds; }
    double GetCountdownRemaining() const { return CountdownRemaining; }
    int32 GetDifficultyIndex() const { return Difficulty; }
    const FADRaceDefinition& GetDefinition() const { return Definition; }
    const TArray<FADRaceDefinition>& GetAvailableRaces() const { return Catalog.GetRaces(); }
    const FADRaceDefinition* GetRaceById(const FString& RaceId) const { return Catalog.Find(RaceId); }
    const TArray<FADRacerState>& GetRacers() const { return Racers; }
    FADRaceStateChanged OnStateChanged;

private:
    void SetState(EADRaceState NewState);
    void BuildMarkers();
    void SelectDefinition(const FADRaceDefinition& Selected);
    void UpdateTargetMarker();
    void UpdateClassification();
    void HandleRecoveryRequest(AADVehiclePawn* Car);
    void RetireRacer(int32 Index);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> GateMarkers;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TargetMarker;
    FADRaceDefinition Definition;
    FADRaceCatalog Catalog;
    TArray<ADRaceRules::Gate> ScoringGates;
    TArray<FADRacerState> Racers;
    TArray<int32> SortOrder;
    FString LoadError;
    FString ActiveChapterId;
    FString RaceReceiptId;
    FString CareerMessage;
    bool bRewardAttempted = false;
    bool bRewardCommitted = false;
    EADRaceState State = EADRaceState::Idle;
    double ElapsedSeconds = 0.;
    double CountdownRemaining = 0.;
    int32 Difficulty = 1;
    int32 DisplayedCheckpoint = -2;
    bool bReady = false;
};
