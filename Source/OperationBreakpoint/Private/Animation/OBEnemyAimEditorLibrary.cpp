#include "Animation/OBEnemyAimEditorLibrary.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimGraphNode_Base.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
TSharedRef<FJsonObject> DescribeMontage(UAnimMontage* Montage)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Montage->GetPathName());
    Result->SetStringField(TEXT("skeleton"), GetPathNameSafe(Montage->GetSkeleton()));
    Result->SetNumberField(TEXT("length"), Montage->GetPlayLength());
    TArray<TSharedPtr<FJsonValue>> Segments, Notifies, Sections;
    for (int32 SlotIndex=0; SlotIndex<Montage->SlotAnimTracks.Num(); ++SlotIndex)
    {
        const auto& Slot = Montage->SlotAnimTracks[SlotIndex];
        for (const auto& Segment : Slot.AnimTrack.AnimSegments)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("slot"), Slot.SlotName.ToString());
            Entry->SetStringField(TEXT("sequence"), GetPathNameSafe(Segment.GetAnimReference()));
            const auto* Sequence = Segment.GetAnimReference().Get();
            Entry->SetStringField(TEXT("sequence_skeleton"), Sequence ? GetPathNameSafe(Sequence->GetSkeleton()) : TEXT("None"));
            Entry->SetNumberField(TEXT("sequence_length"), Sequence ? Sequence->GetPlayLength() : 0.f);
            Entry->SetNumberField(TEXT("sequence_rate_scale"), Sequence ? Sequence->RateScale : 0.f);
            Entry->SetBoolField(TEXT("skeleton_compatible"), Sequence && Montage->GetSkeleton()
                && Montage->GetSkeleton()->IsCompatibleForEditor(Sequence->GetSkeleton()));
            Entry->SetNumberField(TEXT("start"), Segment.StartPos);
            Entry->SetNumberField(TEXT("duration"), Segment.GetLength());
            Entry->SetNumberField(TEXT("clip_start"), Segment.AnimStartTime);
            Entry->SetNumberField(TEXT("clip_end"), Segment.AnimEndTime);
            Entry->SetNumberField(TEXT("rate"), Segment.AnimPlayRate);
            Entry->SetNumberField(TEXT("loops"), Segment.LoopingCount);
            Segments.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }
    for (const auto& Notify : Montage->Notifies)
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
        Entry->SetStringField(TEXT("guid"), Notify.Guid.ToString());
        Entry->SetStringField(TEXT("class"), Notify.Notify ? Notify.Notify->GetClass()->GetPathName() : GetPathNameSafe(Notify.NotifyStateClass));
        Entry->SetNumberField(TEXT("time"), Notify.GetTime());
        Entry->SetNumberField(TEXT("duration"), Notify.GetDuration());
        Entry->SetStringField(TEXT("linked_sequence"), GetPathNameSafe(Notify.GetLinkedSequence()));
        Entry->SetStringField(TEXT("end_linked_sequence"), GetPathNameSafe(Notify.EndLink.GetLinkedSequence()));
        Notifies.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (const auto& Section : Montage->CompositeSections)
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Section.SectionName.ToString());
        Entry->SetStringField(TEXT("next"), Section.NextSectionName.ToString());
        Entry->SetNumberField(TEXT("time"), Section.GetTime());
        Entry->SetStringField(TEXT("linked_sequence"), GetPathNameSafe(Section.GetLinkedSequence()));
        Sections.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("segments"), Segments);
    Result->SetArrayField(TEXT("notifies"), Notifies);
    Result->SetArrayField(TEXT("sections"), Sections);
    return Result;
}
}
#endif

FString UOBEnemyAimEditorLibrary::DescribeEnemyAnimation(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
    if (!Blueprint || !Blueprint->GeneratedClass) return TEXT("{}");
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("skeleton"), GetPathNameSafe(Blueprint->TargetSkeleton));
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    TArray<TSharedPtr<FJsonValue>> GraphEntries;
    TSet<UAnimationAsset*> References;
    for (auto* Graph : Graphs)
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Graph->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            auto NodeEntry = MakeShared<FJsonObject>();
            NodeEntry->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
            NodeEntry->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
            TArray<FString> Links;
            for (auto* Pin : Node->Pins)
                for (auto* Linked : Pin->LinkedTo)
                    Links.Add(Pin->PinName.ToString()+TEXT(":")+Linked->GetOwningNode()->NodeGuid.ToString()+TEXT(":")+Linked->PinName.ToString());
            Links.Sort();
            TArray<TSharedPtr<FJsonValue>> LinkEntries;
            for (const auto& Link : Links) LinkEntries.Add(MakeShared<FJsonValueString>(Link));
            NodeEntry->SetArrayField(TEXT("links"), LinkEntries);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeEntry));
            if (auto* AnimNode = Cast<UAnimGraphNode_Base>(Node))
            {
                TArray<UAnimationAsset*> Assets;
                AnimNode->GetAllAnimationSequencesReferred(Assets);
                if (auto* Asset = AnimNode->GetAnimationAsset()) Assets.AddUnique(Asset);
                for (auto* Asset : Assets) if (Asset) References.Add(Asset);
            }
        }
        Entry->SetArrayField(TEXT("nodes"), Nodes);
        GraphEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("graphs"), GraphEntries);
    TArray<FString> ReferencePaths;
    for (auto* Asset : References) ReferencePaths.Add(Asset->GetPathName());
    ReferencePaths.Sort();
    TArray<TSharedPtr<FJsonValue>> ReferenceEntries, MontageEntries;
    for (const auto& Path : ReferencePaths) ReferenceEntries.Add(MakeShared<FJsonValueString>(Path));
    Result->SetArrayField(TEXT("graph_animations"), ReferenceEntries);
    UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
    for (const FName PropertyName : {FName(TEXT("AttackMontage")), FName(TEXT("DeadMontage"))})
        if (auto* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), PropertyName))
            if (auto* Montage = Cast<UAnimMontage>(Property->GetObjectPropertyValue_InContainer(Defaults)))
            {
                auto Entry = DescribeMontage(Montage);
                Entry->SetStringField(TEXT("property"), PropertyName.ToString());
                MontageEntries.Add(MakeShared<FJsonValueObject>(Entry));
            }
    Result->SetArrayField(TEXT("montages"), MontageEntries);
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
#else
    return TEXT("{}");
#endif
}

int32 UOBEnemyAimEditorLibrary::ReplaceGraphAnimations(UAnimBlueprint* Blueprint,
    const TArray<UAnimationAsset*>& Sources, const TArray<UAnimationAsset*>& Replacements)
{
#if WITH_EDITOR
    if (!Blueprint || Sources.Num()!=Replacements.Num()) return -1;
    TMap<UAnimationAsset*, UAnimationAsset*> Map;
    for (int32 Index=0; Index<Sources.Num(); ++Index)
    {
        auto* From = Sources[Index]; auto* To = Replacements[Index];
        if (!From || !To || From->GetSkeleton()!=Blueprint->TargetSkeleton || To->GetSkeleton()!=Blueprint->TargetSkeleton) return -1;
        Map.Add(From, To);
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    int32 Count=0;
    Blueprint->Modify();
    for (auto* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (auto* AnimNode = Cast<UAnimGraphNode_Base>(Node))
            {
                TArray<UAnimationAsset*> Assets;
                AnimNode->GetAllAnimationSequencesReferred(Assets);
                if (auto* Asset = AnimNode->GetAnimationAsset()) Assets.AddUnique(Asset);
                bool bChanges=false;
                for (auto* Asset : Assets) if (Map.Contains(Asset)) { ++Count; bChanges=true; }
                if (!bChanges) continue;
                AnimNode->Modify();
                AnimNode->ReplaceReferredAnimations(Map);
                // Some graph nodes expose the asset through a pin default as well as their node struct.
                for (auto* Pin : AnimNode->Pins)
                    if (auto* Asset = Cast<UAnimationAsset>(Pin->DefaultObject))
                        if (auto* Replacement = Map.Find(Asset)) Pin->DefaultObject=*Replacement;
            }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status==BS_Error ? -1 : Count;
#else
    return -1;
#endif
}

bool UOBEnemyAimEditorLibrary::ReplaceMontageAnimation(UAnimMontage* Montage, UAnimSequenceBase* Source, UAnimSequenceBase* Replacement)
{
#if WITH_EDITOR
    if (!Montage || !Source || !Replacement)
    {
        UE_LOG(LogTemp, Error, TEXT("Enemy MM montage migration: null asset | Montage=%s Source=%s Replacement=%s"),
            *GetPathNameSafe(Montage), *GetPathNameSafe(Source), *GetPathNameSafe(Replacement));
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("Enemy MM montage migration | Montage=%s Skeleton=%s Source=%s SourceSkeleton=%s SourceLength=%.6f Replacement=%s ReplacementSkeleton=%s ReplacementLength=%.6f RateScale=%.6f"),
        *Montage->GetPathName(), *GetPathNameSafe(Montage->GetSkeleton()), *Source->GetPathName(),
        *GetPathNameSafe(Source->GetSkeleton()), Source->GetPlayLength(), *Replacement->GetPathName(),
        *GetPathNameSafe(Replacement->GetSkeleton()), Replacement->GetPlayLength(), Replacement->RateScale);
    // The old clip is only an exact reference to remove, not the new animation to play.
    // Existing montages can already reference a clip from another compatible skeleton.
    // Validate the replacement against the montage's explicit editor compatibility rules.
    if (!Montage->GetSkeleton() || !Replacement->GetSkeleton()
        || !Montage->GetSkeleton()->IsCompatibleForEditor(Replacement->GetSkeleton()))
    {
        UE_LOG(LogTemp, Error, TEXT("Enemy MM montage migration rejected: replacement skeleton is not compatible with montage"));
        return false;
    }
    if (Source->GetPlayLength()<=SMALL_NUMBER || Replacement->GetPlayLength()<=SMALL_NUMBER
        || FMath::IsNearlyZero(Replacement->RateScale))
    {
        UE_LOG(LogTemp, Error, TEXT("Enemy MM montage migration rejected: invalid clip length or replacement rate scale"));
        return false;
    }
    bool bChanges=false;
    for (const auto& Slot : Montage->SlotAnimTracks)
        for (const auto& Segment : Slot.AnimTrack.AnimSegments)
            if (Segment.GetAnimReference()==Source)
            {
                if (Segment.GetLength()<=SMALL_NUMBER || Segment.LoopingCount<1)
                {
                    UE_LOG(LogTemp, Error, TEXT("Enemy MM montage migration rejected: invalid source segment | Duration=%.6f Loops=%d Rate=%.6f"),
                        Segment.GetLength(), Segment.LoopingCount, Segment.AnimPlayRate);
                    return false;
                }
                bChanges=true;
            }
    if (!bChanges) return true; // Idempotent rerun.
    struct FLinkSnapshot { float Time; int32 Slot; };
    TArray<FLinkSnapshot> NotifyStarts, NotifyEnds, SectionLinks;
    for (const auto& Notify : Montage->Notifies)
    {
        NotifyStarts.Add({Notify.GetTime(), Notify.GetSlotIndex()});
        NotifyEnds.Add({Notify.EndLink.GetTime(), Notify.EndLink.GetSlotIndex()});
    }
    for (const auto& Section : Montage->CompositeSections) SectionLinks.Add({Section.GetTime(), Section.GetSlotIndex()});
    Montage->Modify();
    for (auto& Slot : Montage->SlotAnimTracks)
        for (auto& Segment : Slot.AnimTrack.AnimSegments)
            if (Segment.GetAnimReference()==Source)
            {
                const float Duration = Segment.GetLength();
                const float StartFraction = Segment.AnimStartTime/Source->GetPlayLength();
                const float EndFraction = Segment.AnimEndTime/Source->GetPlayLength();
                const float Sign = Segment.GetValidPlayRate()<0.f ? -1.f : 1.f;
                Segment.SetAnimReference(Replacement);
                Segment.AnimStartTime=StartFraction*Replacement->GetPlayLength();
                Segment.AnimEndTime=EndFraction*Replacement->GetPlayLength();
                // Preserve absolute notify timing and montage duration despite a different clip length.
                Segment.AnimPlayRate=Sign*Segment.LoopingCount*(Segment.AnimEndTime-Segment.AnimStartTime)
                    /(Duration*Replacement->RateScale);
            }
    Montage->UpdateLinkableElements();
    for (int32 Index=0; Index<Montage->Notifies.Num(); ++Index)
    {
        auto& Notify = Montage->Notifies[Index];
        Notify.Link(Montage, NotifyStarts[Index].Time, NotifyStarts[Index].Slot);
        Notify.EndLink.Link(Montage, NotifyEnds[Index].Time, NotifyEnds[Index].Slot);
    }
    for (int32 Index=0; Index<Montage->CompositeSections.Num(); ++Index)
        Montage->CompositeSections[Index].Link(Montage, SectionLinks[Index].Time, SectionLinks[Index].Slot);
    Montage->RefreshCacheData();
    Montage->PostEditChange();
    Montage->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
