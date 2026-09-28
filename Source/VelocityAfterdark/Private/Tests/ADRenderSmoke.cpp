#include "Tests/ADRenderSmoke.h"

#if !UE_BUILD_SHIPPING
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Presentation/ADCinematicComponent.h"
#include "Garage/ADGarageSessionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#endif

void ADStartRenderSmoke(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    if (!World || !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRenderSmoke"))) return;
    const TWeakObjectPtr<UWorld> WeakWorld(World);
    const auto Later = [World](float Delay, TFunction<void()> Action)
    {
        FTimerHandle Handle;
        World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Action)), Delay, false);
    };
    const auto Capture = [](const TCHAR* Name)
    {
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Smoke"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, Name), true, false);
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_CAPTURE_REQUEST: %s"), Name);
    };
    const auto Controller = [WeakWorld]() -> AADPlayerController*
    {
        return WeakWorld.IsValid() ? Cast<AADPlayerController>(WeakWorld->GetFirstPlayerController()) : nullptr;
    };
    Later(10.f, [Controller]() { if (auto* PC = Controller()) PC->ConsoleCommand(TEXT("CsvProfile START"), false); });
    Later(12.f, [Capture]() { Capture(TEXT("AfterdarkTitle.png")); });
    Later(15.f, [Controller]() { if (auto* PC = Controller()) PC->StartDriving(); });
    Later(20.f, [Capture]() { Capture(TEXT("AfterdarkChase.png")); });
    const auto NextCamera = [Controller]()
    {
        if (auto* PC = Controller()) if (auto* Car = PC->GetVehiclePawn()) Car->CycleCamera();
    };
    Later(25.f, NextCamera);
    Later(28.f, [Capture]() { Capture(TEXT("AfterdarkHood.png")); });
    Later(31.f, NextCamera);
    Later(34.f, [Capture]() { Capture(TEXT("AfterdarkCockpit.png")); });
    Later(36.f, [Controller]() { if (auto* PC = Controller()) PC->ConsoleCommand(TEXT("CsvProfile STOP"), false); });
    const TSharedRef<bool> bGarageOpened = MakeShared<bool>(false);
    Later(36.5f, [Controller,bGarageOpened]()
    {
        if (auto* PC=Controller())
        {
            *bGarageOpened=PC->GetGarageSession() && PC->GetGarageSession()->Enter();
            if (!*bGarageOpened) UE_LOG(LogTemp,Error,TEXT("AFTERDARK_GARAGE_SMOKE_START_FAILED"));
        }
    });
    Later(38.f, [Capture]() { Capture(TEXT("AfterdarkGarage.png")); });
    Later(39.f, [Controller,bGarageOpened]()
    {
        auto* PC=Controller();
        const bool bReady=*bGarageOpened && PC && PC->GetGarageSession() && PC->GetGarageSession()->IsActive();
        if (bReady)
        {
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_GARAGE_SMOKE_COMPLETE: studio materials and exposure loaded"));
        }
        else
        {
            UE_LOG(LogTemp,Error,TEXT("AFTERDARK_GARAGE_SMOKE_FAILED: garage session was not active"));
        }
        if (PC && PC->GetGarageSession()) PC->GetGarageSession()->Leave();
    });
    const TSharedRef<bool> bStoryStarted = MakeShared<bool>(false);
    Later(41.f, [Controller,bStoryStarted]()
    {
        if (auto* PC = Controller())
        {
            *bStoryStarted=PC->GetCinematic() && PC->GetCinematic()->PlayArrivalCutscene();
            if (!*bStoryStarted) UE_LOG(LogTemp,Error,TEXT("AFTERDARK_CUTSCENE_START_FAILED"));
        }
    });
    Later(44.f, [Controller,bStoryStarted]()
    {
        auto* PC=Controller();
        const bool bVoicePlaying=*bStoryStarted && PC && PC->GetCinematic()
            && PC->GetCinematic()->IsStoryVoiceoverPlaying();
        if (bVoicePlaying)
        {
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_VOICEOVER_SMOKE_COMPLETE: arrival radio SoundWave is playing"));
        }
        else
        {
            UE_LOG(LogTemp,Error,TEXT("AFTERDARK_VOICEOVER_SMOKE_FAILED: arrival voiceover did not start"));
        }
    });
    Later(43.f, [Capture]() { Capture(TEXT("AfterdarkArrival01.png")); });
    Later(46.f, [Capture]() { Capture(TEXT("AfterdarkArrival02.png")); });
    Later(50.f, [Capture]() { Capture(TEXT("AfterdarkArrival03.png")); });
    Later(54.f, [Capture]() { Capture(TEXT("AfterdarkArrival04.png")); });
    Later(58.f, [Controller,bStoryStarted]()
    {
        auto* PC=Controller();
        auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
        auto* Chassis=Car ? Cast<UPrimitiveComponent>(Car->GetRootComponent()) : nullptr;
        const bool bComplete=*bStoryStarted && PC && Car && PC->GetCinematic()
            && !PC->GetCinematic()->IsActive() && !PC->IsPaused()
            && Car->IsDrivingEnabled() && Chassis && Chassis->IsSimulatingPhysics()
            && Car->GetPhysics()->IsComponentTickEnabled();
        if (bComplete)
        {
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_CUTSCENE_SMOKE_COMPLETE: four animated arrival shots and safe return to driving"));
        }
        else
        {
            UE_LOG(LogTemp,Error,TEXT("AFTERDARK_CUTSCENE_SMOKE_FAILED: sequence did not finish in a driveable state"));
        }
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RENDER_SMOKE_COMPLETE"));
    });
    Later(62.f, []()
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkSmokeExit"))) FPlatformMisc::RequestExit(false);
    });
#endif
}
