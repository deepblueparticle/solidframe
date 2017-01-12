#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/aio/aioreactor.hpp"
#include "solid/frame/aio/aioobject.hpp"
#include "solid/frame/aio/aiolistener.hpp"
#include "solid/frame/aio/aiotimer.hpp"
#include "solid/frame/aio/aioresolver.hpp"

#include "solid/frame/aio/openssl/aiosecurecontext.hpp"
#include "solid/frame/aio/openssl/aiosecuresocket.hpp"

#include "solid/frame/mpipc/mpipcservice.hpp"
#include "solid/frame/mpipc/mpipcconfiguration.hpp"
#include "solid/frame/mpipc/mpipcprotocol_serialization_v1.hpp"
#include "solid/frame/mpipc/mpipcsocketstub_openssl.hpp"


#include <mutex>
#include <thread>
#include <condition_variable>

#include "solid/system/exception.hpp"

#include "solid/system/debug.hpp"

#include <iostream>

using namespace std;
using namespace solid;

typedef frame::Scheduler<frame::aio::Reactor>   AioSchedulerT;
typedef frame::aio::openssl::Context            SecureContextT;

namespace{

struct InitStub{
    size_t      size;
    ulong       flags;
};

InitStub initarray[] = {
    {100000, 0}
};

std::string                     pattern;
const size_t                    initarraysize = sizeof(initarray)/sizeof(InitStub);

std::atomic<size_t>             crtwriteidx(0);
std::atomic<size_t>             crtreadidx(0);
std::atomic<size_t>             crtbackidx(0);
std::atomic<size_t>             crtackidx(0);
std::atomic<size_t>             writecount(0);

size_t                          connection_count(0);

bool                            running = true;
mutex                           mtx;
condition_variable              cnd;
frame::mpipc::Service               *pmpipcclient = nullptr;
std::atomic<uint64_t>           transfered_size(0);
std::atomic<size_t>             transfered_count(0);


size_t real_size(size_t _sz){
    //offset + (align - (offset mod align)) mod align
    return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
}

struct Message: frame::mpipc::Message{
    uint32_t                            idx;
    std::string                     str;
    bool                            serialized;

    Message(uint32_t _idx):idx(_idx), serialized(false){
        idbg("CREATE ---------------- "<<(void*)this<<" idx = "<<idx);
        init();

    }
    Message(): serialized(false){
        idbg("CREATE ---------------- "<<(void*)this);
    }
    ~Message(){
        idbg("DELETE ---------------- "<<(void*)this);
        SOLID_ASSERT(not serialized);
    }

    template <class S>
    void solidSerialize(S &_s, frame::mpipc::ConnectionContext &_rctx){
        _s.push(str, "str");
        _s.push(idx, "idx");

        if(S::IsSerializer){
            serialized = true;
        }
    }

    void init(){
        const size_t    sz = real_size(initarray[idx % initarraysize].size);
        str.resize(sz);
        const size_t    count = sz / sizeof(uint64_t);
        uint64_t            *pu  = reinterpret_cast<uint64_t*>(const_cast<char*>(str.data()));
        const uint64_t  *pup = reinterpret_cast<const uint64_t*>(pattern.data());
        const size_t    pattern_size = pattern.size() / sizeof(uint64_t);
        for(uint64_t i = 0; i < count; ++i){
            pu[i] = pup[(idx + i) % pattern_size];//pattern[i % pattern.size()];
        }
    }

    bool check()const{
        const size_t    sz = real_size(initarray[idx % initarraysize].size);
        idbg("str.size = "<<str.size()<<" should be equal to "<<sz);
        if(sz != str.size()){
            return false;
        }
        //return true;
        const size_t    count = sz / sizeof(uint64_t);
        const uint64_t  *pu = reinterpret_cast<const uint64_t*>(str.data());
        const uint64_t  *pup = reinterpret_cast<const uint64_t*>(pattern.data());
        const size_t    pattern_size = pattern.size() / sizeof(uint64_t);

        for(uint64_t i = 0; i < count; ++i){
            if(pu[i] != pup[(i + idx) % pattern_size]){
                SOLID_THROW("Message check failed.");
                return false;
            }
        }
        return true;
    }

};

void client_connection_stop(frame::mpipc::ConnectionContext &_rctx){
    idbg(_rctx.recipientId()<<" error: "<<_rctx.error().message());
    if(!running){
        ++connection_count;
    }
}

void client_connection_start(frame::mpipc::ConnectionContext &_rctx){
    idbg(_rctx.recipientId());
}

void client_complete_message(
    frame::mpipc::ConnectionContext &_rctx,
    std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
    ErrorConditionT const &_rerror
){
    idbg(_rctx.recipientId()<<" "<<_rerror.message());

    SOLID_CHECK(not _rrecv_msg_ptr);
    SOLID_CHECK(_rsent_msg_ptr);

    SOLID_CHECK(
        _rerror == frame::mpipc::error_message_connection and
        (
            (_rctx.error() == frame::aio::error_stream_shutdown and not _rctx.systemError())
            or
            (_rctx.error() and _rctx.systemError())
        )
    );

    {
        unique_lock<mutex> lock(mtx);
        running = false;
        cnd.notify_one();
    }
}

}//namespace

int test_clientserver_oneshot(int argc, char **argv){
#ifdef SOLID_HAS_DEBUG
    Debug::the().levelMask("ew");
    Debug::the().moduleMask("frame_mpipc:ew any:ew");
    Debug::the().initStdErr(false, nullptr);
    //Debug::the().initFile("test_clientserver_basic", false);
#endif

    size_t max_per_pool_connection_count = 1;

    if(argc > 1){
        max_per_pool_connection_count = atoi(argv[1]);
        if(max_per_pool_connection_count == 0){
            max_per_pool_connection_count = 1;
        }
        if(max_per_pool_connection_count > 100){
            max_per_pool_connection_count = 100;
        }
    }

    bool    secure = false;

    if(argc > 2){
        if(*argv[2] == 's' or *argv[2] == 'S'){
            secure = true;
        }
    }

    for(int j = 0; j < 1; ++j){
        for(int i = 0; i < 127; ++i){
            int c = (i + j) % 127;
            if(isprint(c) and !isblank(c)){
                pattern += static_cast<char>(c);
            }
        }
    }

    const size_t    sz = real_size(pattern.size());

    if(sz > pattern.size()){
        pattern.resize(sz - sizeof(uint64_t));
    }else if(sz < pattern.size()){
        pattern.resize(sz);
    }

    {
        AioSchedulerT           sch_client;


        frame::Manager          m;
        frame::mpipc::ServiceT  mpipcclient(m);
        ErrorConditionT         err;

        frame::aio::Resolver    resolver;

        err = sch_client.start(1);

        if(err){
            edbg("starting aio client scheduler: "<<err.message());
            return 1;
        }

        err = resolver.start(1);

        if(err){
            edbg("starting aio resolver: "<<err.message());
            return 1;
        }

        std::string     server_port = "60432";

        {//mpipc client initialization
            auto                        proto = frame::mpipc::serialization_v1::Protocol::create();
            frame::mpipc::Configuration cfg(sch_client, proto);

            proto->registerType<Message>(
                client_complete_message
            );

            //cfg.recv_buffer_capacity = 1024;
            //cfg.send_buffer_capacity = 1024;

            cfg.connection_stop_fnc = client_connection_stop;
            cfg.client.connection_start_fnc = client_connection_start;

            cfg.pool_max_active_connection_count = max_per_pool_connection_count;

            cfg.client.connection_start_state = frame::mpipc::ConnectionState::Active;

            cfg.client.name_resolve_fnc = frame::mpipc::InternetResolverF(resolver, server_port.c_str()/*, SocketInfo::Inet4*/);

            if(secure){
                idbg("Configure SSL client ------------------------------------");
                frame::mpipc::openssl::setup_client(
                    cfg,
                    [](frame::aio::openssl::Context &_rctx) -> ErrorCodeT{
                        _rctx.loadVerifyFile("echo-ca-cert.pem"/*"/etc/pki/tls/certs/ca-bundle.crt"*/);
                        _rctx.loadCertificateFile("echo-client-cert.pem");
                        _rctx.loadPrivateKeyFile("echo-client-key.pem");
                        return ErrorCodeT();
                    },
                    frame::mpipc::openssl::NameCheckSecureStart{"echo-server"}
                );
            }

            err = mpipcclient.reconfigure(std::move(cfg));

            if(err){
                edbg("starting client mpipcservice: "<<err.message());
                //exiting
                return 1;
            }
        }

        pmpipcclient  = &mpipcclient;

        frame::mpipc::RecipientId       recipient_id;
        frame::mpipc::MessageId     message_id;
        {
            frame::mpipc::MessagePointerT       msgptr(new Message(0));

            err = mpipcclient.sendMessage(
                "localhost", msgptr,
                recipient_id, message_id,
                frame::mpipc::MessageFlags::WaitResponse | frame::mpipc::MessageFlags::OneShotSend
            );
            SOLID_CHECK(not err);
        }

        sleep(5);

        err = mpipcclient.cancelMessage(recipient_id, message_id);

        SOLID_CHECK(err);

        unique_lock<mutex>  lock(mtx);

        while(running){
            //cnd.wait(lock);
            NanoTime    abstime = NanoTime::createRealTime();
            abstime += (120 * 1000);//ten seconds
            cnd.wait(lock);
            bool b = true;//cnd.wait(lock, abstime);
            if(!b){
                //timeout expired
                SOLID_THROW("Process is taking too long.");
            }
        }

        if(crtwriteidx != crtackidx){
            SOLID_THROW("Not all messages were completed");
        }

        m.stop();
    }

    //exiting

    std::cout<<"Transfered size = "<<(transfered_size * 2)/1024<<"KB"<<endl;
    std::cout<<"Transfered count = "<<transfered_count<<endl;
    std::cout<<"Connection count = "<<connection_count<<endl;

    return 0;
}
