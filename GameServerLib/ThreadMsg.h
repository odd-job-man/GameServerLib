#pragma once
enum class en_MsgType
{
	ENTER,
	RELEASE
};

struct InterContentsMessage
{
	const en_MsgType msgType_;
	const Session* pSession_;

	InterContentsMessage(const en_MsgType msgType, const Session* pSession)
		:msgType_{ msgType }, pSession_{ pSession }
	{}
};
