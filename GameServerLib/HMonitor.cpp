#include "HMonitor.h"
#include <string>
#pragma comment(lib,"pdh.lib")

#define ASSERT_NOT_ERROR_SUCCESS(ret) do{\
if(ret != ERROR_SUCCESS) __debugbreak();\
}while(0)\

HMonitor::HMonitor()
{
	_hProcess = GetCurrentProcess();
	//------------------------------------------------------------------
	// 프로세서 개수를 확인한다.
	//
	// 프로세스 (exe) 실행률 계산시 cpu 개수로 나누기를 하여 실제 사용률을 구함.
	//------------------------------------------------------------------
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	_iNumberOfProcessors = SystemInfo.dwNumberOfProcessors;

	_fProcessorTotal = 0;
	_fProcessorUser = 0;
	_fProcessorKernel = 0;
	_fProcessTotal = 0;
	_fProcessUser = 0;
	_fProcessKernel = 0;
	_ftProcessor_LastKernel.QuadPart = 0;
	_ftProcessor_LastUser.QuadPart = 0;
	_ftProcessor_LastIdle.QuadPart = 0;
	_ftProcess_LastUser.QuadPart = 0;
	_ftProcess_LastKernel.QuadPart = 0;
	_ftProcess_LastTime.QuadPart = 0;

	WCHAR programPath[MAX_PATH];
	GetModuleFileName(NULL, programPath, MAX_PATH);

	std::wstring path(programPath);
	size_t pos = path.find_last_of(L"\\");
	std::wstring processName = path.substr(pos + 1);
	//.exe 제거하기
	size_t dotPos = processName.find_last_of(L".");
	processName.erase(dotPos, 4);

	//쿼리 만들기
	WCHAR PPBQueryStr[MAX_PATH];
	WCHAR PNPBQueryStr[MAX_PATH];

	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &PPBQuery));
	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &PNPBQuery));
	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &MABQuery));
	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &NPBQuery));
	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &RETRANSEQuery));
	ASSERT_NOT_ERROR_SUCCESS(PdhOpenQuery(NULL, NULL, &TCPTimeOutQuery));

	wsprintf(PPBQueryStr, L"\\Process(%s)\\Private Bytes", processName.c_str());
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(PPBQuery, PPBQueryStr, NULL, &PPBCounter));

	wsprintf(PNPBQueryStr,L"\\Process(%s)\\Pool Nonpaged Bytes" , processName.c_str());
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(PNPBQuery, PNPBQueryStr, NULL, &PNPBCounter));
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(MABQuery, L"\\Memory\\Available MBytes", NULL, &MABCounter));
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(NPBQuery, L"\\Memory\\Pool Nonpaged Bytes", NULL, &NPBCounter));
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(RETRANSEQuery, L"\\TCPv4\\Segments Retransmitted/sec", NULL, &RETRANSECounter));
	ASSERT_NOT_ERROR_SUCCESS(PdhAddCounter(TCPTimeOutQuery, L"\\TCPIP Performance Diagnostics\\TCP timeouts", NULL, &TCPTimeOutCounter));
	UpdateCpuTime(nullptr, nullptr);
}

HMonitor::~HMonitor()
{
	PdhCloseQuery(PPBQuery);
	PdhCloseQuery(PNPBQuery);
	PdhCloseQuery(MABQuery);
	PdhCloseQuery(NPBQuery);
}

void HMonitor::UpdateCpuTime(ULONGLONG* pOutTickDiffPerSecElapsed_NULLABLE, ULONGLONG* pOutTotalTickElapsed_NULLABLE)
{
	ULARGE_INTEGER Idle;
	ULARGE_INTEGER Kernel;
	ULARGE_INTEGER User;

	//---------------------------------------------------------
	// 시스템 사용 시간을 구한다.
	//
	// 아이들 타임 / 커널 사용 타임 (아이들포함) / 유저 사용 타임
	//---------------------------------------------------------
	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
	{
		return;
	}

	// 커널 타임에는 아이들 타임이 포함됨.
	ULONGLONG KernelDiff = Kernel.QuadPart - _ftProcessor_LastKernel.QuadPart;
	ULONGLONG UserDiff = User.QuadPart - _ftProcessor_LastUser.QuadPart;
	ULONGLONG IdleDiff = Idle.QuadPart - _ftProcessor_LastIdle.QuadPart;
	ULONGLONG Total = KernelDiff + UserDiff;
	ULONGLONG TimeDiff;


	if (Total == 0)
	{
		_fProcessorUser = 0.0f;
		_fProcessorKernel = 0.0f;
		_fProcessorTotal = 0.0f;
	}
	else
	{
		// 커널 타임에 아이들 타임이 있으므로 빼서 계산.
		_fProcessorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		_fProcessorUser = (float)((double)UserDiff / Total * 100.0f);
		_fProcessorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);
	}
	_ftProcessor_LastKernel = Kernel;
	_ftProcessor_LastUser = User;
	_ftProcessor_LastIdle = Idle;

	//---------------------------------------------------------
	// 지정된 프로세스 사용률을 갱신한다.
	//---------------------------------------------------------
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;
	//---------------------------------------------------------
	// 현재의 100 나노세컨드 단위 시간을 구한다. UTC 시간.
	//
	// 프로세스 사용률 판단의 공식
	//
	// a = 샘플간격의 시스템 시간을 구함. (그냥 실제로 지나간 시간)
	// b = 프로세스의 CPU 사용 시간을 구함.
	//
	// a : 100 = b : 사용률 공식으로 사용률을 구함.
	//---------------------------------------------------------
	//---------------------------------------------------------
	// 얼마의 시간이 지났는지 100 나노세컨드 시간을 구함,
	//---------------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime);
	//---------------------------------------------------------
	// 해당 프로세스가 사용한 시간을 구함.
	//
	// 두번째, 세번째는 실행,종료 시간으로 미사용.
	//---------------------------------------------------------
	GetProcessTimes(_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);
	//---------------------------------------------------------
	// 이전에 저장된 프로세스 시간과의 차를 구해서 실제로 얼마의 시간이 지났는지 확인.
	//
	// 그리고 실제 지나온 시간으로 나누면 사용률이 나옴.
	//---------------------------------------------------------
	TimeDiff = NowTime.QuadPart - _ftProcess_LastTime.QuadPart;
	UserDiff = User.QuadPart - _ftProcess_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - _ftProcess_LastKernel.QuadPart;
	Total = KernelDiff + UserDiff;

	if (pOutTickDiffPerSecElapsed_NULLABLE != nullptr && pOutTotalTickElapsed_NULLABLE != nullptr)
	{
		*pOutTickDiffPerSecElapsed_NULLABLE += Total;
		*pOutTotalTickElapsed_NULLABLE += TimeDiff;
	}

	_fProcessTotal = (float)(Total / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessKernel = (float)(KernelDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_fProcessUser = (float)(UserDiff / (double)_iNumberOfProcessors / (double)TimeDiff * 100.0f);
	_ftProcess_LastTime = NowTime;
	_ftProcess_LastKernel = Kernel;
	_ftProcess_LastUser = User;
}

double HMonitor::GetPPB()
{
	PdhCollectQueryData(PPBQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(PPBCounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

double HMonitor::GetPNPB()
{
	PdhCollectQueryData(PNPBQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(PNPBCounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

double HMonitor::GetAB()
{
	PdhCollectQueryData(MABQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(MABCounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

double HMonitor::GetNPB()
{
	PdhCollectQueryData(NPBQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(NPBCounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

double HMonitor::GetRetranse()
{
	PdhCollectQueryData(RETRANSEQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(RETRANSECounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

double HMonitor::GetTCPTimeOuts()
{
	PdhCollectQueryData(TCPTimeOutQuery);
	PDH_FMT_COUNTERVALUE ret;
	PdhGetFormattedCounterValue(TCPTimeOutCounter, PDH_FMT_DOUBLE, NULL, &ret);
	return ret.doubleValue;
}

