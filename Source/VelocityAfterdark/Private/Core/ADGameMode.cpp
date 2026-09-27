#include "Core/ADGameMode.h"
#include "Core/ADGameState.h"
#include "World/ADDistrict.h"
#include "Player/ADVehiclePawn.h"
#include "Player/ADPlayerController.h"
#include "Presentation/ADHUD.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Tests/ADRenderSmoke.h"
#include "Tests/ADDriveBenchmark.h"
#include "Tests/ADRaceSmoke.h"
#include "Tests/ADGarageSmoke.h"
#include "Racing/ADRaceManager.h"
#include "World/ADAtmosphere.h"
#include "World/ADTrafficManager.h"
#include "World/ADPoliceDirector.h"
#include "World/ADRegionalWorld.h"
#include "World/ADExplorationDirector.h"
#include "GameFramework/GameSession.h"
#include "CollisionShape.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AADGameMode::AADGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AADVehiclePawn::StaticClass();
    PlayerControllerClass = AADPlayerController::StaticClass();
    HUDClass = AADHUD::StaticClass();
    GameStateClass=AADGameState::StaticClass();
}

void AADGameMode::PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage)
{
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
    if (ErrorMessage.IsEmpty() && GetNumPlayers()>=4) ErrorMessage=TEXT("This free-roam session is full (4 players).");
}

FString AADGameMode::InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal)
{
    // PreLogin can complete for several pending joins before any reaches Login.
    // Reserve at the serialized login boundary as well; release failed logins.
    for (auto It=PlayerSlots.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    if (!NewPlayerController || PlayerSlots.Num()>=4) return TEXT("This free-roam session is full (4 players).");
    int32 Slot=0;
    for (;Slot<4;++Slot)
    {
        bool bUsed=false;
        for (const auto& Entry:PlayerSlots) if (Entry.Value==Slot) { bUsed=true; break; }
        if (!bUsed) break;
    }
    PlayerSlots.Add(NewPlayerController,Slot);
    const FString Error=Super::InitNewPlayer(NewPlayerController,UniqueId,Options,Portal);
    if (!Error.IsEmpty()) PlayerSlots.Remove(NewPlayerController);
    return Error;
}

void AADGameMode::Logout(AController* Exiting)
{
    if (const auto* Car=PlayerCars.Find(Exiting); Car && Atmosphere) Atmosphere->UnregisterVehicle(Car->Get());
    PlayerCars.Remove(Exiting);
    PlayerSlots.Remove(Exiting);
    Super::Logout(Exiting);
}

void AADGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    District = GetWorld()->SpawnActor<AADDistrict>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    DrivingStart = GetWorld()->SpawnActor<APlayerStart>(FVector(0, 400, 90), FRotator::ZeroRotator, Params);
    for (int32 Slot=0;Slot<4;++Slot)
        OnlineStarts.Add(GetWorld()->SpawnActor<APlayerStart>(FVector(-1400.*Slot,Slot%2==0 ? 400. : -400.,90),FRotator::ZeroRotator,Params));
    if (GameSession) GameSession->MaxPlayers=4;
    RaceManager = GetWorld()->SpawnActor<AADRaceManager>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (!District || !DrivingStart)
    {
        ErrorMessage = TEXT("Afterdark failed to create its district or driving start.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
    }
}

void AADGameMode::StartPlay()
{
    Super::StartPlay();
    if (!IsWorldReady())
    {
        UE_LOG(LogTemp, Error, TEXT("Afterdark startup: %s"), *GetStartupError());
    }
    const bool bDeterministicQA = GIsAutomationTesting
        || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkRenderSmoke"))
        || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkDriveBenchmark"))
        || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkRaceSmoke"))
        || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkGarageSmoke"))
        || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkGarageVerify"));
    if (IsWorldReady() && (!bDeterministicQA || FParse::Param(FCommandLine::Get(),TEXT("AfterdarkEnvironment"))))
    {
        FString Error;
        if (!InitializeLivingWorld(Error)) UE_LOG(LogTemp,Error,TEXT("Living world startup: %s"),*Error);
    }
    if (RaceManager) RaceManager->OnStateChanged.AddUObject(this,&AADGameMode::RaceStateChanged);
    ADStartRenderSmoke(GetWorld());
    ADStartDriveBenchmark(GetWorld());
    ADStartRaceSmoke(GetWorld());
    ADStartGarageSmoke(GetWorld());
}

bool AADGameMode::InitializeLivingWorld(FString& OutError)
{
    OutError.Reset();
    if (!IsWorldReady()) { OutError=GetStartupError(); return false; }
    if (!Atmosphere) Atmosphere=GetWorld()->SpawnActor<AADAtmosphere>();
    if (!RegionalWorld) RegionalWorld=GetWorld()->SpawnActor<AADRegionalWorld>();
    if (RegionalWorld) RegionalWorld->BindAtmosphere(Atmosphere);
    if (GetNetMode()==NM_Standalone)
    {
        if (!Traffic) Traffic=GetWorld()->SpawnActor<AADTrafficManager>();
        if (!Police) Police=GetWorld()->SpawnActor<AADPoliceDirector>();
        if (!Exploration) Exploration=GetWorld()->SpawnActor<AADExplorationDirector>();
    }
    BoundPlayer.Reset(); // A deferred startup must bind an already possessed car.
    PlayerScanElapsed=1.f;
    if (!Atmosphere || !Atmosphere->IsReady()) OutError=Atmosphere ? Atmosphere->GetLoadError() : TEXT("Atmosphere spawn failed.");
    else if (!RegionalWorld || !RegionalWorld->IsReady()) OutError=RegionalWorld ? RegionalWorld->GetLoadError() : TEXT("Regional world spawn failed.");
    else if (GetNetMode()==NM_Standalone)
    {
        if (!Traffic || !Traffic->IsReady()) OutError=Traffic ? Traffic->GetError() : TEXT("Traffic spawn failed.");
        else if (!Police || !Police->IsReady()) OutError=Police ? Police->GetError() : TEXT("Police spawn failed.");
        else if (!Exploration || !Exploration->IsReady()) OutError=Exploration ? Exploration->GetError() : TEXT("Exploration spawn failed.");
    }
    return OutError.IsEmpty();
}

void AADGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (auto* Snapshot=GetGameState<AADGameState>(); Snapshot && Atmosphere && Atmosphere->IsReady())
    {
        Snapshot->Hour=Atmosphere->GetHour(); Snapshot->Wetness=Atmosphere->GetWetness();
        Snapshot->Weather=static_cast<uint8>(Atmosphere->GetWeather()); Snapshot->bEnvironmentReady=true;
    }
    auto* PC=GetWorld()->GetFirstPlayerController();
    PlayerScanElapsed+=DeltaSeconds;
    if (Atmosphere && PlayerScanElapsed>=.2f)
    {
        PlayerScanElapsed=0.f;
        for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            APlayerController* Controller=It->Get();
            if (!Controller) continue;
            AADVehiclePawn* Current=Cast<AADVehiclePawn>(Controller->GetPawn());
            auto& Previous=PlayerCars.FindOrAdd(Controller);
            if (Previous.Get()!=Current)
            {
                Atmosphere->UnregisterVehicle(Previous.Get());
                Atmosphere->RegisterVehicle(Current);
                Previous=Current;
            }
        }
    }
    auto* Car=PC ? Cast<AADVehiclePawn>(PC->GetPawn()) : nullptr;
    if (!Car || BoundPlayer.Get()==Car) return;
    BoundPlayer=Car;
    if (Atmosphere) Atmosphere->BindVehicle(Car);
    if (Traffic) Traffic->BindPlayer(Car,Atmosphere);
    if (Police) Police->RegisterPlayer(Car);
    if (RegionalWorld) RegionalWorld->BindVehicle(Car);
}

FVector2D AADGameMode::GetDriveBounds() const
{
    if (RegionalWorld && RegionalWorld->IsReady()) return RegionalWorld->GetGroundHalfExtent();
    return District ? District->GetGroundHalfExtent() : FVector2D(48000,34000);
}

FVector AADGameMode::GetDrivingStartLocation() const
{ return DrivingStart ? DrivingStart->GetActorLocation() : FVector::ZeroVector; }

void AADGameMode::RaceStateChanged(EADRaceState State)
{
    const bool bFreeDrive=State==EADRaceState::Idle;
    if (Traffic) Traffic->SetEnabled(bFreeDrive);
    if (Police) Police->SetEnabled(bFreeDrive);
    if (Atmosphere && RaceManager && State==EADRaceState::Countdown)
        for (const auto& Racer:RaceManager->GetRacers())
            if (Racer.Car.IsValid()) Atmosphere->RegisterVehicle(Racer.Car.Get());
}

AActor* AADGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    if (GetNetMode()!=NM_Standalone)
    {
        const int32* Slot=PlayerSlots.Find(Player);
        if (Slot && OnlineStarts.IsValidIndex(*Slot) && OnlineStarts[*Slot])
        {
            APlayerStart* Start=OnlineStarts[*Slot];
            // A returning slot may have another car parked on it. Find an empty
            // point on the existing avenue, without creating unbounded starts.
            for (int32 Attempt=0;Attempt<16;++Attempt)
            {
                const FVector Position(-1400.*(*Slot+4*Attempt),*Slot%2==0 ? 400. : -400.,90);
                if (FMath::Abs(Position.X)>38000.) break;
                if (!GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_PhysicsBody,FCollisionShape::MakeBox(FVector(270,115,35))))
                { Start->SetActorLocation(Position); return Start; }
            }
            return Start;
        }
    }
    return DrivingStart ? DrivingStart.Get() : Super::ChoosePlayerStart_Implementation(Player);
}

bool AADGameMode::IsWorldReady() const
{
    return IsValid(District) && District->IsReady();
}

FString AADGameMode::GetStartupError() const
{
    return IsValid(District) ? District->GetLoadError() : TEXT("District actor is unavailable.");
}
