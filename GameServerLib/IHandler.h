#pragma once 

struct Session;
class Packet;
class SmartPacket;
class IHandler
{
public:
	virtual void SendPacket(ULONGLONG id, SmartPacket& sendPacket) = 0;
	virtual BOOL OnConnectionRequest() = 0;
	virtual void* OnAccept(ULONGLONG id) = 0;
	virtual void OnRecv(ULONGLONG id, Packet* pPacket) = 0;
	virtual void OnRelease(ULONGLONG id) = 0;
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
private:
	virtual BOOL SendPost(Session* pSession) = 0;
	virtual BOOL RecvPost(Session* pSession) = 0;
	virtual void ReleaseSession(Session* pSession) = 0;
};
