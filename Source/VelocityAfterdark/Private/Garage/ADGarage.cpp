#include "Garage/ADGarage.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"

AADGarage::AADGarage()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GarageRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("InspectionCamera"));
    Camera->SetupAttachment(RootComponent);
    Camera->FieldOfView = 55.f;
    Camera->PostProcessBlendWeight = 1.f;
    Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    // The studio is calibrated under neutral so pale concrete and
    // metallic paint retain highlight detail in the editor and packaged game.
    Camera->PostProcessSettings.AutoExposureBias = -1.3f;
    Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Camera->PostProcessSettings.BloomIntensity = .15f;
    Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
    Camera->PostProcessSettings.VignetteIntensity = .22f;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0.f;
}

void AADGarage::BeginPlay()
{
    Super::BeginPlay();
    bReady = BuildStage();
    UpdateLightingExposure();
    UpdateCamera(0.f);
    SetActorHiddenInGame(true);
}

UMaterialInstanceDynamic* AADGarage::CreateSurface(const TCHAR* Name, const TCHAR* Asset,
    FLinearColor Color, float Roughness, float Metallic, float Emission)
{
    const FString Path = FString::Printf(TEXT("/Game/Velocity/Materials/%s.%s"), Asset, Asset);
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, *Path);
    if (!Base) return nullptr;
    auto* Surface = UMaterialInstanceDynamic::Create(Base, this, FName(Name));
    if (!Surface) return nullptr;
    Surface->SetVectorParameterValue(TEXT("BaseColor"), Color);
    Surface->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    Surface->SetScalarParameterValue(TEXT("Metallic"), Metallic);
    if (Emission > 0.f) Surface->SetVectorParameterValue(TEXT("EmissiveColor"), Color * Emission);
    Surfaces.Add(Surface);
    return Surface;
}

void AADGarage::AddPiece(FName BatchName, UMaterialInterface* Material, FVector Position,
    FVector SizeCm, bool bCylinder, FRotator Rotation)
{
    TObjectPtr<UInstancedStaticMeshComponent>& Batch = Batches.FindOrAdd(BatchName);
    if (!Batch)
    {
        Batch = NewObject<UInstancedStaticMeshComponent>(this, BatchName);
        AddInstanceComponent(Batch);
        Batch->SetupAttachment(RootComponent);
        Batch->SetStaticMesh(bCylinder ? Cylinder : Cube);
        Batch->SetMaterial(0, Material);
        Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch->SetCanEverAffectNavigation(false);
        Batch->RegisterComponent();
    }
    Batch->AddInstance(FTransform(Rotation, Position, SizeCm / 100.f));
}

void AADGarage::AddStudioLight(FName Name, FVector Position, FVector Target,
    FLinearColor Color, float Lumens, bool bShadows)
{
    auto* Light = NewObject<USpotLightComponent>(this, Name);
    AddInstanceComponent(Light);
    Light->SetupAttachment(RootComponent);
    Light->SetRelativeLocation(Position);
    Light->SetRelativeRotation((Target - Position).Rotation());
    Light->SetIntensityUnits(ELightUnits::Lumens);
    Light->SetIntensity(Lumens);
    Light->SetLightColor(Color);
    Light->SetAttenuationRadius(2000.f);
    Light->SetInnerConeAngle(40.f);
    Light->SetOuterConeAngle(72.f);
    Light->SetSourceRadius(45.f);
    Light->SetSoftSourceRadius(60.f);
    Light->SetCastShadows(bShadows);
    Light->RegisterComponent();
    StudioLights.Add({Light, Lumens});
}

bool AADGarage::BuildStage()
{
    Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (!Cube || !Cylinder) return false;
    auto* Floor = CreateSurface(TEXT("StudioFloor"), TEXT("M_Concrete_PBR_V3"), FLinearColor(.105f,.12f,.13f), .36f, .12f);
    auto* Wall = CreateSurface(TEXT("StudioWall"), TEXT("M_Concrete_PBR_V3"), FLinearColor(.13f,.145f,.155f), .8f, 0.f);
    auto* Dark = CreateSurface(TEXT("StudioDark"), TEXT("M_IndustrialMetal_PBR"), FLinearColor(.022f,.031f,.04f), .47f, .55f);
    auto* Metal = CreateSurface(TEXT("StudioMetal"), TEXT("M_IndustrialMetal_PBR"), FLinearColor(.21f,.235f,.26f), .3f, .8f);
    auto* Cabinet = CreateSurface(TEXT("StudioCabinet"), TEXT("M_Paint"), FLinearColor(.027f,.12f,.14f), .32f, .45f);
    auto* Rubber = CreateSurface(TEXT("StudioRubber"), TEXT("M_Rubber_PBR"), FLinearColor(.018f,.02f,.025f), .87f, 0.f);
    auto* Marking = CreateSurface(TEXT("StudioMarking"), TEXT("M_RoadMarking"), FLinearColor(.58f,.62f,.60f), .64f, 0.f);
    auto* White = CreateSurface(TEXT("StudioLight"), TEXT("M_EmissiveWhite"), FLinearColor(.77f,.88f,1.f), .25f, 0.f, 3.f);
    auto* Amber = CreateSurface(TEXT("StudioAmber"), TEXT("M_EmissiveWhite"), FLinearColor(1.f,.49f,.16f), .25f, 0.f, 2.f);
    if (!Floor || !Wall || !Dark || !Metal || !Cabinet || !Rubber || !Marking || !White || !Amber) return false;

    AddPiece(TEXT("Floor"), Floor, FVector(0,0,-12), FVector(2500,2500,24));
    // A shallow work pad places stock tires at floor contact after ResetState.
    AddPiece(TEXT("Floor"), Floor, FVector(0,0,6), FVector(760,560,12));
    AddPiece(TEXT("Wall"), Wall, FVector(-1230,0,445), FVector(40,2500,890));
    AddPiece(TEXT("Wall"), Wall, FVector(1230,0,445), FVector(40,2500,890));
    AddPiece(TEXT("Wall"), Wall, FVector(0,-1230,445), FVector(2420,40,890));
    AddPiece(TEXT("Wall"), Wall, FVector(0,1230,445), FVector(2420,40,890));
    AddPiece(TEXT("Dark"), Dark, FVector(0,0,910), FVector(2500,2500,40));

    // Concrete joints and bay markings define scale without added textures.
    for (int32 I = -2; I <= 2; ++I)
    {
        AddPiece(TEXT("Dark"), Dark, FVector(I*400.f,0,.08), FVector(1.3,2420,.16));
        AddPiece(TEXT("Dark"), Dark, FVector(0,I*400.f,.1), FVector(2420,1.3,.16));
    }
    for (int32 Side : {-1,1})
    {
        AddPiece(TEXT("Marking"), Marking, FVector(0,Side*273.f,12.2), FVector(710,3,.3));
        AddPiece(TEXT("Marking"), Marking, FVector(Side*368.f,0,12.2), FVector(3,520,.3));
        AddPiece(TEXT("Dark"), Dark, FVector(-1206,Side*670.f,220), FVector(8,18,440));
        AddPiece(TEXT("Metal"), Metal, FVector(-1200,Side*670.f,225), FVector(7,23,8));
        AddPiece(TEXT("White"), White, FVector(-1202,Side*645.f,270), FVector(6,7,330));
        AddPiece(TEXT("Amber"), Amber, FVector(300,Side*1207.f,110), FVector(1550,4,3));
        AddPiece(TEXT("Dark"), Dark, FVector(0,Side*380.f,655), FVector(1200,30,20));
        AddPiece(TEXT("White"), White, FVector(0,Side*380.f,643), FVector(1160,19,4));
        AddPiece(TEXT("Dark"), Dark, FVector(Side*600.f,0,670), FVector(24,2460,30));
        for (int32 End : {-1,1})
            AddPiece(TEXT("Metal"), Metal, FVector(End*530.f,Side*380.f,775), FVector(3,3,230));
    }

    // Rear roller door, metal ribs and surround; all geometry is presentation only.
    AddPiece(TEXT("Dark"), Dark, FVector(-1200,0,240), FVector(12,880,480));
    for (int32 I = 0; I < 16; ++I)
        AddPiece(TEXT("Metal"), Metal, FVector(-1190,0,25.f+I*27.f), FVector(6,820,22));
    AddPiece(TEXT("Amber"), Amber, FVector(-1185,0,489), FVector(5,870,6));

    // Cabinet drawers, workbench, tires and a lift suggest a functioning workshop.
    AddPiece(TEXT("Cabinet"), Cabinet, FVector(-650,-1080,95), FVector(700,155,190));
    AddPiece(TEXT("Metal"), Metal, FVector(-650,-1080,193), FVector(720,170,12));
    for (int32 Drawer = 0; Drawer < 5; ++Drawer)
        for (int32 Column = 0; Column < 3; ++Column)
        {
            AddPiece(TEXT("Dark"), Dark, FVector(-880.f+Column*230.f,-1000,22.f+Drawer*32.f), FVector(218,3,2));
            AddPiece(TEXT("Metal"), Metal, FVector(-880.f+Column*230.f,-994,40.f+Drawer*32.f), FVector(65,7,4));
        }
    AddPiece(TEXT("Dark"), Dark, FVector(-650,-1205,330), FVector(700,12,205));
    for (int32 I = 0; I < 9; ++I)
        AddPiece(TEXT("Metal"), Metal, FVector(-925.f+I*69.f,-1188,335), FVector(7,7,55.f+(I%3)*11.f));
    AddPiece(TEXT("White"), White, FVector(-650,-1180,457), FVector(720,12,5));
    for (int32 I = 0; I < 4; ++I)
        AddPiece(TEXT("Tires"), Rubber, FVector(520,-1060,18.f+I*30.f), FVector(78,78,30), true);
    for (int32 Side : {-1,1})
    {
        AddPiece(TEXT("Cabinet"), Cabinet, FVector(920,Side*670.f,200), FVector(46,70,400));
        AddPiece(TEXT("Dark"), Dark, FVector(920,Side*670.f,5), FVector(135,160,10));
        AddPiece(TEXT("Metal"), Metal, FVector(815,Side*590.f,28), FVector(240,24,22), false, FRotator(0,Side*23.f,0));
        AddPiece(TEXT("Pads"), Rubber, FVector(705,Side*545.f,40), FVector(38,38,10), true);
    }
    AddPiece(TEXT("Cabinet"), Cabinet, FVector(760,1050,60), FVector(190,95,110));
    AddPiece(TEXT("Metal"), Metal, FVector(760,1050,119), FVector(205,106,8));
    AddPiece(TEXT("Dark"), Dark, FVector(760,990,63), FVector(178,6,92));

    // One shadowed key plus inexpensive fills remains usable on the SM5 path.
    AddStudioLight(TEXT("KeyLight"), FVector(420,370,600), FVector(0,0,95), FLinearColor(.83f,.9f,1.f), 22000.f, true);
    AddStudioLight(TEXT("FillLight"), FVector(380,-500,470), FVector(0,0,95), FLinearColor(.58f,.78f,1.f), 11000.f, false);
    AddStudioLight(TEXT("RimLight"), FVector(-570,120,530), FVector(0,0,100), FLinearColor(1.f,.66f,.39f), 19000.f, false);
    AddStudioLight(TEXT("WorkshopLight"), FVector(-620,-800,540), FVector(-650,-1070,140), FLinearColor(.84f,.91f,1.f), 6500.f, false);
    return true;
}

FTransform AADGarage::GetDisplayTransform() const
{
    // The 12 cm service pad + stock unloaded suspension + tire radius = 90 cm.
    float Height = 90.f;
    if (const AADVehiclePawn* Car = Occupant.Get())
    {
        const FADVehicleDefinition& Definition = Car->GetPhysics()->GetDefinition();
        Height = 12.f + 100.f * (Definition.SuspensionRestLengthM + Definition.WheelRadiusM);
    }
    return FTransform(GetActorQuat(), GetActorTransform().TransformPosition(FVector(0,0,Height)));
}

bool AADGarage::Enter(AADVehiclePawn* Car, FString& OutError)
{
    OutError.Reset();
    if (IsOccupied()) { OutError = TEXT("The garage is already occupied."); return false; }
    if (!bReady) { OutError = TEXT("Garage assets are unavailable. Check the content bootstrap."); return false; }
    if (!IsValid(Car) || Car->IsActorBeingDestroyed() || !Car->GetPhysics() || !Car->GetPhysics()->IsReady() || Car->IsInGarage())
    { OutError = TEXT("The vehicle is not ready to enter the garage."); return false; }
    UPrimitiveComponent* Chassis = Cast<UPrimitiveComponent>(Car->GetRootComponent());
    const float Speed = FMath::Abs(Car->GetPhysics()->GetTelemetry().SpeedKmh);
    const float PhysicalSpeed = Chassis ? Chassis->GetComponentVelocity().Size() * .036f : 0.f;
    if (!Chassis || !FMath::IsFinite(Speed) || !FMath::IsFinite(PhysicalSpeed) || Speed > 5.f || PhysicalSpeed > 5.f)
    { OutError = TEXT("Stop the vehicle before entering the garage (below 5 km/h)."); return false; }
    ReturnTransform = Car->GetActorTransform();
    if (ReturnTransform.ContainsNaN()) { OutError = TEXT("The vehicle transform is invalid."); return false; }
    Occupant = Car;
    bReturnDriving = Car->IsDrivingEnabled();
    bReturnSimulating = Chassis->IsSimulatingPhysics();
    bReturnPhysicsTick = Car->GetPhysics()->IsComponentTickEnabled();
    Car->SetDrivingEnabled(false);
    if (bReturnSimulating)
    {
        Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    }
    Chassis->SetSimulatePhysics(false);
    Car->GetPhysics()->SetComponentTickEnabled(false);
    Car->GetPhysics()->ResetState();
    Car->SetGarageMode(true);
    if (!Car->SetActorTransform(GetDisplayTransform(), false, nullptr, ETeleportType::TeleportPhysics))
    {
        Exit();
        OutError = TEXT("The vehicle could not be placed in the garage.");
        return false;
    }
    TargetYaw = CurrentYaw = 37.f;
    TargetPitch = CurrentPitch = 14.f;
    TargetDistance = CurrentDistance = 830.f;
    UpdateCamera(0.f);
    UpdateLightingExposure();
    SetActorHiddenInGame(false);
    SetActorTickEnabled(true);
    return true;
}

void AADGarage::Exit()
{
    AADVehiclePawn* Car = Occupant.Get();
    Occupant.Reset();
    SetActorTickEnabled(false);
    SetActorHiddenInGame(true);
    if (!IsValid(Car) || Car->IsActorBeingDestroyed()) return;
    UPrimitiveComponent* Chassis = Cast<UPrimitiveComponent>(Car->GetRootComponent());
    Car->SetActorTransform(ReturnTransform, false, nullptr, ETeleportType::TeleportPhysics);
    Car->SetGarageMode(false);
    if (Chassis)
    {
        Chassis->SetSimulatePhysics(bReturnSimulating);
        if (bReturnSimulating)
        {
            Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
    }
    Car->GetPhysics()->ResetState();
    Car->GetPhysics()->SetComponentTickEnabled(bReturnPhysicsTick);
    Car->SetDrivingEnabled(bReturnDriving);
}

void AADGarage::Orbit(float YawDelta, float PitchDelta)
{
    if (!IsOccupied() || !FMath::IsFinite(YawDelta) || !FMath::IsFinite(PitchDelta)) return;
    TargetYaw = FMath::UnwindDegrees(TargetYaw + FMath::Clamp(YawDelta, -45.f, 45.f));
    TargetPitch = FMath::Clamp(TargetPitch + FMath::Clamp(PitchDelta, -20.f, 20.f), 8.f, 36.f);
}

void AADGarage::Zoom(float Delta)
{
    if (IsOccupied() && FMath::IsFinite(Delta)) TargetDistance = FMath::Clamp(TargetDistance + Delta, 610.f, 1040.f);
}

void AADGarage::UpdateCamera(float DeltaSeconds)
{
    const float Blend = 1.f - FMath::Exp(-10.f * FMath::Max(0.f, DeltaSeconds));
    CurrentYaw = FMath::UnwindDegrees(CurrentYaw + FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw) * Blend);
    CurrentPitch = FMath::Lerp(CurrentPitch, TargetPitch, Blend);
    CurrentDistance = FMath::Lerp(CurrentDistance, TargetDistance, Blend);
    const float Yaw = FMath::DegreesToRadians(CurrentYaw);
    const float Pitch = FMath::DegreesToRadians(CurrentPitch);
    const FVector Focus(0,0,110);
    const FVector Position = Focus + FVector(FMath::Cos(Yaw)*FMath::Cos(Pitch), FMath::Sin(Yaw)*FMath::Cos(Pitch), FMath::Sin(Pitch))*CurrentDistance;
    const FVector Right(FMath::Sin(Yaw),-FMath::Cos(Yaw),0);
    // Offset the optical center to leave space for the left-hand garage menu.
    const FVector Aim = Focus - Right * (CurrentDistance * .19f);
    Camera->SetRelativeLocationAndRotation(Position, (Aim - Position).Rotation());
}

void AADGarage::UpdateLightingExposure()
{
    // The garage is a controlled studio. Do not inherit the world EV and then
    // counter it with 2^-EV lamp scaling: the city uses a -10 EV daylight bias,
    // which previously drove these local lights above 1,000x their authored power.
    Camera->PostProcessSettings.AutoExposureBias = -1.3f;
    for (const FStudioLight& Light : StudioLights)
        if (Light.Component.IsValid()) Light.Component->SetIntensity(Light.BaseLumens * .5f);
}

void AADGarage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsOccupied()) { Exit(); return; }
    UpdateCamera(DeltaSeconds);
    // Keep the tires on the pad when a tuning preset changes unloaded ride height.
    AADVehiclePawn* Car = Occupant.Get();
    const FTransform Display = GetDisplayTransform();
    if (!Car->GetActorLocation().Equals(Display.GetLocation(), .1f))
        Car->SetActorTransform(Display, false, nullptr, ETeleportType::TeleportPhysics);
}

void AADGarage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Exit();
    Super::EndPlay(EndPlayReason);
}
