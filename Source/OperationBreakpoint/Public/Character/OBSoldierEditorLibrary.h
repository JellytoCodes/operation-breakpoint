#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OBSoldierEditorLibrary.generated.h"
class USkeletalMesh;
class UPhysicsAsset;
class UBlueprint;
class UAnimInstance;
class UOBModularSoldierComponent;

UCLASS()
class OPERATIONBREAKPOINT_API UOBSoldierEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="OB|Soldier|Editor")
    static FString DescribeMesh(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="OB|Soldier|Editor")
    static UPhysicsAsset* CreatePhysics(USkeletalMesh* Mesh,const FString& PackagePath);
    UFUNCTION(BlueprintCallable, Category="OB|Soldier|Editor")
    static bool CopyWeaponSocket(USkeletalMesh* Source,USkeletalMesh* Target);
    UFUNCTION(BlueprintCallable, Category="OB|Soldier|Editor")
    static bool EnsureEnemyAudioSocket(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintPure, Category="OB|Soldier|Editor")
    static FTransform GetSocketLocalTransform(USkeletalMesh* Mesh, FName SocketName);
    UFUNCTION(BlueprintCallable, Category="OB|Soldier|Editor")
    static UOBModularSoldierComponent* InstallAppearance(UBlueprint* Blueprint,USkeletalMesh* Body,
        TSubclassOf<UAnimInstance> AnimClass,const TArray<USkeletalMesh*>& Parts);
};
