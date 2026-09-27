#include "World/ADExplorationDirector.h"

#include "Core/ADGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "World/ADPoliceDirector.h"
#include "CollisionShape.h"

AADExplorationDirector::AADExplorationDirector()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.2f;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
}

void AADExplorationDirector::BeginPlay()
{
    Super::BeginPlay();
    Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (GetNetMode()!=NM_Standalone || !Ownership.IsValid() || !Ownership->IsReady())
    { Error=TEXT("Offline ownership is required for exploration rewards."); SetActorTickEnabled(false); return; }
    if (!RoadNetwork.LoadDefault(Error)) { SetActorTickEnabled(false); return; }
    // A catalog point must actually touch a road; avoid unreachable reward hints.
    for (const auto& Location:Ownership->GetDiscoveries())
    {
        TArray<FVector2D> Probe;
        double Distance=0.;
        if (!RoadNetwork.BuildRoute(Location.Position,Location.Position,Probe,Distance,Error) || Probe.IsEmpty()
            || FVector2D::Distance(Probe[0],Location.Position)>Location.RadiusCm)
        {
            Error=TEXT("Discovery is outside a reachable road: ")+Location.Id;
            UE_LOG(LogTemp,Error,TEXT("%s"),*Error);
            SetActorTickEnabled(false); return;
        }
    }
    bReady=true;
}

bool AADExplorationDirector::IsDiscovered(const FString& Id) const
{ return Ownership.IsValid() && Ownership->GetProfile().DiscoveredLocations.Contains(Id); }

const FADDiscoveryDefinition* AADExplorationDirector::GetTrackedDiscovery() const
{
    return Ownership.IsValid() ? Ownership->GetDiscoveries().FindByPredicate([&](const auto& Location) { return Location.Id==TrackedId; }) : nullptr;
}

void AADExplorationDirector::TrackDiscovery(const FString& Id)
{
    if (!bReady || !Ownership->GetDiscoveries().ContainsByPredicate([&](const auto& Location) { return Location.Id==Id; })) return;
    TrackedId=Id; bManualTarget=true; RouteElapsed=1.f;
    ResetArrival();
    if (Player.IsValid()) RefreshRoute(FVector2D(Player->GetActorLocation()));
}

void AADExplorationDirector::ClearTrackedDiscovery()
{ TrackedId.Reset(); bManualTarget=false; Route.Reset(); RouteError.Reset(); RouteDistanceCm=0.; ResetArrival(); }

bool AADExplorationDirector::FastTravelToDiscovery(AADVehiclePawn* Car,const FString& LocationId,FString& OutError)
{
    OutError.Reset();
    if (!bReady || GetNetMode()!=NM_Standalone || !Ownership.IsValid() || !Ownership->IsReady()
        || !IsValid(Car) || !Car->GetPhysics() || !Car->GetPhysics()->IsReady())
    { OutError=TEXT("Fast travel is unavailable right now."); return false; }
    const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    if (!Mode || Car->IsInGarage() || !Car->IsDrivingEnabled()
        || (Mode->GetRaceManager() && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle)
        || (Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive()))
    { OutError=TEXT("Fast travel is restricted to an active free drive outside a pursuit."); return false; }
    const auto* Location=Ownership->GetDiscoveries().FindByPredicate(
        [&LocationId](const FADDiscoveryDefinition& Item) { return Item.Id==LocationId; });
    if (!Location || !IsDiscovered(LocationId))
    { OutError=TEXT("Fast travel unlocks after you discover that landmark."); return false; }

    TArray<FVector2D> RoutePoints;
    double DistanceCm=0.;
    if (!RoadNetwork.BuildRoute(FVector2D(Car->GetActorLocation()),Location->Position,
        RoutePoints,DistanceCm,OutError) || RoutePoints.IsEmpty())
    { OutError=TEXT("No safe road route reaches this landmark."); return false; }

    const FVector2D RoadPoint=RoutePoints.Last();
    FVector2D Direction=RoutePoints.Num()>1 ? (RoadPoint-RoutePoints[RoutePoints.Num()-2]).GetSafeNormal() : FVector2D::ZeroVector;
    if (Direction.IsNearlyZero())
    {
        double ClosestSquared=TNumericLimits<double>::Max();
        for (const FADRoadNetworkSegment& Segment:RoadNetwork.GetSegments())
        {
            const FVector2D Delta=Segment.End-Segment.Start;
            const double LengthSquared=Delta.SizeSquared();
            const double Alpha=LengthSquared>UE_SMALL_NUMBER
                ? FMath::Clamp(FVector2D::DotProduct(Location->Position-Segment.Start,Delta)/LengthSquared,0.,1.) : 0.;
            const FVector2D Projection=Segment.Start+Delta*Alpha;
            const double ErrorSquared=FVector2D::DistSquared(Location->Position,Projection);
            if (ErrorSquared<ClosestSquared) { ClosestSquared=ErrorSquared; Direction=Delta.GetSafeNormal(); }
        }
    }
    if (Direction.IsNearlyZero()) { OutError=TEXT("The landmark has no valid road orientation."); return false; }

    const FVector2D Right(-Direction.Y,Direction.X);
    const FRotator Rotation(FVector(Direction.X,Direction.Y,0.).Rotation());
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ADFastTravel),false,Car);
    const FCollisionShape Shape=FCollisionShape::MakeBox(FVector(270.,115.,40.));
    FTransform SafePose;
    bool bFoundSafePose=false;
    for (const double Offset:{500.,-500.,0.})
    {
        const FVector Position(RoadPoint.X+Right.X*Offset,RoadPoint.Y+Right.Y*Offset,90.);
        if (!GetWorld()->OverlapBlockingTestByChannel(Position,Rotation.Quaternion(),ECC_PhysicsBody,Shape,Query))
        { SafePose=FTransform(Rotation,Position); bFoundSafePose=true; break; }
    }
    if (!bFoundSafePose) { OutError=TEXT("That landmark is busy. Try fast travel again when its road is clear."); return false; }

    if (!Car->PlaceForRace(SafePose)) { OutError=TEXT("The vehicle could not be placed safely at the landmark."); return false; }
    Car->GetPhysics()->SetControls(0.f,0.f,0.f,false);
    if (Car->GetEffects()) Car->GetEffects()->SetNitrousHeld(false);
    ResetAfterTeleport(Car);
    return true;
}

void AADExplorationDirector::ResetAfterTeleport(AADVehiclePawn* Car)
{
    Player=Car;
    PreviousPosition=Car ? FVector2D(Car->GetActorLocation()) : FVector2D::ZeroVector;
    LastSampleSeconds=GetWorld() ? GetWorld()->GetTimeSeconds() : -1.;
    ResetArrival();
    TrackedId.Reset();
    bManualTarget=false;
    Route.Reset(); RouteError.Reset(); RouteDistanceCm=0.; RouteElapsed=1.f;
}

void AADExplorationDirector::ResetArrival()
{ ArrivalId.Reset(); ArrivalSeconds=0.f; }

float AADExplorationDirector::GetArrivalFraction() const
{
    const auto* Location=GetTrackedDiscovery();
    return Location && Location->Id==ArrivalId ? FMath::Clamp(ArrivalSeconds/Location->DwellSeconds,0.f,1.f) : 0.f;
}

void AADExplorationDirector::RefreshRoute(const FVector2D& Position)
{
    RouteElapsed=0.f;
    if (const auto* Target=GetTrackedDiscovery()) RoadNetwork.BuildRoute(Position,Target->Position,Route,RouteDistanceCm,RouteError);
    else { Route.Reset(); RouteError.Reset(); RouteDistanceCm=0.; }
}

FString AADExplorationDirector::GetDirectionHint() const
{
    if (!Player.IsValid() || Route.IsEmpty()) return TEXT("RETURN TO A ROAD");
    const FVector2D Position(Player->GetActorLocation());
    FVector2D Target=Route.Last();
    for (const FVector2D& Point:Route) if (FVector2D::Distance(Position,Point)>2500.) { Target=Point; break; }
    const FVector Local=Player->GetActorTransform().InverseTransformVectorNoScale(FVector(Target.X-Position.X,Target.Y-Position.Y,0));
    const double Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    return FMath::Abs(Angle)>135. ? TEXT("TURN BACK WHEN SAFE")
        : Angle>35. ? TEXT("FOLLOW THE ROAD RIGHT") : Angle<-35. ? TEXT("FOLLOW THE ROAD LEFT") : TEXT("FOLLOW THE ROAD AHEAD");
}

void AADExplorationDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bReady || !Ownership.IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f) return;
    auto* PC=Cast<AADPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
    const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    const double Now=GetWorld()->GetTimeSeconds();
    if (!Message.IsEmpty() && Now>=MessageUntilSeconds) Message.Reset();
    if (!PC || !Car || !PC->IsSessionStarted() || PC->IsGamePaused() || Car->IsInGarage() || !Car->IsDrivingEnabled()
        || !Car->GetPhysics()->IsReady() || !Mode
        || (Mode->GetRaceManager() && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle)
        || (Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive()))
    { ResetArrival(); LastSampleSeconds=-1.; return; }
    const FVector2D Position(Car->GetActorLocation());
    if (Position.ContainsNaN()) { ResetArrival(); LastSampleSeconds=-1.; return; }
    const double Gap=Now-LastSampleSeconds;
    const bool bContinuous=Player.Get()==Car && LastSampleSeconds>=0. && Gap>0. && Gap<.75
        && FVector2D::Distance(Position,PreviousPosition)<FMath::Max(1500.,Car->GetVelocity().Size()*Gap*1.5+300.);
    Player=Car; PreviousPosition=Position; LastSampleSeconds=Now;
    if (!bManualTarget || !GetTrackedDiscovery())
    {
        const FADDiscoveryDefinition* Nearest=nullptr;
        double Best=TNumericLimits<double>::Max();
        for (const auto& Location:Ownership->GetDiscoveries())
        {
            if (IsDiscovered(Location.Id)) continue;
            const double Distance=FVector2D::DistSquared(Position,Location.Position);
            if (Distance<Best) { Best=Distance; Nearest=&Location; }
        }
        const FString Next=Nearest ? Nearest->Id : FString();
        if (Next!=TrackedId) { TrackedId=Next; RouteElapsed=1.f; }
    }
    RouteElapsed+=DeltaSeconds;
    if (RouteElapsed>=1.f) RefreshRoute(Position);
    if (!bContinuous) { ResetArrival(); return; }
    // Arrival is based on the actual car, independent of whichever map target is selected.
    const FADDiscoveryDefinition* Arrived=nullptr;
    for (const auto& Location:Ownership->GetDiscoveries())
        if (!IsDiscovered(Location.Id) && FVector2D::Distance(Position,Location.Position)<=Location.RadiusCm)
        { Arrived=&Location; break; }
    const auto& Telemetry=Car->GetPhysics()->GetTelemetry();
    if (!Arrived || Telemetry.GroundedWheels<2 || FMath::Abs(Telemetry.SpeedKmh)>40.f)
    { ResetArrival(); return; }
    if (ArrivalId!=Arrived->Id) { ArrivalId=Arrived->Id; ArrivalSeconds=0.f; }
    ArrivalSeconds+=static_cast<float>(Gap);
    if (ArrivalSeconds<Arrived->DwellSeconds || Now<RetryAfterSeconds) return;
    FString SaveError;
    if (Ownership->CommitDiscovery(Arrived->Id,SaveError))
    {
        Message=Ownership->GetStatus();
        if (TrackedId==Arrived->Id) bManualTarget=false;
        ResetArrival();
    }
    else { Message=TEXT("DISCOVERY NOT SAVED: ")+SaveError; RetryAfterSeconds=Now+5.; }
    MessageUntilSeconds=Now+10.;
}
