#include "World/OBEnemyPatternPreview.h"
#include "AI/OBAIController.h"
#include "Camera/CameraComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBActionComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/OBPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Widget/OBHUD.h"
#include "Widget/OBPlayerOverlayWidget.h"

AOBEnemyPatternPreview::AOBEnemyPatternPreview()
{
    PrimaryActorTick.bCanEverTick=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
    PreviewCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("PreviewCamera"));
    PreviewCamera->SetupAttachment(RootComponent);
    PreviewCamera->SetRelativeLocation(FVector(1550,0,550));
    PreviewCamera->SetRelativeRotation(FRotator(-17.f,180.f,0));
    PreviewCamera->SetFieldOfView(70.f);
    PreviewCamera->bConstrainAspectRatio=false;
}

void AOBEnemyPatternPreview::BeginPlay()
{
    Super::BeginPlay();
    if (bAutoStart) GetWorldTimerManager().SetTimer(StartTimer,this,&ThisClass::StartPreview,.25f,false);
}

void AOBEnemyPatternPreview::StartPreview()
{
    if (bStarted) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    if (PC)
    {
        if (auto* Player=Cast<AOBPlayerCharacter>(PC->GetPawn()))
        {
            auto* Rail=Player->GetPlayerSplineComponent();
            Rail->SetStageMovementBlocked(true);Rail->SetComponentTickEnabled(false);
            Player->GetActionComponent()->ResetEncounterForQA();
            Player->GetActionComponent()->SetExternalActionState(true);
            Player->GetActionComponent()->SetComponentTickEnabled(false);
            Player->GetCharacterMovement()->StopMovementImmediately();Player->GetCharacterMovement()->DisableMovement();
            Player->SetCanBeDamaged(false);
            Player->SetActorLocation(GetActorTransform().TransformPosition(PlayerTargetOffset));
        }
        if (auto* Controller=Cast<AOBPlayerController>(PC))
            if (auto* HUD=Controller->GetOBHUD())
                if (auto* Overlay=HUD->GetPlayerOverlayWidget()) Overlay->SetVisibility(ESlateVisibility::Collapsed);
        PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(this);
    }
    int32 Started=0;
    for (const auto& Entry : Entries)
    {
        auto* Enemy=Entry.Enemy.Get();
        if (!IsValid(Enemy) || !Entry.StateTree) continue;
        auto& Behavior=Enemy->EncounterBehavior;
        Behavior.EntryMode=EOBEnemyEntryMode::WaitAtSpawn;Behavior.CoverMode=EOBEnemyCoverMode::None;
        Behavior.CombatPattern=Entry.Pattern;Behavior.ReactionDelay=0.f;
        Behavior.bFacePlayer=false;Behavior.bStartStateTreeAfterReaction=true;
        Enemy->SetCanBeDamaged(true);
        Enemy->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Enemy->GetMesh()->bEnableUpdateRateOptimizations=false;
        if (!Enemy->GetController()) Enemy->SpawnDefaultController();
        if (auto* AI=Cast<AOBAIController>(Enemy->GetController()))
        {
            if (!AI->SetEncounterStateTree(Entry.StateTree)) continue;
            if (!AI->PrepareEncounterEntry(Enemy->GetActorTransform(),Enemy->GetActorTransform(),0.f)) continue;
            Enemy->StartEncounterBehavior();
            if (AI->IsCombatTreeRunning()) ++Started;
        }
    }
    bStarted=Started==Entries.Num() && Started>0;
    UE_LOG(LogTemp,Display,TEXT("ENEMY_PATTERN_PREVIEW started=%d expected=%d | F8 eject keeps StateTrees running"),Started,Entries.Num());
}

void AOBEnemyPatternPreview::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(StartTimer);
    Super::EndPlay(Reason);
}
