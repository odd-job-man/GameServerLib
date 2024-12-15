#pragma once
#include "ContentsBase.h"
class ParallelContent : public ContentsBase
{
public:
	ParallelContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer);
	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, Session* pSession) override;
	virtual void RequestFirstEnter(const void* pPlayer) override;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, Session* pSession) override;
};
