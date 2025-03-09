#include <WinSock2.h>
#include "GameServer.h"
#include "Packet.h"
#include "ParallelContent.h"

ParallelContent::ParallelContent(GameServer* pGameServer)
	:ContentsBase{ false,pGameServer }
{}

ParallelContent::~ParallelContent()
{
}

void ParallelContent::WorkerHanlePacketAtRecvLoop(Packet* pPacket, GameSession* pSession)
{
	pSession->recvMsgQ_.Enqueue(pPacket);

#pragma warning(disable : 26815)
	Packet* pTargetPacket = pSession->recvMsgQ_.Dequeue().value();
#pragma warning(default : 26815)

	OnRecv(pPacket, pGameServer_->GetPlayer(pSession));
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
	OnEnter(pGameServer_->GetPlayer(pSession));
	
	// OnEnter�� ���� ������ ���� OnEnter�� iocnt�� ���ҽ�Ų �� ����ȴٸ� �׻��̿� ���̺귯���� ReleaseSession�� ȣ��� �� ����
	// �ᱹ �ش�Ŭ������ ReleaseSessionPost���� OnLeaveȣ���� ���ÿ� Ǫ����
	// �ش� �Լ��� ��Ƽ�������� �����Լ��̱� ������ OnEnter�� OnLeave�� ���ÿ� ȣ��Ǵ� ���� && ��Ȱ����
	// ���ֱ� ���� OnEnterȣ���� IoCnt ����
	if (bPrevContentsIsSerialize)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
		{
			ReleaseSession_AT_ONCE_NOT_CALL_ONLEAVE(pSession);
			return;
		}
	}
}

void ParallelContent::ReleaseSession(GameSession* pSession)
{
	LONG size = pSession->recvMsgQ_.GetSize();
	// ������ ���� �޽��� ť ���
	for (LONG i = 0; i < size; ++i)
	{
#pragma warning(disable : 26815)
		Packet* pPacket = pSession->recvMsgQ_.Dequeue().value();
#pragma warning(default : 26815)
		PACKET_FREE(pPacket);
	}

	OnLeave(pGameServer_->GetPlayer(pSession));

	pGameServer_->idxStack_.Push((short)(pSession - pGameServer_->pSessionArr_));
	InterlockedIncrement(&pGameServer_->disconnectTPS_);
	InterlockedDecrement(&pGameServer_->lSessionNum_);
}

void ParallelContent::RegisterLeave(void* pPlayer, int nextContent)
{
	OnLeave(pPlayer);
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	GetContentsPtr(nextContent)->RequestEnter(bSerial_, pSession);
}

