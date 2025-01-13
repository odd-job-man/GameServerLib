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
	// 프로토콜상으로 무조건 컨텐츠 이동전에 클라 키입력을 막기위한 핑퐁을 보내고 응답이 도착하면 OnRecv에서 RegisterLeave를 띄우는게 원칙임
	// 혹은 클라와 합의되엇다면 클라에서 특정메시지를 보내고 키 입력을 막는것을 전제로 바로 Leave를 띄울수도 있음.
	FlushSessionRecvMsgQ();
	FlushLeaveStack();

	// 따라서 해당 함수 뒤에서는 FlushLeaveStack()을 호출하지 않는다.
	// Ex : 다른플레이어에게 맞아 죽을시 컨텐츠 이동을 뜻하는 핑퐁을 보내고 이후의 루프인 FlushSessionRecvMsgQ에서 호출한 OnRecv에서 응답을 받아 RegisterLeave를 호출해서
	// 아래의 FlushLeaveStack()에서 Leave 처리후 컨텐츠에서 제거
	ProcessEachPlayer();
}

void SerialContent::RegisterLeave(void* pPlayer, int nextContent)
{
	// 일단 Release잡 들어오는거 막기위해 IoCnt 물고잇음
	GameSession* pSession = pGameServer_->GetSession(pPlayer);
	LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// 이미 락프리큐에 RELEASE 잡이 와잇을것임
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
	// 로비등의 직렬화 안되던 컨텐츠
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
		void* pPlayer = pSession->pPlayer_;

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
			OnLeave(pPlayer);
			sessionList.remove(pSession);

			// 라이브러리에서 일부분 정리햇으므로 GameServer::ReleaseSesion에서 closesocket이후만 수행하며됨
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
			OnRecv(pPacket, pSession->pPlayer_);
			PACKET_FREE(pPacket);
		}

		if (pSession->sendPacketQ_.GetSize() > 0 && pSession->bSendingInProgress_ == FALSE)
		{
			InterlockedIncrement(&pSession->refCnt_);
			PostQueuedCompletionStatus(hcp_, 2, (ULONG_PTR)pSession, (LPOVERLAPPED)&pGameServer_->SendWorkerOverlapped);
		}

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			pGameServer_->ReleaseSession(pSession);

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



