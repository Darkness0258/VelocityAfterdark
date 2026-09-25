#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "ADPlayerController.generated.h"

class AADVehiclePawn;
class UInputAction;
class UInputMappingContext;
class AADRaceManager;
class UADGarageSessionComponent;
class UADCinematicComponent;
class UADMapComponent;
class AADAtmosphere;
class AADRegionalWorld;
enum class EADRaceState : uint8;

/** Local session and Enhanced Input ownership, independent from physical simulation. */
UCLASS()
class VELOCITYAFTERDARK_API AADPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AADPlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    UFUNCTION(Exec) void ADHost();
    UFUNCTION(Exec) void ADJoin(const FString& Address);
    UFUNCTION(Exec) void ADDisconnect();
    virtual void FlushPressedKeys() override;
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override { return true; }
    void BeginDriving();
    void StartDriving() { BeginDriving(); }
    void TogglePause();
    bool IsSessionStarted() const { return bSessionStarted; }
    bool IsGamePaused() const { return bSessionPaused; }
    bool ShouldShowDiagnostics() const { return bShowDiagnostics; }
    AADVehiclePawn* GetVehiclePawn() const;
    AADRaceManager* GetRaceManager() const { return RaceManager; }
    int32 GetSelectedDifficulty() const { return SelectedDifficulty; }
    UADGarageSessionComponent* GetGarageSession() const { return GarageSession; }
    UADCinematicComponent* GetCinematic() const { return Cinematic; }
    UADMapComponent* GetMap() const { return MapComponent; }
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
    void SimulateDeviceDisconnectForAutomation()
    {
        DeviceConnectionChanged(EInputDeviceConnectionState::Disconnected,FPlatformUserId{},FInputDeviceId{});
    }
#endif

private:
    void ApplyControls();
    void RebuildDrivingBindings();
    int32 AppliedBindingRevision=INDEX_NONE;
    TSet<FKey> CapturedKeysAwaitingRelease;
    void ClearControls();
    void ThrottleInput(const FInputActionValue& Value);
    void BrakeInput(const FInputActionValue& Value);
    void SteeringInput(const FInputActionValue& Value);
    void HandbrakeInput(const FInputActionValue& Value);
    void NitrousInput(const FInputActionValue& Value);
    void SettingsInput();
    void MapInput();
    void PhotoInput();
    void ReplayInput();
    void CaptureInput();
    void HideInput();
    bool AreSettingsOpen() const;
    bool bSettingsPausedWorld=false;
    bool bSettingsPreviousCursor=false;
    void CameraInput();
    void RecoverInput();
    void TransmissionInput();
    void ShiftUpInput();
    void ShiftDownInput();
    void ReverseInput();
    void DrivingAction(uint8 Action);
    void ConfirmInput();
    void RaceInput();
    void CareerInput();
    void LeaveRaceInput();
    void DifficultyInput();
    void RaceStateChanged(EADRaceState NewState);
    void PointerInput();
    void GarageInput();
    void PursuitInput();
    void GarageUp();
    void GarageDown();
    void GaragePrevious();
    void GarageNext();
    void GarageZoomIn();
    void GarageZoomOut();
    void DiagnosticsInput() { bShowDiagnostics = !bShowDiagnostics; }
    void DeviceConnectionChanged(EInputDeviceConnectionState State, FPlatformUserId User, FInputDeviceId Device);
    UInputAction* AddAction(const TCHAR* Name, EInputActionValueType Type, bool bWhenPaused = false);

    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> DrivingMappings;
    UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
    UPROPERTY(Transient) TObjectPtr<AADRaceManager> RaceManager;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADGarageSessionComponent> GarageSession;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADCinematicComponent> Cinematic;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UADMapComponent> MapComponent;
    int32 SelectedDifficulty = 1;
    FDelegateHandle DeviceConnectionHandle;
    float Throttle = 0;
    float Brake = 0;
    float Steering = 0;
    bool bHandbrake = false;
    bool bSessionStarted = false;
    bool bSessionPaused = false;
    bool bShowDiagnostics = false;
    float NetworkInputElapsed=0.f;
    float NetworkProbeCountdown=0.f;
    float NetworkProbeDriveRemaining=0.f;
    float NetworkProbeObserveRemaining=0.f;
    bool bNetworkProbeDriveFinished=false;
    UPROPERTY(Transient) TObjectPtr<AADAtmosphere> ClientEnvironment;
    UPROPERTY(Transient) TObjectPtr<AADRegionalWorld> ClientRegions;
    TWeakObjectPtr<AADVehiclePawn> ClientBoundVehicle;
};
