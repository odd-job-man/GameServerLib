#pragma once
#include <MSWSock.h>
#include "CLockFreeStack.h"
#include "MYOVERLAPPED.h"
#include "Scheduler.h"
#include "Monitorable.h"
#include "SendThread.h"

struct GameSession;
class Stack;
class Packet;
class SmartPacket;

class GameServer : public Monitorable
{
public:
	GameServer(const WCHAR* pConfigTxt);
	virtual ~GameServer();
	void SendPacket(ULONGLONG id, SmartPacket& sendPacket);
	void SendPacket(ULONGLONG id, Packet* pPacket);
	void SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket); // �ϳ��� ����ȭ���۸� ���������忡�� �����Ҷ� ���
	void EnqPacket(ULONGLONG id, Packet* pPacket); // ��Ƽ� ������ ������ SendQ�� Enque�� �ϰ� WSASend�� ȣ���ϰ� ���� ������ ���

	virtual BOOL OnConnectionRequest(const SOCKADDR_IN* pSockAddrIn) = 0; // �� IP�� ��ϵ� IP�� ���ؼ��� Accept������ �ٷ� ����� ���� �뵵�� ���ؼ� �����ϴ� �Լ���
	virtual void* OnAccept(void* pPlayer) = 0; // Accept ������ ������ ������� ���Ŀ� ������ �ִٸ� ���ָ� �ȴ�
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;

	ULONGLONG GetSessionID(const void* pPlayer);  // �÷��̾� ���� �ڽ��� ���� ID�� �����Ҷ� �� ���...
	const SOCKADDR_IN* GetSockAddrIn(ULONGLONG sessionID); // ����ڰ� ���� black ip���� ����Ҷ� ��� ������
	void Disconnect(ULONGLONG id); // ������ ������ ���

	// ShutDown �Ҷ� ���°� �Լ�
	void WaitUntilShutDown(); // ���ν������ start���� ������ �̰� ȣ���ؾ��Ѵ�
	void ShutDown(); // WaitUntilShutDown���� ȣ��
	void RequestShutDown(); // ��Ŀ�����尡 ȣ��

	// ShutDown�ÿ� ȣ��� �����Լ�
	// IOCP��Ŀ�����带 ���� �����Ű������ ���������� ���������� PQCS�� ������� ���� �Ϲ������� DB�����忡���� PQCS�� �� ����Ұ��̴�
	virtual void OnLastTaskBeforeAllWorkerThreadEndBeforeShutDown() = 0; 

	// Monitorable override
	virtual void OnMonitor() = 0; 

private:
	void ProcessTimeOut();
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	GameSession* GetSession(const void* pPlayer);
	void* GetPlayer(const GameSession* pSession);

	virtual BOOL SendPost(GameSession* pSession);
	int AcceptPost();
	void RecvProc(GameSession* pSession, int numberOfBytesTransferred);
	void SendProc(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void AcceptProc(GameSession* pSession);
	static inline LPFN_ACCEPTEX pAcceptExFuncPtr = nullptr;

protected:
	void InitialAccept();
public:
	// Accept
	const DWORD IOCP_WORKER_THREAD_NUM_ = 0;
	const DWORD IOCP_ACTIVE_THREAD_NUM_ = 0;
	LONG lSessionNum_ = 0;
	const LONG maxSession_ = 0;
	LONG lPlayerNum_ = 0;
	const LONG maxPlayer_ = 0;
	LONG playerSize_ = 0;
	const LONG TIME_OUT_MILLISECONDS_ = 0;
	const ULONGLONG TIME_OUT_CHECK_INTERVAL_ = 0;
	DWORD SEND_INTERVAL = 0;
	ULONGLONG ullIdCounter = 0;
	GameSession* pSessionArr_;
	CLockFreeStack<short> idxStack_;
	const MYOVERLAPPED SendWorkerOverlapped{ OVERLAPPED{},OVERLAPPED_REASON::SEND_WORKER };
	const MYOVERLAPPED OnPostOverlapped{ OVERLAPPED{},OVERLAPPED_REASON::POST };
	HANDLE hcp_;
	HANDLE* hIOCPWorkerThreadArr_;
	HANDLE hSendPostEndEvent_;
	HANDLE hShutDownEvent_;
	SOCKET hListenSock_;
	BOOL bStopAccept = FALSE;
	LONG updateThreadSendCounter_ = 0;
	void* pPlayerArr_;
	SOCKADDR_IN* pSockAddrInArr_;
	virtual BOOL RecvPost(GameSession* pSession);
	void SendProcAccum(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SetEntirePlayerMemory(int playerSize);
	void ReleaseSession(GameSession* pSession);
	friend class Packet;
	friend class SerialContent;
	friend class ParallelContent;
	friend class SendThread;
	const int bAccSend = 0;
	SendThread* pSendThread_ = nullptr;
public:
	alignas(64) ULONGLONG acceptCounter_ = 0;
	alignas(64) LONG acceptAllocedCnt_ = 0;
	alignas(64) ULONGLONG acceptTotal_ = 0;
	alignas(64) ULONGLONG recvTPS_ = 0;

	// Send (Per MSG)
	alignas(64) LONG sendTPS_ = 0;

	// Disconnect
	alignas(64) ULONGLONG disconnectTPS_ = 0;
};

