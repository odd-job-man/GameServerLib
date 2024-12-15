#include <WinSock2.h>
#include "ContentsBase.h"



ContentsBase::ContentsBase(const DWORD tickPerFrame, const HANDLE hCompletionPort, const LONG pqcsLimit, const bool bSerial, GameServer* pGameServer)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }, pGameServer_{ pGameServer}, bSerial_{ bSerial }
{
}
