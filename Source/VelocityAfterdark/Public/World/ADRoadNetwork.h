#pragma once

#include "CoreMinimal.h"

/** Two-way, planar road centerline in centimeters. Grade-separated or curved roads need a future schema. */
struct VELOCITYAFTERDARK_API FADRoadNetworkSegment
{
    FVector2D Start = FVector2D::ZeroVector;
    FVector2D End = FVector2D::ZeroVector;
};

/** Cached, non-UObject routing data. It has no world, actor, physics or tick dependencies.
 *  Build is bounded to 128 input roads and 1,024 junction nodes. Rebuild failure preserves
 *  the previous usable graph; query failure clears its output. No query rebuilds the graph.
 */
class VELOCITYAFTERDARK_API FADRoadNetwork
{
public:
    bool LoadDefault(FString& OutError);
    bool LoadCatalogs(const FString& DistrictPath, const FString& RegionsPath, FString& OutError);
    bool Build(const TArray<FADRoadNetworkSegment>& InSegments, FString& OutError);
    bool IsReady() const { return bReady; }
    const FString& GetLoadError() const { return LoadError; }
    // Normalized source centerlines, with exact duplicate segments removed.
    const TArray<FADRoadNetworkSegment>& GetSegments() const { return Segments; }
    int32 GetNodeCount() const { return Nodes.Num(); }

    // Start and goal project onto their nearest road, at most 15,000 cm away.
    // OutRoute starts/ends at those projections. DistanceCm excludes off-road connectors.
    // Equal-distance roads use stable source order. All roads are currently bidirectional.
    bool BuildRoute(FVector2D Start, FVector2D Goal, TArray<FVector2D>& OutRoute,
        double& DistanceCm, FString& OutError) const;

private:
    struct FArc
    {
        int32 Node = INDEX_NONE;
        double LengthCm = 0.;
    };
    struct FNode
    {
        FVector2D Position = FVector2D::ZeroVector;
        TArray<FArc> Neighbors;
    };
    struct FEdge
    {
        int32 A = INDEX_NONE;
        int32 B = INDEX_NONE;
        double LengthCm = 0.;
    };

    TArray<FADRoadNetworkSegment> Segments;
    TArray<FNode> Nodes;
    TArray<FEdge> Edges;
    FString LoadError;
    bool bReady = false;
};
