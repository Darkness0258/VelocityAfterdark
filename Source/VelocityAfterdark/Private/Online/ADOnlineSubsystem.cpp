#include "Online/ADOnlineSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ADPlayerController.h"
#include "Garage/ADGarageSessionComponent.h"
#include "Presentation/ADCinematicComponent.h"
#include "Presentation/ADMapComponent.h"
#include "Racing/ADRaceManager.h"
#include "Core/ADGameMode.h"
#include "World/ADPoliceDirector.h"
#include "Settings/ADSettingsSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
// Accept an IPv4 address or ASCII DNS host with an optional TCP/UDP port. Reject URL
// options and path syntax so this command cannot be used as arbitrary map travel.
bool NormalizeAddress(const FString& Address,FString& Out)
{
    Out.Reset();
    if (Address.IsEmpty() || Address.Len()>128 || Address!=Address.TrimStartAndEnd()) return false;
    FString Host=Address;
    FString Port=TEXT("7777");
    int32 Colon=INDEX_NONE;
    if (Address.FindChar(TEXT(':'),Colon))
    {
        Host=Address.Left(Colon);
        Port=Address.Mid(Colon+1);
        if (Host.IsEmpty() || Port.IsEmpty() || Port.Len()>5) return false;
    }
    uint32 PortNumber=0;
    for (const TCHAR Character:Port)
    {
        if (Character<TEXT('0') || Character>TEXT('9')) return false;
        PortNumber=PortNumber*10+static_cast<uint32>(Character-TEXT('0'));
    }
    if (PortNumber<1 || PortNumber>65535) return false;

    const auto IsDigit=[](TCHAR Character) { return Character>=TEXT('0') && Character<=TEXT('9'); };
    const auto IsAsciiAlnum=[&IsDigit](TCHAR Character)
    {
        return IsDigit(Character) || (Character>=TEXT('a') && Character<=TEXT('z'))
            || (Character>=TEXT('A') && Character<=TEXT('Z'));
    };
    TArray<FString> Labels;
    Host.ParseIntoArray(Labels,TEXT("."),false);
    if (Labels.IsEmpty()) return false;
    bool bNumeric=true;
    for (const FString& Label:Labels)
    {
        if (Label.IsEmpty() || Label.Len()>63 || !IsAsciiAlnum(Label[0]) || !IsAsciiAlnum(Label[Label.Len()-1])) return false;
        for (const TCHAR Character:Label)
        {
            if (!IsAsciiAlnum(Character) && Character!=TEXT('-')) return false;
            bNumeric=bNumeric && IsDigit(Character);
        }
    }
    if (bNumeric)
    {
        // Disallow abbreviated/octal address forms; they resolve differently across platforms.
        if (Labels.Num()!=4) return false;
        for (const FString& Label:Labels)
        {
            if (Label.Len()>3 || (Label.Len()>1 && Label[0]==TEXT('0')) || FCString::Atoi(*Label)>255) return false;
        }
    }
    Out=FString::Printf(TEXT("%s:%u"),*Host.ToLower(),PortNumber);
    return true;
}
}

void UADOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (GEngine)
    {
        NetworkFailureHandle=GEngine->OnNetworkFailure().AddUObject(this,&UADOnlineSubsystem::NetworkFailed);
        TravelFailureHandle=GEngine->OnTravelFailure().AddUObject(this,&UADOnlineSubsystem::TravelFailed);
    }
    MapLoadedHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&UADOnlineSubsystem::MapLoaded);
}
void UADOnlineSubsystem::Deinitialize()
{
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
    }
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
    Transition=ETransition::None;
    PendingAddress.Reset();
    Super::Deinitialize();
}
bool UADOnlineSubsystem::CanTravel()
{
    if (Transition!=ETransition::None)
    { Status=TEXT("A session change is already in progress. ADDisconnect cancels a connection attempt."); return false; }
    auto* PC=GetWorld() ? Cast<AADPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    const auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    if (!PC || !PC->IsLocalController() || (Settings && Settings->IsOpen())
        || (PC->GetGarageSession() && PC->GetGarageSession()->IsActive())
        || (PC->GetCinematic() && PC->GetCinematic()->IsActive())
        || (PC->GetMap() && PC->GetMap()->IsOpen())
        || (PC->GetRaceManager() && PC->GetRaceManager()->GetState()!=EADRaceState::Idle))
    { Status=TEXT("Return to free drive before changing online sessions."); return false; }
    if (const auto* Game=GetWorld()->GetAuthGameMode<AADGameMode>(); Game && Game->GetPoliceDirector() && Game->GetPoliceDirector()->IsActive())
    { Status=TEXT("Finish the pursuit before changing online sessions."); return false; }
    return true;
}
bool UADOnlineSubsystem::Host()
{
    if (!CanTravel()) return false;
    if (GetWorld()->GetNetMode()==NM_ListenServer)
    { Status=TEXT("This session is already hosting direct-connect free roam."); return false; }
    Transition=ETransition::Hosting;
    Status=TEXT("Starting direct-connect free roam...");
    UGameplayStatics::OpenLevel(GetWorld(),TEXT("/Game/Velocity/Maps/L_Dockside"),true,TEXT("listen"));
    return true;
}
bool UADOnlineSubsystem::Join(const FString& Address)
{
    if (!CanTravel()) return false;
    FString NormalizedAddress;
    if (!NormalizeAddress(Address,NormalizedAddress))
    { Status=TEXT("Enter an IPv4 or DNS host and an optional port from 1-65535, for example 127.0.0.1:7777."); return false; }
    PendingAddress=MoveTemp(NormalizedAddress);
    Transition=ETransition::Joining;
    Status=TEXT("Connecting to ")+PendingAddress+TEXT("...");
    GetWorld()->GetFirstPlayerController()->ClientTravel(PendingAddress,TRAVEL_Absolute);
    return true;
}
void UADOnlineSubsystem::Disconnect()
{
    if (!GetWorld()) return;
    if (Transition==ETransition::Disconnecting) return;
    if (Transition==ETransition::None && GetWorld()->GetNetMode()==NM_Standalone)
    { Status=TEXT("Already in offline driving."); return; }
    Transition=ETransition::Disconnecting;
    PendingAddress.Reset();
    Status=TEXT("Returning to offline driving...");
    if (auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>(); Settings && Settings->IsOpen()) Settings->Close();
    UGameplayStatics::OpenLevel(GetWorld(),TEXT("/Game/Velocity/Maps/L_Dockside"),true);
}
void UADOnlineSubsystem::MapLoaded(UWorld* World)
{
    if (!World || World->GetGameInstance()!=GetGameInstance() || Transition==ETransition::None) return;
    if (Transition==ETransition::Hosting && World->GetNetMode()==NM_ListenServer)
        Status=TEXT("Hosting direct-connect free roam.");
    else if (Transition==ETransition::Joining && World->GetNetMode()==NM_Client)
        Status=TEXT("Joined direct-connect free roam at ")+PendingAddress+TEXT(".");
    else if (Transition==ETransition::Disconnecting && World->GetNetMode()==NM_Standalone)
    {
        Status=TEXT("Returned to offline driving.");
        if (FParse::Param(FCommandLine::Get(),TEXT("AfterdarkNetDriveProbe")))
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_CLIENT_RETURNED_TO_OFFLINE: session travel completed."));
    }
    else Status=TEXT("The requested network session did not open. You can retry from free drive.");
    Transition=ETransition::None;
    PendingAddress.Reset();
}
void UADOnlineSubsystem::NetworkFailed(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type,const FString& Error)
{
    // Failed initial connections have no destination world yet. Unreal supplies
    // their pending net driver instead; match its context rather than all instances.
    const FWorldContext* PendingContext=GEngine && Driver ? GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver) : nullptr;
    if ((World && World->GetGameInstance()==GetGameInstance())
        || (PendingContext && PendingContext->OwningGameInstance==GetGameInstance()))
    {
        Transition=ETransition::None;
        PendingAddress.Reset();
        Status=TEXT("Connection failed: ")+Error.Left(200);
    }
}
void UADOnlineSubsystem::TravelFailed(UWorld* World,ETravelFailure::Type,const FString& Error)
{
    if (World && World->GetGameInstance()==GetGameInstance())
    {
        Transition=ETransition::None;
        PendingAddress.Reset();
        Status=TEXT("Session travel failed: ")+Error.Left(200);
    }
}
