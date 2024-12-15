#pragma once
#include <windows.h>
#include <stdio.h>
#include "MyJob.h"
#include "CLockFreeQueue.h"

struct ScheduleRsc
{
	DWORD firstTimeCheck_;
	DWORD oldFrameTick_;
	DWORD timeStamp_;
	__forceinline void Init(DWORD timeToInit)
	{
		firstTimeCheck_ = timeToInit;
	}

	DWORD UpdateAndGetTimeToSleep(DWORD timeStamp, DWORD tickPerFrame)
	{
		timeStamp_ = timeStamp;
		oldFrameTick_ = timeStamp_ - ((timeStamp_ - firstTimeCheck_) % tickPerFrame);
		return tickPerFrame - (timeStamp_ - oldFrameTick_);
	}
};

class UpdateBase : Excutable
{
public:
	UpdateBase(DWORD tickPerFrame, HANDLE hCompletionPort, LONG pqcsLimit);
	void firstTimeInit();
	virtual void Excute() override { Update(); }
	virtual void OnMonitor() = 0;
	void Update();
	virtual void Update_IMPL() = 0;
	LONG singleThreadGate_ = 0;
	DWORD timeStamp_ = 0;
	DWORD oldFrameTick_ = 0;
	DWORD firstTimeCheck_ = 0;
	const DWORD TICK_PER_FRAME_;
	LONG fps_ = 0;
	const LONG pqcsLimit_ = 0;
	ScheduleRsc scdr;

	LONG pqcsUpdateCnt_;
	bool bFirst_ = false;
	const HANDLE hcp_;
};


class MonitoringUpdate : public UpdateBase
{
public:
	static constexpr int len = 14;
	UpdateBase* pArr_[len];
	int curNum_ = 0;
	MonitoringUpdate(HANDLE hCompletionPort, DWORD tickPerFrame, LONG pqcsLimit);
	void RegisterMonitor(UpdateBase* pTargetToMonitor);
	virtual void Update_IMPL() override;
	__forceinline virtual void OnMonitor() override {};
};

struct UpdatePQCSInfo
{
	HANDLE hTerminateEvent_;
	static constexpr int len = 15;
	UpdateBase* pUpdateArr_[len];
	int currentNum_ = 0;
#pragma warning(disable : 26495)
	UpdatePQCSInfo()
	{
		hTerminateEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (hTerminateEvent_ == NULL)
		{
			DWORD dwErrCode = GetLastError();
			__debugbreak();
		}
	}
#pragma warning(default: 26495)

	void firstTimeInit(DWORD firstTime)
	{
		for (int i = 0; i < currentNum_; ++i)
			pUpdateArr_[i]->scdr.Init(firstTime);
	}
};

