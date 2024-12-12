#pragma once
#define TO_STRING(x) #x
#define TLS_ASSERT(idx) do{\
if(idx == TLS_OUT_OF_INDEXES)\
{\
DWORD errCode = GetLastError();\
__debugbreak();}\
}while(0)\

#define ASSERT_MYSQL_CONNECTION(connectRet) do{\
if(connectRet == NULL){\
fprintf(stderr, "Mysql connection error : %s", mysql_error(connectRet));\
__debugbreak();\
}\
}while(0)\

#define ASSERT_MYSQL_MULTI_STATEMENT_ON(ret) do{\
if(ret != 0){\
fprintf(stderr, "Mysql Multiple Statement Failed : %s", mysql_error(pBI->connection));\
__debugbreak();\
}\
}while(0)\

#define ASSERT_QUERY(queryRet) do{\
if(queryRet != 0){\
	printf("Mysql query error : %s", mysql_error(pBI->connection));\
	__debugbreak();\
	}\
}while(0)\

#define ASSERT_NOT_ZERO(queryRet) do{\
if(queryRet != 0){\
	__debugbreak();\
	}\
}while(0)\

#define ASSERT_ZERO_LOG(ret,str) do{\
if(ret == 0){\
	LOG(L"ERROR",DEBUG,TEXTFILE,L"%s errCode:%d",TO_STRING(str), GetLastError());\
	__debugbreak();}\
}while(0)\



#define ASSERT_NULL_LOG(ret,str) do{\
if(ret == 0){\
	LOG(L"ERROR",DEBUG,TEXTFILE,L"%s errCode:%d",TO_STRING(str), GetLastError());\
	__debugbreak();}\
}while(0)\

