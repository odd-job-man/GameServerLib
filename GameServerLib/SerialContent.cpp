#include <WinSock2.h>
#include "GameServer.h"
#include "SerialContent.h"

SerialContent::SerialContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }, ContentsBase{ true,pGameServer }, sessionList{ offsetof(GameSession,node) }
{}

SerialContent::~SerialContent()
{
}

void SerialContent::WorkerHanlePacketAtRecvLoop(Packet * pPacket, GameSession * pSession)
{
	pSession->recvMsgQ_.Enqueue(pPacket);
}

void SerialContent::RequestFirstEnter(void* pPlayer)
{
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	InterlockedIncrement(&pSession->refCnt_);
	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::ENTER, pSession));
}

void SerialContent::Update_IMPL()
{
	FlushInterContentsMsgQ();
	// �������ݻ����� ������ ������ �̵����� Ŭ�� Ű�Է��� �������� ������ ������ ������ �����ϸ� OnRecv���� RegisterLeave�� ���°� ��Ģ��
	// Ȥ�� Ŭ��� ���ǵǾ��ٸ� Ŭ�󿡼� Ư���޽����� ������ Ű �Է��� ���°��� ������ �ٷ� Leave�� ������ ����.
	FlushSessionRecvMsgQ();
	FlushLeaveStack();

	// ���� �ش� �Լ� �ڿ����� FlushLeaveStack()�� ȣ������ �ʴ´�.
	// Ex : �ٸ��÷��̾�� �¾� ������ ������ �̵��� ���ϴ� ������ ������ ������ ������ FlushSessionRecvMsgQ���� ȣ���� OnRecv���� ������ �޾� RegisterLeave�� ȣ���ؼ�
	// �Ʒ��� FlushLeaveStack()���� Leave ó���� ���������� ����
	ProcessEachPlayer();
}

void SerialContent::RegisterLeave(void* pPlayer, int nextContent)
{
	// �ϴ� Release�� �����°� �������� IoCnt ��������
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// �̹� ������ť�� RELEASE ���� ����������
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		InterlockedDecrement(&pSession->refCnt_);
		return;
	}

	pSession->ReservedNextContent = nextContent;
	delayedLeaveStack.push(pSession);
}

void SerialContent::RequestEnter(const bool bPrevContentsIsSerialize, GameSession* pSession)
{
	// �κ���� ����ȭ �ȵǴ� ������
	if (!bPrevContentsIsSerialize)
		InterlockedIncrement(&pSession->refCnt_);

	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, (ContentsBase*)this);
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::ENTER, pSession));
}

void SerialContent::ReleaseSession(GameSession* pSession)
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
		void* pPlayer = pGameServer_->GetPlayer(pSession);

		switch (pMsg->msgType_)
		{
		case en_MsgType::ENTER:
		{
			if (InterlockedDecrement(&pSession->refCnt_) == 0)
			{
				ReleaseSession_AT_ONCE_NOT_CALL_ONLEAVE(pSession);
				break;
			}

			sessionList.push_back(pSession);
			OnEnter(pPlayer);
			break;
		}
		case en_MsgType::RELEASE:
		{
			// ���������� �÷��̾� �����۾�(����ڰ� �������̵�)
			OnLeave(pPlayer);

			// ������ ���� ����Ʈ���� ���� ����
			sessionList.remove(pSession);

			//Session Recv Message Queue ����
			LONG size = pSession->recvMsgQ_.GetSize();
			for (LONG i = 0; i < size; ++i)
			{
#pragma warning(disable : 26815)
				Packet* pPacket = pSession->recvMsgQ_.Dequeue().value();
#pragma warning(default : 26815)
				PACKET_FREE(pPacket);
			}

			pGameServer_->idxStack_.Push((short)(pSession - pGameServer_->pSessionArr_));
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
		LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);
		if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
		{
			InterlockedDecrement(&pSession->refCnt_);
			pSession = (GameSession*)sessionList.GetNext(pSession);
			continue;
		}

		while (1)
		{
			auto&& opt = pSession->recvMsgQ_.Dequeue();
			if (!opt.has_value())
				break;

			Packet* pPacket = opt.value();
			OnRecv(pPacket, pGameServer_->GetPlayer(pSession));
			PACKET_FREE(pPacket);
		}

		if (pSession->sendPacketQ_.GetSize() > 0 && pSession->bSendingInProgress_ == FALSE)
		{
			InterlockedIncrement(&pSession->refCnt_);
			PostQueuedCompletionStatus(hcp_, 2, (ULONG_PTR)pSession, (LPOVERLAPPED)&pGameServer_->SendWorkerOverlapped);
		}

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
		{
			pGameServer_->ReleaseSession(pSession);
		}
		pSession = (GameSession*)sessionList.GetNext(pSession);
	}
}

void SerialContent::FlushLeaveStack()
{
	while (!delayedLeaveStack.empty())
	{
		GameSession* pSession = delayedLeaveStack.top();
		OnLeave(pGameServer_->GetPlayer(pSession));
		sessionList.remove(pSession);
		GetContentsPtr(pSession->ReservedNextContent)->RequestEnter(bSerial_, pSession);
		delayedLeaveStack.pop();
	}
}



