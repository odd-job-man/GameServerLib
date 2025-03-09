#pragma once
#include <MSWSock.h>
#include "CLockFreeStack.h"
#include "MYOVERLAPPED.h"
#include "Scheduler.h"
#include "Monitorable.h"
#include "SendThread.h"
#include "GameServerTimeOUt.h"

struct GameSession;
class Stack;
class Packet;
class SmartPacket;

class GameServer : public Monitorable
{
public:
	GameServer(WCHAR* pIP, USHORT port, DWORD iocpWorkerNum, DWORD cunCurrentThreadNum, BOOL bZeroCopy, LONG maxSession, LONG maxUser, LONG playerSize, BYTE packetCode, BYTE packetfixedKey);
	virtual ~GameServer();
	void SendPacket(ULONGLONG sessionID, SmartPacket& sendPacket);
	void SendPacket(ULONGLONG sessionID, Packet* pPacket);
	void SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket); // �ϳ��� ����ȭ���۸� ���������忡�� �����Ҷ� ���
	void EnqPacket(ULONGLONG sessionID, Packet* pPacket); // ��Ƽ� ������ ������ SendQ�� Enque�� �ϰ� WSASend�� ȣ���ϰ� ���� ������ ���

	void SetLogin(ULONGLONG sessionID);
	virtual BOOL OnConnectionRequest(const WCHAR* pIP, const USHORT port) = 0; // �� IP�� ��ϵ� IP�� ���ؼ��� Accept������ �ٷ� ����� ���� �뵵�� ���ؼ� �����ϴ� �Լ���
	virtual void* OnAccept(void* pPlayer) = 0; // Accept ������ ������ ������� ���Ŀ� ������ �ִٸ� ���ָ� �ȴ�
	virtual void OnError(ULONGLONG sessionID, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;

	ULONGLONG GetSessionID(const void* pPlayer);  // �÷��̾� ���� �ڽ��� ���� ID�� �����Ҷ� �� ���...
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
	const WCHAR* GetIp(ULONGLONG sessionId);
	const USHORT GetPort(ULONGLONG sessionId);

private:
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	//static unsigned __stdcall AcceptThread(LPVOID arg);
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
	ULONGLONG ullIdCounter = 0;
	GameSession* pSessionArr_;
	CLockFreeStack<short> idxStack_;
	const MYOVERLAPPED SendWorkerOverlapped{ OVERLAPPED{},OVERLAPPED_REASON::SEND_WORKER };
	const MYOVERLAPPED OnPostOverlapped{ OVERLAPPED{},OVERLAPPED_REASON::POST };
	HANDLE hcp_;
	HANDLE* hIOCPWorkerThreadArr_;
	HANDLE hShutDownEvent_;
	SOCKET hListenSock_;
	BOOL bStopAccept = FALSE;
	void* pPlayerArr_;
	virtual BOOL RecvPost(GameSession* pSession);
	void SetEntirePlayerMemory(int playerSize);
	void ReleaseSession(GameSession* pSession);
	friend class Packet;
	friend class SerialContent;
	friend class ParallelContent;
	friend class SendThread;
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


