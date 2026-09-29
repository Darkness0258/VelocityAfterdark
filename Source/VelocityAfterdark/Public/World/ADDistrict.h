#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ADDistrict.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UStaticMesh;
class UMaterialInterface;

/** Phase 1 road data uses centimeter, axis-aligned centerlines. */
struct FADRoadSegment
{
    FString Id;
    FVector2D Start = FVector2D::ZeroVector;
    FVector2D End = FVector2D::ZeroVector;
};

struct FADDistrictBlock
{
    FString Id;
    FString Kind;
    FVector2D Min = FVector2D::ZeroVector;
    FVector2D Max = FVector2D::ZeroVector;
    int32 Columns = 1;
    int32 Rows = 1;
    float MinHeight = 1000.f;
    float MaxHeight = 1000.f;
};

/**
 * Deterministic, bounded art blockout for validating the driving foundation.
 * Geometry is instanced once at BeginPlay. This is not World Partition streaming;
 * later production districts replace this generator with authored streaming cells.
 */
UCLASS()
class VELOCITYAFTERDARK_API AADDistrict : public AActor
{
    GENERATED_BODY()

public:
    AADDistrict();
    virtual void BeginPlay() override;

    UFUNCTION(BlueprintPure, Category = "Afterdark|District")
    bool IsReady() const { return bReady; }

    UFUNCTION(BlueprintPure, Category = "Afterdark|District")
    FString GetLoadError() const { return LoadError; }

    UFUNCTION(BlueprintPure, Category = "Afterdark|District")
    float GetRoadLengthMeters() const { return RoadLengthMeters; }

    FVector2D GetGroundHalfExtent() const { return GroundHalfExtent; }
    int32 GetImportedDistrictMeshInstanceCount() const;

private:
    bool LoadDefinition();
    bool LoadDistrictArt();
    void BuildRoads();
    void BuildBlocks();
    void BuildStreetFurniture();
    bool IsIntersection(const FVector2D& Position, const FADRoadSegment& Current, float Padding = 0.f) const;
    bool IsOnRoad(const FVector2D& Position, float Padding = 0.f) const;
    UHierarchicalInstancedStaticMeshComponent* GetBatch(FName MaterialName, bool bCollision,
        bool bCylinder = false, UStaticMesh* MeshOverride = nullptr);
    void AddBox(FName MaterialName, const FVector& Center, const FVector& Size, bool bCollision = false, float Yaw = 0.f);
    void AddCylinder(FName MaterialName, const FVector& Center, const FVector& Size);
    void AddDistrictMesh(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, float Yaw = 0.f,
        FName MaterialName = TEXT("KenneyCityKit"));
    void AddBuildingCollider(const FVector& Center, const FVector& Size, float Yaw = 0.f);

    UPROPERTY(VisibleAnywhere, Category = "Afterdark|Lighting")
    TObjectPtr<UDirectionalLightComponent> Moon;
    UPROPERTY(VisibleAnywhere, Category = "Afterdark|Lighting")
    TObjectPtr<USkyLightComponent> Sky;
    UPROPERTY(VisibleAnywhere, Category = "Afterdark|Lighting")
    TObjectPtr<USkyAtmosphereComponent> Atmosphere;
    UPROPERTY(VisibleAnywhere, Category = "Afterdark|Lighting")
    TObjectPtr<UExponentialHeightFogComponent> Fog;
    UPROPERTY(VisibleAnywhere, Category = "Afterdark|Lighting")
    TObjectPtr<UPostProcessComponent> PostProcess;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> Cube;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> DowntownTowerMeshes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> DowntownMidriseMeshes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> IndustrialWarehouseMeshes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> ContainerMeshes;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> TrafficSignalMesh;
    // Hard reference lets the cooker discover the original CC0 static prop.
    UPROPERTY(EditDefaultsOnly, Category = "Afterdark|Environment", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UStaticMesh> ParkedCityCarMesh;
    // Optional, project-authored PBR replacements. The flat bootstrap materials
    // remain as safe fallbacks until the visual-material bootstrap has run.
    UPROPERTY(EditDefaultsOnly, Category = "Afterdark|Environment", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMaterialInterface> DetailedAsphaltMaterial;
    UPROPERTY(EditDefaultsOnly, Category = "Afterdark|Environment", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMaterialInterface> DetailedBuildingMaterial;
    UPROPERTY(EditDefaultsOnly, Category = "Afterdark|Environment", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMaterialInterface> DetailedConcreteMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ParkedCityCars;
    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Batches;
    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UMaterialInterface>> Materials;

    TArray<FADRoadSegment> Roads;
    TArray<FADDistrictBlock> Blocks;
    FVector2D GroundHalfExtent = FVector2D(48000., 34000.);
    float RoadWidth = 2400.f;
    float RoadLengthMeters = 0.f;
    float StreetLightSpacing = 4000.f;
    int32 MaxLocalLights = 24;
    int32 Seed = 4417;
    bool bReady = false;
    FString LoadError;
};
