/**
Copyright 2009-2017 National Technology and Engineering Solutions of Sandia, 
LLC (NTESS).  Under the terms of Contract DE-NA-0003525, the U.S.  Government 
retains certain rights in this software.

Sandia National Laboratories is a multimission laboratory managed and operated
by National Technology and Engineering Solutions of Sandia, LLC., a wholly 
owned subsidiary of Honeywell International, Inc., for the U.S. Department of 
Energy's National Nuclear Security Administration under contract DE-NA0003525.

Copyright (c) 2009-2017, NTESS

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Sandia Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Questions? Contact sst-macro-help@sandia.gov
*/

#include <sstmac/libraries/sumi/distributed_service.h>
#include <sstmac/libraries/sumi/sumi_transport.h>
#include <sumi/transport.h>
#include <sprockit/keyword_registration.h>

MakeDebugSlot(distributed_service)
RegisterKeywords("services");

#define debug(...) debug_printf(sprockit::dbg::distributed_service, __VA_ARGS__)

namespace sstmac {

distributed_service_app::distributed_service_app(sprockit::sim_parameters* params,
                    sw::software_id sid,
                    sw::operating_system* os) :
  app(params, sid, os)
{
  libname_ = params->get_param("libname");
}

void
distributed_service_app::skeleton_main()
{
  //need to pass libname twice - once for the factory, once for OS registration
  distributed_service* srv = distributed_service::factory::get_value(libname_, params_, libname_, sid(), os_);
  srv->init();
  debug("initialized distributed service %s on rank %d", libname_.c_str(), srv->rank());
  srv->barrier(0);
  srv->collective_block(sumi::collective::barrier, 0);
  debug("running distributed service %s on rank %d", libname_.c_str(), srv->rank());
  srv->run();
  srv->barrier(1);
  srv->collective_block(sumi::collective::barrier, 1);
  debug("finalizing distributed service %s on rank %d", libname_.c_str(), srv->rank());
  srv->finish();
  delete srv;
}

sumi::message::ptr
distributed_service::poll_for_message(bool blocking)
{
  sumi::message::ptr msg = poll(blocking);
  if (msg && msg->class_type() == sumi::message::bcast){
    sumi::system_bcast_message::ptr smsg = ptr_safe_cast(sumi::system_bcast_message, msg);
    if (smsg->action() == sumi::system_bcast_message::shutdown){
      terminated_ = true;
      return sumi::message::ptr();
    }
  }
  return msg;
}


}