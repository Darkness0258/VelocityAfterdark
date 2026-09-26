#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Core/ADGameMode.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceDriverComponent.h"
#include "Racing/ADRaceManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "World/ADAtmosphere.h"

namespace ADRaceTests
{
    /** Full races use ordinary throttle, brake and steering, including the QA
     * player. No transform or velocity is injected into the measured race. */
    class FRunRace final : public IAutomationLatentCommand
    {
    public:
        FRunRace(FAutomationTestBase* InTest, int32 InDifficulty, FString InRaceId=TEXT(""))
            : Test(InTest), Difficulty(InDifficulty), Deadline(FPlatformTime::Seconds() + 600.), RaceId(MoveTemp(InRaceId)) {}
        virtual ~FRunRace() override { Cleanup(); }

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > Deadline) return Fail(TEXT("Race exceeded its 600-second wall deadline."));
            UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
            if (!World || !World->HasBegunPlay())
                return Stage == 0 ? false : Fail(TEXT("PIE world disappeared during the race."));

            if (Stage == 0)
            {
                AADGameMode* Mode = Cast<AADGameMode>(World->GetAuthGameMode());
                if (!Mode || !Mode->IsWorldReady()) return false;
                Manager = Mode->GetRaceManager();
                Controller = Cast<AADPlayerController>(World->GetFirstPlayerController());
                Player = Controller.IsValid() ? Controller->GetVehiclePawn() : nullptr;
                if (!Manager.IsValid() || !Controller.IsValid() || !Player.IsValid()) return false;
                if (!Manager->IsReady()) return Fail(Manager->GetLoadError());
                if (!RaceId.IsEmpty())
                {
                    FString Error;
                    if (!Mode->InitializeLivingWorld(Error)) return Fail(Error);
                    // Keep the physical regional roads and normal streaming, with a
                    // stable dry surface so course acceptance is reproducible.
                    FADWorldSnapshot Dry;
                    Dry.bRecorded=true;
                    if (!Mode->GetAtmosphere()->RestoreWorldSnapshot(Dry)) return Fail(TEXT("Cannot set the route test weather."));
                    Mode->GetAtmosphere()->SetFrozen(true);
                }
                PreviousStep = FApp::GetFixedDeltaTime();
                bPreviousFixed = FApp::UseFixedTimeStep();
                FApp::SetFixedDeltaTime(1. / 30.);
                FApp::SetUseFixedTimeStep(true);
                bChangedTimeStep = true;
                Controller->StartDriving();
                if (!StartSelectedRace())
                    return Fail(TEXT("Race did not start: ") + Manager->GetLoadError());
                if (!ValidateGrid()) return Finish();
                Stage = 1;
            }
            if (!Manager.IsValid() || !Player.IsValid() || !Controller.IsValid())
                return Fail(TEXT("Race ownership was destroyed unexpectedly."));

            if (Stage == 1 || Stage == 4)
            {
                if (Manager->GetState() == EADRaceState::Countdown)
                {
                    if (Manager->GetElapsedSeconds() != 0.) return Fail(TEXT("Race clock advanced during countdown."));
                    for (int32 Index = 0; Index < Manager->GetRacers().Num(); ++Index)
                    {
                        const auto& Racer = Manager->GetRacers()[Index];
                        if (Racer.Progress.Started || Racer.Progress.CompletedLaps != 0)
                            return Fail(TEXT("Countdown incorrectly granted race progress."));
                        if (FVector::Dist(Racer.Car->GetActorLocation(), GridPositions[Index]) > 50.)
                            return Fail(TEXT("A racer moved off its grid slot before GO."));
                    }
                    ++CountdownFrames;
                    return false;
                }
                if (Manager->GetState() != EADRaceState::Racing) return Fail(TEXT("Countdown did not transition to Racing."));
                if (CountdownFrames < 30) return Fail(TEXT("Countdown was skipped or shorter than one second."));
                if (Stage == 1)
                {
                    PausedElapsed = Manager->GetElapsedSeconds();
                    Controller->TogglePause();
                    if (!Controller->IsGamePaused()) return Fail(TEXT("Pause input did not pause the session."));
                    PauseEndWall = FPlatformTime::Seconds() + .25;
                    Stage = 2;
                    return false;
                }
                if (!AttachPlayerDriver()) return Finish();
                FirstLapSeconds.Init(0., 4);
                PreviousPositions.Reset();
                LastFrame = GFrameCounter;
                Stage = 5;
            }

            if (Stage == 2)
            {
                if (Manager->GetElapsedSeconds() != PausedElapsed)
                    return Fail(TEXT("Authoritative elapsed time changed while paused."));
                if (FPlatformTime::Seconds() < PauseEndWall) return false;
                Controller->TogglePause();
                const auto Before = Manager->GetRacers()[0].Progress;
                const double PenaltyBefore = Manager->GetRacers()[0].PenaltySeconds;
                if (!Test->TestTrue(TEXT("Explicit recovery succeeds after GO"), Manager->RecoverRacer(0))) return Finish();
                const auto& Recovered = Manager->GetRacers()[0];
                Test->TestEqual(TEXT("Recovery grants no checkpoint"), Recovered.Progress.NextCheckpoint, Before.NextCheckpoint);
                Test->TestEqual(TEXT("Recovery grants no completed lap"), Recovered.Progress.CompletedLaps, Before.CompletedLaps);
                Test->TestTrue(TEXT("Recovery applies the configured time penalty"), FMath::IsNearlyEqual(
                    Recovered.PenaltySeconds - PenaltyBefore, Manager->GetDefinition().RecoveryPenaltySeconds, .0001));
                Test->TestEqual(TEXT("Recovery is counted"), Recovered.RecoveryCount, 1);
                if (!LeaveAndCheckCleanup()) return Finish();
                if (!StartSelectedRace()) return Fail(TEXT("Clean race restart failed after recovery fixture."));
                if (!ValidateGrid()) return Finish();
                Stage = 4;
                return false;
            }

            if (Stage == 5)
            {
                if (!CheckPhysicalRace()) return Finish();
                if (Manager->GetElapsedSeconds() > Manager->GetDefinition().TimeoutSeconds + 1.)
                    return Fail(TEXT("Race exceeded its configured simulation timeout."));
                if (!Manager->IsClassificationFinal()) return false;
                if (!ValidateResults()) return Finish();
                if (!LeaveAndCheckCleanup()) return Finish();
                if (!StartSelectedRace()) return Fail(TEXT("Restart from finished results failed."));
                if (!ValidateGrid()) return Finish();
                if (!LeaveAndCheckCleanup()) return Finish();
                return Finish();
            }
            return false;
        }

    private:
        bool StartSelectedRace()
        {
            return RaceId.IsEmpty() ? Manager->StartRace(Player.Get(),Difficulty)
                : Manager->StartRaceById(Player.Get(),RaceId,Difficulty);
        }

        bool ValidateGrid()
        {
            if (!Test->TestEqual(TEXT("Race contains player and three real opponents"), Manager->GetRacers().Num(), 4)) return false;
            if (!Test->TestTrue(TEXT("Start enters Countdown"), Manager->GetState() == EADRaceState::Countdown)) return false;
            GridPositions.Reset();
            TSet<AADVehiclePawn*> UniqueCars;
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const auto& Racer = Manager->GetRacers()[Index];
                if (!Racer.Car.IsValid() || !Racer.Car->GetPhysics()->IsReady())
                { Test->AddError(TEXT("Grid contains an unavailable physical vehicle.")); return false; }
                UniqueCars.Add(Racer.Car.Get());
                GridPositions.Add(Racer.Car->GetActorLocation());
                Test->TestEqual(TEXT("Restart clears completed laps"), Racer.Progress.CompletedLaps, 0);
                Test->TestEqual(TEXT("Restart clears recovery count"), Racer.RecoveryCount, 0);
                Test->TestTrue(TEXT("Restart clears penalties and finish flags"), Racer.PenaltySeconds == 0. && !Racer.Progress.Finished && !Racer.bDNF);
                if (Index > 0 && !Racer.Driver.IsValid())
                { Test->AddError(TEXT("Opponent lacks a physical race driver.")); return false; }
            }
            Test->TestEqual(TEXT("Each grid entry owns a distinct car"), UniqueCars.Num(), 4);
            Test->TestTrue(TEXT("Racer zero is the possessed player"), Manager->GetRacers()[0].Car.Get() == Player.Get());
            CountdownFrames = 0;
            return true;
        }

        bool AttachPlayerDriver()
        {
            PlayerDriver = NewObject<UADRaceDriverComponent>(Player.Get(), TEXT("AutomationRaceDriver"));
            Player->AddInstanceComponent(PlayerDriver.Get());
            PlayerDriver->RegisterComponent();
            PlayerDriver->AddTickPrerequisiteActor(Controller.Get());
            if (!PlayerDriver->Initialize(Player.Get(), &Manager->GetDefinition(), 1.f, 0.f))
            { Test->AddError(TEXT("Unable to initialize the QA player driver.")); return false; }
            TArray<AADVehiclePawn*> Cars;
            for (const auto& Racer : Manager->GetRacers()) Cars.Add(Racer.Car.Get());
            PlayerDriver->SetCompetitors(Cars);
            PlayerDriver->SetDriving(true);
            return true;
        }

        bool CheckPhysicalRace()
        {
            const auto& Racers = Manager->GetRacers();
            if (Racers.Num() != 4) { Test->AddError(TEXT("Racer count changed during competition.")); return false; }
            const double Dt = double(GFrameCounter - LastFrame) / 30.;
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const auto& Racer = Racers[Index];
                if (!Racer.Car.IsValid()) { Test->AddError(TEXT("Race car was destroyed before results.")); return false; }
                const FVector Position = Racer.Car->GetActorLocation();
                const FVector Velocity = Racer.Car->GetVelocity();
                if (Position.ContainsNaN() || Velocity.ContainsNaN() || Racer.Car->GetActorQuat().ContainsNaN()
                    || !Racer.Car->GetPhysics()->IsReady() || Position.Z < 0. || Position.Z > 1000.)
                { Test->AddError(FString::Printf(TEXT("Racer %d produced an invalid physical state."), Index)); return false; }
                if (Racer.RecoveryCount != 0 || Racer.bDNF)
                { Test->AddError(FString::Printf(TEXT("Racer %d required recovery or DNF; lap %d gate %d at %s."), Index,
                    Racer.Progress.CompletedLaps,Racer.Progress.NextCheckpoint,*Position.ToString())); return false; }
                if (Manager->GetElapsedSeconds()>15. && !Racer.Progress.Started)
                {
                    const auto& Telemetry=Racer.Car->GetPhysics()->GetTelemetry();
                    const UADRaceDriverComponent* Driver=Index==0 ? PlayerDriver.Get() : Racer.Driver.Get();
                    Test->AddError(FString::Printf(TEXT("Racer %d did not leave grid: %s, speed %.2f throttle %.2f brake %.2f, target %.1f, driver tick %d, needs reset %d."),
                        Index,*Position.ToString(),Telemetry.SpeedKmh,Telemetry.Throttle,Telemetry.Brake,Driver ? Driver->GetTargetSpeedKmh() : -1.f,
                        Driver && Driver->IsComponentTickEnabled(),Driver && Driver->NeedsRecovery()));
                    return false;
                }
                if (PreviousPositions.Num() == 4 && !Racer.Progress.Finished
                    && FVector::Dist(Position, PreviousPositions[Index]) > FMath::Max(300., Velocity.Size() * Dt + 150.))
                { Test->AddError(FString::Printf(TEXT("Racer %d jumped without physical continuity."), Index)); return false; }
                if (Racer.Progress.CompletedLaps == 1 && FirstLapSeconds[Index] == 0.)
                    FirstLapSeconds[Index] = Racer.Progress.LastLapSeconds;
            }
            PreviousPositions.Reset();
            for (const auto& Racer : Racers) PreviousPositions.Add(Racer.Car->GetActorLocation());
            LastFrame = GFrameCounter;
            if (Racers[0].Progress.Finished && PlayerDriver.IsValid()) PlayerDriver->SetDriving(false);
            return true;
        }

        bool ValidateResults()
        {
            Test->TestTrue(TEXT("Player finish displays Results"), Manager->GetState() == EADRaceState::Results);
            TArray<double> TimesByPlace;
            TimesByPlace.Init(-1., 4);
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const auto& Racer = Manager->GetRacers()[Index];
                const auto& Progress = Racer.Progress;
                Test->TestTrue(TEXT("Every racer legally starts and finishes"), Progress.Started && Progress.Finished && !Racer.bDNF);
                const bool bPointToPoint=Manager->GetDefinition().Laps==0;
                Test->TestEqual(TEXT("Every racer has correct circuit lap count"), Progress.CompletedLaps,
                    bPointToPoint ? 0 : Manager->GetDefinition().Laps);
                Test->TestTrue(TEXT("Finish time is finite and inside race elapsed time"), FMath::IsFinite(Progress.FinishSeconds)
                    && Progress.FinishSeconds > 0. && Progress.FinishSeconds <= Manager->GetElapsedSeconds());
                if (bPointToPoint)
                    Test->TestTrue(TEXT("Sprint does not fabricate lap timings"),
                        Progress.LastLapSeconds==0. && Progress.BestLapSeconds==0.);
                else
                    Test->TestTrue(TEXT("Last and best lap times are positive"), Progress.LastLapSeconds > 0.
                        && Progress.BestLapSeconds > 0. && Progress.BestLapSeconds <= Progress.LastLapSeconds);
                if (Manager->GetDefinition().Laps == 2)
                {
                    Test->TestTrue(TEXT("Two lap durations sum to the measured race duration"), FMath::IsNearlyEqual(
                        FirstLapSeconds[Index] + Progress.LastLapSeconds, Progress.FinishSeconds - Progress.StartSeconds, .001));
                    Test->TestTrue(TEXT("Best lap is the faster of both completed laps"), FMath::IsNearlyEqual(
                        Progress.BestLapSeconds, FMath::Min(FirstLapSeconds[Index], Progress.LastLapSeconds), .001));
                }
                if (Racer.Place < 1 || Racer.Place > 4 || TimesByPlace[Racer.Place - 1] >= 0.)
                { Test->AddError(TEXT("Classification contains duplicate or out-of-range places.")); return false; }
                TimesByPlace[Racer.Place - 1] = Progress.FinishSeconds + Racer.PenaltySeconds;
                Test->AddInfo(FString::Printf(TEXT("Difficulty %d: %s place %d, finish %.3fs, best %.3fs, recoveries %d"),
                    Difficulty, *Racer.Name, Racer.Place, Progress.FinishSeconds, Progress.BestLapSeconds, Racer.RecoveryCount));
            }
            for (int32 Index = 1; Index < 4; ++Index)
                Test->TestTrue(TEXT("Classification sorts adjusted finish times"), TimesByPlace[Index] >= TimesByPlace[Index - 1]);
            return true;
        }

        bool LeaveAndCheckCleanup()
        {
            if (PlayerDriver.IsValid()) { PlayerDriver->SetDriving(false); PlayerDriver->DestroyComponent(); PlayerDriver.Reset(); }
            TArray<TWeakObjectPtr<AADVehiclePawn>> Opponents;
            for (int32 Index = 1; Index < Manager->GetRacers().Num(); ++Index) Opponents.Add(Manager->GetRacers()[Index].Car);
            Manager->LeaveRace();
            Test->TestTrue(TEXT("Leave returns to Idle"), Manager->GetState() == EADRaceState::Idle);
            Test->TestEqual(TEXT("Leave clears race entries"), Manager->GetRacers().Num(), 0);
            for (const auto& Opponent : Opponents)
                Test->TestTrue(TEXT("Leave destroys each spawned opponent"), !Opponent.IsValid() || Opponent->IsActorBeingDestroyed());
            Test->TestTrue(TEXT("Leave preserves the player and free-roam input"), Player.IsValid()
                && Controller->GetVehiclePawn() == Player.Get() && Player->IsDrivingEnabled());
            return Player.IsValid();
        }

        void Cleanup()
        {
            if (bCleaned) return;
            bCleaned = true;
            if (Controller.IsValid() && Controller->IsGamePaused()) Controller->TogglePause();
            if (PlayerDriver.IsValid()) { PlayerDriver->SetDriving(false); PlayerDriver->DestroyComponent(); }
            if (Manager.IsValid()) Manager->LeaveRace();
            if (bChangedTimeStep) { FApp::SetFixedDeltaTime(PreviousStep); FApp::SetUseFixedTimeStep(bPreviousFixed); }
        }
        bool Fail(const FString& Reason)
        {
            Test->AddError(Reason);
            if (Manager.IsValid())
                for (const auto& Racer : Manager->GetRacers())
                    Test->AddInfo(FString::Printf(TEXT("Failure state: %s place %d, lap %d, gate %d, recovery %d, at %s; race %.3fs"),
                        *Racer.Name, Racer.Place, Racer.Progress.CompletedLaps, Racer.Progress.NextCheckpoint, Racer.RecoveryCount,
                        Racer.Car.IsValid() ? *Racer.Car->GetActorLocation().ToString() : TEXT("missing"), Manager->GetElapsedSeconds()));
            return Finish();
        }
        bool Finish() { Cleanup(); return true; }

        FAutomationTestBase* Test;
        int32 Difficulty;
        int32 Stage = 0;
        int32 CountdownFrames = 0;
        double Deadline;
        FString RaceId;
        double PauseEndWall = 0.;
        double PausedElapsed = 0.;
        double PreviousStep = 0.;
        bool bPreviousFixed = false;
        bool bChangedTimeStep = false;
        bool bCleaned = false;
        uint64 LastFrame = 0;
        TWeakObjectPtr<AADRaceManager> Manager;
        TWeakObjectPtr<AADPlayerController> Controller;
        TWeakObjectPtr<AADVehiclePawn> Player;
        TWeakObjectPtr<UADRaceDriverComponent> PlayerDriver;
        TArray<FVector> GridPositions;
        TArray<FVector> PreviousPositions;
        TArray<double> FirstLapSeconds;
    };
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FADPhysicalRaceTest, "Afterdark.Runtime.Race",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FADPhysicalRaceTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
    OutNames = {TEXT("Easy"), TEXT("Normal"), TEXT("Hard")};
    OutCommands = {TEXT("0"), TEXT("1"), TEXT("2")};
}

bool FADPhysicalRaceTest::RunTest(const FString& Parameters)
{
    if (Parameters != TEXT("0") && Parameters != TEXT("1") && Parameters != TEXT("2"))
    { AddError(TEXT("Expected difficulty index 0, 1 or 2.")); return false; }
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new ADRaceTests::FRunRace(this, FCString::Atoi(*Parameters))));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FADRegionalRaceTest,"Afterdark.Runtime.RegionalRace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FADRegionalRaceTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    Names={TEXT("Foundry"),TEXT("IronQuay"),TEXT("NightSurvey"),TEXT("Northfield"),TEXT("GlassCoast"),TEXT("Sable"),TEXT("AfterdarkFinal"),TEXT("AfterdarkSprint")};
    Commands={TEXT("foundry_shift_v1"),TEXT("iron_quay_v1"),TEXT("night_survey_v1"),TEXT("northfield_run_v1"),TEXT("glass_coast_v1"),TEXT("sable_ring_v1"),TEXT("afterdark_final_v1"),TEXT("afterdark_sprint_v1")};
}
bool FADRegionalRaceTest::RunTest(const FString& Parameters)
{
    TArray<FString> Names,Commands;
    GetTests(Names,Commands);
    if (!Commands.Contains(Parameters)) { AddError(TEXT("Unknown regional course.")); return false; }
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new ADRaceTests::FRunRace(this,1,Parameters)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
