#include "Racing/ADRaceManager.h"
#include "Core/ADGameMode.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Career/ADCareerSubsystem.h"
#include "Engine/GameInstance.h"

#include "Racing/ADRaceDriverComponent.h"
#include "Player/ADVehiclePawn.h"
#include "World/ADPoliceDirector.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogADRace, Log, All);
namespace
{
    ADRaceRules::Vector3 RacePosition(const FVector& P) { return {P.X,P.Y,P.Z}; }

    void AddGate(UInstancedStaticMeshComponent* Mesh, const FADCheckpoint& Gate,double SizeScale=1.)
    {
        const FRotator Rotation = Gate.Forward.Rotation();
        const FVector Right(-Gate.Forward.Y,Gate.Forward.X,0);
        const FVector Ground(Gate.Location.X,Gate.Location.Y,0);
        for (double Side : {-1.,1.})
            Mesh->AddInstance(FTransform(Rotation,Ground+Right*Gate.HalfWidthCm*Side+FVector(0,0,180),FVector(.22,.22,3.6)*SizeScale),true);
        Mesh->AddInstance(FTransform(Rotation,Ground+FVector(0,0,360),FVector(.18,Gate.HalfWidthCm*.02,.12)*SizeScale),true);
    }
}

AADRaceManager::AADRaceManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RaceRoot"));
    GateMarkers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CourseGates"));
    TargetMarker = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NextGate"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (auto* Mesh : {GateMarkers.Get(),TargetMarker.Get()})
    {
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetStaticMesh(Cube.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->SetCullDistances(0,90000);
        Mesh->SetVisibility(false);
    }
}

void AADRaceManager::BeginPlay()
{
    Super::BeginPlay();
    bReady = Catalog.LoadDefault(LoadError);
    if (!bReady)
    {
        UE_LOG(LogADRace,Error,TEXT("Race unavailable: %s"),*LoadError);
        return;
    }
    const FADRaceDefinition* Default = Catalog.Find(TEXT("dockside_circuit_v1"));
    if (!Default)
    {
        bReady = false;
        LoadError = TEXT("Race catalog is missing the default Dockside circuit.");
        UE_LOG(LogADRace,Error,TEXT("Race unavailable: %s"),*LoadError);
        return;
    }
    SelectDefinition(*Default);
    UE_LOG(LogADRace,Display,TEXT("Race catalog ready: %d courses. Default: %s, %d laps, %.0fm, %d checkpoints."),
        Catalog.GetRaces().Num(),*Definition.Name,Definition.Laps,Definition.RouteLengthM,Definition.Checkpoints.Num());
}

void AADRaceManager::SelectDefinition(const FADRaceDefinition& Selected)
{
    // Called only with no live racers: each AI driver holds a pointer to Definition.
    Definition = Selected;
    ScoringGates.Reset(Definition.Checkpoints.Num());
    for (const auto& Gate : Definition.Checkpoints)
        ScoringGates.Add({RacePosition(Gate.Location),RacePosition(Gate.Forward),Gate.HalfWidthCm,Gate.HalfHeightCm});
    BuildMarkers();
    Racers.Reserve(Definition.Grid.Num());
}

void AADRaceManager::BuildMarkers()
{
    GateMarkers->ClearInstances();
    TargetMarker->ClearInstances();
    DisplayedCheckpoint = -2;
    auto* Base = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"));
    if (Base)
    {
        auto* Dim = UMaterialInstanceDynamic::Create(Base,this);
        Dim->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.08f,.18f,.28f));
        Dim->SetVectorParameterValue(TEXT("EmissiveColor"),FLinearColor(.1f,.3f,.55f));
        GateMarkers->SetMaterial(0,Dim);
        auto* Bright = UMaterialInstanceDynamic::Create(Base,this);
        Bright->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.32f,.95f,.7f));
        Bright->SetVectorParameterValue(TEXT("EmissiveColor"),FLinearColor(1.f,3.f,2.f));
        TargetMarker->SetMaterial(0,Bright);
    }
    for (const auto& Gate : Definition.Checkpoints) AddGate(GateMarkers,Gate);
    // Checkered start line is presentation only; the scoring plane is data-driven.
    const auto& Start = Definition.Checkpoints[0];
    const FVector Right(-Start.Forward.Y,Start.Forward.X,0);
    for (int32 Row=0;Row<2;++Row)
        for (int32 Column=0;Column<20;++Column)
            if ((Row+Column)%2==0)
                GateMarkers->AddInstance(FTransform(Start.Forward.Rotation(),
                    FVector(Start.Location.X,Start.Location.Y,1.5)+Start.Forward*(Row*100.-50.)+Right*(Column*100.-950.),
                    FVector(1.,1.,.025)),true);
}

bool AADRaceManager::StartRace(AADVehiclePawn* Player,int32 DifficultyIndex)
{
    // Preserve the deterministic quick-race contract regardless of the last career event.
    return StartRaceById(Player,TEXT("dockside_circuit_v1"),DifficultyIndex);
}

bool AADRaceManager::StartRaceById(AADVehiclePawn* Player,const FString& RaceId,int32 DifficultyIndex)
{
    if (HasPendingCareerReward() && !RetryCareerReward()) return false;
    const FADRaceDefinition* Selected = Catalog.Find(RaceId);
    if (!Selected)
    {
        LoadError = TEXT("The requested race is not present in the validated event catalog.");
        return false;
    }
    if (GetNetMode()!=NM_Standalone || !HasAuthority() || !bReady || !IsValid(Player) || !Player->GetPhysics()->IsReady()
        || (State!=EADRaceState::Idle && State!=EADRaceState::Results)
        || !Selected->DifficultySpeedScales.IsValidIndex(DifficultyIndex)) return false;
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
        Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive())
    {
        // Keep the current race/catalog and pursuit actors intact until police
        // search expires; a caller cannot turn a chase into a free race reset.
        LoadError=TEXT("Lose the pursuit and finish police search before starting a race.");
        return false;
    }
    LeaveRace();
    SelectDefinition(*Selected);
    LoadError.Reset();
    ActiveChapterId.Reset();
    CareerMessage.Reset();
    RaceReceiptId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    bRewardAttempted=bRewardCommitted=false;
    Difficulty=DifficultyIndex;
    ElapsedSeconds=0.;
    CountdownRemaining=Definition.CountdownSeconds;
    SortOrder.SetNumUninitialized(Definition.Grid.Num());
    for (int32 Index=0;Index<Definition.Grid.Num();++Index)
    {
        AADVehiclePawn* Car=Player;
        if (Index>0)
        {
            FActorSpawnParameters Params;
            Params.Owner=this;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Car=GetWorld()->SpawnActor<AADVehiclePawn>(AADVehiclePawn::StaticClass(),Definition.Grid[Index],Params);
        }
        if (!Car || !Car->GetPhysics()->IsReady())
        {
            if (Index>0 && Car) Car->Destroy();
            LoadError=TEXT("A race vehicle could not initialize. Free drive remains available.");
            LeaveRace();
            return false;
        }
        FADRacerState Racer;
        Racer.Car=Car;
        Racer.Name=Index==0 ? TEXT("YOU") : Definition.Opponents[Index-1].Name;
        Racer.RivalId=Index==0 ? FString() : Definition.Opponents[Index-1].Id;
        Racer.Color=Index==0 ? FLinearColor(.53f,.92f,.77f) : Definition.Opponents[Index-1].Color;
        Racer.Place=Index+1;
        Racer.Progress.Reset(ScoringGates.Num(),Definition.Laps);
        Racers.Add(Racer);
        if (!Car->PlaceForRace(Definition.Grid[Index]))
        {
            LoadError=TEXT("A race vehicle could not be placed safely.");
            LeaveRace();
            return false;
        }
        if (Index>0) Car->SetRaceAppearance(Racer.Color,true);
        Car->OnRecoveryRequested.AddUObject(this,&AADRaceManager::HandleRecoveryRequest);
        Car->SetDrivingEnabled(true);
        Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
        if (Index>0)
        {
            auto* Driver=NewObject<UADRaceDriverComponent>(Car,TEXT("RaceDriver"));
            Car->AddInstanceComponent(Driver);
            Driver->RegisterComponent();
            const auto& Opponent=Definition.Opponents[Index-1];
            if (!Driver->Initialize(Car,&Definition,static_cast<float>(Definition.DifficultySpeedScales[Difficulty])*Opponent.SpeedScale,Opponent.LaneOffsetCm))
            {
                LoadError=TEXT("An opponent driver could not initialize.");
                LeaveRace();
                return false;
            }
            if (!Driver->SetPersonality(Opponent.Id,Opponent.Personality))
            {
                LoadError=TEXT("An opponent personality is invalid.");
                LeaveRace();
                return false;
            }
            Racers[Index].Driver=Driver;
            Driver->SetDriving(false);
        }
    }
    TArray<AADVehiclePawn*> Cars;
    Cars.Reserve(Racers.Num());
    for (const auto& Racer : Racers) Cars.Add(Racer.Car.Get());
    for (auto& Racer : Racers) if (Racer.Driver.IsValid()) Racer.Driver->SetCompetitors(Cars);
    GateMarkers->SetVisibility(true);
    DisplayedCheckpoint=-2;
    SetState(EADRaceState::Countdown);
    UpdateTargetMarker();
    UE_LOG(LogADRace,Display,TEXT("Race grid ready: %d cars, difficulty %d."),Racers.Num(),Difficulty);
    return true;
}

bool AADRaceManager::StartCareerRace(AADVehiclePawn* Player)
{
    if (HasPendingCareerReward() && !RetryCareerReward()) return false;
    if (!GetGameInstance() || GetNetMode()!=NM_Standalone) return false;
    const auto* Career=GetGameInstance()->GetSubsystem<UADCareerSubsystem>();
    const auto* Ownership=GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Career || !Career->IsReady() || !Ownership || !Ownership->IsReady()) return false;
    const auto* Chapter=Career->GetActiveChapter(Ownership->GetProfile());
    if (!Chapter) return false;
    const FString Id=Chapter->Id;
    const FString ChapterId=Chapter->Id;
    const FString RaceId=Chapter->RaceId;
    const FADCareerChapter ChapterCopy=*Chapter;
    const FADRaceDefinition* Race=Catalog.Find(RaceId);
    if (!Race) return false;
    const FADRaceDefinition RaceCopy=*Race;
    const FString Briefing=Career->ComposeBriefing(Ownership->GetProfile(),ChapterCopy,RaceCopy);
    if (!StartRaceById(Player,RaceId,Chapter->Difficulty)) return false;
    ActiveChapterId=ChapterId;
    CareerMessage=Briefing;
    return true;
}

bool AADRaceManager::HasPendingCareerReward() const
{
    return !ActiveChapterId.IsEmpty() && !bRewardCommitted && IsClassificationFinal()
        && Racers.IsValidIndex(0) && Racers[0].Progress.Finished && !Racers[0].bDNF;
}

bool AADRaceManager::RetryCareerReward()
{
    if (!HasPendingCareerReward()) return true;
    bRewardAttempted=true;
    auto* Ownership=GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    FString Error;
    TArray<FADRivalRaceResult> RivalResults;
    RivalResults.Reserve(FMath::Max(0,Racers.Num()-1));
    for (int32 Index=1;Index<Racers.Num();++Index)
    {
        if (Racers[Index].RivalId.IsEmpty())
        {
            CareerMessage=TEXT("REWARD NOT SAVED. A rival is missing its stable identity.");
            return false;
        }
        RivalResults.Add({Racers[Index].RivalId,Racers[Index].Place});
    }
    if (!Ownership || !Ownership->CommitRaceResult(RaceReceiptId,ActiveChapterId,Racers[0].Place,RivalResults,Error))
    {
        CareerMessage=TEXT("REWARD NOT SAVED. Enter retries: ")+Error;
        return false;
    }
    bRewardCommitted=true;
    const auto* Career=GetGameInstance()->GetSubsystem<UADCareerSubsystem>();
    const auto* Chapter=Career ? Career->GetChapters().FindByPredicate(
        [this](const FADCareerChapter& Item){ return Item.Id==ActiveChapterId; }) : nullptr;
    const auto* Race=Catalog.Find(Definition.Id);
    CareerMessage=Chapter && Race ? Career->ComposeVictoryLine(Ownership->GetProfile(),*Chapter,*Race) : Ownership->GetStatus();
    return true;
}

void AADRaceManager::SetState(EADRaceState NewState)
{
    if (State==NewState) return;
    State=NewState;
    OnStateChanged.Broadcast(State);
}

bool AADRaceManager::AllowsPlayerInput() const
{
    return State==EADRaceState::Idle || ((State==EADRaceState::Racing || State==EADRaceState::Results)
        && Racers.IsValidIndex(0) && !Racers[0].Progress.Finished && !Racers[0].bDNF);
}

bool AADRaceManager::IsClassificationFinal() const
{
    if (Racers.Num()!=Definition.Grid.Num() || Racers.IsEmpty()) return false;
    for (const auto& Racer : Racers) if (!Racer.Progress.Finished && !Racer.bDNF) return false;
    return true;
}

void AADRaceManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || State==EADRaceState::Idle || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
    if (State==EADRaceState::Countdown)
    {
        for (auto& Racer : Racers)
            if (Racer.Car.IsValid()) Racer.Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
        CountdownRemaining=FMath::Max(0.,CountdownRemaining-DeltaSeconds);
        if (CountdownRemaining<=0.)
        {
            for (auto& Racer : Racers)
            {
                if (Racer.Car.IsValid())
                {
                    Racer.LastPosition=Racer.Car->GetActorLocation();
                    Racer.Progress.ResetSample(RacePosition(Racer.LastPosition),0.);
                }
                if (Racer.Driver.IsValid()) Racer.Driver->SetDriving(true);
            }
            SetState(EADRaceState::Racing);
            UE_LOG(LogADRace,Display,TEXT("Race GO"));
        }
        return;
    }
    if (IsClassificationFinal()) { if (!bRewardAttempted) RetryCareerReward(); return; }
    const double PreviousSeconds=ElapsedSeconds;
    ElapsedSeconds=FMath::Min(ElapsedSeconds+DeltaSeconds,Definition.TimeoutSeconds);
    const double SampleFraction=(ElapsedSeconds-PreviousSeconds)/DeltaSeconds;
    for (int32 Index=0;Index<Racers.Num();++Index)
    {
        auto& Racer=Racers[Index];
        if (Racer.Progress.Finished || Racer.bDNF) continue;
        if (!Racer.Car.IsValid() || !Racer.Car->GetPhysics()->IsReady())
        {
            RetireRacer(Index);
            continue;
        }
        // Clip the final physics segment at the deadline. A finish just before
        // the deadline must count even when its frame ends just after it.
        Racer.LastPosition=FMath::Lerp(Racer.LastPosition,Racer.Car->GetActorLocation(),SampleFraction);
        const auto Event=Racer.Progress.Sample(RacePosition(Racer.LastPosition),ElapsedSeconds,
            ScoringGates.GetData(),static_cast<size_t>(ScoringGates.Num()));
        if (FMath::FloorToInt(ElapsedSeconds/30.)!=FMath::FloorToInt(PreviousSeconds/30.))
        {
            const auto& Telemetry=Racer.Car->GetPhysics()->GetTelemetry();
            UE_LOG(LogADRace,Verbose,TEXT("Race %.1f: %s lap %d gate %d, %s, %.1fkm/h, throttle %.2f brake %.2f, driver target %.1f tick %d"),
                ElapsedSeconds,*Racer.Name,Racer.Progress.CompletedLaps,Racer.Progress.NextCheckpoint,*Racer.LastPosition.ToString(),
                Telemetry.SpeedKmh,Telemetry.Throttle,Telemetry.Brake,Racer.Driver.IsValid() ? Racer.Driver->GetTargetSpeedKmh() : -1.f,
                Racer.Driver.IsValid() && Racer.Driver->IsComponentTickEnabled());
        }
        if (Event==ADRaceRules::Event::Finished)
        {
            if (Definition.Laps==0)
            {
                if (Racer.Driver.IsValid()) Racer.Driver->SetDriving(false);
                Racer.Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
            }
            else if (Racer.Driver.IsValid()) Racer.Driver->BeginRunOut(FMath::Max(60.,12.*Racers.Num()));
            else Racer.Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
            UE_LOG(LogADRace,Display,TEXT("Finished: %s, %.3fs + %.1fs penalty."),*Racer.Name,Racer.Progress.FinishSeconds,Racer.PenaltySeconds);
        }
        else if (Event==ADRaceRules::Event::Rejected)
        {
            Racer.Progress.InvalidateCurrentLap();
        }
        if (!Racer.Progress.Finished && ElapsedSeconds>=Definition.TimeoutSeconds) { RetireRacer(Index); continue; }
        if (Racer.Driver.IsValid() && Racer.Driver->NeedsRecovery()) RecoverRacer(Index);
    }
    if (!Racers.IsEmpty() && (Racers[0].Progress.Finished || Racers[0].bDNF)) SetState(EADRaceState::Results);
    UpdateClassification();
    UpdateTargetMarker();
    if (IsClassificationFinal() && !bRewardAttempted) RetryCareerReward();
}

void AADRaceManager::RetireRacer(int32 Index)
{
    auto& Racer=Racers[Index];
    Racer.bDNF=true;
    if (Racer.Driver.IsValid()) Racer.Driver->SetDriving(false);
    if (Racer.Car.IsValid()) Racer.Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
    UE_LOG(LogADRace,Warning,TEXT("DNF: %s, lap %d, next gate %d, at %s"),*Racer.Name,
        Racer.Progress.CompletedLaps,Racer.Progress.NextCheckpoint,*Racer.LastPosition.ToString());
}

void AADRaceManager::UpdateClassification()
{
    for (auto& Racer : Racers)
    {
        if (Racer.bDNF) continue; // Retirement fixes position; later collisions cannot improve it.
        Racer.RankedDistanceM=Racer.Progress.CompletedLaps*Definition.RouteLengthM;
        if (!Racer.Car.IsValid() || !Racer.Progress.Started || Racer.Progress.Finished) continue;
        const int32 Next=Racer.Progress.NextCheckpoint;
        const bool bPointToPoint=Definition.Laps==0;
        const int32 Previous=bPointToPoint ? Next-1 : (Next+Definition.Checkpoints.Num()-1)%Definition.Checkpoints.Num();
        const double From=Definition.Checkpoints[Previous].DistanceM;
        const double To=(!bPointToPoint && Next==0) ? Definition.RouteLengthM : Definition.Checkpoints[Next].DistanceM;
        double Error=0.;
        double Distance=Definition.ClosestDistanceM(Racer.Car->GetActorLocation(),Error);
        const FVector2D Direction=(Definition.PointAtDistance(Distance+2.)-Definition.PointAtDistance(Distance)).GetSafeNormal();
        const FVector Velocity=Racer.Car->GetVelocity();
        Racer.bWrongWay=Velocity.Size()>500. && FVector::DotProduct(Velocity.GetSafeNormal(),FVector(Direction.X,Direction.Y,0))<-.25;
        // Only position within the currently unlocked route leg contributes to
        // ranking. Crossing another district road cannot jump the race order.
        if (!bPointToPoint && Next==0 && Distance<1.) Distance=Definition.RouteLengthM;
        Racer.RankedDistanceM+=From+(Distance>=From && Distance<=To && Error<15. ? Distance-From : 0.);
    }
    for (int32 Index=0;Index<SortOrder.Num();++Index) SortOrder[Index]=Index;
    const auto Ahead=[this](int32 A,int32 B)
    {
        const auto& Left=Racers[A]; const auto& Right=Racers[B];
        if (Left.bDNF!=Right.bDNF) return !Left.bDNF;
        if (Left.Progress.Finished!=Right.Progress.Finished) return Left.Progress.Finished;
        if (Left.Progress.Finished)
        {
            const double L=Left.Progress.FinishSeconds+Left.PenaltySeconds;
            const double R=Right.Progress.FinishSeconds+Right.PenaltySeconds;
            return L==R ? A<B : L<R;
        }
        return Left.RankedDistanceM==Right.RankedDistanceM ? A<B : Left.RankedDistanceM>Right.RankedDistanceM;
    };
    for (int32 I=1;I<Racers.Num();++I)
        for (int32 J=I;J>0 && Ahead(SortOrder[J],SortOrder[J-1]);--J) Swap(SortOrder[J],SortOrder[J-1]);
    for (int32 Place=0;Place<Racers.Num();++Place) Racers[SortOrder[Place]].Place=Place+1;
}

bool AADRaceManager::RecoverRacer(int32 Index)
{
    if (!HasAuthority() || !Racers.IsValidIndex(Index) || (State!=EADRaceState::Racing && State!=EADRaceState::Results)) return false;
    auto& Racer=Racers[Index];
    if (!Racer.Car.IsValid() || Racer.Progress.Finished || Racer.bDNF || ElapsedSeconds-Racer.LastRecoverySeconds<3.) return false;
    FTransform Safe=Definition.Grid[Index];
    if (Racer.Progress.Started)
    {
        const int32 Last=(Racer.Progress.NextCheckpoint+Definition.Checkpoints.Num()-1)%Definition.Checkpoints.Num();
        const double Distance=Definition.Checkpoints[Last].DistanceM+10.;
        const FVector2D Point=Definition.PointAtDistance(Distance);
        const FVector2D Direction=(Definition.PointAtDistance(Distance+2.)-Point).GetSafeNormal();
        Safe=FTransform(FVector(Direction.X,Direction.Y,0).Rotation(),FVector(Point.X,Point.Y,90.));
    }
    // Recovery cannot spawn into another car; wait for a safe gap instead.
    for (int32 Other=0;Other<Racers.Num();++Other)
        if (Other!=Index && Racers[Other].Car.IsValid() && FVector::DistSquared(Racers[Other].Car->GetActorLocation(),Safe.GetLocation())<FMath::Square(500.)) return false;
    if (!Racer.Car->PlaceForRace(Safe)) return false;
    Racer.Progress.ResetSample(RacePosition(Safe.GetLocation()),ElapsedSeconds);
    Racer.LastPosition=Safe.GetLocation();
    Racer.Progress.InvalidateCurrentLap();
    Racer.PenaltySeconds+=Definition.RecoveryPenaltySeconds;
    Racer.LastRecoverySeconds=ElapsedSeconds;
    ++Racer.RecoveryCount;
    if (Racer.Driver.IsValid()) { Racer.Driver->ResetDriver(); Racer.Driver->SetDriving(true); }
    UE_LOG(LogADRace,Display,TEXT("Recovery: %s, penalty +%.1fs, next checkpoint %d."),*Racer.Name,Definition.RecoveryPenaltySeconds,Racer.Progress.NextCheckpoint);
    return true;
}

void AADRaceManager::HandleRecoveryRequest(AADVehiclePawn* Car)
{
    for (int32 Index=0;Index<Racers.Num();++Index) if (Racers[Index].Car.Get()==Car) { RecoverRacer(Index); return; }
}

void AADRaceManager::UpdateTargetMarker()
{
    const int32 Next=Racers.Num()>0 && !Racers[0].Progress.Finished && !Racers[0].bDNF ? Racers[0].Progress.NextCheckpoint : -1;
    if (DisplayedCheckpoint==Next) return;
    DisplayedCheckpoint=Next;
    TargetMarker->ClearInstances();
    if (Definition.Checkpoints.IsValidIndex(Next)) AddGate(TargetMarker,Definition.Checkpoints[Next],1.06);
    TargetMarker->SetVisibility(Next>=0 && State!=EADRaceState::Idle);
}

void AADRaceManager::LeaveRace()
{
    if (!IsActorBeingDestroyed() && HasPendingCareerReward() && !RetryCareerReward()) return;
    for (int32 Index=0;Index<Racers.Num();++Index)
    {
        auto& Racer=Racers[Index];
        if (Racer.Driver.IsValid()) Racer.Driver->SetDriving(false);
        if (Racer.Car.IsValid())
        {
            Racer.Car->OnRecoveryRequested.RemoveAll(this);
            if (Index>0) Racer.Car->Destroy();
            else { Racer.Car->SetDrivingEnabled(true); Racer.Car->GetPhysics()->SetControls(0.f,1.f,0.f,false); }
        }
    }
    Racers.Reset();
    SortOrder.Reset();
    ElapsedSeconds=CountdownRemaining=0.;
    DisplayedCheckpoint=-2;
    ActiveChapterId.Reset();
    GateMarkers->SetVisibility(false);
    TargetMarker->SetVisibility(false);
    SetState(EADRaceState::Idle);
}

void AADRaceManager::EndPlay(const EEndPlayReason::Type Reason)
{
    LeaveRace();
    Super::EndPlay(Reason);
}
