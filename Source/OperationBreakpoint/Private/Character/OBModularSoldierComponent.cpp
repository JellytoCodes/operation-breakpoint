#include "Character/OBModularSoldierComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Character/OBAllyCharacter.h"
#include "Character/OBSoldierOutfit.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UOBModularSoldierComponent::UOBModularSoldierComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
}
void UOBModularSoldierComponent::OnRegister()
{
    Super::OnRegister();
    if (IsTemplate() || !BodyMesh || !VisualParts.IsEmpty()) return;
    auto* Character=Cast<ACharacter>(GetOwner());
    if (!Character || !Character->GetMesh()) return;
    // Keep enemies and the authored defaults unchanged. The same friendly
    // Blueprints select their kit from the map in both the editor and gameplay.
    const UOBSoldierOutfit* Outfit=nullptr;
    if (Character->IsA<AOBPlayerCharacter>() || Character->IsA<AOBAllyCharacter>())
    {
        const FString Map=GetWorld() ? UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) : FString();
        const TCHAR* Path=Map==TEXT("/Game/Maps/Stage_01") ? TEXT("/Game/Blueprints/Characters/Soldier/Outfits/DA_StageSpecialForces.DA_StageSpecialForces")
            : (Map==TEXT("/Game/Maps/TutorialMap") || Map==TEXT("/Game/Maps/TrainingTestMap")) ? TEXT("/Game/Blueprints/Characters/Soldier/Outfits/DA_TrainingUniform.DA_TrainingUniform") : nullptr;
        if (Path)
        {
            Outfit=LoadObject<UOBSoldierOutfit>(nullptr,Path);
            if (Outfit && !Outfit->Parts.IsEmpty())
            {
                OutfitMeshes.Reset();
                for (const auto& Part:Outfit->Parts) OutfitMeshes.Add(Part.Mesh);
            }
            else Outfit=nullptr;
        }
    }
    auto* Leader=Character->GetMesh();
    Leader->SetSkeletalMeshAsset(BodyMesh);
    if (AnimationClass) Leader->SetAnimInstanceClass(AnimationClass);
    Leader->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    const bool bHasOutfit=OutfitMeshes.ContainsByPredicate([](const auto& Mesh) { return Mesh!=nullptr; });
    Leader->SetVisibility(!bHasOutfit,false);
    Leader->SetHiddenInGame(bHasOutfit,false);
    if (auto* Player=Cast<AOBPlayerCharacter>(Character))
        Leader->AddTickPrerequisiteComponent(Player->GetPlayerSplineComponent());
    for (int32 Index=0;Index<OutfitMeshes.Num();++Index)
    {
        USkeletalMesh* Mesh=OutfitMeshes[Index];
        if (!Mesh) continue;
        auto* Part=NewObject<USkeletalMeshComponent>(Character,NAME_None,RF_Transient);
        // The native appearance component owns these parts across Blueprint construction-script reruns.
        Part->CreationMethod=EComponentCreationMethod::Instance;
        Part->SetupAttachment(Leader);
        Part->SetSkeletalMeshAsset(Mesh);
        if (Outfit && Outfit->Parts.IsValidIndex(Index))
            for (int32 Slot=0;Slot<Outfit->Parts[Index].Materials.Num();++Slot)
                if (auto* Material=Outfit->Parts[Index].Materials[Slot].Get()) Part->SetMaterial(Slot,Material);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        const auto* Specification=Outfit && Outfit->Parts.IsValidIndex(Index) ? &Outfit->Parts[Index] : nullptr;
        const FName RigidBone=Specification ? Specification->RigidBone : NAME_None;
        const auto& Skeleton=Mesh->GetRefSkeleton();
        const int32 BoneIndex=Skeleton.FindBoneIndex(RigidBone);
        if (!RigidBone.IsNone() && BoneIndex!=INDEX_NONE)
        {
            FTransform Reference=FTransform::Identity;
            for (int32 Bone=BoneIndex;Bone!=INDEX_NONE;Bone=Skeleton.GetParentIndex(Bone))
                Reference=Reference*Skeleton.GetRefBonePose()[Bone];
            // Reference-pose geometry is placed relative to the live head bone.
            // Unlike a root-space translation, this follows head turns and crouches.
            Part->SetupAttachment(Leader,RigidBone);
            Part->SetRelativeTransform(FTransform(Specification->RefPoseOffset)*Reference.Inverse());
        }
        else Part->SetLeaderPoseComponent(Leader,true,false);
        Part->SetCastShadow(true);
        Part->SetCastHiddenShadow(true);
        Part->RegisterComponent();
        VisualParts.Add(Part);
    }
    UpdateFirstPersonVisibility();
    SetComponentTickEnabled(Character->IsA<AOBPlayerCharacter>());
}
void UOBModularSoldierComponent::OnUnregister()
{
    for (USkeletalMeshComponent* Part : VisualParts) if (IsValid(Part)) Part->DestroyComponent();
    VisualParts.Reset();
    Super::OnUnregister();
}
void UOBModularSoldierComponent::UpdateFirstPersonVisibility()
{
    auto* Player=Cast<AOBPlayerCharacter>(GetOwner());
    if (!Player || !Player->GetPlayerCamera()) return;
    const bool bFirstPerson=Player->GetPlayerCamera()->GetRelativeLocation().X>-120.f || Player->IsCollisionCameraNearBody();
    // Includes weapon inspection: the table's prop is visible, the held gun is not.
    if (auto* Weapon=Player->GetWeaponMesh()) Weapon->SetOwnerNoSee(bFirstPerson);
    for (USkeletalMeshComponent* Part : VisualParts)
    {
        const FString Name=Part->GetSkeletalMeshAsset()->GetName();
        if (Name.Contains(TEXT("Head")) || Name.Contains(TEXT("Helmet")) || Name.Contains(TEXT("Balaclava")) || Name.Contains(TEXT("Binocular")))
            Part->SetOwnerNoSee(bFirstPerson);
    }
}
void UOBModularSoldierComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    UpdateFirstPersonVisibility();
}
