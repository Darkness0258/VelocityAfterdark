#include "Tests/ADRaceSmoke.h"

#include "Core/ADGameMode.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceDriverComponent.h"
#include "Racing/ADRaceManager.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

void ADStartRaceSmoke(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    if (!World || !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRaceSmoke"))) return;
    if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRenderSmoke"))
        || FParse::Param(FCommandLine::Get(), TEXT("AfterdarkDriveBenchmark")))
    {
        UE_LOG(LogTemp, Error, TEXT("AFTERDARK_RACE_SMOKE_FAILED: smoke modes must run separately."));
        if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRaceSmokeExit"))) FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    if (AActor* Owner = World->GetAuthGameMode())
    {
        UADRaceSmoke* Smoke = NewObject<UADRaceSmoke>(Owner, TEXT("RaceSmoke"));
        Owner->AddInstanceComponent(Smoke);
        Smoke->RegisterComponent();
        Smoke->Initialize();
    }
#endif
}

UADRaceSmoke::UADRaceSmoke()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UADRaceSmoke::Initialize()
{
    BootWallSeconds = FPlatformTime::Seconds();
    NextProgressWall = BootWallSeconds + 15.;
    DistanceMeters.Init(0., 4);
    FirstLapSeconds.Init(0., 4);
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_SMOKE_START: physical four-car race; 120-second rendered CSV capture."));
}

bool UADRaceSmoke::Start()
{
    AADGameMode* Mode = Cast<AADGameMode>(GetWorld()->GetAuthGameMode());
    if (!Mode || !Mode->IsWorldReady()) return false;
    Manager = Mode->GetRaceManager();
    Controller = Cast<AADPlayerController>(GetWorld()->GetFirstPlayerController());
    Player = Controller.IsValid() ? Controller->GetVehiclePawn() : nullptr;
    if (!Manager.IsValid() || !Manager->IsReady() || !Player.IsValid() || !Player->GetPhysics()->IsReady()) return false;
    Controller->StartDriving();
    if (!Manager->StartRace(Player.Get(), 1)) { Finish(false, TEXT("Normal difficulty race failed to start.")); return false; }
    bStarted = true;
    if (Manager->GetRacers().Num() != 4) { Finish(false, TEXT("Race must instantiate four cars.")); return false; }
    PlayerDriver = NewObject<UADRaceDriverComponent>(Player.Get(), TEXT("SmokePlayerDriver"));
    Player->AddInstanceComponent(PlayerDriver.Get());
    PlayerDriver->RegisterComponent();
    PlayerDriver->AddTickPrerequisiteActor(Controller.Get());
    if (!PlayerDriver->Initialize(Player.Get(), &Manager->GetDefinition(), 1.f, 0.f))
    { Finish(false, TEXT("QA player driver could not initialize.")); return false; }
    TArray<AADVehiclePawn*> Cars;
    for (const auto& Racer : Manager->GetRacers()) Cars.Add(Racer.Car.Get());
    PlayerDriver->SetCompetitors(Cars);
    PlayerDriver->SetDriving(false);
    // StartRace teleports the grid after this frame's camera update. Let the
    // chase camera settle before capturing, within the countdown and warmup.
    FTimerHandle GridCaptureTimer;
    GetWorld()->GetTimerManager().SetTimer(GridCaptureTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
    { if (!bFinished) Capture(TEXT("AfterdarkRaceGrid.png")); }),.5f,false);
    return true;
}

void UADRaceSmoke::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (bFinished) return;
    const double Now = FPlatformTime::Seconds();
    if (Now - BootWallSeconds > 560.) { Finish(false, TEXT("Full race exceeded its 560-second wall deadline.")); return; }
    if (!bStarted)
    {
        if (Now - BootWallSeconds < 10.) return;
        if (!Start() && !bFinished && Now - BootWallSeconds > 30.) Finish(false, TEXT("Race ownership or data was not ready after 30 seconds."));
        return;
    }
    if (!Manager.IsValid() || !Player.IsValid() || !PlayerDriver.IsValid())
    { Finish(false, TEXT("Race ownership disappeared.")); return; }
    if (Manager->GetState() == EADRaceState::Countdown) return;
    if (!bDriving)
    {
        if (Manager->GetState() != EADRaceState::Racing) { Finish(false, TEXT("Countdown did not enter Racing.")); return; }
        bDriving = true;
        PlayerDriver->SetDriving(true);
    }
    FString Error;
    if (!ValidateFrame(DeltaTime, Error)) { Finish(false, Error); return; }
    if (Manager->GetRacers()[0].Progress.Finished) PlayerDriver->SetDriving(false);

    // Shader/asset startup and the grid screenshot precede the measured window.
    // No screenshot is requested while CSV is recording.
    if (!bCapturing && !bCaptureComplete && Manager->GetElapsedSeconds() >= 20.)
    {
        CaptureStartWall = Now;
        CaptureStartRace = Manager->GetElapsedSeconds();
        Controller->ConsoleCommand(TEXT("CsvProfile START"), false);
        bCapturing = true;
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_CAPTURE_STARTED"));
    }
    if (bCapturing)
    {
        ++CapturedFrames;
        if (Now - CaptureStartWall >= 120.) StopCapture();
    }
    if (Manager->IsClassificationFinal())
    {
        if (!bCaptureComplete) { Finish(false, TEXT("Race ended before a complete 120-second capture.")); return; }
        if (!ValidateClassification(Error)) { Finish(false, Error); return; }
        Capture(TEXT("AfterdarkRaceResults.png"));
        Finish(true, TEXT("All four physical racers completed both laps without recovery or DNF."));
        return;
    }
    if (Now >= NextProgressWall)
    {
        const auto& Racer = Manager->GetRacers()[0];
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_SMOKE_PROGRESS: race %.1fs; lap %d; gate %d; capture %.1fs; player %.0fm"),
            Manager->GetElapsedSeconds(), Racer.Progress.CompletedLaps, Racer.Progress.NextCheckpoint,
            bCapturing ? Now - CaptureStartWall : CaptureWallSeconds, DistanceMeters[0]);
        NextProgressWall = Now + 15.;
    }
}

bool UADRaceSmoke::ValidateFrame(float DeltaTime, FString& Error)
{
    const auto& Racers = Manager->GetRacers();
    if (Racers.Num() != 4) { Error = TEXT("Racer count changed during competition."); return false; }
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const auto& Racer = Racers[Index];
        if (!Racer.Car.IsValid()) { Error = TEXT("Race car was destroyed."); return false; }
        const FVector Position = Racer.Car->GetActorLocation();
        const FVector Velocity = Racer.Car->GetVelocity();
        if (Position.ContainsNaN() || Velocity.ContainsNaN() || Racer.Car->GetActorQuat().ContainsNaN()
            || !Racer.Car->GetPhysics()->IsReady() || Position.Z < 0. || Position.Z > 1000.)
        { Error = FString::Printf(TEXT("Racer %d has an invalid physical state."), Index); return false; }
        if (Racer.RecoveryCount != 0 || Racer.bDNF)
        { Error = FString::Printf(TEXT("Racer %d required recovery or DNF; clean completion failed."), Index); return false; }
        if (PreviousPositions.Num() == 4)
        {
            const double StepCm = FVector::Dist(Position, PreviousPositions[Index]);
            if (!Racer.Progress.Finished && StepCm > FMath::Max(300., Velocity.Size() * DeltaTime + 150.))
            { Error = FString::Printf(TEXT("Racer %d jumped without physical continuity."), Index); return false; }
            DistanceMeters[Index] += StepCm * .01;
        }
        if (Racer.Progress.CompletedLaps == 1 && FirstLapSeconds[Index] == 0.) FirstLapSeconds[Index] = Racer.Progress.LastLapSeconds;
    }
    PreviousPositions.Reset();
    for (const auto& Racer : Racers) PreviousPositions.Add(Racer.Car->GetActorLocation());
    return true;
}

bool UADRaceSmoke::ValidateClassification(FString& Error) const
{
    if (Manager->GetState() != EADRaceState::Results) { Error = TEXT("Player completion did not show Results."); return false; }
    TArray<double> TimesByPlace;
    TimesByPlace.Init(-1., 4);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const auto& Racer = Manager->GetRacers()[Index];
        const auto& Progress = Racer.Progress;
        if (!Progress.Started || !Progress.Finished || Progress.CompletedLaps != Manager->GetDefinition().Laps
            || Racer.bDNF || Racer.RecoveryCount != 0 || !FMath::IsFinite(Progress.FinishSeconds)
            || Progress.FinishSeconds <= 0. || Progress.BestLapSeconds <= 0. || Progress.LastLapSeconds <= 0.
            || Progress.BestLapSeconds > Progress.LastLapSeconds || Racer.Place < 1 || Racer.Place > 4)
        { Error = FString::Printf(TEXT("Racer %d has an invalid final result."), Index); return false; }
        if (Manager->GetDefinition().Laps == 2 && (!FMath::IsNearlyEqual(FirstLapSeconds[Index] + Progress.LastLapSeconds,
            Progress.FinishSeconds - Progress.StartSeconds, .001) || !FMath::IsNearlyEqual(Progress.BestLapSeconds,
            FMath::Min(FirstLapSeconds[Index], Progress.LastLapSeconds), .001)))
        { Error = FString::Printf(TEXT("Racer %d lap timing does not reconcile."), Index); return false; }
        if (TimesByPlace[Racer.Place - 1] >= 0.) { Error = TEXT("Classification has duplicate places."); return false; }
        TimesByPlace[Racer.Place - 1] = Progress.FinishSeconds + Racer.PenaltySeconds;
    }
    for (int32 Index = 1; Index < 4; ++Index)
        if (TimesByPlace[Index] < TimesByPlace[Index - 1]) { Error = TEXT("Classification is not ordered by adjusted finish time."); return false; }
    return true;
}

void UADRaceSmoke::Capture(const TCHAR* Name)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("RaceSmoke");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FScreenshotRequest::RequestScreenshot(Directory / Name, true, false);
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_CAPTURE_REQUEST: %s"), Name);
}

void UADRaceSmoke::StopCapture()
{
    if (!bCapturing) return;
    CaptureWallSeconds = FPlatformTime::Seconds() - CaptureStartWall;
    CaptureRaceSeconds = Manager.IsValid() ? Manager->GetElapsedSeconds() - CaptureStartRace : 0.;
    if (Controller.IsValid()) Controller->ConsoleCommand(TEXT("CsvProfile STOP"), false);
    bCapturing = false;
    bCaptureComplete = CaptureWallSeconds >= 120.;
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_CAPTURE_STOPPED: %.3fs wall, %.3fs simulation, %d frames"),
        CaptureWallSeconds, CaptureRaceSeconds, CapturedFrames);
}

void UADRaceSmoke::Finish(bool bSuccess, const FString& Reason)
{
    if (bFinished) return;
    bFinished = true;
    SetComponentTickEnabled(false);
    StopCapture();
    if (PlayerDriver.IsValid()) PlayerDriver->SetDriving(false);
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetBoolField(TEXT("success"), bSuccess);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetStringField(TEXT("scope"), TEXT("Rendered normal-difficulty race, three physical opponents, QA player using normal controls; not subjective racing quality acceptance."));
    Report->SetNumberField(TEXT("captureWallSeconds"), CaptureWallSeconds);
    Report->SetNumberField(TEXT("captureSimulationSeconds"), CaptureRaceSeconds);
    Report->SetNumberField(TEXT("capturedFrames"), CapturedFrames);
    Report->SetBoolField(TEXT("classificationFinal"), Manager.IsValid() && Manager->IsClassificationFinal());
    Report->SetNumberField(TEXT("elapsedRaceSeconds"), Manager.IsValid() ? Manager->GetElapsedSeconds() : 0.);
    Report->SetStringField(TEXT("raceId"), Manager.IsValid() ? Manager->GetDefinition().Id : TEXT("unavailable"));
    Report->SetNumberField(TEXT("state"), Manager.IsValid() ? static_cast<int32>(Manager->GetState()) : -1);
    TArray<TSharedPtr<FJsonValue>> Entries;
    if (Manager.IsValid())
    {
        for (int32 Index = 0; Index < Manager->GetRacers().Num(); ++Index)
        {
            const auto& Racer = Manager->GetRacers()[Index];
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Racer.Name);
            Entry->SetBoolField(TEXT("finished"), Racer.Progress.Finished);
            Entry->SetBoolField(TEXT("dnf"), Racer.bDNF);
            Entry->SetNumberField(TEXT("completedLaps"), Racer.Progress.CompletedLaps);
            Entry->SetNumberField(TEXT("nextCheckpoint"), Racer.Progress.NextCheckpoint);
            Entry->SetNumberField(TEXT("finishSeconds"), Racer.Progress.FinishSeconds);
            Entry->SetNumberField(TEXT("bestLapSeconds"), Racer.Progress.BestLapSeconds);
            Entry->SetNumberField(TEXT("lastLapSeconds"), Racer.Progress.LastLapSeconds);
            Entry->SetNumberField(TEXT("penaltySeconds"), Racer.PenaltySeconds);
            Entry->SetNumberField(TEXT("recoveries"), Racer.RecoveryCount);
            Entry->SetNumberField(TEXT("place"), Racer.Place);
            Entry->SetNumberField(TEXT("distanceMeters"), DistanceMeters.IsValidIndex(Index) ? DistanceMeters[Index] : 0.);
            const UADRaceDriverComponent* Driver = Index == 0 ? PlayerDriver.Get() : Racer.Driver.Get();
            if (Driver)
            {
                Entry->SetNumberField(TEXT("avoidanceCount"), Driver->GetAvoidanceCount());
                Entry->SetNumberField(TEXT("overtakeCount"), Driver->GetOvertakeCount());
                Entry->SetNumberField(TEXT("physicalRecoveryAttempts"), Driver->GetRecoveryAttemptCount());
                Entry->SetNumberField(TEXT("routeErrorMetersAtEnd"), Driver->GetRouteErrorM());
                Entry->SetBoolField(TEXT("needsRecovery"), Driver->NeedsRecovery());
            }
            if (Racer.Car.IsValid())
            {
                const FVector Position = Racer.Car->GetActorLocation();
                Entry->SetStringField(TEXT("finalPositionCm"), Position.ToString());
            }
            Entries.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }
    Report->SetArrayField(TEXT("racers"), Entries);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("RaceSmoke");
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("report.json")))) bSuccess = false;
    if (bSuccess) { UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RACE_SMOKE_OK: %s"), *Reason); }
    else { UE_LOG(LogTemp, Error, TEXT("AFTERDARK_RACE_SMOKE_FAILED: %s"), *Reason); }
    if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRaceSmokeExit")))
    {
        FTimerHandle ExitTimer;
        GetWorld()->GetTimerManager().SetTimer(ExitTimer, FTimerDelegate::CreateLambda([bSuccess]()
        { FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1); }), 4.f, false);
    }
}
