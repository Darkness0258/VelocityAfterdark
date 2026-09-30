#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/ADVehicleMath.h"
#include "Vehicle/ADVehicleDefinition.h"
#include "World/ADPhysicalSurface.h"
#include "World/ADTrafficManager.h"
#include "Presentation/ADMapComponent.h"
#include "Sound/SoundWave.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADTrafficSignalPhaseTest,"Afterdark.Runtime.Traffic.SignalPhases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FADTrafficSignalPhaseTest::RunTest(const FString& Parameters)
{
    constexpr float Green=20.f,Amber=3.f,Red=8.f;
    using Phase=EADTrafficSignalPhase;
    TestTrue(TEXT("Cycle begins green"),AADTrafficManager::GetSignalPhaseAtTime(0.f,Green,Amber,Red)==Phase::Green);
    TestTrue(TEXT("Green remains active until its configured boundary"),AADTrafficManager::GetSignalPhaseAtTime(19.99f,Green,Amber,Red)==Phase::Green);
    TestTrue(TEXT("Amber follows green"),AADTrafficManager::GetSignalPhaseAtTime(20.f,Green,Amber,Red)==Phase::Amber);
    TestTrue(TEXT("Amber lasts for its complete configured interval"),AADTrafficManager::GetSignalPhaseAtTime(22.99f,Green,Amber,Red)==Phase::Amber);
    TestTrue(TEXT("Red follows amber"),AADTrafficManager::GetSignalPhaseAtTime(23.f,Green,Amber,Red)==Phase::Red);
    TestTrue(TEXT("The cycle wraps back to green"),AADTrafficManager::GetSignalPhaseAtTime(31.f,Green,Amber,Red)==Phase::Green);
    TestTrue(TEXT("Invalid phase durations fail conservatively to red"),AADTrafficManager::GetSignalPhaseAtTime(1.f,0.f,Amber,Red)==Phase::Red);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADMinimapProjectionTest,"Afterdark.Data.MinimapProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FADMinimapProjectionTest::RunTest(const FString& Parameters)
{
    const FVector2D Origin(12000.,-8500.);
    const FVector2D East(1.,0.),North(0.,1.);
    const FVector2D Center=ADMinimapLayout::Project(Origin,Origin,East);
    TestTrue(TEXT("Player is centred on its own minimap"),Center.IsNearlyZero());
    const FVector2D Ahead=ADMinimapLayout::Project(Origin+FVector2D(10000.,0.),Origin,East);
    TestTrue(TEXT("Heading-up projection places the road ahead above the player"),Ahead.X==0. && Ahead.Y<0.);
    const FVector2D Right=ADMinimapLayout::Project(Origin+FVector2D(0.,10000.),Origin,East);
    TestTrue(TEXT("Roads on the driver's right appear on the right"),Right.X>0. && Right.Y==0.);
    const FVector2D TurnedAhead=ADMinimapLayout::Project(Origin+FVector2D(0.,10000.),Origin,North);
    TestTrue(TEXT("Player heading rotates world roads under the fixed vehicle marker"),TurnedAhead.X==0. && TurnedAhead.Y<0.);
    const FVector2D InvalidHeading=ADMinimapLayout::Project(Origin+FVector2D(10000.,0.),Origin,FVector2D::ZeroVector);
    TestTrue(TEXT("Degenerate heading uses a stable eastward fallback"),InvalidHeading.Y<0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADArrivalVoiceoverAssetTest,"Afterdark.Runtime.Audio.ArrivalVoiceover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FADArrivalVoiceoverAssetTest::RunTest(const FString& Parameters)
{
    USoundWave* Voice=LoadObject<USoundWave>(nullptr,TEXT("/Game/Velocity/Audio/Voice/VO_ArrivalBroadcast.VO_ArrivalBroadcast"));
    TestNotNull(TEXT("Opening broadcast imports as a loadable SoundWave"),Voice);
    if (!Voice) return false;
    TestTrue(TEXT("Broadcast clip fits the ten-second subtitle window"),Voice->Duration>=1.f && Voice->Duration<=10.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADVehicleDataTest, "Afterdark.Data.VehicleDefinition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FADVehicleDataTest::RunTest(const FString& Parameters)
{
    const double DesiredYaw=ADVehicleMath::DesiredYawRateRps(20.0,8.0,2.7);
    TestTrue(TEXT("Steered forward motion requests matching-sign yaw"),DesiredYaw>0.0);
    TestTrue(TEXT("Stability assist brakes toward the requested yaw rate"),
        ADVehicleMath::StabilityBrakeCorrectionN(DesiredYaw,0.0)>0.0);
    TestTrue(TEXT("Stability brake demand is bounded"),
        FMath::Abs(ADVehicleMath::StabilityBrakeCorrectionN(100.0,-100.0))<=3500.0);
    TestTrue(TEXT("Authored asphalt keeps the neutral tire coefficient"),
        FMath::IsNearlyEqual(ADVehicleMath::SurfaceGripScale(.82),1.0,1.e-6));
    TestTrue(TEXT("Lower-friction surfaces reduce tire grip"),
        ADVehicleMath::SurfaceGripScale(.52)<ADVehicleMath::SurfaceGripScale(.82));
    TestTrue(TEXT("Unexpected surface friction remains bounded"),
        ADVehicleMath::SurfaceGripScale(8.0)<=1.35);
    TestTrue(TEXT("Missing surface friction falls back to neutral grip"),
        FMath::IsNearlyEqual(ADVehicleMath::SurfaceGripScale(std::numeric_limits<double>::quiet_NaN()),1.0,1.e-6));
    const FADPhysicalSurfaceProfile Asphalt=ADSurfacePhysics::ResolveProfile(TEXT("M_Asphalt"));
    const FADPhysicalSurfaceProfile Concrete=ADSurfacePhysics::ResolveProfile(TEXT("M_Concrete"));
    const FADPhysicalSurfaceProfile Metal=ADSurfacePhysics::ResolveProfile(TEXT("M_Metal"));
    const FADPhysicalSurfaceProfile Water=ADSurfacePhysics::ResolveProfile(TEXT("M_Water"));
    TestTrue(TEXT("Asphalt profile matches the solver reference"),FMath::IsNearlyEqual(Asphalt.Friction,.82f));
    TestTrue(TEXT("Concrete, metal and water have distinct lower grip"),
        Asphalt.Friction>Concrete.Friction && Concrete.Friction>Metal.Friction && Metal.Friction>Water.Friction);
    TestTrue(TEXT("Surface collision profiles keep restitution low and bounded"),
        Asphalt.Restitution>=0.f && Asphalt.Restitution<=.1f && Water.Restitution<Asphalt.Restitution);
    FADVehicleDefinition Definition;
    FString Error;
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Vehicles/aster_s6.json");
    if (!TestTrue(TEXT("Vehicle source loads and validates"), Definition.LoadFromJson(Path, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Stable vehicle ID"), Definition.VehicleId, FString(TEXT("aster_s6")));
    TestEqual(TEXT("Four wheel contacts"), Definition.WheelAnchorsCm.Num(), 4);
    TestTrue(TEXT("Operating torque is positive"), Definition.GetTorqueNm(4000.f) > 100.f);
    for (const TCHAR* Id:{TEXT("ironwake_v8"),TEXT("vespera_r9")})
    {
        FADVehicleDefinition Additional;
        const FString AdditionalPath=FPaths::ProjectContentDir()/TEXT("Data/Vehicles")/(FString(Id)+TEXT(".json"));
        if (!TestTrue(FString::Printf(TEXT("%s car definition validates"),Id),Additional.LoadFromJson(AdditionalPath,Error)))
        { AddError(Error); return false; }
        TestTrue(FString::Printf(TEXT("%s has independent body dimensions"),Id),Additional.BodyLengthCm>456.f
            && Additional.BodyWidthCm>176.f && Additional.GearRatios.Num()>=6);
        TestTrue(FString::Printf(TEXT("%s has a distinct physical engine curve"),Id),Additional.GetTorqueNm(4000.f)>0.f
            && Additional.EngineCylinders==8);
    }

    const FString BadPath = FPaths::ProjectSavedDir() / TEXT("Automation/invalid-vehicle.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BadPath), true);
    TestTrue(TEXT("Can write malformed test fixture"), FFileHelper::SaveStringToFile(TEXT("{\"schemaVersion\": 999}"), *BadPath));
    FADVehicleDefinition Rejected;
    TestFalse(TEXT("Invalid definition is rejected"), Rejected.LoadFromJson(BadPath, Error));
    TestFalse(TEXT("Invalid definition reports a reason"), Error.IsEmpty());
    IFileManager::Get().Delete(*BadPath);
    return true;
}

#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Core/ADGameMode.h"
#include "Player/ADVehiclePawn.h"
#include "Player/ADPlayerController.h"
#include "Presentation/ADCinematicComponent.h"
#include "Racing/ADRaceManager.h"
#include "InputKeyEventArgs.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "Components/PrimitiveComponent.h"

namespace
{
    TMap<int32, float> AccelerationSamples;
}

/** Exercises real PIE collision/forces; it is not a substitute for handling or GPU review. */
class FADDriveSmokeCommand : public IAutomationLatentCommand
{
public:
    FADDriveSmokeCommand(FAutomationTestBase* InTest, int32 InRate)
        : Test(InTest), Deadline(FPlatformTime::Seconds() + 90.0), Rate(InRate) {}
    virtual ~FADDriveSmokeCommand() override
    {
        if (bChangedTimeStep)
        {
            FApp::SetFixedDeltaTime(PreviousStep);
            FApp::SetUseFixedTimeStep(bPreviousFixed);
        }
    }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline)
        {
            Test->AddError(TEXT("Timed out waiting for the driving smoke sequence."));
            return true;
        }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        if (Stage == 0)
        {
            for (TActorIterator<AADVehiclePawn> It(World); It; ++It) { Vehicle = *It; break; }
            AADGameMode* Mode = Cast<AADGameMode>(World->GetAuthGameMode());
            if (!Vehicle.IsValid() || !Mode || !Mode->IsWorldReady()) return false;
            PreviousStep = FApp::GetFixedDeltaTime();
            bPreviousFixed = FApp::UseFixedTimeStep();
            FApp::SetFixedDeltaTime(1.0 / Rate);
            FApp::SetUseFixedTimeStep(true);
            bChangedTimeStep = true;
            if (AController* Controller = Vehicle->GetController()) Controller->UnPossess();
            Vehicle->SetDrivingEnabled(true);
            Vehicle->GetPhysics()->SetControls(0.f, 0.f, 0.f, false);
            StageStart = World->GetTimeSeconds();
            Stage = 1;
        }
        if (!Vehicle.IsValid())
        {
            Test->AddError(TEXT("Vehicle disappeared during simulation."));
            return true;
        }
        UADVehiclePhysicsComponent* Physics = Vehicle->GetPhysics();
        const float Elapsed = World->GetTimeSeconds() - StageStart;
        const FADVehicleTelemetry& Telemetry = Physics->GetTelemetry();
        if (!FMath::IsFinite(Telemetry.SpeedKmh) || !FMath::IsFinite(Vehicle->GetActorLocation().Z))
        {
            Test->AddError(TEXT("Non-finite vehicle state."));
            return true;
        }
        if (Stage == 1 && Elapsed >= 2.f)
        {
            Test->TestEqual(TEXT("All wheels settle on the road"), Telemetry.GroundedWheels, 4);
            Test->TestTrue(TEXT("Chassis remains above road"), Vehicle->GetActorLocation().Z > 25.);
            Test->TestTrue(TEXT("Wheel contacts read the asphalt physical material"),
                Telemetry.SurfaceGripScale > .95f && Telemetry.SurfaceGripScale < 1.05f);
            StartPosition = Vehicle->GetActorLocation();
            Physics->SetControls(1.f, 0.f, 0.f, false);
            StageStart = World->GetTimeSeconds();
            Stage = 2;
        }
        else if (Stage == 2 && Elapsed >= 4.f)
        {
            SpeedBeforeBraking = Telemetry.SpeedKmh;
            Test->AddInfo(FString::Printf(TEXT("Smoke acceleration at %d Hz: %.2f km/h, %.2f m forward"),
                Rate, SpeedBeforeBraking, (Vehicle->GetActorLocation().X - StartPosition.X) * 0.01));
            AccelerationSamples.Add(Rate, SpeedBeforeBraking);
            for (const auto& Sample : AccelerationSamples)
            {
                if (Sample.Key != Rate && Sample.Value > 1.f)
                {
                    Test->TestTrue(FString::Printf(TEXT("Acceleration agrees within 5 percent with %d Hz"), Sample.Key),
                        FMath::Abs(Sample.Value - SpeedBeforeBraking) / Sample.Value <= .05f);
                }
            }
            Test->TestTrue(TEXT("Engine accelerates real chassis"), SpeedBeforeBraking > 10.f);
            Test->TestTrue(TEXT("Vehicle travels in forward direction"), Vehicle->GetActorLocation().X > StartPosition.X + 300.);
            Physics->SetControls(0.f, 1.f, 0.f, false);
            StageStart = World->GetTimeSeconds();
            Stage = 3;
        }
        else if (Stage == 3 && Elapsed >= 2.f)
        {
            Test->AddInfo(FString::Printf(TEXT("Smoke braking: %.2f km/h"), Telemetry.SpeedKmh));
            Test->TestTrue(TEXT("Service brake reduces speed"), Telemetry.SpeedKmh < SpeedBeforeBraking * .8f);
            Physics->SetControls(0.f, 0.f, 0.f, true);
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<AADVehiclePawn> Vehicle;
    double Deadline;
    float StageStart = 0.f;
    float SpeedBeforeBraking = 0.f;
    FVector StartPosition = FVector::ZeroVector;
    int32 Stage = 0;
    int32 Rate;
    double PreviousStep = 0.;
    bool bPreviousFixed = false;
    bool bChangedTimeStep = false;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FADDrivingSmokeTest, "Afterdark.Runtime.DriveAndBrake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FADDrivingSmokeTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
    for (const int32 Rate : {30, 60, 120})
    {
        OutNames.Add(FString::Printf(TEXT("%dHz"), Rate));
        OutCommands.Add(FString::FromInt(Rate));
    }
}

bool FADDrivingSmokeTest::RunTest(const FString& Parameters)
{
    if (!FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("Velocity/Maps/L_Dockside.umap")))
    {
        AddError(TEXT("Dockside map could not be opened. Run Bootstrap first."));
        return false;
    }
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FADDriveSmokeCommand(this, FCString::Atoi(*Parameters))));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

class FADInputSmokeCommand : public IAutomationLatentCommand
{
public:
    explicit FADInputSmokeCommand(FAutomationTestBase* InTest)
        : Test(InTest), Deadline(FPlatformTime::Seconds()+90.) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds()>Deadline)
        {
            Test->AddError(TEXT("Input integration timed out."));
            return true;
        }
        UWorld* World=GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        AADPlayerController* PC=Cast<AADPlayerController>(World->GetFirstPlayerController());
        AADVehiclePawn* Car=PC ? PC->GetVehiclePawn() : nullptr;
        const AADGameMode* Mode=Cast<AADGameMode>(World->GetAuthGameMode());
        if (!Car || !Mode || !Mode->IsWorldReady()) return false;
        const auto Key=[PC](FKey K,EInputEvent Event,float Amount=1.f)
        { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Event,Amount)); };
        // The first Enter starts a real-time story camera while physics driving
        // controls are suspended. Wall time keeps this case independent of pauses.
        const float Now=static_cast<float>(FPlatformTime::Seconds());
        if (Stage==0)
        {
            // Inspect the actual generated exterior. An inward-wound shell can
            // pass all physics tests while disappearing and blocking the cockpit.
            auto* Coachwork = Car->FindComponentByClass<UProceduralMeshComponent>();
            const FProcMeshSection* Shell = Coachwork ? Coachwork->GetProcMeshSection(0) : nullptr;
            if (Test->TestNotNull(TEXT("Car has generated coachwork"), Shell) &&
                Test->TestTrue(TEXT("Coachwork contains middle cross-section"), Shell->ProcVertexBuffer.Num() > 31))
            {
                bool bHasUpwardCoachwork = false;
                bool bHasDownwardCoachwork = false;
                bool bHasOutwardLeftCoachwork = false;
                for (const FProcMeshVertex& Vertex : Shell->ProcVertexBuffer)
                {
                    bHasUpwardCoachwork |= Vertex.Position.Z > 12.f && Vertex.Normal.Z > .1f;
                    bHasDownwardCoachwork |= Vertex.Position.Z < -5.f && Vertex.Normal.Z < -.1f;
                    bHasOutwardLeftCoachwork |= Vertex.Position.Y < -40.f && Vertex.Normal.Y < -.1f;
                }
                Test->TestTrue(TEXT("Coachwork top normals point upward"), bHasUpwardCoachwork);
                Test->TestTrue(TEXT("Coachwork underside normals point downward"), bHasDownwardCoachwork);
                Test->TestTrue(TEXT("Coachwork left normals point outward"), bHasOutwardLeftCoachwork);
            }
            Key(EKeys::Enter,IE_Pressed);
            StageStart=Now; Stage=1;
        }
        if (Now-StageStart<.4f) return false;
        const FADVehicleTelemetry& State=Car->GetPhysics()->GetTelemetry();
        switch(Stage)
        {
        case 1:
        {
            Test->TestTrue(TEXT("Enter starts the actual driving session"),PC->IsSessionStarted());
            auto* Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent());
            Test->TestTrue(TEXT("First entry presents the Dockside arrival cutscene"),
                PC->GetCinematic()->GetMode()==EADCinematicMode::Story && !PC->IsPaused()
                && !Car->IsDrivingEnabled() && Chassis && !Chassis->IsSimulatingPhysics());
            Key(EKeys::Enter,IE_Released,0);
            break;
        }
        case 2:
            Test->TestTrue(TEXT("Arrival remains active until a separate confirm input"),PC->GetCinematic()->IsActive());
            Key(EKeys::Enter,IE_Pressed);
            break;
        case 3:
            Test->TestFalse(TEXT("Skipping the arrival cutscene returns to live driving"),PC->IsPaused());
            Test->TestFalse(TEXT("Confirm closes the arrival story sequence"),PC->GetCinematic()->IsActive());
            Key(EKeys::W,IE_Pressed);
            break;
        case 4:
            Test->TestTrue(TEXT("W mapping feeds vehicle throttle"),State.Throttle>.9f);
            Key(EKeys::W,IE_Released,0);
            Key(EKeys::A,IE_Pressed);
            break;
        case 5:
            Test->TestTrue(TEXT("Release clears throttle"),State.Throttle<.01f);
            Test->TestTrue(TEXT("A mapping steers left"),State.Steering<-.1f);
            Key(EKeys::A,IE_Released,0);
            Key(EKeys::D,IE_Pressed);
            break;
        case 6:
            Test->TestTrue(TEXT("D mapping steers right"),State.Steering>.1f);
            Key(EKeys::D,IE_Released,0);
            Key(EKeys::C,IE_Pressed);
            break;
        case 7:
            Test->TestTrue(TEXT("C mapping selects hood camera"),Car->GetCameraMode()==EADCameraMode::Hood);
            Key(EKeys::C,IE_Released,0);
            Key(EKeys::Gamepad_RightTriggerAxis,IE_Axis,.5f);
            break;
        case 8:
            Test->TestTrue(TEXT("Gamepad trigger routing preserves analog throttle"),State.Throttle>.1f && State.Throttle<.9f);
            Key(EKeys::Gamepad_RightTriggerAxis,IE_Axis,0);
            Key(EKeys::W,IE_Pressed);
            break;
        case 9:
            Test->TestTrue(TEXT("Throttle can be reacquired after device switch"),State.Throttle>.9f);
            PC->FlushPressedKeys();
            break;
        case 10:
            if (ReverseTestSubstage==0)
            {
                Test->TestTrue(TEXT("Focus-loss flush clears vehicle throttle"),State.Throttle<.01f);
                Car->ResetVehicle();
                auto* Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent());
                if (!Test->TestNotNull(TEXT("Reverse integration has a physics chassis"),Chassis)) return true;
                Chassis->SetPhysicsLinearVelocity(Car->GetActorRightVector()*3000.f);
                Car->GetPhysics()->RequestReverse();
                Test->TestEqual(TEXT("Reverse is blocked while sliding sideways at 108 km/h"),State.Gear,1);
                Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
                ReverseTestSubstage=1;
                StageStart=Now;
                return false;
            }
            if (ReverseTestSubstage==1)
            {
                Test->TestEqual(TEXT("V selects reverse at standstill"),State.Gear,-1);
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
                Car->ResetVehicle();
                PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S,IE_Pressed,1.f));
                ReverseTestSubstage=2;
                StageStart=Now;
                return false;
            }
            Test->TestEqual(TEXT("Automatic brake at rest remains a service brake"),State.Gear,1);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S,IE_Released,0.f));
            break;
        case 11:
            if (auto* Chassis = Cast<UPrimitiveComponent>(Car->GetRootComponent()))
            {
                // Select reverse explicitly at rest before testing the
                // reverse-to-drive interlock during a lateral slide.
                Car->GetPhysics()->RequestReverse();
                Test->TestEqual(TEXT("Explicit reverse is selected before the lateral-slide interlock test"),
                    Car->GetPhysics()->GetTelemetry().Gear,-1);
                Chassis->SetPhysicsLinearVelocity(Car->GetActorRightVector()*3000.f);
                Car->GetPhysics()->ShiftUp();
                Test->TestEqual(TEXT("Reverse-to-drive is blocked during a sideways slide"),
                    Car->GetPhysics()->GetTelemetry().Gear,-1);
            }
            Car->ResetVehicle();
            if (auto* Chassis = Cast<UPrimitiveComponent>(Car->GetRootComponent()))
            {
                SavedDefinitionPath = Car->GetPhysics()->VehicleDefinitionFile;
                Car->GetPhysics()->VehicleDefinitionFile = TEXT("Data/Vehicles/automation_missing_vehicle.json");
                Test->AddExpectedError(TEXT("Driving disabled: Vehicle definition is missing"), EAutomationExpectedErrorFlags::Contains, 1);
                Test->TestFalse(TEXT("Missing data disables the vehicle model"), Car->GetPhysics()->Initialize(Chassis));
                Car->SetDrivingEnabled(true);
                Test->TestFalse(TEXT("Invalid vehicle cannot enter driving mode"), Car->IsDrivingEnabled());
                Test->TestFalse(TEXT("Invalid vehicle cannot fall through missing roads"), Chassis->IsSimulatingPhysics());
                FrozenPosition = Car->GetActorLocation();
            }
            break;
        case 12:
            Test->TestTrue(TEXT("Failed startup holds the chassis safely in place"),Car->GetActorLocation().Equals(FrozenPosition,.1));
            Car->GetPhysics()->VehicleDefinitionFile = SavedDefinitionPath;
            Car->ResetVehicle();
            Test->TestTrue(TEXT("Explicit recovery can reload corrected vehicle data"),Car->GetPhysics()->IsReady());
            Test->TestTrue(TEXT("Recovery restores active driving presentation and controls together"),Car->IsDrivingEnabled());
            Key(EKeys::W,IE_Pressed);
            break;
        case 13:
            Test->TestTrue(TEXT("Throttle can be reacquired after data recovery"),State.Throttle>.9f);
            Key(EKeys::W,IE_Released,0);
            Car->ResetVehicle();
            Key(EKeys::Tab,IE_Pressed);
            break;
        case 14:
            Test->TestEqual(TEXT("Tab selects next difficulty"),PC->GetSelectedDifficulty(),2);
            Key(EKeys::Tab,IE_Released,0);
            Key(EKeys::F,IE_Pressed);
            break;
        case 15:
            Test->TestNotNull(TEXT("Controller caches active race manager"),PC->GetRaceManager());
            if (!PC->GetRaceManager()) return true;
            Test->TestTrue(TEXT("F starts race countdown"),PC->GetRaceManager()->GetState()==EADRaceState::Countdown);
            Test->TestEqual(TEXT("Race uses selected difficulty"),PC->GetRaceManager()->GetDifficultyIndex(),2);
            Key(EKeys::F,IE_Released,0);
            Key(EKeys::W,IE_Pressed);
            break;
        case 16:
            Test->TestTrue(TEXT("Held throttle cannot bypass countdown"),State.Throttle<.01f && State.SpeedKmh<1.f);
            Key(EKeys::W,IE_Released,0);
            Key(EKeys::BackSpace,IE_Pressed);
            break;
        case 17:
            Test->TestTrue(TEXT("Backspace leaves race"),PC->GetRaceManager()->GetState()==EADRaceState::Idle);
            Test->TestEqual(TEXT("Leaving via input clears race entries"),PC->GetRaceManager()->GetRacers().Num(),0);
            Key(EKeys::BackSpace,IE_Released,0);
            Key(EKeys::Gamepad_FaceButton_Right,IE_Pressed);
            break;
        case 18:
            Test->TestTrue(TEXT("Gamepad B starts race countdown"),PC->GetRaceManager()->GetState()==EADRaceState::Countdown);
            Key(EKeys::Gamepad_FaceButton_Right,IE_Released,0);
            Key(EKeys::Gamepad_DPad_Left,IE_Pressed);
            break;
        case 19:
            Test->TestTrue(TEXT("D-pad left returns to free driving"),PC->GetRaceManager()->GetState()==EADRaceState::Idle);
            Key(EKeys::Gamepad_DPad_Left,IE_Released,0);
            Car->ResetVehicle();
            return true;
        default: return true;
        }
        ++Stage; StageStart=Now;
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Deadline;
    float StageStart=0.f;
    int32 Stage=0;
    int32 ReverseTestSubstage=0;
    FString SavedDefinitionPath;
    FVector FrozenPosition=FVector::ZeroVector;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADInputSmokeTest,"Afterdark.Runtime.InputAndCamera",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FADInputSmokeTest::RunTest(const FString& Parameters)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FADInputSmokeCommand(this)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
#endif
