#pragma once

// Authoritative race scoring has no dependency on Unreal objects or rendering.
// Positions are centimetres, timestamps are elapsed race seconds. A caller must
// explicitly resample after recovery; a teleport never grants a checkpoint.
#include <cmath>
#include <cstddef>

namespace ADRaceRules
{
struct Vector3
{
    double X = 0.0, Y = 0.0, Z = 0.0;
};

struct Gate
{
    Vector3 Location;
    Vector3 Forward;
    double HalfWidthCm = 1000.0;
    double HalfHeightCm = 200.0;
};

inline bool IsFinite(const Vector3& Value)
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

inline bool IsValid(const Gate& Value)
{
    const double NormalLengthSquared = Value.Forward.X * Value.Forward.X + Value.Forward.Y * Value.Forward.Y;
    return IsFinite(Value.Location) && IsFinite(Value.Forward)
        && std::abs(Value.Forward.Z) < 0.001 && std::abs(NormalLengthSquared - 1.0) < 0.001
        && std::isfinite(Value.HalfWidthCm) && Value.HalfWidthCm >= 1.0 && Value.HalfWidthCm <= 100000.0
        && std::isfinite(Value.HalfHeightCm) && Value.HalfHeightCm >= 1.0 && Value.HalfHeightCm <= 100000.0;
}

// Sweep the vehicle reference point through a finite vertical gate. Only a
// negative-to-positive crossing counts, with bounds checked at the intersection
// rather than at frame endpoints (important for high speed and low frame rates).
inline bool CrossesForward(const Gate& Target, const Vector3& From, const Vector3& To, double& OutAlpha)
{
    OutAlpha = 0.0;
    if (!IsValid(Target) || !IsFinite(From) || !IsFinite(To)) return false;
    const double Before = (From.X - Target.Location.X) * Target.Forward.X
        + (From.Y - Target.Location.Y) * Target.Forward.Y;
    const double After = (To.X - Target.Location.X) * Target.Forward.X
        + (To.Y - Target.Location.Y) * Target.Forward.Y;
    if (!std::isfinite(Before) || !std::isfinite(After) || Before >= 0.0 || After < 0.0) return false;
    const double Denominator = After - Before;
    if (!std::isfinite(Denominator) || Denominator <= 0.0) return false;
    const double Alpha = -Before / Denominator;
    const double X = From.X + (To.X - From.X) * Alpha - Target.Location.X;
    const double Y = From.Y + (To.Y - From.Y) * Alpha - Target.Location.Y;
    const double Z = From.Z + (To.Z - From.Z) * Alpha - Target.Location.Z;
    const double Lateral = -X * Target.Forward.Y + Y * Target.Forward.X;
    if (!std::isfinite(Lateral) || !std::isfinite(Z) || std::abs(Lateral) > Target.HalfWidthCm
        || std::abs(Z) > Target.HalfHeightCm) return false;
    OutAlpha = Alpha;
    return true;
}

enum class Event { None, Started, Checkpoint, Lap, Finished, Rejected };

class Progress
{
public:
    int NextCheckpoint = 0;
    int CompletedLaps = 0;
    bool Started = false;
    bool Finished = false;
    double StartSeconds = 0.0;
    double LapStartSeconds = 0.0;
    double FinishSeconds = 0.0;
    double LastLapSeconds = 0.0;
    double BestLapSeconds = 0.0;
    double LastSplitSeconds = 0.0;
    bool LastLapValid = true;

    void InvalidateCurrentLap() { CurrentLapValid = false; }

    bool Reset(int InGateCount, int InLapCount)
    {
        *this = Progress();
        if (InGateCount < 2 || InGateCount > 256 || InLapCount < 1 || InLapCount > 99) return false;
        GateCount = InGateCount;
        LapCount = InLapCount;
        return true;
    }

    bool ResetSample(const Vector3& Position, double Seconds)
    {
        HasSample = IsFinite(Position) && std::isfinite(Seconds) && Seconds >= 0.0;
        if (HasSample)
        {
            PreviousPosition = Position;
            PreviousSeconds = Seconds;
        }
        return HasSample;
    }

    Event Sample(const Vector3& Position, double Seconds, const Gate* Gates, std::size_t Count)
    {
        if (Finished) return Event::None;
        if (GateCount < 2 || !Gates || Count != static_cast<std::size_t>(GateCount)
            || NextCheckpoint < 0 || NextCheckpoint >= GateCount || !IsValid(Gates[NextCheckpoint])
            || !IsFinite(Position) || !std::isfinite(Seconds) || Seconds < 0.0)
        {
            HasSample = false;
            return Event::Rejected;
        }
        if (!HasSample)
        {
            ResetSample(Position, Seconds);
            return Event::None;
        }
        const double Dt = Seconds - PreviousSeconds;
        if (!std::isfinite(Dt) || Dt <= 0.0)
        {
            // Do not move the trusted baseline backwards in time.
            return Event::Rejected;
        }
        const double Dx = Position.X - PreviousPosition.X;
        const double Dy = Position.Y - PreviousPosition.Y;
        const double Dz = Position.Z - PreviousPosition.Z;
        const double DistanceSquared = Dx * Dx + Dy * Dy + Dz * Dz;
        const double MaximumDistanceCm = 14000.0 * Dt + 150.0;
        if (Dt > 2.0 || !std::isfinite(DistanceSquared) || DistanceSquared > MaximumDistanceCm * MaximumDistanceCm)
        {
            ResetSample(Position, Seconds);
            return Event::Rejected;
        }
        double Alpha = 0.0;
        const bool Crossed = CrossesForward(Gates[NextCheckpoint], PreviousPosition, Position, Alpha);
        const double CrossingSeconds = PreviousSeconds + Dt * Alpha;
        ResetSample(Position, Seconds);
        if (!Crossed) return Event::None;

        if (!Started)
        {
            Started = true;
            StartSeconds = LapStartSeconds = CrossingSeconds;
            CurrentLapValid = true;
            NextCheckpoint = 1;
            return Event::Started;
        }
        LastSplitSeconds = CrossingSeconds - LapStartSeconds;
        if (NextCheckpoint != 0)
        {
            NextCheckpoint = (NextCheckpoint + 1) % GateCount;
            return Event::Checkpoint;
        }

        ++CompletedLaps;
        LastLapSeconds = LastSplitSeconds;
        LastLapValid = CurrentLapValid;
        if (LastLapValid && (BestLapSeconds <= 0.0 || LastLapSeconds < BestLapSeconds)) BestLapSeconds = LastLapSeconds;
        CurrentLapValid = true;
        LapStartSeconds = CrossingSeconds;
        Finished = CompletedLaps >= LapCount;
        if (Finished)
        {
            FinishSeconds = CrossingSeconds;
            return Event::Finished;
        }
        NextCheckpoint = 1;
        return Event::Lap;
    }

private:
    int GateCount = 0;
    int LapCount = 0;
    bool HasSample = false;
    bool CurrentLapValid = true;
    Vector3 PreviousPosition;
    double PreviousSeconds = 0.0;
};
}
