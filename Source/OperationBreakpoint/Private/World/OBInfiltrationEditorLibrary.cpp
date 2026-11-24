#include "World/OBInfiltrationEditorLibrary.h"
#if WITH_EDITOR
#include "Animation/OBAllyAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "World/OBEncounterSpawner.h"
#include "Character/OBEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "AI/NavigationSystemBase.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "AssetCompilingManager.h"
#include "NavigationPath.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "World/OBRailPath.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"
#endif

bool UOBInfiltrationEditorLibrary::ConfigureEnemyShotCue(USoundCue* Cue,USoundWave* First,USoundWave* Second)
{
#if WITH_EDITOR
    if (!Cue || !First || !Second) return false;
    Cue->Modify();Cue->ClearGraph();Cue->AllNodes.Reset();
    auto* Random=Cue->ConstructSoundNode<USoundNodeRandom>();
    TArray<USoundNode*> Players;
    for (auto* Wave : {First,Second})
    {
        auto* Player=Cue->ConstructSoundNode<USoundNodeWavePlayer>();Player->SetSoundWave(Wave);Player->bLooping=false;Players.Add(Player);
    }
    Random->SetChildNodes(Players);Random->Weights={1.f,1.f};Random->bRandomizeWithoutReplacement=true;Random->PreselectAtLevelLoad=0;
    Random->GraphNode->ReconstructNode();
    Cue->FirstNode=Random;Cue->LinkGraphNodesFromSoundNodes();Cue->PostEditChange();Cue->MarkPackageDirty();return true;
#else
    return false;
#endif
}

FString UOBInfiltrationEditorLibrary::DescribeRailNavigationGaps(UWorld* World)
{
#if WITH_EDITOR
    auto* NS=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    AOBRailPath* Rail=nullptr;for (TActorIterator<AOBRailPath> It(World);It;++It) { Rail=*It;break; }
    if (!NS || !Rail) return TEXT("{}");
    auto* S=Rail->GetSpline();FNavAgentProperties Agent;Agent.AgentRadius=26;Agent.AgentHeight=176;
    const auto* Data=NS->GetNavDataForProps(Agent,S->GetLocationAtSplinePoint(0,ESplineCoordinateSpace::World));
    if (!Data) return TEXT("{}");
    auto Vec=[](FVector P) {return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});};
    auto Connected=[&](FVector A,FVector B,float Limit) {
        FPathFindingQuery Query(nullptr,*Data,A,B);const auto R=NS->FindPathSync(Agent,Query);
        return R.IsSuccessful() && R.Path.IsValid() && !R.Path->IsPartial() && R.Path->GetLength()<=Limit;
    };
    struct FSample { float D;FVector Nav;bool Valid; };
    TArray<FSample> Samples;
    for (float D=0;;D=FMath::Min(D+20.f,S->GetSplineLength()))
    {
        const FVector P=S->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World);FNavLocation N;
        const bool Valid=NS->ProjectPointToNavigation(P,N,FVector(60,60,80),Data);
        Samples.Add({D,N.Location,Valid});if (D>=S->GetSplineLength()) break;
    }
    TArray<TPair<int32,int32>> Gaps;int32 Previous=INDEX_NONE;
    for (int32 I=0;I<Samples.Num();++I) if (Samples[I].Valid)
    {
        if (Previous!=INDEX_NONE && !Connected(Samples[Previous].Nav,Samples[I].Nav,FMath::Max(140.f,(Samples[I].D-Samples[Previous].D)*2.f)))
        {
            if (!Gaps.IsEmpty() && Samples[Previous].D-Samples[Gaps.Last().Value].D<=60.f) Gaps.Last().Value=I;
            else Gaps.Add({Previous,I});
        }
        Previous=I;
    }
    FCollisionQueryParams Collision(SCENE_QUERY_STAT(SquadRailLinkClearance),true);
    for (TActorIterator<APawn> It(World);It;++It) Collision.AddIgnoredActor(*It);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Gap : Gaps)
    {
        const float From=Samples[Gap.Key].D,To=Samples[Gap.Value].D;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("from_distance"),From);Row->SetNumberField(TEXT("to_distance"),To);
        Row->SetNumberField(TEXT("span"),To-From);Row->SetField(TEXT("from"),Vec(Samples[Gap.Key].Nav));Row->SetField(TEXT("to"),Vec(Samples[Gap.Value].Nav));
        bool Safe=false;TArray<TSharedPtr<FJsonValue>> Attempts;
        for (float Offset : {0.f,40.f,-40.f,80.f,-80.f})
        {
            auto Point=[&](float D) { const FVector Direction=S->GetDirectionAtDistanceAlongSpline(D,ESplineCoordinateSpace::World);return S->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World)+FVector(-Direction.Y,Direction.X,0)*Offset; };
            FNavLocation A,B;bool Pass=To-From<=450.f && NS->ProjectPointToNavigation(Point(From),A,FVector(60,60,80),Data) && NS->ProjectPointToNavigation(Point(To),B,FVector(60,60,80),Data);
            FString Blocker;FVector LastFloor=FVector::ZeroVector;bool HasLast=false;
            for (float D=From;Pass;D=FMath::Min(D+10.f,To))
            {
                // A simple link is a direct crossing, even when the rail turns.
                FVector P=FMath::Lerp(A.Location,B.Location,(D-From)/FMath::Max(1.f,To-From));FHitResult Floor;
                Pass=World->LineTraceSingleByChannel(Floor,P+FVector(0,0,65),P-FVector(0,0,100),ECC_Pawn,Collision) && Floor.ImpactNormal.Z>=.49f;
                // A capsule can remain supported over a narrow tread seam even
                // when a zero-width line falls through it.
                if (!Pass) Pass=World->SweepSingleByChannel(Floor,P+FVector(0,0,65),P-FVector(0,0,100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(18),Collision) && Floor.ImpactNormal.Z>=.49f;
                if (!Pass) { Blocker=TEXT("no walkable floor");break; }
                P.Z=Floor.ImpactPoint.Z;
                if (HasLast && FMath::Abs(P.Z-LastFloor.Z)>45.f) { Pass=false;Blocker=TEXT("floor step exceeds 45cm");break; }
                LastFloor=P;HasLast=true;FHitResult Body;
                if (World->SweepSingleByChannel(Body,P+FVector(0,0,106),P+FVector(0,0,106.1),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(18,88),Collision))
                { Pass=false;Blocker=GetNameSafe(Body.GetActor());break; }
                if (D>=To) break;
            }
            auto Attempt=MakeShared<FJsonObject>();Attempt->SetNumberField(TEXT("offset"),Offset);Attempt->SetBoolField(TEXT("clear"),Pass);Attempt->SetStringField(TEXT("blocker"),Blocker);Attempts.Add(MakeShared<FJsonValueObject>(Attempt));
            if (Pass) { Row->SetField(TEXT("from"),Vec(A.Location));Row->SetField(TEXT("to"),Vec(B.Location));Row->SetNumberField(TEXT("offset"),Offset);Safe=true;break; }
        }
        Row->SetBoolField(TEXT("walkable_candidate"),Safe);Row->SetArrayField(TEXT("attempts"),Attempts);Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("nav_data"),Data->GetName());Root->SetNumberField(TEXT("samples"),Samples.Num());Root->SetArrayField(TEXT("gaps"),Rows);
    FString Result;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Result));return Result;
#else
    return TEXT("{}");
#endif
}

FVector UOBInfiltrationEditorLibrary::GetHitImpactPoint(const FHitResult& Hit)
{
    return Hit.ImpactPoint;
}

void UOBInfiltrationEditorLibrary::PrepareForAuthoring(UWorld* World)
{
#if WITH_EDITOR
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (World) { World->UpdateWorldComponents(false,false); World->SendAllEndOfFrameUpdates(); }
#endif
}

bool UOBInfiltrationEditorLibrary::ConfigureAllyAnimation(UAnimBlueprint* Blueprint,UAnimSequence* Idle,UAnimSequence* Jog)
{
#if WITH_EDITOR
    if (!Blueprint || !Idle || !Jog || !Blueprint->ParentClass->IsChildOf(UOBAllyAnimInstance::StaticClass())
        || Idle->GetSkeleton()!=Blueprint->TargetSkeleton || Jog->GetSkeleton()!=Blueprint->TargetSkeleton) return false;
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
    UEdGraph* Graph=nullptr; UAnimGraphNode_Root* Root=nullptr;
    for (auto* Candidate : Graphs)
        for (UEdGraphNode* Node : Candidate->Nodes)
            if (auto* Result=Cast<UAnimGraphNode_Root>(Node)) { Graph=Candidate; Root=Result; break; }
    if (!Graph || !Root) return false;
    const auto Nodes=Graph->Nodes;
    for (UEdGraphNode* Node : Nodes) if (Node!=Root) FBlueprintEditorUtils::RemoveNode(Blueprint,Node,true);
    auto* IdleNode=NewObject<UAnimGraphNode_SequencePlayer>(Graph);
    IdleNode->Node.SetSequence(Idle); IdleNode->Node.SetLoopAnimation(true);
    auto* JogNode=NewObject<UAnimGraphNode_SequencePlayer>(Graph);
    JogNode->Node.SetSequence(Jog); JogNode->Node.SetLoopAnimation(true);
    auto* Blend=NewObject<UAnimGraphNode_BlendListByBool>(Graph);
    auto* Variable=NewObject<UK2Node_VariableGet>(Graph);
    Variable->SetFromProperty(FindFProperty<FBoolProperty>(UOBAllyAnimInstance::StaticClass(),TEXT("bIsMoving")),true,UOBAllyAnimInstance::StaticClass());
    TArray<UEdGraphNode*> Added={IdleNode,JogNode,Blend,Variable};
    for (auto* Node : Added) { Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); }
    IdleNode->NodePosX=-650; IdleNode->NodePosY=170;
    JogNode->NodePosX=-650; JogNode->NodePosY=-50;
    Blend->NodePosX=-300; Variable->NodePosX=-600; Variable->NodePosY=-200;
    Root->NodePosX=60; Root->NodePosY=0;
    auto Link=[&](UEdGraphNode* From,FName Out,UEdGraphNode* To,FName In) {
        auto* A=From->FindPin(Out); auto* B=To->FindPin(In);
        if (A && B && Graph->GetSchema()->TryCreateConnection(A,B)) return true;
        UE_LOG(LogTemp,Error,TEXT("Ally animation pin connection failed: %s -> %s"),*Out.ToString(),*In.ToString());
        return false;
    };
    if (!Link(IdleNode,TEXT("Pose"),Blend,TEXT("BlendPose_1")) || !Link(JogNode,TEXT("Pose"),Blend,TEXT("BlendPose_0"))
        || !Link(Variable,TEXT("bIsMoving"),Blend,TEXT("bActiveValue")) || !Link(Blend,TEXT("Pose"),Root,TEXT("Result"))) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status!=BS_Error;
#else
    return false;
#endif
}
ANavMeshBoundsVolume* UOBInfiltrationEditorLibrary::CreateNavigationBounds(UWorld* World,FVector Center,FVector Extent,bool bRebuildNavigation)
{
#if WITH_EDITOR
    if (!World || Extent.GetMin()<=0.f) return nullptr;
    auto* Volume=World->SpawnActor<ANavMeshBoundsVolume>();
    auto* Builder=NewObject<UCubeBuilder>();
    Builder->X=Extent.X*2; Builder->Y=Extent.Y*2; Builder->Z=Extent.Z*2;
    UActorFactory::CreateBrushForVolumeActor(Volume,Builder);
    Volume->SetActorLocation(Center);
    Volume->GetBrushComponent()->SetCanEverAffectNavigation(false);
    Volume->PostEditChange();
    if (!FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::EditorMode);
    if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World); Nav && bRebuildNavigation)
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        // This explicit commandlet bake has finished all asset loads; its editor ticker is not running.
        Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
        World->UpdateWorldComponents(false,false);
        World->SendAllEndOfFrameUpdates();
        Nav->OnNavigationBoundsUpdated(Volume);
        Nav->Tick(.02f);
        Nav->FlushPendingOperations();
        Nav->Build();
    }
    return Volume;
#else
    return nullptr;
#endif
}
bool UOBInfiltrationEditorLibrary::RebuildNavigation(UWorld* World)
{
#if WITH_EDITOR
    PrepareForAuthoring(World);
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    // Explicit commandlet rebuild: all asset loads are finished before releasing this lock.
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->Tick(.02f);
    Nav->FlushPendingOperations();
    Nav->Build();
    return true;
#else
    return false;
#endif
}

bool UOBInfiltrationEditorLibrary::TickNavigationBuild(UWorld* World)
{
#if WITH_EDITOR
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    FAssetCompilingManager::Get().ProcessAsyncTasks();
    FTSTicker::GetCoreTicker().Tick(.02f);
    Nav->Tick(.02f); FPlatformProcess::Sleep(.01f);
    return !Nav->IsNavigationBuildInProgress();
#else
    return false;
#endif
}

FString UOBInfiltrationEditorLibrary::DescribeEditableProperties(UObject* Object)
{
#if WITH_EDITOR
    if (!Object) return TEXT("{}");
    auto Root=MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Object->GetClass());It;++It)
    {
        FProperty* P=*It;
        if (!P->HasAnyPropertyFlags(CPF_Edit) || P->GetOwnerClass()!=Object->GetClass()) continue;
        FString Value;P->ExportText_InContainer(0,Value,Object,Object,Object,PPF_None);
        Root->SetStringField(P->GetName(),Value);
    }
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return Text;
#else
    return TEXT("{}");
#endif
}

TArray<FVector> UOBInfiltrationEditorLibrary::FindReachableSpawnLocations(UWorld* World,FVector PreferredCenter,FVector Goal,float Radius,float HalfHeight,float SearchRadius)
{
    TArray<FVector> Result;
#if WITH_EDITOR
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!World || !Nav || Radius<=0 || HalfHeight<Radius) return Result;
    FNavLocation End;
    if (!Nav->ProjectPointToNavigation(Goal,End,FVector(100,100,120))) return Result;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(OBSpawnAuthoring),false);
    for (TActorIterator<APawn> It(World);It;++It) Q.AddIgnoredActor(*It);
    const auto Capsule=FCollisionShape::MakeCapsule(Radius,HalfHeight);
    for (float Ring=0;Ring<=FMath::Clamp(SearchRadius,0.f,1500.f);Ring+=50.f)
    {
        for (int32 I=0;I<(Ring==0 ? 1 : 32);++I)
        {
            const float Angle=I*2.f*PI/32.f;
            const FVector XY=PreferredCenter+FVector(FMath::Cos(Angle)*Ring,FMath::Sin(Angle)*Ring,-HalfHeight);
            FHitResult Floor;
            if (!World->LineTraceSingleByChannel(Floor,XY+FVector(0,0,130),XY-FVector(0,0,130),ECC_Pawn,Q)
                || Floor.bStartPenetrating || Floor.ImpactNormal.Z<.7f) continue;
            FNavLocation Start;
            if (!Nav->ProjectPointToNavigation(Floor.ImpactPoint,Start,FVector(25,25,40))) continue;
            const FVector Center=Start.Location+FVector(0,0,HalfHeight+3);
            if (World->OverlapBlockingTestByProfile(Center,FQuat::Identity,TEXT("Pawn"),Capsule,Q)) continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Start.Location,End.Location);
            if (!Path || !Path->IsValid() || Path->IsPartial() || FVector::Dist2D(Start.Location,End.Location)<100.f) continue;
            Result.Add(Center);
            if (Result.Num()>=8) return Result;
        }
    }
#endif
    return Result;
}

FString UOBInfiltrationEditorLibrary::DescribeEncounterNavigation(UWorld* World)
{
#if WITH_EDITOR
    if (!World) return TEXT("{}");
    auto Root=MakeShared<FJsonObject>();
    auto Vector=[](FVector V)->TSharedPtr<FJsonValue> {
        return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
    };
    TArray<TSharedPtr<FJsonValue>> Bounds,Meshes,Spawners;
    for (TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)
    {
        FVector Center,Extent;It->GetActorBounds(false,Center,Extent);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),It->GetActorLabel());
        Row->SetStringField(TEXT("path"),It->GetPathName());
        Row->SetField(TEXT("center"),Vector(Center));Row->SetField(TEXT("extent"),Vector(Extent));
        Bounds.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (TActorIterator<ARecastNavMesh> It(World);It;++It)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),It->GetActorLabel());
        Row->SetNumberField(TEXT("tile_capacity"),It->GetNavMeshTilesCount());
        int32 Polygons=0;
        TArray<FNavTileRef> Tiles;It->GetAllNavMeshTiles(Tiles);
        for (FNavTileRef Tile : Tiles)
        {
            TArray<FNavPoly> TilePolys;
            if (It->GetPolysInTile(Tile,TilePolys)) Polygons+=TilePolys.Num();
        }
        Row->SetNumberField(TEXT("active_tiles"),It->GetNumActiveTiles());Row->SetNumberField(TEXT("polygons"),Polygons);
        Row->SetNumberField(TEXT("agent_radius"),It->GetConfig().AgentRadius);
        Row->SetNumberField(TEXT("agent_height"),It->GetConfig().AgentHeight);
        Meshes.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    int32 Movers=0,Complete=0;
    for (TActorIterator<AOBEncounterSpawner> It(World);It;++It)
    {
        auto* S=*It;const FString Folder=S->GetFolderPath().ToString();
        FString Zone;
        for (int32 I=1;I<=6;++I) if (Folder.Contains(FString::Printf(TEXT("/Zone%d/"),I))) {Zone=FString::Printf(TEXT("Zone%d"),I);break;}
        if (Zone.IsEmpty()) continue;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),S->GetActorLabel());
        Row->SetStringField(TEXT("zone"),Zone);Row->SetStringField(TEXT("path"),S->GetPathName());
        Row->SetField(TEXT("spawn"),Vector(S->GetActorLocation()));
        Row->SetField(TEXT("staging"),Vector(S->GetStagingTransform().GetLocation()));
        auto* Cover=FindFProperty<FObjectPropertyBase>(S->GetClass(),TEXT("CoverPoint"));
        auto* CoverComponent=Cover ? Cast<USceneComponent>(Cover->GetObjectPropertyValue_InContainer(S)) : nullptr;
        const FVector CoverGoal=CoverComponent ? CoverComponent->GetComponentLocation() : S->GetStagingTransform().GetLocation();
        Row->SetField(TEXT("cover"),Vector(CoverGoal));
        auto* ClassProperty=FindFProperty<FClassProperty>(S->GetClass(),TEXT("EnemyClass"));
        auto* Class=ClassProperty ? Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(S)) : nullptr;
        const auto* Defaults=Class ? Class->GetDefaultObject<AOBEnemyCharacter>() : nullptr;
        const float HalfHeight=Defaults ? Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
        const bool Moving=S->bParticipatesInCombat && S->bMoveToStagingPoint;
        Row->SetBoolField(TEXT("moving"),Moving);
        auto DescribePath=[&](FVector Start,FVector Goal,const TCHAR* Key) {
            FNavLocation A,B;
            const bool Projected=Nav && Nav->ProjectPointToNavigation(Start,A,FVector(100,100,120))
                && Nav->ProjectPointToNavigation(Goal,B,FVector(100,100,120));
            auto* Path=Projected ? UNavigationSystemV1::FindPathToLocationSynchronously(World,A.Location,B.Location) : nullptr;
            const bool Valid=Path && Path->IsValid() && !Path->IsPartial();
            Row->SetBoolField(FString(Key)+TEXT("_complete"),Valid);
            TArray<TSharedPtr<FJsonValue>> Points;
            if (Path) for (const FVector& P : Path->PathPoints) Points.Add(Vector(P));
            Row->SetArrayField(Key,Points);return Valid;
        };
        if (Moving)
        {
            ++Movers;
            if (DescribePath(S->GetActorLocation()-FVector(0,0,HalfHeight),S->GetStagingTransform().GetLocation(),TEXT("staging_path"))) ++Complete;
        }
        const FOBEnemyBehaviorSettings Behavior=S->bOverrideEnemyBehavior ? S->EnemyBehavior
            : Defaults ? Defaults->EncounterBehavior : FOBEnemyBehaviorSettings();
        const bool MoveToCover=S->bParticipatesInCombat && Behavior.CoverMode==EOBEnemyCoverMode::MoveToCover;
        Row->SetBoolField(TEXT("cover_active"),MoveToCover);
        if (MoveToCover) DescribePath(Moving ? S->GetStagingTransform().GetLocation() : S->GetActorLocation()-FVector(0,0,HalfHeight),CoverGoal,TEXT("cover_path"));
        Spawners.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("bounds"),Bounds);Root->SetArrayField(TEXT("navmeshes"),Meshes);
    Root->SetArrayField(TEXT("spawners"),Spawners);
    Root->SetNumberField(TEXT("moving"),Movers);Root->SetNumberField(TEXT("complete"),Complete);
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return Text;
#else
    return TEXT("{}");
#endif
}

bool UOBInfiltrationEditorLibrary::SetNavigationAuthoringLock(UWorld* World,bool bLocked)
{
#if WITH_EDITOR
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    if (bLocked) Nav->AddNavigationBuildLock(ENavigationBuildLock::Custom);
    else Nav->RemoveNavigationBuildLock(ENavigationBuildLock::Custom,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    return true;
#else
    return false;
#endif
}
