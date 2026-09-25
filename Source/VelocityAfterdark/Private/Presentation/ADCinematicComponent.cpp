#include "Presentation/ADCinematicComponent.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Garage/ADGarageSessionComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Algo/BinarySearch.h"
#include "EngineUtils.h"
#include "World/ADAtmosphere.h"

namespace
{
    float Analog(float Value)
    {
        if (!FMath::IsFinite(Value) || FMath::Abs(Value)<.12f) return 0.f;
        return FMath::Sign(Value)*FMath::Clamp((FMath::Abs(Value)-.12f)/.88f,0.f,1.f);
    }

    bool ContinuousMotion(const FADReplaySample& Previous,const FADReplaySample& Current)
    {
        const double Step=Current.Seconds-Previous.Seconds;
        if (!FMath::IsFinite(Step) || Step<=0. || Step>.25 || Current.Pose.ContainsNaN()
            || Current.LinearVelocity.ContainsNaN() || Current.AngularVelocity.ContainsNaN()) return false;
        // Position must be compatible with measured chassis motion. The margin
        // tolerates contact corrections without interpolating across recovery jumps.
        const double TravelAllowance=FMath::Max(250.,(Previous.LinearVelocity.Size()+Current.LinearVelocity.Size())*.8*Step+150.);
        const double AngleAllowance=FMath::DegreesToRadians(20.)
            +(Previous.AngularVelocity.Size()+Current.AngularVelocity.Size())*.8*Step;
        return FVector::DistSquared(Previous.Pose.GetLocation(),Current.Pose.GetLocation())<=FMath::Square(TravelAllowance)
            && Previous.Pose.GetRotation().AngularDistance(Current.Pose.GetRotation())<=AngleAllowance;
    }
}

UADCinematicComponent::UADCinematicComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    Ring.Reserve(SampleCapacity);
}

bool UADCinematicComponent::EnterPhoto() { return Enter(EADCinematicMode::Photo); }
bool UADCinematicComponent::EnterReplay() { return Enter(EADCinematicMode::Replay); }

bool UADCinematicComponent::Enter(EADCinematicMode Desired)
{
    if (IsActive()) return false;
    auto* PC=Cast<AADPlayerController>(GetOwner());
    auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
    if (!PC || !Car || !Car->GetPhysics()->IsReady() || !PC->PlayerCameraManager
        || !PC->GetGarageSession() || PC->GetGarageSession()->IsActive() || Car->IsInGarage() || GetNetMode()!=NM_Standalone)
    { Message=TEXT("Photo and replay require an offline drive outside the garage."); return false; }
    if (Desired==EADCinematicMode::Replay && (RecordedVehicle.Get()!=Car || !BuildPlayback()))
    { Message=TEXT("Drive continuously for at least two seconds to record a replay."); return false; }
    Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent());
    if (!Chassis.IsValid() || Car->GetActorTransform().ContainsNaN())
    { Message=TEXT("The vehicle cannot be inspected right now."); Playback.Reset(); return false; }
    bWasPaused=PC->IsPaused();
    if (!bWasPaused && !PC->SetPause(true)) { Message=TEXT("The world could not be paused."); return false; }
    Vehicle=Car;
    ReturnPose=Car->GetActorTransform();
    ReturnVelocity=Chassis->GetPhysicsLinearVelocity();
    ReturnAngularVelocity=Chassis->GetPhysicsAngularVelocityInRadians();
    bWasDriving=Car->IsDrivingEnabled(); bWasSimulating=Chassis->IsSimulatingPhysics();
    bWasPhysicsTicking=Car->GetPhysics()->IsComponentTickEnabled();
    Car->SetDrivingEnabled(false);
    if (Desired==EADCinematicMode::Replay)
    {
        Chassis->SetSimulatePhysics(false);
        Car->GetPhysics()->SetComponentTickEnabled(false);
        Car->SetGarageMode(true);
    }
    Camera=GetWorld()->SpawnActor<ACameraActor>(PC->PlayerCameraManager->GetCameraLocation(),PC->PlayerCameraManager->GetCameraRotation());
    if (!Camera)
    {
        Mode=Desired;
        Leave(); Message=TEXT("Inspection camera could not be created."); return false;
    }
    Camera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(),20.f,100.f));
    PhotoExposureOffset=0.f;
    PhotoBaseExposure=0.f;
    for (TActorIterator<AADAtmosphere> It(GetWorld()); It; ++It)
    { PhotoBaseExposure=It->GetExposureBias(); break; }
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->GetCameraComponent()->PostProcessSettings.AutoExposureBias=PhotoBaseExposure;
    PC->FlushPressedKeys(); PC->SetViewTarget(Camera);
    Mode=Desired; ReplaySeconds=0.; LastWallSeconds=FPlatformTime::Seconds();
    bPlaying=true; bHidden=false;
    Message=Desired==EADCinematicMode::Photo ? TEXT("PHOTO MODE / [ AND ] EXPOSURE / LEFT STICK + TRIGGERS MOVE") : TEXT("DRIVE REPLAY / VEHICLE ONLY");
    return true;
}

double UADCinematicComponent::GetReplayDuration() const
{ return Playback.Num()>1 ? Playback.Last().Seconds-Playback[0].Seconds : 0.; }

void UADCinematicComponent::NotifyRecordingDiscontinuity()
{
    Ring.Reset(); RingHead=0; RecordAccumulator=0.f; RecordedVehicle.Reset();
}

bool UADCinematicComponent::BuildPlayback()
{
    Playback.Reset(Ring.Num());
    if (Ring.Num()<2) return false;
    const int32 First=Ring.Num()==SampleCapacity ? RingHead : 0;
    const double LastTime=Ring[(First+Ring.Num()-1)%Ring.Num()].Seconds;
    for (int32 Index=0;Index<Ring.Num();++Index)
    {
        const FADReplaySample& Sample=Ring[(First+Index)%Ring.Num()];
        if (Sample.Seconds<LastTime-MaximumReplaySeconds) continue;
        if (!FMath::IsFinite(Sample.Seconds) || Sample.Pose.ContainsNaN()
            || (!Playback.IsEmpty() && !ContinuousMotion(Playback.Last(),Sample)))
        { Playback.Reset(); NotifyRecordingDiscontinuity(); return false; }
        Playback.Add(Sample);
    }
    return Playback.Num()>1 && GetReplayDuration()>=2.;
}

void UADCinematicComponent::Record(float DeltaSeconds)
{
    const auto* PC=Cast<AADPlayerController>(GetOwner());
    auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
    if (!Car || GetNetMode()!=NM_Standalone || Car->IsInGarage())
    { NotifyRecordingDiscontinuity(); return; }
    // Pauses do not advance world time and can preserve the preceding segment.
    if (PC->IsPaused()) return;
    if (!Car->IsDrivingEnabled() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f)
    { NotifyRecordingDiscontinuity(); return; }
    auto* Body=Cast<UPrimitiveComponent>(Car->GetRootComponent());
    if (!Body || !Body->IsSimulatingPhysics()) { NotifyRecordingDiscontinuity(); return; }
    const FADReplaySample Sample{GetWorld()->GetTimeSeconds(),Car->GetActorTransform(),
        Body->GetPhysicsLinearVelocity(),Body->GetPhysicsAngularVelocityInRadians()};
    if (Sample.Pose.ContainsNaN() || Sample.LinearVelocity.ContainsNaN() || Sample.AngularVelocity.ContainsNaN()
        || !FMath::IsFinite(Sample.Seconds)) { NotifyRecordingDiscontinuity(); return; }
    if (RecordedVehicle.Get()!=Car) NotifyRecordingDiscontinuity();
    if (!Ring.IsEmpty())
    {
        const int32 Last=Ring.Num()==SampleCapacity ? (RingHead+SampleCapacity-1)%SampleCapacity : Ring.Num()-1;
        const double Gap=Sample.Seconds-Ring[Last].Seconds;
        if (Gap==0.) return; // Never put duplicate timestamps in the search index.
        if (Gap>MaximumSampleGapSeconds || !ContinuousMotion(Ring[Last],Sample)) NotifyRecordingDiscontinuity();
    }
    RecordedVehicle=Car;
    RecordAccumulator+=DeltaSeconds;
    if (RecordAccumulator<.05f && !Ring.IsEmpty()) return;
    RecordAccumulator=FMath::Fmod(RecordAccumulator,.05f);
    if (Ring.Num()<SampleCapacity) Ring.Add(Sample);
    else { Ring[RingHead]=Sample; RingHead=(RingHead+1)%SampleCapacity; }
}

void UADCinematicComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    auto* PC=Cast<AADPlayerController>(GetOwner());
    if (!PC) return;
    if (!IsActive())
    {
        Record(DeltaTime);
        return;
    }
    const double Now=FPlatformTime::Seconds();
    const float RealDelta=static_cast<float>(FMath::Clamp(Now-LastWallSeconds,0.,.05));
    LastWallSeconds=Now;
    if (!Vehicle.IsValid() || !Camera) { Leave(); return; }
    if (Mode==EADCinematicMode::Photo) UpdatePhoto(RealDelta);
    else UpdateReplay(RealDelta);
    if (CaptureHideFrames>0) --CaptureHideFrames;
}

void UADCinematicComponent::UpdatePhoto(float Step)
{
    const auto* PC=Cast<AADPlayerController>(GetOwner());
    const auto Key=[PC](FKey Positive,FKey Negative) { return (PC->IsInputKeyDown(Positive) ? 1.f : 0.f)-(PC->IsInputKeyDown(Negative) ? 1.f : 0.f); };
    FRotator Rotation=Camera->GetActorRotation();
    Rotation.Yaw+=(Key(EKeys::Right,EKeys::Left)+Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX)))*Step*65;
    Rotation.Pitch=FMath::Clamp(Rotation.Pitch+(Key(EKeys::Up,EKeys::Down)+Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY)))*Step*45,-85.f,85.f);
    Camera->SetActorRotation(Rotation);
    const FVector Direction=Camera->GetActorForwardVector()*(Key(EKeys::W,EKeys::S)+Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY)))
        +Camera->GetActorRightVector()*(Key(EKeys::D,EKeys::A)+Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX)))
        +FVector::UpVector*(Key(EKeys::E,EKeys::Q)+Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis))
            -Analog(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis)));
    const float Speed=PC->IsInputKeyDown(EKeys::LeftShift) ? 2500.f : 700.f;
    FVector Position=Camera->GetActorLocation()+Direction.GetClampedToMaxSize(1)*Speed*Step;
    const FVector FromCar=Position-ReturnPose.GetLocation();
    Position=ReturnPose.GetLocation()+FromCar.GetClampedToMaxSize(15000.);
    Camera->SetActorLocation(Position);
    auto* Lens=Camera->GetCameraComponent();
    Lens->SetFieldOfView(FMath::Clamp(Lens->FieldOfView+Key(EKeys::X,EKeys::Z)*Step*25,20.f,100.f));
    const float ExposureInput=Key(EKeys::RightBracket,EKeys::LeftBracket);
    if (ExposureInput!=0.f)
    {
        PhotoExposureOffset=FMath::Clamp(PhotoExposureOffset+ExposureInput*Step,-3.f,3.f);
        Lens->PostProcessSettings.AutoExposureBias=PhotoBaseExposure+PhotoExposureOffset;
        Message=FString::Printf(TEXT("PHOTO MODE / EXPOSURE %+.1f EV / [ AND ] ADJUST"),PhotoExposureOffset);
    }
}

void UADCinematicComponent::UpdateReplay(float Step)
{
    if (Playback.Num()<2 || GetReplayDuration()<=0.) { Leave(); return; }
    const auto* PC=Cast<AADPlayerController>(GetOwner());
    const float Scrub=(PC->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f)-(PC->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
    const float Rate=PC->IsInputKeyDown(EKeys::LeftShift) ? .25f : 1.f;
    ReplaySeconds=FMath::Clamp(ReplaySeconds+(Scrub!=0 ? Scrub*5.f : bPlaying ? Rate : 0.f)*Step,0.,GetReplayDuration());
    const double Time=Playback[0].Seconds+ReplaySeconds;
    const int32 Upper=FMath::Clamp(Algo::UpperBoundBy(Playback,Time,[](const FADReplaySample& Sample){return Sample.Seconds;}),1,Playback.Num()-1);
    const auto& A=Playback[Upper-1]; const auto& B=Playback[Upper];
    const float Alpha=static_cast<float>(FMath::Clamp((Time-A.Seconds)/FMath::Max(.0001,B.Seconds-A.Seconds),0.,1.));
    FTransform Pose; Pose.Blend(A.Pose,B.Pose,Alpha);
    Vehicle->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
    const FVector Target=Pose.GetLocation()+FVector(0,0,70);
    const FVector Offset=CameraIndex==0 ? FVector(-650,110,250) : CameraIndex==1 ? FVector(650,750,500) : FVector(0,0,1700);
    const FVector Position=Pose.TransformPosition(Offset);
    Camera->SetActorLocationAndRotation(Position,(Target-Position).Rotation());
    if (ReplaySeconds>=GetReplayDuration()) bPlaying=false;
}

void UADCinematicComponent::Capture()
{
    if (!IsActive()) return;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Photos");
    if (!IFileManager::Get().MakeDirectory(*Directory,true)) { Message=TEXT("Photo directory is unavailable."); return; }
    CaptureHideFrames=3;
    const FString Path=Directory/(TEXT("Afterdark_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Path,false,false);
    Message=TEXT("Screenshot requested in Saved/Photos.");
}

void UADCinematicComponent::Leave()
{
    if (!IsActive()) return;
    auto* PC=Cast<AADPlayerController>(GetOwner());
    if (Vehicle.IsValid() && !Vehicle->IsActorBeingDestroyed())
    {
        if (Mode==EADCinematicMode::Replay)
        {
            Vehicle->SetActorTransform(ReturnPose,false,nullptr,ETeleportType::TeleportPhysics);
            Vehicle->SetGarageMode(false);
            if (Chassis.IsValid())
            {
                Chassis->SetSimulatePhysics(bWasSimulating);
                if (bWasSimulating) { Chassis->SetPhysicsLinearVelocity(ReturnVelocity); Chassis->SetPhysicsAngularVelocityInRadians(ReturnAngularVelocity); }
            }
            Vehicle->GetPhysics()->SetComponentTickEnabled(bWasPhysicsTicking);
        }
        Vehicle->SetDrivingEnabled(bWasDriving);
    }
    if (PC)
    {
        PC->SetViewTarget(PC->GetVehiclePawn());
        PC->FlushPressedKeys();
        if (!bWasPaused) PC->SetPause(false);
    }
    if (Camera) Camera->Destroy();
    Camera=nullptr; Playback.Reset(); Mode=EADCinematicMode::None; bHidden=false; CaptureHideFrames=0;
}
void UADCinematicComponent::EndPlay(const EEndPlayReason::Type Reason)
{ Leave(); Super::EndPlay(Reason); }
