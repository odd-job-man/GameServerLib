#pragma once
#include <MSWSock.h>
#include "CLockFreeStack.h"
#include "MYOVERLAPPED.h"
#include "Timer.h"
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
	void SendPacket(ULONGLONG id, SmartPacket& sendPacket);
	void SendPacket(ULONGLONG id, Packet* pPacket);
	void SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket); // 하나의 직렬화버퍼를 여러스레드에서 공유할때 사용
	void SendPacket_ENQUEUE_ONLY(ULONGLONG id, Packet* pPacket); // 모아서 보내고 싶을때 SendQ에 Enque만 하고 WSASend는 호출하고 싶지 않을때 사용

	virtual BOOL OnConnectionRequest(const SOCKADDR_IN* pSockAddrIn) = 0; // 블랙 IP로 등록된 IP에 대해서는 Accept성공시 바로 끊어내는 등의 용도를 위해서 존재하는 함수임
	virtual void* OnAccept(void* pPlayer) = 0; // Accept 성공후 세션이 만들어진 이후에 할일이 있다면 해주면 된다
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;

	ULONGLONG GetSessionID(const void* pPlayer);  // 플레이어 등이 자신의 세션 ID를 저장할때 등등에 사용...
	const SOCKADDR_IN* GetSockAddrIn(const void* pPlayer); // 사용자가 직접 black ip등을 등록할때 사용 가능함
	void Disconnect(ULONGLONG id); // 세션을 끊을때 사용

	// ShutDown 할때 쓰는것 함수
	void WaitUntilShutDown(); // 메인스레드는 start이후 무조건 이걸 호출해야한다
	void ShutDown(); // WaitUntilShutDown에서 호출
	void RequestShutDown(); // 워커스레드가 호출

	// ShutDown시에 호출될 가상함수
	// IOCP워커스레드를 전부 종료시키기전에 마지막으로 수행할일을 PQCS로 쏘기위해 존재 일반적으로 DB스레드에대한 PQCS를 쏠때 사용할것이다
	virtual void OnLastTaskBeforeAllWorkerThreadEndBeforeShutDown() = 0; 

	// 해당 라이브러리 클래스를 상속받는 서버 인스턴스 클래스의 할당자원을 정리하는 가상함수
	virtual void OnResourceCleanAtShutDown() = 0;

	// Monitorable override
	virtual void OnMonitor() = 0; 

	// 나중에 추가된 임시기능
	void SENDPACKET(ULONGLONG id, SmartPacket& sendPacket);
private:
	void ProcessTimeOut();
	static unsigned __stdcall AcceptThread(LPVOID arg);
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	GameSession* GetSession(const void* pPlayer);
	void* GetPlayer(const GameSession* pSession);

	static inline LPFN_ACCEPTEX pAcceptExFuncPtr = nullptr;
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
	CLockFreeStack<short> DisconnectStack_;
	MYOVERLAPPED SendPostFrameOverlapped;
	MYOVERLAPPED SendWorkerOverlapped;
	MYOVERLAPPED OnPostOverlapped;
	HANDLE hcp_;
	HANDLE hAcceptThread_;
	HANDLE* hIOCPWorkerThreadArr_;
	HANDLE hSendPostEndEvent_;
	HANDLE hShutDownEvent_;
	SOCKET hListenSock_;
	LONG updateThreadSendCounter_ = 0;
	void* pPlayerArr_;
	SOCKADDR_IN* pSockAddrInArr_;
	virtual BOOL RecvPost(GameSession* pSession);
	virtual BOOL SendPost(GameSession* pSession);
	virtual BOOL SendPostAccum(GameSession* pSession);
	void RecvProc(GameSession* pSession, int numberOfBytesTransferred);
	void SendProc(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SendProcAccum(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SetEntirePlayerMemory(int playerSize);
	void ReleaseSession(GameSession* pSession);
	friend class Packet;
	friend class SerialContent;
	friend class ParallelContent;
	const int bAccSend = 0;
	SendThread* pSendThread_ = nullptr;
public:
	ULONGLONG acceptCounter_ = 0;
	alignas(64) ULONGLONG acceptTotal_ = 0;
	alignas(64) ULONGLONG recvTPS_ = 0;

	// Send (Per MSG)
	alignas(64) LONG sendTPS_ = 0;

	// Disconnect
	alignas(64) ULONGLONG disconnectTPS_ = 0;
};

