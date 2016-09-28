// solid/frame/ipc/src/ipcmessagereader.hpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SOLID_FRAME_MPIPC_SRC_MPIPC_MESSAGE_READER_HPP
#define SOLID_FRAME_MPIPC_SRC_MPIPC_MESSAGE_READER_HPP

#include "solid/utility/queue.hpp"
#include "solid/system/common.hpp"
#include "solid/system/error.hpp"
#include "solid/frame/mpipc/mpipcprotocol.hpp"

namespace solid{
namespace frame{
namespace mpipc{

struct ReaderConfiguration;

struct PacketHeader;

class MessageReader{
public:
	enum Events{
		MessageCompleteE,
		KeepaliveCompleteE,
	};
	using CompleteFunctionT = FUNCTION<void(const Events, MessagePointerT /*const*/&, const size_t)>;
	
	MessageReader();
	
	~MessageReader();
	
	uint32_t read(
		const char *_pbuf,
		uint32_t _bufsz,
		CompleteFunctionT &_complete_fnc,
		ReaderConfiguration const &_rconfig,
		Protocol const &_rproto,
		ConnectionContext &_rctx,
		ErrorConditionT &_rerror
	);
	
	void prepare(ReaderConfiguration const &_rconfig);
	void unprepare();
private:
	void doConsumePacket(
		const char *_pbuf,
		PacketHeader const &_packet_header,
		CompleteFunctionT &_complete_fnc,
		ReaderConfiguration const &_rconfig,
		Protocol const &_rproto,
		ConnectionContext &_rctx,
		ErrorConditionT &_rerror
	);
private:
	
	enum States{
		HeaderReadStateE = 1,
		DataReadStateE,
	};
	
	struct MessageStub{
		MessageStub(
			MessagePointerT &_rmsgptr,
// 			const size_t _msg_type_idx,
			ulong _flags
		): message_ptr(std::move(_rmsgptr)), /*message_type_idx(_msg_type_idx),*/ packet_count(0){}
		
		MessageStub():/*message_type_idx(InvalidIndex()), */packet_count(0){}
		
		void clear(){
			deserializer_ptr = nullptr;
			message_ptr.reset();
// 			message_type_idx = InvalidIndex();
		}
		
		MessagePointerT 		message_ptr;
		//size_t					message_type_idx;
		DeserializerPointerT	deserializer_ptr;
		size_t					packet_count;
	};
	
	typedef Queue<MessageStub>		MessageQueueT;
	
	States			state;
	uint64_t			current_message_type_id;
	MessageQueueT	message_q;
};

}//namespace mpipc
}//namespace frame
}//namespace solid


#endif
