#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Player/ADVehiclePawn.h"
#include "Settings/ADSettingsSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

UADVehicleEffectsComponent::UADVehicleEffectsComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
bool UADVehicleEffectsComponent::LoadDefinition()
{
    FString Text; TSharedPtr<FJsonObject> Data;
    if (!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/Vehicles/aster_s6_effects.json")))
        || Text.Len()>8192 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data) || !Data)
    { Error=TEXT("Vehicle effects settings are missing or malformed."); return false; }
    const auto Number=[&](const TCHAR* Key,double Min,double Max,float& Value)
    {
        const auto* Item=Data->Values.Find(Key); double Parsed=0;
        if (!Item || (*Item)->Type!=EJson::Number || !(*Item)->TryGetNumber(Parsed) || !FMath::IsFinite(Parsed) || Parsed<Min || Parsed>Max) return false;
        Value=static_cast<float>(Parsed); return true;
    };
    float Schema=0;
    if (!Number(TEXT("schemaVersion"),1,1,Schema) || !Number(TEXT("nitrousSeconds"),1,15,DurationSeconds)
        || !Number(TEXT("rechargeSeconds"),5,120,RechargeSeconds) || !Number(TEXT("rechargeDelaySeconds"),0,30,RechargeDelaySeconds)
        || !Number(TEXT("torqueMultiplier"),1.05,1.6,PowerMultiplier))
    { Error=TEXT("Vehicle effects settings are outside supported bounds."); return false; }
    return true;
}
void UADVehicleEffectsComponent::BeginPlay()
{
    Super::BeginPlay();
    Car=Cast<AADVehiclePawn>(GetOwner());
    Chassis=Car.IsValid() ? Cast<UPrimitiveComponent>(Car->GetRootComponent()) : nullptr;
    bReady=Chassis && LoadDefinition();
    if (!bReady) { SetComponentTickEnabled(false); return; }
    Chassis->SetNotifyRigidBodyCollision(true);
    Chassis->OnComponentHit.AddDynamic(this,&UADVehicleEffectsComponent::OnHit);
    Car->GetPhysics()->AddTickPrerequisiteComponent(this);
    CreatePresentation();
}
void UADVehicleEffectsComponent::CreatePresentation()
{
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Glow=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_EmissiveRed.M_EmissiveRed"));
    UMaterialInterface* Dark=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_Rubber.M_Rubber"));
    if (!Cube || !Glow || !Dark) return;
    const auto Piece=[&](FVector Location,FVector Scale,UMaterialInterface* Material)
    {
        auto* Mesh=NewObject<UStaticMeshComponent>(Car.Get());
        Car->AddInstanceComponent(Mesh); Mesh->SetupAttachment(Chassis);
        Mesh->SetStaticMesh(Cube); Mesh->SetMaterial(0,Material);
        Mesh->SetRelativeLocation(Location); Mesh->SetRelativeScale3D(Scale);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false);
        Mesh->SetVisibility(false); Mesh->RegisterComponent(); return Mesh;
    };
    for (const int32 Side:{-1,1}) Flames.Add(Piece(FVector(-242,Side*57,-15),FVector(.55,.08,.08),Glow));
    for (int32 Index=0;Index<4;++Index)
        Scratches.Add(Piece(FVector(120+Index*14,18-Index*12,27),FVector(.45,.018,.01),Dark));
    ExhaustLight=NewObject<UPointLightComponent>(Car.Get()); Car->AddInstanceComponent(ExhaustLight);
    ExhaustLight->SetupAttachment(Chassis); ExhaustLight->SetRelativeLocation(FVector(-260,0,-10));
    ExhaustLight->SetLightColor(FLinearColor(1,.16f,.035f)); ExhaustLight->SetAttenuationRadius(350);
    ExhaustLight->SetCastShadows(false); ExhaustLight->SetIntensity(0); ExhaustLight->RegisterComponent();
}
void UADVehicleEffectsComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (!bReady || !Car.IsValid() || !Car->GetPhysics()->IsReady() || !FMath::IsFinite(DeltaTime) || DeltaTime<=0) return;
    // Clients request boost through pawn input RPCs; only the server consumes
    // the tank or changes mechanical power. Replicated effects drive visuals.
    if (!Car->HasAuthority()) { UpdatePresentation(); return; }
    auto* Physics=Car->GetPhysics(); const auto& State=Physics->GetTelemetry();
    bBoosting=bNitrousHeld && NitrousFraction>0 && Car->IsDrivingEnabled() && !Car->IsInGarage()
        && Chassis->IsSimulatingPhysics() && State.SpeedKmh>10 && State.Gear>0 && State.Throttle>.2f;
    float BoostAlpha=0.f;
    if (bBoosting)
    {
        const float Used=FMath::Min(NitrousFraction,DeltaTime/DurationSeconds);
        BoostAlpha=Used*DurationSeconds/DeltaTime;
        NitrousFraction=FMath::Max(0.f,NitrousFraction-Used); TimeSinceBoost=0;
    }
    else
    {
        const float PreviousElapsed=TimeSinceBoost;
        TimeSinceBoost+=DeltaTime;
        const float RechargeTime=FMath::Max(0.f,TimeSinceBoost-FMath::Max(PreviousElapsed,RechargeDelaySeconds));
        if (!bNitrousHeld) NitrousFraction=FMath::Min(1.f,NitrousFraction+RechargeTime/RechargeSeconds);
        TimeSinceBoost=FMath::Min(TimeSinceBoost,RechargeDelaySeconds+RechargeSeconds);
    }
    Physics->SetNitrousMultiplier(FMath::Lerp(1.f,PowerMultiplier,BoostAlpha));
    const auto* Settings=GetWorld()->GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    Physics->SetDamagePowerScale(Settings && Settings->UsesSimulationDamage() ? FMath::Lerp(.65f,1.f,Health) : 1.f);
    UpdatePresentation();
}
void UADVehicleEffectsComponent::UpdatePresentation()
{
    for (UStaticMeshComponent* Flame:Flames) Flame->SetVisibility(bBoosting);
    if (ExhaustLight) ExhaustLight->SetIntensity(bBoosting ? 1800.f : 0.f);
    for (int32 Index=0;Index<Scratches.Num();++Index) Scratches[Index]->SetVisibility(Health<1.f-(Index+1)*.15f);
}
void UADVehicleEffectsComponent::ReceiveNetworkState(float Fraction,float NewHealth,bool bNewBoosting)
{
    if (GetOwner()->HasAuthority() || !FMath::IsFinite(Fraction) || !FMath::IsFinite(NewHealth)) return;
    NitrousFraction=FMath::Clamp(Fraction,0.f,1.f);
    Health=FMath::Clamp(NewHealth,0.f,1.f);
    bBoosting=bNewBoosting;
}
void UADVehicleEffectsComponent::OnHit(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,FVector Impulse,const FHitResult&)
{
    if (!Car.IsValid() || !Car->HasAuthority() || Car->IsInGarage() || !Chassis || !Car->GetPhysics()->IsReady()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-LastHitSeconds<.5 || Impulse.ContainsNaN()) return;
    const double DeltaVelocity=Impulse.Size()*.01/FMath::Max(1.f,Car->GetPhysics()->GetDefinition().MassKg);
    if (DeltaVelocity<=4.) return;
    LastHitSeconds=Now;
    Health=FMath::Clamp(Health-static_cast<float>((DeltaVelocity-4.)/40.),0.f,1.f);
    for (int32 Index=0;Index<Scratches.Num();++Index) Scratches[Index]->SetVisibility(Health<1.f-(Index+1)*.15f);
}
void UADVehicleEffectsComponent::Repair()
{
    if (!GetOwner()->HasAuthority()) return;
    Health=1.f; NitrousFraction=1.f;
    bNitrousHeld=false; bBoosting=false; TimeSinceBoost=0.f;
    for (UStaticMeshComponent* Scratch:Scratches) Scratch->SetVisibility(false);
    if (Car.IsValid()) Car->GetPhysics()->SetDamagePowerScale(1.f);
    UpdatePresentation();
}
void UADVehicleEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Chassis) Chassis->OnComponentHit.RemoveAll(this);
    if (Car.IsValid()) Car->GetPhysics()->RemoveTickPrerequisiteComponent(this);
    Super::EndPlay(Reason);
}
