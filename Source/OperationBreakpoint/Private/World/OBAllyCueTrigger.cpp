#include "World/OBAllyCueTrigger.h"
#include "World/OBAllyRoute.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
DEFINE_LOG_CATEGORY_STATIC(LogOBAllyCue,Log,All);
AOBAllyCueTrigger::AOBAllyCueTrigger()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickInterval=.1f;
    TriggerBox=CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox")); SetRootComponent(TriggerBox);
    TriggerBox->SetBoxExtent(FVector(150,250,180));
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore); TriggerBox->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    TriggerBox->SetCanEverAffectNavigation(false);
}
void AOBAllyCueTrigger::BeginPlay()
{
    Super::BeginPlay();
    if (Activation==EOBAllyCueActivation::PlayerSplinePoint)
    {
        TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SetActorTickEnabled(true); TryBindPlayer();
    }
    else TriggerBox->OnComponentBeginOverlap.AddDynamic(this,&ThisClass::HandleOverlap);
}
void AOBAllyCueTrigger::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Player=BoundPlayer.Get()) Player->GetPlayerSplineComponent()->OnSplinePointReached.Remove(PlayerPointHandle);
    BoundPlayer.Reset(); PlayerPointHandle.Reset();
    Super::EndPlay(Reason);
}
void AOBAllyCueTrigger::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); TryBindPlayer();
}
void AOBAllyCueTrigger::TryBindPlayer()
{
    if (BoundPlayer.IsValid()) { SetActorTickEnabled(false); return; }
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Player=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
    if (!Player) return;
    BoundPlayer=Player;
    auto* Spline=Player->GetPlayerSplineComponent();
    PlayerPointHandle=Spline->OnSplinePointReached.AddUObject(this,&ThisClass::HandlePlayerSplinePointReached);
    SetActorTickEnabled(false);
    if (Spline->IsAtActionPoint()) HandlePlayerSplinePointReached(Spline->GetCurrentPointIndex());
}
void AOBAllyCueTrigger::HandlePlayerSplinePointReached(int32 PointIndex)
{
    if (PointIndex==PlayerPointIndex) ActivateForPlayer(BoundPlayer.Get());
}
void AOBAllyCueTrigger::HandleOverlap(UPrimitiveComponent*,AActor* Actor,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
    if (auto* Player=Cast<AOBPlayerCharacter>(Actor)) ActivateForPlayer(Player);
}
bool AOBAllyCueTrigger::ActivateForPlayer(AOBPlayerCharacter* Player)
{
    if (!IsValid(Player) || Player->GetWorld()!=GetWorld() || (bTriggerOnce && bTriggered) || Commands.IsEmpty()) return false;
    for (const auto& Entry : Commands)
    {
        auto* Component=IsValid(Entry.Ally) ? Entry.Ally->GetRouteComponent() : nullptr;
        auto* Route=Entry.Route ? Entry.Route.Get() : Component ? Component->AssignedRoute.Get() : nullptr;
        const bool bNeedsExplicitRoute=Entry.Command==EOBAllyCueCommand::Branch || Entry.Command==EOBAllyCueCommand::StageForPlayerDeparture;
        const bool bNeedsRoute=Entry.Command==EOBAllyCueCommand::Start || bNeedsExplicitRoute;
        if (!Component || Entry.Ally->GetWorld()!=GetWorld() || (bNeedsExplicitRoute && !IsValid(Entry.Route))
            || (bNeedsRoute && (!IsValid(Route) || Route->GetWorld()!=GetWorld() || Route->GetSpline()->GetSplineLength()<=KINDA_SMALL_NUMBER
            || Route->GetSpline()->IsClosedLoop() || Entry.StartPoint<0 || Entry.StartPoint>=Route->GetSpline()->GetNumberOfSplinePoints()-1))
            || (Entry.Command==EOBAllyCueCommand::Branch && !Component->HasRouteControl()))
        { UE_LOG(LogOBAllyCue,Warning,TEXT("Invalid ally cue configuration | Trigger=%s Ally=%s"),*GetName(),*GetNameSafe(Entry.Ally)); return false; }
    }
    for (const auto& Entry : Commands)
    {
        auto* Component=Entry.Ally->GetRouteComponent();
        switch (Entry.Command)
        {
        case EOBAllyCueCommand::Start: Component->StartRoute(Entry.Route ? Entry.Route.Get() : Component->AssignedRoute.Get(),Entry.StartPoint,true); break;
        case EOBAllyCueCommand::Pause: Component->PauseRoute(); break;
        case EOBAllyCueCommand::Resume: Component->ResumeRoute(); break;
        case EOBAllyCueCommand::Branch: Component->QueueBranch(Entry.Route ? Entry.Route.Get() : Component->AssignedRoute.Get(),Entry.StartPoint); break;
        case EOBAllyCueCommand::StageForPlayerDeparture: Component->StageRouteForPlayerDeparture(Entry.Route,Player,Entry.StartPoint); break;
        }
    }
    bTriggered=true; if (bTriggerOnce) TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UE_LOG(LogOBAllyCue,Display,TEXT("Ally cue activated | Trigger=%s Commands=%d"),*GetName(),Commands.Num());
    return true;
}
