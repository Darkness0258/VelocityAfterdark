#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Career/ADCareerSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Racing/ADRaceCatalog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FCareerContentFixture
{
    FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CareerRoutes") /
        FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TArray<FString> WrittenFiles;
    FCareerContentFixture() { IFileManager::Get().MakeDirectory(*Directory,true); }
    ~FCareerContentFixture()
    {
        for (const FString& Path : WrittenFiles) IFileManager::Get().Delete(*Path,false,true);
        IFileManager::Get().DeleteDirectory(*Directory,false,false);
    }
    bool Write(const FString& Name,const FString& Contents)
    {
        const FString Path = Directory / Name;
        WrittenFiles.AddUnique(Path);
        return FFileHelper::SaveStringToFile(Contents,*Path);
    }
    bool WriteJson(const FString& Name,const TSharedPtr<FJsonObject>& Object)
    {
        FString Text;
        return FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<>::Create(&Text)) && Write(Name,Text);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADCareerRouteCatalogTest,"Afterdark.Data.RaceCatalog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADCareerRouteCatalogTest::RunTest(const FString&)
{
    FADRaceCatalog Catalog;
    FString Error;
    if (!TestTrue(TEXT("All authored courses load with road coverage"),Catalog.LoadDefault(Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Nine distinct courses"),Catalog.GetRaces().Num(),9);
    TSet<FString> Ids;
    for (const auto& Race : Catalog.GetRaces())
    {
        Ids.Add(Race.Id);
        TestEqual(TEXT("Career races contain three opponents"),Race.Opponents.Num(),3);
        TestEqual(TEXT("Four physical starting positions"),Race.Grid.Num(),4);
        TestTrue(TEXT("Event has a drivable kilometre-scale route"),Race.RouteLengthM>1000.);
        TestTrue(TEXT("Event has a route-specific grade par under its timeout"),Race.ParSeconds>30. && Race.ParSeconds<Race.TimeoutSeconds);
    }
    TestEqual(TEXT("Route identities do not alias"),Ids.Num(),Catalog.GetRaces().Num());
    const auto* Sprint=Catalog.Find(TEXT("afterdark_sprint_v1"));
    if (!TestNotNull(TEXT("Point-to-point sprint is catalogued"),Sprint)) return false;
    TestEqual(TEXT("Sprint has no laps"),Sprint->Laps,0);
    TestTrue(TEXT("Sprint route is open and ends at 4.8 km"),FMath::IsNearlyEqual(Sprint->RouteLengthM,4800.,.01));
    TestTrue(TEXT("Sprint finish differs from its start"),!Sprint->PointAtDistance(4800.).Equals(Sprint->PointAtDistance(0.),.001));
    TestTrue(TEXT("Sprint clamps beyond the finish instead of wrapping"),
        Sprint->PointAtDistance(4900.).Equals(Sprint->PointAtDistance(4800.),.001));
    const auto* Final=Catalog.Find(TEXT("afterdark_final_v1"));
    if (!TestNotNull(TEXT("City final is catalogued"),Final)) return false;
    TestEqual(TEXT("Final is one continuous lap"),Final->Laps,1);
    TestTrue(TEXT("Final spans over five kilometres"),Final->RouteLengthM>5000.);
    TestNull(TEXT("Unknown IDs never resolve to an arbitrary race"),Catalog.Find(TEXT("missing_race")));

    FCareerContentFixture Fixture;
    const FString SourceDirectory=FPaths::ProjectContentDir()/TEXT("Data/Races");
    const FString WorldDirectory=FPaths::ProjectContentDir()/TEXT("Data/World");
    FString Source;
    if (!TestTrue(TEXT("Read production catalog"),FFileHelper::LoadFileToString(Source,*(SourceDirectory/TEXT("catalog.json"))))) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Root)) return false;
    for (const auto& Entry : Root->GetArrayField(TEXT("races")))
    {
        const FString Name=Entry->AsObject()->GetStringField(TEXT("file"));
        FString Contents;
        if (!FFileHelper::LoadFileToString(Contents,*(SourceDirectory/Name)) || !Fixture.Write(Name,Contents)) return false;
    }
    const auto LoadFixture = [&]()
    {
        return Catalog.LoadFromJson(Fixture.Directory/TEXT("catalog.json"),WorldDirectory/TEXT("dockside.json"),
            WorldDirectory/TEXT("regions.json"),Error);
    };
    const auto Reject=[&](const TCHAR* Label,TFunctionRef<void(TSharedPtr<FJsonObject>)> Mutate)
    {
        TSharedPtr<FJsonObject> Object;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Object)) { AddError(TEXT("Malformed fixture source")); return; }
        Mutate(Object);
        if (!Fixture.WriteJson(TEXT("catalog.json"),Object)) { AddError(TEXT("Cannot write isolated catalog fixture")); return; }
        TestFalse(Label,LoadFixture());
        TestFalse(TEXT("Failure explains unavailable event content"),Error.IsEmpty());
        TestEqual(TEXT("Failed reload preserves valid catalog"),Catalog.GetRaces().Num(),9);
        TestNotNull(TEXT("Failed reload preserves final event"),Catalog.Find(TEXT("afterdark_final_v1")));
    };
    Reject(TEXT("Unknown schema rejected"),[](auto O){O->SetNumberField(TEXT("schemaVersion"),2);});
    Reject(TEXT("Duplicate identities rejected"),[](auto O){auto A=O->GetArrayField(TEXT("races")); A[1]=A[0]; O->SetArrayField(TEXT("races"),A);});
    Reject(TEXT("Path traversal rejected"),[](auto O){O->GetArrayField(TEXT("races"))[0]->AsObject()->SetStringField(TEXT("file"),TEXT("../World/dockside.json"));});
    Reject(TEXT("Unknown file rejected"),[](auto O){O->GetArrayField(TEXT("races"))[0]->AsObject()->SetStringField(TEXT("file"),TEXT("missing_race.json"));});
    Reject(TEXT("Catalog and definition IDs must agree"),[](auto O){O->GetArrayField(TEXT("races"))[0]->AsObject()->SetStringField(TEXT("id"),TEXT("wrong_identity"));});
    if (!Fixture.Write(TEXT("catalog.json"),Source)) return false;
    FString CourseSource;
    FFileHelper::LoadFileToString(CourseSource,*(SourceDirectory/TEXT("foundry_shift.json")));
    TSharedPtr<FJsonObject> Course;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CourseSource),Course)) return false;
    // Translating an otherwise valid circuit preserves its local distances and
    // direction, but moves the straights away from actual asphalt.
    for (const TCHAR* Field : {TEXT("routePoints"),TEXT("grid")})
    {
        TArray<TSharedPtr<FJsonValue>> Shifted;
        for (const auto& Value : Course->GetArrayField(Field))
        {
            auto Values=Value->AsArray();
            Values[1]=MakeShared<FJsonValueNumber>(Values[1]->AsNumber()+5000.);
            Shifted.Add(MakeShared<FJsonValueArray>(Values));
        }
        Course->SetArrayField(Field,Shifted);
    }
    for (const auto& Value : Course->GetArrayField(TEXT("checkpoints")))
    {
        auto Location=Value->AsObject()->GetArrayField(TEXT("location"));
        Location[1]=MakeShared<FJsonValueNumber>(Location[1]->AsNumber()+5000.);
        Value->AsObject()->SetArrayField(TEXT("location"),Location);
    }
    if (!Fixture.WriteJson(TEXT("foundry_shift.json"),Course)) return false;
    TestFalse(TEXT("Geometrically valid course away from paved roads rejected"),LoadFixture());
    TestTrue(TEXT("Off-road rejection identifies coverage"),Error.Contains(TEXT("paved road")));
    TestEqual(TEXT("Off-road reload preserves previous content"),Catalog.GetRaces().Num(),9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADCareerProgressionRouteTest,"Afterdark.Career.RouteProgression",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADCareerProgressionRouteTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UADCareerSubsystem> Career(NewObject<UADCareerSubsystem>(Instance.Get()));
    FString Error,Source;
    const FString CareerPath=FPaths::ProjectContentDir()/TEXT("Data/Career/career.json");
    if (!TestTrue(TEXT("Career resolves every chapter to a real event"),Career->LoadCatalog(CareerPath,Error))) { AddError(Error); return false; }
    FADGarageProfile Profile;
    TSet<FString> Routes;
    FADCareerReward Reward;
    for (const auto& Chapter : Career->GetChapters())
    {
        const auto* Active=Career->GetActiveChapter(Profile);
        if (!TestNotNull(TEXT("Next chapter exists"),Active)) return false;
        TestEqual(TEXT("Completion order determines active chapter"),Active->Id,Chapter.Id);
        Routes.Add(Chapter.RaceId);
        TestTrue(TEXT("Classified runner-up receives only a stipend"),Career->ComputeReward(Profile,Chapter.Id,2,Reward,Error)
            && Reward.Credits==Chapter.FinishCredits && Reward.Rep==0 && !Reward.bAdvance);
        TestFalse(TEXT("Invalid finishing place receives no reward"),Career->ComputeReward(Profile,Chapter.Id,0,Reward,Error));
        TestTrue(TEXT("Failure clears reward output"),Reward.Credits==0 && Reward.Rep==0 && Reward.ChapterId.IsEmpty());
        if (!TestTrue(TEXT("Current winner receives chapter reward"),Career->ComputeReward(Profile,Chapter.Id,1,Reward,Error))) return false;
        Profile.Reputation+=Reward.Rep;
        Profile.CompletedChapters.Add(Reward.ChapterId);
        TestFalse(TEXT("Completed chapter cannot reward again"),Career->ComputeReward(Profile,Chapter.Id,1,Reward,Error));
    }
    TestEqual(TEXT("Every chapter uses a distinct route"),Routes.Num(),Career->GetChapters().Num());
    TestEqual(TEXT("Full career awards 8000 REP"),Profile.Reputation,int64(8000));
    TestNull(TEXT("Completed series has no active event"),Career->GetActiveChapter(Profile));
    TestEqual(TEXT("Final rank is earned"),Career->GetRankName(Profile.Reputation),FString(TEXT("Afterdark Champion")));
    const auto& Chapters=Career->GetChapters();
    FADRaceCatalog RaceCatalog;
    if (!TestTrue(TEXT("Rival appearances resolve through the production race catalog"),RaceCatalog.LoadDefault(Error)))
    { AddError(Error); return false; }
    TestEqual(TEXT("Dockside opens with Iron Wolves leader Dax Kerr"),Chapters[0].Crew,FString(TEXT("Iron Wolves")));
    TestEqual(TEXT("Ivo Renn appears across four chapters"),
        Chapters.FilterByPredicate([](const auto& Item){ return Item.Leader==TEXT("Ivo Renn")
            || Item.Briefing.Contains(TEXT("Ivo Renn")) || Item.VictoryLine.Contains(TEXT("Ivo Renn")); }).Num(),4);
    TestEqual(TEXT("Sel Arden appears in two chapters and the final invitation"),
        Chapters.FilterByPredicate([](const auto& Item){ return Item.Leader==TEXT("Sel Arden")
            || Item.Briefing.Contains(TEXT("Sel Arden")) || Item.VictoryLine.Contains(TEXT("Sel Arden")); }).Num(),3);
    TestTrue(TEXT("Opening briefing brings Mara Venn into the story"),Chapters[0].Briefing.Contains(TEXT("Mara Venn")));
    TestTrue(TEXT("Final opens with Sel Arden's invitation"),Chapters.Last().Briefing.Contains(TEXT("Sel Arden")));
    TestTrue(TEXT("Sable Ring hands the rivalry forward to Ivo"),Chapters[6].VictoryLine.Contains(TEXT("Ivo steps forward")));
    TestTrue(TEXT("Final victory names the player as champion"),Chapters.Last().VictoryLine.Contains(TEXT("Afterdark Champion")));
    const auto HasDriver = [&RaceCatalog,&Chapters,this](const TCHAR* ChapterId,const TCHAR* DriverId)
    {
        const FADCareerChapter* Chapter=Chapters.FindByPredicate([ChapterId](const auto& Item){return Item.Id==ChapterId;});
        const FADRaceDefinition* Race=Chapter ? RaceCatalog.Find(Chapter->RaceId) : nullptr;
        return TestTrue(FString::Printf(TEXT("%s is a physical entrant in %s"),DriverId,ChapterId),
            Race && Race->Opponents.ContainsByPredicate([DriverId](const FADRaceOpponent& Opponent){return Opponent.Id==DriverId;}));
    };
    HasDriver(TEXT("arrival"),TEXT("dax_kerr"));
    HasDriver(TEXT("dockside_regular"),TEXT("ivo_renn"));
    HasDriver(TEXT("iron_chord"),TEXT("ivo_renn"));
    HasDriver(TEXT("night_survey"),TEXT("sel_arden"));
    HasDriver(TEXT("slipstream_union"),TEXT("sel_arden"));
    HasDriver(TEXT("glass_hour"),TEXT("reya_voss"));
    HasDriver(TEXT("invitation"),TEXT("wren_ashby"));
    HasDriver(TEXT("invitation"),TEXT("ivo_renn"));
    HasDriver(TEXT("afterdark_final"),TEXT("ivo_renn"));
    HasDriver(TEXT("afterdark_final"),TEXT("sel_arden"));

    FADGarageProfile RivalProfile;
    FADRivalMemory IvoMemory;
    IvoMemory.RivalId=TEXT("ivo_renn"); IvoMemory.Encounters=2; IvoMemory.PlayerWins=1; IvoMemory.RivalWins=1;
    IvoMemory.Respect=.62f; IvoMemory.Grudge=.08f;
    RivalProfile.RivalMemories.Add(IvoMemory);
    const FADCareerChapter& Invitation=Chapters[6];
    const FADCareerChapter& Finale=Chapters.Last();
    const auto* InvitationRace=RaceCatalog.Find(Invitation.RaceId);
    const auto* FinaleRace=RaceCatalog.Find(Finale.RaceId);
    TestTrue(TEXT("Ivo memory changes the Sable Ring briefing"),InvitationRace
        && Career->ComposeBriefing(RivalProfile,Invitation,*InvitationRace).Contains(TEXT("Ivo Renn remembers your tied series")));
    TestTrue(TEXT("Ivo memory appears in the finale victory metadata"),FinaleRace
        && Career->ComposeVictoryLine(RivalProfile,Finale,*FinaleRace).Contains(TEXT("Ivo Renn series: 1 to 1")));
    FADRivalMemory SelMemory;
    SelMemory.RivalId=TEXT("sel_arden"); SelMemory.Encounters=1; SelMemory.PlayerWins=0; SelMemory.RivalWins=1;
    RivalProfile.RivalMemories.Reset(); RivalProfile.RivalMemories.Add(SelMemory);
    TestTrue(TEXT("Sel memory changes the finale invitation"),FinaleRace
        && Career->ComposeBriefing(RivalProfile,Finale,*FinaleRace).Contains(TEXT("Sel Arden beat you last time")));
    Swap(Profile.CompletedChapters[0],Profile.CompletedChapters[1]);
    TestNull(TEXT("Reordered save progress is rejected"),Career->GetActiveChapter(Profile));

    if (!FFileHelper::LoadFileToString(Source,*CareerPath)) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Root)) return false;
    Root->GetArrayField(TEXT("chapters"))[0]->AsObject()->SetStringField(TEXT("raceId"),TEXT("missing_event"));
    FCareerContentFixture Fixture;
    if (!Fixture.WriteJson(TEXT("career.json"),Root)) return false;
    TestFalse(TEXT("Unavailable event reference rejects career reload"),Career->LoadCatalog(Fixture.Directory/TEXT("career.json"),Error));
    TestEqual(TEXT("Rejected reference preserves current valid first chapter"),Career->GetChapters()[0].RaceId,FString(TEXT("dockside_circuit_v1")));
    return true;
}
#endif
