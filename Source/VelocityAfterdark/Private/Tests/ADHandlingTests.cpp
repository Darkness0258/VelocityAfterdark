#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/BoxComponent.h"
#include "CoreGlobals.h"
#include "Core/ADGameMode.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Player/ADVehiclePawn.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

namespace ADHandlingTests
{
    enum class EScenario : uint8 { HighSpeed, Steering, Barrier, Curb };

    /**
     * These are bounded fixtures, not a claimed lap or natural acceleration run.
     * Each fixture settles the real chassis, then seeds velocity exactly once to
     * isolate the requested handling condition. Subsequent translation, rotation,
     * suspension, braking and collision response all come from the runtime model.
     */
    class FScenarioCommand final : public IAutomationLatentCommand
    {
    public:
        FScenarioCommand(FAutomationTestBase* InTest, EScenario InScenario, int32 InRate)
            : Test(InTest), Scenario(InScenario), Rate(InRate), Deadline(FPlatformTime::Seconds() + 120.) {}

        virtual ~FScenarioCommand() override { Cleanup(); }

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > Deadline)
                return Fail(TEXT("Handling fixture exceeded its 120-second wall-clock deadline."));

            UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
            if (!World || !World->HasBegunPlay())
                return Stage == 0 ? false : Fail(TEXT("PIE world disappeared during the handling fixture."));

            if (Stage == 0)
            {
                const AADGameMode* Mode = Cast<AADGameMode>(World->GetAuthGameMode());
                if (!Mode || !Mode->IsWorldReady()) return false;
                for (TActorIterator<AADVehiclePawn> It(World); It; ++It) { Vehicle = *It; break; }
                if (!Vehicle.IsValid()) return false;
                Chassis = Cast<UBoxComponent>(Vehicle->GetRootComponent());
                if (!Chassis.IsValid() || !Vehicle->GetPhysics()->IsReady())
                    return Fail(TEXT("Handling fixture requires the initialized Chaos box chassis."));

                PreviousStep = FApp::GetFixedDeltaTime();
                bPreviousFixed = FApp::UseFixedTimeStep();
                FApp::SetFixedDeltaTime(1. / Rate);
                FApp::SetUseFixedTimeStep(true);
                bChangedTimeStep = true;
                Controller = Vehicle->GetController();
                if (Controller.IsValid()) Controller->UnPossess();
                InitialPosition = Vehicle->GetActorLocation();
                Vehicle->SetDrivingEnabled(true);
                Vehicle->GetPhysics()->SetControls(0.f, 0.f, 0.f, false);
                SetStage(1, World);
            }

            if (!Vehicle.IsValid() || !Chassis.IsValid())
                return Fail(TEXT("Vehicle or chassis was destroyed during the handling fixture."));
            const FVector Position = Vehicle->GetActorLocation();
            const FVector Velocity = Chassis->GetPhysicsLinearVelocity();
            const FVector AngularVelocity = Chassis->GetPhysicsAngularVelocityInRadians();
            if (Position.ContainsNaN() || Velocity.ContainsNaN() || AngularVelocity.ContainsNaN()
                || Vehicle->GetActorQuat().ContainsNaN() || !Vehicle->GetPhysics()->IsReady())
                return Fail(TEXT("Vehicle produced a non-finite or unavailable physical state."));
            if (Velocity.Size() > 10000. || FMath::Abs(Position.X) > 42000.
                || FMath::Abs(Position.Y) > 3000. || Position.Z < 0. || Position.Z > 1500.)
                return Fail(TEXT("Vehicle left the bounded fixture volume or exceeded 360 km/h."));

            if (Stage >= 2 && Stage < 10)
            {
                MaximumX = FMath::Max(MaximumX, Position.X);
                MaximumLateralCm = FMath::Max(MaximumLateralCm, FMath::Abs(Position.Y - Origin.Y));
                PeakHeightCm = FMath::Max(PeakHeightCm, Position.Z);
                MinimumHeightCm = FMath::Min(MinimumHeightCm, Position.Z);
                MinimumUpDot = FMath::Min(MinimumUpDot, Vehicle->GetActorUpVector().Z);
                PeakAngularSpeed = FMath::Max(PeakAngularSpeed, AngularVelocity.Size());
                MinimumForwardSpeedCm = FMath::Min(MinimumForwardSpeedCm, Velocity.X);
                PeakSpeedKmh = FMath::Max(PeakSpeedKmh, Velocity.Size() * .036);
                if (Vehicle->GetPhysics()->GetTelemetry().GroundedWheels < 4) ++PartialContactFrames;
            }

            // Each PIE frame uses exactly 1 / Rate seconds. Counting completed
            // frames avoids float world-time rounding extending a 0.5 s input by
            // a whole extra frame at 30 Hz and skewing rate comparisons.
            const float Elapsed = static_cast<float>(double(GFrameCounter - StageStartFrame) / Rate);
            if (Stage == 1)
            {
                if (Elapsed < 2.f) return false;
                if (!Test->TestEqual(TEXT("All four suspension contacts settle before seeding"),
                    Vehicle->GetPhysics()->GetTelemetry().GroundedWheels, 4)) return Finish();
                if (!CreateFixture(World)) return Finish();
                SetStage(2, World);
                return false;
            }
            if (Stage >= 10) return UpdateRecovery(World, Elapsed);

            switch (Scenario)
            {
            case EScenario::HighSpeed: return UpdateHighSpeed(World, Elapsed);
            case EScenario::Steering: return UpdateSteering(World, Elapsed);
            case EScenario::Barrier: return UpdateBarrier(World, Elapsed);
            case EScenario::Curb: return UpdateCurb(World, Elapsed);
            default: return Fail(TEXT("Unknown handling fixture."));
            }
        }

    private:
        bool CreateFixture(UWorld* World)
        {
            const bool bFast = Scenario == EScenario::HighSpeed || Scenario == EScenario::Barrier;
            Origin = FVector(Scenario == EScenario::HighSpeed ? -25000. : -15000., 400.,
                Vehicle->GetActorLocation().Z);
            SeedSpeedCm = (bFast ? 200. : 60.) / .036;
            Vehicle->GetPhysics()->ResetState();
            Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            Vehicle->SetActorLocationAndRotation(Origin, FRotator::ZeroRotator, false, nullptr,
                ETeleportType::TeleportPhysics);
            Chassis->SetPhysicsLinearVelocity(FVector(SeedSpeedCm, 0., 0.));
            Chassis->WakeAllRigidBodies();
            Test->TestTrue(TEXT("Initialized speed fixture matches its requested velocity"),
                FMath::Abs(Chassis->GetPhysicsLinearVelocity().X - SeedSpeedCm) < 1.);
            MaximumX = Origin.X;
            PeakHeightCm = MinimumHeightCm = Origin.Z;
            MinimumForwardSpeedCm = SeedSpeedCm;
            PeakSpeedKmh = SeedSpeedCm * .036;

            if (Scenario == EScenario::Barrier || Scenario == EScenario::Curb)
            {
                ObstacleX = Origin.X + 2000.;
                const bool bBarrier = Scenario == EScenario::Barrier;
                Obstacle = World->SpawnActor<AActor>();
                if (!Obstacle.IsValid())
                {
                    Test->AddError(TEXT("Unable to spawn the static collision fixture."));
                    return false;
                }
                auto* Box = NewObject<UBoxComponent>(Obstacle.Get(), TEXT("HandlingCollisionFixture"));
                Obstacle->AddInstanceComponent(Box);
                Obstacle->SetRootComponent(Box);
                Box->SetMobility(EComponentMobility::Static);
                // A 10 cm wall probes CCD; the separate 15 cm-high, 1 m-long
                // curb exercises the production swept tire/suspension contacts.
                Box->SetBoxExtent(bBarrier ? FVector(5., 1000., 200.) : FVector(50., 1000., 7.5));
                Box->SetCollisionProfileName(TEXT("BlockAll"));
                Box->SetCollisionObjectType(ECC_WorldStatic);
                Box->SetWorldLocation(FVector(ObstacleX, Origin.Y, bBarrier ? 200. : 7.5));
                Box->RegisterComponent();
                Test->TestTrue(TEXT("Obstacle participates in query and rigid-body collision"),
                    Box->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
                if (bBarrier)
                    Test->TestTrue(TEXT("Production chassis enables continuous collision detection"),
                        Chassis->BodyInstance.bUseCCD);
            }
            Vehicle->GetPhysics()->SetControls(Scenario == EScenario::Curb ? .25f : 0.f,
                0.f, Scenario == EScenario::Steering ? .25f : 0.f, false);
            return true;
        }

        bool UpdateHighSpeed(UWorld* World, float Elapsed)
        {
            if (Stage == 2 && Elapsed >= 1.5f)
            {
                BrakeOrigin = Vehicle->GetActorLocation();
                BrakeStartSpeedKmh = Chassis->GetPhysicsLinearVelocity().Size() * .036;
                Test->TestTrue(TEXT("Seeded high-speed run travels forward under real simulation"),
                    BrakeOrigin.X - Origin.X > 5000.);
                Test->TestTrue(TEXT("Chassis remains in the high-speed regime before braking"),
                    BrakeStartSpeedKmh > 160. && BrakeStartSpeedKmh <= 205.);
                Vehicle->GetPhysics()->SetControls(0.f, 1.f, 0.f, false);
                SetStage(3, World);
            }
            else if (Stage == 3 && Elapsed >= 7.f)
            {
                const double BrakeDistanceM = (Vehicle->GetActorLocation().X - BrakeOrigin.X) * .01;
                const double EndSpeedKmh = Chassis->GetPhysicsLinearVelocity().Size() * .036;
                Test->AddInfo(FString::Printf(TEXT("HighSpeed %d Hz: seed=200 km/h, brake start=%.2f km/h, "
                    "end=%.2f km/h, braking distance=%.2f m, lateral=%.2f cm, Z=[%.2f,%.2f] cm, min up=%.4f"),
                    Rate, BrakeStartSpeedKmh, EndSpeedKmh, BrakeDistanceM, MaximumLateralCm,
                    MinimumHeightCm, PeakHeightCm, MinimumUpDot));
                Test->TestTrue(TEXT("Service brakes bring seeded high speed below 5 km/h within seven seconds"),
                    EndSpeedKmh < 5.);
                Test->TestTrue(TEXT("High-speed stopping distance stays within the available 250 m"),
                    BrakeDistanceM > 20. && BrakeDistanceM < 250.);
                Test->TestTrue(TEXT("Straight-line lateral drift stays under two metres"), MaximumLateralCm < 200.);
                Test->TestTrue(TEXT("Straight-line chassis remains upright"), MinimumUpDot > .9);
                Test->TestTrue(TEXT("Straight-line suspension remains above the road without launching"),
                    MinimumHeightCm > 24. && PeakHeightCm < Origin.Z + 80.);
                return Finish();
            }
            return false;
        }

        bool UpdateSteering(UWorld* World, float Elapsed)
        {
            if (Stage == 2 && Elapsed >= .5f)
            {
                Vehicle->GetPhysics()->SetControls(0.f, 1.f, 0.f, false);
                SetStage(3, World);
            }
            else if (Stage == 3 && Elapsed >= 3.f)
            {
                const double Yaw = Vehicle->GetActorRotation().Yaw;
                const double EndSpeedKmh = Chassis->GetPhysicsLinearVelocity().Size() * .036;
                Test->AddInfo(FString::Printf(TEXT("Steering %d Hz: seed=60 km/h, 0.25 right input for 0.5 s; "
                    "yaw=%.2f deg, lateral=%.2f cm, end=%.2f km/h, min up=%.4f, peak angular=%.3f rad/s"),
                    Rate, Yaw, MaximumLateralCm, EndSpeedKmh, MinimumUpDot, PeakAngularSpeed));
                Test->TestTrue(TEXT("Positive steering produces rightward displacement"),
                    Vehicle->GetActorLocation().Y - Origin.Y > 10.);
                Test->TestTrue(TEXT("Moderate steering changes heading without a spin"), Yaw > .5 && Yaw < 40.);
                Test->TestTrue(TEXT("Steering and braking stay inside the road"), MaximumLateralCm < 900.);
                Test->TestTrue(TEXT("Steering does not roll the chassis"), MinimumUpDot > .65);
                Test->TestTrue(TEXT("Brake remains effective after releasing steering"), EndSpeedKmh < 5.);
                BeginRecovery(World);
            }
            return false;
        }

        bool UpdateBarrier(UWorld* World, float Elapsed)
        {
            if (MaximumX > ObstacleX + 30.)
                return Fail(TEXT("CCD regression: chassis centre passed through the thin static barrier."));
            if (Elapsed < 2.f) return false;
            Test->AddInfo(FString::Printf(TEXT("Barrier %d Hz: seed=200 km/h, wall thickness=10 cm, "
                "closest centre=%.2f cm before wall, minimum forward velocity=%.2f km/h, "
                "peak Z=%.2f cm, peak speed=%.2f km/h"), Rate, ObstacleX-MaximumX,
                MinimumForwardSpeedCm*.036, PeakHeightCm, PeakSpeedKmh));
            Test->TestTrue(TEXT("Real chassis reaches the wall rather than stopping short"), MaximumX > ObstacleX - 500.);
            Test->TestTrue(TEXT("Thin wall stops the forward rigid-body motion"), MinimumForwardSpeedCm < 500.);
            Test->TestTrue(TEXT("Barrier response does not add excessive kinetic speed"), PeakSpeedKmh < 240.);
            BeginRecovery(World);
            return false;
        }

        bool UpdateCurb(UWorld* World, float Elapsed)
        {
            if (Elapsed < 3.5f) return false;
            Test->AddInfo(FString::Printf(TEXT("Curb %d Hz: seed=60 km/h, height=15 cm, length=100 cm; "
                "travel=%.2f m, peak rise=%.2f cm, Z min=%.2f cm, min up=%.4f, partial-contact frames=%d"),
                Rate, (Vehicle->GetActorLocation().X-Origin.X)*.01, PeakHeightCm-Origin.Z,
                MinimumHeightCm, MinimumUpDot, PartialContactFrames));
            Test->TestTrue(TEXT("Vehicle crosses the curb with both axles and continues"),
                Vehicle->GetActorLocation().X > ObstacleX + 1000.);
            Test->TestTrue(TEXT("Tire contacts transmit the raised surface into chassis motion"),
                PeakHeightCm > Origin.Z + .5);
            Test->TestTrue(TEXT("Curb contact avoids falling through the road or launching"),
                MinimumHeightCm > 20. && PeakHeightCm < Origin.Z + 200.);
            Test->TestTrue(TEXT("Curb crossing does not overturn the vehicle"), MinimumUpDot > .5);
            BeginRecovery(World);
            return false;
        }

        void BeginRecovery(UWorld* World)
        {
            DestroyObstacle();
            Vehicle->ResetVehicle();
            Test->TestTrue(TEXT("Recovery returns to the original start area"),
                FVector::Dist(Vehicle->GetActorLocation(), InitialPosition) < 150.);
            Test->TestTrue(TEXT("Recovery clears linear and angular momentum"),
                Chassis->GetPhysicsLinearVelocity().IsNearlyZero(.1)
                && Chassis->GetPhysicsAngularVelocityInRadians().IsNearlyZero(.01));
            Test->TestTrue(TEXT("Recovery preserves a ready, drivable vehicle"),
                Vehicle->IsDrivingEnabled() && Vehicle->GetPhysics()->IsReady());
            SetStage(10, World);
        }

        bool UpdateRecovery(UWorld* World, float Elapsed)
        {
            if (Stage == 10 && Elapsed >= 2.f)
            {
                Test->TestEqual(TEXT("Recovery settles all four wheels"),
                    Vehicle->GetPhysics()->GetTelemetry().GroundedWheels, 4);
                RecoveryOrigin = Vehicle->GetActorLocation();
                Vehicle->GetPhysics()->SetControls(.5f, 0.f, 0.f, false);
                SetStage(11, World);
            }
            else if (Stage == 11 && Elapsed >= 1.f)
            {
                const double SpeedKmh = Chassis->GetPhysicsLinearVelocity().Size() * .036;
                Test->AddInfo(FString::Printf(TEXT("Recovery %d Hz: %.2f km/h and %.2f m after 1 s at 0.5 throttle"),
                    Rate, SpeedKmh, (Vehicle->GetActorLocation().X - RecoveryOrigin.X) * .01));
                Test->TestTrue(TEXT("Recovered vehicle accepts propulsion again"),
                    SpeedKmh > 3. && Vehicle->GetActorLocation().X > RecoveryOrigin.X + 50.);
                return Finish();
            }
            return false;
        }

        void SetStage(int32 InStage, const UWorld*)
        {
            Stage = InStage;
            StageStartFrame = GFrameCounter;
        }

        void DestroyObstacle()
        {
            if (Obstacle.IsValid()) Obstacle->Destroy();
            Obstacle.Reset();
        }

        void Cleanup()
        {
            if (bCleaned) return;
            bCleaned = true;
            DestroyObstacle();
            if (Vehicle.IsValid())
            {
                Vehicle->ResetVehicle();
                Vehicle->SetDrivingEnabled(false);
                if (Controller.IsValid() && !Controller->GetPawn()) Controller->Possess(Vehicle.Get());
            }
            if (bChangedTimeStep)
            {
                FApp::SetFixedDeltaTime(PreviousStep);
                FApp::SetUseFixedTimeStep(bPreviousFixed);
                bChangedTimeStep = false;
            }
        }

        bool Finish() { Cleanup(); return true; }
        bool Fail(const TCHAR* Message) { Test->AddError(Message); return Finish(); }

        FAutomationTestBase* Test;
        EScenario Scenario;
        int32 Rate;
        double Deadline;
        TWeakObjectPtr<AADVehiclePawn> Vehicle;
        TWeakObjectPtr<UBoxComponent> Chassis;
        TWeakObjectPtr<AController> Controller;
        TWeakObjectPtr<AActor> Obstacle;
        int32 Stage = 0;
        uint64 StageStartFrame = 0;
        FVector InitialPosition = FVector::ZeroVector;
        FVector Origin = FVector::ZeroVector;
        FVector BrakeOrigin = FVector::ZeroVector;
        FVector RecoveryOrigin = FVector::ZeroVector;
        double SeedSpeedCm = 0.;
        double ObstacleX = 0.;
        double MaximumX = 0.;
        double MaximumLateralCm = 0.;
        double PeakHeightCm = 0.;
        double MinimumHeightCm = 0.;
        double MinimumUpDot = 1.;
        double PeakAngularSpeed = 0.;
        double MinimumForwardSpeedCm = 0.;
        double PeakSpeedKmh = 0.;
        double BrakeStartSpeedKmh = 0.;
        int32 PartialContactFrames = 0;
        double PreviousStep = 0.;
        bool bPreviousFixed = false;
        bool bChangedTimeStep = false;
        bool bCleaned = false;
    };
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FADHandlingRegressionTest, "Afterdark.Runtime.Handling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FADHandlingRegressionTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
    for (const TCHAR* Scenario : {TEXT("HighSpeed"), TEXT("Steering"), TEXT("Barrier"), TEXT("Curb")})
    {
        for (int32 Rate : {30, 60, 120})
        {
            OutNames.Add(FString::Printf(TEXT("%s.%dHz"), Scenario, Rate));
            OutCommands.Add(FString::Printf(TEXT("%s:%d"), Scenario, Rate));
        }
    }
}

bool FADHandlingRegressionTest::RunTest(const FString& Parameters)
{
    using namespace ADHandlingTests;
    FString ScenarioName, RateString;
    if (!Parameters.Split(TEXT(":"), &ScenarioName, &RateString))
    {
        AddError(TEXT("Expected handling fixture parameters Scenario:Rate."));
        return false;
    }
    const int32 Rate = FCString::Atoi(*RateString);
    if (Rate != 30 && Rate != 60 && Rate != 120)
    {
        AddError(TEXT("Handling fixtures require 30, 60 or 120 Hz."));
        return false;
    }
    EScenario Scenario;
    if (ScenarioName == TEXT("HighSpeed")) Scenario = EScenario::HighSpeed;
    else if (ScenarioName == TEXT("Steering")) Scenario = EScenario::Steering;
    else if (ScenarioName == TEXT("Barrier")) Scenario = EScenario::Barrier;
    else if (ScenarioName == TEXT("Curb")) Scenario = EScenario::Curb;
    else { AddError(TEXT("Unknown handling fixture name.")); return false; }

    if (!FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("Velocity/Maps/L_Dockside.umap")))
    {
        AddError(TEXT("Dockside map is missing. Run Bootstrap before testing handling."));
        return false;
    }
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FScenarioCommand(this, Scenario, Rate)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
