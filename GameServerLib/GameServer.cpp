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
#include "Scheduler.h"
#include "GameServer.h"
#include "ContentsBase.h"
#include "Assert.h"
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

	pSessionArr_ = new GameSession[maxSession_];

	// ���������� IOCP�� ����ؾ� AcceptEx�� ���� �Ϸ������� ������ ����
	CreateIoCompletionPort((HANDLE)hListenSock_, hcp_, (ULONG_PTR)pSessionArr_, NULL);
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

	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	GUID guid = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	if (SOCKET_ERROR == WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &pAcceptExFuncPtr, sizeof(pAcceptExFuncPtr), &bytes, NULL, NULL))
	{
		DWORD errCode = WSAGetLastError();
		LOG(L"ERROR", ERR, TEXTFILE, L"WSAIoCtl Failed ErrCode : %u", errCode);
		__debugbreak();
	}
	closesocket(sock);

	hSendPostEndEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	pSockAddrInArr_ = new SOCKADDR_IN[maxSession_];
	for (int i = maxSession_ - 1; i >= 0; --i)
		idxStack_.Push(i);

	Scheduler::Init();
}
GameServer::~GameServer()
{
}
//�뵵 : SendFlag�� ���� �����ϴٸ� �ٷ� Ŭ���̾�Ʈ���� �޽����� ����
void GameServer::SendPacket(ULONGLONG id, SmartPacket& sendPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	sendPacket->SetHeader<Net>();
	sendPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(sendPacket.GetPacket());
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

// �뵵: SendFlag�� ���� �����ϴٸ� �ٷ� Ŭ���̾�Ʈ���� �޽����� ����
void GameServer::SendPacket(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// �̹� RELEASE �Ϸ��� �ٽ� ������ ��Ȱ�� �Ǿ����� üũ
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���ڵ�


	pPacket->SetHeader<Net>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

// �뵵 : �޽����� Send�޽��� ť�� Enqueue�� �ϴ� �뵵�� ���. �ַ� �޽����� ��Ƽ� ������ ���� ���������� Ȱ��
void GameServer::EnqPacket(ULONGLONG id, Packet* pPacket)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	//char* pTemp = pPacket->GetPayloadStartPos<Net>();
	//if (*((WORD*)pTemp) == 101)
	//	__debugbreak();

	pPacket->SetHeader<Net>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
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
	InterlockedExchange((LONG*)&bStopAccept, TRUE);
	closesocket(hListenSock_);

	//�ɸ� AcceptEx�� ����, ���� ���ǵ� 0�ɶ����� ������
	while (InterlockedXor(&acceptAllocedCnt_, 0) != 0 && InterlockedXor(&lSessionNum_, 0) != 0)
	{
		for (int i = 0; i < maxSession_; ++i)
		{
			GameSession* pSession = pSessionArr_ + i;
			CancelIoEx((HANDLE)pSession->sock_, &pSession->recvOverlapped.overlapped);
			CancelIoEx((HANDLE)pSession->sock_, &pSession->sendOverlapped.overlapped);
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

const SOCKADDR_IN* GameServer::GetSockAddrIn(ULONGLONG sessionID)
{
	return pSockAddrInArr_ + GameSession::GET_SESSION_INDEX(sessionID);
}

void GameServer::Disconnect(ULONGLONG id)
{
	GameSession* pSession = pSessionArr_ + GameSession::GET_SESSION_INDEX(id);
	long IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// RELEASE������ Ȥ�� ����Ϸ�
	if ((IoCnt & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE�� ��Ȱ����� �Ǿ�����
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// Disconnect 1ȸ ����
	if ((bool)InterlockedExchange((LONG*)&pSession->bDisconnectCalled_, TRUE) == TRUE)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���� �����޴ٸ� ���� ���ǿ� ���ؼ� RELEASE ���� ȣ����� ������������ ����ȴ�
	CancelIoEx((HANDLE)pSession->sock_, &pSession->recvOverlapped.overlapped);
	CancelIoEx((HANDLE)pSession->sock_, &pSession->sendOverlapped.overlapped);

	// CancelIoExȣ��� ���ؼ� RELEASE�� ȣ��Ǿ���� ������ �������� InterlockedIncrement ������ ȣ���� �ȵ� ��� ����û��
	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

void GameServer::ProcessTimeOut()
{
	ULONGLONG currentTime = GetTickCount64();
	for (int i = 0; i < maxSession_; ++i)
	{
		ULONGLONG sessionId = pSessionArr_[i].id_;

		if ((pSessionArr_[i].refCnt_ & GameSession::RELEASE_FLAG) == GameSession::RELEASE_FLAG)
			continue;

		if (currentTime < pSessionArr_[i].lastRecvTime + TIME_OUT_MILLISECONDS_)
			continue;

		Disconnect(sessionId);
	}
}

// listen�� Completion Key�� GameSession* �迭�� �� ������ġ
unsigned __stdcall GameServer::IOCPWorkerThread(LPVOID arg)
{
	GameServer* pGameServer = (GameServer*)arg;
	while (1)
	{
		MYOVERLAPPED* pOverlapped = nullptr;
		DWORD dwNOBT = 0;
		GameSession* pSession = nullptr;
		bool bContinue = false;
		BOOL bGQCSRet = GetQueuedCompletionStatus(pGameServer->hcp_, &dwNOBT, 
			(PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);
		do
		{
			if (!pOverlapped && !dwNOBT && !pSession)
				return 0;

			// ������ IO�� ���� �Ϸ�����(Send,Recv�� ��쿡�� �����̲����� ���¶� ReleaseSession �ɰ��̰�, Accept�ǰ�쿡�� �ε����� ��ȯ�ϸ� ��)
			if (!bGQCSRet && pOverlapped)
			{
				if (pOverlapped->why == OVERLAPPED_REASON::ACCEPT)
				{
					InterlockedDecrement((LONG*)&pGameServer->acceptAllocedCnt_);
					GameSession* pTarget = (GameSession*)((char*)pOverlapped - offsetof(GameSession, acceptOverlapped));
					pGameServer->idxStack_.Push((short)(pTarget - pSession));
					bContinue = true;
					// ShutDown�� ���� ���������� ���� ��츦 �����ϰ� AcceptEx�� �����ϴ� ��Ȳ ��ü�� ������ ��Ȳ��
					if (InterlockedXor((LONG*)&pGameServer->bStopAccept, 0) != TRUE)
					{
						LOG(L"ERROR", ERR, TEXTFILE, L"AcceptEx Failed WithOut Close Listen Socket");
						__debugbreak();
					}
				}
				break;
			}

			switch (pOverlapped->why)
			{
			case OVERLAPPED_REASON::SEND:
			{
				pGameServer->SendProc(pSession, dwNOBT);
				break;
			}

			case OVERLAPPED_REASON::RECV:
			{
				pGameServer->RecvProc(pSession, dwNOBT);
				break;
			}

			case OVERLAPPED_REASON::TIMEOUT:
			{
				pGameServer->ProcessTimeOut();
				bContinue = true;
				break;
			}

			case OVERLAPPED_REASON::UPDATE:
			{
				((UpdateBase*)(pSession))->Update();
				bContinue = true;
				break;
			}

			case OVERLAPPED_REASON::POST:
			{
				pGameServer->OnPost(pSession);
				bContinue = true;
				break;
			}

			case OVERLAPPED_REASON::SEND_WORKER:
			{
				pGameServer->SendPost(pSession);
				break;
			}

			case OVERLAPPED_REASON::ACCEPT:
			{
				// CompletionKey�� TargetSession���� ����
				pSession = (GameSession*)((char*)(pOverlapped) - offsetof(GameSession, acceptOverlapped));
				pGameServer->AcceptProc(pSession);
				break;
			}

			case OVERLAPPED_REASON::CONNECT: // �Ⱦ�
			{
				break;
			}

			case OVERLAPPED_REASON::DISCONNECT: // �Ⱦ�
			{
				break;
			}

			case OVERLAPPED_REASON::DB_WRITE:
			{
				bContinue = true;
				break;
			}

			default:
				__debugbreak();
			}

		} while (0);

		if (bContinue)
		{
			while (!pGameServer->bStopAccept && pGameServer->AcceptPost() != -1);
			continue;
		}

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			pGameServer->ReleaseSession(pSession);

		while (!pGameServer->bStopAccept && pGameServer->AcceptPost() != -1);
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
	InterlockedIncrement(&pSession->refCnt_);
	int iRecvRet = WSARecv(pSession->sock_, wsa, 2, nullptr, &flags, (LPWSAOVERLAPPED)&(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, &pSession->recvOverlapped.overlapped);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->refCnt_));
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

		// SendFlag Ȯ�� �� ����
		if (InterlockedExchange((LONG*)&pSession->bSendingInProgress_, TRUE) == TRUE)
			return TRUE;

		dwBufferNum = pSession->sendPacketQ_.GetSize();

		if (dwBufferNum <= 0)
			InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
		else
			break;
	}

	WSABUF wsa[200];
	DWORD i;
	for (i = 0; i < 200 && i < dwBufferNum; ++i)
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
	InterlockedIncrement(&pSession->refCnt_);
	ZeroMemory(&(pSession->sendOverlapped.overlapped), sizeof(WSAOVERLAPPED));
	pSession->sendOverlapped.why = OVERLAPPED_REASON::SEND;
	int iSendRet = WSASend(pSession->sock_, wsa, i, nullptr, 0, (LPWSAOVERLAPPED) & (pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, &pSession->sendOverlapped.overlapped);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->refCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}


int GameServer::AcceptPost()
{
	std::optional<short> opt = idxStack_.Pop();
	if (!opt.has_value())
		return -1;

	short idx = opt.value();
	GameSession* pSession = pSessionArr_ + idx; // Pop�� �ε����� �ش��ϴ� ���� ã��
	pSession->Init(socket(AF_INET, SOCK_STREAM, 0), 
		InterlockedIncrement64((LONG64*)&ullIdCounter) - 1, idx, ((void*)((char*)pPlayerArr_ + playerSize_ * idx)));

	CreateIoCompletionPort((HANDLE)pSession->sock_, hcp_, (ULONG_PTR)pSession, 0); // IOCP�� ���� ���

	ZeroMemory(&pSession->acceptOverlapped.overlapped, sizeof(WSAOVERLAPPED));
	pSession->acceptOverlapped.why = OVERLAPPED_REASON::ACCEPT;

	DWORD trans;
	if (FALSE == pAcceptExFuncPtr(hListenSock_, pSession->sock_, pSession->recvRB_.GetWriteStartPtr(), 0, // AcceptEx
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &trans, &pSession->acceptOverlapped.overlapped))
	{
		DWORD errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			// ������ �����Ǿ����� ���濡 ���� ���� ������ ����� ���
			// ��� �������� ���� ���� ������ �ݾƹ��� ���(�ش��Լ� ���� ���� ShutDown ȣ��Ǿ� �÷��װ� �ٲ�� ���� ���� ������ -> �����Ϸ�)
			if (errCode == WSAECONNRESET || (errCode == WSAENOTSOCK && InterlockedXor((LONG*)&bStopAccept, 0) == TRUE))
			{
				idxStack_.Push(idx);
				return 0;
			}
			LOG(L"ERROR", ERR, TEXTFILE, L"AcceptEx Failed ErrCode : %u", WSAGetLastError());
			__debugbreak();
		}
	}
	InterlockedIncrement(&acceptAllocedCnt_);
	return 0;
}

void GameServer::ReleaseSession(GameSession* pSession) 
{
	if (InterlockedCompareExchange(&pSession->refCnt_, GameSession::RELEASE_FLAG | 0, 0) != 0)
		return;

	// Release �� Session�� Send����ȭ ���� ����
	for (LONG i = 0; i < pSession->lSendBufNum_; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	// Release �� Session�� Send�޽��� ť ����
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
	pSession->pCurContent->ReleaseSession(pSession);
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

void GameServer::AcceptProc(GameSession* pSession)
{
	InterlockedIncrement((LONG*)&acceptCounter_);
	InterlockedDecrement((LONG*)&acceptAllocedCnt_);

	// listen socket�� ���� �Ӽ��� �״�� ������(���� ��...)
	if (SOCKET_ERROR == setsockopt(pSession->sock_, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (const char*)&hListenSock_, sizeof(SOCKET)))
	{
		DWORD errCode = WSAGetLastError();
		if (errCode == WSAENOTSOCK && InterlockedXor((LONG*)&bStopAccept, 0) == TRUE)
			return;

		LOG(L"ERROR", ERR, TEXTFILE, L"setsockopt SO_UPDATE_ACCEPT_CONTEXT At AcceptProc Failed, errCode : %u", errCode);
		__debugbreak();
	}

	int sockAddrLen = sizeof(SOCKADDR_IN);
	SOCKADDR_IN* pSockAddrIn = pSockAddrInArr_ + (pSession - pSessionArr_);
	if (SOCKET_ERROR == getpeername(pSession->sock_, (SOCKADDR*)pSockAddrIn, &sockAddrLen))
	{
		DWORD errCode = WSAGetLastError();
		LOG(L"ERROR", ERR, TEXTFILE, L"getpeername At AcceptProc Failed, errCode : %u", errCode);
		__debugbreak();
	}

	if (OnConnectionRequest(pSockAddrIn) == FALSE)
	{
		closesocket(pSession->sock_);
		idxStack_.Push((short)(pSession - pSessionArr_));
		return;
	}
	InterlockedIncrement(&lSessionNum_);

	InterlockedIncrement(&pSession->refCnt_);
	InterlockedAnd(&pSession->refCnt_, ~GameSession::RELEASE_FLAG);

	OnAccept(pSession->pPlayer_);
	RecvPost(pSession);
}

void GameServer::InitialAccept()
{
	while (AcceptPost() != -1);
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

