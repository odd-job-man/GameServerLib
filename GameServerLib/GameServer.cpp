#include <Winsock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <process.h>
#include "CLockFreeQueue.h"
#include "RingBuffer.h"
#include "GameSession.h"
#include "Packet.h"
#include "ErrType.h"
#include "Logger.h"
#include "Parser.h"
#include <locale>
#include "Timer.h"
#include "GameServer.h"
#include "ContentsBase.h"
#pragma comment(lib,"LoggerMt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib,"TextParser.lib")
#pragma comment(lib,"Winmm.lib")


#define GetPlayerPtr(idx) ((void*)((char*)(pGameServer->pPlayerArr_) + pGameServer->playerSize_ * idx))

GameServer::GameServer(const WCHAR* pConfigTxt)
	:hShutDownEvent_{ CreateEvent(NULL,FALSE,FALSE,NULL) }
{
	timeBeginPeriod(1);
	std::locale::global(std::locale(""));
	char* pStart;
	char* pEnd;
	PARSER psr = CreateParser(pConfigTxt);

	WCHAR ipStr[16];
	GetValue(psr, L"BIND_IP", (PVOID*)&pStart, (PVOID*)&pEnd);
	unsigned long long stringLen = (pEnd - pStart) / sizeof(WCHAR);
	wcsncpy_s(ipStr, _countof(ipStr) - 1, (const WCHAR*)pStart, stringLen);
	// Null terminated String ���� ������ InetPtonW��������
	ipStr[stringLen] = 0;

	ULONG ip;
	InetPtonW(AF_INET, ipStr, &ip);
	GetValue(psr, L"BIND_PORT", (PVOID*)&pStart, nullptr);
	short SERVER_PORT = (short)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"IOCP_WORKER_THREAD", (PVOID*)&pStart, nullptr);
	*(DWORD*)(&IOCP_WORKER_THREAD_NUM_) = (DWORD)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"IOCP_ACTIVE_THREAD", (PVOID*)&pStart, nullptr);
	*(DWORD*)(&IOCP_ACTIVE_THREAD_NUM_) = (DWORD)_wtoi((LPCWSTR)pStart);
	updateThreadSendCounter_ = IOCP_ACTIVE_THREAD_NUM_;

	GetValue(psr, L"IS_ZERO_BYTE_SEND", (PVOID*)&pStart, nullptr);
	int bZeroByteSend = _wtoi((LPCWSTR)pStart);

	GetValue(psr, L"SESSION_MAX", (PVOID*)&pStart, nullptr);
	*(LONG*)(&maxSession_) = _wtoi((LPCWSTR)pStart);

	GetValue(psr, L"PACKET_CODE", (PVOID*)&pStart, nullptr);
	Packet::PACKET_CODE = (unsigned char)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"PACKET_KEY", (PVOID*)&pStart, nullptr);
	Packet::FIXED_KEY = (unsigned char)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"TIME_OUT_MILLISECONDS", (PVOID*)&pStart, nullptr);
	*((ULONGLONG*)&TIME_OUT_MILLISECONDS_) = _wtoi((LPCWSTR)pStart);

	GetValue(psr, L"TIME_OUT_CHECK_INTERVAL", (PVOID*)&pStart, nullptr);
	*((ULONGLONG*)&TIME_OUT_CHECK_INTERVAL_) = (ULONGLONG)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"bAccSend", (PVOID*)&pStart, nullptr);
	*((int*)(&bAccSend)) = (int)_wtoi((LPCWSTR)pStart);

	if (bAccSend == 1)
	{
		GetValue(psr, L"SEND_INTERVAL", (PVOID*)&pStart, nullptr);
		SEND_INTERVAL = (DWORD)_wtoi((LPCWSTR)pStart);
	}

	GetValue(psr, L"USER_MAX", (PVOID*)&pStart, nullptr);
	*((LONG*)&maxPlayer_) = (int)_wtoi((LPCWSTR)pStart);
	ReleaseParser(psr);

#ifdef DEBUG_LEAK
	InitializeCriticalSection(&Packet::cs_for_debug_leak);
#endif

	int retval;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");
	// NOCT�� 0���� �����μ��� ����ŭ�� ������
	hcp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, IOCP_ACTIVE_THREAD_NUM_);
	if (!hcp_)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"CreateIoCompletionPort Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	hListenSock_ = socket(AF_INET, SOCK_STREAM, 0);
	if (hListenSock_ == INVALID_SOCKET)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET OK");

	// bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = ip;
	serveraddr.sin_port = htons(SERVER_PORT);
	retval = bind(hListenSock_, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");

	// listen
	retval = listen(hListenSock_, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen() OK");

	linger linger;
	linger.l_linger = 0;
	linger.l_onoff = 1;
	setsockopt(hListenSock_, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");

	if (bZeroByteSend == 1)
	{
		DWORD dwSendBufSize = 0;
		setsockopt(hListenSock_, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize));
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"ZeroByte Send OK");
	}
	else
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"NO ZeroByte Send");
	}

	hIOCPWorkerThreadArr_ = new HANDLE[IOCP_WORKER_THREAD_NUM_];
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
	{
		hIOCPWorkerThreadArr_[i] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, CREATE_SUSPENDED, nullptr);
		if (!hIOCPWorkerThreadArr_[i])
		{
			LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE WorkerThread Fail ErrCode : %u", WSAGetLastError());
			__debugbreak();
		}
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);

	// ���� 17��Ʈ�� ������ ������Ʈ�� 16�� ���ϰ� �Ǵ³����� ������� ū�׸��̴�.
	if (!CAddressTranslator::CheckMetaCntBits())
		__debugbreak();


	hAcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, CREATE_SUSPENDED, nullptr);
	if (!hAcceptThread_)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");


	SendPostFrameOverlapped.why = OVERLAPPED_REASON::SEND_POST_FRAME;
	hSendPostEndEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	OnPostOverlapped.why = OVERLAPPED_REASON::POST;
	SendWorkerOverlapped.why = OVERLAPPED_REASON::SEND_WORKER;

	pSessionArr_ = new GameSession[maxSession_];
	pSockAddrInArr_ = new SOCKADDR_IN[maxSession_];
	for (int i = maxSession_ - 1; i >= 0; --i)
		DisconnectStack_.Push(i);

	if (bAccSend == 1)
		pSendThread_ = new SendThread{ SEND_INTERVAL,hcp_,3,this };

	Scheduler::Init();

	if (bAccSend == 1)
		Scheduler::Register_UPDATE(static_cast<UpdateBase*>(pSendThread_));
}

void GameServer::SendPacket(ULONGLONG id, SmartPacket& sendPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���ڵ�
	sendPacket->SetHeader<Net>();
	sendPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(sendPacket.GetPacket());
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::SendPacket(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���ڵ�
	pPacket->SetHeader<Net>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);

	SendPost(pSession);
	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);

	SendPost(pSession);
	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::SendPacket_ENQUEUE_ONLY(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	pPacket->SetHeader<Net>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);
	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::WaitUntilShutDown()
{
	WaitForSingleObject(hShutDownEvent_, INFINITE);
	ShutDown();
}

void GameServer::ShutDown()
{
	// ��Ŀ�����忡�� ȣ���Ѱ�� �ȵ�, ��Ŀ������ RequestShutDown�� ȣ���ؾ���
	HANDLE hDebug = GetCurrentThread();
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
	{
		if (hIOCPWorkerThreadArr_[i] == hDebug)
		{
			LOG(L"ERROR", ERR, CONSOLE, L"WORKER Call Shutdown Must Have To Call RequestShutDown", Packet::packetPool_.capacity_, Packet::packetPool_.size_);
			LOG(L"ERROR", ERR, TEXTFILE, L"WORKER Call Shutdown Must Have To Call RequestShutDown", Packet::packetPool_.capacity_, Packet::packetPool_.size_);
			__debugbreak();
		}
	}
	// ���������� �ݾƼ� Accept�� ���´�
	closesocket(hListenSock_);
	WaitForSingleObject(hAcceptThread_, INFINITE);
	CloseHandle(hAcceptThread_);

	//���� 0�ɶ����� ������
	while (InterlockedXor(&lSessionNum_, 0) != 0)
	{
		for (int i = 0; i < maxSession_; ++i)
		{
			CancelIoEx((HANDLE)pSessionArr_[i].sock_, nullptr);
			InterlockedExchange((LONG*)&pSessionArr_[i].bDisconnectCalled_, TRUE);
		}
	}

	// ���̻� PQCS�� ������ �����Ƿ� UpdateBase* �� PQCS�� ��°��� �������� Timer�����带 �����Ѵ�
	Scheduler::Release_SchedulerThread();

	// ������ DB� ���� �ܿ����� ó���� PQCS���� ���⼭ ���
	OnLastTaskBeforeAllWorkerThreadEndBeforeShutDown();

	// ��Ŀ�����带 �����ϱ����� PQCS�� ��� ����Ѵ�
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
		PostQueuedCompletionStatus(hcp_, 0, 0, 0);

	WaitForMultipleObjects(IOCP_WORKER_THREAD_NUM_, hIOCPWorkerThreadArr_, TRUE, INFINITE);

	CloseHandle(hcp_);
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
		CloseHandle(hIOCPWorkerThreadArr_[i]);
	delete[] pSessionArr_;
	delete[] pSockAddrInArr_;
	CloseHandle(hSendPostEndEvent_);
	CloseHandle(hShutDownEvent_);

	if (bAccSend == 1)
		delete pSendThread_;
	OnResourceCleanAtShutDown();
}

void GameServer::RequestShutDown()
{
	SetEvent(hShutDownEvent_);
}

ULONGLONG GameServer::GetSessionID(const void* pPlayer)
{
	short idx = (short)(((ULONG_PTR)pPlayer - (ULONG_PTR)pPlayerArr_) / playerSize_);
	return ((GameSession*)(pSessionArr_ + idx))->id_;
}

const SOCKADDR_IN* GameServer::GetSockAddrIn(const void* pPlayer)
{
	short idx = (short)(((ULONG_PTR)pPlayer - (ULONG_PTR)pPlayerArr_) / playerSize_);
	return pSockAddrInArr_ + idx;
}

void GameServer::Disconnect(ULONGLONG id)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);

	// RELEASE������ Ȥ�� ����Ϸ�
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE�� ��Ȱ����� �Ǿ�����
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// Disconnect 1ȸ ����
	if ((bool)InterlockedExchange((LONG*)&pSession->bDisconnectCalled_, TRUE) == TRUE)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���� �����޴ٸ� ���� ���ǿ� ���ؼ� RELEASE ���� ȣ����� ������������ ����ȴ�
	CancelIoEx((HANDLE)pSession->sock_, nullptr);

	// CancelIoExȣ��� ���ؼ� RELEASE�� ȣ��Ǿ���� ������ �������� InterlockedIncrement ������ ȣ���� �ȵ� ��� ����û��
	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::ProcessTimeOut()
{
	ULONGLONG currentTime = GetTickCount64();
	for (int i = 0; i < maxSession_; ++i)
	{
		ULONGLONG sessionId = pSessionArr_[i].id_;

		if ((pSessionArr_[i].IoCnt_ & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
			continue;

		if (currentTime < pSessionArr_[i].lastRecvTime + TIME_OUT_MILLISECONDS_)
			continue;

		Disconnect(sessionId);
	}
}



unsigned __stdcall GameServer::AcceptThread(LPVOID arg)
{
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen;
	GameServer* pGameServer = (GameServer*)arg;
	addrlen = sizeof(clientAddr);

	while (1)
	{
		clientSock = accept(pGameServer->hListenSock_, (SOCKADDR*)&clientAddr, &addrlen);
		InterlockedIncrement((LONG*)&pGameServer->acceptCounter_);

		if (clientSock == INVALID_SOCKET)
		{
			DWORD dwErrCode = WSAGetLastError();
			if (dwErrCode != WSAEINTR && dwErrCode != WSAENOTSOCK)
			{
				__debugbreak();
			}
			return 0;
		}

		if (!pGameServer->OnConnectionRequest(&clientAddr))
		{
			closesocket(clientSock);
			continue;
		}

		InterlockedIncrement((LONG*)&pGameServer->lSessionNum_);

		short idx = pGameServer->DisconnectStack_.Pop().value();
		GameSession* pSession = pGameServer->pSessionArr_ + idx;
		pSession->Init(clientSock, pGameServer->ullIdCounter, idx, GetPlayerPtr(idx));
		memcpy(pGameServer->pSockAddrInArr_ + idx, &clientAddr, sizeof(SOCKADDR_IN));

		CreateIoCompletionPort((HANDLE)pSession->sock_, pGameServer->hcp_, (ULONG_PTR)pSession, 0);
		++pGameServer->ullIdCounter;

		InterlockedIncrement(&pSession->IoCnt_);
		InterlockedAnd(&pSession->IoCnt_, ~GameSession::RELEASE_FLAG);

		pGameServer->OnAccept(pSession->pPlayer_);
		pGameServer->RecvPost(pSession);

		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			pGameServer->ReleaseSession(pSession);
	}
	return 0;
}

unsigned __stdcall GameServer::IOCPWorkerThread(LPVOID arg)
{
	GameServer* pNetServer = (GameServer*)arg;
	while (1)
	{
		MYOVERLAPPED* pOverlapped = nullptr;
		DWORD dwNOBT = 0;
		GameSession* pSession = nullptr;
		bool bContinue = false;
		BOOL bGQCSRet = GetQueuedCompletionStatus(pNetServer->hcp_, &dwNOBT, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);
		do
		{
			if (!pOverlapped && !dwNOBT && !pSession)
				return 0;

			//��������
			if (bGQCSRet && dwNOBT == 0)
				break;

			if (!bGQCSRet && pOverlapped)
				break;

			switch (pOverlapped->why)
			{
			case OVERLAPPED_REASON::SEND:
				pNetServer->SendProc(pSession, dwNOBT);
				break;

			case OVERLAPPED_REASON::RECV:
				pNetServer->RecvProc(pSession, dwNOBT);
				break;

			case OVERLAPPED_REASON::TIMEOUT:
				pNetServer->ProcessTimeOut();
				bContinue = true;
				break;

			case OVERLAPPED_REASON::SEND_POST_FRAME:
				bContinue = true;
				break;

			case OVERLAPPED_REASON::SEND_ACCUM:
				pNetServer->SendProcAccum(pSession, dwNOBT);
				break;

			case OVERLAPPED_REASON::UPDATE:
				((UpdateBase*)(pSession))->Update();
				bContinue = true;
				break;

			case OVERLAPPED_REASON::POST:
				pNetServer->OnPost(pSession);
				bContinue = true;
				break;

			case OVERLAPPED_REASON::SEND_WORKER:
				pNetServer->SendPost(pSession);
				InterlockedExchange((LONG*)&pSession->bSendingAtWorker_, FALSE);
				break;

			case OVERLAPPED_REASON::CONNECT: // �Ⱦ�
				break;

			case OVERLAPPED_REASON::DISCONNECT: // �Ⱦ�
				break;

			case OVERLAPPED_REASON::DB_WRITE:
				bContinue = true;
				break;

			default:
				__debugbreak();
			}

		} while (0);

		if (bContinue)
			continue;

		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			pNetServer->ReleaseSession(pSession);
	}
	return 0;
}

GameSession* GameServer::GetSession(const void* pPlayer)
{
	short idx = (short)(((ULONG_PTR)pPlayer - (ULONG_PTR)(pPlayerArr_)) / playerSize_);
	return pSessionArr_ + idx;
}

void* GameServer::GetPlayer(const GameSession* pSession)
{
	return (void*)((char*)pPlayerArr_ + playerSize_ * (pSession - pSessionArr_));
}

BOOL GameServer::RecvPost(GameSession* pSession)
{
	WSABUF wsa[2];
	wsa[0].buf = pSession->recvRB_.GetWriteStartPtr();
	wsa[0].len = pSession->recvRB_.DirectEnqueueSize();
	wsa[1].buf = pSession->recvRB_.Buffer_;
	wsa[1].len = pSession->recvRB_.GetFreeSize() - wsa[0].len;

	ZeroMemory(&pSession->recvOverlapped, sizeof(WSAOVERLAPPED));
	pSession->recvOverlapped.why = OVERLAPPED_REASON::RECV;
	DWORD flags = 0;
	InterlockedIncrement(&pSession->IoCnt_);
	int iRecvRet = WSARecv(pSession->sock_, wsa, 2, nullptr, &flags, (LPWSAOVERLAPPED)&(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, nullptr);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->IoCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

BOOL GameServer::SendPost(GameSession* pSession)
{
	DWORD dwBufferNum;
	while (1)
	{
		if (pSession->sendPacketQ_.GetSize() <= 0)
			return FALSE;

		// ���� ���� TRUE�� �ٲ۴�. ���� TRUE���ٸ� ��ȯ���� TRUE�ϰ��̸� �׷��ٸ� ���� SEND �������̱� ������ �׳� ����������
		// �� ���ǹ��� ��ġ�� ���Ͽ� Out�� �ٲ��� ���������� ����ȴ�.
		// ������ SendPost ������ü�� Send�Ϸ����� �������� ��쿡�� in�� ��ġ�� SendPacket���� ���ؼ� �ٲ���� �ִ�.
		// iUseSize�� ���ϴ� ���������� DirectDequeueSize�� ���� �޶������ִ�.
		if (InterlockedExchange((LONG*)&pSession->bSendingInProgress_, TRUE) == TRUE)
			return TRUE;

		// SendPacket���� in�� �Űܼ� UseSize�� 0���� Ŀ���������� Send�Ϸ������� �����ؼ� Out�� �ű�� �÷��� ���� Recv�Ϸ����� �����尡 ���� SendPost�� ������ �÷��׸� �����Ѱ�� UseSize�� 0�̳��´�.
		// ���⼭ flag�� �ٽ� FALSE�� �ٲپ����� �ʾƼ� ����߻�
		dwBufferNum = pSession->sendPacketQ_.GetSize();

		if (dwBufferNum <= 0)
			InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
		else
			break;
	}

	WSABUF wsa[50];
	DWORD i;
	for (i = 0; i < 50 && i < dwBufferNum; ++i)
	{
#pragma warning(disable : 26815)
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
#pragma warning(default : 26815)
		wsa[i].buf = (char*)pPacket->pBuffer_;
		wsa[i].len = pPacket->GetUsedDataSize() + sizeof(Packet::NetHeader);
		pSession->pSendPacketArr_[i] = pPacket;
	}

	InterlockedExchange(&pSession->lSendBufNum_, i);
	InterlockedAdd(&sendTPS_, i);
	InterlockedIncrement(&pSession->IoCnt_);
	ZeroMemory(&(pSession->sendOverlapped.overlapped), sizeof(WSAOVERLAPPED));
	pSession->sendOverlapped.why = OVERLAPPED_REASON::SEND;
	int iSendRet = WSASend(pSession->sock_, wsa, i, nullptr, 0, (LPWSAOVERLAPPED)&(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, nullptr);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->IoCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

BOOL GameServer::SendPostAccum(GameSession* pSession)
{
	DWORD dwBufferNum;
	while (1)
	{
		if (pSession->sendPacketQ_.GetSize() <= 0)
			return FALSE;

		// ���� ���� TRUE�� �ٲ۴�. ���� TRUE���ٸ� ��ȯ���� TRUE�ϰ��̸� �׷��ٸ� ���� SEND �������̱� ������ �׳� ����������
		// �� ���ǹ��� ��ġ�� ���Ͽ� Out�� �ٲ��� ���������� ����ȴ�.
		// ������ SendPost ������ü�� Send�Ϸ����� �������� ��쿡�� in�� ��ġ�� SendPacket���� ���ؼ� �ٲ���� �ִ�.
		// iUseSize�� ���ϴ� ���������� DirectDequeueSize�� ���� �޶������ִ�.
		if (InterlockedExchange((LONG*)&pSession->bSendingInProgress_, TRUE) == TRUE)
			return TRUE;

		// SendPacket���� in�� �Űܼ� UseSize�� 0���� Ŀ���������� Send�Ϸ������� �����ؼ� Out�� �ű�� �÷��� ���� Recv�Ϸ����� �����尡 ���� SendPost�� ������ �÷��׸� �����Ѱ�� UseSize�� 0�̳��´�.
		// ���⼭ flag�� �ٽ� FALSE�� �ٲپ����� �ʾƼ� ����߻�
		dwBufferNum = pSession->sendPacketQ_.GetSize();
		if (dwBufferNum <= 0)
			InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
		else
			break;
	}

	WSABUF wsa[50];
	DWORD i;
	for (i = 0; i < 50 && i < dwBufferNum; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
		wsa[i].buf = (char*)pPacket->pBuffer_;
		wsa[i].len = pPacket->GetUsedDataSize() + sizeof(Packet::NetHeader);
		pSession->pSendPacketArr_[i] = pPacket;
	}

	InterlockedExchange(&pSession->lSendBufNum_, i);
	InterlockedAdd(&sendTPS_, i);
	InterlockedIncrement(&pSession->IoCnt_);
	ZeroMemory(&(pSession->sendOverlapped.overlapped), sizeof(WSAOVERLAPPED));
	pSession->sendOverlapped.why = OVERLAPPED_REASON::SEND_ACCUM;
	int iSendRet = WSASend(pSession->sock_, wsa, i, nullptr, 0, (LPWSAOVERLAPPED)&(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, nullptr);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->IoCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

void GameServer::ReleaseSession(GameSession* pSession) 
{
	if (InterlockedCompareExchange(&pSession->IoCnt_, GameSession::RELEASE_FLAG | 0, 0) != 0)
		return;

	// Release �� Session�� ����ȭ ���� ����
	for (LONG i = 0; i < pSession->lSendBufNum_; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	LONG size = pSession->sendPacketQ_.GetSize();
	for (LONG i = 0; i < size; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	closesocket(pSession->sock_);
	pSession->pCurContent->ReleaseSessionPost(pSession);
}

void GameServer::RecvProc(GameSession* pSession, int numberOfBytesTransferred)
{
	using NetHeader = Packet::NetHeader;
	pSession->recvRB_.MoveInPos(numberOfBytesTransferred);
	while (1)
	{
		Packet::NetHeader header;
		if (pSession->recvRB_.Peek((char*)&header, sizeof(NetHeader)) == 0)
			break;

		if (header.code_ != Packet::PACKET_CODE)
		{
			Disconnect(pSession->id_);
			return;
		}

		if (pSession->recvRB_.GetUseSize() < sizeof(NetHeader) + header.payloadLen_)
		{
			if (header.payloadLen_ > BUFFER_SIZE)
			{
				Disconnect(pSession->id_);
				return;
			}
			break;
		}

		pSession->recvRB_.MoveOutPos(sizeof(NetHeader));
		Packet* pPacket = PACKET_ALLOC(Net);
		pSession->recvRB_.Dequeue(pPacket->GetPayloadStartPos<Net>(), header.payloadLen_);
		pPacket->MoveWritePos(header.payloadLen_);
		memcpy(pPacket->pBuffer_, &header, sizeof(Packet::NetHeader));

		// �ݼ��������� ȣ��Ǵ� �Լ��� ���� �� ���ڵ��� üũ�� Ȯ��
		if (pPacket->ValidateReceived() == false)
		{
			PACKET_FREE(pPacket);
			Disconnect(pSession->id_);
			return;
		}

		pSession->lastRecvTime = GetTickCount64();
		InterlockedIncrement(&recvTPS_);

		pSession->pCurContent->WorkerHanlePacketAtRecvLoop(pPacket, pSession);
	}
	RecvPost(pSession);
}

void GameServer::SendProc(GameSession* pSession, DWORD dwNumberOfBytesTransferred)
{
	LONG sendBufNum = InterlockedExchange(&pSession->lSendBufNum_, 0);
	for (LONG i = 0; i < sendBufNum; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
	SendPost(pSession);
}

void GameServer::SendProcAccum(GameSession* pSession, DWORD dwNumberOfBytesTransferred)
{
	LONG sendBufNum = InterlockedExchange(&pSession->lSendBufNum_, 0);
	for (LONG i = 0; i < sendBufNum; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}
	InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
}

void GameServer::SetEntirePlayerMemory(int playerSize)
{
	playerSize_ = playerSize;
	pPlayerArr_ = malloc(playerSize * maxSession_);
}

void GameServer::SENDPACKET(ULONGLONG id, SmartPacket& sendPacket)
{
	if (bAccSend == 1)
		SendPacket_ENQUEUE_ONLY(id, sendPacket.GetPacket());
	else
		SendPacket(id, sendPacket.GetPacket());
}

