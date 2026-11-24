#include "Character/OBSoldierEditorLibrary.h"
#include "Character/OBModularSoldierComponent.h"
#include "Character/OBCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "PhysicsAssetUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#endif

FString UOBSoldierEditorLibrary::DescribeMesh(USkeletalMesh* Mesh)
{
    if (!Mesh) return TEXT("{}");
    auto Result=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Bones;
    const auto& Ref=Mesh->GetRefSkeleton();
    for (int32 I=0; I<Ref.GetNum(); ++I)
    {
        auto Bone=MakeShared<FJsonObject>();
        Bone->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());
        Bone->SetStringField(TEXT("parent"),Ref.GetParentIndex(I)==INDEX_NONE ? TEXT("") : Ref.GetBoneName(Ref.GetParentIndex(I)).ToString());
        const auto& T=Ref.GetRefBonePose()[I];
        TArray<TSharedPtr<FJsonValue>> P,Q;
        for (double V : {T.GetLocation().X,T.GetLocation().Y,T.GetLocation().Z}) P.Add(MakeShared<FJsonValueNumber>(V));
        const FQuat R=T.GetRotation();
        for (double V : {R.X,R.Y,R.Z,R.W}) Q.Add(MakeShared<FJsonValueNumber>(V));
        Bone->SetArrayField(TEXT("translation"),P); Bone->SetArrayField(TEXT("rotation"),Q);
        Bones.Add(MakeShared<FJsonValueObject>(Bone));
    }
    Result->SetArrayField(TEXT("bones"),Bones);
    Result->SetNumberField(TEXT("lod_count"),Mesh->GetLODNum());
    Result->SetStringField(TEXT("bounds"),Mesh->GetImportedBounds().BoxExtent.ToString());
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (const auto& M:Mesh->GetMaterials()) Materials.Add(MakeShared<FJsonValueString>(M.MaterialSlotName.ToString()));
    Result->SetArrayField(TEXT("material_slots"),Materials);
    TArray<TSharedPtr<FJsonValue>> Sockets;
    for (const USkeletalMeshSocket* Socket : Mesh->GetActiveSocketList())
    {
        if (!Socket) continue;
        auto Entry=MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"),Socket->SocketName.ToString());
        Entry->SetStringField(TEXT("bone"),Socket->BoneName.ToString());
        Entry->SetStringField(TEXT("transform"),Socket->GetSocketLocalTransform().ToString());
        Sockets.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("sockets"),Sockets);
    FString Json; auto Writer=TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Result,Writer); return Json;
}
UPhysicsAsset* UOBSoldierEditorLibrary::CreatePhysics(USkeletalMesh* Mesh,const FString& PackagePath)
{
#if WITH_EDITOR
    if (!Mesh || !FPackageName::IsValidLongPackageName(PackagePath) || FindObject<UPackage>(nullptr,*PackagePath)) return nullptr;
    auto* Package=CreatePackage(*PackagePath);
    auto* Asset=NewObject<UPhysicsAsset>(Package,*FPackageName::GetLongPackageAssetName(PackagePath),RF_Public|RF_Standalone|RF_Transactional);
    FPhysAssetCreateParams Params; Params.MinBoneSize=10.f; Params.GeomType=EFG_Sphyl;
    FText Error;
    if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset,Mesh,Params,Error,true))
    { UE_LOG(LogTemp,Error,TEXT("Soldier physics creation failed: %s"),*Error.ToString()); return nullptr; }
    FAssetRegistryModule::AssetCreated(Asset); Asset->MarkPackageDirty(); Mesh->MarkPackageDirty(); return Asset;
#else
    return nullptr;
#endif
}
bool UOBSoldierEditorLibrary::CopyWeaponSocket(USkeletalMesh* Source,USkeletalMesh* Target)
{
#if WITH_EDITOR
    auto* Socket=Source ? Source->FindSocket(TEXT("WeaponSocket")) : nullptr;
    if (!Socket || !Target || Target->FindSocket(TEXT("WeaponSocket"))) return false;
    auto* Copy=DuplicateObject<USkeletalMeshSocket>(Socket,Target);
    Target->GetMeshOnlySocketList().Add(Copy); Target->MarkPackageDirty(); return true;
#else
    return false;
#endif
}
bool UOBSoldierEditorLibrary::EnsureEnemyAudioSocket(USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    if (!Mesh || Mesh->GetRefSkeleton().FindBoneIndex(TEXT("head"))==INDEX_NONE) return false;
    if (const auto* Existing=Mesh->FindSocket(TEXT("EnemyAudioSocket"))) return Existing->BoneName==TEXT("head");
    auto* Socket=NewObject<USkeletalMeshSocket>(Mesh,NAME_None,RF_Transactional);
    Socket->SocketName=TEXT("EnemyAudioSocket");Socket->BoneName=TEXT("head");
    Socket->bForceAlwaysAnimated=true;
    Mesh->GetMeshOnlySocketList().Add(Socket);Mesh->MarkPackageDirty();return true;
#else
    return false;
#endif
}

FTransform UOBSoldierEditorLibrary::GetSocketLocalTransform(USkeletalMesh* Mesh, FName SocketName)
{
    const USkeletalMeshSocket* Socket=Mesh ? Mesh->FindSocket(SocketName) : nullptr;
    return Socket ? Socket->GetSocketLocalTransform() : FTransform::Identity;
}
UOBModularSoldierComponent* UOBSoldierEditorLibrary::InstallAppearance(UBlueprint* Blueprint,USkeletalMesh* Body,
    TSubclassOf<UAnimInstance> AnimClass,const TArray<USkeletalMesh*>& Parts)
{
#if WITH_EDITOR
    if (!Blueprint || !Blueprint->SimpleConstructionScript || !Body || !AnimClass ||
        !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AOBCharacter::StaticClass())) return nullptr;
    USimpleConstructionScript* SCS=Blueprint->SimpleConstructionScript;
    const TArray<USCS_Node*> Nodes=SCS->GetAllNodes();
    bool bRemovedLegacy=false;
    for (USCS_Node* Node : Nodes)
        if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<UOBModularSoldierComponent>())
        { SCS->RemoveNodeAndPromoteChildren(Node); bRemovedLegacy=true; }
    if (bRemovedLegacy)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }
    auto* Character=Cast<AOBCharacter>(Blueprint->GeneratedClass->GetDefaultObject());
    auto* Component=Character ? Character->GetSoldierAppearanceComponent() : nullptr;
    if (!Component) return nullptr;
    Character->Modify(); Component->Modify(); Character->GetMesh()->Modify();
    Component->BodyMesh=Body; Component->AnimationClass=AnimClass; Component->OutfitMeshes.Reset();
    for (auto* Part:Parts) Component->OutfitMeshes.Add(Part);
    Character->GetMesh()->SetSkeletalMeshAsset(Body);
    Character->GetMesh()->SetAnimInstanceClass(AnimClass);
    Character->GetMesh()->SetVisibility(false,false);
    Character->GetMesh()->SetHiddenInGame(true,false);
    Character->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->GeneratedClass->GetDefaultObject<AOBCharacter>()->GetSoldierAppearanceComponent();
#else
    return nullptr;
#endif
}
