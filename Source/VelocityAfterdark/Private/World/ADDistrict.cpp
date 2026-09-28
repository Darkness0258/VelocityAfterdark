#include "World/ADDistrict.h"
#include "World/ADPhysicalSurface.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    bool ReadVector2(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector2D& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        double X = 0., Y = 0.;
        if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 2 ||
            !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y) ||
            !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
        Out = FVector2D(X, Y);
        return true;
    }

    bool ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Min, double Max, double& Out)
    {
        return Object->TryGetNumberField(Key, Out) && FMath::IsFinite(Out) && Out >= Min && Out <= Max;
    }

    bool InsideRoad(const FVector2D& P, const FADRoadSegment& Road, double Padding)
    {
        return P.X >= FMath::Min(Road.Start.X, Road.End.X) - Padding &&
            P.X <= FMath::Max(Road.Start.X, Road.End.X) + Padding &&
            P.Y >= FMath::Min(Road.Start.Y, Road.End.Y) - Padding &&
            P.Y <= FMath::Max(Road.Start.Y, Road.End.Y) + Padding;
    }
}

AADDistrict::AADDistrict()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DistrictRoot"));
    RootComponent->SetMobility(EComponentMobility::Static);
    Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
    Moon->SetupAttachment(RootComponent);
    Moon->SetMobility(EComponentMobility::Movable);
    Moon->SetRelativeRotation(FRotator(-24.f, -38.f, 0.f));
    Moon->SetLightColor(FLinearColor(0.47f, 0.62f, 1.f));
    Moon->SetAtmosphereSunLight(true);
    Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("NightSky"));
    Sky->SetupAttachment(RootComponent);
    Sky->SetMobility(EComponentMobility::Movable);
    // A stable ambient probe keeps the SM5 path readable without Lumen. Replace
    // this engine probe with a district-authored capture during the art pass.
    static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientProbe(
        TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
    Sky->SourceType = SLS_SpecifiedCubemap;
    Sky->SetCubemap(AmbientProbe.Object);
    // Keep a cool night fill while lifting unlit facades enough to read their
    // PBR response on the project's SM5 path (there is no baked/Lumen bounce).
    Sky->SetLightColor(FLinearColor(.58f, .70f, .96f));
    Sky->SetRealTimeCaptureEnabled(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ParkedCarMesh(
        TEXT("/Game/Velocity/External/CityCarStaticFinal/SM_CC0_CityCar.SM_CC0_CityCar"));
    if (ParkedCarMesh.Succeeded()) ParkedCityCarMesh = ParkedCarMesh.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> AsphaltMaterial(
        TEXT("/Game/Velocity/Materials/M_Asphalt_PolyHaven.M_Asphalt_PolyHaven"));
    if (AsphaltMaterial.Succeeded()) DetailedAsphaltMaterial = AsphaltMaterial.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BuildingMaterial(
        TEXT("/Game/Velocity/Materials/M_Building_PBR_V3.M_Building_PBR_V3"));
    if (BuildingMaterial.Succeeded()) DetailedBuildingMaterial = BuildingMaterial.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ConcreteMaterial(
        TEXT("/Game/Velocity/Materials/M_Concrete_PBR_V3.M_Concrete_PBR_V3"));
    if (ConcreteMaterial.Succeeded()) DetailedConcreteMaterial = ConcreteMaterial.Object;
    Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
    Atmosphere->SetupAttachment(RootComponent);
    Atmosphere->SetSkyLuminanceFactor(FLinearColor(0.08f, 0.12f, 0.25f));
    Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HarborMist"));
    Fog->SetupAttachment(RootComponent);
    Fog->SetFogInscatteringColor(FLinearColor(0.022f, 0.034f, 0.057f));
    Fog->SetStartDistance(4000.f);
    Fog->SetFogMaxOpacity(.55f);
    PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("NightGrade"));
    PostProcess->SetupAttachment(RootComponent);
    PostProcess->bUnbound = true;
    // Explicit manual exposure avoids the legacy zero-brightness clamp, which
    // can amplify the entire night scene to white when luminance-range defaults vary.
    PostProcess->Settings.bOverride_AutoExposureMethod = true;
    PostProcess->Settings.AutoExposureMethod = AEM_Manual;
    PostProcess->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    PostProcess->Settings.AutoExposureApplyPhysicalCameraExposure = false;
    PostProcess->Settings.bOverride_AutoExposureBias = true;
    PostProcess->Settings.AutoExposureBias = 0.f;
    PostProcess->Settings.bOverride_BloomIntensity = true;
    PostProcess->Settings.BloomIntensity = 0.2f;
    // SSR is already the configured SM5 reflection method. Use a restrained
    // quality budget so wet-road and clear-coat highlights have a readable
    // response without defaulting to the highest sampling cost.
    PostProcess->Settings.bOverride_ScreenSpaceReflectionQuality = true;
    PostProcess->Settings.ScreenSpaceReflectionQuality = 35.f;
    PostProcess->Settings.bOverride_ScreenSpaceReflectionIntensity = true;
    PostProcess->Settings.ScreenSpaceReflectionIntensity = 62.f;
}

void AADDistrict::BeginPlay()
{
    Super::BeginPlay();
    if (!LoadDefinition())
    {
        UE_LOG(LogTemp, Error, TEXT("Dockside load failed: %s"), *LoadError);
        return;
    }
    Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (!Cube || !Cylinder)
    {
        LoadError = TEXT("Engine basic shape assets are missing from the build.");
        return;
    }
    if (!LoadDistrictArt())
    {
        UE_LOG(LogTemp, Error, TEXT("Dockside art load failed: %s"), *LoadError);
        return;
    }
    const TCHAR* Required[] = { TEXT("M_Asphalt"), TEXT("M_Concrete"), TEXT("M_Building"),
        TEXT("M_WindowWarm"), TEXT("M_WindowCool"), TEXT("M_Metal"), TEXT("M_RoadMarking"),
        TEXT("M_EmissiveWhite"), TEXT("M_EmissiveRed"), TEXT("M_ContainerBlue"),
        TEXT("M_ContainerRust"), TEXT("M_Water"), TEXT("M_RoadYellow") };
    for (const TCHAR* Name : Required)
    {
        const FString Path = FString::Printf(TEXT("/Game/Velocity/Materials/%s.%s"), Name, Name);
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
        if (!Material)
        {
            LoadError = FString::Printf(TEXT("Missing %s. Run Scripts/Editor/bootstrap_content.py in Unreal Editor."), Name);
            UE_LOG(LogTemp, Error, TEXT("%s"), *LoadError);
            return;
        }
        Materials.Add(FName(Name), Material);
    }
    if (DetailedAsphaltMaterial) Materials.Add(TEXT("M_Asphalt"), DetailedAsphaltMaterial);
    if (DetailedBuildingMaterial) Materials.Add(TEXT("M_Building"), DetailedBuildingMaterial);
    if (DetailedConcreteMaterial) Materials.Add(TEXT("M_Concrete"), DetailedConcreteMaterial);
    AddBox(TEXT("M_Concrete"), FVector(0, 0, -105), FVector(GroundHalfExtent.X * 2., GroundHalfExtent.Y * 2., 200), true);
    BuildRoads();
    BuildBlocks();
    BuildStreetFurniture();
    for (const auto& Pair : Batches)
    {
        Pair.Value->bAutoRebuildTreeOnInstanceChanges = true;
        Pair.Value->BuildTreeIfOutdated(false, true);
    }
    bReady = true;
    UE_LOG(LogTemp, Display, TEXT("Dockside ready: %.0fm roads, %d instance batches, %d imported building/prop instances."),
        RoadLengthMeters, Batches.Num(), GetImportedDistrictMeshInstanceCount());
}

bool AADDistrict::LoadDistrictArt()
{
    const auto LoadFamily = [this](const TCHAR* Directory,const TArray<FString>& Names,
        TArray<TObjectPtr<UStaticMesh>>& Destination)
    {
        for (const FString& Name:Names)
        {
            const FString Path=FString::Printf(TEXT("%s/%s.%s"),Directory,*Name,*Name);
            UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
            if (!Mesh)
            {
                LoadError=FString::Printf(TEXT("Required Dockside city-kit mesh is missing: %s"),*Path);
                return false;
            }
            Destination.Add(Mesh);
        }
        return !Destination.IsEmpty();
    };
    const TCHAR* Commercial=TEXT("/Game/Velocity/External/KenneyCityKitCommercial");
    const TCHAR* Industrial=TEXT("/Game/Velocity/External/KenneyCityKitIndustrial");
    if (!LoadFamily(Commercial,
            {TEXT("SM_Kenney_Commercial_SkyscraperA"),TEXT("SM_Kenney_Commercial_SkyscraperB"),
             TEXT("SM_Kenney_Commercial_SkyscraperC"),TEXT("SM_Kenney_Commercial_SkyscraperD"),
             TEXT("SM_Kenney_Commercial_SkyscraperE")},DowntownTowerMeshes)
        || !LoadFamily(Commercial,
            {TEXT("SM_Kenney_Commercial_BuildingA"),TEXT("SM_Kenney_Commercial_BuildingD"),
             TEXT("SM_Kenney_Commercial_BuildingH"),TEXT("SM_Kenney_Commercial_BuildingN")},DowntownMidriseMeshes)
        || !LoadFamily(Industrial,
            {TEXT("SM_Kenney_Industrial_BuildingA"),TEXT("SM_Kenney_Industrial_BuildingD"),
             TEXT("SM_Kenney_Industrial_BuildingH"),TEXT("SM_Kenney_Industrial_BuildingM"),
             TEXT("SM_Kenney_Industrial_BuildingQ"),TEXT("SM_Kenney_Industrial_BuildingT")},IndustrialWarehouseMeshes)
        || !LoadFamily(Industrial,
            {TEXT("SM_Kenney_Industrial_ContainerA"),TEXT("SM_Kenney_Industrial_ContainerC")},ContainerMeshes))
        return false;
    TrafficSignalMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Velocity/External/KenneyCityKitRoads/SM_Kenney_Roads_TrafficLight.SM_Kenney_Roads_TrafficLight"));
    if (!TrafficSignalMesh)
    {
        LoadError=TEXT("Required Dockside road signal mesh is missing.");
        return false;
    }
    return true;
}

int32 AADDistrict::GetImportedDistrictMeshInstanceCount() const
{
    int32 Count=0;
    for (const auto& Entry:Batches)
        if (IsValid(Entry.Value) && IsValid(Entry.Value->GetStaticMesh())
            && Entry.Value->GetStaticMesh()->GetName().StartsWith(TEXT("SM_Kenney_")))
            Count+=Entry.Value->GetInstanceCount();
    return Count;
}

bool AADDistrict::LoadDefinition()
{
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/World/dockside.json"));
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (!FFileHelper::LoadFileToString(Json, *Path) || Json.Len() > 256 * 1024 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
    {
        LoadError = TEXT("Cannot read a valid dockside.json (maximum 256 KB).");
        return false;
    }
    const auto Fail = [this](const TCHAR* Error) { LoadError = Error; return false; };
    double Number = 0.;
    FString Units;
    if (!ReadNumber(Object, TEXT("schemaVersion"), 1, 1, Number) ||
        !Object->TryGetStringField(TEXT("units"), Units) || Units != TEXT("centimeters") ||
        !ReadVector2(Object, TEXT("groundHalfExtent"), GroundHalfExtent) ||
        GroundHalfExtent.X < 10000 || GroundHalfExtent.Y < 10000 ||
        GroundHalfExtent.X > 100000 || GroundHalfExtent.Y > 100000)
        return Fail(TEXT("Unsupported schema, units, or ground bounds."));
    if (!ReadNumber(Object, TEXT("roadWidth"), 1200, 4000, Number)) return Fail(TEXT("Invalid roadWidth."));
    RoadWidth = static_cast<float>(Number);
    if (!ReadNumber(Object, TEXT("seed"), 0, 2147483647, Number)) return Fail(TEXT("Invalid seed."));
    Seed = static_cast<int32>(Number);

    const TArray<TSharedPtr<FJsonValue>>* RoadValues = nullptr;
    if (!Object->TryGetArrayField(TEXT("roads"), RoadValues) || RoadValues->IsEmpty() || RoadValues->Num() > 32)
        return Fail(TEXT("District requires between 1 and 32 roads."));
    TSet<FString> RoadIds;
    for (const auto& Value : *RoadValues)
    {
        if (Value->Type != EJson::Object) return Fail(TEXT("Road must be an object."));
        const auto RoadObject = Value->AsObject();
        FADRoadSegment Road;
        if (!RoadObject->TryGetStringField(TEXT("id"), Road.Id) || Road.Id.IsEmpty() || RoadIds.Contains(Road.Id) ||
            !ReadVector2(RoadObject, TEXT("start"), Road.Start) || !ReadVector2(RoadObject, TEXT("end"), Road.End))
            return Fail(TEXT("Road has invalid coordinates or duplicate id."));
        const FVector2D Delta = Road.End - Road.Start;
        if ((FMath::Abs(Delta.X) > 0.1 && FMath::Abs(Delta.Y) > 0.1) || Delta.Size() < 2000.)
            return Fail(TEXT("Phase 1 roads must be axis aligned and at least 20m long."));
        for (const FVector2D P : { Road.Start, Road.End })
            if (FMath::Abs(P.X) + RoadWidth > GroundHalfExtent.X || FMath::Abs(P.Y) + RoadWidth > GroundHalfExtent.Y)
                return Fail(TEXT("Road extends outside the safe ground bounds."));
        RoadIds.Add(Road.Id);
        Roads.Add(Road);
        RoadLengthMeters += static_cast<float>(Delta.Size() / 100.);
    }
    if (RoadLengthMeters > 12000 || !IsOnRoad(FVector2D(0, 400)))
        return Fail(TEXT("Road budget exceeded or player spawn is off-road."));

    const TArray<TSharedPtr<FJsonValue>>* BlockValues = nullptr;
    if (!Object->TryGetArrayField(TEXT("blocks"), BlockValues) || BlockValues->Num() > 32)
        return Fail(TEXT("Invalid blocks array."));
    int32 BuildingCount = 0;
    for (const auto& Value : *BlockValues)
    {
        if (Value->Type != EJson::Object) return Fail(TEXT("Block must be an object."));
        const auto B = Value->AsObject();
        FADDistrictBlock Block;
        if (!B->TryGetStringField(TEXT("id"), Block.Id) || !B->TryGetStringField(TEXT("kind"), Block.Kind) ||
            !ReadVector2(B, TEXT("min"), Block.Min) || !ReadVector2(B, TEXT("max"), Block.Max) ||
            Block.Max.X - Block.Min.X < 2000 || Block.Max.Y - Block.Min.Y < 2000)
            return Fail(TEXT("Invalid block identity or bounds."));
        if (Block.Kind != TEXT("tower") && Block.Kind != TEXT("midrise") &&
            Block.Kind != TEXT("warehouse") && Block.Kind != TEXT("containers"))
            return Fail(TEXT("Unknown block kind."));
        if (!ReadNumber(B, TEXT("columns"), 1, 12, Number) || FMath::FloorToDouble(Number) != Number) return Fail(TEXT("Invalid block columns."));
        Block.Columns = static_cast<int32>(Number);
        if (!ReadNumber(B, TEXT("rows"), 1, 12, Number) || FMath::FloorToDouble(Number) != Number) return Fail(TEXT("Invalid block rows."));
        Block.Rows = static_cast<int32>(Number);
        if (!ReadNumber(B, TEXT("minHeight"), 100, 20000, Number)) return Fail(TEXT("Invalid minimum building height."));
        Block.MinHeight = static_cast<float>(Number);
        if (!ReadNumber(B, TEXT("maxHeight"), Block.MinHeight, 20000, Number)) return Fail(TEXT("Invalid maximum building height."));
        Block.MaxHeight = static_cast<float>(Number);
        for (const FVector2D P : { Block.Min, Block.Max })
            if (FMath::Abs(P.X) > GroundHalfExtent.X || FMath::Abs(P.Y) > GroundHalfExtent.Y)
                return Fail(TEXT("Block extends beyond ground."));
        for (const auto& Road : Roads)
        {
            const double Padding = RoadWidth * 0.5 + 500.;
            if (Block.Min.X < FMath::Max(Road.Start.X, Road.End.X) + Padding &&
                Block.Max.X > FMath::Min(Road.Start.X, Road.End.X) - Padding &&
                Block.Min.Y < FMath::Max(Road.Start.Y, Road.End.Y) + Padding &&
                Block.Max.Y > FMath::Min(Road.Start.Y, Road.End.Y) - Padding)
                return Fail(TEXT("Building block overlaps a road or sidewalk."));
        }
        BuildingCount += Block.Columns * Block.Rows;
        if (BuildingCount > 256) return Fail(TEXT("District building budget exceeded."));
        Blocks.Add(Block);
    }
    const TSharedPtr<FJsonObject>* Lighting = nullptr;
    if (!Object->TryGetObjectField(TEXT("lighting"), Lighting)) return Fail(TEXT("Missing lighting definition."));
    if (!ReadNumber(*Lighting, TEXT("moonIntensityLux"), 0, 20, Number)) return Fail(TEXT("Invalid moon intensity."));
    Moon->SetIntensity(static_cast<float>(Number));
    if (!ReadNumber(*Lighting, TEXT("skyIntensity"), 0, 5, Number)) return Fail(TEXT("Invalid sky intensity."));
    Sky->SetIntensity(static_cast<float>(Number));
    if (!ReadNumber(*Lighting, TEXT("fogDensity"), 0, 0.1, Number)) return Fail(TEXT("Invalid fog density."));
    Fog->SetFogDensity(static_cast<float>(Number));
    if (!ReadNumber(*Lighting, TEXT("streetLightSpacing"), 2500, 10000, Number)) return Fail(TEXT("Invalid lamp spacing."));
    StreetLightSpacing = static_cast<float>(Number);
    if (!ReadNumber(*Lighting, TEXT("maxLocalLights"), 0, 32, Number)) return Fail(TEXT("Invalid local light budget."));
    MaxLocalLights = static_cast<int32>(Number);
    return true;
}

UHierarchicalInstancedStaticMeshComponent* AADDistrict::GetBatch(FName MaterialName, bool bCollision,
    bool bCylinder, UStaticMesh* MeshOverride)
{
    const FName Key=MeshOverride
        ? FName(*FString::Printf(TEXT("%s_%s_%d"),*MaterialName.ToString(),*MeshOverride->GetName(),bCollision))
        : FName(*FString::Printf(TEXT("%s_%d_%d"),*MaterialName.ToString(),bCollision,bCylinder));
    if (const auto* Existing = Batches.Find(Key)) return Existing->Get();
    auto* Batch = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, Key);
    Batch->SetupAttachment(RootComponent);
    Batch->SetMobility(EComponentMobility::Static);
    Batch->SetStaticMesh(MeshOverride ? MeshOverride : bCylinder ? Cylinder.Get() : Cube.Get());
    if (Materials.Contains(MaterialName)) Batch->SetMaterial(0, Materials.FindChecked(MaterialName));
    Batch->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
    if (bCollision)
    {
        const FName PhysicalName(*FString::Printf(TEXT("PM_%s"),*MaterialName.ToString()));
        Batch->SetPhysMaterialOverride(ADSurfacePhysics::CreatePhysicalMaterial(this,PhysicalName,MaterialName));
    }
    Batch->SetGenerateOverlapEvents(false);
    Batch->SetCanEverAffectNavigation(false);
    Batch->SetCastShadow(bCollision || MeshOverride != nullptr);
    // Collision geometry never vanishes visually. Small dressing can be culled.
    if (!bCollision) Batch->SetCullDistances(45000, 65000);
    Batch->bAutoRebuildTreeOnInstanceChanges = false;
    AddInstanceComponent(Batch);
    Batch->RegisterComponent();
    Batches.Add(Key, Batch);
    return Batch;
}

void AADDistrict::AddBox(FName MaterialName, const FVector& Center, const FVector& Size, bool bCollision, float Yaw)
{
    GetBatch(MaterialName, bCollision)->AddInstance(FTransform(FRotator(0, Yaw, 0), Center, Size / 100.));
}

void AADDistrict::AddCylinder(FName MaterialName, const FVector& Center, const FVector& Size)
{
    GetBatch(MaterialName, false, true)->AddInstance(FTransform(FQuat::Identity, Center, Size / 100.));
}

void AADDistrict::AddDistrictMesh(UStaticMesh* Mesh,const FVector& Center,const FVector& Size,float Yaw)
{
    if (!Mesh || Size.ContainsNaN() || Size.GetMin()<=0.f) return;
    UHierarchicalInstancedStaticMeshComponent* Batch=GetBatch(TEXT("KenneyCityKit"),false,false,Mesh);
    const FBoxSphereBounds Bounds=Mesh->GetBounds();
    const FVector MeshSize=Bounds.BoxExtent*2.f;
    if (MeshSize.ContainsNaN() || MeshSize.GetMin()<=KINDA_SMALL_NUMBER) return;
    const FVector Scale=Size/MeshSize;
    const FRotator Rotation(0,Yaw,0);
    const FVector Origin=Center-Rotation.RotateVector(Bounds.Origin*Scale);
    Batch->AddInstance(FTransform(Rotation,Origin,Scale));
}

void AADDistrict::AddBuildingCollider(const FVector& Center,const FVector& Size,float Yaw)
{
    UHierarchicalInstancedStaticMeshComponent* Batch=GetBatch(TEXT("M_Building"),true);
    // The original cuboids remain as query/physics proxies only; the imported
    // static meshes provide all visible facades and shadows.
    Batch->SetVisibility(false);
    Batch->SetCastShadow(false);
    Batch->AddInstance(FTransform(FRotator(0,Yaw,0),Center,Size/100.f));
}

bool AADDistrict::IsIntersection(const FVector2D& Position, const FADRoadSegment& Current, float Padding) const
{
    for (const auto& Road : Roads)
        if (Road.Id != Current.Id && InsideRoad(Position, Road, RoadWidth * 0.5 + Padding)) return true;
    return false;
}

bool AADDistrict::IsOnRoad(const FVector2D& Position, float Padding) const
{
    for (const auto& Road : Roads)
        if (InsideRoad(Position, Road, RoadWidth * 0.5 + Padding)) return true;
    return false;
}

void AADDistrict::BuildRoads()
{
    for (const auto& Road : Roads)
    {
        const FVector2D Direction = (Road.End - Road.Start).GetSafeNormal();
        const FVector2D Side(-Direction.Y, Direction.X);
        const FVector2D Middle = (Road.Start + Road.End) * 0.5;
        const float Length = static_cast<float>((Road.End - Road.Start).Size());
        const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
        AddBox(TEXT("M_Asphalt"), FVector(Middle, -20), FVector(Length + RoadWidth, RoadWidth, 40), true, Yaw);
        for (float Distance = 500; Distance < Length; Distance += 1000)
        {
            const FVector2D P = Road.Start + Direction * Distance;
            if (IsIntersection(P, Road, 650)) continue;
            for (const float Sign : { -1.f, 1.f })
            {
                AddBox(TEXT("M_RoadYellow"), FVector(P + Side * (Sign * 10), 1), FVector(850, 8, 1), false, Yaw);
                AddBox(TEXT("M_RoadMarking"), FVector(P + Side * (Sign * RoadWidth * .25), 1), FVector(300, 12, 1), false, Yaw);
                // Keep the driveable shoulder close to road height. The former
                // 19 cm exposed step snagged low chassis after side impacts.
                AddBox(TEXT("M_Concrete"), FVector(P + Side * (Sign * (RoadWidth * .5 + 260)), -1), FVector(1000, 500, 10), true, Yaw);
                AddBox(TEXT("M_RoadMarking"), FVector(P + Side * (Sign * (RoadWidth * .5 - 35)), 1), FVector(1000, 10, 1), false, Yaw);
            }
        }
    }
    // A low boundary fence communicates the extent of this test district.
    for (const float Sign : { -1.f, 1.f })
    {
        AddBox(TEXT("M_Concrete"), FVector(Sign * (GroundHalfExtent.X - 200), 0, 55), FVector(100, GroundHalfExtent.Y * 2, 120), true);
        AddBox(TEXT("M_Concrete"), FVector(0, Sign * (GroundHalfExtent.Y - 200), 55), FVector(GroundHalfExtent.X * 2, 100, 120), true);
    }
}

void AADDistrict::BuildBlocks()
{
    FRandomStream Random(Seed);
    for (const auto& Block : Blocks)
    {
        const FVector2D Cell((Block.Max.X - Block.Min.X) / Block.Columns, (Block.Max.Y - Block.Min.Y) / Block.Rows);
        for (int32 Y = 0; Y < Block.Rows; ++Y)
        for (int32 X = 0; X < Block.Columns; ++X)
        {
            const FVector2D P = Block.Min + FVector2D((X + .5) * Cell.X, (Y + .5) * Cell.Y);
            const float Height = Random.FRandRange(Block.MinHeight, Block.MaxHeight);
            const float Width = static_cast<float>(Cell.X * Random.FRandRange(.52f, .72f));
            const float Depth = static_cast<float>(Cell.Y * Random.FRandRange(.52f, .72f));
            if (Block.Kind == TEXT("containers"))
            {
                const int32 Stack = FMath::Clamp(FMath::FloorToInt(Height / 280), 1, 3);
                for (int32 Layer = 0; Layer < Stack; ++Layer)
                {
                    const FVector Center(P,140+Layer*285);
                    const FVector Size(1200,245,280);
                    const int32 Variant=FMath::Abs(X*31+Y*17+Layer)%ContainerMeshes.Num();
                    AddBuildingCollider(Center,Size);
                    // Kit container length is on local Y; rotate it to retain
                    // Dockside's east-west stack layout and true 12 m length.
                    AddDistrictMesh(ContainerMeshes[Variant],Center,FVector(245,1200,280),90.f);
                }
                continue;
            }

            UStaticMesh* BuildingMesh=nullptr;
            if (Block.Kind==TEXT("warehouse"))
                BuildingMesh=IndustrialWarehouseMeshes[Random.RandRange(0,IndustrialWarehouseMeshes.Num()-1)].Get();
            else if (Block.Kind==TEXT("tower"))
                BuildingMesh=DowntownTowerMeshes[Random.RandRange(0,DowntownTowerMeshes.Num()-1)].Get();
            else
                BuildingMesh=DowntownMidriseMeshes[Random.RandRange(0,DowntownMidriseMeshes.Num()-1)].Get();
            const float BuildingYaw=Random.RandRange(0,3)*90.f;
            if (BuildingMesh)
            {
                const FVector BuildingCenter(P,Height*.5f);
                const FVector BuildingSize(Width,Depth,Height);
                AddBuildingCollider(BuildingCenter,BuildingSize,BuildingYaw);
                AddDistrictMesh(BuildingMesh,BuildingCenter,BuildingSize,BuildingYaw);
                continue;
            }
            AddBox(TEXT("M_Building"), FVector(P, Height * .5), FVector(Width, Depth, Height), true);
            AddBox(TEXT("M_Metal"), FVector(P, Height + 25), FVector(Width + 80, Depth + 80, 50));
            const int32 FacadeVariant = FMath::Abs((X * 31 + Y * 17 + Block.Id.Len()) % 3);
            if (Block.Kind != TEXT("warehouse"))
            {
                const int32 Floors = FMath::Clamp(FMath::FloorToInt(Height / 400), 2, 30);
                const float FloorHeight = Height / Floors;

                // Give the repeated blockout shells real facade depth: corner
                // fins catch the street lights, while recessed belt lines break
                // up the long window grids. These are uncolliding HISM details.
                for (const float XSign : { -1.f, 1.f })
                for (const float YSign : { -1.f, 1.f })
                {
                    AddBox(TEXT("M_Concrete"),
                        FVector(P + FVector2D(XSign * Width * .43f, YSign * (Depth * .5f + 16.f)), Height * .5f),
                        FVector(76.f, 36.f, Height * .96f));
                    AddBox(TEXT("M_Concrete"),
                        FVector(P + FVector2D(XSign * (Width * .5f + 16.f), YSign * Depth * .43f), Height * .5f),
                        FVector(36.f, 76.f, Height * .96f));
                }

                const int32 BeltStride = Block.Kind == TEXT("tower") ? 2 : 3;
                const FName BeltMaterial = FacadeVariant == 1 ? TEXT("M_Concrete") : TEXT("M_Metal");
                for (int32 Floor = 1; Floor < Floors; Floor += BeltStride)
                {
                    const float Z = (Floor - .5f) * FloorHeight;
                    for (const float Sign : { -1.f, 1.f })
                    {
                        AddBox(BeltMaterial, FVector(P + FVector2D(0, Sign * (Depth * .5f + 14.f)), Z),
                            FVector(Width * 1.04f, 28.f, 32.f));
                        AddBox(BeltMaterial, FVector(P + FVector2D(Sign * (Width * .5f + 14.f), 0), Z),
                            FVector(28.f, Depth * 1.04f, 32.f));
                    }
                }

                // Vary the roof plant by building so the skyline has a distinct
                // rhythm instead of ending in identical flat caps.
                const int32 PlantCount = 1 + FacadeVariant;
                for (int32 Plant = 0; Plant < PlantCount; ++Plant)
                {
                    const float Offset = (Plant - (PlantCount - 1) * .5f) * Width * .19f;
                    const FVector2D PlantPosition = P + FVector2D(Offset, 0);
                    const float PlantWidth = Width * (FacadeVariant == 2 ? .16f : .12f);
                    const float PlantDepth = Depth * .17f;
                    AddBox(TEXT("M_Concrete"), FVector(PlantPosition, Height + 88.f),
                        FVector(PlantWidth, PlantDepth, 126.f));
                    AddBox(TEXT("M_Metal"), FVector(PlantPosition, Height + 166.f),
                        FVector(PlantWidth * .72f, PlantDepth * .78f, 34.f));
                }

                // Restrained roof beacons mark a handful of taller blocks.
                if (Block.Kind == TEXT("tower") && FacadeVariant == 0)
                    AddBox(TEXT("M_EmissiveWhite"), FVector(P, Height + 1110.f), FVector(110.f, 110.f, 36.f));
            }
            if (Block.Kind == TEXT("warehouse"))
            {
                AddBox(TEXT("M_Metal"), FVector(P + FVector2D(0, Depth * .5 + 190), Height * .55), FVector(Width * .85, 420, 40));
                for (int32 Door = -1; Door <= 1; ++Door)
                {
                    AddBox(TEXT("M_Metal"), FVector(P + FVector2D(Door * Width * .27, Depth * .5 + 3), 190), FVector(Width * .19, 8, 380));
                    AddBox(TEXT("M_WindowWarm"), FVector(P + FVector2D(Door * Width * .27, Depth * .5 + 9), Height * .72), FVector(Width * .16, 12, 75));
                }
            }
            else
            {
                const int32 Floors = FMath::Clamp(FMath::FloorToInt(Height / 400), 2, 30);
                for (int32 Floor = 1; Floor < Floors; ++Floor)
                for (int32 Window = -2; Window <= 2; ++Window)
                {
                    if (Random.FRand() < .3f) continue;
                    const FName Color = Random.FRand() < .72f ? TEXT("M_WindowWarm") : TEXT("M_WindowCool");
                    const float Z = Floor * (Height / Floors);
                    for (const float Sign : { -1.f, 1.f })
                    {
                    // Window dimensions use real-world centimeters. The previous
                    // facade rectangles were several metres wide and read as
                    // lit signage from the road instead of individual windows.
                    AddBox(Color, FVector(P + FVector2D(Window * Width * .16, Sign * (Depth * .5 + 4)), Z),
                        FVector(FMath::Min(210.f, Width * .045f), 8, 135));
                    AddBox(Color, FVector(P + FVector2D(Sign * (Width * .5 + 4), Window * Depth * .16), Z),
                        FVector(8, FMath::Min(210.f, Depth * .045f), 135));
                    }
                }
                if (Block.Kind == TEXT("tower"))
                {
                    AddBox(TEXT("M_Building"), FVector(P, Height + 250), FVector(Width * .55, Depth * .6, 500), true);
                    AddCylinder(TEXT("M_Metal"), FVector(P, Height + 800), FVector(18, 18, 800));
                    AddBox(TEXT("M_EmissiveRed"), FVector(P, Height + 1210), FVector(35, 35, 25));
                }
            }
        }
    }
    // Visible water and crane silhouettes place the district at Nova City's harbor.
    AddBox(TEXT("M_Water"), FVector(0, -GroundHalfExtent.Y - 11000, -85), FVector(GroundHalfExtent.X * 3, 21000, 20));
    for (int32 Crane = 0; Crane < 4; ++Crane)
    {
        const float X = 7000.f + Crane * 8500.f;
        const float Y = -GroundHalfExtent.Y + 2000.f;
        AddBox(TEXT("M_ContainerRust"), FVector(X, Y, 2600), FVector(200, 200, 5200), true);
        AddBox(TEXT("M_ContainerRust"), FVector(X, Y - 2100, 5300), FVector(150, 6000, 180));
        AddBox(TEXT("M_Metal"), FVector(X, Y - 4300, 3100), FVector(12, 12, 4300));
    }
}

void AADDistrict::BuildStreetFurniture()
{
    struct FLamp { FVector Position; bool bWarm; };
    TArray<FLamp> Lamps;
    for (const auto& Road : Roads)
    {
        const FVector2D Direction = (Road.End - Road.Start).GetSafeNormal();
        const FVector2D Side(-Direction.Y, Direction.X);
        const float Heading=FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X));
        const float Length = static_cast<float>((Road.End - Road.Start).Size());
        for (float Distance = 1800; Distance < Length; Distance += StreetLightSpacing)
        {
            const FVector2D P = Road.Start + Direction * Distance;
            if (IsIntersection(P, Road, 600)) continue;
            for (const float Sign : { -1.f, 1.f })
            {
                const FVector2D Pole = P + Side * (Sign * (RoadWidth * .5 + 380));
                const FVector2D TowardRoad=-Side*Sign;
                const FVector2D Lamp=Pole+TowardRoad*420.f;
                const float ArmYaw=FMath::RadiansToDegrees(FMath::Atan2(TowardRoad.Y,TowardRoad.X));
                const FVector Luminaire(Lamp,850.f);
                const bool bWarm=P.Y<=0.f;
                const FName LensMaterial=bWarm ? FName(TEXT("M_WindowWarm")) : FName(TEXT("M_EmissiveWhite"));

                AddCylinder(TEXT("M_Concrete"),FVector(Pole,20.f),FVector(52.f,52.f,40.f));
                AddCylinder(TEXT("M_Metal"),FVector(Pole,370.f),FVector(28.f,28.f,740.f));
                AddCylinder(TEXT("M_Metal"),FVector(Pole,750.f),FVector(18.f,18.f,190.f));
                AddBox(TEXT("M_Metal"),FVector(Pole+TowardRoad*210.f,835.f),FVector(420.f,14.f,14.f),false,ArmYaw);
                AddBox(TEXT("M_Metal"),FVector(Lamp,850.f),FVector(132.f,54.f,20.f),false,Heading);
                AddBox(TEXT("M_Metal"),FVector(Pole+TowardRoad*34.f,814.f),FVector(16.f,16.f,42.f));
                AddBox(LensMaterial,FVector(Lamp,833.f),FVector(98.f,34.f,5.f),false,Heading);
                // The collision primitive follows the narrow mast only, so a car
                // cannot snag on the former oversized art bounding box.
                AddBox(TEXT("M_Concrete"),FVector(Pole,410.f),FVector(32.f,32.f,820.f),true);
                Lamps.Add({ FVector(Lamp, 800.f), bWarm });
            }
        }
    }

    // Signal posts are placed once per perpendicular approach. They are static
    // kit geometry; traffic control and right-of-way remain owned by AI logic.
    for (const auto& Road:Roads)
    {
        const FVector2D Direction=(Road.End-Road.Start).GetSafeNormal();
        for (const bool bAtEnd:{false,true})
        {
            const FVector2D Endpoint=bAtEnd ? Road.End : Road.Start;
            bool bCrossStreet=false;
            for (const auto& Other:Roads)
            {
                if (Other.Id==Road.Id) continue;
                const FVector2D OtherDirection=(Other.End-Other.Start).GetSafeNormal();
                if (FMath::Abs(FVector2D::DotProduct(Direction,OtherDirection))<.5f
                    && InsideRoad(Endpoint,Other,RoadWidth*.5+100.f))
                { bCrossStreet=true; break; }
            }
            if (!bCrossStreet) continue;
            const FVector2D Approach=bAtEnd ? Direction : -Direction;
            const FVector2D Right(Approach.Y,-Approach.X);
            const FVector2D Position=Endpoint+Right*(RoadWidth*.5+220.f)-Approach*450.f;
            const float SignalYaw=FMath::RadiansToDegrees(FMath::Atan2(Approach.Y,Approach.X));
            AddDistrictMesh(TrafficSignalMesh,FVector(Position,260.f),FVector(70.f,70.f,520.f),SignalYaw);
            AddBox(TEXT("M_Concrete"),FVector(Position,260.f),FVector(70.f,70.f,520.f),true,SignalYaw);
        }
    }
    // Light objects have a hard bound, independent of scenery density. Emissive
    // lamps continue through the district; only the drive-feel area has local fill.
    Lamps.Sort([](const FLamp& A, const FLamp& B) { return A.Position.SizeSquared2D() < B.Position.SizeSquared2D(); });
    for (int32 Index = 0; Index < FMath::Min(MaxLocalLights, Lamps.Num()); ++Index)
    {
        auto* Light = NewObject<UPointLightComponent>(this);
        Light->SetupAttachment(RootComponent);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetRelativeLocation(Lamps[Index].Position);
        Light->SetLightColor(Lamps[Index].bWarm ? FLinearColor(1.f,.68f,.40f) : FLinearColor(.72f,.84f,1.f));
        Light->SetIntensity(7600.f);
        Light->SetAttenuationRadius(3200.f);
        Light->SetCastShadows(false);
        AddInstanceComponent(Light);
        Light->RegisterComponent();
    }

    if (!ParkedCityCarMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("CC0 parked-car art is unavailable; continuing with procedural district dressing."));
        return;
    }
    ParkedCityCars = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("ParkedCityCars"));
    ParkedCityCars->SetupAttachment(RootComponent);
    ParkedCityCars->SetMobility(EComponentMobility::Static);
    ParkedCityCars->SetStaticMesh(ParkedCityCarMesh);
    ParkedCityCars->SetCollisionProfileName(TEXT("NoCollision"));
    ParkedCityCars->SetGenerateOverlapEvents(false);
    ParkedCityCars->SetCanEverAffectNavigation(false);
    ParkedCityCars->SetCastShadow(true);
    ParkedCityCars->SetCullDistances(65000, 85000);
    AddInstanceComponent(ParkedCityCars);
    ParkedCityCars->RegisterComponent();

    // These parked background cars frame the first playable avenue. The model's
    // converted +X axis is forward, and it remains visual-only (no traffic AI).
    const FVector ParkedPositions[] = {
        FVector(5000.f, -1925.f, 1.f), FVector(8500.f, -1925.f, 1.f), FVector(12000.f, -1925.f, 1.f)
    };
    int32 AddedCars = 0;
    for (const FVector& Position : ParkedPositions)
    {
        AddedCars += ParkedCityCars->AddInstance(FTransform(FRotator::ZeroRotator, Position, FVector::OneVector)) >= 0;
        // Background cars remain instanced visual art; one cheap box per car
        // gives them predictable collision without relying on imported meshes.
        AddBox(TEXT("M_Metal"),Position+FVector(0.f,0.f,78.f),FVector(450.f,195.f,145.f),true);
    }
    const int32 ExpectedCarCount = static_cast<int32>(UE_ARRAY_COUNT(ParkedPositions));
    if (AddedCars == ExpectedCarCount)
    {
        UE_LOG(LogTemp, Display, TEXT("AFTERDARK_PARKED_ART_READY: %d static cars from CC0 source."), AddedCars);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Only %d of %d parked city cars could be instantiated."), AddedCars, ExpectedCarCount);
    }
}
