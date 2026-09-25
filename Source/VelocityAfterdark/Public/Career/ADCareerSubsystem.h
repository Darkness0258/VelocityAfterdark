#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ADCareerSubsystem.generated.h"

struct FADGarageProfile;

/** Authored challenge data. Chapter order is the unlock order; RaceId identifies a real race definition. */
struct VELOCITYAFTERDARK_API FADCareerChapter
{
    FString Id;
    FString Title;
    FString Crew;
    FString Leader;
    FString Briefing;
    FString VictoryLine;
    FString RaceId;
    int32 Difficulty = 0;
    int64 WinCredits = 0;
    int64 FinishCredits = 0;
    int32 RepReward = 0;
};

struct VELOCITYAFTERDARK_API FADCareerRank
{
    FString Id;
    FString Name;
    int64 MinimumRep = 0;
};

/** A decision, not a payment. Ownership must atomically save this with a unique race receipt. */
struct VELOCITYAFTERDARK_API FADCareerReward
{
    int64 Credits = 0;
    int64 Rep = 0;
    bool bAdvance = false;
    FString ChapterId;
};

/** Immutable career catalog and progression rules. No currency, profile or race-state mutation. */
UCLASS()
class VELOCITYAFTERDARK_API UADCareerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    bool IsReady() const { return bReady; }
    const FString& GetError() const { return Error; }
    const TArray<FADCareerChapter>& GetChapters() const { return Chapters; }
    const TArray<FADCareerRank>& GetRanks() const { return Ranks; }

    // Null means the series is finished, unavailable, or the saved completion order is invalid.
    const FADCareerChapter* GetActiveChapter(const FADGarageProfile& Profile) const;
    FString GetRankName(int64 Reputation) const;

    // Only the current chapter can pay out. A completed race in places 2-4 pays a finish stipend;
    // only first place grants REP and advances. DNF must never call this with a classified place.
    bool ComputeReward(const FADGarageProfile& Profile, const FString& ChapterId, int32 Place,
        FADCareerReward& OutReward, FString& OutError) const;

    // Explicit paths allow data fixtures. A failed reload preserves the last valid catalog.
    bool LoadCatalog(const FString& AbsolutePath, FString& OutError);

private:
    bool ValidateProgress(const FADGarageProfile& Profile, FString& OutError) const;

    TArray<FADCareerChapter> Chapters;
    TArray<FADCareerRank> Ranks;
    FString Error;
    bool bReady = false;
};
