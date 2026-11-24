
#include "Character/OBCharacter.h"
#include "Character/OBModularSoldierComponent.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/MeshComponent.h"

AOBCharacter::AOBCharacter()
{
    SoldierAppearanceComponent=CreateDefaultSubobject<UOBModularSoldierComponent>(TEXT("SoldierAppearanceComponent"));
    WeaponGripComponent=CreateDefaultSubobject<UOBWeaponGripComponent>(TEXT("WeaponGripComponent"));
}

void AOBCharacter::SetAimOutline(bool bEnabled)
{
    if (bEnabled==bAimOutlined) return;
    bAimOutlined=bEnabled;
    if (bEnabled)
    {
        TInlineComponentArray<UMeshComponent*> Meshes(this);
        for (auto* VisualMesh:Meshes)
        {
            if (!VisualMesh->IsVisible() || VisualMesh->bHiddenInGame) continue;
            OutlineMeshStates.Add({VisualMesh,bool(VisualMesh->bRenderCustomDepth),VisualMesh->CustomDepthStencilValue,VisualMesh->CustomDepthStencilWriteMask});
            VisualMesh->SetCustomDepthStencilValue(GetAimOutlineStencilValue());
            VisualMesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
            VisualMesh->SetRenderCustomDepth(true);
        }
    }
    else
    {
        for (const auto& Saved:OutlineMeshStates) if (auto* VisualMesh=Saved.Mesh.Get())
        {
            VisualMesh->SetRenderCustomDepth(Saved.bCustomDepth);
            VisualMesh->SetCustomDepthStencilValue(Saved.Stencil);
            VisualMesh->SetCustomDepthStencilWriteMask(Saved.WriteMask);
        }
        OutlineMeshStates.Reset();
    }
}

void AOBCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    WeaponGripComponent->ApplyWeaponAttachment();
}

void AOBCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    WeaponGripComponent->ApplyWeaponAttachment();
}
