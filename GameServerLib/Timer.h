#pragma once
#include "MyJob.h"
#include "UpdateBase.h"

namespace Timer
{
	void Reigster_UPDATE(UpdateBase* pUpdate);
	void Init();
	void Release();
	void Start();
}
