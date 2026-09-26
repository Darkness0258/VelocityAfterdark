#include "World/ADRegionalWorld.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/ADVehiclePawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/ADAtmosphere.h"

DEFINE_LOG_CATEGORY_STATIC(LogADRegionalWorld, Log, All);

namespace
{
    bool ReadObject(const FString& Path, TSharedPtr<FJsonObject>& Out)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        FString Json;
        return Size > 0 && Size <= 131072 && FFileHelper::LoadFileToString(Json,*Path)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Out) && Out.IsValid();
    }
    bool ReadRegionalNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Min, double Max, double& Out)
    {
        const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
        return Value && Value->Type == EJson::Number && Value->TryGetNumber(Out)
            && FMath::IsFinite(Out) && Out >= Min && Out <= Max;
    }
    bool ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FVector2D& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object->TryGetArrayField(Name,Values) || Values->Num() != 2) return false;
        double X = 0., Y = 0.;
        if (!(*Values)[0] || (*Values)[0]->Type != EJson::Number || !(*Values)[0]->TryGetNumber(X)
            || !(*Values)[1] || (*Values)[1]->Type != EJson::Number || !(*Values)[1]->TryGetNumber(Y)
            || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
        Out = FVector2D(X,Y);
        return true;
    }
    FBox2D RoadBox(const FADRegionalRoad& Road, double HalfWidth)
    {
        return FBox2D(FVector2D(FMath::Min(Road.Start.X,Road.End.X)-HalfWidth,FMath::Min(Road.Start.Y,Road.End.Y)-HalfWidth),
            FVector2D(FMath::Max(Road.Start.X,Road.End.X)+HalfWidth,FMath::Max(Road.Start.Y,Road.End.Y)+HalfWidth));
    }
    bool StrictlyOverlaps(const FBox2D& A, const FBox2D& B)
    {
        return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
    }
    bool PointInside(FVector2D Point, const FBox2D& Bounds)
    {
        return Point.X >= Bounds.Min.X && Point.Y >= Bounds.Min.Y && Point.X <= Bounds.Max.X && Point.Y <= Bounds.Max.Y;
    }
    double DistanceToBox(FVector2D Point, const FBox2D& Bounds)
    {
        const double X = FMath::Max(FMath::Max(Bounds.Min.X-Point.X,Point.X-Bounds.Max.X),0.);
        const double Y = FMath::Max(FMath::Max(Bounds.Min.Y-Point.Y,Point.Y-Bounds.Max.Y),0.);
        return FMath::Sqrt(X*X+Y*Y);
    }
}

AADRegionalWorld::AADRegionalWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RegionalWorldRoot"));
    RootComponent->SetMobility(EComponentMobility::Static);
}

void AADRegionalWorld::BeginPlay()
{
    Super::BeginPlay();
    if (!LoadDefinition() || !LoadMaterials())
    {
        SetActorTickEnabled(false);
        UE_LOG(LogADRegionalWorld,Error,TEXT("Regional world disabled: %s"),*LoadError);
        return;
    }
    BuildResidentRoads();
    BuildCells();
    bReady = true;
    for (TActorIterator<AADAtmosphere> It(GetWorld()); It; ++It) { Atmosphere = *It; break; }
    if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
        BindVehicle(Cast<AADVehiclePawn>(Controller->GetPawn()));
    UE_LOG(LogADRegionalWorld,Display,TEXT("Regional development world ready: %.0f m connected roads, %d scenery cells; max %d resident."),
        RoadLengthMeters,Cells.Num(),MaximumLoadedCells);
}

bool AADRegionalWorld::LoadDefinition()
{
    const auto Fail = [this](const TCHAR* Reason) { LoadError = Reason; return false; };
    TSharedPtr<FJsonObject> Object, Legacy;
    if (!ReadObject(FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/World/regions.json")),Object)
        || !ReadObject(FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/World/dockside.json")),Legacy))
        return Fail(TEXT("Cannot read regions.json and dockside.json objects (128 KB maximum each)."));
    double Value = 0.;
    FVector2D LegacyBounds;
    if (!ReadRegionalNumber(Object,TEXT("schemaVersion"),1,1,Value)
        || !ReadPoint(Object,TEXT("groundHalfExtent"),GroundHalfExtent)
        || GroundHalfExtent.X < 50000 || GroundHalfExtent.Y < 40000 || GroundHalfExtent.X > 250000 || GroundHalfExtent.Y > 250000
        || !ReadPoint(Legacy,TEXT("groundHalfExtent"),LegacyBounds)
        || LegacyBounds.X < GroundHalfExtent.X || LegacyBounds.Y < GroundHalfExtent.Y)
        return Fail(TEXT("Regional bounds must fit inside the resident district ground and boundary fence."));
    if (!ReadRegionalNumber(Object,TEXT("roadWidth"),1200,4000,Value)) return Fail(TEXT("Invalid regional road width."));
    RoadWidth = static_cast<float>(Value);
    if (!ReadRegionalNumber(Legacy,TEXT("roadWidth"),1200,4000,Value)) return Fail(TEXT("Invalid existing road width."));
    LegacyRoadWidth = static_cast<float>(Value);
    if (!ReadRegionalNumber(Object,TEXT("cellSizeCm"),8000,30000,Value)) return Fail(TEXT("Invalid cell size."));
    CellSizeCm = static_cast<float>(Value);
    if (!ReadRegionalNumber(Object,TEXT("loadRadiusCm"),16000,60000,Value)) return Fail(TEXT("Invalid scenery load radius."));
    LoadRadiusCm = static_cast<float>(Value);
    if (!ReadRegionalNumber(Object,TEXT("unloadRadiusCm"),LoadRadiusCm+4000.,90000,Value)) return Fail(TEXT("Unloading requires at least 40 m of hysteresis."));
    UnloadRadiusCm = static_cast<float>(Value);
    if (!ReadRegionalNumber(Object,TEXT("maximumLoadedCells"),4,48,Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("Invalid maximum cell count."));
    MaximumLoadedCells = static_cast<int32>(Value);
    if (!ReadRegionalNumber(Object,TEXT("cellOperationsPerUpdate"),1,4,Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("Invalid cell operation budget."));
    CellOperationsPerUpdate = static_cast<int32>(Value);
    if (!ReadRegionalNumber(Object,TEXT("updateBudgetMs"),.25,8,Value)) return Fail(TEXT("Invalid streaming time budget."));
    UpdateBudgetMs = static_cast<float>(Value);

    TSet<FString> RoadIds;
    const auto ReadRoads = [&RoadIds,this](const TSharedPtr<FJsonObject>& Source, TArray<FADRegionalRoad>& Destination, float Width)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Source->TryGetArrayField(TEXT("roads"),Values) || Values->IsEmpty() || Values->Num() > 32) return false;
        for (const auto& Entry : *Values)
        {
            if (!Entry || Entry->Type != EJson::Object) return false;
            FADRegionalRoad Road;
            const auto Data = Entry->AsObject();
            if (!Data->TryGetStringField(TEXT("id"),Road.Id) || Road.Id.IsEmpty() || Road.Id.Len() > 64 || RoadIds.Contains(Road.Id)
                || !ReadPoint(Data,TEXT("start"),Road.Start) || !ReadPoint(Data,TEXT("end"),Road.End)) return false;
            const FVector2D Delta = Road.End-Road.Start;
            if ((FMath::Abs(Delta.X) > .1 && FMath::Abs(Delta.Y) > .1) || Delta.Size() < 2000.) return false;
            for (const FVector2D P : {Road.Start,Road.End})
                if (FMath::Abs(P.X)+Width >= GroundHalfExtent.X-500 || FMath::Abs(P.Y)+Width >= GroundHalfExtent.Y-500) return false;
            RoadIds.Add(Road.Id);
            Destination.Add(Road);
        }
        return true;
    };
    if (!ReadRoads(Legacy,LegacyRoads,LegacyRoadWidth) || !ReadRoads(Object,Roads,RoadWidth)) return Fail(TEXT("Invalid, duplicate or out-of-bounds road."));
    // Every extension must have a geometric road connection back to Dockside.
    TSet<int32> Connected;
    for (int32 I = 0; I < Roads.Num(); ++I)
        for (const auto& Existing : LegacyRoads)
            if (StrictlyOverlaps(RoadBox(Roads[I],RoadWidth*.5),RoadBox(Existing,LegacyRoadWidth*.5))) { Connected.Add(I); break; }
    bool bChanged = true;
    while (bChanged)
    {
        bChanged = false;
        for (int32 I = 0; I < Roads.Num(); ++I)
        {
            if (Connected.Contains(I)) continue;
            bool bTouchesConnected=false;
            for (const int32 J : Connected)
                if (StrictlyOverlaps(RoadBox(Roads[I],RoadWidth*.5),RoadBox(Roads[J],RoadWidth*.5)))
                { bTouchesConnected=true; break; }
            if (bTouchesConnected) { Connected.Add(I); bChanged=true; }
        }
    }
    if (Connected.Num() != Roads.Num()) return Fail(TEXT("Every regional road must connect to the existing road network."));
    for (const auto& Road : Roads) RoadLengthMeters += static_cast<float>((Road.End-Road.Start).Size()*.01);
    if (RoadLengthMeters > 15000.f) return Fail(TEXT("Regional road length exceeds the bounded development world budget."));
    const TArray<TSharedPtr<FJsonValue>>* RegionValues = nullptr;
    if (!Object->TryGetArrayField(TEXT("regions"),RegionValues) || RegionValues->IsEmpty() || RegionValues->Num() > 12)
        return Fail(TEXT("Between one and twelve regions are required."));
    TSet<FString> RegionIds;
    const TSet<FString> Styles = {TEXT("coast"),TEXT("countryside"),TEXT("industrial"),TEXT("downtown"),TEXT("mountain"),TEXT("desert"),TEXT("racing")};
    for (const auto& Entry : *RegionValues)
    {
        if (!Entry || Entry->Type != EJson::Object) return Fail(TEXT("Region entries must be objects."));
        const auto Data = Entry->AsObject();
        FRegion Region;
        if (!Data->TryGetStringField(TEXT("id"),Region.Id) || Region.Id.IsEmpty() || RegionIds.Contains(Region.Id)
            || !Data->TryGetStringField(TEXT("name"),Region.Name) || Region.Name.IsEmpty() || Region.Name.Len() > 48
            || !Data->TryGetStringField(TEXT("style"),Region.Style) || !Styles.Contains(Region.Style)
            || !ReadPoint(Data,TEXT("min"),Region.Min) || !ReadPoint(Data,TEXT("max"),Region.Max)
            || Region.Max.X-Region.Min.X < 8000 || Region.Max.Y-Region.Min.Y < 8000
            || Region.Min.X < -GroundHalfExtent.X+1000 || Region.Max.X > GroundHalfExtent.X-1000
            || Region.Min.Y < -GroundHalfExtent.Y+1000 || Region.Max.Y > GroundHalfExtent.Y-1000)
            return Fail(TEXT("Invalid region identity, style or bounds."));
        const FBox2D Bounds(Region.Min,Region.Max);
        // Existing authored district blocks occupy this protected rectangle.
        const FBox2D DocksideBlocks(FVector2D(-38000,-23000),FVector2D(38000,23000));
        if (StrictlyOverlaps(Bounds,DocksideBlocks)) return Fail(TEXT("New scenery may not overlap Dockside's existing blocks."));
        for (const FRegion& Other : Regions)
            if (StrictlyOverlaps(Bounds,FBox2D(Other.Min,Other.Max))) return Fail(TEXT("Region scenery bounds may not overlap."));
        if (!ReadRegionalNumber(Data,TEXT("seed"),0,2147483647,Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("Invalid region seed."));
        Region.Seed = static_cast<int32>(Value);
        if (!ReadRegionalNumber(Data,TEXT("propsPerCell"),1,16,Value) || Value != FMath::FloorToDouble(Value)) return Fail(TEXT("Invalid cell prop count."));
        Region.PropsPerCell = static_cast<int32>(Value);
        RegionIds.Add(Region.Id);
        Regions.Add(Region);
    }
    int32 CellCount = 0;
    for (const FRegion& Region : Regions)
        CellCount += FMath::CeilToInt((Region.Max.X-Region.Min.X)/CellSizeCm)*FMath::CeilToInt((Region.Max.Y-Region.Min.Y)/CellSizeCm);
    if (CellCount > 256) return Fail(TEXT("The development streaming world is limited to 256 scenery cells."));
    return true;
}

UMaterialInstanceDynamic* AADRegionalWorld::MakeMaterial(const TCHAR* Name, const TCHAR* Asset,
    FLinearColor Color, float Roughness, float Metallic)
{
    const FString Path = FString::Printf(TEXT("/Game/Velocity/Materials/%s.%s"),Asset,Asset);
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,*Path);
    if (!Base) return nullptr;
    auto* Material = UMaterialInstanceDynamic::Create(Base,this,FName(Name));
    if (!Material) return nullptr;
    Material->SetVectorParameterValue(TEXT("BaseColor"),Color);
    Material->SetScalarParameterValue(TEXT("Roughness"),Roughness);
    Material->SetScalarParameterValue(TEXT("Metallic"),Metallic);
    Materials.Add(FName(Name),Material);
    return Material;
}

bool AADRegionalWorld::LoadMaterials()
{
    Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cylinder = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Sphere = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!Cube || !Cylinder || !Sphere) { LoadError = TEXT("Regional scenery requires cooked engine primitive meshes."); return false; }

    const auto LoadKitFamily = [this](const TCHAR* Directory, const TArray<FString>& Names,
        TArray<TObjectPtr<UStaticMesh>>& Destination)
    {
        for (const FString& Name : Names)
        {
            const FString Path = FString::Printf(TEXT("%s/%s.%s"),Directory,*Name,*Name);
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,*Path);
            if (!Mesh)
            {
                LoadError = FString::Printf(TEXT("Required streamed district mesh is missing: %s"),*Path);
                return false;
            }
            Destination.Add(Mesh);
        }
        return !Destination.IsEmpty();
    };
    const auto LoadKitMesh = [this](const TCHAR* Directory, const TCHAR* Name) -> UStaticMesh*
    {
        const FString Path = FString::Printf(TEXT("%s/%s.%s"),Directory,Name,Name);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,*Path);
        if (!Mesh) LoadError = FString::Printf(TEXT("Required streamed district mesh is missing: %s"),*Path);
        return Mesh;
    };
    if (!LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitCommercial"),
            {TEXT("SM_Kenney_Commercial_SkyscraperA"),TEXT("SM_Kenney_Commercial_SkyscraperB"),
             TEXT("SM_Kenney_Commercial_SkyscraperC"),TEXT("SM_Kenney_Commercial_SkyscraperD"),
             TEXT("SM_Kenney_Commercial_SkyscraperE")},CommercialSkyscrapers)
        || !LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitCommercial"),
            {TEXT("SM_Kenney_Commercial_BuildingA"),TEXT("SM_Kenney_Commercial_BuildingD"),
             TEXT("SM_Kenney_Commercial_BuildingH"),TEXT("SM_Kenney_Commercial_BuildingN")},CommercialMidRises)
        || !LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitIndustrial"),
            {TEXT("SM_Kenney_Industrial_BuildingA"),TEXT("SM_Kenney_Industrial_BuildingD"),
             TEXT("SM_Kenney_Industrial_BuildingH"),TEXT("SM_Kenney_Industrial_BuildingM"),
             TEXT("SM_Kenney_Industrial_BuildingQ"),TEXT("SM_Kenney_Industrial_BuildingT")},IndustrialWarehouses)
        || !LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitIndustrial"),
            {TEXT("SM_Kenney_Industrial_ContainerA"),TEXT("SM_Kenney_Industrial_ContainerC")},IndustrialContainers)
        || !LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitIndustrial"),
            {TEXT("SM_Kenney_Industrial_ChimneyLarge"),TEXT("SM_Kenney_Industrial_TankLarge"),
             TEXT("SM_Kenney_Industrial_WaterTower")},IndustrialDetails)
        || !LoadKitFamily(TEXT("/Game/Velocity/External/KenneyCityKitRoads"),
            {TEXT("SM_Kenney_Roads_LampCurved"),TEXT("SM_Kenney_Roads_LampSquare")},RoadLamps))
        return false;
    TrafficSignal = LoadKitMesh(TEXT("/Game/Velocity/External/KenneyCityKitRoads"),TEXT("SM_Kenney_Roads_TrafficLight"));
    ConstructionBarrier = LoadKitMesh(TEXT("/Game/Velocity/External/KenneyCityKitRoads"),TEXT("SM_Kenney_Roads_ConstructionBarrier"));
    if (!TrafficSignal || !ConstructionBarrier) return false;
    WetAsphalt = MakeMaterial(TEXT("Asphalt"),TEXT("M_Asphalt"),FLinearColor(.022f,.028f,.034f),.27f,.08f);
    auto* White = MakeMaterial(TEXT("White"),TEXT("M_RoadMarking"),FLinearColor(.62f,.65f,.61f),.48f,0.f);
    auto* Yellow = MakeMaterial(TEXT("Yellow"),TEXT("M_RoadYellow"),FLinearColor(.73f,.46f,.08f),.52f,0.f);
    auto* Concrete = MakeMaterial(TEXT("Concrete"),TEXT("M_Concrete"),FLinearColor(.23f,.24f,.23f),.8f,0.f);
    auto* Metal = MakeMaterial(TEXT("Metal"),TEXT("M_Metal"),FLinearColor(.10f,.13f,.15f),.4f,.55f);
    auto* Glass = MakeMaterial(TEXT("Glass"),TEXT("M_Glass"),FLinearColor(.026f,.074f,.10f),.15f,.40f);
    auto* Foliage = MakeMaterial(TEXT("Foliage"),TEXT("M_Concrete"),FLinearColor(.045f,.14f,.062f),.92f,0.f);
    auto* Wood = MakeMaterial(TEXT("Wood"),TEXT("M_Concrete"),FLinearColor(.12f,.075f,.038f),.9f,0.f);
    auto* Sand = MakeMaterial(TEXT("Sand"),TEXT("M_Concrete"),FLinearColor(.45f,.30f,.16f),.95f,0.f);
    auto* Rock = MakeMaterial(TEXT("Rock"),TEXT("M_Concrete"),FLinearColor(.13f,.145f,.12f),.97f,0.f);
    auto* Rust = MakeMaterial(TEXT("Rust"),TEXT("M_ContainerRust"),FLinearColor(.32f,.095f,.045f),.68f,.32f);
    Windows = MakeMaterial(TEXT("Windows"),TEXT("M_WindowWarm"),FLinearColor(.72f,.45f,.19f),.24f,.1f);
    if (!WetAsphalt || !White || !Yellow || !Concrete || !Metal || !Glass || !Foliage || !Wood || !Sand || !Rock || !Rust || !Windows)
    { LoadError = TEXT("Regional scenery material assets are incomplete."); return false; }
    return true;
}

UInstancedStaticMeshComponent* AADRegionalWorld::CreateBatch(FName Name, UMaterialInterface* Material,
    UStaticMesh* Mesh, bool bCollision)
{
    auto* Batch = NewObject<UInstancedStaticMeshComponent>(this,Name);
    AddInstanceComponent(Batch);
    Batch->SetupAttachment(RootComponent);
    Batch->SetMobility(EComponentMobility::Static);
    Batch->SetStaticMesh(Mesh);
    if (Material) Batch->SetMaterial(0,Material);
    Batch->SetCanEverAffectNavigation(false);
    Batch->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    if (bCollision) Batch->SetCollisionProfileName(TEXT("BlockAll"));
    Batch->SetCastShadow(!bCollision);
    Batch->RegisterComponent();
    return Batch;
}

void AADRegionalWorld::AddResident(FName Material, FVector Position, FVector Size, FRotator Rotation)
{
    TObjectPtr<UInstancedStaticMeshComponent>& Batch = ResidentBatches.FindOrAdd(Material);
    if (!Batch) Batch = CreateBatch(FName(*(TEXT("Resident")+Material.ToString())),Materials.FindChecked(Material),Cube,Material == TEXT("Asphalt"));
    Batch->AddInstance(FTransform(Rotation,Position,Size/100.f));
}

void AADRegionalWorld::AddResidentMesh(FName BatchKey, UStaticMesh* Mesh, FVector Position,
    FVector Size, FRotator Rotation)
{
    if (!Mesh || Size.ContainsNaN() || Size.GetMin() <= 0.f) return;
    TObjectPtr<UInstancedStaticMeshComponent>& Batch = ResidentBatches.FindOrAdd(BatchKey);
    if (!Batch) Batch = CreateBatch(FName(*(TEXT("ResidentMesh_")+BatchKey.ToString())),nullptr,Mesh,false);
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const FVector MeshSize = Bounds.BoxExtent*2.f;
    if (MeshSize.ContainsNaN() || MeshSize.GetMin() <= KINDA_SMALL_NUMBER) return;
    const FVector Scale = Size/MeshSize;
    const FVector Origin = Position-Rotation.RotateVector(Bounds.Origin*Scale);
    Batch->AddInstance(FTransform(Rotation,Origin,Scale));
}

bool AADRegionalWorld::IsOnRegionalRoad(FVector2D Position, double MarginCm) const
{
    if (Position.ContainsNaN() || !FMath::IsFinite(MarginCm)) return false;
    for (const FADRegionalRoad& Road : Roads)
        if (PointInside(Position,RoadBox(Road,RoadWidth*.5+FMath::Max(0.,MarginCm)))) return true;
    return false;
}

void AADRegionalWorld::BuildResidentRoads()
{
    TArray<double> X, Y;
    const auto AddBreaks = [&X,&Y](const FADRegionalRoad& Road, double HalfWidth)
    {
        const FBox2D Box = RoadBox(Road,HalfWidth);
        X.AddUnique(Box.Min.X); X.AddUnique(Box.Max.X);
        Y.AddUnique(Box.Min.Y); Y.AddUnique(Box.Max.Y);
    };
    for (const auto& Road : Roads) AddBreaks(Road,RoadWidth*.5);
    for (const auto& Road : LegacyRoads) AddBreaks(Road,LegacyRoadWidth*.5);
    X.Sort(); Y.Sort();
    // Partition the rectangle union. New tiles never overlap each other or the
    // original roads, preserving the original circuit geometry and elevation.
    for (int32 Row = 0; Row < Y.Num()-1; ++Row)
    for (int32 Column = 0; Column < X.Num()-1; ++Column)
    {
        const FVector2D Center((X[Column]+X[Column+1])*.5,(Y[Row]+Y[Row+1])*.5);
        if (!IsOnRegionalRoad(Center)) continue;
        bool bExisting = false;
        for (const auto& Road : LegacyRoads)
            if (PointInside(Center,RoadBox(Road,LegacyRoadWidth*.5))) { bExisting = true; break; }
        if (!bExisting) AddResident(TEXT("Asphalt"),FVector(Center,-20),FVector(X[Column+1]-X[Column],Y[Row+1]-Y[Row],40));
    }
    for (const auto& Road : Roads)
    {
        const FVector2D Delta = Road.End-Road.Start;
        const FVector2D Direction = Delta.GetSafeNormal();
        const FVector2D Side(-Direction.Y,Direction.X);
        const float Heading = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X));
        const FRotator Rotation(0,Heading,0);
        for (double Distance = 500.; Distance < Delta.Size(); Distance += 1000.)
        {
            const FVector2D P = Road.Start+Direction*Distance;
            bool bIntersection = false;
            for (const auto& Other : Roads)
                if (Other.Id != Road.Id && PointInside(P,RoadBox(Other,RoadWidth*.5+650.))) { bIntersection = true; break; }
            for (const auto& Other : LegacyRoads)
                if (PointInside(P,RoadBox(Other,LegacyRoadWidth*.5+650.))) { bIntersection = true; break; }
            if (bIntersection) continue;
            for (const float Sign : {-1.f,1.f})
            {
                AddResident(TEXT("Yellow"),FVector(P+Side*(Sign*10.f),1),FVector(850,8,1),Rotation);
                AddResident(TEXT("White"),FVector(P+Side*(Sign*RoadWidth*.25),1),FVector(300,12,1),Rotation);
                AddResident(TEXT("White"),FVector(P+Side*(Sign*(RoadWidth*.5-35)),1),FVector(1000,10,1),Rotation);
            }
        }

        // Modular kit lights are instanced by mesh, kept outside the driveable
        // asphalt and omitted at junction approaches to protect sightlines.
        for (double Distance = 3000.; Distance < Delta.Size()-2000.; Distance += 5000.)
        {
            const FVector2D P = Road.Start+Direction*Distance;
            bool bNearJunction = false;
            for (const auto& Other : Roads)
                if (Other.Id != Road.Id && PointInside(P,RoadBox(Other,RoadWidth*.5+2200.))) { bNearJunction=true; break; }
            for (const auto& Other : LegacyRoads)
                if (PointInside(P,RoadBox(Other,LegacyRoadWidth*.5+2200.))) { bNearJunction=true; break; }
            if (bNearJunction) continue;
            for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
            {
                const float Sign = SideIndex == 0 ? -1.f : 1.f;
                UStaticMesh* Lamp = RoadLamps[(FMath::FloorToInt(Distance/5000.)+SideIndex)%RoadLamps.Num()].Get();
                const float LampHeading = Heading+(Sign > 0.f ? 180.f : 0.f);
                AddResidentMesh(Lamp->GetFName(),Lamp,FVector(P+Side*(Sign*(RoadWidth*.5+550.f)),410.f),
                    FVector(70.f,220.f,820.f),FRotator(0,LampHeading,0));
            }
        }

        // A static signal prop at each perpendicular approach makes the
        // generated network legible without adding costly independent lights.
        for (const bool bAtEnd : {false,true})
        {
            const FVector2D Endpoint = bAtEnd ? Road.End : Road.Start;
            const FVector2D Approach = bAtEnd ? Direction : -Direction;
            const FVector2D Right(Approach.Y,-Approach.X);
            bool bConnectedCrossStreet = false;
            const auto IsCrossStreet = [&Road,Endpoint,Direction](const FADRegionalRoad& Other,double OtherWidth)
            {
                return Other.Id != Road.Id
                    && FMath::Abs(FVector2D::DotProduct(Direction,(Other.End-Other.Start).GetSafeNormal())) < .5
                    && PointInside(Endpoint,RoadBox(Other,OtherWidth*.5+100.));
            };
            for (const auto& Other : Roads)
                if (IsCrossStreet(Other,RoadWidth)) { bConnectedCrossStreet=true; break; }
            for (const auto& Other : LegacyRoads)
                if (IsCrossStreet(Other,LegacyRoadWidth)) { bConnectedCrossStreet=true; break; }
            if (!bConnectedCrossStreet) continue;
            const FVector2D SignalPoint = Endpoint+Right*(RoadWidth*.5+220.)-Approach*450.;
            const float SignalHeading = FMath::RadiansToDegrees(FMath::Atan2(Approach.Y,Approach.X));
            AddResidentMesh(TrafficSignal->GetFName(),TrafficSignal,FVector(SignalPoint,260.f),
                FVector(70.f,70.f,520.f),FRotator(0,SignalHeading,0));
        }
    }
}

void AADRegionalWorld::BuildCells()
{
    for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
    {
        const FRegion& Region = Regions[RegionIndex];
        int32 Sequence = 0;
        for (double Y = Region.Min.Y; Y < Region.Max.Y; Y += CellSizeCm)
        for (double X = Region.Min.X; X < Region.Max.X; X += CellSizeCm)
        {
            FCell Cell;
            Cell.Region = RegionIndex;
            Cell.Seed = static_cast<int32>(static_cast<uint32>(Region.Seed)+static_cast<uint32>(Sequence++)*7907u);
            Cell.Bounds = FBox2D(FVector2D(X,Y),FVector2D(FMath::Min(X+CellSizeCm,Region.Max.X),FMath::Min(Y+CellSizeCm,Region.Max.Y)));
            CellPriority.Add(Cells.Add(MoveTemp(Cell)));
        }
    }
}

bool AADRegionalWorld::IsRoadClear(FBox2D Footprint, double MarginCm) const
{
    for (const auto& Road : Roads) if (StrictlyOverlaps(Footprint,RoadBox(Road,RoadWidth*.5+MarginCm))) return false;
    for (const auto& Road : LegacyRoads) if (StrictlyOverlaps(Footprint,RoadBox(Road,LegacyRoadWidth*.5+MarginCm))) return false;
    return true;
}

void AADRegionalWorld::LoadCell(int32 Index)
{
    FCell& Cell = Cells[Index];
    if (Cell.bLoaded) return;
    const FRegion& Region = Regions[Cell.Region];
    FRandomStream Random(Cell.Seed);
    TMap<FName,UInstancedStaticMeshComponent*> Batches;
    const auto Piece = [this,&Cell,&Batches,Index](FName Material, FVector Position, FVector Size,
        int32 Shape = 0, FRotator Rotation = FRotator::ZeroRotator, UStaticMesh* MeshOverride = nullptr)
    {
        UStaticMesh* Mesh = MeshOverride ? MeshOverride : Shape == 1 ? Cylinder.Get() : Shape == 2 ? Sphere.Get() : Cube.Get();
        if (!Mesh || Size.ContainsNaN() || Size.GetMin() <= 0.f) return;
        const FName Key = MeshOverride
            ? FName(*FString::Printf(TEXT("%s_%s"),*Material.ToString(),*Mesh->GetName()))
            : FName(*FString::Printf(TEXT("%s_%d"),*Material.ToString(),Shape));
        UInstancedStaticMeshComponent*& Batch = Batches.FindOrAdd(Key);
        if (!Batch)
        {
            // Unloading renames destroyed components through Unreal's normal
            // UObject lifecycle; use a unique object name for later reloads.
            const FName Name = MakeUniqueObjectName(this,UInstancedStaticMeshComponent::StaticClass(),
                FName(*FString::Printf(TEXT("Cell_%d_%s"),Index,*Key.ToString())));
            UMaterialInterface* OverrideMaterial = Materials.FindRef(Material);
            Batch = CreateBatch(Name,OverrideMaterial,Mesh,false);
            ActiveComponents.Add(Batch);
            Cell.Components.Add(Batch);
        }
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const FVector MeshSize = Bounds.BoxExtent*2.f;
        if (MeshSize.ContainsNaN() || MeshSize.GetMin() <= KINDA_SMALL_NUMBER) return;
        const FVector Scale = Size/MeshSize;
        const FVector Origin = Position-Rotation.RotateVector(Bounds.Origin*Scale);
        Batch->AddInstance(FTransform(Rotation,Origin,Scale));
    };
    TArray<FBox2D> Footprints;
    int32 Placed = 0;
    for (int32 Attempt = 0; Attempt < Region.PropsPerCell*10 && Placed < Region.PropsPerCell; ++Attempt)
    {
        const bool bTree = Region.Style == TEXT("countryside");
        const bool bMountain = Region.Style == TEXT("mountain");
        const bool bDesert = Region.Style == TEXT("desert");
        const float Radius = bTree ? 520.f : bMountain ? Random.FRandRange(1400,2300) : bDesert ? Random.FRandRange(700,1500) : Random.FRandRange(850,1400);
        if (Cell.Bounds.GetSize().X <= Radius*2+400 || Cell.Bounds.GetSize().Y <= Radius*2+400) continue;
        const FVector2D P(Random.FRandRange(Cell.Bounds.Min.X+Radius+200,Cell.Bounds.Max.X-Radius-200),
            Random.FRandRange(Cell.Bounds.Min.Y+Radius+200,Cell.Bounds.Max.Y-Radius-200));
        const FBox2D Footprint(P-FVector2D(Radius),P+FVector2D(Radius));
        if (!IsRoadClear(Footprint,650.)) continue;
        bool bOverlap = false;
        for (const FBox2D& Other : Footprints)
            if (StrictlyOverlaps(Footprint.ExpandBy(180.),Other)) { bOverlap = true; break; }
        if (bOverlap) continue;
        Footprints.Add(Footprint);
        ++Placed;
        if (bTree)
        {
            const float Height = Random.FRandRange(700,1400);
            Piece(TEXT("Wood"),FVector(P,Height*.38),FVector(60,60,Height*.76),1);
            Piece(TEXT("Foliage"),FVector(P,Height*.74),FVector(850,850,Height*.66),2);
            Piece(TEXT("Foliage"),FVector(P,Height*.98),FVector(540,540,Height*.48),2);
        }
        else if (bMountain)
        {
            const float Height = Random.FRandRange(1800,4200);
            Piece(TEXT("Rock"),FVector(P,Height*.21),FVector(Radius*1.9,Radius*1.9,Height),2);
            Piece(TEXT("Rock"),FVector(P+FVector2D(Radius*.18,-Radius*.13),Height*.5),FVector(Radius*1.1,Radius*1.1,Height*.72),2);
        }
        else if (bDesert)
        {
            const float Height = Random.FRandRange(400,1400);
            Piece(TEXT("Sand"),FVector(P,Height*.12),FVector(Radius*1.96,Radius*1.96,Height),2);
            Piece(TEXT("Rock"),FVector(P,Height*.54),FVector(Radius*.57,Radius*.53,Height*.7),2);
        }
        else if (Region.Style == TEXT("industrial"))
        {
            const float Height = Random.FRandRange(1500.f,2600.f);
            UStaticMesh* Warehouse = IndustrialWarehouses[Random.RandRange(0,IndustrialWarehouses.Num()-1)].Get();
            Piece(TEXT("KenneyIndustrial"),FVector(P,Height*.5f),FVector(Radius*1.25f,Radius*1.18f,Height),
                0,FRotator(0,Random.FRandRange(-180.f,180.f),0),Warehouse);

            // Yard dressing reuses imported kit meshes and stays inside the
            // cell footprint so it cannot spill onto a neighboring road.
            if (Random.FRand()<.52f)
            {
                if (Random.FRand()<.58f)
                {
                    UStaticMesh* Container=IndustrialContainers[Random.RandRange(0,IndustrialContainers.Num()-1)].Get();
                    const FVector2D Offset(Radius*.84f,Radius*Random.FRandRange(-.12f,.12f));
                    Piece(TEXT("KenneyIndustrial"),FVector(P+Offset,Radius*.14f),
                        FVector(Radius*.26f,Radius*.68f,Radius*.28f),0,
                        FRotator(0,Random.RandRange(0,3)*90.f,0),Container);
                }
                else
                {
                    const int32 DetailIndex=Random.RandRange(0,IndustrialDetails.Num()-1);
                    UStaticMesh* Detail=IndustrialDetails[DetailIndex].Get();
                    const float Width=Radius*(DetailIndex==1 ? .48f : .32f);
                    const float DetailHeight=Radius*(DetailIndex==0 ? .92f : DetailIndex==1 ? .42f : 1.12f);
                    const FVector2D Offset(-Radius*.82f,Radius*Random.FRandRange(-.14f,.14f));
                    Piece(TEXT("KenneyIndustrial"),FVector(P+Offset,DetailHeight*.5f),
                        FVector(Width,Width,DetailHeight),0,FRotator::ZeroRotator,Detail);
                }
            }
        }
        else if (Region.Style == TEXT("downtown"))
        {
            const bool bSkyscraper=Random.FRand()<.72f;
            const float Height=bSkyscraper ? Random.FRandRange(6500.f,11500.f) : Random.FRandRange(2600.f,4800.f);
            UStaticMesh* Building=bSkyscraper
                ? CommercialSkyscrapers[Random.RandRange(0,CommercialSkyscrapers.Num()-1)].Get()
                : CommercialMidRises[Random.RandRange(0,CommercialMidRises.Num()-1)].Get();
            Piece(TEXT("KenneyCommercial"),FVector(P,Height*.5f),FVector(Radius*1.72f,Radius*1.54f,Height),
                0,FRotator(0,Random.FRandRange(-180.f,180.f),0),Building);
        }
        else if (Region.Style == TEXT("coast"))
        {
            const float Height=Random.FRandRange(1800.f,3800.f);
            UStaticMesh* Hotel=CommercialMidRises[Random.RandRange(0,CommercialMidRises.Num()-1)].Get();
            Piece(TEXT("KenneyCommercial"),FVector(P,Height*.5f),FVector(Radius*1.72f,Radius*1.45f,Height),
                0,FRotator(0,Random.FRandRange(-180.f,180.f),0),Hotel);
        }
        else // Motorsport paddock: open stands and pit structures.
        {
            Piece(TEXT("Metal"),FVector(P,620),FVector(Radius*1.8,Radius*1.5,100));
            for (const float Side : {-1.f,1.f})
                Piece(TEXT("Concrete"),FVector(P+FVector2D(Side*Radius*.73,0),285),FVector(80,Radius*1.4,570));
            for (int32 Step = 0; Step < 3; ++Step)
                Piece(TEXT("Concrete"),FVector(P+FVector2D(0,-Radius*.55+Step*Radius*.42),80.f+Step*135.f),
                    FVector(Radius*1.55,Radius*.38,150));
            Piece(TEXT("Rust"),FVector(P+FVector2D(0,Radius*.68),470),FVector(Radius*1.58,20,70));
            Piece(TEXT("KitBarrier"),FVector(P+FVector2D(Radius*.35f,-Radius*.8f),55.f),
                FVector(90.f,Radius*.72f,110.f),0,FRotator::ZeroRotator,ConstructionBarrier);
        }
    }
    Cell.bLoaded = true;
    ++LoadedCellCount;
}

void AADRegionalWorld::UnloadCell(int32 Index)
{
    FCell& Cell = Cells[Index];
    if (!Cell.bLoaded) return;
    for (const auto& Entry : Cell.Components)
    {
        if (UInstancedStaticMeshComponent* Component = Entry.Get())
        {
            ActiveComponents.Remove(Component);
            RemoveInstanceComponent(Component);
            Component->DestroyComponent();
        }
    }
    Cell.Components.Reset();
    Cell.bLoaded = false;
    --LoadedCellCount;
}

int32 AADRegionalWorld::GetLoadedKenneyInstanceCount() const
{
    int32 Count=0;
    for (const UInstancedStaticMeshComponent* Component : ActiveComponents)
        if (IsValid(Component) && IsValid(Component->GetStaticMesh())
            && Component->GetStaticMesh()->GetName().StartsWith(TEXT("SM_Kenney_")))
            Count+=Component->GetInstanceCount();
    return Count;
}

void AADRegionalWorld::BindVehicle(AADVehiclePawn* Car)
{
    Player = Car;
}

void AADRegionalWorld::BindAtmosphere(AADAtmosphere* Actor)
{
    Atmosphere = Actor;
}

void AADRegionalWorld::UpdateWeatherMaterials()
{
    const float Wetness = Atmosphere.IsValid() ? Atmosphere->GetWetness() : 0.f;
    WetAsphalt->SetScalarParameterValue(TEXT("Roughness"),FMath::Lerp(.27f,.075f,Wetness));
    WetAsphalt->SetVectorParameterValue(TEXT("BaseColor"),FLinearColor(.022f,.028f,.034f)*FMath::Lerp(1.f,.62f,Wetness));
    const double Hour = Atmosphere.IsValid() ? Atmosphere->GetHour() : 23.;
    const float SolarElevation = FMath::Sin(static_cast<float>((Hour-6.)*UE_DOUBLE_PI/12.));
    const float Night = 1.f-FMath::Clamp((SolarElevation+.12f)/.22f,0.f,1.f);
    Windows->SetVectorParameterValue(TEXT("EmissiveColor"),FLinearColor(1.8f,1.1f,.45f)*Night);
}

void AADRegionalWorld::UpdateStreaming()
{
    const AADVehiclePawn* Car = Player.Get();
    if (!IsValid(Car) || Car->IsInGarage()) return;
    const FVector2D Position(Car->GetActorLocation());
    CurrentDistrict = TEXT("Dockside");
    CurrentDistrictId = TEXT("nova_dockside");
    for (const FRegion& Region : Regions)
        if (PointInside(Position,FBox2D(Region.Min,Region.Max)))
        { CurrentDistrict = Region.Name; CurrentDistrictId = Region.Id; break; }
    const double Started = FPlatformTime::Seconds();
    int32 Operations = 0;
    for (int32 Index = 0; Index < Cells.Num() && Operations < CellOperationsPerUpdate; ++Index)
    {
        if (!Cells[Index].bLoaded || DistanceToBox(Position,Cells[Index].Bounds) <= UnloadRadiusCm) continue;
        UnloadCell(Index);
        ++Operations;
        if ((FPlatformTime::Seconds()-Started)*1000. >= UpdateBudgetMs) return;
    }
    if (LoadedCellCount >= MaximumLoadedCells || Operations >= CellOperationsPerUpdate) return;
    CellPriority.Sort([this,Position](int32 A,int32 B)
    { return DistanceToBox(Position,Cells[A].Bounds) < DistanceToBox(Position,Cells[B].Bounds); });
    for (const int32 Index : CellPriority)
    {
        if (Operations >= CellOperationsPerUpdate || LoadedCellCount >= MaximumLoadedCells) break;
        if (Cells[Index].bLoaded) continue;
        if (DistanceToBox(Position,Cells[Index].Bounds) > LoadRadiusCm) break;
        LoadCell(Index);
        ++Operations;
        if ((FPlatformTime::Seconds()-Started)*1000. >= UpdateBudgetMs) break;
    }
}

void AADRegionalWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bReady || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    TickElapsed += DeltaSeconds;
    if (TickElapsed < .1f) return;
    TickElapsed = 0.f;
    UpdateWeatherMaterials();
    UpdateStreaming();
}
