#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Ownership/ADOwnershipSubsystem.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FVehicleProfileFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
    UADOwnershipSubsystem* Service=NewObject<UADOwnershipSubsystem>(Instance.Get());
    FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/VehicleOwnership")/
        (FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".json")));
    FVehicleProfileFixture() { IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true); }
    ~FVehicleProfileFixture()
    {
        for (const TCHAR* Suffix:{TEXT(""),TEXT(".bak"),TEXT(".tmp"),TEXT(".bak.tmp"),TEXT(".blocker")})
            IFileManager::Get().Delete(*(Path+Suffix),false,true);
    }
};
FString ProfileDigest(const FString& Text)
{
    const FTCHARToUTF8 Utf8(*Text);
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash);
    return BytesToHex(Hash,UE_ARRAY_COUNT(Hash));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADVehicleCollectionTest,"Afterdark.Ownership.VehicleCollection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADVehicleCollectionTest::RunTest(const FString&)
{
    FVehicleProfileFixture F; FString Error;
    if (!TestTrue(TEXT("Initialize vehicle profile"),F.Service->InitializeProfile(F.Path,Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Five validated fictional vehicles"),F.Service->GetVehicles().Num(),5);
    const auto* Starter=F.Service->FindVehicle(TEXT("aster_s6"));
    const auto* Hatch=F.Service->FindVehicle(TEXT("kestrel_xr"));
    const auto* Sedan=F.Service->FindVehicle(TEXT("meridian_gt"));
    const auto* Muscle=F.Service->FindVehicle(TEXT("ironwake_v8"));
    const auto* Hyper=F.Service->FindVehicle(TEXT("vespera_r9"));
    if (!TestNotNull(TEXT("Starter exists"),Starter) || !TestNotNull(TEXT("Hatch exists"),Hatch)
        || !TestNotNull(TEXT("Sedan exists"),Sedan) || !TestNotNull(TEXT("Muscle car exists"),Muscle)
        || !TestNotNull(TEXT("Hypercar exists"),Hyper)) return false;
    TestTrue(TEXT("Distinct drive layouts"),Starter->Definition.Drivetrain==EADDrivetrain::RearWheelDrive
        && Hatch->Definition.Drivetrain==EADDrivetrain::FrontWheelDrive && Sedan->Definition.Drivetrain==EADDrivetrain::AllWheelDrive);
    TestTrue(TEXT("Distinct mass and torque"),Hatch->Definition.MassKg<Starter->Definition.MassKg
        && Starter->Definition.MassKg<Sedan->Definition.MassKg
        && Hatch->Definition.GetTorqueNm(4200)<Starter->Definition.GetTorqueNm(4200)
        && Starter->Definition.GetTorqueNm(4200)<Sedan->Definition.GetTorqueNm(4200));
    TestTrue(TEXT("Expanded roster adds distinct rear-drive V8 and all-wheel-drive hypercar physics"),
        Muscle->Definition.BodyStyle==TEXT("muscle") && Hyper->Definition.BodyStyle==TEXT("hypercar")
        && Muscle->Definition.Drivetrain==EADDrivetrain::RearWheelDrive
        && Hyper->Definition.Drivetrain==EADDrivetrain::AllWheelDrive
        && Muscle->Definition.BodyLengthCm>Starter->Definition.BodyLengthCm
        && Hyper->Definition.DownforceCoefficient>Sedan->Definition.DownforceCoefficient
        && Hyper->Definition.GetTorqueNm(6200)>Muscle->Definition.GetTorqueNm(6200));
    TestEqual(TEXT("New driver owns one car"),F.Service->GetProfile().Vehicles.Num(),1);
    auto Draft=F.Service->GetProfile();
    Draft.PaintId=TEXT("pearl"); Draft.OwnedUpgrades.Add(TEXT("ecu")); Draft.EquippedUpgrades.Add(TEXT("ecu"));
    if (!TestTrue(TEXT("Customize starter"),F.Service->Commit(Draft,Error))) { AddError(Error); return false; }
    TestTrue(TEXT("Build unowned car preview"),F.Service->MakeVehicleDraft(Hatch->Id,Draft,Error));
    TestTrue(TEXT("Starter parts do not transfer for free"),Draft.OwnedUpgrades.IsEmpty());
    TestEqual(TEXT("New car starts factory paint"),Draft.PaintId,FString(TEXT("mint")));
    int64 Cost=0;
    TestTrue(TEXT("Dealer quotes authoritative cost"),F.Service->GetPurchaseCost(Draft,Cost,Error));
    TestEqual(TEXT("Unowned car costs catalog price"),Cost,Hatch->Price);
    Draft.Credits=99999999; Draft.Vehicles.Reset();
    FADOwnedVehicle Forged; Forged.VehicleId=Sedan->Id; Draft.Vehicles.Add(Forged);
    if (!TestTrue(TEXT("Purchase and equip hatch atomically"),F.Service->Commit(Draft,Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Catalog price debited once"),F.Service->GetProfile().Credits,int64(0));
    TestEqual(TEXT("Selected car changes"),F.Service->GetProfile().ActiveVehicleId,Hatch->Id);
    TestEqual(TEXT("Collection preserves old car and grants selected car only"),F.Service->GetProfile().Vehicles.Num(),2);
    TestFalse(TEXT("Forged collection cannot grant an exotic"),F.Service->IsVehicleOwned(Sedan->Id));
    TestTrue(TEXT("Repeat selected purchase is free"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("No repeat charge"),F.Service->GetProfile().Credits,int64(0));
    TestTrue(TEXT("Reload selected car"),F.Service->InitializeProfile(F.Path,Error));
    TestEqual(TEXT("Active car survives restart"),F.Service->GetProfile().ActiveVehicleId,Hatch->Id);
    TestTrue(TEXT("Select saved starter build"),F.Service->MakeVehicleDraft(Starter->Id,Draft,Error));
    TestEqual(TEXT("Starter retains its own paint"),Draft.PaintId,FString(TEXT("pearl")));
    TestTrue(TEXT("Starter retains purchased ECU"),Draft.EquippedUpgrades.Contains(TEXT("ecu")));
    TestTrue(TEXT("Switching owned cars is free"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Stock lookup follows selected car"),F.Service->GetStockDefinition().VehicleId,Starter->Id);
    TestTrue(TEXT("Build expensive preview"),F.Service->MakeVehicleDraft(Sedan->Id,Draft,Error));
    TestFalse(TEXT("Insufficient credits reject entire vehicle purchase"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Failure preserves selected starter"),F.Service->GetProfile().ActiveVehicleId,Starter->Id);
    TestFalse(TEXT("Failure grants no car"),F.Service->IsVehicleOwned(Sedan->Id));
    Draft=F.Service->GetProfile(); Draft.ActiveVehicleId=TEXT("unknown_vehicle");
    TestFalse(TEXT("Unknown vehicle rejected"),F.Service->Commit(Draft,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADVehicleMigrationTest,"Afterdark.Ownership.VehicleMigrationAndRollback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADVehicleMigrationTest::RunTest(const FString&)
{
    FVehicleProfileFixture F; FString Error;
    if (!TestTrue(TEXT("Initialize migration fixture"),F.Service->InitializeProfile(F.Path,Error))) return false;
    FADWorldSnapshot World;
    World.bRecorded=true; World.Hour=4.5; World.Wetness=.7f;
    F.Service->StageWorldSnapshot(World);
    if (!TestTrue(TEXT("Save discovery and world"),F.Service->CommitDiscovery(F.Service->GetDiscoveries()[0].Id,Error))) return false;
    auto Draft=F.Service->GetProfile(); Draft.PaintId=TEXT("blue");
    Draft.OwnedUpgrades.Add(TEXT("tires")); Draft.EquippedUpgrades.Add(TEXT("tires"));
    if (!TestTrue(TEXT("Save starter build"),F.Service->Commit(Draft,Error))) return false;
    const auto Before=F.Service->GetProfile();
    FString Text;
    TSharedPtr<FJsonObject> Envelope,Payload;
    if (!FFileHelper::LoadFileToString(Text,*F.Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Envelope)) return false;
    FString PayloadText=Envelope->GetStringField(TEXT("payload"));
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PayloadText),Payload)) return false;
    Payload->RemoveField(TEXT("vehicles"));
    PayloadText.Reset();
    FJsonSerializer::Serialize(Payload.ToSharedRef(),TJsonWriterFactory<>::Create(&PayloadText));
    Envelope->SetNumberField(TEXT("schemaVersion"),4);
    Envelope->SetStringField(TEXT("payload"),PayloadText);
    Envelope->SetStringField(TEXT("checksum"),ProfileDigest(PayloadText));
    Text.Reset(); FJsonSerializer::Serialize(Envelope.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    if (!TestTrue(TEXT("Write genuine schema4 fixture"),FFileHelper::SaveStringToFile(Text,*F.Path))) return false;
    if (!TestTrue(TEXT("Migrate schema4"),F.Service->InitializeProfile(F.Path,Error))) { AddError(Error); return false; }
    const auto Migrated=F.Service->GetProfile();
    TestEqual(TEXT("Migrated starter collection"),Migrated.Vehicles.Num(),1);
    TestEqual(TEXT("Migration preserves credits"),Migrated.Credits,Before.Credits);
    TestEqual(TEXT("Migration preserves REP"),Migrated.Reputation,Before.Reputation);
    TestEqual(TEXT("Migration preserves paint"),Migrated.PaintId,Before.PaintId);
    TestTrue(TEXT("Migration preserves parts"),Migrated.EquippedUpgrades==Before.EquippedUpgrades);
    TestTrue(TEXT("Migration preserves discoveries"),Migrated.DiscoveredLocations==Before.DiscoveredLocations);
    TestEqual(TEXT("Migration preserves world clock"),Migrated.World.Hour,Before.World.Hour);
    TestEqual(TEXT("Migration preserves road wetness"),Migrated.World.Wetness,Before.World.Wetness);
    TestTrue(TEXT("Migrate on successful next transaction"),F.Service->Commit(Migrated,Error));
    TestTrue(TEXT("Reload schema5"),F.Service->InitializeProfile(F.Path,Error));
    const FString Blocker=F.Path+TEXT(".blocker");
    TestTrue(TEXT("Create guaranteed blocked save path"),FFileHelper::SaveStringToFile(TEXT("blocked"),*Blocker));
    TestTrue(TEXT("Initialize new profile below blocked parent"),F.Service->InitializeProfile(Blocker/TEXT("driver.json"),Error));
    TestTrue(TEXT("Build affordable vehicle draft"),F.Service->MakeVehicleDraft(TEXT("kestrel_xr"),Draft,Error));
    TestFalse(TEXT("Failed durable save grants no purchase"),F.Service->Commit(Draft,Error));
    TestEqual(TEXT("Write failure preserves credits"),F.Service->GetProfile().Credits,int64(12000));
    TestEqual(TEXT("Write failure preserves active car"),F.Service->GetProfile().ActiveVehicleId,FString(TEXT("aster_s6")));
    TestFalse(TEXT("Write failure preserves ownership"),F.Service->IsVehicleOwned(TEXT("kestrel_xr")));
    return true;
}
#endif
