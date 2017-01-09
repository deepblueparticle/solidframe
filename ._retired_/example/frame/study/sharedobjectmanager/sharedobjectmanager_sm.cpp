#include "sharedobjectmanager.hpp"
#include "system/atomic.hpp"
#include "system/mutex.hpp"
#include "system/condition.hpp"
#include "system/mutualstore.hpp"
#include "utility/workpool.hpp"
#include <deque>
#include <vector>
//sharedobject manager - using sharedmutex

using namespace solid;
using namespace std;

typedef ATOMIC_NS::atomic<size_t>           AtomicSizeT;
typedef ATOMIC_NS::atomic<uint32_t>         AtomicUint32_tT;
typedef std::vector<size_t>                 SizeVectorT;

struct ObjectStub{
    ObjectStub(
    ):  idx(InvalidIndex()), thridx(static_cast<size_t>(InvalidIndex())), value(0), flags(0), flag1cnt(0),
        flag2cnt(0),flag3cnt(0), flag4cnt(0), minvecsz(InvalidSize()), maxvecsz(0), eventcnt(0), raisecnt(0){}
    ObjectStub(const ObjectStub &_ros){
        *this = _ros;
    }
    ObjectStub& operator=(const ObjectStub &_ros){
        idx = _ros.idx;
        thridx = _ros.thridx.load();
        value = _ros.value;
        flags = _ros.flags.load();
        valvec = _ros.valvec;
        flag1cnt = _ros.flag1cnt;
        flag2cnt = _ros.flag2cnt;
        flag3cnt = _ros.flag3cnt;
        flag4cnt = _ros.flag4cnt;
        minvecsz = _ros.minvecsz;
        maxvecsz = _ros.maxvecsz;
        eventcnt = _ros.eventcnt;
        raisecnt = _ros.raisecnt;
        return *this;
    }
    size_t          idx;
    AtomicSizeT     thridx;
    uint64_t            value;
    AtomicSizeT     flags;
    SizeVectorT     valvec;
    size_t          flag1cnt;
    size_t          flag2cnt;
    size_t          flag3cnt;
    size_t          flag4cnt;
    size_t          minvecsz;
    size_t          maxvecsz;
    size_t          eventcnt;
    size_t          raisecnt;
};

typedef std::pair<size_t, ObjectStub*>          JobT;
typedef std::deque<ObjectStub>                  ObjectVectorT;
typedef std::deque<ObjectStub*>                 ObjectPointerVectorT;
typedef MutualStore<Mutex>                      MutexMutualStoreT;

struct Worker: WorkerBase{
    ObjectPointerVectorT    objvec;
};

typedef WorkPool<JobT, WorkPoolController, Worker>      WorkPoolT;

struct WorkPoolController: WorkPoolControllerBase{
    typedef std::vector<JobT>       JobVectorT;

    SharedObjectManager &rsom;

    WorkPoolController(SharedObjectManager &_rsom):rsom(_rsom){}

    bool createWorker(WorkPoolT &_rwp, ushort _wkrcnt){
        _rwp.createMultiWorker(32)->start();
        return true;
    }

    void execute(WorkPoolBase &_rwp, Worker &_rwkr, JobVectorT &_rjobvec){
        for(JobVectorT::const_iterator it(_rjobvec.begin()); it != _rjobvec.end(); ++it){
            size_t  idx;
            if(it->second){
                idx = it->second->thridx = _rwkr.objvec.size();
                _rwkr.objvec.push_back(it->second);
            }else{
                idx = it->first;
            }
            rsom.executeObject(*_rwkr.objvec[idx]);
        }
    }
};


struct SharedObjectManager::Data: WorkPoolControllerBase{
    Data(SharedObjectManager &_rsom):wp(_rsom){}

    Mutex& mutex(const ObjectStub& _robj){
        return mtxstore.at(_robj.idx);
    }

    WorkPoolT           wp;
    ObjectVectorT       objvec;
    SharedMutex         shrmtx;
    MutexMutualStoreT   mtxstore;
};

//=========================================================================

SharedObjectManager::SharedObjectManager():d(*(new Data(*this))){

}
SharedObjectManager::~SharedObjectManager(){
    delete &d;
}

bool SharedObjectManager::start(){
    d.wp.start(1);
    return true;
}

void SharedObjectManager::insert(size_t _v){
    Locker<SharedMutex> lock(d.shrmtx);
    size_t sz = d.objvec.size();
    d.mtxstore.safeAt(sz);
    Locker<Mutex>       lock2(d.mtxstore.at(sz));
    d.objvec.resize(d.objvec.size() + 1);
    ObjectStub          &robj = d.objvec.back();
    robj.value = _v;
    robj.flags = RaiseFlag;
    robj.idx = sz;
    d.wp.push(JobT(-1, &robj));
}

bool SharedObjectManager::notify(size_t _idx, uint32_t _flags){
    _flags |= (RaiseFlag);
    SharedLocker<SharedMutex>   lock(d.shrmtx);
    _idx %= d.objvec.size();
    Locker<Mutex>   lock2(d.mtxstore.at(_idx));
    ObjectStub      &robj = d.objvec[_idx];
    const size_t    flags = robj.flags.fetch_or(_flags);
    const size_t    thridx = robj.thridx;
    if(!(flags & RaiseFlag) && thridx != InvalidIndex()){
        d.wp.push(JobT(thridx, NULL));
    }
    return true;
}
bool SharedObjectManager::notify(size_t _idx, uint32_t _flags, size_t _v){
    _flags |= (EventFlag | RaiseFlag);
    SharedLocker<SharedMutex>   lock(d.shrmtx);
    _idx %= d.objvec.size();
    Locker<Mutex>   lock2(d.mtxstore.at(_idx));
    ObjectStub      &robj = d.objvec[_idx];
    const size_t    flags = robj.flags.fetch_or(_flags);
    robj.valvec.push_back(_v);
    const size_t    thridx = robj.thridx;
    if(!(flags & RaiseFlag) && thridx != InvalidIndex()){
        d.wp.push(JobT(thridx, NULL));
    }
    return true;
}

bool SharedObjectManager::notifyAll(uint32_t _flags){
    _flags |= (RaiseFlag);
    SharedLocker<SharedMutex>   lock(d.shrmtx);
    const size_t    objcnt = d.objvec.size();
    for(size_t i = 0; i < objcnt; ++i){
        ObjectStub      &robj = d.objvec[i];
        Locker<Mutex>   lock2(d.mtxstore.at(robj.idx));
        const size_t    flags = robj.flags.fetch_or(_flags);
        const size_t    thridx = robj.thridx;
        if(!(flags & RaiseFlag) && thridx != InvalidIndex()){
            d.wp.push(JobT(thridx, NULL));
        }
    }
    return true;
}
bool SharedObjectManager::notifyAll(uint32_t _flags, size_t _v){
    _flags |= (EventFlag | RaiseFlag);
    SharedLocker<SharedMutex>   lock(d.shrmtx);
    const size_t    objcnt = d.objvec.size();
    for(size_t i = 0; i < objcnt; ++i){
        ObjectStub      &robj = d.objvec[i];
        Locker<Mutex>   lock2(d.mtxstore.at(robj.idx));
        const size_t    flags = robj.flags.fetch_or(_flags);
        robj.valvec.push_back(_v);
        const size_t    thridx = robj.thridx;
        if(!(flags & RaiseFlag) && thridx != InvalidIndex()){
            d.wp.push(JobT(thridx, NULL));
        }
    }
    return true;
}

void SharedObjectManager::executeObject(ObjectStub &_robj){
    size_t flags = _robj.flags.fetch_and(0);
    if(flags & EventFlag){
        Locker<Mutex>   lock(d.mutex(_robj));
        flags |= _robj.flags.fetch_and(0);  //this is to prevent from the following SOLID_ASSERT -
                                            //between the first fetch_and and the lock, other thread can
                                            //add new values in valvec.
        SOLID_ASSERT(_robj.valvec.size());
        for(SizeVectorT::const_iterator it(_robj.valvec.begin()); it != _robj.valvec.end(); ++it){
            _robj.value += *it;
        }
        _robj.eventcnt += _robj.valvec.size();
        if(_robj.valvec.size() < _robj.minvecsz){
            _robj.minvecsz = _robj.valvec.size();
        }
        if(_robj.valvec.size() > _robj.maxvecsz){
            _robj.maxvecsz = _robj.valvec.size();
        }
        _robj.valvec.clear();
    }
    ++_robj.raisecnt;
    if(flags & Flag1){
        ++_robj.flag1cnt;
    }
    if(flags & Flag2){
        ++_robj.flag2cnt;
    }
    if(flags & Flag3){
        ++_robj.flag3cnt;
    }
    if(flags & Flag4){
        ++_robj.flag4cnt;
    }
}

void SharedObjectManager::stop(std::ostream &_ros){
    d.wp.stop(true);
    size_t      minvecsz{InvalidSize()};
    size_t      maxvecsz(0);
    size_t      mineventcnt{InvalidSize()};
    size_t      maxeventcnt(0);
    size_t      minraisecnt{InvalidSize()};
    size_t      maxraisecnt(0);
    uint64_t        eventcnt(0);
    uint64_t        raisecnt(0);
    for(ObjectVectorT::const_iterator it(d.objvec.begin()); it != d.objvec.end(); ++it){
        eventcnt += it->eventcnt;
        raisecnt += it->raisecnt;
        if(it->eventcnt < mineventcnt){
            mineventcnt = it->eventcnt;
        }
        if(it->eventcnt > maxeventcnt){
            maxeventcnt = it->eventcnt;
        }
        if(it->raisecnt < minraisecnt){
            minraisecnt = it->raisecnt;
        }
        if(it->raisecnt > maxraisecnt){
            maxraisecnt = it->raisecnt;
        }
        if(it->minvecsz < minvecsz){
            minvecsz = it->minvecsz;
        }
        if(it->maxvecsz > maxvecsz){
            maxvecsz = it->maxvecsz;
        }
    }
    _ros<<"objvec.size       =  "<<d.objvec.size()<<endl;
    _ros<<"eventcnt          =  "<<eventcnt<<endl;
    _ros<<"raisecnt          =  "<<raisecnt<<endl;
    _ros<<"minvecsz          =  "<<minvecsz<<endl;
    _ros<<"maxvecsz          =  "<<maxvecsz<<endl;
    _ros<<"mineventcnt       =  "<<mineventcnt<<endl;
    _ros<<"maxeventcnt       =  "<<maxeventcnt<<endl;
    _ros<<"minraisecnt       =  "<<minraisecnt<<endl;
    _ros<<"maxraisecnt       =  "<<maxraisecnt<<endl;
#if 0
    for(ObjectVectorT::const_iterator it(d.objvec.begin()); it != d.objvec.end(); ++it){
        if(it->eventcnt == mineventcnt){
            _ros<<"mineventcnt found for "<<it->idx<<endl;
        }
    }
#endif
}
