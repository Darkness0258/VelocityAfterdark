#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ADGameState.generated.h"
/** Small authority-owned environment snapshot for direct-connect free roam. */
UCLASS()
class VELOCITYAFTERDARK_API AADGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated) double Hour=23.;
    UPROPERTY(Replicated) float Wetness=0.f;
    UPROPERTY(Replicated) uint8 Weather=0;
    UPROPERTY(Replicated) bool bEnvironmentReady=false;
};
