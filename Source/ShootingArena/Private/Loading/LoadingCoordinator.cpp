#include "Loading/LoadingCoordinator.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ALoadingCoordinator::ALoadingCoordinator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.0f);
	PrimaryActorTick.bCanEverTick = true;
}

void ALoadingCoordinator::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;

	const TCHAR* Option = GetWorld()->URL.GetOption(TEXT("ExpectedHumanPlayers="), nullptr);
	if (Option)
	{
		ExpectedHumanPlayers = FMath::Clamp(FCString::Atoi(Option), 1, 64);
		bExpectedCountWasProvided = true;
	}
	else
	{
		ExpectedHumanPlayers = FMath::Max(1, CountConnectedHumanPlayers());
		UE_LOG(LogTemp, Warning, TEXT("[Loading] ExpectedHumanPlayers URL option is absent; using current count %d."), ExpectedHumanPlayers);
	}

	GetWorldTimerManager().SetTimer(ServerReadyFallbackTimer, this, &ALoadingCoordinator::ApplyServerReadyFallback, 2.0f, false);
	RefreshServerState();
}

void ALoadingCoordinator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	RefreshAccumulator += DeltaSeconds;
	if (RefreshAccumulator >= 0.2f)
	{
		RefreshAccumulator = 0.0f;
		RefreshServerState();
	}
}

void ALoadingCoordinator::MarkServerWorldReady()
{
	if (!HasAuthority()) return;
	bServerWorldReady = true;
	GetWorldTimerManager().ClearTimer(ServerReadyFallbackTimer);
	RefreshServerState();
}

void ALoadingCoordinator::RefreshServerState()
{
	if (!HasAuthority()) return;
	ConnectedHumanPlayers = CountConnectedHumanPlayers();
	const ELoadingCoordinatorPhase NewPhase = !bServerWorldReady ? ELoadingCoordinatorPhase::Initializing
		: (ConnectedHumanPlayers < ExpectedHumanPlayers ? ELoadingCoordinatorPhase::WaitingForPlayers : ELoadingCoordinatorPhase::ReadyToPlay);
	if (Phase != NewPhase)
	{
		Phase = NewPhase;
		ForceNetUpdate();
	}
}

int32 ALoadingCoordinator::CountConnectedHumanPlayers() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState) return 0;
	int32 Count = 0;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (IsValid(PlayerState) && IsValid(PlayerState->GetPlayerController())) ++Count;
	}
	return Count;
}

void ALoadingCoordinator::ApplyServerReadyFallback()
{
	if (!HasAuthority() || bServerWorldReady) return;
	UE_LOG(LogTemp, Warning, TEXT("[Loading] MarkServerWorldReady was not called; using the 2-second fallback."));
	bServerWorldReady = true;
	RefreshServerState();
}

void ALoadingCoordinator::OnRep_Phase() {}

void ALoadingCoordinator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALoadingCoordinator, Phase);
	DOREPLIFETIME(ALoadingCoordinator, ExpectedHumanPlayers);
	DOREPLIFETIME(ALoadingCoordinator, ConnectedHumanPlayers);
}
