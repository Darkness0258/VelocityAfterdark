#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/ADRaceRules.h"
#include "Racing/ADRaceDefinition.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADRaceDataTest, "Afterdark.Data.RaceDefinition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FADRaceDataTest::RunTest(const FString& Parameters)
{
    const FString Path=FPaths::ProjectContentDir()/TEXT("Data/Races/dockside_circuit.json");
    FADRaceDefinition Definition;
    FString Error,Source;
    if (!TestTrue(TEXT("Production race loads"),Definition.LoadFromJson(Path,Error))) { AddError(Error); return false; }
    if (!TestTrue(TEXT("Read fixture source"),FFileHelper::LoadFileToString(Source,*Path))) return false;
    TestEqual(TEXT("Four grid slots"),Definition.Grid.Num(),4);
    TestEqual(TEXT("Three opponents"),Definition.Opponents.Num(),3);
    TestEqual(TEXT("Seven directed gates"),Definition.Checkpoints.Num(),7);
    TestTrue(TEXT("Two kilometre circuit length"),FMath::IsNearlyEqual(Definition.RouteLengthM,2047.95785,.001));
    TestTrue(TEXT("Route wraps backwards"),Definition.PointAtDistance(-10.).Equals(Definition.PointAtDistance(Definition.RouteLengthM-10.),.001));
    double StartProjectionError=0.;
    TestTrue(TEXT("Closed-loop seam projects to distance zero"),FMath::IsNearlyZero(
        Definition.ClosestDistanceM(FVector(0.,0.,80.),StartProjectionError),.001) && StartProjectionError<.001);
    for (const auto& Gate : Definition.Checkpoints)
    {
        double DistanceError=0.;
        TestTrue(TEXT("Gate distance projects onto production route"),FMath::IsNearlyEqual(
            Definition.ClosestDistanceM(Gate.Location,DistanceError),Gate.DistanceM,.01));
        TestTrue(TEXT("Gate lies on route"),DistanceError<.001);
    }
    const FString BadPath=FPaths::CreateTempFilename(*(FPaths::ProjectSavedDir()/TEXT("Automation")),TEXT("race-"),TEXT(".json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BadPath),true);
    const auto Reject=[&](const TCHAR* Label,TFunctionRef<void(TSharedPtr<FJsonObject>)> Mutate)
    {
        TSharedPtr<FJsonObject> Object;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Object) || !Object.IsValid())
        { AddError(TEXT("Cannot create race fixture")); return; }
        Mutate(Object);
        FString Json;
        FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
        if (!TestTrue(TEXT("Write race fixture"),FFileHelper::SaveStringToFile(Json,*BadPath))) return;
        TestFalse(Label,Definition.LoadFromJson(BadPath,Error));
        TestFalse(TEXT("Invalid content explains failure"),Error.IsEmpty());
        TestEqual(TEXT("Rejected reload preserves previous identity"),Definition.Id,FString(TEXT("dockside_circuit_v1")));
        TestEqual(TEXT("Rejected reload preserves grid"),Definition.Grid.Num(),4);
    };
    Reject(TEXT("Unknown schema"),[](auto O){O->SetNumberField(TEXT("schemaVersion"),999);});
    Reject(TEXT("Numeric strings rejected"),[](auto O){O->SetStringField(TEXT("laps"),TEXT("2"));});
    Reject(TEXT("Fractional lap count rejected"),[](auto O){O->SetNumberField(TEXT("laps"),1.5);});
    Reject(TEXT("Impossible time limit rejected"),[](auto O){O->SetNumberField(TEXT("timeoutSeconds"),60);});
    Reject(TEXT("Degenerate route rejected"),[](auto O){auto A=O->GetArrayField(TEXT("routePoints"));A[1]=A[0];O->SetArrayField(TEXT("routePoints"),A);});
    Reject(TEXT("Backwards gate rejected"),[](auto O){O->GetArrayField(TEXT("checkpoints"))[0]->AsObject()->SetArrayField(TEXT("forward"),
        {MakeShared<FJsonValueNumber>(-1),MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(0)});});
    Reject(TEXT("False gate distance rejected"),[](auto O){O->GetArrayField(TEXT("checkpoints"))[1]->AsObject()->SetNumberField(TEXT("distanceM"),310);});
    Reject(TEXT("Overlapping grid rejected"),[](auto O){auto A=O->GetArrayField(TEXT("grid"));A[1]=A[0];O->SetArrayField(TEXT("grid"),A);});
    Reject(TEXT("Duplicate opponents rejected"),[](auto O){auto A=O->GetArrayField(TEXT("opponents"));A[1]=A[0];O->SetArrayField(TEXT("opponents"),A);});
    Reject(TEXT("Unsupported pace combination rejected"),[](auto O){O->GetArrayField(TEXT("opponents"))[0]->AsObject()->SetNumberField(TEXT("speedScale"),1.2);
        O->SetArrayField(TEXT("difficultySpeedScales"),{MakeShared<FJsonValueNumber>(.8),MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(1.2)});});
    Reject(TEXT("Lane outside driver envelope rejected"),[](auto O){O->GetArrayField(TEXT("opponents"))[0]->AsObject()->SetNumberField(TEXT("laneOffsetCm"),400);});
    Reject(TEXT("Unbounded driver personality rejected"),[](auto O){O->GetArrayField(TEXT("opponents"))[0]->AsObject()
        ->GetObjectField(TEXT("personality"))->SetNumberField(TEXT("overtakeAggression"),1.2);});
    IFileManager::Get().Delete(*BadPath);
    TestFalse(TEXT("Missing race rejected"),Definition.LoadFromJson(BadPath,Error));
    TestTrue(TEXT("Valid reload clears error"),Definition.LoadFromJson(Path,Error) && Error.IsEmpty());
    const FString SprintPath=FPaths::ProjectContentDir()/TEXT("Data/Races/afterdark_sprint.json");
    FADRaceDefinition Sprint;
    if (!TestTrue(TEXT("Open-road sprint loads"),Sprint.LoadFromJson(SprintPath,Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Sprint uses zero laps"),Sprint.Laps,0);
    const FVector2D SprintStart(-70950.,-25000.);
    const FVector2D SprintFinish(-40000.,-56546.0377860675);
    TestTrue(TEXT("Sprint retains its Sable road endpoint"),Sprint.PointAtDistance(6000.).Equals(SprintFinish,.001));
    TestTrue(TEXT("Sprint does not wrap behind its start"),Sprint.PointAtDistance(-100.).Equals(SprintStart,.001));
    double SprintStartError=0.;
    TestTrue(TEXT("Sprint start gate projects to route distance zero"),FMath::IsNearlyZero(
        Sprint.ClosestDistanceM(FVector(SprintStart.X,SprintStart.Y,80.),SprintStartError),.001) && SprintStartError<.001);
    double SprintError=0.;
    TestTrue(TEXT("Sprint projection retains the finish distance"),FMath::IsNearlyEqual(
        Sprint.ClosestDistanceM(FVector(SprintFinish.X,SprintFinish.Y,80.),SprintError),4800.,.001) && SprintError<.001);
    TArray<ADRaceRules::Gate> SprintGates;
    for (const auto& Gate : Sprint.Checkpoints)
        SprintGates.Add({{Gate.Location.X,Gate.Location.Y,Gate.Location.Z},
            {Gate.Forward.X,Gate.Forward.Y,Gate.Forward.Z},Gate.HalfWidthCm,Gate.HalfHeightCm});
    ADRaceRules::Progress Progress;
    TestTrue(TEXT("Point-to-point progress initializes without laps"),Progress.Reset(SprintGates.Num(),Sprint.Laps));
    TestTrue(TEXT("Grid sample is valid"),Progress.ResetSample({-71550.,-25000.,80.},0.));
    ADRaceRules::Event StartEvent=ADRaceRules::Event::None;
    ADRaceRules::Event MiddleEvent=ADRaceRules::Event::None;
    ADRaceRules::Event FinishEvent=ADRaceRules::Event::None;
    for (int32 Step=1;Step<=240;++Step)
    {
        const double DistanceM=Step*20.;
        const FVector2D Position=Sprint.PointAtDistance(DistanceM);
        const auto Event=Progress.Sample({Position.X,Position.Y,80.},Step,SprintGates.GetData(),SprintGates.Num());
        if (Event==ADRaceRules::Event::Started) StartEvent=Event;
        if (Event==ADRaceRules::Event::Checkpoint) MiddleEvent=Event;
        if (Event==ADRaceRules::Event::Finished) FinishEvent=Event;
    }
    TestEqual(TEXT("Start crossing starts the sprint"),StartEvent,ADRaceRules::Event::Started);
    TestEqual(TEXT("Middle gate advances route progress"),MiddleEvent,ADRaceRules::Event::Checkpoint);
    TestEqual(TEXT("Final gate ends the sprint"),FinishEvent,ADRaceRules::Event::Finished);
    TestTrue(TEXT("Sprint finishes once without accumulating laps"),Progress.Finished && Progress.CompletedLaps==0
        && Progress.FinishSeconds>=239. && Progress.FinishSeconds<=240.);
    return true;
}
#endif
