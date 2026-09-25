#include "World/ADWorldProgress.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FADWorldSnapshot::IsValid() const
{
    return FMath::IsFinite(Hour) && Hour >= 0. && Hour < 24.
        && FMath::IsFinite(WeatherElapsed) && WeatherElapsed >= 0. && WeatherElapsed <= 86400.
        && WeatherIndex >= 0 && WeatherIndex < 16 && Weather <= 2
        && FMath::IsFinite(Wetness) && Wetness >= 0.f && Wetness <= 1.f
        && FMath::IsFinite(RainAmount) && RainAmount >= 0.f && RainAmount <= 1.f
        && FMath::IsFinite(FogAmount) && FogAmount >= 0.f && FogAmount <= 1.f;
}

bool FADDiscoveryDefinition::LoadCatalog(const FString& Path, TArray<FADDiscoveryDefinition>& Out, FString& Error)
{
    Error.Reset();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    const int64 Bytes = IFileManager::Get().FileSize(*Path);
    if (Bytes < 1 || Bytes > 65536 || !FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    { Error = TEXT("Discovery catalog is missing, too large, or malformed."); return false; }
    const auto Number = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Min, double Max, double& Value)
    {
        const auto Field = Object->TryGetField(Key);
        return Field && Field->Type == EJson::Number && Field->TryGetNumber(Value)
            && FMath::IsFinite(Value) && Value >= Min && Value <= Max;
    };
    double Value = 0.;
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!Number(Root, TEXT("schemaVersion"), 1, 1, Value)
        || !Root->TryGetArrayField(TEXT("locations"), Entries) || Entries->Num() < 1 || Entries->Num() > 128)
    { Error = TEXT("Discovery catalog schema or location count is invalid."); return false; }
    TArray<FADDiscoveryDefinition> Candidate;
    for (const auto& Entry : *Entries)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FADDiscoveryDefinition Item;
        const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
        if (!Entry || !Entry->TryGetObject(Object) || !Object || !Object->IsValid()
            || !(*Object)->TryGetStringField(TEXT("id"), Item.Id) || Item.Id.IsEmpty() || Item.Id.Len() > 48
            || !(*Object)->TryGetStringField(TEXT("name"), Item.Name) || Item.Name.IsEmpty() || Item.Name.Len() > 48
            || !(*Object)->TryGetStringField(TEXT("description"), Item.Description) || Item.Description.IsEmpty() || Item.Description.Len() > 180
            || !(*Object)->TryGetArrayField(TEXT("position"), Position) || Position->Num() != 2)
        { Error = TEXT("A discovery location has invalid text or position fields."); return false; }
        for (const TCHAR C : Item.Id)
            if (!(C >= 'a' && C <= 'z') && !(C >= '0' && C <= '9') && C != '_')
            { Error = TEXT("Discovery IDs must contain lowercase letters, numbers or underscores."); return false; }
        if (Candidate.ContainsByPredicate([&](const auto& Existing) { return Existing.Id == Item.Id; }))
        { Error = TEXT("Discovery IDs must be unique."); return false; }
        double Coordinates[2]{};
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const auto& Field = (*Position)[Index];
            if (!Field || Field->Type != EJson::Number || !Field->TryGetNumber(Coordinates[Index])
                || !FMath::IsFinite(Coordinates[Index]) || FMath::Abs(Coordinates[Index]) > 10000000.)
            { Error = TEXT("Discovery coordinates must be finite world centimeters."); return false; }
        }
        Item.Position = FVector2D(Coordinates[0], Coordinates[1]);
        if (!Number(*Object, TEXT("radiusCm"), 500, 5000, Value))
        { Error = TEXT("Discovery radius must be between 5 and 50 meters."); return false; }
        Item.RadiusCm = static_cast<float>(Value);
        if (!Number(*Object, TEXT("dwellSeconds"), .5, 10, Value))
        { Error = TEXT("Discovery dwell time must be between 0.5 and 10 seconds."); return false; }
        Item.DwellSeconds = static_cast<float>(Value);
        if (!Number(*Object, TEXT("credits"), 0, 10000, Value) || Value != FMath::FloorToDouble(Value))
        { Error = TEXT("Discovery credits are invalid."); return false; }
        Item.Credits = static_cast<int64>(Value);
        if (!Number(*Object, TEXT("reputation"), 0, 1000, Value) || Value != FMath::FloorToDouble(Value))
        { Error = TEXT("Discovery reputation is invalid."); return false; }
        Item.Reputation = static_cast<int64>(Value);
        Candidate.Add(MoveTemp(Item));
    }
    Out = MoveTemp(Candidate);
    return true;
}
