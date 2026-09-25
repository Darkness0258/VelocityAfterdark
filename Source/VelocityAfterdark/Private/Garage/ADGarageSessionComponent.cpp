#include "Garage/ADGarageSessionComponent.h"
#include "Garage/ADGarage.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Core/ADGameMode.h"
#include "World/ADPoliceDirector.h"

UADGarageSessionComponent::UADGarageSessionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

AADPlayerController* UADGarageSessionComponent::Controller() const { return Cast<AADPlayerController>(GetOwner()); }

UADOwnershipSubsystem* UADGarageSessionComponent::GetOwnership() const
{
    return GetWorld() && GetWorld()->GetGameInstance()
        ? GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>() : nullptr;
}

bool UADGarageSessionComponent::IsActive() const { return IsValid(Garage) && Garage->IsOccupied(); }

int32 UADGarageSessionComponent::GetSaveRow() const
{
    const auto* Ownership = GetOwnership();
    return Ownership ? Ownership->GetUpgrades().Num() + 2 : 2;
}

void UADGarageSessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    auto* PC = Controller();
    if (!PC) return;
    if (!bInitialProfileApplied)
    {
        if (GetNetMode()!=NM_Standalone)
        { bInitialProfileApplied=true; Message=TEXT("ONLINE FREE ROAM / stock cars. Career and garage purchases are available offline."); SetComponentTickEnabled(false); return; }
        auto* Car = PC->GetVehiclePawn();
        auto* Ownership = GetOwnership();
        if (!Car || !Car->GetPhysics()->IsReady() || !Ownership) return;
        bInitialProfileApplied = true;
        if (Ownership->IsReady())
        {
            FString Error;
            if (!ApplyProfile(Ownership->GetProfile(), Error)) Message = Error;
            else Message = Ownership->GetStatus();
        }
        else Message = Ownership->GetError();
    }
    if (!IsActive()) { SetComponentTickEnabled(false); return; }
    const float Yaw = (PC->IsInputKeyDown(EKeys::L) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::J) ? 1.f : 0.f)
        + PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
    const float Pitch = (PC->IsInputKeyDown(EKeys::I) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::K) ? 1.f : 0.f)
        + PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
    Garage->Orbit(Yaw * 55.f * DeltaTime, Pitch * 25.f * DeltaTime);
}

bool UADGarageSessionComponent::ApplyProfile(const FADGarageProfile& Profile, FString& Error)
{
    auto* PC = Controller();
    auto* Car = PC ? PC->GetVehiclePawn() : nullptr;
    auto* Ownership = GetOwnership();
    auto* Chassis = Car ? Cast<UPrimitiveComponent>(Car->GetRootComponent()) : nullptr;
    if (!Chassis || !Ownership || !Ownership->IsReady()) { Error = TEXT("Vehicle or ownership data unavailable."); return false; }
    if (Chassis->GetPhysicsLinearVelocity().Size() > 5.f / .036f)
    { Error = TEXT("Stop the vehicle before applying a garage configuration."); return false; }
    const bool bWasSimulating = Chassis->IsSimulatingPhysics();
    Chassis->SetSimulatePhysics(false);
    const bool bApplied = Car->ApplyGarageVehicle(Ownership->GetStockDefinition(Profile),Ownership->BuildDefinition(Profile),Error);
    Chassis->SetSimulatePhysics(bWasSimulating);
    if (!bApplied) return false;
    for (const auto& Paint : Ownership->GetPaints())
        if (Paint.Id == Profile.PaintId) { Car->SetPaintColor(Paint.Color); break; }
    return true;
}

bool UADGarageSessionComponent::Enter()
{
    if (GetNetMode()!=NM_Standalone) { Message=TEXT("Garage purchases are available in offline career."); return false; }
    auto* PC = Controller();
    auto* Ownership = GetOwnership();
    if (IsActive()) return true;
    if (!PC || !Ownership || !Ownership->IsReady())
    { Message = Ownership ? Ownership->GetError() : TEXT("Ownership system unavailable."); return false; }
    if (PC->IsGamePaused()) { Message = TEXT("Resume the drive before entering the garage."); return false; }
    if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive())
    { Message = TEXT("Lose the police and finish their search before entering the garage."); return false; }
    if (PC->GetRaceManager() && PC->GetRaceManager()->GetState() != EADRaceState::Idle)
    { Message = TEXT("Leave the race before entering the garage."); return false; }
    auto* Car = PC->GetVehiclePawn();
    if (!Car || !Car->GetPhysics()->IsReady()) { Message = TEXT("Vehicle unavailable."); return false; }
    if (!Garage)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Garage = GetWorld()->SpawnActor<AADGarage>(FVector(200000,200000,3000), FRotator::ZeroRotator, Params);
    }
    if (!Garage) { Message = TEXT("Garage scene could not be created."); return false; }
    if (!Garage->Enter(Car, Message)) return false;
    Draft = Ownership->GetProfile();
    SelectedRow = 0;
    bInitialProfileApplied = true;
    PC->FlushPressedKeys();
    PC->SetViewTarget(Garage);
    PC->bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(Mode);
    SetComponentTickEnabled(true);
    if (PreviewPaint()) Message = TEXT("Preview your build. The VEHICLE row opens the dealership; SAVE purchases and fits the selection.");
    return true;
}

void UADGarageSessionComponent::Leave(bool bDiscardDraft)
{
    if (!IsActive()) return;
    if (!bDiscardDraft && !CommitChanges()) return;
    DiscardChanges();
    auto* PC = Controller();
    Garage->Exit();
    if (PC)
    {
        PC->SetViewTarget(PC->GetVehiclePawn());
        PC->FlushPressedKeys();
        PC->bShowMouseCursor = !PC->IsSessionStarted();
        if (PC->IsSessionStarted()) PC->SetInputMode(FInputModeGameOnly());
        else PC->SetInputMode(FInputModeGameAndUI());
    }
    SetComponentTickEnabled(false);
    Message = TEXT("Garage closed. Your saved build is ready.");
}

void UADGarageSessionComponent::MoveSelection(int32 Delta)
{
    if (!IsActive()) return;
    const int32 Count = GetVehicleRow() + 1;
    SelectedRow = ((SelectedRow + Delta) % Count + Count) % Count;
}

void UADGarageSessionComponent::SelectRow(int32 Row)
{
    if (IsActive()) SelectedRow = FMath::Clamp(Row, 0, GetVehicleRow());
}

void UADGarageSessionComponent::AdjustSelection(int32 Delta)
{
    if (!IsActive() || Delta == 0) return;
    const auto* Ownership = GetOwnership();
    if (!Ownership) return;
    const auto Cycle = [Delta](int32 Current, int32 Count) { return ((Current + Delta) % Count + Count) % Count; };
    if (SelectedRow == 0)
    {
        const auto& Paints = Ownership->GetPaints();
        const int32 Index = Paints.IndexOfByPredicate([this](const auto& P) { return P.Id == Draft.PaintId; });
        if (!Paints.IsEmpty()) Draft.PaintId = Paints[Cycle(FMath::Max(0,Index), Paints.Num())].Id;
    }
    else if (SelectedRow <= Ownership->GetUpgrades().Num())
    {
        const auto& Upgrade = Ownership->GetUpgrades()[SelectedRow - 1];
        if (Draft.EquippedUpgrades.Contains(Upgrade.Id))
        {
            Draft.EquippedUpgrades.Remove(Upgrade.Id);
            const auto* Saved=Ownership->GetProfile().Vehicles.FindByPredicate([&](const auto& Item) { return Item.VehicleId==Draft.ActiveVehicleId; });
            if (!Saved || !Saved->OwnedUpgrades.Contains(Upgrade.Id)) Draft.OwnedUpgrades.Remove(Upgrade.Id);
        }
        else { Draft.OwnedUpgrades.AddUnique(Upgrade.Id); Draft.EquippedUpgrades.AddUnique(Upgrade.Id); }
    }
    else if (SelectedRow == GetSaveRow() - 1)
    {
        const auto& Tunes = Ownership->GetTunes();
        const int32 Index = Tunes.IndexOfByPredicate([this](const auto& T) { return T.Id == Draft.TuneId; });
        if (!Tunes.IsEmpty()) Draft.TuneId = Tunes[Cycle(FMath::Max(0,Index), Tunes.Num())].Id;
    }
    else if (SelectedRow == GetVehicleRow())
    {
        const auto& Vehicles=Ownership->GetVehicles();
        const int32 Index=Vehicles.IndexOfByPredicate([&](const auto& Item) { return Item.Id==Draft.ActiveVehicleId; });
        if (!Vehicles.IsEmpty() && !Ownership->MakeVehicleDraft(Vehicles[Cycle(FMath::Max(0,Index),Vehicles.Num())].Id,Draft,Message)) return;
        if (PreviewPaint()) Message=TEXT("VEHICLE PREVIEW. Browsing restores each car's saved build. SAVE purchases/equips; RETURN cancels.");
        return;
    }
    if (PreviewPaint()) Message = TEXT("Preview only. Save changes to fit this build; return cancels unsaved edits.");
}

void UADGarageSessionComponent::ConfirmSelection()
{
    if (SelectedRow == GetSaveRow()) CommitChanges();
    else if (SelectedRow == GetSaveRow()+1) Leave();
    else AdjustSelection(1);
}

bool UADGarageSessionComponent::PreviewPaint()
{
    const auto* Ownership = GetOwnership();
    auto* PC = Controller();
    if (!Ownership || !PC || !PC->GetVehiclePawn()) { Message=TEXT("Vehicle preview is unavailable."); return false; }
    PreviewDefinition = Ownership->BuildDefinition(Draft);
    if (!PC->GetVehiclePawn()->PreviewGarageVehicle(PreviewDefinition,Message)) return false;
    for (const auto& Paint : Ownership->GetPaints())
        if (Paint.Id == Draft.PaintId) { PC->GetVehiclePawn()->SetPaintColor(Paint.Color); break; }
    return true;
}

int64 UADGarageSessionComponent::GetPendingCost() const
{
    const auto* Ownership = GetOwnership();
    int64 Cost = 0;
    FString Error;
    if (Ownership) Ownership->GetPurchaseCost(Draft,Cost,Error);
    return Cost;
}

bool UADGarageSessionComponent::CommitChanges()
{
    if (!IsActive()) return false;
    auto* Ownership = GetOwnership();
    if (!Ownership) return false;
    const FADGarageProfile Previous = Ownership->GetProfile();
    // Validate the live application while the car is frozen, before committing
    // credits. A disk failure restores the previous physical configuration.
    if (!ApplyProfile(Draft, Message)) return false;
    if (!Ownership->Commit(Draft, Message))
    {
        FString RestoreError;
        if (!ApplyProfile(Previous, RestoreError)) Message += TEXT(" Restore failed: ") + RestoreError;
        const FString SaveError=Message;
        PreviewPaint();
        Message=SaveError;
        return false;
    }
    Draft = Ownership->GetProfile();
    Message = TEXT("BUILD SAVED. Vehicle selected, parts fitted, and ready to drive.");
    if (Controller() && Controller()->GetVehiclePawn()) Controller()->GetVehiclePawn()->GetEffects()->Repair();
    return true;
}

void UADGarageSessionComponent::DiscardChanges()
{
    if (!IsActive() || !GetOwnership()) return;
    Draft = GetOwnership()->GetProfile();
    if (PreviewPaint()) Message = TEXT("Preview reset to your saved build.");
}

void UADGarageSessionComponent::Zoom(float Delta) { if (IsActive()) Garage->Zoom(Delta); }

void UADGarageSessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(Garage)) { Garage->Exit(); Garage->Destroy(); }
    Super::EndPlay(Reason);
}
