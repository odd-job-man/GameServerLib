#include <WinSock2.h>
#include "RingBuffer.h"
#include "GameSession.h"


BOOL GameSession::Init(SOCKET clientSock, ULONGLONG counter, SHORT idx, void* pPlayer)
{
    sock_ = clientSock;
    pPlayer_ = pPlayer;
    bSendingInProgress_ = FALSE;
    id_ = (counter << 16) ^ idx;
    lastRecvTime = GetTickCount64();
    bDisconnectCalled_ = FALSE;
    lSendBufNum_ = 0;
    recvRB_.ClearBuffer();
    return TRUE;
}

    //InterlockedExchange(&id_, ((counter << 16) ^ idx));