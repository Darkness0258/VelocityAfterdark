#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Settings/ADInputBindings.h"
#include "ADSettingsSubsystem.generated.h"

/** Applies supported engine scalability options and persists presentation preferences. */
UCLASS()
class VELOCITYAFTERDARK_API UADSettingsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    void Open();
    void Close();
    bool IsOpen() const { return bOpen; }
    void Select(int32 Delta);
    void Adjust(int32 Delta);
    void Confirm();
    void Back();
    void Click(FVector2D Point);
    bool IsCapturingBinding() const { return !CapturingBinding.IsNone(); }
    void CaptureBinding(FKey Key);
    void CancelCapture();
    bool IsBindingsPage() const { return bBindingsPage; }
    FString GetPageTitle() const;
    int32 GetRowCount() const;
    int32 GetFirstVisibleRow() const;
    static constexpr int32 VisibleRows=7;
    const FADInputBindings& GetBindings() const { return bOpen ? Snapshot.Bindings : Bindings; }
    int32 GetBindingRevision() const { return BindingRevision; }
    void Apply();
    int32 GetSelectedRow() const { return SelectedRow; }
    FString GetRowLabel(int32 Index) const;
    FString GetRowValue(int32 Index) const;
    const FString& GetMessage() const { return Message; }
    float GetUIScale() const { return bOpen ? Snapshot.UIScale : UIScale; }
    bool UsesMph() const { return bOpen ? Snapshot.bMph : bMph; }
    bool UsesSpeedFov() const { return bOpen ? Snapshot.bSpeedFov : bSpeedFov; }
    bool UsesSimulationDamage() const { return bOpen ? Snapshot.bSimulationDamage : bSimulationDamage; }
private:
    void ReadGraphicsState();
    bool VerifySavedSettings() const;
    void SaveSnapshot();
    void RestoreSnapshot();
    struct FSnapshot
    {
        int32 Quality=1,FpsIndex=1;
        float ResolutionPercent=85.f,FrameRateLimit=60.f;
        float MasterVolume=.8f,UIScale=1.f;
        bool bMph=false,bSpeedFov=true,bSimulationDamage=false,bMotionBlur=false,bVSync=false;
        FADInputBindings Bindings;
    } Snapshot;
    FString Message;
    int32 SelectedRow=0;
    int32 Quality=1;
    float ResolutionPercent=85.f;
    float FrameRateLimit=60.f;
    int32 FpsIndex=1;
    float MasterVolume=.8f;
    float UIScale=1.f;
    bool bMph=false;
    bool bSpeedFov=true;
    bool bSimulationDamage=false;
    bool bMotionBlur=false;
    bool bVSync=false;
    bool bOpen=false;
    bool bPersistenceAllowed=true;
    bool bBindingsPage=false;
    bool bGamepadBindings=false;
    FADInputBindings Bindings;
    FName CapturingBinding;
    int32 BindingRevision=0;
    TArray<const FADBindingSlot*> GetVisibleBindings() const;
};
