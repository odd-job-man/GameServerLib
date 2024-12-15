#include <WinSock2.h>
#include "SerialContent.h"

SerialContent::SerialContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer)
	:ContentsBase{ tickPerFrame,hCompletionPort,pqcsLimit,true,pGameServer }, sessionList{ offsetof(Session,node) }
{}

void SerialContent::WorkerHanlePacketAtRecvLoop(Packet * pPacket, Session * pSession)
{
	pSession->recvMsgQ_.Enqueue(pPacket);
}

void SerialContent::RequestFirstEnter(const void* pPlayer)
{
	Session* pSession = pGameServer_->GetSession(pPlayer);
	InterlockedIncrement(&pSession->IoCnt_);
	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, this);
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

void SerialContent::RegisterLeave(const void* pPlayer, int nextContent)
{
	// 일단 Release잡 들어오는거 막기위해 IoCnt 물고잇음
	Session* pSession = pGameServer_->GetSession(pPlayer);

	if (pSession->bRegisterLeave)
		__debugbreak();

	LONG IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// 이미 락프리큐에 RELEASE 잡이 와잇을것임
	if ((IoCnt & Session::RELEASE_FLAG) == Session::RELEASE_FLAG)
		return;

	pSession->bRegisterLeave = TRUE;
	pSession->ReservedNextContent = nextContent;
	delayedLeaveStack.push(pSession);
}

void SerialContent::RequestEnter(const bool bPrevContentsIsSerialize, Session* pSession)
{
	// 로비등의 직렬화 안되던 컨텐츠
	if (!bPrevContentsIsSerialize)
		InterlockedIncrement(&pSession->IoCnt_);

	InterlockedExchangePointer((PVOID*)&pSession->pCurContent, this);
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::ENTER, pSession));
}

void SerialContent::ReleaseSessionPost(Session* pSession)
{
	msgQ_.Enqueue(SerialContent::pool_.Alloc(en_MsgType::RELEASE, pSession));
}

void SerialContent::ReleaseSession(Session* pSession)
{
	if (InterlockedCompareExchange(&pSession->IoCnt_, Session::RELEASE_FLAG | 0, 0) != 0)
		return;

	// Release 될 Session의 직렬화 버퍼 정리
	for (LONG i = 0; i < pSession->lSendBufNum_; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	LONG sendQSize = pSession->sendPacketQ_.GetSize();
	for (LONG i = 0; i < sendQSize; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	closesocket(pSession->sock_);
	LONG recvQSize = pSession->recvMsgQ_.GetSize();
	for (LONG i = 0; i < recvQSize; ++i)
	{
		Packet* pPacket = pSession->recvMsgQ_.Dequeue().value();
		PACKET_FREE(pPacket);
	}

	pGameServer_->DisconnectStack_.Push((short)(pSession - pGameServer_->pSessionArr_));
	InterlockedIncrement(&pGameServer_->disconnectTPS_);
	InterlockedDecrement(&pGameServer_->lSessionNum_);
}

void SerialContent::FlushInterContentsMsgQ()
{
	while (1)
	{
		auto&& opt = msgQ_.Dequeue();
		if (!opt.has_value())
			break;

		InterContentsMessage* pMsg = opt.value();
		Session* pSession = pMsg->pSession_;
		void* pPlayer = pSession->pPlayer_;

		switch (pMsg->msgType_)
		{
		case en_MsgType::ENTER:
		{
			if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			{
				ReleaseSession(pSession);
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
	Session* pSession = (Session*)sessionList.GetFirst();
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

			if (pSession->bRegisterLeave)
				break;
		}
		pSession = (Session*)sessionList.GetNext(pSession);
	}
}

void SerialContent::FlushLeaveStack()
{
	while (!delayedLeaveStack.empty())
	{
		Session* pSession = delayedLeaveStack.top();
		pSession->bRegisterLeave = FALSE;
		OnLeave(pSession->pPlayer_);
		sessionList.remove(pSession);
		GetContentsPtr(pSession->ReservedNextContent)->RequestEnter(bSerial_, pSession);
		delayedLeaveStack.pop();
	}
}


