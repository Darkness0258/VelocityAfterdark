#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "Core/ADGameMode.h"
#include "Racing/ADRaceManager.h"
#include "Engine/World.h"
#include "Garage/ADGarageSessionComponent.h"
#include "World/ADPoliceDirector.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Settings/ADSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Presentation/ADCinematicComponent.h"
#include "Online/ADOnlineSubsystem.h"
#include "World/ADDistrict.h"
#include "World/ADAtmosphere.h"
#include "World/ADRegionalWorld.h"
#include "Core/ADGameState.h"
#include "Presentation/ADMapComponent.h"
#include "InputKeyEventArgs.h"
#include "Settings/ADInputBindings.h"
#include "Misc/Parse.h"

AADPlayerController::AADPlayerController()
{
    PrimaryActorTick.bTickEvenWhenPaused = true;
    bShowMouseCursor = true;
    GarageSession = CreateDefaultSubobject<UADGarageSessionComponent>(TEXT("GarageSession"));
    Cinematic=CreateDefaultSubobject<UADCinematicComponent>(TEXT("Cinematic"));
    MapComponent=CreateDefaultSubobject<UADMapComponent>(TEXT("Map"));
}

void AADPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    // Procedural collision/scenery is deterministic local content. Only the
    // server spawns replicated vehicles and simulates authoritative driving.
    if (GetNetMode()==NM_Client && IsLocalController())
    {
        GetWorld()->SpawnActor<AADDistrict>();
        ClientEnvironment=GetWorld()->SpawnActor<AADAtmosphere>();
        ClientRegions=GetWorld()->SpawnActor<AADRegionalWorld>();
        if (ClientRegions) ClientRegions->BindAtmosphere(ClientEnvironment);
    }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    DeviceConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange()
        .AddUObject(this, &AADPlayerController::DeviceConnectionChanged);
    if (auto* GameMode=GetWorld()->GetAuthGameMode<AADGameMode>())
    {
        RaceManager=GameMode->GetRaceManager();
        if (RaceManager) RaceManager->OnStateChanged.AddUObject(this,&AADPlayerController::RaceStateChanged);
    }
    if (GetNetMode()==NM_Client && FParse::Param(FCommandLine::Get(),TEXT("AfterdarkNetDriveProbe")))
        NetworkProbeCountdown=2.5f;
}

AADVehiclePawn* AADPlayerController::GetVehiclePawn() const { return Cast<AADVehiclePawn>(GetPawn()); }

void AADPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (IsLocalController())
    {
        const auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
        if (Settings && AppliedBindingRevision!=Settings->GetBindingRevision()) RebuildDrivingBindings();
    }
    if (GetNetMode()!=NM_Client || !IsLocalController()) return;
    if (NetworkProbeCountdown>0.f)
    {
        NetworkProbeCountdown=FMath::Max(0.f,NetworkProbeCountdown-DeltaTime);
        if (NetworkProbeCountdown==0.f)
        {
            BeginDriving();
            if (bSessionStarted)
            {
                Throttle=.62f; Brake=0.f; Steering=0.f;
                NetworkProbeDriveRemaining=4.f;
                UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_DRIVE_STARTED: client is sending normal throttle input."));
            }
            else
            {
                UE_LOG(LogTemp,Error,TEXT("AFTERDARK_NET_DRIVE_START_FAILED: player vehicle is unavailable."));
                bNetworkProbeDriveFinished=true;
            }
        }
    }
    if (NetworkProbeDriveRemaining>0.f)
    {
        NetworkProbeDriveRemaining=FMath::Max(0.f,NetworkProbeDriveRemaining-DeltaTime);
        if (NetworkProbeDriveRemaining==0.f)
        {
            Throttle=0.f; Brake=1.f;
            NetworkProbeObserveRemaining=1.f;
            bNetworkProbeDriveFinished=true;
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_DRIVE_RELEASED: client input ended and braking was sent."));
        }
    }
    if (bNetworkProbeDriveFinished && NetworkProbeObserveRemaining>0.f)
    {
        NetworkProbeObserveRemaining=FMath::Max(0.f,NetworkProbeObserveRemaining-DeltaTime);
        if (NetworkProbeObserveRemaining==0.f)
        {
            const AADVehiclePawn* ProbeCar=GetVehiclePawn();
            const float Speed=ProbeCar ? ProbeCar->GetPhysics()->GetTelemetry().SpeedKmh : 0.f;
            if (FMath::IsFinite(Speed) && Speed>10.f)
            {
                UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_TELEMETRY_CONFIRMED: replicated speed %.1f km/h."),Speed);
            }
            else
            {
                UE_LOG(LogTemp,Error,TEXT("AFTERDARK_NET_TELEMETRY_FAILED: replicated speed remained %.1f km/h."),Speed);
            }
        }
    }
    auto* LocalCar=GetVehiclePawn();
    if (LocalCar && ClientBoundVehicle.Get()!=LocalCar)
    {
        ClientBoundVehicle=LocalCar;
        if (ClientEnvironment) ClientEnvironment->BindVehicle(LocalCar);
        if (ClientRegions) ClientRegions->BindVehicle(LocalCar);
    }
    if (const auto* Snapshot=GetWorld()->GetGameState<AADGameState>(); Snapshot && Snapshot->bEnvironmentReady && ClientEnvironment)
        ClientEnvironment->ApplyNetworkSnapshot(Snapshot->Hour,Snapshot->Weather,Snapshot->Wetness);
    NetworkInputElapsed+=DeltaTime;
    if (NetworkInputElapsed<.05f) return;
    NetworkInputElapsed=FMath::Fmod(NetworkInputElapsed,.05f);
    if (auto* Car=GetVehiclePawn())
        Car->ServerDrive(Throttle,Brake,Steering,bHandbrake,Car->GetEffects()->IsNitrousHeld(),bSessionStarted && !bSessionPaused && !AreSettingsOpen());
}
void AADPlayerController::ADHost() { GetGameInstance()->GetSubsystem<UADOnlineSubsystem>()->Host(); }
void AADPlayerController::ADJoin(const FString& Address) { GetGameInstance()->GetSubsystem<UADOnlineSubsystem>()->Join(Address); }
void AADPlayerController::ADDisconnect() { GetGameInstance()->GetSubsystem<UADOnlineSubsystem>()->Disconnect(); }

UInputAction* AADPlayerController::AddAction(const TCHAR* Name, EInputActionValueType Type, bool bWhenPaused)
{
    UInputAction* Action = NewObject<UInputAction>(this, Name);
    Action->ValueType = Type;
    Action->bTriggerWhenPaused = bWhenPaused;
    Actions.Add(Action);
    return Action;
}

void AADPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(InputComponent);
    if (!ensureMsgf(Enhanced, TEXT("Velocity requires EnhancedInputComponent in DefaultInput.ini"))) { return; }
    DrivingMappings = NewObject<UInputMappingContext>(this, TEXT("DrivingMappings"));
    auto Axis = [this, Enhanced](const TCHAR* Name, FKey Keyboard, FKey Gamepad,
        void (AADPlayerController::*Callback)(const FInputActionValue&))
    {
        UInputAction* Action = AddAction(Name,EInputActionValueType::Axis1D);
        DrivingMappings->MapKey(Action, Keyboard);
        FEnhancedActionKeyMapping& Mapping = DrivingMappings->MapKey(Action, Gamepad);
        UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(DrivingMappings);
        DeadZone->LowerThreshold = 0.08f;
        DeadZone->UpperThreshold = 1.0f;
        Mapping.Modifiers.Add(DeadZone);
        Enhanced->BindAction(Action,ETriggerEvent::Triggered,this,Callback);
        Enhanced->BindAction(Action,ETriggerEvent::Completed,this,Callback);
        Enhanced->BindAction(Action,ETriggerEvent::Canceled,this,Callback);
        return Action;
    };
    Axis(TEXT("Throttle"),EKeys::W,EKeys::Gamepad_RightTriggerAxis,&AADPlayerController::ThrottleInput);
    Axis(TEXT("Brake"),EKeys::S,EKeys::Gamepad_LeftTriggerAxis,&AADPlayerController::BrakeInput);
    UInputAction* Steer = Axis(TEXT("Steering"),EKeys::D,EKeys::Gamepad_LeftX,&AADPlayerController::SteeringInput);
    FEnhancedActionKeyMapping& Left = DrivingMappings->MapKey(Steer,EKeys::A);
    Left.Modifiers.Add(NewObject<UInputModifierNegate>(DrivingMappings));
    Steer->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    UInputAction* Handbrake = AddAction(TEXT("Handbrake"),EInputActionValueType::Boolean);
    DrivingMappings->MapKey(Handbrake,EKeys::SpaceBar);
    DrivingMappings->MapKey(Handbrake,EKeys::Gamepad_FaceButton_Left);
    Enhanced->BindAction(Handbrake,ETriggerEvent::Triggered,this,&AADPlayerController::HandbrakeInput);
    Enhanced->BindAction(Handbrake,ETriggerEvent::Completed,this,&AADPlayerController::HandbrakeInput);
    Enhanced->BindAction(Handbrake,ETriggerEvent::Canceled,this,&AADPlayerController::HandbrakeInput);
    UInputAction* Nitrous=AddAction(TEXT("Nitrous"),EInputActionValueType::Boolean);
    DrivingMappings->MapKey(Nitrous,EKeys::LeftShift);
    DrivingMappings->MapKey(Nitrous,EKeys::Gamepad_FaceButton_Bottom);
    Nitrous->bConsumeInput=false;
    Enhanced->BindAction(Nitrous,ETriggerEvent::Triggered,this,&AADPlayerController::NitrousInput);
    Enhanced->BindAction(Nitrous,ETriggerEvent::Completed,this,&AADPlayerController::NitrousInput);
    Enhanced->BindAction(Nitrous,ETriggerEvent::Canceled,this,&AADPlayerController::NitrousInput);
    auto Button = [this, Enhanced](const TCHAR* Name,FKey Key,FKey Pad,void(AADPlayerController::*Callback)(),bool bPaused=false)
    {
        UInputAction* Action = AddAction(Name,EInputActionValueType::Boolean,bPaused);
        DrivingMappings->MapKey(Action,Key);
        if (Pad.IsValid()) { DrivingMappings->MapKey(Action,Pad); }
        Enhanced->BindAction(Action,ETriggerEvent::Started,this,Callback);
        return Action;
    };
    Button(TEXT("Camera"),EKeys::C,EKeys::Gamepad_FaceButton_Top,&AADPlayerController::CameraInput,true);
    Button(TEXT("Photo"),EKeys::F2,FKey(),&AADPlayerController::PhotoInput,true);
    Button(TEXT("Replay"),EKeys::F4,FKey(),&AADPlayerController::ReplayInput,true);
    Button(TEXT("CapturePhoto"),EKeys::F8,FKey(),&AADPlayerController::CaptureInput,true);
    Button(TEXT("HidePhotoHUD"),EKeys::H,FKey(),&AADPlayerController::HideInput,true);
    Button(TEXT("WorldMap"),EKeys::F5,FKey(),&AADPlayerController::MapInput,true);
    Button(TEXT("Recover"),EKeys::R,EKeys::Gamepad_Special_Left,&AADPlayerController::RecoverInput);
    Button(TEXT("Transmission"),EKeys::M,EKeys::Gamepad_DPad_Up,&AADPlayerController::TransmissionInput,true);
    Button(TEXT("ShiftUp"),EKeys::E,EKeys::Gamepad_RightShoulder,&AADPlayerController::ShiftUpInput,true);
    Button(TEXT("ShiftDown"),EKeys::Q,EKeys::Gamepad_LeftShoulder,&AADPlayerController::ShiftDownInput,true);
    Button(TEXT("Reverse"),EKeys::V,EKeys::Gamepad_DPad_Down,&AADPlayerController::ReverseInput,true);
    Button(TEXT("Pause"),EKeys::Escape,EKeys::Gamepad_Special_Right,&AADPlayerController::TogglePause,true);
    UInputAction* Confirm=Button(TEXT("Confirm"),EKeys::Enter,EKeys::Gamepad_FaceButton_Bottom,&AADPlayerController::ConfirmInput,true);
    Confirm->bConsumeInput=false;
    Button(TEXT("Settings"),EKeys::F10,FKey(),&AADPlayerController::SettingsInput,true);
    Button(TEXT("Pointer"),EKeys::LeftMouseButton,FKey(),&AADPlayerController::PointerInput,true);
    Button(TEXT("Diagnostics"),EKeys::F3,FKey(),&AADPlayerController::DiagnosticsInput,true);
    Button(TEXT("Race"),EKeys::F,EKeys::Gamepad_FaceButton_Right,&AADPlayerController::RaceInput);
    Button(TEXT("Career"),EKeys::N,FKey(),&AADPlayerController::CareerInput);
    Button(TEXT("LeaveRace"),EKeys::BackSpace,EKeys::Gamepad_DPad_Left,&AADPlayerController::LeaveRaceInput,true);
    Button(TEXT("RaceDifficulty"),EKeys::Tab,EKeys::Gamepad_DPad_Right,&AADPlayerController::DifficultyInput);
    Button(TEXT("Garage"),EKeys::G,EKeys::Gamepad_LeftThumbstick,&AADPlayerController::GarageInput);
    Button(TEXT("Pursuit"),EKeys::P,EKeys::Gamepad_RightThumbstick,&AADPlayerController::PursuitInput);
    Button(TEXT("GarageUp"),EKeys::Up,FKey(),&AADPlayerController::GarageUp,true);
    Button(TEXT("GarageDown"),EKeys::Down,FKey(),&AADPlayerController::GarageDown,true);
    Button(TEXT("GaragePrevious"),EKeys::Left,FKey(),&AADPlayerController::GaragePrevious,true);
    Button(TEXT("GarageNext"),EKeys::Right,FKey(),&AADPlayerController::GarageNext,true);
    Button(TEXT("GarageZoomIn"),EKeys::MouseScrollUp,EKeys::Gamepad_LeftTrigger,&AADPlayerController::GarageZoomIn);
    Button(TEXT("GarageZoomOut"),EKeys::MouseScrollDown,EKeys::Gamepad_RightTrigger,&AADPlayerController::GarageZoomOut);
    if (ULocalPlayer* Local = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            Subsystem->AddMappingContext(DrivingMappings,0);
        }
    }
}

void AADPlayerController::RebuildDrivingBindings()
{
    auto* Settings=GetGameInstance() ? GetGameInstance()->GetSubsystem<UADSettingsSubsystem>() : nullptr;
    if (!Settings || !DrivingMappings) return;
    FlushPressedKeys();
    TSet<FName> ClearedActions;
    for (const auto& Slot:FADInputBindings::GetSlots())
    {
        const auto* Found=Actions.FindByPredicate([&](const auto& Action) { return Action && Action->GetFName()==Slot.Action; });
        if (!Found) continue;
        UInputAction* Action=Found->Get();
        if (!ClearedActions.Contains(Slot.Action))
        { DrivingMappings->UnmapAllKeysFromAction(Action); ClearedActions.Add(Slot.Action); }
        auto& Mapping=DrivingMappings->MapKey(Action,Settings->GetBindings().Get(Slot.Id));
        if (Slot.bNegate) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(DrivingMappings));
        if (Slot.bAxis)
        {
            auto* DeadZone=NewObject<UInputModifierDeadZone>(DrivingMappings);
            DeadZone->LowerThreshold=.08f;
            DeadZone->UpperThreshold=1.f;
            Mapping.Modifiers.Add(DeadZone);
        }
    }
    if (auto* Local=GetLocalPlayer())
        if (auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            FModifyContextOptions Options;
            Options.bIgnoreAllPressedKeysUntilRelease=true;
            Subsystem->RequestRebuildControlMappings(Options,EInputMappingRebuildType::RebuildWithFlush);
        }
    AppliedBindingRevision=Settings->GetBindingRevision();
}

bool AADPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (CapturedKeysAwaitingRelease.Contains(Params.Key))
    {
        if (Params.Event==IE_Released || (Params.Event==IE_Axis && FMath::Abs(Params.AmountDepressed)<.1f))
            CapturedKeysAwaitingRelease.Remove(Params.Key);
        return true;
    }
    auto* Settings=GetGameInstance() ? GetGameInstance()->GetSubsystem<UADSettingsSubsystem>() : nullptr;
    if (!Settings || !Settings->IsOpen())
    {
        if (Settings && bSessionPaused && Params.Key==EKeys::Gamepad_FaceButton_Top && Params.Event==IE_Pressed)
        { SettingsInput(); return true; }
        return Super::InputKey(Params);
    }
    // Capture occurs before Enhanced Input dispatch. New bindings therefore cannot
    // trigger driving, leave the menu, or confirm another row in the same frame.
    if (Settings->IsCapturingBinding())
    {
        if (Params.Event==IE_Pressed || (Params.Event==IE_Axis && FMath::Abs(Params.AmountDepressed)>.6f))
        {
            CapturedKeysAwaitingRelease.Add(Params.Key);
            if (Params.Key==EKeys::Escape || Params.Key==EKeys::Gamepad_FaceButton_Right) Settings->CancelCapture();
            else Settings->CaptureBinding(Params.Key);
        }
        return true;
    }
    if (Params.Event!=IE_Pressed && Params.Event!=IE_Repeat) return true;
    const FKey Key=Params.Key;
    if (Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up) Settings->Select(-1);
    else if (Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Down) Settings->Select(1);
    else if (Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_LeftShoulder) Settings->Adjust(-1);
    else if (Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Gamepad_RightShoulder) Settings->Adjust(1);
    else if (Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)
    { if (Params.Event==IE_Pressed) Settings->Confirm(); }
    else if (Key==EKeys::LeftMouseButton) PointerInput();
    else if (Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right || Key==EKeys::F10 || Key==EKeys::Gamepad_Special_Right)
    {
        if (Settings->IsBindingsPage() && Key!=EKeys::F10) Settings->Back();
        else SettingsInput();
    }
    return true;
}

void AADPlayerController::ApplyControls()
{
    if (GetNetMode()==NM_Client) return;
    if (AADVehiclePawn* Car = GetVehiclePawn(); Car && bSessionStarted && !bSessionPaused && !MapComponent->IsOpen() && !Cinematic->IsActive() && !AreSettingsOpen() && !GarageSession->IsActive() && Car->IsDrivingEnabled())
    {
        if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector()
            && Mode->GetPoliceDirector()->GetState()==EADPoliceState::Busted)
        { Car->GetPhysics()->SetControls(0.f,1.f,0.f,true); return; }
        if (!RaceManager || RaceManager->AllowsPlayerInput()) Car->GetPhysics()->SetControls(Throttle,Brake,Steering,bHandbrake);
        else Car->GetPhysics()->SetControls(0.f,1.f,0.f,false);
    }
}

void AADPlayerController::ClearControls()
{
    Throttle = Brake = Steering = 0;
    bHandbrake = false;
    if (AADVehiclePawn* Car = GetVehiclePawn()) { Car->GetPhysics()->SetControls(0,1,0,true); Car->GetEffects()->SetNitrousHeld(false); }
}

void AADPlayerController::FlushPressedKeys()
{
    Super::FlushPressedKeys();
    ClearControls();
}

void AADPlayerController::ThrottleInput(const FInputActionValue& Value) { Throttle=FMath::Clamp(Value.Get<float>(),0.0f,1.0f); ApplyControls(); }
void AADPlayerController::BrakeInput(const FInputActionValue& Value) { Brake=FMath::Clamp(Value.Get<float>(),0.0f,1.0f); ApplyControls(); }
void AADPlayerController::SteeringInput(const FInputActionValue& Value) { Steering=FMath::Clamp(Value.Get<float>(),-1.0f,1.0f); ApplyControls(); }
void AADPlayerController::HandbrakeInput(const FInputActionValue& Value) { bHandbrake=Value.Get<bool>(); ApplyControls(); }
void AADPlayerController::NitrousInput(const FInputActionValue& Value)
{
    const auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    const FADInputBindings Defaults;
    const auto& Bindings=Settings ? Settings->GetBindings() : Defaults;
    if (auto* Car=GetVehiclePawn()) Car->GetEffects()->SetNitrousHeld(bSessionStarted && !bSessionPaused && !AreSettingsOpen()
        && !MapComponent->IsOpen() && !Cinematic->IsActive() && !GarageSession->IsActive() && (!RaceManager || RaceManager->AllowsPlayerInput())
        && (Value.Get<bool>() || IsInputKeyDown(Bindings.Get(TEXT("Keyboard.Nitrous"))) || IsInputKeyDown(Bindings.Get(TEXT("Gamepad.Nitrous")))));
}
bool AADPlayerController::AreSettingsOpen() const
{
    const auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    return Settings && Settings->IsOpen();
}
void AADPlayerController::SettingsInput()
{
    auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    if (!Settings || GarageSession->IsActive() || Cinematic->IsActive() || MapComponent->IsOpen()) return;
    FlushPressedKeys();
    if (Settings->IsOpen())
    {
        Settings->Close();
        if (bSettingsPausedWorld) SetPause(false);
        bSettingsPausedWorld=false;
        bShowMouseCursor=bSettingsPreviousCursor;
        if (bShowMouseCursor) SetInputMode(FInputModeGameAndUI());
        else SetInputMode(FInputModeGameOnly());
    }
    else
    {
        bSettingsPreviousCursor=bShowMouseCursor;
        bShowMouseCursor=true;
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        SetInputMode(InputMode);
        bSettingsPausedWorld=GetNetMode()==NM_Standalone && !IsPaused() && SetPause(true);
        Settings->Open();
    }
}
void AADPlayerController::MapInput() { MapComponent->Toggle(); }
void AADPlayerController::CameraInput() { if (bSessionPaused) { SettingsInput(); return; } if (MapComponent->IsOpen() || AreSettingsOpen()) return; if (Cinematic->IsActive()) { Cinematic->CycleCamera(); return; } if (AADVehiclePawn* Car=GetVehiclePawn(); Car && bSessionStarted && !GarageSession->IsActive()) { Car->CycleCamera(); } }
void AADPlayerController::PhotoInput() { if (MapComponent->IsOpen() || AreSettingsOpen()) return; if (Cinematic->IsActive()) Cinematic->Leave(); else Cinematic->EnterPhoto(); }
void AADPlayerController::ReplayInput() { if (MapComponent->IsOpen() || AreSettingsOpen()) return; if (Cinematic->IsActive()) Cinematic->Leave(); else Cinematic->EnterReplay(); }
void AADPlayerController::CaptureInput() { Cinematic->Capture(); }
void AADPlayerController::HideInput() { if (Cinematic->IsActive()) Cinematic->ToggleHidden(); }
void AADPlayerController::RecoverInput()
{
    if (MapComponent->IsOpen()) return;
    if (Cinematic->IsActive()) return;
    if (AreSettingsOpen()) return;
    if (GarageSession->IsActive()) return;
    if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive()) return;
    if (AADVehiclePawn* Car=GetVehiclePawn(); Car && bSessionStarted)
    {
        FlushPressedKeys();
        Cinematic->NotifyRecordingDiscontinuity();
        if (RaceManager && RaceManager->GetState()!=EADRaceState::Idle) RaceManager->RecoverRacer(0);
        else if (GetNetMode()==NM_Client) Car->ServerDrivingAction(4);
        else Car->ResetVehicle();
    }
}

void AADPlayerController::RaceInput()
{
    if (MapComponent->IsOpen()) { MapComponent->Close(); return; }
    if (Cinematic->IsActive()) return;
    if (AreSettingsOpen()) { SettingsInput(); return; }
    if (GarageSession->IsActive()) { GarageSession->Leave(); return; }
    if (IsInputKeyDown(EKeys::Gamepad_LeftShoulder)) { CareerInput(); return; }
    if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive()) return;
    if (!RaceManager || bSessionPaused || !RaceManager->IsReady()) return;
    if (RaceManager->GetState()!=EADRaceState::Idle && RaceManager->GetState()!=EADRaceState::Results) return;
    BeginDriving();
    if (bSessionStarted) { FlushPressedKeys(); RaceManager->StartRace(GetVehiclePawn(),SelectedDifficulty); }
}

void AADPlayerController::LeaveRaceInput()
{
    if (MapComponent->IsOpen())
    {
        if (IsInputKeyDown(EKeys::Gamepad_DPad_Left)) MapComponent->Close();
        else MapComponent->FollowNearest();
        return;
    }
    if (Cinematic->IsActive() || AreSettingsOpen()) return;
    if (IsInputKeyDown(EKeys::Gamepad_DPad_Left) && (!RaceManager || RaceManager->GetState()==EADRaceState::Idle))
    { MapComponent->Toggle(); return; }
    if (GarageSession->IsActive()) { GarageSession->DiscardChanges(); return; }
    if (!RaceManager || bSessionPaused) return;
    FlushPressedKeys();
    RaceManager->LeaveRace();
}

void AADPlayerController::CareerInput()
{
    if (MapComponent->IsOpen()) return;
    if (Cinematic->IsActive()) return;
    if (AreSettingsOpen()) return;
    if (!RaceManager || bSessionPaused || GarageSession->IsActive()) return;
    if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->IsActive()) return;
    if (RaceManager->GetState()!=EADRaceState::Idle && RaceManager->GetState()!=EADRaceState::Results) return;
    BeginDriving();
    if (bSessionStarted) { FlushPressedKeys(); RaceManager->StartCareerRace(GetVehiclePawn()); }
}

void AADPlayerController::DifficultyInput()
{
    if (MapComponent->IsOpen()) { MapComponent->ChangeFilter(1); return; }
    if (Cinematic->IsActive() || AreSettingsOpen()) return;
    if (GarageSession->IsActive()) { GarageDown(); return; }
    if (!RaceManager || bSessionPaused || (RaceManager->GetState()!=EADRaceState::Idle && RaceManager->GetState()!=EADRaceState::Results)) return;
    SelectedDifficulty=(SelectedDifficulty+1)%3;
}

void AADPlayerController::RaceStateChanged(EADRaceState)
{
    FlushPressedKeys();
    Cinematic->NotifyRecordingDiscontinuity();
}
void AADPlayerController::TransmissionInput() { if (MapComponent->IsOpen() || GarageSession->IsActive() || AreSettingsOpen()) { GarageUp(); return; } DrivingAction(0); }
void AADPlayerController::ShiftUpInput() { if (MapComponent->IsOpen() || GarageSession->IsActive() || AreSettingsOpen()) { GarageNext(); return; } DrivingAction(1); }
void AADPlayerController::ShiftDownInput() { if (MapComponent->IsOpen() || GarageSession->IsActive() || AreSettingsOpen()) { GaragePrevious(); return; } DrivingAction(2); }
void AADPlayerController::ReverseInput() { if (MapComponent->IsOpen() || GarageSession->IsActive() || AreSettingsOpen()) { GarageDown(); return; } DrivingAction(3); }
void AADPlayerController::DrivingAction(uint8 Action)
{
    auto* Car=GetVehiclePawn();
    if (!Car || !bSessionStarted || bSessionPaused || Cinematic->IsActive() || MapComponent->IsOpen()) return;
    if (GetNetMode()==NM_Client) { Car->ServerDrivingAction(Action); return; }
    switch (Action)
    {
    case 0: Car->GetPhysics()->ToggleTransmission(); break;
    case 1: Car->GetPhysics()->ShiftUp(); break;
    case 2: Car->GetPhysics()->ShiftDown(); break;
    case 3: Car->GetPhysics()->RequestReverse(); break;
    default: break;
    }
}

void AADPlayerController::GarageInput() { if (MapComponent->IsOpen() || AreSettingsOpen() || Cinematic->IsActive()) return; if (GarageSession->IsActive()) GarageSession->Leave(); else GarageSession->Enter(); }
void AADPlayerController::PursuitInput()
{
    if (MapComponent->IsOpen() || Cinematic->IsActive() || AreSettingsOpen() || GarageSession->IsActive() || bSessionPaused || (RaceManager && RaceManager->GetState()!=EADRaceState::Idle)) return;
    BeginDriving();
    if (auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>(); Mode && Mode->GetPoliceDirector()) Mode->GetPoliceDirector()->StartPursuit();
}
void AADPlayerController::GarageUp() { if (MapComponent->IsOpen()) MapComponent->Select(-1); else if (AreSettingsOpen()) GetGameInstance()->GetSubsystem<UADSettingsSubsystem>()->Select(-1); else GarageSession->MoveSelection(-1); }
void AADPlayerController::GarageDown() { if (MapComponent->IsOpen()) MapComponent->Select(1); else if (AreSettingsOpen()) GetGameInstance()->GetSubsystem<UADSettingsSubsystem>()->Select(1); else GarageSession->MoveSelection(1); }
void AADPlayerController::GaragePrevious() { if (MapComponent->IsOpen()) MapComponent->ChangeFilter(-1); else if (AreSettingsOpen()) GetGameInstance()->GetSubsystem<UADSettingsSubsystem>()->Adjust(-1); else GarageSession->AdjustSelection(-1); }
void AADPlayerController::GarageNext() { if (MapComponent->IsOpen()) MapComponent->ChangeFilter(1); else if (AreSettingsOpen()) GetGameInstance()->GetSubsystem<UADSettingsSubsystem>()->Adjust(1); else GarageSession->AdjustSelection(1); }
void AADPlayerController::GarageZoomIn() { GarageSession->Zoom(-60.f); }
void AADPlayerController::GarageZoomOut() { GarageSession->Zoom(60.f); }

void AADPlayerController::BeginDriving()
{
    if (bSessionStarted || GarageSession->IsActive()) { return; }
    if (AADVehiclePawn* Car = GetVehiclePawn())
    {
        FlushPressedKeys();
        Car->SetDrivingEnabled(true);
        bSessionStarted = Car->IsDrivingEnabled();
        if (bSessionStarted)
        {
            bShowMouseCursor = false;
            SetInputMode(FInputModeGameOnly());
        }
    }
}

void AADPlayerController::TogglePause()
{
    if (MapComponent->IsOpen()) { MapComponent->Close(); return; }
    if (Cinematic->IsActive()) { Cinematic->Leave(); return; }
    if (AreSettingsOpen()) { SettingsInput(); return; }
    if (GarageSession->IsActive()) { GarageSession->Leave(); return; }
    if (!bSessionStarted) { return; }
    const bool bDesired = !bSessionPaused;
    if (GetNetMode()==NM_Standalone && !SetPause(bDesired)) { return; }
    bSessionPaused = bDesired;
    FlushPressedKeys();
    if (AADVehiclePawn* Car=GetVehiclePawn()) { Car->SetDrivingEnabled(!bSessionPaused); }
    bShowMouseCursor = bSessionPaused;
    if (bSessionPaused)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        SetInputMode(Mode);
    }
    else { SetInputMode(FInputModeGameOnly()); }
}

void AADPlayerController::ConfirmInput()
{
    if (MapComponent->IsOpen()) { MapComponent->Confirm(); return; }
    if (Cinematic->IsActive()) { Cinematic->TogglePlayback(); return; }
    if (AreSettingsOpen()) { GetGameInstance()->GetSubsystem<UADSettingsSubsystem>()->Confirm(); return; }
    if (GarageSession->IsActive()) { GarageSession->ConfirmSelection(); return; }
    if (RaceManager && RaceManager->HasPendingCareerReward()) { RaceManager->RetryCareerReward(); return; }
    if (bSessionPaused) { TogglePause(); }
    else if (RaceManager && RaceManager->GetState()==EADRaceState::Results) { RaceInput(); }
    else if (!bSessionStarted) { BeginDriving(); }
}

void AADPlayerController::PointerInput()
{
    if (Cinematic->IsActive()) return;
    // The title's drive/resume panel occupies this normalized screen region.
    float X=0,Y=0;
    int32 Width=0,Height=0;
    GetViewportSize(Width,Height);
    if (!GetMousePosition(X,Y) || Width<=0 || Height<=0) return;
    auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    const auto Transform=FADCanvasTransform::Fit(Width,Height,Settings ? Settings->GetUIScale() : 1.f);
    const FVector2D Point=Transform.ToCanvas(FVector2D(X,Y));
    X=Point.X; Y=Point.Y;
    if (Settings && Settings->IsOpen()) { Settings->Click(Point); return; }
    if (MapComponent->IsOpen()) { MapComponent->Click(FVector2D(X,Y)); return; }
    if (GarageSession->IsActive())
    {
        if (X>=64 && X<=640 && Y>=230 && Y<230+(GarageSession->GetVehicleRow()+1)*62)
        {
            GarageSession->SelectRow(static_cast<int32>((Y-230)/62));
            GarageSession->ConfirmSelection();
        }
        return;
    }
    if (X>=76 && X<=731 && Y>=640 && Y<=966)
    {
        if (!bSessionPaused && Y>=750 && Y<=790) { RaceInput(); return; }
        ConfirmInput();
    }
}

void AADPlayerController::DeviceConnectionChanged(EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId Device)
{
    if (State == EInputDeviceConnectionState::Disconnected)
    {
        FlushPressedKeys();
        // Closing a modal alone would resume a stopped world on disconnect.
        // Restore its state first, then enter the ordinary disconnected pause.
        if (MapComponent->IsOpen()) MapComponent->Close();
        if (Cinematic->IsActive()) Cinematic->Leave();
        if (AreSettingsOpen()) SettingsInput();
        if (bSessionStarted && !bSessionPaused && !GarageSession->IsActive()) { TogglePause(); }
    }
}

void AADPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (RaceManager) RaceManager->OnStateChanged.RemoveAll(this);
    IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(DeviceConnectionHandle);
    if (ULocalPlayer* Local=GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            Subsystem->RemoveMappingContext(DrivingMappings);
        }
    }
    Super::EndPlay(Reason);
}
