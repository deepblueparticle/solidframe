// utility/iostream.ipp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifdef SOLID_HAS_NO_INLINES
#define inline
#endif

inline InputOutputStreamIterator::InputOutputStreamIterator(InputOutputStream *_ps, int64_t _off):ps(_ps),off(_off){
}
inline void InputOutputStreamIterator::reinit(InputOutputStream *_ps, int64_t _off){
    ps = _ps;
    off = _off;
}
inline int64_t InputOutputStreamIterator::start(){
    return ps->seek(off);
}


#ifdef SOLID_HAS_NO_INLINES
#undef inline
#endif
