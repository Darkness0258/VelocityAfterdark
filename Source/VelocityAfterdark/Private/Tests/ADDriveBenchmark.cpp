#include "Tests/ADDriveBenchmark.h"

#include "Core/ADGameMode.h"
#include "Core/ADVehicleMath.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

UADDriveBenchmark::UADDriveBenchmark()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void ADStartDriveBenchmark(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    if (!World || !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkDriveBenchmark"))) return;
    if (FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRenderSmoke")))
    {
        UE_LOG(LogTemp, Error, TEXT("Driving benchmark and screenshot smoke must run separately."));
        return;
    }
    AActor* Owner = World->GetAuthGameMode();
    if (!Owner) return;
    auto* Benchmark = NewObject<UADDriveBenchmark>(Owner, TEXT("DrivingBenchmark"));
    Owner->AddInstanceComponent(Benchmark);
    Benchmark->RegisterComponent();
    Benchmark->Initialize();
#endif
}

void UADDriveBenchmark::Initialize()
{
    BootTime = FPlatformTime::Seconds();
    FString Error;
    if (!LoadRoute(Error)) Finish(false, Error);
}

bool UADDriveBenchmark::LoadRoute(FString& Error)
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Tests/dockside_benchmark.json");
    const int64 Size = IFileManager::Get().FileSize(*Path);
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (Size < 1 || Size > 65536 || !FFileHelper::LoadFileToString(Json, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        Error = TEXT("Benchmark route JSON is missing or invalid (64KB maximum).");
        return false;
    }
    const auto Number = [&Root](const TCHAR* Key, double Min, double Max, double& Out)
    { return Root->TryGetNumberField(Key, Out) && FMath::IsFinite(Out) && Out >= Min && Out <= Max; };
    double Version = 0.;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Number(TEXT("schemaVersion"),1,1,Version) || !Root->TryGetStringField(TEXT("id"),RouteId) || RouteId.IsEmpty() ||
        !Number(TEXT("warmupSeconds"),10,60,WarmupSeconds) || !Number(TEXT("durationSeconds"),120,600,DurationSeconds) ||
        !Number(TEXT("cruiseSpeedKmh"),40,120,CruiseSpeedMps) ||
        !Number(TEXT("brakeDecelerationMps2"),1,6,BrakeDecelerationMps2) ||
        !Root->TryGetArrayField(TEXT("points"),Values) || Values->Num() < 8 || Values->Num() > 128)
    {
        Error = TEXT("Benchmark route settings exceed supported bounds.");
        return false;
    }
    CruiseSpeedMps /= 3.6;
    for (const auto& Value : *Values)
    {
        const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
        double X=0., Y=0., Speed=0.;
        if (!Value.IsValid() || !Value->TryGetArray(Point) || Point->Num()!=3 ||
            !(*Point)[0]->TryGetNumber(X) || !(*Point)[1]->TryGetNumber(Y) || !(*Point)[2]->TryGetNumber(Speed) ||
            !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Speed) ||
            FMath::Abs(X)>45000 || FMath::Abs(Y)>30000 || Speed<20 || Speed>120)
        {
            Error = TEXT("Benchmark waypoint must be [xCm,yCm,speedKmh] inside Dockside.");
            return false;
        }
        Points.Add(FVector2D(X,Y));
        CornerSpeeds.Add(Speed/3.6);
    }
    for (int32 Index=0; Index<Points.Num(); ++Index)
    {
        Distances.Add(RouteLength);
        const double Length = FVector2D::Distance(Points[Index],Points[(Index+1)%Points.Num()])*.01;
        if (Length<1. || Length>1000.)
        {
            Error = TEXT("Benchmark route has a degenerate or excessively long segment.");
            return false;
        }
        RouteLength += Length;
    }
    return true;
}

FVector2D UADDriveBenchmark::PointAtDistance(double Distance) const
{
    Distance = FMath::Fmod(Distance,RouteLength);
    for (int32 Index=0; Index<Points.Num(); ++Index)
    {
        const double End = Index+1<Points.Num() ? Distances[Index+1] : RouteLength;
        if (Distance<=End)
            return FMath::Lerp(Points[Index],Points[(Index+1)%Points.Num()],(Distance-Distances[Index])/(End-Distances[Index]));
    }
    return Points[0];
}

void UADDriveBenchmark::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (bFinished) return;
    const double Now=FPlatformTime::Seconds();
    if (!bDriving)
    {
        // Initial discovery is bounded and happens only during initialization.
        auto* PC=Cast<AADPlayerController>(GetWorld()->GetFirstPlayerController());
        auto* Vehicle=PC ? PC->GetVehiclePawn() : nullptr;
        auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
        if (!Vehicle || !Vehicle->GetPhysics()->IsReady() || !Mode || !Mode->IsWorldReady())
        {
            if (Now-BootTime>15.) Finish(false,TEXT("World/vehicle was not ready within 15 seconds."));
            return;
        }
        if (Now-BootTime<2.) return; // Let the suspension settle at the normal player start.
        Controller=PC; Car=Vehicle; Physics=Vehicle->GetPhysics();
        AddTickPrerequisiteActor(PC);
        Physics->AddTickPrerequisiteComponent(this);
        PC->StartDriving();
        if (!Car->IsDrivingEnabled()) { Finish(false,TEXT("Driving session could not start.")); return; }
        bDriving=true;
        DriveTime=Now;
        NextLogTime=Now+15.;
        LastPosition=Car->GetActorLocation();
        UE_LOG(LogTemp,Display,TEXT("AFTERDARK_BENCHMARK_DRIVING: %s; warmup %.0fs, capture %.0fs"),*RouteId,WarmupSeconds,DurationSeconds);
    }
    if (!Car.IsValid() || !Physics.IsValid() || !Controller.IsValid() || !Physics->IsReady() || !Car->IsDrivingEnabled())
    { Finish(false,TEXT("Driving state became unavailable during benchmark.")); return; }
    const FVector Position=Car->GetActorLocation();
    const double SpeedMps=Car->GetVelocity().Size()*.01;
    if (Position.ContainsNaN() || !FMath::IsFinite(SpeedMps) || Position.Z<20. || Position.Z>250. || Car->GetActorUpVector().Z<.7)
    { Finish(false,TEXT("Vehicle became non-finite, overturned, airborne or left the road height envelope.")); return; }
    const double StepM=FVector::Distance(Position,LastPosition)*.01;
    if (StepM>FMath::Max(20.,SpeedMps*DeltaTime+10.))
    { Finish(false,TEXT("Unexpected transform jump/recovery invalidated the continuous driving sample.")); return; }
    LastPosition=Position;
    AirborneSeconds = Physics->GetTelemetry().GroundedWheels<2 ? AirborneSeconds+DeltaTime : 0.;
    if (AirborneSeconds>.5) { Finish(false,TEXT("Vehicle lost road contact for over half a second.")); return; }

    const auto& Definition=Physics->GetDefinition();
    const double RearX=(Definition.WheelAnchorsCm[2].X+Definition.WheelAnchorsCm[3].X)*.5;
    const FVector RearWorld=Position+Car->GetActorForwardVector()*RearX;
    const FVector2D Rear(RearWorld.X,RearWorld.Y);
    double BestSquared=TNumericLimits<double>::Max(), Progress=0.;
    for (int32 Index=0; Index<Points.Num(); ++Index)
    {
        const FVector2D Segment=Points[(Index+1)%Points.Num()]-Points[Index];
        const double T=FMath::Clamp(FVector2D::DotProduct(Rear-Points[Index],Segment)/Segment.SizeSquared(),0.,1.);
        const double Squared=FVector2D::DistSquared(Rear,Points[Index]+Segment*T);
        if (Squared<BestSquared) { BestSquared=Squared; Progress=Distances[Index]+Segment.Size()*.01*T; }
    }
    const double ErrorM=FMath::Sqrt(BestSquared)*.01;
    if (ErrorM>10.) { Finish(false,TEXT("Route deviation exceeded 10m; benchmark cannot certify this run.")); return; }
    if (LastProgress>RouteLength*.8 && Progress<RouteLength*.2) ++StartLineCrossings;
    LastProgress=Progress;
    const double LookaheadM=FMath::Clamp(6.+SpeedMps*.6,8.,18.);
    const FVector2D Aim=PointAtDistance(Progress+LookaheadM);
    const FVector Local=Car->GetActorTransform().InverseTransformVectorNoScale(FVector(Aim.X,Aim.Y,RearWorld.Z)-RearWorld)*.01;
    const double WheelbaseM=(Definition.WheelAnchorsCm[0].X-Definition.WheelAnchorsCm[2].X)*.01;
    const double DesiredDegrees=FMath::RadiansToDegrees(FMath::Atan2(2.*WheelbaseM*Local.Y,FMath::Max(Local.SizeSquared2D(),1.)));
    const double Limit=ADVehicleMath::SteeringLimitDegrees(SpeedMps,Definition.MaxSteeringDegrees,
        Definition.HighSpeedSteeringDegrees,Definition.SteeringFalloffMps);
    const float Steering=static_cast<float>(FMath::Clamp(DesiredDegrees/FMath::Max(Limit,1.),-1.,1.));

    double TargetSpeed=CruiseSpeedMps;
    for (int32 Index=0; Index<Points.Num(); ++Index)
    {
        const double Ahead=FMath::Fmod(Distances[Index]-Progress+RouteLength,RouteLength);
        if (Ahead<100.) TargetSpeed=FMath::Min(TargetSpeed,FMath::Sqrt(FMath::Square(CornerSpeeds[Index])+
            2.*BrakeDecelerationMps2*FMath::Max(0.,Ahead-6.)));
    }
    const double SpeedError=TargetSpeed-SpeedMps;
    const float Throttle=SpeedError<-.6 ? 0.f : static_cast<float>(FMath::Clamp(.15+SpeedError*.25,0.,1.));
    const float Brake=static_cast<float>(FMath::Clamp(-SpeedError*.2,0.,.8));
    Physics->SetControls(Throttle,Brake,Steering,false);

    if (!bCapturing && Now-DriveTime>=WarmupSeconds)
    {
        Controller->ConsoleCommand(TEXT("CsvProfile START"),false);
        bCapturing=true; CaptureTime=Now; CaptureWorldTime=GetWorld()->GetTimeSeconds();
        UE_LOG(LogTemp,Display,TEXT("AFTERDARK_BENCHMARK_CAPTURE_STARTED"));
    }
    if (bCapturing)
    {
        ++MeasuredFrames;
        MeasuredDistanceM+=StepM;
        MaxSpeedKmh=FMath::Max(MaxSpeedKmh,SpeedMps*3.6);
        MaxRouteErrorM=FMath::Max(MaxRouteErrorM,ErrorM);
        if (Now-CaptureTime>=DurationSeconds)
        {
            Finish(MeasuredDistanceM>=1200.,MeasuredDistanceM>=1200. ? TEXT("Completed continuous driving capture.") : TEXT("Distance was too low for a valid driving sample."));
            return;
        }
    }
    if (Now>=NextLogTime)
    {
        UE_LOG(LogTemp,Display,TEXT("AFTERDARK_BENCHMARK_PROGRESS: %.0f km/h; route error %.2fm; captured %.1fs; %.0fm"),
            SpeedMps*3.6,ErrorM,bCapturing ? Now-CaptureTime : 0.,MeasuredDistanceM);
        NextLogTime=Now+15.;
    }
}

void UADDriveBenchmark::Finish(bool bSuccess,const FString& Reason)
{
    if (bFinished) return;
    bFinished=true;
    SetComponentTickEnabled(false);
    if (Physics.IsValid())
    {
        Physics->SetControls(0.f,1.f,0.f,false);
        Physics->RemoveTickPrerequisiteComponent(this);
    }
    if (Controller.IsValid() && bCapturing) Controller->ConsoleCommand(TEXT("CsvProfile STOP"),false);
    const double Now=FPlatformTime::Seconds();
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("success"),bSuccess);
    Report->SetStringField(TEXT("reason"),Reason);
    Report->SetStringField(TEXT("routeId"),RouteId);
    Report->SetStringField(TEXT("scope"),TEXT("QA driver using normal controls; not racing AI or subjective handling acceptance."));
    Report->SetNumberField(TEXT("captureWallSeconds"),bCapturing ? Now-CaptureTime : 0.);
    Report->SetNumberField(TEXT("captureSimulationSeconds"),bCapturing ? GetWorld()->GetTimeSeconds()-CaptureWorldTime : 0.);
    Report->SetNumberField(TEXT("distanceMeters"),MeasuredDistanceM);
    Report->SetNumberField(TEXT("maxSpeedKmh"),MaxSpeedKmh);
    Report->SetNumberField(TEXT("maxRouteErrorMeters"),MaxRouteErrorM);
    Report->SetNumberField(TEXT("measuredFrames"),MeasuredFrames);
    // The rear axle begins just behind the start line. Its first crossing is
    // entry onto the loop, so crossings must not be reported as completed laps.
    Report->SetNumberField(TEXT("startLineCrossingsIncludingWarmup"),StartLineCrossings);
    FString Json;
    FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Benchmark");
    IFileManager::Get().MakeDirectory(*Directory,true);
    if (!FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("drive-report.json")))) bSuccess=false;
    if (bSuccess)
    {
        UE_LOG(LogTemp,Display,TEXT("AFTERDARK_BENCHMARK_OK: %s; %.0fm"),*Reason,MeasuredDistanceM);
    }
    else
    {
        UE_LOG(LogTemp,Error,TEXT("AFTERDARK_BENCHMARK_FAILED: %s"),*Reason);
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("AfterdarkBenchmarkExit")))
    {
        FTimerHandle ExitTimer;
        GetWorld()->GetTimerManager().SetTimer(ExitTimer,FTimerDelegate::CreateLambda([bSuccess]()
        { FPlatformMisc::RequestExitWithStatus(false,bSuccess ? 0 : 1); }),4.f,false);
    }
}
