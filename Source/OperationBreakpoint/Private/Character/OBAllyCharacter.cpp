#include "Character/OBAllyCharacter.h"
#include "Character/OBAllySplineComponent.h"
#include "Character/OBAllyNavigationComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
AOBAllyCharacter::AOBAllyCharacter()
{
    PrimaryActorTick.bCanEverTick=true;
    AutoPossessAI=EAutoPossessAI::Disabled; SetCanBeDamaged(false);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    WeaponMesh=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    WeaponMesh->SetupAttachment(GetMesh()); WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetGenerateOverlapEvents(false); WeaponMesh->SetCanEverAffectNavigation(false);
    RouteComponent=CreateDefaultSubobject<UOBAllySplineComponent>(TEXT("RouteComponent"));
    NavigationComponent=CreateDefaultSubobject<UOBAllyNavigationComponent>(TEXT("NavigationComponent"));
    GetMesh()->AddTickPrerequisiteComponent(RouteComponent);
}
FVector AOBAllyCharacter::GetVelocity() const
{
    return RouteComponent && RouteComponent->HasRouteControl() ? RouteComponent->GetRouteVelocity() : Super::GetVelocity();
}
void AOBAllyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Combat, narration and travel all use the same armed presentation.
    if (WeaponMesh->bHiddenInGame) WeaponMesh->SetHiddenInGame(false);
    if (!WeaponMesh->IsVisible()) WeaponMesh->SetVisibility(true);
}
void AOBAllyCharacter::FireSupportAt(AOBEnemyCharacter* Target)
{
    if (!IsValid(Target)) return;
    WeaponMesh->SetHiddenInGame(false);
    const FVector Direction=Target->GetActorLocation()-GetActorLocation();
    if (!Direction.IsNearlyZero()) SetActorRotation(FRotator(0,Direction.Rotation().Yaw,0));
    OnSupportShot(Target);
}
