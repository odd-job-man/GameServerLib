#include <WinSock2.h>
#include "ParallelContent.h"

ParallelContent::ParallelContent(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, GameServer* pGameServer)
	:ContentsBase{ tickPerFrame,hCompletionPort,pqcsLimit,false,pGameServer }
{
}
