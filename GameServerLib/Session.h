#pragma once
#include "Packet.h"
#include "RingBuffer.h"
#include "MYOVERLAPPED.h"
#include "CLinkedList.h"

class Packet;
class ContentsBase;

struct Session
{
	static constexpr LONG RELEASE_FLAG = 0x80000000;
	LINKED_NODE node{ offsetof(Session,node) };
	CLockFreeQueue<Packet*> recvMsgQ_;
	SOCKET sock_;
	ULONGLONG id_;
	ULONGLONG lastRecvTime;
	LONG lSendBufNum_;
	BOOL bDisconnectCalled_;
	ContentsBase* pContent;
	MYOVERLAPPED recvOverlapped;
	MYOVERLAPPED sendOverlapped;
	LONG IoCnt_;
	CLockFreeQueue<Packet*> sendPacketQ_;
	BOOL bSendingInProgress_;
	BOOL bSendingAtWorker_;
	Packet* pSendPacketArr_[50];
	RingBuffer recvRB_;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx);

	Session()
		:IoCnt_{ Session::RELEASE_FLAG | 0 }
	{}

	inline static short GET_SESSION_INDEX(ULONGLONG id)
	{
		return id & 0xFFFF;
	}

};

using CSession = const Session;