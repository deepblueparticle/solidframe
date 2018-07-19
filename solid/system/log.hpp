// solid/system/log.hpp
//
// Copyright (c) 2018 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/system/common.hpp"
#include "solid/system/error.hpp"
#include <memory>
#include <atomic>
#include <ostream>
#include <string>
#include <cstring>

namespace solid {

using LogAtomicFlagsBackT = unsigned long;
using LogAtomicFlagsT     = std::atomic<LogAtomicFlagsBackT>;

enum struct LogFlags : LogAtomicFlagsBackT {
    Verbose,
    Info,
    Warning,
    Error,
    Statistic,
    LastFlag
};

struct LogCategoryBase {
    virtual ~LogCategoryBase();

    virtual void parse(LogAtomicFlagsBackT& _ror_flags, LogAtomicFlagsBackT& _rand_flags, const std::string& _txt) const = 0;
};

struct LogCategory : public LogCategoryBase {
    static const LogCategory& the();

    const char* flagName(const LogFlags _flag) const
    {
        switch (_flag) {
        case LogFlags::Verbose:
            return "V";
        case LogFlags::Info:
            return "I";
        case LogFlags::Warning:
            return "W";
        case LogFlags::Error:
            return "E";
        case LogFlags::Statistic:
            return "S";
        case LogFlags::LastFlag:
            break;
        }
        return "?";
    }

private:
    void parse(LogAtomicFlagsBackT& _ror_flags, LogAtomicFlagsBackT& _rand_flags, const std::string& _txt) const override;
};

namespace impl{

template <size_t Size>
class LogLineBuffer: public std::streambuf{
    struct BufNode{
        static constexpr const size_t buffer_capacity = Size - sizeof(std::unique_ptr<BufNode>);
        
        char buf_[buffer_capacity];
        
        const char* begin()const{
            return &buf_[0];
        }
        const char *end()const{
            return &buf_[buffer_capacity];
        }
        
        std::unique_ptr<BufNode> pnext_;
        
        BufNode():pnext_(nullptr){}
        
    } first_;
    BufNode *plast_;
    char    *pcrt_;
    const char* pend_;
private:
    
    void allocate(){
        plast_->pnext_.reset(new BufNode);
        plast_ = plast_->pnext_.get();
        pcrt_ = plast_->buf_;
        pend_ = plast_->end();
    }
    
    // write one character
    int_type overflow(int_type c) override{
        if(c != EOF){
            if(pcrt_ == pend_){
                allocate();
            }
            *pcrt_ = c;
            ++pcrt_;
        }
        return c;
    }
    
    // write multiple characters
    std::streamsize xsputn(const char* s, std::streamsize num) override{
        std::streamsize sz = num;
        while(sz){
            if(pcrt_ == pend_){
                allocate();
            }
            std::streamsize towrite = pend_ - pcrt_;
            if(towrite > sz) towrite = sz;
            memcpy(pcrt_, s, towrite);
            s += towrite;
            sz -= towrite;
            pcrt_ += towrite;
        }
        return num;
    }
public:
    LogLineBuffer():plast_(&first_), pcrt_(first_.buf_), pend_(first_.end()){
        static_assert(sizeof(BufNode) == Size, "sizeof(BufNode) not equal to Size");
    }
    
    std::ostream& writeTo(std::ostream &_ros)const{
        const BufNode *pbn = &first_;
        do{
            if(pbn == plast_){
                _ros.write(pbn->buf_, pcrt_ - pbn->begin());
            }else{
                _ros.write(pbn->buf_, BufNode::buffer_capacity);
            }
            pbn = pbn->pnext_.get();
        }while(pbn);
        return _ros;
    }
};

class LogLineStreamBase: public std::ostream{
protected:
    LogLineStreamBase():std::ostream(0){}
public:
    virtual std::ostream& writeTo(std::ostream&) = 0;
};

template <size_t Size>
class LogLineStream : public LogLineStreamBase {
protected:
    LogLineBuffer<Size> buf_;

public:
    LogLineStream()
    {
        rdbuf(&buf_);
    }
    
    std::ostream& writeTo(std::ostream& _ros) override{
        return buf_.writeTo(_ros);
    }
};

}//namespace impl

namespace {
class Engine;
} //namespace

class LoggerBase {
    friend class Engine;

    const std::string name_;
    LogAtomicFlagsT   flags_;
    const size_t      idx_;

protected:
    LoggerBase(const std::string& _name, const LogCategoryBase& _rlc);
    ~LoggerBase();

    std::ostream& doLog(std::ostream &_ros, const char* _flag_name, const char* _file, const char* _fnc, int _line) const;
    void          doDone(impl::LogLineStreamBase &_log_ros) const;

public:
    const std::string& name() const
    {
        return name_;
    }
    void remask(const LogAtomicFlagsBackT _msk)
    {
        flags_.store(_msk);
    }
    LogAtomicFlagsBackT flags() const
    {
        return flags_.load(std::memory_order_relaxed);
    }
};

template <class Flgs = LogFlags, class LogCat = LogCategory>
class Logger : protected LoggerBase {
    const LogCat& rcat_;

public:
    using ThisT = Logger<Flgs, LogCat>;
    using FlagT = Flgs;

    Logger(const std::string& _name)
        : LoggerBase(_name, LogCat::the())
        , rcat_(LogCat::the())
    {
    }

    bool shouldLog(const FlagT _flag) const
    {
        return (flags() & (1UL << static_cast<size_t>(_flag))) != 0;
    }

    std::ostream& log(std::ostream &_ros, const FlagT _flag, const char* _file, const char* _fnc, int _line) const
    {
        return this->doLog(_ros, rcat_.flagName(_flag), _file, _fnc, _line);
    }
    void done(impl::LogLineStreamBase &_log_ros) const
    {
        doDone(_log_ros);
    }
};

using LoggerT = Logger<>;

extern const LoggerT generic_logger;

void log_stop();

ErrorConditionT log_start(
    std::ostream&                   _ros,
    const std::vector<std::string>& _rmodule_mask_vec);

ErrorConditionT log_start(
    const char*                     _fname,
    const std::vector<std::string>& _rmodule_mask_vec,
    bool                            _buffered   = true,
    uint32_t                        _respincnt  = 2,
    uint64_t                        _respinsize = 1024 * 1024 * 1024);

ErrorConditionT log_start(
    const char*                     _addr,
    const char*                     _port,
    const std::vector<std::string>& _rmodule_mask_vec,
    bool                            _buffered = true);

} //namespace solid

#ifndef SOLID_FUNCTION_NAME
#ifdef SOLID_ON_WINDOWS
#define SOLID_FUNCTION_NAME __func__
#else
#define SOLID_FUNCTION_NAME __FUNCTION__
#endif
#endif

#ifndef SOLID_LOG_BUFFER_SIZE
#define SOLID_LOG_BUFFER_SIZE 2*1024
#endif

#ifdef SOLID_HAS_DEBUG

#define solid_dbg(Lgr, Flg, Txt)                                                                         \
    if (Lgr.shouldLog(decltype(Lgr)::FlagT::Flg)) {                                                      \
        solid::impl::LogLineStream<SOLID_LOG_BUFFER_SIZE> os;\
        Lgr.log(os, decltype(Lgr)::FlagT::Flg, __FILE__, SOLID_FUNCTION_NAME, __LINE__) << Txt << std::endl; \
        Lgr.done(os);                                                                                      \
    }

#else

#define solid_dbg(...)

#endif

#define solid_log(Lgr, Flg, Txt)                                                                         \
    if (Lgr.shouldLog(decltype(Lgr)::FlagT::Flg)) {                                                      \
        solid::impl::LogLineStream<SOLID_LOG_BUFFER_SIZE> os;\
        Lgr.log(os, decltype(Lgr)::FlagT::Flg, __FILE__, SOLID_FUNCTION_NAME, __LINE__) << Txt << std::endl; \
        Lgr.done(os);                                                                                      \
    }

#ifdef SOLID_HAS_STATISTICS

#define SOLID_COLLECT(om, ...) \
    om(__VA_ARGS__)

#else
#define SOLID_COLLECT(...)
#endif
