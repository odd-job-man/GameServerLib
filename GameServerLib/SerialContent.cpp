#include <WinSock2.h>
#include "SerialContent.h"
#include "GameServer.h"

SerialContent::SerialContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }, ContentsBase{ true,pGameServer }, sessionList{ offsetof(GameSession,node) }
{}

void SerialContent::WorkerHanlePacketAtRecvLoop(Packet * pPacket, GameSession * pSession)
{
	pSession->recvMsgQ_.Enqueue(pPacket);
}

void SerialContent::RequestFirstEnter(void* pPlayer)
{
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	InterlockedIncrement(&pSession->IoCnt_);
	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::ENTER, pSession));
}

void SerialContent::Update_IMPL()
{
	FlushInterContentsMsgQ();

	FlushSessionRecvMsgQ();
	FlushLeaveStack();
	
	ProcessEachPlayer();
	FlushLeaveStack();
}

void SerialContent::RegisterLeave(void* pPlayer, int nextContent)
{
	// �ϴ� Release�� �����°� �������� IoCnt ��������
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	LONG IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// �̹� ������ť�� RELEASE ���� ����������
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
		return;

	pSession->ReservedNextContent = nextContent;
	delayedLeaveStack.push(pSession);
}

void SerialContent::RequestEnter(const bool bPrevContentsIsSerialize, GameSession* pSession)
{
	// �κ���� ����ȭ �ȵǴ� ������
	if (!bPrevContentsIsSerialize)
		InterlockedIncrement(&pSession->IoCnt_);

	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::ENTER, pSession));
}

void SerialContent::ReleaseSessionPost(GameSession* pSession)
{
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::RELEASE, pSession));
}

void SerialContent::FlushInterContentsMsgQ()
{
	while (1)
	{
		auto&& opt = msgQ_.Dequeue();
		if (!opt.has_value())
			break;

		InterContentsMessage* pMsg = opt.value();
		GameSession* pSession = pMsg->pSession_;
		void* pPlayer = pSession->pPlayer_;

		switch (pMsg->msgType_)
		{
		case en_MsgType::ENTER:
		{
			if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			{
				ReleaseSession_AT_ONCE_NOT_CALL_ONLEAVE_ONRELEASE(pSession);
				break;
			}

			sessionList.push_back(pSession);
			OnEnter(pPlayer);
			break;
		}
		case en_MsgType::RELEASE:
		{
			OnLeave(pPlayer);
			sessionList.remove(pSession);
			LONG size = pSession->recvMsgQ_.GetSize();
			for (LONG i = 0; i < size; ++i)
			{
				Packet* pPacket = pSession->recvMsgQ_.Dequeue().value();
				PACKET_FREE(pPacket);
			}

			pGameServer_->DisconnectStack_.Push((short)(pSession - pGameServer_->pSessionArr_));
			InterlockedIncrement(&pGameServer_->disconnectTPS_);
			InterlockedDecrement(&pGameServer_->lSessionNum_);
			break;
		}
		default:
			__debugbreak();
			break;
		}

		pool_.Free(pMsg);
	}

}

void SerialContent::FlushSessionRecvMsgQ()
{
	GameSession* pSession = (GameSession*)sessionList.GetFirst();
	while (pSession)
	{
		while (1)
		{
			auto&& opt = pSession->recvMsgQ_.Dequeue();
			if (!opt.has_value())
				break;

			Packet* pPacket = opt.value();
			OnRecv(pPacket, pSession->pPlayer_);
			PACKET_FREE(pPacket);

		}
		pSession = (GameSession*)sessionList.GetNext(pSession);
	}
}

void SerialContent::FlushLeaveStack()
{
	while (!delayedLeaveStack.empty())
	{
		GameSession* pSession = delayedLeaveStack.top();
		OnLeave(pSession->pPlayer_);
		sessionList.remove(pSession);
		GetContentsPtr(pSession->ReservedNextContent)->RequestEnter(bSerial_, pSession);
		delayedLeaveStack.pop();
	}
}


