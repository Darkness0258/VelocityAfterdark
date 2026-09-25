#include "World/ADTrafficManager.h"
#include "World/ADAtmosphere.h"
#include "Core/ADGameMode.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Racing/ADRaceDriverComponent.h"
#include "Racing/ADRaceManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

AADTrafficManager::AADTrafficManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TrafficRoot"));
    Lamps = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SignalLamps"));
    Lamps->SetupAttachment(RootComponent);
    Lamps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Lamps->SetCastShadow(false);
}

bool AADTrafficManager::LoadSettings()
{
    FString Text;
    TSharedPtr<FJsonObject> Data;
    if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir()/TEXT("Data/World/traffic.json")))
        || Text.Len()>16384 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data) || !Data)
    { Error=TEXT("Traffic settings could not be read."); return false; }
    const auto Number=[&](const TCHAR* Key,double Minimum,double Maximum,double& Value)
    {
        const auto* Item=Data->Values.Find(Key);
        return Item && (*Item)->Type==EJson::Number && (*Item)->TryGetNumber(Value)
            && FMath::IsFinite(Value) && Value>=Minimum && Value<=Maximum;
    };
    double Schema=0,Count=0,Speed=0,Red=0,Green=0;
    if (!Number(TEXT("schemaVersion"),1,1,Schema) || !Number(TEXT("maxCars"),0,8,Count) || Count!=FMath::FloorToDouble(Count)
        || !Number(TEXT("speedScale"),.4,.75,Speed) || !Number(TEXT("redSeconds"),3,30,Red) || !Number(TEXT("greenSeconds"),5,60,Green))
    { Error=TEXT("Traffic settings are invalid."); return false; }
    MaxCars=static_cast<int32>(Count); SpeedScale=Speed; RedSeconds=Red; GreenSeconds=Green;
    if (!Route.LoadFromJson(FPaths::ProjectContentDir()/TEXT("Data/Races/dockside_circuit.json"),Error)) return false;
    double DistanceError=0;
    SignalsM={0.,Route.ClosestDistanceM(FVector(0,25000,0),DistanceError)};
    return true;
}

void AADTrafficManager::BeginPlay()
{
    Super::BeginPlay();
    bReady=LoadSettings();
    if (!bReady) return;
    Lamps->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"));
    if (Base)
    {
        SignalMaterial=UMaterialInstanceDynamic::Create(Base,this);
        Lamps->SetMaterial(0,SignalMaterial);
        for (const double Distance : SignalsM)
        {
            const FVector2D P=Route.PointAtDistance(Distance);
            Lamps->AddInstance(FTransform(FRotator::ZeroRotator,FVector(P.X,P.Y,580),FVector(.7,.7,.9)));
        }
    }
}

void AADTrafficManager::BindPlayer(AADVehiclePawn* Car,AADAtmosphere* Weather)
{
    if (Player.Get()!=Car) ClearTraffic();
    if (Atmosphere.Get()!=Weather)
    {
        for (AADVehiclePawn* Existing:Cars)
        {
            if (!IsValid(Existing)) continue;
            if (Atmosphere.IsValid()) Atmosphere->UnregisterVehicle(Existing);
            if (IsValid(Weather)) Weather->RegisterVehicle(Existing);
        }
    }
    Player=Car; Atmosphere=Weather;
    if (bReady && bEnabled) Populate();
}

void AADTrafficManager::Populate()
{
    if (!bReady || !bEnabled || !Player.IsValid() || !Player->IsDrivingEnabled() || Player->IsInGarage()
        || Cars.Num()>=MaxCars || !HasAuthority()) return;
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetRaceManager()
        && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle) return;
    // A small fixed candidate budget can refill a partial population without
    // moving any existing car or creating a new one beside the player.
    for (int32 Index=0;Index<MaxCars*3 && Cars.Num()<MaxCars;++Index)
    {
        const double Distance=(Index%MaxCars+.4+(Index/MaxCars)*.27)*Route.RouteLengthM/MaxCars;
        const FVector2D Point=Route.PointAtDistance(Distance);
        const FVector2D Direction=(Route.PointAtDistance(Distance+3)-Point).GetSafeNormal();
        FVector Location(Point.X-Direction.Y*300,Point.Y+Direction.X*300,90);
        if (FVector::DistSquared2D(Location,Player->GetActorLocation())<FMath::Square(15000.)) continue;
        bool bOccupied=false;
        for (const auto& Existing:Cars)
            if (IsValid(Existing.Get()) && FVector::DistSquared2D(Location,Existing->GetActorLocation())<FMath::Square(2000.))
            { bOccupied=true; break; }
        if (bOccupied) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ADTrafficSpawn),false,this);
        FHitResult Ground;
        if (!GetWorld()->LineTraceSingleByChannel(Ground,Location+FVector(0,0,400),Location-FVector(0,0,500),ECC_Visibility,Query)
            || Ground.ImpactNormal.Z<.8 || FMath::Abs(Ground.ImpactPoint.Z)>150.) continue;
        Location.Z=Ground.ImpactPoint.Z+90.;
        const FRotator Rotation=FVector(Direction.X,Direction.Y,0).Rotation();
        if (GetWorld()->OverlapBlockingTestByChannel(Location,Rotation.Quaternion(),ECC_Visibility,
            FCollisionShape::MakeBox(FVector(250.,125.,55.)),Query)) continue;
        FActorSpawnParameters Params;
        Params.Owner=this;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        auto* Car=GetWorld()->SpawnActor<AADVehiclePawn>(Location,Rotation,Params);
        if (!Car || !Car->GetPhysics()->IsReady()) { if (Car) Car->Destroy(); continue; }
        auto* Driver=NewObject<UADRaceDriverComponent>(Car);
        Car->AddInstanceComponent(Driver); Driver->RegisterComponent();
        Driver->AddTickPrerequisiteActor(this);
        if (!Driver->Initialize(Car,&Route,SpeedScale,300)) { Car->Destroy(); continue; }
        Car->SetRaceAppearance(FLinearColor(.14f+.08f*(Index%MaxCars),.16f+.025f*(Index%MaxCars),.19f),true);
        Car->SetDrivingEnabled(true);
        Driver->SetDriving(true);
        // A disabled or overturned traffic car never teleports in front of the player.
        Car->OnRecoveryRequested.AddWeakLambda(this,[this](AADVehiclePawn* Broken)
        {
            const int32 Slot=Cars.IndexOfByKey(Broken);
            RetireCar(Slot);
        });
        Cars.Add(Car); Drivers.Add(Driver); RetirementSeconds.Add(-1.f);
        if (Atmosphere.IsValid()) Atmosphere->RegisterVehicle(Car);
    }
    RefreshNeighbors();
}

void AADTrafficManager::RefreshNeighbors()
{
    TArray<AADVehiclePawn*> Neighbors;
    if (Player.IsValid()) Neighbors.Add(Player.Get());
    for (AADVehiclePawn* Car:Cars) if (IsValid(Car)) Neighbors.Add(Car);
    for (UADRaceDriverComponent* Driver:Drivers) if (IsValid(Driver)) Driver->SetCompetitors(Neighbors);
}

void AADTrafficManager::RetireCar(int32 Index)
{
    if (!Cars.IsValidIndex(Index) || !RetirementSeconds.IsValidIndex(Index) || RetirementSeconds[Index]>=0.f) return;
    RetirementSeconds[Index]=0.f;
    if (Drivers.IsValidIndex(Index) && IsValid(Drivers[Index].Get())) Drivers[Index]->SetDriving(false);
    AADVehiclePawn* Car=Cars[Index].Get();
    if (!IsValid(Car)) return;
    Car->SetDrivingEnabled(false);
    Car->GetPhysics()->SetComponentTickEnabled(false);
    if (auto* Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent()))
    {
        if (Chassis->IsSimulatingPhysics())
        {
            Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
        Chassis->SetSimulatePhysics(false);
    }
    Car->SetActorTickEnabled(false);
}

bool AADTrafficManager::RemoveRetiredCars(float DeltaSeconds)
{
    bool bRemoved=false;
    for (int32 Index=Cars.Num()-1;Index>=0;--Index)
    {
        AADVehiclePawn* Car=Cars[Index].Get();
        const bool bInvalid=!IsValid(Car);
        if (!bInvalid && RetirementSeconds[Index]<0.f) continue;
        if (!bInvalid)
        {
            RetirementSeconds[Index]+=DeltaSeconds;
            const FVector Position=Car->GetActorLocation();
            if (!Position.ContainsNaN() && (RetirementSeconds[Index]<5.f
                || FVector::DistSquared(Position,Player->GetActorLocation())<FMath::Square(15000.))) continue;
            Car->OnRecoveryRequested.RemoveAll(this);
            if (Atmosphere.IsValid()) Atmosphere->UnregisterVehicle(Car);
            Car->Destroy();
        }
        Cars.RemoveAt(Index); Drivers.RemoveAt(Index); RetirementSeconds.RemoveAt(Index);
        bRemoved=true;
    }
    return bRemoved;
}

bool AADTrafficManager::IsSignalRed() const { return SignalTime>=GreenSeconds; }

void AADTrafficManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bEnabled || !bReady || !HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f) return;
    if (!Player.IsValid()) { if (!Cars.IsEmpty()) ClearTraffic(); return; }
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetRaceManager()
        && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle) { SetEnabled(false); return; }
    if (RemoveRetiredCars(DeltaSeconds)) RefreshNeighbors();
    RetryTime-=DeltaSeconds;
    if (RetryTime<=0.f) { Populate(); RetryTime=3.f; }
    SignalTime=FMath::Fmod(SignalTime+DeltaSeconds,GreenSeconds+RedSeconds);
    const bool bRed=IsSignalRed();
    if (SignalMaterial && (bRed!=bPreviousRed || SignalTime<DeltaSeconds*2))
    {
        const FLinearColor Color=bRed ? FLinearColor(1.f,.015f,.01f) : FLinearColor(.015f,1.f,.1f);
        SignalMaterial->SetVectorParameterValue(TEXT("EmissiveColor"),Color*4);
        SignalMaterial->SetVectorParameterValue(TEXT("BaseColor"),Color);
    }
    bPreviousRed=bRed;
    for (int32 Index=0;Index<Cars.Num();++Index)
    {
        auto* Car=Cars[Index].Get(); auto* Driver=Drivers[Index].Get();
        if (RetirementSeconds[Index]>=0.f) continue;
        if (!IsValid(Car) || !IsValid(Driver)) { RetireCar(Index); continue; }
        if (Driver->NeedsRecovery()) { RetireCar(Index); continue; }
        double ErrorM=0;
        const FVector Position=Car->GetActorLocation();
        if (Position.ContainsNaN()) { RetireCar(Index); continue; }
        const double Progress=Route.ClosestDistanceM(Position,ErrorM);
        if (ErrorM>=35. || Car->GetActorUpVector().Z<=.3f) { RetireCar(Index); continue; }
        const double Speed=FMath::Abs(Car->GetPhysics()->GetTelemetry().SpeedKmh)/3.6;
        bool bStop=false;
        if (bRed)
        {
            for (const double Signal:SignalsM)
            {
                const double Gap=FMath::Fmod(Signal-Progress+Route.RouteLengthM,Route.RouteLengthM);
                if (Gap>0 && Gap<Speed*Speed/6.+8.) bStop=true;
            }
        }
        Driver->SetDriving(!bStop);
    }
}

void AADTrafficManager::ClearTraffic()
{
    for (AADVehiclePawn* Car:Cars)
        if (IsValid(Car))
        {
            Car->OnRecoveryRequested.RemoveAll(this);
            if (Atmosphere.IsValid()) Atmosphere->UnregisterVehicle(Car);
            Car->Destroy();
        }
    Cars.Reset(); Drivers.Reset(); RetirementSeconds.Reset(); RetryTime=0.f;
}

void AADTrafficManager::SetEnabled(bool bInEnabled)
{
    if (bEnabled==bInEnabled) return;
    bEnabled=bInEnabled;
    Lamps->SetVisibility(bEnabled);
    if (!bEnabled) ClearTraffic();
    else if (bReady) Populate();
}

void AADTrafficManager::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearTraffic();
    Super::EndPlay(Reason);
}
