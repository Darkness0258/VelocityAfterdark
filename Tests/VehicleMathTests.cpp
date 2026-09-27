#include "Core/ADVehicleMath.h"
#include <iostream>
#include <limits>
#include <cstdlib>

namespace
{
int Checks = 0;
void Check(bool Result, const char* Name)
{
    ++Checks;
    if (!Result) { std::cerr << "FAIL: " << Name << '\n'; std::exit(1); }
}
bool Near(double A, double B, double Tolerance = 1e-8) { return std::abs(A-B) <= Tolerance; }
struct TorquePoint { double Rpm; double TorqueNm; };
}

int main()
{
    using namespace ADVehicleMath;
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    Check(Finite(NaN, 7) == 7, "nonfinite input uses safe fallback");
    Check(Clamp(2, 0, 1) == 1 && Clamp(-1, 0, 1) == 0, "pedal clamping");
    const TorquePoint Curve[] = {{1000, 100}, {3000, 300}, {6000, 240}};
    Check(Near(SampleTorque(Curve, 3, 2000), 200), "torque interpolates in physical units");
    Check(Near(SampleTorque(Curve, 3, 4500), 270), "descending torque interpolates");
    Check(SampleTorque(Curve, 3, 0) == 100 && SampleTorque(Curve, 3, 9000) == 240,
        "torque endpoints remain bounded");
    Check(SampleTorque<TorquePoint>(nullptr, 0, 4000) == 0, "empty torque curve fails safe");
    Check(Near(WheelRpm(2*Pi*.34, .34), 60), "one wheel revolution per second is 60 RPM");
    Check(Near(SlipRatio(20,20),0), "a freely rolling wheel has zero longitudinal slip");
    Check(SlipRatio(22,20)>0 && SlipRatio(18,20)<0, "wheelspin and braking slip retain opposite signs");
    Check(LongitudinalTireForceN(0,4200)==0, "a tire at free-rolling slip has no longitudinal demand");
    Check(LongitudinalTireForceN(.2,4200)>0 && LongitudinalTireForceN(-.2,4200)<0,
        "longitudinal tire force follows driven and braking slip");
    Check(std::abs(LongitudinalTireForceN(3,4200))<=4200,
        "longitudinal slip force remains inside the tire capacity");
    Check(Near(DriveForceN(300, 3, 4, .9, .3), 10800), "torque converts to tire force");
    Check(Near(DriveForceN(300, -3, 4, .9, .3), -10800), "reverse torque sign");
    const double InitialWheelOmega=20./.34;
    auto RollDrivenWheel=[&](double Step)
    {
        double Omega=InitialWheelOmega;
        const int Count=static_cast<int>(std::round(1.0/Step));
        for (int Index=0;Index<Count;++Index)
            Omega=SolveWheelTireStep(Omega,20.,220.,0.,4200.,.34,1.2,Step).AngularSpeedRadPerSecond;
        return Omega;
    };
    const double Omega30=RollDrivenWheel(1./30), Omega60=RollDrivenWheel(1./60), Omega120=RollDrivenWheel(1./120);
    Check(std::abs(Omega30-Omega120)<.02 && std::abs(Omega60-Omega120)<.02,
        "implicit driven-wheel inertia agrees at 30, 60 and 120 Hz");
    const WheelTireStep BrakeStep=SolveWheelTireStep(InitialWheelOmega,20.,0.,800.,4200.,.34,1.2,1./60);
    Check(BrakeStep.AngularSpeedRadPerSecond<InitialWheelOmega && BrakeStep.AngularSpeedRadPerSecond>0
        && BrakeStep.LongitudinalForceN<0,
        "braking slows but does not reverse a rolling wheel and produces retarding grip");
    const WheelTireStep AirborneWheel=SolveWheelTireStep(0.,0.,100.,0.,0.,.34,1.2,.1);
    Check(Near(AirborneWheel.AngularSpeedRadPerSecond,100.*.1/1.2)
        && AirborneWheel.LongitudinalForceN==0,
        "an airborne driven wheel free-spins without creating tire force");
    Check(EngineRpm(0, .34, 3, 4, 850, 7200) == 850, "idle clutch bound");
    Check(EngineRpm(200, .34, 3, 4, 850, 7200) == 7200, "redline bound");

    const double Load = 1480*Gravity/4;
    const double Compression = Load/38000;
    Check(Near(SuspensionForceN(Compression,0,38000,4300,Load*4),Load),
        "springs balance static quarter-car weight");
    Check(SuspensionForceN(Compression,1,38000,4300,Load*4) < Load,
        "damper opposes rebound");
    Check(SuspensionForceN(Compression,-1,38000,4300,Load*4) > Load,
        "damper opposes compression");
    Check(SuspensionForceN(-1,5,38000,4300,Load*4) == 0, "suspension cannot pull road down");
    Check(TireCapacityN(0,Load,1.15,.12) == 0, "airborne tires have no grip");
    Check(Near(TireCapacityN(Load,Load,1.15,.12),Load*1.15), "nominal grip equals mu times load");
    Check(TireCapacityN(Load*2,Load,1.15,.12) < 2*TireCapacityN(Load,Load,1.15,.12),
        "load sensitivity discourages excess load transfer");
    for (int X=-12000; X<=12000; X+=1200)
    {
        for (int Y=-12000; Y<=12000; Y+=1200)
        {
            const TireForce F = FrictionCircle(X,Y,4200);
            Check(std::hypot(F.LongitudinalN,F.LateralN) <= 4200.000001, "combined tire grip budget");
            Check(F.LongitudinalN*X >= 0 && F.LateralN*Y >= 0, "friction clamp preserves demand direction");
        }
    }
    const TireForce Invalid = FrictionCircle(NaN, std::numeric_limits<double>::infinity(), 4200);
    Check(Invalid.LongitudinalN == 0 && Invalid.LateralN == 0, "invalid forces cannot poison chassis");
    for (const double Step : {1./15,1./30,1./60,1./120})
    {
        for (const double Speed : {-50.,-1.,-.01,0.,.01,1.,50.})
        {
            const double Force = BrakeForceN(Speed,10000,370,Step);
            const double After = Speed + Force/370*Step;
            Check(Force*Speed <= 0, "braking never supplies kinetic energy");
            Check(std::abs(After) <= std::abs(Speed)+1e-9 && After*Speed >= -1e-9,
                "braking cannot reverse velocity at any supported step");
        }
    }
    double Smooth30=0, Smooth120=0;
    for (int I=0; I<30; ++I) Smooth30=Smooth(Smooth30,1,6.5,1./30);
    for (int I=0; I<120; ++I) Smooth120=Smooth(Smooth120,1,6.5,1./120);
    Check(Near(Smooth30,Smooth120), "input response agrees across 30 and 120 Hz");
    Check(SteeringLimitDegrees(70,32,6,24) < SteeringLimitDegrees(10,32,6,24),
        "steering sensitivity reduces at speed");
    Check(SteeringLimitDegrees(500,32,6,24) >= 6, "high-speed steering stays bounded");
    Check(AerodynamicDragN(30,.31,2.1) < 0 && AerodynamicDragN(-30,.31,2.1) > 0,
        "drag opposes both travel directions");
    Check(Near(AerodynamicDragN(60,.31,2.1),4*AerodynamicDragN(30,.31,2.1)),
        "aerodynamic drag grows with speed squared");
    Check(CanReverse(0) && CanReverse(-1) && !CanReverse(20) && !CanReverse(NaN),
        "direction changes require near standstill and finite speed");
    std::cout << "PASS: " << Checks << " numerical checks against the runtime vehicle math.\n";
    return 0;
}
