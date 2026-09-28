#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADVehicleEffectsComponent.generated.h"
class AADVehiclePawn;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
/** Bounded nitrous and collision damage; all power still passes through tire grip. */
UCLASS()
class VELOCITYAFTERDARK_API UADVehicleEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UADVehicleEffectsComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetNitrousHeld(bool bHeld) { bNitrousHeld=bHeld; }
    void Repair();
    void ReceiveNetworkState(float Fraction,float NewHealth,bool bNewBoosting);
    float GetNitrousFraction() const { return NitrousFraction; }
    float GetHealth() const { return Health; }
    bool IsBoosting() const { return bBoosting; }
    bool IsNitrousHeld() const { return bNitrousHeld; }
    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    int32 GetImpactPresentationCount() const { return ImpactPresentationCount; }
    int32 GetImpactSparkPoolSize() const { return ImpactSparks.Num(); }
    int32 GetActiveImpactSparkCount() const;
    void PlayImpactPresentation(const FVector& Location, const FVector& SurfaceNormal, float DeltaVelocityMps);
private:
    struct FImpactSparkState
    {
        FVector Velocity = FVector::ZeroVector;
        float AgeSeconds = 0.f;
        float LifetimeSeconds = 0.f;
        float LengthCm = 0.f;
        bool bActive = false;
    };
    UFUNCTION() void OnHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse,const FHitResult& Hit);
    bool LoadDefinition();
    void CreatePresentation();
    void UpdatePresentation(float DeltaSeconds);
    void UpdateImpactSparks(float DeltaSeconds);
    void PlaceDamageMarks(const FVector& LocalPoint, const FVector& LocalNormal);
    TWeakObjectPtr<AADVehiclePawn> Car;
    UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Chassis;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Flames;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Scratches;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ImpactSparks;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> SparkMaterials;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> ExhaustLight;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> ImpactLight;
    TArray<FImpactSparkState> ImpactSparkStates;
    FString Error;
    float NitrousFraction=1.f;
    float Health=1.f;
    float DurationSeconds=5.f;
    float RechargeSeconds=22.f;
    float RechargeDelaySeconds=3.f;
    float PowerMultiplier=1.35f;
    float TimeSinceBoost=0.f;
    float ImpactDamageThresholdMps=4.f;
    float ImpactDamageScaleMps=40.f;
    float ImpactCooldownSeconds=.5f;
    float SparkThresholdMps=7.f;
    float SparkLifetimeMinSeconds=.16f;
    float SparkLifetimeMaxSeconds=.42f;
    float ImpactLightLumens=1200.f;
    float ImpactPresentationAgeSeconds=1.f;
    float ImpactFlashStrength=0.f;
    float PresentationTimeSeconds=0.f;
    int32 MaxImpactSparks=10;
    int32 NextSparkIndex=0;
    int32 ImpactPresentationCount=0;
    FLinearColor ImpactColor=FLinearColor(1.f,.28f,.055f);
    double LastHitSeconds=-1.;
    bool bNitrousHeld=false;
    bool bBoosting=false;
    bool bReady=false;
};
