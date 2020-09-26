/*
 * Copyright (c) 2015, 2019 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/tre/tre.hh"

#include "base/random.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "debug/TRE.hh"
#include "sim/sim_exit.hh"
#include "sim/stat_control.hh"
#include "sim/stats.hh"
#include "sim/system.hh"

using namespace std;

static unsigned int TESTER_ALLOCATOR = 0, numIdleTREs = 0;

std::map<Addr, TREMutex> TRE::mutexMap;
std::map<Addr, TREBarrier> TRE::barrierMap;
std::vector<TRE *> TRE::allTREs;
std::map<std::pair<unsigned, unsigned>, TREQueue *> TRE::queues;

void
TRE::initQueues(unsigned long depth)
{
    // queues.at(std::make_pair(source, sink))(new TREQueue(depth));
    // begin generated code for init_queues
    queues.insert(std::make_pair(std::make_pair(0, 1), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 2), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 3), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 4), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 5), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 6), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 7), new TREQueue(depth)));
    queues.insert(std::make_pair(std::make_pair(0, 8), new TREQueue(depth)));
    // end generated code for init_queues
}

void 
TRE::teardownQueues(void)
{
    for (auto &e : queues) {
        delete e.second;
    }
    queues.clear();
}

bool
TRE::CpuPort::recvTimingResp(PacketPtr pkt)
{
    tre.completeRequest(pkt);
    return true;
}

void
TRE::CpuPort::recvReqRetry()
{
    tre.recvRetry();
}

bool
TRE::sendPkt(PacketPtr pkt) {
    if (atomic) {
        port.sendAtomic(pkt);
        completeRequest(pkt);
    } else {
        if (!port.sendTimingReq(pkt)) {
            retryPkt = pkt;
            return false;
        }
    }
    return true;
}

TRE::TRE(const Params *p)
    : ClockedObject(p),
      tickEvent([this]{ tick(); }, name()),
      noRequestEvent([this]{ noRequest(); }, name()),
      noResponseEvent([this]{ noResponse(); }, name()),
      port("port", *this),
      retryPkt(nullptr),
      size(p->size),
      interval(p->interval),
      masterId(p->system->getMasterId(this)),
      id(p->id),
      queueDepth(p->queue_depth),
      blockSize(p->system->cacheLineSize()),
      blockAddrMask(blockSize - 1),
      blocked(false),
      traceRoot(p->trace_root),
      paddr(0),
      data(0),
      progressInterval(p->progress_interval),
      progressCheck(p->progress_check),
      nextProgressMessage(p->progress_interval),
      atomic(p->system->isAtomicMode()),
      suppressFuncErrors(p->suppress_func_errors),
      maxNumOutstandingAddrs(p->max_outstanding_addrs),
      lastTokEntryPos(0)
{

    // set up counters
    numLoads = 0;
    numStores = 0;
    numTraceEntries = 0;
    numBlkingAccesses = 0;

    DPRINTF(TRE, "TRE[%u] booting up with clock period %lu ticks\n", id, clockPeriod());

    // open trace files
    std::string traceFilename = traceRoot + "/pe_" + std::to_string(id) + ".trace";

    DPRINTF(TRE, "Opening trace file: %s\n", traceFilename.c_str());
    trace_fh.open(traceFilename.c_str());
    if (!trace_fh) {
        warn("TRE[%u] has no work allotted to it", id);
        return;
    }

    // populate global array of pointers to TRE memobjects
    allTREs.push_back(this);

    // kick things into action
    DPRINTF(TRE, "Scheduling first tick() event @%lu\n", clockEdge(interval));
    schedule(tickEvent, curTick());
    schedule(noRequestEvent, clockEdge(progressCheck));

    // hook up events for explicit primitive handling
    initMutexEvent   = new EventFunctionWrapper([this] { initMutex(); }, name());
    lockMutexEvent   = new EventFunctionWrapper([this] { lockMutex(); }, name());
    unlockMutexEvent = new EventFunctionWrapper([this] { unlockMutex(); }, name());

    initBarrierEvent   = new EventFunctionWrapper([this] { initBarrier(); }, name());
    waitAtBarrierEvent = new EventFunctionWrapper([this] { waitAtBarrier(); }, name());

    sleepEvent = new EventFunctionWrapper([this] { sleep(); }, name());
    wakeEvent  = new EventFunctionWrapper([this] { wake(); }, name());

    pushEvent = new EventFunctionWrapper([this] { push(); }, name());
    popEvent  = new EventFunctionWrapper([this] { pop(); }, name());

    dumpStatsEvent      = new EventFunctionWrapper([this] { dumpstats(0, 0); }, name());
    resetStatsEvent     = new EventFunctionWrapper([this] { resetstats(0, 0); }, name());
    dumpResetStatsEvent = new EventFunctionWrapper([this] { dumpresetstats(0, 0); }, name());

    // initialize queues connecting TREs (only done once)
    if (TESTER_ALLOCATOR == 0 && queueDepth != 0) {
        DPRINTF(TRE, "Initializing the TRE queue interface with depth = %lu\n", queueDepth);
        initQueues(queueDepth);
    }

    TESTER_ALLOCATOR++;
}

TRE::~TRE() {
    TESTER_ALLOCATOR--;

    delete initMutexEvent;
    delete lockMutexEvent;
    delete unlockMutexEvent;

    delete initBarrierEvent;
    delete waitAtBarrierEvent;

    delete sleepEvent;
    delete wakeEvent;

    delete pushEvent;
    delete popEvent;

    delete dumpStatsEvent;
    delete resetStatsEvent;
    delete dumpResetStatsEvent;

    // teardown queues connecting TREs (only done once)
    if (TESTER_ALLOCATOR == 0 && queueDepth != 0)
        teardownQueues();
}

Port &
TRE::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port")
        return port;
    else
        return ClockedObject::getPort(if_name, idx);
}

void
TRE::completeRequest(PacketPtr pkt, bool functional)
{
    const RequestPtr &req = pkt->req;
    assert(req->getSize() == 4);

    DPRINTF(TRE, "Completing %s request to address 0x%x (blk 0x%x) %s\n",
            pkt->isWrite() ? "write" : "read",
            req->getPaddr(), blockAlign(req->getPaddr()),
            pkt->isError() ? "error" : "success");

    // this address is no longer outstanding
    bool found = addrInFlight(req->getPaddr(), true/*erase*/);
    fatal_if(!found, "Received unsolicited response to TRE\n");

    if (pkt->isError()) {
        if (!functional || !suppressFuncErrors)
            panic( "%s access failed at %#x\n",
                pkt->isWrite() ? "Write" : "Read", req->getPaddr());
    } else {
        if (pkt->isRead()) {
            numLoads++;
            if ((long unsigned int)(numLoads.value()) == (uint64_t)nextProgressMessage) {
                ccprintf(cerr, "%s: completed %lu read, %lu write accesses @%d\n", name(),
                         (long unsigned int)(numLoads.value()),
                         (long unsigned int)(numStores.value()), curTick());
                nextProgressMessage += progressInterval;
            }
        } else {
            assert(pkt->isWrite());
            numStores++;
        }
    }

    // the packet will delete the data
    delete pkt;

    // if we were blocked previously, we need to schedule a new tickEvent now
    if (!tickEvent.scheduled() && blocked) {
        DPRINTF(TRE, "Scheduling next tick() event @%lu\n", clockEdge(interval));
        schedule(tickEvent, clockEdge(interval));
    }

    // finally shift the response timeout forward if we are still
    // expecting responses; deschedule it otherwise
    if (outstandingAddrs.size() != 0)
        reschedule(noResponseEvent, clockEdge(progressCheck));
    else if (noResponseEvent.scheduled())
        deschedule(noResponseEvent);
}

void
TRE::regStats()
{
    ClockedObject::regStats();

    using namespace Stats;

    numLoads
        .name(name() + ".num_reads")
        .desc("number of read accesses completed")
        ;

    numStores
        .name(name() + ".num_writes")
        .desc("number of write accesses completed")
        ;

    numTraceEntries
        .name(name() + ".num_trace_entries")
        .desc("number of trace entries (\"instructions\") completed")
        ;

    numBlkingAccesses
        .name(name() + ".num_blocking_accesses")
        .desc("number of blocking accesses completed")
        ;
}

void
TRE::tick() 
{
    // we should never tick if we are waiting for a retry
    assert(!retryPkt);

    // create a new request
    TRECmd cmd = TRECmd::LD;
    bool uncacheable = false;
    bool blkingAccess = false;
    Request::Flags flags;
    Addr depAddr;

    string word;
    Addr pc = -1;

    if (blocked) {
        DPRINTF(TRE, "Tick() called after a dependency response returned\n");
        // check each address in the blockedAddr dependency list to see if any of them are in the
        // outstandingAddrs queue; if yes, we'll have to wait until that response comes back before
        // we issue this access
        for (set<Addr>::iterator it = blockedPkt.deps.begin(); it != blockedPkt.deps.end(); ++it) {
            if (addrInFlight(*it)) { // If this dep hasn't returned.
                DPRINTF(TRE,
                        "Response to address 0x%x hasn't returned, will retry when the response "
                        "returns\n",
                        *it);
                return;
            }
        }
        // if all dependencies have returned, proceed with the pending instruction
        blocked = false;
        // clear dependencies of blocked packet
        blockedPkt.deps.clear();

        // extract metadata from blocked packet        
        paddr = blockedPkt.addr;
        cmd = blockedPkt.cmd;
        data = blockedPkt.data; // we need this only for TRECmd::STALL

        DPRINTF(TRE, "Blocked packet cmd: %s, addr: 0x%x, data: %u\n", cmd2str(cmd), paddr, data);

        uncacheable = blockedPkt.uncacheable;
        blkingAccess = blockedPkt.blking;
    } else {
        lastTokEntryPos = trace_fh.tellg();
        word = readNextToken();
        if (!word.compare("EOF")) { // halt TRE upon encountering EOF symbol
            halt();
            return;
            // create the packet based on the token read
        } else {
            if (!word.compare("LD") ||   // non-blocking load
                !word.compare("LDU") ||  // uncacheable load
                !word.compare("LDB") ||  // blocking load
                !word.compare("LDUB") ||  // blocking uncacheable load
                !word.compare("ST") ||   // non-blocking store
                !word.compare("STU") ||  // non-blocking uncacheable store
                !word.compare("STB") ||  // blocking store
                !word.compare("STUB")) { // blocking uncacheable store

                cmd = (!word.compare("LD") || !word.compare("LDU") || !word.compare("LDB") ||
                       !word.compare("LDUB"))
                          ? TRECmd::LD
                          : TRECmd::ST;
                blkingAccess = !word.compare("LDB") || !word.compare("LDUB") ||
                               !word.compare("STB") || !word.compare("STUB");
                uncacheable = !word.compare("LDU") || !word.compare("LDUB") ||
                              !word.compare("STU") || !word.compare("STUB");
                if (blkingAccess)
                    ++numBlkingAccesses;

                // get the virtual PC and address
                word = readNextToken();
                if (word.front() == '@') {
                    pc = stol(word.substr(1));
                    DPRINTF(TRE, "Setting virtual PC of LD to 0x%x\n", pc);
                    word = readNextToken();
                }
                paddr = stol(word, nullptr, 16); // base 16

                if (outstandingAddrs.size() >= maxNumOutstandingAddrs) {
                    blocked = true;
                    DPRINTF(TRE,
                            "Blocking access to 0x%x because outstandingReq queue is full\n", paddr);
                }
                // if there is a blocking access amongst the accesses currently in flight, block the
                // current packet until the response to that access returns
                if ((depAddr = blkingAccAddrInFlight()) != INVALID_PADDR) {
                    blocked = true;
                    assert(blockedPkt.deps.empty());
                    blockedPkt.deps.insert(depAddr);
                    DPRINTF(TRE,
                            "Blocking access to 0x%x because blocking load to 0x%x is in flight\n",
                            paddr, depAddr);
                }

                word = readNextToken();
                fatal_if(word.compare("("),
                         "Expecting parenthesis indicating the start of dependency list");
                word = readNextToken();
                while (word.compare(")")) {
                    depAddr = stol(word, nullptr, 16);
                    DPRINTF(TRE, "Dependency for access @0x%x: 0x%x\n", paddr, depAddr);
                    if (addrInFlight(depAddr)) { // if this request wasn't completed
                        blocked = true;
                        blockedPkt.deps.insert(depAddr);
                        DPRINTF(TRE, "Value @ address 0x%x has not yet returned\n", depAddr);
                    }
                    word = readNextToken();
                }
                if (blocked) {
                    blockedPkt.addr = paddr;
                    blockedPkt.cmd = cmd;
                    blockedPkt.uncacheable = uncacheable;
                    blockedPkt.blking = blkingAccess;
                    if (DTRACE(TRE)) {
                        DPRINTF(TRE, "Blocking PKT [0x%x, %s, uncache=%d], dependencies: {",
                                (unsigned)paddr, cmd2str(cmd), 0);
                        for (set<Addr>::iterator it = blockedPkt.deps.begin();
                             it != blockedPkt.deps.end(); ++it) {
                            printf(" 0x%x,", (unsigned)*it);
                        }
                        printf(" }\n");
                    }
                }
                numTraceEntries += 1;
            } else if (!word.compare("STALL")        // regular stall
                    || !word.compare("STALLB")) {    // blocking stall
                cmd = TRECmd::STALL;
                blkingAccess = !word.compare("STALLB");
                if (blkingAccess)
                    ++numBlkingAccesses;

                data = stol(readNextToken()); // this is the stall count
                word = readNextToken();
                assert(!word.compare("(") &&
                       "Expecting paranthesis indicating start of dependency list");
                word = readNextToken();
                while (word.compare(")")) {
                    depAddr = stol(word, nullptr, 16);
                    DPRINTF(TRE, "Dependency for STALL: 0x%x\n", depAddr);
                    if (addrInFlight(depAddr)) { // If this load wasn't completed.
                        blocked = true;
                        blockedPkt.deps.insert(depAddr);
                        DPRINTF(TRE, "Value @ address 0x%x has not yet returned\n", depAddr);
                    }
                    word = readNextToken();
                }
                // if there is a blocking access amongst the accesses currently in flight, block the
                // current packet until the response to that access returns
                if ((depAddr = blkingAccAddrInFlight()) != INVALID_PADDR) {
                    blocked = true;
                    assert(blockedPkt.deps.empty());
                    blockedPkt.deps.insert(depAddr);
                    DPRINTF(TRE, "Blocking STALL because blocking load to 0x%x is in flight\n",
                            depAddr);
                }
                if (blocked) {
                    assert(!blockedPkt.deps.empty());
                    blockedPkt.cmd = STALL;
                    blockedPkt.data = data;
                    blockedPkt.blking = blkingAccess;

                    if (DTRACE(TRE)) {
                        DPRINTF(TRE, "Blocking PKT [STALL %u], dependencies: {", data);
                        for (set<Addr>::iterator it = blockedPkt.deps.begin();
                             it != blockedPkt.deps.end(); ++it) {
                            printf(" 0x%x,", (unsigned)*it);
                        }
                        printf(" }\n");
                    }
                }
                numStalls += data;
                numTraceEntries += data;
            } else if (!word.compare("MTXINIT")) {
                // save mutex address to global variable
                paddr = stol(readNextToken(), nullptr, 16);
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    initMutex();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(initMutexEvent, when);
                    DPRINTF(TRE, "Scheduling initMutexEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("MTXLOCK")) {
                // save mutex address to global variable
                paddr = stol(readNextToken(), nullptr, 16);
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    lockMutex();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(lockMutexEvent, when);
                    DPRINTF(TRE, "Scheduling lockMutexEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("MTXUNLOCK")) {
                // save mutex address to global variable
                paddr = stol(readNextToken(), nullptr, 16);
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    unlockMutex();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(unlockMutexEvent, when);
                    DPRINTF(TRE, "Scheduling unlockMutexEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("BARINIT")) {
                // save barrier address to global variable
                paddr = stol(readNextToken(), nullptr, 16);
                // save barrier participant count to global variable
                data = stol(readNextToken());
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    initBarrier();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(initBarrierEvent, when);
                    DPRINTF(TRE, "Scheduling initBarrierEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("BARWAIT")) {
                // save barrier address to global variable
                paddr = stol(readNextToken(), nullptr, 16);
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    waitAtBarrier();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(waitAtBarrierEvent, when);
                    DPRINTF(TRE, "Scheduling waitAtBarrierEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("SLEEP")) {
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    sleep();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(sleepEvent, when);
                    DPRINTF(TRE, "Scheduling sleepEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("WAKE")) {
                // save PE # to wake up to global variable
                data = stoi(readNextToken());
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    wake();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(wakeEvent, when);
                    DPRINTF(TRE, "Scheduling wakeEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("POP")) {
                // save target PE # to global variable
                data = stoi(readNextToken());
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    pop();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(popEvent, when);
                    DPRINTF(TRE, "Scheduling popEvent at tick %lu\n", when);
                }
                return;
            } else if (!word.compare("PUSH")) {
                // save target PE # to global variable
                data = stoi(readNextToken());
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    push();
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    schedule(pushEvent, when);
                    DPRINTF(TRE, "Scheduling pushEvent at tick %lu\n", when);
                }
                return;
            } else if ((!word.compare("DMP")) ||    // DUMP_STATS()
                       (!word.compare("RST")) ||    // RESET_STATS()
                       (!word.compare("DMPRST"))) { // DUMP_RESET_STATS()
                printf("TRE[%u]: triggered %s\n", id, word.c_str());
                // get cycle count
                unsigned cycles = stol(readNextToken());
                if (cycles == 0) {
                    if (!word.compare("DMP")) {
                        dumpstats(0, 0);
                    } else if (!word.compare("RST")) {
                        resetstats(0, 0);
                    } else if (!word.compare("DMPRST")) {
                        dumpresetstats(0, 0);
                    }
                } else {
                    Tick when = clockEdge(Cycles(cycles));
                    if (!word.compare("DMP")) {
                        schedule(dumpStatsEvent, when);
                        DPRINTF(TRE, "Scheduling dumpStatsEvent at tick %lu\n", when);
                    } else if (!word.compare("RST")) {
                        schedule(resetStatsEvent, when);
                        DPRINTF(TRE, "Scheduling resetStatsEvent at tick %lu\n", when);
                    } else if (!word.compare("DMPRST")) {
                        schedule(dumpResetStatsEvent, when);
                        DPRINTF(TRE, "Scheduling dumpResetStatsEvent at tick %lu\n", when);
                    }
                }
                return;
            } else {
                fatal("Invalid token read: " + word);
            }
        }
    }

    // if blocked, return and wait for completeResponse() to schedule next tick event
    if (blocked)
        return;

    // create a new request based on the packet
    if (cmd == TRECmd::STALL) {
        DPRINTF(TRE, "Executing STALL for %u cycles\n", data);
        schedule(tickEvent, clockEdge(Cycles(interval * data)));
        return;
    }

    // mark access as uncacheable
    if (uncacheable) {
        flags.set(Request::UNCACHEABLE);
    }
    RequestPtr req = std::make_shared<Request>(paddr, 4, flags, masterId);
    req->setContext(id);

    // if valid virtual PC was extracted from the trace, set the PC of the new packet
    if (pc != -1)
        req->setPC(pc);
    DPRINTF(TRE, "Inserting into outstandingAddrs: {0x%x, %d}\n", paddr, blkingAccess);
    outstandingAddrs.push_back(std::make_pair(paddr, blkingAccess));

    // sanity check
    panic_if(outstandingAddrs.size() > 100, "Tester %s has more than 100 outstanding requests\n",
             name());

    PacketPtr pkt = nullptr;
    uint8_t *pkt_data = new uint8_t[1];

    if (cmd == TRECmd::LD) {
        DPRINTF(TRE, "Initiating read at addr 0x%x (blk 0x%x)\n", req->getPaddr(),
                blockAlign(req->getPaddr()));

        pkt = new Packet(req, MemCmd::ReadReq);
        pkt->dataDynamic(pkt_data);
    } else if (cmd == TRECmd::ST) {
        DPRINTF(TRE, "Initiating %s write at addr 0x%x (blk 0x%x)\n",
                uncacheable ? "uncacheable " : "", req->getPaddr(), blockAlign(req->getPaddr()));

        pkt = new Packet(req, MemCmd::WriteReq);
        pkt->dataDynamic(pkt_data);
        // pkt_data[0] = data;
    } else {
        assert(false);
    }

    DPRINTF(TRE, "Current number of outstanding addresses = %lu/%lu\n", outstandingAddrs.size(),
            maxNumOutstandingAddrs);

    // there is no point in ticking if we are waiting for a retry
    bool keep_ticking = sendPkt(pkt);

    if (keep_ticking) {
        // schedule the next tick
        DPRINTF(TRE, "Scheduling next tick() event @%lu\n", clockEdge(interval));
        schedule(tickEvent, clockEdge(interval));

        // finally shift the timeout for sending of requests forwards
        // as we have successfully sent a packet
        reschedule(noRequestEvent, clockEdge(progressCheck), true);
    } else {
        DPRINTF(TRE, "Waiting for retry\n");
    }

    // Schedule noResponseEvent now if we are expecting a response
    if (!noResponseEvent.scheduled() && (outstandingAddrs.size() != 0))
        schedule(noResponseEvent, clockEdge(progressCheck));
}

void
TRE::noRequest()
{
    panic("%s did not send a request for %d cycles", name(), progressCheck);
}

void
TRE::noResponse()
{
    panic("%s did not see a response for %d cycles", name(), progressCheck);
}

void
TRE::recvRetry()
{
    assert(retryPkt);
    if (port.sendTimingReq(retryPkt)) {
        DPRINTF(TRE, "Proceeding after successful retry\n");

        retryPkt = nullptr;
        // kick things into action again
        if (!blocked) {
            DPRINTF(TRE, "Scheduling post-retry tick() event @%lu\n", clockEdge(interval));
            schedule(tickEvent, clockEdge(interval));
        }
        reschedule(noRequestEvent, clockEdge(progressCheck), true);
    }
}

TRE *
TREParams::create()
{
    return new TRE(this);
}

bool 
TRE::addrInFlight(Addr paddr, bool erase) {
    bool found = false;
    for (auto it = outstandingAddrs.begin(); it != outstandingAddrs.end(); ++it) {
        if (it->first == paddr) {
            if (erase)
                outstandingAddrs.erase(it);
            assert(!found);
            found = true;
            break;
        }
    }
    // if this is called with erase == true, we must find a match of paddr in the outstandingAddrs
    // queue
    assert(!erase || found);
    return found;
}

Addr 
TRE::blkingAccAddrInFlight() {
    for (auto e : outstandingAddrs) {
        if (e.second) {     // "blocking" bit
            return e.first; // paddr
        }
    }
    return INVALID_PADDR;
}


std::string
TRE::readNextToken()
{
    std::string word;
    trace_fh >> word;
    DPRINTF (TRE, "Token read: %s\n", word);
    return word;
}

void
TRE::halt()
{
    numIdleTREs++;
    assert(numIdleTREs <= TESTER_ALLOCATOR);
    printf("TRE[%u]: halted @%lu after completing %lu trace entries\n", id, curTick(),
           (long unsigned int)(numTraceEntries.value()));
    printf("Number of TREs IDLE now: %u/%u\n", numIdleTREs, TESTER_ALLOCATOR);
    if (numIdleTREs == TESTER_ALLOCATOR) {
        trace_fh.close();
        exitSimLoop("all TREs are done");
    }
}

void
TRE::resetstats(Tick delay, Tick period)
{
    DPRINTF(TRE, "TRE::resetstats(%i, %i)\n", delay, period);
    Tick when = curTick() + delay * SimClock::Int::ns;
    Tick repeat = period * SimClock::Int::ns;

    Stats::schedStatEvent(false, true, when, repeat);
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
}

void
TRE::dumpstats(Tick delay, Tick period)
{
    DPRINTF(TRE, "TRE::dumpstats(%i, %i)\n", delay, period);
    Tick when = curTick() + delay * SimClock::Int::ns;
    Tick repeat = period * SimClock::Int::ns;

    Stats::schedStatEvent(true, false, when, repeat);
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
}

void
TRE::dumpresetstats(Tick delay, Tick period)
{
    DPRINTF(TRE, "TRE::dumpresetstats(%i, %i)\n", delay, period);
    Tick when = curTick() + delay * SimClock::Int::ns;
    Tick repeat = period * SimClock::Int::ns;

    Stats::schedStatEvent(true, true, when, repeat);
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
}

void
TRE::initMutex()
{
    assert(paddr);
    fatal_if(mutexMap.find(paddr) != mutexMap.end(),
              "Mutex cannot be re-initialized!");
    mutexMap.insert(std::make_pair(paddr, TREMutex()));
    DPRINTF(TRE, "Mutex @0x%x initialized\n", paddr);
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    paddr = 0;
}

void
TRE::lockMutex()
{
    assert(paddr);
    if (mutexMap.find(paddr) == mutexMap.end()) {
        DPRINTF(TRE, "Mutex hasn't yet been initialized\n");
        // rewind to last token entry
        trace_fh.seekg(lastTokEntryPos);
        // start "ticking" again to retry
        schedule(tickEvent, clockEdge(interval));
        return;
    }
    if (!mutexMap.at(paddr).owner) {
        DPRINTF(TRE, "Lock at 0x%lx is free, grabbing it\n", paddr);
        mutexMap.at(paddr).owner = this;
        // start "ticking" again since lock is granted to this TRE
        schedule(tickEvent, clockEdge(interval));
    } else {
        fatal_if(mutexMap.at(paddr).owner == this, "Double lock on mutex not allowed!");
        DPRINTF(TRE, "Lock at 0x%lx is not free, waiting\n", paddr);
        mutexMap.at(paddr).waiting.push_back(this);
    }
    // update stats
    numTraceEntries += 1; 
    // reset global variables
    paddr = 0;
}

void
TRE::unlockMutex()
{
    assert(paddr);
    fatal_if(mutexMap.at(paddr).owner != this, "Non-owner attempted to call unlock on mutex!");
    if (mutexMap.at(paddr).waiting.empty()) {
        mutexMap.at(paddr).owner = nullptr;
        mutexMap.at(paddr).waiting.clear();
    } else {
        // Pick one waiting tester to wake up at random.
        int idx = rand() % mutexMap.at(paddr).waiting.size();
        TRE *targetTester = mutexMap.at(paddr).waiting[idx];
        DPRINTF(TRE, "Waking a tester up waiting on mutex 0x%lx!\n", paddr);
        targetTester->schedule(targetTester->tickEvent, clockEdge(interval));
        // Hand mutex ownership to target.
        mutexMap.at(paddr).owner = targetTester;
        mutexMap.at(paddr).waiting.erase(mutexMap.at(paddr).waiting.begin() + idx);
    }
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    paddr = 0;
}

void
TRE::initBarrier()
{
    assert(paddr);
    assert(data);
    fatal_if(barrierMap.find(paddr) != barrierMap.end(),
              "Barrier cannot be re-initialized!");
    barrierMap.insert(std::make_pair(paddr, TREBarrier(data)));
    DPRINTF(TRE, "Barrier @0x%x initialized to count=%u\n", paddr, data);
    // update stats
    numTraceEntries += 1; 
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    paddr = 0; data = 0;
}

void
TRE::waitAtBarrier()
{
    assert(paddr);
    if(barrierMap.find(paddr) == barrierMap.end()) {
        DPRINTF(TRE, "Barrier hasn't yet been initialized\n");
        // rewind to last token entry
        trace_fh.seekg(lastTokEntryPos);
        // start "ticking" again to retry
        schedule(tickEvent, clockEdge(interval));
        return;
    }
    barrierMap.at(paddr).arrived.push_back(this);
    DPRINTF(TRE, "Going to sleep at barrier 0x%lx (%lu asleep now)!\n", paddr,
            barrierMap.at(paddr).arrived.size());
    // check if all participants have arrived
    if (barrierMap.at(paddr).arrived.size() == barrierMap.at(paddr).count) {
        // release all participants including self
        for (auto t : barrierMap.at(paddr).arrived) {
            t->schedule(t->tickEvent, clockEdge(interval));
        }
        DPRINTF(TRE, "Barrier reached at tick %lu\n", curTick());
        // clear for next usage
        barrierMap.at(paddr).arrived.clear();
    }
    // update stats
    numTraceEntries += 1; 
    // reset global variables
    paddr = 0;
}

void
TRE::wake()
{
    assert(data);
    unsigned int targetTRE = data;
    DPRINTF(TRE, "Waking up TRE %d\n", targetTRE);
    fatal_if(allTREs[targetTRE]->tickEvent.scheduled(), "Target TRE %u already awake!");
    allTREs[targetTRE]->schedule(allTREs[targetTRE]->tickEvent, clockEdge(interval));
    // update stats
    numTraceEntries += 1;
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    data = 0;
}

void
TRE::sleep()
{
    DPRINTF(TRE, "Going to sleep until woken up by another TRE\n");
}

void
TRE::push()
{
    unsigned source = id, sink = data;
    TREQueue *queue;
    try {
        queue = queues.at(std::make_pair(source, sink));
    } catch (const std::out_of_range &e) {
        printf("%s(): exception occured \"%s\" for source=%u, sink=%u, aborting. Perhaps you "
               "didn't run scripts/populate_init_queues.py?\n",
               __FUNCTION__, e.what(), source, sink);
        exit(1);
    }
    if (queue->q.size() == queue->size) {
        DPRINTF(TRE, "Queue[%u, %u] is full, stalling for now\n", source, sink);
        queue->waitingToPush = this;
        return;
    }
    queue->q.push(0);
    DPRINTF(TRE, "Queue[%u, %u] size is now %lu/%lu\n", source, sink, queue->q.size(), queue->size);

    // if there is someone waiting to pop, release them, and clear waiting
    TRE *&t = queue->waitingToPop;
    if (t) {
        DPRINTF(TRE, "Scheduling popEvent for TRE[%u]\n", sink);
        t->schedule(t->popEvent, clockEdge(interval));
        t = nullptr;
    }

    // update stats
    numTraceEntries += 1;
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    data = 0;
}

void
TRE::pop()
{
    unsigned source = data, sink = id;
    TREQueue *queue;
    try {
        queue = queues.at(std::make_pair(source, sink));
    } catch (const std::out_of_range &e) {
        printf("%s(): exception occured \"%s\" for source=%u, sink=%u, aborting. Perhaps you "
               "didn't run scripts/populate_init_queues.py?\n",
               __FUNCTION__, e.what(), source, sink);
        exit(1);
    }
    if (queue->q.size() == 0) {
        DPRINTF(TRE, "Queue[%u, %u] is empty, stalling for now\n", source, sink);
        queue->waitingToPop = this;
        return;
    }
    queue->q.pop();
    DPRINTF(TRE, "Queue[%u, %u] size is now %lu/%lu\n", source, sink, queue->q.size(), queue->size);

    // if there is someone waiting to push, release them, and clear waiting
    TRE *&t = queue->waitingToPush;
    if (t) {
        DPRINTF(TRE, "Scheduling pushEvent for TRE[%u]\n", source);
        t->schedule(t->pushEvent, clockEdge(interval));
        t = nullptr;
    }

    // update stats
    numTraceEntries += 1;
    // start "ticking" again
    schedule(tickEvent, clockEdge(interval));
    // reset global variables
    data = 0;
}
