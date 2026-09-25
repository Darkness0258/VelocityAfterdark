#include "World/ADAtmosphere.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/ADVehiclePawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "World/ADDistrict.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Engine/GameInstance.h"
#include "Core/ADGameMode.h"
#include "Racing/ADRaceManager.h"
#include "World/ADPoliceDirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogADAtmosphere, Log, All);

namespace
{
    bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Min, double Max, double& Out)
    {
        const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
        return Value && Value->Type == EJson::Number && Value->TryGetNumber(Out)
            && FMath::IsFinite(Out) && Out >= Min && Out <= Max;
    }

    bool WeatherFromString(const FString& Name, EADWeather& Out)
    {
        if (Name == TEXT("Clear")) Out = EADWeather::Clear;
        else if (Name == TEXT("Rain")) Out = EADWeather::Rain;
        else if (Name == TEXT("Fog")) Out = EADWeather::Fog;
        else return false;
        return true;
    }

    float SmoothRange(float Min, float Max, float Value)
    {
        const float T = FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f);
        return T * T * (3.f - 2.f * T);
    }

    double Wrap(double Value, double Period)
    {
        const double Remainder = FMath::Fmod(Value, Period);
        return Remainder < 0. ? Remainder + Period : Remainder;
    }
}

AADAtmosphere::AADAtmosphere()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("AtmosphereRoot"));
    Rain = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LocalRain"));
    Rain->SetupAttachment(RootComponent);
    Rain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rain->SetCanEverAffectNavigation(false);
    Rain->SetCastShadow(false);
    Rain->SetMobility(EComponentMobility::Movable);
    Rain->SetVisibility(false);
}

void AADAtmosphere::BeginPlay()
{
    Super::BeginPlay();
    // GameMode owns QA opt-in by deciding whether to spawn the living world.
    // An explicit InitializeLivingWorld call must initialize the actual actor;
    // a second command-line gate here left a spawned but unusable atmosphere.
    if (!LoadDefinition() || !CacheDistrict() || !BuildRain())
    {
        RestoreDistrict();
        SetActorTickEnabled(false);
        UE_LOG(LogADAtmosphere, Error, TEXT("Atmosphere disabled: %s"), *LoadError);
        return;
    }
    bReady = true;
    if (GetNetMode()==NM_Standalone && GetWorld()->GetGameInstance())
    {
        Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
        if (Ownership.IsValid() && Ownership->IsReady()) RestoreWorldSnapshot(Ownership->GetProfile().World);
    }
    if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
        BindVehicle(Cast<AADVehiclePawn>(Controller->GetPawn()));
    ApplyLighting();
    UE_LOG(LogADAtmosphere, Display, TEXT("Atmosphere ready: %.2f hour, %.0fs day, %d rain instances."), Hour, DayLengthSeconds, RainCount);
}

bool AADAtmosphere::LoadDefinition()
{
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/World/atmosphere.json"));
    FString Json;
    TSharedPtr<FJsonObject> Object;
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > 16384 || !FFileHelper::LoadFileToString(Json, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
    { LoadError = TEXT("Cannot read atmosphere.json (expected a JSON object below 16 KB)."); return false; }
    const auto Fail = [this](const TCHAR* Name)
    { LoadError = FString::Printf(TEXT("Invalid atmosphere field: %s."), Name); return false; };
    double Value = 0.;
    if (!Number(Object, TEXT("schemaVersion"), 1, 1, Value)) return Fail(TEXT("schemaVersion"));
    if (!Number(Object, TEXT("initialHour"), 0, 24, Value) || Value == 24) return Fail(TEXT("initialHour"));
    Hour = Value;
    FString InitialWeatherName;
    EADWeather InitialWeather = EADWeather::Clear;
    if (!Object->TryGetStringField(TEXT("initialWeather"), InitialWeatherName)
        || !WeatherFromString(InitialWeatherName, InitialWeather)) return Fail(TEXT("initialWeather"));
    double InitialRainAmount = 0., InitialWetness = 0.;
    if (!Number(Object, TEXT("initialRainAmount"), 0, 1, InitialRainAmount)) return Fail(TEXT("initialRainAmount"));
    if (!Number(Object, TEXT("initialWetness"), 0, 1, InitialWetness)) return Fail(TEXT("initialWetness"));
    if (!Number(Object, TEXT("dayLengthSeconds"), 120, 86400, Value)) return Fail(TEXT("dayLengthSeconds"));
    DayLengthSeconds = Value;
    if (!Object->TryGetBoolField(TEXT("automaticWeather"), bAutomaticWeather)) return Fail(TEXT("automaticWeather"));
    if (!Number(Object, TEXT("weatherIntervalSeconds"), 30, 86400, Value)) return Fail(TEXT("weatherIntervalSeconds"));
    WeatherIntervalSeconds = Value;
    if (!Number(Object, TEXT("transitionSeconds"), 1, 120, Value)) return Fail(TEXT("transitionSeconds"));
    TransitionSeconds = static_cast<float>(Value);
    if (TransitionSeconds >= WeatherIntervalSeconds) return Fail(TEXT("transitionSeconds must be shorter than weatherIntervalSeconds"));
    if (!Number(Object, TEXT("wettingSeconds"), 1, 3600, Value)) return Fail(TEXT("wettingSeconds"));
    WettingSeconds = static_cast<float>(Value);
    if (!Number(Object, TEXT("dryingSeconds"), 1, 7200, Value)) return Fail(TEXT("dryingSeconds"));
    DryingSeconds = static_cast<float>(Value);
    if (!Number(Object, TEXT("rainCount"), 16, 2048, Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("rainCount"));
    RainCount = static_cast<int32>(Value);
    if (!Number(Object, TEXT("rainRadiusCm"), 500, 5000, Value)) return Fail(TEXT("rainRadiusCm"));
    RainRadiusCm = static_cast<float>(Value);
    if (!Number(Object, TEXT("rainHeightCm"), 500, 3000, Value)) return Fail(TEXT("rainHeightCm"));
    RainHeightCm = static_cast<float>(Value);
    if (!Number(Object, TEXT("rainSpeedCmPerSecond"), 100, 5000, Value)) return Fail(TEXT("rainSpeedCmPerSecond"));
    RainSpeedCmPerSecond = static_cast<float>(Value);
    if (!Number(Object, TEXT("rainSeed"), 0, 2147483647, Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("rainSeed"));
    RainRandom.Initialize(static_cast<int32>(Value));
    if (!Number(Object, TEXT("daySunIntensityLux"), 100, 120000, Value)) return Fail(TEXT("daySunIntensityLux"));
    DaySunIntensity = static_cast<float>(Value);
    if (!Number(Object, TEXT("daySkyIntensity"), 1, 5000, Value)) return Fail(TEXT("daySkyIntensity"));
    DaySkyIntensity = static_cast<float>(Value);
    if (!Number(Object, TEXT("dayExposureBias"), -16, 0, Value)) return Fail(TEXT("dayExposureBias"));
    DayExposureBias = static_cast<float>(Value);
    const TArray<TSharedPtr<FJsonValue>>* Sequence = nullptr;
    if (!Object->TryGetArrayField(TEXT("weatherSequence"), Sequence) || Sequence->Num() < 2 || Sequence->Num() > 16)
        return Fail(TEXT("weatherSequence"));
    for (const auto& Entry : *Sequence)
    {
        EADWeather EntryWeather;
        if (!Entry || Entry->Type != EJson::String || !WeatherFromString(Entry->AsString(), EntryWeather)) return Fail(TEXT("weatherSequence entry"));
        WeatherSequence.Add(EntryWeather);
    }
    if (WeatherSequence[0] != EADWeather::Clear) return Fail(TEXT("weatherSequence must start Clear"));
    WeatherIndex = WeatherSequence.IndexOfByKey(InitialWeather);
    if (WeatherIndex == INDEX_NONE) return Fail(TEXT("initialWeather must appear in weatherSequence"));
    if (InitialWeather != EADWeather::Rain && (InitialRainAmount > 0. || InitialWetness > 0.))
        return Fail(TEXT("initial rain amounts require initialWeather Rain"));
    Weather = InitialWeather;
    RainAmount = static_cast<float>(InitialRainAmount);
    Wetness = static_cast<float>(InitialWetness);
    return true;
}

bool AADAtmosphere::CacheDistrict()
{
    AADDistrict* District = nullptr;
    // Actor discovery occurs once. Neither the tick nor rain emitter scans the world.
    for (TActorIterator<AADDistrict> It(GetWorld()); It; ++It) { District = *It; break; }
    if (!IsValid(District) || !District->IsReady())
    { LoadError = TEXT("A ready Dockside district must exist before atmosphere initialization."); return false; }
    Sun = District->FindComponentByClass<UDirectionalLightComponent>();
    Sky = District->FindComponentByClass<USkyLightComponent>();
    SkyAtmosphere = District->FindComponentByClass<USkyAtmosphereComponent>();
    Fog = District->FindComponentByClass<UExponentialHeightFogComponent>();
    PostProcess = District->FindComponentByClass<UPostProcessComponent>();
    if (!Sun.IsValid() || !Sky.IsValid() || !Fog.IsValid() || !PostProcess.IsValid() || !SkyAtmosphere.IsValid())
    { LoadError = TEXT("District lighting is incomplete."); return false; }
    OriginalSunIntensity = Sun->Intensity;
    OriginalSunColor = Sun->GetLightColor();
    OriginalSunRotation = Sun->GetRelativeRotation();
    OriginalSkyIntensity = Sky->Intensity;
    OriginalSkyColor = Sky->GetLightColor();
    OriginalFogDensity = Fog->FogDensity;
    OriginalFogColor = Fog->FogInscatteringLuminance;
    OriginalFogStart = Fog->StartDistance;
    OriginalFogOpacity = Fog->FogMaxOpacity;
    OriginalSkyLuminance = SkyAtmosphere->SkyLuminanceFactor;
    OriginalExposureBias = PostProcess->Settings.AutoExposureBias;
    bOriginalExposureOverride = PostProcess->Settings.bOverride_AutoExposureBias;
    bDistrictCached = true;
    TInlineComponentArray<UPointLightComponent*> Lights(District);
    for (UPointLightComponent* Light : Lights) StreetLights.Add({Light, Light->Intensity});

    TMap<UMaterialInterface*, UMaterialInstanceDynamic*> MaterialCache;
    TInlineComponentArray<UStaticMeshComponent*> Meshes(District);
    for (UStaticMeshComponent* Mesh : Meshes)
    for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
    {
        UMaterialInterface* Original = Mesh->GetMaterial(Slot);
        if (!Original) continue;
        const FString Name = Original->GetName();
        const bool bRoad = Name == TEXT("M_Asphalt");
        const bool bEmissive = Name == TEXT("M_WindowWarm") || Name == TEXT("M_WindowCool") || Name == TEXT("M_EmissiveWhite");
        if (!bRoad && !bEmissive) continue;
        UMaterialInstanceDynamic*& Dynamic = MaterialCache.FindOrAdd(Original);
        if (!Dynamic)
        {
            Dynamic = UMaterialInstanceDynamic::Create(Original, this);
            if (!Dynamic) { LoadError = TEXT("Could not create weather material instances."); return false; }
            DynamicMaterials.Add(Dynamic);
            OriginalMaterials.Add(Original);
            if (bRoad)
            {
                RoadMaterial = Dynamic;
                Original->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), OriginalRoadRoughness);
                Original->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), OriginalRoadColor);
            }
            else
            {
                FLinearColor Emission = FLinearColor::White;
                Original->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EmissiveColor")), Emission);
                EmissiveSurfaces.Add({Dynamic, Emission});
            }
        }
        SurfaceBindings.Add({Mesh, Slot, Original});
        Mesh->SetMaterial(Slot, Dynamic);
    }
    return true;
}

bool AADAtmosphere::BuildRain()
{
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"));
    if (!Mesh || !Base) { LoadError = TEXT("Rain requires the cooked engine cube and emissive material."); return false; }
    auto* Material = UMaterialInstanceDynamic::Create(Base, this);
    if (!Material) { LoadError = TEXT("Rain material allocation failed."); return false; }
    Material->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(.18f,.23f,.30f));
    Material->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor(.28f,.38f,.52f));
    Material->SetScalarParameterValue(TEXT("Roughness"), .24f);
    DynamicMaterials.Add(Material);
    Rain->SetStaticMesh(Mesh);
    Rain->SetMaterial(0, Material);
    RainPositions.SetNumUninitialized(RainCount);
    RainTransforms.SetNum(RainCount);
    for (int32 I = 0; I < RainCount; ++I)
    {
        RainPositions[I] = FVector(RainRandom.FRandRange(-RainRadiusCm,RainRadiusCm), RainRandom.FRandRange(-RainRadiusCm,RainRadiusCm), RainRandom.FRandRange(0,RainHeightCm));
        RainTransforms[I] = FTransform(FRotator(12,0,0), RainPositions[I], FVector(.007f,.007f,.5f));
    }
    Rain->AddInstances(RainTransforms, false);
    return true;
}

void AADAtmosphere::SetHour(double NewHour)
{
    if (!FMath::IsFinite(NewHour)) return;
    Hour = Wrap(NewHour, 24.);
    if (bReady) ApplyLighting();
}

FString AADAtmosphere::GetWeatherName() const
{
    switch (Weather)
    {
    case EADWeather::Rain: return TEXT("Rain");
    case EADWeather::Fog: return TEXT("Fog");
    default: return TEXT("Clear");
    }
}

float AADAtmosphere::GetExposureBias() const
{
    return bReady && PostProcess.IsValid() ? PostProcess->Settings.AutoExposureBias : 0.f;
}

void AADAtmosphere::SetWeather(EADWeather NewWeather)
{
    if (NewWeather != EADWeather::Clear && NewWeather != EADWeather::Rain && NewWeather != EADWeather::Fog) return;
    Weather = NewWeather;
    WeatherElapsed = 0.;
    const int32 Index = WeatherSequence.Find(NewWeather);
    if (Index != INDEX_NONE) WeatherIndex = Index;
}

void AADAtmosphere::SetRainEnabled(bool bValue)
{
    bRainEnabled = bValue;
    if (!bRainEnabled) Rain->SetVisibility(false);
}

void AADAtmosphere::RegisterVehicle(AADVehiclePawn* Car)
{
    if (!IsValid(Car)) return;
    Vehicles.AddUnique(TWeakObjectPtr<AADVehiclePawn>(Car));
    if (bReady && !Car->IsInGarage() && Car->GetPhysics()) Car->GetPhysics()->SetRoadWetness(Wetness);
}

void AADAtmosphere::UnregisterVehicle(AADVehiclePawn* Car)
{
    Vehicles.Remove(TWeakObjectPtr<AADVehiclePawn>(Car));
    if (Player.Get() == Car) { Player.Reset(); Rain->SetVisibility(false); bRainPositioned = false; }
}

void AADAtmosphere::BindVehicle(AADVehiclePawn* Car)
{
    Player = Car;
    bRainPositioned = false;
    RegisterVehicle(Car);
}

void AADAtmosphere::UpdateVehicles()
{
    for (int32 Index = Vehicles.Num() - 1; Index >= 0; --Index)
    {
        AADVehiclePawn* Car = Vehicles[Index].Get();
        if (!IsValid(Car)) { Vehicles.RemoveAtSwap(Index, 1, EAllowShrinking::No); continue; }
        if (!Car->IsInGarage() && Car->GetPhysics()) Car->GetPhysics()->SetRoadWetness(Wetness);
    }
}

void AADAtmosphere::ApplyLighting()
{
    if (!bReady || !Sun.IsValid() || !Sky.IsValid() || !Fog.IsValid()) return;
    const float Elevation = FMath::Sin(static_cast<float>((Hour - 6.) * UE_DOUBLE_PI / 12.));
    const float Day = SmoothRange(-.07f, .26f, Elevation);
    const float Night = 1.f - SmoothRange(-.12f,.1f,Elevation);
    const float Overcast = FMath::Clamp(RainAmount*.62f + FogAmount*.38f, 0.f, .8f);
    // Logarithmic light/exposure transitions avoid a flash between night and day.
    const float SunIntensity = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(.01f,OriginalSunIntensity)), FMath::Loge(DaySunIntensity), Day));
    const float SkyIntensity = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(.01f,OriginalSkyIntensity)), FMath::Loge(DaySkyIntensity), Day));
    Sun->SetIntensity(SunIntensity * (1.f - Overcast));
    const FLinearColor Dusk = FLinearColor(1.f,.61f,.32f);
    const FLinearColor DayColor = FMath::Lerp(Dusk,FLinearColor(1.f,.96f,.87f),SmoothRange(.05f,.45f,Elevation));
    Sun->SetLightColor(FMath::Lerp(OriginalSunColor,DayColor,Day));
    Sun->SetRelativeRotation(FRotator(-FMath::Max(4.f,FMath::Abs(Elevation)*(Elevation > 0.f ? 68.f : 42.f)),
        static_cast<float>(Hour*15. + (Elevation > 0.f ? -90. : 90.)),0));
    Sky->SetIntensity(SkyIntensity * (1.f - Overcast*.35f));
    Sky->SetLightColor(FMath::Lerp(OriginalSkyColor,FLinearColor(.82f,.91f,1.f),Day));
    if (SkyAtmosphere.IsValid()) SkyAtmosphere->SetSkyLuminanceFactor(FMath::Lerp(OriginalSkyLuminance,FLinearColor::White,Day));
    if (PostProcess.IsValid())
    {
        PostProcess->Settings.bOverride_AutoExposureBias = true;
        PostProcess->Settings.AutoExposureBias = FMath::Lerp(OriginalExposureBias,DayExposureBias,Day);
    }
    Fog->SetFogDensity(FMath::Lerp(OriginalFogDensity,.055f,FogAmount) + RainAmount*.013f);
    Fog->SetStartDistance(FMath::Lerp(OriginalFogStart,320.f,FMath::Clamp(FogAmount+RainAmount*.45f,0.f,1.f)));
    Fog->SetFogMaxOpacity(FMath::Lerp(OriginalFogOpacity,.94f,FMath::Clamp(FogAmount+RainAmount*.4f,0.f,1.f)));
    // Fog is multiplied by exposure along with the scene, preserving daylight visibility.
    const float FogExposureCompensation = FMath::Pow(2.f,-DayExposureBias*Day);
    Fog->SetFogInscatteringColor(FMath::Lerp(OriginalFogColor,FLinearColor(.20f,.245f,.285f),Day) * FogExposureCompensation);
    for (const FStreetLight& Light : StreetLights)
        if (Light.Component.IsValid()) Light.Component->SetIntensity(Light.OriginalIntensity*Night);
    for (const FEmissiveSurface& Surface : EmissiveSurfaces)
        Surface.Material->SetVectorParameterValue(TEXT("EmissiveColor"),Surface.OriginalColor*Night);
    if (RoadMaterial)
    {
        RoadMaterial->SetScalarParameterValue(TEXT("Roughness"),FMath::Lerp(OriginalRoadRoughness,.075f,Wetness));
        RoadMaterial->SetVectorParameterValue(TEXT("BaseColor"),OriginalRoadColor*FMath::Lerp(1.f,.62f,Wetness));
    }
}

void AADAtmosphere::UpdateRain(float ElapsedSeconds)
{
    const AADVehiclePawn* Car = Player.Get();
    if (!bRainEnabled || RainAmount < .025f || !IsValid(Car) || Car->IsInGarage())
    {
        Rain->SetVisibility(false);
        bRainPositioned = false;
        if (bRainActiveLogged)
        {
            UE_LOG(LogADAtmosphere, Display, TEXT("Rain streak emitter stopped."));
            bRainActiveLogged = false;
        }
        return;
    }
    Rain->SetVisibility(true);
    if (!bRainActiveLogged)
    {
        UE_LOG(LogADAtmosphere, Display, TEXT("Rain streak emitter active: %d instances at %.0f%% intensity."),
            RainCount, RainAmount * 100.f);
        bRainActiveLogged = true;
    }
    const FVector Center = Car->GetActorLocation();
    const FVector Min = Center - FVector(RainRadiusCm,RainRadiusCm,100.f);
    const float Diameter = RainRadiusCm*2.f;
    for (int32 I = 0; I < RainCount; ++I)
    {
        FVector& Position = RainPositions[I];
        if (!bRainPositioned)
            Position = Min + FVector(RainRandom.FRandRange(0,Diameter),RainRandom.FRandRange(0,Diameter),RainRandom.FRandRange(0,RainHeightCm));
        Position += FVector(95.f,30.f,-RainSpeedCmPerSecond)*ElapsedSeconds;
        Position.X = Min.X + Wrap(Position.X-Min.X,Diameter);
        Position.Y = Min.Y + Wrap(Position.Y-Min.Y,Diameter);
        Position.Z = Min.Z + Wrap(Position.Z-Min.Z,RainHeightCm);
        const FVector VehicleLocal = Car->GetActorTransform().InverseTransformPosition(Position);
        const bool bInsideCar = FMath::Abs(VehicleLocal.X) < 270. && FMath::Abs(VehicleLocal.Y) < 125.
            && VehicleLocal.Z > -50. && VehicleLocal.Z < 170.;
        const float Width = bInsideCar ? 0.f : .012f * FMath::Sqrt(RainAmount);
        RainTransforms[I] = FTransform(FRotator(12,0,0),Position,FVector(Width,Width,.65f*RainAmount));
    }
    bRainPositioned = true;
    Rain->BatchUpdateInstancesTransforms(0,RainTransforms,true,true,true);
}

void AADAtmosphere::ApplyNetworkSnapshot(double NewHour,uint8 NewWeather,float NewWetness)
{
    if (GetNetMode()!=NM_Client || !FMath::IsFinite(NewHour) || !FMath::IsFinite(NewWetness) || NewWeather>2) return;
    Hour=Wrap(NewHour,24.); Weather=static_cast<EADWeather>(NewWeather); Wetness=FMath::Clamp(NewWetness,0.f,1.f);
    const float Step=GetWorld()->GetDeltaSeconds()/FMath::Max(1.f,TransitionSeconds);
    RainAmount=FMath::FInterpConstantTo(RainAmount,Weather==EADWeather::Rain ? 1.f : 0.f,Step,1.f);
    FogAmount=FMath::FInterpConstantTo(FogAmount,Weather==EADWeather::Fog ? 1.f : 0.f,Step,1.f);
}

FADWorldSnapshot AADAtmosphere::CaptureWorldSnapshot() const
{
    FADWorldSnapshot Result;
    Result.bRecorded=bReady;
    Result.Hour=Hour;
    Result.WeatherElapsed=WeatherElapsed;
    Result.WeatherIndex=WeatherIndex;
    Result.Weather=static_cast<uint8>(Weather);
    Result.Wetness=Wetness;
    Result.RainAmount=RainAmount;
    Result.FogAmount=FogAmount;
    return Result;
}

bool AADAtmosphere::RestoreWorldSnapshot(const FADWorldSnapshot& Snapshot)
{
    if (!bReady || GetNetMode()!=NM_Standalone || !Snapshot.bRecorded || !Snapshot.IsValid()) return false;
    Hour=Snapshot.Hour;
    Weather=static_cast<EADWeather>(Snapshot.Weather);
    // A revised weather catalog may have a shorter sequence. Preserve the
    // actual weather and resume from its first supported occurrence in that case.
    WeatherIndex=WeatherSequence.IsValidIndex(Snapshot.WeatherIndex)
        && WeatherSequence[Snapshot.WeatherIndex]==Weather ? Snapshot.WeatherIndex : WeatherSequence.Find(Weather);
    if (WeatherIndex==INDEX_NONE) WeatherIndex=0;
    WeatherElapsed=FMath::Min(Snapshot.WeatherElapsed,WeatherIntervalSeconds);
    Wetness=Snapshot.Wetness; RainAmount=Snapshot.RainAmount; FogAmount=Snapshot.FogAmount;
    ApplyLighting(); UpdateVehicles();
    return true;
}

void AADAtmosphere::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bReady || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    if (!bFrozen && GetNetMode()!=NM_Client)
    {
        Hour = Wrap(Hour + static_cast<double>(DeltaSeconds)*24./DayLengthSeconds,24.);
        if (bAutomaticWeather)
        {
            WeatherElapsed += DeltaSeconds;
            if (WeatherElapsed >= WeatherIntervalSeconds)
            {
                const int64 Steps = FMath::FloorToInt64(WeatherElapsed/WeatherIntervalSeconds);
                WeatherElapsed = Wrap(WeatherElapsed,WeatherIntervalSeconds);
                WeatherIndex = (WeatherIndex + static_cast<int32>(Steps % WeatherSequence.Num())) % WeatherSequence.Num();
                Weather = WeatherSequence[WeatherIndex];
            }
        }
        const float Step = DeltaSeconds/TransitionSeconds;
        const float PreviousRain = RainAmount;
        RainAmount = FMath::FInterpConstantTo(RainAmount,Weather == EADWeather::Rain ? 1.f : 0.f,Step,1.f);
        FogAmount = FMath::FInterpConstantTo(FogAmount,Weather == EADWeather::Fog ? 1.f : 0.f,Step,1.f);
        // Water remains after the rain ends. Drying takes longer than wetting.
        const float AverageRain = (PreviousRain+RainAmount)*.5f;
        const float Wetting = AverageRain/WettingSeconds;
        const float Drying = (1.f-AverageRain)/DryingSeconds;
        Wetness = FMath::Clamp(Wetness + DeltaSeconds*(Wetting-Drying),0.f,1.f);
    }
    VisualElapsed += DeltaSeconds;
    if (VisualElapsed >= .1f)
    {
        ApplyLighting();
        UpdateVehicles();
        UpdateRain(VisualElapsed);
        VisualElapsed = 0.f;
        if (Ownership.IsValid() && Ownership->IsReady()) Ownership->StageWorldSnapshot(CaptureWorldSnapshot());
    }
    SaveElapsed+=DeltaSeconds;
    // Synchronous durable writes are restricted to a stopped free-drive car.
    // Racing, discovery and garage transactions also carry the latest snapshot.
    if (SaveElapsed>=60.f && Ownership.IsValid() && Ownership->IsReady() && Player.IsValid()
        && Player->IsDrivingEnabled() && !Player->IsInGarage() && FMath::Abs(Player->GetPhysics()->GetTelemetry().SpeedKmh)<1.f)
    {
        const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
        if (Mode && (!Mode->GetRaceManager() || Mode->GetRaceManager()->GetState()==EADRaceState::Idle)
            && (!Mode->GetPoliceDirector() || !Mode->GetPoliceDirector()->IsActive()))
        {
            SaveElapsed=0.f;
            if (!Ownership->SaveWorldSnapshot(SaveError))
                UE_LOG(LogADAtmosphere, Warning,TEXT("World autosave failed: %s"),*SaveError);
        }
    }
}

void AADAtmosphere::RestoreDistrict()
{
    if (!bDistrictCached) return;
    if (Sun.IsValid()) { Sun->SetIntensity(OriginalSunIntensity); Sun->SetLightColor(OriginalSunColor); Sun->SetRelativeRotation(OriginalSunRotation); }
    if (Sky.IsValid()) { Sky->SetIntensity(OriginalSkyIntensity); Sky->SetLightColor(OriginalSkyColor); }
    if (SkyAtmosphere.IsValid()) SkyAtmosphere->SetSkyLuminanceFactor(OriginalSkyLuminance);
    if (Fog.IsValid())
    {
        Fog->SetFogDensity(OriginalFogDensity);
        Fog->SetFogInscatteringColor(OriginalFogColor);
        Fog->SetStartDistance(OriginalFogStart);
        Fog->SetFogMaxOpacity(OriginalFogOpacity);
    }
    if (PostProcess.IsValid())
    {
        PostProcess->Settings.AutoExposureBias = OriginalExposureBias;
        PostProcess->Settings.bOverride_AutoExposureBias = bOriginalExposureOverride;
    }
    for (const FStreetLight& Light : StreetLights)
        if (Light.Component.IsValid()) Light.Component->SetIntensity(Light.OriginalIntensity);
    for (const FSurfaceBinding& Surface : SurfaceBindings)
        if (Surface.Component.IsValid()) Surface.Component->SetMaterial(Surface.Slot,Surface.Original);
    bDistrictCached = false;
}

void AADAtmosphere::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bReady && Ownership.IsValid() && Ownership->IsReady() && GetNetMode()==NM_Standalone)
    {
        Ownership->StageWorldSnapshot(CaptureWorldSnapshot());
        if (!Ownership->SaveWorldSnapshot(SaveError))
            UE_LOG(LogADAtmosphere,Warning,TEXT("World exit save failed: %s"),*SaveError);
    }
    bReady = false;
    for (const TWeakObjectPtr<AADVehiclePawn>& Entry : Vehicles)
        if (AADVehiclePawn* Car = Entry.Get()) if (Car->GetPhysics()) Car->GetPhysics()->SetRoadWetness(0.f);
    RestoreDistrict();
    Super::EndPlay(EndPlayReason);
}
