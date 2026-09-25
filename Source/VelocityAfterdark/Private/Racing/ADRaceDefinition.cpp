#include "Racing/ADRaceDefinition.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out,
    double Minimum, double Maximum, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Key) : nullptr;
    if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Out) || !FMath::IsFinite(Out)
        || Out < Minimum || Out > Maximum)
    {
        Error = FString::Printf(TEXT("'%s' must be a finite number in [%g, %g]."), Key, Minimum, Maximum);
        return false;
    }
    return true;
}

bool String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Key) : nullptr;
    if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(Out)
        || Out.TrimStartAndEnd().IsEmpty() || Out.Len() > 128)
    {
        Error = FString::Printf(TEXT("'%s' must be a nonempty string of at most 128 characters."), Key);
        return false;
    }
    return true;
}

bool Array(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
    const TArray<TSharedPtr<FJsonValue>>*& Out, int32 Minimum, int32 Maximum, FString& Error)
{
    if (!Object.IsValid() || !Object->TryGetArrayField(Key, Out) || !Out || Out->Num() < Minimum || Out->Num() > Maximum)
    {
        Error = FString::Printf(TEXT("'%s' must be an array with %d-%d entries."), Key, Minimum, Maximum);
        return false;
    }
    return true;
}

bool Tuple(const TSharedPtr<FJsonValue>& Value, double* Out, int32 Count, const TCHAR* Label, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Value.IsValid() || !Value->TryGetArray(Values) || !Values || Values->Num() != Count)
    {
        Error = FString::Printf(TEXT("'%s' requires exactly %d numbers."), Label, Count);
        return false;
    }
    for (int32 I = 0; I < Count; ++I)
    {
        if (!(*Values)[I].IsValid() || (*Values)[I]->Type != EJson::Number
            || !(*Values)[I]->TryGetNumber(Out[I]) || !FMath::IsFinite(Out[I]))
        {
            Error = FString::Printf(TEXT("'%s' has a nonnumeric or nonfinite value at index %d."), Label, I);
            return false;
        }
    }
    return true;
}

bool Vector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out, FString& Error)
{
    double Values[3] = {};
    if (!Object.IsValid() || !Tuple(Object->TryGetField(Key), Values, 3, Key, Error)) return false;
    Out = FVector(Values[0], Values[1], Values[2]);
    return true;
}
}

FVector2D FADRaceDefinition::PointAtDistance(double DistanceM) const
{
    if (RoutePoints.Num() < 2 || RouteDistancesM.Num() != RoutePoints.Num()
        || !FMath::IsFinite(DistanceM) || RouteLengthM <= 0.0) return FVector2D::ZeroVector;
    const double Wrapped = FMath::Fmod(FMath::Fmod(DistanceM, RouteLengthM) + RouteLengthM, RouteLengthM);
    for (int32 I = RoutePoints.Num() - 1; I >= 0; --I)
    {
        if (Wrapped < RouteDistancesM[I]) continue;
        const int32 Next = (I + 1) % RoutePoints.Num();
        const double EndDistance = Next == 0 ? RouteLengthM : RouteDistancesM[Next];
        const double Interval = EndDistance - RouteDistancesM[I];
        if (Interval <= UE_SMALL_NUMBER) return RoutePoints[I].Position;
        return FMath::Lerp(RoutePoints[I].Position, RoutePoints[Next].Position,
            FMath::Clamp((Wrapped - RouteDistancesM[I]) / Interval, 0.0, 1.0));
    }
    return RoutePoints[0].Position;
}

double FADRaceDefinition::ClosestDistanceM(const FVector& Position, double& OutErrorM) const
{
    OutErrorM = TNumericLimits<double>::Max();
    if (RoutePoints.Num() < 2 || RouteDistancesM.Num() != RoutePoints.Num() || Position.ContainsNaN()) return 0.0;
    const FVector2D Point(Position.X, Position.Y);
    double Closest = 0.0;
    for (int32 I = 0; I < RoutePoints.Num(); ++I)
    {
        const FVector2D Start = RoutePoints[I].Position;
        const FVector2D Delta = RoutePoints[(I + 1) % RoutePoints.Num()].Position - Start;
        const double LengthSquared = Delta.SizeSquared();
        if (LengthSquared < UE_SMALL_NUMBER) continue;
        const double Alpha = FMath::Clamp(FVector2D::DotProduct(Point - Start, Delta) / LengthSquared, 0.0, 1.0);
        const double ErrorM = (Point - Start - Delta * Alpha).Size() * 0.01;
        if (ErrorM < OutErrorM)
        {
            OutErrorM = ErrorM;
            Closest = RouteDistancesM[I] + FMath::Sqrt(LengthSquared) * Alpha * 0.01;
        }
    }
    return RouteLengthM > 0.0 && Closest >= RouteLengthM ? 0.0 : Closest;
}

bool FADRaceDefinition::LoadFromJson(const FString& Path, FString& Error)
{
    Error.Reset();
    const int64 FileSize = IFileManager::Get().FileSize(*Path);
    if (FileSize < 1 || FileSize > 256 * 1024)
    {
        Error = TEXT("Race definition is missing, empty, or larger than 256 KB.");
        return false;
    }
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path))
    {
        Error = FString::Printf(TEXT("Could not read race definition: %s"), *Path);
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        Error = FString::Printf(TEXT("Invalid race JSON: %s"), *Reader->GetErrorMessage());
        return false;
    }
    FADRaceDefinition Candidate;
    double Schema = 0.0, LapsValue = 0.0;
    if (!Number(Root, TEXT("schemaVersion"), Schema, 1, 1, Error)
        || !String(Root, TEXT("id"), Candidate.Id, Error) || !String(Root, TEXT("name"), Candidate.Name, Error)
        || !Number(Root, TEXT("laps"), LapsValue, 1, 99, Error)
        || !Number(Root, TEXT("countdownSeconds"), Candidate.CountdownSeconds, 1, 10, Error)
        || !Number(Root, TEXT("timeoutSeconds"), Candidate.TimeoutSeconds, 60, 7200, Error)
        || !Number(Root, TEXT("recoveryPenaltySeconds"), Candidate.RecoveryPenaltySeconds, 1, 60, Error)) return false;
    if (LapsValue != FMath::FloorToDouble(LapsValue)) { Error = TEXT("laps must be an integer."); return false; }
    Candidate.Laps = static_cast<int32>(LapsValue);

    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Array(Root, TEXT("routePoints"), Items, 8, 256, Error)) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        double Values[3] = {};
        if (!Tuple(Item, Values, 3, TEXT("routePoints"), Error)) return false;
        if (FMath::Abs(Values[0]) > 250000 || FMath::Abs(Values[1]) > 250000 || Values[2] < 20 || Values[2] > 140)
        { Error = TEXT("routePoints must remain within 2.5 km of the origin and use speed limits of 20-140 km/h."); return false; }
        Candidate.RoutePoints.Add({FVector2D(Values[0], Values[1]), Values[2]});
    }
    for (int32 I = 0; I < Candidate.RoutePoints.Num(); ++I)
    {
        const int32 Next = (I + 1) % Candidate.RoutePoints.Num();
        const double SegmentM = (Candidate.RoutePoints[Next].Position - Candidate.RoutePoints[I].Position).Size() * 0.01;
        if (SegmentM < 1 || SegmentM > 1000)
        { Error = FString::Printf(TEXT("Route segment %d must be 1-1000 metres long."), I); return false; }
        Candidate.RouteDistancesM.Add(Candidate.RouteLengthM);
        Candidate.RouteLengthM += SegmentM;
    }
    if (Candidate.RouteLengthM < 500 || Candidate.RouteLengthM > 20000)
    { Error = TEXT("Race loop must be 500-20000 metres long."); return false; }
    // A time limit below theoretical full-throttle travel would make valid content impossible to finish.
    if (Candidate.TimeoutSeconds < Candidate.RouteLengthM * Candidate.Laps / (140.0 / 3.6))
    { Error = TEXT("Race timeout is shorter than the theoretical minimum completion time."); return false; }

    if (!Array(Root, TEXT("checkpoints"), Items, 4, 256, Error)) return false;
    double PreviousDistanceM = -1.0;
    for (int32 I = 0; I < Items->Num(); ++I)
    {
        const TSharedPtr<FJsonValue>& Item = (*Items)[I];
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Item.IsValid() || !Item->TryGetObject(Object) || !Object || !Object->IsValid())
        { Error = FString::Printf(TEXT("Checkpoint %d must be an object."), I); return false; }
        FADCheckpoint Gate;
        if (!Vector(*Object, TEXT("location"), Gate.Location, Error)
            || !Vector(*Object, TEXT("forward"), Gate.Forward, Error)
            || !Number(*Object, TEXT("halfWidthCm"), Gate.HalfWidthCm, 300, 1100, Error)
            || !Number(*Object, TEXT("halfHeightCm"), Gate.HalfHeightCm, 100, 400, Error)
            || !Number(*Object, TEXT("distanceM"), Gate.DistanceM, 0, Candidate.RouteLengthM, Error)) return false;
        if (Gate.Location.Z < 30 || Gate.Location.Z > 200 || FMath::Abs(Gate.Forward.Z) > 0.001
            || FMath::Abs(Gate.Forward.SizeSquared() - 1.0) > 0.001)
        { Error = FString::Printf(TEXT("Checkpoint %d requires a 30-200 cm height and a unit horizontal forward vector."), I); return false; }
        if ((I == 0 && Gate.DistanceM != 0.0) || Gate.DistanceM <= PreviousDistanceM
            || Gate.DistanceM >= Candidate.RouteLengthM || (I > 0 && Gate.DistanceM - PreviousDistanceM < 20.0))
        { Error = TEXT("Checkpoint 0 must start at distance 0; later distances must increase by at least 20 metres and precede loop end."); return false; }
        double ErrorM = 0.0;
        const double ActualDistanceM = Candidate.ClosestDistanceM(Gate.Location, ErrorM);
        if (ErrorM > 0.5 || FMath::Abs(ActualDistanceM - Gate.DistanceM) > 0.5)
        { Error = FString::Printf(TEXT("Checkpoint %d must lie on its declared route distance (within 0.5 metres)."), I); return false; }
        const FVector2D Tangent = (Candidate.PointAtDistance(Gate.DistanceM + 1.0)
            - Candidate.PointAtDistance(Gate.DistanceM - 1.0)).GetSafeNormal();
        if (FVector2D::DotProduct(Tangent, FVector2D(Gate.Forward.X, Gate.Forward.Y)) < 0.95)
        { Error = FString::Printf(TEXT("Checkpoint %d faces against the route direction."), I); return false; }
        Candidate.Checkpoints.Add(Gate);
        PreviousDistanceM = Gate.DistanceM;
    }
    if (Candidate.RouteLengthM - PreviousDistanceM < 20.0)
    { Error = TEXT("Last checkpoint must be at least 20 metres before the finish line."); return false; }

    if (!Array(Root, TEXT("opponents"), Items, 1, 15, Error)) return false;
    TSet<FString> Names;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Item.IsValid() || !Item->TryGetObject(Object) || !Object || !Object->IsValid())
        { Error = TEXT("Each opponent must be an object."); return false; }
        FADRaceOpponent Opponent;
        double Color[3] = {}, Speed = 0.0, Lane = 0.0;
        if (!String(*Object, TEXT("name"), Opponent.Name, Error)
            || !Tuple((*Object)->TryGetField(TEXT("color")), Color, 3, TEXT("color"), Error)
            || !Number(*Object, TEXT("speedScale"), Speed, 0.6, 1.2, Error)
            || !Number(*Object, TEXT("laneOffsetCm"), Lane, -300, 300, Error)) return false;
        if (Names.Contains(Opponent.Name.ToLower())) { Error = TEXT("Opponent names must be unique."); return false; }
        Names.Add(Opponent.Name.ToLower());
        if (Color[0] < 0 || Color[0] > 1 || Color[1] < 0 || Color[1] > 1 || Color[2] < 0 || Color[2] > 1)
        { Error = TEXT("Opponent color components must be in [0, 1]."); return false; }
        Opponent.Color = FLinearColor(static_cast<float>(Color[0]), static_cast<float>(Color[1]), static_cast<float>(Color[2]), 1.0f);
        Opponent.SpeedScale = static_cast<float>(Speed);
        Opponent.LaneOffsetCm = static_cast<float>(Lane);
        Candidate.Opponents.Add(Opponent);
    }

    if (!Array(Root, TEXT("grid"), Items, Candidate.Opponents.Num() + 1, Candidate.Opponents.Num() + 1, Error)) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        double Values[4] = {};
        if (!Tuple(Item, Values, 4, TEXT("grid"), Error)) return false;
        const FVector Position(Values[0], Values[1], Values[2]);
        const FVector FromStart = Position - Candidate.Checkpoints[0].Location;
        const double StartProjection = FVector::DotProduct(FromStart, Candidate.Checkpoints[0].Forward);
        double ErrorM = 0.0;
        Candidate.ClosestDistanceM(Position, ErrorM);
        if (Values[2] < 60 || Values[2] > 150 || FMath::Abs(Values[3]) > 180 || StartProjection > -500
            || StartProjection < -15000 || ErrorM > 8)
        { Error = TEXT("Grid entries must be 5-150 metres behind the start, within 8 metres of the route, with valid height/yaw."); return false; }
        const FTransform Transform(FRotator(0, Values[3], 0), Position);
        if (FVector::DotProduct(Transform.GetUnitAxis(EAxis::X), Candidate.Checkpoints[0].Forward) < 0.95)
        { Error = TEXT("Grid entries must face the start gate."); return false; }
        for (const FTransform& Existing : Candidate.Grid)
        {
            if (FVector::Dist2D(Existing.GetLocation(), Position) < 500)
            { Error = TEXT("Grid entries must be at least five metres apart to avoid overlapping cars."); return false; }
        }
        Candidate.Grid.Add(Transform);
    }

    if (!Array(Root, TEXT("difficultySpeedScales"), Items, 3, 3, Error)) return false;
    double PreviousScale = 0.0;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        double Scale = 0.0;
        if (!Item.IsValid() || Item->Type != EJson::Number || !Item->TryGetNumber(Scale)
            || !FMath::IsFinite(Scale) || Scale < 0.5 || Scale > 1.2 || Scale <= PreviousScale)
        { Error = TEXT("difficultySpeedScales must contain three strictly increasing values in [0.5, 1.2]."); return false; }
        Candidate.DifficultySpeedScales.Add(Scale);
        PreviousScale = Scale;
    }
    for (const auto& Opponent : Candidate.Opponents)
        for (const double Scale : Candidate.DifficultySpeedScales)
            if (Opponent.SpeedScale * Scale < .4 || Opponent.SpeedScale * Scale > 1.3)
            { Error = TEXT("Combined opponent and difficulty pace must fit the driver's [0.4, 1.3] envelope."); return false; }
    *this = MoveTemp(Candidate);
    return true;
}
