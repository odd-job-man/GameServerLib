#pragma once
#include <tuple>
#include <type_traits>
#include <memory>
#include "CLockFreeQueue.h"

template<typename Func>
struct SignatureDecomp;

template<typename ReturnType,typename...Args>
struct SignatureDecomp<ReturnType(*)(Args...)>
{
	using ReturnType_ = ReturnType;
	using TupleType_ = std::tuple<Args...>;
	using fPtr = ReturnType(*)(Args...);
};

template<typename ObjType, typename ReturnType, typename...Args>
struct SignatureDecomp<ReturnType(ObjType::*)(Args...)>
{
	using ReturnType_ = ReturnType;
	using ObjType_ = ObjType;
	using TupleType_ = std::tuple<Args...>;
	using fPtr = ReturnType(ObjType::*)(Args...);
};

class Excutable
{
public:
	virtual void Excute() = 0;
};

class IJob : public Excutable
{
public:
	virtual void Free() = 0;
	HANDLE hcp_;
	__forceinline void SetCompletionPort(HANDLE hcp) { hcp_ = hcp; }
	__forceinline HANDLE GetCompletionPort() { return hcp_; }
};

template<typename FuncType>
class FuncJob : public IJob
{
private:
	using ReturnType = SignatureDecomp<FuncType>::ReturnType_;
	using Argument = SignatureDecomp<FuncType>::TupleType_;
	using DummyFuncType = SignatureDecomp<FuncType>::fPtr;

	FuncJob(DummyFuncType pFuncPtr, Argument&& args)
		:pFuncPtr_{ pFuncPtr }, argTuple_{std::forward<Argument>(args)}
	{
	}

	virtual void Free() override
	{
		jobPool_.Free(this);
	}

public:
	static FuncJob* Alloc(DummyFuncType pFuncPtr, Argument&& args)
	{
		return jobPool_.Alloc(pFuncPtr, std::forward<Argument>(args));
	}

	virtual void Execute() override
	{
		std::apply(pFuncPtr_, argTuple_);
	}

private:
	DummyFuncType pFuncPtr_;
	Argument argTuple_;
	friend class CTlsObjectPool<FuncJob, true>;
	static inline CTlsObjectPool<FuncJob, true> jobPool_;
};

template<typename T>
using ReturnType = SignatureDecomp<T>::ReturnType_;

template<typename T>
using Argument = SignatureDecomp<T>::TupleType_;

template<typename T>
using ObjType = SignatureDecomp<T>::ObjType_;

template<typename T>
using DummyFuncType = SignatureDecomp<T>::fPtr;


template<typename memberFuncType, bool bHasMemoryPool, bool bHasParam>
class MemberJob : public IJob
{
};

template<typename memberFuncType>
class MemberJob<typename memberFuncType, true, true> : public IJob
{
	using ReturnType = SignatureDecomp<memberFuncType>::ReturnType_;
	using Argument = SignatureDecomp<memberFuncType>::TupleType_;
	using ObjType = SignatureDecomp<memberFuncType>::ObjType_;
	using DummyFuncType = SignatureDecomp<memberFuncType>::fPtr;
public:
	MemberJob(ObjType* pObj, DummyFuncType pMemFuncPtr, Argument&& args)
		: pObj_{ pObj }, pMemFuncPtr_{ pMemFuncPtr }, argTuple_{ std::forward<Argument>(args) }
	{}

	static MemberJob* Alloc(ObjType* pObj, DummyFuncType pMemFuncPtr, Argument&& args) 
	{
		return jobPool_.Alloc(pObj, pMemFuncPtr, std::forward<Argument>(args));
	}

	virtual void Free()
	{
		jobPool_.Free(this);
	}

	template<size_t... Size>
	void ExcuteHelper(std::index_sequence<Size...>)
	{
		(pObj_->*pMemFuncPtr_)(std::get<Size>(argTuple_)...);
	}

	virtual void Execute() override
	{
		constexpr auto tupleSize = std::tuple_size_v<Argument>;
		ExcuteHelper(std::make_index_sequence<tupleSize>{});
	}

	ObjType* GetObjPtr() { return pObj_; }

private:
	ObjType* pObj_;
	memberFuncType pMemFuncPtr_;
	Argument argTuple_;
	static inline CTlsObjectPool<MemberJob, true> jobPool_;
};

template<typename memberFuncType> 
class MemberJob<typename memberFuncType, true, false> : public IJob
{
	using ReturnType = SignatureDecomp<memberFuncType>::ReturnType_;
	using ObjType = SignatureDecomp<memberFuncType>::ObjType_;
	using DummyFuncType = SignatureDecomp<memberFuncType>::fPtr;
public:
	MemberJob(ObjType* pObj, DummyFuncType pMemFuncPtr)
		: pObj_{ pObj }, pMemFuncPtr_{ pMemFuncPtr }
	{}

	static MemberJob* Alloc(ObjType* pObj, DummyFuncType pMemFuncPtr)
	{
		return jobPool_.Alloc(pObj, pMemFuncPtr);
	}

	virtual void Free()
	{
		jobPool_.Free(this);
	}

	virtual void Execute() override
	{
		(pObj_->*pMemFuncPtr_)();
	}

	ObjType* GetObjPtr() { return pObj_; }

	ObjType* pObj_;
	memberFuncType pMemFuncPtr_;
	static inline CTlsObjectPool<MemberJob, true> jobPool_;
};

template<typename memberFuncType>
class MemberJob<typename memberFuncType, false, true> : public IJob
{
	using ReturnType = SignatureDecomp<memberFuncType>::ReturnType_;
	using Argument = SignatureDecomp<memberFuncType>::TupleType_;
	using ObjType = SignatureDecomp<memberFuncType>::ObjType_;
	using DummyFuncType = SignatureDecomp<memberFuncType>::fPtr;

	MemberJob(ObjType* pObj, DummyFuncType pMemFuncPtr, Argument&& args)
		: pObj_{ pObj }, pMemFuncPtr_{ pMemFuncPtr }, argTuple_{ std::forward<Argument>(args) }
	{}

	virtual void Free() override {};
	
	template<size_t... Size>
	void ExcuteHelper(std::index_sequence<Size...>)
	{
		(pObj_->*pMemFuncPtr_)(std::get<Size>(argTuple_)...);
	}

	virtual void Execute() override
	{
		constexpr auto tupleSize = std::tuple_size_v<Argument>;
		ExcuteHelper(std::make_index_sequence<tupleSize>{});
	}

	ObjType* GetObjPtr() { return pObj_; }

private:
	ObjType* pObj_;
	memberFuncType pMemFuncPtr_;
	Argument argTuple_;
};

template<typename memberFuncType>
class MemberJob<typename memberFuncType, false, false> : public IJob
{
	using ReturnType = SignatureDecomp<memberFuncType>::ReturnType_;
	using ObjType = SignatureDecomp<memberFuncType>::ObjType_;
	using DummyFuncType = SignatureDecomp<memberFuncType>::fPtr;

public:
	MemberJob(ObjType* pObj, DummyFuncType pMemFuncPtr)
		: pObj_{ pObj }, pMemFuncPtr_{ pMemFuncPtr }
	{}

	virtual void Free() override 
	{}

	virtual void Execute() override
	{
		(pObj_->*pMemFuncPtr_)();
	}

	__forceinline ObjType* GetObjPtr() { return pObj_; }


private:
	ObjType* pObj_;
	memberFuncType pMemFuncPtr_;
};

class SmartJob
{
private:
	IJob* pJob_;
public:
	SmartJob(const SmartJob& job) = delete;
	SmartJob() = delete;
	SmartJob& operator=(const SmartJob& lhs) = delete;
	SmartJob& operator= (SmartJob&& lhs) = delete;

	SmartJob (IJob*&& pJob)
	{
		pJob_ = pJob;
	}

	~SmartJob()
	{
		pJob_->Free();
		pJob_ = nullptr;
	}

	IJob* operator->()
	{
		return pJob_;
	}

};

