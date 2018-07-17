// testb.cpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include "utility/workpool.hpp"
#include <iostream>

using namespace std;
using namespace solid;

///\cond 0
class MyWorkPool: public WorkPool<int>{
public:
    void run(Worker &);
protected:
    int createWorkers(uint);
};
///\endcond

void MyWorkPool::run(Worker &_wk){
    int k;
    while(!pop(_wk.wid(),k)){
        idbg(_wk.wid()<<" is processing "<<k);
        Thread::sleep(1000);
    }
}
int MyWorkPool::createWorkers(uint _cnt){
    //typedef GenericWorker<Worker, MyWorkPool> MyWorkerT;
    for(uint i = 0; i < _cnt; ++i){
        Worker *pw = createWorker<Worker>(*this);//new MyWorkerT(*this);
        pw->start(true);//wait for start
    }
    return _cnt;
}


int main(int argc, char *argv[]){
    {
    string s = "dbg/";
    s+= argv[0]+2;
    //initDebug(s.c_str());
    }
    Thread::init();
    MyWorkPool mwp;
    idbg("before start");
    mwp.start(5);
    idbg("after start");
    for(int i = 0; i < 1000; ++i){
        mwp.push(i);
        if(!(i % 10)) Thread::sleep(500);
    }
    idbg("before stop");
    mwp.stop();
    Thread::waitAll();
    return 0;
}
