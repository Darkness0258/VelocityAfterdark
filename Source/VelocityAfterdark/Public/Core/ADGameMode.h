#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ADGameMode.generated.h"

class AADDistrict;
class APlayerStart;
class AADRaceManager;
class AADAtmosphere;
class AADTrafficManager;
class AADPoliceDirector;
class AADVehiclePawn;
class AADRegionalWorld;
class AADExplorationDirector;
enum class EADRaceState : uint8;

/** Boot ownership remains independent from vehicle, rendering and input. */
UCLASS()
class VELOCITYAFTERDARK_API AADGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AADGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal=TEXT("")) override;
    virtual void Logout(AController* Exiting) override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    bool IsWorldReady() const;
    FString GetStartupError() const;
    AADRaceManager* GetRaceManager() const { return RaceManager; }
    AADAtmosphere* GetAtmosphere() const { return Atmosphere; }
    AADPoliceDirector* GetPoliceDirector() const { return Police; }
    AADTrafficManager* GetTrafficManager() const { return Traffic; }
    AADRegionalWorld* GetRegionalWorld() const { return RegionalWorld; }
    AADDistrict* GetDistrict() const { return District; }
    AADExplorationDirector* GetExploration() const { return Exploration; }
    FVector GetDrivingStartLocation() const;
    // Idempotent startup is shared by normal play and integrated PIE acceptance.
    bool InitializeLivingWorld(FString& OutError);
    FVector2D GetDriveBounds() const;
private:
    void RaceStateChanged(EADRaceState State);
    UPROPERTY(Transient) TObjectPtr<AADDistrict> District;
    UPROPERTY(Transient) TObjectPtr<APlayerStart> DrivingStart;
    UPROPERTY(Transient) TObjectPtr<AADAtmosphere> Atmosphere;
    UPROPERTY(Transient) TObjectPtr<AADTrafficManager> Traffic;
    UPROPERTY(Transient) TObjectPtr<AADPoliceDirector> Police;
    UPROPERTY(Transient) TObjectPtr<AADRegionalWorld> RegionalWorld;
    UPROPERTY(Transient) TObjectPtr<AADExplorationDirector> Exploration;
    UPROPERTY(Transient) TArray<TObjectPtr<APlayerStart>> OnlineStarts;
    TMap<TWeakObjectPtr<AController>,int32> PlayerSlots;
    TMap<TWeakObjectPtr<AController>,TWeakObjectPtr<AADVehiclePawn>> PlayerCars;
    float PlayerScanElapsed=1.f;
    TWeakObjectPtr<AADVehiclePawn> BoundPlayer;
    UPROPERTY(Transient) TObjectPtr<AADRaceManager> RaceManager;
};
