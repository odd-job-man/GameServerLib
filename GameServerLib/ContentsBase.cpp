#include <WinSock2.h>
#include "ContentsBase.h"

void ContentsBase::ReleaseSession(Session* pSession)
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
