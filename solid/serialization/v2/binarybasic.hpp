// solid/serialization/v2/binarybasic.hpp
//
// Copyright (c) 2018 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/utility/algorithm.hpp"
#include <array>
#include <cstring>

namespace solid {
namespace serialization {
namespace v2 {
namespace binary {

inline char* store(char* _pd, const uint8_t _val)
{
    uint8_t* pd = reinterpret_cast<uint8_t*>(_pd);
    *pd         = _val;
    return _pd + 1;
}

inline char* store(char* _pd, const uint16_t _val)
{
    uint8_t* pd = reinterpret_cast<uint8_t*>(_pd);
    *(pd)       = ((_val >> 8) & 0xff);
    *(pd + 1)   = (_val & 0xff);
    return _pd + 2;
}

inline char* store(char* _pd, const uint32_t _val)
{

    _pd = store(_pd, static_cast<uint16_t>(_val >> 16));

    return store(_pd, static_cast<uint16_t>(_val & 0xffff));
}

inline char* store(char* _pd, const uint64_t _val)
{

    _pd = store(_pd, static_cast<uint32_t>(_val >> 32));

    return store(_pd, static_cast<uint32_t>(_val & 0xffffffffULL));
}

template <size_t S>
inline char* store(char* _pd, const std::array<uint8_t, S> _val)
{
    memcpy(_pd, _val.data(), S);
    return _pd + S;
}

inline const char* load(const char* _ps, uint8_t& _val)
{
    const uint8_t* ps = reinterpret_cast<const uint8_t*>(_ps);
    _val              = *ps;
    return _ps + 1;
}

inline const char* load(const char* _ps, uint16_t& _val)
{
    const uint8_t* ps = reinterpret_cast<const uint8_t*>(_ps);
    _val              = *ps;
    _val <<= 8;
    _val |= *(ps + 1);
    return _ps + 2;
}

inline const char* load(const char* _ps, uint32_t& _val)
{
    uint16_t upper;
    uint16_t lower;
    _ps  = load(_ps, upper);
    _ps  = load(_ps, lower);
    _val = upper;
    _val <<= 16;
    _val |= lower;
    return _ps;
}

inline const char* load(const char* _ps, uint64_t& _val)
{
    uint32_t upper;
    uint32_t lower;
    _ps  = load(_ps, upper);
    _ps  = load(_ps, lower);
    _val = upper;
    _val <<= 32;
    _val |= lower;
    return _ps;
}
template <size_t S>
inline const char* load(const char* _ps, std::array<uint8_t, S>& _val)
{
    memcpy(_val.data, _ps, S);
    return _ps + S;
}

//cross integer serialization
namespace cross {
inline size_t size(const char* _ps)
{
    const uint8_t* ps = reinterpret_cast<const uint8_t*>(_ps);
    uint8_t        v  = *ps;
    const bool     ok = check_value_with_crc(v, v);
    if (ok) {
        return v + 1;
    }
    return InvalidSize();
}

inline size_t size(uint8_t _v)
{
    return max_padded_byte_cout(_v) + 1;
}

inline size_t size(uint16_t _v)
{
    return max_padded_byte_cout(_v) + 1;
}

inline size_t size(uint32_t _v)
{
    return max_padded_byte_cout(_v) + 1;
}
inline size_t size(uint64_t _v)
{
    return max_padded_byte_cout(_v) + 1;
}

char* store_with_check(char* _pd, const size_t _sz, uint8_t _v);
char* store_with_check(char* _pd, const size_t _sz, uint16_t _v);
char* store_with_check(char* _pd, const size_t _sz, uint32_t _v);

inline char* store_with_check(char* _pd, const size_t _sz, uint64_t _v)
{
    uint8_t*     pd = reinterpret_cast<uint8_t*>(_pd);
    const size_t sz = max_padded_byte_cout(_v);
    if ((sz + 1) <= _sz) {
        const bool ok = compute_value_with_crc(*pd, static_cast<uint8_t>(sz));
        if (ok) {
            ++pd;

            switch (sz) {
            case 0:
                break;
            case 1:
                *pd = (_v & 0xff);
                break;
            case 2:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                break;
            case 3:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                break;
            case 4:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                _v >>= 8;
                *(pd + 3) = (_v & 0xff);
                break;
            case 5:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                _v >>= 8;
                *(pd + 3) = (_v & 0xff);
                _v >>= 8;
                *(pd + 4) = (_v & 0xff);
                break;
            case 6:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                _v >>= 8;
                *(pd + 3) = (_v & 0xff);
                _v >>= 8;
                *(pd + 4) = (_v & 0xff);
                _v >>= 8;
                *(pd + 5) = (_v & 0xff);
                break;
            case 7:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                _v >>= 8;
                *(pd + 3) = (_v & 0xff);
                _v >>= 8;
                *(pd + 4) = (_v & 0xff);
                _v >>= 8;
                *(pd + 5) = (_v & 0xff);
                _v >>= 8;
                *(pd + 6) = (_v & 0xff);
                break;
            case 8:
                *(pd + 0) = (_v & 0xff);
                _v >>= 8;
                *(pd + 1) = (_v & 0xff);
                _v >>= 8;
                *(pd + 2) = (_v & 0xff);
                _v >>= 8;
                *(pd + 3) = (_v & 0xff);
                _v >>= 8;
                *(pd + 4) = (_v & 0xff);
                _v >>= 8;
                *(pd + 5) = (_v & 0xff);
                _v >>= 8;
                *(pd + 6) = (_v & 0xff);
                _v >>= 8;
                *(pd + 7) = (_v & 0xff);
                break;
            default:
                return nullptr;
            }

            return _pd + sz + 1;
        }
    }
    return nullptr;
}

const char* load_with_check(const char* _ps, const size_t _sz, uint8_t& _val);
const char* load_with_check(const char* _ps, const size_t _sz, uint16_t& _val);
const char* load_with_check(const char* _ps, const size_t _sz, uint32_t& _val);

inline const char* load_with_check(const char* _ps, const size_t _sz, uint64_t& _val)
{
    if (_sz != 0) {
        const uint8_t* ps = reinterpret_cast<const uint8_t*>(_ps);
        uint8_t        v  = *ps;
        const bool     ok = check_value_with_crc(v, v);
        const size_t   sz = v;

        if (ok && (sz + 1) <= _sz) {
            ++ps;

            switch (sz) {
            case 0:
                _val = 0;
                break;
            case 1:
                _val = *ps;
                break;
            case 2:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                break;
            case 3:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                break;
            case 4:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                _val |= static_cast<uint64_t>(*(ps + 3)) << 24;
                break;
            case 5:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                _val |= static_cast<uint64_t>(*(ps + 3)) << 24;
                _val |= static_cast<uint64_t>(*(ps + 4)) << 32;
                break;
            case 6:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                _val |= static_cast<uint64_t>(*(ps + 3)) << 24;
                _val |= static_cast<uint64_t>(*(ps + 4)) << 32;
                _val |= static_cast<uint64_t>(*(ps + 5)) << 40;
                break;
            case 7:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                _val |= static_cast<uint64_t>(*(ps + 3)) << 24;
                _val |= static_cast<uint64_t>(*(ps + 4)) << 32;
                _val |= static_cast<uint64_t>(*(ps + 5)) << 40;
                _val |= static_cast<uint64_t>(*(ps + 6)) << 48;
                break;
            case 8:
                _val = *ps;
                _val |= static_cast<uint64_t>(*(ps + 1)) << 8;
                _val |= static_cast<uint64_t>(*(ps + 2)) << 16;
                _val |= static_cast<uint64_t>(*(ps + 3)) << 24;
                _val |= static_cast<uint64_t>(*(ps + 4)) << 32;
                _val |= static_cast<uint64_t>(*(ps + 5)) << 40;
                _val |= static_cast<uint64_t>(*(ps + 6)) << 48;
                _val |= static_cast<uint64_t>(*(ps + 7)) << 56;
                break;
            default:
                return nullptr;
            }
            return _ps + static_cast<size_t>(v) + 1;
        }
    }
    return nullptr;
}
} //namespace cross

inline void store_bit_at(uint8_t* _pbeg, const size_t _bit_idx, const bool _opt)
{
    _pbeg += (_bit_idx >> 3);
    const size_t  bit_off = _bit_idx & 7;
    const uint8_t opt     = _opt;
    //clear the bit
    *_pbeg &= ~static_cast<uint8_t>(1 << bit_off);
    *_pbeg |= ((opt & 1) << bit_off);
}

inline bool load_bit_from(const uint8_t* _pbeg, const size_t _bit_idx)
{
    static const bool b[2] = {false, true};
    _pbeg += (_bit_idx >> 3);
    const size_t bit_off = _bit_idx & 7;
    return b[(*_pbeg >> bit_off) & 1];
}

} //namespace binary
} //namespace v2
} //namespace serialization
} //namespace solid
