#pragma once
#include "ThreadMessage.h"
#include "Session.h"

class GameServer;

class ContentsBase
{
public:
	ContentsBase(const bool bSerial, GameServer* pGameServer)
		:pGameServer_{ pGameServer }, bSerial_{ bSerial } 
	{}

	virtual void OnEnter(void* pPlayer) = 0;
	virtual void OnLeave(void* pPlayer) = 0;
	virtual void OnRecv(Packet* pPacket, void* pPlayer) = 0;
	virtual void WorkerHanlePacketAtRecvLoop(Packet* pPacket, Session* pSession) = 0;
	virtual void ReleaseSessionPost(Session* pSession) = 0;
	virtual void RequestFirstEnter(void* pPlayer) = 0;
	virtual void RequestEnter(const bool bPrevContentsIsSerialize, Session* pSession) = 0;
	virtual void RegisterLeave(void* pPlayer, int nextContent) = 0;

	static inline void RegisterContents(int contentsType, const ContentsBase* pContent)
	{
		if (contentsType >= arrayLength) __debugbreak();
		pArr_[contentsType] = const_cast<ContentsBase*>(pContent);
	}

	__forceinline static void SetContentsToFirst(int firstContentType)
	{
		pFirst = pArr_[firstContentType];
	}

	__forceinline static void FirstEnter(void* pPlayer)
	{
		const_cast<ContentsBase*>(pFirst)->RequestFirstEnter(pPlayer);
	}

	__forceinline static  ContentsBase* GetContentsPtr(int contentType)
	{
		return pArr_[contentType];
	}

	void ReleaseSession(Session* pSession);
	static constexpr int arrayLength = 1000;
	static inline ContentsBase* pArr_[arrayLength];
	static inline const ContentsBase* pFirst;
	GameServer* pGameServer_;
	const bool bSerial_;
	friend class GameServer;
};
