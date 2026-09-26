#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/PrimitiveComponent.h"
#include "Core/ADGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Garage/ADGarageSessionComponent.h"
#include "Misc/App.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Presentation/ADCinematicComponent.h"
#include "Presentation/ADMapComponent.h"
#include "Racing/ADRaceManager.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "World/ADAtmosphere.h"
#include "World/ADExplorationDirector.h"
#include "World/ADPoliceDirector.h"
#include "World/ADDistrict.h"
#include "World/ADRegionalWorld.h"
#include "World/ADTrafficManager.h"

namespace
{
class FIntegratedCommand : public IAutomationLatentCommand
{
public:
    FIntegratedCommand(FAutomationTestBase* InTest,bool bInWorld)
        : Test(InTest),bWorld(bInWorld),Deadline(FPlatformTime::Seconds()+240.) {}
    ~FIntegratedCommand() override
    {
        if (bFixed) { FApp::SetFixedDeltaTime(PreviousStep); FApp::SetUseFixedTimeStep(bPreviousFixed); }
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds()>Deadline) { Test->AddError(TEXT("Integrated scenario timed out.")); return true; }
        UWorld* World=GEditor ? GEditor->PlayWorld : nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        auto* PC=Cast<AADPlayerController>(World->GetFirstPlayerController());
        auto* Car=PC ? PC->GetVehiclePawn() : nullptr;
        auto* Mode=World->GetAuthGameMode<AADGameMode>();
        if (!Car || !Mode || !Mode->IsWorldReady() || !Car->GetPhysics()->IsReady()) return false;
        auto* Chassis=Cast<UPrimitiveComponent>(Car->GetRootComponent());
        auto* Ownership=World->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
        FString Error;
        if (Stage==0)
        {
            if (!Test->TestTrue(TEXT("Isolated profile initializes"),Ownership->InitializeProfile(TEXT(""),Error)))
            { Test->AddError(Error); return true; }
            PreviousStep=FApp::GetFixedDeltaTime(); bPreviousFixed=FApp::UseFixedTimeStep(); bFixed=true;
            FApp::SetFixedDeltaTime(1./60.); FApp::SetUseFixedTimeStep(true);
            PC->StartDriving(); Car->ResetVehicle();
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
            PC->SimulateDeviceDisconnectForAutomation();
            Test->TestTrue(TEXT("Controller disconnect clears driving and pauses the session"),
                PC->IsGamePaused() && !Car->IsDrivingEnabled());
            PC->SimulateDeviceDisconnectForAutomation();
            Test->TestTrue(TEXT("Repeated disconnect remains safely paused"),PC->IsGamePaused());
            PC->TogglePause();
            Test->TestTrue(TEXT("Explicit player confirmation resumes after disconnect"),!PC->IsGamePaused() && Car->IsDrivingEnabled());
#endif
            if (bWorld && !Test->TestTrue(TEXT("Production living-world startup succeeds"),Mode->InitializeLivingWorld(Error)))
            { Test->AddError(Error); return true; }
            Stage=1; StageTime=World->GetTimeSeconds(); return false;
        }
        if (bWorld) return WorldScenario(World,PC,Car,Mode,Chassis);
        return PresentationScenario(World,PC,Car,Chassis);
    }
private:
    bool WorldScenario(UWorld* World,AADPlayerController* PC,AADVehiclePawn* Car,AADGameMode* Mode,UPrimitiveComponent* Chassis)
    {
        auto* Weather=Mode->GetAtmosphere(); auto* Regions=Mode->GetRegionalWorld();
        auto* Traffic=Mode->GetTrafficManager(); auto* Police=Mode->GetPoliceDirector();
        const double Elapsed=World->GetTimeSeconds()-StageTime;
        if (Stage==1 && Elapsed>=2.)
        {
            Test->TestTrue(TEXT("Nova City opens at night with active rain and a wet road"),
                Weather->GetHour()>=22. && Weather->GetHour()<24. && Weather->GetWeather()==EADWeather::Rain
                && Weather->GetRainIntensity()>=.8f && Weather->GetWetness()>=.45f);
            Test->TestTrue(TEXT("Physical traffic population is bounded"),Traffic->GetTrafficCount()>0 && Traffic->GetTrafficCount()<=4);
            Test->TestTrue(TEXT("Road graph and discovery system load"),Mode->GetExploration()->IsReady());
            AADDistrict* Dockside=nullptr;
            for (TActorIterator<AADDistrict> It(World);It;++It) { Dockside=*It; break; }
            Test->TestTrue(TEXT("Starter skyline uses instanced imported city-kit buildings and props"),
                Dockside && Dockside->GetImportedDistrictMeshInstanceCount()>0);
            if (Dockside)
            {
                FCollisionQueryParams BuildingQuery(SCENE_QUERY_STAT(ADBuildingProxyAcceptance),false,Car);
                FHitResult BuildingHit;
                const FVector BuildingProbe(-33750.f,6500.f,30000.f);
                const bool bBuildingBlocks=World->LineTraceSingleByChannel(BuildingHit,BuildingProbe,
                    FVector(BuildingProbe.X,BuildingProbe.Y,-200.f),ECC_Visibility,BuildingQuery);
                Test->TestTrue(TEXT("Hidden visual proxy keeps imported Dockside buildings physically solid"),
                    bBuildingBlocks && BuildingHit.GetActor()==Dockside);
            }
            Test->TestTrue(TEXT("Regional road length extends the original district"),Regions->GetRoadLengthMeters()>6000.f);
            Test->TestTrue(TEXT("Recovery bounds include the regional road world"),Mode->GetDriveBounds().X>=Regions->GetGroundHalfExtent().X
                && Mode->GetDriveBounds().Y>=Regions->GetGroundHalfExtent().Y);
            // Sample resident road contact before scenery streams. Ground remains physical.
            FCollisionQueryParams Query(SCENE_QUERY_STAT(ADRegionalAcceptance),false,Car);
            for (const auto& Road:Regions->GetRoadSegments())
            {
                const FVector2D P=(Road.Start+Road.End)*.5;
                FHitResult Hit;
                const bool bHit=World->LineTraceSingleByChannel(Hit,FVector(P,500),FVector(P,-200),ECC_Visibility,Query);
                Test->TestTrue(FString::Printf(TEXT("Road contact exists: %s"),*Road.Id),bHit && Hit.ImpactNormal.Z>.98 && FMath::Abs(Hit.ImpactPoint.Z)<50.);
            }
            const FADWorldSnapshot Original=Weather->CaptureWorldSnapshot();
            Weather->SetHour(23.999); Weather->SetWeather(EADWeather::Rain);
            // Drive the actual atmosphere at fixed quarter-second updates; world physics
            // still runs normally outside this clock-transition portion of the fixture.
            for (int32 I=0;I<320;++I) Weather->Tick(.25f);
            Test->TestTrue(TEXT("Clock wraps through midnight"),Weather->GetHour()<2.);
            Test->TestTrue(TEXT("Rain gradually wets the road"),Weather->GetRainIntensity()>.99f && Weather->GetWetness()>.9f);
            Test->TestTrue(TEXT("Wetness reaches actual player physics"),Car->GetPhysics()->GetRoadWetness()>.9f);
            for (TActorIterator<AADVehiclePawn> It(World);It;++It)
                Test->TestTrue(TEXT("Registered traffic/police share wet grip"),FMath::IsNearlyEqual(It->GetPhysics()->GetRoadWetness(),Weather->GetWetness(),.02f));
            Weather->SetWeather(EADWeather::Clear);
            for (int32 I=0;I<600;++I) Weather->Tick(.25f);
            Test->TestTrue(TEXT("Clear weather dries the road"),Weather->GetWetness()<.02f && Weather->GetRainIntensity()<.01f);
            Weather->SetWeather(EADWeather::Fog);
            for (int32 I=0;I<80;++I) Weather->Tick(.25f);
            Test->TestTrue(TEXT("Fog transition completes"),Weather->CaptureWorldSnapshot().FogAmount>.99f);
            Test->TestTrue(TEXT("Clock/weather snapshot restores"),Weather->RestoreWorldSnapshot(Original));
            Weather->SetFrozen(true);
            Test->TestTrue(TEXT("Pursuit starts with physical units"),Police->StartPursuit());
            Test->TestEqual(TEXT("Initial heat is one"),Police->GetHeat(),1);
            Test->TestFalse(TEXT("Garage rejects active pursuit"),PC->GetGarageSession()->Enter());
            const FString RaceBeforePursuit=Mode->GetRaceManager()->GetDefinition().Id;
            Test->TestFalse(TEXT("Race rejects active pursuit"),Mode->GetRaceManager()->StartRace(Car,1));
            Test->TestTrue(TEXT("Rejected race preserves idle state and police units"),
                Mode->GetRaceManager()->GetState()==EADRaceState::Idle && Mode->GetRaceManager()->GetRacers().IsEmpty()
                && Police->IsActive() && Police->GetUnitCount()==2);
            Test->TestEqual(TEXT("Rejected race preserves selected catalog route"),Mode->GetRaceManager()->GetDefinition().Id,RaceBeforePursuit);
            for (TActorIterator<AADVehiclePawn> It(World);It;++It)
                if (It->GetOwner()==Police) UnitStarts.Add(*It,It->GetActorLocation());
            Stage=2; StageTime=World->GetTimeSeconds(); return false;
        }
        if (Stage==2 && Elapsed>=2.)
        {
            bool bMoved=false;
            for (const auto& Pair:UnitStarts) if (Pair.Key.IsValid() && FVector::Dist2D(Pair.Value,Pair.Key->GetActorLocation())>100.) bMoved=true;
            Test->TestTrue(TEXT("Interceptors move under vehicle physics"),bMoved);
            Test->TestTrue(TEXT("Pursuit unit budget holds"),Police->GetUnitCount()<=3);
            // Controlled separation tests actual LOS/search logic without adding
            // a gameplay teleport or claiming a human-driven pursuit acceptance.
            Car->PlaceForRace(FTransform(FRotator(0,90,0),FVector(82000,-18000,90)));
            Stage=3; StageTime=World->GetTimeSeconds(); return false;
        }
        if (Stage==3 && Elapsed>=4.)
        {
            Test->TestEqual(TEXT("Lost contact enters timed search"),Police->GetState(),EADPoliceState::Search);
            Test->TestTrue(TEXT("Search remains active instead of immediate escape"),Police->GetSearchRemainingSeconds()>20.f);
            Test->TestTrue(TEXT("Scenery streams within the cell budget"),Regions->GetLoadedCellCount()>0 && Regions->GetLoadedCellCount()<=24);
            Test->TestTrue(TEXT("District cells instantiate imported Kenney building meshes"),
                Regions->GetLoadedKenneyInstanceCount()>0);
            Stage=4; StageTime=World->GetTimeSeconds(); return false;
        }
        if (Stage==4 && Elapsed>=31.)
        {
            Test->TestEqual(TEXT("Search expiration enters cooldown"),Police->GetState(),EADPoliceState::Cooldown);
            Test->TestEqual(TEXT("Escaping clears heat"),Police->GetHeat(),0);
            Test->TestTrue(TEXT("Race starts after escape"),Mode->GetRaceManager()->StartRace(Car,1));
            Test->TestEqual(TEXT("Race removes free-roam traffic"),Traffic->GetTrafficCount(),0);
            Test->TestEqual(TEXT("Race removes police units"),Police->GetUnitCount(),0);
            Mode->GetRaceManager()->LeaveRace();
            Stage=5; StageTime=World->GetTimeSeconds(); return false;
        }
        if (Stage==5 && Elapsed>=4.)
        {
            Test->TestTrue(TEXT("Leaving race repopulates bounded traffic"),Traffic->GetTrafficCount()>0 && Traffic->GetTrafficCount()<=4);
            Police->SetEnabled(false); Traffic->SetEnabled(false); Weather->SetFrozen(false);
            return true;
        }
        return false;
    }
    bool PresentationScenario(UWorld* World,AADPlayerController* PC,AADVehiclePawn* Car,UPrimitiveComponent* Chassis)
    {
        auto* Cinematic=PC->GetCinematic();
        const double Elapsed=World->GetTimeSeconds()-StageTime;
        if (Stage==1 && Elapsed>=3.)
        {
            ReturnPose=Car->GetActorTransform(); ReturnVelocity=Chassis->GetPhysicsLinearVelocity();
            if (!Test->TestTrue(TEXT("Photo opens in offline drive"),Cinematic->EnterPhoto())) { Test->AddError(Cinematic->GetMessage()); return true; }
            Test->TestTrue(TEXT("Photo pauses scoring and simulation time"),PC->IsPaused());
            PausedTime=World->GetTimeSeconds(); Stage=2; return false;
        }
        if (Stage==2)
        {
            Test->TestEqual(TEXT("World time remains frozen in photo"),World->GetTimeSeconds(),PausedTime);
            Cinematic->ToggleHidden(); Test->TestTrue(TEXT("Photo UI can be hidden"),Cinematic->IsHudHidden());
            Cinematic->Leave();
            Test->TestFalse(TEXT("Leaving photo resumes drive"),PC->IsPaused());
            Test->TestTrue(TEXT("Photo preserves position"),Car->GetActorTransform().Equals(ReturnPose,.001));
            Test->TestTrue(TEXT("Photo preserves physical velocity"),Chassis->GetPhysicsLinearVelocity().Equals(ReturnVelocity,.01));
            if (!Test->TestTrue(TEXT("Replay has a continuous recorded segment"),Cinematic->EnterReplay())) { Test->AddError(Cinematic->GetMessage()); return true; }
            Test->TestTrue(TEXT("Replay is bounded to 60 seconds"),Cinematic->GetReplayDuration()>=2. && Cinematic->GetReplayDuration()<=60.);
            Test->TestFalse(TEXT("Replay suspends live chassis simulation"),Chassis->IsSimulatingPhysics());
            Test->TestFalse(TEXT("Replay suspends tire forces"),Car->GetPhysics()->IsComponentTickEnabled());
            Cinematic->CycleCamera(); Cinematic->TogglePlayback(); Stage=3; return false;
        }
        if (Stage==3)
        {
            Cinematic->Leave();
            Test->TestFalse(TEXT("Leaving replay resumes world"),PC->IsPaused());
            Test->TestTrue(TEXT("Replay restores exact drive pose"),Car->GetActorTransform().Equals(ReturnPose,.001));
            Test->TestTrue(TEXT("Replay restores physical velocity"),Chassis->GetPhysicsLinearVelocity().Equals(ReturnVelocity,.01));
            Test->TestTrue(TEXT("Replay restores chassis and force simulation"),Chassis->IsSimulatingPhysics() && Car->GetPhysics()->IsComponentTickEnabled());
            Cinematic->NotifyRecordingDiscontinuity();
            Test->TestFalse(TEXT("Recovery cannot replay across a teleport"),Cinematic->EnterReplay());
            if (!Test->TestTrue(TEXT("Arrival story sequence starts"),Cinematic->PlayArrivalCutscene()))
            { Test->AddError(Cinematic->GetMessage()); return true; }
            Test->TestFalse(TEXT("Story presentation keeps the city simulation running"),PC->IsPaused());
            Test->TestFalse(TEXT("Story presentation suspends player vehicle controls"),Car->IsDrivingEnabled());
            Test->TestFalse(TEXT("Story presentation holds the player chassis safely"),Chassis->IsSimulatingPhysics());
            Test->TestFalse(TEXT("Story presentation suspends tire-force updates"),Car->GetPhysics()->IsComponentTickEnabled());
            StoryCameraStart=Cinematic->GetCinematicCameraTransform();
            StoryVehicleStart=Car->GetActorTransform();
            StoryWorldStart=World->GetTimeSeconds();
            StoryWallStart=FPlatformTime::Seconds();
            Stage=4; StageTime=StoryWallStart; return false;
        }
        if (Stage==4 && FPlatformTime::Seconds()-StageTime>=1.25)
        {
            const FTransform CurrentCamera=Cinematic->GetCinematicCameraTransform();
            Test->TestTrue(TEXT("Realtime story camera travels through the opening shot"),
                FVector::Dist(StoryCameraStart.GetLocation(),CurrentCamera.GetLocation())>100.f);
            Test->TestTrue(TEXT("World time advances behind the moving cutscene"),World->GetTimeSeconds()>StoryWorldStart+.8);
            Test->TestTrue(TEXT("Cinematic freeze holds vehicle pose while the city moves"),Car->GetActorTransform().Equals(StoryVehicleStart,.01));
            Test->TestEqual(TEXT("Arrival begins on its first authored camera shot"),Cinematic->GetStoryShotIndex(),1);
            Stage=5; StageTime=FPlatformTime::Seconds(); return false;
        }
        if (Stage==5 && FPlatformTime::Seconds()-StageTime>=4.0)
        {
            Test->TestTrue(TEXT("Story cutscene progresses to a second camera shot"),Cinematic->GetStoryShotIndex()>=2);
            Stage=6; return false;
        }
        if (Stage==6 && FPlatformTime::Seconds()-StoryWallStart>=14.5)
        {
            Test->TestFalse(TEXT("Arrival story sequence completes without input"),Cinematic->IsActive());
            Test->TestFalse(TEXT("Story completion leaves the world unpaused"),PC->IsPaused());
            Test->TestTrue(TEXT("Story completion restores vehicle physics and force updates"),
                Chassis->IsSimulatingPhysics() && Car->GetPhysics()->IsComponentTickEnabled());
            Test->TestTrue(TEXT("Story completion restores driving controls"),Car->IsDrivingEnabled());
            Test->TestTrue(TEXT("Story completion preserves the original vehicle pose"),Car->GetActorTransform().Equals(StoryVehicleStart,.01));
            return true;
        }
        return false;
    }
    FAutomationTestBase* Test;
    bool bWorld=false,bFixed=false,bPreviousFixed=false;
    double Deadline=0,PreviousStep=0,StageTime=0;
    double PausedTime=0;
    double StoryWorldStart=0.;
    double StoryWallStart=0.;
    int32 Stage=0;
    FTransform ReturnPose;
    FTransform StoryCameraStart,StoryVehicleStart;
    FVector ReturnVelocity;
    TMap<TWeakObjectPtr<AADVehiclePawn>,FVector> UnitStarts;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADLivingWorldTest,"Afterdark.Runtime.LivingWorld",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADLivingWorldTest::RunTest(const FString&)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FIntegratedCommand(this,true)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADPhotoReplayTest,"Afterdark.Runtime.PhotoReplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADPhotoReplayTest::RunTest(const FString&)
{
    FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Velocity/Maps/L_Dockside"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FIntegratedCommand(this,false)));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
