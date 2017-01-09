// testa.cpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include "solid/utility/workpool.hpp"
#include "solid/system/debug.hpp"
#include <iostream>
#include <thread>

using namespace std;
using namespace solid;

struct MyWorkPoolController;

typedef WorkPool<int, MyWorkPoolController> MyWorkPool;

struct MyWorkPoolController: WorkPoolControllerBase{
    typedef std::vector<int>    IntVectorT;
    
    void createWorker(MyWorkPool &_rwp, ushort _wkrcnt, std::thread &_rthr){
        _rwp.createMultiWorker(_rthr, 4);
    }
    void execute(WorkPoolBase &_rwp, WorkerBase &, int _i){
        idbg("i = "<<_i);
        std::this_thread::sleep_for(std::chrono::milliseconds(_i * 10));
    }
    void execute(WorkPoolBase &_rwp, WorkerBase &, IntVectorT &_rjobvec){
        for(IntVectorT::const_iterator it(_rjobvec.begin()); it != _rjobvec.end(); ++it){
            idbg("it = "<<*it);
            std::this_thread::sleep_for(std::chrono::milliseconds(*it * 10));
        }
    }
};


int main(int argc, char *argv[]){
#ifdef SOLID_HAS_DEBUG
    {
    string dbgout;
    Debug::the().levelMask("iew");
    Debug::the().moduleMask("any");
    Debug::the().initStdErr(true);
    }
#endif
    MyWorkPool  mwp;
    mwp.start(2);
    
    for(int i(0); i < 100; ++i){
        mwp.push(i);
    }
    mwp.stop(true);
    return 0;
}
