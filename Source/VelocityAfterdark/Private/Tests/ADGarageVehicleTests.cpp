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
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

namespace
{
double RoofHeight(AADVehiclePawn* Car)
{
    auto* Body=Car->FindComponentByClass<UProceduralMeshComponent>();
    const auto* Roof=Body ? Body->GetProcMeshSection(2) : nullptr;
    return Roof ? Roof->SectionLocalBox.Max.Z : -1.;
}

class FADGarageVehicleCommand final:public IAutomationLatentCommand
{
public:
    explicit FADGarageVehicleCommand(FAutomationTestBase* InTest):Test(InTest),Deadline(FPlatformTime::Seconds()+90.) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds()>Deadline) { Test->AddError(TEXT("Garage vehicle switching timed out.")); return true; }
        UWorld* World=GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        auto* PC=Cast<AADPlayerController>(World->GetFirstPlayerController());
        auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
        auto* Mode=Cast<AADGameMode>(World->GetAuthGameMode());
        auto* Ownership=World->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
        auto* Garage=PC ? PC->GetGarageSession() : nullptr;
        if (!Car || !Mode || !Mode->IsWorldReady() || !Garage || !Ownership || !Car->GetPhysics()->IsReady()) return false;
        auto* Physics=Car->GetPhysics();
        auto* Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent());
        if (!Chassis) return false;
        const auto Key=[PC](FKey K,EInputEvent Event)
        { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Event,Event==IE_Released ? 0.f : 1.f)); };
        FString Error;
        const double Now=World->GetTimeSeconds();
        if (Stage==0)
        {
            if (!Test->TestTrue(TEXT("Switching test uses isolated session profile"),Ownership->InitializeProfile(TEXT(""),Error))) return true;
            PC->StartDriving(); Car->ResetVehicle();
            Stage=1; StageStart=Now;
            return false;
        }
        if (Stage==1 && Now-StageStart>=.5)
        {
            Car->ResetVehicle();
            if (!Test->TestTrue(TEXT("Vehicle showroom opens"),Garage->Enter())) { Test->AddError(Garage->GetMessage()); return true; }
            if (!Test->TestTrue(TEXT("Known starter configuration installs"),Garage->CommitChanges())) return true;
            const double StarterRoof=RoofHeight(Car);
            Garage->SelectRow(Garage->GetVehicleRow()); Garage->AdjustSelection(1);
            Test->TestEqual(TEXT("Showroom selects hatchback"),Garage->GetDraft().ActiveVehicleId,FString(TEXT("kestrel_xr")));
            Test->TestTrue(TEXT("Preview changes real roof mesh"),RoofHeight(Car)>StarterRoof+10.);
            Test->TestEqual(TEXT("Preview does not replace physical drivetrain"),Physics->GetDefinition().VehicleId,FString(TEXT("aster_s6")));
            Test->TestEqual(TEXT("Preview does not purchase a vehicle"),Ownership->GetProfile().Credits,int64(12000));
            Garage->DiscardChanges();
            Test->TestEqual(TEXT("Cancel restores rendered roof mesh"),RoofHeight(Car),StarterRoof);
            Garage->SelectRow(Garage->GetVehicleRow()); Garage->AdjustSelection(1);
            if (!Test->TestTrue(TEXT("Buy and equip hatchback"),Garage->CommitChanges())) { Test->AddError(Garage->GetMessage()); return true; }
            Test->TestEqual(TEXT("Physical vehicle changes"),Physics->GetDefinition().VehicleId,FString(TEXT("kestrel_xr")));
            Test->TestTrue(TEXT("Physical drivetrain is front-wheel drive"),Physics->GetDefinition().Drivetrain==EADDrivetrain::FrontWheelDrive);
            Test->TestEqual(TEXT("Purchase debits catalog credits"),Ownership->GetProfile().Credits,int64(3500));
            const float StockHatchTorque=Physics->GetDefinition().GetTorqueNm(4000.f);
            Garage->SelectRow(1); Garage->AdjustSelection(1);
            if (!Test->TestTrue(TEXT("Install hatch-specific ECU"),Garage->CommitChanges())) return true;
            HatchTorque=Physics->GetDefinition().GetTorqueNm(4000.f);
            Test->TestTrue(TEXT("Hatch-specific ECU changes real engine"),HatchTorque>StockHatchTorque*1.1f);
            Garage->SelectRow(Garage->GetVehicleRow()); Garage->AdjustSelection(-1);
            if (!Test->TestTrue(TEXT("Equip owned starter"),Garage->CommitChanges())) return true;
            Test->TestTrue(TEXT("Starter retains factory parts"),Ownership->GetProfile().EquippedUpgrades.IsEmpty());
            Test->TestEqual(TEXT("Starter remains physically stock"),Physics->GetDefinition().GetTorqueNm(4000.f),Ownership->GetStockDefinition().GetTorqueNm(4000.f));
            Garage->SelectRow(Garage->GetVehicleRow()); Garage->AdjustSelection(1);
            Test->TestTrue(TEXT("Returning hatch restores its own ECU draft"),Garage->GetDraft().EquippedUpgrades.Contains(TEXT("ecu")));
            if (!Test->TestTrue(TEXT("Re-equip customized hatch"),Garage->CommitChanges())) return true;
            Test->TestEqual(TEXT("Saved hatch engine survives switching"),Physics->GetDefinition().GetTorqueNm(4000.f),HatchTorque);
            Garage->Leave(); Car->ResetVehicle();
            Physics->SetControls(0.f,0.f,0.f,false);
            Stage=2; StageStart=Now;
            return false;
        }
        if (Stage==2 && Now-StageStart>=2.)
        {
            Test->TestEqual(TEXT("Purchased chassis has the configured mass while simulating"),Chassis->GetMass(),Ownership->GetStockDefinition().MassKg);
            Test->TestEqual(TEXT("New vehicle settles on four physical wheels"),Physics->GetTelemetry().GroundedWheels,4);
            Key(EKeys::W,IE_Pressed);
            Stage=3; StageStart=Now;
            return false;
        }
        if (Stage==3 && Now-StageStart>=4.)
        {
            const float Speed=Physics->GetTelemetry().SpeedKmh;
            Test->TestTrue(TEXT("Purchased front-drive vehicle accelerates through held keyboard input"),FMath::IsFinite(Speed) && Speed>20.f);
            Key(EKeys::W,IE_Released);
            PC->FlushPressedKeys();
            Physics->SetControls(0.f,1.f,0.f,false); Car->ResetVehicle();
            Stage=4; StageStart=Now;
            return false;
        }
        if (Stage==4 && Now-StageStart>=.5)
        {
            Car->ResetVehicle();
            if (!Test->TestTrue(TEXT("Driven hatch can return to showroom"),Garage->Enter())) return true;
            const double HatchRoof=RoofHeight(Car);
            Garage->SelectRow(Garage->GetVehicleRow()); Garage->AdjustSelection(1);
            Test->TestEqual(TEXT("Showroom selects premium sedan"),Garage->GetDraft().ActiveVehicleId,FString(TEXT("meridian_gt")));
            Test->TestFalse(TEXT("Unaffordable purchase fails"),Garage->CommitChanges());
            Test->TestEqual(TEXT("Failed purchase restores physical hatch"),Physics->GetDefinition().VehicleId,FString(TEXT("kestrel_xr")));
            Test->TestEqual(TEXT("Failed purchase preserves hatch-specific engine"),Physics->GetDefinition().GetTorqueNm(4000.f),HatchTorque);
            Test->TestEqual(TEXT("Failed purchase preserves active ownership"),Ownership->GetProfile().ActiveVehicleId,FString(TEXT("kestrel_xr")));
            Test->TestFalse(TEXT("Failed purchase does not grant premium car"),Ownership->IsVehicleOwned(TEXT("meridian_gt")));
            Garage->Leave();
            Test->TestEqual(TEXT("Leaving restores saved hatch silhouette"),RoofHeight(Car),HatchRoof);
            Test->TestTrue(TEXT("Leaving restores driving simulation"),Chassis->IsSimulatingPhysics() && Car->IsDrivingEnabled());
            Car->ResetVehicle();
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Deadline;
    double StageStart=0.;
    int32 Stage=0;
    float HatchTorque=0.f;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADGarageVehicleSwitchTest,"Afterdark.Runtime.GarageVehicleSwitch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADGarageVehicleSwitchTest::RunTest(const FString&)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FADGarageVehicleCommand(this)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
