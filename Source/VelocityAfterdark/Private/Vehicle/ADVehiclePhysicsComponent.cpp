#include "Vehicle/ADVehiclePhysicsComponent.h"

#include "Core/ADVehicleMath.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogADVehiclePhysics, Log, All);

UADVehiclePhysicsComponent::UADVehiclePhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UADVehiclePhysicsComponent::Initialize(UPrimitiveComponent* InChassis)
{
    bReady = false;
    Telemetry.bReady = false;
    Chassis = InChassis;
    if (!IsValid(Chassis) || !Chassis->IsSimulatingPhysics())
    {
        InitializationError = TEXT("Vehicle chassis must be a valid simulating primitive component.");
        UE_LOG(LogADVehiclePhysics, Error, TEXT("%s"), *InitializationError);
        return false;
    }
    if (!Definition.LoadFromJson(FPaths::Combine(FPaths::ProjectContentDir(), VehicleDefinitionFile), InitializationError))
    {
        UE_LOG(LogADVehiclePhysics, Error, TEXT("Driving disabled: %s"), *InitializationError);
        return false;
    }
    Chassis->SetMassOverrideInKg(NAME_None, Definition.MassKg, true);
    Chassis->SetCenterOfMass(Definition.CenterOfMassOffsetCm);
    WheelQuery = FCollisionQueryParams(SCENE_QUERY_STAT(ADVehicleWheel), false, GetOwner());
    WheelQuery.bReturnPhysicalMaterial = false;
    bReady = true;
    ResetState();
    UE_LOG(LogADVehiclePhysics, Display, TEXT("Loaded %s, %.0f kg, %d gears."), *Definition.Name, Definition.MassKg, Definition.GearRatios.Num());
    return true;
}

bool UADVehiclePhysicsComponent::ApplyGarageDefinition(const FADVehicleDefinition& Candidate, FString& OutError)
{
    return ApplyGarageVehicle(Definition,Candidate,OutError);
}

bool UADVehiclePhysicsComponent::ApplyGarageVehicle(const FADVehicleDefinition& Stock, const FADVehicleDefinition& Candidate, FString& OutError)
{
    OutError.Reset();
    if (!bReady || !IsValid(Chassis) || Chassis->IsSimulatingPhysics())
    {
        OutError = TEXT("Park and freeze the vehicle before changing its configuration.");
        return false;
    }
    if (GetNetMode()!=NM_Standalone || !Stock.Validate(OutError) || !Candidate.Validate(OutError))
    {
        if (OutError.IsEmpty()) OutError=TEXT("Vehicle ownership configurations are only available offline.");
        return false;
    }
    const auto InRange = [](float Value, float Min, float Max)
    { return FMath::IsFinite(Value) && Value >= Min && Value <= Max; };
    if (Candidate.VehicleId != Stock.VehicleId || Candidate.TorqueCurve.Num() != Stock.TorqueCurve.Num()
        || !InRange(Candidate.MassKg, 700, 3500) || !InRange(Candidate.TireFriction, .4f, 2.5f)
        || !InRange(Candidate.MaxBrakeForceN, 5000, 60000) || !InRange(Candidate.FinalDrive, 2, 6)
        || !InRange(Candidate.LateralStiffnessNPerRad, 5000, 80000)
        || !InRange(Candidate.SpringRateNPerM, 10000, 100000) || !InRange(Candidate.DampingNsPerM, 1000, 12000))
    {
        OutError = TEXT("The proposed garage configuration is invalid for this chassis.");
        return false;
    }
    for (int32 Index = 0; Index < Candidate.TorqueCurve.Num(); ++Index)
    {
        if (Candidate.TorqueCurve[Index].Rpm != Stock.TorqueCurve[Index].Rpm
            || !InRange(Candidate.TorqueCurve[Index].TorqueNm, 1, 1500))
        { OutError = TEXT("The proposed engine curve is invalid."); return false; }
    }
    // Validation above has no side effects. Structural data always comes from
    // the immutable catalog stock, never from editable customization state.
    FADVehicleDefinition Next=Stock;
    Next.TorqueCurve=Candidate.TorqueCurve;
    Next.MassKg=Candidate.MassKg;
    Next.TireFriction=Candidate.TireFriction;
    Next.MaxBrakeForceN=Candidate.MaxBrakeForceN;
    Next.FinalDrive=Candidate.FinalDrive;
    Next.LateralStiffnessNPerRad=Candidate.LateralStiffnessNPerRad;
    Next.SpringRateNPerM=Candidate.SpringRateNPerM;
    Next.DampingNsPerM=Candidate.DampingNsPerM;
    if (!Next.Validate(OutError)) return false;
    Definition=MoveTemp(Next);
    Chassis->SetMassOverrideInKg(NAME_None, Definition.MassKg, true);
    Chassis->SetCenterOfMass(Definition.CenterOfMassOffsetCm);
    ResetState();
    return true;
}

void UADVehiclePhysicsComponent::ReceiveNetworkTelemetry(const FADVehicleTelemetry& State)
{
    if (GetOwner()->HasAuthority()) return;
    Telemetry=State;
    CurrentRpm=State.Rpm;
    SteeringDegrees=State.Steering*FMath::Lerp(Definition.MaxSteeringDegrees,Definition.HighSpeedSteeringDegrees,
        FMath::Clamp(FMath::Abs(State.SpeedKmh)/3.6f/FMath::Max(1.f,Definition.SteeringFalloffMps),0.f,1.f));
}

void UADVehiclePhysicsComponent::AdvanceRemoteWheels(float DeltaSeconds)
{
    if (GetOwner()->HasAuthority() || !bReady) return;
    const float Sag=Definition.MassKg*9.81f/(4.f*Definition.SpringRateNPerM);
    const float Direction=Telemetry.Gear<0 ? -1.f : 1.f;
    const float WheelSpeedMps=Telemetry.SpeedKmh/3.6f*Direction;
    for (auto& Wheel:Wheels)
    {
        Wheel.SuspensionLengthM=Definition.SuspensionRestLengthM-(Telemetry.GroundedWheels>0 ? Sag : 0.f);
        Wheel.AngularSpeedRadPerSecond=WheelSpeedMps/FMath::Max(.1f,Definition.WheelRadiusM);
        Wheel.SpinDegrees=FMath::Fmod(Wheel.SpinDegrees+Wheel.AngularSpeedRadPerSecond*DeltaSeconds*180.f/PI,360.f);
    }
}

void UADVehiclePhysicsComponent::SetControls(float Throttle, float Brake, float Steering, bool Handbrake)
{
    ThrottleInput = static_cast<float>(ADVehicleMath::Clamp(Throttle, 0.0, 1.0));
    BrakeInput = static_cast<float>(ADVehicleMath::Clamp(Brake, 0.0, 1.0));
    SteeringInput = static_cast<float>(ADVehicleMath::Clamp(ADVehicleMath::Finite(Steering), -1.0, 1.0));
    bHandbrake = Handbrake;
}

void UADVehiclePhysicsComponent::ResetState()
{
    SetControls(0.f, 0.f, 0.f, false);
    SteeringDegrees = 0.f;
    ShiftCooldown = 0.f;
    CurrentGear = 1;
    CurrentRpm = Definition.IdleRpm;
    Telemetry = FADVehicleTelemetry();
    Telemetry.Rpm = CurrentRpm;
    Telemetry.bReady = bReady;
    Telemetry.bAutomaticTransmission = bAutomatic;
    for (FWheelState& Wheel : Wheels)
    {
        Wheel.SuspensionLengthM = Definition.SuspensionRestLengthM;
        Wheel.SpinDegrees = 0.f;
        Wheel.AngularSpeedRadPerSecond = 0.f;
    }
}

float UADVehiclePhysicsComponent::GetForwardSpeedMps() const
{
    return IsValid(Chassis) ? static_cast<float>(FVector::DotProduct(Chassis->GetPhysicsLinearVelocity(), Chassis->GetForwardVector()) * .01) : 0.f;
}

float UADVehiclePhysicsComponent::GetDrivenWheelSurfaceSpeedMps() const
{
    float SpeedSum=0.f;
    int32 DrivenWheelCount=0;
    for (int32 Index=0;Index<4;++Index)
    {
        if (GetDriveShare(Index)<=0.f) continue;
        SpeedSum+=Wheels[Index].AngularSpeedRadPerSecond*Definition.WheelRadiusM;
        ++DrivenWheelCount;
    }
    return DrivenWheelCount>0 ? SpeedSum/DrivenWheelCount : GetForwardSpeedMps();
}

#if WITH_DEV_AUTOMATION_TESTS
void UADVehiclePhysicsComponent::SeedWheelSpeedsFromChassisForAutomation()
{
    if (!IsValid(Chassis) || Definition.WheelRadiusM<=0.f) return;
    const FVector Forward=Chassis->GetForwardVector();
    const float Speed=static_cast<float>(FVector::DotProduct(Chassis->GetPhysicsLinearVelocity()*0.01,Forward));
    for (FWheelState& Wheel:Wheels) Wheel.AngularSpeedRadPerSecond=Speed/Definition.WheelRadiusM;
}
#endif

float UADVehiclePhysicsComponent::GetGearRatio() const
{
    if (CurrentGear == -1) return -Definition.ReverseGearRatio;
    return Definition.GearRatios.IsValidIndex(CurrentGear - 1) ? Definition.GearRatios[CurrentGear - 1] : 0.f;
}

void UADVehiclePhysicsComponent::ToggleTransmission()
{
    bAutomatic = !bAutomatic;
    Telemetry.bAutomaticTransmission = bAutomatic;
}

void UADVehiclePhysicsComponent::ChangeGear(int32 NewGear)
{
    if (!bReady || ShiftCooldown > 0.f || NewGear == CurrentGear) return;
    CurrentGear = NewGear;
    ShiftCooldown = Definition.ShiftTimeSeconds;
    Telemetry.Gear = CurrentGear;
}

void UADVehiclePhysicsComponent::ShiftUp()
{
    if (!bReady) return;
    if (CurrentGear == -1)
    {
        // A sideways slide must not bypass the direction-change interlock.
        if (IsValid(Chassis) && ADVehicleMath::CanReverse(Chassis->GetPhysicsLinearVelocity().Size() * .01)) ChangeGear(1);
        return;
    }
    ChangeGear(FMath::Min(CurrentGear + 1, Definition.GearRatios.Num()));
}

void UADVehiclePhysicsComponent::ShiftDown()
{
    if (!bReady || CurrentGear <= 1) return;
    const float TargetRpm = static_cast<float>(ADVehicleMath::WheelRpm(GetDrivenWheelSurfaceSpeedMps(), Definition.WheelRadiusM))
        * Definition.GearRatios[CurrentGear - 2] * Definition.FinalDrive;
    if (TargetRpm < Definition.RedlineRpm * .98f) ChangeGear(CurrentGear - 1);
}

void UADVehiclePhysicsComponent::RequestReverse()
{
    if (bReady && IsValid(Chassis)
        && ADVehicleMath::CanReverse(Chassis->GetPhysicsLinearVelocity().Size() * .01))
        ChangeGear(CurrentGear == -1 ? 1 : -1);
}

void UADVehiclePhysicsComponent::UpdateTransmission(float DeltaTime, float ForwardSpeedMps)
{
    ShiftCooldown = FMath::Max(0.f, ShiftCooldown - DeltaTime);
    const float RoadRpm = static_cast<float>(ADVehicleMath::EngineRpm(GetDrivenWheelSurfaceSpeedMps(), Definition.WheelRadiusM,
        GetGearRatio(), Definition.FinalDrive, Definition.IdleRpm, Definition.RedlineRpm));
    // A simple automatic launch clutch lets the engine rise above idle at low speed.
    const float LaunchRpm = FMath::Lerp(Definition.IdleRpm, 2200.f, ThrottleInput);
    const float TargetRpm = FMath::Max(RoadRpm, FMath::Abs(ForwardSpeedMps) < 6.f ? LaunchRpm : Definition.IdleRpm);
    CurrentRpm = static_cast<float>(ADVehicleMath::Smooth(CurrentRpm, TargetRpm, 12.0, DeltaTime));
    if (bAutomatic && CurrentGear > 0 && ShiftCooldown <= 0.f)
    {
        if (RoadRpm >= Definition.UpshiftRpm && CurrentGear < Definition.GearRatios.Num()) ShiftUp();
        else if (RoadRpm <= Definition.DownshiftRpm && CurrentGear > 1) ShiftDown();
    }
}

float UADVehiclePhysicsComponent::GetDriveShare(int32 WheelIndex) const
{
    if (Definition.Drivetrain == EADDrivetrain::AllWheelDrive) return .25f;
    if (Definition.Drivetrain == EADDrivetrain::FrontWheelDrive) return WheelIndex < 2 ? .5f : 0.f;
    return WheelIndex >= 2 ? .5f : 0.f;
}

FVector UADVehiclePhysicsComponent::GetWheelLocalPosition(int32 WheelIndex) const
{
    return Definition.WheelAnchorsCm.IsValidIndex(WheelIndex) && WheelIndex < 4
        ? Definition.WheelAnchorsCm[WheelIndex] - FVector(0, 0, Wheels[WheelIndex].SuspensionLengthM * 100.f)
        : FVector::ZeroVector;
}

float UADVehiclePhysicsComponent::GetWheelSpinDegrees(int32 WheelIndex) const
{
    return WheelIndex >= 0 && WheelIndex < 4 ? Wheels[WheelIndex].SpinDegrees : 0.f;
}

void UADVehiclePhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bReady || !IsValid(Chassis) || !GetWorld() || !Chassis->IsSimulatingPhysics()
        || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
    const float Dt = FMath::Min(DeltaTime, .1f);
    const FTransform Transform = Chassis->GetComponentTransform();
    const FVector Up = Transform.GetUnitAxis(EAxis::Z);
    const FVector Forward = Transform.GetUnitAxis(EAxis::X);
    const FVector VelocityMps = Chassis->GetPhysicsLinearVelocity() * .01;
    if (VelocityMps.ContainsNaN() || Transform.ContainsNaN())
    {
        bReady = false;
        Telemetry.bReady = false;
        InitializationError = TEXT("Vehicle rigid body produced non-finite state; reset or restart required.");
        UE_LOG(LogADVehiclePhysics, Error, TEXT("%s"), *InitializationError);
        return;
    }
    const float ForwardSpeedMps = static_cast<float>(FVector::DotProduct(VelocityMps, Forward));
    const float SpeedMps = static_cast<float>(VelocityMps.Size());
    UpdateTransmission(Dt, ForwardSpeedMps);
    const float SteeringLimit = static_cast<float>(ADVehicleMath::SteeringLimitDegrees(SpeedMps,
        Definition.MaxSteeringDegrees, Definition.HighSpeedSteeringDegrees, Definition.SteeringFalloffMps));
    SteeringDegrees = static_cast<float>(ADVehicleMath::Smooth(SteeringDegrees, SteeringInput * SteeringLimit,
        Definition.SteeringResponsePerSecond, Dt));
    const double WheelbaseM=FMath::Max(.5,static_cast<double>(Definition.WheelAnchorsCm[0].X-Definition.WheelAnchorsCm[2].X)*.01);
    const double DesiredYawRate=ADVehicleMath::DesiredYawRateRps(ForwardSpeedMps,SteeringDegrees,WheelbaseM);
    const double ActualYawRate=FVector::DotProduct(Chassis->GetPhysicsAngularVelocityInRadians(),Up);
    const float YawError=ForwardSpeedMps>4.f && bStabilityControl
        ? static_cast<float>(DesiredYawRate-ActualYawRate) : 0.f;
    const float StabilityBrakeN=static_cast<float>(ADVehicleMath::StabilityBrakeCorrectionN(YawError,0.0,3500.0,3500.0));

    const float UnclampedRoadRpm = static_cast<float>(ADVehicleMath::WheelRpm(GetDrivenWheelSurfaceSpeedMps(), Definition.WheelRadiusM))
        * FMath::Abs(GetGearRatio()) * Definition.FinalDrive;
    const float RevLimiter = UnclampedRoadRpm >= Definition.RedlineRpm ? 0.f : 1.f;
    const float ShiftTorqueScale = ShiftCooldown > 0.f ? .12f : 1.f;
    const float TorqueNm = Definition.GetTorqueNm(CurrentRpm) * ThrottleInput * ShiftTorqueScale * RevLimiter * NitrousMultiplier * DamagePowerScale;
    const float TotalDriveForceN = static_cast<float>(ADVehicleMath::DriveForceN(TorqueNm, GetGearRatio(),
        Definition.FinalDrive, Definition.DrivetrainEfficiency, Definition.WheelRadiusM));
    const float ReferenceLoadN = Definition.MassKg * static_cast<float>(ADVehicleMath::Gravity) * .25f;
    const float QuarterMassKg = Definition.MassKg * .25f;
    Telemetry.GroundedWheels = 0;
    Telemetry.Slip = 0.f;

    for (int32 Index = 0; Index < 4; ++Index)
    {
        FWheelState& Wheel = Wheels[Index];
        const FVector Anchor = Transform.TransformPosition(Definition.WheelAnchorsCm[Index]);
        const FVector End = Anchor - Up * (Definition.SuspensionRestLengthM * 100.f);
        FHitResult Hit;
        const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Anchor, End, FQuat::Identity, ECC_Visibility,
            FCollisionShape::MakeSphere(Definition.WheelRadiusM * 100.f), WheelQuery);
        const bool bContact = bHit && !Hit.bStartPenetrating && FVector::DotProduct(Hit.ImpactNormal, Up) > .35;
        Wheel.SuspensionLengthM = bContact ? FMath::Clamp(Hit.Distance * .01f,
            Definition.SuspensionRestLengthM - Definition.SuspensionTravelM, Definition.SuspensionRestLengthM)
            : Definition.SuspensionRestLengthM;
        float WheelForwardSpeedMps = ForwardSpeedMps;
        float LateralSpeedMps = 0.f;
        float SlipAngle = 0.f;
        float SuspensionN = 0.f;
        float CapacityN = 0.f;
        float LateralN = 0.f;
        FVector TireForward=Forward;
        FVector TireRight=FVector::CrossProduct(Up,Forward).GetSafeNormal();
        if (bContact)
        {
            ++Telemetry.GroundedWheels;
            FVector GroundVelocityMps = FVector::ZeroVector;
            if (UPrimitiveComponent* Ground = Hit.GetComponent(); IsValid(Ground) && Ground->IsSimulatingPhysics())
                GroundVelocityMps = Ground->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) * .01;
            const FVector AnchorVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(Anchor) * .01 - GroundVelocityMps;
            const float CompressionM = Definition.SuspensionRestLengthM - Wheel.SuspensionLengthM;
            SuspensionN = static_cast<float>(ADVehicleMath::SuspensionForceN(CompressionM,
                FVector::DotProduct(AnchorVelocity, Up), Definition.SpringRateNPerM, Definition.DampingNsPerM, ReferenceLoadN * 4.f));
            // Suspension load and tangential grip share a real contact point. Chaos
            // resolves chassis roll, pitch, weight transfer, and collision response.
            Chassis->AddForceAtLocation(Up * (SuspensionN * 100.f), Anchor);
            const FVector SteeredForward = Index < 2 ? Forward.RotateAngleAxis(SteeringDegrees, Up) : Forward;
            TireForward = FVector::VectorPlaneProject(SteeredForward, Hit.ImpactNormal).GetSafeNormal();
            TireRight = FVector::CrossProduct(Hit.ImpactNormal, TireForward).GetSafeNormal();
            const FVector ContactVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint) * .01 - GroundVelocityMps;
            WheelForwardSpeedMps = static_cast<float>(FVector::DotProduct(ContactVelocity, TireForward));
            LateralSpeedMps = static_cast<float>(FVector::DotProduct(ContactVelocity, TireRight));
            SlipAngle = static_cast<float>(ADVehicleMath::SlipAngleRadians(WheelForwardSpeedMps, LateralSpeedMps));
            CapacityN = static_cast<float>(ADVehicleMath::TireCapacityN(SuspensionN * FVector::DotProduct(Up, Hit.ImpactNormal),
                ReferenceLoadN, Definition.TireFriction * FMath::Lerp(1.f,.72f,RoadWetness), Definition.TireLoadSensitivity));
            const bool bRearHandbrake = bHandbrake && Index >= 2;
            if (bRearHandbrake) CapacityN *= .65f;
            // Limit lateral correction by available lateral momentum for low-speed
            // stability. This avoids alternating impulses near rest at low FPS.
            const float LateralStopForceN = FMath::Abs(LateralSpeedMps) * QuarterMassKg / Dt;
            LateralN = FMath::Clamp(-SlipAngle * Definition.LateralStiffnessNPerRad,
                -LateralStopForceN, LateralStopForceN);
        }

        const bool bRearHandbrake=bHandbrake && Index>=2;
        const float LateralMagnitude=FMath::Min(FMath::Abs(LateralN),CapacityN);
        const float LongitudinalCapacityN=FMath::Sqrt(FMath::Max(0.f,
            CapacityN*CapacityN-LateralMagnitude*LateralMagnitude));
        float DriveN=TotalDriveForceN*GetDriveShare(Index);
        // TCS limits driveline demand. With the assist disabled, surplus torque
        // accelerates the wheel and becomes measurable slip instead of vanishing.
        if (bTractionControl) DriveN=FMath::Clamp(DriveN,-LongitudinalCapacityN*.9f,LongitudinalCapacityN*.9f);
        const float BrakeShare=Index<2 ? Definition.FrontBrakeBias*.5f : (1.f-Definition.FrontBrakeBias)*.5f;
        float RequestedBrakeN=Definition.MaxBrakeForceN*BrakeInput*BrakeShare;
        const float WheelSide=FMath::Sign(Definition.WheelAnchorsCm[Index].Y);
        if (YawError*WheelSide>0.f) RequestedBrakeN+=FMath::Abs(StabilityBrakeN)*(Index<2 ? .3f : .7f);
        if (bRearHandbrake) RequestedBrakeN=FMath::Max(RequestedBrakeN,Definition.MaxBrakeForceN*.5f);
        if (bAntiLockBrakes && !bRearHandbrake && bContact)
            RequestedBrakeN=FMath::Min(RequestedBrakeN,LongitudinalCapacityN*.9f);
        const float EngineBrakeN=(1.f-ThrottleInput)*GetDriveShare(Index)*
            static_cast<float>(ADVehicleMath::DriveForceN(Definition.EngineBrakingNm,FMath::Abs(GetGearRatio()),
                Definition.FinalDrive,Definition.DrivetrainEfficiency,Definition.WheelRadiusM));
        const float ResistanceN=bContact ? SuspensionN*Definition.RollingResistance : 0.f;
        const float BrakeTorqueNm=(RequestedBrakeN+EngineBrakeN+ResistanceN)*Definition.WheelRadiusM;
        const ADVehicleMath::WheelTireStep TireStep=ADVehicleMath::SolveWheelTireStep(
            Wheel.AngularSpeedRadPerSecond,WheelForwardSpeedMps,DriveN*Definition.WheelRadiusM,
            BrakeTorqueNm,LongitudinalCapacityN,Definition.WheelRadiusM,Definition.WheelInertiaKgM2,Dt);
        Wheel.AngularSpeedRadPerSecond=static_cast<float>(TireStep.AngularSpeedRadPerSecond);
        const float WheelSlipRatio=static_cast<float>(ADVehicleMath::SlipRatio(
            Wheel.AngularSpeedRadPerSecond*Definition.WheelRadiusM,WheelForwardSpeedMps));
        if (bContact)
        {
            const ADVehicleMath::TireForce Forces=ADVehicleMath::FrictionCircle(TireStep.LongitudinalForceN,LateralN,CapacityN);
            const FVector TireForceN = TireForward * Forces.LongitudinalN + TireRight * Forces.LateralN;
            Chassis->AddForceAtLocation(TireForceN * 100.f, Hit.ImpactPoint);
            if (UPrimitiveComponent* Ground = Hit.GetComponent(); IsValid(Ground) && Ground->IsSimulatingPhysics())
                Ground->AddForceAtLocation(-(TireForceN + Up * SuspensionN) * 100.f, Hit.ImpactPoint);
            Telemetry.Slip = FMath::Max(Telemetry.Slip, FMath::Clamp(FMath::Abs(SlipAngle) / .45f, 0.f, 1.f));
            Telemetry.Slip = FMath::Max(Telemetry.Slip, FMath::Clamp(FMath::Abs(WheelSlipRatio) / .3f, 0.f, 1.f));
        }
        Wheel.SpinDegrees=FMath::Fmod(Wheel.SpinDegrees+Wheel.AngularSpeedRadPerSecond*Dt*(180.f/PI),360.f);
    }

    if (SpeedMps > .01f)
    {
        const float DragN = static_cast<float>(ADVehicleMath::AerodynamicDragN(SpeedMps, Definition.DragCoefficient, Definition.FrontalAreaM2));
        Chassis->AddForce(VelocityMps.GetSafeNormal() * (DragN * 100.f));
        // Aero only presses along chassis up when it is upright; no airborne
        // orientation correction or invisible yaw torque is added.
        if (FVector::DotProduct(Up, FVector::UpVector) > .5)
        {
            const float DownforceN = .5f * 1.225f * Definition.DownforceCoefficient * Definition.FrontalAreaM2 * SpeedMps * SpeedMps;
            Chassis->AddForce(-Up * (DownforceN * 100.f));
        }
    }
    Telemetry.SpeedKmh = SpeedMps * 3.6f;
    Telemetry.StabilityIntervention=FMath::Clamp(FMath::Abs(StabilityBrakeN)/3500.f,0.f,1.f);
    Telemetry.Rpm = CurrentRpm;
    Telemetry.Gear = CurrentGear;
    Telemetry.Throttle = ThrottleInput;
    Telemetry.Brake = BrakeInput;
    Telemetry.Steering = SteeringDegrees / Definition.MaxSteeringDegrees;
    Telemetry.bAutomaticTransmission = bAutomatic;
    Telemetry.bReady = bReady;
}
