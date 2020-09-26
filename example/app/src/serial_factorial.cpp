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

int main() {
    printf("Mgr starting up.\n");
    __init_queues(WQ_DEPTH);
    __register_core_id(0);
#if defined(MANUAL_TRACING) || defined(AUTO_TRACING)
    __open_trace_log(0);
#endif // MANUAL_TRACING || AUTO_TRACING
#ifdef EMULATION
    char *dspm = (char *)mmap((void *)(SPM_BASE_ADDR), SPM_SIZE_BYTES, PROT_READ | PROT_WRITE,
                              MAP_ANON | MAP_PRIVATE, 0, 0);
#else  // !EMULATION
    char *dspm = (char *)SPM_BASE_ADDR;
#endif // EMULATION

	int N = 10;
	volatile unsigned *fact = (volatile unsigned *) dspm;
	*fact = 1;

	// ==========================================================
#ifdef MANUAL_TRACING
	_C_mgr__reset_stats();
#endif // MANUAL_TRACING
	__reset_stats();

	for (int i = 1; i <= N; ++i) {
		*fact *= i;
#ifdef MANUAL_TRACING
        emit_stall_with_deps(2, 1, (void *)fact);
        emit_load((void *)fact, 0, 0);
        emit_stall_with_deps(2, 1, (void *)fact);
        emit_store((void *)fact, (void *)*fact, 1, 0);
#endif // MANUAL_TRACING
	}

#ifdef MANUAL_TRACING
	_C_mgr__dump_reset_stats();
#endif // MANUAL_TRACING
	__dump_reset_stats();
	// ==========================================================

	printf("fact(%u) = %u\n", N, *fact);

    // clean up
#ifdef EMULATION
    munmap(dspm, SPM_SIZE_BYTES);
#endif // EMULATION
    __teardown_queues();
#if defined(MANUAL_TRACING) || defined(AUTO_TRACING)
    __close_trace_log(0);
#endif // MANUAL_TRACING || AUTO_TRACING

	return 0;
}
