#include <WinSock2.h>
#include "GameServer.h"
#include "Packet.h"
#include "ParallelContent.h"

ParallelContent::ParallelContent(GameServer* pGameServer)
	:ContentsBase{ false,pGameServer }
{}

void ParallelContent::WorkerHanlePacketAtRecvLoop(Packet * pPacket, GameSession * pSession)
{
	pSession->recvMsgQ_.Enqueue(pPacket);
	Packet* pTargetPacket = pSession->recvMsgQ_.Dequeue().value();
	OnRecv(pPacket, pSession->pPlayer_);
	PACKET_FREE(pTargetPacket);
}

void ParallelContent::RequestFirstEnter(void* pPlayer)
{
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);
	OnEnter(pPlayer);
}

void ParallelContent::RequestEnter(const bool bPrevContentsIsSerialize, GameSession* pSession)
{
	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);

	if (bPrevContentsIsSerialize)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		{
			ReleaseSession(pSession);
			return;
		}
	}

	OnEnter(pSession->pPlayer_);
}

void ParallelContent::ReleaseSessionPost(GameSession* pSession)
{
	LONG size = pSession->recvMsgQ_.GetSize();
	for (LONG i = 0; i < size; ++i)
	{
		Packet* pPacket = pSession->recvMsgQ_.Dequeue().value();
		PACKET_FREE(pPacket);
	}

	OnLeave(pSession->pPlayer_);
	pGameServer_->DisconnectStack_.Push((short)(pSession - pGameServer_->pSessionArr_));
	InterlockedIncrement(&pGameServer_->disconnectTPS_);
	InterlockedDecrement(&pGameServer_->lSessionNum_);
}

void ParallelContent::RegisterLeave(void* pPlayer, int nextContent)
{
	OnLeave(pPlayer);
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	GetContentsPtr(nextContent)->RequestEnter(bSerial_, pSession);
}
