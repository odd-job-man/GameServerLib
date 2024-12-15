#pragma once
#include <stack>
#include "ContentsBase.h"
class GameServer;

class SerialContent : public ContentsBase
{
public:
	SerialContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer);
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, Session* pSession) override;
	virtual void RequestFirstEnter(const void* pPlayer) override;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, Session* pSession) override;
	virtual void ReleaseSessionPost(Session* pSession) override;
	virtual void ProcessEachPlayer() = 0;
	virtual void Update_IMPL() override;
	void RegisterLeave(const void* pPlayer, int nextContent);

private:
	void ReleaseSession(Session* pSession);
	void FlushInterContentsMsgQ();
	void FlushSessionRecvMsgQ();
	void FlushLeaveStack();
	CLockFreeQueue<InterContentsMessage*> msgQ_;
	static inline CTlsObjectPool<InterContentsMessage, true> pool_;
	CLinkedList sessionList;
	std::stack<Session*> delayedLeaveStack;
};
