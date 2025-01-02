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
	OnEnter(pSession->pPlayer_);
	
	// OnEnter가 앞인 이유는 만약 OnEnter가 iocnt를 감소시킨 뒤 진행된다면 그사이에 라이브러리의 ReleaseSession이 호출될 수 잇음
	// 결국 해당클래스의 ReleaseSessionPost에서 OnLeave호출후 스택에 푸시함
	// 해당 함수는 멀티컨텐츠의 가상함수이기 때문에 OnEnter와 OnLeave가 동시에 호출되는 위험 && 재활용을
	// 없애기 위해 OnEnter호출후 IoCnt 내림
	if (bPrevContentsIsSerialize)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		{
			ReleaseSession_AT_ONCE_NOT_CALL_ONLEAVE_ONRELEASE(pSession);
			return;
		}
	}
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
