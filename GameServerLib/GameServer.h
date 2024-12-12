#pragma once
#include "IHandler.h"
#include "CLockFreeStack.h"
#include "MyOVERLAPPED.h"
#include "Timer.h"


class Stack;
class SmartPacket;

class GameServer : public IHandler
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
	virtual void* OnAccept(ULONGLONG id) = 0;
	virtual void OnRelease(ULONGLONG id) = 0;
	virtual void OnRecv(ULONGLONG id, Packet* pPacket) = 0;
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;
	// µð¹ö±ë¿ë
	void Disconnect(ULONGLONG id);
	void SendPostPerFrame();
protected:
	void SendPostPerFrame_IMPL(LONG* pCounter);
private:
	void ProcessTimeOut();
	static unsigned __stdcall AcceptThread(LPVOID arg);
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
public:
	// Accept
	DWORD IOCP_WORKER_THREAD_NUM_ = 0;
	DWORD IOCP_ACTIVE_THREAD_NUM_ = 0;
	LONG lSessionNum_ = 0;
	LONG maxSession_ = 0;
	LONG lPlayerNum_ = 0;
	LONG maxPlayer_ = 0;
	LONG playerSize_ = 0;
	LONG TIME_OUT_MILLISECONDS_ = 0;
	ULONGLONG TIME_OUT_CHECK_INTERVAL_ = 0;
	ULONGLONG ullIdCounter = 0;
	Session* pSessionArr_;
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
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual BOOL SendPostAccum(Session* pSession);
	void RecvProc(Session* pSession, int numberOfBytesTransferred);
	void SendProc(Session* pSession, DWORD dwNumberOfBytesTransferred);
	void SendProcAccum(Session* pSession, DWORD dwNumberOfBytesTransferred);
	void SetEntirePlayerMemory(int MaxPlayerNum, int playerSize);
	virtual void ReleaseSession(Session* pSession);
	friend class Packet;
	int bAccSend = 0;
public:
	void ReleaseSessionContents(Session* pSession);
	ULONGLONG acceptCounter_ = 0;
	alignas(64) ULONGLONG acceptTotal_ = 0;
	alignas(64) ULONGLONG recvTPS_ = 0;

	// Send (Per MSG)
	alignas(64) LONG sendTPS_ = 0;

	// Disconnect
	alignas(64) ULONGLONG disconnectTPS_ = 0;
};

using CGameServer = const GameServer;
