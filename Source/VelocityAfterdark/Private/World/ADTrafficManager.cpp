#include "World/ADTrafficManager.h"
#include "World/ADAtmosphere.h"
#include "World/ADPoliceDirector.h"
#include "Core/ADGameMode.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Racing/ADRaceDriverComponent.h"
#include "Racing/ADRaceManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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
    const auto MakeSignalBatch = [this](const TCHAR* Name)
    {
        auto* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Component->SetupAttachment(RootComponent);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
        return Component;
    };
    SignalPoles = MakeSignalBatch(TEXT("SignalPoles"));
    SignalHousings = MakeSignalBatch(TEXT("SignalHousings"));
    RedLenses = MakeSignalBatch(TEXT("RedSignalLenses"));
    AmberLenses = MakeSignalBatch(TEXT("AmberSignalLenses"));
    GreenLenses = MakeSignalBatch(TEXT("GreenSignalLenses"));
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
    double Schema=0,Count=0,Speed=0,Red=0,Amber=0,Green=0;
    if (!Number(TEXT("schemaVersion"),1,1,Schema) || !Number(TEXT("maxCars"),0,8,Count) || Count!=FMath::FloorToDouble(Count)
        || !Number(TEXT("speedScale"),.4,.75,Speed) || !Number(TEXT("redSeconds"),3,30,Red)
        || !Number(TEXT("amberSeconds"),1,8,Amber) || !Number(TEXT("greenSeconds"),5,60,Green))
    { Error=TEXT("Traffic settings are invalid."); return false; }
    MaxCars=static_cast<int32>(Count); SpeedScale=Speed; RedSeconds=Red; AmberSeconds=Amber; GreenSeconds=Green;
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
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* Metal=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_Metal.M_Metal"));
    UMaterialInterface* White=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"));
    SignalOffMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_Glass.M_Glass"));
    if (!Cube || !Cylinder || !Sphere || !Metal || !White || !SignalOffMaterial)
    {
        UE_LOG(LogTemp,Warning,TEXT("Traffic is active but its detailed signal-light materials or meshes are unavailable."));
        return;
    }
    SignalPoles->SetStaticMesh(Cylinder);
    SignalPoles->SetMaterial(0,Metal);
    SignalHousings->SetStaticMesh(Cube);
    SignalHousings->SetMaterial(0,Metal);
    for (const auto& Lens: {RedLenses.Get(),AmberLenses.Get(),GreenLenses.Get()}) Lens->SetStaticMesh(Sphere);

    const auto MakeLensMaterial=[this,White](const TCHAR* Name,const FLinearColor& Tint)
    {
        auto* Material=UMaterialInstanceDynamic::Create(White,this,FName(Name));
        if (Material)
        {
            Material->SetVectorParameterValue(TEXT("BaseColor"),Tint);
            Material->SetVectorParameterValue(TEXT("EmissiveColor"),Tint*2.2f);
            Material->SetScalarParameterValue(TEXT("Roughness"),.28f);
        }
        return Material;
    };
    const FLinearColor RedTint(1.f,.045f,.025f),AmberTint(1.f,.40f,.045f),GreenTint(.12f,.88f,.25f);
    RedLensMaterial=MakeLensMaterial(TEXT("RedLensGlow"),RedTint);
    AmberLensMaterial=MakeLensMaterial(TEXT("AmberLensGlow"),AmberTint);
    GreenLensMaterial=MakeLensMaterial(TEXT("GreenLensGlow"),GreenTint);
    if (!RedLensMaterial || !AmberLensMaterial || !GreenLensMaterial)
    {
        UE_LOG(LogTemp,Warning,TEXT("Traffic is active but signal lens material creation failed."));
        return;
    }
    RedLenses->SetMaterial(0,SignalOffMaterial);
    AmberLenses->SetMaterial(0,SignalOffMaterial);
    GreenLenses->SetMaterial(0,SignalOffMaterial);
    for (const double Distance : SignalsM)
    {
        const FVector2D P=Route.PointAtDistance(Distance);
        const FVector2D RoadDirection=(Route.PointAtDistance(Distance+10.)-Route.PointAtDistance(Distance-10.)).GetSafeNormal();
        const FVector2D RoadRight(RoadDirection.Y,-RoadDirection.X);
        const FVector2D Post=P+RoadRight*1370.-RoadDirection*350.;
        const FRotator Facing(0.f,FMath::RadiansToDegrees(FMath::Atan2(RoadDirection.Y,RoadDirection.X))+180.f,0.f);
        const FVector Ground(Post,0.f);
        SignalPoles->AddInstance(FTransform(FRotator::ZeroRotator,Ground+FVector(0.f,0.f,230.f),FVector(.11f,.11f,4.6f)));
        const FVector Head(Post,455.f);
        SignalHousings->AddInstance(FTransform(Facing,Head,FVector(.26f,.44f,1.f)));
        const FVector Offsets[] = {FVector(18.f,0.f,28.f),FVector(18.f,0.f,0.f),FVector(18.f,0.f,-28.f)};
        const FVector LensScale(.15f,.15f,.15f);
        RedLenses->AddInstance(FTransform(Facing,Head+Facing.RotateVector(Offsets[0]),LensScale));
        AmberLenses->AddInstance(FTransform(Facing,Head+Facing.RotateVector(Offsets[1]),LensScale));
        GreenLenses->AddInstance(FTransform(Facing,Head+Facing.RotateVector(Offsets[2]),LensScale));
    }
    UpdateSignalVisuals(EADTrafficSignalPhase::Green);
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

EADTrafficSignalPhase AADTrafficManager::GetSignalPhaseAtTime(float TimeSeconds,float Green,float Amber,float Red)
{
    if (!FMath::IsFinite(TimeSeconds) || !FMath::IsFinite(Green) || !FMath::IsFinite(Amber) || !FMath::IsFinite(Red)
        || Green<=0.f || Amber<=0.f || Red<=0.f) return EADTrafficSignalPhase::Red;
    const float Cycle=Green+Amber+Red;
    const float Position=FMath::Fmod(FMath::Max(0.f,TimeSeconds),Cycle);
    if (Position<Green) return EADTrafficSignalPhase::Green;
    if (Position<Green+Amber) return EADTrafficSignalPhase::Amber;
    return EADTrafficSignalPhase::Red;
}

bool AADTrafficManager::IsSignalRed() const
{
    // Traffic AI stops on amber as well, using the same conservative approach as a yellow-light rule.
    return SignalPhase!=EADTrafficSignalPhase::Green;
}

void AADTrafficManager::UpdateSignalVisuals(EADTrafficSignalPhase NewPhase)
{
    SignalPhase=NewPhase;
    if (!RedLenses || !AmberLenses || !GreenLenses) return;
    RedLenses->SetMaterial(0,NewPhase==EADTrafficSignalPhase::Red ? RedLensMaterial.Get() : SignalOffMaterial.Get());
    AmberLenses->SetMaterial(0,NewPhase==EADTrafficSignalPhase::Amber ? AmberLensMaterial.Get() : SignalOffMaterial.Get());
    GreenLenses->SetMaterial(0,NewPhase==EADTrafficSignalPhase::Green ? GreenLensMaterial.Get() : SignalOffMaterial.Get());
}

int32 AADTrafficManager::GetEmergencyYieldCount() const
{
    int32 Count=0;
    for (const UADRaceDriverComponent* Driver:Drivers)
        if (IsValid(Driver) && Driver->IsEmergencyYielding()) ++Count;
    return Count;
}

void AADTrafficManager::GetTrafficLocations(TArray<FVector2D>& OutLocations) const
{
    OutLocations.Reset(Cars.Num());
    for (const AADVehiclePawn* Car:Cars)
        if (IsValid(Car)) OutLocations.Add(FVector2D(Car->GetActorLocation()));
}

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
    SignalTime=FMath::Fmod(SignalTime+DeltaSeconds,GreenSeconds+AmberSeconds+RedSeconds);
    const EADTrafficSignalPhase NextPhase=GetSignalPhaseAtTime(SignalTime,GreenSeconds,AmberSeconds,RedSeconds);
    if (NextPhase!=SignalPhase) UpdateSignalVisuals(NextPhase);
    const AADPoliceDirector* Police=nullptr;
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>()) Police=Mode->GetPoliceDirector();
    const bool bPursuit=Police && Police->GetState()==EADPoliceState::Pursuit && Police->GetHeat()>0;
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
        const bool bEmergencyYield=bPursuit
            && FVector::DistSquared2D(Position,Player->GetActorLocation())<FMath::Square(3500.);
        Driver->SetEmergencyYield(bEmergencyYield,Index%2==0 ? 285.f : -285.f);
        bool bStop=false;
        if (IsSignalRed())
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
    for (UInstancedStaticMeshComponent* Component:{SignalPoles.Get(),SignalHousings.Get(),RedLenses.Get(),AmberLenses.Get(),GreenLenses.Get()})
        if (Component) Component->SetVisibility(bEnabled);
    if (!bEnabled) ClearTraffic();
    else if (bReady) Populate();
}

void AADTrafficManager::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearTraffic();
    Super::EndPlay(Reason);
}
