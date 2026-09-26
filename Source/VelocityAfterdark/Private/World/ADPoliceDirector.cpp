#include "World/ADPoliceDirector.h"

#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/ADVehiclePawn.h"
#include "Racing/ADRaceDriverComponent.h"
#include "Racing/ADRaceManager.h"
#include "Serialization/JsonSerializer.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Core/ADGameMode.h"
#include "World/ADAtmosphere.h"

DEFINE_LOG_CATEGORY_STATIC(LogADPolice, Log, All);

namespace
{
bool PoliceNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out,
    double Minimum, double Maximum, FString& Error)
{
    const auto Value = Object->TryGetField(Key);
    if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Out) || !FMath::IsFinite(Out)
        || Out < Minimum || Out > Maximum)
    { Error = FString::Printf(TEXT("Police '%s' requires a number in [%g, %g]."), Key, Minimum, Maximum); return false; }
    return true;
}

bool PoliceArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TArray<double>& Out,
    double Minimum, double Maximum, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(Key, Values) || !Values || Values->Num() != 5)
    { Error = FString::Printf(TEXT("Police '%s' requires five heat-level values."), Key); return false; }
    for (const auto& Value : *Values)
    {
        double Number = 0.;
        if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number)
            || Number < Minimum || Number > Maximum || (!Out.IsEmpty() && Number <= Out.Last()))
        { Error = FString::Printf(TEXT("Police '%s' values must be finite, bounded and strictly increasing."), Key); return false; }
        Out.Add(Number);
    }
    return true;
}
}

AADPoliceDirector::AADPoliceDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Units.Reserve(5);
}

void AADPoliceDirector::BeginPlay()
{
    Super::BeginPlay();
    bReady = HasAuthority() && LoadSettings(Error);
    if (!bReady) UE_LOG(LogADPolice, Error, TEXT("Police disabled: %s"), *Error);
}

bool AADPoliceDirector::LoadSettings(FString& OutError)
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/World/police.json");
    const int64 Size = IFileManager::Get().FileSize(*Path);
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (Size < 1 || Size > 32768 || !FFileHelper::LoadFileToString(Json, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
    { OutError = TEXT("Police settings are missing, oversized or invalid JSON."); return false; }
    FPoliceSettings Candidate;
    double Schema = 0.;
    double MaxUnits = 0.;
    FString RoutePath;
    if (!PoliceNumber(Object, TEXT("schemaVersion"), Schema, 1., 1., OutError)
        || !PoliceNumber(Object, TEXT("patrolSpeedScale"), Candidate.PatrolSpeedScale, .4, .8, OutError)
        || !PoliceArray(Object, TEXT("pursuitSpeedScales"), Candidate.PursuitSpeedScales, .4, 1.3, OutError)
        || !PoliceArray(Object, TEXT("heatThresholdSeconds"), Candidate.HeatThresholdSeconds, 0., 3600., OutError)
        || !PoliceNumber(Object, TEXT("detectionDistanceM"), Candidate.DetectionDistanceM, 20., 500., OutError)
        || !PoliceNumber(Object, TEXT("speedingKmh"), Candidate.SpeedingKmh, 30., 250., OutError)
        || !PoliceNumber(Object, TEXT("detectionGraceSeconds"), Candidate.DetectionGraceSeconds, 1., 15., OutError)
        || !PoliceNumber(Object, TEXT("lostSightGraceSeconds"), Candidate.LostSightGraceSeconds, 1., 15., OutError)
        || !PoliceNumber(Object, TEXT("searchSeconds"), Candidate.SearchSeconds, 5., 120., OutError)
        || !PoliceNumber(Object, TEXT("cooldownSeconds"), Candidate.CooldownSeconds, 5., 120., OutError)
        || !PoliceNumber(Object, TEXT("bustSeconds"), Candidate.BustSeconds, 3., 30., OutError)
        || !PoliceNumber(Object, TEXT("bustDistanceM"), Candidate.BustDistanceM, 5., 15., OutError)
        || !PoliceNumber(Object, TEXT("bustMaxSpeedKmh"), Candidate.BustMaxSpeedKmh, 1., 10., OutError)
        || !PoliceNumber(Object, TEXT("spawnMinDistanceM"), Candidate.SpawnMinDistanceM, 100., 300., OutError)
        || !PoliceNumber(Object, TEXT("spawnMaxDistanceM"), Candidate.SpawnMaxDistanceM, 100., 500., OutError)
        || !PoliceNumber(Object, TEXT("senseIntervalSeconds"), Candidate.SenseIntervalSeconds, .1, .5, OutError)
        || !PoliceNumber(Object, TEXT("maxUnits"), MaxUnits, 2., 5., OutError)) return false;
    if (Candidate.HeatThresholdSeconds[0] != 0. || Candidate.SpawnMaxDistanceM <= Candidate.SpawnMinDistanceM
        || MaxUnits != FMath::FloorToDouble(MaxUnits) || !Object->TryGetStringField(TEXT("routePath"), RoutePath)
        || !RoutePath.StartsWith(TEXT("Data/Races/")) || !RoutePath.EndsWith(TEXT(".json")) || RoutePath.Contains(TEXT(".."))
        || RoutePath.Contains(TEXT("\\")) || RoutePath.Contains(TEXT(":")))
    { OutError = TEXT("Police spawn distances, initial heat, unit count or content-relative route path are invalid."); return false; }
    Candidate.MaxUnits = static_cast<int32>(MaxUnits);
    FADRaceDefinition CandidateRoute;
    if (!CandidateRoute.LoadFromJson(FPaths::ProjectContentDir() / RoutePath, OutError)) return false;
    Settings = MoveTemp(Candidate);
    Route = MoveTemp(CandidateRoute);
    OutError.Reset();
    return true;
}

void AADPoliceDirector::RegisterPlayer(AADVehiclePawn* InPlayer)
{
    if (Player.Get() == InPlayer) return;
    ClearUnits();
    Player = InPlayer;
    Heat = 0;
    PursuitUnitsDispatched = 0;
    State = EADPoliceState::Patrol;
    StateSeconds = PursuitSeconds = MissingSightSeconds = ViolationSeconds = BustProgressSeconds = 0.;
    SpawnRetrySeconds = 0.;
    RoadblockRetrySeconds=0.;
    bRoadblockDispatched=false;
    PITCount=0;
    SenseCountdown = 0.;
}

void AADPoliceDirector::SetEnabled(bool bInEnabled)
{
    if (bEnabled == bInEnabled) return;
    bEnabled = bInEnabled;
    if (!bEnabled)
    {
        ClearUnits();
        Heat = 0;
        PursuitUnitsDispatched = 0;
        bRoadblockDispatched=false;
        State = EADPoliceState::Patrol;
        StateSeconds = PursuitSeconds = MissingSightSeconds = ViolationSeconds = BustProgressSeconds = 0.;
    }
    SpawnRetrySeconds = 0.;
    SenseCountdown = 0.;
}

bool AADPoliceDirector::StartPursuit()
{
    if (!bReady || !bEnabled || !HasAuthority() || !Player.IsValid() || Player->IsInGarage()
        || !Player->IsDrivingEnabled() || State != EADPoliceState::Patrol) return false;
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetRaceManager()
        && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle) return false;
    while (GetUnitCount() < 2) if (!SpawnUnit()) return false;
    PursuitUnitsDispatched = GetUnitCount();
    bRoadblockDispatched=false;
    PITCount=0;
    Heat = 1;
    PursuitSeconds = MissingSightSeconds = BustProgressSeconds = 0.;
    LastSeenPosition = Player->GetActorLocation();
    LastSeenVelocity = Player->GetVelocity();
    EnterState(EADPoliceState::Pursuit);
    UE_LOG(LogADPolice, Display, TEXT("Pursuit started with %d physical units."), GetUnitCount());
    return true;
}

void AADPoliceDirector::EnterState(EADPoliceState NewState)
{
    State = NewState;
    StateSeconds = 0.;
    ViolationSeconds = 0.;
    if (State == EADPoliceState::Cooldown || State == EADPoliceState::Busted || State == EADPoliceState::Patrol) Heat = 0;
    if (State==EADPoliceState::Busted && Player.IsValid())
    { Player->GetPhysics()->SetControls(0,1,0,true); Player->GetEffects()->SetNitrousHeld(false); }
    RefreshDrivers();
    UE_LOG(LogADPolice, Display, TEXT("Police state: %s"), *GetStateLabel());
}

double AADPoliceDirector::SpeedScale() const
{
    return IsActive() && Settings.PursuitSpeedScales.IsValidIndex(Heat - 1)
        ? Settings.PursuitSpeedScales[Heat - 1] : Settings.PatrolSpeedScale;
}

void AADPoliceDirector::RefreshDrivers()
{
    TArray<AADVehiclePawn*> Competitors;
    Competitors.Reserve(4);
    if (Player.IsValid()) Competitors.Add(Player.Get());
    for (const auto& Unit : Units) if (Unit.Car.IsValid()) Competitors.Add(Unit.Car.Get());
    SightQuery = FCollisionQueryParams(SCENE_QUERY_STAT(ADPoliceSight), false, Player.Get());
    for (auto& Unit : Units)
    {
        if (!Unit.Car.IsValid()) continue;
        SightQuery.AddIgnoredActor(Unit.Car.Get());
        if (Unit.bRetired || !Unit.Driver.IsValid()) continue;
        Unit.bDirectControl = false;
        if (Unit.Driver->Initialize(Unit.Car.Get(), &Route, static_cast<float>(SpeedScale()), 300.f))
        {
            Unit.Driver->SetCompetitors(Competitors);
        Unit.Driver->SetDriving(!Unit.bRoadblock && State != EADPoliceState::Busted);
        }
        else RetireUnit(Unit);
    }
    SenseCountdown = 0.;
}

bool AADPoliceDirector::SpawnUnit(bool bRoadblock, int32 RoadblockSlot)
{
    if (!Player.IsValid() || Units.Num() >= Settings.MaxUnits || !GetWorld()) return false;
    double RouteError = 0.;
    const double PlayerProgress = Route.ClosestDistanceM(Player->GetActorLocation(), RouteError);
    const FVector PlayerPosition = Player->GetActorLocation();
    for (int32 Attempt = 0; Attempt < 12; ++Attempt)
    {
        // Place new units on the known drivable route at least 100 m away.
        // Existing units are never repositioned to keep up with the player.
        const double Distance = FMath::Lerp(Settings.SpawnMinDistanceM, Settings.SpawnMaxDistanceM,
            static_cast<double>(Attempt) / 11.);
        const double Progress = bRoadblock
            ? PlayerProgress + 105. + RoadblockSlot * 7.
            : PlayerProgress - Distance - Units.Num() * 35.;
        const FVector2D Tangent = (Route.PointAtDistance(Progress + 1.) - Route.PointAtDistance(Progress - 1.)).GetSafeNormal();
        const double Lateral = bRoadblock ? (RoadblockSlot == 0 ? -520. : 520.) : 300.;
        const FVector2D Point = Route.PointAtDistance(Progress) + FVector2D(-Tangent.Y, Tangent.X) * Lateral;
        FVector Position(Point.X, Point.Y, 90.);
        if (FVector::Dist2D(Position, PlayerPosition) < Settings.SpawnMinDistanceM * 100.) continue;
        bool bNearUnit = false;
        for (const auto& Unit : Units)
            if (Unit.Car.IsValid() && FVector::Dist2D(Position, Unit.Car->GetActorLocation()) < 1500.) { bNearUnit = true; break; }
        if (bNearUnit) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ADPoliceSpawn), false, this);
        FHitResult Ground;
        if (!GetWorld()->LineTraceSingleByChannel(Ground, Position + FVector(0., 0., 400.), Position - FVector(0., 0., 500.), ECC_Visibility, Query)
            || Ground.ImpactNormal.Z < .8 || FMath::Abs(Ground.ImpactPoint.Z) > 150.) continue;
        Position.Z = Ground.ImpactPoint.Z + 90.;
        FRotator Facing=FVector(Tangent.X,Tangent.Y,0.).Rotation();
        if (bRoadblock) Facing.Yaw+=RoadblockSlot==0 ? 38.f : -38.f;
        const FQuat Rotation = Facing.Quaternion();
        if (GetWorld()->OverlapBlockingTestByChannel(Position, Rotation, ECC_Visibility,
            FCollisionShape::MakeBox(FVector(250., 125., 55.)), Query)) continue;
        FActorSpawnParameters Spawn;
        Spawn.Owner = this;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        auto* Car = GetWorld()->SpawnActor<AADVehiclePawn>(AADVehiclePawn::StaticClass(), FTransform(Rotation, Position), Spawn);
        if (!Car || !Car->GetPhysics()->IsReady()) { if (Car) Car->Destroy(); continue; }
        Car->SetRaceAppearance(FLinearColor(.035f, .05f, .08f), true);
        Car->SetDrivingEnabled(true);
        Car->GetPhysics()->AddTickPrerequisiteActor(this);
        auto* Driver = NewObject<UADRaceDriverComponent>(Car, TEXT("PoliceRoadDriver"));
        Car->AddInstanceComponent(Driver);
        Driver->RegisterComponent();
        Driver->AddTickPrerequisiteActor(this);
        FPoliceUnit Unit;
        Unit.Car = Car;
        Unit.Driver = Driver;
        Unit.bRoadblock=bRoadblock;
        if (bRoadblock)
        {
            Unit.RoadblockSeconds=0.f;
            Unit.bDirectControl=false;
            Driver->SetDriving(false);
        }
        Car->OnRecoveryRequested.AddWeakLambda(this,[this](AADVehiclePawn* Disabled)
        {
            for (auto& Existing:Units)
                if (Existing.Car.Get()==Disabled) { RetireUnit(Existing); break; }
        });
        if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetAtmosphere()) Mode->GetAtmosphere()->RegisterVehicle(Car);
        AddIdentification(Unit);
        Units.Add(MoveTemp(Unit));
        RefreshDrivers();
        return true;
    }
    return false;
}

int32 AADPoliceDirector::GetUnitCount() const
{
    int32 Count=0;
    for (const auto& Unit:Units)
        if (!Unit.bRetired && Unit.Car.IsValid() && Unit.Driver.IsValid()) ++Count;
    return Count;
}

int32 AADPoliceDirector::GetRoadblockCount() const
{
    int32 Count=0;
    for (const FPoliceUnit& Unit:Units) if (!Unit.bRetired && Unit.bRoadblock && Unit.Car.IsValid()) ++Count;
    return Count;
}

void AADPoliceDirector::RetireUnit(FPoliceUnit& Unit)
{
    if (Unit.bRetired) return;
    Unit.bRetired=true;
    Unit.bSeesPlayer=false;
    Unit.bDirectControl=false;
    Unit.RetirementSeconds=0.f;
    SenseCountdown=0.;
    if (Unit.Driver.IsValid()) Unit.Driver->SetDriving(false);
    if (Unit.RedLight.IsValid()) Unit.RedLight->SetIntensity(0.f);
    if (Unit.BlueLight.IsValid()) Unit.BlueLight->SetIntensity(0.f);
    if (!Unit.Car.IsValid()) return;
    Unit.Car->SetDrivingEnabled(false);
    Unit.Car->GetPhysics()->SetComponentTickEnabled(false);
    if (auto* Chassis=Cast<UPrimitiveComponent>(Unit.Car->GetRootComponent()))
    {
        if (Chassis->IsSimulatingPhysics())
        {
            Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
        Chassis->SetSimulatePhysics(false);
    }
    Unit.Car->SetActorTickEnabled(false);
}

void AADPoliceDirector::RemoveRetiredUnits(float DeltaSeconds)
{
    bool bRemoved=false;
    for (int32 Index=Units.Num()-1;Index>=0;--Index)
    {
        auto& Unit=Units[Index];
        if (!Unit.Car.IsValid()) { Units.RemoveAt(Index); bRemoved=true; continue; }
        if (!Unit.Driver.IsValid() || Unit.Driver->NeedsRecovery() || Unit.Car->GetActorLocation().ContainsNaN()) RetireUnit(Unit);
        if (!Unit.bRetired) continue;
        Unit.RetirementSeconds+=DeltaSeconds;
        const FVector Position=Unit.Car->GetActorLocation();
        // Keep a visible disabled car in place. Replacement happens later on a
        // safe distant road; there is no recovery teleport or pursuit catch-up.
        if (!Position.ContainsNaN() && (Unit.RetirementSeconds<5.f
            || FVector::DistSquared(Position,Player->GetActorLocation())<FMath::Square(15000.))) continue;
        Unit.Car->OnRecoveryRequested.RemoveAll(this);
        Unit.Car->GetPhysics()->RemoveTickPrerequisiteActor(this);
        if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetAtmosphere())
            Mode->GetAtmosphere()->UnregisterVehicle(Unit.Car.Get());
        Unit.Car->Destroy();
        Units.RemoveAt(Index);
        bRemoved=true;
    }
    if (bRemoved) RefreshDrivers();
}

void AADPoliceDirector::AddIdentification(FPoliceUnit& Unit)
{
    AADVehiclePawn* Car = Unit.Car.Get();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Car || !Cube || !Base) return;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FLinearColor Color = Side == 0 ? FLinearColor(1.f, .015f, .01f) : FLinearColor(.015f, .07f, 1.f);
        const FVector Location(0., Side == 0 ? -32. : 32., 88.);
        auto* Lens = NewObject<UStaticMeshComponent>(Car, Side == 0 ? TEXT("PoliceRedLens") : TEXT("PoliceBlueLens"));
        Car->AddInstanceComponent(Lens);
        Lens->SetupAttachment(Car->GetRootComponent());
        Lens->SetStaticMesh(Cube);
        Lens->SetRelativeLocation(Location);
        Lens->SetRelativeScale3D(FVector(.20, .58, .075));
        Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Lens->SetCastShadow(false);
        auto* Material = UMaterialInstanceDynamic::Create(Base, Lens);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Lens->SetMaterial(0, Material);
        Lens->RegisterComponent();
        auto* Light = NewObject<UPointLightComponent>(Car, Side == 0 ? TEXT("PoliceRedLight") : TEXT("PoliceBlueLight"));
        Car->AddInstanceComponent(Light);
        Light->SetupAttachment(Car->GetRootComponent());
        Light->SetRelativeLocation(Location + FVector(0., 0., 12.));
        Light->SetLightColor(Color);
        Light->SetAttenuationRadius(500.f);
        Light->SetIntensity(0.f);
        Light->SetCastShadows(false);
        Light->RegisterComponent();
        if (Side == 0) { Unit.RedLight = Light; Unit.RedLens = Lens; }
        else { Unit.BlueLight = Light; Unit.BlueLens = Lens; }
    }
}

void AADPoliceDirector::Sense()
{
    bAnySeesPlayer = false;
    if (!Player.IsValid()) return;
    const FVector Target = Player->GetActorLocation() + FVector(0., 0., 65.);
    for (auto& Unit : Units)
    {
        Unit.bSeesPlayer = false;
        if (Unit.bRetired || !Unit.Car.IsValid()) continue;
        const FVector Origin = Unit.Car->GetActorLocation() + FVector(0., 0., 65.);
        if (FVector::DistSquared(Origin, Target) <= FMath::Square(Settings.DetectionDistanceM * 100.))
        {
            FHitResult Hit;
            Unit.bSeesPlayer = !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Target, ECC_Visibility, SightQuery);
            bAnySeesPlayer |= Unit.bSeesPlayer;
        }
        const FVector Forward = Unit.Car->GetActorForwardVector().GetSafeNormal2D();
        const FVector Nose = Unit.Car->GetActorLocation() + Forward * 250.;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ADPoliceObstacle), false, Unit.Car.Get());
        Query.AddIgnoredActor(Player.Get());
        FHitResult Obstacle;
        Unit.ClearanceM = GetWorld()->SweepSingleByChannel(Obstacle, Nose, Nose + Forward * 3000., Forward.Rotation().Quaternion(),
            ECC_Visibility, FCollisionShape::MakeBox(FVector(10., 100., 18.)), Query)
            ? static_cast<float>(Obstacle.Distance * .01) : 100.f;
    }
    if (bAnySeesPlayer)
    {
        LastSeenPosition = Player->GetActorLocation();
        LastSeenVelocity = Player->GetVelocity();
    }
}

void AADPoliceDirector::DriveUnit(FPoliceUnit& Unit, int32 Index, float DeltaSeconds)
{
    if (Unit.bRetired || !Unit.Car.IsValid() || !Unit.Driver.IsValid() || !Player.IsValid()) return;
    auto* Car = Unit.Car.Get();
    auto* Physics = Car->GetPhysics();
    Unit.PITCooldownSeconds=FMath::Max(0.f,Unit.PITCooldownSeconds-DeltaSeconds);
    const bool bRed = FMath::Fmod(GetWorld()->GetTimeSeconds() * 4. + Index, 2.) < 1.;
    if (Unit.RedLight.IsValid()) Unit.RedLight->SetIntensity(IsActive() && bRed ? 2000.f : 0.f);
    if (Unit.BlueLight.IsValid()) Unit.BlueLight->SetIntensity(IsActive() && !bRed ? 2000.f : 0.f);
    if (State == EADPoliceState::Busted) { Physics->SetControls(0.f, 1.f, 0.f, true); return; }
    if (Unit.bRoadblock)
    {
        Unit.RoadblockSeconds+=DeltaSeconds;
        const FVector LocalPlayer=Car->GetActorTransform().InverseTransformPosition(Player->GetActorLocation());
        if (Unit.RoadblockSeconds>=24.f || (LocalPlayer.X<0.f && LocalPlayer.Size2D()<3000.f))
        {
            Unit.bRoadblock=false;
            Unit.Driver->ResetDriver();
            Unit.Driver->SetDriving(true);
            return;
        }
        Physics->SetControls(0.f,1.f,0.f,false);
        return;
    }
    const FVector Position = Car->GetActorLocation();
    const FVector ToPlayer = LastSeenPosition - Position;
    const double DistanceM = ToPlayer.Size2D() * .01;
    double OwnRouteError = 0.;
    double PlayerRouteError = 0.;
    const double Progress = Route.ClosestDistanceM(Position, OwnRouteError);
    const double TargetProgress = Route.ClosestDistanceM(LastSeenPosition, PlayerRouteError);
    const FVector2D OwnTangent = (Route.PointAtDistance(Progress + 3.) - Route.PointAtDistance(Progress - 3.)).GetSafeNormal();
    const FVector2D TargetTangent = (Route.PointAtDistance(TargetProgress + 3.) - Route.PointAtDistance(TargetProgress - 3.)).GetSafeNormal();
    const bool bDirect = State == EADPoliceState::Pursuit && Unit.bSeesPlayer && DistanceM < 60.
        && OwnRouteError < 10. && PlayerRouteError < 10. && FVector2D::DotProduct(OwnTangent, TargetTangent) > .95
        && FVector::DotProduct(Car->GetActorForwardVector().GetSafeNormal2D(), ToPlayer.GetSafeNormal2D()) > .2
        && !Unit.Driver->NeedsRecovery() && !Unit.Driver->IsReversing();
    if (bDirect != Unit.bDirectControl)
    {
        Unit.bDirectControl = bDirect;
        if (!bDirect) Unit.Driver->ResetDriver();
        Unit.Driver->SetDriving(!bDirect);
    }
    if (!bDirect) return;
    // Predict a modest interception lead on the same straight road segment. Far
    // units keep the road driver's braking points rather than cutting blocks.
    const FVector Target = LastSeenPosition + LastSeenVelocity * FMath::Clamp(DistanceM / 80., .15, .65);
    const FVector Local = Car->GetActorTransform().InverseTransformPosition(Target);
    const double WheelbaseM = (Physics->GetDefinition().WheelAnchorsCm[0].X - Physics->GetDefinition().WheelAnchorsCm[2].X) * .01;
    const double Curvature = 2. * (Local.Y * .01) / FMath::Max(16., Local.SizeSquared2D() * .0001);
    const double DesiredSteerDegrees = FMath::RadiansToDegrees(FMath::Atan(WheelbaseM * Curvature));
    const double SpeedMps = FMath::Max(0., FVector::DotProduct(Car->GetVelocity(), Car->GetActorForwardVector()) * .01);
    const double SteerLimit = FMath::Lerp(static_cast<double>(Physics->GetDefinition().MaxSteeringDegrees),
        static_cast<double>(Physics->GetDefinition().HighSpeedSteeringDegrees),
        FMath::Clamp(SpeedMps / Physics->GetDefinition().SteeringFalloffMps, 0., 1.));
    const double PlayerSpeedMps = LastSeenVelocity.Size2D() * .01;
    double DesiredSpeed = FMath::Clamp(PlayerSpeedMps + (DistanceM - 7.) * .65, 0., 33. + Heat * 2.);
    DesiredSpeed = FMath::Min(DesiredSpeed, FMath::Sqrt(FMath::Max(0., (Unit.ClearanceM - 3.) * 7.)));
    if (DistanceM < 6.) DesiredSpeed = FMath::Min(DesiredSpeed, PlayerSpeedMps * .8);
    const double SpeedError = DesiredSpeed - SpeedMps;
    Physics->SetControls(static_cast<float>(FMath::Clamp(SpeedError * .3, 0., 1.)),
        static_cast<float>(FMath::Clamp(-SpeedError * .25, 0., 1.)),
        static_cast<float>(FMath::Clamp(DesiredSteerDegrees / FMath::Max(1., SteerLimit), -1., 1.)), false);

    // A controlled rear-quarter tap gives the player a readable PIT counterplay
    // window. It never teleports either car and cannot repeat every frame.
    if (Heat>=3 && Unit.PITCooldownSeconds<=0.f)
    {
        const FVector PlayerLocal=Player->GetActorTransform().InverseTransformPosition(Car->GetActorLocation());
        const FVector RelativeVelocity=Player->GetActorTransform().InverseTransformVector(
            Car->GetVelocity()-Player->GetVelocity());
        const double PlayerSpeed=Player->GetVelocity().Size2D();
        const bool bAtRearQuarter=PlayerLocal.X>=-520. && PlayerLocal.X<=-150.
            && FMath::Abs(PlayerLocal.Y)>=85. && FMath::Abs(PlayerLocal.Y)<=260.;
        const bool bMatchedSpeed=RelativeVelocity.Size2D()<=1400.
            && FVector::DotProduct(Car->GetActorForwardVector().GetSafeNormal2D(),
                Player->GetActorForwardVector().GetSafeNormal2D())>.75;
        if (bAtRearQuarter && bMatchedSpeed && PlayerSpeed>1200.)
        {
            if (UPrimitiveComponent* PlayerBody=Cast<UPrimitiveComponent>(Player->GetRootComponent());
                PlayerBody && PlayerBody->IsSimulatingPhysics())
            {
                const float Side=FMath::Sign(PlayerLocal.Y);
                const FVector Contact=Player->GetActorLocation()-Player->GetActorForwardVector()*120.f
                    +Player->GetActorRightVector()*Side*90.f;
                PlayerBody->AddImpulseAtLocation(-Player->GetActorRightVector()*52000.f,Contact);
                Unit.PITCooldownSeconds=5.f;
                ++PITCount;
            }
        }
    }
}

void AADPoliceDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bReady || !bEnabled || !HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    if (!Player.IsValid())
    {
        if (!Units.IsEmpty()) ClearUnits();
        Heat=0; State=EADPoliceState::Patrol; PursuitUnitsDispatched=0;
        return;
    }
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetRaceManager()
        && Mode->GetRaceManager()->GetState()!=EADRaceState::Idle)
    { SetEnabled(false); return; }
    if (Player->IsInGarage() || !Player->IsDrivingEnabled())
    {
        // Garage entry is blocked by the controller during an active pursuit.
        // When parked normally, remove simulation cost; no pursuit credit is awarded.
        if (!IsActive() && !Units.IsEmpty()) ClearUnits();
        return;
    }
    const double Step = static_cast<double>(DeltaSeconds);
    RemoveRetiredUnits(DeltaSeconds);
    StateSeconds += Step;
    SpawnRetrySeconds -= Step;
    const int32 DesiredUnits = Heat >= 3 ? FMath::Min(3,Settings.MaxUnits-2) : 2;
    const bool bPatrolNeedsUnits=State==EADPoliceState::Patrol && GetUnitCount()<2;
    const bool bHeatNeedsUnit=(State==EADPoliceState::Pursuit || State==EADPoliceState::Search)
        && PursuitUnitsDispatched<DesiredUnits;
    if ((bPatrolNeedsUnits || bHeatNeedsUnit) && Units.Num()<Settings.MaxUnits && SpawnRetrySeconds <= 0.)
    {
        if (SpawnUnit() && bHeatNeedsUnit) ++PursuitUnitsDispatched;
        SpawnRetrySeconds = 3.;
    }
    RoadblockRetrySeconds-=Step;
    if (State==EADPoliceState::Pursuit && Heat>=4 && !bRoadblockDispatched
        && RoadblockRetrySeconds<=0. && Units.Num()+2<=Settings.MaxUnits)
    {
        if (SpawnUnit(true,0))
        {
            if (SpawnUnit(true,1))
            {
                bRoadblockDispatched=true;
                UE_LOG(LogADPolice,Display,TEXT("Roadblock deployed ahead on validated pursuit route."));
            }
            else
            {
                for (FPoliceUnit& Unit:Units)
                    if (Unit.bRoadblock) { Unit.bRoadblock=false; Unit.Driver->ResetDriver(); Unit.Driver->SetDriving(true); }
                RoadblockRetrySeconds=10.;
                RefreshDrivers();
            }
        }
        else RoadblockRetrySeconds=10.;
    }
    SenseCountdown -= Step;
    if (SenseCountdown <= 0.) { Sense(); SenseCountdown = Settings.SenseIntervalSeconds; }
    if (State == EADPoliceState::Patrol)
    {
        ViolationSeconds = bAnySeesPlayer && Player->GetVelocity().Size2D() * .036 >= Settings.SpeedingKmh
            ? ViolationSeconds + Step : 0.;
        if (ViolationSeconds >= Settings.DetectionGraceSeconds) StartPursuit();
    }
    else if (State == EADPoliceState::Pursuit)
    {
        PursuitSeconds += Step;
        int32 DesiredHeat = 1;
        for (int32 Index = 1; Index < Settings.HeatThresholdSeconds.Num(); ++Index)
            if (PursuitSeconds >= Settings.HeatThresholdSeconds[Index]) DesiredHeat = Index + 1;
        if (DesiredHeat != Heat) { Heat = DesiredHeat; RefreshDrivers(); }
        MissingSightSeconds = bAnySeesPlayer ? 0. : MissingSightSeconds + Step;
        bool bInArrestRange = false;
        if (Player->GetVelocity().Size2D() * .036 < Settings.BustMaxSpeedKmh)
            for (const auto& Unit : Units)
                if (!Unit.bRetired && Unit.bSeesPlayer && Unit.Car.IsValid() && Unit.Car->GetVelocity().Size2D() * .036 < 12.
                    && FVector::Dist2D(Unit.Car->GetActorLocation(), Player->GetActorLocation()) < Settings.BustDistanceM * 100.)
                { bInArrestRange = true; break; }
        BustProgressSeconds = bInArrestRange ? BustProgressSeconds + Step : 0.;
        if (BustProgressSeconds >= Settings.BustSeconds) EnterState(EADPoliceState::Busted);
        else if (MissingSightSeconds >= Settings.LostSightGraceSeconds) EnterState(EADPoliceState::Search);
    }
    else if (State == EADPoliceState::Search)
    {
        if (bAnySeesPlayer) { MissingSightSeconds = 0.; EnterState(EADPoliceState::Pursuit); }
        else if (StateSeconds >= Settings.SearchSeconds)
        {
            EnterState(EADPoliceState::Cooldown);
            OnEscaped.Broadcast();
        }
    }
    else if ((State == EADPoliceState::Cooldown || State == EADPoliceState::Busted) && StateSeconds >= Settings.CooldownSeconds)
    {
        MissingSightSeconds = PursuitSeconds = BustProgressSeconds = 0.;
        PursuitUnitsDispatched = 0;
        bRoadblockDispatched=false;
        EnterState(EADPoliceState::Patrol);
    }
    for (int32 Index = 0; Index < Units.Num(); ++Index) DriveUnit(Units[Index], Index, DeltaSeconds);
}

FString AADPoliceDirector::GetStateLabel() const
{
    if (!bReady) return TEXT("POLICE UNAVAILABLE");
    if (!bEnabled) return TEXT("POLICE PAUSED");
    switch (State)
    {
    case EADPoliceState::Patrol: return TEXT("PATROL");
    case EADPoliceState::Pursuit: return TEXT("PURSUIT");
    case EADPoliceState::Search: return TEXT("SEARCH — BREAK CONTACT");
    case EADPoliceState::Cooldown: return TEXT("ESCAPED");
    case EADPoliceState::Busted: return TEXT("BUSTED — RELEASE PENDING");
    default: return TEXT("PATROL");
    }
}

float AADPoliceDirector::GetSearchRemainingSeconds() const
{
    return State == EADPoliceState::Search ? static_cast<float>(FMath::Max(0., Settings.SearchSeconds - StateSeconds)) : 0.f;
}

void AADPoliceDirector::ClearUnits()
{
    for (auto& Unit : Units)
    {
        if (Unit.Driver.IsValid()) Unit.Driver->SetDriving(false);
        if (Unit.Car.IsValid())
        {
            Unit.Car->OnRecoveryRequested.RemoveAll(this);
            Unit.Car->GetPhysics()->RemoveTickPrerequisiteActor(this);
            if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetAtmosphere())
                Mode->GetAtmosphere()->UnregisterVehicle(Unit.Car.Get());
            Unit.Car->Destroy();
        }
    }
    Units.Reset();
    bAnySeesPlayer = false;
}

void AADPoliceDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearUnits();
    Super::EndPlay(EndPlayReason);
}
