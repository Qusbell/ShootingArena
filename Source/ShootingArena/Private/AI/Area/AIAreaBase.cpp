#include "AI/Area/AIAreaBase.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"

#if WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreDelegates.h"
#include "UObject/ConstructorHelpers.h"
#endif

AAIAreaBase::AAIAreaBase()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    AreaBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("AreaBounds"));
    AreaBounds->SetupAttachment(SceneRoot);
    AreaBounds->SetBoxExtent(FVector(500.0f, 500.0f, 200.0f));
    AreaBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AreaBounds->SetGenerateOverlapEvents(false);
    AreaBounds->SetHiddenInGame(true);

#if WITH_EDITORONLY_DATA
    /*
     * 에디터에서만 존재하는 시각화 컴포넌트입니다.
     * AreaBounds의 자식이 아니라 SceneRoot의 형제로 두어
     * AreaBounds Scale이 자식에게 중복 적용되는 가능성을 제거합니다.
     */
    AreaDebugPreview =
        CreateDefaultSubobject<UStaticMeshComponent>(
            TEXT("AreaDebugPreview"));

    AreaDebugPreview->SetupAttachment(SceneRoot);
    AreaDebugPreview->SetIsVisualizationComponent(true);
    AreaDebugPreview->SetCollisionEnabled(
        ECollisionEnabled::NoCollision);
    AreaDebugPreview->SetGenerateOverlapEvents(false);
    AreaDebugPreview->SetCanEverAffectNavigation(false);
    AreaDebugPreview->SetCastShadow(false);
    AreaDebugPreview->SetReceivesDecals(false);

    /*
     * Area Preview는 반투명 디버그 표시용이므로 Nanite를 사용하지 않습니다.
     * Translucent Material + Nanite 조합에서 발생하는 경고를 막고,
     * 실제 Area 판정/경로/충돌 로직에는 영향을 주지 않습니다.
     */
    AreaDebugPreview->bDisallowNanite = true;

    AreaDebugPreview->SetHiddenInGame(true);
    AreaDebugPreview->SetVisibility(true);

    /*
     * Engine Cube를 사용하지만, 실제 크기 계산은 100uu를 하드코딩하지 않고
     * StaticMesh Bounds를 직접 읽으므로 Mesh 크기가 달라도 정확하게 맞습니다.
     */
    static ConstructorHelpers::FObjectFinder<UStaticMesh>
        CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

    if (CubeMeshFinder.Succeeded())
    {
        AreaDebugPreview->SetStaticMesh(CubeMeshFinder.Object);
    }
#endif
}

void AAIAreaBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

#if WITH_EDITOR
    // BP UserConstructionScript가 끝난 뒤 최종 Preview Transform을 다시 맞춥니다.
    UpdateAreaDebugPreview();

    // 맵에 처음 배치하거나 Construction이 다시 실행될 때
    // World Outliner의 Display Name을 Area Name으로 저장합니다.
    BindActorLabelChangedDelegate();
    SyncAreaIdFromActorLabel();
#endif
}

void AAIAreaBase::PostActorCreated()
{
    Super::PostActorCreated();

#if WITH_EDITOR
    // 새로 배치되거나 복제된 Area도 Actor Label 변경을 즉시 받을 수 있게 등록합니다.
    BindActorLabelChangedDelegate();
    SyncAreaIdFromActorLabel();
#endif
}

void AAIAreaBase::PostLoad()
{
    Super::PostLoad();

#if WITH_EDITOR
    // 이미 맵에 존재하던 Area도 에디터를 다시 열면 현재 Display Name으로 맞춥니다.
    BindActorLabelChangedDelegate();
    SyncAreaIdFromActorLabel();
#endif
}

void AAIAreaBase::BeginDestroy()
{
#if WITH_EDITOR
    UnbindActorLabelChangedDelegate();
#endif

    Super::BeginDestroy();
}

bool AAIAreaBase::ContainsPosition(const FVector& WorldPosition) const
{
    if (!IsValid(AreaBounds) || !bAreaEnabled)
    {
        return false;
    }

    // 회전된 Box도 정확하게 검사할 수 있도록 월드 위치를 Box 로컬 공간으로 변환합니다.
    const FVector LocalPosition = AreaBounds->GetComponentTransform().InverseTransformPosition(WorldPosition);
    const FVector Extent = AreaBounds->GetUnscaledBoxExtent();

    return FMath::Abs(LocalPosition.X) <= Extent.X
        && FMath::Abs(LocalPosition.Y) <= Extent.Y
        && FMath::Abs(LocalPosition.Z) <= Extent.Z;
}

FVector AAIAreaBase::GetAreaCenter() const
{
    return IsValid(AreaBounds) ? AreaBounds->GetComponentLocation() : GetActorLocation();
}

FVector AAIAreaBase::GetAreaExtent() const
{
    return IsValid(AreaBounds) ? AreaBounds->GetScaledBoxExtent() : FVector::ZeroVector;
}

FVector AAIAreaBase::GetClosestPointInArea(const FVector& WorldPosition, const float Inset) const
{
    if (!IsValid(AreaBounds))
    {
        return GetActorLocation();
    }

    const FTransform& BoxTransform = AreaBounds->GetComponentTransform();
    const FVector LocalPosition = BoxTransform.InverseTransformPosition(WorldPosition);
    const FVector RawExtent = AreaBounds->GetUnscaledBoxExtent();

    // Inset이 Box 크기보다 커져 음수가 되지 않게 축마다 제한합니다.
    const FVector SafeExtent(
        FMath::Max(0.0f, RawExtent.X - Inset),
        FMath::Max(0.0f, RawExtent.Y - Inset),
        FMath::Max(0.0f, RawExtent.Z - Inset));

    const FVector ClampedLocal(
        FMath::Clamp(LocalPosition.X, -SafeExtent.X, SafeExtent.X),
        FMath::Clamp(LocalPosition.Y, -SafeExtent.Y, SafeExtent.Y),
        FMath::Clamp(LocalPosition.Z, -SafeExtent.Z, SafeExtent.Z));

    return BoxTransform.TransformPosition(ClampedLocal);
}

FVector AAIAreaBase::GetPointInsideTowards(const FVector& WorldTarget, const float Inset) const
{
    return GetClosestPointInArea(WorldTarget, Inset);
}

FBox AAIAreaBase::GetAreaBounds() const
{
    if (!IsValid(AreaBounds))
    {
        return FBox(GetActorLocation(), GetActorLocation());
    }

    // Component Bounds는 회전과 스케일까지 반영된 월드 AABB입니다.
    return AreaBounds->Bounds.GetBox();
}

#if WITH_EDITOR
void AAIAreaBase::UpdateAreaDebugPreview()
{
#if WITH_EDITORONLY_DATA
    /*
     * CDO(Default__...)와 Blueprint Archetype/Template에서는 MID를 생성하지 않습니다.
     * Template이 임시 UMaterialInstanceDynamic을 참조하면 패키지 저장 시
     * "Illegal reference to private object"가 발생할 수 있습니다.
     *
     * 맵에 실제로 배치된 Area 인스턴스만 아래 Preview MID를 생성합니다.
     */
    if (IsTemplate())
    {
        AreaDebugMID = nullptr;
        return;
    }

    if (!IsValid(AreaBounds) || !IsValid(AreaDebugPreview))
    {
        return;
    }

    UStaticMesh* PreviewMesh = AreaDebugPreview->GetStaticMesh();
    if (!IsValid(PreviewMesh))
    {
        AreaDebugPreview->SetVisibility(false);
        return;
    }

    /*
     * AreaBounds의 실제 월드 크기입니다.
     * GetScaledBoxExtent를 사용하므로 Actor/Component Scale까지 반영됩니다.
     */
    const FVector DesiredWorldSize =
        AreaBounds->GetScaledBoxExtent() * 2.0f;

    /*
     * Preview Mesh의 원본 로컬 크기입니다.
     * Engine Cube가 반드시 100uu라고 가정하지 않고 실제 Bounds를 사용합니다.
     */
    const FVector MeshLocalSize =
        PreviewMesh->GetBounds().BoxExtent * 2.0f;

    const FVector PreviewWorldScale(
        MeshLocalSize.X > KINDA_SMALL_NUMBER
            ? DesiredWorldSize.X / MeshLocalSize.X
            : 1.0f,
        MeshLocalSize.Y > KINDA_SMALL_NUMBER
            ? DesiredWorldSize.Y / MeshLocalSize.Y
            : 1.0f,
        MeshLocalSize.Z > KINDA_SMALL_NUMBER
            ? DesiredWorldSize.Z / MeshLocalSize.Z
            : 1.0f);

    /*
     * Relative Scale이 아니라 월드 Transform을 한 번에 적용합니다.
     * 따라서 부모 Scale이나 BP 컴포넌트 계층에 의한 중복 확대를 피합니다.
     */
    AreaDebugPreview->SetWorldTransform(
        FTransform(
            AreaBounds->GetComponentQuat(),
            AreaBounds->GetComponentLocation(),
            PreviewWorldScale));

    // Box 선 색도 Preview 색과 맞추고, 선택하지 않아도 선이 보이게 합니다.
    AreaBounds->ShapeColor = AreaDebugColor.ToFColor(true);
    AreaBounds->bDrawOnlyIfSelected = false;

    if (!IsValid(AreaDebugMaterial))
    {
        AreaDebugPreview->SetVisibility(false);
        AreaDebugMID = nullptr;
        return;
    }

    AreaDebugPreview->SetVisibility(true);

    /*
     * Construction 변경 시 Material Parent가 바뀔 수 있으므로
     * 새 MID를 만들어 현재 설정을 정확히 반영합니다.
     */
    AreaDebugMID =
        UMaterialInstanceDynamic::Create(
            AreaDebugMaterial,
            this);

    if (IsValid(AreaDebugMID))
    {
        /*
         * Material이 areaColor.A를 직접 Opacity로 사용하든,
         * previewOpacity Scalar를 사용하든 같은 투명도가 적용되게 합니다.
         */
        FLinearColor PreviewColor = AreaDebugColor;
        PreviewColor.A = AreaDebugOpacity;

        AreaDebugMID->SetVectorParameterValue(
            TEXT("areaColor"),
            PreviewColor);

        AreaDebugMID->SetScalarParameterValue(
            TEXT("previewOpacity"),
            AreaDebugOpacity);

        AreaDebugPreview->SetMaterial(
            0,
            AreaDebugMID);
    }
#endif
}

void AAIAreaBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    UpdateAreaDebugPreview();

    // 일반 에디터 속성 변경 후에도 Area Name을 현재 Display Name과 동일하게 유지합니다.
    SyncAreaIdFromActorLabel();
}

void AAIAreaBase::PostEditImport()
{
    Super::PostEditImport();

    // 복사/붙여넣기나 복제 후 새로 부여된 Display Name을 반영합니다.
    BindActorLabelChangedDelegate();
    UpdateAreaDebugPreview();
    SyncAreaIdFromActorLabel();
}

void AAIAreaBase::PostEditUndo()
{
    Super::PostEditUndo();

    // 이름 변경 Undo/Redo 이후에도 Preview와 두 이름을 동일하게 유지합니다.
    UpdateAreaDebugPreview();
    SyncAreaIdFromActorLabel();
}

void AAIAreaBase::BindActorLabelChangedDelegate()
{
    // CDO나 Blueprint Archetype은 맵 인스턴스용 이름 변경 이벤트를 받을 필요가 없습니다.
    if (IsTemplate() || ActorLabelChangedHandle.IsValid())
    {
        return;
    }

    ActorLabelChangedHandle = FCoreDelegates::OnActorLabelChanged.AddUObject(
        this,
        &AAIAreaBase::HandleActorLabelChanged);
}

void AAIAreaBase::UnbindActorLabelChangedDelegate()
{
    if (!ActorLabelChangedHandle.IsValid())
    {
        return;
    }

    FCoreDelegates::OnActorLabelChanged.Remove(ActorLabelChangedHandle);
    ActorLabelChangedHandle.Reset();
}

void AAIAreaBase::HandleActorLabelChanged(AActor* ChangedActor)
{
    // 모든 Actor 이름 변경 이벤트가 들어오므로, 이 Area 자신의 변경만 처리합니다.
    if (ChangedActor == this)
    {
        SyncAreaIdFromActorLabel();
    }
}

void AAIAreaBase::SyncAreaIdFromActorLabel()
{
    // CDO나 Blueprint Archetype에는 맵 인스턴스용 Display Name을 저장하지 않습니다.
    if (IsTemplate())
    {
        return;
    }

    // Actor Label은 에디터/Development 환경에서만 제공되므로,
    // 여기서 FName으로 저장해 패키징된 런타임에서도 같은 Area 식별자를 사용합니다.
    const FString CurrentDisplayName = GetActorLabel();
    if (!CurrentDisplayName.IsEmpty())
    {
        AreaId = FName(*CurrentDisplayName);
    }
}
#endif
