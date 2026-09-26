#include "Career/ADCareerSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ownership/ADOwnershipSubsystem.h"
#include "Racing/ADRaceCatalog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogADCareer, Log, All);

namespace
{
constexpr int64 MaximumCatalogBytes = 128 * 1024;
constexpr int64 MaximumReward = 1000000;
constexpr int64 MaximumReputation = 1000000000;

bool ReadInteger(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int64 Minimum,
    int64 Maximum, int64& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Key) : nullptr;
    double Number = 0;
    if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number)
        || !FMath::IsFinite(Number) || Number != FMath::FloorToDouble(Number)
        || Number < static_cast<double>(Minimum) || Number > static_cast<double>(Maximum))
    {
        Error = FString::Printf(TEXT("Career field '%s' must be an integer in [%lld, %lld]."),
            Key, static_cast<long long>(Minimum), static_cast<long long>(Maximum));
        return false;
    }
    Out = static_cast<int64>(Number);
    return true;
}

bool ReadText(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 MaximumLength,
    FString& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Key) : nullptr;
    if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(Out)
        || Out.IsEmpty() || Out.Len() > MaximumLength || Out != Out.TrimStartAndEnd())
    {
        Error = FString::Printf(TEXT("Career field '%s' must be nonempty text of at most %d characters without surrounding whitespace."),
            Key, MaximumLength);
        return false;
    }
    for (const TCHAR Character : Out)
    {
        if (Character < 32 || Character == 127)
        {
            Error = FString::Printf(TEXT("Career field '%s' contains a control character."), Key);
            return false;
        }
    }
    return true;
}

bool ReadId(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out, FString& Error)
{
    if (!ReadText(Object, Key, 48, Out, Error)) return false;
    for (const TCHAR Character : Out)
    {
        if ((Character < TEXT('a') || Character > TEXT('z'))
            && (Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('_'))
        {
            Error = FString::Printf(TEXT("Career identifier '%s' must contain only lowercase ASCII letters, digits and underscores."), Key);
            return false;
        }
    }
    return true;
}

bool ReadArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
    const TArray<TSharedPtr<FJsonValue>>*& Out, FString& Error)
{
    if (!Object.IsValid() || !Object->TryGetArrayField(Key, Out) || !Out
        || Out->IsEmpty() || Out->Num() > 64)
    {
        Error = FString::Printf(TEXT("Career field '%s' must contain 1-64 entries."), Key);
        return false;
    }
    return true;
}

bool ReadObject(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    if (!Value.IsValid() || Value->Type != EJson::Object || !(Out = Value->AsObject()).IsValid())
    {
        Error = TEXT("Every career rank and chapter must be a JSON object.");
        return false;
    }
    return true;
}
}

void UADCareerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FString LoadError;
    if (!LoadCatalog(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Career/career.json")), LoadError))
    {
        Error = MoveTemp(LoadError);
        UE_LOG(LogADCareer, Error, TEXT("Career unavailable: %s"), *Error);
    }
}

bool UADCareerSubsystem::LoadCatalog(const FString& AbsolutePath, FString& OutError)
{
    OutError.Reset();
    const int64 Bytes = IFileManager::Get().FileSize(*AbsolutePath);
    if (Bytes < 1 || Bytes > MaximumCatalogBytes)
    {
        OutError = TEXT("Career catalog is missing, empty or larger than 128 KB.");
        return false;
    }
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *AbsolutePath))
    {
        OutError = TEXT("Career catalog could not be read.");
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = FString::Printf(TEXT("Career catalog JSON is malformed: %s"), *Reader->GetErrorMessage());
        return false;
    }
    int64 Schema = 0;
    if (!ReadInteger(Root, TEXT("schemaVersion"), 1, 1, Schema, OutError)) return false;

    TArray<FADCareerChapter> CandidateChapters;
    TArray<FADCareerRank> CandidateRanks;
    TSet<FString> ChapterIds;
    TSet<FString> RankIds;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!ReadArray(Root, TEXT("ranks"), Items, OutError)) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        TSharedPtr<FJsonObject> Object;
        FADCareerRank Rank;
        if (!ReadObject(Item, Object, OutError)
            || !ReadId(Object, TEXT("id"), Rank.Id, OutError)
            || !ReadText(Object, TEXT("name"), 48, Rank.Name, OutError)
            || !ReadInteger(Object, TEXT("minimumRep"), 0, MaximumReputation, Rank.MinimumRep, OutError)) return false;
        if (RankIds.Contains(Rank.Id) || (CandidateRanks.IsEmpty() && Rank.MinimumRep != 0)
            || (!CandidateRanks.IsEmpty() && Rank.MinimumRep <= CandidateRanks.Last().MinimumRep))
        {
            OutError = TEXT("Career ranks need unique IDs and strictly increasing REP thresholds starting at zero.");
            return false;
        }
        RankIds.Add(Rank.Id);
        CandidateRanks.Add(MoveTemp(Rank));
    }

    if (!ReadArray(Root, TEXT("chapters"), Items, OutError)) return false;
    int64 TotalRep = 0;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        TSharedPtr<FJsonObject> Object;
        FADCareerChapter Chapter;
        int64 Difficulty = 0;
        int64 Rep = 0;
        if (!ReadObject(Item, Object, OutError)
            || !ReadId(Object, TEXT("id"), Chapter.Id, OutError)
            || !ReadText(Object, TEXT("title"), 64, Chapter.Title, OutError)
            || !ReadText(Object, TEXT("crew"), 64, Chapter.Crew, OutError)
            || !ReadText(Object, TEXT("leader"), 64, Chapter.Leader, OutError)
            || !ReadText(Object, TEXT("briefing"), 512, Chapter.Briefing, OutError)
            || !ReadText(Object, TEXT("victoryLine"), 256, Chapter.VictoryLine, OutError)
            || !ReadId(Object, TEXT("raceId"), Chapter.RaceId, OutError)
            || !ReadInteger(Object, TEXT("difficulty"), 0, 2, Difficulty, OutError)
            || !ReadInteger(Object, TEXT("winCredits"), 1, MaximumReward, Chapter.WinCredits, OutError)
            || !ReadInteger(Object, TEXT("finishCredits"), 0, MaximumReward, Chapter.FinishCredits, OutError)
            || !ReadInteger(Object, TEXT("repReward"), 1, MaximumReward, Rep, OutError)) return false;
        if (ChapterIds.Contains(Chapter.Id) || Chapter.FinishCredits > Chapter.WinCredits)
        {
            OutError = TEXT("Career chapters need unique IDs and a finish stipend no larger than the winning prize.");
            return false;
        }
        Chapter.Difficulty = static_cast<int32>(Difficulty);
        Chapter.RepReward = static_cast<int32>(Rep);
        ChapterIds.Add(Chapter.Id);
        TotalRep += Rep;
        CandidateChapters.Add(MoveTemp(Chapter));
    }
    if (TotalRep < CandidateRanks.Last().MinimumRep)
    {
        OutError = TEXT("Completing the career must award enough REP to reach its highest rank.");
        return false;
    }

    // Reject dangling route identities when loading content, before a player can
    // reach an unwinnable chapter. Race files are resolved through a safe catalog.
    FADRaceCatalog RaceCatalog;
    if (!RaceCatalog.LoadDefault(OutError)) return false;
    for (const FADCareerChapter& Chapter : CandidateChapters)
    {
        const FADRaceDefinition* Race = RaceCatalog.Find(Chapter.RaceId);
        if (!Race || Race->Opponents.Num() != 3)
        {
            OutError = FString::Printf(TEXT("Chapter '%s' must reference a validated four-car race; '%s' is unavailable or incompatible."),
                *Chapter.Id, *Chapter.RaceId);
            return false;
        }
    }

    Chapters = MoveTemp(CandidateChapters);
    Ranks = MoveTemp(CandidateRanks);
    Error.Reset();
    bReady = true;
    return true;
}

bool UADCareerSubsystem::ValidateProgress(const FADGarageProfile& Profile, FString& OutError) const
{
    OutError.Reset();
    if (!bReady)
    {
        OutError = Error.IsEmpty() ? TEXT("Career catalog is not loaded.") : Error;
        return false;
    }
    if (Profile.Reputation < 0 || Profile.Reputation > MaximumReputation
        || Profile.CompletedChapters.Num() > Chapters.Num())
    {
        OutError = TEXT("Saved career progress is outside supported bounds.");
        return false;
    }
    // Reject holes, unknown IDs, duplicates and reordered progress instead of silently skipping story.
    for (int32 Index = 0; Index < Profile.CompletedChapters.Num(); ++Index)
    {
        if (Profile.CompletedChapters[Index] != Chapters[Index].Id)
        {
            OutError = TEXT("Saved chapters are not a contiguous prefix of this career catalog.");
            return false;
        }
    }
    return true;
}

const FADCareerChapter* UADCareerSubsystem::GetActiveChapter(const FADGarageProfile& Profile) const
{
    FString ProgressError;
    if (!ValidateProgress(Profile, ProgressError) || Profile.CompletedChapters.Num() == Chapters.Num()) return nullptr;
    return &Chapters[Profile.CompletedChapters.Num()];
}

FString UADCareerSubsystem::GetRankName(int64 Reputation) const
{
    if (!bReady || Ranks.IsEmpty() || Reputation < 0 || Reputation > MaximumReputation) return TEXT("Unavailable");
    for (int32 Index = Ranks.Num() - 1; Index >= 0; --Index)
    {
        if (Reputation >= Ranks[Index].MinimumRep) return Ranks[Index].Name;
    }
    return Ranks[0].Name;
}

FString UADCareerSubsystem::ComposeBriefing(const FADGarageProfile& Profile,
    const FADCareerChapter& Chapter, const FADRaceDefinition& Race) const
{
    const FADRivalMemory* MostRelevant = nullptr;
    const FADRaceOpponent* RelevantOpponent = nullptr;
    for (const FADRaceOpponent& Opponent : Race.Opponents)
    {
        const FADRivalMemory* Memory = Profile.RivalMemories.FindByPredicate(
            [&Opponent](const FADRivalMemory& Item) { return Item.RivalId == Opponent.Id; });
        if (Memory && (!MostRelevant || Memory->Encounters > MostRelevant->Encounters))
        {
            MostRelevant = Memory;
            RelevantOpponent = &Opponent;
        }
    }
    if (!MostRelevant || !RelevantOpponent || MostRelevant->Encounters < 1) return Chapter.Briefing;

    FString Context;
    if (MostRelevant->PlayerWins > MostRelevant->RivalWins)
        Context = FString::Printf(TEXT("%s remembers the last loss and wants a runback."), *RelevantOpponent->Name);
    else if (MostRelevant->RivalWins > MostRelevant->PlayerWins)
        Context = FString::Printf(TEXT("%s beat you last time and expects you to fold."), *RelevantOpponent->Name);
    else
        Context = FString::Printf(TEXT("%s remembers your tied series. This run decides who leads."), *RelevantOpponent->Name);
    return Chapter.Briefing + TEXT(" ") + Context;
}

FString UADCareerSubsystem::ComposeVictoryLine(const FADGarageProfile& Profile,
    const FADCareerChapter& Chapter, const FADRaceDefinition& Race) const
{
    const FADRivalMemory* MostRelevant = nullptr;
    const FADRaceOpponent* RelevantOpponent = nullptr;
    for (const FADRaceOpponent& Opponent : Race.Opponents)
    {
        const FADRivalMemory* Memory = Profile.RivalMemories.FindByPredicate(
            [&Opponent](const FADRivalMemory& Item) { return Item.RivalId == Opponent.Id; });
        if (Memory && (!MostRelevant || Memory->Encounters > MostRelevant->Encounters))
        {
            MostRelevant = Memory;
            RelevantOpponent = &Opponent;
        }
    }
    if (!MostRelevant || !RelevantOpponent) return Chapter.VictoryLine;

    const FString SeriesLine = FString::Printf(TEXT("%s series: %d to %d. Respect %d%% / grudge %d%%."),
        *RelevantOpponent->Name, MostRelevant->PlayerWins, MostRelevant->RivalWins,
        FMath::RoundToInt(MostRelevant->Respect * 100.f), FMath::RoundToInt(MostRelevant->Grudge * 100.f));
    return Chapter.VictoryLine + TEXT(" ") + SeriesLine;
}

bool UADCareerSubsystem::ComputeReward(const FADGarageProfile& Profile, const FString& ChapterId,
    int32 Place, FADCareerReward& OutReward, FString& OutError) const
{
    OutError.Reset();
    OutReward = FADCareerReward{};
    if (!ValidateProgress(Profile, OutError)) return false;
    if (Place < 1 || Place > 4)
    {
        OutError = TEXT("Only a completed race classified in places 1-4 is eligible for a career reward.");
        return false;
    }
    const FADCareerChapter* Chapter = GetActiveChapter(Profile);
    if (!Chapter || Chapter->Id != ChapterId)
    {
        OutError = TEXT("This result does not belong to the current unfinished career chapter.");
        return false;
    }
    const bool bWinner = Place == 1;
    if (bWinner && Profile.Reputation > MaximumReputation - Chapter->RepReward)
    {
        OutError = TEXT("Career REP would exceed the supported profile limit.");
        return false;
    }
    OutReward.Credits = bWinner ? Chapter->WinCredits : Chapter->FinishCredits;
    OutReward.Rep = bWinner ? Chapter->RepReward : 0;
    OutReward.bAdvance = bWinner;
    OutReward.ChapterId = Chapter->Id;
    return true;
}
