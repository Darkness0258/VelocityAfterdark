#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADCinematicComponent.generated.h"
class ACameraActor;
class AADVehiclePawn;
class UPrimitiveComponent;
UENUM()
enum class EADCinematicMode : uint8 { None, Photo, Replay, Story };
struct FADReplaySample
{
    double Seconds=0.;
    FTransform Pose;
    FVector LinearVelocity=FVector::ZeroVector;
    FVector AngularVelocity=FVector::ZeroVector;
};

/** Local photo camera and bounded vehicle-only replay. No scoring runs during playback. */
UCLASS()
class VELOCITYAFTERDARK_API UADCinematicComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UADCinematicComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool EnterPhoto();
    bool EnterReplay();
    bool PlayArrivalCutscene();
    bool PlayCareerBriefing(const FString& ChapterTitle,const FString& CrewLine,const FString& Narrative);
    void Leave();
    void Capture();
    void ToggleHidden() { bHidden=!bHidden; }
    void TogglePlayback() { if (Mode==EADCinematicMode::Replay) bPlaying=!bPlaying; }
    void CycleCamera() { CameraIndex=(CameraIndex+1)%3; }
    bool IsActive() const { return Mode!=EADCinematicMode::None; }
    bool IsHudHidden() const { return bHidden || CaptureHideFrames>0; }
    EADCinematicMode GetMode() const { return Mode; }
    const FString& GetStoryTitle() const { return StoryTitle; }
    const FString& GetStoryAttribution() const { return StoryAttribution; }
    const FString& GetStorySubtitle() const;
    float GetStorySeconds() const { return StorySeconds; }
    double GetReplaySeconds() const { return ReplaySeconds; }
    double GetReplayDuration() const;
    // Call before an explicit reset, race-grid placement or other teleport.
    // A motion/timestamp guard also protects callers that cannot notify us.
    void NotifyRecordingDiscontinuity();
    float GetPhotoExposureOffset() const { return PhotoExposureOffset; }
    const FString& GetMessage() const { return Message; }
private:
    bool Enter(EADCinematicMode Desired);
    void UpdatePhoto(float RealDelta);
    void ApplyPhotoLook();
    void UpdateReplay(float RealDelta);
    void UpdateStory(float RealDelta);
    bool StartStory(const FString& Title,const FString& Attribution,const FString& Narrative,
        const FString& Closing,bool bContinueToCareerRace);
    void Record(float DeltaSeconds);
    bool BuildPlayback();
    UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AADVehiclePawn> Vehicle;
    TWeakObjectPtr<UPrimitiveComponent> Chassis;
    TWeakObjectPtr<AADVehiclePawn> RecordedVehicle;
    TArray<FADReplaySample> Ring;
    TArray<FADReplaySample> Playback;
    FTransform ReturnPose;
    FVector ReturnVelocity,ReturnAngularVelocity;
    double LastWallSeconds=0.;
    double ReplaySeconds=0.;
    float RecordAccumulator=0.f;
    float PhotoBaseExposure=0.f;
    float PhotoExposureOffset=0.f;
    float PhotoFocusDistanceCm=1500.f;
    float PhotoFStop=8.f;
    int32 PhotoFilterIndex=0;
    int32 RingHead=0;
    int32 CameraIndex=0;
    int32 CaptureHideFrames=0;
    bool bWasPaused=false;
    bool bWasDriving=false;
    bool bWasSimulating=false;
    bool bWasPhysicsTicking=false;
    bool bPlaying=true;
    bool bHidden=false;
    bool bContinueToCareerRace=false;
    EADCinematicMode Mode=EADCinematicMode::None;
    FString Message;
    FString StoryTitle,StoryAttribution,StoryNarrative,StoryClosing;
    float StorySeconds=0.f;
    static constexpr float StoryDurationSeconds=9.5f;
    static constexpr int32 SampleCapacity=1200;
    static constexpr double MaximumReplaySeconds=60.;
    static constexpr double MaximumSampleGapSeconds=.25;
};
