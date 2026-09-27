#include "Player/ADVehiclePawn.h"
#include "Audio/ADEngineSynthComponent.h"
#include "Core/ADGameMode.h"
#include "World/ADAtmosphere.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Settings/ADSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/SpringArmComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"
#include "Misc/Parse.h"

namespace
{
    struct FBodySection { float X, Width, Bottom, Shoulder; };

    float CatmullRom(float P0, float P1, float P2, float P3, float T)
    {
        const float T2 = T * T;
        const float T3 = T2 * T;
        return .5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2
            + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
    }

    FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
    {
        return FVector(CatmullRom(P0.X, P1.X, P2.X, P3.X, T), CatmullRom(P0.Y, P1.Y, P2.Y, P3.Y, T),
            CatmullRom(P0.Z, P1.Z, P2.Z, P3.Z, T));
    }

    void CreateLoft(UProceduralMeshComponent* Mesh, int32 Section, const TArray<FBodySection>& Sections,
        UMaterialInterface* Material)
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        if (!Mesh || Sections.Num() < 2) return;

        // Interpolate the authored body stations before tessellation. This keeps the
        // vehicle-data silhouettes while removing the stepped, eight-plane profile.
        TArray<FBodySection> SmoothSections;
        SmoothSections.Reserve((Sections.Num() - 1) * 4 + 1);
        for (int32 I = 0; I < Sections.Num() - 1; ++I)
        {
            const FBodySection& A = Sections[FMath::Max(0, I - 1)];
            const FBodySection& B = Sections[I];
            const FBodySection& C = Sections[I + 1];
            const FBodySection& D = Sections[FMath::Min(Sections.Num() - 1, I + 2)];
            for (int32 Step = 0; Step < 4; ++Step)
            {
                const float T = Step * .25f;
                SmoothSections.Add({
                    CatmullRom(A.X, B.X, C.X, D.X, T),
                    FMath::Max(1.f, CatmullRom(A.Width, B.Width, C.Width, D.Width, T)),
                    CatmullRom(A.Bottom, B.Bottom, C.Bottom, D.Bottom, T),
                    CatmullRom(A.Shoulder, B.Shoulder, C.Shoulder, D.Shoulder, T)
                });
            }
        }
        SmoothSections.Add(Sections.Last());

        constexpr int32 CrossSectionControlCount = 8;
        constexpr int32 CrossSectionSubdivisions = 4;
        constexpr int32 RingVertexCount = CrossSectionControlCount * CrossSectionSubdivisions;
        for (const FBodySection& S : SmoothSections)
        {
            const FVector ControlRing[CrossSectionControlCount] = {
                {S.X, -S.Width, S.Bottom}, {S.X, -S.Width, S.Shoulder - 8},
                {S.X, -S.Width * 0.86f, S.Shoulder}, {S.X, S.Width * 0.86f, S.Shoulder},
                {S.X, S.Width, S.Shoulder - 8}, {S.X, S.Width, S.Bottom},
                {S.X, S.Width * 0.83f, S.Bottom - 4}, {S.X, -S.Width * 0.83f, S.Bottom - 4}
            };
            for (int32 Control = 0; Control < CrossSectionControlCount; ++Control)
            {
                const FVector& P0 = ControlRing[(Control + CrossSectionControlCount - 1) % CrossSectionControlCount];
                const FVector& P1 = ControlRing[Control];
                const FVector& P2 = ControlRing[(Control + 1) % CrossSectionControlCount];
                const FVector& P3 = ControlRing[(Control + 2) % CrossSectionControlCount];
                for (int32 Step = 0; Step < CrossSectionSubdivisions; ++Step)
                {
                    const FVector Point = CatmullRom(P0, P1, P2, P3, Step * .25f);
                    const int32 RingIndex = Control * CrossSectionSubdivisions + Step;
                    Vertices.Add(Point);
                    UVs.Add(FVector2D(S.X / 450.0f, static_cast<float>(RingIndex) / RingVertexCount));
                }
            }
        }

        for (int32 I = 0; I < SmoothSections.Num() - 1; ++I)
        {
            for (int32 J = 0; J < RingVertexCount; ++J)
            {
                const int32 A = I * RingVertexCount + J, B = A + RingVertexCount;
                const int32 D = I * RingVertexCount + (J + 1) % RingVertexCount, C = D + RingVertexCount;
                // Unreal's front face and procedural normal convention is clockwise.
                Indices.Append({A, C, B, A, D, C});
            }
        }

        const int32 FrontCenter = Vertices.Num();
        const int32 RearCenter = FrontCenter + 1;
        const auto RingCenter = [&SmoothSections](const FBodySection& S)
        {
            return FVector(S.X, 0.f, (S.Bottom + S.Shoulder) * .5f);
        };
        Vertices.Add(RingCenter(SmoothSections[0]));
        Vertices.Add(RingCenter(SmoothSections.Last()));
        UVs.Add(FVector2D(SmoothSections[0].X / 450.0f, .5f));
        UVs.Add(FVector2D(SmoothSections.Last().X / 450.0f, .5f));
        const int32 RearRingStart = (SmoothSections.Num() - 1) * RingVertexCount;
        for (int32 J = 0; J < RingVertexCount; ++J)
        {
            const int32 Next = (J + 1) % RingVertexCount;
            Indices.Append({FrontCenter, Next, J});
            Indices.Append({RearCenter, RearRingStart + J, RearRingStart + Next});
        }

        TArray<FVector> Normals;
        TArray<FProcMeshTangent> Tangents;
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Indices, UVs, Normals, Tangents);
        Mesh->CreateMeshSection_LinearColor(Section, Vertices, Indices, Normals, UVs,
            TArray<FLinearColor>(), Tangents, false);
        Mesh->SetMaterial(Section, Material);
    }
}

AADVehiclePawn::AADVehiclePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    bReplicates=true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    SetMinNetUpdateFrequency(15.f);
    Chassis = CreateDefaultSubobject<UBoxComponent>(TEXT("Chassis"));
    SetRootComponent(Chassis);
    Chassis->SetBoxExtent(FVector(210, 85, 24));
    Chassis->SetCollisionProfileName(TEXT("PhysicsActor"));
    Chassis->SetSimulatePhysics(true);
    Chassis->BodyInstance.bUseCCD = true;
    Chassis->SetLinearDamping(0.02f);
    Chassis->SetAngularDamping(0.4f);
    VehiclePhysics = CreateDefaultSubobject<UADVehiclePhysicsComponent>(TEXT("VehiclePhysics"));
    VehicleEffects = CreateDefaultSubobject<UADVehicleEffectsComponent>(TEXT("VehicleEffects"));
    Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Coachwork"));
    Body->SetupAttachment(Chassis);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->bUseAsyncCooking = true;
    ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
    ChaseArm->SetupAttachment(Chassis);
    ChaseArm->TargetArmLength = 780;
    ChaseArm->SetRelativeLocation(FVector(0, 0, 100));
    ChaseArm->SetRelativeRotation(FRotator(-10, 0, 0));
    ChaseArm->bInheritRoll = false;
    ChaseArm->bInheritPitch = false;
    ChaseArm->bEnableCameraLag = true;
    ChaseArm->CameraLagSpeed = 9;
    ChaseArm->CameraLagMaxDistance = 60;
    ChaseArm->bUseCameraLagSubstepping = true;
    ChaseArm->bEnableCameraRotationLag = true;
    ChaseArm->CameraRotationLagSpeed = 7;
    ChaseArm->ProbeSize = 14;
    ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    ChaseCamera->SetupAttachment(ChaseArm);
    ChaseCamera->FieldOfView = 78;
    HoodCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HoodCamera"));
    HoodCamera->SetupAttachment(Chassis);
    // Keep a thin bonnet reference below the road without looking through the
    // lamp housings. The eye remains above the highest forward body section.
    HoodCamera->SetRelativeLocation(FVector(126, 0, 46));
    HoodCamera->FieldOfView = 80;
    HoodCamera->SetAutoActivate(false);
    CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    CockpitCamera->SetupAttachment(Chassis);
    CockpitCamera->SetRelativeLocation(FVector(76, -30, 67));
    CockpitCamera->FieldOfView = 84;
    CockpitCamera->SetAutoActivate(false);
    EngineAudio = CreateDefaultSubobject<UADEngineSynthComponent>(TEXT("EngineAudio"));
    EngineAudio->SetupAttachment(Chassis);
}

void AADVehiclePawn::BeginPlay()
{
    Super::BeginPlay();
    RecoveryTransform = GetActorTransform();
    bNetworkProbeEnabled=HasAuthority() && FParse::Param(FCommandLine::Get(),TEXT("AfterdarkNetHostProbe"));
    VehiclePhysics->Initialize(Chassis);
    if (VehiclePhysics->IsReady())
    {
        const FADVehicleDefinition& Definition=VehiclePhysics->GetDefinition();
        Chassis->SetBoxExtent(FVector(Definition.BodyLengthCm*.5f-18.f,Definition.BodyWidthCm*.5f-3.f,24.f),true);
    }
    BuildVehicle();
    for (TActorIterator<AADAtmosphere> It(GetWorld()); It; ++It) { Atmosphere=*It; break; }
    if (!HasAuthority()) { VehiclePhysics->SetComponentTickEnabled(false); SetPaintColor(NetworkPaint); }
    SetDrivingEnabled(false);
    if (!HasAuthority()) { OnRepTelemetry(); OnRepEffects(); }
}

void AADVehiclePawn::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    // A prior owner's input watchdog must never brake a newly possessed car.
    bRemoteInputControlled=false;
    LastRemoteInputSeconds=0.;
    LastRemoteActionSeconds=-1.;
    LastRemoteRecoverySeconds=-3.;
    VehiclePhysics->SetControls(0.f,1.f,0.f,true);
    VehicleEffects->SetNitrousHeld(false);
    if (HasActorBegunPlay() && GetNetMode()!=NM_Standalone)
        SetRaceAppearance(NetworkPaint,!IsLocallyControlled());
}

void AADVehiclePawn::UnPossessed()
{
    bRemoteInputControlled=false;
    SetDrivingEnabled(false);
    Super::UnPossessed();
}

void AADVehiclePawn::PawnClientRestart()
{
    Super::PawnClientRestart();
    // Controller ownership can arrive after the first telemetry notification.
    // Restore the owning driver's camera/audio even if the parked telemetry never changes.
    if (HasActorBegunPlay() && !HasAuthority()) SetRaceAppearance(NetworkPaint,!IsLocallyControlled());
}

void AADVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AADVehiclePawn,NetworkTelemetry);
    DOREPLIFETIME(AADVehiclePawn,NetworkEffects);
    DOREPLIFETIME(AADVehiclePawn,NetworkPaint);
}
void AADVehiclePawn::OnRepTelemetry()
{
    if (!HasActorBegunPlay()) return;
    VehiclePhysics->ReceiveNetworkTelemetry(NetworkTelemetry);
    const bool bRemote=!IsLocallyControlled();
    if (bRaceOpponent!=bRemote) SetRaceAppearance(NetworkPaint,bRemote);
    if (bRemote) SetDrivingEnabled(FMath::Abs(NetworkTelemetry.SpeedKmh)>1.f || NetworkTelemetry.Throttle>.01f);
}
void AADVehiclePawn::OnRepPaint() { if (HasActorBegunPlay()) SetPaintColor(NetworkPaint); }
void AADVehiclePawn::OnRepEffects()
{
    if (HasActorBegunPlay()) VehicleEffects->ReceiveNetworkState(NetworkEffects.NitrousFraction,NetworkEffects.Health,NetworkEffects.bBoosting);
}
void AADVehiclePawn::ServerDrive_Implementation(float Throttle,float Brake,float Steering,bool bHandbrake,bool bNitrous,bool bActive)
{
    if (!HasAuthority() || !IsPlayerControlled() || !FMath::IsFinite(Throttle) || !FMath::IsFinite(Brake) || !FMath::IsFinite(Steering)) return;
    LastRemoteInputSeconds=FPlatformTime::Seconds(); bRemoteInputControlled=true;
    const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    const bool bCanDrive=bActive && !bInGarage && VehiclePhysics->IsReady() && Chassis->IsSimulatingPhysics()
        && (!Mode || Mode->IsWorldReady());
    if (bDrivingEnabled!=bCanDrive) SetDrivingEnabled(bCanDrive);
    VehiclePhysics->SetControls(bCanDrive ? Throttle : 0.f,bCanDrive ? Brake : 1.f,bCanDrive ? Steering : 0.f,bHandbrake || !bCanDrive);
    VehicleEffects->SetNitrousHeld(bCanDrive && bNitrous);
    if (bNetworkProbeEnabled && !bNetworkProbeMovementReported && bCanDrive && Throttle>.2f)
    {
        const double Now=FPlatformTime::Seconds();
        if (NetworkProbeDriveStartedSeconds<0.)
        {
            NetworkProbeDriveStartedSeconds=Now;
            NetworkProbeStartPosition=GetActorLocation();
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_DRIVE_ACCEPTED: server accepted player throttle."));
        }
        else if (Now-NetworkProbeDriveStartedSeconds>=2.0
            && FVector::Dist2D(NetworkProbeStartPosition,GetActorLocation())>250.)
        {
            bNetworkProbeMovementReported=true;
            UE_LOG(LogTemp,Display,TEXT("AFTERDARK_NET_MOVEMENT_CONFIRMED: server-authoritative car moved %.0f cm."),
                FVector::Dist2D(NetworkProbeStartPosition,GetActorLocation()));
        }
    }
}
void AADVehiclePawn::ServerDrivingAction_Implementation(uint8 Action)
{
    if (!HasAuthority() || !IsPlayerControlled() || bInGarage || !bDrivingEnabled || Action>4) return;
    const double Now=FPlatformTime::Seconds();
    // Reliable RPCs may arrive in a burst. Bound the applied actions independently
    // of the client key repeat rate; recovery also gets its own slower cooldown.
    if (Now-LastRemoteActionSeconds<.1 || (Action==4 && Now-LastRemoteRecoverySeconds<3.)) return;
    LastRemoteActionSeconds=Now;
    if (Action==4)
    {
        LastRemoteRecoverySeconds=Now;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(ADNetworkRecovery),false,this);
        if (GetWorld()->OverlapBlockingTestByChannel(RecoveryTransform.GetLocation(),RecoveryTransform.GetRotation(),
            ECC_PhysicsBody,FCollisionShape::MakeBox(Chassis->GetScaledBoxExtent()),Query)) return;
    }
    switch (Action)
    {
    case 0: VehiclePhysics->ToggleTransmission(); break;
    case 1: VehiclePhysics->ShiftUp(); break;
    case 2: VehiclePhysics->ShiftDown(); break;
    case 3: VehiclePhysics->RequestReverse(); break;
    case 4: ResetVehicle(); break;
    default: break;
    }
}

UMaterialInterface* AADVehiclePawn::LoadSurface(const TCHAR* Path, FLinearColor Color, float Metallic)
{
    if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, Path)) { return Loaded; }
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Base) { Base = UMaterial::GetDefaultMaterial(MD_Surface); }
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
    Instance->SetVectorParameterValue(TEXT("Color"), Color);
    Instance->SetVectorParameterValue(TEXT("BaseColor"), Color);
    Instance->SetScalarParameterValue(TEXT("Metallic"), Metallic);
    FallbackMaterials.Add(Instance);
    return Instance;
}

UStaticMeshComponent* AADVehiclePawn::AddPiece(FName Name, FVector Location, FVector Scale,
    UMaterialInterface* Material, bool bCylinder, FRotator Rotation)
{
    UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this, Name);
    AddInstanceComponent(Piece);
    Piece->SetupAttachment(Chassis);
    Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bCylinder
        ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Cube.Cube")));
    Piece->SetRelativeLocation(Location);
    Piece->SetRelativeRotation(Rotation);
    Piece->SetRelativeScale3D(Scale);
    Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Piece->SetMaterial(0, Material);
    Piece->RegisterComponent();
    return Piece;
}

void AADVehiclePawn::BuildVehicle()
{
    UMaterialInterface* Paint = LoadSurface(TEXT("/Game/Velocity/Materials/M_Paint.M_Paint"), FLinearColor(0.04f, 0.21f, 0.25f), 0.8f);
    UMaterialInterface* Glass = LoadSurface(TEXT("/Game/Velocity/Materials/M_Glass.M_Glass"), FLinearColor(0.012f, 0.025f, 0.04f));
    UMaterialInterface* Rubber = LoadSurface(TEXT("/Game/Velocity/Materials/M_Rubber.M_Rubber"), FLinearColor(0.01f, 0.012f, 0.015f));
    UMaterialInterface* TireRubber = LoadSurface(TEXT("/Game/Velocity/Materials/M_Rubber_PBR.M_Rubber_PBR"), FLinearColor(0.018f, 0.020f, 0.023f));
    UMaterialInterface* Metal = LoadSurface(TEXT("/Game/Velocity/Materials/M_Metal.M_Metal"), FLinearColor(0.35f, 0.4f, 0.45f), 0.9f);
    UMaterialInterface* WheelMetal = LoadSurface(TEXT("/Game/Velocity/Materials/M_IndustrialMetal_PBR.M_IndustrialMetal_PBR"), FLinearColor(0.35f, 0.4f, 0.45f), 0.9f);
    UMaterialInterface* White = LoadSurface(TEXT("/Game/Velocity/Materials/M_EmissiveWhite.M_EmissiveWhite"), FLinearColor(1, 1, 1));
    UMaterialInterface* Red = LoadSurface(TEXT("/Game/Velocity/Materials/M_EmissiveRed.M_EmissiveRed"), FLinearColor(0.8f, 0.01f, 0.01f));

    BuildBody(VehiclePhysics->GetDefinition().BodyStyle,Paint,Glass);
    EngineAudio->SetCylinderCount(VehiclePhysics->GetDefinition().EngineCylinders);
    AddPiece(TEXT("Splitter"), FVector(205,0,-22), FVector(.58,1.78,.055), Rubber);
    AddPiece(TEXT("RearDiffuser"), FVector(-211,0,-24), FVector(.35,1.65,.10), Rubber);
    AddPiece(TEXT("FrontIntake"), FVector(226,0,-4), FVector(.04,.90,.15), Rubber);
    AddPiece(TEXT("RearLip"), FVector(-203,0,29), FVector(.22,1.60,.045), Paint);
    AddPiece(TEXT("Dashboard"), FVector(81,0,20), FVector(.25,1.42,.19), Rubber);
    for (int32 Side : {-1, 1})
    {
        const FString Suffix = Side < 0 ? TEXT("L") : TEXT("R");
        AddPiece(FName(TEXT("Sill") + Suffix), FVector(0,Side*88,-23), FVector(2.5,.08,.11), Rubber);
        AddPiece(FName(TEXT("Mirror") + Suffix), FVector(71,Side*93,31), FVector(.21,.17,.10), Paint);
        AddPiece(FName(TEXT("DoorHandle") + Suffix), FVector(-49,Side*85,17), FVector(.16,.025,.028), Metal);
        AddPiece(FName(TEXT("Seat") + Suffix), FVector(-23,Side*36,3), FVector(.55,.43,.18), Rubber);
        AddPiece(FName(TEXT("SeatBack") + Suffix), FVector(-48,Side*36,20), FVector(.13,.43,.54), Rubber, false, FRotator(-12,0,0));
        // Mount lenses on the front fascia below the bonnet. The former mount
        // protruded through the sloping hood and exposed emissive tops to the driver.
        AddPiece(FName(TEXT("FrontLamp") + Suffix), FVector(229.8,Side*49,-1), FVector(.035,.30,.037), White);
        BrakeLenses.Add(AddPiece(FName(TEXT("RearLamp") + Suffix), FVector(-222,Side*50,12), FVector(.03,.42,.041), Red));
        AddPiece(FName(TEXT("Exhaust") + Suffix), FVector(-224,Side*56,-22), FVector(.10,.10,.18), Metal, true, FRotator(90,0,0));
        USpotLightComponent* Headlight = NewObject<USpotLightComponent>(this, FName(TEXT("Headlight") + Suffix));
        AddInstanceComponent(Headlight);
        Headlight->SetupAttachment(Chassis);
        Headlight->SetRelativeLocation(FVector(234, Side*49, -1));
        Headlight->SetRelativeRotation(FRotator(-5, Side*3, 0));
        Headlight->SetIntensityUnits(ELightUnits::Lumens);
        Headlight->SetIntensity(1900);
        Headlight->SetLightColor(FLinearColor(0.84f,0.91f,1));
        Headlight->SetAttenuationRadius(7000);
        Headlight->SetInnerConeAngle(15);
        Headlight->SetOuterConeAngle(32);
        Headlight->SetCastShadows(false);
        Headlight->RegisterComponent();
        UPointLightComponent* Tail = NewObject<UPointLightComponent>(this, FName(TEXT("BrakeLight") + Suffix));
        AddInstanceComponent(Tail);
        Tail->SetupAttachment(Chassis);
        Tail->SetRelativeLocation(FVector(-236,Side*50,12));
        Tail->SetLightColor(FLinearColor(1,.012f,.003f));
        Tail->SetIntensity(20);
        Tail->SetAttenuationRadius(230);
        Tail->SetCastShadows(false);
        Tail->RegisterComponent();
        BrakeLights.Add(Tail);
    }
    // An open rim preserves road visibility from the eye position. A solid
    // cylinder here obscures the dashboard and most of the lower windshield.
    SteeringWheel = NewObject<USceneComponent>(this, TEXT("SteeringWheel"));
    AddInstanceComponent(SteeringWheel);
    SteeringWheel->SetupAttachment(Chassis);
    SteeringWheel->SetRelativeLocation(FVector(63,-36,26));
    SteeringWheel->SetRelativeRotation(FRotator(75,0,0));
    SteeringWheel->RegisterComponent();
    for (int32 Segment = 0; Segment < 16; ++Segment)
    {
        const float Angle = Segment*22.5f;
        const float Radians = FMath::DegreesToRadians(Angle);
        auto* Rim = AddPiece(FName(*FString::Printf(TEXT("SteeringRim%d"), Segment)),
            FVector(12.5f*FMath::Cos(Radians),12.5f*FMath::Sin(Radians),0),
            FVector(.061,.025,.025),Rubber,false,FRotator(0,Angle+90,0));
        Rim->AttachToComponent(SteeringWheel,FAttachmentTransformRules::KeepRelativeTransform);
    }
    auto* Hub = AddPiece(TEXT("SteeringHub"),FVector::ZeroVector,FVector(.075,.075,.03),Metal);
    Hub->AttachToComponent(SteeringWheel,FAttachmentTransformRules::KeepRelativeTransform);
    for (int32 Spoke = 0; Spoke < 3; ++Spoke)
    {
        const float Angle = Spoke*120.f+30.f;
        const float Radians = FMath::DegreesToRadians(Angle);
        auto* Bar = AddPiece(FName(*FString::Printf(TEXT("SteeringSpoke%d"), Spoke)),
            FVector(6.25f*FMath::Cos(Radians),6.25f*FMath::Sin(Radians),0),
            FVector(.105,.021,.018),Metal,false,FRotator(0,Angle,0));
        Bar->AttachToComponent(SteeringWheel,FAttachmentTransformRules::KeepRelativeTransform);
    }
    // Functional wipers bring the wet-weather cockpit to life. The sweep rate
    // follows current precipitation and parks cleanly as the rain stops.
    for (int32 Side : {-1,1})
    {
        const FString Suffix=Side<0 ? TEXT("L") : TEXT("R");
        USceneComponent* Pivot=NewObject<USceneComponent>(this,*FString(TEXT("WiperPivot")+Suffix));
        AddInstanceComponent(Pivot);
        Pivot->SetupAttachment(Chassis);
        Pivot->SetRelativeLocation(FVector(105.f,Side*15.f,40.f));
        Pivot->RegisterComponent();
        WiperPivots.Add(Pivot);
        UStaticMeshComponent* Blade=AddPiece(FName(*FString(TEXT("WiperBlade")+Suffix)),
            FVector(0.f,Side*27.f,0.f),FVector(.018f,.30f,.014f),Rubber);
        Blade->AttachToComponent(Pivot,FAttachmentTransformRules::KeepRelativeTransform);
    }
    AddPiece(TEXT("InstrumentCluster"), FVector(70,-36,32), FVector(.02,.34,.12), Metal);
    for (int32 I = 0; I < 4; ++I)
    {
        USceneComponent* Pivot = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("WheelPivot%d"),I)));
        AddInstanceComponent(Pivot);
        Pivot->SetupAttachment(Chassis);
        Pivot->SetRelativeLocation(FVector(I < 2 ? 135 : -135, (I % 2 == 0 ? -80 : 80), -38));
        Pivot->RegisterComponent();
        WheelPivots.Add(Pivot);
        UStaticMeshComponent* Tire = AddPiece(FName(*FString::Printf(TEXT("Tire%d"),I)), FVector::ZeroVector,
            FVector(.68,.68,.24), TireRubber,true,FRotator(0,0,90));
        Tire->AttachToComponent(Pivot,FAttachmentTransformRules::KeepRelativeTransform);
        for (int32 Face : {-1,1})
        {
            UStaticMeshComponent* Rim = AddPiece(FName(*FString::Printf(TEXT("Rim%d_%d"),I,Face)),
                FVector(0,Face*12.1,0),FVector(.49,.49,.025),WheelMetal,true,FRotator(0,0,90));
            Rim->AttachToComponent(Pivot,FAttachmentTransformRules::KeepRelativeTransform);
            for (int32 Spoke = 0; Spoke < 5; ++Spoke)
            {
                const float Angle = Spoke*72;
                UStaticMeshComponent* Bar = AddPiece(FName(*FString::Printf(TEXT("Spoke%d_%d_%d"),I,Face,Spoke)),
                    FVector(0,Face*13.5,0),FVector(.43,.03,.04),Rubber,false,FRotator(Angle,0,0));
                Bar->AttachToComponent(Pivot,FAttachmentTransformRules::KeepRelativeTransform);
            }
        }
    }
}

void AADVehiclePawn::BuildBody(const FString& Style,UMaterialInterface* Paint,UMaterialInterface* Glass)
{
    // Five original development silhouettes share the same data-driven wheelbase model.
    // Only mesh sections change when browsing; components/materials are reused.
    PresentedBodyStyle=Style;
    if (Style==TEXT("hatchback"))
    {
        CreateLoft(Body,0,{{-224,74,-15,18},{-210,86,-20,34},{-140,88,-21,35},
            {-72,85,-22,33},{55,84,-22,29},{140,90,-20,24},{205,85,-17,14},{228,68,-12,5}},Paint);
        CreateLoft(Body,1,{{-197,75,34,46},{-158,68,33,89},{42,63,28,90},{121,72,25,35}},Glass);
        CreateLoft(Body,2,{{-162,62,85,91},{39,58,85,92}},Paint);
    }
    else if (Style==TEXT("sedan"))
    {
        CreateLoft(Body,0,{{-224,74,-15,17},{-210,88,-20,32},{-140,93,-21,34},
            {-72,88,-22,32},{55,88,-22,30},{140,94,-20,25},{205,85,-17,14},{228,68,-12,5}},Paint);
        CreateLoft(Body,1,{{-154,74,31,35},{-93,67,32,83},{42,64,29,83},{121,74,25,35}},Glass);
        CreateLoft(Body,2,{{-98,61,77,84},{40,58,77,85}},Paint);
    }
    else if (Style==TEXT("muscle"))
    {
        CreateLoft(Body,0,{{-242,76,-13,14},{-228,94,-20,31},{-160,96,-22,34},
            {-50,96,-22,35},{40,93,-22,37},{145,92,-20,35},{218,87,-17,24},{245,70,-12,7}},Paint);
        CreateLoft(Body,1,{{-120,72,30,35},{-83,68,32,82},{-20,62,30,84},{42,74,28,40}},Glass);
        CreateLoft(Body,2,{{-78,59,77,83},{-19,57,78,85}},Paint);
    }
    else if (Style==TEXT("hypercar"))
    {
        CreateLoft(Body,0,{{-234,76,-13,7},{-220,91,-21,22},{-150,98,-24,28},
            {-80,94,-24,29},{0,90,-24,28},{100,100,-23,25},{180,102,-20,18},{230,80,-15,7}},Paint);
        CreateLoft(Body,1,{{-120,70,30,40},{-80,65,31,72},{20,60,30,74},{75,78,27,38}},Glass);
        CreateLoft(Body,2,{{-55,57,68,74},{17,55,69,76}},Paint);
    }
    else
    {
        CreateLoft(Body,0,{{-224,70,-15,10},{-210,88,-20,24},{-140,93,-21,33},
            {-72,85,-22,30},{55,84,-22,27},{140,94,-20,24},{205,85,-17,14},{228,68,-12,5}},Paint);
        CreateLoft(Body,1,{{-120,70,28,38},{-58,64,29,72},{48,61,27,73},{121,72,25,35}},Glass);
        CreateLoft(Body,2,{{-65,57,67,73},{45,55,69,75}},Paint);
    }
}

bool AADVehiclePawn::PreviewGarageVehicle(const FADVehicleDefinition& Definition,FString& OutError)
{
    OutError.Reset();
    if (GetNetMode()!=NM_Standalone || Chassis->IsSimulatingPhysics())
    { OutError=TEXT("Freeze the offline vehicle before previewing a different body."); return false; }
    if (!Definition.Validate(OutError)) return false;
    if (PresentedBodyStyle!=Definition.BodyStyle)
        BuildBody(Definition.BodyStyle,Body->GetMaterial(0),Body->GetMaterial(1));
    return true;
}

bool AADVehiclePawn::ApplyGarageVehicle(const FADVehicleDefinition& Stock,const FADVehicleDefinition& Effective,FString& OutError)
{
    if (!VehiclePhysics->ApplyGarageVehicle(Stock,Effective,OutError)) return false;
    // The physics transaction has validated both definitions; preview cannot
    // fail under the same offline/frozen preconditions.
    PreviewGarageVehicle(Stock,OutError);
    EngineAudio->SetCylinderCount(Stock.EngineCylinders);
    return true;
}

void AADVehiclePawn::SetDrivingEnabled(bool bEnabled)
{
    bDrivingRequested = bEnabled;
    const AADGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AADGameMode>() : nullptr;
    bDrivingEnabled = bEnabled && !bInGarage && VehiclePhysics->IsReady() && (!Mode || Mode->IsWorldReady());
    if (!VehiclePhysics->IsReady() || (Mode && !Mode->IsWorldReady()))
    {
        // Required data failures must hold the chassis safely in place. Without
        // road collision, gravity otherwise causes an endless recovery/reload loop.
        Chassis->SetSimulatePhysics(false);
    }
    VehiclePhysics->SetControls(0, bDrivingEnabled ? 0 : 1, 0, !bDrivingEnabled);
    if (bDrivingEnabled) { EngineAudio->Start(); }
    else { EngineAudio->Stop(); }
    if (!bDrivingEnabled) VehicleEffects->SetNitrousHeld(false);
}

void AADVehiclePawn::ResetVehicle()
{
    if (!HasAuthority() || bInGarage) return;
    Chassis->SetSimulatePhysics(true);
    Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Chassis->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    SetActorTransform(RecoveryTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (!VehiclePhysics->IsReady()) { VehiclePhysics->Initialize(Chassis); }
    else { VehiclePhysics->ResetState(); }
    // Preserve session intent across a temporary data failure, so a successful
    // explicit recovery restores controls, chase camera and engine sound together.
    SetDrivingEnabled(bDrivingRequested);
}

void AADVehiclePawn::CycleCamera()
{
    CameraMode = static_cast<EADCameraMode>((static_cast<uint8>(CameraMode) + 1) % 3);
    ChaseCamera->SetActive(CameraMode == EADCameraMode::Chase);
    HoodCamera->SetActive(CameraMode == EADCameraMode::Hood);
    CockpitCamera->SetActive(CameraMode == EADCameraMode::Cockpit);
}

bool AADVehiclePawn::PlaceForRace(const FTransform& Transform)
{
    if (!HasAuthority() || bInGarage || Transform.ContainsNaN()) return false;
    RecoveryTransform=Transform;
    Chassis->SetSimulatePhysics(true);
    Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
    if (!VehiclePhysics->IsReady()) VehiclePhysics->Initialize(Chassis);
    else VehiclePhysics->ResetState();
    SetDrivingEnabled(true);
    return IsDrivingEnabled();
}

void AADVehiclePawn::SetRaceAppearance(FLinearColor Color,bool bOpponent)
{
    bRaceOpponent=bOpponent;
    ChaseArm->SetComponentTickEnabled(!bOpponent && !bInGarage);
    SetPaintColor(Color);
    if (bOpponent)
    {
        ChaseArm->SetComponentTickEnabled(false);
        EngineAudio->Stop();
        EngineAudio->bAllowSpatialization=true;
        EngineAudio->bOverrideAttenuation=true;
        EngineAudio->AttenuationOverrides.bAttenuate=true;
        EngineAudio->AttenuationOverrides.bSpatialize=true;
        EngineAudio->AttenuationOverrides.AttenuationShape=EAttenuationShape::Sphere;
        EngineAudio->AttenuationOverrides.AttenuationShapeExtents=FVector(300,0,0);
        EngineAudio->AttenuationOverrides.FalloffDistance=6000;
        EngineAudio->SetVolumeMultiplier(.55f);
        if (bDrivingEnabled) EngineAudio->Start();
    }
    else
    {
        EngineAudio->bAllowSpatialization=false;
        EngineAudio->bOverrideAttenuation=false;
        EngineAudio->SetVolumeMultiplier(1.f);
    }
}

void AADVehiclePawn::SetPaintColor(FLinearColor Color)
{
    if (HasAuthority()) NetworkPaint=Color;
    UMaterialInterface* Original=Body->GetMaterial(0);
    if (Original && !RacePaint)
    {
        RacePaint=UMaterialInstanceDynamic::Create(Original,this);
        TInlineComponentArray<UMeshComponent*> Meshes(this);
        for (auto* Mesh : Meshes)
            for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
                if (Mesh->GetMaterial(Slot)==Original) Mesh->SetMaterial(Slot,RacePaint);
    }
    if (RacePaint) RacePaint->SetVectorParameterValue(TEXT("BaseColor"),Color);
}

void AADVehiclePawn::SetGarageMode(bool bEnabled)
{
    bInGarage = bEnabled;
    if (bInGarage) SetDrivingEnabled(false);
    ChaseArm->SetComponentTickEnabled(!bInGarage && !bRaceOpponent);
}

void AADVehiclePawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FVector Position=GetActorLocation();
    if (HasAuthority())
    {
        NetworkTelemetry=VehiclePhysics->GetTelemetry();
        NetworkEffects.NitrousFraction=VehicleEffects->GetNitrousFraction();
        NetworkEffects.Health=VehicleEffects->GetHealth();
        NetworkEffects.bBoosting=VehicleEffects->IsBoosting();
        // Lost input/connection cannot leave a remote accelerator latched down.
        if (bRemoteInputControlled && FPlatformTime::Seconds()-LastRemoteInputSeconds>.35)
        {
            SetDrivingEnabled(false);
            bRemoteInputControlled=false;
        }
    }
    else VehiclePhysics->AdvanceRemoteWheels(DeltaSeconds);
    const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    const FVector2D Bounds=Mode ? Mode->GetDriveBounds()+FVector2D(1000,1000) : FVector2D(101000,81000);
    if (HasAuthority() && !bInGarage && Chassis->IsSimulatingPhysics() && (Position.Z < -1500 || FMath::Abs(Position.X)>Bounds.X || FMath::Abs(Position.Y)>Bounds.Y))
    {
        if (OnRecoveryRequested.IsBound()) OnRecoveryRequested.Broadcast(this);
        else ResetVehicle();
    }
    UpdatePresentation(DeltaSeconds);
}

void AADVehiclePawn::UpdatePresentation(float DeltaSeconds)
{
    const FADVehicleTelemetry& State = VehiclePhysics->GetTelemetry();
    for (int32 I = 0; I < WheelPivots.Num(); ++I)
    {
        WheelPivots[I]->SetRelativeLocation(VehiclePhysics->GetWheelLocalPosition(I));
        WheelPivots[I]->SetRelativeRotation(FRotator(VehiclePhysics->GetWheelSpinDegrees(I),
            I < 2 ? VehiclePhysics->GetSteeringDegrees() : 0, 0));
    }
    if (SteeringWheel) { SteeringWheel->SetRelativeRotation(FRotator(75,0,State.Steering*170)); }
    const bool bBraking = bDrivingEnabled && State.Brake > 0.02f;
    for (UPointLightComponent* Light : BrakeLights) { Light->SetIntensity(bBraking ? 180 : 20); }
    if (Atmosphere.IsValid() && !WiperPivots.IsEmpty())
    {
        const float Rain=Atmosphere->GetRainIntensity();
        if (Rain>.035f) WiperPhase=FMath::Fmod(WiperPhase+DeltaSeconds*FMath::Lerp(.7f,1.6f,Rain),1.f);
        else WiperPhase=FMath::FInterpTo(WiperPhase,0.f,DeltaSeconds,.35f);
        const float Sweep=Rain>.035f ? FMath::Sin(WiperPhase*2.f*PI)*34.f : 0.f;
        for (int32 Index=0;Index<WiperPivots.Num();++Index)
            WiperPivots[Index]->SetRelativeRotation(FRotator(0.f,0.f,Sweep*(Index==0 ? 1.f : -1.f)));
    }
    EngineAudio->SetEngineTargets(State.Rpm,State.Throttle,State.SpeedKmh,CameraMode == EADCameraMode::Cockpit);
    if (bRaceOpponent || bInGarage) return; // Only the local player's driving camera needs presentation updates.
    const float SpeedAlpha = FMath::Clamp(FMath::Abs(State.SpeedKmh)/250.0f,0.0f,1.0f);
    const auto* Settings=GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    const float FovEffect=Settings && !Settings->UsesSpeedFov() ? 0.f : SpeedAlpha*8.f+(VehicleEffects->IsBoosting() ? 3.f : 0.f);
    ChaseCamera->SetFieldOfView(FMath::FInterpTo(ChaseCamera->FieldOfView,78+FovEffect,DeltaSeconds,3));
    if (!bDrivingEnabled)
    {
        ShowcaseTime += DeltaSeconds;
        // Frame the parked car to the right of the menu, with its wheels clear
        // of the bottom control hints. SocketOffset participates in arm collision.
        ChaseArm->TargetArmLength = 740;
        ChaseArm->SocketOffset = FVector(0, -180, 0);
        ChaseArm->SetRelativeLocation(FVector(0, 0, 35));
        ChaseArm->SetRelativeRotation(FRotator(-15,28 + FMath::Sin(ShowcaseTime*.11f)*10,0));
    }
    else
    {
        ChaseArm->TargetArmLength = 780;
        ChaseArm->SocketOffset = FVector::ZeroVector;
        ChaseArm->SetRelativeLocation(FVector(0, 0, 120));
        ChaseArm->SetRelativeRotation(FRotator(-10,0,0));
    }
}

void AADVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EngineAudio->Stop();
    Super::EndPlay(EndPlayReason);
}
