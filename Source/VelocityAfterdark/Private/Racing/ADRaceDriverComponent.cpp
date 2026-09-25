#include "Racing/ADRaceDriverComponent.h"

#include "Core/ADVehicleMath.h"
#include "Racing/ADRaceDefinition.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

namespace
{
    constexpr double MaxLaneOffsetCm = 300.;
    constexpr double SenseIntervalSeconds = .1;
    constexpr double BrakingDecelerationMps2 = 3.5;
    constexpr double MinimumLaneSeparationCm = 250.;
}

UADRaceDriverComponent::UADRaceDriverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UADRaceDriverComponent::Initialize(AADVehiclePawn* Car, const FADRaceDefinition* Route,
    float SpeedScale, float LaneOffsetCm)
{
    SetDriving(false);
    if (Physics.IsValid()) Physics->RemoveTickPrerequisiteComponent(this);
    Vehicle = nullptr;
    Physics = nullptr;
    Definition = nullptr;
    Competitors.Reset();
    if (!IsValid(Car) || !Car->HasAuthority() || !Car->GetPhysics() || !Car->GetPhysics()->IsReady() ||
        !Route || Route->RoutePoints.Num() < 3 || Route->RoutePoints.Num() != Route->RouteDistancesM.Num() ||
        !FMath::IsFinite(Route->RouteLengthM) || Route->RouteLengthM < 50. ||
        !FMath::IsFinite(SpeedScale) || SpeedScale < .4f || SpeedScale > 1.3f ||
        !FMath::IsFinite(LaneOffsetCm)) return false;

    const FADVehicleDefinition& CarDefinition = Car->GetPhysics()->GetDefinition();
    if (CarDefinition.WheelAnchorsCm.Num() != 4) return false;
    for (int32 Index = 0; Index < Route->RoutePoints.Num(); ++Index)
    {
        const FADRaceRoutePoint& Point = Route->RoutePoints[Index];
        const double DistanceM = Route->RouteDistancesM[Index];
        if (Point.Position.ContainsNaN() || !FMath::IsFinite(Point.SpeedKmh) || Point.SpeedKmh < 10. ||
            Point.SpeedKmh > 200. || !FMath::IsFinite(DistanceM) || DistanceM < 0. || DistanceM >= Route->RouteLengthM ||
            (Index > 0 && DistanceM <= Route->RouteDistancesM[Index - 1])) return false;
    }
    Vehicle = Car;
    Physics = Car->GetPhysics();
    Definition = Route;
    DriverSpeedScale = SpeedScale;
    BaseLaneOffsetCm = FMath::Clamp(static_cast<double>(LaneOffsetCm), -MaxLaneOffsetCm, MaxLaneOffsetCm);
    CruiseSpeedMps = 0.;
    for (const FADRaceRoutePoint& Point : Definition->RoutePoints)
        CruiseSpeedMps = FMath::Max(CruiseSpeedMps, Point.SpeedKmh / 3.6);
    if (const UBoxComponent* Chassis = Cast<UBoxComponent>(Car->GetRootComponent()))
    {
        VehicleHalfLengthCm = Chassis->GetScaledBoxExtent().X;
        VehicleHalfWidthCm = Chassis->GetScaledBoxExtent().Y;
    }
    ObstacleQuery = FCollisionQueryParams(SCENE_QUERY_STAT(ADRaceObstacle), false, Car);
    ObstacleQuery.bReturnPhysicalMaterial = false;
    Physics->AddTickPrerequisiteComponent(this);
    if (!Physics->GetTelemetry().bAutomaticTransmission) Physics->ToggleTransmission();
    RecoveryAttemptCount = AvoidanceCount = OvertakeCount = 0;
    ResetDriver();
    return true;
}

void UADRaceDriverComponent::SetCompetitors(const TArray<AADVehiclePawn*>& Cars)
{
    Competitors.Reset(Cars.Num());
    ObstacleQuery = FCollisionQueryParams(SCENE_QUERY_STAT(ADRaceObstacle), false, Vehicle.Get());
    for (AADVehiclePawn* Car : Cars)
    {
        if (!IsValid(Car) || Car == Vehicle.Get()) continue;
        Competitors.AddUnique(Car);
        // Moving competitors have a relative-speed following model. Treating
        // them as static obstacles would brake a flowing pack to a standstill.
        ObstacleQuery.AddIgnoredActor(Car);
    }
}

void UADRaceDriverComponent::SetDriving(bool bEnabled)
{
    bDriving = bEnabled && Vehicle.IsValid() && Physics.IsValid() && Definition && !bNeedsRecovery;
    SetComponentTickEnabled(bDriving);
    if (!bDriving && Physics.IsValid()) Physics->SetControls(0.f, 1.f, 0.f, false);
}

void UADRaceDriverComponent::ResetDriver()
{
    bRunOut=false;
    RunOutRemainingM=RunOutSeconds=0.;
    PassingVehicle.Reset();
    RecoveryState = ERecoveryState::Driving;
    DesiredLaneOffsetCm = CurrentLaneOffsetCm = BaseLaneOffsetCm;
    if (Vehicle.IsValid() && Definition)
    {
        // Hold the actual starting lane until sensing confirms a safe merge.
        // Immediately aiming at the preferred lane can cut across the grid.
        double ErrorM=0.;
        const FVector Position=Vehicle->GetActorLocation();
        const double ProgressM=Definition->ClosestDistanceM(Position,ErrorM);
        CurrentLaneOffsetCm=FMath::Clamp(FVector2D::DotProduct(FVector2D(Position.X,Position.Y)-
            Definition->PointAtDistance(ProgressM),RouteRightAt(ProgressM)),-MaxLaneOffsetCm,MaxLaneOffsetCm);
        DesiredLaneOffsetCm=CurrentLaneOffsetCm;
    }
    FollowingSpeedLimitMps = ObstacleClearanceM = 100.;
    TargetSpeedMps = RouteErrorM = SenseCountdown = 0.;
    StuckSeconds = OffRouteSeconds = OverturnedSeconds = HealthyDrivingSeconds = RecoverySeconds = 0.f;
    RecoverySteering = 0.f;
    ConsecutiveRecoveryAttempts = 0;
    bNeedsRecovery = false;
    bWasAvoiding = false;
    // Lifetime counters intentionally survive a managed reset for honest results.
    if (Physics.IsValid()) Physics->SetControls(0.f, 1.f, 0.f, false);
}

void UADRaceDriverComponent::BeginRunOut(double DistanceM)
{
    if (!Vehicle.IsValid() || !FMath::IsFinite(DistanceM)) { SetDriving(false); return; }
    // Continue through the timing line under normal physics. Parking at the
    // line can strand a following car before its final scoring-plane crossing.
    bRunOut=true;
    RunOutRemainingM=FMath::Clamp(DistanceM,60.,250.);
    RunOutSeconds=0.;
    RunOutLastPosition=Vehicle->GetActorLocation();
}

bool UADRaceDriverComponent::IsReversing() const
{
    return RecoveryState != ERecoveryState::Driving;
}

void UADRaceDriverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SetDriving(false);
    if (Physics.IsValid()) Physics->RemoveTickPrerequisiteComponent(this);
    Definition = nullptr;
    Super::EndPlay(EndPlayReason);
}

double UADRaceDriverComponent::SignedRouteGapM(double OtherProgressM, double OwnProgressM) const
{
    double Gap = FMath::Fmod(OtherProgressM - OwnProgressM + Definition->RouteLengthM, Definition->RouteLengthM);
    if (Gap > Definition->RouteLengthM * .5) Gap -= Definition->RouteLengthM;
    return Gap;
}

FVector2D UADRaceDriverComponent::RouteRightAt(double ProgressM) const
{
    const FVector2D Tangent = (Definition->PointAtDistance(ProgressM + 1.) -
        Definition->PointAtDistance(ProgressM - 1.)).GetSafeNormal();
    return FVector2D(-Tangent.Y, Tangent.X);
}

double UADRaceDriverComponent::SweepClearanceM(const FVector& Start, const FVector& Direction, double LengthM) const
{
    FHitResult Hit;
    const FVector End = Start + Direction * LengthM * 100.;
    const FQuat Orientation = Direction.Rotation().Quaternion();
    // A shallow box at chassis height sees cars and barriers without treating the
    // road as an obstacle. The physical chassis and suspension still handle curbs.
    const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, Orientation, ECC_Visibility,
        FCollisionShape::MakeBox(FVector(20., VehicleHalfWidthCm + 20., 18.)), ObstacleQuery);
    return bHit ? FMath::Max(0., Hit.Distance * .01) : LengthM;
}

bool UADRaceDriverComponent::IsLaneClear(double OffsetCm, double ProgressM) const
{
    for (const TWeakObjectPtr<AADVehiclePawn>& Other : Competitors)
    {
        if (!Other.IsValid()) continue;
        double OtherErrorM = 0.;
        const FVector Position = Other->GetActorLocation();
        const double OtherProgress = Definition->ClosestDistanceM(Position, OtherErrorM);
        const double Gap = SignedRouteGapM(OtherProgress, ProgressM);
        if (OtherErrorM > 12. || Gap < -15. || Gap > 45.) continue;
        const FVector2D Center = Definition->PointAtDistance(OtherProgress);
        const double OtherOffset = FVector2D::DotProduct(FVector2D(Position.X, Position.Y) - Center,
            RouteRightAt(OtherProgress));
        if (FMath::Abs(OtherOffset - OffsetCm) < MinimumLaneSeparationCm) return false;
    }
    return true;
}

void UADRaceDriverComponent::Sense(double ProgressM, double SpeedMps, double DesiredSpeedMps)
{
    const FVector Position = Vehicle->GetActorLocation();
    const FVector Forward = Vehicle->GetActorForwardVector().GetSafeNormal2D();
    const FVector2D Right2D = RouteRightAt(ProgressM);
    const FVector Right(Right2D.X, Right2D.Y, 0.);
    const FVector2D Center = Definition->PointAtDistance(ProgressM);
    const double OwnOffset = FVector2D::DotProduct(FVector2D(Position.X, Position.Y) - Center, Right2D);
    const FVector Nose = Position + Forward * (VehicleHalfLengthCm + 25.);
    const double SenseLengthM = FMath::Clamp(5. + SpeedMps * 1.5, 8., 42.);
    const FVector2D RouteAim = Definition->PointAtDistance(ProgressM + SenseLengthM);
    const FVector Direction = (FVector(RouteAim.X, RouteAim.Y, Nose.Z) + Right * CurrentLaneOffsetCm - Nose).GetSafeNormal();
    const double CenterClearance = SweepClearanceM(Nose, Direction, SenseLengthM);
    ObstacleClearanceM = CenterClearance < SenseLengthM - .01 ? CenterClearance : 100.;
    const double LeftClearance = SweepClearanceM(Nose + Right * (-MaxLaneOffsetCm - OwnOffset), Direction, SenseLengthM);
    const double RightClearance = SweepClearanceM(Nose + Right * (MaxLaneOffsetCm - OwnOffset), Direction, SenseLengthM);
    FollowingSpeedLimitMps = 100.;
    AADVehiclePawn* Leader = nullptr;
    double LeaderGapM = 100.;
    double LeaderSpeedMps = 0.;
    for (const TWeakObjectPtr<AADVehiclePawn>& Other : Competitors)
    {
        if (!Other.IsValid()) continue;
        double OtherErrorM = 0.;
        const FVector OtherPosition = Other->GetActorLocation();
        const double OtherProgress = Definition->ClosestDistanceM(OtherPosition, OtherErrorM);
        const double Gap = SignedRouteGapM(OtherProgress, ProgressM);
        if (OtherErrorM > 12. || Gap < 0. || Gap > 50.) continue;
        const double OtherOffset = FVector2D::DotProduct(FVector2D(OtherPosition.X, OtherPosition.Y) -
            Definition->PointAtDistance(OtherProgress), RouteRightAt(OtherProgress));
        if (FMath::Abs(OtherOffset - OwnOffset) > MinimumLaneSeparationCm) continue;
        if (Gap < LeaderGapM)
        {
            Leader = Other.Get();
            LeaderGapM = Gap;
            LeaderSpeedMps = FMath::Max(0., FVector::DotProduct(Other->GetVelocity(), Forward) * .01);
        }
    }

    if (PassingVehicle.IsValid())
    {
        double IgnoredError = 0.;
        const double Gap = SignedRouteGapM(Definition->ClosestDistanceM(PassingVehicle->GetActorLocation(), IgnoredError), ProgressM);
        if (Gap < -8.)
        {
            ++OvertakeCount;
            PassingVehicle.Reset();
            // Wait until the original lane is clear before merging back.
        }
    }
    if (!PassingVehicle.IsValid() && IsLaneClear(BaseLaneOffsetCm, ProgressM)) DesiredLaneOffsetCm = BaseLaneOffsetCm;

    if (Leader)
    {
        const double BumperGapM = FMath::Max(0., LeaderGapM - VehicleHalfLengthCm * .02);
        const double DesiredGapM = 3. + SpeedMps * .75;
        FollowingSpeedLimitMps = FMath::Max(0., LeaderSpeedMps + (BumperGapM - DesiredGapM) * .6);
        if (BumperGapM < 2.) FollowingSpeedLimitMps = 0.;
        if (!PassingVehicle.IsValid() && LeaderGapM < 45. && DesiredSpeedMps > LeaderSpeedMps + .7)
        {
            const double CandidateA = BaseLaneOffsetCm <= 0. ? -MaxLaneOffsetCm : MaxLaneOffsetCm;
            const double CandidateB = -CandidateA;
            const auto CandidateClear = [&](double Candidate)
            {
                const double Clearance = Candidate < OwnOffset ? LeftClearance : RightClearance;
                return FMath::Abs(Candidate - OwnOffset) > 150. && Clearance > FMath::Min(15., SenseLengthM * .8) &&
                    IsLaneClear(Candidate, ProgressM);
            };
            if (CandidateClear(CandidateA) || CandidateClear(CandidateB))
            {
                DesiredLaneOffsetCm = CandidateClear(CandidateA) ? CandidateA : CandidateB;
                PassingVehicle = Leader;
            }
        }
    }
    else if (ObstacleClearanceM < SenseLengthM * .7 && !PassingVehicle.IsValid())
    {
        // A static obstacle can be passed only when an entire neighboring lane
        // is sensed clear. Otherwise this driver stops and attempts recovery.
        if (LeftClearance > SenseLengthM * .9 && IsLaneClear(-MaxLaneOffsetCm, ProgressM))
            DesiredLaneOffsetCm = -MaxLaneOffsetCm;
        else if (RightClearance > SenseLengthM * .9 && IsLaneClear(MaxLaneOffsetCm, ProgressM))
            DesiredLaneOffsetCm = MaxLaneOffsetCm;
    }
    const bool bAvoiding = FollowingSpeedLimitMps < DesiredSpeedMps || ObstacleClearanceM < SenseLengthM * .8;
    if (bAvoiding && !bWasAvoiding) ++AvoidanceCount;
    bWasAvoiding = bAvoiding;
}

void UADRaceDriverComponent::BeginRecovery(float ForwardSteering)
{
    if (ConsecutiveRecoveryAttempts >= 2) { RequestManagedRecovery(); return; }
    ++ConsecutiveRecoveryAttempts;
    ++RecoveryAttemptCount;
    RecoveryState = ERecoveryState::BrakeForReverse;
    RecoverySeconds = 0.f;
    RecoverySteering = -FMath::Clamp(ForwardSteering, -.7f, .7f);
    PassingVehicle.Reset();
    Physics->SetControls(0.f, 1.f, 0.f, false);
}

void UADRaceDriverComponent::RequestManagedRecovery()
{
    bNeedsRecovery = true;
    SetDriving(false);
}

void UADRaceDriverComponent::TickRecovery(float DeltaTime, double SpeedMps)
{
    RecoverySeconds += DeltaTime;
    switch (RecoveryState)
    {
    case ERecoveryState::BrakeForReverse:
        Physics->SetControls(0.f, 1.f, 0.f, false);
        if (SpeedMps < .3)
        {
            if (Physics->GetTelemetry().Gear != -1) Physics->RequestReverse();
            if (Physics->GetTelemetry().Gear == -1)
            {
                RecoveryState = ERecoveryState::Reverse;
                RecoverySeconds = 0.f;
                ReverseStart = Vehicle->GetActorLocation();
            }
        }
        if (RecoverySeconds > 3.f) RequestManagedRecovery();
        break;
    case ERecoveryState::Reverse:
    {
        const FVector Back = -Vehicle->GetActorForwardVector().GetSafeNormal2D();
        const FVector Tail = Vehicle->GetActorLocation() + Back * (VehicleHalfLengthCm + 25.);
        // Recovery lasts at most 1.6 seconds; one short rear sweep prevents the
        // reverse maneuver from blindly pushing into another stopped vehicle.
        bool bRearBlocked = SweepClearanceM(Tail, Back, 2.) < .8;
        // Known racers are omitted from the static sweeps; include them in the
        // rear safety check as physical extents before applying reverse torque.
        for (const TWeakObjectPtr<AADVehiclePawn>& Other : Competitors)
        {
            if (!Other.IsValid()) continue;
            const FVector Relative = Vehicle->GetActorTransform().InverseTransformVectorNoScale(
                Other->GetActorLocation() - Vehicle->GetActorLocation());
            if (Relative.X < 0. && Relative.X > -(VehicleHalfLengthCm * 2. + 180.) &&
                FMath::Abs(Relative.Y) < VehicleHalfWidthCm * 2. + 70.) bRearBlocked = true;
        }
        if (RecoverySeconds >= 1.6f || FVector::DistSquared(ReverseStart, Vehicle->GetActorLocation()) > 90000. || bRearBlocked)
        {
            RecoveryState = ERecoveryState::BrakeForForward;
            RecoverySeconds = 0.f;
            Physics->SetControls(0.f, 1.f, 0.f, false);
        }
        else Physics->SetControls(SpeedMps < 2.5 ? .35f : 0.f, SpeedMps > 3. ? .5f : 0.f, RecoverySteering, false);
        break;
    }
    case ERecoveryState::BrakeForForward:
        Physics->SetControls(0.f, 1.f, 0.f, false);
        if (SpeedMps < .3)
        {
            if (Physics->GetTelemetry().Gear == -1) Physics->ShiftUp();
            if (Physics->GetTelemetry().Gear > 0)
            {
                RecoveryState = ERecoveryState::Driving;
                RecoverySeconds = StuckSeconds = OffRouteSeconds = 0.f;
                SenseCountdown = 0.;
            }
        }
        if (RecoverySeconds > 3.f) RequestManagedRecovery();
        break;
    default:
        break;
    }
}

void UADRaceDriverComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bDriving) return;
    if (!Vehicle.IsValid() || !Physics.IsValid() || !Definition || !Vehicle->HasAuthority() ||
        !Physics->IsReady() || !Vehicle->IsDrivingEnabled()) { SetDriving(false); return; }
    if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
    const float Dt = FMath::Min(DeltaTime, .1f);
    const FVector Position = Vehicle->GetActorLocation();
    const double SpeedMps = Vehicle->GetVelocity().Size() * .01;
    if (Position.ContainsNaN() || !FMath::IsFinite(SpeedMps)) { RequestManagedRecovery(); return; }
    if (bRunOut)
    {
        RunOutRemainingM-=FVector::Dist2D(Position,RunOutLastPosition)*.01;
        RunOutLastPosition=Position;
        RunOutSeconds+=Dt;
        if (RunOutRemainingM<=0. || RunOutSeconds>20.) { SetDriving(false); return; }
    }
    OverturnedSeconds = Vehicle->GetActorUpVector().Z < .5 ? OverturnedSeconds + Dt : 0.f;
    if (OverturnedSeconds > 1.f) { RequestManagedRecovery(); return; }
    if (RecoveryState != ERecoveryState::Driving) { TickRecovery(Dt, SpeedMps); return; }

    const FADVehicleDefinition& CarDefinition = Physics->GetDefinition();
    const double RearX = (CarDefinition.WheelAnchorsCm[2].X + CarDefinition.WheelAnchorsCm[3].X) * .5;
    const FVector RearWorld = Position + Vehicle->GetActorForwardVector() * RearX;
    const double ProgressM = Definition->ClosestDistanceM(RearWorld, RouteErrorM);
    if (!FMath::IsFinite(ProgressM) || !FMath::IsFinite(RouteErrorM) || RouteErrorM > 25.)
    { RequestManagedRecovery(); return; }
    double DesiredSpeedMps = CruiseSpeedMps * DriverSpeedScale;
    for (int32 Index = 0; Index < Definition->RoutePoints.Num(); ++Index)
    {
        const double AheadM = FMath::Fmod(Definition->RouteDistancesM[Index] - ProgressM + Definition->RouteLengthM,
            Definition->RouteLengthM);
        if (AheadM < 120.)
        {
            const double CornerSpeedMps = Definition->RoutePoints[Index].SpeedKmh / 3.6 * DriverSpeedScale;
            DesiredSpeedMps = FMath::Min(DesiredSpeedMps, FMath::Sqrt(CornerSpeedMps * CornerSpeedMps +
                2. * BrakingDecelerationMps2 * FMath::Max(0., AheadM - 6.)));
        }
    }
    SenseCountdown -= Dt;
    if (SenseCountdown <= 0.)
    {
        // Both participants in a following gap must use chassis centers. Mixing
        // our rear axle with another car's center makes abreast cars look ahead.
        double CenterErrorM=0.;
        Sense(Definition->ClosestDistanceM(Position,CenterErrorM), SpeedMps, DesiredSpeedMps);
        SenseCountdown = SenseIntervalSeconds;
    }
    CurrentLaneOffsetCm = FMath::FInterpConstantTo(CurrentLaneOffsetCm, DesiredLaneOffsetCm, Dt, 180.);
    const double LookaheadM = FMath::Clamp(6. + SpeedMps * .6, 8., 18.);
    const FVector2D Aim = Definition->PointAtDistance(ProgressM + LookaheadM) +
        RouteRightAt(ProgressM + LookaheadM) * CurrentLaneOffsetCm;
    const FVector Local = Vehicle->GetActorTransform().InverseTransformVectorNoScale(
        FVector(Aim.X, Aim.Y, RearWorld.Z) - RearWorld) * .01;
    const double WheelbaseM = (CarDefinition.WheelAnchorsCm[0].X - CarDefinition.WheelAnchorsCm[2].X) * .01;
    const double DesiredDegrees = FMath::RadiansToDegrees(FMath::Atan2(2. * WheelbaseM * Local.Y,
        FMath::Max(Local.SizeSquared2D(), 1.)));
    const double Limit = ADVehicleMath::SteeringLimitDegrees(SpeedMps, CarDefinition.MaxSteeringDegrees,
        CarDefinition.HighSpeedSteeringDegrees, CarDefinition.SteeringFalloffMps);
    const float Steering = static_cast<float>(FMath::Clamp(DesiredDegrees / FMath::Max(Limit, 1.), -1., 1.));
    TargetSpeedMps = FMath::Min(DesiredSpeedMps, FollowingSpeedLimitMps);
    if (bRunOut) TargetSpeedMps=FMath::Min(TargetSpeedMps,16.7); // At most 60 km/h after classification.
    // The collision corridor supplies a separate emergency stopping envelope.
    // Brake and engine torque remain exactly those of the vehicle definition.
    if (ObstacleClearanceM < 99.)
        TargetSpeedMps = FMath::Min(TargetSpeedMps, FMath::Sqrt(2. * BrakingDecelerationMps2 *
            FMath::Max(0., ObstacleClearanceM - 2.)));
    if (Local.X < 0.) TargetSpeedMps = FMath::Min(TargetSpeedMps, 3.);
    if (RouteErrorM > 7.) TargetSpeedMps = FMath::Min(TargetSpeedMps, 6.);
    const double SpeedError = TargetSpeedMps - SpeedMps;
    const bool bMustStop = TargetSpeedMps < .4;
    const float Throttle = bMustStop || SpeedError < -.6 ? 0.f : static_cast<float>(FMath::Clamp(.15 + SpeedError * .25, 0., 1.));
    const float Brake = bMustStop ? 1.f : static_cast<float>(FMath::Clamp(-SpeedError * .2, 0., 1.));
    Physics->SetControls(Throttle, Brake, Steering, false);

    StuckSeconds = SpeedMps < 1. && (Throttle > .2f || TargetSpeedMps < 1.) ? StuckSeconds + Dt : 0.f;
    OffRouteSeconds = RouteErrorM > 12. ? OffRouteSeconds + Dt : 0.f;
    HealthyDrivingSeconds = SpeedMps > 3. && RouteErrorM < 7. ? HealthyDrivingSeconds + Dt : 0.f;
    if (HealthyDrivingSeconds > 4.f) ConsecutiveRecoveryAttempts = 0;
    if (StuckSeconds > 3.5f || OffRouteSeconds > 2.f)
    {
        if (bRunOut) SetDriving(false); // Finished cars never need a competitive reset.
        else BeginRecovery(Steering);
    }
}
