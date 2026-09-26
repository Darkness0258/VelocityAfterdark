#include "Ownership/ADOwnershipSubsystem.h"
#include "Career/ADCareerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace
{
constexpr int32 CurrentSchema = 6;
constexpr int64 MaxSaveBytes = 64 * 1024;
constexpr int64 MaxCredits = 1000000000;
constexpr int32 MaxRivalMemories = 32;
constexpr int32 MaxRivalEncounters = 128;

bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Min, double Max,
    double& Out, FString& Error)
{
    const TSharedPtr<FJsonValue>* Value = Object.IsValid() ? Object->Values.Find(Key) : nullptr;
    if (!Value || !Value->IsValid() || (*Value)->Type != EJson::Number || !(*Value)->TryGetNumber(Out)
        || !FMath::IsFinite(Out) || Out < Min || Out > Max)
    { Error = FString::Printf(TEXT("Invalid numeric field '%s'."), Key); return false; }
    return true;
}

bool String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out, FString& Error,
    int32 MaximumLength = 256)
{
    if (!Object.IsValid() || !Object->TryGetStringField(Key, Out) || Out.IsEmpty()
        || Out.Len() > MaximumLength || Out != Out.TrimStartAndEnd())
    { Error = FString::Printf(TEXT("Invalid string field '%s'."), Key); return false; }
    return true;
}

bool Identifier(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out, FString& Error)
{
    if (!String(Object, Key, Out, Error, 48)) return false;
    for (const TCHAR Character : Out)
    {
        if ((Character < TEXT('a') || Character > TEXT('z'))
            && (Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('_'))
        { Error = FString::Printf(TEXT("Invalid identifier '%s'."), Key); return false; }
    }
    return true;
}

bool Array(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
    const TArray<TSharedPtr<FJsonValue>>*& Out, int32 Minimum, int32 Maximum, FString& Error)
{
    if (!Object.IsValid() || !Object->TryGetArrayField(Key, Out) || !Out
        || Out->Num() < Minimum || Out->Num() > Maximum)
    { Error = FString::Printf(TEXT("Invalid array '%s'."), Key); return false; }
    return true;
}

bool ReadIds(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TArray<FString>& Out, FString& Error, int32 Maximum = 32)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Array(Object, Key, Values, 0, Maximum, Error)) return false;
    Out.Reset();
    for (const auto& Value : *Values)
    {
        FString Id;
        if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(Id)
            || Id.IsEmpty() || Id.Len() > 48 || Out.Contains(Id))
        { Error = FString::Printf(TEXT("'%s' contains a duplicate or invalid identifier."), Key); return false; }
        Out.Add(Id);
    }
    return true;
}

FString Digest(const FString& Text)
{
    const FTCHARToUTF8 Utf8(*Text);
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
    return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
}

FADOwnedVehicle ActiveRecord(const FADGarageProfile& Profile)
{
    FADOwnedVehicle Result;
    Result.VehicleId=Profile.ActiveVehicleId;
    Result.PaintId=Profile.PaintId;
    Result.OwnedUpgrades=Profile.OwnedUpgrades;
    Result.EquippedUpgrades=Profile.EquippedUpgrades;
    Result.TuneId=Profile.TuneId;
    return Result;
}

void StoreActiveRecord(FADGarageProfile& Profile)
{
    auto* Record=Profile.Vehicles.FindByPredicate([&](const auto& Item) { return Item.VehicleId==Profile.ActiveVehicleId; });
    if (Record) *Record=ActiveRecord(Profile);
    else Profile.Vehicles.Add(ActiveRecord(Profile));
}

FString Encode(const FADGarageProfile& Profile)
{
    const auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("vehicleId"), Profile.ActiveVehicleId);
    Payload->SetNumberField(TEXT("credits"), static_cast<double>(Profile.Credits));
    Payload->SetStringField(TEXT("paintId"), Profile.PaintId);
    Payload->SetStringField(TEXT("tuneId"), Profile.TuneId);
    const auto Ids = [](const TArray<FString>& Strings)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FString& Id : Strings) Result.Add(MakeShared<FJsonValueString>(Id));
        return Result;
    };
    Payload->SetArrayField(TEXT("ownedUpgrades"), Ids(Profile.OwnedUpgrades));
    Payload->SetArrayField(TEXT("equippedUpgrades"), Ids(Profile.EquippedUpgrades));
    TArray<TSharedPtr<FJsonValue>> OwnedVehicles;
    for (const FADOwnedVehicle& Record:Profile.Vehicles)
    {
        const auto Vehicle=MakeShared<FJsonObject>();
        Vehicle->SetStringField(TEXT("vehicleId"),Record.VehicleId);
        Vehicle->SetStringField(TEXT("paintId"),Record.PaintId);
        Vehicle->SetStringField(TEXT("tuneId"),Record.TuneId);
        Vehicle->SetArrayField(TEXT("ownedUpgrades"),Ids(Record.OwnedUpgrades));
        Vehicle->SetArrayField(TEXT("equippedUpgrades"),Ids(Record.EquippedUpgrades));
        OwnedVehicles.Add(MakeShared<FJsonValueObject>(Vehicle));
    }
    Payload->SetArrayField(TEXT("vehicles"),OwnedVehicles);
    Payload->SetNumberField(TEXT("reputation"),static_cast<double>(Profile.Reputation));
    Payload->SetNumberField(TEXT("raceWins"),static_cast<double>(Profile.RaceWins));
    Payload->SetNumberField(TEXT("racesFinished"),static_cast<double>(Profile.RacesFinished));
    Payload->SetArrayField(TEXT("completedChapters"),Ids(Profile.CompletedChapters));
    Payload->SetArrayField(TEXT("awardedRaceIds"),Ids(Profile.AwardedRaceIds));
    TArray<TSharedPtr<FJsonValue>> RivalMemories;
    for (const FADRivalMemory& Memory : Profile.RivalMemories)
    {
        const auto Rival = MakeShared<FJsonObject>();
        Rival->SetStringField(TEXT("rivalId"), Memory.RivalId);
        Rival->SetStringField(TEXT("lastChapterId"), Memory.LastChapterId);
        Rival->SetNumberField(TEXT("encounters"), Memory.Encounters);
        Rival->SetNumberField(TEXT("playerWins"), Memory.PlayerWins);
        Rival->SetNumberField(TEXT("rivalWins"), Memory.RivalWins);
        Rival->SetNumberField(TEXT("respect"), Memory.Respect);
        Rival->SetNumberField(TEXT("grudge"), Memory.Grudge);
        RivalMemories.Add(MakeShared<FJsonValueObject>(Rival));
    }
    Payload->SetArrayField(TEXT("rivalMemories"), RivalMemories);
    Payload->SetArrayField(TEXT("discoveredLocations"),Ids(Profile.DiscoveredLocations));
    const auto World = MakeShared<FJsonObject>();
    World->SetBoolField(TEXT("recorded"),Profile.World.bRecorded);
    World->SetNumberField(TEXT("hour"),Profile.World.Hour);
    World->SetNumberField(TEXT("weatherElapsed"),Profile.World.WeatherElapsed);
    World->SetNumberField(TEXT("weatherIndex"),Profile.World.WeatherIndex);
    World->SetNumberField(TEXT("weather"),Profile.World.Weather);
    World->SetNumberField(TEXT("wetness"),Profile.World.Wetness);
    World->SetNumberField(TEXT("rainAmount"),Profile.World.RainAmount);
    World->SetNumberField(TEXT("fogAmount"),Profile.World.FogAmount);
    Payload->SetObjectField(TEXT("world"),World);
    FString PayloadText;
    FJsonSerializer::Serialize(Payload, TJsonWriterFactory<>::Create(&PayloadText));
    const auto Envelope = MakeShared<FJsonObject>();
    Envelope->SetNumberField(TEXT("schemaVersion"), CurrentSchema);
    Envelope->SetStringField(TEXT("payload"), PayloadText);
    Envelope->SetStringField(TEXT("checksum"), Digest(PayloadText));
    FString Result;
    FJsonSerializer::Serialize(Envelope, TJsonWriterFactory<>::Create(&Result));
    return Result;
}

enum class EReadProfile { Missing, Valid, Invalid, Future };

EReadProfile Decode(const FString& Text, const FString& VehicleId, FADGarageProfile& Out,
    bool& bMigrated, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    { Error = TEXT("Profile JSON is malformed or truncated."); return EReadProfile::Invalid; }
    double Schema = 0;
    if (!Number(Root, TEXT("schemaVersion"), 1, MAX_int32, Schema, Error) || Schema != FMath::FloorToDouble(Schema))
    { Error = TEXT("Profile schema version is invalid."); return EReadProfile::Invalid; }
    if (Schema > CurrentSchema)
    { Error = TEXT("This profile was written by a newer game version. It has been preserved."); return EReadProfile::Future; }
    bMigrated = Schema < CurrentSchema;
    if (Schema >= 2)
    {
        FString Payload, Checksum;
        if (!String(Root, TEXT("payload"), Payload, Error, MaxSaveBytes)
            || !String(Root, TEXT("checksum"), Checksum, Error, 40) || Checksum != Digest(Payload))
        { Error = TEXT("Profile integrity checksum does not match."); return EReadProfile::Invalid; }
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Root) || !Root.IsValid())
        { Error = TEXT("Profile payload is malformed."); return EReadProfile::Invalid; }
    }
    FADGarageProfile Candidate;
    FString SavedVehicle;
    double Credits = 0;
    if (!Identifier(Root, TEXT("vehicleId"), SavedVehicle, Error) || (Schema < 5 && SavedVehicle != VehicleId)
        || !Number(Root, TEXT("credits"), 0, MaxCredits, Credits, Error) || Credits != FMath::FloorToDouble(Credits)
        || !Identifier(Root, TEXT("paintId"), Candidate.PaintId, Error))
    { if (Error.IsEmpty()) Error = TEXT("Profile vehicle or credit balance is invalid."); return EReadProfile::Invalid; }
    Candidate.Credits = static_cast<int64>(Credits);
    Candidate.ActiveVehicleId=SavedVehicle;
    if (Schema == 1)
    {
        // Version 1 treated purchased parts as always equipped and had no tuning preset.
        if (!ReadIds(Root, TEXT("upgrades"), Candidate.OwnedUpgrades, Error)) return EReadProfile::Invalid;
        Candidate.EquippedUpgrades = Candidate.OwnedUpgrades;
    }
    else if (!ReadIds(Root, TEXT("ownedUpgrades"), Candidate.OwnedUpgrades, Error)
        || !ReadIds(Root, TEXT("equippedUpgrades"), Candidate.EquippedUpgrades, Error)
        || !Identifier(Root, TEXT("tuneId"), Candidate.TuneId, Error)) return EReadProfile::Invalid;
    if (Schema >= 3)
    {
        double Rep=0,Wins=0,Finishes=0;
        if (!Number(Root,TEXT("reputation"),0,MaxCredits,Rep,Error) || Rep!=FMath::FloorToDouble(Rep)
            || !Number(Root,TEXT("raceWins"),0,MaxCredits,Wins,Error) || Wins!=FMath::FloorToDouble(Wins)
            || !Number(Root,TEXT("racesFinished"),Wins,MaxCredits,Finishes,Error) || Finishes!=FMath::FloorToDouble(Finishes)
            || !ReadIds(Root,TEXT("completedChapters"),Candidate.CompletedChapters,Error)
            || !ReadIds(Root,TEXT("awardedRaceIds"),Candidate.AwardedRaceIds,Error,128)) return EReadProfile::Invalid;
        Candidate.Reputation=static_cast<int64>(Rep);
        Candidate.RaceWins=static_cast<int64>(Wins);
        Candidate.RacesFinished=static_cast<int64>(Finishes);
    }
    if (Schema >= 4)
    {
        const TSharedPtr<FJsonObject>* World = nullptr;
        double Index=0,Weather=0,Wetness=0,Rain=0,Fog=0;
        if (!ReadIds(Root,TEXT("discoveredLocations"),Candidate.DiscoveredLocations,Error,128)
            || !Root->TryGetObjectField(TEXT("world"),World) || !World || !World->IsValid()
            || !(*World)->TryGetBoolField(TEXT("recorded"),Candidate.World.bRecorded)
            || !Number(*World,TEXT("hour"),0,24,Candidate.World.Hour,Error) || Candidate.World.Hour>=24
            || !Number(*World,TEXT("weatherElapsed"),0,86400,Candidate.World.WeatherElapsed,Error)
            || !Number(*World,TEXT("weatherIndex"),0,15,Index,Error) || Index!=FMath::FloorToDouble(Index)
            || !Number(*World,TEXT("weather"),0,2,Weather,Error) || Weather!=FMath::FloorToDouble(Weather)
            || !Number(*World,TEXT("wetness"),0,1,Wetness,Error)
            || !Number(*World,TEXT("rainAmount"),0,1,Rain,Error)
            || !Number(*World,TEXT("fogAmount"),0,1,Fog,Error))
        { if (Error.IsEmpty()) Error=TEXT("Saved world state is invalid."); return EReadProfile::Invalid; }
        Candidate.World.WeatherIndex=static_cast<int32>(Index);
        Candidate.World.Weather=static_cast<uint8>(Weather);
        Candidate.World.Wetness=static_cast<float>(Wetness);
        Candidate.World.RainAmount=static_cast<float>(Rain);
        Candidate.World.FogAmount=static_cast<float>(Fog);
    }
    Candidate.Vehicles.Reset();
    if (Schema >= 5)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Array(Root,TEXT("vehicles"),Values,1,32,Error)) return EReadProfile::Invalid;
        for (const auto& Value:*Values)
        {
            const TSharedPtr<FJsonObject>* Object=nullptr;
            FADOwnedVehicle Record;
            if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object
                || !Identifier(*Object,TEXT("vehicleId"),Record.VehicleId,Error)
                || !Identifier(*Object,TEXT("paintId"),Record.PaintId,Error)
                || !Identifier(*Object,TEXT("tuneId"),Record.TuneId,Error)
                || !ReadIds(*Object,TEXT("ownedUpgrades"),Record.OwnedUpgrades,Error)
                || !ReadIds(*Object,TEXT("equippedUpgrades"),Record.EquippedUpgrades,Error))
            { if (Error.IsEmpty()) Error=TEXT("Owned vehicle record is malformed."); return EReadProfile::Invalid; }
            Candidate.Vehicles.Add(MoveTemp(Record));
        }
    }
    else StoreActiveRecord(Candidate); // All previous schemas described the starter car only.
    if (Schema >= 6)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Array(Root, TEXT("rivalMemories"), Values, 0, MaxRivalMemories, Error)) return EReadProfile::Invalid;
        TSet<FString> RivalIds;
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FADRivalMemory Memory;
            double Encounters = 0., PlayerWins = 0., RivalWins = 0., Respect = 0., Grudge = 0.;
            if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid()
                || !Identifier(*Object, TEXT("rivalId"), Memory.RivalId, Error)
                || !Identifier(*Object, TEXT("lastChapterId"), Memory.LastChapterId, Error)
                || !Number(*Object, TEXT("encounters"), 1., MaxRivalEncounters, Encounters, Error)
                || Encounters != FMath::FloorToDouble(Encounters)
                || !Number(*Object, TEXT("playerWins"), 0., Encounters, PlayerWins, Error)
                || PlayerWins != FMath::FloorToDouble(PlayerWins)
                || !Number(*Object, TEXT("rivalWins"), 0., Encounters, RivalWins, Error)
                || RivalWins != FMath::FloorToDouble(RivalWins)
                || PlayerWins + RivalWins != Encounters
                || !Number(*Object, TEXT("respect"), 0., 1., Respect, Error)
                || !Number(*Object, TEXT("grudge"), 0., 1., Grudge, Error)
                || RivalIds.Contains(Memory.RivalId))
            {
                if (Error.IsEmpty()) Error = TEXT("Rival history is duplicated or outside its supported bounds.");
                return EReadProfile::Invalid;
            }
            Memory.Encounters = static_cast<int32>(Encounters);
            Memory.PlayerWins = static_cast<int32>(PlayerWins);
            Memory.RivalWins = static_cast<int32>(RivalWins);
            Memory.Respect = static_cast<float>(Respect);
            Memory.Grudge = static_cast<float>(Grudge);
            RivalIds.Add(Memory.RivalId);
            Candidate.RivalMemories.Add(MoveTemp(Memory));
        }
    }
    Out = MoveTemp(Candidate);
    return EReadProfile::Valid;
}

EReadProfile Read(const FString& Path, const FString& VehicleId, FADGarageProfile& Out,
    bool& bMigrated, FString& Text, FString& Error)
{
    Text.Reset();
    if (!IFileManager::Get().FileExists(*Path))
    {
        if (IFileManager::Get().DirectoryExists(*Path))
        { Error = TEXT("Profile path points to a directory."); return EReadProfile::Invalid; }
        return EReadProfile::Missing;
    }
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 1 || Size > MaxSaveBytes || !FFileHelper::LoadFileToString(Text, *Path))
    { Error = TEXT("Profile cannot be read or exceeds the 64 KB limit."); return EReadProfile::Invalid; }
    return Decode(Text, VehicleId, Out, bMigrated, Error);
}

bool WriteFlushed(const FString& Path, const FString& Text)
{
    TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path));
    if (!Handle) return false;
    const FTCHARToUTF8 Utf8(*Text);
    return Handle->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()) && Handle->Flush(true);
}

bool AtomicReplace(const FString& Destination, const FString& Source)
{
    // Both files are siblings, so the replacement never crosses a filesystem boundary.
#if PLATFORM_WINDOWS
    const FString NativeDestination = FPaths::ConvertRelativePathToFull(Destination).Replace(TEXT("/"), TEXT("\\"));
    const FString NativeSource = FPaths::ConvertRelativePathToFull(Source).Replace(TEXT("/"), TEXT("\\"));
    return ::MoveFileExW(*NativeSource, *NativeDestination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(TCHAR_TO_UTF8(*Source), TCHAR_TO_UTF8(*Destination)) == 0;
#endif
}

struct FProfileWriteLock
{
#if PLATFORM_WINDOWS
    HANDLE Handle=INVALID_HANDLE_VALUE;
    explicit FProfileWriteLock(const FString& Path)
    {
        const FString Native=(FPaths::ConvertRelativePathToFull(Path)+TEXT(".lock")).Replace(TEXT("/"),TEXT("\\"));
        Handle=::CreateFileW(*Native,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    }
    ~FProfileWriteLock() { if (Handle!=INVALID_HANDLE_VALUE) ::CloseHandle(Handle); }
    bool IsLocked() const { return Handle!=INVALID_HANDLE_VALUE; }
#else
    explicit FProfileWriteLock(const FString&) {}
    bool IsLocked() const { return true; } // Windows is the supported save target for this slice.
#endif
};
}

void UADOwnershipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UADCareerSubsystem>();
    FString Path;
    const bool bExplicitPath = FParse::Value(FCommandLine::Get(), TEXT("AfterdarkProfile="), Path);
    if (!bExplicitPath && !GIsEditor && !IsRunningCommandlet()
        && !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRenderSmoke"))
        && !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkRaceSmoke"))
        && !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkDriveBenchmark"))
        && !FParse::Param(FCommandLine::Get(), TEXT("AfterdarkGarageSmoke")))
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profiles/driver.json"));
    FString InitializationError;
    InitializeProfile(Path, InitializationError);
}

bool UADOwnershipSubsystem::LoadData(FString& OutError)
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Garage/catalog.json");
    FString Text;
    const int64 Size = IFileManager::Get().FileSize(*Path);
    TSharedPtr<FJsonObject> Root;
    if (Size < 1 || Size > MaxSaveBytes || !FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    { OutError = TEXT("Garage catalog is missing, too large, or malformed."); return false; }
    double Schema = 0;
    FString VehicleId;
    FADVehicleDefinition Vehicle;
    if (!Number(Root, TEXT("schemaVersion"), 1, 1, Schema, OutError)
        || !Identifier(Root, TEXT("vehicleId"), VehicleId, OutError)
        || !Vehicle.LoadFromJson(FPaths::ProjectContentDir() / TEXT("Data/Vehicles") / (VehicleId + TEXT(".json")), OutError)) return false;

    TArray<FADGaragePaint> NewPaints;
    TArray<FADGarageUpgrade> NewUpgrades;
    TArray<FADGarageTune> NewTunes;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Array(Root, TEXT("paints"), Values, 1, 32, OutError)) return false;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FADGaragePaint Paint;
        const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object
            || !Identifier(*Object, TEXT("id"), Paint.Id, OutError)
            || !String(*Object, TEXT("name"), Paint.Name, OutError)
            || !Array(*Object, TEXT("color"), Color, 3, 3, OutError))
        { if (OutError.IsEmpty()) OutError = TEXT("Invalid paint catalog entry."); return false; }
        if (NewPaints.ContainsByPredicate([&](const auto& Item) { return Item.Id == Paint.Id; }))
        { OutError = TEXT("Duplicate paint identifier."); return false; }
        double Components[3] = {};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (!(*Color)[Index].IsValid() || (*Color)[Index]->Type != EJson::Number
                || !(*Color)[Index]->TryGetNumber(Components[Index]) || !FMath::IsFinite(Components[Index])
                || Components[Index] < 0 || Components[Index] > 1)
            { OutError = TEXT("Paint colors must contain three linear values from zero to one."); return false; }
        }
        Paint.Color = FLinearColor(Components[0], Components[1], Components[2], 1);
        NewPaints.Add(Paint);
    }
    if (!Array(Root, TEXT("upgrades"), Values, 1, 32, OutError)) return false;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FADGarageUpgrade Upgrade;
        double Price = 0;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object
            || !Identifier(*Object, TEXT("id"), Upgrade.Id, OutError)
            || !String(*Object, TEXT("name"), Upgrade.Name, OutError)
            || !String(*Object, TEXT("description"), Upgrade.Description, OutError)
            || !Number(*Object, TEXT("price"), 1, MaxCredits, Price, OutError) || Price != FMath::FloorToDouble(Price))
        { if (OutError.IsEmpty()) OutError = TEXT("Invalid upgrade catalog entry."); return false; }
        if (NewUpgrades.ContainsByPredicate([&](const auto& Item) { return Item.Id == Upgrade.Id; }))
        { OutError = TEXT("Duplicate upgrade identifier."); return false; }
        Upgrade.Price = static_cast<int64>(Price);
        struct FEffect { const TCHAR* Key; float* Target; double Min; double Max; };
        const FEffect Effects[] = {
            {TEXT("torqueMultiplier"), &Upgrade.TorqueMultiplier, 1, 1.5},
            {TEXT("gripMultiplier"), &Upgrade.GripMultiplier, 1, 1.3},
            {TEXT("brakeMultiplier"), &Upgrade.BrakeMultiplier, 1, 1.5},
            {TEXT("massMultiplier"), &Upgrade.MassMultiplier, .8, 1}
        };
        for (const auto& Effect : Effects)
        {
            double Multiplier = 1;
            if (!Number(*Object, Effect.Key, Effect.Min, Effect.Max, Multiplier, OutError)) return false;
            *Effect.Target = static_cast<float>(Multiplier);
        }
        if (Upgrade.TorqueMultiplier == 1 && Upgrade.GripMultiplier == 1
            && Upgrade.BrakeMultiplier == 1 && Upgrade.MassMultiplier == 1)
        { OutError = TEXT("Every upgrade must change at least one performance property."); return false; }
        NewUpgrades.Add(Upgrade);
    }
    if (!Array(Root, TEXT("tunes"), Values, 1, 16, OutError)) return false;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FADGarageTune Tune;
        double FinalDrive = 1;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object
            || !Identifier(*Object, TEXT("id"), Tune.Id, OutError)
            || !String(*Object, TEXT("name"), Tune.Name, OutError)
            || !String(*Object, TEXT("description"), Tune.Description, OutError)
            || !Number(*Object, TEXT("finalDriveMultiplier"), .8, 1.2, FinalDrive, OutError))
        { if (OutError.IsEmpty()) OutError = TEXT("Invalid tuning catalog entry."); return false; }
        if (NewTunes.ContainsByPredicate([&](const auto& Item) { return Item.Id == Tune.Id; }))
        { OutError = TEXT("Duplicate tuning identifier."); return false; }
        Tune.FinalDriveMultiplier = static_cast<float>(FinalDrive);
        NewTunes.Add(Tune);
    }
    if (!NewPaints.ContainsByPredicate([](const auto& Item) { return Item.Id == TEXT("mint"); })
        || !NewTunes.ContainsByPredicate([](const auto& Item) { return Item.Id == TEXT("street"); }))
    { OutError = TEXT("Garage catalog must provide the mint paint and street tuning defaults."); return false; }
    const FString DealerPath=FPaths::ProjectContentDir()/TEXT("Data/Vehicles/dealership.json");
    const int64 DealerSize=IFileManager::Get().FileSize(*DealerPath);
    TSharedPtr<FJsonObject> DealerRoot;
    if (DealerSize<1 || DealerSize>MaxSaveBytes || !FFileHelper::LoadFileToString(Text,*DealerPath)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),DealerRoot) || !DealerRoot.IsValid()
        || !Number(DealerRoot,TEXT("schemaVersion"),1,1,Schema,OutError)
        || !Array(DealerRoot,TEXT("vehicles"),Values,1,32,OutError))
    { if (OutError.IsEmpty()) OutError=TEXT("Dealership catalog is missing, too large, or malformed."); return false; }
    TArray<FADDealerVehicle> NewVehicles;
    for (const auto& Value:*Values)
    {
        const TSharedPtr<FJsonObject>* Object=nullptr;
        FADDealerVehicle Listing;
        double Price=0;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object
            || !Identifier(*Object,TEXT("vehicleId"),Listing.Id,OutError)
            || !String(*Object,TEXT("description"),Listing.Description,OutError)
            || !Number(*Object,TEXT("price"),0,MaxCredits,Price,OutError) || Price!=FMath::FloorToDouble(Price)
            || (Listing.Id!=VehicleId && Price<1)
            || !Listing.Definition.LoadFromJson(FPaths::ProjectContentDir()/TEXT("Data/Vehicles")/(Listing.Id+TEXT(".json")),OutError)
            || Listing.Definition.VehicleId!=Listing.Id)
        { if (OutError.IsEmpty()) OutError=TEXT("Dealership vehicle definition or price is invalid."); return false; }
        if (NewVehicles.ContainsByPredicate([&](const auto& Item) { return Item.Id==Listing.Id; }))
        { OutError=TEXT("Duplicate dealership vehicle identifier."); return false; }
        Listing.Price=static_cast<int64>(Price);
        NewVehicles.Add(MoveTemp(Listing));
    }
    if (!NewVehicles.ContainsByPredicate([&](const auto& Item) { return Item.Id==VehicleId && Item.Price==0; }))
    { OutError=TEXT("Dealership must include the free starter vehicle."); return false; }
    TArray<FADDiscoveryDefinition> NewDiscoveries;
    if (!FADDiscoveryDefinition::LoadCatalog(FPaths::ProjectContentDir()/TEXT("Data/World/discoveries.json"),NewDiscoveries,OutError)) return false;
    StockDefinition = MoveTemp(Vehicle);
    Paints = MoveTemp(NewPaints);
    Upgrades = MoveTemp(NewUpgrades);
    Tunes = MoveTemp(NewTunes);
    Vehicles = MoveTemp(NewVehicles);
    Discoveries = MoveTemp(NewDiscoveries);
    bDataReady = true;
    return true;
}

bool UADOwnershipSubsystem::ValidateProfile(const FADGarageProfile& Candidate, FString& OutError) const
{
    if (!FindVehicle(Candidate.ActiveVehicleId) || Candidate.Vehicles.IsEmpty() || Candidate.Vehicles.Num()>Vehicles.Num())
    { OutError=TEXT("Profile vehicle collection is invalid."); return false; }
    TSet<FString> VehicleIds;
    const FADOwnedVehicle* Active=nullptr;
    for (const auto& Record:Candidate.Vehicles)
    {
        if (!FindVehicle(Record.VehicleId) || VehicleIds.Contains(Record.VehicleId)
            || !Paints.ContainsByPredicate([&](const auto& Item) { return Item.Id==Record.PaintId; })
            || !Tunes.ContainsByPredicate([&](const auto& Item) { return Item.Id==Record.TuneId; }))
        { OutError=TEXT("Saved vehicle collection contains a duplicate, unknown vehicle, or invalid build."); return false; }
        VehicleIds.Add(Record.VehicleId);
        TSet<FString> Parts,Equipment;
        for (const auto& Id:Record.OwnedUpgrades)
        {
            if (Parts.Contains(Id) || !Upgrades.ContainsByPredicate([&](const auto& Item) { return Item.Id==Id; }))
            { OutError=TEXT("Saved vehicle has a duplicate or unknown upgrade."); return false; }
            Parts.Add(Id);
        }
        for (const auto& Id:Record.EquippedUpgrades)
        {
            if (Equipment.Contains(Id) || !Parts.Contains(Id))
            { OutError=TEXT("Saved vehicle equipment must be owned and unique."); return false; }
            Equipment.Add(Id);
        }
        if (Record.VehicleId==Candidate.ActiveVehicleId) Active=&Record;
    }
    if (!Active || Active->PaintId!=Candidate.PaintId || Active->TuneId!=Candidate.TuneId
        || Active->OwnedUpgrades!=Candidate.OwnedUpgrades || Active->EquippedUpgrades!=Candidate.EquippedUpgrades)
    { OutError=TEXT("Active vehicle build does not match its ownership record."); return false; }
    if (!Candidate.World.IsValid() || Candidate.DiscoveredLocations.Num()>Discoveries.Num())
    { OutError=TEXT("World snapshot or discovery count is invalid."); return false; }
    TSet<FString> Discovered;
    for (const FString& Id:Candidate.DiscoveredLocations)
    {
        if (Discovered.Contains(Id) || !Discoveries.ContainsByPredicate([&](const auto& Item) { return Item.Id==Id; }))
        { OutError=TEXT("Profile contains a duplicate or unknown discovery."); return false; }
        Discovered.Add(Id);
    }
    if (Candidate.Credits < 0 || Candidate.Credits > MaxCredits
        || !Paints.ContainsByPredicate([&](const auto& Item) { return Item.Id == Candidate.PaintId; })
        || !Tunes.ContainsByPredicate([&](const auto& Item) { return Item.Id == Candidate.TuneId; }))
    { OutError = TEXT("Profile has an invalid balance, paint, or tuning preset."); return false; }
    TSet<FString> Owned, Equipped;
    for (const FString& Id : Candidate.OwnedUpgrades)
    {
        if (Owned.Contains(Id) || !Upgrades.ContainsByPredicate([&](const auto& Item) { return Item.Id == Id; }))
        { OutError = TEXT("Profile contains a duplicate or unknown upgrade."); return false; }
        Owned.Add(Id);
    }
    for (const FString& Id : Candidate.EquippedUpgrades)
    {
        if (Equipped.Contains(Id) || !Owned.Contains(Id))
        { OutError = TEXT("Only unique, owned upgrades may be equipped."); return false; }
        Equipped.Add(Id);
    }
    if (Candidate.Reputation<0 || Candidate.Reputation>MaxCredits || Candidate.RaceWins<0
        || Candidate.RacesFinished<Candidate.RaceWins || Candidate.RacesFinished>MaxCredits
        || Candidate.AwardedRaceIds.Num()>128 || Candidate.CompletedChapters.Num()>32
        || Candidate.RivalMemories.Num()>MaxRivalMemories)
    { OutError=TEXT("Career counters are invalid."); return false; }
    TSet<FString> Receipts;
    for (const auto& Receipt:Candidate.AwardedRaceIds)
    {
        FGuid Parsed;
        if (!FGuid::ParseExact(Receipt,EGuidFormats::Digits,Parsed) || !Parsed.IsValid() || Receipts.Contains(Receipt))
        { OutError=TEXT("Career receipt list is invalid."); return false; }
        Receipts.Add(Receipt);
    }
    TSet<FString> RivalIds;
    for (const FADRivalMemory& Memory : Candidate.RivalMemories)
    {
        const bool bValidId = !Memory.RivalId.IsEmpty() && Memory.RivalId.Len() <= 48
            && Memory.RivalId == Memory.RivalId.ToLower();
        const bool bValidChapter = !Memory.LastChapterId.IsEmpty() && Memory.LastChapterId.Len() <= 48
            && Memory.LastChapterId == Memory.LastChapterId.ToLower();
        if (!bValidId || !bValidChapter || RivalIds.Contains(Memory.RivalId)
            || Memory.Encounters < 1 || Memory.Encounters > MaxRivalEncounters
            || Memory.PlayerWins < 0 || Memory.RivalWins < 0
            || Memory.PlayerWins + Memory.RivalWins != Memory.Encounters
            || !FMath::IsFinite(Memory.Respect) || Memory.Respect < 0.f || Memory.Respect > 1.f
            || !FMath::IsFinite(Memory.Grudge) || Memory.Grudge < 0.f || Memory.Grudge > 1.f)
        { OutError=TEXT("Rival history contains invalid, duplicate or unbounded values."); return false; }
        for (const TCHAR Character : Memory.RivalId)
            if ((Character < TEXT('a') || Character > TEXT('z'))
                && (Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('_'))
            { OutError=TEXT("Rival identifier contains unsupported characters."); return false; }
        for (const TCHAR Character : Memory.LastChapterId)
            if ((Character < TEXT('a') || Character > TEXT('z'))
                && (Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('_'))
            { OutError=TEXT("Rival chapter identifier contains unsupported characters."); return false; }
        RivalIds.Add(Memory.RivalId);
    }
    if (auto* Career=GetGameInstance()->GetSubsystem<UADCareerSubsystem>(); Career && Career->IsReady())
    {
        const auto& Chapters=Career->GetChapters();
        if (Candidate.CompletedChapters.Num()>Chapters.Num()) { OutError=TEXT("Career chapter list is invalid."); return false; }
        for (int32 Index=0;Index<Candidate.CompletedChapters.Num();++Index)
            if (Candidate.CompletedChapters[Index]!=Chapters[Index].Id)
            { OutError=TEXT("Career progress is not a valid chapter sequence."); return false; }
    }
    return true;
}

bool UADOwnershipSubsystem::InitializeProfile(const FString& AbsoluteSavePath, FString& OutError)
{
    OutError.Reset();
    Error.Reset();
    Status.Reset();
    bReady = false;
    PendingWorld=FADWorldSnapshot();
    if (!bDataReady && !LoadData(OutError)) { Error = OutError; return false; }
    if (!AbsoluteSavePath.IsEmpty() && FPaths::IsRelative(AbsoluteSavePath))
    { OutError = Error = TEXT("Profile path must be absolute."); return false; }
    SavePath = AbsoluteSavePath;
    PrimarySnapshot.Reset();
    bPrimaryExisted = false;
    FADGarageProfile Candidate;
    if (SavePath.IsEmpty())
    {
        Profile = Candidate;
        bReady = true;
        Status = TEXT("Session profile: changes are not saved to disk.");
        return true;
    }
    bool bMigrated = false;
    const EReadProfile PrimaryResult = Read(SavePath, StockDefinition.VehicleId, Candidate,
        bMigrated, PrimarySnapshot, OutError);
    bPrimaryExisted = IFileManager::Get().FileExists(*SavePath);
    if (PrimaryResult == EReadProfile::Future) { Error = OutError; return false; }
    if (PrimaryResult == EReadProfile::Valid && ValidateProfile(Candidate, OutError))
        Status = bMigrated ? TEXT("Legacy profile loaded; next save upgrades it to version 5.") : TEXT("Profile loaded.");
    else
    {
        FString BackupText, BackupError;
        const EReadProfile BackupResult = Read(SavePath + TEXT(".bak"), StockDefinition.VehicleId,
            Candidate, bMigrated, BackupText, BackupError);
        if (BackupResult == EReadProfile::Valid && ValidateProfile(Candidate, BackupError))
        { Status = TEXT("Recovered the last good backup. The next save repairs the primary profile."); OutError.Reset(); }
        else if (PrimaryResult == EReadProfile::Missing && BackupResult == EReadProfile::Missing)
        { Candidate = FADGarageProfile(); Status = TEXT("New driver: 12,000 credits available."); OutError.Reset(); }
        else
        {
            if (OutError.IsEmpty()) OutError = BackupError.IsEmpty() ? TEXT("No valid profile or backup could be loaded.") : BackupError;
            Error = OutError + TEXT(" Existing files were preserved.");
            OutError = Error;
            return false;
        }
    }
    Profile = MoveTemp(Candidate);
    PendingWorld=Profile.World;
    bReady = true;
    OutError.Reset();
    return true;
}

const FADDealerVehicle* UADOwnershipSubsystem::FindVehicle(const FString& VehicleId) const
{
    return Vehicles.FindByPredicate([&](const auto& Item) { return Item.Id==VehicleId; });
}

bool UADOwnershipSubsystem::IsVehicleOwned(const FString& VehicleId) const
{
    return Profile.Vehicles.ContainsByPredicate([&](const auto& Item) { return Item.VehicleId==VehicleId; });
}

const FADVehicleDefinition& UADOwnershipSubsystem::GetStockDefinition(const FADGarageProfile& Desired) const
{
    if (const auto* Vehicle=FindVehicle(Desired.ActiveVehicleId)) return Vehicle->Definition;
    return StockDefinition; // Read-only preview fallback; transaction validation rejects unknown IDs.
}

bool UADOwnershipSubsystem::MakeVehicleDraft(const FString& VehicleId,FADGarageProfile& OutDraft,FString& OutError) const
{
    OutError.Reset();
    if (!bReady || !FindVehicle(VehicleId))
    { OutError=TEXT("Vehicle is unavailable in the dealership catalog."); return false; }
    FADGarageProfile Candidate=Profile;
    Candidate.ActiveVehicleId=VehicleId;
    FADOwnedVehicle Record;
    if (const auto* Saved=Profile.Vehicles.FindByPredicate([&](const auto& Item) { return Item.VehicleId==VehicleId; })) Record=*Saved;
    Candidate.PaintId=Record.PaintId;
    Candidate.TuneId=Record.TuneId;
    Candidate.OwnedUpgrades=Record.OwnedUpgrades;
    Candidate.EquippedUpgrades=Record.EquippedUpgrades;
    // Unowned cars enter only the preview, not the durable collection.
    OutDraft=MoveTemp(Candidate);
    return true;
}

bool UADOwnershipSubsystem::GetPurchaseCost(const FADGarageProfile& Desired,int64& OutCost,FString& OutError) const
{
    OutCost=0;
    OutError.Reset();
    const auto* Listing=FindVehicle(Desired.ActiveVehicleId);
    if (!bReady || !Listing) { OutError=TEXT("Vehicle is unavailable in the dealership catalog."); return false; }
    const auto* OwnedCar=Profile.Vehicles.FindByPredicate([&](const auto& Item) { return Item.VehicleId==Desired.ActiveVehicleId; });
    FADGarageProfile Candidate=Profile;
    Candidate.ActiveVehicleId=Desired.ActiveVehicleId;
    Candidate.PaintId=Desired.PaintId;
    Candidate.TuneId=Desired.TuneId;
    Candidate.OwnedUpgrades=Desired.OwnedUpgrades;
    Candidate.EquippedUpgrades=Desired.EquippedUpgrades;
    StoreActiveRecord(Candidate);
    if (!ValidateProfile(Candidate,OutError)) return false;
    if (OwnedCar)
        for (const auto& Id:OwnedCar->OwnedUpgrades)
            if (!Candidate.OwnedUpgrades.Contains(Id))
            { OutError=TEXT("Purchased upgrades remain with their vehicle; remove them from equipment instead."); return false; }
    int64 Cost=OwnedCar ? 0 : Listing->Price;
    for (const auto& Id:Candidate.OwnedUpgrades)
        if (!OwnedCar || !OwnedCar->OwnedUpgrades.Contains(Id))
            Cost+=Upgrades.FindByPredicate([&](const auto& Item) { return Item.Id==Id; })->Price;
    OutCost=Cost;
    return true;
}

FADVehicleDefinition UADOwnershipSubsystem::BuildDefinition(const FADGarageProfile& Desired) const
{
    FADVehicleDefinition Result = GetStockDefinition(Desired);
    TSet<FString> Applied;
    for (const FString& Id : Desired.EquippedUpgrades)
    {
        const auto* Upgrade = Upgrades.FindByPredicate([&](const auto& Item) { return Item.Id == Id; });
        if (!Upgrade || Applied.Contains(Id) || !Desired.OwnedUpgrades.Contains(Id)) continue;
        Applied.Add(Id);
        for (auto& Point : Result.TorqueCurve) Point.TorqueNm *= Upgrade->TorqueMultiplier;
        Result.TireFriction *= Upgrade->GripMultiplier;
        Result.LateralStiffnessNPerRad *= Upgrade->GripMultiplier;
        Result.MaxBrakeForceN *= Upgrade->BrakeMultiplier;
        Result.MassKg *= Upgrade->MassMultiplier;
        // Keep the same static sag and damping ratio after weight reduction.
        Result.SpringRateNPerM *= Upgrade->MassMultiplier;
        Result.DampingNsPerM *= Upgrade->MassMultiplier;
    }
    if (const auto* Tune = Tunes.FindByPredicate([&](const auto& Item) { return Item.Id == Desired.TuneId; }))
        Result.FinalDrive *= Tune->FinalDriveMultiplier;
    return Result;
}

bool UADOwnershipSubsystem::WriteProfile(const FADGarageProfile& Candidate, FString& OutError)
{
    if (SavePath.IsEmpty()) return true;
    const FString ParentDirectory = FPaths::GetPath(SavePath);
    if (!IFileManager::Get().DirectoryExists(*ParentDirectory)
        && !IFileManager::Get().MakeDirectory(*ParentDirectory, true))
    { OutError = TEXT("Cannot create the profile directory. No credits or changes were committed."); return false; }
    // Exclusive sharing closes the stale-check/replace race between two game processes.
    FProfileWriteLock Lock(SavePath);
    if (!Lock.IsLocked()) { OutError=TEXT("The save file is in use. No credits were charged; please retry."); return false; }
    // Reject stale sessions instead of overwriting another instance's save or a newer schema.
    const bool bExistsNow = IFileManager::Get().FileExists(*SavePath);
    FString CurrentText;
    if (bExistsNow != bPrimaryExisted || (bExistsNow
        && (!FFileHelper::LoadFileToString(CurrentText, *SavePath) || CurrentText != PrimarySnapshot)))
    { OutError = TEXT("The profile changed on disk. Restart the game before saving; your changes were not charged."); return false; }

    const FString Temp = SavePath + TEXT(".tmp");
    const FString BackupTemp = SavePath + TEXT(".bak.tmp");
    const FString NewText = Encode(Candidate);
    const FString BackupText = Encode(Profile);
    const auto Stage = [&](const FString& Path, const FString& Text)
    {
        if (!WriteFlushed(Path, Text)) return false;
        FString Readback, ReadError;
        FADGarageProfile Verified;
        bool bMigrated = false;
        return Read(Path, StockDefinition.VehicleId, Verified, bMigrated, Readback, ReadError) == EReadProfile::Valid
            && Readback == Text && ValidateProfile(Verified, ReadError);
    };
    if (!Stage(Temp, NewText) || !Stage(BackupTemp, BackupText))
    { OutError = TEXT("Profile staging or verification failed. No credits or changes were committed."); return false; }
    // Back up the known-good in-memory profile, never a corrupt primary recovered at startup.
    if (!AtomicReplace(SavePath + TEXT(".bak"), BackupTemp) || !AtomicReplace(SavePath, Temp))
    { OutError = TEXT("Profile replacement failed. Previous ownership and credits are unchanged."); return false; }
    PrimarySnapshot = NewText;
    bPrimaryExisted = true;
    return true;
}

bool UADOwnershipSubsystem::Commit(const FADGarageProfile& Desired, FString& OutError)
{
    OutError.Reset();
    if (!bReady)
    { OutError = Error.IsEmpty() ? TEXT("Ownership data is not ready.") : Error; return false; }
    if (GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone)
    { OutError=TEXT("Vehicle purchases and garage changes are available offline only."); return false; }
    int64 Cost=0;
    if (!GetPurchaseCost(Desired,Cost,OutError)) return false;
    if (Cost>Profile.Credits)
    { OutError=TEXT("Not enough credits for the selected vehicle and parts. Earn credits in career races or choose an owned car."); return false; }
    FADGarageProfile Candidate = Desired;
    Candidate.Credits = Profile.Credits-Cost;
    // Collection edits are derived exclusively from the selected catalog vehicle.
    // Caller-supplied records can neither grant other cars nor alter their saved builds.
    Candidate.Vehicles=Profile.Vehicles;
    // Garage edits cannot manufacture career progress or erase reward receipts.
    Candidate.Reputation=Profile.Reputation;
    Candidate.RaceWins=Profile.RaceWins;
    Candidate.RacesFinished=Profile.RacesFinished;
    Candidate.CompletedChapters=Profile.CompletedChapters;
    Candidate.AwardedRaceIds=Profile.AwardedRaceIds;
    Candidate.DiscoveredLocations=Profile.DiscoveredLocations;
    Candidate.World=PendingWorld.bRecorded ? PendingWorld : Profile.World;
    Candidate.OwnedUpgrades.Sort();
    Candidate.EquippedUpgrades.Sort();
    StoreActiveRecord(Candidate);
    if (!ValidateProfile(Candidate,OutError)) return false;
    if (!WriteProfile(Candidate, OutError)) return false;
    Profile = MoveTemp(Candidate);
    Status = SavePath.IsEmpty() ? TEXT("Applied for this session. Disk saving is disabled.") : TEXT("Garage changes saved.");
    return true;
}

bool UADOwnershipSubsystem::CommitRaceResult(const FString& ReceiptId, const FString& ChapterId, int32 Place,
    FString& OutError)
{
    return CommitRaceResult(ReceiptId, ChapterId, Place, TArray<FADRivalRaceResult>(), OutError);
}

bool UADOwnershipSubsystem::CommitRaceResult(const FString& ReceiptId, const FString& ChapterId, int32 Place,
    const TArray<FADRivalRaceResult>& Rivals, FString& OutError)
{
    OutError.Reset();
    if (!bReady) { OutError=Error; return false; }
    if (GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone)
    { OutError=TEXT("Career rewards are available in offline career only."); return false; }
    FGuid Receipt;
    if (!FGuid::ParseExact(ReceiptId,EGuidFormats::Digits,Receipt) || !Receipt.IsValid())
    { OutError=TEXT("Race receipt is invalid."); return false; }
    if (Profile.AwardedRaceIds.Contains(ReceiptId)) return true;
    auto* Career=GetGameInstance()->GetSubsystem<UADCareerSubsystem>();
    FADCareerReward Reward;
    if (!Career || !Career->ComputeReward(Profile,ChapterId,Place,Reward,OutError)) return false;
    if (!Rivals.IsEmpty())
    {
        if (Rivals.Num() > 15)
        { OutError=TEXT("Race result contains too many named rivals."); return false; }
        TSet<FString> RivalIds;
        TSet<int32> Places;
        Places.Add(Place);
        for (const FADRivalRaceResult& Result : Rivals)
        {
            if (Result.RivalId.IsEmpty() || Result.RivalId.Len() > 48
                || Result.RivalId != Result.RivalId.ToLower() || Result.Place < 1 || Result.Place > Rivals.Num() + 1
                || RivalIds.Contains(Result.RivalId) || Places.Contains(Result.Place))
            { OutError=TEXT("Career rival classification is duplicated or outside the finalized race places."); return false; }
            for (const TCHAR Character : Result.RivalId)
                if ((Character < TEXT('a') || Character > TEXT('z'))
                    && (Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('_'))
                { OutError=TEXT("Career rival ID contains unsupported characters."); return false; }
            RivalIds.Add(Result.RivalId);
            Places.Add(Result.Place);
        }
        if (Places.Num() != Rivals.Num() + 1)
        { OutError=TEXT("Career rival classification is incomplete."); return false; }
    }
    FADGarageProfile Candidate=Profile;
    if (PendingWorld.bRecorded) Candidate.World=PendingWorld;
    Candidate.Credits=FMath::Min(MaxCredits,Candidate.Credits+Reward.Credits);
    Candidate.Reputation=FMath::Min(MaxCredits,Candidate.Reputation+Reward.Rep);
    Candidate.RacesFinished=FMath::Min(MaxCredits,Candidate.RacesFinished+1);
    if (Place==1) Candidate.RaceWins=FMath::Min(MaxCredits,Candidate.RaceWins+1);
    if (Reward.bAdvance) Candidate.CompletedChapters.Add(Reward.ChapterId);
    for (const FADRivalRaceResult& Result : Rivals)
    {
        FADRivalMemory* Memory=Candidate.RivalMemories.FindByPredicate(
            [&](const FADRivalMemory& Item) { return Item.RivalId==Result.RivalId; });
        if (!Memory)
        {
            if (Candidate.RivalMemories.Num()>=MaxRivalMemories)
            { OutError=TEXT("Rival history is full; no part of this race result was saved."); return false; }
            FADRivalMemory NewMemory;
            NewMemory.RivalId=Result.RivalId;
            Candidate.RivalMemories.Add(MoveTemp(NewMemory));
            Memory=&Candidate.RivalMemories.Last();
        }
        if (Memory->Encounters>=MaxRivalEncounters)
        { OutError=TEXT("Rival encounter history reached its supported limit."); return false; }
        ++Memory->Encounters;
        Memory->LastChapterId=Reward.ChapterId;
        if (Place<Result.Place)
        {
            ++Memory->PlayerWins;
            Memory->Respect=FMath::Min(1.f,Memory->Respect+.15f);
            Memory->Grudge=FMath::Max(0.f,Memory->Grudge-.10f);
        }
        else
        {
            ++Memory->RivalWins;
            Memory->Grudge=FMath::Min(1.f,Memory->Grudge+.20f);
            Memory->Respect=FMath::Max(0.f,Memory->Respect-.05f);
        }
    }
    Candidate.AwardedRaceIds.Add(ReceiptId);
    if (Candidate.AwardedRaceIds.Num()>128) Candidate.AwardedRaceIds.RemoveAt(0);
    if (!ValidateProfile(Candidate,OutError) || !WriteProfile(Candidate,OutError)) return false;
    Profile=MoveTemp(Candidate);
    Status=FString::Printf(TEXT("CAREER SAVED  +%lld CR  +%lld REP"),Reward.Credits,Reward.Rep);
    return true;
}

bool UADOwnershipSubsystem::CommitDiscovery(const FString& LocationId,FString& OutError)
{
    OutError.Reset();
    if (!bReady) { OutError=Error.IsEmpty() ? TEXT("Ownership is unavailable.") : Error; return false; }
    if (GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone)
    { OutError=TEXT("Discovery rewards are available offline only."); return false; }
    const auto* Location=Discoveries.FindByPredicate([&](const auto& Item) { return Item.Id==LocationId; });
    if (!Location) { OutError=TEXT("Unknown discovery location."); return false; }
    if (Profile.DiscoveredLocations.Contains(LocationId)) return true;
    FADGarageProfile Candidate=Profile;
    if (PendingWorld.bRecorded) Candidate.World=PendingWorld;
    Candidate.DiscoveredLocations.Add(LocationId);
    Candidate.Credits=FMath::Min(MaxCredits,Candidate.Credits+Location->Credits);
    Candidate.Reputation=FMath::Min(MaxCredits,Candidate.Reputation+Location->Reputation);
    if (!ValidateProfile(Candidate,OutError) || !WriteProfile(Candidate,OutError)) return false;
    Profile=MoveTemp(Candidate);
    Status=FString::Printf(TEXT("%s  +%lld CR  +%lld REP%s"),*Location->Name,Location->Credits,Location->Reputation,
        SavePath.IsEmpty() ? TEXT("  (session only)") : TEXT("  / SAVED"));
    return true;
}

void UADOwnershipSubsystem::StageWorldSnapshot(const FADWorldSnapshot& Snapshot)
{
    if (bReady && Snapshot.bRecorded && Snapshot.IsValid()
        && (!GetWorld() || GetWorld()->GetNetMode()==NM_Standalone)) PendingWorld=Snapshot;
}

bool UADOwnershipSubsystem::SaveWorldSnapshot(FString& OutError)
{
    OutError.Reset();
    if (!bReady) { OutError=Error; return false; }
    if (GetWorld() && GetWorld()->GetNetMode()!=NM_Standalone)
    { OutError=TEXT("Offline world saving is unavailable in an online session."); return false; }
    if (!PendingWorld.bRecorded) return true;
    FADGarageProfile Candidate=Profile;
    Candidate.World=PendingWorld;
    if (!ValidateProfile(Candidate,OutError) || !WriteProfile(Candidate,OutError)) return false;
    Profile=MoveTemp(Candidate);
    return true;
}
