#include "Tests/ADRenderSmoke.h"

#if !UE_BUILD_SHIPPING
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
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
    Later(40.f, []()
    {
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_RENDER_SMOKE_COMPLETE"));
        if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkSmokeExit"))) FPlatformMisc::RequestExit(false);
    });
#endif
}
