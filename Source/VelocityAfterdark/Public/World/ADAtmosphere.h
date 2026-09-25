#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ADWorldProgress.h"
#include "ADAtmosphere.generated.h"

class AADVehiclePawn;
class UADOwnershipSubsystem;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EADWeather : uint8 { Clear, Rain, Fog };

/** Bounded district atmosphere; race and traffic spawning register their own cars. */
UCLASS()
class VELOCITYAFTERDARK_API AADAtmosphere : public AActor
{
    GENERATED_BODY()
public:
    AADAtmosphere();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    double GetHour() const { return Hour; }
    FString GetWeatherName() const;
    EADWeather GetWeather() const { return Weather; }
    float GetWetness() const { return Wetness; }
    float GetRainIntensity() const { return RainAmount; }
    float GetExposureBias() const;
    bool IsReady() const { return bReady; }
    const FString& GetLoadError() const { return LoadError; }
    void SetHour(double NewHour);
    void SetWeather(EADWeather NewWeather);
    void SetFrozen(bool bValue) { bFrozen = bValue; }
    bool IsFrozen() const { return bFrozen; }
    void SetRainEnabled(bool bValue);
    void BindVehicle(AADVehiclePawn* Car);
    void RegisterVehicle(AADVehiclePawn* Car);
    void UnregisterVehicle(AADVehiclePawn* Car);
    void ApplyNetworkSnapshot(double NewHour,uint8 NewWeather,float NewWetness);
    FADWorldSnapshot CaptureWorldSnapshot() const;
    bool RestoreWorldSnapshot(const FADWorldSnapshot& Snapshot);
    const FString& GetSaveError() const { return SaveError; }

private:
    struct FStreetLight
    {
        TWeakObjectPtr<UPointLightComponent> Component;
        float OriginalIntensity = 0.f;
    };
    struct FSurfaceBinding
    {
        TWeakObjectPtr<UStaticMeshComponent> Component;
        int32 Slot = 0;
        TObjectPtr<UMaterialInterface> Original = nullptr;
    };
    struct FEmissiveSurface
    {
        TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
        FLinearColor OriginalColor = FLinearColor::White;
    };
    bool LoadDefinition();
    bool CacheDistrict();
    bool BuildRain();
    void ApplyLighting();
    void UpdateRain(float ElapsedSeconds);
    void UpdateVehicles();
    void RestoreDistrict();

    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Rain;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RoadMaterial;
    TWeakObjectPtr<UDirectionalLightComponent> Sun;
    TWeakObjectPtr<USkyLightComponent> Sky;
    TWeakObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;
    TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
    TWeakObjectPtr<UPostProcessComponent> PostProcess;
    TWeakObjectPtr<AADVehiclePawn> Player;
    TWeakObjectPtr<UADOwnershipSubsystem> Ownership;
    FString SaveError;
    float SaveElapsed = 0.f;
    TArray<TWeakObjectPtr<AADVehiclePawn>> Vehicles;
    TArray<FStreetLight> StreetLights;
    TArray<FSurfaceBinding> SurfaceBindings;
    TArray<FEmissiveSurface> EmissiveSurfaces;
    TArray<FVector> RainPositions;
    TArray<FTransform> RainTransforms;
    TArray<EADWeather> WeatherSequence;
    FRandomStream RainRandom;
    FString LoadError;

    FRotator OriginalSunRotation;
    FLinearColor OriginalSunColor, OriginalSkyColor, OriginalFogColor, OriginalSkyLuminance;
    float OriginalSunIntensity = 4.f;
    float OriginalSkyIntensity = .7f;
    float OriginalFogDensity = .018f;
    float OriginalFogStart = 4000.f;
    float OriginalFogOpacity = .55f;
    float OriginalExposureBias = 0.f;
    bool bOriginalExposureOverride = false;
    float OriginalRoadRoughness = .27f;
    FLinearColor OriginalRoadColor = FLinearColor(.022f,.028f,.034f);

    double Hour = 23.;
    double DayLengthSeconds = 1800.;
    double WeatherIntervalSeconds = 240.;
    double WeatherElapsed = 0.;
    float TransitionSeconds = 18.f;
    float WettingSeconds = 50.f;
    float DryingSeconds = 120.f;
    float Wetness = 0.f;
    float RainAmount = 0.f;
    float FogAmount = 0.f;
    float VisualElapsed = 0.f;
    float RainRadiusCm = 1600.f;
    float RainHeightCm = 1400.f;
    float RainSpeedCmPerSecond = 1400.f;
    float DaySunIntensity = 24000.f;
    float DaySkyIntensity = 1100.f;
    float DayExposureBias = -10.f;
    int32 RainCount = 192;
    int32 WeatherIndex = 0;
    EADWeather Weather = EADWeather::Clear;
    bool bAutomaticWeather = true;
    bool bReady = false;
    bool bFrozen = false;
    bool bRainEnabled = true;
    bool bRainActiveLogged = false;
    bool bRainPositioned = false;
    bool bDistrictCached = false;
};
