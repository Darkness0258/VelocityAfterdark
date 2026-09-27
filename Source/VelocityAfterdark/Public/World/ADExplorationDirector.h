#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ADRoadNetwork.h"
#include "World/ADWorldProgress.h"
#include "ADExplorationDirector.generated.h"

class AADVehiclePawn;
class UADOwnershipSubsystem;

/** Offline discovery arrivals, navigation targets and transactional one-time rewards. */
UCLASS()
class VELOCITYAFTERDARK_API AADExplorationDirector : public AActor
{
    GENERATED_BODY()
public:
    AADExplorationDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    const FString& GetMessage() const { return Message; }
    const FADRoadNetwork& GetRoadNetwork() const { return RoadNetwork; }
    const FADDiscoveryDefinition* GetTrackedDiscovery() const;
    void TrackDiscovery(const FString& Id);
    void ClearTrackedDiscovery();
    float GetArrivalFraction() const;
    bool IsDiscovered(const FString& Id) const;
    const TArray<FVector2D>& GetRoute() const { return Route; }
    double GetRouteDistanceCm() const { return RouteDistanceCm; }
    const FString& GetRouteError() const { return RouteError; }
    FString GetDirectionHint() const;
    bool FastTravelToDiscovery(AADVehiclePawn* Car, const FString& LocationId, FString& OutError);

private:
    void ResetArrival();
    void RefreshRoute(const FVector2D& Position);
    void ResetAfterTeleport(AADVehiclePawn* Car);
    TWeakObjectPtr<UADOwnershipSubsystem> Ownership;
    TWeakObjectPtr<AADVehiclePawn> Player;
    FADRoadNetwork RoadNetwork;
    TArray<FVector2D> Route;
    FVector2D PreviousPosition=FVector2D::ZeroVector;
    double RouteDistanceCm=0.;
    double LastSampleSeconds=-1.;
    double RetryAfterSeconds=0.;
    double MessageUntilSeconds=0.;
    float ArrivalSeconds=0.f;
    float RouteElapsed=1.f;
    FString TrackedId;
    FString ArrivalId;
    FString Error;
    FString Message;
    FString RouteError;
    bool bManualTarget=false;
    bool bReady=false;
};
