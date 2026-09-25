#include "Audio/ADEngineSynthComponent.h"

UADEngineSynthComponent::UADEngineSynthComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    NumChannels = 2;
    bAutoActivate = false;
    bAllowSpatialization = false;
}

void UADEngineSynthComponent::SetEngineTargets(float Rpm, float Load, float SpeedKmh, bool bInterior)
{
    Rpm = FMath::IsFinite(Rpm) ? Rpm : 850.f;
    Load = FMath::IsFinite(Load) ? Load : 0.f;
    SpeedKmh = FMath::IsFinite(SpeedKmh) ? SpeedKmh : 0.f;
    TargetRpm.store(FMath::Clamp(Rpm, 500.0f, 12000.0f), std::memory_order_relaxed);
    TargetLoad.store(FMath::Clamp(Load, 0.0f, 1.0f), std::memory_order_relaxed);
    TargetSpeed.store(FMath::Clamp(FMath::Abs(SpeedKmh), 0.0f, 400.0f), std::memory_order_relaxed);
    Interior.store(bInterior, std::memory_order_relaxed);
}

bool UADEngineSynthComponent::Init(int32& SampleRate)
{
    AudioSampleRate = static_cast<float>(FMath::Max(8000, SampleRate));
    return true;
}

void UADEngineSynthComponent::SetCylinderCount(int32 Count)
{
    CylinderCount.store(Count==6 || Count==8 ? Count : 4,std::memory_order_relaxed);
}

int32 UADEngineSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
    // No UObjects, allocation, locks, or game-thread access in the mixer callback.
    const float Rpm = TargetRpm.load(std::memory_order_relaxed);
    const float Load = TargetLoad.load(std::memory_order_relaxed);
    const float Wind = TargetSpeed.load(std::memory_order_relaxed) / 400.0f;
    const bool bInside = Interior.load(std::memory_order_relaxed);
    const double FiringsPerRevolution=CylinderCount.load(std::memory_order_relaxed)*.5;
    const float Smoothing = 1.0f - FMath::Exp(-1.0f / (AudioSampleRate * 0.04f));
    for (int32 Sample = 0; Sample < NumSamples; Sample += 2)
    {
        SmoothedRpm += (Rpm - SmoothedRpm) * Smoothing;
        SmoothedLoad += (Load - SmoothedLoad) * Smoothing;
        const double Frequency = static_cast<double>(SmoothedRpm) / 60.0 * FiringsPerRevolution;
        Phase += Frequency / AudioSampleRate;
        Phase -= FMath::FloorToDouble(Phase);
        const float Angle = static_cast<float>(Phase * 2.0 * PI);
        NoiseState = NoiseState * 1664525u + 1013904223u;
        const float Noise = static_cast<float>((NoiseState >> 8) & 0xFFFFFFu) / 8388607.5f - 1.0f;
        NoiseFilter += (Noise - NoiseFilter) * 0.08f;
        const float Exhaust = FMath::Sin(Angle) * 0.18f + FMath::Sin(Angle * 2.0f + 0.4f) * 0.07f
            + FMath::Sin(Angle * 3.0f) * (0.025f + SmoothedLoad * 0.07f);
        const float Intake = FMath::Sin(Angle * 4.0f + FMath::Sin(Angle) * 0.25f) * SmoothedLoad * 0.025f;
        const float Raw = (Exhaust + Intake) * (0.35f + SmoothedLoad * 0.65f)
            + NoiseFilter * (0.015f + Wind * Wind * 0.28f);
        CabinFilter += (Raw - CabinFilter) * (bInside ? 0.065f : 0.45f);
        const float Output = FMath::Clamp(CabinFilter * (bInside ? 0.75f : 1.0f), -0.85f, 0.85f);
        OutAudio[Sample] = Output;
        if (Sample + 1 < NumSamples) { OutAudio[Sample + 1] = Output; }
    }
    return NumSamples;
}
