/*
 * Copyright (c) 2010-2013, 2015 ARM Limited
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
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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
 *
 * Authors: Subhankar Pal
 */

#include <string>
#include <bitset>

#include "mem/work_queue.hh"

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "debug/WorkQueue.hh"

WorkQueue::WorkQueue(const WorkQueueParams* p) :
    AbstractMemory(p),
    popPort(name() + ".pop_port", *this, PopPortType),
    pushPort(name() + ".push_port", *this, PushPortType),
    size(p->size),
    latency(p->latency),
    // latency_var(p->latency_var), bandwidth(p->bandwidth), isBusy(false),
    isBusy(false), isEmpty(true), isFull(false),
    retryPop(false), retryPush(false), retryResp(false),
    popPortAddr(p->pop_addr),
    pushPortAddr(p->push_addr),
    // releasePopPortEvent([this]{ release(PopPortType); }, name()),
    releaseEvent([this]{ release(); }, name()),
    dequeueEvent([this]{ dequeue(); }, name())
{
}

void
WorkQueue::init()
{
    AbstractMemory::init();

    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    // if (port.isConnected()) {
    //     port.sendRangeChange();
    // }
    panic_if(!popPort.isConnected(), "pop_port unconnected");
    panic_if(!pushPort.isConnected(), "push_port unconnected");
    popPort.sendRangeChange();
    pushPort.sendRangeChange();
}

void
WorkQueue::access(PacketPtr pkt)
{
    // Taken from AbstractMemory::access() in abstract_mem.cc.
    // assert(AddrRange(pkt->getAddr(),
    //                  pkt->getAddr() + (pkt->getSize() - 1)).isSubset(range));

    panic_if(!writeOK(pkt), "writeOK(pkt) not set");
    panic_if(pkt->getSize() != 4, "Work item (req payload) size must be 4B, but is: %luB", pkt->getSize());
    if (pkt->isRead()) {
        DPRINTF(WorkQueue, "Received pop request in access()\n");
        // Read and delete from the work queue since this is pop_port.
        assert(!pkt->isWrite());
        panic_if(pkt->isLLSC(), "Should only see load/store requests to work queue");

        panic_if(workQ.size() == 0, "Work popped when queue was empty");
        uint32_t data = workQ.front();
        workQ.pop();
        memcpy(pkt->getPtr<uint32_t>(), &data, pkt->getSize());
        if(DTRACE(WorkQueue)) {
            printf("Packet on pop_port now contains data: 0x%X\n", *(pkt->getPtr<uint32_t>()));
            printf("Entries: ");
            std::queue<uint32_t> copy = workQ;
            while(!copy.empty()) {
                printf("0x%x -> ", copy.front());
                copy.pop();
            }
            printf("\n");
        }

        // Update statistics.
        pops++;
    } else if (pkt->isWrite()) {
        DPRINTF(WorkQueue, "Received push request in access() with data: 0x%X\n", *(pkt->getConstPtr<uint32_t>()));
        // Write into the work queue since this is push_port.
        panic_if(workQ.size() == this->size, "Work pushed when queue was full");
        workQ.push(*(pkt->getConstPtr<uint32_t>()));
        if(DTRACE(WorkQueue)) {
            printf("Entries: ");
            std::queue<uint32_t> copy = workQ;
            while(!copy.empty()) {
                printf("0x%x -> ", copy.front());
                copy.pop();
            }
            printf("\n");
        }

        // Update statistics.
        pushes++;
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    if (pkt->needsResponse()) {
        pkt->makeResponse();
    }
    DPRINTF(WorkQueue, "WorkQueue size is now: %lu\n", workQ.size());
    isEmpty = workQ.empty();
    isFull = (workQ.size() == this->size);
}

Tick
WorkQueue::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return getLatency();
}

/*void
WorkQueue::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    bool done = false;
    auto p = packetQueue.begin();
    // potentially update the packets in our packet queue as well
    while (!done && p != packetQueue.end()) {
        done = pkt->checkFunctional(p->pkt);
        ++p;
    }

    pkt->popLabel();
}*/

bool
WorkQueue::recvTimingReq(PacketPtr pkt, WorkQueuePort_e portType)
{
    std::string portName = (portType == PopPortType) ? "PopPort" : "PushPort";

    DPRINTF(WorkQueue, "%s: Received %s to %#lx\n", portName,
            pkt->cmdString().c_str(), pkt->getAddr());
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "%s: Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", portName, pkt->cmdString(), pkt->getAddr());

    panic_if((portType == PopPortType) && !pkt->isRead(), "Should only "
             "see load requests to pop_port");
    // panic_if((portType == PushPortType) && !pkt->isWrite(), "Should only "
    //          "see store requests to push_port");

    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();

    DPRINTF(WorkQueue, "%s: status: isBusy:%d, isEmpty:%d, isFull:%d\n", portName,
            isBusy, isEmpty, isFull);

    std::bitset<8> workQStatus;
    if(isEmpty) workQStatus.set(0);
    if(isFull) workQStatus.set(1);

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if ((retryPop && portType == PopPortType) || (retryPush && portType == PushPortType)) {
        DPRINTF(WorkQueue, "%s: Returning because retry was set\n", portName);
        return false;
    }

    // if we are busy with a read or write, remember that we have to
    // retry
    /*if (isBusy) {
        retryReq = true;
        return false;
    }*/
    if (portType == PopPortType) {
        if(isBusy) {
            DPRINTF(WorkQueue, "%s: Work queue is busy; setting retryPop\n", portName);
            retryPop = true;
            return false;
        } else if(isEmpty) {
            DPRINTF(WorkQueue, "%s: Work queue is empty; setting retryPop"
                "\n", portName);
            retryPop = true;
            return false;
        }
    } else if (portType == PushPortType) {
        if(isBusy) {
            DPRINTF(WorkQueue, "%s: Work queue is busy; setting retryPush"
                "\n", portName);
            retryPush = true;
            return false;
        } else if(isFull) {
            DPRINTF(WorkQueue, "%s: Work queue is full; setting retryPush"
                "\n", portName);
            retryPush = true;
            return false;
        }
    }

    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    // Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    // pkt->headerDelay = pkt->payloadDelay = 0;

    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    // Tick duration = pkt->getSize() * bandwidth;

    // only consider ourselves busy if there is any need to wait
    // to avoid extra events being scheduled for (infinitely) fast
    // memories
    /*if (duration != 0) {
        schedule(releaseEvent, curTick() + duration);
        isBusy = true;
    }*/

    DPRINTF(WorkQueue, "Scheduling releaseEvent at Tick %lu and setting busy\n", curTick() + getLatency());
    schedule(releaseEvent, curTick() + getLatency());
    isBusy = true;

    recvAtomic(pkt);

    // turn packet around to go back to requester if response expected
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response
        assert(pkt->isResponse());

        Tick when_to_send = curTick() + getLatency();
        // Tick when_to_send = curTick() + receive_delay + getLatency();

        // typically this should be added at the end, so start the
        // insertion sort with the last element, also make sure not to
        // re-order in front of some existing packet with the same
        // address, the latter is important as this memory effectively
        // hands out exclusive copies (shared is not asserted)
        // auto i = packetQueue.end();
        // --i;
        // while (i != packetQueue.begin() && when_to_send < i->tick &&
        //        i->pkt->getAddr() != pkt->getAddr())
        //     --i;
        // emplace inserts the element before the position pointed to by
        // the iterator, so advance it one step
        // packetQueue.emplace(++i, pkt, when_to_send);
        packetQueue.emplace(packetQueue.end(), pkt, when_to_send, portType);

        DPRINTF(WorkQueue, "packetQueue size is now: %lu\n", packetQueue.size());

        DPRINTF(WorkQueue, "Scheduling dequeueEvent at tick %lu\n", packetQueue.back().tick);
        if (!retryResp && !dequeueEvent.scheduled())
            schedule(dequeueEvent, packetQueue.back().tick);
    } else {
        pendingDelete.reset(pkt);
    }

    return true;
}

void
WorkQueue::release()
{
    // assert(isBusy || (portType == PopPortType && isEmpty) || (portType == PushPortType && isFull));
    DPRINTF(WorkQueue, "%s: isBusy: %d, retryPop: %d, retryPush: %d\n", __func__, isBusy, retryPop, retryPush);
    if(isBusy) {
        isBusy = false;
    }
    if (retryPop) {
        retryPop = false;
        popPort.sendRetryReq();
    }
    if (retryPush) {
        retryPush = false;
        pushPort.sendRetryReq();
    }
}

void
WorkQueue::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    if(deferred_pkt.portType == PopPortType) {
        retryResp = !popPort.sendTimingResp(deferred_pkt.pkt);
        DPRINTF(WorkQueue, "Sending timing response to pop_port: %s\n", retryResp ? "Retry" : "Succeeded");
    } else if(deferred_pkt.portType == PushPortType) {
        retryResp = !pushPort.sendTimingResp(deferred_pkt.pkt);
        DPRINTF(WorkQueue, "Sending timing response to push_port: %s\n", retryResp ? "Retry" : "Succeeded");
    }

    if (!retryResp) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            DPRINTF(WorkQueue, "Rescheduling dequeue event at tick %lu\n", std::max(packetQueue.front().tick, curTick()));
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainState() == DrainState::Draining) {
            DPRINTF(WorkQueue, "Draining of WorkQueue complete\n");
            signalDrainDone();
        }
    }
}

Tick
WorkQueue::getLatency() const
{
    // return latency +
    //     (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
    return latency;
}

void
WorkQueue::recvRespRetry()
{
    assert(retryResp);

    dequeue();
}

Port &
WorkQueue::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pop_port") {
        return popPort;
    } else if (if_name == "push_port") {
        return pushPort;
    } else {
        return AbstractMemory::getPort(if_name, idx);
    }
}

Addr
WorkQueue::getPopPortAddr() const
{
    return popPortAddr;
}

Addr
WorkQueue::getPushPortAddr() const
{
    return pushPortAddr;
}

DrainState
WorkQueue::drain()
{
    if (!packetQueue.empty()) {
        DPRINTF(WorkQueue, "WorkQueue packetQueue has requests, waiting to drain\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

WorkQueue::MemoryPort::MemoryPort(const std::string& _name,
                                     WorkQueue& _workQueue, WorkQueuePort_e _type)
    : SlavePort(_name, &_workQueue), workQueue(_workQueue) {
    this->type = _type;
}

AddrRangeList
WorkQueue::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    // ranges.push_back(workQueue.getAddrRange());

    if(this->type == PopPortType) {
        ranges.push_back(AddrRange(workQueue.getPopPortAddr(), workQueue.getPopPortAddr()+4));
    } else if(this->type == PushPortType) {
        ranges.push_back(AddrRange(workQueue.getPushPortAddr(), workQueue.getPushPortAddr()+4));
    }
    return ranges;
}

Tick
WorkQueue::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return workQueue.recvAtomic(pkt);
}

void
WorkQueue::MemoryPort::recvFunctional(PacketPtr pkt)
{
    panic("Should not be receiving a functional packet in the work queue");
    // workQueue.recvFunctional(pkt);
}

bool
WorkQueue::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return workQueue.recvTimingReq(pkt, this->type);
}

void
WorkQueue::MemoryPort::recvRespRetry()
{
    workQueue.recvRespRetry();
}

WorkQueue*
WorkQueueParams::create()
{
    return new WorkQueue(this);
}

void
WorkQueue::regStats()
{
    AbstractMemory::regStats();

    using namespace Stats;

    pops
        .name(name() + ".pops")
        .desc("number of pops to this work queue")
        ;

    pushes
        .name(name() + ".pushes")
        .desc("number of pushes to this work queue")
        ;
}
