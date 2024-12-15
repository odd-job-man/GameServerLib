#pragma once
#include "ThreadMessage.h"
#include "UpdateBase.h"
#include "Session.h"

class GameServer;

class IContents
{
public:
	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, Session* pSession) = 0;

};

class ContentsBase : public UpdateBase
{
public:
	ContentsBase(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, const bool bSerial, GameServer* pNetServer);
	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, Session* pSession) = 0;
	virtual void ReleaseSessionPost(Session* pSession) = 0;
	virtual void RequestFirstEnter(const void* pPlayer) = 0;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, Session* pSession) = 0;
	static inline void RegisterContents(int contentsType, const ContentsBase* pContent)
	{
		if (contentsType >= arrayLength) __debugbreak();
		pArr_[contentsType] = const_cast<ContentsBase*>(pContent);
	}

	__forceinline static void SetContentsToFirst(int firstContentType)
	{
		pFirst = pArr_[firstContentType];
	}

	__forceinline static void FirstEnter(const void* pPlayer)
	{
		const_cast<ContentsBase*>(pFirst)->RequestFirstEnter(pPlayer);
	}

	__forceinline static  ContentsBase* GetContentsPtr(int contentType)
	{
		return pArr_[contentType];
	}


	static constexpr int arrayLength = 1000;
	static inline ContentsBase* pArr_[arrayLength];
	static inline const ContentsBase* pFirst;
	GameServer* pGameServer_;
	const bool bSerial_;
	friend class GameServer;
};
