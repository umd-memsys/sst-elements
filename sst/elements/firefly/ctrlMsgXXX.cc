// Copyright 2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>

#include <sst/core/link.h>
#include <sst/core/params.h>
#include <sstream>

#include "virtNic.h"
#include "ctrlMsgXXX.h"
#include "ctrlMsgSendState.h"
#include "ctrlMsgRecvState.h"
#include "ctrlMsgReadState.h"
#include "ctrlMsgRegRegionState.h"
#include "ctrlMsgUnregRegionState.h"
#include "ctrlMsgWaitAnyState.h"
#include "ctrlMsgProcessQueuesState.h"
#include "info.h"
#include "loopBack.h"

using namespace SST::Firefly::CtrlMsg;
using namespace SST::Firefly;
using namespace SST;

XXX::XXX( Component* owner, Params& params ) :
    m_retLink( NULL ),
    m_info( NULL ),
    m_returnState( NULL )
{
    m_dbg_level = params.find_integer("verboseLevel",0);
    m_dbg_loc = (Output::output_location_t)params.find_integer("debug", 0);

    m_dbg.init("@t:CtrlMsg::@p():@l ", m_dbg_level, 0, m_dbg_loc );

    std::stringstream ss;
    ss << this;

    m_delayLink = owner->configureSelfLink( 
        "CtrlMsgSelfLink." + ss.str(), "1 ns",
        new Event::Handler<XXX>(this,&XXX::delayHandler));

    m_matchDelay_ps = params.find_integer( "matchDelay_ps", 1 );
    m_memcpyDelay_ps = params.find_integer( "memcpyDelay_ps", 1 );
    m_txDelay = params.find_integer( "txDelay_ns", 100 );
    m_rxDelay = params.find_integer( "rxDelay_ns", 100 );
    m_regRegionBaseDelay_ps = params.find_integer( "regRegionBaseDelay_ps", 3000 );
    m_regRegionPerByteDelay_ps = params.find_integer( "regRegionPerByteDelay_ps", 26 );
    m_regRegionXoverLength = params.find_integer( "regRegionXoverLength", 65000 );
    
    m_shortMsgLength = params.find_integer( "shortMsgLength", 4096 );

    m_loopLink = owner->configureLink("loop", "1 ns",
            new Event::Handler<XXX>(this,&XXX::loopHandler) );
    assert(m_loopLink);
}

void XXX::init( Info* info, VirtNic* nic )
{
    m_info = info;
    m_nic = nic;
    nic->setNotifyOnSendPioDone(
        new VirtNic::Handler<XXX,void*>(this, &XXX::notifySendPioDone )
    );
    nic->setNotifyOnSendDmaDone(
        new VirtNic::Handler<XXX,void*>(this, &XXX::notifySendDmaDone )
    );

    nic->setNotifyOnRecvDmaDone(
        new VirtNic::Handler4Args<XXX,int,int,size_t,void*>(
                                this, &XXX::notifyRecvDmaDone )
    );
    nic->setNotifyNeedRecv(
        new VirtNic::Handler3Args<XXX,int,int,size_t>(
                                this, &XXX::notifyNeedRecv )
    );
}

void XXX::setup() 
{
    char buffer[100];
    snprintf(buffer,100,"@t:%#x:%d:CtrlMsg::XXX::@p():@l ",m_info->nodeId(), 
                                                m_info->worldRank());
    m_dbg.setPrefix(buffer);

    m_sendState = new SendState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_recvState = new RecvState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_readState = new ReadState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_regRegionState = new RegRegionState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_unregRegionState = new UnregRegionState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_waitAnyState = new WaitAnyState<XXX>( m_dbg_level, m_dbg_loc, *this );
    m_processQueuesState = new ProcessQueuesState<XXX>( m_dbg_level, m_dbg_loc, *this );

    m_dbg.verbose(CALL_INFO,1,0,"matchDelay %d ns. memcpyDelay %d ns\n", m_matchDelay_ps, m_memcpyDelay_ps );
    m_dbg.verbose(CALL_INFO,1,0,"txDelay %d ns. rxDelay %d ns\n", m_txDelay, m_rxDelay );
}

void XXX::setRetLink( Link* link ) 
{
    m_retLink = link;
}

class Foo : public LoopBackEvent {
  public:
    Foo( int core, _CommReq* _req, bool _response ) : 
        LoopBackEvent( core ),
        req( _req ),
        response( _response ) 
    {
    }
    _CommReq* req;
    bool      response;
};

void XXX::loopSend( int core, _CommReq* req, bool response ) 
{
    m_dbg.verbose(CALL_INFO,1,0,"dest core=%d req=%p\n",core,req);    
    
    m_loopLink->send(0, new Foo( core, req, response ) );
}    

void XXX::loopHandler( Event* ev )
{
    Foo* event = static_cast< Foo* >(ev);
    m_dbg.verbose(CALL_INFO,1,0,"req=%p\n",event->req);    
    m_processQueuesState->loopHandler( event->req, event->response );
    delete ev;
}

static size_t calcLength( std::vector<IoVec>& ioVec )
{
    size_t len = 0;
    for ( size_t i = 0; i < ioVec.size(); i++ ) {
        len += ioVec[i].len;
    }
    return len;
}

void XXX::sendv( bool blocking, std::vector<IoVec>& ioVec, nid_t dest, 
                   tag_t tag, CommReq* commReq, FunctorBase_0<bool>* functor )
{
    m_dbg.verbose(CALL_INFO,1,0,"dest=%#x tag=%#x length=%lu \n",
                                        dest, tag, calcLength(ioVec) );
    m_sendState->enter( blocking, ioVec, dest, tag, commReq, functor );
}

void XXX::recvv( bool blocking, std::vector<IoVec>& ioVec, nid_t src,
      tag_t tag, CommReq* commReq, FunctorBase_0<bool>* functor )
{
    m_dbg.verbose(CALL_INFO,1,0,"src=%#x tag=%#x length=%lu\n",
                                        src, tag, calcLength(ioVec) );
    m_recvState->enter( blocking, ioVec, src, tag, 0, commReq, functor );
}

void XXX::recvv( bool blocking, std::vector<IoVec>& ioVec, nid_t src,
      tag_t tag, tag_t ignore, CommReq* commReq, FunctorBase_0<bool>* functor )
{
    m_dbg.verbose(CALL_INFO,1,0,"src=%#x tag=%#x length=%lu\n",
                                        src, tag, calcLength(ioVec) );
    m_recvState->enter( blocking, ioVec, src, tag, ignore, commReq, functor );
}


void XXX::waitAny( std::vector<CommReq*>& reqs, FunctorBase_1<CommReq*,bool>* functor )
{
    m_waitAnyState->enter( reqs, functor );
}

void XXX::read( nid_t nid, region_t region , void* buf, size_t len,
                                            FunctorBase_0<bool>* functor )
{
    m_readState->enter( nid, region, buf, len, functor );
}

void XXX::registerRegion( region_t region, nid_t nid, void* buf, size_t len,
                                            FunctorBase_0<bool>* functor )
{
    m_regRegionState->enter( region, nid, buf, len, functor );
}

void XXX::unregisterRegion( region_t region, FunctorBase_0<bool>* functor )
{
    m_unregRegionState->enter( region, functor );
}

void XXX::getStatus( CommReq* req, Status* status )
{
    *status = req->status;
    status->nid = info()->nidToWorldRank( status->nid );
    m_dbg.verbose(CALL_INFO,1,0,"nid=%#x tag=%#x length=%lu \n",
                        status->nid, status->tag, status->length );
}

void XXX::schedFunctor( FunctorBase_0<bool>* functor, int delay )
{
    m_delayLink->send( delay, new DelayEvent(functor) );
}

void XXX::delayHandler( SST::Event* e )
{
    DelayEvent* event = static_cast<DelayEvent*>(e);
    
    m_dbg.verbose(CALL_INFO,2,0,"\n");
    if ( event->functor0 ) {
        if ( (*event->functor0)( ) ) {
            delete event->functor0;
        }
    } else if ( event->functor1 ) {
        if ( (*event->functor1)( event->req ) ) {
            delete event->functor1;
        }
    }
    delete e;
}

bool XXX::notifySendPioDone( void* key )
{
    m_dbg.verbose(CALL_INFO,2,0,"key=%p\n",key);

    if ( key ) {
        FunctorBase_0<bool>* functor = static_cast<FunctorBase_0<bool>*>(key);
        if ( (*functor)() ) {
            delete functor;
        }     
    }

    return true;
}

bool XXX::notifySendDmaDone( void* key )
{
    m_dbg.verbose(CALL_INFO,2,0,"key=%p\n",key);

    if ( key ) {
        FunctorBase_0<bool>* functor = static_cast<FunctorBase_0<bool>*>(key);
        if ( (*functor)() ) {
            delete functor;
        }     
    }

    return true;
}

bool XXX::notifyRecvDmaDone( int nid, int tag, size_t len, void* key )
{
    m_dbg.verbose(CALL_INFO,1,0,"src=%#x tag=%#x len=%lu key=%p\n",
                                                    nid,tag,len,key);
    if ( key ) {
        FunctorBase_3<int,int,size_t,bool>* functor = 
            static_cast< FunctorBase_3<int,int,size_t,bool>* >(key);
        if ( (*functor)( nid, tag, len ) ) {
            delete functor;
        }     
    }

    return true;
}

bool XXX::notifyNeedRecv(int nid, int tag, size_t len )
{

    m_dbg.verbose(CALL_INFO,1,0,"src=%#x tag=%#x len=%lu\n",nid,tag,len);
    m_processQueuesState->needRecv( nid, tag, len );
    
    return true;
}

void XXX::passCtrlToFunction( int delay, FunctorBase_1<CommReq*, bool>* functor, CommReq* req )
{
    m_dbg.verbose(CALL_INFO,1,0,"back to Function delay=%d functor=%p\n",
                                delay, functor);

    if ( functor ) {
        m_delayLink->send( delay, new DelayEvent( functor, req ) );
    } else {
        m_retLink->send( delay, NULL );
    }
}

void XXX::passCtrlToFunction( int delay, FunctorBase_0<bool>* functor )
{
    m_dbg.verbose(CALL_INFO,1,0,"back to Function delay=%d functor=%p\n",
                                delay, functor);

    if ( functor ) {
        m_delayLink->send( delay, new DelayEvent( functor ) );
    } else {
        m_retLink->send( delay, NULL );
    }
}

