#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADMapComponent.generated.h"

class AADExplorationDirector;
class AADPlayerController;
struct FADDiscoveryDefinition;

namespace ADMapLayout
{
    constexpr float X=72.f,Y=174.f,Width=1190.f,Height=780.f;
    constexpr float ListX=1310.f,ListY=233.f,ListWidth=535.f,RowHeight=67.f;
    inline FVector2D Project(FVector2D World)
    { return FVector2D(X+Width*.5f+World.X*.0047,Y+Height*.5f-World.Y*.0047); }
}

/** Offline map modal: owns pause/input restoration and discovery filtering. */
UCLASS()
class VELOCITYAFTERDARK_API UADMapComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void Toggle();
    void Close();
    bool IsOpen() const { return bOpen; }
    void Select(int32 Delta);
    void ChangeFilter(int32 Delta);
    void Confirm();
    void FollowNearest();
    void Click(FVector2D CanvasPosition);
    const TArray<int32>& GetVisibleLocations() const { return VisibleLocations; }
    int32 GetSelectedRow() const { return SelectedRow; }
    FString GetFilterName() const;
    const FADDiscoveryDefinition* GetSelectedLocation() const;
    AADExplorationDirector* GetExploration() const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void RebuildFilter();
    AADPlayerController* Controller() const;
    TArray<int32> VisibleLocations;
    int32 SelectedRow=0;
    int32 Filter=0;
    bool bOpen=false;
    bool bPausedByMap=false;
    bool bPreviousCursor=false;
};
