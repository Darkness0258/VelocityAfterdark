#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "ADGarageSessionComponent.generated.h"

class AADGarage;
class AADVehiclePawn;
class AADPlayerController;

/** Local garage session: draft edits, preview, commit and return to the world. */
UCLASS()
class VELOCITYAFTERDARK_API UADGarageSessionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UADGarageSessionComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Enter();
    void Leave(bool bDiscardDraft = true);
    bool IsActive() const;
    void MoveSelection(int32 Delta);
    void AdjustSelection(int32 Delta);
    void ConfirmSelection();
    bool CommitChanges();
    void DiscardChanges();
    int32 GetSelectedRow() const { return SelectedRow; }
    int32 GetSaveRow() const;
    int32 GetVehicleRow() const { return GetSaveRow() + 2; }
    const FADGarageProfile& GetDraft() const { return Draft; }
    const FString& GetMessage() const { return Message; }
    const FADVehicleDefinition& GetPreviewDefinition() const { return PreviewDefinition; }
    int64 GetPendingCost() const;
    UADOwnershipSubsystem* GetOwnership() const;
    void SelectRow(int32 Row);
    void Zoom(float Delta);
private:
    AADPlayerController* Controller() const;
    bool ApplyProfile(const FADGarageProfile& Profile, FString& Error);
    bool PreviewPaint();
    UPROPERTY(Transient) TObjectPtr<AADGarage> Garage;
    FADGarageProfile Draft;
    FADVehicleDefinition PreviewDefinition;
    FString Message;
    int32 SelectedRow = 0;
    bool bInitialProfileApplied = false;
};
