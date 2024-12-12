#pragma once
#include "UpdateBase.h"
#include "ContentsType.h"
#include "GameServer.h"
#include "Session.h"
#include "CLinkedList.h"

enum class en_MsgType
{
	ENTER,
	RELEASE
};

using en_CMsgType = const en_MsgType;

struct InterContentsMessage
{
	en_CMsgType msgType_;
	CSession* pSession_;

	InterContentsMessage(en_CMsgType msgType, CSession* pSession)
		:msgType_{ msgType }, pSession_{ pSession }
	{}
};

class ContentsBase : public UpdateBase
{
public:
	using CContentsBase = const ContentsBase;
	ContentsBase(DWORD tickPerFrame, HANDLE hCompletionPort, LONG pqcsLimit, GameServer* pNetServer);
	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	void RequestContentsMove(en_CContentsType nextContents, Session* pSession, bool bFirst);
	void FlushInterContentsMsgQ();
	void FlushSessionRecvMsgQ();

	__forceinline void EnqMessage(en_CMsgType msgType, CSession* pSession)
	{
		InterContentsMessage* pMsg = pool_.Alloc(msgType, pSession);
		MessageQ_.Enqueue(pMsg);
	}

	__forceinline static void RegisterContents(CContentsBase* pContents, en_CContentsType type)
	{
		if (((int)type) >= arrayLength) __debugbreak();
		pArr_[(int)type] = const_cast<ContentsBase*>(pContents);
	}

	static  ContentsBase* GetContentsPtr(en_CContentsType type)
	{
		return pArr_[(int)type];
	}

private:
	friend class CTlsObjectPool<InterContentsMessage, true>;
	CLockFreeQueue<InterContentsMessage*> MessageQ_;
	CTlsObjectPool<InterContentsMessage, true> pool_;
	GameServer* pGameServer_;
	CLinkedList sessionList;
	static constexpr int arrayLength = 1000;
	static inline ContentsBase* pArr_[arrayLength];
};

