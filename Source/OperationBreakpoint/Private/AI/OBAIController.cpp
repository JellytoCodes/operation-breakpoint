#include "AI/OBAIController.h"

#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/StateTreeAIComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "StateTree.h"
#include "AI/StateTree/OBStateTreeEnemyPatternTask.h"
#include "AI/OBEnemyCombatPattern.h"
#include "Components/StateTreeAIComponentSchema.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBAIController, Log, All);

AOBAIController::AOBAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
    auto* Crowd=CastChecked<UCrowdFollowingComponent>(GetPathFollowingComponent());
    Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High,false);
    // Extra lateral separation/corner offsets push agents into the narrow deck railings.
    // Keep velocity obstacle avoidance, but follow the baked corridor without these offsets.
    Crowd->SetCrowdSeparation(false,false);
    Crowd->SetCrowdAnticipateTurns(true,false);
    Crowd->SetCrowdPathOffset(false,false);
    StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeComponent"));
    BrainComponent = StateTreeComponent;
    StateTreeComponent->SetStartLogicAutomatically(false);
    bStartAILogicOnPossess = false;
}

void AOBAIController::OnPossess(APawn* InPawn)
{
    bStartAILogicOnPossess = false;
    Super::OnPossess(InPawn);

    StagingMoveRequestId = FAIRequestID::InvalidRequest;
    bMovingToStagingPoint = false;
    bHasReachedStagingPoint = false;
    bEncounterActivated = bMovingToCover = false;
    EncounterTreeOverride=nullptr;ActiveCombatTree=nullptr;
    Zone6Hostage.Reset();bZone6FarGuard=bZone6Entry=false;
}

void AOBAIController::OnUnPossess()
{
    StopStateTree();
    Super::OnUnPossess();
}

bool AOBAIController::PrepareEncounterEntry(const FTransform& InStagingTransform, const FTransform& InCoverTransform, float StagingMoveDelay)
{
    auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (!Enemy || !Enemy->IsEncounterCombatant() || Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return false;
    StopStateTree();
    Enemy->SetCombatPosture(EOBEnemyPosture::Stand);
    Enemy->SetCombatState(EOBEnemyCombatState::Idle);
    bHasReachedStagingPoint = false;
    CoverTargetTransform = InCoverTransform;
    Enemy->GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.f, Enemy->EncounterBehavior.RunSpeed);
    Enemy->bUseControllerRotationYaw = false;
    Enemy->GetCharacterMovement()->bOrientRotationToMovement = true;
    if (Enemy->EncounterBehavior.EntryMode == EOBEnemyEntryMode::RunToStaging)
    {
        const float Delay=FMath::Clamp((FMath::IsFinite(StagingMoveDelay) ? FMath::Clamp(StagingMoveDelay,0.f,10.f) : 0.f)
            +(Enemy->CombatPatterns ? Enemy->CombatPatterns->SampleEntryDelay() : 0.f),0.f,10.f);
        StagingTargetTransform=InStagingTransform;
        if (Delay>0.f)
        {
            GetWorldTimerManager().SetTimer(StagingDelayTimer,this,&ThisClass::BeginDelayedStagingMove,Delay,false);
            UE_LOG(LogOBAIController,Display,TEXT("Staging movement waiting | Pawn=%s Delay=%.2f"),*GetNameSafe(Enemy),Delay);
            return true;
        }
        return MoveToStagingPoint(InStagingTransform);
    }
    StagingTargetTransform = Enemy->GetActorTransform();
    FinishStagingMove(true);
    return true;
}

void AOBAIController::BeginDelayedStagingMove()
{
    const auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn());
    if (!IsValid(Enemy) || !Enemy->IsEncounterCombatant() || Enemy->GetCombatState()==EOBEnemyCombatState::Dead) return;
    MoveToStagingPoint(StagingTargetTransform);
}

void AOBAIController::StartEncounterBehavior()
{
    const auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (!Enemy || !Enemy->IsEncounterCombatant() || Enemy->GetCombatState() == EOBEnemyCombatState::Dead || !bHasReachedStagingPoint || bEncounterActivated) return;
    if (Enemy->CombatPatterns && !Enemy->CanRunCombatPattern()) return;
    bEncounterActivated = true;
    const float Delay = FMath::Max(0.f, Enemy->EncounterBehavior.ReactionDelay)
        +(Enemy->CombatPatterns ? Enemy->CombatPatterns->SampleEntryDelay() : 0.f);
    if (Delay > 0.f) GetWorldTimerManager().SetTimer(ReactionTimer, this, &ThisClass::BeginEncounterReaction, Delay, false);
    else BeginEncounterReaction();
}

void AOBAIController::BeginEncounterReaction()
{
    auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (!Enemy || Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return;
    if (Enemy->EncounterBehavior.CoverMode == EOBEnemyCoverMode::None) { FinishEncounterReaction(); return; }
    if (Enemy->EncounterBehavior.CoverMode == EOBEnemyCoverMode::CrouchInPlace)
    {
        // The new trees own stance entry and the cover loop, including the first crouch.
        if (Enemy->CombatPatterns) { FinishEncounterReaction();return; }
        BeginCoverHold();return;
    }
    Enemy->SetCombatState(EOBEnemyCombatState::MovingToCover);
    Enemy->GetCharacterMovement()->bOrientRotationToMovement = true;
    FAIMoveRequest Request(CoverTargetTransform.GetLocation());
    Request.SetUsePathfinding(true); Request.SetProjectGoalLocation(true);
    Request.SetAllowPartialPath(false); Request.SetRequireNavigableEndLocation(true);
    Request.SetAcceptanceRadius(StagingAcceptanceRadius); Request.SetReachTestIncludesAgentRadius(false);
    const FPathFollowingRequestResult Result = MoveTo(Request);
    if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal) { BeginCoverHold(); return; }
    if (Result.Code == EPathFollowingRequestResult::RequestSuccessful)
    {
        CoverMoveRequestId = Result.MoveId; bMovingToCover = true;
        return;
    }
    UE_LOG(LogOBAIController, Warning, TEXT("Cover move failed; continuing combat without crouching | Pawn=%s"), *GetNameSafe(Enemy));
    Enemy->SetCombatState(EOBEnemyCombatState::Idle);
    FinishEncounterReaction();
}

void AOBAIController::BeginCoverHold()
{
    auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (!Enemy || Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return;
    Enemy->SetCombatState(EOBEnemyCombatState::InCover);
    Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
    if (Enemy->EncounterBehavior.CoverMode == EOBEnemyCoverMode::MoveToCover)
    {
        const FRotator Rotation(0,CoverTargetTransform.Rotator().Yaw,0);
        SetControlRotation(Rotation); Enemy->SetActorRotation(Rotation);
    }
    const float Hold = FMath::Max(0.f, Enemy->EncounterBehavior.CoverHoldSeconds);
    if (!Enemy->EncounterBehavior.bStartStateTreeAfterReaction) return;
    if (Hold > 0.f && !Enemy->CombatPatterns) GetWorldTimerManager().SetTimer(ReactionTimer, this, &ThisClass::FinishEncounterReaction, Hold, false);
    else FinishEncounterReaction();
}

void AOBAIController::FinishEncounterReaction()
{
    const auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (Enemy) Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
    if (Enemy && Enemy->GetCombatState() != EOBEnemyCombatState::Dead && Enemy->EncounterBehavior.bStartStateTreeAfterReaction)
        StartStateTree();
}

void AOBAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    FacePlayer(DeltaSeconds);
}

void AOBAIController::FacePlayer(float DeltaSeconds, bool bImmediately)
{
    if (IsZone6GuardScene()) return; // The dedicated StateTree owns both hostage and entry-facing targets.
    auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn());
    if (!Enemy || !bHasReachedStagingPoint || !Enemy->IsEncounterCombatant() || !Enemy->EncounterBehavior.bFacePlayer || bMovingToStagingPoint || bMovingToCover
        || Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return;
    auto* PlayerController = GetWorld()->GetFirstPlayerController();
    const APawn* Target = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!Target) return;
    const FVector Direction = Target->GetActorLocation() - Enemy->GetActorLocation();
    if (Direction.SizeSquared2D() < KINDA_SMALL_NUMBER) return;
    const float Yaw = bImmediately ? Direction.Rotation().Yaw : FMath::FixedTurn(Enemy->GetActorRotation().Yaw, Direction.Rotation().Yaw,
        FMath::Max(1.f, Enemy->EncounterBehavior.TurnSpeed) * FMath::Max(0.f, DeltaSeconds));
    SetControlRotation(FRotator(0,Yaw,0));
    Enemy->SetActorRotation(FRotator(0,Yaw,0));
}

bool AOBAIController::MoveToStagingPoint(const FTransform& InStagingTransform)
{
    const auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn());
    if (!IsValid(Enemy) || !Enemy->IsEncounterCombatant()) return false;

    bMovingToStagingPoint = false;
    bHasReachedStagingPoint = false;
    StagingMoveRequestId = FAIRequestID::InvalidRequest;
    StagingTargetTransform = InStagingTransform;

    FVector Goal=InStagingTransform.GetLocation();
    bool bProjectedOnAuthoredDeck=false;
    if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        FNavLocation Projected;
        // The ship has decks about 300 cm apart. The default tall projection can select the deck above.
        bProjectedOnAuthoredDeck=Nav->ProjectPointToNavigation(Goal,Projected,FVector(100,100,120),&GetNavAgentPropertiesRef());
        if (bProjectedOnAuthoredDeck) Goal=Projected.Location;
    }
    FAIMoveRequest MoveRequest(Goal);
    MoveRequest.SetUsePathfinding(true);
    MoveRequest.SetProjectGoalLocation(!bProjectedOnAuthoredDeck);
    MoveRequest.SetAllowPartialPath(false);
    MoveRequest.SetRequireNavigableEndLocation(true);
    MoveRequest.SetAcceptanceRadius(StagingAcceptanceRadius);
    MoveRequest.SetReachTestIncludesAgentRadius(false);
    MoveRequest.SetCanStrafe(false);

    const FPathFollowingRequestResult MoveResult = MoveTo(MoveRequest);

    if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
    {
        StagingMoveRequestId = MoveResult.MoveId;
        bMovingToStagingPoint = true;

        UE_LOG(LogOBAIController, Display,
            TEXT("Staging move started | Pawn=%s | Target=%s"),
            *GetNameSafe(GetPawn()),
            *InStagingTransform.GetLocation().ToCompactString());

        return true;
    }

    if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
    {
        FinishStagingMove(true);
        return true;
    }

    FinishStagingMove(false);
    return false;
}

void AOBAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID, Result);

    if (bMovingToCover && RequestID.IsEquivalent(CoverMoveRequestId))
    {
        bMovingToCover = false;
        if (Result.IsSuccess()) BeginCoverHold();
        else
        {
            UE_LOG(LogOBAIController, Warning, TEXT("Cover path interrupted; continuing combat | Pawn=%s"), *GetNameSafe(GetPawn()));
            if (auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn())) Enemy->SetCombatState(EOBEnemyCombatState::Idle);
            FinishEncounterReaction();
        }
        return;
    }

    if (!bMovingToStagingPoint) return;
    if (!RequestID.IsEquivalent(StagingMoveRequestId)) return;

    FinishStagingMove(Result.IsSuccess());
}

void AOBAIController::FinishStagingMove(bool bSucceeded)
{
    bMovingToStagingPoint = false;
    StagingMoveRequestId = FAIRequestID::InvalidRequest;

    if (bSucceeded && IsValid(GetPawn()))
    {
        FRotator TargetRotation = StagingTargetTransform.Rotator();
        TargetRotation.Pitch = 0.0f;
        TargetRotation.Roll = 0.0f;
        GetPawn()->SetActorRotation(TargetRotation);
        SetControlRotation(TargetRotation);
        if (auto* Enemy = Cast<AOBEnemyCharacter>(GetPawn())) Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
        bHasReachedStagingPoint = true;
        FacePlayer(0.f,true);
    }

    if (bSucceeded)
    {
        UE_LOG(LogOBAIController, Display,
            TEXT("Staging move completed | Pawn=%s"),
            *GetNameSafe(GetPawn()));
    }
    else
    {
        UE_LOG(LogOBAIController, Warning,
            TEXT("Staging move failed | Pawn=%s"),
            *GetNameSafe(GetPawn()));
    }

    OnStagingMoveFinished.Broadcast(this, bSucceeded);
}

void AOBAIController::StartStateTree()
{
    AOBEnemyCharacter* Enemy = Cast<AOBEnemyCharacter>(GetPawn());

    if (!IsValid(Enemy) || !Enemy->IsEncounterCombatant()) return;
    if (Enemy->GetCombatState() == EOBEnemyCombatState::Dead) return;

    if (!bHasReachedStagingPoint)
    {
        UE_LOG(LogOBAIController, Warning,
            TEXT("StateTree start blocked: staging is not complete | Pawn=%s"),
            *GetNameSafe(GetPawn()));
        return;
    }

    if (!StateTreeComponent || StateTreeComponent->IsRunning()) return;

    UStateTree* Tree=EncounterTreeOverride;
    if (Enemy->CombatPatterns)
    {
        // RPG must never inherit a spawner's rifle burst or the old controller's cover tree.
        if (Enemy->IsRPGTrooper() || !Tree)
            if (auto* Found=Enemy->CombatPatterns->Trees.Find(Enemy->ResolveCombatPattern())) Tree=*Found;
        if (!Tree || !SetEncounterStateTree(Tree,false)) return;
        if (!Enemy->CanRunCombatPattern()) return;
        for (int32 Index=0;Index<Tree->GetNodes().Num();++Index)
            if (Tree->GetNode(Index).GetScriptStruct()==FOBStateTreeEnemyPatternTask::StaticStruct())
            { Enemy->BeginCombatPattern();break; }
    }
    else if (Enemy->StartArchetypeAttack()) return;

    StateTreeComponent->StartLogic();
    if (Enemy->CombatPatterns && !StateTreeComponent->IsRunning()) Enemy->StopCombatPattern();

    UE_LOG(LogOBAIController, Display,
        TEXT("StateTree started | Pawn=%s"),
        *GetNameSafe(GetPawn()));
}

bool AOBAIController::SetEncounterStateTree(UStateTree* Tree,bool bExplicitOverride)
{
    if (!StateTreeComponent || !Tree || !Tree->GetSchema()
        || !Tree->GetSchema()->IsA<UStateTreeAIComponentSchema>())
    {
        UE_LOG(LogOBAIController,Warning,TEXT("CombatStateTree must use StateTreeAIComponentSchema | Tree=%s"),*GetNameSafe(Tree));
        return false;
    }
    if (StateTreeComponent->IsRunning()) StateTreeComponent->StopLogic(TEXT("Encounter StateTree changed"));
    StateTreeComponent->SetStateTree(Tree);
    ActiveCombatTree=Tree;
    if (bExplicitOverride) EncounterTreeOverride=Tree;
    return true;
}

bool AOBAIController::IsCombatTreeRunning() const { return StateTreeComponent && StateTreeComponent->IsRunning(); }
bool AOBAIController::BeginZone6GuardScene(AActor* Hostage,UStateTree* Tree,bool bFarGuard)
{
    auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn());
    if (!IsValid(Enemy) || !IsValid(Hostage) || !bHasReachedStagingPoint || !Tree) return false;
    if (IsZone6GuardScene()) return IsCombatTreeRunning();
    StopStateTree();
    if (!SetEncounterStateTree(Tree)) return false;
    Zone6Hostage=Hostage;bZone6FarGuard=bFarGuard;bZone6Entry=false;
    Enemy->EncounterBehavior.bFacePlayer=false;
    Enemy->GetCharacterMovement()->bOrientRotationToMovement=false;
    Enemy->bUseControllerRotationYaw=false;
    Enemy->SetCombatPosture(EOBEnemyPosture::Stand);
    Enemy->SetCombatState(EOBEnemyCombatState::PreparingAttack);
    UpdateZone6Facing(0,true);
    bEncounterActivated=true;
    StateTreeComponent->StartLogic();
    return IsCombatTreeRunning();
}
AActor* AOBAIController::GetZone6FacingTarget() const
{
    if (!Zone6Hostage.IsValid()) return nullptr;
    if (bZone6FarGuard && bZone6Entry)
        if (auto* PC=GetWorld()->GetFirstPlayerController()) return PC->GetPawn();
    return Zone6Hostage.Get();
}
void AOBAIController::UpdateZone6Facing(float DeltaSeconds,bool bImmediately)
{
    auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn());auto* Target=GetZone6FacingTarget();
    if (!Enemy || !Target || Enemy->GetCombatState()==EOBEnemyCombatState::Dead) return;
    const FVector Direction=Target->GetActorLocation()-Enemy->GetActorLocation();
    if (Direction.SizeSquared2D()<KINDA_SMALL_NUMBER) return;
    const float Yaw=bImmediately ? Direction.Rotation().Yaw : FMath::FixedTurn(Enemy->GetActorRotation().Yaw,Direction.Rotation().Yaw,180.f*FMath::Max(0.f,DeltaSeconds));
    SetControlRotation(FRotator(0,Yaw,0));Enemy->SetActorRotation(FRotator(0,Yaw,0));
}

void AOBAIController::StopStateTree()
{
    if (auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn())) Enemy->StopCombatPattern();
    if (auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn())) Enemy->StopArchetypeAttack();
    GetWorldTimerManager().ClearTimer(ReactionTimer);
    GetWorldTimerManager().ClearTimer(StagingDelayTimer);
    bEncounterActivated = bMovingToCover = false;
    CoverMoveRequestId = FAIRequestID::InvalidRequest;
    bMovingToStagingPoint = false;
    StagingMoveRequestId = FAIRequestID::InvalidRequest;

    if (const auto* Enemy=Cast<AOBEnemyCharacter>(GetPawn()); Enemy && Enemy->GetCombatState()==EOBEnemyCombatState::Dead)
        if (auto* Crowd=Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
            Crowd->SetCrowdSimulationState(ECrowdSimulationState::Disabled);

    FNavigationSystem::StopMovement(*this);

    if (!StateTreeComponent || !StateTreeComponent->IsRunning()) return;
    StateTreeComponent->StopLogic(TEXT("Enemy Dead"));
}
