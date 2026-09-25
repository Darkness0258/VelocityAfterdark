#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/PrimitiveComponent.h"
#include "Core/ADGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Garage/ADGarageSessionComponent.h"
#include "InputKeyEventArgs.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "ProceduralMeshComponent.h"
#include "Racing/ADRaceManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

namespace
{
void SelectRow(UADGarageSessionComponent* Garage, int32 Row)
{
    Garage->MoveSelection(Row - Garage->GetSelectedRow());
}

FLinearColor BodyPaint(AADVehiclePawn* Car)
{
    const auto* Body = Car->FindComponentByClass<UProceduralMeshComponent>();
    auto* Material = Body ? Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)) : nullptr;
    return Material ? Material->K2_GetVectorParameterValue(TEXT("BaseColor")) : FLinearColor::Transparent;
}

class FADGarageFlowCommand final : public IAutomationLatentCommand
{
public:
    explicit FADGarageFlowCommand(FAutomationTestBase* InTest)
        : Test(InTest), Deadline(FPlatformTime::Seconds() + 90.) {}

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Garage integration timed out.")); return true; }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        auto* PC = Cast<AADPlayerController>(World->GetFirstPlayerController());
        auto* Car = PC ? PC->GetVehiclePawn() : nullptr;
        auto* Mode = Cast<AADGameMode>(World->GetAuthGameMode());
        auto* Ownership = World->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
        auto* Garage = PC ? PC->GetGarageSession() : nullptr;
        if (!Car || !Mode || !Mode->IsWorldReady() || !Garage || !Ownership) return false;
        auto* Chassis = Cast<UPrimitiveComponent>(Car->GetRootComponent());
        auto* Physics = Car->GetPhysics();
        if (!Chassis || !Physics->IsReady()) return false;
        const auto Key = [PC](FKey K, EInputEvent Event) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Event, Event == IE_Released ? 0.f : 1.f)); };
        const float Now = World->GetTimeSeconds();
        if (Stage > 0 && Now - StageStart < .5f) return false;
        FString Error;
        switch (Stage)
        {
        case 0:
            if (!Test->TestTrue(TEXT("Runtime test uses an isolated memory profile"), Ownership->InitializeProfile(TEXT(""), Error)))
            { Test->AddError(Error); return true; }
            if (!Test->TestTrue(TEXT("Garage catalog is ready"), Ownership->IsReady()) || Ownership->GetPaints().Num() < 2
                || Ownership->GetUpgrades().Num() != 4 || Ownership->GetTunes().Num() < 2)
            { Test->AddError(TEXT("Garage catalog lacks the phase-three entries.")); return true; }
            PC->StartDriving();
            Car->ResetVehicle();
            break;
        case 1:
            Chassis->SetPhysicsLinearVelocity(Car->GetActorForwardVector() * 1000.f);
            Test->TestFalse(TEXT("Garage rejects a moving car"), Garage->Enter());
            Test->TestFalse(TEXT("Moving guard preserves driving simulation"), Car->IsInGarage());
            Car->ResetVehicle();
            if (!Test->TestTrue(TEXT("Race starts for garage exclusion check"), PC->GetRaceManager()->StartRace(Car, 1))) return true;
            Test->TestFalse(TEXT("Garage rejects a live race countdown"), Garage->Enter());
            PC->GetRaceManager()->LeaveRace();
            Car->ResetVehicle();
            break;
        case 2:
            ReturnPose = Car->GetActorTransform();
            Key(EKeys::G, IE_Pressed);
            break;
        case 3:
            Key(EKeys::G, IE_Released);
            if (!Test->TestTrue(TEXT("G enters the rendered garage through real input"), Garage->IsActive())) return true;
            Test->TestTrue(TEXT("Pawn is in garage presentation mode"), Car->IsInGarage());
            Test->TestFalse(TEXT("Parked vehicle physics is suspended"), Chassis->IsSimulatingPhysics());
            StartingCredits = Ownership->GetProfile().Credits;
            StockTorque = Physics->GetDefinition().GetTorqueNm(4000.f);
            SelectRow(Garage, 0);
            Garage->AdjustSelection(1);
            Test->TestTrue(TEXT("Paint preview selects a different catalog color"), Garage->GetDraft().PaintId != Ownership->GetProfile().PaintId);
            Test->TestTrue(TEXT("Paint preview changes the actual body material"), BodyPaint(Car).Equals(Ownership->GetPaints()[1].Color, .001f));
            SelectRow(Garage, 1);
            Garage->AdjustSelection(1);
            Test->TestTrue(TEXT("Upgrade preview increases real torque definition"), Garage->GetPreviewDefinition().GetTorqueNm(4000.f) > StockTorque);
            Test->TestEqual(TEXT("Preview does not mutate live physical configuration"), Physics->GetDefinition().GetTorqueNm(4000.f), StockTorque);
            Test->TestEqual(TEXT("Preview does not spend credits"), Ownership->GetProfile().Credits, StartingCredits);
            Garage->DiscardChanges();
            Test->TestTrue(TEXT("Cancel discards pending ownership"), Garage->GetDraft().OwnedUpgrades.IsEmpty());
            Test->TestTrue(TEXT("Cancel restores the actual body paint"), BodyPaint(Car).Equals(Ownership->GetPaints()[0].Color, .001f));
            Test->TestEqual(TEXT("Cancel restores stock preview"), Garage->GetPreviewDefinition().GetTorqueNm(4000.f), StockTorque);
            SelectRow(Garage, 1);
            Garage->AdjustSelection(1);
            SelectRow(Garage, 5);
            Garage->AdjustSelection(1);
            if (!Test->TestTrue(TEXT("Commit installs ECU and Sprint tuning"), Garage->CommitChanges())) { Test->AddError(Garage->GetMessage()); return true; }
            PaidCredits = StartingCredits - Ownership->GetUpgrades()[0].Price;
            Test->TestEqual(TEXT("Purchase costs exactly the catalog price"), Ownership->GetProfile().Credits, PaidCredits);
            Test->TestTrue(TEXT("Commit changes the actual physical engine curve"), Physics->GetDefinition().GetTorqueNm(4000.f) > StockTorque);
            Test->TestTrue(TEXT("Sprint changes physical final drive"), FMath::IsNearlyEqual(Physics->GetDefinition().FinalDrive,
                Ownership->GetStockDefinition().FinalDrive * Ownership->GetTunes()[1].FinalDriveMultiplier));
            Test->TestTrue(TEXT("Duplicate commit succeeds without another purchase"), Garage->CommitChanges());
            Test->TestEqual(TEXT("Duplicate commit never charges again"), Ownership->GetProfile().Credits, PaidCredits);
            break;
        case 4:
            SelectRow(Garage, 1);
            Garage->AdjustSelection(1);
            Test->TestTrue(TEXT("Owned ECU can be unequipped"), Garage->CommitChanges());
            Test->TestEqual(TEXT("Unequipping restores the original engine curve"), Physics->GetDefinition().GetTorqueNm(4000.f), StockTorque);
            Test->TestEqual(TEXT("Unequipping preserves spent credits"), Ownership->GetProfile().Credits, PaidCredits);
            SelectRow(Garage, 5);
            Garage->AdjustSelection(-1);
            Test->TestTrue(TEXT("Street preset can be restored"), Garage->CommitChanges());
            Test->TestEqual(TEXT("Street preset restores stock final drive"), Physics->GetDefinition().FinalDrive, Ownership->GetStockDefinition().FinalDrive);
            SelectRow(Garage, 1);
            Garage->AdjustSelection(1);
            Test->TestTrue(TEXT("Owned ECU can be re-equipped"), Garage->CommitChanges());
            Test->TestEqual(TEXT("Re-equipping is free"), Ownership->GetProfile().Credits, PaidCredits);
            Garage->Leave();
            Test->TestFalse(TEXT("Returning closes the garage session"), Garage->IsActive());
            Test->TestTrue(TEXT("Returning restores driving"), Car->IsDrivingEnabled());
            Test->TestTrue(TEXT("Returning restores the saved world position"), Car->GetActorLocation().Equals(ReturnPose.GetLocation(), 3.));
            Test->TestTrue(TEXT("Returning restores the saved world orientation"), Car->GetActorQuat().Equals(ReturnPose.GetRotation(), .002));
            Key(EKeys::G, IE_Pressed);
            break;
        case 5:
            Key(EKeys::G, IE_Released);
            if (!Test->TestTrue(TEXT("Garage reopens through input"), Garage->IsActive())) return true;
            SelectRow(Garage, 0);
            Key(EKeys::Down, IE_Pressed);
            break;
        case 6:
            Key(EKeys::Down, IE_Released);
            Test->TestEqual(TEXT("Down selects the next garage row"), Garage->GetSelectedRow(), 1);
            Key(EKeys::Enter, IE_Pressed);
            break;
        case 7:
            Key(EKeys::Enter, IE_Released);
            Test->TestTrue(TEXT("Enter toggles the selected ECU in the draft"), Garage->GetDraft().EquippedUpgrades.IsEmpty());
            Key(EKeys::Escape, IE_Pressed);
            break;
        case 8:
            Key(EKeys::Escape, IE_Released);
            Test->TestFalse(TEXT("Escape leaves the garage"), Garage->IsActive());
            Test->TestFalse(TEXT("Garage Escape does not pause the driving world"), PC->IsGamePaused());
            Test->TestEqual(TEXT("Escape preserves committed ownership"), Ownership->GetProfile().EquippedUpgrades.Num(), 1);
            Test->TestTrue(TEXT("Escape preserves the committed physical upgrade"), Physics->GetDefinition().GetTorqueNm(4000.f) > StockTorque);
            PC->FlushPressedKeys();
            Car->ResetVehicle();
            return true;
        default: return true;
        }
        ++Stage;
        StageStart = Now;
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Deadline;
    float StageStart = 0.f;
    float StockTorque = 0.f;
    int32 Stage = 0;
    int64 StartingCredits = 0;
    int64 PaidCredits = 0;
    FTransform ReturnPose;
};

class FADGarageAccelerationCommand final : public IAutomationLatentCommand
{
public:
    explicit FADGarageAccelerationCommand(FAutomationTestBase* InTest)
        : Test(InTest), Deadline(FPlatformTime::Seconds() + 90.) {}
    virtual ~FADGarageAccelerationCommand() override
    {
        if (bChangedTimeStep) { FApp::SetFixedDeltaTime(PreviousStep); FApp::SetUseFixedTimeStep(bPreviousFixed); }
    }
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Garage physical acceleration comparison timed out.")); return true; }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        auto* PC = Cast<AADPlayerController>(World->GetFirstPlayerController());
        auto* Car = PC ? PC->GetVehiclePawn() : nullptr;
        auto* Mode = Cast<AADGameMode>(World->GetAuthGameMode());
        auto* Ownership = World->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
        auto* Garage = PC ? PC->GetGarageSession() : nullptr;
        if (!Car || !Mode || !Mode->IsWorldReady() || !Garage || !Ownership || !Car->GetPhysics()->IsReady()) return false;
        auto* Physics = Car->GetPhysics();
        FString Error;
        if (Stage == 0)
        {
            if (!Test->TestTrue(TEXT("Physical comparison uses isolated ownership"), Ownership->InitializeProfile(TEXT(""), Error))) return true;
            PreviousStep = FApp::GetFixedDeltaTime();
            bPreviousFixed = FApp::UseFixedTimeStep();
            FApp::SetFixedDeltaTime(1. / 60.);
            FApp::SetUseFixedTimeStep(true);
            bChangedTimeStep = true;
            PC->StartDriving();
            Car->ResetVehicle();
            if (!Test->TestTrue(TEXT("Stock comparison car enters garage"), Garage->Enter())) return true;
            if (!Test->TestTrue(TEXT("Stock definition installs before physical run"), Garage->CommitChanges())) return true;
            Garage->Leave();
            Car->ResetVehicle();
            ResetPose = Car->GetActorTransform();
            Physics->SetControls(0.f, 0.f, 0.f, false);
            Stage = 1; Frames = 0;
            return false;
        }
        ++Frames;
        if ((Stage == 1 || Stage == 3) && Frames >= 120)
        {
            Test->TestEqual(TEXT("Comparison car settles on four tires"), Physics->GetTelemetry().GroundedWheels, 4);
            Physics->SetControls(1.f, 0.f, 0.f, false);
            ++Stage; Frames = 0;
        }
        else if ((Stage == 2 || Stage == 4) && Frames >= 240)
        {
            const float Speed = Physics->GetTelemetry().SpeedKmh;
            Test->TestTrue(TEXT("Comparison launch produces finite forward acceleration"), FMath::IsFinite(Speed) && Speed > 20.f);
            Physics->SetControls(0.f, 1.f, 0.f, false);
            if (Stage == 4)
            {
                Test->AddInfo(FString::Printf(TEXT("Actual 4-second launches at 60 Hz: stock %.3f km/h; ECU %.3f km/h."), StockSpeed, Speed));
                Test->TestTrue(TEXT("ECU provides a measurable physical acceleration gain"), Speed > StockSpeed * 1.01f);
                Car->ResetVehicle();
                return true;
            }
            StockSpeed = Speed;
            Car->ResetVehicle();
            Test->TestTrue(TEXT("Second launch starts from the identical reset pose"), Car->GetActorTransform().Equals(ResetPose, .01f));
            if (!Test->TestTrue(TEXT("Stopped comparison car enters garage"), Garage->Enter())) return true;
            SelectRow(Garage, 1);
            Garage->AdjustSelection(1);
            if (!Test->TestTrue(TEXT("ECU purchase installs physical definition"), Garage->CommitChanges())) return true;
            Garage->Leave();
            Car->ResetVehicle();
            Physics->SetControls(0.f, 0.f, 0.f, false);
            Stage = 3; Frames = 0;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Deadline;
    double PreviousStep = 0.;
    bool bPreviousFixed = false;
    bool bChangedTimeStep = false;
    int32 Stage = 0;
    int32 Frames = 0;
    float StockSpeed = 0.f;
    FTransform ResetPose;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADGarageFlowTest, "Afterdark.Runtime.GarageFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FADGarageFlowTest::RunTest(const FString& Parameters)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FADGarageFlowCommand(this)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADGarageAccelerationTest, "Afterdark.Runtime.GarageAcceleration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FADGarageAccelerationTest::RunTest(const FString& Parameters)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FADGarageAccelerationCommand(this)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
