#pragma once
#include "Packet.h"
#include "UpdateBase.h"
#include "CTlsObjectPool.h"
#include "CLockFreeQueue.h"
#include "ThreadMsg.h"


class IContents : public UpdateBase
{
public:
	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	virtual void RequestFirstEnter(const void* pPlayer) = 0;
	virtual void RequestPlayerMove(int nextContents, const void* pPlayer) = 0;
	virtual void ReleaseSession();

	__forceinline void EnqMessage(const en_MsgType msgType, const Session* pSession)
	{
		InterContentsMessage* pMsg = pool_.Alloc(msgType, pSession);
	}

	__forceinline static void RegisterContents(const IContents* pContents, const int type)
	{
		if (type >= arrayLength) __debugbreak();
		pArr_[type] = const_cast<IContents*>(pContents);
	}


	CLockFreeQueue<InterContentsMessage*> MessageQ_;
	CTlsObjectPool<InterContentsMessage, true> pool_;
	GameServer* pGameServer_;
	static constexpr int arrayLength = 1000;
	static inline IContents* pArr_[arrayLength];
};
