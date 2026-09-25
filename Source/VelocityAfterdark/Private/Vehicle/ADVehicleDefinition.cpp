#include "Vehicle/ADVehicleDefinition.h"

#include "Core/ADVehicleMath.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, float& Out,
    double Minimum, double Maximum, FString& Error)
{
    double Value = 0.0;
    if (!Object.IsValid() || !Object->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value)
        || Value < Minimum || Value > Maximum)
    {
        Error = FString::Printf(TEXT("Invalid or missing '%s'; expected a number in [%g, %g]."), Key, Minimum, Maximum);
        return false;
    }
    Out = static_cast<float>(Value);
    return true;
}

bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out, FString& Error)
{
    if (!Object->TryGetStringField(Key, Out) || Out.TrimStartAndEnd().IsEmpty() || Out.Len() > 128)
    {
        Error = FString::Printf(TEXT("Invalid or missing '%s'; expected 1-128 characters."), Key);
        return false;
    }
    return true;
}

bool ReadVector(const TSharedPtr<FJsonValue>& Value, FVector& Out, const TCHAR* Key, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Value.IsValid() || !Value->TryGetArray(Items) || !Items || Items->Num() != 3)
    {
        Error = FString::Printf(TEXT("'%s' must contain exactly three numbers."), Key);
        return false;
    }
    double Coordinates[3] = {};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!(*Items)[Index].IsValid() || !(*Items)[Index]->TryGetNumber(Coordinates[Index])
            || !FMath::IsFinite(Coordinates[Index]) || FMath::Abs(Coordinates[Index]) > 1000.0)
        {
            Error = FString::Printf(TEXT("'%s' has an invalid coordinate."), Key);
            return false;
        }
    }
    Out = FVector(Coordinates[0], Coordinates[1], Coordinates[2]);
    return true;
}
}

bool FADVehicleDefinition::LoadFromJson(const FString& AbsolutePath, FString& OutError)
{
    OutError.Reset();
    const int64 FileSize=IFileManager::Get().FileSize(*AbsolutePath);
    if (FileSize < 1 || FileSize > 256*1024)
    {
        OutError=TEXT("Vehicle definition is missing, empty, or larger than 256 KB.");
        return false;
    }
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *AbsolutePath))
    {
        OutError = FString::Printf(TEXT("Vehicle definition could not be read: %s"), *AbsolutePath);
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = FString::Printf(TEXT("Vehicle definition contains invalid JSON: %s"), *Reader->GetErrorMessage());
        return false;
    }

    FADVehicleDefinition Candidate;
    float SchemaVersion = 0.f;
    if (!ReadNumber(Root, TEXT("schemaVersion"), SchemaVersion, 1.0, 1.0, OutError)
        || !ReadString(Root, TEXT("vehicleId"), Candidate.VehicleId, OutError)
        || !ReadString(Root, TEXT("name"), Candidate.Name, OutError)
        || !ReadString(Root, TEXT("manufacturer"), Candidate.Manufacturer, OutError)) return false;
    FString DrivetrainString;
    if (!ReadString(Root, TEXT("drivetrain"), DrivetrainString, OutError)) return false;
    if (DrivetrainString == TEXT("FWD")) Candidate.Drivetrain = EADDrivetrain::FrontWheelDrive;
    else if (DrivetrainString == TEXT("RWD")) Candidate.Drivetrain = EADDrivetrain::RearWheelDrive;
    else if (DrivetrainString == TEXT("AWD")) Candidate.Drivetrain = EADDrivetrain::AllWheelDrive;
    else { OutError = TEXT("drivetrain must be FWD, RWD, or AWD."); return false; }
    if (Root->HasField(TEXT("bodyStyle")) && !ReadString(Root,TEXT("bodyStyle"),Candidate.BodyStyle,OutError)) return false;
    if (Root->HasField(TEXT("bodyLengthCm"))
        && !ReadNumber(Root,TEXT("bodyLengthCm"),Candidate.BodyLengthCm,320,650,OutError)) return false;
    if (Root->HasField(TEXT("bodyWidthCm"))
        && !ReadNumber(Root,TEXT("bodyWidthCm"),Candidate.BodyWidthCm,140,260,OutError)) return false;
    if (Root->HasField(TEXT("engineCylinders")))
    {
        float Cylinders=0.f;
        if (!ReadNumber(Root,TEXT("engineCylinders"),Cylinders,4,8,OutError) || Cylinders!=FMath::FloorToFloat(Cylinders))
        { OutError=TEXT("engineCylinders must be an integer: 4, 6, or 8."); return false; }
        Candidate.EngineCylinders=static_cast<int32>(Cylinders);
    }

    struct FScalarField { const TCHAR* Key; float* Destination; double Minimum; double Maximum; };
    const FScalarField Fields[] = {
        {TEXT("massKg"), &Candidate.MassKg, 500, 5000},
        {TEXT("idleRpm"), &Candidate.IdleRpm, 400, 2500},
        {TEXT("redlineRpm"), &Candidate.RedlineRpm, 3000, 16000},
        {TEXT("reverseGearRatio"), &Candidate.ReverseGearRatio, .2, 8},
        {TEXT("finalDrive"), &Candidate.FinalDrive, 1, 8},
        {TEXT("drivetrainEfficiency"), &Candidate.DrivetrainEfficiency, .4, 1},
        {TEXT("engineBrakingNm"), &Candidate.EngineBrakingNm, 0, 500},
        {TEXT("upshiftRpm"), &Candidate.UpshiftRpm, 1500, 16000},
        {TEXT("downshiftRpm"), &Candidate.DownshiftRpm, 500, 14000},
        {TEXT("shiftTimeSeconds"), &Candidate.ShiftTimeSeconds, .05, 1},
        {TEXT("wheelRadiusM"), &Candidate.WheelRadiusM, .2, .65},
        {TEXT("suspensionRestLengthM"), &Candidate.SuspensionRestLengthM, .1, .8},
        {TEXT("suspensionTravelM"), &Candidate.SuspensionTravelM, .03, .6},
        {TEXT("springRateNPerM"), &Candidate.SpringRateNPerM, 10000, 150000},
        {TEXT("dampingNsPerM"), &Candidate.DampingNsPerM, 500, 20000},
        {TEXT("tireFriction"), &Candidate.TireFriction, .2, 2.5},
        {TEXT("tireLoadSensitivity"), &Candidate.TireLoadSensitivity, 0, .5},
        {TEXT("lateralStiffnessNPerRad"), &Candidate.LateralStiffnessNPerRad, 1000, 100000},
        {TEXT("rollingResistance"), &Candidate.RollingResistance, 0, .1},
        {TEXT("maxBrakeForceN"), &Candidate.MaxBrakeForceN, 1000, 100000},
        {TEXT("frontBrakeBias"), &Candidate.FrontBrakeBias, .3, .85},
        {TEXT("dragCoefficient"), &Candidate.DragCoefficient, .1, 2},
        {TEXT("frontalAreaM2"), &Candidate.FrontalAreaM2, 1, 8},
        {TEXT("downforceCoefficient"), &Candidate.DownforceCoefficient, 0, 3},
        {TEXT("maxSteeringDegrees"), &Candidate.MaxSteeringDegrees, 10, 55},
        {TEXT("highSpeedSteeringDegrees"), &Candidate.HighSpeedSteeringDegrees, 1, 20},
        {TEXT("steeringFalloffMps"), &Candidate.SteeringFalloffMps, 5, 60},
        {TEXT("steeringResponsePerSecond"), &Candidate.SteeringResponsePerSecond, 1, 20}
    };
    for (const FScalarField& Field : Fields)
        if (!ReadNumber(Root, Field.Key, *Field.Destination, Field.Minimum, Field.Maximum, OutError)) return false;

    if (Candidate.IdleRpm >= Candidate.DownshiftRpm || Candidate.DownshiftRpm >= Candidate.UpshiftRpm
        || Candidate.UpshiftRpm >= Candidate.RedlineRpm || Candidate.SuspensionTravelM >= Candidate.SuspensionRestLengthM
        || Candidate.HighSpeedSteeringDegrees > Candidate.MaxSteeringDegrees)
    {
        OutError = TEXT("Vehicle RPM thresholds, steering limits, or suspension lengths are inconsistent.");
        return false;
    }
    const double StaticCompressionM = Candidate.MassKg * ADVehicleMath::Gravity / (4.0 * Candidate.SpringRateNPerM);
    if (StaticCompressionM >= Candidate.SuspensionTravelM * .8)
    {
        OutError = TEXT("Springs cannot support vehicle mass with sufficient bump travel.");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Root->TryGetArrayField(TEXT("gearRatios"), Items) || !Items || Items->Num() < 2 || Items->Num() > 10)
    { OutError = TEXT("gearRatios must contain 2-10 positive descending ratios."); return false; }
    double PreviousRatio = 20.0;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        double Ratio = 0.0;
        if (!Item.IsValid() || !Item->TryGetNumber(Ratio) || !FMath::IsFinite(Ratio)
            || Ratio < .2 || Ratio > 8.0 || Ratio >= PreviousRatio)
        { OutError = TEXT("gearRatios must be finite, positive, and strictly descending."); return false; }
        Candidate.GearRatios.Add(static_cast<float>(Ratio));
        PreviousRatio = Ratio;
    }

    if (!Root->TryGetArrayField(TEXT("torqueCurve"), Items) || !Items || Items->Num() < 2 || Items->Num() > 64)
    { OutError = TEXT("torqueCurve must contain 2-64 ordered RPM/torque points."); return false; }
    float PreviousRpm = -1.f;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* PointObject = nullptr;
        if (!Item.IsValid() || !Item->TryGetObject(PointObject) || !PointObject)
        { OutError = TEXT("Each torqueCurve point must be an object."); return false; }
        FADTorquePoint Point;
        if (!ReadNumber(*PointObject, TEXT("rpm"), Point.Rpm, 0, 20000, OutError)
            || !ReadNumber(*PointObject, TEXT("torqueNm"), Point.TorqueNm, 1, 2000, OutError)) return false;
        if (Point.Rpm <= PreviousRpm)
        { OutError = TEXT("torqueCurve RPM points must be strictly increasing."); return false; }
        Candidate.TorqueCurve.Add(Point);
        PreviousRpm = Point.Rpm;
    }
    if (Candidate.TorqueCurve[0].Rpm > Candidate.IdleRpm || Candidate.TorqueCurve.Last().Rpm < Candidate.RedlineRpm)
    { OutError = TEXT("torqueCurve must cover idle through redline."); return false; }

    if (!Root->TryGetArrayField(TEXT("wheelAnchorsCm"), Items) || !Items || Items->Num() != 4)
    { OutError = TEXT("wheelAnchorsCm must contain FL, FR, RL, RR coordinates."); return false; }
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        FVector Anchor;
        if (!ReadVector(Item, Anchor, TEXT("wheelAnchorsCm"), OutError)) return false;
        Candidate.WheelAnchorsCm.Add(Anchor);
    }
    if (Candidate.WheelAnchorsCm[0].X <= Candidate.WheelAnchorsCm[2].X
        || Candidate.WheelAnchorsCm[1].X <= Candidate.WheelAnchorsCm[3].X
        || Candidate.WheelAnchorsCm[0].Y >= Candidate.WheelAnchorsCm[1].Y
        || Candidate.WheelAnchorsCm[2].Y >= Candidate.WheelAnchorsCm[3].Y)
    { OutError = TEXT("wheelAnchorsCm must use X forward, Y right, and FL/FR/RL/RR ordering."); return false; }
    const TSharedPtr<FJsonValue>* CenterOfMass = Root->Values.Find(TEXT("centerOfMassOffsetCm"));
    if (!CenterOfMass || !ReadVector(*CenterOfMass, Candidate.CenterOfMassOffsetCm, TEXT("centerOfMassOffsetCm"), OutError))
    { if (OutError.IsEmpty()) OutError = TEXT("centerOfMassOffsetCm is required."); return false; }

    if (!Candidate.Validate(OutError)) return false;
    *this = MoveTemp(Candidate);
    return true;
}

bool FADVehicleDefinition::Validate(FString& OutError) const
{
    OutError.Reset();
    const auto InRange=[](double V,double Minimum,double Maximum) { return FMath::IsFinite(V) && V>=Minimum && V<=Maximum; };
    if (VehicleId.IsEmpty() || VehicleId.Len()>128 || Name.IsEmpty() || Manufacturer.IsEmpty()
        || (BodyStyle!=TEXT("coupe") && BodyStyle!=TEXT("hatchback") && BodyStyle!=TEXT("sedan")
            && BodyStyle!=TEXT("muscle") && BodyStyle!=TEXT("hypercar"))
        || (EngineCylinders!=4 && EngineCylinders!=6 && EngineCylinders!=8)
        || static_cast<uint8>(Drivetrain)>static_cast<uint8>(EADDrivetrain::AllWheelDrive))
    { OutError=TEXT("Invalid vehicle identity, body style, cylinder count, or drivetrain."); return false; }
    struct FBound { double Value,Minimum,Maximum; };
    const FBound Bounds[] = {
        {BodyLengthCm,320,650},{BodyWidthCm,140,260},
        {MassKg,500,5000},{IdleRpm,400,2500},{RedlineRpm,3000,16000},
        {ReverseGearRatio,.2,8},{FinalDrive,1,8},{DrivetrainEfficiency,.4,1},{EngineBrakingNm,0,500},
        {UpshiftRpm,1500,16000},{DownshiftRpm,500,14000},{ShiftTimeSeconds,.05,1},
        {WheelRadiusM,.2,.65},{SuspensionRestLengthM,.1,.8},{SuspensionTravelM,.03,.6},
        {SpringRateNPerM,10000,150000},{DampingNsPerM,500,20000},{TireFriction,.2,2.5},
        {TireLoadSensitivity,0,.5},{LateralStiffnessNPerRad,1000,100000},{RollingResistance,0,.1},
        {MaxBrakeForceN,1000,100000},{FrontBrakeBias,.3,.85},{DragCoefficient,.1,2},
        {FrontalAreaM2,1,8},{DownforceCoefficient,0,3},{MaxSteeringDegrees,10,55},
        {HighSpeedSteeringDegrees,1,20},{SteeringFalloffMps,5,60},{SteeringResponsePerSecond,1,20}
    };
    for (const auto& B:Bounds) if (!InRange(B.Value,B.Minimum,B.Maximum))
    { OutError=TEXT("Vehicle contains an out-of-range or non-finite physical value."); return false; }
    if (IdleRpm>=DownshiftRpm || DownshiftRpm>=UpshiftRpm || UpshiftRpm>=RedlineRpm
        || SuspensionTravelM>=SuspensionRestLengthM || HighSpeedSteeringDegrees>MaxSteeringDegrees
        || MassKg*ADVehicleMath::Gravity/(4.*SpringRateNPerM)>=SuspensionTravelM*.8)
    { OutError=TEXT("Vehicle engine, steering, or suspension limits are inconsistent."); return false; }
    if (GearRatios.Num()<2 || GearRatios.Num()>10 || TorqueCurve.Num()<2 || TorqueCurve.Num()>64 || WheelAnchorsCm.Num()!=4)
    { OutError=TEXT("Vehicle requires valid gear ratios, torque curve, and four wheel contacts."); return false; }
    float PreviousRatio=20.f;
    for (float Ratio:GearRatios)
    {
        if (!InRange(Ratio,.2,8) || Ratio>=PreviousRatio) { OutError=TEXT("Invalid descending gear ratios."); return false; }
        PreviousRatio=Ratio;
    }
    float PreviousRpm=-1.f;
    for (const auto& Point:TorqueCurve)
    {
        if (!InRange(Point.Rpm,0,20000) || !InRange(Point.TorqueNm,1,2000) || Point.Rpm<=PreviousRpm)
        { OutError=TEXT("Invalid ordered engine curve."); return false; }
        PreviousRpm=Point.Rpm;
    }
    if (TorqueCurve[0].Rpm>IdleRpm || TorqueCurve.Last().Rpm<RedlineRpm)
    { OutError=TEXT("Engine curve must cover idle through redline."); return false; }
    const auto ValidVector=[&InRange](const FVector& V) { return InRange(V.X,-1000,1000) && InRange(V.Y,-1000,1000) && InRange(V.Z,-1000,1000); };
    if (!ValidVector(CenterOfMassOffsetCm)) { OutError=TEXT("Invalid center of mass."); return false; }
    for (const FVector& Anchor:WheelAnchorsCm) if (!ValidVector(Anchor)) { OutError=TEXT("Invalid wheel contact."); return false; }
    if (WheelAnchorsCm[0].X<=WheelAnchorsCm[2].X || WheelAnchorsCm[1].X<=WheelAnchorsCm[3].X
        || WheelAnchorsCm[0].Y>=WheelAnchorsCm[1].Y || WheelAnchorsCm[2].Y>=WheelAnchorsCm[3].Y)
    { OutError=TEXT("Wheel ordering must be FL, FR, RL, RR."); return false; }
    return true;
}

float FADVehicleDefinition::GetTorqueNm(float Rpm) const
{
    return static_cast<float>(ADVehicleMath::SampleTorque(TorqueCurve.GetData(), static_cast<size_t>(TorqueCurve.Num()), Rpm));
}
