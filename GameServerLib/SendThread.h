#pragma once
#include "UpdateBase.h"
class GameServer;
class SendThread : public UpdateBase
{
public:
	SendThread(DWORD tickPerFrame, HANDLE hCompletionPort, LONG pqcsLimit, GameServer* pGameServer);
private:
	virtual void Update_IMPL() override;
	GameServer* pGameServer_;
};
