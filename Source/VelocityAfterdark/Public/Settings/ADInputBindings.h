#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FADBindingSlot
{
    FName Id;
    FName Action;
    FString Label;
    FKey DefaultKey;
    bool bGamepad=false;
    bool bAxis=false;
    bool bNegate=false;
};

/** Small validated profile. Menu/navigation keys remain fixed so a bad binding cannot trap the player. */
struct VELOCITYAFTERDARK_API FADInputBindings
{
    static const TArray<FADBindingSlot>& GetSlots();
    FKey Get(FName Id) const;
    bool Assign(FName Id,FKey Key,FString& Message);
    bool Load(const TMap<FName,FKey>& Values,FString& Error);
    void ResetDevice(bool bGamepad);
    bool Equals(const FADInputBindings& Other) const;
private:
    static bool IsAllowed(const FADBindingSlot& Slot,FKey Key);
    TMap<FName,FKey> Overrides;
};

/** One transform for drawing and pointer hit testing. Scale always fits the complete 1920 x 1080 layout. */
struct VELOCITYAFTERDARK_API FADCanvasTransform
{
    float Scale=1.f;
    FVector2D Offset=FVector2D::ZeroVector;
    static FADCanvasTransform Fit(float Width,float Height,float UserScale);
    FVector2D ToCanvas(FVector2D Point) const { return (Point-Offset)/Scale; }
    FVector2D ToScreen(FVector2D Point) const { return Offset+Point*Scale; }
};
