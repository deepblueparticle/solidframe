#ifndef FOUNDATION_REQUESTUID_HPP
#define FOUNDATION_REQUESTUID_HPP

#include "foundation/common.hpp"

namespace foundation{

//! Unique identifier for a request
struct RequestUid{
	RequestUid(
		IndexT  _objidx = 0,
		uint32 _objuid = 0,
		uint32 _reqidx = 0,
		uint32 _requid = 0
	):objidx(_objidx), objuid(_objuid), reqidx(_reqidx), requid(_requid){
	}
	RequestUid(
		const ObjectUidT &_ruid,
		uint32 _reqidx = 0,
		uint32 _requid = 0
	):objidx(_ruid.first), objuid(_ruid.second), reqidx(_reqidx), requid(_requid){
	}
	void set(
		IndexT  _objidx = 0,
		uint32 _objuid = 0,
		uint32 _reqidx = 0,
		uint32 _requid = 0
	){
		objidx = _objidx;
		objuid = _objuid;
		reqidx = _reqidx;
		requid = _requid;
	}
	void set(
		const ObjectUidT &_ruid,
		uint32 _reqidx = 0,
		uint32 _requid = 0
	){
		objidx = _ruid.first;
		objuid = _ruid.second;
		reqidx = _reqidx;
		requid = _requid;
	}
	IndexT	objidx;
	uint32	objuid;
	uint32	reqidx;
	uint32	requid;
};


struct SpecificRequestUid{
	class ForcedCreate;
	SpecificRequestUid(const ForcedCreate&);
	SpecificRequestUid(){}
	void prepareThread();
	void unprepareThread();
	RequestUid* operator->()const;
	RequestUid* get() const;
	RequestUid& operator*()const;
private:
	SpecificRequestUid(const SpecificRequestUid&);
	SpecificRequestUid& operator=(const SpecificRequestUid&);
};

extern SpecificRequestUid requestuidptr;

}


#endif
