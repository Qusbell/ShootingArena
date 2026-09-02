#include "ShootingArenaGameInstance.h"

void UShootingArenaGameInstance::SetSavedNickname(const FString& NetworkAddress, const FString& Nickname)
{
	if (NetworkAddress.IsEmpty() || Nickname.IsEmpty())
	{
		return;
	}

	NicknameByNetworkAddress.Add(NetworkAddress, Nickname);
}

FString UShootingArenaGameInstance::GetSavedNickname(const FString& NetworkAddress) const
{
	if (const FString* Found = NicknameByNetworkAddress.Find(NetworkAddress))
	{
		return *Found;
	}

	return FString();
}
