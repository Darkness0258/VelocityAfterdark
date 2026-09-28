#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ADRegionalWorld.generated.h"

class AADAtmosphere;
class AADVehiclePawn;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

struct FADRegionalRoad
{
    FString Id;
    FVector2D Start = FVector2D::ZeroVector;
    FVector2D End = FVector2D::ZeroVector;
};

/** Resident road collision with budgeted, distance-streamed development scenery. */
UCLASS()
class VELOCITYAFTERDARK_API AADRegionalWorld : public AActor
{
    GENERATED_BODY()
public:
    AADRegionalWorld();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void BindVehicle(AADVehiclePawn* Car);
    void BindAtmosphere(AADAtmosphere* Actor);
    bool IsReady() const { return bReady; }
    const FString& GetLoadError() const { return LoadError; }
    FString GetCurrentDistrict() const { return CurrentDistrict; }
    FString GetCurrentDistrictId() const { return CurrentDistrictId; }
    int32 GetLoadedCellCount() const { return LoadedCellCount; }
    int32 GetTotalCellCount() const { return Cells.Num(); }
    int32 GetLoadedKenneyInstanceCount() const;
    int32 GetLoadedCollisionProxyCount() const;
    float GetRoadLengthMeters() const { return RoadLengthMeters; }
    FVector2D GetGroundHalfExtent() const { return GroundHalfExtent; }
    const TArray<FADRegionalRoad>& GetRoadSegments() const { return Roads; }
    bool IsOnRegionalRoad(FVector2D Position, double MarginCm = 0.) const;

private:
    struct FRegion
    {
        FString Id, Name, Style;
        FVector2D Min, Max;
        int32 Seed = 0;
        int32 PropsPerCell = 8;
    };
    struct FCell
    {
        FBox2D Bounds;
        int32 Region = 0;
        int32 Seed = 0;
        bool bLoaded = false;
        TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>> Components;
    };
    bool LoadDefinition();
    bool LoadMaterials();
    void BuildResidentRoads();
    void BuildCells();
    void UpdateStreaming();
    void UpdateWeatherMaterials();
    void LoadCell(int32 Index);
    void UnloadCell(int32 Index);
    bool IsRoadClear(FBox2D Footprint, double MarginCm) const;
    UInstancedStaticMeshComponent* CreateBatch(FName Name, UMaterialInterface* Material,
        UStaticMesh* Mesh, bool bCollision);
    UMaterialInstanceDynamic* MakeMaterial(const TCHAR* Name, const TCHAR* Asset,
        FLinearColor Color, float Roughness, float Metallic);
    void AddResident(FName Material, FVector Position, FVector Size, FRotator Rotation = FRotator::ZeroRotator);
    void AddResidentCollision(FName Surface, FVector Position, FVector Size,
        FRotator Rotation = FRotator::ZeroRotator);
    void AddResidentMesh(FName BatchKey, UStaticMesh* Mesh, FVector Position, FVector Size,
        FRotator Rotation = FRotator::ZeroRotator);

    UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> CommercialSkyscrapers;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> CommercialMidRises;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> IndustrialWarehouses;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> IndustrialContainers;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> IndustrialDetails;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> RoadLamps;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> TrafficSignal;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> ConstructionBarrier;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UInstancedStaticMeshComponent>> ResidentBatches;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UInstancedStaticMeshComponent>> ResidentCollisionBatches;
    UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> ActiveComponents;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WetAsphalt;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Windows;
    TWeakObjectPtr<AADVehiclePawn> Player;
    TWeakObjectPtr<AADAtmosphere> Atmosphere;
    TArray<FADRegionalRoad> Roads;
    TArray<FADRegionalRoad> LegacyRoads;
    TArray<FRegion> Regions;
    TArray<FCell> Cells;
    TArray<int32> CellPriority;
    FVector2D GroundHalfExtent = FVector2D(100000.,80000.);
    float RoadWidth = 2400.f;
    float LegacyRoadWidth = 2400.f;
    float CellSizeCm = 14000.f;
    float LoadRadiusCm = 28000.f;
    float UnloadRadiusCm = 36000.f;
    float UpdateBudgetMs = 2.f;
    float TickElapsed = 0.f;
    float RoadLengthMeters = 0.f;
    int32 CellOperationsPerUpdate = 2;
    int32 MaximumLoadedCells = 24;
    int32 LoadedCellCount = 0;
    FString CurrentDistrict = TEXT("Dockside");
    FString CurrentDistrictId = TEXT("nova_dockside");
    FString LoadError;
    bool bReady = false;
};
