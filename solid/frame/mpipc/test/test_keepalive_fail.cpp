#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/aio/aiolistener.hpp"
#include "solid/frame/aio/aioobject.hpp"
#include "solid/frame/aio/aioreactor.hpp"
#include "solid/frame/aio/aioresolver.hpp"
#include "solid/frame/aio/aiotimer.hpp"

#include "solid/frame/aio/openssl/aiosecurecontext.hpp"
#include "solid/frame/aio/openssl/aiosecuresocket.hpp"

#include "solid/frame/mpipc/mpipcconfiguration.hpp"
#include "solid/frame/mpipc/mpipcerror.hpp"
#include "solid/frame/mpipc/mpipcprotocol_serialization_v2.hpp"
#include "solid/frame/mpipc/mpipcservice.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "solid/system/exception.hpp"

#include "solid/system/log.hpp"

#include <iostream>

using namespace std;
using namespace solid;

using AioSchedulerT  = frame::Scheduler<frame::aio::Reactor>;
using SecureContextT = frame::aio::openssl::Context;
using ProtocolT      = frame::mpipc::serialization_v2::Protocol<uint8_t>;

namespace {

struct InitStub {
    size_t                      size;
    frame::mpipc::MessageFlagsT flags;
};

InitStub initarray[] = {
    {100000, 0},
    {2000, 0},
    {4000, 0},
    {8000, 0},
    {16000, 0},
    {32000, 0},
    {64000, 0},
    {128000, 0},
    {256000, 0},
    {512000, 0},
    {1024000, 0},
    {2048000, 0},
    {4096000, 0},
    {8192000, 0},
    {16384000, 0}};

std::string  pattern;
const size_t initarraysize = sizeof(initarray) / sizeof(InitStub);

std::atomic<size_t> crtwriteidx(0);
std::atomic<size_t> crtreadidx(0);
std::atomic<size_t> crtbackidx(0);
std::atomic<size_t> crtackidx(0);
std::atomic<size_t> writecount(0);

size_t connection_count(0);

bool                   running = true;
mutex                  mtx;
condition_variable     cnd;
frame::mpipc::Service* pmpipcclient = nullptr;
std::atomic<uint64_t>  transfered_size(0);
std::atomic<size_t>    transfered_count(0);

int test_scenario = 0;

/*
 * test_scenario == 0: test for error_connection_too_many_keepalive_packets_received
 * test_scenario == 1: test for error_inactivity_timeout
*/

size_t real_size(size_t _sz)
{
    //offset + (align - (offset mod align)) mod align
    return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
}

struct Message : frame::mpipc::Message {
    uint32_t    idx;
    std::string str;

    Message(uint32_t _idx)
        : idx(_idx)
    {
        solid_dbg(generic_logger, Info, "CREATE ---------------- " << (void*)this << " idx = " << idx);
        init();
    }
    Message()
    {
        solid_dbg(generic_logger, Info, "CREATE ---------------- " << (void*)this);
    }
    ~Message()
    {
        solid_dbg(generic_logger, Info, "DELETE ---------------- " << (void*)this);
    }

    SOLID_PROTOCOL_V2(_s, _rthis, _rctx, _name)
    {
        _s.add(_rthis.idx, _rctx, "idx").add(_rthis.str, _rctx, "str");
    }

    void init()
    {
        const size_t sz = real_size(initarray[idx % initarraysize].size);
        str.resize(sz);
        const size_t    count        = sz / sizeof(uint64_t);
        uint64_t*       pu           = reinterpret_cast<uint64_t*>(const_cast<char*>(str.data()));
        const uint64_t* pup          = reinterpret_cast<const uint64_t*>(pattern.data());
        const size_t    pattern_size = pattern.size() / sizeof(uint64_t);
        for (uint64_t i = 0; i < count; ++i) {
            pu[i] = pup[i % pattern_size]; //pattern[i % pattern.size()];
        }
    }
    bool check() const
    {
        const size_t sz = real_size(initarray[idx % initarraysize].size);
        solid_dbg(generic_logger, Info, "str.size = " << str.size() << " should be equal to " << sz);
        if (sz != str.size()) {
            return false;
        }
        //return true;
        const size_t    count        = sz / sizeof(uint64_t);
        const uint64_t* pu           = reinterpret_cast<const uint64_t*>(str.data());
        const uint64_t* pup          = reinterpret_cast<const uint64_t*>(pattern.data());
        const size_t    pattern_size = pattern.size() / sizeof(uint64_t);

        for (uint64_t i = 0; i < count; ++i) {
            if (pu[i] != pup[i % pattern_size])
                return false;
        }
        return true;
    }
};

void client_connection_stop(frame::mpipc::ConnectionContext& _rctx)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId() << " error: " << _rctx.error().message());
    if (!running) {
        ++connection_count;
    }
}

void client_connection_start(frame::mpipc::ConnectionContext& _rctx)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId());
}

void server_connection_stop(frame::mpipc::ConnectionContext& _rctx)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId() << " error: " << _rctx.error().message());

    if (test_scenario == 0) {
        if (_rctx.error() == frame::mpipc::error_connection_too_many_keepalive_packets_received) {
            lock_guard<mutex> lock(mtx);
            running = false;
            cnd.notify_one();
        }
    } else if (test_scenario == 1) {
        if (_rctx.error() == frame::mpipc::error_connection_inactivity_timeout) {
            lock_guard<mutex> lock(mtx);
            running = false;
            cnd.notify_one();
        }
    } else {
        SOLID_THROW("Invalid test scenario.");
    }
}

void server_connection_start(frame::mpipc::ConnectionContext& _rctx)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId());
}

void client_receive_message(frame::mpipc::ConnectionContext& _rctx, std::shared_ptr<Message>& _rmsgptr)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId());

    if (!_rmsgptr->check()) {
        SOLID_THROW("Message check failed.");
    }

    //cout<< _rmsgptr->str.size()<<'\n';
    transfered_size += _rmsgptr->str.size();
    ++transfered_count;

    if (!_rmsgptr->isBackOnSender()) {
        SOLID_THROW("Message not back on sender!.");
    }

    ++crtbackidx;

    //  if(crtbackidx == writecount){
    //      lock_guard<mutex> lock(mtx);
    //      running = false;
    //      cnd.notify_one();
    //  }
}

void client_complete_message(
    frame::mpipc::ConnectionContext& _rctx,
    std::shared_ptr<Message>& _rsent_msg_ptr, std::shared_ptr<Message>& _rrecv_msg_ptr,
    ErrorConditionT const& _rerror)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId());
    if (_rsent_msg_ptr.get()) {
        if (!_rerror) {
            ++crtackidx;
        }
    }

    if (_rrecv_msg_ptr.get()) {
        client_receive_message(_rctx, _rrecv_msg_ptr);
    }
}

void server_receive_message(frame::mpipc::ConnectionContext& _rctx, std::shared_ptr<Message>& _rmsgptr)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId() << " message id on sender " << _rmsgptr->senderRequestId());
    if (!_rmsgptr->check()) {
        SOLID_THROW("Message check failed.");
    }

    if (!_rmsgptr->isOnPeer()) {
        SOLID_THROW("Message not on peer!.");
    }

    //send message back
    _rctx.service().sendResponse(_rctx.recipientId(), _rmsgptr);

    ++crtreadidx;
    solid_dbg(generic_logger, Info, crtreadidx);
    if (crtwriteidx < writecount) {
        frame::mpipc::MessagePointerT msgptr(new Message(crtwriteidx));
        ++crtwriteidx;
        pmpipcclient->sendMessage(
            "localhost", msgptr,
            initarray[crtwriteidx % initarraysize].flags | frame::mpipc::MessageFlagsE::WaitResponse);
    }
}

void server_complete_message(
    frame::mpipc::ConnectionContext& _rctx,
    std::shared_ptr<Message>& _rsent_msg_ptr, std::shared_ptr<Message>& _rrecv_msg_ptr,
    ErrorConditionT const& _rerror)
{
    solid_dbg(generic_logger, Info, _rctx.recipientId());
    if (_rrecv_msg_ptr.get()) {
        server_receive_message(_rctx, _rrecv_msg_ptr);
    }
}

} //namespace

int test_keepalive_fail(int argc, char* argv[])
{
    solid::log_start(std::cerr, {".*:EW"});

    size_t max_per_pool_connection_count = 1;

    if (argc > 1) {
        test_scenario = atoi(argv[1]);
        if (test_scenario > 1) {
            SOLID_THROW("Invalid test scenario.");
        }
    }

    for (int i = 0; i < 127; ++i) {
        if (isprint(i) && !isblank(i)) {
            pattern += static_cast<char>(i);
        }
    }

    size_t sz = real_size(pattern.size());

    if (sz > pattern.size()) {
        pattern.resize(sz - sizeof(uint64_t));
    } else if (sz < pattern.size()) {
        pattern.resize(sz);
    }

    {
        AioSchedulerT sch_client;
        AioSchedulerT sch_server;

        frame::Manager         m;
        frame::mpipc::ServiceT mpipcserver(m);
        frame::mpipc::ServiceT mpipcclient(m);
        ErrorConditionT        err;

        frame::aio::Resolver resolver;

        err = sch_client.start(1);

        if (err) {
            solid_dbg(generic_logger, Error, "starting aio client scheduler: " << err.message());
            return 1;
        }

        err = sch_server.start(1);

        if (err) {
            solid_dbg(generic_logger, Error, "starting aio server scheduler: " << err.message());
            return 1;
        }

        err = resolver.start(1);

        if (err) {
            solid_dbg(generic_logger, Error, "starting aio resolver: " << err.message());
            return 1;
        }

        std::string server_port;

        { //mpipc server initialization
            auto                        proto = ProtocolT::create();
            frame::mpipc::Configuration cfg(sch_server, proto);

            proto->null(0);
            proto->registerMessage<Message>(server_complete_message, 1);

            //cfg.recv_buffer_capacity = 1024;
            //cfg.send_buffer_capacity = 1024;

            cfg.connection_stop_fnc         = &server_connection_stop;
            cfg.server.connection_start_fnc = &server_connection_start;

            cfg.server.listener_address_str   = "0.0.0.0:0";
            cfg.server.connection_start_state = frame::mpipc::ConnectionState::Active;

            if (test_scenario == 0) {
                cfg.connection_inactivity_timeout_seconds = 60;
                cfg.connection_inactivity_keepalive_count = 4;
            } else if (test_scenario == 1) {
                cfg.connection_inactivity_timeout_seconds = 20;
                cfg.connection_inactivity_keepalive_count = 4;
            }

            cfg.writer.max_message_count_multiplex = 6;

            err = mpipcserver.reconfigure(std::move(cfg));

            if (err) {
                solid_dbg(generic_logger, Error, "starting server mpipcservice: " << err.message());
                //exiting
                return 1;
            }

            {
                std::ostringstream oss;
                oss << mpipcserver.configuration().server.listenerPort();
                server_port = oss.str();
                solid_dbg(generic_logger, Info, "server listens on port: " << server_port);
            }
        }

        { //mpipc client initialization
            auto                        proto = ProtocolT::create();
            frame::mpipc::Configuration cfg(sch_client, proto);

            proto->null(0);
            proto->registerMessage<Message>(client_complete_message, 1);

            //cfg.recv_buffer_capacity = 1024;
            //cfg.send_buffer_capacity = 1024;
            cfg.client.connection_start_state = frame::mpipc::ConnectionState::Active;

            if (test_scenario == 0) {
                cfg.connection_keepalive_timeout_seconds = 10;
            } else if (test_scenario == 1) {
                cfg.connection_keepalive_timeout_seconds = 100;
            }

            cfg.connection_stop_fnc         = &client_connection_stop;
            cfg.client.connection_start_fnc = &client_connection_start;

            cfg.pool_max_active_connection_count = max_per_pool_connection_count;

            cfg.writer.max_message_count_multiplex = 6;

            cfg.client.name_resolve_fnc = frame::mpipc::InternetResolverF(resolver, server_port.c_str() /*, SocketInfo::Inet4*/);

            err = mpipcclient.reconfigure(std::move(cfg));

            if (err) {
                solid_dbg(generic_logger, Error, "starting client mpipcservice: " << err.message());
                //exiting
                return 1;
            }
        }

        pmpipcclient = &mpipcclient;

        writecount = 1;

        {
            frame::mpipc::MessagePointerT msgptr(new Message(crtwriteidx));
            ++crtwriteidx;
            mpipcclient.sendMessage(
                "localhost", msgptr,
                initarray[crtwriteidx % initarraysize].flags | frame::mpipc::MessageFlagsE::WaitResponse);
        }

        unique_lock<mutex> lock(mtx);

        if (!cnd.wait_for(lock, std::chrono::seconds(120), []() { return !running; })) {
            SOLID_THROW("Process is taking too long.");
        }

        if (crtwriteidx != crtackidx) {
            SOLID_THROW("Not all messages were completed");
        }

        //m.stop();
    }

    //exiting

    std::cout << "Transfered size = " << (transfered_size * 2) / 1024 << "KB" << endl;
    std::cout << "Transfered count = " << transfered_count << endl;
    std::cout << "Connection count = " << connection_count << endl;

    return 0;
}
