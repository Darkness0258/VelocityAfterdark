#include "World/ADExplorationDirector.h"

#include "Core/ADGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "World/ADPoliceDirector.h"

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
