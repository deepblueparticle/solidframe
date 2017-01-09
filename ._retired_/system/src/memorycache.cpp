// system/src/memorycache.cpp
//
// Copyright (c) 2014 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#include "system/memorycache.hpp"
#include "system/memory.hpp"
#include "system/cassert.hpp"
//#undef SOLID_HAS_DEBUG
#include "system/debug.hpp"
#include <vector>
#include <boost/type_traits.hpp>
#include <memory>


#ifdef SOLID_HAS_DEBUG
namespace{
    size_t dbgid = solid::Debug::the().registerModule("memory_cache");
}
#endif



namespace solid{

struct Node{
    Node *pnext;
};


inline void *align( std::size_t alignment, std::size_t size,
                    void *&ptr, std::size_t &space ) {
    std::uintptr_t pn = reinterpret_cast< std::uintptr_t >( ptr );
    std::uintptr_t aligned = ( pn + alignment - 1 ) & - alignment;
    std::size_t padding = aligned - pn;
    if ( space < size + padding ) return nullptr;
    space -= padding;
    return ptr = reinterpret_cast< void * >( aligned );
}

struct Page{
    static size_t   dataCapacity(Configuration const &_rcfg){
        const size_t    headsz = sizeof(Page);
        size_t          headpadd = (_rcfg.alignsz - (headsz % _rcfg.alignsz)) %  _rcfg.alignsz;
        return _rcfg.pagecp - headsz - headpadd;
    }
    static Page* computePage(void *_pv, Configuration const &_rcfg){
        std::uintptr_t pn = reinterpret_cast< std::uintptr_t >( _pv );
        pn -= (pn % _rcfg.pagecp);
        return reinterpret_cast<Page*>(pn);
    }

    bool empty()const{
        return usecount == 0;
    }

    bool full()const{
        return ptop == NULL;
    }

    void* pop(const size_t _cp, Configuration const &_rcfg){
        void *pv = ptop;
        ptop = ptop->pnext;
        ++usecount;
        return pv;
    }

    void push(void *_pv, const size_t _cp, Configuration const &_rcfg){
        --usecount;
        Node *pnode = static_cast<Node*>(_pv);
        pnode->pnext = ptop;
        ptop = pnode;
    }

    size_t init(const size_t _cp, Configuration const &_rcfg){
        pprev = pnext = NULL;
        ptop = NULL;
        usecount = 0;

        char    *pc = reinterpret_cast<char *>(this);
        void    *pv = pc + sizeof(*this);
        size_t  sz = _rcfg.pagecp - sizeof(*this);
        Node    *pn = NULL;
        size_t  cnt = 0;
        while(align(_rcfg.alignsz, _cp, pv, sz)){
            pn = reinterpret_cast<Node*>(pv);
            pn->pnext = ptop;
            ptop = pn;
            uint8_t *pu = static_cast<uint8_t*>(pv);
            pv = pu + _cp;
            sz -= _cp;
            ++cnt;
        }
        return cnt;
    }

    void print()const{
        idbgx(dbgid, "Page: "<<(void*)this<<" pnext = "<<(void*)pnext<<" pprev = "<<(void*)pprev<<" ptop = "<<ptop<<" usecount = "<<usecount);
    }

    Page    *pprev;
    Page    *pnext;

    Node    *ptop;

    uint16_t    usecount;
};


CacheStub::CacheStub(
    Configuration const &_rcfg
):  pfrontpage(NULL), pbackpage(NULL), emptypagecnt(0), pagecnt(0), keeppagecnt(_rcfg.emptypagecnt)
#ifdef SOLID_HAS_DEBUG
    , itemcnt(0)
#endif
    {}

CacheStub::~CacheStub(){
    clear();
}

void CacheStub::clear(){
    Page *pit = pfrontpage;
    while(pit){
        Page *ptmp = pit;
        pit = pit->pprev;

        memory_free_aligned(ptmp);
    }
    pfrontpage = pbackpage = NULL;
}

inline size_t CacheStub::allocate(const size_t _cp, Configuration const &_rcfg){
    void *pv = memory_allocate_aligned(_rcfg.pagecp, _rcfg.pagecp);
    size_t cnt = 0;
    if(pv){
        Page *ppage = reinterpret_cast<Page*>(pv);
        cnt = ppage->init(_cp, _rcfg);

        ppage->pprev = pfrontpage;

        if(pfrontpage){
            pfrontpage->pnext = ppage;
            pfrontpage = ppage;
        }else{
            pbackpage = pfrontpage = ppage;
        }
        ++pagecnt;
        ++emptypagecnt;
#ifdef SOLID_HAS_DEBUG
        itemcnt += cnt;
        vdbgx(dbgid, "itemcnt = "<<itemcnt);
#endif
    }
    return cnt;
}

void* CacheStub::pop(const size_t _cp, Configuration const &_rcfg){
    if(!pfrontpage || pfrontpage->full()){
        vdbgx(dbgid, "must allocate");
        size_t cnt = allocate(_cp, _rcfg);
        if(!cnt){
            return NULL;
        }
#ifdef SOLID_HAS_DEBUG
        itemcnt += cnt;
#endif
    }

    if(pfrontpage->empty()){
        --emptypagecnt;
    }

    void *pv = pfrontpage->pop(_cp, _rcfg);

    if(pfrontpage->full() && pfrontpage != pbackpage && !pfrontpage->pprev->full()){
        vdbgx(dbgid, "move frontpage to back: itemcnt = "<<itemcnt);
        //move the frontpage to back
        Page *ptmp = pfrontpage;

        pfrontpage = ptmp->pprev;
        pfrontpage->pnext = NULL;

        pbackpage->pprev = ptmp;
        ptmp->pnext = pbackpage;
        ptmp->pprev = NULL;
        pbackpage = ptmp;
    }
#ifdef SOLID_HAS_DEBUG
    --itemcnt;
#endif
    return pv;
}

void CacheStub::push(void *_pv, size_t _cp, Configuration const &_rcfg){
#ifdef SOLID_HAS_DEBUG
    ++itemcnt;
#endif
    Page *ppage = Page::computePage(_pv, _rcfg);
    ppage->push(_pv, _cp, _rcfg);
    if(ppage->empty() && shouldFreeEmptyPage(_rcfg)){
        //the page can be deleted
        if(ppage->pnext){
            ppage->pnext->pprev = ppage->pprev;
        }else{
            SOLID_ASSERT(pfrontpage == ppage);
            pfrontpage = ppage->pprev;
        }

        if(ppage->pprev){
            ppage->pprev->pnext = ppage->pnext;
        }else{
            SOLID_ASSERT(pbackpage == ppage);
            pbackpage = ppage->pnext;
        }
        --emptypagecnt;
        --pagecnt;
        idbgx(dbgid, "free page - pagecnt = "<<pagecnt<<" emptypagecnt = "<<emptypagecnt<<" itemcnt = "<<itemcnt);
        memory_free_aligned(ppage);
    }else if(pfrontpage != ppage && pfrontpage->usecount > ppage->usecount){
        //move the page to front
        ppage->pnext->pprev = ppage->pprev;
        if(ppage->pprev){
            ppage->pprev->pnext = ppage->pnext;
        }
        if(pbackpage == ppage){
            pbackpage = ppage->pnext;
        }

        ppage->pnext = NULL;
        ppage->pprev = pfrontpage;
        pfrontpage->pnext = ppage;
        pfrontpage = ppage;
    }
}

bool CacheStub::shouldFreeEmptyPage(Configuration const &_rcfg){
    ++emptypagecnt;
    return /*emptypagecnt > _rcfg.emptypagecnt && */emptypagecnt > keeppagecnt;
}

void CacheStub::print(size_t _cp, Configuration const &_rcfg)const{
    idbgx(dbgid, "emptypagecnt = "<<emptypagecnt<<" pagecnt = "<<pagecnt<<" itemcnt = "<<itemcnt);
    idbgx(dbgid, "pfrontpage = "<<(void*)pfrontpage<<" pbackpage = "<<(void*)pbackpage);
    Page *ppage = pfrontpage;
    while(ppage){
        ppage->print();
        ppage = ppage->pprev;
    }
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
MemoryCache::MemoryCache():pagedatacp(0){}

MemoryCache::MemoryCache(
    const size_t _pagecp,
    const size_t _emptypagecnt
){
    configure(_pagecp, _emptypagecnt);
}

MemoryCache::~MemoryCache(){
}

void MemoryCache::configure(
    const size_t _pagecp,
    const size_t _emptypagecnt
){
    cachevec.clear();
    cfg.reset((_pagecp ? _pagecp : memory_page_size()), boost::alignment_of<long>::value, _emptypagecnt);
    pagedatacp = Page::dataCapacity(cfg);
    const size_t  cnt = (pagedatacp / cfg.alignsz) + 1;

    cachevec.reserve(cnt);
    for(size_t i = 0; i < cnt; ++i){
        cachevec.push_back(CacheStub(cfg));
    }
}


void MemoryCache::print(const size_t _sz)const{
    const size_t    idx = sizeToIndex(_sz);
    const size_t    cp = indexToCapacity(idx);
    const CacheStub &cs(cachevec[idx]);
    cs.print(cp, cfg);
}

size_t MemoryCache::reserve(const size_t _sz, const size_t _cnt, const bool _lazy){
    size_t          crtcnt;
    size_t          totcnt = 0;
    const size_t    idx = sizeToIndex(_sz);
    const size_t    cp = indexToCapacity(idx);
    CacheStub       &cs(cachevec[idx]);
    size_t          pgcnt = 0;

    if(!cs.pagecnt){
        cs.keeppagecnt = 0;
    }

    while(totcnt < _cnt && (crtcnt = cs.allocate(cp, cfg))){
        totcnt += crtcnt;
        ++pgcnt;
        if(_lazy){
            break;
        }
    }
    cs.keeppagecnt += pgcnt;
    if(_lazy && totcnt < _cnt){
        size_t remaincnt = _cnt - totcnt;
        size_t pgcnt = (remaincnt / crtcnt) + 1;
        cs.keeppagecnt += pgcnt;
    }
    idbg("page count = "<<pgcnt);
    return totcnt;
}

#ifdef SOLID_HAS_NO_INLINES
#include "system/memorycache.ipp"
#endif

//-----------------------------------------------------------------------------

}//namespace solid
