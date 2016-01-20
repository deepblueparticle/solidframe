// frame/ipc/ipcservice.hpp
//
// Copyright (c) 2014 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SOLID_FRAME_IPC_IPCSERVICE_HPP
#define SOLID_FRAME_IPC_IPCSERVICE_HPP

#include "system/function.hpp"
#include "system/exception.hpp"

#include "frame/service.hpp"
#include "frame/ipc/ipccontext.hpp"
#include "frame/ipc/ipcmessage.hpp"
#include "frame/ipc/ipcserialization.hpp"
#include "system/debug.hpp"

namespace solid{
namespace frame{

namespace aio{
struct ReactorContext;
}//namespace aio

namespace ipc{

template <class M>
struct response_traits;

template <class M>
struct response_traits<void(*)(ConnectionContext&, DynamicPointer<M> &, ErrorConditionT const&)>{
	typedef M message_type;
};

template <class M>
struct response_traits<void(ConnectionContext&, DynamicPointer<M> &, ErrorConditionT const&)>{
	typedef M message_type;
};

template <class C, class M>
struct response_traits<void(C::*)(ConnectionContext&, DynamicPointer<M> &, ErrorConditionT const&)>{
	typedef M message_type;
};

template <class C, class M>
struct response_traits<void(C::*)(ConnectionContext&, DynamicPointer<M> &, ErrorConditionT const&) const>{
	typedef M message_type;
};

template <class C, class R>
struct response_traits<R(C::*)>: public response_traits<R(C&)>{
};

template <class F>
struct response_traits{
private:
	using call_type = response_traits<decltype(&F::operator())>;
public:
	using message_type = typename call_type::message_type;
};

struct Message;
class Configuration;
class Connection;

//! Inter Process Communication service
/*!
	Allows exchanging ipc::Messages between processes.
	Synchronous vs Asynchronous messages
	A synchronous message is one sent with Message::SynchronousFlagE flag set:
	sendMessage(..., Message::SynchronousFlagE)
	Messages with Message::SynchronousFlagE flag unset are asynchronous.
	
	Synchronous messages
		* are always sent one after another, so they reach destination
			in the same order they were sent.
		* if multiple connections through peers are posible, only one is used for
			synchronous messages.
	
	Asynchronous messages
		* Because the messages are multiplexed, although the messages start being
			serialized on the socket stream in the same order sendMessage was called
			they will reach destination in a different order deppending on the
			message serialization size.
			
	
	Example:
		sendMessage("peer_name", m1_500MB, Message::SynchronousFlagE);
		sendMessage("peer_name", m2_100MB, Message::SynchronousFlagE);
		sendMessage("peer_name", m3_10MB, 0);
		sendMessage("peer_name", m4_1MB, 0);
		
		The order they will reach the peer side is:
		m4_1MB, m3_10MB, m1_500MB, m2_100MB
	
*/
class Service: public Dynamic<Service, frame::Service>{
	typedef FUNCTION<std::pair<MessagePointerT, uint32>(ErrorConditionT const &)>			ActivateConnectionMessageFactoryFunctionT;
	
	template <class F, class M>
	struct ResponseHandler{
		F		f;
		
		ResponseHandler(F _f):f(_f){}
		
		void operator()(ConnectionContext &_rctx, MessagePointerT &_rmsgptr, ErrorConditionT const &_err){
			M  					*presponse = dynamic_cast<M*>(_rmsgptr.get());
			DynamicPointer<M>	msgptr(presponse);
			ErrorConditionT		error(_err);
			if(presponse or error){
			}else{
				//TODO:
				error.assign(-1, error.category());
			}
			f(_rctx, msgptr, error);
		}
	};
	
public:
	typedef Dynamic<Service, frame::Service> BaseT;
	
	Service(
		frame::Manager &_rm
	);
	
	//! Destructor
	~Service();

	ErrorConditionT reconfigure(Configuration const& _rcfg);
	
	Configuration const & configuration()const;
	
	// send message using recipient name --------------------------------------
	template <class T>
	ErrorConditionT sendMessage(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerFunctionT	response_handler;
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, nullptr, nullptr, _flags);
	}
	
	template <class T>
	ErrorConditionT sendMessage(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		ConnectionPoolId &_rpool_id,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerFunctionT	response_handler;
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, &_rpool_id, nullptr, _flags);
	}
	
	template <class T>
	ErrorConditionT sendMessage(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		ConnectionPoolId &_rpool_id,
		MessageId &_rmsg_id,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerFunctionT	response_handler;
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, &_rpool_id, &_rmsg_id, _flags);
	}
	
	// send message using connection uid  -------------------------------------
	
	template <class T>
	ErrorConditionT sendMessage(
		RecipientId const &_rrecipient_id,
		DynamicPointer<T> const &_rmsgptr,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		ResponseHandlerFunctionT	response_handler;
		return doSendMessage(nullptr, _rrecipient_id, msgptr, response_handler, nullptr, nullptr, _flags);
	}
	
	template <class T>
	ErrorConditionT sendMessage(
		RecipientId const &_rrecipient_id,
		DynamicPointer<T> const &_rmsgptr,
		MessageId &_rmsg_id,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		ResponseHandlerFunctionT	response_handler;
		return doSendMessage(nullptr, _rrecipient_id, msgptr, response_handler, nullptr, &_rmsg_id, _flags);
	}
	
	// send message using pool uid --------------------------------------------
	
	template <class T>
	ErrorConditionT sendMessage(
		ConnectionPoolId const &_rpool_id,
		DynamicPointer<T> const &_rmsgptr,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id(_rpool_id);
		ResponseHandlerFunctionT	response_handler;
		
		return doSendMessage(nullptr, recipient_id, msgptr, response_handler, nullptr, nullptr, _flags);
	}
	
	
	template <class T>
	ErrorConditionT sendMessage(
		ConnectionPoolId const &_rpool_id,
		DynamicPointer<T> const &_rmsgptr,
		MessageId &_rmsguid,
		ulong _flags = 0
	){
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id(_rpool_id);
		ResponseHandlerFunctionT	response_handler;
		
		return doSendMessage(nullptr, recipient_id, msgptr, response_handler, nullptr, &_rmsguid, _flags);
	}
	
	// send request using recipient name --------------------------------------
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, nullptr, nullptr, _flags | Message::WaitResponseFlagE);
	}
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		ConnectionPoolId &_rpool_id,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, &_rpool_id, nullptr, _flags | Message::WaitResponseFlagE);
	}
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		const char *_recipient_name,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		ConnectionPoolId &_rpool_id,
		MessageId &_rmsguid,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id;
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(_recipient_name, recipient_id, msgptr, response_handler, &_rpool_id, &_rmsguid, _flags | Message::WaitResponseFlagE);
	}
	
	// send request using connection uid --------------------------------------
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		RecipientId const &_rrecipient_id,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(nullptr, _rrecipient_id, msgptr, response_handler, nullptr, nullptr, _flags | Message::WaitResponseFlagE);
	}
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		RecipientId const &_rrecipient_id,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		MessageId &_rmsguid,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(nullptr, _rrecipient_id, msgptr, response_handler, nullptr, &_rmsguid, _flags | Message::WaitResponseFlagE);
	}
	
	// send request using pool uid --------------------------------------------
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		ConnectionPoolId const &_rpool_id,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id(_rpool_id);
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(nullptr, recipient_id, msgptr, response_handler, nullptr, nullptr, _flags | Message::WaitResponseFlagE);
	}
	
	template <class T, class Fnc>
	ErrorConditionT sendRequest(
		ConnectionPoolId const &_rpool_id,
		DynamicPointer<T> const &_rmsgptr,
		Fnc _fnc,
		MessageId &_rmsg_id,
		ulong _flags = 0
	){
		typedef ResponseHandler<Fnc, typename response_traits<decltype(_fnc)>::message_type>		ResponseHandlerT;
		
		MessagePointerT				msgptr(_rmsgptr);
		RecipientId					recipient_id(_rpool_id);
		ResponseHandlerT			fnc(_fnc);
		ResponseHandlerFunctionT	response_handler(fnc);
		
		return doSendMessage(nullptr, recipient_id, msgptr, response_handler, nullptr, &_rmsg_id, _flags | Message::WaitResponseFlagE);
	}
	
	//----------------------
	
	ErrorConditionT forcedConnectionClose(
		RecipientId const &_rconnection_uid
	);
	
	ErrorConditionT delayedConnectionClose(
		RecipientId const &_rconnection_uid
	);
	
	ErrorConditionT activateConnection(
		RecipientId const &_rconnection_uid
	){
		ActivateConnectionMessageFactoryFunctionT	msgfactory;
		return doActivateConnection(_rconnection_uid, nullptr, msgfactory, false);
	}
	
	template <class MF>
	ErrorConditionT activateConnection(
		RecipientId const &_rconnection_uid,
		const char *_recipient_name,
		MF _msgfactory,
		const bool _may_quit
	){
		ActivateConnectionMessageFactoryFunctionT	msgfactory(_msgfactory);
		return doActivateConnection(_rconnection_uid, _recipient_name, msgfactory, _may_quit);
	}
	
	ErrorConditionT cancelMessage(RecipientId const &_rrecipient_id, MessageId const &_rmsg_id);
	
private:
	friend struct ServiceProxy;
	friend class Listener;
	friend class Connection;
	
	void acceptIncomingConnection(SocketDevice &_rsd);
	
	ErrorConditionT doActivateConnection(
		RecipientId const &_rrecipient_id,
		const char *_recipient_name,
		ActivateConnectionMessageFactoryFunctionT const &_rmsgfactory,
		const bool _may_quit
	);
	
	void activateConnectionComplete(Connection &_rcon);
	
	void onConnectionClose(Connection &_rcon, aio::ReactorContext &_rctx, ObjectIdT const &_robjuid);
	
	void onIncomingConnectionStart(ConnectionContext &_rconctx);
	void onOutgoingConnectionStart(ConnectionContext &_rconctx);
	void onConnectionStop(ConnectionContext &_rconctx, ErrorConditionT const &_err);
	
	void tryFetchNewMessage(Connection &_rcon, aio::ReactorContext &_rctx, const bool _has_no_message_to_send);
	
	
	void forwardResolveMessage(ConnectionPoolId const &_rconpoolid, Event const&_revent);
	
	template <class F, class M>
	struct ReceiveProxy{
		F	f;
		ReceiveProxy(F _f):f(_f){}
		
		void operator()(ConnectionContext &_rctx, MessagePointerT &_rmsgptr){
			DynamicPointer<M>	msgptr(std::move(_rmsgptr));
			f(_rctx, msgptr);
		}
	};
	
	template <class F, class M>
	struct CompleteProxy{
		F	f;
		CompleteProxy(F _f):f(_f){}
		
		void operator()(ConnectionContext &_rctx, MessagePointerT &_rmsgptr, ErrorConditionT const &_err){
			DynamicPointer<M>	msgptr(std::move(_rmsgptr));
			f(_rctx, msgptr, _err);
		}
	};
	
	template <class F, class M>
	struct PrepareProxy{
		F	f;
		PrepareProxy(F _f):f(_f){}
		
		void operator()(ConnectionContext &_rctx, Message const &_rmsg){
			M const	&rmsg = static_cast<M const &>(_rmsg);
			f(_rctx, rmsg);
		}
	};
	
	template <class T, class FactoryFnc>
	size_t registerType(FactoryFnc _facf, size_t _idx = 0){
		TypeStub ts;
		size_t rv = tm.registerType<T>(_facf, ts, _idx);
		registerCast<T, ipc::Message>();
		return rv;
	}
	
	template <class T, class FactoryFnc, class ReceiveFnc/*, class PrepareFnc*/, class CompleteFnc>
	size_t registerType(FactoryFnc _facf, ReceiveFnc _rcvf/*, PrepareFnc _prepf*/, CompleteFnc _cmpltf, size_t _idx = 0){
		TypeStub ts;
		ts.complete_fnc = MessageCompleteFunctionT(CompleteProxy<CompleteFnc, T>(_cmpltf));
		//ts.prepare_fnc = MessagePrepareFunctionT(PrepareProxy<PrepareFnc, T>(_prepf));
		ts.receive_fnc = MessageReceiveFunctionT(ReceiveProxy<ReceiveFnc, T>(_rcvf));
		size_t rv = tm.registerType<T>(ts, Message::serialize<SerializerT, T>, Message::serialize<DeserializerT, T>, _facf, _idx);
		registerCast<T, ipc::Message>();
		return rv;
	}
	
	template <class Derived, class Base>
	bool registerCast(){
		return tm.registerCast<Derived, Base>();
	}
	
	template <class Derived, class Base>
	bool registerDownCast(){
		return tm.registerDownCast<Derived, Base>();
	}
	
	
	const TypeIdMapT& typeMap()const{
		return tm;
	}
	
	ErrorConditionT doSendMessage(
		const char *_recipient_name,
		const RecipientId	&_rrecipient_id_in,
		MessagePointerT &_rmsgptr,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ConnectionPoolId *_ppool_id_out,
		MessageId *_pmsg_id_out,
		ulong _flags
	);
	
	ErrorConditionT doSendMessageToNewPool(
		const char *_recipient_name,
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ConnectionPoolId *_ppool_id_out,
		MessageId *_pmsguid_out,
		ulong _flags
	);
	
	ErrorConditionT doNotifyConnectionPushMessage(
		ObjectIdT const &_robjuid,
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		MessageId *_pmsg_id_out,
		ulong _flags
	);
	
	ErrorConditionT doNotifyConnectionPushMessage(
		ObjectIdT const &_robjuid,
		const size_t _msg_idx,
		const size_t _pool_idx,
		ConnectionPoolId *_ppool_id_out
	);
	
	ErrorConditionT doNotifyConnectionDelayedClose(
		ObjectIdT const &_robjuid
	);
	
	ErrorConditionT doNotifyConnectionActivate(
		ObjectIdT const &_robjuid,
		ConnectionPoolId const &_rconpooluid
	);
	
	size_t doPushNewConnectionPool();
	
	void pushBackMessageToConnectionPool(
		ConnectionPoolId &_rconpoolid,
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ulong _flags
	);
private:
	struct	Data;
	Data			&d;
	TypeIdMapT		tm;
};

struct ServiceProxy{
	template <class T, class FactoryFnc, class ReceiveFnc/*, class PrepareFnc*/, class CompleteFnc>
	size_t registerType(FactoryFnc _facf, ReceiveFnc _rcvf/*, PrepareFnc _prepf*/, CompleteFnc _cmpltf, size_t _idx = 0){
		return rservice.registerType<T>(_facf, _rcvf/*, _prepf*/, _cmpltf, _idx);
	}
	template <class T, class FactoryFnc>
	size_t registerType(FactoryFnc _facf, size_t _idx = 0){
		return rservice.registerType<T>(_facf, _idx);
	}
private:
	friend class Service;
	friend class Configuration;
	ServiceProxy(Service &_rservice):rservice(_rservice){}
	Service &rservice;
};

}//namespace ipc
}//namespace frame
}//namespace solid

#endif