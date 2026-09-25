#include "Core/ADGameState.h"
#include "Net/UnrealNetwork.h"
void AADGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AADGameState,Hour);
    DOREPLIFETIME(AADGameState,Wetness);
    DOREPLIFETIME(AADGameState,Weather);
    DOREPLIFETIME(AADGameState,bEnvironmentReady);
}
