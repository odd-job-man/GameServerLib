#pragma once
#include "CLockFreeStack.h"
#include "MyOVERLAPPED.h"
#include "Timer.h"

#include "Monitorable.h"

struct GameSession;
class Stack;
class Packet;
class SmartPacket;

class GameServer : public Monitorable
{
public:
	GameServer();
	// Test¿ë
	void SENDPACKET(ULONGLONG id, SmartPacket& sendPacket);
	void SEND_POST_PER_FRAME();
	void SendPacket(ULONGLONG id, SmartPacket& sendPacket);
	void SendPacket(ULONGLONG id, Packet* pPacket);
	void SendPacket_ALREADY_ENCODED(ULONGLONG id, Packet* pPacket);
	void SendPacket_ENQUEUE_ONLY(ULONGLONG id, Packet* pPacket);
	virtual BOOL OnConnectionRequest() = 0;
	virtual void* OnAccept(void* pPlayer) = 0;
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;
	// Monitorable
	virtual void OnMonitor() = 0; 

	ULONGLONG GetSessionID(const void* pPlayer);
	// µð¹ö±ë¿ë
	void Disconnect(ULONGLONG id);
	void SendPostPerFrame();
protected:
	void SendPostPerFrame_IMPL(LONG* pCounter);
private:
	void ProcessTimeOut();
	static unsigned __stdcall AcceptThread(LPVOID arg);
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	GameSession* GetSession(const void* pPlayer);
	void* GetPlayer(const GameSession* pSession);
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
	ULONGLONG ullIdCounter = 0;
	GameSession* pSessionArr_;
	CLockFreeStack<short> DisconnectStack_;
	MYOVERLAPPED SendPostFrameOverlapped;
	MYOVERLAPPED SendWorkerOverlapped;
	MYOVERLAPPED OnPostOverlapped;
	HANDLE hcp_;
	HANDLE hAcceptThread_;
	HANDLE* hIOCPWorkerThreadArr_;
	HANDLE SendPostEndEvent_;
	SOCKET hListenSock_;
	LONG updateThreadSendCounter_ = 0;
	void* pPlayerArr_;
	virtual BOOL RecvPost(GameSession* pSession);
	virtual BOOL SendPost(GameSession* pSession);
	virtual BOOL SendPostAccum(GameSession* pSession);
	void RecvProc(GameSession* pSession, int numberOfBytesTransferred);
	void SendProc(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SendProcAccum(GameSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SetEntirePlayerMemory(int MaxPlayerNum, int playerSize);
	virtual void ReleaseSession(GameSession* pSession);
	friend class Packet;
	friend class SerialContent;
	friend class ParallelContent;
	const int bAccSend = 0;
public:
	void ReleaseSessionContents(GameSession* pSession);
	ULONGLONG acceptCounter_ = 0;
	alignas(64) ULONGLONG acceptTotal_ = 0;
	alignas(64) ULONGLONG recvTPS_ = 0;

	// Send (Per MSG)
	alignas(64) LONG sendTPS_ = 0;

	// Disconnect
	alignas(64) ULONGLONG disconnectTPS_ = 0;
};

