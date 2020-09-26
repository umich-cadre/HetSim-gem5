/*
 * Copyright (c) 2015 ARM Limited
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

#ifndef __CPU_TRE_TRE_HH__
#define __CPU_TRE_TRE_HH__

#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <vector>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/TRE.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "sim/stats.hh"

// TRE mutex, barrier, and queue implementations
struct TREMutex {
    TRE *owner; // Pointer to TRE object has grabbed the lock to this mutex (nullptr => unlocked)
    std::vector<TRE *> waiting; // vector of all TREs waiting to grab this lock
    TREMutex() : owner(nullptr) { waiting.clear(); }
};
struct TREBarrier {
    unsigned count;             // number of participants in this barrier
    std::vector<TRE *> arrived; // vector of all TREs waiting at this barrier
    TREBarrier(unsigned _count) : count(_count) { arrived.clear(); }
};
struct TREQueue {
    std::queue<int> q;
    long unsigned int size;
    TRE *waitingToPush, *waitingToPop;
    TREQueue(unsigned _size) : size(_size), waitingToPush(nullptr), waitingToPop(nullptr) {}
};
/**
 * The TRE class tests a cache coherent memory system by
 * generating false sharing and verifying the read data against a
 * reference updated on the completion of writes. Each tester reads
 * and writes a specific byte in a cache line, as determined by its
 * unique id. Thus, all requests issued by the TRE instance are a
 * single byte and a specific address is only ever touched by a single
 * tester.
 *
 * In addition to verifying the data, the tester also has timeouts for
 * both requests and responses, thus checking that the memory-system
 * is making progress.
 */
class TRE : public ClockedObject
{
    // map of mutex/barrier address to the TRE mutex/barrier class object
    static std::map<Addr, TREMutex> mutexMap;
    static std::map<Addr, TREBarrier> barrierMap;

    static std::vector<TRE *> allTREs;

    static std::map<std::pair<unsigned, unsigned>, TREQueue *> queues;

    void initQueues(unsigned long depth);

    void teardownQueues(void);

  public:

    typedef TREParams Params;
    TRE(const Params *p);
    ~TRE();

    void regStats() override;

    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

  protected:

    void tick();

    EventFunctionWrapper tickEvent;

    void noRequest();

    EventFunctionWrapper noRequestEvent;

    void noResponse();

    EventFunctionWrapper noResponseEvent;

    class CpuPort : public MasterPort
    {
        TRE &tre;

      public:

        CpuPort(const std::string &_name, TRE &_tre)
            : MasterPort(_name, &_tre), tre(_tre)
        { }

      protected:

        bool recvTimingResp(PacketPtr pkt);

        void recvTimingSnoopReq(PacketPtr pkt) { }

        void recvFunctionalSnoop(PacketPtr pkt) { }

        Tick recvAtomicSnoop(PacketPtr pkt) { return 0; }

        void recvReqRetry();
    };

    CpuPort port;

    PacketPtr retryPkt;

    const unsigned size;

    const Cycles interval;

    // input file handler for HetSim trace file
    std::ifstream trace_fh;

    /** Request id for all generated traffic */
    MasterID masterId;

    const unsigned int id;

    // -1 = no queues in the system
    const long unsigned int queueDepth;

    // pair of address and a flag indicating whether an access to it blocks others from being sent
    // out of the TRE; std::vector is fastest to search for small values of maxNumOutstandingAddrs
    // despite O(N) complexity
    std::vector<std::pair<Addr, bool>> outstandingAddrs; 

    const unsigned blockSize;

    const Addr blockAddrMask;

    const Addr INVALID_PADDR = -1;

    typedef enum TRECmd { LD, ST, STALL } TRECmd;

    std::string
    cmd2str(TRECmd cmd)
    {
        if (cmd == TRECmd::LD)
            return "LD";
        else if (cmd == TRECmd::ST)
            return "ST";
        else if (cmd == TRECmd::STALL)
            return "STALL";
        else
            fatal("unknown command encountered in cmd2str()\n");
    }

    struct blockedPkt {
        Addr addr;
        TRECmd cmd;
        uint32_t data;
        bool uncacheable;
        bool blking;
        std::set<Addr> deps;
    } blockedPkt;

    bool blocked;

    std::string traceRoot;

    /**
     * Get the block aligned address.
     *
     * @param addr Address to align
     * @return The block aligned address
     */
    Addr blockAlign(Addr addr) const
    {
        return (addr & ~blockAddrMask);
    }

    Addr paddr; unsigned int data;

    const unsigned progressInterval;  // frequency of progress reports
    const Cycles progressCheck;
    Tick nextProgressMessage;   // access # for next progress report

    const bool atomic;

    const bool suppressFuncErrors;

    const uint64_t maxNumOutstandingAddrs;

    std::istream::streampos lastTokEntryPos;

    // statistics
    Stats::Scalar numLoads;
    Stats::Scalar numStores;
    Stats::Scalar numStalls;
    Stats::Scalar numBlkingAccesses;
    Stats::Scalar numTraceEntries;

    /**
     * Complete a request by checking the response.
     *
     * @param pkt Response packet
     * @param functional Whether the access was functional or not
     */
    void completeRequest(PacketPtr pkt, bool functional = false);

    bool sendPkt(PacketPtr pkt);

    void recvRetry();

    bool addrInFlight(Addr paddr, bool erase=false);

    Addr blkingAccAddrInFlight();

    std::string readNextToken();

    // halt TRE after finishing all trace entries
    void halt();

    // *stats(...) definitions copied over from src/sim/PseudoInst.cpp
    void resetstats(Tick delay, Tick period);
    void dumpstats(Tick delay, Tick period);
    void dumpresetstats(Tick delay, Tick period);

    // mutex implementation
    void initMutex();
    void lockMutex();
    void unlockMutex();

    // barrier implementation
    void initBarrier();
    void waitAtBarrier();

    // sleep-wake implementation
    void sleep();
    void wake();

    // push-pop implementation
    void push();
    void pop();

    // event function wrappers corresponding to explicit primitives
    EventFunctionWrapper *initMutexEvent;
    EventFunctionWrapper *lockMutexEvent;
    EventFunctionWrapper *unlockMutexEvent;

    EventFunctionWrapper *initBarrierEvent;
    EventFunctionWrapper *waitAtBarrierEvent;

    EventFunctionWrapper *sleepEvent;
    EventFunctionWrapper *wakeEvent;

    EventFunctionWrapper *pushEvent;
    EventFunctionWrapper *popEvent;

    EventFunctionWrapper *dumpStatsEvent;
    EventFunctionWrapper *resetStatsEvent;
    EventFunctionWrapper *dumpResetStatsEvent;
};

#endif // __CPU_TRE_TRE_HH__
