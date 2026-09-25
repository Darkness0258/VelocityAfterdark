#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADVehicleEffectsComponent.generated.h"
class AADVehiclePawn;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UPointLightComponent;
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
private:
    UFUNCTION() void OnHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse,const FHitResult& Hit);
    bool LoadDefinition();
    void CreatePresentation();
    void UpdatePresentation();
    TWeakObjectPtr<AADVehiclePawn> Car;
    UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Chassis;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Flames;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Scratches;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> ExhaustLight;
    FString Error;
    float NitrousFraction=1.f;
    float Health=1.f;
    float DurationSeconds=5.f;
    float RechargeSeconds=22.f;
    float RechargeDelaySeconds=3.f;
    float PowerMultiplier=1.35f;
    float TimeSinceBoost=0.f;
    double LastHitSeconds=-1.;
    bool bNitrousHeld=false;
    bool bBoosting=false;
    bool bReady=false;
};
