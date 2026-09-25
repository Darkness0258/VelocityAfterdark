#include "Tests/ADGarageSmoke.h"

#include "Core/ADGameMode.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Garage/ADGarageSessionComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

void ADStartGarageSmoke(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    const bool bSave = FParse::Param(FCommandLine::Get(), TEXT("AfterdarkGarageSmoke"));
    const bool bVerify = FParse::Param(FCommandLine::Get(), TEXT("AfterdarkGarageVerify"));
    if (!World || (!bSave && !bVerify)) return;
    FString ProfilePath;
    if (bSave == bVerify || FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRenderSmoke"))
        || FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRaceSmoke"))
        || FParse::Param(FCommandLine::Get(), TEXT("AfterdarkDriveBenchmark"))
        || !FParse::Value(FCommandLine::Get(), TEXT("AfterdarkProfile="), ProfilePath)
        || ProfilePath.IsEmpty() || FPaths::IsRelative(ProfilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("AFTERDARK_GARAGE_SMOKE_FAILED: choose exactly one garage mode and supply an absolute isolated -AfterdarkProfile path."));
        if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkGarageSmokeExit"))) FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    if (AActor* Owner = World->GetAuthGameMode())
    {
        auto* Smoke = NewObject<UADGarageSmoke>(Owner, TEXT("GarageSmoke"));
        Owner->AddInstanceComponent(Smoke);
        Smoke->RegisterComponent();
        Smoke->Initialize(bVerify);
    }
#endif
}

UADGarageSmoke::UADGarageSmoke()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UADGarageSmoke::Initialize(bool bInVerify)
{
    bVerify = bInVerify;
    BootWallSeconds = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_GARAGE_SMOKE_START: %s isolated profile; rendered garage and real physical configuration."),
        bVerify ? TEXT("reload") : TEXT("save"));
}

bool UADGarageSmoke::Start()
{
    const auto* Mode = Cast<AADGameMode>(GetWorld()->GetAuthGameMode());
    Controller = Cast<AADPlayerController>(GetWorld()->GetFirstPlayerController());
    Player = Controller.IsValid() ? Controller->GetVehiclePawn() : nullptr;
    Garage = Controller.IsValid() ? Controller->GetGarageSession() : nullptr;
    Ownership = GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Mode || !Mode->IsWorldReady() || !Player.IsValid() || !Garage.IsValid() || !Ownership.IsValid()) return false;
    if (!Ownership->IsReady()) { Finish(false, Ownership->GetError()); return false; }
    if (!Player->GetPhysics()->IsReady()) return false;
    if (Ownership->GetPaints().Num() < 2 || Ownership->GetUpgrades().Num() != 4 || Ownership->GetTunes().Num() < 2)
    { Finish(false, TEXT("Garage catalog is incomplete.")); return false; }
    FString Error;
    if (bVerify)
    {
        // Validate before opening the garage: startup must restore the actual car,
        // not merely show saved selections in a menu.
        if (!ValidateCommitted(Error)) { Finish(false, Error); return false; }
    }
    else
    {
        const auto& Profile = Ownership->GetProfile();
        const FADGarageProfile Defaults;
        if (Profile.Credits != Defaults.Credits || !Profile.OwnedUpgrades.IsEmpty() || !Profile.EquippedUpgrades.IsEmpty()
            || Profile.PaintId != Defaults.PaintId || Profile.TuneId != Defaults.TuneId)
        { Finish(false, TEXT("Save smoke requires a fresh isolated profile; existing progress was not modified.")); return false; }
    }
    Controller->StartDriving();
    ReturnPosition = Player->GetActorLocation();
    if (!Garage->Enter()) { Finish(false, Garage->GetMessage()); return false; }
    Stage = 1;
    StageWallSeconds = FPlatformTime::Seconds();
    return true;
}

bool UADGarageSmoke::ValidateCommitted(FString& OutError) const
{
    if (!Ownership.IsValid() || !Player.IsValid()) { OutError = TEXT("Ownership or player disappeared."); return false; }
    const auto& Profile = Ownership->GetProfile();
    const auto& Paint = Ownership->GetPaints()[1];
    const auto& Upgrade = Ownership->GetUpgrades()[0];
    const auto& Tune = Ownership->GetTunes()[1];
    const FADGarageProfile Defaults;
    if (Profile.PaintId != Paint.Id || Profile.TuneId != Tune.Id || Profile.Credits != Defaults.Credits - Upgrade.Price
        || Profile.OwnedUpgrades.Num() != 1 || Profile.EquippedUpgrades.Num() != 1
        || !Profile.OwnedUpgrades.Contains(Upgrade.Id) || !Profile.EquippedUpgrades.Contains(Upgrade.Id))
    { OutError = TEXT("Saved palette, ECU, Sprint tune or charged credits did not match the committed transaction."); return false; }
    const auto Expected = Ownership->BuildDefinition(Profile);
    const auto& Actual = Player->GetPhysics()->GetDefinition();
    if (!FMath::IsNearlyEqual(Expected.GetTorqueNm(4000.f), Actual.GetTorqueNm(4000.f), .001f)
        || !FMath::IsNearlyEqual(Expected.FinalDrive, Actual.FinalDrive, .001f)
        || !FMath::IsNearlyEqual(Expected.MassKg, Actual.MassKg, .001f)
        || Actual.GetTorqueNm(4000.f) <= Ownership->GetStockDefinition().GetTorqueNm(4000.f))
    { OutError = TEXT("Ownership was not applied to the car's actual physical definition."); return false; }
    const auto* Body = Player->FindComponentByClass<UProceduralMeshComponent>();
    auto* Material = Body ? Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)) : nullptr;
    if (!Material || !Material->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(Paint.Color, .001f))
    { OutError = TEXT("Committed paint was not applied to the actual body material."); return false; }
    return true;
}

void UADGarageSmoke::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (bFinished) return;
    const double Now = FPlatformTime::Seconds();
    if (Now - BootWallSeconds > 100.) { Finish(false, TEXT("Garage smoke exceeded its 100-second deadline.")); return; }
    if (Stage == 0)
    {
        if (Now - BootWallSeconds < 10.) return;
        if (!Start() && !bFinished && Now - BootWallSeconds > 30.) Finish(false, TEXT("Garage or ownership was not ready after 30 seconds."));
        return;
    }
    if (!Player.IsValid() || !Garage.IsValid() || !Garage->IsActive() || !Ownership.IsValid())
    { Finish(false, TEXT("Garage session disappeared during capture.")); return; }
    FString Error;
    if (Stage == 1 && Now - StageWallSeconds >= 2.)
    {
        Capture(bVerify ? TEXT("AfterdarkGarageReload.png") : TEXT("AfterdarkGarageStock.png"));
        Stage = bVerify ? 5 : 2;
        StageWallSeconds = Now;
    }
    else if (Stage == 2 && Now - StageWallSeconds >= 2.)
    {
        Garage->MoveSelection(-Garage->GetSelectedRow());
        Garage->AdjustSelection(1);
        Garage->MoveSelection(1 - Garage->GetSelectedRow());
        Garage->AdjustSelection(1);
        Garage->MoveSelection(5 - Garage->GetSelectedRow());
        Garage->AdjustSelection(1);
        if (!Garage->CommitChanges()) { Finish(false, Garage->GetMessage()); return; }
        if (!ValidateCommitted(Error)) { Finish(false, Error); return; }
        Capture(TEXT("AfterdarkGarageCustom.png"));
        Stage = 3;
        StageWallSeconds = Now;
    }
    else if (Stage == 3 && Now - StageWallSeconds >= 4.)
    {
        // Screenshots, shader startup and scene entry precede this bounded capture.
        // The measured garage window contains no screenshot requests.
        Controller->ConsoleCommand(TEXT("CsvProfile START"), false);
        CaptureStartWall = Now;
        bCapturing = true;
        Stage = 4;
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_GARAGE_CAPTURE_STARTED"));
    }
    else if (Stage == 4)
    {
        ++CapturedFrames;
        if (Now - CaptureStartWall < 20.) return;
        StopCapture();
        const int64 Credits = Ownership->GetProfile().Credits;
        if (!Garage->CommitChanges() || Ownership->GetProfile().Credits != Credits)
        { Finish(false, TEXT("Duplicate commit changed the purchase balance.")); return; }
        if (!ValidateCommitted(Error)) { Finish(false, Error); return; }
        Garage->Leave();
        if (!Player->GetActorLocation().Equals(ReturnPosition, 3.) || !Player->IsDrivingEnabled())
        { Finish(false, TEXT("Garage return did not restore the original driving position/state.")); return; }
        Finish(true, TEXT("Rendered garage customized, physically applied, saved and returned; duplicate commit did not charge twice."));
    }
    else if (Stage == 5 && Now - StageWallSeconds >= 2.)
    {
        if (!ValidateCommitted(Error)) { Finish(false, Error); return; }
        Garage->Leave();
        if (!Player->IsDrivingEnabled() || !Player->GetActorLocation().Equals(ReturnPosition, 3.))
        { Finish(false, TEXT("Reloaded garage did not return to driving.")); return; }
        Finish(true, TEXT("A new process restored credits, ownership, equipment, tuning, body material and physical engine configuration."));
    }
}

FString UADGarageSmoke::GetOutputDirectory() const
{
    return FPaths::ProjectSavedDir() / TEXT("GarageSmoke") / (bVerify ? TEXT("Reload") : TEXT("Save"));
}

void UADGarageSmoke::Capture(const TCHAR* Name)
{
    const FString Directory = GetOutputDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);
    FScreenshotRequest::RequestScreenshot(Directory / Name, true, false);
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_GARAGE_CAPTURE_REQUEST: %s"), Name);
}

void UADGarageSmoke::StopCapture()
{
    if (!bCapturing) return;
    CaptureWallSeconds = FPlatformTime::Seconds() - CaptureStartWall;
    if (Controller.IsValid()) Controller->ConsoleCommand(TEXT("CsvProfile STOP"), false);
    bCapturing = false;
    UE_LOG(LogTemp, Display, TEXT("AFTERDARK_GARAGE_CAPTURE_STOPPED: %.3fs, %d frames"), CaptureWallSeconds, CapturedFrames);
}

void UADGarageSmoke::Finish(bool bSuccess, const FString& Reason)
{
    if (bFinished) return;
    bFinished = true;
    SetComponentTickEnabled(false);
    StopCapture();
    const auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetBoolField(TEXT("success"), bSuccess);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetStringField(TEXT("mode"), bVerify ? TEXT("reload") : TEXT("save"));
    Report->SetStringField(TEXT("scope"), TEXT("Isolated local ownership, actual rendered paint and physical configuration, garage return and process relaunch; not full production art or device acceptance."));
    Report->SetNumberField(TEXT("captureWallSeconds"), CaptureWallSeconds);
    Report->SetNumberField(TEXT("capturedFrames"), CapturedFrames);
    if (Ownership.IsValid())
    {
        const auto& Profile = Ownership->GetProfile();
        Report->SetNumberField(TEXT("credits"), static_cast<double>(Profile.Credits));
        Report->SetStringField(TEXT("paintId"), Profile.PaintId);
        Report->SetStringField(TEXT("tuneId"), Profile.TuneId);
        TArray<TSharedPtr<FJsonValue>> Owned;
        TArray<TSharedPtr<FJsonValue>> Equipped;
        for (const auto& Id : Profile.OwnedUpgrades) Owned.Add(MakeShared<FJsonValueString>(Id));
        for (const auto& Id : Profile.EquippedUpgrades) Equipped.Add(MakeShared<FJsonValueString>(Id));
        Report->SetArrayField(TEXT("ownedUpgrades"), Owned);
        Report->SetArrayField(TEXT("equippedUpgrades"), Equipped);
    }
    if (Player.IsValid())
    {
        const auto& Definition = Player->GetPhysics()->GetDefinition();
        Report->SetNumberField(TEXT("torque4000Nm"), Definition.GetTorqueNm(4000.f));
        Report->SetNumberField(TEXT("finalDrive"), Definition.FinalDrive);
        Report->SetNumberField(TEXT("massKg"), Definition.MassKg);
        Report->SetBoolField(TEXT("returnedToDriving"), Player->IsDrivingEnabled() && !Player->IsInGarage());
    }
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    const FString Directory = GetOutputDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("report.json")))) bSuccess = false;
    if (bSuccess) { UE_LOG(LogTemp, Display, TEXT("AFTERDARK_GARAGE_SMOKE_OK: %s"), *Reason); }
    else { UE_LOG(LogTemp, Error, TEXT("AFTERDARK_GARAGE_SMOKE_FAILED: %s"), *Reason); }
    if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkGarageSmokeExit")))
    {
        FTimerHandle ExitTimer;
        GetWorld()->GetTimerManager().SetTimer(ExitTimer, FTimerDelegate::CreateLambda([bSuccess]()
        { FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1); }), 3.f, false);
    }
}
