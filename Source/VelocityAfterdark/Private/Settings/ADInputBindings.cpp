#include "Settings/ADInputBindings.h"

const TArray<FADBindingSlot>& FADInputBindings::GetSlots()
{
    static const TArray<FADBindingSlot> Slots={
        {TEXT("Keyboard.Throttle"),TEXT("Throttle"),TEXT("THROTTLE"),EKeys::W},
        {TEXT("Keyboard.Brake"),TEXT("Brake"),TEXT("BRAKE"),EKeys::S},
        {TEXT("Keyboard.SteerLeft"),TEXT("Steering"),TEXT("STEER LEFT"),EKeys::A,false,false,true},
        {TEXT("Keyboard.SteerRight"),TEXT("Steering"),TEXT("STEER RIGHT"),EKeys::D},
        {TEXT("Keyboard.Handbrake"),TEXT("Handbrake"),TEXT("HANDBRAKE"),EKeys::SpaceBar},
        {TEXT("Keyboard.Reverse"),TEXT("Reverse"),TEXT("REVERSE"),EKeys::V},
        {TEXT("Keyboard.Nitrous"),TEXT("Nitrous"),TEXT("NITROUS"),EKeys::LeftShift},
        {TEXT("Keyboard.Camera"),TEXT("Camera"),TEXT("CAMERA"),EKeys::C},
        {TEXT("Keyboard.Recover"),TEXT("Recover"),TEXT("RECOVER VEHICLE"),EKeys::R},
        {TEXT("Gamepad.Throttle"),TEXT("Throttle"),TEXT("THROTTLE AXIS"),EKeys::Gamepad_RightTriggerAxis,true,true},
        {TEXT("Gamepad.Brake"),TEXT("Brake"),TEXT("BRAKE AXIS"),EKeys::Gamepad_LeftTriggerAxis,true,true},
        {TEXT("Gamepad.Steering"),TEXT("Steering"),TEXT("STEERING AXIS"),EKeys::Gamepad_LeftX,true,true},
        {TEXT("Gamepad.Handbrake"),TEXT("Handbrake"),TEXT("HANDBRAKE"),EKeys::Gamepad_FaceButton_Left,true},
        {TEXT("Gamepad.Reverse"),TEXT("Reverse"),TEXT("REVERSE"),EKeys::Gamepad_DPad_Down,true},
        {TEXT("Gamepad.Nitrous"),TEXT("Nitrous"),TEXT("NITROUS"),EKeys::Gamepad_FaceButton_Bottom,true},
        {TEXT("Gamepad.Camera"),TEXT("Camera"),TEXT("CAMERA"),EKeys::Gamepad_FaceButton_Top,true},
        {TEXT("Gamepad.Recover"),TEXT("Recover"),TEXT("RECOVER VEHICLE"),EKeys::Gamepad_Special_Left,true}
    };
    return Slots;
}

FKey FADInputBindings::Get(FName Id) const
{
    if (const FKey* Override=Overrides.Find(Id)) return *Override;
    for (const auto& Slot:GetSlots()) if (Slot.Id==Id) return Slot.DefaultKey;
    return FKey();
}

bool FADInputBindings::IsAllowed(const FADBindingSlot& Slot,FKey Key)
{
    if (!Key.IsValid() || Key.IsGamepadKey()!=Slot.bGamepad) return false;
    if (Slot.bGamepad)
    {
        if (Slot.Action==TEXT("Steering")) return Key==EKeys::Gamepad_LeftX || Key==EKeys::Gamepad_RightX;
        if (Slot.bAxis) return Key==EKeys::Gamepad_LeftTriggerAxis || Key==EKeys::Gamepad_RightTriggerAxis;
        return Key==EKeys::Gamepad_FaceButton_Left || Key==EKeys::Gamepad_FaceButton_Bottom
            || Key==EKeys::Gamepad_FaceButton_Top || Key==EKeys::Gamepad_Special_Left
            || Key==EKeys::Gamepad_DPad_Down;
    }
    // Preserve contextual race, garage, photo, map, transmission and menu actions.
    static const TArray<FKey> Reserved={EKeys::Escape,EKeys::Enter,EKeys::BackSpace,EKeys::Tab,
        EKeys::Up,EKeys::Down,EKeys::Left,EKeys::Right,EKeys::F,EKeys::G,EKeys::N,EKeys::P,EKeys::H,
        EKeys::M,EKeys::Q,EKeys::E,EKeys::Tilde,EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,
        EKeys::F5,EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10,EKeys::F11,EKeys::F12};
    return !Key.IsMouseButton() && !Key.IsAxis1D() && !Key.IsAxis2D() && !Key.IsAxis3D()
        && Key!=EKeys::AnyKey && !Reserved.Contains(Key);
}

bool FADInputBindings::Assign(FName Id,FKey Key,FString& Message)
{
    const auto& Slots=GetSlots();
    const auto* Target=Slots.FindByPredicate([Id](const auto& Slot) { return Slot.Id==Id; });
    if (!Target || !IsAllowed(*Target,Key))
    { Message=TEXT("That key is reserved or incompatible. Choose another input, or Esc / B to cancel."); return false; }
    const FKey Previous=Get(Id);
    const auto* Conflict=Slots.FindByPredicate([&](const auto& Slot) { return Slot.Id!=Id && Get(Slot.Id)==Key; });
    if (Conflict)
    {
        if (!IsAllowed(*Conflict,Previous))
        { Message=TEXT("That input is already bound to an incompatible control."); return false; }
        Overrides.Add(Conflict->Id,Previous);
        Message=FString::Printf(TEXT("Swapped with %s. Select APPLY AND SAVE to keep your bindings."),*Conflict->Label);
    }
    else Message=TEXT("Binding changed. Select APPLY AND SAVE to keep your bindings.");
    Overrides.Add(Id,Key);
    return true;
}

bool FADInputBindings::Load(const TMap<FName,FKey>& Values,FString& Error)
{
    FADInputBindings Candidate;
    for (const auto& Pair:Values)
    {
        const auto* Slot=GetSlots().FindByPredicate([&](const auto& Entry) { return Entry.Id==Pair.Key; });
        if (!Slot || !IsAllowed(*Slot,Pair.Value)) { Error=TEXT("Saved input profile contains an invalid binding."); return false; }
        Candidate.Overrides.Add(Pair.Key,Pair.Value);
    }
    TSet<FKey> Used;
    for (const auto& Slot:GetSlots())
    {
        const FKey Key=Candidate.Get(Slot.Id);
        if (Used.Contains(Key)) { Error=TEXT("Saved input profile contains conflicting bindings."); return false; }
        Used.Add(Key);
    }
    *this=MoveTemp(Candidate);
    Error.Reset();
    return true;
}

void FADInputBindings::ResetDevice(bool bGamepad)
{
    for (const auto& Slot:GetSlots()) if (Slot.bGamepad==bGamepad) Overrides.Remove(Slot.Id);
}
bool FADInputBindings::Equals(const FADInputBindings& Other) const
{
    for (const auto& Slot:GetSlots()) if (Get(Slot.Id)!=Other.Get(Slot.Id)) return false;
    return true;
}
FADCanvasTransform FADCanvasTransform::Fit(float Width,float Height,float UserScale)
{
    FADCanvasTransform Result;
    const float Preference=FMath::IsFinite(UserScale) ? FMath::Clamp(UserScale,.7f,1.f) : 1.f;
    Result.Scale=FMath::Max(.01f,FMath::Min(Width/1920.f,Height/1080.f)*Preference);
    Result.Offset=FVector2D((Width-1920.f*Result.Scale)*.5f,(Height-1080.f*Result.Scale)*.5f);
    return Result;
}
