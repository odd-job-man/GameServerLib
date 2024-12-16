#pragma once
#include "ContentsBase.h"
class ParallelContent : public ContentsBase
{
public:
	ParallelContent(GameServer* pGameServer);

	// ContentBase overriding
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, GameSession* pSession) override;
	virtual void RequestFirstEnter(void* pPlayer) override;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, GameSession* pSession) override;
	virtual void ReleaseSessionPost(GameSession* pSession) override;
	virtual void RegisterLeave(void* pPlayer, int nextContent) override;
};
