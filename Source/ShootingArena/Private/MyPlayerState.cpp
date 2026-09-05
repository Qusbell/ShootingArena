#include "MyPlayerState.h"
#include "MyReconnectionGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void AMyPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMyPlayerState, bWantsReturnToLobby);
	DOREPLIFETIME(AMyPlayerState, bIsHumanPlayer);
}

void AMyPlayerState::SetOwner(AActor* NewOwner)
{
	Super::SetOwner(NewOwner);

	// HasAuthority()가 아닌 클라이언트에서도 SetOwner가 (리플리케이션으로) 호출될 수 있는데,
	// 그때는 NewOwner가 relevancy 제약 때문에 부정확할 수 있으므로 서버에서만 계산합니다.
	if (HasAuthority())
	{
		bIsHumanPlayer = (Cast<APlayerController>(NewOwner) != nullptr);
		MARK_PROPERTY_DIRTY_FROM_NAME(AMyPlayerState, bIsHumanPlayer, this);
		FlushNetDormancy();
	}
}

void AMyPlayerState::Server_SetWantsReturnToLobby_Implementation(bool bWants)
{
	if (bWantsReturnToLobby == bWants)
	{
		return;
	}
	bWantsReturnToLobby = bWants;
	MARK_PROPERTY_DIRTY_FROM_NAME(AMyPlayerState, bWantsReturnToLobby, this);

	// 이 PlayerState가 NetDormancy(휴면)로 설정되어 있으면 Push Model dirty-mark만으로는
	// 리플리케이션이 실제로 나가지 않습니다 — 휴면 액터는 명시적으로 깨워야(flush) 즉시 전송됩니다.
	FlushNetDormancy();

	if (UWorld* World = GetWorld())
	{
		if (AMyReconnectionGameMode* GameMode = World->GetAuthGameMode<AMyReconnectionGameMode>())
		{
			GameMode->CheckReturnToLobbyVotes();
		}
	}
}

void AMyPlayerState::Multicast_UpdateReturnToLobbyVoteCounts_Implementation(int32 InVotedCount, int32 InEligibleCount)
{
	CachedVotedCount = InVotedCount;
	CachedEligibleCount = InEligibleCount;
	OnWantsReturnToLobbyChanged();
}

void AMyPlayerState::Server_RequestReturnToLobbyVotesRefresh_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (AMyReconnectionGameMode* GameMode = World->GetAuthGameMode<AMyReconnectionGameMode>())
		{
			GameMode->CheckReturnToLobbyVotes();
		}
	}
}

void AMyPlayerState::OnRep_WantsReturnToLobby()
{
	OnWantsReturnToLobbyChanged();
}

void AMyPlayerState::ComputeReturnToLobbyVotes(const UWorld* World, int32& OutVotedCount, int32& OutEligibleCount)
{
	OutVotedCount = 0;
	OutEligibleCount = 0;

	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return;
	}

	for (const APlayerState* PS : GameState->PlayerArray)
	{
		const AMyPlayerState* MyPS = Cast<AMyPlayerState>(PS);
		if (!MyPS || !MyPS->bIsHumanPlayer)
		{
			continue;
		}
		++OutEligibleCount;
		if (MyPS->bWantsReturnToLobby)
		{
			++OutVotedCount;
		}
	}
}

void AMyPlayerState::GetReturnToLobbyVoteCounts(int32& OutVotedCount, int32& OutEligibleCount) const
{
	// bWantsReturnToLobby를 매번 재계산하지 않고, 서버가 Multicast로 방송해준 캐시값을 그대로 씁니다
	// (일반 프로퍼티 리플리케이션이 지연/누락되는 문제를 피하기 위함).
	OutVotedCount = CachedVotedCount;
	OutEligibleCount = CachedEligibleCount;
}

FText AMyPlayerState::GetReturnToLobbyVoteCountsText() const
{
	int32 Voted = 0;
	int32 Eligible = 0;
	GetReturnToLobbyVoteCounts(Voted, Eligible);
	return FText::FromString(FString::Printf(TEXT("%d / %d"), Voted, Eligible));
}
