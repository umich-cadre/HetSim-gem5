/*
 * Copyright (c) 2020 The Regents of The University of Michigan
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

#ifndef EMU_INC_UTIL_H
#define EMU_INC_UTIL_H
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

class q_intfc_t;

extern std::map<std::thread::id, unsigned> tid_to_core_id_map;
extern std::map<std::pair<unsigned, unsigned>, q_intfc_t *> queues;

class q_intfc_t {
  public:
    std::queue<uint64_t> q;
    std::mutex *m;
    std::condition_variable *fullCv, *emptyCv;
    unsigned size;

    q_intfc_t(unsigned _size) : size(_size) {
        m = new std::mutex;
        fullCv = new std::condition_variable;
        emptyCv = new std::condition_variable;
    }
    ~q_intfc_t() {
        delete m;
        delete fullCv;
        delete emptyCv;
    }
};
void __register_core_id(unsigned);

// work queue interface
void __init_queues(long unsigned int depth);
void __teardown_queues(void);
void __push(unsigned int, uint64_t data);
void __push_mmap(unsigned int, uint64_t data);
uint64_t __pop(unsigned int);
uint64_t __pop_mmap(unsigned int);

// barrier synchronization interface
void __barrier_init(pthread_barrier_t *bar, unsigned int count);
void __barrier_wait(pthread_barrier_t *bar);

// mutex interface
void __mutex_init(pthread_mutex_t *lock);
void __mutex_lock(pthread_mutex_t *lock);
void __mutex_unlock(pthread_mutex_t *lock);

// emulation lib treats these as no-ops
void __reset_stats();
void __dump_stats();
void __dump_reset_stats();
void __print_debug(unsigned int code);

int get_id();

#endif // EMU_INC_UTIL_H
