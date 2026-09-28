#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Racing/ADRaceDefinition.h"
#include "ADTrafficManager.generated.h"

class AADVehiclePawn;
class UADRaceDriverComponent;
class AADAtmosphere;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM()
enum class EADTrafficSignalPhase : uint8
{
    Green,
    Amber,
    Red
};

/** Bounded physical road traffic. Route geometry is shared with the road circuit. */
UCLASS()
class VELOCITYAFTERDARK_API AADTrafficManager : public AActor
{
    GENERATED_BODY()
public:
    AADTrafficManager();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void BindPlayer(AADVehiclePawn* Car, AADAtmosphere* Weather);
    void SetEnabled(bool bEnabled);
    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    bool IsSignalRed() const;
    EADTrafficSignalPhase GetSignalPhase() const { return SignalPhase; }
    static EADTrafficSignalPhase GetSignalPhaseAtTime(float TimeSeconds,float Green,float Amber,float Red);
    int32 GetTrafficCount() const { return Cars.Num(); }
    int32 GetEmergencyYieldCount() const;
private:
    bool LoadSettings();
    void Populate();
    void RetireCar(int32 Index);
    bool RemoveRetiredCars(float DeltaSeconds);
    void ClearTraffic();
    void RefreshNeighbors();
    void UpdateSignalVisuals(EADTrafficSignalPhase NewPhase);
    TWeakObjectPtr<AADVehiclePawn> Player;
    TWeakObjectPtr<AADAtmosphere> Atmosphere;
    UPROPERTY(Transient) TArray<TObjectPtr<AADVehiclePawn>> Cars;
    UPROPERTY(Transient) TArray<TObjectPtr<UADRaceDriverComponent>> Drivers;
    // Negative means active. Failed cars stay frozen until the player leaves the area.
    TArray<float> RetirementSeconds;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SignalPoles;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SignalHousings;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> RedLenses;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> AmberLenses;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> GreenLenses;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RedLensMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> AmberLensMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GreenLensMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SignalOffMaterial;
    FADRaceDefinition Route;
    TArray<double> SignalsM;
    FString Error;
    int32 MaxCars = 4;
    float SpeedScale = .58f;
    float RedSeconds = 8.f;
    float GreenSeconds = 20.f;
    float AmberSeconds = 3.f;
    float SignalTime = 0.f;
    float RetryTime = 0.f;
    bool bEnabled = true;
    bool bReady = false;
    EADTrafficSignalPhase SignalPhase = EADTrafficSignalPhase::Green;
};
