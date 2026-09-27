#include "Presentation/ADMapComponent.h"

#include "Core/ADGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Garage/ADGarageSessionComponent.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Presentation/ADCinematicComponent.h"
#include "Racing/ADRaceManager.h"
#include "Settings/ADSettingsSubsystem.h"
#include "World/ADExplorationDirector.h"
#include "Player/ADVehiclePawn.h"
#include "World/ADPoliceDirector.h"

AADPlayerController* UADMapComponent::Controller() const { return Cast<AADPlayerController>(GetOwner()); }
AADExplorationDirector* UADMapComponent::GetExploration() const
{
    const auto* Mode=GetWorld() ? GetWorld()->GetAuthGameMode<AADGameMode>() : nullptr;
    return Mode ? Mode->GetExploration() : nullptr;
}

void UADMapComponent::Toggle()
{
    if (bOpen) { Close(); return; }
    auto* PC=Controller();
    const auto* Director=GetExploration();
    const auto* Settings=GetWorld()->GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    if (!PC || !Director || !Director->IsReady() || !PC->IsSessionStarted() || PC->IsGamePaused()
        || PC->GetGarageSession()->IsActive() || PC->GetCinematic()->IsActive() || (Settings && Settings->IsOpen())
        || (PC->GetRaceManager() && PC->GetRaceManager()->GetState()!=EADRaceState::Idle)
        || (Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive())) return;
    bPausedByMap=!PC->IsPaused();
    if (bPausedByMap && !PC->SetPause(true)) { bPausedByMap=false; return; }
    bPreviousCursor=PC->bShowMouseCursor;
    PC->FlushPressedKeys(); PC->bShowMouseCursor=true;
    FInputModeGameAndUI Input; Input.SetHideCursorDuringCapture(false);
    PC->SetInputMode(Input);
    RebuildFilter(); bOpen=true;
}

void UADMapComponent::Close()
{
    if (!bOpen) return;
    bOpen=false;
    if (auto* PC=Controller())
    {
        PC->FlushPressedKeys();
        if (bPausedByMap) PC->SetPause(false);
        PC->bShowMouseCursor=bPreviousCursor;
        if (bPreviousCursor) PC->SetInputMode(FInputModeGameAndUI());
        else PC->SetInputMode(FInputModeGameOnly());
    }
    bPausedByMap=false;
}

void UADMapComponent::RebuildFilter()
{
    VisibleLocations.Reset();
    const auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Ownership || !Ownership->IsReady()) return;
    const auto& Locations=Ownership->GetDiscoveries();
    for (int32 Index=0;Index<Locations.Num();++Index)
    {
        const bool bFound=Ownership->GetProfile().DiscoveredLocations.Contains(Locations[Index].Id);
        if (Filter==0 || (Filter==1 && !bFound) || (Filter==2 && bFound)) VisibleLocations.Add(Index);
    }
    SelectedRow=0;
}

void UADMapComponent::Select(int32 Delta)
{ if (bOpen && !VisibleLocations.IsEmpty()) SelectedRow=((SelectedRow+Delta)%VisibleLocations.Num()+VisibleLocations.Num())%VisibleLocations.Num(); }
void UADMapComponent::ChangeFilter(int32 Delta)
{ if (bOpen) { Filter=((Filter+Delta)%3+3)%3; RebuildFilter(); } }
FString UADMapComponent::GetFilterName() const
{ return Filter==1 ? TEXT("UNDISCOVERED") : Filter==2 ? TEXT("DISCOVERED") : TEXT("ALL LANDMARKS"); }
const FADDiscoveryDefinition* UADMapComponent::GetSelectedLocation() const
{
    const auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    return Ownership && VisibleLocations.IsValidIndex(SelectedRow)
        && Ownership->GetDiscoveries().IsValidIndex(VisibleLocations[SelectedRow])
        ? &Ownership->GetDiscoveries()[VisibleLocations[SelectedRow]] : nullptr;
}
void UADMapComponent::Confirm()
{
    if (!bOpen) return;
    auto* Director=GetExploration();
    if (const auto* Location=GetSelectedLocation(); Director && Location) Director->TrackDiscovery(Location->Id);
    Close();
}
void UADMapComponent::FollowNearest()
{ if (bOpen) { if (auto* Director=GetExploration()) Director->ClearTrackedDiscovery(); Close(); } }

void UADMapComponent::FastTravel()
{
    if (!bOpen) return;
    auto* PC=Controller();
    auto* Director=GetExploration();
    const auto* Location=GetSelectedLocation();
    auto* Mode=GetWorld() ? GetWorld()->GetAuthGameMode<AADGameMode>() : nullptr;
    auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
    if (!PC || !Director || !Mode || !Car || !PC->IsSessionStarted())
    { Message=TEXT("FAST TRAVEL IS UNAVAILABLE RIGHT NOW."); return; }
    if (const AADRaceManager* Race=Mode->GetRaceManager(); Race && Race->GetState()!=EADRaceState::Idle)
    { Message=TEXT("FINISH OR LEAVE THE EVENT BEFORE FAST TRAVEL."); return; }
    if (const AADPoliceDirector* Police=Mode->GetPoliceDirector(); Police && Police->IsActive())
    { Message=TEXT("LOSE THE POLICE SEARCH BEFORE FAST TRAVEL."); return; }
    if (!Location || !Director->IsDiscovered(Location->Id))
    { Message=TEXT("SELECT A DISCOVERED LANDMARK TO FAST TRAVEL."); return; }

    PC->FlushPressedKeys();
    FString Error;
    if (!Director->FastTravelToDiscovery(Car,Location->Id,Error))
    { Message=Error; return; }
    if (PC->GetCinematic()) PC->GetCinematic()->NotifyRecordingDiscontinuity();
    Message=FString::Printf(TEXT("ARRIVED AT %s  /  SPEED AND CONTROLS RESET"),*Location->Name.ToUpper());
}

void UADMapComponent::ClearWaypoint()
{
    if (!bOpen) return;
    auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Ownership || !Ownership->GetProfile().World.bCustomWaypointRecorded)
    { Message=TEXT("NO CUSTOM WAYPOINT IS SET."); return; }
    FString Error;
    Message=Ownership->SetCustomWaypoint(false,FVector2D::ZeroVector,Error)
        ? TEXT("CUSTOM WAYPOINT CLEARED.") : TEXT("WAYPOINT SAVE FAILED: ")+Error;
}

void UADMapComponent::Zoom(float Delta)
{ if (bOpen && FMath::IsFinite(Delta)) ZoomFactor=FMath::Clamp(ZoomFactor+Delta,.65f,2.4f); }

void UADMapComponent::Pan(FVector2D WorldDelta)
{
    if (!bOpen || !FMath::IsFinite(WorldDelta.X) || !FMath::IsFinite(WorldDelta.Y)) return;
    PanCenter.X=FMath::Clamp(PanCenter.X+WorldDelta.X,-200000.,200000.);
    PanCenter.Y=FMath::Clamp(PanCenter.Y+WorldDelta.Y,-200000.,200000.);
}

void UADMapComponent::SetWaypoint(FVector2D CanvasPosition)
{
    auto* Director=GetExploration();
    auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Director || !Ownership) { Message=TEXT("MAP DATA IS UNAVAILABLE."); return; }
    const FVector2D WorldPoint=ADMapLayout::Unproject(CanvasPosition,ZoomFactor,PanCenter);
    TArray<FVector2D> Route;
    double DistanceCm=0.;
    FString Error;
    if (!Director->GetRoadNetwork().BuildRoute(WorldPoint,WorldPoint,Route,DistanceCm,Error) || Route.IsEmpty())
    { Message=TEXT("WAYPOINT MUST BE PLACED NEAR A CONNECTED ROAD."); return; }
    const FVector2D RoadPoint=Route.Last();
    if (FVector2D::Distance(WorldPoint,RoadPoint)>1800.)
    { Message=TEXT("WAYPOINT MUST BE PLACED ON OR BESIDE A ROAD."); return; }
    if (!Ownership->SetCustomWaypoint(true,RoadPoint,Error))
    { Message=TEXT("WAYPOINT SAVE FAILED: ")+Error; return; }
    Message=TEXT("CUSTOM WAYPOINT SAVED.");
}

void UADMapComponent::Click(FVector2D Position)
{
    if (!bOpen) return;
    // Eight visible rows per page keep larger data catalogs inside the panel.
    const int32 First=(SelectedRow/8)*8;
    if (Position.X>=ADMapLayout::ListX && Position.X<=ADMapLayout::ListX+ADMapLayout::ListWidth
        && Position.Y>=ADMapLayout::ListY && Position.Y<ADMapLayout::ListY+8*ADMapLayout::RowHeight)
    {
        const int32 Row=First+FMath::FloorToInt((Position.Y-ADMapLayout::ListY)/ADMapLayout::RowHeight);
        if (VisibleLocations.IsValidIndex(Row)) { SelectedRow=Row; Confirm(); }
        return;
    }
    const auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Ownership) return;
    for (int32 Row=0;Row<VisibleLocations.Num();++Row)
    {
        const auto& Location=Ownership->GetDiscoveries()[VisibleLocations[Row]];
        if (FVector2D::Distance(Position,Project(Location.Position))<20.)
        { SelectedRow=Row; Confirm(); return; }
    }
    if (Position.X>=ADMapLayout::X && Position.X<=ADMapLayout::X+ADMapLayout::Width
        && Position.Y>=ADMapLayout::Y && Position.Y<=ADMapLayout::Y+ADMapLayout::Height)
        SetWaypoint(Position);
}
void UADMapComponent::EndPlay(const EEndPlayReason::Type Reason)
{ Close(); Super::EndPlay(Reason); }
