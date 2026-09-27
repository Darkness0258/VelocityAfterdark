#pragma once

#include "CoreMinimal.h"

struct FADRaceRoutePoint
{
    FVector2D Position = FVector2D::ZeroVector;
    double SpeedKmh = 60.0;
};

struct FADCheckpoint
{
    FVector Location = FVector::ZeroVector;
    FVector Forward = FVector::ForwardVector;
    double HalfWidthCm = 1000.0;
    double HalfHeightCm = 200.0;
    double DistanceM = 0.0;
};

/** Decision weights only; vehicle grip, torque and braking physics stay identical. */
struct FADDriverPersonality
{
    float OvertakeAggression = .5f;
    float BrakingConservatism = .5f;
    float PressureMistakeFrequency = 0.f;
};

struct FADRaceOpponent
{
    FString Id;
    FString Name;
    FLinearColor Color = FLinearColor::White;
    float SpeedScale = 1.0f;
    float LaneOffsetCm = 0.0f;
    FADDriverPersonality Personality;
};

// Validated, immutable race content. Runtime state belongs to the race manager.
struct VELOCITYAFTERDARK_API FADRaceDefinition
{
    FString Id;
    FString Name;
    // Zero is an A-to-B point-to-point event; positive values are circuits.
    int32 Laps = 2;
    double CountdownSeconds = 3.0;
    double TimeoutSeconds = 480.0;
    double RecoveryPenaltySeconds = 5.0;
    double ParSeconds = 240.0;
    double RouteLengthM = 0.0;
    TArray<FADRaceRoutePoint> RoutePoints;
    TArray<double> RouteDistancesM;
    TArray<FADCheckpoint> Checkpoints;
    TArray<FTransform> Grid;
    TArray<FADRaceOpponent> Opponents;
    TArray<double> DifficultySpeedScales;

    // Failure preserves the last valid definition and returns a useful error.
    bool LoadFromJson(const FString& Path, FString& Error);
    FVector2D PointAtDistance(double DistanceM) const;
    double ClosestDistanceM(const FVector& Position, double& OutErrorM) const;
};
