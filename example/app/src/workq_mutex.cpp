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

#include "workq_mutex.hpp"
#include "params.h"
#include "util.h"

#if defined(MANUAL_TRACING) || defined(AUTO_TRACING)
#include "hetsim_default_rt.h"
#ifdef AUTO_TRACING
#pragma message("Tracing enabled for this run")
#else
#pragma message("Tracing enabled for this run")
#endif // AUTO_TRACING
#endif // MANUAL_TRACING || AUTO_TRACING

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <pthread.h>
#include <sys/mman.h>

#define ITER 2

// these are defined in CMakeLists.txt based on env variable settings
// #define EMULATION
// #define MANUAL_TRACING

//------------------------------------------------------------------------
// Create NUM_WORKER threads, run them in parallel and wait for them in the master thread. Each wrkr
// thread increments exclusive elements in a shared array n_steps times. The total number of
// iterations is set to be ITER. There is also a shared variable that each wrkr increments by 1.
//------------------------------------------------------------------------

inline void inc_wrkr_count(pthread_mutex_t *lock, unsigned *wrkr_count) {
#ifdef MANUAL_TRACING
    _C_wrkr__mutex_lock(lock);
#endif // MANUAL_TRACING
    pthread_mutex_lock(lock);
    // printf("wrkr_count (old) = %u\n", *wrkr_count);
    (*wrkr_count)++;
    // printf("wrkr_count (new) = %u\n", *wrkr_count);
#ifdef MANUAL_TRACING
    emit_load(wrkr_count, 2, 1, (void *)lock);
    emit_stall_with_deps(1, 1, (void *)lock);
    emit_store(wrkr_count, (void *)*wrkr_count, 3, 1, (void *)lock);
    _C_wrkr__mutex_unlock(lock);
#endif // MANUAL_TRACING
    pthread_mutex_unlock(lock);
}

void *func(void *args) {
    // printf("New thread started.\n");
    ThreadArg *my_args = (ThreadArg *)args;
    unsigned tid = my_args->tid;
    __register_core_id(tid);
#ifdef MANUAL_TRACING
    __open_trace_log(tid);
#endif // MANUAL_TRACING

#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __open_trace_log(tid);
#endif // AUTO_TRACING || MANUAL_TRACING


    // in EMU mode: use pthread barrier located in DSPM
    // in TRE-SIM mode: use SLEEP-WAKE: wake up wrkr 2, that wakes up wrkr 3, and so on
    pthread_barrier_t *gbar = my_args->gbar;
#ifdef MANUAL_TRACING
    _C_wrkr__sleep();
#endif // MANUAL_TRACING
    __barrier_wait(gbar);
#ifdef MANUAL_TRACING
    if (tid != NUM_WORKER) _C_wrkr__wake(tid + 1);
#endif // MANUAL_TRACING

    // retrieve variables from work queue
#ifdef MANUAL_TRACING
    _C_wrkr__pop(0);
    _C_wrkr__pop(0);
    _C_wrkr__pop(0);
    _C_wrkr__pop(0);
#endif // MANUAL_TRACING
    int nsteps = (int)__pop(0);
    volatile int *shared_var = (int *)__pop(0);
    pthread_mutex_t *lock = (pthread_mutex_t *)__pop(0);
    unsigned *wrkr_count = (unsigned *)__pop(0);

    // get a pointer to private variable accessed by each wrkr
    // volatile is needed here because otherwise the compiler register-allocates it
    volatile int *my_shared_var = (shared_var + tid - 1);

    // printf("nsteps = %d, shared_var = %p, lock = %p.\n", nsteps, shared_var, lock);
    for (int i = 0; i < ITER; ++i) {
        // receive ack from mgr to go
#ifdef MANUAL_TRACING
        _C_wrkr__pop(0);
#endif // MANUAL_TRACING
        __pop(0);

        // loop over nsteps that is set by the mgr core
        for (int i = 0; i < nsteps; ++i) {
            // increment my_shared_var by thread ID
            *my_shared_var += tid;
#ifdef MANUAL_TRACING
            emit_load((void *)my_shared_var, 0, 0);
            emit_stall_with_deps(1, 1, (void *)my_shared_var);
            emit_store((void *)my_shared_var, (void *)*my_shared_var, 1, 0);
            emit_stall_with_deps(3, 1, (void *)my_shared_var);
#endif // MANUAL_TRACING
        }
        // printf("After %u steps, shared_var[%u] = %d\n", nsteps, tid-1, *my_shared_var);
        inc_wrkr_count(lock, wrkr_count);

#ifdef MANUAL_TRACING
        _C_wrkr__barrier_wait(gbar);
#endif // MANUAL_TRACING
        __barrier_wait(gbar);
    }

#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __close_trace_log(tid);
#endif // AUTO_TRACING || MANUAL_TRACING

    return NULL;
}

int main(int argc, const char *argv[]) {
    printf("== Running Test with %u Workers ==\n", NUM_WORKER);
    __init_queues(WQ_DEPTH);
    __register_core_id(0);
#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __open_trace_log(0);
#endif // AUTO_TRACING || MANUAL_TRACING

    // allocate all threads
    pthread_t *threads = new pthread_t[NUM_WORKER];
    ThreadArg *t_args = new ThreadArg[NUM_WORKER];

#ifdef EMULATION
    // printf("Mmap %d bytes beginning %p\n", SPM_SIZE_BYTES, (void *)(SPM_BASE_ADDR));
    char *dspm = (char *)mmap((void *)(SPM_BASE_ADDR), SPM_SIZE_BYTES,
                              PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
#else  // !EMULATION
    char *dspm = (char *)SPM_BASE_ADDR;
#endif // EMULATION

    // variable shared among all threads
    int *shared_var = new (dspm) int[NUM_WORKER];
    // create a shared lock next to the shared variable in DSPM
    pthread_mutex_t *lock = new (shared_var + NUM_WORKER) pthread_mutex_t;
    // Initialize global barrier.
    pthread_barrier_t *global_bar = new (lock + 1) pthread_barrier_t;
    // wrkr count
    unsigned *wrkr_count = new (global_bar + 1) unsigned;

    // printf("Shared var is at SPM addr: %p.\n", shared_var);
    // printf("Initializing global barrier at SPM addr: %p.\n", global_bar);
    // printf("Initializing mutex at SPM addr: %p.\n", lock);
#ifdef MANUAL_TRACING
    _C_mgr__barrier_init(global_bar, NUM_WORKER + 1);
    _C_mgr__mutex_init(lock);
#endif // MANUAL_TRACING
    __barrier_init(global_bar, NUM_WORKER + 1);
    __mutex_init(lock);

#ifdef MANUAL_TRACING
    emit_stall();
    emit_store(wrkr_count, (void *)wrkr_count, 0, 0);
#endif // MANUAL_TRACING
    *wrkr_count = 0;

    // number of steps each thread increments the shared_var
    int nsteps = 10000;

    // try to spawn as many worker threads as possible
    for (int tid = 0; tid < NUM_WORKER; ++tid) {
        shared_var[tid] = tid + 1;

        // thread ID and pointer to barrier object are passed using traditional pthreads interface
        t_args[tid].tid = tid + 1;
        t_args[tid].gbar = global_bar;

        // spawn thread
        if (pthread_create(threads + tid, NULL, func, &t_args[tid]) != 0) {
            break;
        }
        // printf("Spawned %u threads.\n", n_worker_threads);
    }

    // in EMU mode: synchronize using barrier in DSPM
    // in TRE-SIM mode: synchronize using SLEEP-WAKE
    //     after synchronization, push through the work queues
#ifdef MANUAL_TRACING
    _C_mgr__wake(1); // wake up wrkr 1, that wakes up wrkr 2, and so on
#endif // MANUAL_TRACING
    __barrier_wait(global_bar);

    for (int tid = 0; tid < NUM_WORKER; ++tid) {
        // communicate nsteps, pointer to the shared variable, and pointer to the mutex object
        // through the faster work queue interface
        // printf("Pushing data to thread %d\n", tid);
#ifdef MANUAL_TRACING
        _C_mgr__push(tid + 1);
        _C_mgr__push(tid + 1);
        _C_mgr__push(tid + 1);
        _C_mgr__push(tid + 1);
#endif // MANUAL_TRACING
        __push(tid + 1, (uint64_t)nsteps);
        __push(tid + 1, (uint64_t)shared_var);
        __push(tid + 1, (uint64_t)lock);
        __push(tid + 1, (uint64_t)wrkr_count);
    }

    for (int i = 0; i < ITER; ++i) {
        // capture runtime between go-signal and a barrier_wait call in the worker threads' code
        // ================ Start Timing ================
#ifdef MANUAL_TRACING
        _C_mgr__reset_stats();
#endif // MANUAL_TRACING
        __reset_stats();
        // ================ Start Timing ================

        for (int tid = 0; tid < NUM_WORKER; ++tid) {
#ifdef MANUAL_TRACING
            _C_mgr__push(tid + 1);
#endif // MANUAL_TRACING
            __push(tid + 1, (uint64_t)1);
        }

        // ================ Stop Timing ================
#ifdef MANUAL_TRACING
        _C_mgr__barrier_wait(global_bar);
#endif // MANUAL_TRACING
        __barrier_wait(global_bar);
    }
    // ================ Stop Timing ================
#ifdef MANUAL_TRACING
    _C_mgr__dump_reset_stats();
#endif // MANUAL_TRACING
    __dump_reset_stats();

    // join with all threads
    for (int tid = 0; tid < NUM_WORKER; ++tid) {
        pthread_join(threads[tid], NULL);
    }
    // printf("Threads finished.\n");

    // verify
    bool passed = true;
    if (*wrkr_count != NUM_WORKER * ITER) {
        printf("[FAIL] wrkr_count (%u) != exp (%u)\n", *wrkr_count, NUM_WORKER * ITER);
        passed = false;
    } else {
        for (int tid = 0; tid < NUM_WORKER; ++tid) {
            int exp = (tid + 1) + (tid + 1) * nsteps * ITER;
            if (shared_var[tid] != exp) {
                passed = false;
                printf("[FAIL] shared_var[%u] (%d) != exp[%u] (%d)\n", tid, shared_var[tid], tid,
                       exp);
                break;
            }
        }
    }
    // clean up
#ifdef EMULATION
    munmap(dspm, SPM_SIZE_BYTES);
#endif // EMULATION
    delete[] threads;
    delete[] t_args;
    __teardown_queues();

#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __close_trace_log(0);
#endif // AUTO_TRACING || MANUAL_TRACING

    if (!passed) {
        printf("== Test failed! ==\n");
        return EXIT_FAILURE;
    }

    printf("== Test passed! ==\n");
    return EXIT_SUCCESS;
}
