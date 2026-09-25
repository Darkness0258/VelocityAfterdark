#include "Racing/ADRaceCatalog.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/ADRoadNetwork.h"

namespace
{
bool ReadObject(const FString& Path, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    const int64 Bytes = IFileManager::Get().FileSize(*Path);
    FString Json;
    if (Bytes < 1 || Bytes > 256 * 1024 || !FFileHelper::LoadFileToString(Json, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Out) || !Out.IsValid())
    {
        Error = FString::Printf(TEXT("Race catalog dependency is missing, malformed or larger than 256 KB: %s"), *Path);
        return false;
    }
    return true;
}

bool IsId(const FString& Text)
{
    if (Text.IsEmpty() || Text.Len() > 48) return false;
    for (const TCHAR C : Text)
        if ((C < TEXT('a') || C > TEXT('z')) && (C < TEXT('0') || C > TEXT('9')) && C != TEXT('_')) return false;
    return true;
}

bool ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector2D& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double X = 0., Y = 0.;
    if (!Object->TryGetArrayField(Key, Values) || !Values || Values->Num() != 2
        || !(*Values)[0].IsValid() || (*Values)[0]->Type != EJson::Number || !(*Values)[0]->TryGetNumber(X)
        || !(*Values)[1].IsValid() || (*Values)[1]->Type != EJson::Number || !(*Values)[1]->TryGetNumber(Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
    Out = FVector2D(X, Y);
    return true;
}

bool ReadRoadBounds(const FString& Path, TArray<FBox2D>& Out, FVector2D& Ground, FString& Error)
{
    TSharedPtr<FJsonObject> Object;
    if (!ReadObject(Path, Object, Error)) return false;
    const TSharedPtr<FJsonValue> WidthValue = Object->TryGetField(TEXT("roadWidth"));
    double Width = 0.;
    FVector2D Extent;
    const TArray<TSharedPtr<FJsonValue>>* Roads = nullptr;
    if (!WidthValue.IsValid() || WidthValue->Type != EJson::Number || !WidthValue->TryGetNumber(Width)
        || !FMath::IsFinite(Width) || Width < 1200. || Width > 4000.
        || !ReadPoint(Object, TEXT("groundHalfExtent"), Extent) || Extent.X < 10000. || Extent.Y < 10000.
        || Extent.X > 250000. || Extent.Y > 250000.
        || !Object->TryGetArrayField(TEXT("roads"), Roads) || !Roads || Roads->IsEmpty() || Roads->Num() > 128)
    {
        Error = TEXT("Race road coverage requires valid widths, ground bounds and 1-128 roads.");
        return false;
    }
    Ground.X = FMath::Min(Ground.X, Extent.X);
    Ground.Y = FMath::Min(Ground.Y, Extent.Y);
    for (const auto& Value : *Roads)
    {
        const TSharedPtr<FJsonObject>* Road = nullptr;
        FVector2D Start, End;
        if (!Value.IsValid() || !Value->TryGetObject(Road) || !Road || !Road->IsValid()
            || !ReadPoint(*Road, TEXT("start"), Start) || !ReadPoint(*Road, TEXT("end"), End))
        {
            Error = TEXT("Race road coverage found malformed road coordinates.");
            return false;
        }
        Out.Add(FBox2D(FVector2D(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y)) - FVector2D(Width * .5),
            FVector2D(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y)) + FVector2D(Width * .5)));
    }
    return true;
}

bool ValidateCoverage(const FADRaceDefinition& Race, const TArray<FBox2D>& Roads,
    FVector2D Ground, FString& Error)
{
    const auto OnRoad = [&Roads, Ground](FVector2D Point)
    {
        if (FMath::Abs(Point.X) >= Ground.X - 500. || FMath::Abs(Point.Y) >= Ground.Y - 500.) return false;
        for (const FBox2D& Road : Roads)
            if (Point.X >= Road.Min.X && Point.X <= Road.Max.X && Point.Y >= Road.Min.Y && Point.Y <= Road.Max.Y) return true;
        return false;
    };
    // Validate the complete polyline, not just authoring points. Four-metre lateral
    // clearance covers the driver's +/-3m lane preference plus the car half-width.
    // Road rectangles match the procedural district's actual asphalt union.
    for (double Distance = 0.; Distance < Race.RouteLengthM; Distance += 2.)
    {
        const FVector2D Point = Race.PointAtDistance(Distance);
        const FVector2D Forward = (Race.PointAtDistance(Distance + 1.) - Race.PointAtDistance(Distance - 1.)).GetSafeNormal();
        const FVector2D Right(-Forward.Y, Forward.X);
        if (!OnRoad(Point) || !OnRoad(Point - Right * 400.) || !OnRoad(Point + Right * 400.))
        {
            Error = FString::Printf(TEXT("Race '%s' leaves paved road coverage or lane clearance at %.1f metres."), *Race.Id, Distance);
            return false;
        }
    }
    for (const auto& Gate : Race.Checkpoints)
    {
        const FVector2D Center(Gate.Location.X, Gate.Location.Y), Right(-Gate.Forward.Y, Gate.Forward.X);
        if (!OnRoad(Center - Right * Gate.HalfWidthCm) || !OnRoad(Center + Right * Gate.HalfWidthCm))
        {
            Error = FString::Printf(TEXT("Race '%s' has a checkpoint wider than its paved road."), *Race.Id);
            return false;
        }
    }
    for (const FTransform& Slot : Race.Grid)
    {
        const FVector2D Center(Slot.GetLocation());
        const FVector2D Forward(Slot.GetUnitAxis(EAxis::X)), Right(-Forward.Y, Forward.X);
        for (const double X : {-240., 240.})
            for (const double Y : {-120., 120.})
                if (!OnRoad(Center + Forward * X + Right * Y))
                {
                    Error = FString::Printf(TEXT("Race '%s' has a starting car outside paved road coverage."), *Race.Id);
                    return false;
                }
    }
    return true;
}
}

const FADRaceDefinition* FADRaceCatalog::Find(const FString& RaceId) const
{
    return Races.FindByPredicate([&RaceId](const FADRaceDefinition& Race) { return Race.Id == RaceId; });
}

bool FADRaceCatalog::LoadDefault(FString& OutError)
{
    const FString Data = FPaths::ProjectContentDir() / TEXT("Data");
    return LoadFromJson(Data / TEXT("Races/catalog.json"), Data / TEXT("World/dockside.json"),
        Data / TEXT("World/regions.json"), OutError);
}

bool FADRaceCatalog::LoadFromJson(const FString& CatalogPath, const FString& DistrictPath,
    const FString& RegionsPath, FString& OutError)
{
    OutError.Reset();
    TSharedPtr<FJsonObject> Root;
    if (!ReadObject(CatalogPath, Root, OutError)) return false;
    const TSharedPtr<FJsonValue> Schema = Root->TryGetField(TEXT("schemaVersion"));
    double Version = 0.;
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (!Schema.IsValid() || Schema->Type != EJson::Number || !Schema->TryGetNumber(Version) || Version != 1.
        || !Root->TryGetArrayField(TEXT("races"), Entries) || !Entries || Entries->IsEmpty() || Entries->Num() > 32)
    {
        OutError = TEXT("Race catalog requires schema version 1 and 1-32 race entries.");
        return false;
    }
    // The road graph rejects degenerate or unsupported geometry; route queries
    // below check event connectivity as well as the usable asphalt width.
    FADRoadNetwork Network;
    if (!Network.LoadCatalogs(DistrictPath, RegionsPath, OutError)) return false;
    TArray<FBox2D> RoadBounds;
    FVector2D Ground(250000.);
    if (!ReadRoadBounds(DistrictPath, RoadBounds, Ground, OutError)
        || !ReadRoadBounds(RegionsPath, RoadBounds, Ground, OutError)) return false;
    TArray<FADRaceDefinition> Candidate;
    TSet<FString> Ids, Files;
    for (const auto& Entry : *Entries)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Id, File;
        if (!Entry.IsValid() || !Entry->TryGetObject(Object) || !Object || !Object->IsValid()
            || !(*Object)->TryGetStringField(TEXT("id"), Id) || !IsId(Id)
            || !(*Object)->TryGetStringField(TEXT("file"), File) || !File.EndsWith(TEXT(".json"), ESearchCase::CaseSensitive)
            || !IsId(File.LeftChop(5)) || Ids.Contains(Id) || Files.Contains(File))
        {
            OutError = TEXT("Race entries require unique lowercase IDs and local lowercase JSON filenames without path components.");
            return false;
        }
        FADRaceDefinition Race;
        if (!Race.LoadFromJson(FPaths::GetPath(CatalogPath) / File, OutError)) return false;
        if (Race.Id != Id)
        {
            OutError = FString::Printf(TEXT("Race catalog ID '%s' does not match definition '%s'."), *Id, *Race.Id);
            return false;
        }
        if (!ValidateCoverage(Race, RoadBounds, Ground, OutError)) return false;
        Ids.Add(Id);
        Files.Add(File);
        Candidate.Add(MoveTemp(Race));
    }
    const FVector2D Anchor(Candidate[0].Grid[0].GetLocation());
    for (const FADRaceDefinition& Race : Candidate)
    {
        TArray<FVector2D> AccessRoute;
        double AccessDistance = 0.;
        if (!Network.BuildRoute(Anchor,FVector2D(Race.Grid[0].GetLocation()),AccessRoute,AccessDistance,OutError))
        {
            OutError = FString::Printf(TEXT("Race '%s' is disconnected from the event road network: %s"),*Race.Id,*OutError);
            return false;
        }
    }
    Races = MoveTemp(Candidate);
    return true;
}
