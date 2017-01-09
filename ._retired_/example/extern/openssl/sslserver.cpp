#include <iostream>
#include <vector>
#include "system/debug.hpp"
#include "openssl/bio.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "system/socketaddress.hpp"
#include "system/socketdevice.hpp"
#include <sys/epoll.h>
#include "utility/queue.hpp"

using namespace std;
using namespace solid;

enum{
    AsyncError = -1,
    AsyncSuccess = 0,
    AsyncWait,
};

struct Handle{
    enum{
        BufferCapacity = 1024,
        Init,
        Banner,
        DoIO
    };
    Handle(SocketDevice &_sd):eevents(0), events(0), sd(std::move(_sd)), ssl(NULL){
        wait_read_on_write = false;
        wait_read_on_read = false;
        wait_write_on_write = false;
        wait_write_on_read = false;
        sevents = 0;
        state = Init;
        doread = false;
        dowrite = false;
    }
    void clearWaitWrite(){
        wait_read_on_write = false;
        wait_write_on_write = false;
    }
    void clearWaitRead(){
        wait_read_on_read = false;
        wait_write_on_read = false;
    }
    bool shouldWait(){
        return SSL_want(ssl) != SSL_NOTHING;
    }
    void setWaitWrite(){
        if(SSL_want_read(ssl)){
            wait_read_on_write = true;
        }
        if(SSL_want_write(ssl)){
            wait_write_on_write = true;
        }
    }
    void setWaitRead(){
        if(SSL_want_read(ssl)){
            wait_read_on_read = true;
        }
        if(SSL_want_write(ssl)){
            wait_write_on_read = true;
        }
    }
    int write(const char *_pb, const unsigned _bl){
        return SSL_write(ssl, _pb, _bl);
    }
    int read(char *_pb, const unsigned _bl){
        return SSL_read(ssl, _pb, _bl);
    }
    int ssl_accept(){
        return SSL_accept(ssl);
    }
    void setExpectedEvents(){
        eevents = 0;
        if(wait_read_on_write || wait_read_on_read){
            eevents = EPOLLIN;
        }
        if(wait_write_on_write || wait_write_on_read){
            eevents = EPOLLOUT;
        }
    }
    uint32_t                    eevents;//expected events
    uint32_t                    sevents;//set events
    uint32_t                    events;//incomming events

    bool                    wait_read_on_write;
    bool                    wait_read_on_read;
    bool                    wait_write_on_write;
    bool                    wait_write_on_read;
    bool                    doread;
    bool                    dowrite;
    int                     state;
    SocketDevice            sd;
    SSL                     *ssl;
    //we use multiple buffers to be able to
    //test synchrounous read and writes
    char                    buf[2][BufferCapacity];
    int                     len[2];
    char                    *pwbuf[2];
    Queue<solid::uint>      readbufs;
    Queue<solid::uint>      writebufs;
};

typedef std::vector<Handle> HandleVectorT;
HandleVectorT       handles;
int                 epollfd;
Queue<uint32_t>         execq;
SSL_CTX             *ctx;


int executeConnection(uint32_t _pos);
int executeListener();

//a simple nonblocking echo server using sslbio

int main(int argc, char* argv[]){
    if(argc != 3){
        cout<<"Usage:\n./sslserver addr port"<<endl;
        return 0;
    }
#ifdef SOLID_HAS_DEBUG
    string s;
    Debug::the().levelMask("iew");
    Debug::the().moduleMask();
    Debug::the().initStdErr(false, &s);

    cout<<"Debug output: "<<s<<endl;
    s.clear();
    Debug::the().moduleNames(s);
    cout<<"Debug bits: "<<s<<endl;
#endif
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();


    ctx = SSL_CTX_new (SSLv23_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(2);
    }
    const char *pcertpath = OSSL_SOURCE_PATH"ssl_/certs/A-server.pem";
    cout<<"Client certificate path: "<<pcertpath<<endl;

    if (SSL_CTX_use_certificate_file(ctx, pcertpath, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(3);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, pcertpath, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(4);
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr,"Private key does not match the certificate public key\n");
        exit(5);
    }

    //create a connection
    handles.reserve(2048);
    ResolveData rd = synchronous_resolve(argv[1], argv[2]);

    if(rd.empty()){
        cout<<"no such address"<<endl;
        return 0;
    }
    typedef std::vector<Handle> HandleVectorT;
    const unsigned epoll_cp = 4096;
    epoll_event         events[epoll_cp];
    epollfd = epoll_create(epoll_cp);
    {
        SocketDevice sd;
        sd.create(rd.begin());
        sd.prepareAccept(rd.begin());
        sd.makeNonBlocking();
        if(!sd.ok()){
            cout<<"Error preparing accept"<<endl;
            return 0;
        }
        handles.push_back(Handle(sd));
        epoll_event ev;
        ev.data.u32 = 0;
        ev.events = EPOLLIN;//must be LevelTriggered
        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, handles[0].sd.descriptor(), &ev)){
            edbg("epoll_ctl: "<<strerror(errno));
            SOLID_ASSERT(false);
            return AsyncError;
        }
    }
    int selected = 0;
    while(true){
        for(int i = 0; i < selected; ++i){
            handles[events[i].data.u32].events |= events[i].events;
            execq.push(events[i].data.u32);
        }
        uint32_t qsz = execq.size();
        while(qsz--){
            uint32_t pos = execq.front();execq.pop();
            if(pos){
                if(executeConnection(pos) == AsyncSuccess){
                    execq.push(pos);
                }
            }else{
                if(executeListener() == AsyncSuccess){
                    execq.push(pos);
                }
            }
        }
        selected = epoll_wait(epollfd, events, handles.size(), execq.size() ? 0 : -1);
        if(selected < 0) selected = 0;
    }
    return 0;
}

const char          *echo_str = "Hello from echo server\r\n";
const solid::uint   echo_len = strlen(echo_str);

int executeConnection(uint32_t _pos){
    Handle &h(handles[_pos]);
    int rv = 0;
    int retval = AsyncWait;
    switch(h.state){
        case Handle::Init:
            rv = h.ssl_accept();
            if(rv == 0) return AsyncError;
            if(rv > 0){
                h.state = Handle::Banner;
                h.clearWaitRead();
            }else{
                //timeout
                if(h.shouldWait()){
                    h.setWaitRead();
                }else return AsyncError;
                break;
            }
        case Handle::Banner:
            rv = h.write(echo_str, echo_len);
            if(rv == echo_len){
                h.state = Handle::DoIO;
                h.doread = true;
                retval = AsyncSuccess;
                h.readbufs.push(0);
                h.readbufs.push(1);
            }else if(h.shouldWait()){
                SOLID_ASSERT(rv > 0);
                h.setWaitWrite();
                retval = AsyncWait;
            }else return AsyncError;
            break;
        case Handle::DoIO:{
                if(h.events & EPOLLIN){
                    h.doread = h.doread || h.wait_read_on_read;
                    h.dowrite = h.dowrite || h.wait_read_on_write;
                }
                if(h.events & EPOLLOUT){
                    h.doread = h.doread || h.wait_write_on_read;
                    h.dowrite = h.dowrite || h.wait_write_on_write;
                }
                if(h.doread){
                    h.doread = false;
                    rv = h.read(h.buf[h.readbufs.front()], Handle::BufferCapacity);
                    if(rv > 0){
                        h.writebufs.push(h.readbufs.front());
                        h.len[h.readbufs.front()] = rv;
                        h.pwbuf[h.readbufs.front()] = h.buf[h.readbufs.front()];
                        h.readbufs.pop();
                        //if we dont wait for something on write, force write
                        if(!h.wait_write_on_write && !h.wait_read_on_write)
                            h.dowrite = true;
                        if(h.readbufs.size())
                            h.doread = true;
                    }else if(h.shouldWait()){
                        h.setWaitRead();
                    }else return AsyncError;
                }
                if(h.dowrite){
                    h.dowrite = false;
                    int &len = h.len[h.writebufs.front()];
                    rv = h.write(h.pwbuf[h.writebufs.front()], len);
                    if(rv > 0){
                        if(rv == len){
                            h.readbufs.push(h.writebufs.front());
                            h.writebufs.pop();
                            if(h.readbufs.size() == 1){
                                h.doread = true;
                            }
                            if(h.writebufs.size()){
                                h.dowrite = true;
                            }
                        }else{
                            h.dowrite = true;//continue writing
                            h.pwbuf[h.writebufs.front()] += rv;
                            len -= rv;
                        }
                    }else if(h.shouldWait()){
                        h.setWaitWrite();
                    }else return AsyncError;
                }
            }break;
    }
    h.events = 0;
    h.setExpectedEvents();
    if(h.eevents != h.sevents){
        h.sevents = h.eevents;
        epoll_event ev;
        ev.data.u32 = _pos;
        ev.events = h.sevents;
        epoll_ctl(epollfd, EPOLL_CTL_MOD, h.sd.descriptor(), &ev);
    }
    if(h.doread || h.dowrite) return AsyncSuccess;
    return retval;
}
int executeListener(){
    Handle          &h(handles[0]);
    SocketDevice    sd;
    bool            can_retry = false;
    while(!h.sd.accept(sd, can_retry)){
        epoll_event ev;
        ev.data.u32 = handles.size();
        sd.makeNonBlocking();
        handles.push_back(Handle(sd));
        execq.push(ev.data.u32);
        handles.back().eevents = 0;
        handles.back().ssl = SSL_new(ctx);
        SSL_set_fd(handles.back().ssl, handles.back().sd.descriptor());
        ev.events = 0;
        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, handles.back().sd.descriptor(), &ev)){
            edbg("epoll_ctl: "<<strerror(errno));
            SOLID_ASSERT(false);
            return AsyncError;
        }
    }
    return AsyncWait;
}

