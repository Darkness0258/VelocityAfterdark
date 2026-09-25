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
    if (!PC || !Director || !Director->IsReady() || !PC->IsSessionStarted() || PC->IsGamePaused()
        || PC->GetGarageSession()->IsActive() || PC->GetCinematic()->IsActive() || (Settings && Settings->IsOpen())
        || (PC->GetRaceManager() && PC->GetRaceManager()->GetState()!=EADRaceState::Idle)) return;
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
        if (FVector2D::Distance(Position,ADMapLayout::Project(Location.Position))<20.)
        { SelectedRow=Row; Confirm(); return; }
    }
}
void UADMapComponent::EndPlay(const EEndPlayReason::Type Reason)
{ Close(); Super::EndPlay(Reason); }
