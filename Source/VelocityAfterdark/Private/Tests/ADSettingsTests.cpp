#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Settings/ADInputBindings.h"
#include "Settings/ADSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADInputBindingsTest,"Afterdark.Input.Rebinding",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADInputBindingsTest::RunTest(const FString&)
{
    FADInputBindings Profile;
    FString Message;
    TestTrue(TEXT("Keyboard reverse defaults to V"),Profile.Get(TEXT("Keyboard.Reverse"))==EKeys::V);
    TestTrue(TEXT("Controller reverse defaults to D-pad down"),Profile.Get(TEXT("Gamepad.Reverse"))==EKeys::Gamepad_DPad_Down);
    TestTrue(TEXT("Reverse key can be remapped"),Profile.Assign(TEXT("Keyboard.Reverse"),EKeys::X,Message));
    TestTrue(TEXT("Reverse key remap is retained"),Profile.Get(TEXT("Keyboard.Reverse"))==EKeys::X);
    Profile.ResetDevice(false);
    TestTrue(TEXT("Keyboard conflict swaps throttle and brake"),Profile.Assign(TEXT("Keyboard.Throttle"),EKeys::S,Message));
    TestTrue(TEXT("Throttle is S"),Profile.Get(TEXT("Keyboard.Throttle"))==EKeys::S);
    TestTrue(TEXT("Brake inherits W instead of becoming unbound"),Profile.Get(TEXT("Keyboard.Brake"))==EKeys::W);
    TestTrue(TEXT("Controller triggers can swap"),Profile.Assign(TEXT("Gamepad.Throttle"),EKeys::Gamepad_LeftTriggerAxis,Message));
    TestTrue(TEXT("Brake gets the opposite trigger"),Profile.Get(TEXT("Gamepad.Brake"))==EKeys::Gamepad_RightTriggerAxis);
    TestFalse(TEXT("Trigger cannot become a signed steering axis"),Profile.Assign(TEXT("Gamepad.Throttle"),EKeys::Gamepad_LeftX,Message));
    TestFalse(TEXT("Gamepad slots reject keyboard input"),Profile.Assign(TEXT("Gamepad.Handbrake"),EKeys::K,Message));
    TestFalse(TEXT("Menu escape cannot be stolen"),Profile.Assign(TEXT("Keyboard.Throttle"),EKeys::Escape,Message));
    TestFalse(TEXT("Reserved garage shortcut cannot be stolen"),Profile.Assign(TEXT("Keyboard.Throttle"),EKeys::G,Message));
    TestFalse(TEXT("Unknown slots are rejected"),Profile.Assign(TEXT("Unknown"),EKeys::T,Message));
    TestTrue(TEXT("Controller button conflicts swap"),Profile.Assign(TEXT("Gamepad.Handbrake"),EKeys::Gamepad_FaceButton_Bottom,Message));
    TestTrue(TEXT("Nitrous moves to former handbrake button"),Profile.Get(TEXT("Gamepad.Nitrous"))==EKeys::Gamepad_FaceButton_Left);
    Profile.ResetDevice(false);
    TestTrue(TEXT("Reset keyboard restores W"),Profile.Get(TEXT("Keyboard.Throttle"))==EKeys::W);
    TestTrue(TEXT("Reset keyboard preserves pad trigger swap"),Profile.Get(TEXT("Gamepad.Throttle"))==EKeys::Gamepad_LeftTriggerAxis);
    TMap<FName,FKey> Serialized;
    for (const auto& Slot:FADInputBindings::GetSlots()) Serialized.Add(Slot.Id,Profile.Get(Slot.Id));
    FADInputBindings Loaded;
    TestTrue(TEXT("Saved complete profile restores"),Loaded.Load(Serialized,Message));
    TestTrue(TEXT("All restored slots match"),Loaded.Equals(Profile));
    Serialized.Add(TEXT("Keyboard.Throttle"),EKeys::S);
    TestFalse(TEXT("Conflicting saved profile fails atomically"),Loaded.Load(Serialized,Message));
    TestTrue(TEXT("Previous profile survives corrupt load"),Loaded.Equals(Profile));
    Serialized.Add(TEXT("Keyboard.Throttle"),EKeys::Escape);
    TestFalse(TEXT("Reserved saved binding rejected"),Loaded.Load(Serialized,Message));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADSettingsDraftTest,"Afterdark.Settings.DraftApplyCancel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADSettingsDraftTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("Engine settings exist"),GEngine ? GEngine->GetGameUserSettings() : nullptr)) return false;
    auto* Instance=NewObject<UGameInstance>();
    auto* Settings=NewObject<UADSettingsSubsystem>(Instance);
    Settings->Open();
    Settings->Select(8);
    Settings->Adjust(-1);
    TestEqual(TEXT("UI draft does not alter live layout"),Settings->GetUIScale(),1.f);
    Settings->Close();
    TestEqual(TEXT("Cancel restores scale"),Settings->GetUIScale(),1.f);
    Settings->Open();
    Settings->Select(10); Settings->Confirm();
    TestTrue(TEXT("Driving controls page opens"),Settings->IsBindingsPage());
    Settings->Select(1); Settings->Confirm();
    TestTrue(TEXT("Throttle row starts capture"),Settings->IsCapturingBinding());
    Settings->CaptureBinding(EKeys::Escape);
    TestTrue(TEXT("Rejected key keeps capture active"),Settings->IsCapturingBinding());
    Settings->CaptureBinding(EKeys::T);
    TestFalse(TEXT("Valid key completes capture"),Settings->IsCapturingBinding());
    TestTrue(TEXT("Draft binding is not yet live"),Settings->GetBindings().Get(TEXT("Keyboard.Throttle"))==EKeys::W);
    Settings->Close();
    TestTrue(TEXT("Closing discards staged binding"),Settings->GetBindings().Get(TEXT("Keyboard.Throttle"))==EKeys::W);
    Settings->Open(); Settings->Select(10); Settings->Confirm();
    Settings->Select(1); Settings->Confirm(); Settings->CaptureBinding(EKeys::T);
    Settings->Back(); Settings->Select(1); Settings->Confirm();
    TestTrue(TEXT("Apply activates binding while menu remains open"),Settings->GetBindings().Get(TEXT("Keyboard.Throttle"))==EKeys::T);
    TestEqual(TEXT("Applied profile requests one controller rebuild"),Settings->GetBindingRevision(),1);
    Settings->Close();
    TestTrue(TEXT("Applied binding survives close"),Settings->GetBindings().Get(TEXT("Keyboard.Throttle"))==EKeys::T);
    Settings->Open(); Settings->Select(8); Settings->Adjust(-1); Settings->Apply();
    TestTrue(TEXT("Apply activates actual UI scale"),FMath::IsNearlyEqual(Settings->GetUIScale(),.95f));
    Settings->Close();
    // Unattended automation must never report player config writes as successful.
    TestTrue(TEXT("Automation apply reports session-only persistence"),Settings->GetMessage().Contains(TEXT("session")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FADCanvasScaleTest,"Afterdark.Settings.CanvasScale",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FADCanvasScaleTest::RunTest(const FString&)
{
    const FVector2D Resolutions[]={{1280,720},{1920,1080},{3440,1440},{1080,1920},{800,600}};
    for (const auto& Resolution:Resolutions)
        for (float Scale:{.7f,.85f,1.f,1.5f})
        {
            const auto Transform=FADCanvasTransform::Fit(Resolution.X,Resolution.Y,Scale);
            const FVector2D TopLeft=Transform.ToScreen({0,0}),BottomRight=Transform.ToScreen({1920,1080});
            TestTrue(TEXT("Scaled layout stays within viewport"),TopLeft.X>=-.01 && TopLeft.Y>=-.01
                && BottomRight.X<=Resolution.X+.01 && BottomRight.Y<=Resolution.Y+.01);
            const FVector2D Target(521,719);
            TestTrue(TEXT("Draw and hit testing agree"),Transform.ToCanvas(Transform.ToScreen(Target)).Equals(Target,.01));
        }
    return true;
}
#endif
