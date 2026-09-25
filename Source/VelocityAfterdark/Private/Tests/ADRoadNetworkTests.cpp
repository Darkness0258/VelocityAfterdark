#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "World/ADRoadNetwork.h"
#include "World/ADWorldProgress.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADRoadNetworkTest,"Afterdark.Data.RoadNetwork",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADRoadNetworkTest::RunTest(const FString&)
{
    FADRoadNetwork Network;
    FString Error;
    TArray<FVector2D> Route;
    double Distance=0.;
    const TArray<FADRoadNetworkSegment> Junctions={{{-1000,0},{1000,0}},{{0,-1000},{0,1000}},{{500,0},{500,500}}};
    if (!TestTrue(TEXT("Build crossings and a T-junction"),Network.Build(Junctions,Error))) return false;
    TestTrue(TEXT("Route across an interior crossing"),Network.BuildRoute({-800,30},{20,700},Route,Distance,Error));
    TestEqual(TEXT("Crossing route follows roads, including projected partial edges"),Distance,1500.);
    TestTrue(TEXT("Crossing route passes through junction"),Route.Contains(FVector2D(0,0)));
    TestTrue(TEXT("T-junction route"),Network.BuildRoute({900,0},{500,400},Route,Distance,Error));
    TestEqual(TEXT("T-junction length"),Distance,800.);
    TestTrue(TEXT("Two endpoints on one split edge"),Network.BuildRoute({100,25},{400,-25},Route,Distance,Error));
    TestEqual(TEXT("Same-edge shortcut retains exact partial length"),Distance,300.);
    TestTrue(TEXT("Identical endpoints"),Network.BuildRoute({100,25},{100,25},Route,Distance,Error));
    TestEqual(TEXT("Identical endpoints produce one projection"),Route.Num(),1);
    TestFalse(TEXT("Remote endpoint rejected"),Network.BuildRoute({0,50000},{0,0},Route,Distance,Error));
    TestTrue(TEXT("Failed query clears previous route"),Route.IsEmpty());
    TestEqual(TEXT("Failed query clears previous length"),Distance,0.);
    TestFalse(TEXT("Unsupported diagonal rejected without replacing graph"),Network.Build({{{0,0},{100,100}}},Error));
    TestTrue(TEXT("Previous graph survives rejected replacement"),Network.BuildRoute({900,0},{500,400},Route,Distance,Error));
    TestEqual(TEXT("Previous graph route remains unchanged"),Distance,800.);
    TestTrue(TEXT("Collinear overlap and duplicate roads"),Network.Build({{{0,0},{1000,0}},{{200,0},{800,0}},{{1000,0},{0,0}},{{500,0},{500,600}}},Error));
    TestTrue(TEXT("Overlapping roads remain connected"),Network.BuildRoute({100,0},{500,500},Route,Distance,Error));
    TestEqual(TEXT("Overlaps do not inflate route cost"),Distance,900.);
    TestTrue(TEXT("Disconnected graph can be represented"),Network.Build({{{0,0},{1000,0}},{{0,1000},{1000,1000}}},Error));
    TestFalse(TEXT("Disconnected route fails explicitly"),Network.BuildRoute({500,0},{500,1000},Route,Distance,Error));
    TestTrue(TEXT("Production catalogs load"),Network.LoadDefault(Error));
    TArray<FADDiscoveryDefinition> Locations;
    TestTrue(TEXT("Production discoveries load"),FADDiscoveryDefinition::LoadCatalog(FPaths::ProjectContentDir()/TEXT("Data/World/discoveries.json"),Locations,Error));
    for (const auto& Location:Locations)
    {
        TestTrue(*FString::Printf(TEXT("Road route reaches %s"),*Location.Id),Network.BuildRoute({0,400},Location.Position,Route,Distance,Error));
        if (!Route.IsEmpty()) TestTrue(TEXT("Discovery lies on its reachable road"),FVector2D::Distance(Route.Last(),Location.Position)<=Location.RadiusCm);
    }
    return true;
}
#endif
