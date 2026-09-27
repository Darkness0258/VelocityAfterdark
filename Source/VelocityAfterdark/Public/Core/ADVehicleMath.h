#pragma once

// Engine-independent SI calculations shared by the runtime and portable tests.
// Unreal uses centimetres; the component converts at its force/velocity boundary.
#include <cmath>
#include <cstddef>

namespace ADVehicleMath
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Gravity = 9.81;

inline double Finite(double Value, double Fallback = 0.0)
{
    return std::isfinite(Value) ? Value : Fallback;
}

inline double Clamp(double Value, double Minimum, double Maximum)
{
    Value = Finite(Value, Minimum);
    return Value < Minimum ? Minimum : (Value > Maximum ? Maximum : Value);
}

inline double Sign(double Value)
{
    return Value > 0.0 ? 1.0 : (Value < 0.0 ? -1.0 : 0.0);
}

inline double Lerp(double A, double B, double Alpha)
{
    return A + (B - A) * Clamp(Alpha, 0.0, 1.0);
}

// Analytic exponential smoothing: the response is independent of render frequency.
inline double Smooth(double Current, double Target, double ResponsePerSecond, double Dt)
{
    Current = Finite(Current);
    Target = Finite(Target);
    return Lerp(Current, Target, 1.0 - std::exp(-Clamp(ResponsePerSecond, 0.0, 1000.0) * Clamp(Dt, 0.0, 1.0)));
}

// Point must expose Rpm and TorqueNm. Definition validation guarantees ordering.
template <typename Point>
double SampleTorque(const Point* Points, std::size_t Count, double Rpm)
{
    if (!Points || Count == 0) return 0.0;
    Rpm = Finite(Rpm);
    if (Rpm <= Points[0].Rpm) return Clamp(Points[0].TorqueNm, 0.0, 5000.0);
    for (std::size_t Index = 1; Index < Count; ++Index)
    {
        if (Rpm <= Points[Index].Rpm)
        {
            const double Interval = Points[Index].Rpm - Points[Index - 1].Rpm;
            if (Interval <= 0.0) return 0.0;
            return Clamp(Lerp(Points[Index - 1].TorqueNm, Points[Index].TorqueNm,
                (Rpm - Points[Index - 1].Rpm) / Interval), 0.0, 5000.0);
        }
    }
    return Clamp(Points[Count - 1].TorqueNm, 0.0, 5000.0);
}

inline double WheelRpm(double SpeedMps, double WheelRadiusM)
{
    return std::abs(Finite(SpeedMps)) * 60.0 / (2.0 * Pi * Clamp(WheelRadiusM, 0.1, 2.0));
}

inline double EngineRpm(double SpeedMps, double WheelRadiusM, double GearRatio,
    double FinalDrive, double IdleRpm, double RedlineRpm)
{
    return Clamp(WheelRpm(SpeedMps, WheelRadiusM) * std::abs(Finite(GearRatio)) *
        Clamp(FinalDrive, 0.1, 20.0), IdleRpm, RedlineRpm);
}

inline double DriveForceN(double TorqueNm, double GearRatio, double FinalDrive,
    double Efficiency, double WheelRadiusM)
{
    return Clamp(TorqueNm, 0.0, 5000.0) * Finite(GearRatio) * Clamp(FinalDrive, 0.1, 20.0) *
        Clamp(Efficiency, 0.0, 1.0) / Clamp(WheelRadiusM, 0.1, 2.0);
}

inline double SuspensionForceN(double CompressionM, double VerticalSpeedMps,
    double SpringRate, double Damping, double MaxForceN)
{
    return Clamp(Clamp(CompressionM, 0.0, 2.0) * Clamp(SpringRate, 0.0, 1000000.0)
        - Finite(VerticalSpeedMps) * Clamp(Damping, 0.0, 100000.0), 0.0, MaxForceN);
}

inline double TireCapacityN(double NormalLoadN, double ReferenceLoadN, double Friction,
    double LoadSensitivity)
{
    NormalLoadN = Clamp(NormalLoadN, 0.0, 1000000.0);
    if (NormalLoadN <= 0.0) return 0.0;
    const double LoadRatio = NormalLoadN / Clamp(ReferenceLoadN, 1.0, 1000000.0);
    const double LoadFactor = std::pow(Clamp(LoadRatio, 0.1, 10.0), -Clamp(LoadSensitivity, 0.0, 0.5));
    return NormalLoadN * Clamp(Friction, 0.0, 4.0) * LoadFactor;
}

struct TireForce
{
    double LongitudinalN;
    double LateralN;
};

inline TireForce FrictionCircle(double LongitudinalN, double LateralN, double CapacityN)
{
    LongitudinalN = Finite(LongitudinalN);
    LateralN = Finite(LateralN);
    CapacityN = Clamp(CapacityN, 0.0, 1000000.0);
    // hypot avoids overflow when squaring unexpectedly large inputs.
    const double Magnitude = std::hypot(LongitudinalN, LateralN);
    const double Scale = Magnitude > CapacityN && Magnitude > 0.0 ? CapacityN / Magnitude : 1.0;
    return {LongitudinalN * Scale, LateralN * Scale};
}

// Clamp braking to momentum available this frame so braking cannot launch a car
// backward at a standstill. A quarter-car effective mass is supplied per wheel.
inline double BrakeForceN(double WheelSpeedMps, double RequestedForceN, double EffectiveMassKg, double Dt)
{
    Dt = Clamp(Dt, 0.001, 0.1);
    const double StopForce = std::abs(Finite(WheelSpeedMps)) * Clamp(EffectiveMassKg, 0.0, 10000.0) / Dt;
    return -Sign(WheelSpeedMps) * Clamp(RequestedForceN, 0.0, StopForce);
}

inline double SlipAngleRadians(double ForwardSpeedMps, double LateralSpeedMps)
{
    return std::atan2(Finite(LateralSpeedMps), std::fmax(std::abs(Finite(ForwardSpeedMps)), 2.0));
}

inline double SlipRatio(double WheelSurfaceSpeedMps, double GroundSpeedMps)
{
    WheelSurfaceSpeedMps = Finite(WheelSurfaceSpeedMps);
    GroundSpeedMps = Finite(GroundSpeedMps);
    const double Denominator = std::fmax(2.0,
        std::fmax(std::abs(WheelSurfaceSpeedMps), std::abs(GroundSpeedMps)));
    return Clamp((WheelSurfaceSpeedMps - GroundSpeedMps) / Denominator, -3.0, 3.0);
}

inline double LongitudinalTireForceN(double Slip, double CapacityN, double Stiffness = 8.0)
{
    return Clamp(CapacityN, 0.0, 1000000.0)
        * std::tanh(Clamp(Slip, -3.0, 3.0) * Clamp(Stiffness, 0.1, 40.0));
}

struct WheelTireStep
{
    double AngularSpeedRadPerSecond;
    double LongitudinalForceN;
};

// Implicit wheel/tire coupling stays stable at low speed and at 30 Hz even when
// a wheel is locked or spinning. The tire's monotone slip curve makes this
// bounded bisection cheap and deterministic, without frame-dependent impulses.
inline WheelTireStep SolveWheelTireStep(double AngularSpeedRadPerSecond, double GroundSpeedMps,
    double DriveTorqueNm, double BrakeTorqueNm, double LongitudinalCapacityN,
    double WheelRadiusM, double WheelInertiaKgM2, double Dt, double SlipStiffness = 8.0)
{
    AngularSpeedRadPerSecond = Finite(AngularSpeedRadPerSecond);
    GroundSpeedMps = Finite(GroundSpeedMps);
    DriveTorqueNm = Finite(DriveTorqueNm);
    BrakeTorqueNm = Clamp(BrakeTorqueNm, 0.0, 1000000.0);
    LongitudinalCapacityN = Clamp(LongitudinalCapacityN, 0.0, 1000000.0);
    WheelRadiusM = Clamp(WheelRadiusM, 0.1, 2.0);
    WheelInertiaKgM2 = Clamp(WheelInertiaKgM2, 0.05, 20.0);
    Dt = Clamp(Dt, 0.001, 0.1);

    const double BrakeReference = std::abs(AngularSpeedRadPerSecond) > 0.5
        ? AngularSpeedRadPerSecond : (std::abs(GroundSpeedMps) > 0.5 ? GroundSpeedMps : DriveTorqueNm);
    const double AppliedBrakeTorque = Sign(BrakeReference) * BrakeTorqueNm;
    const double NetDrivelineTorque = DriveTorqueNm - AppliedBrakeTorque;
    const double FreeAngularSpeed = AngularSpeedRadPerSecond + NetDrivelineTorque * Dt / WheelInertiaKgM2;
    const double TireAngularDelta = LongitudinalCapacityN * WheelRadiusM * Dt / WheelInertiaKgM2;
    double Low = FreeAngularSpeed - TireAngularDelta;
    double High = FreeAngularSpeed + TireAngularDelta;

    for (int Iteration = 0; Iteration < 18; ++Iteration)
    {
        const double Candidate = (Low + High) * 0.5;
        const double Slip = SlipRatio(Candidate * WheelRadiusM, GroundSpeedMps);
        const double TireForce = LongitudinalTireForceN(Slip, LongitudinalCapacityN, SlipStiffness);
        const double Residual = Candidate - AngularSpeedRadPerSecond
            - (NetDrivelineTorque - TireForce * WheelRadiusM) * Dt / WheelInertiaKgM2;
        if (Residual > 0.0) High = Candidate;
        else Low = Candidate;
    }

    const double ResultSpeed = (Low + High) * 0.5;
    const double ResultSlip = SlipRatio(ResultSpeed * WheelRadiusM, GroundSpeedMps);
    return {ResultSpeed, LongitudinalTireForceN(ResultSlip, LongitudinalCapacityN, SlipStiffness)};
}

inline double SteeringLimitDegrees(double SpeedMps, double LowSpeedDegrees,
    double HighSpeedDegrees, double FalloffMps)
{
    const double Ratio = std::abs(Finite(SpeedMps)) / Clamp(FalloffMps, 1.0, 100.0);
    return Lerp(HighSpeedDegrees, LowSpeedDegrees, 1.0 / (1.0 + Ratio * Ratio));
}

inline double DesiredYawRateRps(double ForwardSpeedMps,double SteeringDegrees,double WheelbaseM)
{
    if (!std::isfinite(ForwardSpeedMps) || !std::isfinite(SteeringDegrees)
        || !std::isfinite(WheelbaseM) || WheelbaseM<0.5) return 0.0;
    const double SteeringRadians=Clamp(SteeringDegrees,-45.0,45.0)*3.14159265358979323846/180.0;
    return Clamp(ForwardSpeedMps,-100.0,100.0)*std::tan(SteeringRadians)/WheelbaseM;
}

inline double StabilityBrakeCorrectionN(double DesiredYawRate,double ActualYawRate,
    double GainNPerRps=3500.0,double MaximumForceN=3500.0)
{
    if (!std::isfinite(DesiredYawRate) || !std::isfinite(ActualYawRate)) return 0.0;
    return Clamp((DesiredYawRate-ActualYawRate)*Clamp(GainNPerRps,0.0,10000.0),
        -Clamp(MaximumForceN,0.0,10000.0),Clamp(MaximumForceN,0.0,10000.0));
}

inline double AerodynamicDragN(double SpeedMps, double Cd, double FrontalAreaM2)
{
    const double Speed = Clamp(SpeedMps, -200.0, 200.0);
    return -0.5 * 1.225 * Clamp(Cd, 0.0, 3.0) * Clamp(FrontalAreaM2, 0.0, 20.0) * Speed * std::abs(Speed);
}

inline bool CanReverse(double SpeedMps)
{
    return std::isfinite(SpeedMps) && std::abs(SpeedMps) < 1.5;
}
}
