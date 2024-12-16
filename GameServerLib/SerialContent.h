#pragma once
#include <stack>
#include "UpdateBase.h"
#include "ContentsBase.h"
class GameServer;

class SerialContent :public UpdateBase, public ContentsBase
{
public:
	SerialContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer);

	// SerialContent 오리지널
	virtual void ProcessEachPlayer() = 0;

	// ContentBase overriding
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, GameSession* pSession) override;
	virtual void RequestFirstEnter(void* pPlayer) override;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, GameSession* pSession) override;
	virtual void ReleaseSessionPost(GameSession* pSession) override;
	virtual void RegisterLeave(void* pPlayer, int nextContent) override;

	// UpdateBase overriding;
	virtual void Update_IMPL() override;

private:
	void FlushInterContentsMsgQ();
	void FlushSessionRecvMsgQ();
	void FlushLeaveStack();
	CLockFreeQueue<InterContentsMessage*> msgQ_;
	static inline CTlsObjectPool<InterContentsMessage, true> pool_;
	CLinkedList sessionList;
	std::stack<GameSession*> delayedLeaveStack;
};
