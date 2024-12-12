#pragma once
#include <windows.h>
#include <pdh.h>

struct HMonitor 
{
	HMonitor();
	~HMonitor();
	void UpdateCpuTime(ULONGLONG* pOutTickDiffPerSec_NULLABLE, ULONGLONG* pOutTotalTick_NULLABLE);
	HANDLE _hProcess;
	int _iNumberOfProcessors;

	double GetPPB();
	double GetPNPB();
	double GetAB();
	double GetNPB();
	double GetRetranse();
	double GetTCPTimeOuts();

public:
	float _fProcessorTotal;
	float _fProcessorUser;
	float _fProcessorKernel;
	float _fProcessTotal;
	float _fProcessUser;
	float _fProcessKernel;
private:
	ULARGE_INTEGER _ftProcessor_LastKernel;
	ULARGE_INTEGER _ftProcessor_LastUser;
	ULARGE_INTEGER _ftProcessor_LastIdle;
	ULARGE_INTEGER _ftProcess_LastKernel;
	ULARGE_INTEGER _ftProcess_LastUser;
	ULARGE_INTEGER _ftProcess_LastTime;

	PDH_HCOUNTER PPBCounter;
	PDH_HCOUNTER PNPBCounter;
	PDH_HCOUNTER MABCounter;
	PDH_HCOUNTER NPBCounter;
	PDH_HCOUNTER RETRANSECounter;
	PDH_HCOUNTER TCPTimeOutCounter;

	PDH_HQUERY PPBQuery;
	PDH_HQUERY PNPBQuery;
	PDH_HQUERY MABQuery;
	PDH_HQUERY NPBQuery;
	PDH_HQUERY RETRANSEQuery;
	PDH_HQUERY TCPTimeOutQuery;
};
