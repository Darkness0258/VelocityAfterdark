#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CollisionQueryParams.h"
#include "Vehicle/ADVehicleDefinition.h"
#include "ADVehiclePhysicsComponent.generated.h"

class UPrimitiveComponent;

USTRUCT(BlueprintType)
struct VELOCITYAFTERDARK_API FADVehicleTelemetry
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SpeedKmh = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Rpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Gear = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Throttle = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Brake = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Steering = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Slip = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SurfaceGripScale = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float StabilityIntervention = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 GroundedWheels = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bAutomaticTransmission = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bReady = false;
};

// Four swept wheel contacts drive one Chaos rigid body. All force calculations
// are SI. Contact sampling runs in TG_PrePhysics: engine rigid-body substeps do
// not make this tire-contact solver a fixed-frequency simulation.
UCLASS(ClassGroup=(Vehicle), meta=(BlueprintSpawnableComponent))
class VELOCITYAFTERDARK_API UADVehiclePhysicsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UADVehiclePhysicsComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintCallable) bool Initialize(UPrimitiveComponent* InChassis);
    UFUNCTION(BlueprintCallable) void SetControls(float Throttle, float Brake, float Steering, bool Handbrake);
    UFUNCTION(BlueprintCallable) void ToggleTransmission();
    UFUNCTION(BlueprintCallable) void ShiftUp();
    UFUNCTION(BlueprintCallable) void ShiftDown();
    UFUNCTION(BlueprintCallable) void RequestReverse();
    UFUNCTION(BlueprintCallable) void ResetState();
    UFUNCTION(BlueprintPure) bool IsReady() const { return bReady; }
    UFUNCTION(BlueprintPure) FString GetInitializationError() const { return InitializationError; }
    const FADVehicleTelemetry& GetTelemetry() const { return Telemetry; }
    const FADVehicleDefinition& GetDefinition() const { return Definition; }
    // Only a parked, non-simulating chassis can change physical configuration.
    bool ApplyGarageDefinition(const FADVehicleDefinition& Candidate, FString& OutError);
    // Replaces stock configuration as one transaction, retaining only supported upgrade deltas.
    bool ApplyGarageVehicle(const FADVehicleDefinition& Stock, const FADVehicleDefinition& Effective, FString& OutError);
    void SetRoadWetness(float Value) { RoadWetness = FMath::IsFinite(Value) ? FMath::Clamp(Value,0.f,1.f) : RoadWetness; }
    float GetRoadWetness() const { return RoadWetness; }
    void SetNitrousMultiplier(float Value) { NitrousMultiplier=FMath::IsFinite(Value) ? FMath::Clamp(Value,1.f,1.6f) : 1.f; }
    void SetDamagePowerScale(float Value) { DamagePowerScale=FMath::IsFinite(Value) ? FMath::Clamp(Value,.65f,1.f) : 1.f; }
    void ReceiveNetworkTelemetry(const FADVehicleTelemetry& State);
    void AdvanceRemoteWheels(float DeltaSeconds);
    FVector GetWheelLocalPosition(int32 WheelIndex) const;
    float GetWheelSpinDegrees(int32 WheelIndex) const;
    float GetSteeringDegrees() const { return SteeringDegrees; }
#if WITH_DEV_AUTOMATION_TESTS
    void SeedWheelSpeedsFromChassisForAutomation();
#endif

    UPROPERTY(EditDefaultsOnly, Category="Vehicle") FString VehicleDefinitionFile = TEXT("Data/Vehicles/aster_s6.json");
    // Slip-aware wheel inertia, demand-limited ABS/TCS and yaw-rate ESC.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assists") bool bTractionControl = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assists") bool bAntiLockBrakes = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assists") bool bStabilityControl = true;

private:
    struct FWheelState
    {
        float SuspensionLengthM = 0.f;
        float SpinDegrees = 0.f;
        float AngularSpeedRadPerSecond = 0.f;
    };

    UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Chassis;
    UPROPERTY(Transient) FADVehicleDefinition Definition;
    FADVehicleTelemetry Telemetry;
    FWheelState Wheels[4];
    FCollisionQueryParams WheelQuery;
    FString InitializationError;
    bool bReady = false;
    bool bAutomatic = true;
    bool bHandbrake = false;
    float ThrottleInput = 0.f;
    float RoadWetness = 0.f;
    float NitrousMultiplier = 1.f;
    float DamagePowerScale = 1.f;
    float BrakeInput = 0.f;
    float SteeringInput = 0.f;
    float SteeringDegrees = 0.f;
    float ShiftCooldown = 0.f;
    float CurrentRpm = 0.f;
    int32 CurrentGear = 1;

    float GetGearRatio() const;
    float GetForwardSpeedMps() const;
    float GetDrivenWheelSurfaceSpeedMps() const;
    void ChangeGear(int32 NewGear);
    void UpdateTransmission(float DeltaTime, float ForwardSpeedMps);
    float GetDriveShare(int32 WheelIndex) const;
};
