// utility/stream.hpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef UTILITY_STREAM_HPP
#define UTILITY_STREAM_HPP

#include "utility/common.hpp"
#include <cstdlib>

namespace solid{

struct StreamFlags{
    enum{
        IBad  = 1,
        OBad  = 2,
        IFail = 4,
        OFail = 8,
        IEof  = 16,
        OEof  = 32,
    };
    StreamFlags(uint32_t _flags = 0);
    uint32_t flags;
};

//! The base class for all streams
class Stream{
public:
    virtual ~Stream();
    virtual int64_t seek(int64_t, SeekRef _ref = SeekBeg) = 0;
    virtual int release();
    virtual int64_t size()const;
    virtual void close();
    bool ok()const;
    bool eof()const;
    bool bad()const;
    bool fail()const;
protected:
    StreamFlags flags;
};

#ifndef SOLID_HAS_NO_INLINES
#include "utility/stream.ipp"
#endif

}//namespace solid

#endif

