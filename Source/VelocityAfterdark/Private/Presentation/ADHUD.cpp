#include "Presentation/ADHUD.h"
#include "Player/ADPlayerController.h"
#include "Player/ADVehiclePawn.h"
#include "Vehicle/ADVehiclePhysicsComponent.h"
#include "Core/ADGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CanvasItem.h"
#include "Fonts/SlateFontInfo.h"
#include "Racing/ADRaceManager.h"
#include "Garage/ADGarageSessionComponent.h"
#include "World/ADAtmosphere.h"
#include "World/ADPoliceDirector.h"
#include "World/ADRegionalWorld.h"
#include "Career/ADCareerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Settings/ADSettingsSubsystem.h"
#include "Settings/ADInputBindings.h"
#include "Vehicle/ADVehicleEffectsComponent.h"
#include "Presentation/ADCinematicComponent.h"
#include "Online/ADOnlineSubsystem.h"
#include "Presentation/ADMapComponent.h"
#include "World/ADExplorationDirector.h"

namespace
{
    FString RaceTime(double Seconds)
    {
        const int64 Milliseconds=FMath::Max<int64>(0,FMath::RoundToInt64(Seconds*1000.));
        return FString::Printf(TEXT("%02lld:%02lld.%03lld"),Milliseconds/60000,(Milliseconds/1000)%60,Milliseconds%1000);
    }
    const TCHAR* DifficultyName(int32 Index)
    {
        return Index==0 ? TEXT("RELAXED") : Index==2 ? TEXT("EXPERT") : TEXT("STREET");
    }
}

void AADHUD::Label(const FString& Text,float X,float Y,float Size,FLinearColor Color)
{
    const FSlateFontInfo Font(GEngine->GetMediumFont(), FMath::Max(10,FMath::RoundToInt(22.f*Size*UiScale)));
    FCanvasTextItem Item(FVector2D(UiOffsetX+X*UiScale,UiOffsetY+Y*UiScale),FText::FromString(Text),Font,Color);
    Canvas->DrawItem(Item);
}

void AADHUD::Panel(float X,float Y,float Width,float Height,FLinearColor Color)
{
    DrawRect(Color,UiOffsetX+X*UiScale,UiOffsetY+Y*UiScale,Width*UiScale,Height*UiScale);
}

void AADHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !GEngine) { return; }
    AADPlayerController* PC=Cast<AADPlayerController>(PlayerOwner);
    AADVehiclePawn* Car=PC ? PC->GetVehiclePawn() : nullptr;
    if (!PC || !Car) { return; }
    const auto* Settings=GetWorld()->GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    const auto Transform=FADCanvasTransform::Fit(Canvas->SizeX,Canvas->SizeY,Settings ? Settings->GetUIScale() : 1.f);
    UiScale=Transform.Scale;
    UiOffsetX=Transform.Offset.X;
    UiOffsetY=Transform.Offset.Y;
    if (PC->GetMap()->IsOpen()) { DrawMap(); return; }
    if (PC->GetCinematic()->IsActive())
    {
        const auto* Cinematic=PC->GetCinematic();
        if (Cinematic->IsHudHidden()) return;
        const FLinearColor White(.92f,.94f,.95f),Accent(.53f,.92f,.77f),Muted(.52f,.60f,.64f),Black(.015f,.023f,.031f,.90f);
        if (Cinematic->GetMode()==EADCinematicMode::Story)
        {
            Panel(0,0,1920,132,FLinearColor(0,0,0,.94f));
            Panel(0,872,1920,208,FLinearColor(0,0,0,.96f));
            Panel(78,78,54,3,Accent);
            Label(TEXT("VELOCITY  /  AFTERDARK"),78,31,.68f,Muted);
            Label(Cinematic->GetStoryTitle(),78,87,1.18f,White);
            Label(FString::Printf(TEXT("SHOT %02d  /  04"),Cinematic->GetStoryShotIndex()),1510,87,.68f,Accent);
            Label(Cinematic->GetStoryAttribution(),92,896,.73f,Accent);
            TArray<FString> Words; Cinematic->GetStorySubtitle().ParseIntoArray(Words,TEXT(" "),true);
            FString Line; TArray<FString> Lines;
            for (const FString& Word:Words)
            {
                if (Line.Len()+Word.Len()>96) { Lines.Add(Line.TrimEnd()); Line.Reset(); }
                Line+=Word+TEXT(" ");
            }
            if (!Line.IsEmpty()) Lines.Add(Line.TrimEnd());
            const int32 LineCount=FMath::Min(2,Lines.Num());
            for (int32 Index=0;Index<LineCount;++Index) Label(Lines[Index],92,936.f+Index*31.f,.9f,White);
            Panel(92,1069,1736,3,FLinearColor(.22f,.27f,.30f));
            Panel(92,1069,1736*Cinematic->GetStoryProgress(),3,Accent);
            Label(TEXT("ENTER / ESC  ·  SKIP SEQUENCE"),1510,1025,.62f,Muted);
            return;
        }
        Panel(52,52,1180,74,Black);
        Label(Cinematic->GetMessage(),76,71,1.0f,White);
        Panel(52,922,1810,100,Black);
        if (Cinematic->GetMode()==EADCinematicMode::Photo)
            Label(TEXT("WASD MOVE    Q / E VERTICAL    ARROWS / RIGHT STICK LOOK    Z / X FOV"),76,940,.8f,White);
        else Label(FString::Printf(TEXT("%.1f / %.1f SEC     LEFT / RIGHT  SCRUB    ENTER / A  PLAY / PAUSE    SHIFT  SLOW    C  CAMERA"),
            Cinematic->GetReplaySeconds(),Cinematic->GetReplayDuration()),76,940,.8f,White);
        if (Cinematic->GetMode()==EADCinematicMode::Photo)
            Label(TEXT("1-4 LOOK    R / F FOCUS    G / T APERTURE    [ / ] EXPOSURE    F8 SAVE    H HIDE    ESC RETURN"),76,982,.7f,Accent);
        else Label(TEXT("F8  SAVE PHOTO    H  HIDE UI    ESC  RETURN TO DRIVE"),76,982,.78f,Accent);
        return;
    }
    if (Settings && Settings->IsOpen()) { DrawSettings(); return; }
    if (PC->GetGarageSession()->IsActive()) { DrawGarage(); return; }
    const FLinearColor White(.92f,.94f,.95f),Muted(.52f,.60f,.64f),Accent(.53f,.92f,.77f),Black(.015f,.023f,.031f,.82f);
    UADVehiclePhysicsComponent* Physics=Car->GetPhysics();
    const FADVehicleTelemetry& State=Physics->GetTelemetry();
    const AADGameMode* Mode=GetWorld()->GetAuthGameMode<AADGameMode>();
    FString Error;
    if (!Physics->IsReady()) { Error=Physics->GetInitializationError(); }
    else if (Mode && !Mode->IsWorldReady()) { Error=Mode->GetStartupError(); }
    if (!PC->IsSessionStarted() || PC->IsGamePaused())
    {
        Panel(0,0,1920,1080,FLinearColor(0,0,0,.20f));
        Label(GetNetMode()==NM_Standalone ? TEXT("N O V A   C I T Y     /     D O C K S I D E") : TEXT("N O V A   C I T Y     /     ONLINE FREE ROAM"),76,70,.9f,White);
        Label(TEXT("VELOCITY"),72,178,4.2f,White);
        Label(TEXT("A F T E R D A R K"),78,330,1.8f,Accent);
        Panel(76,640,655,326,Black);
        Panel(76,640,4,326,Accent);
        Label(PC->IsGamePaused()?TEXT("DRIVE PAUSED"):TEXT("THE CITY CAN WAIT."),108,672,1.35f,White);
        Label(PC->IsGamePaused()?TEXT("Your car is right where you left it."):TEXT("Take the long way home."),108,715,1.05f,Muted);
        if (!PC->IsGamePaused()) Label(TEXT("F / B   DOCKSIDE CIRCUIT  >"),108,760,.9f,Accent);
        Label(PC->IsGamePaused()?TEXT("RESUME DRIVE  >"):TEXT("ENTER NOVA CITY  >"),108,798,1.5f,Accent);
        Label(TEXT("ENTER / A     OR CLICK TO DRIVE"),108,848,.82f,Muted);
        const auto Drive=Physics->GetDefinition().Drivetrain;
        const TCHAR* DriveName=Drive==EADDrivetrain::FrontWheelDrive ? TEXT("FRONT-WHEEL DRIVE")
            : Drive==EADDrivetrain::AllWheelDrive ? TEXT("ALL-WHEEL DRIVE") : TEXT("REAR-WHEEL DRIVE");
        Label(Physics->GetDefinition().Name.ToUpper()+TEXT("     |     ")+DriveName,108,910,.8f,White);
        // A single footer keeps the car silhouette clear and gives hints a
        // consistent contrast backing at every supported viewport aspect ratio.
        Panel(0,978,1920,102,Black);
        const FADInputBindings DefaultBindings;
        const auto& Bindings=Settings ? Settings->GetBindings() : DefaultBindings;
        const auto KeyName=[&Bindings](const TCHAR* Id) { return Bindings.Get(Id).GetDisplayName().ToString().ToUpper(); };
        Label(FString::Printf(TEXT("%s / %s  Pedals    %s / %s  Steer    %s  Handbrake    %s  Camera    %s  Recover"),
            *KeyName(TEXT("Keyboard.Throttle")),*KeyName(TEXT("Keyboard.Brake")),*KeyName(TEXT("Keyboard.SteerLeft")),
            *KeyName(TEXT("Keyboard.SteerRight")),*KeyName(TEXT("Keyboard.Handbrake")),*KeyName(TEXT("Keyboard.Camera")),
            *KeyName(TEXT("Keyboard.Recover"))),76,990,.70f,White);
        Label(TEXT("G / L3  Garage    SHIFT / A  Nitrous    F2  Photo    F4  Replay    F10  Settings    M  Transmission    Q / E  Shift"),76,1026,.66f,Muted);
        Label(TEXT("CONTROLLER"),1390,990,.70f,White);
        Label(TEXT("RT / LT  Pedals    LS  Steer"),1390,1019,.70f,Muted);
        Label(TEXT("X  Handbrake    Y  Camera"),1390,1047,.70f,Muted);
    }
    else
    {
        Label(TEXT("NOVA CITY"),62,54,.95f,White);
        const FString District=Mode && Mode->GetRegionalWorld() && Mode->GetRegionalWorld()->IsReady()
            ? Mode->GetRegionalWorld()->GetCurrentDistrict().ToUpper() : TEXT("DOCKSIDE");
        Label(PC->GetRaceManager() && PC->GetRaceManager()->GetState()!=EADRaceState::Idle
            ? PC->GetRaceManager()->GetDefinition().Name.ToUpper() : District+TEXT(" / FREE DRIVE"),62,88,.73f,Muted);
        Panel(62,982,1115,48,Black);
        Panel(62,982,3,48,Accent);
        Label(TEXT("F / B  RACE     G / L3  GARAGE     P / R3  PURSUIT     F5  MAP     C  CAMERA     ESC  PAUSE"),82,994,.66f,White);
        if (Mode && Mode->GetAtmosphere() && Mode->GetAtmosphere()->IsReady())
        {
            const auto* Weather=Mode->GetAtmosphere();
            const int32 Minutes=FMath::FloorToInt(Weather->GetHour()*60.);
            Label(FString::Printf(TEXT("%02d:%02d  /  %s"),Minutes/60,Minutes%60,*Weather->GetWeatherName().ToUpper()),1510,56,.85f,White);
            Label(FString::Printf(TEXT("ROAD WETNESS  %.0f%%"),Weather->GetWetness()*100.f),1510,90,.7f,Muted);
            if (!Weather->GetSaveError().IsEmpty())
                Label(TEXT("WORLD SAVE FAILED / ")+Weather->GetSaveError().Left(120),62,1030,.61f,FLinearColor(1,.55f,.3f));
        }
        if (Mode && Mode->GetPoliceDirector() && Mode->GetPoliceDirector()->GetState()!=EADPoliceState::Patrol)
        {
            const auto* Police=Mode->GetPoliceDirector();
            Panel(1300,144,550,75,Black);
            Label(FString::Printf(TEXT("HEAT %d   /   %s"),Police->GetHeat(),*Police->GetStateLabel().ToUpper()),1320,162,.92f,FLinearColor(1,.37f,.23f));
        }
        Panel(1530,740,326,277,Black);
        Panel(1530,740,326,2,FLinearColor(.18f,.31f,.32f,.95f));
        const bool bMph=Settings && Settings->UsesMph();
        Label(FString::Printf(TEXT("%03d"),FMath::RoundToInt(FMath::Abs(State.SpeedKmh)*(bMph ? .621371f : 1.f))),1552,834,4.0f,White);
        Label(bMph ? TEXT("MPH") : TEXT("KM/H"),1770,925,.8f,Muted);
        const FString Gear=State.Gear<0?TEXT("R"):(State.Gear==0?TEXT("N"):FString::FromInt(State.Gear));
        Label(Gear,1790,841,2.0f,Accent);
        Label(State.bAutomaticTransmission?TEXT("AUTO"):TEXT("MANUAL"),1554,951,.7f,Muted);
        Label(FString::Printf(TEXT("%d RPM"),FMath::RoundToInt(State.Rpm)),1694,951,.7f,White);
        const float RpmFraction=FMath::Clamp(State.Rpm/FMath::Max(1.0f,Physics->GetDefinition().RedlineRpm),0.0f,1.0f);
        for (int32 I=0;I<28;++I)
        {
            const FLinearColor Color=I/28.0f<RpmFraction?(I>23?FLinearColor(1,.22f,.13f):Accent):FLinearColor(.14f,.19f,.21f);
            Panel(1554+I*10,986,7,6,Color);
        }
        const float SpeedFraction=FMath::Clamp(FMath::Abs(State.SpeedKmh)/300.f,0.f,1.f);
        for (int32 Tick=0;Tick<25;++Tick)
        {
            const float Angle=FMath::DegreesToRadians(210.f+Tick*(120.f/24.f));
            // Lower the speed arc so its crown clears the two compact vehicle
            // status readouts above it at every UI scale.
            const FVector2D Center(1693.f,868.f);
            const FVector2D Inner=Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*86.f;
            const FVector2D Outer=Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*99.f;
            const FLinearColor TickColor=Tick<SpeedFraction*24.f
                ? (Tick>19 ? FLinearColor(1.f,.27f,.16f) : Accent) : FLinearColor(.18f,.24f,.26f);
            DrawLine(UiOffsetX+Inner.X*UiScale,UiOffsetY+Inner.Y*UiScale,
                UiOffsetX+Outer.X*UiScale,UiOffsetY+Outer.Y*UiScale,TickColor,2.f*UiScale);
        }
        // Keep two short labels in their own columns instead of one long line
        // running through the instrument arc.
        Label(TEXT("NITROUS"),1552,750,.52f,Muted);
        Label(FString::Printf(TEXT("%.0f%%"),Car->GetEffects()->GetNitrousFraction()*100.f),1552,770,.70f,Accent);
        Label(TEXT("CONDITION"),1734,750,.52f,Muted);
        Label(FString::Printf(TEXT("%.0f%%"),Car->GetEffects()->GetHealth()*100.f),1734,770,.70f,White);
        DrawLine(UiOffsetX+1693.f*UiScale,UiOffsetY+749.f*UiScale,
            UiOffsetX+1693.f*UiScale,UiOffsetY+787.f*UiScale,FLinearColor(.20f,.28f,.30f),1.f*UiScale);
        if (Mode && Mode->GetExploration() && Mode->GetExploration()->IsReady()
            && (!PC->GetRaceManager() || PC->GetRaceManager()->GetState()==EADRaceState::Idle)
            && (!Mode->GetPoliceDirector() || !Mode->GetPoliceDirector()->IsActive()))
        {
            const auto* Exploration=Mode->GetExploration();
            if (const auto* Target=Exploration->GetTrackedDiscovery())
            {
                Panel(62,304,488,157,Black);
                Label(TEXT("EXPLORE / ")+Target->Name.ToUpper(),80,322,.77f,Accent);
                Label(Exploration->GetDirectionHint(),80,358,.64f,White);
                const FString Distance=Exploration->GetRouteError().IsEmpty()
                    ? FString::Printf(TEXT("%.2f KM BY ROAD"),Exploration->GetRouteDistanceCm()/100000.) : TEXT("RETURN TO THE ROAD");
                Label(Distance+TEXT(" / F5  MAP"),80,392,.63f,Muted);
                Label(TEXT("SLOW BELOW 40 KM/H TO DISCOVER"),80,425,.6f,Muted);
                Panel(80,451,450*Exploration->GetArrivalFraction(),3,Accent);
            }
            if (!Exploration->GetMessage().IsEmpty())
            {
                Panel(570,309,1230,62,Black);
                Label(Exploration->GetMessage().Left(140),590,326,.68f,Accent);
            }
        }
    }
    DrawRace();
    if (const auto* Ownership=PC->GetGarageSession()->GetOwnership(); Ownership && Ownership->IsReady())
    {
        const auto* Career=GetWorld()->GetGameInstance()->GetSubsystem<UADCareerSubsystem>();
        const auto* Race=PC->GetRaceManager();
        if (GetNetMode()==NM_Standalone && Career && Career->IsReady() && (!Race || Race->GetState()==EADRaceState::Idle))
        {
            const auto& Profile=Ownership->GetProfile();
            const auto* Chapter=Career->GetActiveChapter(Profile);
            const float X=1070.f;
            Panel(X,142,770,Chapter ? 144.f : 90.f,Black);
            Panel(X,142,3,Chapter ? 144.f : 90.f,Accent);
            Label(TEXT("CAREER  /  DRIVER PROFILE"),X+24,153,.54f,Muted);
            Label(FString::Printf(TEXT("CREDITS  %lld"),Profile.Credits),X+606,153,.57f,Accent);
            DrawLine(UiOffsetX+(X+24)*UiScale,UiOffsetY+176*UiScale,
                UiOffsetX+(X+746)*UiScale,UiOffsetY+176*UiScale,FLinearColor(.16f,.24f,.26f),1.f*UiScale);
            Label(FString::Printf(TEXT("%s  /  %lld REP"),*Career->GetRankName(Profile.Reputation).ToUpper(),Profile.Reputation),X+24,184,.66f,Accent);
            Label(Chapter ? Chapter->Title.ToUpper() : TEXT("DOCKSIDE SERIES COMPLETE"),X+24,210,1.0f,White);
            if (Chapter) Label(TEXT("N / LB+B   OPEN CAREER    /    ")+Chapter->Crew.ToUpper(),X+24,248,.62f,Muted);
        }
    }
    if (const auto* Online=GetWorld()->GetGameInstance()->GetSubsystem<UADOnlineSubsystem>(); Online && !Online->GetStatus().IsEmpty())
    {
        if (GetNetMode()!=NM_Standalone)
            Label(GetNetMode()==NM_Client ? TEXT("ONLINE / CONNECTED / STOCK FREE ROAM") : TEXT("ONLINE / HOSTING / STOCK FREE ROAM"),1080,1049,.62f,Accent);
        else Label(Online->GetStatus().Left(120),670,110,.65f,Accent);
    }
    if (PC->ShouldShowDiagnostics())
    {
        Panel(60,135,530,135,Black);
        Label(FString::Printf(TEXT("RPM %.0f | GEAR %d | %.1f KM/H"),State.Rpm,State.Gear,State.SpeedKmh),78,153,.82f,White);
        Label(FString::Printf(TEXT("GROUND %d / 4 | SLIP %.3f | STEER %.2f"),State.GroundedWheels,State.Slip,State.Steering),78,187,.82f,Accent);
        Label(FString::Printf(TEXT("FRAME %.1f MS | THROTTLE %.2f"),GetWorld()->GetDeltaSeconds()*1000,State.Throttle),78,221,.82f,Muted);
    }
    if (!Error.IsEmpty())
    {
        Panel(50,1020,1820,48,FLinearColor(.24f,.025f,.015f,.96f));
        Label(TEXT("DRIVING UNAVAILABLE: ")+Error,68,1032,.75f,White);
    }
}

void AADHUD::DrawMap()
{
    const auto* PC=Cast<AADPlayerController>(PlayerOwner);
    const auto* Map=PC ? PC->GetMap() : nullptr;
    const auto* Exploration=Map ? Map->GetExploration() : nullptr;
    const auto* Ownership=GetWorld()->GetGameInstance()->GetSubsystem<UADOwnershipSubsystem>();
    if (!Map || !Exploration || !Ownership) return;
    const FLinearColor White(.92f,.94f,.95f),Muted(.50f,.59f,.64f),Accent(.53f,.92f,.77f),Black(.015f,.023f,.031f,.98f);
    Panel(0,0,1920,1080,Black);
    Label(TEXT("NOVA CITY / EXPLORE"),72,58,1.9f,White);
    Label(FString::Printf(TEXT("%d / %d LANDMARKS DISCOVERED"),Ownership->GetProfile().DiscoveredLocations.Num(),Ownership->GetDiscoveries().Num()),74,120,.78f,Muted);
    Panel(ADMapLayout::X,ADMapLayout::Y,ADMapLayout::Width,ADMapLayout::Height,FLinearColor(.025f,.045f,.055f));
    const auto Line=[&](FVector2D A,FVector2D B,FLinearColor Color,float Width)
    {
        DrawLine(UiOffsetX+A.X*UiScale,UiOffsetY+A.Y*UiScale,UiOffsetX+B.X*UiScale,UiOffsetY+B.Y*UiScale,Color,Width*UiScale);
    };
    for (const auto& Road:Exploration->GetRoadNetwork().GetSegments())
        Line(Map->Project(Road.Start),Map->Project(Road.End),FLinearColor(.16f,.25f,.29f),7.f);
    const auto& Route=Exploration->GetRoute();
    for (int32 Index=1;Index<Route.Num();++Index)
        Line(Map->Project(Route[Index-1]),Map->Project(Route[Index]),Accent,3.f);
    const FVector2D CarWorld=PC->GetVehiclePawn() ? FVector2D(PC->GetVehiclePawn()->GetActorLocation()) : FVector2D::ZeroVector;
    const auto& Snapshot=Ownership->GetProfile().World;
    if (Snapshot.bCustomWaypointRecorded)
    {
        TArray<FVector2D> PersonalRoute;
        double DistanceCm=0.; FString RouteError;
        if (Exploration->GetRoadNetwork().BuildRoute(CarWorld,Snapshot.CustomWaypoint,PersonalRoute,DistanceCm,RouteError))
            for (int32 Index=1;Index<PersonalRoute.Num();++Index)
                Line(Map->Project(PersonalRoute[Index-1]),Map->Project(PersonalRoute[Index]),FLinearColor(.95f,.63f,.27f),2.f);
        const FVector2D Pin=Map->Project(Snapshot.CustomWaypoint);
        Line(Pin+FVector2D(-9,-9),Pin+FVector2D(9,9),FLinearColor(.98f,.68f,.29f),3.f);
        Line(Pin+FVector2D(-9,9),Pin+FVector2D(9,-9),FLinearColor(.98f,.68f,.29f),3.f);
        Label(TEXT("WAYPOINT"),Pin.X+11,Pin.Y-12,.54f,FLinearColor(.98f,.68f,.29f));
    }

    const auto* Career=GetWorld()->GetGameInstance()->GetSubsystem<UADCareerSubsystem>();
    const auto* RaceManager=PC->GetRaceManager();
    if (Career && Career->IsReady() && RaceManager)
    {
        for (int32 Index=0;Index<Career->GetChapters().Num();++Index)
        {
            const FADCareerChapter& Chapter=Career->GetChapters()[Index];
            const FADRaceDefinition* Race=RaceManager->GetRaceById(Chapter.RaceId);
            if (!Race || Race->Checkpoints.IsEmpty()) continue;
            const FVector2D Pin=Map->Project(FVector2D(Race->Checkpoints[0].Location));
            const bool bCurrent=Ownership->GetProfile().CompletedChapters.Num()==Index;
            const FLinearColor Color=bCurrent ? Accent : FLinearColor(.77f,.82f,.84f);
            Line(Pin+FVector2D(0,-9),Pin+FVector2D(8,7),Color,2.f);
            Line(Pin+FVector2D(8,7),Pin+FVector2D(-8,7),Color,2.f);
            Line(Pin+FVector2D(-8,7),Pin+FVector2D(0,-9),Color,2.f);
            Label(FString::Printf(TEXT("R%d"),Index+1),Pin.X+9,Pin.Y-12,.5f,Color);
        }
    }
    if (const auto* Mode=GetWorld()->GetAuthGameMode<AADGameMode>())
    {
        const FVector2D Pin=Map->Project(FVector2D(Mode->GetDrivingStartLocation()));
        Panel(Pin.X-7,Pin.Y-7,14,14,FLinearColor(.92f,.55f,.28f));
        Panel(Pin.X-3,Pin.Y-3,6,6,Black);
        Label(TEXT("HOME GARAGE"),Pin.X+10,Pin.Y+3,.52f,FLinearColor(.98f,.67f,.39f));
    }
    const auto* Selected=Map->GetSelectedLocation();
    for (const int32 Index:Map->GetVisibleLocations())
    {
        const auto& Location=Ownership->GetDiscoveries()[Index];
        const FVector2D Point=Map->Project(Location.Position);
        const bool bSelected=Selected && Selected->Id==Location.Id;
        const auto Color=Exploration->IsDiscovered(Location.Id) ? Accent : White;
        if (bSelected) Panel(Point.X-11,Point.Y-11,22,22,Accent);
        Panel(Point.X-7,Point.Y-7,14,14,Black);
        Panel(Point.X-4,Point.Y-4,8,8,Color);
        Label(FString::FromInt(Index+1),Point.X+12,Point.Y-10,.59f,Color);
    }
    if (const auto* Car=PC->GetVehiclePawn())
    {
        const FVector2D Center=Map->Project(FVector2D(Car->GetActorLocation()));
        const FVector Forward=Car->GetActorForwardVector();
        const FVector2D Direction(Forward.X,-Forward.Y),Right(-Direction.Y,Direction.X);
        const FVector2D Nose=Center+Direction*14.,Left=Center-Direction*9.-Right*7.,TailRight=Center-Direction*9.+Right*7.;
        Line(Nose,Left,White,2.5f); Line(Left,TailRight,White,2.5f); Line(TailRight,Nose,White,2.5f);
    }
    Label(TEXT("N"),ADMapLayout::X+20,ADMapLayout::Y+15,.9f,Muted);
    Panel(1292,174,570,780,FLinearColor(.035f,.058f,.065f));
    Label(Map->GetFilterName(),ADMapLayout::ListX+10,192,.85f,Accent);
    const int32 First=(Map->GetSelectedRow()/8)*8;
    const auto& Visible=Map->GetVisibleLocations();
    for (int32 Row=First;Row<FMath::Min(First+8,Visible.Num());++Row)
    {
        const int32 Index=Visible[Row];
        const auto& Location=Ownership->GetDiscoveries()[Index];
        const float Y=ADMapLayout::ListY+(Row-First)*ADMapLayout::RowHeight;
        if (Row==Map->GetSelectedRow()) Panel(ADMapLayout::ListX,Y,ADMapLayout::ListWidth,60,FLinearColor(.08f,.20f,.19f));
        Label(FString::Printf(TEXT("%02d  %s"),Index+1,*Location.Name.ToUpper()),ADMapLayout::ListX+10,Y+7,.72f,White);
        Label(Exploration->IsDiscovered(Location.Id) ? TEXT("DISCOVERED")
            : FString::Printf(TEXT("+%lld CR  /  +%lld REP"),Location.Credits,Location.Reputation),ADMapLayout::ListX+47,Y+34,.59f,Muted);
    }
    if (Visible.IsEmpty()) Label(TEXT("NO LANDMARKS IN THIS FILTER"),1320,255,.73f,Muted);
    if (Selected)
    {
        // This panel uses two bounded lines; catalog descriptions have a length cap.
        FString FirstLine=Selected->Description.Left(46),SecondLine=Selected->Description.Mid(46);
        int32 Break=INDEX_NONE;
        if (Selected->Description.Len()>46 && FirstLine.FindLastChar(' ',Break))
        { FirstLine=Selected->Description.Left(Break); SecondLine=Selected->Description.Mid(Break+1); }
        Label(FirstLine,1320,801,.68f,White); Label(SecondLine.Left(52),1320,829,.68f,White);
        Label(TEXT("ENTER / A  SET ROUTE"),1320,879,.72f,Accent);
        Label(TEXT("T / Y  FAST TRAVEL  (DISCOVERED ONLY)"),1320,911,.61f,Muted);
    }
    if (Snapshot.bCustomWaypointRecorded) Label(TEXT("DELETE / RIGHT CLICK  CLEAR CUSTOM PIN"),1320,933,.55f,Muted);
    if (!Map->GetMessage().IsEmpty()) Label(Map->GetMessage().Left(55),1320,933,.55f,Accent);
    Label(FString::Printf(TEXT("MAP SCALE  %.1fX"),Map->GetZoomFactor()),74,152,.59f,Muted);
    Label(TEXT("UP / DOWN  SELECT     LEFT / RIGHT  FILTER     CLICK LANDMARK  ROUTE     CLICK ROAD  PIN"),72,988,.66f,White);
    Label(TEXT("ESC / F5 / D-PAD LEFT  RETURN TO DRIVE"),72,1026,.70f,Accent);
    Label(TEXT("I J K L  PAN   /   WHEEL  ZOOM"),1320,982,.58f,Muted);
    Label(TEXT("RACE START   /   HOME GARAGE   /   CUSTOM PIN"),1320,1006,.55f,Muted);
}

void AADHUD::DrawSettings()
{
    const auto* Settings=GetWorld()->GetGameInstance()->GetSubsystem<UADSettingsSubsystem>();
    if (!Settings) return;
    const FLinearColor White(.92f,.94f,.95f),Muted(.55f,.63f,.67f),Accent(.53f,.92f,.77f),Black(.015f,.023f,.031f,.97f);
    Panel(370,95,1180,870,Black);
    Label(Settings->GetPageTitle(),415,135,2.0f,White);
    Label(FString::Printf(TEXT("%d / %d     CHANGES ARE STAGED UNTIL APPLY"),Settings->GetSelectedRow()+1,Settings->GetRowCount()),420,202,.70f,Muted);
    const int32 First=Settings->GetFirstVisibleRow();
    for (int32 Visible=0;Visible<Settings->VisibleRows;++Visible)
    {
        const int32 Index=First+Visible;
        if (Index>=Settings->GetRowCount()) break;
        const float Y=250.f+Visible*72.f;
        if (Index==Settings->GetSelectedRow()) Panel(403,Y-5,1110,66,FLinearColor(.08f,.2f,.19f));
        Label(Settings->GetRowLabel(Index),420,Y,.90f,White);
        Label(Settings->GetRowValue(Index),1050,Y,.82f,Accent);
    }
    Label(Settings->GetMessage(),420,785,.62f,Muted);
    if (Settings->IsBindingsPage()) Label(TEXT("MENU, TRANSMISSION AND ACTIVITY SHORTCUTS REMAIN FIXED."),420,831,.62f,Muted);
    Label(TEXT("ARROWS / D-PAD  SELECT / CHANGE     ENTER / A  CONFIRM"),420,875,.73f,Accent);
    Label(TEXT("ESC / B  BACK OR CANCEL     F10 / START  CLOSE WITHOUT APPLYING"),420,917,.65f,Accent);
}

void AADHUD::DrawGarage()
{
    auto* PC = Cast<AADPlayerController>(PlayerOwner);
    auto* Session = PC ? PC->GetGarageSession() : nullptr;
    auto* Ownership = Session ? Session->GetOwnership() : nullptr;
    if (!Ownership) return;
    const FLinearColor White(.92f,.94f,.95f), Muted(.55f,.63f,.67f), Accent(.53f,.92f,.77f), Black(.015f,.023f,.031f,.92f);
    const auto& Draft = Session->GetDraft();
    const auto& Stock = Ownership->GetStockDefinition(Draft);
    const auto& Preview = Session->GetPreviewDefinition();
    Panel(42,42,620,864,Black);
    Label(TEXT("AFTERDARK  /  MOTOR WORKS"),68,68,.85f,Accent);
    Label(Stock.Name.ToUpper(),68,117,2.1f,White);
    Label(FString::Printf(TEXT("CREDITS  %lld     BUILD COST  %lld"), Ownership->GetProfile().Credits, Session->GetPendingCost()),68,191,.85f,Muted);
    const auto Row = [&](int32 Index, const FString& Left, const FString& Right)
    {
        const float Y = 230.f + Index * 62.f;
        const bool bSelected = Index == Session->GetSelectedRow();
        Panel(64,Y,576,56,bSelected ? FLinearColor(.08f,.2f,.19f,.95f) : FLinearColor(.035f,.05f,.057f,.8f));
        if (bSelected) Panel(64,Y,3,56,Accent);
        Label(Left,81,Y+6,.88f,White);
        Label(Right,81,Y+31,.64f,bSelected ? Accent : Muted);
    };
    const auto* Paint = Ownership->GetPaints().FindByPredicate([&](const auto& P) { return P.Id == Draft.PaintId; });
    Row(0,TEXT("PAINT"),Paint ? Paint->Name.ToUpper() : TEXT("UNAVAILABLE"));
    const auto& Upgrades = Ownership->GetUpgrades();
    for (int32 Index=0; Index<Upgrades.Num(); ++Index)
    {
        const auto& Part = Upgrades[Index];
        const bool bEquipped = Draft.EquippedUpgrades.Contains(Part.Id);
        const auto* OwnedCar = Ownership->GetProfile().Vehicles.FindByPredicate([&](const auto& V) { return V.VehicleId==Draft.ActiveVehicleId; });
        const bool bOwned = OwnedCar && OwnedCar->OwnedUpgrades.Contains(Part.Id);
        FString State = bOwned ? (bEquipped ? TEXT("OWNED / FITTED") : TEXT("OWNED / REMOVED"))
            : bEquipped ? FString::Printf(TEXT("PURCHASE + FIT  /  %lld CR"),Part.Price)
            : FString::Printf(TEXT("AVAILABLE  /  %lld CR"),Part.Price);
        Row(Index+1,Part.Name.ToUpper(),State);
    }
    const auto* Tune = Ownership->GetTunes().FindByPredicate([&](const auto& T) { return T.Id == Draft.TuneId; });
    Row(Session->GetSaveRow()-1,TEXT("TUNING PRESET"),Tune ? Tune->Name.ToUpper() : TEXT("UNAVAILABLE"));
    Row(Session->GetSaveRow(),TEXT("SAVE CHANGES"),TEXT("PURCHASE CAR / PARTS, FIT BUILD AND SAVE"));
    Row(Session->GetSaveRow()+1,TEXT("RETURN TO CITY"),TEXT("UNSAVED PREVIEW WILL BE DISCARDED"));
    const auto* DealerCar=Ownership->FindVehicle(Draft.ActiveVehicleId);
    const bool bOwnedCar=Ownership->IsVehicleOwned(Draft.ActiveVehicleId);
    Row(Session->GetVehicleRow(),TEXT("VEHICLE COLLECTION / DEALER"),DealerCar
        ? FString::Printf(TEXT("%s  /  %s"),*DealerCar->Definition.Name.ToUpper(),
            bOwnedCar ? TEXT("OWNED") : *FString::Printf(TEXT("%lld CR"),DealerCar->Price)) : TEXT("UNAVAILABLE"));
    if (DealerCar)
    {
        Panel(720,652,1140,110,Black);
        const TCHAR* Drive=Stock.Drivetrain==EADDrivetrain::FrontWheelDrive ? TEXT("FWD") : Stock.Drivetrain==EADDrivetrain::AllWheelDrive ? TEXT("AWD") : TEXT("RWD");
        Label(FString::Printf(TEXT("%s    /    %s    /    %d CYLINDERS"),*Stock.Manufacturer.ToUpper(),Drive,Stock.EngineCylinders),744,669,.8f,Accent);
        Label(DealerCar->Description.Left(100),744,708,.68f,White);
    }
    Label(TEXT("ARROWS / D-PAD  SELECT     LEFT / RIGHT / LB / RB  CHANGE"),68,817,.62f,Muted);
    Label(TEXT("ENTER / A  CONFIRM     ESC / B  RETURN"),68,850,.72f,Accent);
    // Quantities come directly from the effective definition. Do not present
    // estimated acceleration/top speed as measured performance.
    const auto Peak = [](const FADVehicleDefinition& D, bool bPower)
    {
        float Value=0;
        for (float Rpm=D.IdleRpm; Rpm<=D.RedlineRpm; Rpm+=25.f)
            Value=FMath::Max(Value,bPower ? D.GetTorqueNm(Rpm)*Rpm/7127.f : D.GetTorqueNm(Rpm));
        return Value;
    };
    Panel(720,786,1140,120,Black);
    Label(TEXT("BUILD PREVIEW   /   STOCK > SELECTED"),744,802,.7f,Muted);
    Label(FString::Printf(TEXT("%.0f > %.0f HP      %.0f > %.0f NM      %.0f KG      %.2f FINAL DRIVE"),
        Peak(Stock,true),Peak(Preview,true),Peak(Stock,false),Peak(Preview,false),Preview.MassKg,Preview.FinalDrive),744,839,.92f,White);
    Panel(42,934,1818,100,Black);
    Label(Session->GetMessage(),64,949,.72f,Accent);
    Label(TEXT("J / L  ORBIT    I / K  ELEVATION    MOUSE WHEEL  ZOOM     |     CONTROLLER: RIGHT STICK / TRIGGERS"),64,991,.68f,Muted);
}

void AADHUD::DrawRace()
{
    auto* PC=Cast<AADPlayerController>(PlayerOwner);
    auto* Manager=PC ? PC->GetRaceManager() : nullptr;
    if (!PC || !Manager) return;
    const FLinearColor White(.92f,.94f,.95f),Muted(.52f,.60f,.64f),Accent(.53f,.92f,.77f),Black(.015f,.023f,.031f,.87f);
    if (!Manager->IsReady())
    {
        Panel(60,450,1080,65,Black);
        Label(TEXT("RACE UNAVAILABLE: ")+Manager->GetLoadError(),80,469,.8f,White);
        return;
    }
    if (Manager->GetState()==EADRaceState::Idle)
    {
        if (!PC->IsGamePaused())
            Label(FString::Printf(TEXT("%s   /   TAB OR D-PAD RIGHT TO CHANGE"),DifficultyName(PC->GetSelectedDifficulty())),
                76,PC->IsSessionStarted() ? 936.f : 410.f,.65f,Muted);
        return;
    }
    if (PC->IsGamePaused() || Manager->GetRacers().IsEmpty()) return;
    const auto& Definition=Manager->GetDefinition();
    const auto& Player=Manager->GetRacers()[0];
    const auto State=Manager->GetState();
    Panel(1360,50,496,175,Black);
    Label(TEXT("POSITION"),1386,70,.7f,Muted);
    Label(FString::Printf(TEXT("%d / %d"),Player.Place,Manager->GetRacers().Num()),1386,101,2.1f,Accent);
    Label(Definition.Laps==0 ? TEXT("SPRINT") :
        *FString::Printf(TEXT("LAP %d / %d"),FMath::Min(Player.Progress.CompletedLaps+1,Definition.Laps),Definition.Laps),
        1630,79,1.05f,White);
    Label(DifficultyName(Manager->GetDifficultyIndex()),1630,129,.7f,Muted);
    const FString Checkpoint=Player.Progress.NextCheckpoint==0
        ? (Player.Progress.Started ? TEXT("FINISH LINE") : TEXT("START LINE"))
        : FString::Printf(TEXT("CHECKPOINT %d / %d"),Player.Progress.NextCheckpoint,Definition.Checkpoints.Num()-1);
    Label(Checkpoint,1386,184,.72f,White);

    Panel(62,147,420,185,Black);
    Label(TEXT("RACE TIME"),82,164,.7f,Muted);
    Label(RaceTime((Player.Progress.Finished ? Player.Progress.FinishSeconds : Manager->GetElapsedSeconds())+Player.PenaltySeconds),82,196,1.6f,White);
    Label(Definition.Laps==0 ? TEXT("POINT TO POINT  /  NO LAPS") :
        TEXT("BEST LAP  ")+(Player.Progress.BestLapSeconds>0. ? RaceTime(Player.Progress.BestLapSeconds) : TEXT("--:--.---")),82,249,.77f,Accent);
    Label(Player.PenaltySeconds>0. ? FString::Printf(TEXT("RECOVERY PENALTY  +%.1f S"),Player.PenaltySeconds)
        : FString::Printf(TEXT("R  RECOVER  /  +%.0f SECOND PENALTY"),Definition.RecoveryPenaltySeconds),82,289,.65f,Muted);
    if (Player.bWrongWay && !Player.Progress.Finished)
    {
        Panel(725,125,470,75,Black);
        Label(TEXT("WRONG WAY"),786,147,1.2f,FLinearColor(1.f,.62f,.28f));
    }
    if (State==EADRaceState::Countdown)
    {
        if (!Manager->GetCareerMessage().IsEmpty())
        {
            const FString Briefing=Manager->GetCareerMessage();
            TArray<FString> Words; Briefing.ParseIntoArray(Words,TEXT(" "),true);
            FString Line; float Y=495;
            Panel(410,473,1100,138,Black);
            for (const FString& Word:Words)
            {
                if (Line.Len()+Word.Len()>82)
                { Label(Line,435,Y,.78f,White); Y+=33; Line.Reset(); }
                Line+=Word+TEXT(" ");
            }
            if (!Line.IsEmpty()) Label(Line,435,Y,.78f,White);
        }
        Panel(820,330,280,230,Black);
        Label(FString::FromInt(FMath::Max(1,FMath::CeilToInt(Manager->GetCountdownRemaining()))),902,345,4.4f,White);
        Label(TEXT("GET READY"),875,490,.85f,Accent);
    }
    else if (State==EADRaceState::Racing && Manager->GetElapsedSeconds()<1.2)
        Label(TEXT("GO"),884,340,3.2f,Accent);

    // North-up route overview uses the same data and actor positions as scoring.
    Panel(62,700,420,250,Black);
    Label(Definition.Name.ToUpper(),82,715,.7f,White);
    FVector2D Minimum(TNumericLimits<double>::Max(),TNumericLimits<double>::Max());
    FVector2D Maximum(-TNumericLimits<double>::Max(),-TNumericLimits<double>::Max());
    for (const auto& Point : Definition.RoutePoints)
    {
        Minimum.X=FMath::Min(Minimum.X,Point.Position.X); Minimum.Y=FMath::Min(Minimum.Y,Point.Position.Y);
        Maximum.X=FMath::Max(Maximum.X,Point.Position.X); Maximum.Y=FMath::Max(Maximum.Y,Point.Position.Y);
    }
    const double MapScale=FMath::Min(360./FMath::Max(1.,Maximum.X-Minimum.X),150./FMath::Max(1.,Maximum.Y-Minimum.Y));
    const auto Plot=[&](FVector2D P)
    { return FVector2f(92.f+static_cast<float>((P.X-Minimum.X)*MapScale),918.f-static_cast<float>((P.Y-Minimum.Y)*MapScale)); };
    const auto Line=[&](FVector2f A,FVector2f B,FLinearColor Color,float Width)
    { DrawLine(UiOffsetX+A.X*UiScale,UiOffsetY+A.Y*UiScale,UiOffsetX+B.X*UiScale,UiOffsetY+B.Y*UiScale,Color,Width*UiScale); };
    for (int32 Index=0;Index<Definition.RoutePoints.Num();++Index)
        if (Definition.Laps>0 || Index+1<Definition.RoutePoints.Num())
            Line(Plot(Definition.RoutePoints[Index].Position),Plot(Definition.RoutePoints[(Index+1)%Definition.RoutePoints.Num()].Position),Muted,3.f);
    if (Definition.Checkpoints.IsValidIndex(Player.Progress.NextCheckpoint) && !Player.Progress.Finished)
    {
        const FVector Gate=Definition.Checkpoints[Player.Progress.NextCheckpoint].Location;
        const auto P=Plot(FVector2D(Gate.X,Gate.Y));
        Panel(P.X-5,P.Y-5,10,10,Accent);
    }
    for (const auto& Racer : Manager->GetRacers())
    {
        if (!Racer.Car.IsValid()) continue;
        const FVector Position=Racer.Car->GetActorLocation();
        const auto P=Plot(FVector2D(Position.X,Position.Y));
        Panel(P.X-3,P.Y-3,6,6,Racer.Color);
        if (&Racer==&Player)
        {
            const FVector Direction=Racer.Car->GetActorForwardVector();
            Line(P,P+FVector2f(static_cast<float>(Direction.X*13.),static_cast<float>(-Direction.Y*13.)),White,2.f);
        }
    }
    Label(TEXT("BACKSPACE / D-PAD LEFT  LEAVE EVENT"),62,962,.6f,Muted);

    if (State==EADRaceState::Results)
    {
        Panel(505,280,1030,580,Black);
        Label(Player.bDNF ? TEXT("EVENT ENDED") : TEXT("FINISH"),540,309,2.1f,Accent);
        Label(Definition.Name.ToUpper(),540,373,1.0f,White);
        Label(Manager->IsClassificationFinal() ? TEXT("FINAL CLASSIFICATION") : TEXT("Waiting for remaining drivers..."),540,412,.75f,Muted);
        const FString Grade(ANSI_TO_TCHAR(ADRaceRules::GradeName(Player.Grade)));
        Label(FString::Printf(TEXT("PERFORMANCE GRADE  /  %s%s"),*Grade,
            Manager->IsClassificationFinal() ? TEXT("") : TEXT("  /  PROVISIONAL")),540,439,.68f,Accent);
        Label(TEXT("DRIVER"),620,464,.7f,Muted);
        Label(TEXT("TOTAL TIME"),1030,464,.7f,Muted);
        Label(TEXT("PENALTY"),1230,464,.7f,Muted);
        Label(Definition.Laps==0 ? TEXT("RACE TYPE") : TEXT("BEST LAP"),1380,464,.7f,Muted);
        for (int32 Place=1;Place<=Manager->GetRacers().Num();++Place)
        {
            for (const auto& Racer : Manager->GetRacers())
            {
                if (Racer.Place!=Place) continue;
                const float Y=480.f+Place*49.f;
                Label(FString::FromInt(Place),549,Y,1.0f,White);
                Label(Racer.Name.ToUpper(),620,Y,.85f,Racer.Color);
                Label(Racer.bDNF ? TEXT("DNF") : Racer.Progress.Finished ? RaceTime(Racer.Progress.FinishSeconds+Racer.PenaltySeconds) : TEXT("RACING"),1030,Y,.8f,White);
                Label(FString::Printf(TEXT("+%.1f"),Racer.PenaltySeconds),1230,Y,.8f,Muted);
                Label(Definition.Laps==0 ? TEXT("SPRINT") :
                    Racer.Progress.BestLapSeconds>0 ? RaceTime(Racer.Progress.BestLapSeconds) : TEXT("--:--.---"),1380,Y,.72f,Muted);
            }
        }
        Label(TEXT("ENTER / A  RACE AGAIN     BACKSPACE / D-PAD LEFT  FREE DRIVE"),540,766,.73f,Accent);
        Label(FString::Printf(TEXT("NEXT RACE: %s   /   TAB TO CHANGE"),DifficultyName(PC->GetSelectedDifficulty())),540,811,.65f,Muted);
        if (!Manager->GetCareerMessage().IsEmpty()) Label(Manager->GetCareerMessage(),540,842,.58f,Accent);
    }
}
