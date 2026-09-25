#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include <atomic>
#include "ADEngineSynthComponent.generated.h"

/** Original procedural combustion sound; parameters cross to the audio thread atomically. */
UCLASS(ClassGroup=(Velocity), meta=(BlueprintSpawnableComponent))
class VELOCITYAFTERDARK_API UADEngineSynthComponent : public USynthComponent
{
    GENERATED_BODY()

public:
    UADEngineSynthComponent(const FObjectInitializer& ObjectInitializer);
    void SetEngineTargets(float Rpm, float Load, float SpeedKmh, bool bInterior);
    void SetCylinderCount(int32 Count);

protected:
    virtual bool Init(int32& SampleRate) override;
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
    std::atomic<float> TargetRpm{850.0f};
    std::atomic<float> TargetLoad{0.0f};
    std::atomic<float> TargetSpeed{0.0f};
    std::atomic<bool> Interior{false};
    std::atomic<int32> CylinderCount{4};
    float AudioSampleRate = 48000.0f;
    float SmoothedRpm = 850.0f;
    float SmoothedLoad = 0.0f;
    float NoiseFilter = 0.0f;
    float CabinFilter = 0.0f;
    double Phase = 0.0;
    uint32 NoiseState = 0x4E4F5641u;
};
