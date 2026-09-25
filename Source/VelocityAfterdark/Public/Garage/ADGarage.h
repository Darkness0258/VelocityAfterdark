#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ADGarage.generated.h"

class AADVehiclePawn;
class AADAtmosphere;
class UCameraComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class USpotLightComponent;

/** A small, isolated workshop stage. Ownership and menu actions belong to the controller. */
UCLASS()
class VELOCITYAFTERDARK_API AADGarage : public AActor
{
    GENERATED_BODY()

public:
    AADGarage();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // Caller must prevent entry during a race and return its view target on exit.
    bool Enter(AADVehiclePawn* Car, FString& OutError);
    void Exit();
    bool IsOccupied() const { return Occupant.IsValid(); }
    void Orbit(float YawDelta, float PitchDelta);
    // Positive distances move the camera outward; units are centimeters.
    void Zoom(float Delta);
    FTransform GetDisplayTransform() const;

private:
    struct FStudioLight
    {
        TWeakObjectPtr<USpotLightComponent> Component;
        float BaseLumens = 0.f;
    };
    bool BuildStage();
    void UpdateCamera(float DeltaSeconds);
    void UpdateLightingExposure();
    UMaterialInstanceDynamic* CreateSurface(const TCHAR* Name, const TCHAR* Asset,
        FLinearColor Color, float Roughness, float Metallic, float Emission = 0.f);
    void AddPiece(FName BatchName, UMaterialInterface* Material, FVector Position,
        FVector SizeCm, bool bCylinder = false, FRotator Rotation = FRotator::ZeroRotator);
    void AddStudioLight(FName Name, FVector Position, FVector Target,
        FLinearColor Color, float Lumens, bool bShadows);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Surfaces;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> Batches;
    TWeakObjectPtr<AADVehiclePawn> Occupant;
    TWeakObjectPtr<AADAtmosphere> Atmosphere;
    TArray<FStudioLight> StudioLights;
    FTransform ReturnTransform;
    bool bReturnDriving = false;
    bool bReturnSimulating = false;
    bool bReturnPhysicsTick = false;
    bool bReady = false;
    float TargetYaw = 37.f;
    float CurrentYaw = 37.f;
    float TargetPitch = 14.f;
    float CurrentPitch = 14.f;
    float TargetDistance = 830.f;
    float CurrentDistance = 830.f;
    float AppliedExposureBias = MAX_flt;
};
