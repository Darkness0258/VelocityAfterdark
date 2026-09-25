#include "Settings/ADSettingsSubsystem.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Scalability.h"

namespace
{
const TCHAR* Section=TEXT("Afterdark.Presentation");
const TCHAR* InputSection=TEXT("Afterdark.InputBindings");
constexpr int32 SettingsRowCount=12;
constexpr int32 FrameLimits[]={0,60,90,120};
const TCHAR* QualityNames[]={TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("ULTRA"),TEXT("CINEMATIC")};
}

void UADSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bPersistenceAllowed=!GIsAutomationTesting && !FParse::Param(FCommandLine::Get(),TEXT("unattended"));
    ReadGraphicsState();
    if (!bPersistenceAllowed || !GConfig) return;
    GConfig->GetFloat(Section,TEXT("MasterVolume"),MasterVolume,GGameUserSettingsIni);
    GConfig->GetFloat(Section,TEXT("UIScale"),UIScale,GGameUserSettingsIni);
    GConfig->GetBool(Section,TEXT("UseMph"),bMph,GGameUserSettingsIni);
    GConfig->GetBool(Section,TEXT("SpeedFov"),bSpeedFov,GGameUserSettingsIni);
    GConfig->GetBool(Section,TEXT("SimulationDamage"),bSimulationDamage,GGameUserSettingsIni);
    GConfig->GetBool(Section,TEXT("MotionBlur"),bMotionBlur,GGameUserSettingsIni);
    MasterVolume=FMath::IsFinite(MasterVolume) ? FMath::Clamp(MasterVolume,0.f,1.f) : .8f;
    UIScale=FMath::IsFinite(UIScale) ? FMath::Clamp(UIScale,.7f,1.f) : 1.f;
    TMap<FName,FKey> SavedBindings;
    for (const auto& Slot:FADInputBindings::GetSlots())
    {
        FString KeyName;
        if (GConfig->GetString(InputSection,*Slot.Id.ToString(),KeyName,GGameUserSettingsIni))
            SavedBindings.Add(Slot.Id,FKey(FName(*KeyName)));
    }
    FString BindingError;
    if (!Bindings.Load(SavedBindings,BindingError)) Message=BindingError+TEXT(" Defaults restored for this session.");
    FApp::SetVolumeMultiplier(MasterVolume);
    if (auto* Blur=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))) Blur->Set(bMotionBlur ? 3 : 0,ECVF_SetByGameSetting);
}

void UADSettingsSubsystem::ReadGraphicsState()
{
    if (auto* Settings=GEngine ? GEngine->GetGameUserSettings() : nullptr)
    {
        // -1 is Unreal's actual mixed-quality state, not the LOW preset.
        const int32 ActualQuality=Settings->GetOverallScalabilityLevel();
        Quality=ActualQuality>=0 && ActualQuality<UE_ARRAY_COUNT(QualityNames) ? ActualQuality : -1;
        float Normalized=0,Current=0,Minimum=0,Maximum=0;
        Settings->GetResolutionScaleInformationEx(Normalized,Current,Minimum,Maximum);
        ResolutionPercent=FMath::IsFinite(Current) ? Current : 85.f;
        bVSync=Settings->IsVSyncEnabled();
        FrameRateLimit=Settings->GetFrameRateLimit();
        if (!FMath::IsFinite(FrameRateLimit) || FrameRateLimit<0.f) FrameRateLimit=0.f;
        FpsIndex=-1;
        for (int32 I=0;I<UE_ARRAY_COUNT(FrameLimits);++I)
            if (FMath::IsNearlyEqual(FrameRateLimit,static_cast<float>(FrameLimits[I]))) { FpsIndex=I; break; }
    }
}

void UADSettingsSubsystem::SaveSnapshot()
{
    Snapshot={Quality,FpsIndex,ResolutionPercent,FrameRateLimit,MasterVolume,UIScale,bMph,bSpeedFov,bSimulationDamage,bMotionBlur,bVSync,Bindings};
}
void UADSettingsSubsystem::RestoreSnapshot()
{
    Quality=Snapshot.Quality; ResolutionPercent=Snapshot.ResolutionPercent; FpsIndex=Snapshot.FpsIndex;
    FrameRateLimit=Snapshot.FrameRateLimit;
    MasterVolume=Snapshot.MasterVolume; bMph=Snapshot.bMph; bSpeedFov=Snapshot.bSpeedFov;
    bSimulationDamage=Snapshot.bSimulationDamage; bMotionBlur=Snapshot.bMotionBlur;
    UIScale=Snapshot.UIScale; bVSync=Snapshot.bVSync; Bindings=Snapshot.Bindings;
}
void UADSettingsSubsystem::Open()
{
    if (bOpen) return;
    ReadGraphicsState(); SaveSnapshot(); bOpen=true; SelectedRow=0; bBindingsPage=false; CapturingBinding=NAME_None;
    Message=TEXT("Changes take effect when you select APPLY AND SAVE. Unchanged custom graphics values are preserved.");
}
void UADSettingsSubsystem::Close() { if (bOpen) { RestoreSnapshot(); bOpen=false; CapturingBinding=NAME_None; bBindingsPage=false; } }
void UADSettingsSubsystem::Select(int32 Delta) { if (bOpen && !IsCapturingBinding()) SelectedRow=((SelectedRow+Delta)%GetRowCount()+GetRowCount())%GetRowCount(); }
void UADSettingsSubsystem::Adjust(int32 Delta)
{
    if (!bOpen || Delta==0 || IsCapturingBinding()) return;
    if (bBindingsPage)
    {
        if (SelectedRow==0) bGamepadBindings=!bGamepadBindings;
        return;
    }
    const auto Cycle=[Delta](int32 Value,int32 Count) { return ((Value+Delta)%Count+Count)%Count; };
    switch (SelectedRow)
    {
    case 0: Quality=Quality<0 ? (Delta>0 ? 0 : 4) : Cycle(Quality,5); break;
    case 1: ResolutionPercent=FMath::Clamp(ResolutionPercent+Delta*5.f,50.f,100.f); break;
    case 2:
        FpsIndex=FpsIndex<0 ? (Delta>0 ? 0 : UE_ARRAY_COUNT(FrameLimits)-1) : Cycle(FpsIndex,UE_ARRAY_COUNT(FrameLimits));
        FrameRateLimit=static_cast<float>(FrameLimits[FpsIndex]);
        break;
    case 3: bMotionBlur=!bMotionBlur; break;
    case 4: MasterVolume=FMath::Clamp(MasterVolume+Delta*.1f,0.f,1.f); break;
    case 5: bMph=!bMph; break;
    case 6: bSpeedFov=!bSpeedFov; break;
    case 7: bSimulationDamage=!bSimulationDamage; break;
    case 8: UIScale=FMath::Clamp(UIScale+Delta*.05f,.7f,1.f); break;
    case 9: bVSync=!bVSync; break;
    case 10: bBindingsPage=true; SelectedRow=0; break;
    case 11: Apply(); break;
    default: break;
    }
}

void UADSettingsSubsystem::Apply()
{
    if (!bOpen) return;
    auto* Settings=GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (!Settings) { Message=TEXT("Graphics settings are unavailable."); return; }
    const bool bPresetChanged=Quality>=0 && Quality!=Snapshot.Quality;
    const bool bScaleChanged=ResolutionPercent!=Snapshot.ResolutionPercent;
    const bool bFrameLimitChanged=FrameRateLimit!=Snapshot.FrameRateLimit;
    const bool bVSyncChanged=bVSync!=Snapshot.bVSync;
    const bool bBindingsChanged=!Bindings.Equals(Snapshot.Bindings);
    if (bPresetChanged) Settings->SetOverallScalabilityLevel(Quality);
    // A preset also changes engine resolution quality; keep the separately
    // displayed scale unless the user explicitly adjusts that row.
    if (bScaleChanged || bPresetChanged) Settings->SetResolutionScaleValueEx(ResolutionPercent);
    if (bFrameLimitChanged) Settings->SetFrameRateLimit(FrameRateLimit);
    if (bVSyncChanged) Settings->SetVSyncEnabled(bVSync);
    if (bPresetChanged || bScaleChanged || bFrameLimitChanged || bVSyncChanged) Settings->ApplyNonResolutionSettings();
    FApp::SetVolumeMultiplier(MasterVolume);
    if (auto* Blur=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))) Blur->Set(bMotionBlur ? 3 : 0,ECVF_SetByGameSetting);
    ReadGraphicsState();
    if (bBindingsChanged) ++BindingRevision;
    SaveSnapshot();
    if (!bPersistenceAllowed || GIsAutomationTesting || !GConfig || GGameUserSettingsIni.IsEmpty())
    { Message=TEXT("Settings applied for this session. Config saving is unavailable in this run."); return; }
    GConfig->SetFloat(Section,TEXT("MasterVolume"),MasterVolume,GGameUserSettingsIni);
    GConfig->SetFloat(Section,TEXT("UIScale"),UIScale,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("UseMph"),bMph,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("SpeedFov"),bSpeedFov,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("SimulationDamage"),bSimulationDamage,GGameUserSettingsIni);
    GConfig->SetBool(Section,TEXT("MotionBlur"),bMotionBlur,GGameUserSettingsIni);
    for (const auto& Slot:FADInputBindings::GetSlots())
        GConfig->SetString(InputSection,*Slot.Id.ToString(),*Bindings.Get(Slot.Id).GetFName().ToString(),GGameUserSettingsIni);
    Settings->SaveSettings();
    bool bFlushed=GConfig->Flush(false,GGameUserSettingsIni);
    if (GIsEditor && GEditorSettingsIni!=GGameUserSettingsIni)
        bFlushed=GConfig->Flush(false,GEditorSettingsIni) && bFlushed;
    // SaveSettings has no return value. Read the files independently of GConfig
    // before telling the player persistence succeeded (read-only disks can fail).
    Message=bFlushed && VerifySavedSettings() ? TEXT("Settings applied and saved.")
        : TEXT("Settings applied for this session, but saving could not be verified. Check Saved/Config and try again.");
}

bool UADSettingsSubsystem::VerifySavedSettings() const
{
    const auto* Settings=GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (!Settings || IFileManager::Get().FileSize(*GGameUserSettingsIni)<=0) return false;
    FConfigFile File;
    File.Read(GGameUserSettingsIni);
    const auto FloatMatches=[&File](const TCHAR* Name,float Expected)
    { float Value=0.f; return File.GetFloat(Section,Name,Value) && FMath::IsNearlyEqual(Value,Expected,.001f); };
    const auto BoolMatches=[&File](const TCHAR* Name,bool Expected)
    { bool Value=false; return File.GetBool(Section,Name,Value) && Value==Expected; };
    float SavedFrameLimit=0.f;
    if (!FloatMatches(TEXT("MasterVolume"),MasterVolume) || !FloatMatches(TEXT("UIScale"),UIScale)
        || !BoolMatches(TEXT("UseMph"),bMph) || !BoolMatches(TEXT("SpeedFov"),bSpeedFov)
        || !BoolMatches(TEXT("SimulationDamage"),bSimulationDamage) || !BoolMatches(TEXT("MotionBlur"),bMotionBlur)
        || !File.GetFloat(*Settings->GetClass()->GetPathName(),TEXT("FrameRateLimit"),SavedFrameLimit)
        || !FMath::IsNearlyEqual(SavedFrameLimit,FrameRateLimit,.001f)) return false;
    bool SavedVSync=false;
    if (!File.GetBool(*Settings->GetClass()->GetPathName(),TEXT("bUseVSync"),SavedVSync) || SavedVSync!=bVSync) return false;
    for (const auto& Slot:FADInputBindings::GetSlots())
    {
        FString KeyName;
        if (!File.GetString(InputSection,*Slot.Id.ToString(),KeyName) || FKey(FName(*KeyName))!=Bindings.Get(Slot.Id)) return false;
    }
    const FString& ScalabilityPath=GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni;
    if (IFileManager::Get().FileSize(*ScalabilityPath)<=0) return false;
    FConfigFile ScalabilityFile;
    ScalabilityFile.Read(ScalabilityPath);
    const auto Actual=Scalability::GetQualityLevels();
    float SavedScale=0.f;
    if (!ScalabilityFile.GetFloat(TEXT("ScalabilityGroups"),TEXT("sg.ResolutionQuality"),SavedScale)
        || !FMath::IsNearlyEqual(SavedScale,Actual.ResolutionQuality,.001f)) return false;
    const struct FQualityValue { const TCHAR* Name; int32 Value; } Values[]={
        {TEXT("sg.ViewDistanceQuality"),Actual.ViewDistanceQuality},{TEXT("sg.AntiAliasingQuality"),Actual.AntiAliasingQuality},
        {TEXT("sg.ShadowQuality"),Actual.ShadowQuality},{TEXT("sg.GlobalIlluminationQuality"),Actual.GlobalIlluminationQuality},
        {TEXT("sg.ReflectionQuality"),Actual.ReflectionQuality},{TEXT("sg.PostProcessQuality"),Actual.PostProcessQuality},
        {TEXT("sg.TextureQuality"),Actual.TextureQuality},{TEXT("sg.EffectsQuality"),Actual.EffectsQuality},
        {TEXT("sg.FoliageQuality"),Actual.FoliageQuality},{TEXT("sg.ShadingQuality"),Actual.ShadingQuality},
        {TEXT("sg.LandscapeQuality"),Actual.LandscapeQuality}};
    for (const auto& Expected : Values)
    {
        int32 Value=0;
        if (!ScalabilityFile.GetInt(TEXT("ScalabilityGroups"),Expected.Name,Value) || Value!=Expected.Value) return false;
    }
    return true;
}

TArray<const FADBindingSlot*> UADSettingsSubsystem::GetVisibleBindings() const
{
    TArray<const FADBindingSlot*> Result;
    for (const auto& Slot:FADInputBindings::GetSlots()) if (Slot.bGamepad==bGamepadBindings) Result.Add(&Slot);
    return Result;
}
int32 UADSettingsSubsystem::GetRowCount() const { return bBindingsPage ? GetVisibleBindings().Num()+3 : SettingsRowCount; }
int32 UADSettingsSubsystem::GetFirstVisibleRow() const { return FMath::Clamp(SelectedRow-VisibleRows/2,0,FMath::Max(0,GetRowCount()-VisibleRows)); }
FString UADSettingsSubsystem::GetPageTitle() const { return bBindingsPage ? TEXT("DRIVING CONTROLS") : TEXT("SETTINGS"); }
void UADSettingsSubsystem::Confirm()
{
    if (!bOpen || IsCapturingBinding()) return;
    if (!bBindingsPage) { Adjust(1); return; }
    const auto Slots=GetVisibleBindings();
    if (SelectedRow==0) { bGamepadBindings=!bGamepadBindings; return; }
    if (SelectedRow==Slots.Num()+1)
    { Bindings.ResetDevice(bGamepadBindings); Message=TEXT("Default bindings staged. Select APPLY AND SAVE to keep them."); return; }
    if (SelectedRow==Slots.Num()+2) { Back(); return; }
    if (Slots.IsValidIndex(SelectedRow-1))
    {
        CapturingBinding=Slots[SelectedRow-1]->Id;
        Message=Slots[SelectedRow-1]->bAxis ? TEXT("Move the required axis beyond halfway. Esc / B cancels.")
            : TEXT("Press a new key or controller button. Esc / B cancels. Conflicting controls swap.");
    }
}
void UADSettingsSubsystem::Back()
{
    if (IsCapturingBinding()) { CancelCapture(); return; }
    if (bBindingsPage) { bBindingsPage=false; SelectedRow=10; Message=TEXT("Select APPLY AND SAVE to keep all staged changes."); }
}
void UADSettingsSubsystem::CancelCapture()
{
    CapturingBinding=NAME_None;
    Message=TEXT("Binding capture canceled. Staged changes are unchanged.");
}
void UADSettingsSubsystem::CaptureBinding(FKey Key)
{
    if (!IsCapturingBinding()) return;
    if (Bindings.Assign(CapturingBinding,Key,Message)) CapturingBinding=NAME_None;
}
void UADSettingsSubsystem::Click(FVector2D Point)
{
    if (!bOpen || IsCapturingBinding() || Point.X<403 || Point.X>1513 || Point.Y<245 || Point.Y>=245+VisibleRows*72) return;
    const int32 Row=GetFirstVisibleRow()+FMath::FloorToInt((Point.Y-245)/72);
    if (Row>=GetRowCount()) return;
    SelectedRow=Row;
    Confirm();
}
FString UADSettingsSubsystem::GetRowLabel(int32 Index) const
{
    if (bBindingsPage)
    {
        const auto Slots=GetVisibleBindings();
        if (Index==0) return TEXT("INPUT DEVICE");
        if (Slots.IsValidIndex(Index-1)) return Slots[Index-1]->Label;
        return Index==Slots.Num()+1 ? TEXT("RESET THIS DEVICE") : TEXT("BACK TO SETTINGS");
    }
    static const TCHAR* Labels[]={TEXT("GRAPHICS PRESET"),TEXT("RESOLUTION SCALE"),TEXT("FRAME LIMIT"),TEXT("MOTION BLUR"),
        TEXT("MASTER VOLUME"),TEXT("SPEED UNITS"),TEXT("SPEED CAMERA FOV"),TEXT("DAMAGE (OFFLINE)"),TEXT("UI SCALE"),TEXT("VSYNC"),TEXT("DRIVING CONTROLS"),TEXT("APPLY AND SAVE")};
    return Index>=0 && Index<SettingsRowCount ? Labels[Index] : TEXT("");
}
FString UADSettingsSubsystem::GetRowValue(int32 Index) const
{
    if (bBindingsPage)
    {
        const auto Slots=GetVisibleBindings();
        if (Index==0) return bGamepadBindings ? TEXT("CONTROLLER") : TEXT("KEYBOARD");
        if (Slots.IsValidIndex(Index-1)) return CapturingBinding==Slots[Index-1]->Id ? TEXT("LISTENING...")
            : Bindings.Get(Slots[Index-1]->Id).GetDisplayName().ToString();
        return TEXT("ENTER / A");
    }
    switch (Index)
    {
    case 0: return Quality>=0 && Quality<UE_ARRAY_COUNT(QualityNames) ? QualityNames[Quality] : TEXT("CUSTOM");
    case 1: return FString::Printf(TEXT("%s%%"),*FString::SanitizeFloat(ResolutionPercent,0));
    case 2:
        if (FrameRateLimit==0.f) return TEXT("UNCAPPED");
        return FpsIndex<0 ? FString::Printf(TEXT("CUSTOM (%s)"),*FString::SanitizeFloat(FrameRateLimit,0))
            : FString::SanitizeFloat(FrameRateLimit,0);
    case 3: return bMotionBlur ? TEXT("ON") : TEXT("OFF");
    case 4: return FString::Printf(TEXT("%.0f%%"),MasterVolume*100);
    case 5: return bMph ? TEXT("MPH") : TEXT("KM/H");
    case 6: return bSpeedFov ? TEXT("ON") : TEXT("OFF");
    case 7: return bSimulationDamage ? TEXT("SIMULATION") : TEXT("COSMETIC");
    case 8: return FString::Printf(TEXT("%.0f%%"),UIScale*100);
    case 9: return bVSync ? TEXT("ON") : TEXT("OFF");
    default: return TEXT("ENTER / A");
    }
}
