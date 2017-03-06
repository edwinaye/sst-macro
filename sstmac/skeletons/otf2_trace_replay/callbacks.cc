/*
 *  This file is part of SST/macroscale:
 *               The macroscale architecture simulator from the SST suite.
 *  Copyright (c) 2009 Sandia Corporation.
 *  This software is distributed under the BSD License.
 *  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
 *  the U.S. Government retains certain rights in this software.
 *  For more information, see the LICENSE file in the top
 *  SST/macroscale directory.
 */

#include <sprockit/errors.h>

#include "structures.h"
#include "callbacks.h"

#include "callbase.h"
#include "mpi_calls.h"
#include "callqueue.h"
#include "otf2_trace_replay.h"

#if 0
    #define DEF_PRINT(...) printf("DEF: " __VA_ARGS__)
#else
    #define DEF_PRINT(...)
#endif

#if 1
    #define EVENT_PRINT(...) cerr << "EVENT: " __VA_ARGS__ << endl;
#else
    #define EVENT_PRINT(...)
#endif

//todo delete this. It should be defined by dumpi's mpi
#define MPI_CHAR 1

/******************************************************************************
 * Definition callbacks
 *
 * OTF2 definition reader will use these as callbacks when streaming through a
 * trace definition file. Definitions are necessary for converting callback
 * data (integer hashes) into meaningful data about MPI calls
 */

OTF2_CallbackCode def_clock_properties(
    void*    userData,
    uint64_t timerResolution,
    uint64_t globalOffset,
    uint64_t traceLength ) {

	auto app = (OTF2_trace_replay_app*)userData;
	app->otf2_clock_properties =  {
        timerResolution, globalOffset, traceLength
    };

    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode def_string(
    void*          userData,
    OTF2_StringRef self,
    const char*    str ) {

	auto app = (OTF2_trace_replay_app*)userData;
    app->otf2_string_table.push_back(str);
    app->otf2_mpi_call_map[self] = MPI_call_to_id[str];

    DEF_PRINT("STRING\n");
    return OTF2_CALLBACK_SUCCESS;
}

// probably not needed
OTF2_CallbackCode def_location(
    void*                 userData,
    OTF2_LocationRef      self,
    OTF2_StringRef        name,
    OTF2_LocationType     locationType,
    uint64_t              numberOfEvents,
    OTF2_LocationGroupRef locationGroup ) {

    DEF_PRINT("LOCATION\n");
    return OTF2_CALLBACK_SUCCESS;
}

// probably not needed
OTF2_CallbackCode def_location_group(
    void*                  userData,
    OTF2_LocationGroupRef  self,
    OTF2_StringRef         name,
    OTF2_LocationGroupType locationGroupType,
    OTF2_SystemTreeNodeRef systemTreeParent ) {

    DEF_PRINT("LOCATION GROUP\n");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode def_region(
    void*           userData,
    OTF2_RegionRef  self,
    OTF2_StringRef  name,
    OTF2_StringRef  canonicalName,
    OTF2_StringRef  description,
    OTF2_RegionRole regionRole,
    OTF2_Paradigm   paradigm,
    OTF2_RegionFlag regionFlags,
    OTF2_StringRef  sourceFile,
    uint32_t        beginLineNumber,
    uint32_t        endLineNumber ) {

	auto app = (OTF2_trace_replay_app*)userData;
	app->otf2_regions.push_back({name, regionRole, paradigm});

    DEF_PRINT("REGION\n");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode def_callpath(
    void*            userData,
    OTF2_CallpathRef self,
    OTF2_CallpathRef parent,
    OTF2_RegionRef   region ) {

	auto app = (OTF2_trace_replay_app*)userData;
	app->otf2_callpaths.push_back({region});

    DEF_PRINT("CALLPATH\n");
    return OTF2_CALLBACK_SUCCESS;
}

// May be useful for ID'ing threads
OTF2_CallbackCode def_group(
    void*           userData,
    OTF2_GroupRef   self,
    OTF2_StringRef  name,
    OTF2_GroupType  groupType,
    OTF2_Paradigm   paradigm,
    OTF2_GroupFlag  groupFlags,
    uint32_t        numberOfMembers,
    const uint64_t* members ) {

	auto app = (OTF2_trace_replay_app*)userData;
	app->otf2_groups.push_back({
        name, groupType, paradigm, groupFlags
    });

    DEF_PRINT("GROUP\n");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode def_comm(
    void*          userData,
    OTF2_CommRef   self,
    OTF2_StringRef name,
    OTF2_GroupRef  group,
    OTF2_CommRef   parent ) {

	auto app = (OTF2_trace_replay_app*)userData;
	app->otf2_regions.push_back({ name });

    DEF_PRINT("COMMUNICATOR\n");
    return OTF2_CALLBACK_SUCCESS;
}

// probably not needed
OTF2_CallbackCode
def_location_group_property(
	void*                 userData,
	OTF2_LocationGroupRef locationGroup,
	OTF2_StringRef        name,
	OTF2_Type             type,
	OTF2_AttributeValue   value ) {

    cout << "LOCATION_GROUP_PROPERTY" << endl;
    return OTF2_CALLBACK_SUCCESS;
}

// probably not needed
OTF2_CallbackCode
def_location_property(
	void*               userData,
	OTF2_LocationRef    location,
	OTF2_StringRef      name,
	OTF2_Type           type,
	OTF2_AttributeValue value ) {

    cout << "LOCATION_PROPERTY" << endl;
    return OTF2_CALLBACK_SUCCESS;
}

/******************************************************************************
 * Event callbacks
 *
 * OTF2 event reader will use these as callbacks when streaming through a trace.
 */


OTF2_CallbackCode event_mpi_send(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint32_t            receiver,
    OTF2_CommRef        communicator,
    uint32_t            msgTag,
    uint64_t            msgLength ) {

    auto app = (OTF2_trace_replay_app*)userData;
    MpiSendCall* call = app->get_callqueue().find_latest<MpiSendCall>();

    CallBase::assert_call(call, "Lookup for MpiSendCall in 'event_mpi_send' returned NULL");

    call->comm = communicator;
    call->count = msgLength;
    call->datatype = MPI_CHAR;
    call->dest = receiver;
    call->tag = msgTag;
    call->location = location;

    EVENT_PRINT("MPI SEND");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_mpi_isend(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint32_t            receiver,
    OTF2_CommRef        communicator,
    uint32_t            msgTag,
    uint64_t            msgLength,
    uint64_t            requestID ) {

    auto app = (OTF2_trace_replay_app*)userData;
    MpiIsendCall* call = app->get_callqueue().find_latest<MpiIsendCall>();

    CallBase::assert_call(call, "Lookup for MpiIsendCall in 'event_mpi_isend' returned NULL");

    call->comm = communicator;
    call->count = msgLength;
    call->datatype = MPI_CHAR;
    call->dest = receiver;
    call->request = requestID;
    call->tag = msgTag;

    EVENT_PRINT("MPI ISEND");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_mpi_isend_complete(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint64_t            requestID ) {

	auto app = (OTF2_trace_replay_app*)userData;
	auto& callqueue = app->get_callqueue();

	// find the wait event responsible for this and assign the request ID
	auto latest_call = callqueue.PeekBack();

	switch(latest_call->GetID()) {
	case ID_MPI_Wait:
		((MpiWaitCall*)latest_call)->request = requestID; break;
	case ID_MPI_Waitall:
	case ID_MPI_Waitany:
	case ID_MPI_Waitsome:
		cerr << "ERROR " << latest_call->ToString() << " is not supported call" << endl;
		spkt_throw(sprockit::null_error, " not implemented"); break;
	default:
		spkt_throw(sprockit::null_error, " not recognized as a wait call"); break;
	}

    EVENT_PRINT("ISEND COMPLETE");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_mpi_irecv_request(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint64_t            requestID ) {

    auto app = (OTF2_trace_replay_app*)userData;
    MpiIrecvCall* call = app->get_callqueue().find_latest<MpiIrecvCall>();

    CallBase::assert_call(call, "Lookup for MpiIrecvCall in 'event_mpi_irecv_request' returned NULL");

    call->request = requestID;

    EVENT_PRINT("IRECV REQUEST id: " << requestID );
    return OTF2_CALLBACK_SUCCESS;
}

// This event is triggered inside a wait. Must finish the Irecv event,
// and find the parent wait call to give it this request
OTF2_CallbackCode event_mpi_irecv(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint32_t            sender,
    OTF2_CommRef        communicator,
    uint32_t            msgTag,
    uint64_t            msgLength,
    uint64_t            requestID ) {

    auto app = (OTF2_trace_replay_app*)userData;
    auto& callqueue = app->get_callqueue();

    // finish off the Irecv call
    MpiIrecvCall* call = callqueue.find_latest<MpiIrecvCall>(requestID);
    CallBase::assert_call(call, "Lookup for MpiIrecvCall in 'event_mpi_irecv' returned NULL");

    call->comm = communicator;
    call->count = msgLength;
    call->datatype = MPI_CHAR;
    call->source = sender;
    call->tag = msgTag;

    callqueue.CallReady(call);

    // find the wait event responsible for this and assign the request ID
    auto latest_call = callqueue.PeekBack();

    switch(latest_call->GetID()) {
    case ID_MPI_Wait:
    	((MpiWaitCall*)latest_call)->request = requestID; break;
    case ID_MPI_Waitall:
    case ID_MPI_Waitany:
    case ID_MPI_Waitsome:
    	cerr << "ERROR " << latest_call->ToString() << " is not supported call" << endl;
    	spkt_throw(sprockit::null_error, " not implemented"); break;
    default:
    	spkt_throw(sprockit::null_error, " not recognized as a wait call"); break;
    }

    EVENT_PRINT("IRECV count: " << msgLength << " source: " << sender << " tag: " << msgTag);
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_mpi_recv(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint32_t            sender,
    OTF2_CommRef        communicator,
    uint32_t            msgTag,
    uint64_t            msgLength ) {

	auto app = (OTF2_trace_replay_app*)userData;
    MpiRecvCall* call = app->get_callqueue().find_latest<MpiRecvCall>();

    CallBase::assert_call(call, "Lookup for MpiIrecvCall in 'event_mpi_irecv_request' returned NULL");

    call->comm = communicator;
    call->count = msgLength;
    call->datatype = MPI_CHAR;
    call->source = sender;
    call->tag = msgTag;

    EVENT_PRINT("RECV count: " << msgLength << " source: " << sender << " tag: " << msgTag);
    return OTF2_CALLBACK_SUCCESS;
}

// TODO implement
OTF2_CallbackCode event_mpi_request_test(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint64_t            requestID ) {

	EVENT_PRINT("REQUEST TEST\n");
    return OTF2_CALLBACK_SUCCESS;
}

// TODO implement
OTF2_CallbackCode event_mpi_request_cancelled(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    uint64_t            requestID ) {

	EVENT_PRINT("REQUEST CANCELLED\n");
    return OTF2_CALLBACK_SUCCESS;
}

// probably not needed
OTF2_CallbackCode event_mpi_collective_begin(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes ) {

	EVENT_PRINT("COLLECTIVE BEGIN\n");
    return OTF2_CALLBACK_SUCCESS;
}

/* OTF2_CollectiveOp_enum
    OTF2_COLLECTIVE_OP_BARRIER                       = 0,
    OTF2_COLLECTIVE_OP_BCAST                         = 1,
    OTF2_COLLECTIVE_OP_GATHER                        = 2,
    OTF2_COLLECTIVE_OP_GATHERV                       = 3,
    OTF2_COLLECTIVE_OP_SCATTER                       = 4,
    OTF2_COLLECTIVE_OP_SCATTERV                      = 5,
    OTF2_COLLECTIVE_OP_ALLGATHER                     = 6,
    OTF2_COLLECTIVE_OP_ALLGATHERV                    = 7,
    OTF2_COLLECTIVE_OP_ALLTOALL                      = 8,
    OTF2_COLLECTIVE_OP_ALLTOALLV                     = 9,
    OTF2_COLLECTIVE_OP_ALLTOALLW                     = 10,
    OTF2_COLLECTIVE_OP_ALLREDUCE                     = 11,
    OTF2_COLLECTIVE_OP_REDUCE                        = 12,
    OTF2_COLLECTIVE_OP_REDUCE_SCATTER                = 13,
    OTF2_COLLECTIVE_OP_SCAN                          = 14,
 */

OTF2_CallbackCode event_mpi_collective_end(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    OTF2_CollectiveOp   collectiveOp,
    OTF2_CommRef        communicator,
    uint32_t            root,
    uint64_t            sizeSent,
    uint64_t            sizeReceived ) {

    auto app = (OTF2_trace_replay_app*)userData;
#define HANDLE_CASE(_class) case _class::id : { \
            auto call = app->get_callqueue().find_latest<_class>(); \
            CallBase::assert_call(call, "Lookup for #_class in 'event_mpi_collective_end' returned NULL");\
            call->comm = communicator;      \
            call->sendcount = sizeSent;     \
            call->recvcount = sizeReceived; \
            call->recvtype = MPI_CHAR;
#define END_CASE break;}

    switch (collectiveOp) {
        HANDLE_CASE(MpiAllgatherCall) END_CASE
        //HANDLE_CASE(MpiAllgatherCall) END_CASE
        //HANDLE_CASE(MpiBarrierCall) END_CASE
        HANDLE_CASE(MpiGatherCall) END_CASE
        //HANDLE_CASE(MpiGathervCall) END_CASE
        HANDLE_CASE(MpiScatterCall) END_CASE
        //HANDLE_CASE(MpiScattervCall) END_CASE
        HANDLE_CASE(MpiAlltoallCall) END_CASE

    //HANDLE_CASE(MpiAlltoallvCall) END_CASE
    //HANDLE_CASE(MpiAlltoallwCall) END_CASE
    //HANDLE_CASE(MpiAllreduceCall) END_CASE
    //HANDLE_CASE(MpiReduceCall) END_CASE
    //HANDLE_CASE(MpiScatterCall) END_CASE
    //HANDLE_CASE(MpiReducescatterCall) END_CASE
    //HANDLE_CASE(MpiScanCall) END_CASE
    default:
        cout << "ERROR: Collective not handled; " << (int)collectiveOp << endl;
    }

#undef HANDLE_CASE
#undef END_CASE
    EVENT_PRINT("COLLECTIVE END\n");
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_parameter_string(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    OTF2_ParameterRef   parameter,
    OTF2_StringRef      string ) {

    EVENT_PRINT("PARAMETER STRING\n");
    return OTF2_CALLBACK_SUCCESS;
}

// Ignoring MPI calls in event_enter and event_leave will prevent it being
// added to the queue.
#define CASE_IGNORE(obj_name) case obj_name::id : \
	break;

OTF2_CallbackCode event_enter(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    OTF2_RegionRef      region ) {

    auto app = (OTF2_trace_replay_app*)userData;
    auto id = app->otf2_mpi_call_map[app->otf2_regions[region].name];
    CallBase* call = NULL;

#define CASE_ADD_CALL(obj_name) case obj_name::id : \
	call = new obj_name(location, time); \
	app->get_callqueue().AddCall(call); \
	EVENT_PRINT("ENTER " << call->ToString() << " time: " << time); \
	break;

#define ADD_SPECIAL_CASE(obj_name) case obj_name::id :
#define END_SPECIAL_CASE break;

    switch (id) {
        CASE_ADD_CALL(MpiAbortCall)
        CASE_ADD_CALL(MpiAccumulateCall)
        CASE_ADD_CALL(MpiAdderrorclassCall)
        CASE_ADD_CALL(MpiAdderrorcodeCall)
        CASE_ADD_CALL(MpiAdderrorstringCall)
        CASE_ADD_CALL(MpiAddressCall)
        CASE_ADD_CALL(MpiAllgatherCall)
        CASE_ADD_CALL(MpiAllgathervCall)
        CASE_ADD_CALL(MpiAllocmemCall)
        CASE_ADD_CALL(MpiAllreduceCall)
        CASE_ADD_CALL(MpiAlltoallCall)
        CASE_ADD_CALL(MpiAlltoallvCall)
        CASE_ADD_CALL(MpiAlltoallwCall)
        CASE_ADD_CALL(MpiAttrdeleteCall)
        CASE_ADD_CALL(MpiAttrgetCall)
        CASE_ADD_CALL(MpiAttrputCall)
        CASE_ADD_CALL(MpiBarrierCall)
        CASE_ADD_CALL(MpiBcastCall)
        CASE_ADD_CALL(MpiBsendCall)
        CASE_ADD_CALL(MpiBsendinitCall)
        CASE_ADD_CALL(MpiBufferattachCall)
        CASE_ADD_CALL(MpiBufferdetachCall)
        CASE_ADD_CALL(MpiCancelCall)
        CASE_ADD_CALL(MpiCartcoordsCall)
        CASE_ADD_CALL(MpiCartcreateCall)
        CASE_ADD_CALL(MpiCartgetCall)
        CASE_ADD_CALL(MpiCartmapCall)
        CASE_ADD_CALL(MpiCartrankCall)
        CASE_ADD_CALL(MpiCartshiftCall)
        CASE_ADD_CALL(MpiCartsubCall)
        CASE_ADD_CALL(MpiCartdimgetCall)
        CASE_ADD_CALL(MpiCloseportCall)
        CASE_ADD_CALL(MpiCommacceptCall)
        CASE_ADD_CALL(MpiCommcallerrhandlerCall)
        CASE_ADD_CALL(MpiCommcompareCall)
        CASE_ADD_CALL(MpiCommconnectCall)
        CASE_ADD_CALL(MpiCommcreateCall)
        CASE_ADD_CALL(MpiCommcreateerrhandlerCall)
        CASE_ADD_CALL(MpiCommcreatekeyvalCall)
        CASE_ADD_CALL(MpiCommdeleteattrCall)
        CASE_ADD_CALL(MpiCommdisconnectCall)
        CASE_ADD_CALL(MpiCommdupCall)
        CASE_ADD_CALL(MpiCommfreeCall)
        CASE_ADD_CALL(MpiCommfreekeyvalCall)
        CASE_ADD_CALL(MpiCommgetattrCall)
        CASE_ADD_CALL(MpiCommgeterrhandlerCall)
        CASE_ADD_CALL(MpiCommgetnameCall)
        CASE_ADD_CALL(MpiCommgetparentCall)
        CASE_ADD_CALL(MpiCommgroupCall)
        CASE_ADD_CALL(MpiCommjoinCall)
		CASE_IGNORE(MpiCommrankCall)
        CASE_ADD_CALL(MpiCommremotegroupCall)
        CASE_ADD_CALL(MpiCommremotesizeCall)
        CASE_ADD_CALL(MpiCommsetattrCall)
        CASE_ADD_CALL(MpiCommseterrhandlerCall)
        CASE_ADD_CALL(MpiCommsetnameCall)
		CASE_IGNORE(MpiCommsizeCall)
        CASE_ADD_CALL(MpiCommspawnCall)
        CASE_ADD_CALL(MpiCommspawnmultipleCall)
        CASE_ADD_CALL(MpiCommsplitCall)
        CASE_ADD_CALL(MpiCommtestinterCall)
        CASE_ADD_CALL(MpiDimscreateCall)
        CASE_ADD_CALL(MpiErrhandlercreateCall)
        CASE_ADD_CALL(MpiErrhandlerfreeCall)
        CASE_ADD_CALL(MpiErrhandlergetCall)
        CASE_ADD_CALL(MpiErrhandlersetCall)
        CASE_ADD_CALL(MpiErrorclassCall)
        CASE_ADD_CALL(MpiErrorstringCall)
        CASE_ADD_CALL(MpiExscanCall)
        CASE_ADD_CALL(MpiFilecallerrhandlerCall)
        CASE_ADD_CALL(MpiFilecloseCall)
        CASE_ADD_CALL(MpiFilecreateerrhandlerCall)
        CASE_ADD_CALL(MpiFiledeleteCall)
        CASE_ADD_CALL(MpiFilegetamodeCall)
        CASE_ADD_CALL(MpiFilegetatomicityCall)
        CASE_ADD_CALL(MpiFilegetbyteoffsetCall)
        CASE_ADD_CALL(MpiFilegeterrhandlerCall)
        CASE_ADD_CALL(MpiFilegetgroupCall)
        CASE_ADD_CALL(MpiFilegetinfoCall)
        CASE_ADD_CALL(MpiFilegetpositionCall)
        CASE_ADD_CALL(MpiFilegetpositionsharedCall)
        CASE_ADD_CALL(MpiFilegetsizeCall)
        CASE_ADD_CALL(MpiFilegettypeextentCall)
        CASE_ADD_CALL(MpiFilegetviewCall)
        CASE_ADD_CALL(MpiFileireadCall)
        CASE_ADD_CALL(MpiFileireadatCall)
        CASE_ADD_CALL(MpiFileireadsharedCall)
        CASE_ADD_CALL(MpiFileiwriteCall)
        CASE_ADD_CALL(MpiFileiwriteatCall)
        CASE_ADD_CALL(MpiFileiwritesharedCall)
        CASE_ADD_CALL(MpiFileopenCall)
        CASE_ADD_CALL(MpiFilepreallocateCall)
        CASE_ADD_CALL(MpiFilereadCall)
        CASE_ADD_CALL(MpiFilereadallCall)
        CASE_ADD_CALL(MpiFilereadallbeginCall)
        CASE_ADD_CALL(MpiFilereadallendCall)
        CASE_ADD_CALL(MpiFilereadatCall)
        CASE_ADD_CALL(MpiFilereadatallCall)
        CASE_ADD_CALL(MpiFilereadatallbeginCall)
        CASE_ADD_CALL(MpiFilereadatallendCall)
        CASE_ADD_CALL(MpiFilereadorderedCall)
        CASE_ADD_CALL(MpiFilereadorderedbeginCall)
        CASE_ADD_CALL(MpiFilereadorderedendCall)
        CASE_ADD_CALL(MpiFilereadsharedCall)
        CASE_ADD_CALL(MpiFileseekCall)
        CASE_ADD_CALL(MpiFileseeksharedCall)
        CASE_ADD_CALL(MpiFilesetatomicityCall)
        CASE_ADD_CALL(MpiFileseterrhandlerCall)
        CASE_ADD_CALL(MpiFilesetinfoCall)
        CASE_ADD_CALL(MpiFilesetsizeCall)
        CASE_ADD_CALL(MpiFilesetviewCall)
        CASE_ADD_CALL(MpiFilesyncCall)
        CASE_ADD_CALL(MpiFilewriteCall)
        CASE_ADD_CALL(MpiFilewriteallCall)
        CASE_ADD_CALL(MpiFilewriteallbeginCall)
        CASE_ADD_CALL(MpiFilewriteallendCall)
        CASE_ADD_CALL(MpiFilewriteatCall)
        CASE_ADD_CALL(MpiFilewriteatallCall)
        CASE_ADD_CALL(MpiFilewriteatallbeginCall)
        CASE_ADD_CALL(MpiFilewriteatallendCall)
        CASE_ADD_CALL(MpiFilewriteorderedCall)
        CASE_ADD_CALL(MpiFilewriteorderedbeginCall)
        CASE_ADD_CALL(MpiFilewriteorderedendCall)
        CASE_ADD_CALL(MpiFilewritesharedCall)
        CASE_ADD_CALL(MpiFinalizeCall)
        CASE_ADD_CALL(MpiFinalizedCall)
        CASE_ADD_CALL(MpiFreememCall)
        CASE_ADD_CALL(MpiGatherCall)
        CASE_ADD_CALL(MpiGathervCall)
        CASE_ADD_CALL(MpiGetCall)
        CASE_ADD_CALL(MpiGetaddressCall)
        CASE_ADD_CALL(MpiGetcountCall)
        CASE_ADD_CALL(MpiGetelementsCall)
        CASE_ADD_CALL(MpiGetprocessornameCall)
        CASE_ADD_CALL(MpiGetversionCall)
        CASE_ADD_CALL(MpiGraphcreateCall)
        CASE_ADD_CALL(MpiGraphgetCall)
        CASE_ADD_CALL(MpiGraphmapCall)
        CASE_ADD_CALL(MpiGraphneighborsCall)
        CASE_ADD_CALL(MpiGraphneighborscountCall)
        CASE_ADD_CALL(MpiGraphdimsgetCall)
        CASE_ADD_CALL(MpiGrequestcompleteCall)
        CASE_ADD_CALL(MpiGrequeststartCall)
        CASE_ADD_CALL(MpiGroupcompareCall)
        CASE_ADD_CALL(MpiGroupdifferenceCall)
        CASE_ADD_CALL(MpiGroupexclCall)
        CASE_ADD_CALL(MpiGroupfreeCall)
        CASE_ADD_CALL(MpiGroupinclCall)
        CASE_ADD_CALL(MpiGroupintersectionCall)
        CASE_ADD_CALL(MpiGrouprangeexclCall)
        CASE_ADD_CALL(MpiGrouprangeinclCall)
        CASE_ADD_CALL(MpiGrouprankCall)
        CASE_ADD_CALL(MpiGroupsizeCall)
        CASE_ADD_CALL(MpiGrouptranslateranksCall)
        CASE_ADD_CALL(MpiGroupunionCall)
        CASE_ADD_CALL(MpiInfocreateCall)
        CASE_ADD_CALL(MpiInfodeleteCall)
        CASE_ADD_CALL(MpiInfodupCall)
        CASE_ADD_CALL(MpiInfofreeCall)
        CASE_ADD_CALL(MpiInfogetCall)
        CASE_ADD_CALL(MpiInfogetnkeysCall)
        CASE_ADD_CALL(MpiInfogetnthkeyCall)
        CASE_ADD_CALL(MpiInfogetvaluelenCall)
        CASE_ADD_CALL(MpiInfosetCall)
        CASE_ADD_CALL(MpiInitCall)
        CASE_ADD_CALL(MpiInitthreadCall)
		CASE_IGNORE(MpiInitializedCall)
        CASE_ADD_CALL(MpiIntercommcreateCall)
        CASE_ADD_CALL(MpiIntercommmergeCall)
        CASE_ADD_CALL(MpiIprobeCall)
        CASE_ADD_CALL(MpiIrecvCall)
        CASE_ADD_CALL(MpiIrsendCall)
        CASE_ADD_CALL(MpiIsthreadmainCall)
        CASE_ADD_CALL(MpiIsendCall)
        CASE_ADD_CALL(MpiIssendCall)
        CASE_ADD_CALL(MpiKeyvalcreateCall)
        CASE_ADD_CALL(MpiKeyvalfreeCall)
        CASE_ADD_CALL(MpiLookupnameCall)
        CASE_ADD_CALL(MpiOpcreateCall)
        CASE_ADD_CALL(MpiOpfreeCall)
        CASE_ADD_CALL(MpiOpenportCall)
        CASE_ADD_CALL(MpiPackCall)
        CASE_ADD_CALL(MpiPackexternalCall)
        CASE_ADD_CALL(MpiPackexternalsizeCall)
        CASE_ADD_CALL(MpiPacksizeCall)
        CASE_ADD_CALL(MpiProbeCall)
        CASE_ADD_CALL(MpiPublishnameCall)
        CASE_ADD_CALL(MpiPutCall)
        CASE_ADD_CALL(MpiQuerythreadCall)
        CASE_ADD_CALL(MpiRecvCall)
        CASE_ADD_CALL(MpiRecvinitCall)
        CASE_ADD_CALL(MpiReduceCall)
        CASE_ADD_CALL(MpiReducescatterCall)
        CASE_ADD_CALL(MpiRegisterdatarepCall)
        CASE_ADD_CALL(MpiRequestfreeCall)
        CASE_ADD_CALL(MpiRequestgetstatusCall)
        CASE_ADD_CALL(MpiRsendCall)
        CASE_ADD_CALL(MpiRsendinitCall)
        CASE_ADD_CALL(MpiScanCall)
        CASE_ADD_CALL(MpiScatterCall)
        CASE_ADD_CALL(MpiScattervCall)
        CASE_ADD_CALL(MpiSendCall)
        CASE_ADD_CALL(MpiSendinitCall)
        CASE_ADD_CALL(MpiSendrecvCall)
        CASE_ADD_CALL(MpiSendrecvreplaceCall)
        CASE_ADD_CALL(MpiSsendCall)
        CASE_ADD_CALL(MpiSsendinitCall)
        CASE_ADD_CALL(MpiStartCall)
        CASE_ADD_CALL(MpiStartallCall)
        CASE_ADD_CALL(MpiStatussetcancelledCall)
        CASE_ADD_CALL(MpiStatussetelementsCall)
        CASE_ADD_CALL(MpiTestCall)
        CASE_ADD_CALL(MpiTestcancelledCall)
        CASE_ADD_CALL(MpiTestallCall)
        CASE_ADD_CALL(MpiTestanyCall)
        CASE_ADD_CALL(MpiTestsomeCall)
        CASE_ADD_CALL(MpiTopotestCall)
        CASE_ADD_CALL(MpiTypecommitCall)
        CASE_ADD_CALL(MpiTypecontiguousCall)
        CASE_ADD_CALL(MpiTypecreatedarrayCall)
        CASE_ADD_CALL(MpiTypecreatehindexedCall)
        CASE_ADD_CALL(MpiTypecreatehvectorCall)
        CASE_ADD_CALL(MpiTypecreateindexedblockCall)
        CASE_ADD_CALL(MpiTypecreatekeyvalCall)
        CASE_ADD_CALL(MpiTypecreateresizedCall)
        CASE_ADD_CALL(MpiTypecreatestructCall)
        CASE_ADD_CALL(MpiTypecreatesubarrayCall)
        CASE_ADD_CALL(MpiTypedeleteattrCall)
        CASE_ADD_CALL(MpiTypedupCall)
        CASE_ADD_CALL(MpiTypeextentCall)
        CASE_ADD_CALL(MpiTypefreeCall)
        CASE_ADD_CALL(MpiTypefreekeyvalCall)
        CASE_ADD_CALL(MpiTypegetattrCall)
        CASE_ADD_CALL(MpiTypegetcontentsCall)
        CASE_ADD_CALL(MpiTypegetenvelopeCall)
        CASE_ADD_CALL(MpiTypegetextentCall)
        CASE_ADD_CALL(MpiTypegetnameCall)
        CASE_ADD_CALL(MpiTypegettrueextentCall)
        CASE_ADD_CALL(MpiTypehindexedCall)
        CASE_ADD_CALL(MpiTypehvectorCall)
        CASE_ADD_CALL(MpiTypeindexedCall)
        CASE_ADD_CALL(MpiTypelbCall)
        CASE_ADD_CALL(MpiTypematchsizeCall)
        CASE_ADD_CALL(MpiTypesetattrCall)
        CASE_ADD_CALL(MpiTypesetnameCall)
        CASE_ADD_CALL(MpiTypesizeCall)
        CASE_ADD_CALL(MpiTypestructCall)
        CASE_ADD_CALL(MpiTypeubCall)
        CASE_ADD_CALL(MpiTypevectorCall)
        CASE_ADD_CALL(MpiUnpackCall)
        CASE_ADD_CALL(MpiUnpackexternalCall)
        CASE_ADD_CALL(MpiUnpublishnameCall)
        CASE_ADD_CALL(MpiWaitCall)
        CASE_ADD_CALL(MpiWaitallCall)
        CASE_ADD_CALL(MpiWaitanyCall)
        CASE_ADD_CALL(MpiWaitsomeCall)
        CASE_ADD_CALL(MpiWincallerrhandlerCall)
        CASE_ADD_CALL(MpiWincompleteCall)
        CASE_ADD_CALL(MpiWincreateCall)
        CASE_ADD_CALL(MpiWincreateerrhandlerCall)
        CASE_ADD_CALL(MpiWincreatekeyvalCall)
        CASE_ADD_CALL(MpiWindeleteattrCall)
        CASE_ADD_CALL(MpiWinfenceCall)
        CASE_ADD_CALL(MpiWinfreeCall)
        CASE_ADD_CALL(MpiWinfreekeyvalCall)
        CASE_ADD_CALL(MpiWingetattrCall)
        CASE_ADD_CALL(MpiWingeterrhandlerCall)
        CASE_ADD_CALL(MpiWingetgroupCall)
        CASE_ADD_CALL(MpiWingetnameCall)
        CASE_ADD_CALL(MpiWinlockCall)
        CASE_ADD_CALL(MpiWinpostCall)
        CASE_ADD_CALL(MpiWinsetattrCall)
        CASE_ADD_CALL(MpiWinseterrhandlerCall)
        CASE_ADD_CALL(MpiWinsetnameCall)
        CASE_ADD_CALL(MpiWinstartCall)
        CASE_ADD_CALL(MpiWintestCall)
        CASE_ADD_CALL(MpiWinunlockCall)
        CASE_ADD_CALL(MpiWinwaitCall)
        CASE_ADD_CALL(MpiWtickCall)
        CASE_ADD_CALL(MpiWtimeCall)

    default:
        cout << "ERROR: 'event_enter' callback did not capture event: " << id << endl;
    }


#undef ADD_CALL
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode event_leave(
    OTF2_LocationRef    location,
    OTF2_TimeStamp      time,
    uint64_t            eventPosition,
    void*               userData,
    OTF2_AttributeList* attributes,
    OTF2_RegionRef      region ) {

    auto app = (OTF2_trace_replay_app*)userData;
    CallQueue& callqueue = app->get_callqueue();

    // Record end time and trigger the call
	#define CASE_READY(_class) case _class::id : { \
		auto call = callqueue.find_latest<_class>(); \
		CallBase::assert_call(call, "Lookup for " #_class " in 'event_leave' returned NULL"); \
		EVENT_PRINT("LEAVE " << call->ToString() << " time: " << time); \
		call->end_time = time; \
		callqueue.CallReady(call); \
		break;}

    // Record end time and do not trigger the call. This happens when
    // there is not enough information yet available in the callback.
    #define CASE_NOT_READY(_class) case _class::id : { \
		auto call = callqueue.find_latest<_class>(); \
		CallBase::assert_call(call, "Lookup for " #_class " in 'event_leave' returned NULL"); \
		EVENT_PRINT("LEAVE " << call->ToString() << " time: " << time); \
		call->end_time = time; \
		break;}

    const auto id = app->otf2_mpi_call_map[app->otf2_regions[region].name];

    switch (id) {
        CASE_READY(MpiAbortCall)
        CASE_READY(MpiAccumulateCall)
        CASE_READY(MpiAdderrorclassCall)
        CASE_READY(MpiAdderrorcodeCall)
        CASE_READY(MpiAdderrorstringCall)
        CASE_READY(MpiAddressCall)
        CASE_READY(MpiAllgatherCall)
        CASE_READY(MpiAllgathervCall)
        CASE_READY(MpiAllocmemCall)
        CASE_READY(MpiAllreduceCall)
        CASE_READY(MpiAlltoallCall)
        CASE_READY(MpiAlltoallvCall)
        CASE_READY(MpiAlltoallwCall)
        CASE_READY(MpiAttrdeleteCall)
        CASE_READY(MpiAttrgetCall)
        CASE_READY(MpiAttrputCall)
        CASE_READY(MpiBarrierCall)
        CASE_READY(MpiBcastCall)
        CASE_READY(MpiBsendCall)
        CASE_READY(MpiBsendinitCall)
        CASE_READY(MpiBufferattachCall)
        CASE_READY(MpiBufferdetachCall)
        CASE_READY(MpiCancelCall)
        CASE_READY(MpiCartcoordsCall)
        CASE_READY(MpiCartcreateCall)
        CASE_READY(MpiCartgetCall)
        CASE_READY(MpiCartmapCall)
        CASE_READY(MpiCartrankCall)
        CASE_READY(MpiCartshiftCall)
        CASE_READY(MpiCartsubCall)
        CASE_READY(MpiCartdimgetCall)
        CASE_READY(MpiCloseportCall)
        CASE_READY(MpiCommacceptCall)
        CASE_READY(MpiCommcallerrhandlerCall)
        CASE_READY(MpiCommcompareCall)
        CASE_READY(MpiCommconnectCall)
        CASE_READY(MpiCommcreateCall)
        CASE_READY(MpiCommcreateerrhandlerCall)
        CASE_READY(MpiCommcreatekeyvalCall)
        CASE_READY(MpiCommdeleteattrCall)
        CASE_READY(MpiCommdisconnectCall)
        CASE_READY(MpiCommdupCall)
        CASE_READY(MpiCommfreeCall)
        CASE_READY(MpiCommfreekeyvalCall)
        CASE_READY(MpiCommgetattrCall)
        CASE_READY(MpiCommgeterrhandlerCall)
        CASE_READY(MpiCommgetnameCall)
        CASE_READY(MpiCommgetparentCall)
        CASE_READY(MpiCommgroupCall)
        CASE_READY(MpiCommjoinCall)
		CASE_IGNORE(MpiCommrankCall)
        CASE_READY(MpiCommremotegroupCall)
        CASE_READY(MpiCommremotesizeCall)
        CASE_READY(MpiCommsetattrCall)
        CASE_READY(MpiCommseterrhandlerCall)
        CASE_READY(MpiCommsetnameCall)
		CASE_IGNORE(MpiCommsizeCall)
        CASE_READY(MpiCommspawnCall)
        CASE_READY(MpiCommspawnmultipleCall)
        CASE_READY(MpiCommsplitCall)
        CASE_NOT_READY(MpiCommtestinterCall)
        CASE_READY(MpiDimscreateCall)
        CASE_READY(MpiErrhandlercreateCall)
        CASE_READY(MpiErrhandlerfreeCall)
        CASE_READY(MpiErrhandlergetCall)
        CASE_READY(MpiErrhandlersetCall)
        CASE_READY(MpiErrorclassCall)
        CASE_READY(MpiErrorstringCall)
        CASE_READY(MpiExscanCall)
        CASE_READY(MpiFilecallerrhandlerCall)
        CASE_READY(MpiFilecloseCall)
        CASE_READY(MpiFilecreateerrhandlerCall)
        CASE_READY(MpiFiledeleteCall)
        CASE_READY(MpiFilegetamodeCall)
        CASE_READY(MpiFilegetatomicityCall)
        CASE_READY(MpiFilegetbyteoffsetCall)
        CASE_READY(MpiFilegeterrhandlerCall)
        CASE_READY(MpiFilegetgroupCall)
        CASE_READY(MpiFilegetinfoCall)
        CASE_READY(MpiFilegetpositionCall)
        CASE_READY(MpiFilegetpositionsharedCall)
        CASE_READY(MpiFilegetsizeCall)
        CASE_READY(MpiFilegettypeextentCall)
        CASE_READY(MpiFilegetviewCall)
        CASE_READY(MpiFileireadCall)
        CASE_READY(MpiFileireadatCall)
        CASE_READY(MpiFileireadsharedCall)
        CASE_READY(MpiFileiwriteCall)
        CASE_READY(MpiFileiwriteatCall)
        CASE_READY(MpiFileiwritesharedCall)
        CASE_READY(MpiFileopenCall)
        CASE_READY(MpiFilepreallocateCall)
        CASE_READY(MpiFilereadCall)
        CASE_READY(MpiFilereadallCall)
        CASE_READY(MpiFilereadallbeginCall)
        CASE_READY(MpiFilereadallendCall)
        CASE_READY(MpiFilereadatCall)
        CASE_READY(MpiFilereadatallCall)
        CASE_READY(MpiFilereadatallbeginCall)
        CASE_READY(MpiFilereadatallendCall)
        CASE_READY(MpiFilereadorderedCall)
        CASE_READY(MpiFilereadorderedbeginCall)
        CASE_READY(MpiFilereadorderedendCall)
        CASE_READY(MpiFilereadsharedCall)
        CASE_READY(MpiFileseekCall)
        CASE_READY(MpiFileseeksharedCall)
        CASE_READY(MpiFilesetatomicityCall)
        CASE_READY(MpiFileseterrhandlerCall)
        CASE_READY(MpiFilesetinfoCall)
        CASE_READY(MpiFilesetsizeCall)
        CASE_READY(MpiFilesetviewCall)
        CASE_READY(MpiFilesyncCall)
        CASE_READY(MpiFilewriteCall)
        CASE_READY(MpiFilewriteallCall)
        CASE_READY(MpiFilewriteallbeginCall)
        CASE_READY(MpiFilewriteallendCall)
        CASE_READY(MpiFilewriteatCall)
        CASE_READY(MpiFilewriteatallCall)
        CASE_READY(MpiFilewriteatallbeginCall)
        CASE_READY(MpiFilewriteatallendCall)
        CASE_READY(MpiFilewriteorderedCall)
        CASE_READY(MpiFilewriteorderedbeginCall)
        CASE_READY(MpiFilewriteorderedendCall)
        CASE_READY(MpiFilewritesharedCall)
        CASE_READY(MpiFinalizeCall)
        CASE_READY(MpiFinalizedCall)
        CASE_READY(MpiFreememCall)
        CASE_READY(MpiGatherCall)
        CASE_READY(MpiGathervCall)
        CASE_READY(MpiGetCall)
        CASE_READY(MpiGetaddressCall)
        CASE_READY(MpiGetcountCall)
        CASE_READY(MpiGetelementsCall)
        CASE_READY(MpiGetprocessornameCall)
        CASE_READY(MpiGetversionCall)
        CASE_READY(MpiGraphcreateCall)
        CASE_READY(MpiGraphgetCall)
        CASE_READY(MpiGraphmapCall)
        CASE_READY(MpiGraphneighborsCall)
        CASE_READY(MpiGraphneighborscountCall)
        CASE_READY(MpiGraphdimsgetCall)
        CASE_READY(MpiGrequestcompleteCall)
        CASE_READY(MpiGrequeststartCall)
        CASE_READY(MpiGroupcompareCall)
        CASE_READY(MpiGroupdifferenceCall)
        CASE_READY(MpiGroupexclCall)
        CASE_READY(MpiGroupfreeCall)
        CASE_READY(MpiGroupinclCall)
        CASE_READY(MpiGroupintersectionCall)
        CASE_READY(MpiGrouprangeexclCall)
        CASE_READY(MpiGrouprangeinclCall)
        CASE_READY(MpiGrouprankCall)
        CASE_READY(MpiGroupsizeCall)
        CASE_READY(MpiGrouptranslateranksCall)
        CASE_READY(MpiGroupunionCall)
        CASE_READY(MpiInfocreateCall)
        CASE_READY(MpiInfodeleteCall)
        CASE_READY(MpiInfodupCall)
        CASE_READY(MpiInfofreeCall)
        CASE_READY(MpiInfogetCall)
        CASE_READY(MpiInfogetnkeysCall)
        CASE_READY(MpiInfogetnthkeyCall)
        CASE_READY(MpiInfogetvaluelenCall)
        CASE_READY(MpiInfosetCall)
        CASE_READY(MpiInitCall)
        CASE_READY(MpiInitthreadCall)
		CASE_IGNORE(MpiInitializedCall)
        CASE_READY(MpiIntercommcreateCall)
        CASE_READY(MpiIntercommmergeCall)
        CASE_READY(MpiIprobeCall)
		CASE_NOT_READY(MpiIrecvCall)
        CASE_READY(MpiIrsendCall)
        CASE_READY(MpiIsthreadmainCall)
        CASE_READY(MpiIsendCall)
        CASE_READY(MpiIssendCall)
        CASE_READY(MpiKeyvalcreateCall)
        CASE_READY(MpiKeyvalfreeCall)
        CASE_READY(MpiLookupnameCall)
        CASE_READY(MpiOpcreateCall)
        CASE_READY(MpiOpfreeCall)
        CASE_READY(MpiOpenportCall)
        CASE_READY(MpiPackCall)
        CASE_READY(MpiPackexternalCall)
        CASE_READY(MpiPackexternalsizeCall)
        CASE_READY(MpiPacksizeCall)
        CASE_READY(MpiProbeCall)
        CASE_READY(MpiPublishnameCall)
        CASE_READY(MpiPutCall)
        CASE_READY(MpiQuerythreadCall)
        CASE_READY(MpiRecvCall)
        CASE_READY(MpiRecvinitCall)
        CASE_READY(MpiReduceCall)
        CASE_READY(MpiReducescatterCall)
        CASE_READY(MpiRegisterdatarepCall)
        CASE_READY(MpiRequestfreeCall)
        CASE_READY(MpiRequestgetstatusCall)
        CASE_READY(MpiRsendCall)
        CASE_READY(MpiRsendinitCall)
        CASE_READY(MpiScanCall)
        CASE_READY(MpiScatterCall)
        CASE_READY(MpiScattervCall)
        CASE_READY(MpiSendCall)
        CASE_READY(MpiSendinitCall)
        CASE_READY(MpiSendrecvCall)
        CASE_READY(MpiSendrecvreplaceCall)
        CASE_READY(MpiSsendCall)
        CASE_READY(MpiSsendinitCall)
        CASE_READY(MpiStartCall)
        CASE_READY(MpiStartallCall)
        CASE_READY(MpiStatussetcancelledCall)
        CASE_READY(MpiStatussetelementsCall)
        CASE_NOT_READY(MpiTestCall)
        CASE_NOT_READY(MpiTestcancelledCall)
        CASE_NOT_READY(MpiTestallCall)
        CASE_NOT_READY(MpiTestanyCall)
        CASE_NOT_READY(MpiTestsomeCall)
        CASE_NOT_READY(MpiTopotestCall)
        CASE_READY(MpiTypecommitCall)
        CASE_READY(MpiTypecontiguousCall)
        CASE_READY(MpiTypecreatedarrayCall)
        CASE_READY(MpiTypecreatehindexedCall)
        CASE_READY(MpiTypecreatehvectorCall)
        CASE_READY(MpiTypecreateindexedblockCall)
        CASE_READY(MpiTypecreatekeyvalCall)
        CASE_READY(MpiTypecreateresizedCall)
        CASE_READY(MpiTypecreatestructCall)
        CASE_READY(MpiTypecreatesubarrayCall)
        CASE_READY(MpiTypedeleteattrCall)
        CASE_READY(MpiTypedupCall)
        CASE_READY(MpiTypeextentCall)
        CASE_READY(MpiTypefreeCall)
        CASE_READY(MpiTypefreekeyvalCall)
        CASE_READY(MpiTypegetattrCall)
        CASE_READY(MpiTypegetcontentsCall)
        CASE_READY(MpiTypegetenvelopeCall)
        CASE_READY(MpiTypegetextentCall)
        CASE_READY(MpiTypegetnameCall)
        CASE_READY(MpiTypegettrueextentCall)
        CASE_READY(MpiTypehindexedCall)
        CASE_READY(MpiTypehvectorCall)
        CASE_READY(MpiTypeindexedCall)
        CASE_READY(MpiTypelbCall)
        CASE_READY(MpiTypematchsizeCall)
        CASE_READY(MpiTypesetattrCall)
        CASE_READY(MpiTypesetnameCall)
        CASE_READY(MpiTypesizeCall)
        CASE_READY(MpiTypestructCall)
        CASE_READY(MpiTypeubCall)
        CASE_READY(MpiTypevectorCall)
        CASE_READY(MpiUnpackCall)
        CASE_READY(MpiUnpackexternalCall)
        CASE_READY(MpiUnpublishnameCall)
        CASE_READY(MpiWaitCall)
        CASE_READY(MpiWaitallCall)
        CASE_READY(MpiWaitanyCall)
        CASE_READY(MpiWaitsomeCall)
        CASE_READY(MpiWincallerrhandlerCall)
        CASE_READY(MpiWincompleteCall)
        CASE_READY(MpiWincreateCall)
        CASE_READY(MpiWincreateerrhandlerCall)
        CASE_READY(MpiWincreatekeyvalCall)
        CASE_READY(MpiWindeleteattrCall)
        CASE_READY(MpiWinfenceCall)
        CASE_READY(MpiWinfreeCall)
        CASE_READY(MpiWinfreekeyvalCall)
        CASE_READY(MpiWingetattrCall)
        CASE_READY(MpiWingeterrhandlerCall)
        CASE_READY(MpiWingetgroupCall)
        CASE_READY(MpiWingetnameCall)
        CASE_READY(MpiWinlockCall)
        CASE_READY(MpiWinpostCall)
        CASE_READY(MpiWinsetattrCall)
        CASE_READY(MpiWinseterrhandlerCall)
        CASE_READY(MpiWinsetnameCall)
        CASE_READY(MpiWinstartCall)
        CASE_READY(MpiWintestCall)
        CASE_READY(MpiWinunlockCall)
        CASE_READY(MpiWinwaitCall)
        CASE_READY(MpiWtickCall)
        CASE_READY(MpiWtimeCall)
    default:
        cout << "ERROR: 'event_leave' callback did not capture event: " << id << endl;
    }

#undef CASE_READY
#undef CASE_NOT_READY
    return OTF2_CALLBACK_SUCCESS;
}

#undef CASE_IGNORE
