#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Ownership/ADOwnershipSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FProfileFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
    UADOwnershipSubsystem* Service = NewObject<UADOwnershipSubsystem>(Instance.Get());
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Ownership")/
        (FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".json")));
    FProfileFixture() { IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true); }
    ~FProfileFixture()
    {
        // Only the five exact filenames reserved by this fixture are removed.
        for (const TCHAR* Suffix : {TEXT(""),TEXT(".bak"),TEXT(".tmp"),TEXT(".bak.tmp"),TEXT(".blocker")})
            IFileManager::Get().Delete(*(Path+Suffix),false,true);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADOwnershipTransactionTest,"Afterdark.Ownership.Transactions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADOwnershipTransactionTest::RunTest(const FString&)
{
    FProfileFixture F;
    FString Error;
    if (!TestTrue(TEXT("Initialize isolated profile"),F.Service->InitializeProfile(F.Path,Error))) { AddError(Error); return false; }
    auto Draft=F.Service->GetProfile();
    const auto& Part=F.Service->GetUpgrades()[0];
    Draft.OwnedUpgrades.Add(Part.Id); Draft.EquippedUpgrades.Add(Part.Id);
    const int64 Expected=Draft.Credits-Part.Price;
    Draft.Credits=99999999;
    TestTrue(TEXT("Purchase commits"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Caller cannot invent credit balance"),F.Service->GetProfile().Credits,Expected);
    TestTrue(TEXT("Repeated purchase is idempotent"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("No duplicate charge"),F.Service->GetProfile().Credits,Expected);
    TestTrue(TEXT("Profile reload"),F.Service->InitializeProfile(F.Path,Error));
    TestEqual(TEXT("Balance persists"),F.Service->GetProfile().Credits,Expected);
    TestTrue(TEXT("Equipment persists"),F.Service->GetProfile().EquippedUpgrades.Contains(Part.Id));
    const auto Derived=F.Service->BuildDefinition(F.Service->GetProfile());
    TestTrue(TEXT("Torque changes while stock remains immutable"),Derived.GetTorqueNm(4200)>
        F.Service->GetStockDefinition().GetTorqueNm(4200)*1.1f);
    Draft=F.Service->GetProfile(); Draft.EquippedUpgrades.Reset();
    TestTrue(TEXT("Removing owned equipment is free"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Unequip returns exactly stock torque"),F.Service->BuildDefinition(F.Service->GetProfile()).GetTorqueNm(4200),
        F.Service->GetStockDefinition().GetTorqueNm(4200));
    Draft.OwnedUpgrades.Reset();
    TestFalse(TEXT("Ownership cannot be removed to buy again"),F.Service->Commit(Draft,Error));
    Draft=F.Service->GetProfile(); Draft.EquippedUpgrades.Add(TEXT("unknown_part"));
    TestFalse(TEXT("Unknown or unowned equipment rejected"),F.Service->Commit(Draft,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADOwnershipRecoveryTest,"Afterdark.Ownership.CorruptionRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADOwnershipRecoveryTest::RunTest(const FString&)
{
    FProfileFixture F; FString Error;
    if (!TestTrue(TEXT("Initialize"),F.Service->InitializeProfile(F.Path,Error))) return false;
    auto Draft=F.Service->GetProfile();
    TestTrue(TEXT("Write initial primary"),F.Service->Commit(Draft,Error));
    const FString OriginalPaint=Draft.PaintId;
    Draft.PaintId=F.Service->GetPaints()[1].Id;
    TestTrue(TEXT("Write second revision with last good backup"),F.Service->Commit(Draft,Error));
    TestTrue(TEXT("Stage an interrupted write fixture"),FFileHelper::SaveStringToFile(TEXT("{truncated"),*(F.Path+TEXT(".tmp"))));
    TestTrue(TEXT("Interrupted staging does not replace primary"),F.Service->InitializeProfile(F.Path,Error));
    TestEqual(TEXT("Most recent committed paint survives interruption"),F.Service->GetProfile().PaintId,Draft.PaintId);
    TestTrue(TEXT("Corrupt primary fixture"),FFileHelper::SaveStringToFile(TEXT("{truncated"),*F.Path));
    TestTrue(TEXT("Backup recovers corrupt primary"),F.Service->InitializeProfile(F.Path,Error));
    TestEqual(TEXT("Recovery uses previous good revision"),F.Service->GetProfile().PaintId,OriginalPaint);
    TestTrue(TEXT("Commit repairs primary"),F.Service->Commit(F.Service->GetProfile(),Error));
    TestTrue(TEXT("Repaired primary reloads"),F.Service->InitializeProfile(F.Path,Error));
    TestTrue(TEXT("Write future-schema fixture"),FFileHelper::SaveStringToFile(TEXT("{\"schemaVersion\":999}"),*F.Path));
    TestFalse(TEXT("Future schema never silently falls back and overwrites"),F.Service->InitializeProfile(F.Path,Error));
    TestFalse(TEXT("Saving remains blocked on future schema"),F.Service->Commit(Draft,Error));
    FString Preserved; FFileHelper::LoadFileToString(Preserved,*F.Path);
    TestTrue(TEXT("Unknown version file preserved"),Preserved.Contains(TEXT("999")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADOwnershipMigrationTest,"Afterdark.Ownership.MigrationAndRollback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADOwnershipMigrationTest::RunTest(const FString&)
{
    FProfileFixture F; FString Error;
    const FString Legacy=TEXT("{\"schemaVersion\":1,\"vehicleId\":\"aster_s6\",\"credits\":8750,\"paintId\":\"mint\",\"upgrades\":[\"ecu\"]}");
    TestTrue(TEXT("Write v1 fixture"),FFileHelper::SaveStringToFile(Legacy,*F.Path));
    if (!TestTrue(TEXT("Read and migrate v1"),F.Service->InitializeProfile(F.Path,Error))) return false;
    TestEqual(TEXT("Migration retains balance"),F.Service->GetProfile().Credits,int64(8750));
    TestTrue(TEXT("Legacy purchased parts become equipped"),F.Service->GetProfile().EquippedUpgrades.Contains(TEXT("ecu")));
    TestEqual(TEXT("Missing legacy tune defaults safely"),F.Service->GetProfile().TuneId,FString(TEXT("street")));
    auto Draft=F.Service->GetProfile(); Draft.PaintId=TEXT("pearl");
    TestTrue(TEXT("Commit migrated save"),F.Service->Commit(Draft,Error));
    FString Encoded; FFileHelper::LoadFileToString(Encoded,*F.Path);
    TestTrue(TEXT("Migrated save carries integrity envelope"),Encoded.Contains(TEXT("checksum")));
    // Simulate another process writing after we read. A stale session must not
    // overwrite that revision or charge credits on a rejected transaction.
    TestTrue(TEXT("Write external revision"),FFileHelper::SaveStringToFile(Legacy,*F.Path));
    Draft.OwnedUpgrades.Add(TEXT("tires")); Draft.EquippedUpgrades.Add(TEXT("tires"));
    const int64 Before=F.Service->GetProfile().Credits;
    TestFalse(TEXT("Concurrent stale save rejected"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Rejected transaction rolls back credits"),F.Service->GetProfile().Credits,Before);
    TestFalse(TEXT("Rejected transaction does not grant ownership"),F.Service->GetProfile().OwnedUpgrades.Contains(TEXT("tires")));
    // A file occupying the intended parent directory reliably prevents writes
    // even under elevated privileges; this does not rely on read-only attributes.
    const FString Blocker=F.Path+TEXT(".blocker");
    TestTrue(TEXT("Write path blocker"),FFileHelper::SaveStringToFile(TEXT("blocked"),*Blocker));
    TestTrue(TEXT("Initialize missing child"),F.Service->InitializeProfile(Blocker/TEXT("driver.json"),Error));
    Draft=F.Service->GetProfile(); Draft.OwnedUpgrades.Add(TEXT("ecu")); Draft.EquippedUpgrades.Add(TEXT("ecu"));
    TestFalse(TEXT("Actual filesystem write failure rejected"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Write failure preserves starting balance"),F.Service->GetProfile().Credits,int64(12000));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADWorldProgressTest,"Afterdark.Ownership.WorldProgress",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADWorldProgressTest::RunTest(const FString&)
{
    FProfileFixture F; FString Error;
    if (!TestTrue(TEXT("Initialize isolated world profile"),F.Service->InitializeProfile(F.Path,Error))) return false;
    FADWorldSnapshot World;
    World.bRecorded=true; World.Hour=5.75; World.Weather=1; World.WeatherIndex=1;
    World.WeatherElapsed=83.25; World.Wetness=.61f; World.RainAmount=.7f; World.FogAmount=.1f;
    F.Service->StageWorldSnapshot(World);
    const auto Location=F.Service->GetDiscoveries()[0];
    const int64 BeforeCredits=F.Service->GetProfile().Credits;
    TestTrue(TEXT("Discovery commits alongside staged world state"),F.Service->CommitDiscovery(Location.Id,Error));
    TestEqual(TEXT("Catalog controls discovery credit payout"),F.Service->GetProfile().Credits,BeforeCredits+Location.Credits);
    TestEqual(TEXT("Catalog controls discovery REP payout"),F.Service->GetProfile().Reputation,Location.Reputation);
    TestTrue(TEXT("Repeat discovery is idempotent"),F.Service->CommitDiscovery(Location.Id,Error));
    TestEqual(TEXT("No duplicate payout"),F.Service->GetProfile().Credits,BeforeCredits+Location.Credits);
    TestFalse(TEXT("Unknown discovery never grants credits"),F.Service->CommitDiscovery(TEXT("invented_location"),Error));
    TestTrue(TEXT("Reload schema 4 profile"),F.Service->InitializeProfile(F.Path,Error));
    TestEqual(TEXT("Discovery persists once"),F.Service->GetProfile().DiscoveredLocations.Num(),1);
    TestEqual(TEXT("World clock persists"),F.Service->GetProfile().World.Hour,World.Hour);
    TestEqual(TEXT("Weather phase persists"),F.Service->GetProfile().World.WeatherElapsed,World.WeatherElapsed);
    TestEqual(TEXT("Road wetness persists"),F.Service->GetProfile().World.Wetness,World.Wetness);
    TestEqual(TEXT("Weather transition persists"),F.Service->GetProfile().World.RainAmount,World.RainAmount);
    auto Draft=F.Service->GetProfile(); Draft.DiscoveredLocations.Reset(); Draft.World.Hour=12.;
    TestTrue(TEXT("Garage cannot erase discoveries or change world state"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Garage preserves discoveries"),F.Service->GetProfile().DiscoveredLocations.Num(),1);
    TestEqual(TEXT("Garage preserves staged clock"),F.Service->GetProfile().World.Hour,World.Hour);
    auto Invalid=World; Invalid.Hour=24.;
    F.Service->StageWorldSnapshot(Invalid);
    TestTrue(TEXT("Invalid staged snapshot does not poison save"),F.Service->SaveWorldSnapshot(Error));
    TestEqual(TEXT("Invalid clock rejected"),F.Service->GetProfile().World.Hour,World.Hour);
    const FString Blocker=F.Path+TEXT(".blocker");
    TestTrue(TEXT("Create unwritable parent fixture"),FFileHelper::SaveStringToFile(TEXT("blocked"),*Blocker));
    TestTrue(TEXT("Initialize new profile below blocked parent"),F.Service->InitializeProfile(Blocker/TEXT("driver.json"),Error));
    TestFalse(TEXT("Unwritable discovery is not committed"),F.Service->CommitDiscovery(Location.Id,Error));
    TestEqual(TEXT("Failed discovery grants no credits"),F.Service->GetProfile().Credits,int64(12000));
    TestTrue(TEXT("Failed discovery remains discoverable"),F.Service->GetProfile().DiscoveredLocations.IsEmpty());
    return true;
}
#endif
