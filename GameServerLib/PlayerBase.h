#pragma once
#include <windows.h>
#include "CLinkedList.h"

struct PlayerBase
{
	LINKED_NODE node_{ offsetof(PlayerBase,node) };
	ULONGLONG sessionID_;
	PlayerBase(ULONGLONG sessionID)
		:sessionID_{sessionID}
	{}
};
