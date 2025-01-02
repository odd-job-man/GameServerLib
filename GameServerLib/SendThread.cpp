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
	for (int i = 0; i < pGameServer_->maxSession_; ++i)
	{
		GameSession* pSession = pGameServer_->pSessionArr_ + i;
		long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

		// �̹� RELEASE �������̰ų� RELEASE�� ���
		if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
		{
			if (InterlockedDecrement(&pSession->IoCnt_) == 0)
				pGameServer_->ReleaseSession(pSession);
			continue;
		}

		pGameServer_->SendPostAccum(pSession);

		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			pGameServer_->ReleaseSession(pSession);
	}
}
