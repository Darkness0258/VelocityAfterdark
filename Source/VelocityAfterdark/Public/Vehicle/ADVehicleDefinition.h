#pragma once

#include "CoreMinimal.h"
#include "ADVehicleDefinition.generated.h"

USTRUCT(BlueprintType)
struct VELOCITYAFTERDARK_API FADTorquePoint
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Rpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float TorqueNm = 0.f;
};

UENUM(BlueprintType)
enum class EADDrivetrain : uint8
{
    FrontWheelDrive,
    RearWheelDrive,
    AllWheelDrive
};

// The JSON document is authoritative. These zero defaults are deliberately not
// a second, silently working vehicle when the definition cannot be loaded.
USTRUCT(BlueprintType)
struct VELOCITYAFTERDARK_API FADVehicleDefinition
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString VehicleId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Manufacturer;
    // Optional presentation fields retain compatibility with schema-one car files.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString BodyStyle = TEXT("coupe");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float BodyLengthCm = 456.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float BodyWidthCm = 176.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EngineCylinders = 4;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EADDrivetrain Drivetrain = EADDrivetrain::RearWheelDrive;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MassKg = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float IdleRpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float RedlineRpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FADTorquePoint> TorqueCurve;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<float> GearRatios;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float ReverseGearRatio = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FinalDrive = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float DrivetrainEfficiency = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float EngineBrakingNm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float UpshiftRpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float DownshiftRpm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float ShiftTimeSeconds = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float WheelRadiusM = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float WheelInertiaKgM2 = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FVector> WheelAnchorsCm;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector CenterOfMassOffsetCm = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SuspensionRestLengthM = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SuspensionTravelM = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SpringRateNPerM = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float DampingNsPerM = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float TireFriction = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float TireLoadSensitivity = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float LateralStiffnessNPerRad = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float RollingResistance = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MaxBrakeForceN = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FrontBrakeBias = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float DragCoefficient = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FrontalAreaM2 = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float DownforceCoefficient = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MaxSteeringDegrees = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float HighSpeedSteeringDegrees = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SteeringFalloffMps = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SteeringResponsePerSecond = 0.f;

    bool LoadFromJson(const FString& AbsolutePath, FString& OutError);
    bool Validate(FString& OutError) const;
    float GetTorqueNm(float Rpm) const;
};
