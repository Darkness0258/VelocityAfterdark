#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ADHUD.generated.h"

/** Resolution-independent minimal HUD; all displayed driving values use physics telemetry. */
UCLASS()
class VELOCITYAFTERDARK_API AADHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void Label(const FString& Text,float X,float Y,float Size,FLinearColor Color);
    void Panel(float X,float Y,float Width,float Height,FLinearColor Color);
    void DrawRace();
    void DrawGarage();
    void DrawSettings();
    void DrawMap();
    void DrawMinimap();
    float UiScale=1;
    float UiOffsetX=0;
    float UiOffsetY=0;
    TArray<FVector2D> MinimapWaypointRoute;
    TArray<FVector2D> MinimapTrafficLocations;
    FVector2D MinimapLastRouteStart=FVector2D::ZeroVector;
    FVector2D MinimapLastRouteGoal=FVector2D::ZeroVector;
    double MinimapLastRouteUpdate=-1.;
    double MinimapWaypointDistanceCm=0.;
    FString MinimapRouteError;
};
