// solid/frame/ipc/src/ipcmessagereader.cpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include "mpipcmessagewriter.hpp"
#include "mpipcutility.hpp"

#include "solid/frame/mpipc/mpipcconfiguration.hpp"
#include "solid/frame/mpipc/mpipcerror.hpp"

#include "solid/system/cassert.hpp"
#include "solid/system/debug.hpp"

namespace solid {
namespace frame {
namespace mpipc {
//-----------------------------------------------------------------------------
MessageWriter::MessageWriter()
    : current_message_type_id_(InvalidIndex())
    , current_synchronous_message_idx_(InvalidIndex())
    , order_inner_list_(message_vec_)
    , write_inner_list_(message_vec_)
    , cache_inner_list_(message_vec_)
{
}
//-----------------------------------------------------------------------------
MessageWriter::~MessageWriter() {}
//-----------------------------------------------------------------------------
void MessageWriter::prepare(WriterConfiguration const& _rconfig)
{
    //WARNING: message_vec_ MUST NOT be resized later
    //as it would interfere with pointers stored in serializer.
    message_vec_.reserve(_rconfig.max_message_count_multiplex + _rconfig.max_message_count_response_wait);
    message_vec_.resize(message_vec_.capacity());

    for (size_t i = 0; i < message_vec_.size(); ++i) {
        cache_inner_list_.pushBack(i);
    }
}
//-----------------------------------------------------------------------------
void MessageWriter::unprepare()
{
}
//-----------------------------------------------------------------------------
bool MessageWriter::full(WriterConfiguration const& _rconfig) const
{
    return write_inner_list_.size() >= _rconfig.max_message_count_multiplex;
}
//-----------------------------------------------------------------------------
bool MessageWriter::enqueue(
    WriterConfiguration const& _rconfig,
    MessageBundle&             _rmsgbundle,
    MessageId const&           _rpool_msg_id,
    MessageId&                 _rconn_msg_id)
{

    //see if we can accept the message
    if (full(_rconfig) or cache_inner_list_.empty()) {
        return false;
    }

    //see if we have too many messages waiting for responses
    if (
        Message::is_waiting_response(_rmsgbundle.message_flags) and ((order_inner_list_.size() - write_inner_list_.size()) >= _rconfig.max_message_count_response_wait)) {
        return false;
    }

    SOLID_ASSERT(_rmsgbundle.message_ptr.get());

    //clear all disrupting flags
    _rmsgbundle.message_flags.reset(MessageFlagsE::StartedSend).reset(MessageFlagsE::DoneSend);

    const size_t idx = cache_inner_list_.popFront();
    MessageStub& rmsgstub(message_vec_[idx]);

    rmsgstub.msgbundle_   = std::move(_rmsgbundle);
    rmsgstub.pool_msg_id_ = _rpool_msg_id;

    _rconn_msg_id = MessageId(idx, rmsgstub.unique_);

    order_inner_list_.pushBack(idx);
    write_inner_list_.pushBack(idx);
    vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));

    return true;
}
//-----------------------------------------------------------------------------
bool MessageWriter::enqueue(
    WriterConfiguration const& _rconfig,
    RelayData*                 _prelay_data,
    MessageId const&           _rengine_msg_id,
    MessageId&                 _rconn_msg_id)
{
    if (full(_rconfig) or cache_inner_list_.empty()) {
        return false;
    }

    //see if we have too many messages waiting for responses

    if (_rconn_msg_id.isInvalid()) { //front message data
        if (
            Message::is_waiting_response(_prelay_data->message_header_.flags_) and ((order_inner_list_.size() - write_inner_list_.size()) >= _rconfig.max_message_count_response_wait)) {
            return false;
        }
    }

    const size_t idx = cache_inner_list_.popFront();
    MessageStub& rmsgstub(message_vec_[idx]);

    rmsgstub.prelay_data_ = _prelay_data;
    rmsgstub.pool_msg_id_ = _rengine_msg_id;

    _rconn_msg_id = MessageId(idx, rmsgstub.unique_);

    order_inner_list_.pushBack(idx);
    write_inner_list_.pushBack(idx);
    vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));

    return true;
    ;
}
//-----------------------------------------------------------------------------
void MessageWriter::doUnprepareMessageStub(const size_t _msgidx)
{
    MessageStub& rmsgstub(message_vec_[_msgidx]);

    rmsgstub.clear();
    cache_inner_list_.pushFront(_msgidx);
}
//-----------------------------------------------------------------------------
bool MessageWriter::cancel(
    MessageId const& _rmsguid,
    MessageBundle&   _rmsgbundle,
    MessageId&       _rpool_msg_id)
{
    if (_rmsguid.isValid() and _rmsguid.index < message_vec_.size() and _rmsguid.unique == message_vec_[_rmsguid.index].unique_) {
        return doCancel(_rmsguid.index, _rmsgbundle, _rpool_msg_id);
    }

    return false;
}
//-----------------------------------------------------------------------------
MessagePointerT MessageWriter::fetchRequest(MessageId const& _rmsguid) const
{
    if (_rmsguid.isValid() and _rmsguid.index < message_vec_.size() and _rmsguid.unique == message_vec_[_rmsguid.index].unique_) {
        const MessageStub& rmsgstub = message_vec_[_rmsguid.index];
        return MessagePointerT(rmsgstub.msgbundle_.message_ptr);
    }
    return MessagePointerT();
}
//-----------------------------------------------------------------------------
bool MessageWriter::cancelOldest(
    MessageBundle& _rmsgbundle,
    MessageId&     _rpool_msg_id)
{
    if (order_inner_list_.size()) {
        return doCancel(order_inner_list_.frontIndex(), _rmsgbundle, _rpool_msg_id);
    }
    return false;
}
//-----------------------------------------------------------------------------
bool MessageWriter::doCancel(
    const size_t   _msgidx,
    MessageBundle& _rmsgbundle,
    MessageId&     _rpool_msg_id)
{

    vdbgx(Debug::mpipc, "" << _msgidx);

    MessageStub& rmsgstub = message_vec_[_msgidx];

    if (rmsgstub.isCanceled()) {
        vdbgx(Debug::mpipc, "" << _msgidx << " already canceled");
        return false; //already canceled
    }

    rmsgstub.cancel();

    _rmsgbundle   = std::move(rmsgstub.msgbundle_);
    _rpool_msg_id = rmsgstub.pool_msg_id_;

    order_inner_list_.erase(_msgidx);

    if (rmsgstub.serializer_ptr_.get()) {
        //the message is being sent
        rmsgstub.serializer_ptr_->clear();
    } else if (Message::is_waiting_response(rmsgstub.msgbundle_.message_flags)) {
        //message is waiting response
        doUnprepareMessageStub(_msgidx);
    } else {
        //message is waiting to be sent
        write_inner_list_.erase(_msgidx);
        doUnprepareMessageStub(_msgidx);
    }

    return true;
}
//-----------------------------------------------------------------------------
bool MessageWriter::empty() const
{
    return order_inner_list_.empty();
}
//-----------------------------------------------------------------------------
// Does:
// prepare message
// serialize messages on buffer
// completes messages - those that do not need wait for response
// keeps a serializer for every message that is multiplexed
// compress the filled buffer
//
// Needs:
//

ErrorConditionT MessageWriter::write(
    WriteBuffer&       _rbuffer,
    const WriteFlagsT& _flags,
    uint8_t&           _rackd_buf_count,
    RequestIdVectorT&  _cancel_remote_msg_vec,
    uint8_t&           _rrelay_free_count,
    Sender&            _rsender)
{

    char*           pbufpos = _rbuffer.data();
    char*           pbufend = _rbuffer.end();
    uint32_t        freesz  = _rbuffer.size();
    bool            more    = true;
    ErrorConditionT error;

    while (more and freesz >= (PacketHeader::SizeOfE + _rsender.protocol().minimumFreePacketDataSize())) {

        PacketHeader  packet_header(PacketHeader::MessageTypeE, 0, 0);
        PacketOptions packet_options;
        char*         pbufdata = pbufpos + PacketHeader::SizeOfE;
        size_t        fillsz   = doWritePacketData(pbufdata, pbufend, packet_options, _rackd_buf_count, _cancel_remote_msg_vec, _rrelay_free_count, _rsender, error);

        if (fillsz) {

            if (not packet_options.force_no_compress) {
                ErrorConditionT compress_error;
                size_t          compressed_size = _rsender.configuration().inplace_compress_fnc(pbufdata, fillsz, compress_error);

                if (compressed_size) {
                    packet_header.flags(packet_header.flags() | PacketHeader::CompressedFlagE);
                    fillsz = compressed_size;
                } else if (!compress_error) {
                    //the buffer was not modified, we can send it uncompressed
                } else {
                    //there was an error and the inplace buffer was changed - exit with error
                    more  = false;
                    error = compress_error;
                    break;
                }
            }
            if (packet_options.request_accept) {
                SOLID_ASSERT(_rrelay_free_count != 0);
                vdbgx(Debug::mpipc, "send AckRequestFlagE");
                --_rrelay_free_count;
                packet_header.flags(packet_header.flags() | PacketHeader::Flags::AckRequestFlagE);
                more = false; //do not allow multiple packets per relay buffer
            }

            SOLID_ASSERT(static_cast<size_t>(fillsz) < static_cast<size_t>(0xffffUL));

            packet_header.type(packet_options.packet_type);
            packet_header.size(fillsz);

            pbufpos = packet_header.store(pbufpos, _rsender.protocol());
            pbufpos = pbufdata + fillsz;
            freesz  = pbufend - pbufpos;
        } else {
            more = false;
        }
    }

    if (not error and _rbuffer.data() == pbufpos) {
        if (_flags.has(WriteFlagsE::ShouldSendKeepAlive)) {
            PacketHeader packet_header(PacketHeader::KeepAliveTypeE);
            pbufpos = packet_header.store(pbufpos, _rsender.protocol());
        }
    }
    _rbuffer.resize(pbufpos - _rbuffer.data());
    return error;
}
//-----------------------------------------------------------------------------
bool MessageWriter::doFindEligibleMessage(const bool _can_send_relay, const size_t _size, const bool _fast)
{
    if (_fast) {
        return true; //TODO:
    }
    size_t qsz = write_inner_list_.size();
    while (qsz--) {
        const size_t msgidx   = write_inner_list_.frontIndex();
        MessageStub& rmsgstub = message_vec_[msgidx];

        if (rmsgstub.isHeaderState())
            return true; //prevent splitting the header

        if (rmsgstub.isSynchronous()) {
            if (current_synchronous_message_idx_ != InvalidIndex() or msgidx == current_synchronous_message_idx_) {
            } else {
                write_inner_list_.pushBack(write_inner_list_.popFront());
                continue;
            }
        }

        if (rmsgstub.isRelay()) {
            if (_can_send_relay) {
            } else {
                write_inner_list_.pushBack(write_inner_list_.popFront());
                continue;
            }
        }

        if (rmsgstub.isRelayed()) {
            if (rmsgstub.willRelayedFit(_size)) {
            } else {
                write_inner_list_.pushBack(write_inner_list_.popFront());
                continue;
            }
        }
        return true;
    }
    return false;
}
//-----------------------------------------------------------------------------
// we have three types of messages:
// - direct: serialized onto buffer
// - relay: serialized onto a relay buffer (one that needs confirmation)
// - relayed: copyed onto the output buffer (no serialization)

size_t MessageWriter::doWritePacketData(
    char*             _pbufbeg,
    char*             _pbufend,
    PacketOptions&    _rpacket_options,
    uint8_t&          _rackd_buf_count,
    RequestIdVectorT& _cancel_remote_msg_vec,
    uint8_t           _relay_free_count,
    Sender&           _rsender,
    ErrorConditionT&  _rerror)
{
    SerializerPointerT tmp_serializer;
    char*              pbufpos              = _pbufbeg;
    size_t             packet_message_count = 0;

    if (_rackd_buf_count) {
        vdbgx(Debug::mpipc, "stored ackd_buf_count = " << (int)_rackd_buf_count);
        pbufpos                      = _rsender.protocol().storeValue(pbufpos, _rackd_buf_count);
        _rackd_buf_count             = 0;
        _rpacket_options.packet_type = PacketHeader::AckdCountTypeE;
        ++packet_message_count;
    }

    while (_cancel_remote_msg_vec.size() and (_pbufend - pbufpos) >= _rsender.protocol().minimumFreePacketDataSize()) {
        if (packet_message_count) {
            uint8_t tmp = PacketHeader::CancelRequestTypeE;
            pbufpos     = _rsender.protocol().storeValue(pbufpos, tmp);
        } else {
            _rpacket_options.packet_type = PacketHeader::CancelRequestTypeE;
            ++packet_message_count;
            if (_rackd_buf_count) {
                pbufpos          = _rsender.protocol().storeValue(pbufpos, _rackd_buf_count);
                _rackd_buf_count = 0;
            }
        }
        pbufpos = _rsender.protocol().storeCrossValue(pbufpos, _pbufend - pbufpos, _cancel_remote_msg_vec.back().index);
        SOLID_CHECK(pbufpos != nullptr, "fail store cross value");
        pbufpos = _rsender.protocol().storeCrossValue(pbufpos, _pbufend - pbufpos, _cancel_remote_msg_vec.back().unique);
        SOLID_CHECK(pbufpos != nullptr, "fail store cross value");
        _cancel_remote_msg_vec.pop_back();
    }

    PacketHeader::Types msgtype            = PacketHeader::MessageTypeE;
    bool                fast_find_eligible = false;
    while (
        not _rerror and (_pbufend - pbufpos) >= _rsender.protocol().minimumFreePacketDataSize() and doFindEligibleMessage(_relay_free_count != 0, _pbufend - pbufpos, fast_find_eligible)) {
        const size_t msgidx = write_inner_list_.frontIndex();

        vdbgx(Debug::mpipc, "msgidx = " << msgidx);

        fast_find_eligible = false;

        switch (message_vec_[msgidx].state_) {
        case MessageStub::StateE::WriteStart: {
            MessageStub& rmsgstub = message_vec_[msgidx];

            if (tmp_serializer) {
                rmsgstub.serializer_ptr_ = std::move(tmp_serializer);
            } else {
                rmsgstub.serializer_ptr_ = _rsender.protocol().createSerializer();
            }

            _rsender.protocol().reset(*rmsgstub.serializer_ptr_);

            rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::StartedSend);

            rmsgstub.state_ = MessageStub::StateE::WriteHead;

            vdbgx(Debug::mpipc, "message header url: " << rmsgstub.msgbundle_.message_url);

            rmsgstub.serializer_ptr_->push(rmsgstub.msgbundle_.message_ptr->header_);

            msgtype = PacketHeader::NewMessageTypeE;
        }
        case MessageStub::StateE::WriteHead:
            ++packet_message_count;
            pbufpos            = doWriteMessageHead(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, packet_message_count == 0, msgtype, _rerror);
            fast_find_eligible = true;
            break;
        case MessageStub::StateE::WriteBody:
            ++packet_message_count;
            pbufpos = doWriteMessageBody(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, packet_message_count == 0, msgtype, tmp_serializer, _rerror);
            break;
        case MessageStub::StateE::RelayedStart:
        case MessageStub::StateE::RelayedHead:
        case MessageStub::StateE::RelayedBody:
        case MessageStub::StateE::Canceled:
            ++packet_message_count;
            pbufpos = doWriteMessageCancel(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, packet_message_count == 0, _rerror);
            break;
        }
    } //while

    vdbgx(Debug::mpipc, "write_q_size " << write_inner_list_.size() << " order_q_size " << order_inner_list_.size());

    return pbufpos - _pbufbeg;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageHead(
    char*                     _pbufpos,
    char*                     _pbufend,
    const size_t              _msgidx,
    PacketOptions&            _rpacket_options,
    Sender&                   _rsender,
    const bool                _is_first,
    const PacketHeader::Types _msg_type,
    ErrorConditionT&          _rerror)
{
    if (_is_first) {
        _rpacket_options.packet_type = _msg_type;
    } else {
        uint8_t tmp = static_cast<uint8_t>(_msg_type);
        _pbufpos    = _rsender.protocol().storeValue(_pbufpos, tmp);
    }

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(_pbufpos, static_cast<uint16_t>(0));

    MessageStub& rmsgstub = message_vec_[_msgidx];

    _rsender.context().request_id.index  = (_msgidx + 1);
    _rsender.context().request_id.unique = rmsgstub.unique_;
    _rsender.context().message_flags     = Message::clear_state_flags(rmsgstub.msgbundle_.message_flags) | Message::state_flags(rmsgstub.msgbundle_.message_ptr->flags());
    _rsender.context().message_flags     = Message::update_state_flags(_rsender.context().message_flags);
    _rsender.context().pmessage_url      = &rmsgstub.msgbundle_.message_url;

    const int rv = rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos);

    if (rv >= 0) {
        _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(rv));
        vdbgx(Debug::mpipc, "stored message header with index = " << _msgidx << " and size = " << rv);

        if (rmsgstub.serializer_ptr_->empty()) {
            //we've just finished serializing header

            rmsgstub.serializer_ptr_->push(rmsgstub.msgbundle_.message_ptr, rmsgstub.msgbundle_.message_type_id);

            rmsgstub.state_ = MessageStub::StateE::WriteBody;
        }
        _pbufpos += rv;
    } else {
        _rerror = rmsgstub.serializer_ptr_->error();
    }

    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageBody(
    char*               _pbufpos,
    char*               _pbufend,
    const size_t        _msgidx,
    PacketOptions&      _rpacket_options,
    Sender&             _rsender,
    const bool          _is_first,
    PacketHeader::Types _msg_type,
    SerializerPointerT& _rtmp_serializer,
    ErrorConditionT&    _rerror)
{
    if (_is_first) {
        _rpacket_options.packet_type = _msg_type;
    } else {
        uint8_t tmp = static_cast<uint8_t>(_msg_type);
        _pbufpos    = _rsender.protocol().storeValue(_pbufpos, tmp);
    }

    char* ptypepos = nullptr;

    if (not _is_first) {
        ptypepos = _pbufpos;
        _pbufpos += 1;
    }

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(_pbufpos, static_cast<uint16_t>(0));

    MessageStub& rmsgstub = message_vec_[_msgidx];

    _rsender.context().request_id.index  = (_msgidx + 1);
    _rsender.context().request_id.unique = rmsgstub.unique_;
    _rsender.context().message_flags     = Message::clear_state_flags(rmsgstub.msgbundle_.message_flags) | Message::state_flags(rmsgstub.msgbundle_.message_ptr->flags());
    _rsender.context().message_flags     = Message::update_state_flags(_rsender.context().message_flags);
    _rsender.context().pmessage_url      = &rmsgstub.msgbundle_.message_url;

    const int rv = rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos);

    if (rv >= 0) {

        if (rmsgstub.isRelay()) {
            _rpacket_options.request_accept = true;
        }

        if (rmsgstub.serializer_ptr_->empty()) {
            //we've just finished serializing body
            _msg_type = static_cast<PacketHeader::Types>(_msg_type | PacketHeader::EndMessageTypeFlagE);

            doTryCompleteMessageAfterSerialization(_msgidx, _rsender, _rtmp_serializer, _rerror);
        } else {
            //try reschedule message - so we can multiplex multiple messages on the same stream
            ++rmsgstub.packet_count_;

            if (rmsgstub.packet_count_ >= _rsender.configuration().max_message_continuous_packet_count) {
                if (rmsgstub.isSynchronous()) {
                    current_synchronous_message_idx_ = _msgidx;
                }

                rmsgstub.packet_count_ = 0;
                write_inner_list_.popFront();
                write_inner_list_.pushBack(_msgidx);

                vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));
            }
        }

        if (_is_first) {
            _rpacket_options.packet_type = _msg_type;
        } else {
            uint8_t tmp = static_cast<uint8_t>(_msg_type);
            _pbufpos    = _rsender.protocol().storeValue(ptypepos, tmp);
        }

        //store the data size
        _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(rv));
        vdbgx(Debug::mpipc, "stored message body with index = " << _msgidx << " and size = " << rv);

        _pbufpos += rv;
    } else {
        _rerror = rmsgstub.serializer_ptr_->error();
    }

    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageCancel(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    const bool       _is_first,
    ErrorConditionT& _rerror)
{
    if (_is_first) {
        _rpacket_options.packet_type = PacketHeader::CancelMessageTypeE;
    } else {
        uint8_t tmp = static_cast<uint8_t>(PacketHeader::CancelMessageTypeE);
        _pbufpos    = _rsender.protocol().storeValue(_pbufpos, tmp);
    }
    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    write_inner_list_.erase(_msgidx);
    doUnprepareMessageStub(_msgidx);
    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }
    return _pbufpos;
}
//-----------------------------------------------------------------------------
void MessageWriter::doTryCompleteMessageAfterSerialization(
    const size_t        _msgidx,
    Sender&             _rsender,
    SerializerPointerT& _rtmp_serializer,
    ErrorConditionT&    _rerror)
{

    MessageStub& rmsgstub(message_vec_[_msgidx]);
    RequestId    requid(_msgidx, rmsgstub.unique_);

    vdbgx(Debug::mpipc, "done serializing message " << requid << ". Message id sent to client " << _rsender.context().request_id);

    _rtmp_serializer = std::move(rmsgstub.serializer_ptr_);

    write_inner_list_.popFront();

    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }

    rmsgstub.msgbundle_.message_flags.reset(MessageFlagsE::StartedSend);
    rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::DoneSend);

    rmsgstub.serializer_ptr_ = nullptr;
    rmsgstub.state_          = MessageStub::StateE::WriteStart;

    vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));

    if (not Message::is_waiting_response(rmsgstub.msgbundle_.message_flags)) {
        //no wait response for the message - complete
        MessageBundle tmp_msg_bundle(std::move(rmsgstub.msgbundle_));
        MessageId     tmp_pool_msg_id(rmsgstub.pool_msg_id_);

        order_inner_list_.erase(_msgidx);
        doUnprepareMessageStub(_msgidx);

        _rerror = _rsender.completeMessage(tmp_msg_bundle, tmp_pool_msg_id);

        vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));
    } else {
        rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::WaitResponse);
    }

    vdbgx(Debug::mpipc, MessageWriterPrintPairT(*this, PrintInnerListsE));
}

//-----------------------------------------------------------------------------

void MessageWriter::forEveryMessagesNewerToOlder(VisitFunctionT const& _rvisit_fnc)
{

    { //iterate through non completed messages
        size_t msgidx = order_inner_list_.frontIndex();

        while (msgidx != InvalidIndex()) {
            MessageStub& rmsgstub = message_vec_[msgidx];

            if (not rmsgstub.msgbundle_.message_ptr) {
                SOLID_THROW("invalid message - something went wrong with the nested queue for message: " << msgidx);
                continue;
            }

            const bool message_in_write_queue = not Message::is_waiting_response(rmsgstub.msgbundle_.message_flags);

            _rvisit_fnc(
                rmsgstub.msgbundle_,
                rmsgstub.pool_msg_id_);

            if (not rmsgstub.msgbundle_.message_ptr) {

                if (message_in_write_queue) {
                    write_inner_list_.erase(msgidx);
                }

                const size_t oldidx = msgidx;

                msgidx = order_inner_list_.previousIndex(oldidx);

                order_inner_list_.erase(oldidx);
                doUnprepareMessageStub(oldidx);

            } else {

                msgidx = order_inner_list_.previousIndex(msgidx);
            }
        }
    }
}

//-----------------------------------------------------------------------------

void MessageWriter::print(std::ostream& _ros, const PrintWhat _what) const
{
    if (_what == PrintInnerListsE) {
        _ros << "InnerLists: ";
        auto print_fnc = [&_ros](const size_t _idx, MessageStub const&) { _ros << _idx << ' '; };
        _ros << "OrderList: ";
        order_inner_list_.forEach(print_fnc);
        _ros << '\t';

        _ros << "WriteList: ";
        write_inner_list_.forEach(print_fnc);
        _ros << '\t';

        _ros << "CacheList size: " << cache_inner_list_.size();
        //cache_inner_list_.forEach(print_fnc);
        _ros << '\t';
    }
}

//-----------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& _ros, std::pair<MessageWriter const&, MessageWriter::PrintWhat> const& _msgwriterpair)
{
    _msgwriterpair.first.print(_ros, _msgwriterpair.second);
    return _ros;
}
//-----------------------------------------------------------------------------
} //namespace mpipc
} //namespace frame
} //namespace solid
