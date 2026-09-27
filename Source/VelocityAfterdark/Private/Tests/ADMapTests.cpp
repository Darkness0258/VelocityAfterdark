#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Presentation/ADMapComponent.h"
#include "Racing/ADRaceCatalog.h"
#include "World/ADRoadNetwork.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADMapProjectionTest,"Afterdark.Map.ZoomPanProjection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADMapProjectionTest::RunTest(const FString&)
{
    const FVector2D Point(-82000.,62000.);
    const FVector2D Pan(12000.,-5000.);
    for (const float Zoom:{.65f,1.f,1.8f,2.4f})
    {
        const FVector2D Canvas=ADMapLayout::Project(Point,Zoom,Pan);
        const FVector2D Recovered=ADMapLayout::Unproject(Canvas,Zoom,Pan);
        TestTrue(FString::Printf(TEXT("Projection round-trips at %.2fx zoom"),Zoom),Recovered.Equals(Point,.01));
    }

    FADRoadNetwork Roads;
    FString Error;
    if (!TestTrue(TEXT("Map uses the validated open-world road graph"),Roads.LoadDefault(Error)))
    { AddError(Error); return false; }
    TArray<FVector2D> HomeRoute;
    double HomeDistance=0.;
    TestTrue(TEXT("Home garage start projects onto its nearby road"),
        Roads.BuildRoute(FVector2D(0.,400.),FVector2D(0.,400.),HomeRoute,HomeDistance,Error) && !HomeRoute.IsEmpty());
    if (!HomeRoute.IsEmpty()) TestTrue(TEXT("Home garage pin remains within the road corridor"),
        FVector2D::Distance(HomeRoute[0],FVector2D(0.,400.))<1500.);
    FADRaceCatalog Races;
    if (!TestTrue(TEXT("Race start pins resolve from the existing race catalog"),Races.LoadDefault(Error)))
    { AddError(Error); return false; }
    int32 ValidStarts=0;
    for (const FADRaceDefinition& Race:Races.GetRaces())
    {
        if (Race.Checkpoints.IsEmpty()) continue;
        TArray<FVector2D> Route;
        double DistanceCm=0.;
        if (Roads.BuildRoute(FVector2D(Race.Checkpoints[0].Location),FVector2D(Race.Checkpoints[0].Location),Route,DistanceCm,Error)
            && !Route.IsEmpty()) ++ValidStarts;
    }
    TestEqual(TEXT("All authored race starts can be shown as map pins"),ValidStarts,Races.GetRaces().Num());
    return true;
}
#endif
