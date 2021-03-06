// solid/system/common.hpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/solid_config.hpp"
#include <cstdint>
#include <cstdlib>

#ifdef SOLID_ON_WINDOWS
#define UBOOSTMUTEX
#define UBOOSTSHAREDPTR
//#endif
#endif

namespace solid {

typedef unsigned char uchar;
typedef unsigned int  uint;

typedef unsigned long  ulong;
typedef unsigned short ushort;

typedef long long          longlong;
typedef unsigned long long ulonglong;

enum SeekRef {
    SeekBeg = 0,
    SeekCur = 1,
    SeekEnd = 2
};

struct EmptyType {
};
class NullType {
};

template <class T>
struct TypeToType {
    using TypeT = T;
};

template <class T>
struct UnsignedType;

template <>
struct UnsignedType<int8_t> {
    typedef uint8_t Type;
};

template <>
struct UnsignedType<int16_t> {
    typedef uint16_t Type;
};

template <>
struct UnsignedType<int32_t> {
    typedef uint32_t Type;
};

template <>
struct UnsignedType<int64_t> {
    typedef uint64_t Type;
};

template <>
struct UnsignedType<uint8_t> {
    typedef uint8_t Type;
};

template <>
struct UnsignedType<uint16_t> {
    typedef uint16_t Type;
};

template <>
struct UnsignedType<uint32_t> {
    typedef uint32_t Type;
};

template <>
struct UnsignedType<uint64_t> {
    typedef uint64_t Type;
};

} //namespace solid

#ifdef SOLID_ON_WINDOWS
#include <type_traits>

using ssize_t = std::make_signed<size_t>::type;

#endif

//Some macro helpers:

//adapted from: https://stackoverflow.com/questions/9183993/msvc-variadic-macro-expansion/9338429#9338429
#define SOLID_GLUE(x, y) x y

#define SOLID_RETURN_ARG_COUNT(_1_, _2_, _3_, _4_, _5_, count, ...) count
#define SOLID_EXPAND_ARGS(args) SOLID_RETURN_ARG_COUNT args
#define SOLID_COUNT_ARGS_MAX5(...) SOLID_EXPAND_ARGS((__VA_ARGS__, 5, 4, 3, 2, 1, 0))

#define SOLID_OVERLOAD_MACRO2(name, count) name##count
#define SOLID_OVERLOAD_MACRO1(name, count) SOLID_OVERLOAD_MACRO2(name, count)
#define SOLID_OVERLOAD_MACRO(name, count) SOLID_OVERLOAD_MACRO1(name, count)

#define SOLID_CALL_OVERLOAD(name, ...) SOLID_GLUE(SOLID_OVERLOAD_MACRO(name, SOLID_COUNT_ARGS_MAX5(__VA_ARGS__)), (__VA_ARGS__))
