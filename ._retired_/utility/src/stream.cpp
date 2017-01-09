// utility/src/stream.cpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include <cstdlib>
#include "utility/stream.hpp"
#include "utility/istream.hpp"
#include "utility/ostream.hpp"
#include "utility/iostream.hpp"
#include "utility/streampointer.hpp"

namespace solid{

void StreamPointerBase::clear(Stream *_ps){
    if(_ps->release()) delete _ps;
}

Stream::~Stream(){
}

void Stream::close(){
}

int Stream::release(){return -1;}
//bool Stream::isOk()const{return true;}

int64_t Stream::size()const{return -1;}

InputStream::~InputStream(){
}

OutputStream::~OutputStream(){
}

InputOutputStream::~InputOutputStream(){
}

#ifdef SOLID_HAS_NO_INLINES
#include "utility/stream.ipp"
#include "utility/istream.ipp"
#include "utility/ostream.ipp"
#include "utility/iostream.ipp"
#endif

bool InputStream::readAll(char *_pd, uint32_t _dl, uint32_t){
    int rv;
    char *pd = (char*)_pd;
    while(_dl && (rv = this->read(pd, _dl)) != (int)_dl){
        if(rv > 0){
            pd += rv;
            _dl -= rv;
        }else return false;
    }
    return true;
}

 int InputStream::read(uint64_t _offset, char* _pbuf, uint32_t _blen, uint32_t _flags){
     const int64_t  crtoff(seek(0, SeekCur));
     
     if(crtoff < 0) return -1;
     if(_offset != static_cast<uint64_t>(seek(_offset))) return -1;
     
     int rv = read(_pbuf, _blen, _flags);
     
     seek(crtoff);
     return rv;
 }


int OutputStream::write(uint64_t _offset, const char *_pbuf, uint32_t _blen, uint32_t _flags){
     const int64_t  crtoff(seek(0, SeekCur));
     
     if(crtoff < 0) return -1;
     if(_offset != static_cast<uint64_t>(seek(_offset))) return -1;
     
     int rv = write(_pbuf, _blen, _flags);
     
     seek(crtoff);
     return rv;
}

}//namespace solid
