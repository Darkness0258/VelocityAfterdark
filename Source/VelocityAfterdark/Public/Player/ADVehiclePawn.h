#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "ADVehiclePawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UADVehiclePhysicsComponent;
class UADEngineSynthComponent;
class UADVehicleEffectsComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class USpotLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM()
enum class EADCameraMode : uint8 { Chase, Hood, Cockpit };

/** Authority-owned effects state; client button intent is kept separate. */
USTRUCT()
struct FADVehicleNetworkEffects
{
    GENERATED_BODY()
    UPROPERTY() float NitrousFraction=1.f;
    UPROPERTY() float Health=1.f;
    UPROPERTY() bool bBoosting=false;
};

class AADVehiclePawn;
DECLARE_MULTICAST_DELEGATE_OneParam(FADVehicleRecoveryRequest, AADVehiclePawn*);

/** Physical pawn and visual facade. Drivetrain logic lives in the physics component. */
UCLASS()
class VELOCITYAFTERDARK_API AADVehiclePawn : public APawn
{
    GENERATED_BODY()

public:
    AADVehiclePawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void PawnClientRestart() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(Server,Unreliable) void ServerDrive(float Throttle,float Brake,float Steering,bool bHandbrake,bool bNitrous,bool bActive);
    UFUNCTION(Server,Reliable) void ServerDrivingAction(uint8 Action);

    void SetDrivingEnabled(bool bEnabled);
    void ResetVehicle();
    bool PlaceForRace(const FTransform& Transform);
    void SetRaceAppearance(FLinearColor Color, bool bOpponent);
    void SetPaintColor(FLinearColor Color);
    bool ApplyGarageVehicle(const FADVehicleDefinition& Stock, const FADVehicleDefinition& Effective, FString& OutError);
    bool PreviewGarageVehicle(const FADVehicleDefinition& Definition, FString& OutError);
    void SetGarageMode(bool bEnabled);
    bool IsInGarage() const { return bInGarage; }
    FADVehicleRecoveryRequest OnRecoveryRequested;
    void CycleCamera();
    UADVehiclePhysicsComponent* GetPhysics() const { return VehiclePhysics; }
    UADVehicleEffectsComponent* GetEffects() const { return VehicleEffects; }
    EADCameraMode GetCameraMode() const { return CameraMode; }
    bool IsDrivingEnabled() const { return bDrivingEnabled; }

private:
    void BuildVehicle();
    void BuildBody(const FString& Style, UMaterialInterface* Paint, UMaterialInterface* Glass);
    UFUNCTION() void OnRepTelemetry();
    UFUNCTION() void OnRepEffects();
    UFUNCTION() void OnRepPaint();
    void UpdatePresentation(float DeltaSeconds);
    UStaticMeshComponent* AddPiece(FName Name, FVector Location, FVector Scale,
        UMaterialInterface* Material, bool bCylinder = false, FRotator Rotation = FRotator::ZeroRotator);
    UMaterialInterface* LoadSurface(const TCHAR* Path, FLinearColor FallbackColor, float Metallic = 0.0f);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Chassis;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADVehiclePhysicsComponent> VehiclePhysics;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADVehicleEffectsComponent> VehicleEffects;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> ChaseArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> ChaseCamera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> HoodCamera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> CockpitCamera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADEngineSynthComponent> EngineAudio;
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> WheelPivots;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BrakeLenses;
    UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> BrakeLights;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FallbackMaterials;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> SteeringWheel;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RacePaint;
    FTransform RecoveryTransform;
    EADCameraMode CameraMode = EADCameraMode::Chase;
    bool bDrivingEnabled = false;
    bool bDrivingRequested = false;
    bool bRaceOpponent = false;
    bool bInGarage = false;
    UPROPERTY(ReplicatedUsing=OnRepTelemetry) FADVehicleTelemetry NetworkTelemetry;
    UPROPERTY(ReplicatedUsing=OnRepEffects) FADVehicleNetworkEffects NetworkEffects;
    UPROPERTY(ReplicatedUsing=OnRepPaint) FLinearColor NetworkPaint=FLinearColor(.055f,.58f,.4f);
    double LastRemoteInputSeconds=0.;
    double LastRemoteActionSeconds=-1.;
    double LastRemoteRecoverySeconds=-3.;
    double NetworkProbeDriveStartedSeconds=-1.;
    FVector NetworkProbeStartPosition=FVector::ZeroVector;
    bool bRemoteInputControlled=false;
    bool bNetworkProbeEnabled=false;
    bool bNetworkProbeMovementReported=false;
    float ShowcaseTime = 0.0f;
    FString PresentedBodyStyle;
};
