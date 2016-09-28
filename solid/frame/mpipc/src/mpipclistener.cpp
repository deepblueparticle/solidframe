// solid/frame/ipc/src/ipclistener.cpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#include "solid/utility/event.hpp"
#include "solid/frame/aio/aioreactorcontext.hpp"
#include "mpipclistener.hpp"
#include "solid/frame/mpipc/mpipcservice.hpp"

namespace solid{
namespace frame{
namespace mpipc{

Listener::Listener(
	SocketDevice &_rsd
):
	sock(this->proxy(), std::move(_rsd)), timer(this->proxy())
{
	idbgx(Debug::mpipc, this);
}
Listener::~Listener(){
	idbgx(Debug::mpipc, this);
}

inline Service& Listener::service(frame::aio::ReactorContext &_rctx){
	return static_cast<Service&>(_rctx.service());
}

/*virtual*/ void Listener::onEvent(frame::aio::ReactorContext &_rctx, Event &&_uevent){
	idbg("event = "<<_uevent);
	if(
		_uevent == generic_event_category.event(GenericEvents::Start) or
		_uevent == generic_event_category.event(GenericEvents::Timer)
	){
		sock.postAccept(
			_rctx,
			[this](frame::aio::ReactorContext &_rctx, SocketDevice &_rsd){onAccept(_rctx, _rsd);}
		);
	}else if(_uevent == generic_event_category.event(GenericEvents::Kill)){
		postStop(_rctx);
	}
}

void Listener::onAccept(frame::aio::ReactorContext &_rctx, SocketDevice &_rsd){
	idbg("");
	unsigned	repeatcnt = 4;
	
	do{
		if(!_rctx.error()){
			service(_rctx).acceptIncomingConnection(_rsd);
		}else{
			timer.waitFor(
				_rctx, NanoTime(10),
				[this](frame::aio::ReactorContext &_rctx){onEvent(_rctx, generic_event_category.event(GenericEvents::Timer));}
			);
			break;
		}
		--repeatcnt;
	}while(
		repeatcnt && 
		sock.accept(
			_rctx,
			[this](frame::aio::ReactorContext &_rctx, SocketDevice &_rsd){onAccept(_rctx, _rsd);},
			_rsd
		)
	);
	
	if(!repeatcnt){
		sock.postAccept(
			_rctx,
			[this](frame::aio::ReactorContext &_rctx, SocketDevice &_rsd){onAccept(_rctx, _rsd);}
		);//fully asynchronous call
	}
}

}//namespace mpipc
}//namespace frame
}//namespace solid
