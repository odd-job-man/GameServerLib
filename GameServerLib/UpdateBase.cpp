#include <WinSock2.h>
#include <windows.h>
#include "Timer.h"
#include "UpdateBase.h"
#include "MYOVERLAPPED.h"

MYOVERLAPPED updatePostOverlapped = { WSAOVERLAPPED{},OVERLAPPED_REASON::POST };

UpdateBase::UpdateBase(DWORD tickPerFrame, HANDLE hCompletionPort, LONG pqcsLimit)
	:TICK_PER_FRAME_{ tickPerFrame }, singleThreadGate_{ 0 }, fps_{ 0 }, pqcsUpdateCnt_{ 0 },
	hcp_{ hCompletionPort }, pqcsLimit_{ pqcsLimit } 
{
}

void UpdateBase::firstTimeInit()
{
	firstTimeCheck_ = timeGetTime();
	oldFrameTick_ = firstTimeCheck_;
	timeStamp_ = firstTimeCheck_;
}

void UpdateBase::Update()
{
	if (InterlockedIncrement(&singleThreadGate_) - 1 > 0)
	{
		InterlockedDecrement(&pqcsUpdateCnt_);
		return;
	}

	if (bFirst_ == false)
	{
		firstTimeInit();
		bFirst_ = true;
	}

	while (1)
	{
		Update_IMPL();
		InterlockedIncrement(&fps_);
		timeStamp_ = timeGetTime();
		if (timeStamp_ > oldFrameTick_ + TICK_PER_FRAME_)
		{
			oldFrameTick_ = timeStamp_ - ((timeStamp_ - firstTimeCheck_) % TICK_PER_FRAME_);
			InterlockedExchange(&singleThreadGate_, 1);
			continue;
		}

		oldFrameTick_ += TICK_PER_FRAME_;
		if (InterlockedDecrement(&singleThreadGate_) > 0)
		{
			InterlockedExchange(&singleThreadGate_, 1);
			continue;
		}

		InterlockedDecrement(&pqcsUpdateCnt_);
		break;
	}
}



MonitoringUpdate::MonitoringUpdate(HANDLE hCompletionPort, DWORD tickPerFrame, LONG pqcsLimit)
	:UpdateBase{ tickPerFrame,hCompletionPort,pqcsLimit }
{}

void MonitoringUpdate::RegisterMonitor(UpdateBase * pTargetToMonitor)
{
	if (curNum_ >= len)	__debugbreak();
	pArr_[curNum_++] = pTargetToMonitor;
}

void MonitoringUpdate::Update_IMPL()
{
	printf("--------------------------\n");
	for (int i = 0; i < curNum_; ++i)
	{
		pArr_[i]->OnMonitor();
	}
	printf("--------------------------\n\n");
}


