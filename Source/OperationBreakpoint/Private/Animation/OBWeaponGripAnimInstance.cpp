#include "Animation/OBWeaponGripAnimInstance.h"
#include "Animation/OBEnemyAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Character/OBCharacter.h"
#include "Character/OBWeaponGripComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "BonePose.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TwoBoneIK.h"

namespace
{
    struct FGripFinger
    {
        FName Name;
        FQuat Offset;
    };

    // Rotate the elbow around the shoulder-to-wrist axis. Wrist position/orientation (and
    // therefore the rifle and its aim) stay fixed; only choose a nonpenetrating bend plane.
    FVector ProneElbowPole(const FTransform& Upper, const FTransform& Lower, const FTransform& Hand,
        const FVector& Target, const FVector& FloorPoint, const FVector& FloorNormal, float Clearance)
    {
        const FVector Shoulder=Upper.GetLocation();
        const FVector Axis=(Target-Shoulder).GetSafeNormal();
        const float Distance=FVector::Distance(Shoulder,Target);
        const float UpperLength=FVector::Distance(Shoulder,Lower.GetLocation());
        const float LowerLength=FVector::Distance(Lower.GetLocation(),Hand.GetLocation());
        const FVector AuthoredPole=Lower.GetLocation()+(Lower.GetLocation()
            -(Shoulder+Hand.GetLocation())*.5f).GetSafeNormal()*35.f;
        if (Distance<UE_KINDA_SMALL_NUMBER || Distance>=UpperLength+LowerLength) return AuthoredPole;
        const float Along=(UpperLength*UpperLength-LowerLength*LowerLength+Distance*Distance)/(2.f*Distance);
        const FVector Center=Shoulder+Axis*Along;
        const float Radius=FMath::Sqrt(FMath::Max(0.f,UpperLength*UpperLength-Along*Along));
        const FVector Up=FloorNormal-Axis*FVector::DotProduct(FloorNormal,Axis);
        const float UpLength=Up.Size();
        if (Radius*UpLength<UE_KINDA_SMALL_NUMBER) return AuthoredPole;
        const FVector UpDirection=Up/UpLength;
        const FVector Bend=(AuthoredPole-Center-Axis*FVector::DotProduct(AuthoredPole-Center,Axis)).GetSafeNormal();
        const float Required=FMath::Clamp((Clearance-FVector::DotProduct(Center-FloorPoint,FloorNormal))/(Radius*UpLength),-1.f,1.f);
        if (FVector::DotProduct(Bend,UpDirection)>=Required) return AuthoredPole;
        FVector Side=(Bend-UpDirection*FVector::DotProduct(Bend,UpDirection)).GetSafeNormal();
        if (Side.IsNearlyZero()) Side=FVector::CrossProduct(Axis,UpDirection).GetSafeNormal();
        return Center+(UpDirection*Required+Side*FMath::Sqrt(FMath::Max(0.f,1.f-Required*Required)))*Radius;
    }

    struct FOBWeaponGripProxy : FAnimInstanceProxy
    {
        using FAnimInstanceProxy::FAnimInstanceProxy;
        float Alpha = 0.f;
        FTransform WeaponInHand = FTransform::Identity;
        FTransform SupportHand = FTransform::Identity;
        TArray<FGripFinger> Fingers;
        FOBEnemyAimContext Aim;
        FTransform AimWeaponInHand = FTransform::Identity;
        FVector AimMuzzleInWeapon = FVector::ZeroVector;
        FVector AimBarrelInWeapon = FVector::ForwardVector;
        bool bHasAimWeapon = false;
        UAnimSequence* RPGPoseSequence=nullptr;
        float RPGPoseTime=0.f;
        FOBEnemyPatternPose PatternPose;
        bool bProneRecoil=false,bProneFloor=false;
        FVector ProneFloorPoint=FVector::ZeroVector,ProneFloorNormal=FVector::UpVector;
        float ProneArmClearance=6.f;

        virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
        {
            FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
            Aim = FOBEnemyAimContext();
            bHasAimWeapon = false;
            RPGPoseSequence=nullptr;RPGPoseTime=0.f;
            PatternPose=FOBEnemyPatternPose();
            bProneRecoil=bProneFloor=false;
            if (auto* EnemyAnimation = Cast<UOBEnemyAnimInstance>(Instance))
            {
                EnemyAnimation->UpdateEnemyAimContext(DeltaSeconds);
                Aim = EnemyAnimation->GetEnemyAimContext();
                RPGPoseSequence=EnemyAnimation->GetRPGPoseSequence();RPGPoseTime=EnemyAnimation->GetRPGPoseTime();
                PatternPose=EnemyAnimation->GetPatternPose();
                const auto* Enemy = Cast<AOBEnemyCharacter>(Instance->TryGetPawnOwner());
                const auto* Body = Enemy ? Enemy->GetMesh() : nullptr;
                if (Enemy && Body && PatternPose.Base && Enemy->GetCombatPosture()==EOBEnemyPosture::Prone)
                {
                    bProneRecoil=Enemy->GetPatternAction()==EOBEnemyPatternAction::Fire;
                    const auto* Movement=Enemy->GetCharacterMovement();
                    if (Movement && Movement->CurrentFloor.IsWalkableFloor())
                    {
                        const auto& Floor=Movement->CurrentFloor.HitResult;
                        const FTransform MeshWorld=Body->GetComponentTransform();
                        ProneFloorPoint=MeshWorld.InverseTransformPosition(Floor.ImpactPoint);
                        ProneFloorNormal=MeshWorld.InverseTransformVectorNoScale(Floor.ImpactNormal).GetSafeNormal();
                        ProneArmClearance=MeshWorld.InverseTransformVector(Floor.ImpactNormal*6.f).Size();
                        bProneFloor=true;
                    }
                }
                if (Aim.bActive && Body && Body->DoesSocketExist(TEXT("hand_r")))
                {
                    // Snapshot every UObject-derived transform on the game thread. Worker evaluation
                    // uses only these values and the newly evaluated animation pose.
                    const FTransform HandWorld = Body->GetSocketTransform(TEXT("hand_r"));
                    if (Enemy->IsRPGTrooper() && Enemy->GetRPGVisualMesh() && Enemy->GetRPGVisualMesh()->IsVisible())
                    {
                        AimWeaponInHand = Enemy->GetRPGVisualMesh()->GetComponentTransform().GetRelativeTransform(HandWorld);
                        AimMuzzleInWeapon = Enemy->RPGMuzzleOffset;
                        AimBarrelInWeapon = Enemy->RPGBarrelAxis.GetSafeNormal();
                        bHasAimWeapon = true;
                    }
                    else if (const auto* Rifle = Enemy->GetWeaponMesh(); Rifle && Rifle->IsVisible()
                        && !Rifle->bHiddenInGame && Rifle->DoesSocketExist(Enemy->GetMuzzleSocketName()))
                    {
                        AimWeaponInHand = Rifle->GetComponentTransform().GetRelativeTransform(HandWorld);
                        AimMuzzleInWeapon = Rifle->GetSocketTransform(Enemy->GetMuzzleSocketName(), RTS_Component).GetLocation();
                        // The muzzle-flash bone's X axis is not necessarily the barrel axis. The
                        // saved weapon's Muzzle socket carries its authored firing orientation.
                        const FName DirectionSocket = Rifle->DoesSocketExist(TEXT("Muzzle"))
                            ? FName(TEXT("Muzzle")) : Enemy->GetMuzzleSocketName();
                        AimBarrelInWeapon = Rifle->GetSocketTransform(DirectionSocket, RTS_Component).GetRotation().GetForwardVector();
                        bHasAimWeapon = true;
                    }
                }
            }
            const auto* Character = Cast<AOBCharacter>(Instance->TryGetPawnOwner());
            const auto* Grip = Character ? Character->GetWeaponGripComponent() : nullptr;
            const bool bActive = Grip && Grip->ShouldApplyGrip();
            // Never retain a hand constraint during ragdoll or after weapon detachment.
            const bool bDetached = !Grip || !Grip->GetWeapon()
                || Grip->GetWeapon()->GetAttachParent() != Character->GetMesh()
                || Character->GetMesh()->IsSimulatingPhysics();
            Alpha = bDetached ? 0.f : FMath::FInterpConstantTo(Alpha, bActive ? 1.f : 0.f,
                DeltaSeconds, 1.f / FMath::Max(.01f, Grip->BlendSeconds));
            Fingers.Reset();
            if (Grip)
            {
                WeaponInHand = Grip->GetWeapon() ? Grip->GetWeapon()->GetRelativeTransform() : Grip->GetWeaponAttachment();
                SupportHand = Grip->GetSupportHand();
                for (const auto& Pair : Grip->GetFingerOffsets()) Fingers.Add({Pair.Key, Pair.Value.Quaternion()});
            }
        }

        virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* Root) override
        {
            // Call only the graph evaluator here: EvaluateAnimation_WithRoot would recurse into this override.
            EvaluateAnimationNode_WithRoot(Output, Root);
            if (RPGPoseSequence && !Output.ExpectsAdditivePose())
            {
                FAnimationPoseData PoseData(Output.Pose,Output.Curve,Output.CustomAttributes);
                RPGPoseSequence->GetAnimationPose(PoseData,FAnimExtractContext(double(RPGPoseTime),false));
            }
            if (Output.ExpectsAdditivePose()) return true;
            if (PatternPose.Base)
            {
                FPoseContext BasePose(Output);
                FAnimationPoseData BaseData(BasePose.Pose,BasePose.Curve,BasePose.CustomAttributes);
                PatternPose.Base->GetAnimationPose(BaseData,FAnimExtractContext(double(PatternPose.BaseTime),false));
                FAnimationPoseData OutputData(Output.Pose,Output.Curve,Output.CustomAttributes);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData,BaseData,1.f-PatternPose.BlendAlpha);
                if (PatternPose.Action)
                {
                    FPoseContext ActionPose(Output);
                    FAnimationPoseData ActionData(ActionPose.Pose,ActionPose.Curve,ActionPose.CustomAttributes);
                    PatternPose.Action->GetAnimationPose(ActionData,FAnimExtractContext(double(PatternPose.ActionTime),false));
                    FPoseContext RecoilReference(Output);
                    if (bProneRecoil)
                    {
                        FAnimationPoseData ReferenceData(RecoilReference.Pose,RecoilReference.Curve,RecoilReference.CustomAttributes);
                        PatternPose.Action->GetAnimationPose(ReferenceData,FAnimExtractContext(0.,false));
                    }
                    const auto& PatternBones=Output.Pose.GetBoneContainer();
                    TSet<int32> ActionRoots;
                    for (FName Name : PatternPose.bArmsOnly
                        ? TArray<FName>{TEXT("clavicle_l"),TEXT("clavicle_r")}
                        : TArray<FName>{TEXT("spine_01")})
                    {
                        const int32 MeshIndex=PatternBones.GetPoseBoneIndexForBoneName(Name);
                        if (MeshIndex!=INDEX_NONE) ActionRoots.Add(PatternBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)).GetInt());
                    }
                    // Preserve pelvis/legs (and the prone spine) throughout all five shots and reload.
                    for (FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
                    {
                        auto Parent=Bone;
                        while (Parent!=INDEX_NONE && !ActionRoots.Contains(Parent.GetInt())) Parent=PatternBones.GetParentBoneIndex(Parent);
                        if (Parent==INDEX_NONE) continue;
                        if (bProneRecoil)
                        {
                            // A standing arm pose becomes a downward-pointing arm under a prone
                            // spine. Transfer only its local recoil delta onto the prone arms.
                            const FQuat Delta=ActionPose.Pose[Bone].GetRotation()*RecoilReference.Pose[Bone].GetRotation().Inverse();
                            Output.Pose[Bone].SetRotation((Delta*Output.Pose[Bone].GetRotation()).GetNormalized());
                        }
                        else Output.Pose[Bone]=ActionPose.Pose[Bone];
                    }
                }
            }
            const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
            auto Index = [&](FName Name) {
                const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
                return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
                    : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            };
            // Rotate the upper body before solving the support hand. The right hand, weapon,
            // neck and hit-box bones inherit this transform; pelvis, legs and capsule do not.
            if (bHasAimWeapon && Aim.bActive && Aim.Alpha > ZERO_ANIMWEIGHT_THRESH)
            {
                auto Spine = Index(TEXT("spine_01"));
                if (Spine == INDEX_NONE) Spine = Index(TEXT("spine_02"));
                const auto RightHand = Index(TEXT("hand_r"));
                if (Spine != INDEX_NONE && RightHand != INDEX_NONE)
                {
                    FCSPose<FCompactPose> AimedPose;
                    AimedPose.InitPose(Output.Pose);
                    FTransform SpineTransform = AimedPose.GetComponentSpaceTransform(Spine);
                    const FVector Pivot = SpineTransform.GetLocation();
                    const FTransform Weapon = AimWeaponInHand * AimedPose.GetComponentSpaceTransform(RightHand);
                    const FVector Muzzle = Weapon.TransformPosition(AimMuzzleInWeapon);
                    const FVector Barrel = Weapon.TransformVectorNoScale(AimBarrelInWeapon).GetSafeNormal();
                    FQuat Correction = FQuat::Identity;
                    if (Weapon.IsValid() && !Barrel.IsNearlyZero() && !Aim.TargetComponentSpace.ContainsNaN())
                    {
                        // Rotation moves the muzzle as well as its direction. Iterate around the
                        // spine pivot to remove that parallax instead of aiming from the old muzzle.
                        for (int32 Iteration = 0; Iteration < 8; ++Iteration)
                        {
                            const FVector RotatedMuzzle = Pivot + Correction.RotateVector(Muzzle - Pivot);
                            const FVector ToTarget = (Aim.TargetComponentSpace - RotatedMuzzle).GetSafeNormal();
                            if (ToTarget.IsNearlyZero()) break;
                            const FVector CurrentBarrel = Correction.RotateVector(Barrel);
                            Correction = (FQuat::FindBetweenNormals(CurrentBarrel, ToTarget) * Correction).GetNormalized();
                        }
                        // A malformed weapon axis must not turn the torso inside out. Native aim
                        // already limits target yaw/pitch; this is a final pose-data guard.
                        const float Angle = Correction.GetAngle();
                        const float Limit = FMath::DegreesToRadians(120.f);
                        if (Angle > Limit) Correction = FQuat::Slerp(FQuat::Identity, Correction, Limit / Angle).GetNormalized();
                        SpineTransform.SetRotation((Correction * SpineTransform.GetRotation()).GetNormalized());
                        const FBoneTransform Transform(Spine, SpineTransform);
                        AimedPose.LocalBlendCSBoneTransforms(MakeArrayView(&Transform, 1), Aim.Alpha);
                        FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(AimedPose), Output.Pose);
                    }
                }
            }
            if (Alpha <= ZERO_ANIMWEIGHT_THRESH) return true;
            const auto Upper = Index(TEXT("upperarm_l")), Lower = Index(TEXT("lowerarm_l"));
            const auto Left = Index(TEXT("hand_l")), Right = Index(TEXT("hand_r"));
            // Required bones can omit the fingers or arm at distant LODs.
            if (Upper == INDEX_NONE || Lower == INDEX_NONE || Left == INDEX_NONE || Right == INDEX_NONE) return true;
            FCSPose<FCompactPose> CS;
            CS.InitPose(Output.Pose);
            if (bProneFloor)
            {
                const auto RightUpper=Index(TEXT("upperarm_r")),RightLower=Index(TEXT("lowerarm_r"));
                if (RightUpper!=INDEX_NONE && RightLower!=INDEX_NONE)
                {
                    FTransform UpperR=CS.GetComponentSpaceTransform(RightUpper),LowerR=CS.GetComponentSpaceTransform(RightLower);
                    FTransform HandR=CS.GetComponentSpaceTransform(Right);
                    FVector WristTarget=HandR.GetLocation();
                    const float WristHeight=FVector::DotProduct(WristTarget-ProneFloorPoint,ProneFloorNormal);
                    if (WristHeight<ProneArmClearance) WristTarget+=ProneFloorNormal*(ProneArmClearance-WristHeight);
                    const FVector Pole=ProneElbowPole(UpperR,LowerR,HandR,WristTarget,ProneFloorPoint,ProneFloorNormal,ProneArmClearance);
                    const FQuat WristRotation=HandR.GetRotation();
                    AnimationCore::SolveTwoBoneIK(UpperR,LowerR,HandR,Pole,WristTarget,false,1.f,1.f);
                    HandR.SetRotation(WristRotation);
                    const FBoneTransform RightArm[]={{RightUpper,UpperR},{RightLower,LowerR},{Right,HandR}};
                    CS.LocalBlendCSBoneTransforms(RightArm,Alpha);
                }
            }
            FTransform UpperT = CS.GetComponentSpaceTransform(Upper);
            FTransform LowerT = CS.GetComponentSpaceTransform(Lower);
            FTransform HandT = CS.GetComponentSpaceTransform(Left);
            const FTransform Target = SupportHand * WeaponInHand * CS.GetComponentSpaceTransform(Right);
            // Retain the authored elbow plane; it follows crouching, locomotion and recoil.
            const FVector Pole = bProneFloor
                ? ProneElbowPole(UpperT,LowerT,HandT,Target.GetLocation(),ProneFloorPoint,ProneFloorNormal,ProneArmClearance)
                : LowerT.GetLocation() + (LowerT.GetLocation()
                    - (UpperT.GetLocation() + HandT.GetLocation()) * .5f).GetSafeNormal() * 35.f;
            AnimationCore::SolveTwoBoneIK(UpperT, LowerT, HandT, Pole, Target.GetLocation(), false, 1.f, 1.f);
            HandT.SetRotation(Target.GetRotation());
            const FBoneTransform Arm[] = {{Upper, UpperT}, {Lower, LowerT}, {Left, HandT}};
            CS.LocalBlendCSBoneTransforms(Arm, Alpha);
            FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
            for (const FGripFinger& Finger : Fingers)
            {
                const auto Bone = Index(Finger.Name);
                if (Bone == INDEX_NONE) continue;
                FTransform& Local = Output.Pose[Bone];
                const FQuat Desired = Bones.GetRefPoseTransform(Bone).GetRotation() * Finger.Offset;
                Local.SetRotation(FQuat::Slerp(Local.GetRotation(), Desired, Alpha).GetNormalized());
            }
            return true;
        }
    };
}

FAnimInstanceProxy* UOBWeaponGripAnimInstance::CreateAnimInstanceProxy() { return new FOBWeaponGripProxy(this); }
void UOBWeaponGripAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
