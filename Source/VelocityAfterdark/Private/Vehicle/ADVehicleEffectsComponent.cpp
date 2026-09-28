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
#include "Materials/MaterialInstanceDynamic.h"
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
    if (!Number(TEXT("schemaVersion"),1,2,Schema) || !FMath::IsNearlyEqual(Schema,FMath::RoundToFloat(Schema))
        || !Number(TEXT("nitrousSeconds"),1,15,DurationSeconds)
        || !Number(TEXT("rechargeSeconds"),5,120,RechargeSeconds) || !Number(TEXT("rechargeDelaySeconds"),0,30,RechargeDelaySeconds)
        || !Number(TEXT("torqueMultiplier"),1.05,1.6,PowerMultiplier))
    { Error=TEXT("Vehicle effects settings are outside supported bounds."); return false; }
    if (Schema >= 2.f)
    {
        float SparkCount=0.f;
        const TArray<TSharedPtr<FJsonValue>>* ColorValues=nullptr;
        if (!Number(TEXT("impactDamageThresholdMps"),2,12,ImpactDamageThresholdMps)
            || !Number(TEXT("impactDamageScaleMps"),12,80,ImpactDamageScaleMps)
            || !Number(TEXT("impactCooldownSeconds"),.1,2,ImpactCooldownSeconds)
            || !Number(TEXT("sparkThresholdMps"),4,30,SparkThresholdMps)
            || !Number(TEXT("maxImpactSparks"),4,12,SparkCount)
            || !FMath::IsNearlyEqual(SparkCount,FMath::RoundToFloat(SparkCount))
            || !Number(TEXT("sparkLifetimeMinSeconds"),.08,.35,SparkLifetimeMinSeconds)
            || !Number(TEXT("sparkLifetimeMaxSeconds"),.16,.65,SparkLifetimeMaxSeconds)
            || SparkLifetimeMaxSeconds < SparkLifetimeMinSeconds
            || !Number(TEXT("impactLightLumens"),100,4000,ImpactLightLumens)
            || !Data->TryGetArrayField(TEXT("impactColor"),ColorValues) || !ColorValues || ColorValues->Num()!=3)
        { Error=TEXT("Vehicle impact settings are missing or outside supported bounds."); return false; }
        float Channels[3]={};
        for (int32 Index=0;Index<3;++Index)
        {
            double Channel=0.;
            if (!(*ColorValues)[Index].IsValid() || (*ColorValues)[Index]->Type!=EJson::Number
                || !(*ColorValues)[Index]->TryGetNumber(Channel) || !FMath::IsFinite(Channel) || Channel<0. || Channel>4.)
            { Error=TEXT("Vehicle impact color is invalid."); return false; }
            Channels[Index]=static_cast<float>(Channel);
        }
        if (Channels[0]+Channels[1]+Channels[2]<=0.f)
        { Error=TEXT("Vehicle impact color cannot be black."); return false; }
        MaxImpactSparks=FMath::RoundToInt(SparkCount);
        ImpactColor=FLinearColor(Channels[0],Channels[1],Channels[2]);
    }
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
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(Cube); Mesh->SetMaterial(0,Material);
        Mesh->SetRelativeLocation(Location); Mesh->SetRelativeScale3D(Scale);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false);
        Mesh->SetVisibility(false); Mesh->RegisterComponent(); return Mesh;
    };
    for (const int32 Side:{-1,1})
    {
        UStaticMeshComponent* Flame=Piece(FVector(-242,Side*57,-15),FVector(.55,.08,.08),Glow);
        Flames.Add(Flame);
        UMaterialInstanceDynamic* FlameMaterial=UMaterialInstanceDynamic::Create(Glow,Car.Get());
        Flame->SetMaterial(0,FlameMaterial);
        FlameMaterials.Add(FlameMaterial);
    }
    for (int32 Index=0;Index<4;++Index)
        Scratches.Add(Piece(FVector(120+Index*14,18-Index*12,27),FVector(.45,.018,.01),Dark));
    UMaterialInterface* SparkBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"));
    if (!SparkBase) SparkBase=Glow;
    ImpactSparkStates.SetNum(MaxImpactSparks);
    for (int32 Index=0;Index<MaxImpactSparks;++Index)
    {
        UStaticMeshComponent* Spark=Piece(FVector::ZeroVector,FVector(.2f,.014f,.012f),SparkBase);
        Spark->SetReceivesDecals(false);
        ImpactSparks.Add(Spark);
        UMaterialInstanceDynamic* Material=UMaterialInstanceDynamic::Create(SparkBase,Car.Get());
        Spark->SetMaterial(0,Material);
        SparkMaterials.Add(Material);
    }
    ExhaustLight=NewObject<UPointLightComponent>(Car.Get()); Car->AddInstanceComponent(ExhaustLight);
    ExhaustLight->SetupAttachment(Chassis); ExhaustLight->SetRelativeLocation(FVector(-260,0,-10));
    ExhaustLight->SetLightColor(FLinearColor(1,.16f,.035f)); ExhaustLight->SetAttenuationRadius(350);
    ExhaustLight->SetCastShadows(false); ExhaustLight->SetIntensity(0); ExhaustLight->RegisterComponent();
    ImpactLight=NewObject<UPointLightComponent>(Car.Get()); Car->AddInstanceComponent(ImpactLight);
    ImpactLight->SetupAttachment(Chassis); ImpactLight->SetLightColor(ImpactColor);
    ImpactLight->SetIntensityUnits(ELightUnits::Lumens);
    ImpactLight->SetAttenuationRadius(420); ImpactLight->SetSourceRadius(42.f);
    ImpactLight->SetCastShadows(false); ImpactLight->SetIntensity(0); ImpactLight->SetVisibility(false); ImpactLight->RegisterComponent();
}
void UADVehicleEffectsComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (!bReady || !Car.IsValid() || !FMath::IsFinite(DeltaTime) || DeltaTime<=0) return;
    UpdateImpactSparks(DeltaTime);
    if (!Car->GetPhysics()->IsReady()) return;
    // Clients request boost through pawn input RPCs; only the server consumes
    // the tank or changes mechanical power. Replicated effects drive visuals.
    if (!Car->HasAuthority()) { UpdatePresentation(DeltaTime); return; }
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
    UpdatePresentation(DeltaTime);
}
void UADVehicleEffectsComponent::UpdatePresentation(float DeltaSeconds)
{
    PresentationTimeSeconds=FMath::Fmod(PresentationTimeSeconds+DeltaSeconds,1000.f);
    const float Pulse=.78f+.22f*FMath::Sin(PresentationTimeSeconds*39.f);
    const FLinearColor NitrousColor=FMath::Lerp(FLinearColor(.16f,.62f,1.f),ImpactColor,
        FMath::Clamp(1.f-NitrousFraction,0.f,1.f));
    for (int32 Index=0;Index<Flames.Num();++Index)
    {
        UStaticMeshComponent* Flame=Flames[Index];
        Flame->SetVisibility(bBoosting);
        if (!bBoosting) continue;
        Flame->SetRelativeScale3D(FVector(.46f+.13f*Pulse,.08f,.08f));
        if (FlameMaterials.IsValidIndex(Index) && FlameMaterials[Index])
            FlameMaterials[Index]->SetVectorParameterValue(TEXT("EmissiveColor"),NitrousColor*(1.4f+.9f*Pulse));
    }
    if (ExhaustLight)
    {
        ExhaustLight->SetLightColor(NitrousColor);
        ExhaustLight->SetIntensity(bBoosting ? 1400.f+500.f*Pulse : 0.f);
    }
    for (int32 Index=0;Index<Scratches.Num();++Index) Scratches[Index]->SetVisibility(Health<1.f-(Index+1)*.15f);
}
void UADVehicleEffectsComponent::UpdateImpactSparks(float DeltaSeconds)
{
    for (int32 Index=0;Index<ImpactSparkStates.Num();++Index)
    {
        FImpactSparkState& State=ImpactSparkStates[Index];
        if (!State.bActive || !ImpactSparks.IsValidIndex(Index) || !ImpactSparks[Index]) continue;
        State.AgeSeconds+=DeltaSeconds;
        const float Alpha=1.f-State.AgeSeconds/FMath::Max(.01f,State.LifetimeSeconds);
        if (Alpha<=0.f)
        {
            State.bActive=false;
            ImpactSparks[Index]->SetVisibility(false);
            continue;
        }
        State.Velocity.X*=FMath::Exp(-1.1f*DeltaSeconds);
        State.Velocity.Y*=FMath::Exp(-1.1f*DeltaSeconds);
        State.Velocity.Z-=980.f*DeltaSeconds;
        ImpactSparks[Index]->AddWorldOffset(State.Velocity*DeltaSeconds,false,nullptr,ETeleportType::None);
        const FVector Direction=State.Velocity.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
        ImpactSparks[Index]->SetWorldRotation(FRotationMatrix::MakeFromX(Direction).Rotator());
        ImpactSparks[Index]->SetWorldScale3D(FVector(FMath::Max(.015f,State.LengthCm*Alpha/100.f),.014f,.012f));
        if (SparkMaterials.IsValidIndex(Index) && SparkMaterials[Index])
        {
            const FLinearColor Color=Index%4==0 ? FLinearColor(.20f,.75f,1.f) : ImpactColor;
            SparkMaterials[Index]->SetVectorParameterValue(TEXT("EmissiveColor"),Color*(.8f+2.4f*Alpha));
        }
    }
    if (ImpactLight)
    {
        ImpactPresentationAgeSeconds+=DeltaSeconds;
        const float Alpha=FMath::Clamp(1.f-ImpactPresentationAgeSeconds/.22f,0.f,1.f);
        ImpactLight->SetIntensity(ImpactLightLumens*ImpactFlashStrength*Alpha*Alpha);
        if (Alpha<=0.f) ImpactLight->SetVisibility(false);
    }
}
void UADVehicleEffectsComponent::ReceiveNetworkState(float Fraction,float NewHealth,bool bNewBoosting)
{
    if (GetOwner()->HasAuthority() || !FMath::IsFinite(Fraction) || !FMath::IsFinite(NewHealth)) return;
    NitrousFraction=FMath::Clamp(Fraction,0.f,1.f);
    Health=FMath::Clamp(NewHealth,0.f,1.f);
    bBoosting=bNewBoosting;
}
void UADVehicleEffectsComponent::OnHit(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,FVector Impulse,const FHitResult& Hit)
{
    if (!Car.IsValid() || !Car->HasAuthority() || Car->IsInGarage() || !Chassis || !Car->GetPhysics()->IsReady()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-LastHitSeconds<ImpactCooldownSeconds || Impulse.ContainsNaN()) return;
    const double DeltaVelocity=Impulse.Size()*.01/FMath::Max(1.f,Car->GetPhysics()->GetDefinition().MassKg);
    if (!FMath::IsFinite(DeltaVelocity) || DeltaVelocity<=ImpactDamageThresholdMps) return;
    LastHitSeconds=Now;
    Health=FMath::Clamp(Health-static_cast<float>((DeltaVelocity-ImpactDamageThresholdMps)/ImpactDamageScaleMps),0.f,1.f);
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,-Car->GetActorForwardVector());
    const uint8 EncodedSpeed=static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(DeltaVelocity/80.,0.,1.)*255.));
    Car->MulticastImpactPresentation(FVector_NetQuantize(Hit.ImpactPoint),FVector_NetQuantizeNormal(Normal),EncodedSpeed);
}
void UADVehicleEffectsComponent::PlayImpactPresentation(const FVector& Location,const FVector& SurfaceNormal,float DeltaVelocityMps)
{
    if (!bReady || !Car.IsValid() || Location.ContainsNaN() || !FMath::IsFinite(DeltaVelocityMps)) return;
    ++ImpactPresentationCount;
    const FVector Normal=SurfaceNormal.GetSafeNormal(UE_SMALL_NUMBER,-Car->GetActorForwardVector());
    const FVector LocalPoint=Chassis->GetComponentTransform().InverseTransformPosition(Location);
    const FVector LocalNormal=Chassis->GetComponentTransform().InverseTransformVectorNoScale(Normal)
        .GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    PlaceDamageMarks(LocalPoint,LocalNormal);
    ImpactPresentationAgeSeconds=0.f;
    ImpactFlashStrength=FMath::Clamp(DeltaVelocityMps/32.f,.15f,1.f);
    if (ImpactLight)
    {
        ImpactLight->SetWorldLocation(Location+Normal*8.f);
        ImpactLight->SetLightColor(FMath::Lerp(ImpactColor,FLinearColor(1.f,.92f,.72f),ImpactFlashStrength*.65f));
        ImpactLight->SetIntensity(ImpactLightLumens*ImpactFlashStrength);
        ImpactLight->SetVisibility(true);
    }
    if (DeltaVelocityMps<SparkThresholdMps || ImpactSparks.IsEmpty()) return;
    const float Strength=FMath::Clamp((DeltaVelocityMps-SparkThresholdMps)/38.f,0.f,1.f);
    const int32 SparkCount=FMath::Clamp(FMath::RoundToInt(FMath::Lerp(4.f,static_cast<float>(MaxImpactSparks),Strength)),1,ImpactSparks.Num());
    const FVector Reference=FMath::Abs(Normal.Z)<.84f ? FVector::UpVector : FVector::RightVector;
    const FVector Tangent=FVector::CrossProduct(Normal,Reference).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    const FVector Bitangent=FVector::CrossProduct(Normal,Tangent).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    for (int32 SparkIndex=0;SparkIndex<SparkCount;++SparkIndex)
    {
        const int32 Index=NextSparkIndex++%ImpactSparks.Num();
        FImpactSparkState& State=ImpactSparkStates[Index];
        const FVector Direction=(Normal*FMath::FRandRange(.25f,.9f)
            +Tangent*FMath::FRandRange(-.9f,.9f)+Bitangent*FMath::FRandRange(-.75f,.75f)
            +FVector::UpVector*FMath::FRandRange(.15f,.9f)).GetSafeNormal(UE_SMALL_NUMBER,Normal);
        State.Velocity=Direction*FMath::FRandRange(280.f,850.f)*(.65f+.5f*Strength);
        State.AgeSeconds=0.f;
        State.LifetimeSeconds=FMath::FRandRange(SparkLifetimeMinSeconds,SparkLifetimeMaxSeconds);
        State.LengthCm=FMath::FRandRange(18.f,46.f);
        State.bActive=true;
        UStaticMeshComponent* Spark=ImpactSparks[Index];
        if (Spark->GetAttachParent()) Spark->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        Spark->SetWorldLocation(Location+Normal*FMath::FRandRange(2.f,10.f));
        Spark->SetWorldRotation(FRotationMatrix::MakeFromX(Direction).Rotator());
        Spark->SetWorldScale3D(FVector(State.LengthCm/100.f,.014f,.012f));
        Spark->SetVisibility(true);
        if (SparkMaterials.IsValidIndex(Index) && SparkMaterials[Index])
        {
            const FLinearColor Color=Index%4==0 ? FLinearColor(.20f,.75f,1.f) : ImpactColor;
            SparkMaterials[Index]->SetVectorParameterValue(TEXT("EmissiveColor"),Color*(.8f+2.4f*Strength));
        }
    }
}
int32 UADVehicleEffectsComponent::GetActiveImpactSparkCount() const
{
    int32 ActiveCount=0;
    for (const FImpactSparkState& State:ImpactSparkStates) if (State.bActive) ++ActiveCount;
    return ActiveCount;
}
void UADVehicleEffectsComponent::PlaceDamageMarks(const FVector& LocalPoint,const FVector& LocalNormal)
{
    FVector Reference=FMath::Abs(LocalNormal.Z)<.85f ? FVector::UpVector : FVector::RightVector;
    const FVector Tangent=FVector::CrossProduct(LocalNormal,Reference).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector Bitangent=FVector::CrossProduct(LocalNormal,Tangent).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    for (int32 Index=0;Index<Scratches.Num();++Index)
    {
        const float Offset=static_cast<float>(Index)-1.5f;
        Scratches[Index]->SetRelativeLocation(LocalPoint+LocalNormal*2.5f
            +Tangent*(Offset*7.f)+Bitangent*(FMath::Abs(Offset)*2.f));
        Scratches[Index]->SetRelativeRotation(FRotationMatrix::MakeFromXZ(Tangent,LocalNormal).Rotator());
    }
}
void UADVehicleEffectsComponent::Repair()
{
    if (!GetOwner()->HasAuthority()) return;
    Health=1.f; NitrousFraction=1.f;
    bNitrousHeld=false; bBoosting=false; TimeSinceBoost=0.f;
    for (UStaticMeshComponent* Scratch:Scratches) Scratch->SetVisibility(false);
    for (int32 Index=0;Index<ImpactSparkStates.Num();++Index)
    {
        ImpactSparkStates[Index].bActive=false;
        if (ImpactSparks.IsValidIndex(Index) && ImpactSparks[Index]) ImpactSparks[Index]->SetVisibility(false);
    }
    ImpactFlashStrength=0.f;
    ImpactPresentationAgeSeconds=1.f;
    if (ImpactLight) ImpactLight->SetIntensity(0.f);
    if (Car.IsValid()) Car->GetPhysics()->SetDamagePowerScale(1.f);
    UpdatePresentation(0.f);
}
void UADVehicleEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Chassis) Chassis->OnComponentHit.RemoveAll(this);
    if (Car.IsValid()) Car->GetPhysics()->RemoveTickPrerequisiteComponent(this);
    Super::EndPlay(Reason);
}
