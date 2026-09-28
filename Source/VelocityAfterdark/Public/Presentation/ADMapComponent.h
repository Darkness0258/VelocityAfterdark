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
    constexpr float WorldScale=.0047f;
    inline FVector2D Project(FVector2D World,float Zoom=1.f,FVector2D Pan=FVector2D::ZeroVector)
    { const FVector2D Relative=(World-Pan)*(WorldScale*Zoom); return FVector2D(X+Width*.5f+Relative.X,Y+Height*.5f-Relative.Y); }
    inline FVector2D Unproject(FVector2D Canvas,float Zoom=1.f,FVector2D Pan=FVector2D::ZeroVector)
    { const FVector2D Relative((Canvas.X-X-Width*.5f)/(WorldScale*Zoom),-(Canvas.Y-Y-Height*.5f)/(WorldScale*Zoom)); return Pan+Relative; }
}

/** Player-centred heading-up minimap projection, in reference HUD pixels. */
namespace ADMinimapLayout
{
    constexpr float PixelsPerCm=.0024f;
    inline FVector2D Project(FVector2D World,FVector2D Player,FVector2D Forward)
    {
        FVector2D Heading=Forward.GetSafeNormal();
        if (Heading.IsNearlyZero()) Heading=FVector2D(1.,0.);
        const FVector2D Delta=World-Player;
        const FVector2D Right(-Heading.Y,Heading.X);
        return FVector2D(FVector2D::DotProduct(Delta,Right)*PixelsPerCm,
            -FVector2D::DotProduct(Delta,Heading)*PixelsPerCm);
    }
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
    void FastTravel();
    void ClearWaypoint();
    void Zoom(float Delta);
    void Pan(FVector2D WorldDelta);
    FVector2D Project(FVector2D World) const { return ADMapLayout::Project(World,ZoomFactor,PanCenter); }
    float GetZoomFactor() const { return ZoomFactor; }
    FVector2D GetPanCenter() const { return PanCenter; }
    const FString& GetMessage() const { return Message; }
    const TArray<int32>& GetVisibleLocations() const { return VisibleLocations; }
    int32 GetSelectedRow() const { return SelectedRow; }
    FString GetFilterName() const;
    const FADDiscoveryDefinition* GetSelectedLocation() const;
    AADExplorationDirector* GetExploration() const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void RebuildFilter();
    void SetWaypoint(FVector2D CanvasPosition);
    AADPlayerController* Controller() const;
    TArray<int32> VisibleLocations;
    int32 SelectedRow=0;
    int32 Filter=0;
    float ZoomFactor=1.f;
    FVector2D PanCenter=FVector2D::ZeroVector;
    FString Message;
    bool bOpen=false;
    bool bPausedByMap=false;
    bool bPreviousCursor=false;
};
