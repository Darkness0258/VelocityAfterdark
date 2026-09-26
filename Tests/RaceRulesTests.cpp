#include "Core/ADRaceRules.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int Checks = 0;
int Failures = 0;
void Check(bool Condition, const char* Description)
{
    ++Checks;
    if (!Condition) { ++Failures; std::cerr << "FAIL: " << Description << '\n'; }
}
void Near(double Actual, double Expected, const char* Description)
{
    Check(std::abs(Actual - Expected) < 1e-8, Description);
}
using namespace ADRaceRules;

const Gate Gates[] = {
    {{0, 0, 80}, {1, 0, 0}, 100, 100},
    {{1000, 0, 80}, {1, 0, 0}, 100, 100},
    {{1000, 1000, 80}, {0, 1, 0}, 100, 100},
    {{0, 1000, 80}, {-1, 0, 0}, 100, 100}
};

Event Cross(Progress& State, int Index, double Seconds)
{
    const Gate& Target = Gates[Index];
    const Vector3 Before{Target.Location.X - Target.Forward.X * 10.0,
        Target.Location.Y - Target.Forward.Y * 10.0, Target.Location.Z};
    const Vector3 After{Target.Location.X + Target.Forward.X * 10.0,
        Target.Location.Y + Target.Forward.Y * 10.0, Target.Location.Z};
    Check(State.ResetSample(Before, Seconds), "explicit trusted baseline accepts a finite pose");
    return State.Sample(After, Seconds + 0.2, Gates, 4);
}

void GateGeometry()
{
    double Alpha = -1;
    Check(CrossesForward(Gates[0], {-200, 0, 80}, {200, 0, 80}, Alpha), "swept forward crossing scores");
    Near(Alpha, 0.5, "crossing time interpolates halfway through sample");
    Check(CrossesForward(Gates[0], {-5000, -200, 80}, {5000, 200, 80}, Alpha), "gate tests intersection rather than endpoints");
    Check(!CrossesForward(Gates[0], {200, 0, 80}, {-200, 0, 80}, Alpha), "backwards crossing rejected");
    Check(!CrossesForward(Gates[0], {-200, 101, 80}, {200, 101, 80}, Alpha), "outside gate width rejected");
    Check(!CrossesForward(Gates[0], {-200, 0, 181}, {200, 0, 181}, Alpha), "above gate height rejected");
    Check(!CrossesForward(Gates[0], {-200, 0, -21}, {200, 0, -21}, Alpha), "below gate height rejected");
    Check(CrossesForward(Gates[0], {-200, 100, 180}, {200, 100, 180}, Alpha), "finite gate boundary accepted");
    Check(!CrossesForward(Gates[0], {0, 0, 80}, {200, 0, 80}, Alpha), "starting on plane does not repeat score");
    Check(!CrossesForward(Gates[0], {-200, 0, 80}, {-100, 0, 80}, Alpha), "approaching gate without crossing does not score");
    Check(CrossesForward(Gates[0], {-200, 0, 80}, {0, 0, 80}, Alpha), "arriving exactly at plane counts once");
    Near(Alpha, 1, "plane endpoint uses end timestamp");
    Check(CrossesForward(Gates[2], {1000, 900, 80}, {1000, 1100, 80}, Alpha), "north-facing gate uses correct plane");
    Check(CrossesForward(Gates[3], {100, 1000, 80}, {-100, 1000, 80}, Alpha), "west-facing gate uses correct direction");
    const double Nan = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    Check(!CrossesForward(Gates[0], {Nan, 0, 80}, {1, 0, 80}, Alpha), "NaN position rejected");
    Check(!CrossesForward(Gates[0], {-1, 0, 80}, {Infinity, 0, 80}, Alpha), "infinite position rejected");
    Gate Broken = Gates[0];
    Broken.Forward = {0, 0, 0};
    Check(!CrossesForward(Broken, {-1, 0, 80}, {1, 0, 80}, Alpha), "zero gate normal rejected");
    Broken = Gates[0]; Broken.Forward = {2, 0, 0};
    Check(!IsValid(Broken), "non-unit gate normal rejected");
    Broken = Gates[0]; Broken.Forward.Z = 1;
    Check(!IsValid(Broken), "tilted gate normal rejected");
    Broken = Gates[0]; Broken.HalfWidthCm = -1;
    Check(!IsValid(Broken), "negative width rejected");
    Broken = Gates[0]; Broken.HalfHeightCm = Nan;
    Check(!IsValid(Broken), "NaN height rejected");
    Broken = Gates[0]; Broken.Location.X = Infinity;
    Check(!IsValid(Broken), "infinite gate location rejected");
}

void OrderedLaps()
{
    Progress State;
    Check(State.Reset(4, 2), "valid two-lap race initialized");
    Check(Cross(State, 0, 0) == Event::Started, "first start crossing begins lap");
    Check(State.Started && !State.Finished && State.CompletedLaps == 0 && State.NextCheckpoint == 1,
        "start gate does not award a completed lap");
    Near(State.StartSeconds, 0.1, "start time interpolated");
    Check(Cross(State, 2, 1) == Event::None, "skipped checkpoint does not advance progress");
    Check(State.NextCheckpoint == 1, "missing checkpoint remains required");
    Check(Cross(State, 0, 2) == Event::None, "repeated start cannot award lap");
    Check(Cross(State, 1, 3) == Event::Checkpoint, "first required checkpoint accepted");
    Near(State.LastSplitSeconds, 3, "split time is relative to lap start");
    Check(Cross(State, 1, 4) == Event::None, "repeated checkpoint cannot advance progress");
    Check(Cross(State, 2, 5) == Event::Checkpoint, "second required checkpoint accepted");
    Check(Cross(State, 3, 6) == Event::Checkpoint && State.NextCheckpoint == 0, "all route gates unlock finish");
    Check(Cross(State, 0, 7) == Event::Lap, "first full lap completes without finishing race");
    Check(State.CompletedLaps == 1 && State.NextCheckpoint == 1 && State.LastLapValid, "second lap begins with first route checkpoint");
    Near(State.LastLapSeconds, 7, "first lap time includes all ordered checkpoints");
    Near(State.BestLapSeconds, 7, "first clean lap establishes best time");
    Check(Cross(State, 1, 8) == Event::Checkpoint, "lap two gate one");
    Check(Cross(State, 2, 9) == Event::Checkpoint, "lap two gate two");
    Check(Cross(State, 3, 10) == Event::Checkpoint, "lap two gate three");
    Check(Cross(State, 0, 11) == Event::Finished, "second full lap finishes two-lap race");
    Check(State.Finished && State.CompletedLaps == 2, "finished state retains completed lap count");
    Near(State.FinishSeconds, 11.1, "race result includes starting grid approach time");
    Near(State.LastLapSeconds, 4, "second lap time independent from first");
    Near(State.BestLapSeconds, 4, "faster clean lap updates best");
    Check(Cross(State, 0, 12) == Event::None, "finished race ignores extra finish crossings");
    Near(State.FinishSeconds, 11.1, "finish timestamp remains immutable");
    Check(State.CompletedLaps == 2, "finished race cannot gain extra laps");
}

void InvalidSamplesAndRecovery()
{
    Progress State;
    Check(!State.Reset(1, 1), "one-gate circuits rejected");
    Check(!State.Reset(257, 1), "unbounded checkpoint count rejected");
    Check(!State.Reset(4, -1), "negative lap count rejected");
    Check(!State.Reset(4, 100), "unbounded lap count rejected");
    Check(State.Reset(4, 1), "valid reset after invalid configuration");
    Check(State.Sample({-100, 0, 80}, 0, Gates, 4) == Event::None, "first sample establishes baseline without scoring");
    Check(State.Sample({100, 0, 80}, 0, Gates, 4) == Event::Rejected, "zero elapsed time rejected");
    Check(State.Sample({100, 0, 80}, -1, Gates, 4) == Event::Rejected, "negative timestamp rejected");
    Check(!State.Started, "invalid samples did not start race");
    Check(State.ResetSample({-100, 0, 80}, 1), "valid baseline restored");
    Check(State.Sample({100, 0, 80}, 0.9, Gates, 4) == Event::Rejected, "backward timestamp rejected");
    Check(State.Sample({100, 0, 80}, 1.1, Gates, 4) == Event::Started, "backward timestamp did not destroy trusted baseline");
    Near(State.StartSeconds, 1.05, "valid crossing retains prior trusted timestamp");
    Check(State.Sample({1100, 0, 80}, 1.101, Gates, 4) == Event::Rejected, "impossible displacement rejected as teleport");
    Check(State.NextCheckpoint == 1, "teleport cannot award required checkpoint");
    Check(State.Sample({1200, 0, 80}, 1.2, Gates, 4) == Event::None, "teleport resample cannot score on following frame");
    Check(State.ResetSample({1100, 0, 80}, 2), "recovery baseline can be set beyond gate without crossing it");
    Check(State.Sample({1200, 0, 80}, 2.1, Gates, 4) == Event::None, "recovery beyond gate never grants credit");
    Check(State.ResetSample({900, 0, 80}, 3), "legitimate recovery before expected gate");
    Check(State.Sample({1100, 0, 80}, 3.1, Gates, 4) == Event::Checkpoint, "gate can be driven after recovery");
    Check(State.NextCheckpoint == 2, "progress still advances exactly one checkpoint");
    Check(State.Sample({1000, 1100, 80}, 6, Gates, 4) == Event::Rejected, "stale multi-second sample cannot sweep score");
    Check(State.NextCheckpoint == 2, "long hitch does not grant a checkpoint");
    Check(State.Sample({1000, 1100, 80}, 6.1, nullptr, 4) == Event::Rejected, "null gate array rejected");
    Check(State.Sample({1000, 1100, 80}, 6.2, Gates, 3) == Event::Rejected, "changed gate count rejected");
    Check(!State.ResetSample({std::numeric_limits<double>::quiet_NaN(), 0, 0}, 7), "invalid recovery pose rejected");
    Check(!State.ResetSample({0, 0, 0}, std::numeric_limits<double>::infinity()), "invalid recovery time rejected");
    Check(State.Sample({1000, 1100, 80}, 7, Gates, 4) == Event::None, "first valid sample after invalid recovery only establishes baseline");
}

void PointToPoint()
{
    Progress State;
    Check(State.Reset(4, 0), "zero-lap point-to-point route initialized");
    Check(Cross(State, 0, 0) == Event::Started, "point-to-point start gate begins the route");
    Check(Cross(State, 2, 1) == Event::None, "point-to-point route cannot skip its first checkpoint");
    Check(State.NextCheckpoint == 1, "skipped sprint checkpoint remains required");
    Check(Cross(State, 1, 2) == Event::Checkpoint, "point-to-point first checkpoint accepted");
    Check(State.NextCheckpoint == 2, "point-to-point advances to the next checkpoint");
    Check(Cross(State, 2, 3) == Event::Checkpoint, "point-to-point intermediate checkpoint accepted");
    Check(State.NextCheckpoint == 3, "point-to-point advances to its finish gate");
    Check(Cross(State, 3, 4) == Event::Finished, "point-to-point finish gate ends the route");
    Check(State.Finished && State.CompletedLaps == 0, "point-to-point finish does not invent a lap");
    Near(State.FinishSeconds, 4.1, "point-to-point finish timestamp is interpolated");
    Check(Cross(State, 0, 5) == Event::None, "finished point-to-point race cannot restart or score another lap");
    Check(State.CompletedLaps == 0, "point-to-point route remains a zero-lap result");
}

void RecoveryInvalidatesBestLap()
{
    Progress State;
    Check(State.Reset(4, 3), "three-lap recovery test initialized");
    Check(Cross(State, 0, 0) == Event::Started, "recovery test starts");
    for (int I = 1; I <= 3; ++I) Check(Cross(State, I, I) == Event::Checkpoint, "clean first lap gate");
    Check(Cross(State, 0, 5) == Event::Lap, "clean lap completed");
    Near(State.BestLapSeconds, 5, "clean lap benchmark retained");
    State.InvalidateCurrentLap();
    for (int I = 1; I <= 3; ++I) Check(Cross(State, I, 5 + I * 0.5) == Event::Checkpoint, "recovery lap can still progress");
    Check(Cross(State, 0, 7) == Event::Lap, "recovery lap can complete");
    Check(!State.LastLapValid, "recovered lap explicitly marked invalid for best time");
    Near(State.LastLapSeconds, 2, "raw recovered lap time remains available for results");
    Near(State.BestLapSeconds, 5, "faster recovered lap cannot replace clean best");
    for (int I = 1; I <= 3; ++I) Check(Cross(State, I, 7 + I) == Event::Checkpoint, "following clean lap progresses");
    Check(Cross(State, 0, 11) == Event::Finished, "race can finish following recovery");
    Check(State.LastLapValid, "next lap is valid again without another recovery");
    Near(State.BestLapSeconds, 4, "later clean lap can improve best");

    Check(State.Reset(4, 1), "single recovered lap reset");
    Check(Cross(State, 0, 0) == Event::Started, "single recovered lap start");
    State.InvalidateCurrentLap();
    for (int I = 1; I <= 3; ++I) Cross(State, I, I);
    Check(Cross(State, 0, 4) == Event::Finished, "only recovered lap can finish race");
    Near(State.BestLapSeconds, 0, "no invented best time when all laps invalid");
    Check(!State.LastLapValid, "finished recovered lap stays invalid");
}

void FrameRateIndependentTiming()
{
    const int Rates[] = {15, 30, 60, 120};
    for (const int Rate : Rates)
    {
        Progress State;
        Check(State.Reset(4, 1), "rate test configured");
        State.ResetSample({-2000, 0, 80}, 1);
        int StartedEvents = 0;
        for (int Frame = 1; Frame <= Rate; ++Frame)
        {
            const double Dt = static_cast<double>(Frame) / Rate;
            const Event Result = State.Sample({-2000 + 5000 * Dt, 0, 80}, 1 + Dt, Gates, 4);
            if (Result == Event::Started) ++StartedEvents;
            Check(Result != Event::Rejected, "180 km/h physical sweep accepted at supported frame rates");
        }
        Check(StartedEvents == 1, "one start event at each frame rate");
        Near(State.StartSeconds, 1.4, "interpolated crossing timestamp independent of frame rate");
        Check(State.NextCheckpoint == 2, "high-speed run also crossed required next gate once");
    }
}
}

int main()
{
    GateGeometry();
    OrderedLaps();
    PointToPoint();
    InvalidSamplesAndRecovery();
    RecoveryInvalidatesBestLap();
    FrameRateIndependentTiming();
    std::cout << Checks << " race rule checks; " << Failures << " failures.\n";
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
