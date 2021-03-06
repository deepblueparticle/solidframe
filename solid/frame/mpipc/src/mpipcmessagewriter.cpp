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
#include "solid/system/log.hpp"

namespace solid {
namespace frame {
namespace mpipc {

extern const LoggerT logger;

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
    if (full(_rconfig) || cache_inner_list_.empty()) {
        return false;
    }

    //see if we have too many messages waiting for responses
    if (
        Message::is_waiting_response(_rmsgbundle.message_flags) && ((order_inner_list_.size() - write_inner_list_.size()) >= _rconfig.max_message_count_response_wait)) {
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
    solid_dbg(logger, Verbose, "is_relayed = " << Message::is_relayed(rmsgstub.msgbundle_.message_ptr->flags()) << ' ' << MessageWriterPrintPairT(*this, PrintInnerListsE));

    return true;
}
//-----------------------------------------------------------------------------
bool MessageWriter::enqueue(
    WriterConfiguration const& _rconfig,
    RelayData*&                _rprelay_data,
    MessageId const&           _rengine_msg_id,
    MessageId&                 _rconn_msg_id,
    bool&                      _rmore)
{
    if (full(_rconfig)) {
        _rmore = false;
        return false;
    }

    size_t msgidx;
    //see if we have too many messages waiting for responses

    if (_rconn_msg_id.isInvalid()) { //front message data
        if (cache_inner_list_.empty()) {
            _rmore = false;
            return false;
        }
        if (
            Message::is_waiting_response(_rprelay_data->pmessage_header_->flags_) && ((order_inner_list_.size() - write_inner_list_.size()) >= _rconfig.max_message_count_response_wait)) {
            return false;
        }

        msgidx                      = cache_inner_list_.popFront();
        _rconn_msg_id               = MessageId(msgidx, message_vec_[msgidx].unique_);
        message_vec_[msgidx].state_ = MessageStub::StateE::RelayedStart;
        order_inner_list_.pushBack(msgidx);
    } else {
        msgidx = _rconn_msg_id.index;
        SOLID_ASSERT(message_vec_[msgidx].unique_ == _rconn_msg_id.unique);
        if (message_vec_[msgidx].unique_ != _rconn_msg_id.unique || message_vec_[msgidx].prelay_data_ != nullptr) {
            return false;
        }
    }

    SOLID_ASSERT(_rprelay_data);

    MessageStub& rmsgstub(message_vec_[msgidx]);

    if (_rprelay_data->pdata_ != nullptr) {

        solid_dbg(logger, Verbose, msgidx << " relay_data.is_last " << _rprelay_data->is_last_ << ' ' << MessageWriterPrintPairT(*this, PrintInnerListsE));

        write_inner_list_.pushBack(msgidx);
        rmsgstub.prelay_data_ = _rprelay_data;
        rmsgstub.prelay_pos_  = rmsgstub.prelay_data_->pdata_;
        rmsgstub.relay_size_  = rmsgstub.prelay_data_->data_size_;
        rmsgstub.pool_msg_id_ = _rengine_msg_id;
        _rprelay_data         = nullptr;
    } else if (rmsgstub.state_ < MessageStub::StateE::RelayedWait) {
        SOLID_ASSERT(rmsgstub.relay_size_ == 0);
        solid_dbg(logger, Error, "" << msgidx << " uid = " << rmsgstub.unique_ << " state = " << (int)rmsgstub.state_);
        //called from relay engine on cancel request from the reader (RR - see mpipcrelayengine.cpp) side of the link.
        //after the current function call, the MessageStub in the RelayEngine is distroyed.
        //we need to forward the cancel on the connection
        rmsgstub.state_ = MessageStub::StateE::RelayedCancel;
        write_inner_list_.pushBack(msgidx);
        solid_dbg(logger, Verbose, "relayedcancel msg " << msgidx);
        //we do not need the relay_data - leave it to the relay engine to delete it.
    } else if (rmsgstub.state_ == MessageStub::StateE::RelayedWait) {
        solid_dbg(logger, Verbose, "relayedcancel erase msg " << msgidx << " state = " << (int)rmsgstub.state_);
        //do nothing - we cannot erase a message stub waiting for response
    } else {
        solid_dbg(logger, Verbose, "relayedcancel erase msg " << msgidx << " state = " << (int)rmsgstub.state_);
        order_inner_list_.erase(msgidx);
        doUnprepareMessageStub(msgidx);
    }

    return true;
}
//-----------------------------------------------------------------------------
void MessageWriter::doUnprepareMessageStub(const size_t _msgidx)
{
    MessageStub& rmsgstub(message_vec_[_msgidx]);

    rmsgstub.clear();
    cache_inner_list_.pushFront(_msgidx);
}
//-----------------------------------------------------------------------------
void MessageWriter::cancel(
    MessageId const& _rmsguid,
    Sender&          _rsender,
    const bool       _force)
{
    if (_rmsguid.isValid() && _rmsguid.index < message_vec_.size() && _rmsguid.unique == message_vec_[_rmsguid.index].unique_) {
        doCancel(_rmsguid.index, _rsender, _force);
    }
}
//-----------------------------------------------------------------------------
MessagePointerT MessageWriter::fetchRequest(MessageId const& _rmsguid) const
{
    if (_rmsguid.isValid() && _rmsguid.index < message_vec_.size() && _rmsguid.unique == message_vec_[_rmsguid.index].unique_) {
        const MessageStub& rmsgstub = message_vec_[_rmsguid.index];
        return MessagePointerT(rmsgstub.msgbundle_.message_ptr);
    }
    return MessagePointerT();
}
//-----------------------------------------------------------------------------
ResponseStateE MessageWriter::checkResponseState(MessageId const& _rmsguid, MessageId& _rrelay_id)
{
    if (_rmsguid.isValid() && _rmsguid.index < message_vec_.size() && _rmsguid.unique == message_vec_[_rmsguid.index].unique_) {
        MessageStub& rmsgstub = message_vec_[_rmsguid.index];
        switch (rmsgstub.state_) {
        case MessageStub::StateE::WriteWait:
            return ResponseStateE::Wait;
        case MessageStub::StateE::RelayedWait:
            _rrelay_id = rmsgstub.pool_msg_id_;
            order_inner_list_.erase(_rmsguid.index);
            doUnprepareMessageStub(_rmsguid.index);
            return ResponseStateE::RelayedWait;
        case MessageStub::StateE::WriteCanceled:
            SOLID_ASSERT(write_inner_list_.size());
            order_inner_list_.erase(_rmsguid.index);
            write_inner_list_.erase(_rmsguid.index);
            doUnprepareMessageStub(_rmsguid.index);
            return ResponseStateE::Cancel;
        case MessageStub::StateE::WriteWaitCanceled:
            order_inner_list_.erase(_rmsguid.index);
            doUnprepareMessageStub(_rmsguid.index);
            return ResponseStateE::Cancel;
        default:
            SOLID_ASSERT(false);
            //SOLID_CHECK(false, "Unknown state for response: "<<(int)rmsgstub.state_<<" for messageid: "<<_rmsguid);
            return ResponseStateE::Invalid;
        }
    }
    return ResponseStateE::None;
}
//-----------------------------------------------------------------------------
void MessageWriter::cancelOldest(Sender& _rsender)
{
    if (order_inner_list_.size()) {
        doCancel(order_inner_list_.frontIndex(), _rsender, true);
    }
}
//-----------------------------------------------------------------------------
void MessageWriter::doCancel(
    const size_t _msgidx,
    Sender&      _rsender,
    const bool   _force)
{

    solid_dbg(logger, Verbose, "" << _msgidx);

    MessageStub& rmsgstub = message_vec_[_msgidx];

    if (rmsgstub.state_ == MessageStub::StateE::WriteWaitCanceled) {
        solid_dbg(logger, Verbose, "" << _msgidx << " already canceled");

        if (_force) {
            order_inner_list_.erase(_msgidx);
            doUnprepareMessageStub(_msgidx);
        }
        return; //already canceled
    }

    if (rmsgstub.state_ == MessageStub::StateE::WriteCanceled) {
        solid_dbg(logger, Verbose, "" << _msgidx << " already canceled");

        if (_force) {
            SOLID_ASSERT(write_inner_list_.size());
            order_inner_list_.erase(_msgidx);
            write_inner_list_.erase(_msgidx);
            doUnprepareMessageStub(_msgidx);
        }
        return; //already canceled
    }

    if (rmsgstub.msgbundle_.message_ptr) {
        //called on explicit user request or on peer request (via reader) or on response received
        rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::Canceled);
        _rsender.cancelMessage(rmsgstub.msgbundle_, rmsgstub.pool_msg_id_);

        if (rmsgstub.serializer_ptr_.get()) {
            //the message is being sent
            rmsgstub.serializer_ptr_->clear();
            rmsgstub.state_ = MessageStub::StateE::WriteCanceled;
        } else if (!_force && rmsgstub.state_ == MessageStub::StateE::WriteWait) {
            //message is waiting response
            rmsgstub.state_ = MessageStub::StateE::WriteWaitCanceled;
        } else if (rmsgstub.state_ == MessageStub::StateE::WriteWait || rmsgstub.state_ == MessageStub::StateE::WriteWaitCanceled) {
            order_inner_list_.erase(_msgidx);
            doUnprepareMessageStub(_msgidx);
        } else {
            //message is waiting to be sent
            SOLID_ASSERT(write_inner_list_.size());
            order_inner_list_.erase(_msgidx);
            write_inner_list_.erase(_msgidx);
            doUnprepareMessageStub(_msgidx);
        }
    } else {
        //usually called when reader receives a cancel request
        switch (rmsgstub.state_) {
        case MessageStub::StateE::RelayedHeadStart:
        case MessageStub::StateE::RelayedHeadContinue:
            rmsgstub.serializer_ptr_->clear();
        case MessageStub::StateE::RelayedBody:
            rmsgstub.state_ = MessageStub::StateE::RelayedCancelRequest;
            _rsender.cancelRelayed(rmsgstub.prelay_data_, rmsgstub.pool_msg_id_);
            if (!rmsgstub.prelay_data_) { //message not in write_inner_list_
                write_inner_list_.pushBack(_msgidx);
            }
            rmsgstub.prelay_data_ = nullptr;
            break;
        case MessageStub::StateE::RelayedStart:
        //message not yet started
        case MessageStub::StateE::RelayedWait:
            //message waiting for response
            _rsender.cancelRelayed(rmsgstub.prelay_data_, rmsgstub.pool_msg_id_);
            rmsgstub.prelay_data_ = nullptr;
            order_inner_list_.erase(_msgidx);
            doUnprepareMessageStub(_msgidx);
            break;
        case MessageStub::StateE::RelayedCancelRequest:
            if (_force) {
                write_inner_list_.erase(_msgidx);
                order_inner_list_.erase(_msgidx);
                doUnprepareMessageStub(_msgidx);
                return;
            }
        default:
            SOLID_ASSERT(false);
            return;
        }
    }
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
    size_t          freesz  = _rbuffer.size();
    bool            more    = true;
    ErrorConditionT error;

    while (more && freesz >= (PacketHeader::SizeOfE + _rsender.protocol().minimumFreePacketDataSize())) {

        PacketHeader  packet_header(PacketHeader::TypeE::Data, 0, 0);
        PacketOptions packet_options;
        char*         pbufdata = pbufpos + PacketHeader::SizeOfE;
        size_t        fillsz   = doWritePacketData(pbufdata, pbufend, packet_options, _rackd_buf_count, _cancel_remote_msg_vec, _rrelay_free_count, _rsender, error);

        if (fillsz) {

            if (!packet_options.force_no_compress) {
                ErrorConditionT compress_error;
                size_t          compressed_size = _rsender.configuration().inplace_compress_fnc(pbufdata, fillsz, compress_error);

                if (compressed_size) {
                    packet_header.flags(packet_header.flags() | static_cast<uint8_t>(PacketHeader::FlagE::Compressed));
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
                solid_dbg(logger, Verbose, "send AckRequestFlagE");
                --_rrelay_free_count;
                packet_header.flags(packet_header.flags() | static_cast<uint8_t>(PacketHeader::FlagE::AckRequest));
                more = false; //do not allow multiple packets per relay buffer
            }

            SOLID_ASSERT(static_cast<size_t>(fillsz) < static_cast<size_t>(0xffffUL));

            packet_header.size(static_cast<uint32_t>(fillsz));

            pbufpos = packet_header.store(pbufpos, _rsender.protocol());
            pbufpos = pbufdata + fillsz;
            freesz  = pbufend - pbufpos;
        } else {
            more = false;
        }
    }

    if (!error && _rbuffer.data() == pbufpos) {
        if (_flags.has(WriteFlagsE::ShouldSendKeepAlive)) {
            PacketHeader packet_header(PacketHeader::TypeE::KeepAlive);
            pbufpos = packet_header.store(pbufpos, _rsender.protocol());
        }
    }
    _rbuffer.resize(pbufpos - _rbuffer.data());
    return error;
}
//-----------------------------------------------------------------------------
//NOTE:
// Objectives for doFindEligibleMessage:
// - be fast
// - try to fill up the package
// - be fair with all messages
bool MessageWriter::doFindEligibleMessage(const bool _can_send_relay, const size_t _size)
{
    size_t qsz = write_inner_list_.size();
    while (qsz--) {
        const size_t msgidx   = write_inner_list_.frontIndex();
        MessageStub& rmsgstub = message_vec_[msgidx];

        if (rmsgstub.isHeadState())
            return true; //prevent splitting the header

        if (rmsgstub.isSynchronous()) {
            if (current_synchronous_message_idx_ == InvalidIndex() || msgidx == current_synchronous_message_idx_) {
            } else {
                write_inner_list_.pushBack(write_inner_list_.popFront());
                continue;
            }
        }
        if (rmsgstub.isStartOrHeadState()) {
            return true;
        }
        if (rmsgstub.isRelay()) {
            if (_can_send_relay) {
            } else {
                write_inner_list_.pushBack(write_inner_list_.popFront());
                continue;
            }
        }
        solid_dbg(logger, Verbose, "FOUND eligible message");
        return true;
    }
    solid_dbg(logger, Verbose, "NO eligible message in a queue of " << write_inner_list_.size());
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
    char* pbufpos = _pbufbeg;

    if (_rackd_buf_count) {
        solid_dbg(logger, Verbose, this << " stored ackd_buf_count = " << (int)_rackd_buf_count);
        uint8_t cmd      = static_cast<uint8_t>(PacketHeader::CommandE::AckdCount);
        pbufpos          = _rsender.protocol().storeValue(pbufpos, cmd);
        pbufpos          = _rsender.protocol().storeValue(pbufpos, _rackd_buf_count);
        _rackd_buf_count = 0;
    }

    while (_cancel_remote_msg_vec.size() && static_cast<size_t>(_pbufend - pbufpos) >= _rsender.protocol().minimumFreePacketDataSize()) {
        solid_dbg(logger, Verbose, this << " send CancelRequest " << _cancel_remote_msg_vec.back());
        uint8_t cmd = static_cast<uint8_t>(PacketHeader::CommandE::CancelRequest);
        pbufpos     = _rsender.protocol().storeValue(pbufpos, cmd);
        pbufpos     = _rsender.protocol().storeCrossValue(pbufpos, _pbufend - pbufpos, _cancel_remote_msg_vec.back().index);
        SOLID_CHECK(pbufpos != nullptr, "fail store cross value");
        pbufpos = _rsender.protocol().storeCrossValue(pbufpos, _pbufend - pbufpos, _cancel_remote_msg_vec.back().unique);
        SOLID_CHECK(pbufpos != nullptr, "fail store cross value");
        _cancel_remote_msg_vec.pop_back();
    }

    while (
        !_rerror && static_cast<size_t>(_pbufend - pbufpos) >= _rsender.protocol().minimumFreePacketDataSize() && doFindEligibleMessage(_relay_free_count != 0, _pbufend - pbufpos)) {
        const size_t msgidx = write_inner_list_.frontIndex();

        PacketHeader::CommandE cmd = PacketHeader::CommandE::Message;

        solid_dbg(logger, Verbose, this << " msgidx = " << msgidx << " state " << (int)message_vec_[msgidx].state_);

        switch (message_vec_[msgidx].state_) {
        case MessageStub::StateE::WriteStart: {
            MessageStub& rmsgstub = message_vec_[msgidx];

            rmsgstub.serializer_ptr_ = createSerializer(_rsender);

            rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::StartedSend);

            rmsgstub.state_ = MessageStub::StateE::WriteHeadStart;

            solid_dbg(logger, Verbose, this << " message header url: " << rmsgstub.msgbundle_.message_url);

            cmd = PacketHeader::CommandE::NewMessage;
        }
        case MessageStub::StateE::WriteHeadStart:
        case MessageStub::StateE::WriteHeadContinue:
            pbufpos = doWriteMessageHead(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, cmd, _rerror);
            break;
        case MessageStub::StateE::WriteBodyStart:
        case MessageStub::StateE::WriteBodyContinue:
            pbufpos = doWriteMessageBody(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, _rerror);
            break;
        case MessageStub::StateE::WriteWait:
            SOLID_THROW("Invalid state for write queue - WriteWait");
            break;
        case MessageStub::StateE::WriteCanceled:
            pbufpos = doWriteMessageCancel(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, _rerror);
            break;
        case MessageStub::StateE::RelayedStart: {
            MessageStub& rmsgstub = message_vec_[msgidx];

            rmsgstub.serializer_ptr_ = createSerializer(_rsender);

            rmsgstub.state_ = MessageStub::StateE::RelayedHeadStart;

            solid_dbg(logger, Verbose, this << " message header url: " << rmsgstub.prelay_data_->pmessage_header_->url_);

            cmd = PacketHeader::CommandE::NewMessage;
        }
        case MessageStub::StateE::RelayedHeadStart:
        case MessageStub::StateE::RelayedHeadContinue:
            pbufpos = doWriteRelayedHead(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, cmd, _rerror);
            break;
        case MessageStub::StateE::RelayedBody:
            pbufpos = doWriteRelayedBody(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, _rerror);
            break;
        case MessageStub::StateE::RelayedWait:
            SOLID_THROW("Invalid state for write queue - RelayedWait");
            break;
        case MessageStub::StateE::RelayedCancelRequest:
            pbufpos = doWriteRelayedCancelRequest(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, _rerror);
            break;
        case MessageStub::StateE::RelayedCancel:
            pbufpos = doWriteRelayedCancel(pbufpos, _pbufend, msgidx, _rpacket_options, _rsender, _rerror);
            break;
        default:
            //SOLID_CHECK(false, "message state not handled: "<<(int)message_vec_[msgidx].state_<<" for message "<<msgidx);
            SOLID_ASSERT(false);
            break;
        }
    } //while

    solid_dbg(logger, Verbose, this << " write_q_size " << write_inner_list_.size() << " order_q_size " << order_inner_list_.size());

    return pbufpos - _pbufbeg;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageHead(
    char*                        _pbufpos,
    char*                        _pbufend,
    const size_t                 _msgidx,
    PacketOptions&               _rpacket_options,
    Sender&                      _rsender,
    const PacketHeader::CommandE _cmd,
    ErrorConditionT&             _rerror)
{

    uint8_t cmd = static_cast<uint8_t>(_cmd);
    _pbufpos    = _rsender.protocol().storeValue(_pbufpos, cmd);

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(0));

    MessageStub& rmsgstub             = message_vec_[_msgidx];
    rmsgstub.msgbundle_.message_flags = Message::update_state_flags(Message::clear_state_flags(rmsgstub.msgbundle_.message_flags) | Message::state_flags(rmsgstub.msgbundle_.message_ptr->flags()));

    _rsender.context().request_id.index  = static_cast<uint32_t>(_msgidx + 1);
    _rsender.context().request_id.unique = rmsgstub.unique_;
    _rsender.context().message_flags     = rmsgstub.msgbundle_.message_flags;
    _rsender.context().pmessage_url      = &rmsgstub.msgbundle_.message_url;

    const long rv   = rmsgstub.state_ == MessageStub::StateE::WriteHeadStart ? rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos, rmsgstub.msgbundle_.message_ptr->header_) : rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos);
    rmsgstub.state_ = MessageStub::StateE::WriteHeadContinue;

    if (rv >= 0) {
        _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(rv));
        solid_dbg(logger, Verbose, "stored message header with index = " << _msgidx << " and size = " << rv);

        if (rmsgstub.serializer_ptr_->empty()) {
            //we've just finished serializing header
            rmsgstub.state_ = MessageStub::StateE::WriteBodyStart;
        }
        _pbufpos += rv;
    } else {
        _rerror = rmsgstub.serializer_ptr_->error();
    }

    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageBody(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{
    char* pcmdpos = nullptr;

    pcmdpos = _pbufpos;
    _pbufpos += 1;

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(0));
    uint8_t cmd    = static_cast<uint8_t>(PacketHeader::CommandE::Message);

    MessageStub& rmsgstub = message_vec_[_msgidx];

    _rsender.context().request_id.index  = static_cast<uint32_t>(_msgidx + 1);
    _rsender.context().request_id.unique = rmsgstub.unique_;
    _rsender.context().message_flags     = rmsgstub.msgbundle_.message_flags;
    _rsender.context().pmessage_url      = &rmsgstub.msgbundle_.message_url;

    const long rv   = rmsgstub.state_ == MessageStub::StateE::WriteBodyStart ? rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos, rmsgstub.msgbundle_.message_ptr, rmsgstub.msgbundle_.message_type_id) : rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos);
    rmsgstub.state_ = MessageStub::StateE::WriteBodyContinue;

    if (rv >= 0) {

        if (rmsgstub.isRelay()) {
            _rpacket_options.request_accept = true;
        }

        if (rmsgstub.serializer_ptr_->empty()) {
            //we've just finished serializing body
            cmd |= static_cast<uint8_t>(PacketHeader::CommandE::EndMessageFlag);

            doTryCompleteMessageAfterSerialization(_msgidx, _rsender, _rerror);
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

                solid_dbg(logger, Verbose, MessageWriterPrintPairT(*this, PrintInnerListsE));
            }
        }

        _rsender.protocol().storeValue(pcmdpos, cmd);

        //store the data size
        _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(rv));
        solid_dbg(logger, Verbose, "stored message body with index = " << _msgidx << " and size = " << rv << " cmd = " << (int)cmd);

        _pbufpos += rv;
    } else {
        _rerror = rmsgstub.serializer_ptr_->error();
    }

    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteRelayedHead(
    char*                        _pbufpos,
    char*                        _pbufend,
    const size_t                 _msgidx,
    PacketOptions&               _rpacket_options,
    Sender&                      _rsender,
    const PacketHeader::CommandE _cmd,
    ErrorConditionT&             _rerror)
{
    uint8_t cmd = static_cast<uint8_t>(_cmd);
    _pbufpos    = _rsender.protocol().storeValue(_pbufpos, cmd);

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(0));

    MessageStub& rmsgstub = message_vec_[_msgidx];

    _rsender.context().request_id.index  = static_cast<uint32_t>(_msgidx + 1);
    _rsender.context().request_id.unique = rmsgstub.unique_;
    _rsender.context().message_flags     = rmsgstub.prelay_data_->pmessage_header_->flags_;
    _rsender.context().message_flags.set(MessageFlagsE::Relayed);
    _rsender.context().pmessage_url = &rmsgstub.prelay_data_->pmessage_header_->url_;

    const long rv   = rmsgstub.state_ == MessageStub::StateE::RelayedHeadStart ? rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos, *rmsgstub.prelay_data_->pmessage_header_) : rmsgstub.serializer_ptr_->run(_rsender.context(), _pbufpos, _pbufend - _pbufpos);
    rmsgstub.state_ = MessageStub::StateE::RelayedHeadContinue;

    if (rv >= 0) {
        _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(rv));
        solid_dbg(logger, Verbose, "stored message header with index = " << _msgidx << " and size = " << rv);

        if (rmsgstub.serializer_ptr_->empty()) {
            //we've just finished serializing header
            cache(rmsgstub.serializer_ptr_);
            rmsgstub.state_ = MessageStub::StateE::RelayedBody;
        }
        _pbufpos += rv;
    } else {
        cache(rmsgstub.serializer_ptr_);
        _rerror = rmsgstub.serializer_ptr_->error();
    }

    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteRelayedBody(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{
    char* pcmdpos = nullptr;

    pcmdpos = _pbufpos;
    _pbufpos += 1;

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    char* psizepos = _pbufpos;
    _pbufpos       = _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(0));
    uint8_t cmd    = static_cast<uint8_t>(PacketHeader::CommandE::Message);

    MessageStub& rmsgstub = message_vec_[_msgidx];

    SOLID_ASSERT(rmsgstub.prelay_data_);

    size_t towrite = _pbufend - _pbufpos;
    if (towrite > rmsgstub.relay_size_) {
        towrite = rmsgstub.relay_size_;
    }

    memcpy(_pbufpos, rmsgstub.prelay_pos_, towrite);

    _pbufpos += towrite;
    rmsgstub.prelay_pos_ += towrite;
    rmsgstub.relay_size_ -= towrite;

    solid_dbg(logger, Verbose, "storing " << towrite << " bytes"
                                          << " for msg " << _msgidx << " cmd = " << (int)cmd << " is_last = " << rmsgstub.prelay_data_->is_last_ << " relaydata = " << rmsgstub.prelay_data_);

    if (rmsgstub.relay_size_ == 0) {
        SOLID_ASSERT(write_inner_list_.size());
        write_inner_list_.erase(_msgidx); //call before _rsender.pollRelayEngine

        const bool is_last                 = rmsgstub.prelay_data_->is_last_;
        const bool is_waiting_for_response = Message::is_waiting_response(rmsgstub.prelay_data_->pmessage_header_->flags_);

        _rsender.completeRelayed(rmsgstub.prelay_data_, rmsgstub.pool_msg_id_);
        rmsgstub.prelay_data_ = nullptr; //when prelay_data_ is null we consider the message not in write_inner_list_

        if (is_last) {
            cmd |= static_cast<uint8_t>(PacketHeader::CommandE::EndMessageFlag);
            if (is_waiting_for_response) {
                rmsgstub.state_ = MessageStub::StateE::RelayedWait;
            } else {
                order_inner_list_.erase(_msgidx);
                rmsgstub.clear();
            }
        }
    }

    _rsender.protocol().storeValue(pcmdpos, cmd);

    //store the data size
    _rsender.protocol().storeValue(psizepos, static_cast<uint16_t>(towrite));
    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteMessageCancel(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{

    solid_dbg(logger, Verbose, "" << _msgidx);

    uint8_t cmd = static_cast<uint8_t>(PacketHeader::CommandE::CancelMessage);
    _pbufpos    = _rsender.protocol().storeValue(_pbufpos, cmd);

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    SOLID_ASSERT(write_inner_list_.size());
    write_inner_list_.erase(_msgidx);
    order_inner_list_.erase(_msgidx);
    doUnprepareMessageStub(_msgidx);
    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }
    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteRelayedCancel(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{
    MessageStub& rmsgstub = message_vec_[_msgidx];

    solid_dbg(logger, Error, "" << _msgidx << " uid = " << rmsgstub.unique_ << " state = " << (int)rmsgstub.state_);

    uint8_t cmd = static_cast<uint8_t>(PacketHeader::CommandE::CancelMessage);
    _pbufpos    = _rsender.protocol().storeValue(_pbufpos, cmd);

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");
    SOLID_ASSERT(rmsgstub.prelay_data_ == nullptr);
    //_rsender.completeRelayed(rmsgstub.prelay_data_, rmsgstub.pool_msg_id_);
    //rmsgstub.prelay_data_ = nullptr;

    SOLID_ASSERT(write_inner_list_.size());
    write_inner_list_.erase(_msgidx);
    order_inner_list_.erase(_msgidx);
    doUnprepareMessageStub(_msgidx);
    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }
    return _pbufpos;
}
//-----------------------------------------------------------------------------
char* MessageWriter::doWriteRelayedCancelRequest(
    char*            _pbufpos,
    char*            _pbufend,
    const size_t     _msgidx,
    PacketOptions&   _rpacket_options,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{

    solid_dbg(logger, Verbose, "" << _msgidx);

    uint8_t cmd = static_cast<uint8_t>(PacketHeader::CommandE::CancelMessage);
    _pbufpos    = _rsender.protocol().storeValue(_pbufpos, cmd);

    _pbufpos = _rsender.protocol().storeCrossValue(_pbufpos, _pbufend - _pbufpos, static_cast<uint32_t>(_msgidx));
    SOLID_CHECK(_pbufpos != nullptr, "fail store cross value");

    SOLID_ASSERT(write_inner_list_.size());
    write_inner_list_.erase(_msgidx);
    order_inner_list_.erase(_msgidx);
    doUnprepareMessageStub(_msgidx);
    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }
    return _pbufpos;
}
//-----------------------------------------------------------------------------
void MessageWriter::doTryCompleteMessageAfterSerialization(
    const size_t     _msgidx,
    Sender&          _rsender,
    ErrorConditionT& _rerror)
{

    MessageStub& rmsgstub(message_vec_[_msgidx]);
    RequestId    requid(static_cast<uint32_t>(_msgidx), rmsgstub.unique_);

    solid_dbg(logger, Verbose, "done serializing message " << requid << ". Message id sent to client " << _rsender.context().request_id);

    cache(rmsgstub.serializer_ptr_);

    SOLID_ASSERT(write_inner_list_.size());
    write_inner_list_.popFront();

    if (current_synchronous_message_idx_ == _msgidx) {
        current_synchronous_message_idx_ = InvalidIndex();
    }

    rmsgstub.msgbundle_.message_flags.reset(MessageFlagsE::StartedSend);
    rmsgstub.msgbundle_.message_flags.set(MessageFlagsE::DoneSend);

    rmsgstub.serializer_ptr_ = nullptr;
    rmsgstub.state_          = MessageStub::StateE::WriteStart;

    solid_dbg(logger, Verbose, MessageWriterPrintPairT(*this, PrintInnerListsE));

    if (!Message::is_waiting_response(rmsgstub.msgbundle_.message_flags)) {
        //no wait response for the message - complete
        MessageBundle tmp_msg_bundle(std::move(rmsgstub.msgbundle_));
        MessageId     tmp_pool_msg_id(rmsgstub.pool_msg_id_);

        order_inner_list_.erase(_msgidx);
        doUnprepareMessageStub(_msgidx);

        _rerror = _rsender.completeMessage(tmp_msg_bundle, tmp_pool_msg_id);

        solid_dbg(logger, Verbose, MessageWriterPrintPairT(*this, PrintInnerListsE));
    } else {
        rmsgstub.state_ = MessageStub::StateE::WriteWait;
    }

    solid_dbg(logger, Verbose, MessageWriterPrintPairT(*this, PrintInnerListsE));
}

//-----------------------------------------------------------------------------
void MessageWriter::forEveryMessagesNewerToOlder(VisitFunctionT const& _rvisit_fnc)
{

    solid_dbg(logger, Verbose, "");
    size_t msgidx = order_inner_list_.backIndex();

    while (msgidx != InvalidIndex()) {
        MessageStub& rmsgstub = message_vec_[msgidx];

        const bool message_in_write_queue = !rmsgstub.isWaitResponseState();

        if (rmsgstub.msgbundle_.message_ptr) { //skip relayed messages
            _rvisit_fnc(
                rmsgstub.msgbundle_,
                rmsgstub.pool_msg_id_);

            if (!rmsgstub.msgbundle_.message_ptr) { //message fetched

                if (message_in_write_queue) {
                    SOLID_ASSERT(write_inner_list_.size());
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
    } //while
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

void MessageWriter::cache(Serializer::PointerT& _ser)
{
    if (_ser) {
        _ser->clear();
        _ser->link(ser_top_);
        ser_top_ = std::move(_ser);
    }
}

//-----------------------------------------------------------------------------

Serializer::PointerT MessageWriter::createSerializer(Sender& _sender)
{
    if (ser_top_) {
        Serializer::PointerT ser{std::move(ser_top_)};
        ser_top_ = std::move(ser->link());
        _sender.protocol().reconfigure(*ser, _sender.configuration());
        return ser;
    } else {
        return _sender.protocol().createSerializer(_sender.configuration());
    }
}

//-----------------------------------------------------------------------------

/*virtual*/ MessageWriter::Sender::~Sender()
{
}
/*virtual*/ ErrorConditionT MessageWriter::Sender::completeMessage(MessageBundle& /*_rmsgbundle*/, MessageId const& /*_rmsgid*/)
{
    return ErrorConditionT{};
}
/*virtual*/ void MessageWriter::Sender::completeRelayed(RelayData* _relay_data, MessageId const& _rmsgid)
{
}
/*virtual*/ void MessageWriter::Sender::cancelMessage(MessageBundle& /*_rmsgbundle*/, MessageId const& /*_rmsgid*/)
{
}
/*virtual*/ void MessageWriter::Sender::cancelRelayed(RelayData* _relay_data, MessageId const& _rmsgid)
{
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
