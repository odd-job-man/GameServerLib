#include <WinSock2.h>
#include "GameServer.h"
#include "SendThread.h"
#include <ContentsBase.h>

SendThread::SendThread(DWORD tickPerFrame, HANDLE hCompletionPort, LONG pqcsLimit, GameServer* pGameServer)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }, pGameServer_{ pGameServer }
{
}

void SendThread::Update_IMPL()
{
	__debugbreak();
	for (int i = 0; i < pGameServer_->maxSession_; ++i)
	{
		GameSession* pSession = pGameServer_->pSessionArr_ + i;
		long IoCnt = InterlockedIncrement(&pSession->refCnt_);

		// 이미 RELEASE 진행중이거나 RELEASE된 경우
		if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
		{
			if (InterlockedDecrement(&pSession->refCnt_) == 0)
				pGameServer_->ReleaseSession(pSession);
			continue;
		}

		pGameServer_->SendPost(pSession);

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			pGameServer_->ReleaseSession(pSession);
	}
}
