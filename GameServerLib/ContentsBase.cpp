#include <WinSock2.h>
#include "GameServer.h"
#include "ContentsBase.h"

#define GetPlayerPtr(sessionID) ((void*)((char*)(pGameServer_->pPlayerArr_) + pGameServer_->playerSize_ * Session::GET_SESSION_INDEX(sessionID)));

ContentsBase::ContentsBase(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pNetServer)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }, pGameServer_{ pNetServer }, sessionList{ offsetof(Session,node) }
{
}

void ContentsBase::RequestContentsMove(en_CContentsType nextContents, Session* pSession, bool bFirst)
{
	InterlockedIncrement(&pSession->IoCnt_);
	InterlockedExchangePointer((PVOID*)&pSession->pContent, GetContentsPtr(nextContents));
	if (bFirst)
	{
		void* pPlayer = GetPlayerPtr(pSession->id_);
		sessionList.remove(pSession);
		OnLeave(pPlayer);
	}
	pSession->pContent->EnqMessage(en_MsgType::ENTER, pSession);
}

void ContentsBase::FlushInterContentsMsgQ()
{
	while (1)
	{
		auto&& opt = MessageQ_.Dequeue();
		if (!opt.has_value())
			break;

		InterContentsMessage* pMessage = opt.value();
		Session* pSession = const_cast<Session*>(pMessage->pSession_);

		switch (pMessage->msgType_)
		{
		case en_MsgType::ENTER:
		{
			if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			{
				pGameServer_->ReleaseSession(pSession);
				break;
			}
			sessionList.push_back(pSession);
			void* pPlayer = GetPlayerPtr(pSession->id_);
			OnEnter(pPlayer);
			break;
		}
		case en_MsgType::RELEASE:
		{
			void* pPlayer = GetPlayerPtr(pSession->id_);
			sessionList.remove(pSession);
			OnLeave(pPlayer);
			pGameServer_->ReleaseSessionContents(pSession);
			break;
		}
		default:
			__debugbreak();
			break;
		}
	}
}

void ContentsBase::FlushSessionRecvMsgQ()
{
	Session* pSession = (Session*)sessionList.GetFirst();
	while (pSession)
	{
		while (1)
		{
			std::optional<Packet*>&& opt = pSession->recvMsgQ_.Dequeue();
			if (!opt.has_value())
				break;

			Packet* pPacket = opt.value();
			void* pPlayer = GetPlayerPtr(pSession->id_);

			OnRecv(pPacket, pPlayer);
			PACKET_FREE(pPacket);
		}
		pSession = (Session*)sessionList.GetNext(pSession);
	}
}
