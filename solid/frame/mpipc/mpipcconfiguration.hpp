// solid/frame/mpipc/mpipcconfiguration.hpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/frame/aio/aioreactor.hpp"
#include "solid/frame/aio/aioreactorcontext.hpp"
#include "solid/frame/mpipc/mpipcmessage.hpp"
#include "solid/frame/mpipc/mpipcprotocol.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/system/socketaddress.hpp"
#include "solid/system/socketdevice.hpp"
#include "solid/utility/function.hpp"
#include <vector>

#include "solid/frame/mpipc/mpipcsocketstub.hpp"

namespace solid {

namespace frame {

namespace aio {
class Resolver;
struct ObjectProxy;
} // namespace aio

namespace mpipc {

enum struct ConnectionValues : size_t {
    SocketEmplacementSize = 128
};

class Service;
class Connection;
class MessageWriter;
struct ConnectionContext;
struct Configuration;

typedef void (*OnSecureConnectF)(frame::aio::ReactorContext&);
typedef void (*OnSecureAcceptF)(frame::aio::ReactorContext&);

struct BufferBase {
    virtual ~BufferBase();

    char*  data() const { return data_; }
    size_t capacity() const { return capacity_; }

protected:
    BufferBase(char* _data = nullptr, size_t _cap = 0)
        : data_(_data)
        , capacity_(_cap)
    {
    }
    void reset(char* _data = nullptr, size_t _cap = 0)
    {
        data_     = _data;
        capacity_ = _cap;
    }

protected:
    char*  data_;
    size_t capacity_;
};

template <size_t Cp>
struct Buffer;

template <>
struct Buffer<0> : BufferBase {
    Buffer(size_t _cp)
        : BufferBase(new char[_cp], _cp)
    {
    }
    ~Buffer()
    {
        delete[] data_;
    }
};

template <size_t Cp>
struct Buffer : BufferBase {
    char d_[Cp];
    Buffer()
    {
        reset(d_, Cp);
    }
};

using SendBufferPointerT = std::unique_ptr<char[]>;
using RecvBufferPointerT = std::shared_ptr<BufferBase>;

RecvBufferPointerT make_recv_buffer(const size_t _cp);

struct RelayData {
    RecvBufferPointerT bufptr_;
    const char*        pdata_;
    size_t             data_size_;
    RelayData*         pnext_;
    bool               is_last_;
    MessageHeader*     pmessage_header_;

    RelayData()
        : pdata_(nullptr)
        , data_size_(0)
        , pnext_(nullptr)
        , is_last_(false)
        , pmessage_header_(nullptr)
    {
    }
    RelayData(
        RelayData&& _rrelmsg)
        : bufptr_(std::move(_rrelmsg.bufptr_))
        , pdata_(_rrelmsg.pdata_)
        , data_size_(_rrelmsg.data_size_)
        , pnext_(nullptr)
        , is_last_(_rrelmsg.is_last_)
        , pmessage_header_(nullptr)
    {
    }

    RelayData& operator=(RelayData&& _rrelmsg)
    {
        bufptr_          = std::move(_rrelmsg.bufptr_);
        pdata_           = _rrelmsg.pdata_;
        data_size_       = _rrelmsg.data_size_;
        pnext_           = _rrelmsg.pnext_;
        is_last_         = _rrelmsg.is_last_;
        pmessage_header_ = _rrelmsg.pmessage_header_;
        return *this;
    }

    RelayData(const RelayData&) = delete;
    RelayData& operator=(const RelayData&) = delete;

    void clear()
    {
        pdata_     = nullptr;
        data_size_ = 0;
        //connection_id_.clear();
        bufptr_.reset();
        pnext_           = nullptr;
        is_last_         = false;
        pmessage_header_ = nullptr;
    }

private:
    friend class Connection;
    RelayData(
        RecvBufferPointerT& _bufptr,
        const char*         _pdata,
        size_t              _data_size,
        const bool          _is_last)
        : bufptr_(_bufptr)
        , pdata_(_pdata)
        , data_size_(_data_size)
        , pnext_(nullptr)
        , is_last_(_is_last)
        , pmessage_header_(nullptr)
    {
    }
};

enum struct RelayEngineNotification {
    NewData,
    DoneData,
};

class RelayEngine {

protected:
    using PushFunctionT   = SOLID_FUNCTION(bool(RelayData*&, const MessageId&, MessageId&, bool&));
    using DoneFunctionT   = SOLID_FUNCTION(void(RecvBufferPointerT&));
    using CancelFunctionT = SOLID_FUNCTION(void(const MessageHeader&));

    RelayEngine() {}
    virtual ~RelayEngine();

    virtual bool notifyConnection(Manager& _rm, const ObjectIdT& _rrelay_uid, const RelayEngineNotification _what);

private:
    friend class Connection;
    friend struct Configuration;
    friend class Service;

    static RelayEngine& instance();

    virtual void stopConnection(const UniqueId& _rrelay_uid);

    //NOTE: we require _rmsghdr parameter because the relay function
    // will know if it can move it into _rrelay_data.message_header_ (for unicasts)
    // or copy it in case of multicasts
    virtual bool doRelayStart(
        const ObjectIdT& _rcon_uid,
        UniqueId&        _rrelay_uid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _urelay_data,
        MessageId&       _rrelay_id,
        ErrorConditionT& _rerror);

    virtual bool doRelayResponse(
        const UniqueId&  _rrelay_uid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _urelay_data,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror);

    virtual bool doRelay(
        const UniqueId&  _rrelay_uid,
        RelayData&&      _urelay_data,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror);

    virtual void doComplete(
        const UniqueId& _rrelay_uid,
        RelayData* /*_prelay_data*/,
        MessageId const& /*_rengine_msg_id*/,
        bool& /*_rmore*/);

    virtual void doCancel(
        const UniqueId& _rrelay_uid,
        RelayData* /*_prelay_data*/,
        MessageId const& /*_rengine_msg_id*/,
        DoneFunctionT& /*_done_fnc*/);

    virtual void doPollNew(const UniqueId& _rrelay_uid, PushFunctionT& /*_try_push_fnc*/, bool& /*_rmore*/);
    virtual void doPollDone(const UniqueId& _rrelay_uid, DoneFunctionT& /*_done_fnc*/, CancelFunctionT& /*_cancel_fnc*/);

    template <class F>
    void pollNew(const UniqueId& _rrelay_uid, F& _try_push, bool& _rmore)
    {
        PushFunctionT try_push_fnc{std::ref(_try_push)};
        doPollNew(_rrelay_uid, try_push_fnc, _rmore);
    }

    template <class DF, class CF>
    void pollDone(const UniqueId& _rrelay_uid, DF& _done_fnc, CF& _cancel_fnc)
    {
        DoneFunctionT   done_fnc{std::ref(_done_fnc)};
        CancelFunctionT cancel_fnc{std::ref(_cancel_fnc)};

        doPollDone(_rrelay_uid, done_fnc, cancel_fnc);
    }

    void complete(const UniqueId& _rrelay_uid, RelayData* _prelay_data, MessageId const& _rengine_msg_id, bool& _rmore)
    {
        doComplete(_rrelay_uid, _prelay_data, _rengine_msg_id, _rmore);
    }

    template <class DF>
    void cancel(const UniqueId& _rrelay_uid, RelayData* _prelay_data, MessageId const& _rengine_msg_id, DF& _done_fnc)
    {
        DoneFunctionT done_fnc{std::ref(_done_fnc)};
        doCancel(_rrelay_uid, _prelay_data, _rengine_msg_id, done_fnc);
    }

    bool relayStart(
        const ObjectIdT& _rcon_uid,
        UniqueId&        _rrelay_uid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _urelay_data,
        MessageId&       _rrelay_id,
        ErrorConditionT& _rerror)
    {
        return doRelayStart(_rcon_uid, _rrelay_uid, _rmsghdr, std::move(_urelay_data), _rrelay_id, _rerror);
    }

    bool relayResponse(
        const UniqueId&  _rrelay_uid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _urelay_data,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror)
    {
        return doRelayResponse(_rrelay_uid, _rmsghdr, std::move(_urelay_data), _rrelay_id, _rerror);
    }

    bool relay(
        const UniqueId&  _rrelay_uid,
        RelayData&&      _urelay_data,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror)
    {
        return doRelay(_rrelay_uid, std::move(_urelay_data), _rrelay_id, _rerror);
    }
};

using AddressVectorT                            = std::vector<SocketAddressInet>;
using ServerSetupSocketDeviceFunctionT          = SOLID_FUNCTION(bool(SocketDevice&));
using ClientSetupSocketDeviceFunctionT          = SOLID_FUNCTION(bool(SocketDevice&));
using ResolveCompleteFunctionT                  = SOLID_FUNCTION(void(AddressVectorT&&));
using ConnectionStopFunctionT                   = SOLID_FUNCTION(void(ConnectionContext&));
using ConnectionStartFunctionT                  = SOLID_FUNCTION(void(ConnectionContext&));
using SendAllocateBufferFunctionT               = SOLID_FUNCTION(SendBufferPointerT(const uint32_t));
using RecvAllocateBufferFunctionT               = SOLID_FUNCTION(RecvBufferPointerT(const uint32_t));
using CompressFunctionT                         = SOLID_FUNCTION(size_t(char*, size_t, ErrorConditionT&));
using UncompressFunctionT                       = SOLID_FUNCTION(size_t(char*, const char*, size_t, ErrorConditionT&));
using ExtractRecipientNameFunctionT             = SOLID_FUNCTION(const char*(const char*, std::string&, std::string&));
using AioSchedulerT                             = frame::Scheduler<frame::aio::Reactor>;
using ConnectionEnterActiveCompleteFunctionT    = SOLID_FUNCTION(MessagePointerT(ConnectionContext&, ErrorConditionT const&));
using ConnectionEnterPassiveCompleteFunctionT   = SOLID_FUNCTION(void(ConnectionContext&, ErrorConditionT const&));
using ConnectionSecureHandhakeCompleteFunctionT = SOLID_FUNCTION(void(ConnectionContext&, ErrorConditionT const&));
using ConnectionSendRawDataCompleteFunctionT    = SOLID_FUNCTION(void(ConnectionContext&, ErrorConditionT const&));
using ConnectionRecvRawDataCompleteFunctionT    = SOLID_FUNCTION(void(ConnectionContext&, const char*, size_t&, ErrorConditionT const&));
using ConnectionOnEventFunctionT                = SOLID_FUNCTION(void(ConnectionContext&, Event&));

enum struct ConnectionState {
    Raw,
    Passive,
    Active
};

struct ReaderConfiguration {
    ReaderConfiguration();

    size_t   string_size_limit;
    size_t   container_size_limit;
    uint64_t stream_size_limit;

    size_t              max_message_count_multiplex;
    UncompressFunctionT decompress_fnc;
};

struct WriterConfiguration {
    WriterConfiguration();

    size_t max_message_count_multiplex;
    size_t max_message_count_response_wait;
    size_t max_message_continuous_packet_count;

    size_t   string_size_limit;
    size_t   container_size_limit;
    uint64_t stream_size_limit;

    CompressFunctionT inplace_compress_fnc;
};

struct Configuration {
private:
    Configuration& operator=(const Configuration&) = delete;
    Configuration& operator=(Configuration&&) = default;

public:
    template <class P>
    Configuration(
        AioSchedulerT&      _rsch,
        std::shared_ptr<P>& _rprotcol_ptr)
        : pools_mutex_count(16)
        , protocol_ptr(std::static_pointer_cast<Protocol>(_rprotcol_ptr))
        , pscheduler(&_rsch)
        , prelayengine(&RelayEngine::instance())
    {
        init();
    }

    template <class P>
    Configuration(
        AioSchedulerT&      _rsch,
        RelayEngine&        _rrelayengine,
        std::shared_ptr<P>& _rprotcol_ptr)
        : pools_mutex_count(16)
        , protocol_ptr(std::static_pointer_cast<Protocol>(_rprotcol_ptr))
        , pscheduler(&_rsch)
        , prelayengine(&_rrelayengine)
    {
        init();
    }

    Configuration& reset(Configuration&& _ucfg)
    {
        *this = std::move(_ucfg);
        prepare();
        return *this;
    }

    AioSchedulerT& scheduler() const
    {
        return *pscheduler;
    }

    RelayEngine& relayEngine() const
    {
        return *prelayengine;
    }

    bool isServer() const
    {
        return server.listener_address_str.size() != 0;
    }

    bool isClient() const
    {
        return !SOLID_FUNCTION_EMPTY(client.name_resolve_fnc);
    }

    bool isServerOnly() const
    {
        return isServer() && !isClient();
    }

    bool isClientOnly() const
    {
        return !isServer() && isClient();
    }

    void limitString(const size_t _sz)
    {
        reader.string_size_limit = _sz;
        writer.string_size_limit = _sz;
    }

    void limitContainer(const size_t _sz)
    {
        reader.container_size_limit = _sz;
        writer.container_size_limit = _sz;
    }

    void limitStream(const uint64_t _sz)
    {
        reader.stream_size_limit = _sz;
        writer.stream_size_limit = _sz;
    }

    RecvBufferPointerT allocateRecvBuffer(uint8_t& _rbuffer_capacity_kb) const;

    SendBufferPointerT allocateSendBuffer(uint8_t& _rbuffer_capacity_kb) const;

    size_t connectionReconnectTimeoutSeconds(
        const uint8_t _retry_count,
        const bool    _failed_create_connection_object,
        const bool    _last_connection_was_connected,
        const bool    _last_connection_was_active,
        const bool    _last_connection_was_secured) const;

    ErrorConditionT check() const;

    size_t connetionReconnectTimeoutSeconds() const;

    size_t pool_max_active_connection_count;
    size_t pool_max_pending_connection_count;
    size_t pool_max_message_queue_size;

    size_t pools_mutex_count;
    bool   relay_enabled;

    ReaderConfiguration reader;
    WriterConfiguration writer;

    struct Server {
        using ConnectionCreateSocketFunctionT    = SOLID_FUNCTION(SocketStubPtrT(Configuration const&, frame::aio::ObjectProxy const&, SocketDevice&&, char*));
        using ConnectionSecureHandshakeFunctionT = SOLID_FUNCTION(void(ConnectionContext&));

        Server()
            : listener_port(-1)
        {
        }

        bool hasSecureConfiguration() const
        {
            return !secure_any.empty();
        }

        ConnectionCreateSocketFunctionT    connection_create_socket_fnc;
        ConnectionState                    connection_start_state;
        bool                               connection_start_secure;
        ConnectionStartFunctionT           connection_start_fnc;
        ConnectionSecureHandshakeFunctionT connection_on_secure_handshake_fnc;
        ServerSetupSocketDeviceFunctionT   socket_device_setup_fnc;
        std::string                        listener_address_str;
        std::string                        listener_service_str;
        Any<>                              secure_any;

        int listenerPort() const
        {
            return listener_port;
        }

    private:
        friend class Service;
        int listener_port;

    } server;

    struct Client {
        using ConnectionCreateSocketFunctionT    = SOLID_FUNCTION(SocketStubPtrT(Configuration const&, frame::aio::ObjectProxy const&, char*));
        using AsyncResolveFunctionT              = SOLID_FUNCTION(void(const std::string&, ResolveCompleteFunctionT&));
        using ConnectionSecureHandshakeFunctionT = SOLID_FUNCTION(void(ConnectionContext&));

        bool hasSecureConfiguration() const
        {
            return !secure_any.empty();
        }

        ConnectionCreateSocketFunctionT    connection_create_socket_fnc;
        ConnectionState                    connection_start_state;
        bool                               connection_start_secure;
        ConnectionStartFunctionT           connection_start_fnc;
        AsyncResolveFunctionT              name_resolve_fnc;
        ConnectionSecureHandshakeFunctionT connection_on_secure_handshake_fnc;
        ClientSetupSocketDeviceFunctionT   socket_device_setup_fnc;
        Any<>                              secure_any;

    } client;

    size_t                        connection_reconnect_timeout_seconds;
    uint32_t                      connection_inactivity_timeout_seconds;
    uint32_t                      connection_keepalive_timeout_seconds;
    uint32_t                      connection_inactivity_keepalive_count; //server error if receives more than inactivity_keepalive_count keep alive messages during inactivity_timeout_seconds interval
    uint8_t                       connection_recv_buffer_start_capacity_kb;
    uint8_t                       connection_recv_buffer_max_capacity_kb;
    uint8_t                       connection_send_buffer_start_capacity_kb;
    uint8_t                       connection_send_buffer_max_capacity_kb;
    uint16_t                      connection_relay_buffer_count;
    ExtractRecipientNameFunctionT extract_recipient_name_fnc;
    ConnectionStopFunctionT       connection_stop_fnc;
    ConnectionOnEventFunctionT    connection_on_event_fnc;
    RecvAllocateBufferFunctionT   connection_recv_buffer_allocate_fnc;
    SendAllocateBufferFunctionT   connection_send_buffer_allocate_fnc;
    Protocol::PointerT            protocol_ptr;

    Protocol& protocol()
    {
        return *protocol_ptr;
    }

    const Protocol& protocol() const
    {
        return *protocol_ptr;
    }

private:
    void init();
    void prepare();

private:
    AioSchedulerT* pscheduler;
    RelayEngine*   prelayengine;

private:
    friend class Service;
    //friend class MessageWriter;
    Configuration()
        : pscheduler(nullptr)
        , prelayengine(nullptr)
    {
    }
};

struct InternetResolverF {
    aio::Resolver&     rresolver;
    std::string        default_service;
    SocketInfo::Family family;

    InternetResolverF(
        aio::Resolver&     _rresolver,
        const char*        _default_service,
        SocketInfo::Family _family = SocketInfo::AnyFamily)
        : rresolver(_rresolver)
        , default_service(_default_service)
        , family(_family)
    {
    }

    void operator()(const std::string&, ResolveCompleteFunctionT&);
};

} //namespace mpipc
} //namespace frame
} //namespace solid
