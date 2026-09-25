#include "World/ADRoadNetwork.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr int32 MaximumRoads = 128;
constexpr int32 MaximumNodes = 1024;
constexpr int32 MaximumEdges = 4096;
constexpr double MaximumCoordinateCm = 250000.;
constexpr double MaximumProjectionCm = 15000.;

bool FinitePoint(FVector2D Point)
{
    return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
}

bool ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Minimum, double Maximum, double& Out)
{
    const auto Value = Object->TryGetField(Name);
    return Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(Out)
        && FMath::IsFinite(Out) && Out >= Minimum && Out <= Maximum;
}

bool ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FVector2D& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double X = 0., Y = 0.;
    if (!Object->TryGetArrayField(Name, Values) || !Values || Values->Num() != 2
        || !(*Values)[0].IsValid() || (*Values)[0]->Type != EJson::Number || !(*Values)[0]->TryGetNumber(X)
        || !(*Values)[1].IsValid() || (*Values)[1]->Type != EJson::Number || !(*Values)[1]->TryGetNumber(Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
    Out = FVector2D(X, Y);
    return true;
}

bool ValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 64) return false;
    for (TCHAR Character : Id)
        if (!(Character >= TEXT('a') && Character <= TEXT('z')) && !(Character >= TEXT('A') && Character <= TEXT('Z'))
            && !(Character >= TEXT('0') && Character <= TEXT('9')) && Character != TEXT('_')) return false;
    return true;
}

bool ReadCatalog(const FString& Path, bool bDistrict, TArray<FADRoadNetworkSegment>& OutRoads,
    TSet<FString>& Ids, FVector2D& OutBounds, FString& Error)
{
    const auto Fail = [&](const TCHAR* Reason)
    { Error = FString::Printf(TEXT("Road catalog %s: %s"), *FPaths::GetCleanFilename(Path), Reason); return false; };
    const int64 FileSize = IFileManager::Get().FileSize(*Path);
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (FileSize < 1 || FileSize > 131072 || !FFileHelper::LoadFileToString(Json, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
        return Fail(TEXT("requires a valid JSON object between 1 and 131072 bytes."));
    double Schema = 0., Width = 0.;
    if (!ReadNumber(Object, TEXT("schemaVersion"), 1., 1., Schema)
        || !ReadNumber(Object, TEXT("roadWidth"), 1200., 4000., Width)
        || !ReadPoint(Object, TEXT("groundHalfExtent"), OutBounds)
        || OutBounds.X < 2000. || OutBounds.Y < 2000. || OutBounds.X > MaximumCoordinateCm || OutBounds.Y > MaximumCoordinateCm)
        return Fail(TEXT("invalid schema, road width or finite ground bounds."));
    FString Units;
    const auto UnitValue = Object->TryGetField(TEXT("units"));
    if ((bDistrict || UnitValue.IsValid()) && (!UnitValue.IsValid() || UnitValue->Type != EJson::String
        || !UnitValue->TryGetString(Units) || Units != TEXT("centimeters")))
        return Fail(TEXT("road coordinates must use centimeters."));
    const TArray<TSharedPtr<FJsonValue>>* Roads = nullptr;
    if (!Object->TryGetArrayField(TEXT("roads"), Roads) || !Roads || Roads->IsEmpty()
        || Roads->Num() > MaximumRoads || OutRoads.Num() + Roads->Num() > MaximumRoads)
        return Fail(TEXT("requires roads, with at most 128 across both catalogs."));
    for (const auto& Entry : *Roads)
    {
        if (!Entry.IsValid() || Entry->Type != EJson::Object || !Entry->AsObject().IsValid())
            return Fail(TEXT("each road must be an object."));
        const auto Road = Entry->AsObject();
        const auto IdValue = Road->TryGetField(TEXT("id"));
        FString Id;
        FADRoadNetworkSegment Segment;
        if (!IdValue.IsValid() || IdValue->Type != EJson::String || !IdValue->TryGetString(Id) || !ValidId(Id)
            || Ids.Contains(Id) || !ReadPoint(Road, TEXT("start"), Segment.Start) || !ReadPoint(Road, TEXT("end"), Segment.End))
            return Fail(TEXT("road IDs must be unique and bounded; endpoints require two finite numbers."));
        if ((Segment.Start.X != Segment.End.X && Segment.Start.Y != Segment.End.Y)
            || (Segment.End - Segment.Start).Size() < 2000.)
            return Fail(TEXT("only axis-aligned centerlines at least 2000 cm long are supported."));
        for (const FVector2D Point : {Segment.Start, Segment.End})
            if (FMath::Abs(Point.X) + Width > OutBounds.X || FMath::Abs(Point.Y) + Width > OutBounds.Y)
                return Fail(TEXT("road extends outside the catalog's safe ground bounds."));
        Ids.Add(Id);
        OutRoads.Add(Segment);
    }
    return true;
}
}

bool FADRoadNetwork::LoadDefault(FString& OutError)
{
    const FString Directory = FPaths::ProjectContentDir() / TEXT("Data/World");
    return LoadCatalogs(Directory / TEXT("dockside.json"), Directory / TEXT("regions.json"), OutError);
}

bool FADRoadNetwork::LoadCatalogs(const FString& DistrictPath, const FString& RegionsPath, FString& OutError)
{
    TArray<FADRoadNetworkSegment> Combined;
    TSet<FString> Ids;
    FVector2D DistrictBounds, RegionalBounds;
    if (!ReadCatalog(DistrictPath, true, Combined, Ids, DistrictBounds, OutError)
        || !ReadCatalog(RegionsPath, false, Combined, Ids, RegionalBounds, OutError))
    { LoadError = OutError; return false; }
    if (RegionalBounds.X > DistrictBounds.X || RegionalBounds.Y > DistrictBounds.Y)
    { LoadError = OutError = TEXT("Regional road bounds exceed the resident district ground bounds."); return false; }
    return Build(Combined, OutError);
}

bool FADRoadNetwork::Build(const TArray<FADRoadNetworkSegment>& InSegments, FString& OutError)
{
    const auto Fail = [&](const TCHAR* Reason) { LoadError = OutError = Reason; return false; };
    if (InSegments.IsEmpty() || InSegments.Num() > MaximumRoads)
        return Fail(TEXT("Road graph requires between 1 and 128 input centerlines."));
    TArray<FADRoadNetworkSegment> NewSegments;
    NewSegments.Reserve(InSegments.Num());
    for (const auto& Input : InSegments)
    {
        if (!FinitePoint(Input.Start) || !FinitePoint(Input.End)
            || FMath::Abs(Input.Start.X) > MaximumCoordinateCm || FMath::Abs(Input.Start.Y) > MaximumCoordinateCm
            || FMath::Abs(Input.End.X) > MaximumCoordinateCm || FMath::Abs(Input.End.Y) > MaximumCoordinateCm)
            return Fail(TEXT("Road endpoints must be finite and within +/-250000 cm."));
        if (Input.Start.X != Input.End.X && Input.Start.Y != Input.End.Y)
            return Fail(TEXT("Road graph supports only axis-aligned centerlines; diagonal geometry is unsupported."));
        if ((Input.End - Input.Start).Size() < 1.) return Fail(TEXT("Road centerline is degenerate or shorter than 1 cm."));
        FADRoadNetworkSegment Normalized;
        Normalized.Start = FVector2D(FMath::Min(Input.Start.X, Input.End.X), FMath::Min(Input.Start.Y, Input.End.Y));
        Normalized.End = FVector2D(FMath::Max(Input.Start.X, Input.End.X), FMath::Max(Input.Start.Y, Input.End.Y));
        if (!NewSegments.ContainsByPredicate([&](const auto& Existing)
            { return Existing.Start == Normalized.Start && Existing.End == Normalized.End; })) NewSegments.Add(Normalized);
    }
    TArray<TArray<FVector2D>> SplitPoints;
    SplitPoints.SetNum(NewSegments.Num());
    for (int32 I = 0; I < NewSegments.Num(); ++I)
    {
        SplitPoints[I].Add(NewSegments[I].Start);
        SplitPoints[I].Add(NewSegments[I].End);
    }
    for (int32 I = 0; I < NewSegments.Num(); ++I)
    {
        const auto& A = NewSegments[I];
        const bool bHorizontalA = A.Start.Y == A.End.Y;
        for (int32 J = I + 1; J < NewSegments.Num(); ++J)
        {
            const auto& B = NewSegments[J];
            const bool bHorizontalB = B.Start.Y == B.End.Y;
            const auto AddShared = [&](FVector2D Point) { SplitPoints[I].AddUnique(Point); SplitPoints[J].AddUnique(Point); };
            if (bHorizontalA != bHorizontalB)
            {
                const auto& H = bHorizontalA ? A : B;
                const auto& V = bHorizontalA ? B : A;
                if (V.Start.X >= H.Start.X && V.Start.X <= H.End.X && H.Start.Y >= V.Start.Y && H.Start.Y <= V.End.Y)
                    AddShared(FVector2D(V.Start.X, H.Start.Y));
            }
            else if (bHorizontalA && A.Start.Y == B.Start.Y)
            {
                const double Low = FMath::Max(A.Start.X, B.Start.X), High = FMath::Min(A.End.X, B.End.X);
                if (Low <= High) { AddShared(FVector2D(Low, A.Start.Y)); AddShared(FVector2D(High, A.Start.Y)); }
            }
            else if (!bHorizontalA && A.Start.X == B.Start.X)
            {
                const double Low = FMath::Max(A.Start.Y, B.Start.Y), High = FMath::Min(A.End.Y, B.End.Y);
                if (Low <= High) { AddShared(FVector2D(A.Start.X, Low)); AddShared(FVector2D(A.Start.X, High)); }
            }
        }
    }
    TArray<FNode> NewNodes;
    TArray<FEdge> NewEdges;
    TSet<uint64> EdgeKeys;
    const auto NodeFor = [&](FVector2D Position) -> int32
    {
        const int32 Existing = NewNodes.IndexOfByPredicate([&](const FNode& Node) { return Node.Position == Position; });
        if (Existing != INDEX_NONE) return Existing;
        if (NewNodes.Num() >= MaximumNodes) return INDEX_NONE;
        FNode Node;
        Node.Position = Position;
        return NewNodes.Add(MoveTemp(Node));
    };
    for (int32 I = 0; I < NewSegments.Num(); ++I)
    {
        auto& Points = SplitPoints[I];
        const bool bHorizontal = NewSegments[I].Start.Y == NewSegments[I].End.Y;
        Points.Sort([bHorizontal](FVector2D A, FVector2D B) { return bHorizontal ? A.X < B.X : A.Y < B.Y; });
        for (int32 P = 1; P < Points.Num(); ++P)
        {
            const int32 A = NodeFor(Points[P - 1]), B = NodeFor(Points[P]);
            if (A == INDEX_NONE || B == INDEX_NONE) return Fail(TEXT("Road intersections exceed the 1024-node graph budget."));
            if (A == B) continue;
            const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
            if (EdgeKeys.Contains(Key)) continue;
            if (NewEdges.Num() >= MaximumEdges) return Fail(TEXT("Road graph exceeds the 4096-edge adjacency budget."));
            const double Length = (NewNodes[A].Position - NewNodes[B].Position).Size();
            if (!FMath::IsFinite(Length) || Length <= 0.) return Fail(TEXT("Road graph contains an invalid split edge."));
            EdgeKeys.Add(Key);
            NewEdges.Add(FEdge{A, B, Length});
            NewNodes[A].Neighbors.Add(FArc{B, Length});
            NewNodes[B].Neighbors.Add(FArc{A, Length});
        }
    }
    if (NewEdges.IsEmpty()) return Fail(TEXT("Road graph contains no traversable edges."));
    Segments = MoveTemp(NewSegments);
    Nodes = MoveTemp(NewNodes);
    Edges = MoveTemp(NewEdges);
    bReady = true;
    LoadError.Reset();
    OutError.Reset();
    return true;
}

bool FADRoadNetwork::BuildRoute(FVector2D Start, FVector2D Goal, TArray<FVector2D>& OutRoute,
    double& DistanceCm, FString& OutError) const
{
    OutRoute.Reset();
    DistanceCm = 0.;
    OutError.Reset();
    const auto Fail = [&](const TCHAR* Reason) { OutError = Reason; return false; };
    if (!bReady || Edges.IsEmpty()) return Fail(TEXT("Road network is not ready."));
    if (!FinitePoint(Start) || !FinitePoint(Goal)) return Fail(TEXT("Route endpoints must be finite coordinates."));
    struct FProjection { int32 Edge = INDEX_NONE; FVector2D Point = FVector2D::ZeroVector; double ErrorSquared = TNumericLimits<double>::Max(); };
    const auto Project = [&](FVector2D Point)
    {
        FProjection Best;
        for (int32 I = 0; I < Edges.Num(); ++I)
        {
            const auto& Edge = Edges[I];
            const FVector2D A = Nodes[Edge.A].Position, Delta = Nodes[Edge.B].Position - A;
            const double Alpha = FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / Delta.SizeSquared(), 0., 1.);
            const FVector2D Projected = A + Delta * Alpha;
            const double Error = (Point - Projected).SizeSquared();
            if (FMath::IsFinite(Error) && Error < Best.ErrorSquared)
            { Best.Edge = I; Best.Point = Projected; Best.ErrorSquared = Error; }
        }
        return Best;
    };
    const auto From = Project(Start), To = Project(Goal);
    if (From.Edge == INDEX_NONE || To.Edge == INDEX_NONE || From.ErrorSquared > FMath::Square(MaximumProjectionCm)
        || To.ErrorSquared > FMath::Square(MaximumProjectionCm))
        return Fail(TEXT("A route endpoint is more than 150 m from the road network."));
    if (From.Point == To.Point) { OutRoute.Add(From.Point); return true; }

    // Two temporary endpoint indices attach to existing split edges. Cached
    // junctions and adjacency remain untouched, including on disconnected queries.
    const int32 Source = Nodes.Num(), Destination = Source + 1, Count = Nodes.Num() + 2;
    TArray<double> Distances;
    TArray<int32> Previous;
    TArray<bool> Visited;
    Distances.Init(TNumericLimits<double>::Max(), Count);
    Previous.Init(INDEX_NONE, Count);
    Visited.Init(false, Count);
    Distances[Source] = 0.;
    const auto Relax = [&](int32 A, int32 B, double Cost)
    {
        const double Candidate = Distances[A] + Cost;
        if (!Visited[B] && Candidate < Distances[B]) { Distances[B] = Candidate; Previous[B] = A; }
    };
    const auto& StartEdge = Edges[From.Edge];
    const auto& GoalEdge = Edges[To.Edge];
    for (int32 Iteration = 0; Iteration < Count; ++Iteration)
    {
        int32 Current = INDEX_NONE;
        double Best = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Count; ++I)
            if (!Visited[I] && Distances[I] < Best) { Current = I; Best = Distances[I]; }
        if (Current == INDEX_NONE) break;
        Visited[Current] = true;
        if (Current == Destination) break;
        if (Current == Source)
        {
            Relax(Current, StartEdge.A, (From.Point - Nodes[StartEdge.A].Position).Size());
            Relax(Current, StartEdge.B, (From.Point - Nodes[StartEdge.B].Position).Size());
            if (From.Edge == To.Edge) Relax(Current, Destination, (To.Point - From.Point).Size());
        }
        else
        {
            for (const auto& Arc : Nodes[Current].Neighbors) Relax(Current, Arc.Node, Arc.LengthCm);
            if (Current == GoalEdge.A || Current == GoalEdge.B) Relax(Current, Destination, (Nodes[Current].Position - To.Point).Size());
        }
    }
    if (!Visited[Destination]) return Fail(TEXT("The projected route endpoints belong to disconnected road networks."));
    TArray<FVector2D> ReversePath;
    int32 Current = Destination;
    for (int32 Steps = 0; Steps < Count && Current != INDEX_NONE; ++Steps)
    {
        const FVector2D Point = Current == Source ? From.Point : Current == Destination ? To.Point : Nodes[Current].Position;
        if (ReversePath.IsEmpty() || ReversePath.Last() != Point) ReversePath.Add(Point);
        if (Current == Source) break;
        Current = Previous[Current];
    }
    if (Current != Source || ReversePath.IsEmpty()) return Fail(TEXT("Road route reconstruction failed."));
    Algo::Reverse(ReversePath);
    OutRoute = MoveTemp(ReversePath);
    DistanceCm = Distances[Destination];
    return true;
}
