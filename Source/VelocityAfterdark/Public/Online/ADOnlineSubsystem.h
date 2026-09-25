#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "ADOnlineSubsystem.generated.h"
class UNetDriver;

/** Optional direct-connect free roam. Career and purchases remain offline-only. */
UCLASS()
class VELOCITYAFTERDARK_API UADOnlineSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool Host();
    bool Join(const FString& Address);
    void Disconnect();
    const FString& GetStatus() const { return Status; }
private:
    enum class ETransition : uint8 { None, Hosting, Joining, Disconnecting };
    bool CanTravel();
    void MapLoaded(UWorld* World);
    void NetworkFailed(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type Failure,const FString& Error);
    void TravelFailed(UWorld* World,ETravelFailure::Type Failure,const FString& Error);
    FString Status;
    FString PendingAddress;
    ETransition Transition=ETransition::None;
    FDelegateHandle NetworkFailureHandle,TravelFailureHandle,MapLoadedHandle;
};
