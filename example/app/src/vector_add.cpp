#include "params.h"  // import parameters of target hardware as macros
#include "util.h"    // import primitive definitions
#include <pthread.h>
#include <sys/mman.h>
#include <cstdio>

#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
#include "hetsim_default_rt.h"
#endif

#define N 100000

void *work(void *arg) { // manager "spawns" worker threads with tid=1,2,3...
    unsigned tid = *(unsigned *)(arg);
    __register_core_id(tid);
#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __open_trace_log(tid);
#endif // AUTO_TRACING || MANUAL_TRACING

    // retrieve variables from work queue
    volatile float *a = (volatile float *)__pop(0);
    volatile float *b = (volatile float *)__pop(0);
    volatile float *c = (volatile float *)__pop(0);
    int start_idx = (int)__pop(0);
    int end_idx = (int)__pop(0);
    pthread_barrier_t *bar = (pthread_barrier_t *)__pop(0);

    // receive start signal
    __pop(0);

    // perform actual computation
    for (int i = start_idx; i <= end_idx; ++i) {
        c[i] += a[i] + b[i];
    }

    // synchronize with manager
    __barrier_wait(bar);

#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __close_trace_log(tid);
#endif // AUTO_TRACING || MANUAL_TRACING
    return NULL;
} // end of work()

int main() {
    printf("== Vector Add Test with N = %u, NUM_WORKER = %u\n", N, NUM_WORKER);
    __init_queues(WQ_DEPTH);
    __register_core_id(0); // manager is assigned core-id 0

    // main memory allocation
    // in this example, we are working with 3 float arrays each of size N
    size_t RAM_SIZE_BYTES = 3 * N * sizeof(float);
    char *ram = (char *)mmap((void *)(RAM_BASE_ADDR), RAM_SIZE_BYTES,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_ANON | MAP_PRIVATE, 0, 0);

    // scratchpad memory allocation
#ifdef EMULATION
    // for emulation
    char *dspm = (char *)mmap((void *)(SPM_BASE_ADDR), SPM_SIZE_BYTES,
                              PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_ANON | MAP_PRIVATE, 0, 0);
#else  // !EMULATION
    // the model uses physically-addressed scratchpad that does not need explicit allocation
    char *dspm = (char *)SPM_BASE_ADDR;
#endif // EMULATION

    // allocate the vectors and populate them
    float *a = (float *)(ram);
    float *b = (float *)(ram + N * sizeof(float));
    float *c = (float *)(ram + 2 * N * sizeof(float));
    for (int i = 0; i < N; ++i) {
        a[i] = float(i + 1);
        b[i] = float(i + 1);
        c[i] = 0.0;
    }
#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __open_trace_log(0); // use core-id as argument
#endif


    // allocate barrier object for synchronization
    pthread_barrier_t *bar = (pthread_barrier_t *)(dspm);
    // initialize barrier with participants = 1 manager + NUM_WORKER workers
    __barrier_init(bar, NUM_WORKER + 1);

    // allocate thread objects for each "worker" PE
    pthread_t *workers = new pthread_t[NUM_WORKER];

    // create vector of core IDs to send to each thread
    unsigned *tids = new unsigned[NUM_WORKER];
    for (int i = 0; i < NUM_WORKER; ++i) {
        tids[i] = i + 1;
        // spawn worker thread
        pthread_create(workers + i, NULL, work, &tids[i]);
    }

    // partition the work and push work "packets"
    for (int i = 0; i < NUM_WORKER; ++i) {
    // each worker is assigned floor(N / NUM_WORKER) elements
        int n = N / NUM_WORKER;
        int start_idx = i * n;
        int end_idx = (i + 1) * n - 1;

        // handle trailing elements by assigning to final worker
        if (i == NUM_WORKER - 1) {
            end_idx = N - 1;
        }
        // push through work queues
        __push(i + 1, (uintptr_t)(a));
        __push(i + 1, (uintptr_t)(b));
        __push(i + 1, (uintptr_t)(c));
        __push(i + 1, (unsigned)(start_idx));
        __push(i + 1, (unsigned)(end_idx));
        __push(i + 1, (uintptr_t)(bar));
    }
// ----- ROI begin -----
    __reset_stats(); // begin recording time here
    for (int i = 0; i < NUM_WORKER; ++i) {
        __push(i + 1, 0); // start signal, value is ignored
    }
    __barrier_wait(bar); // synchronize with worker threads

    __dump_reset_stats(); // end recording time here
// ----- ROI end -----
#if defined(AUTO_TRACING) || defined(MANUAL_TRACING)
    __close_trace_log(0);
#endif // AUTO_TRACING || MANUAL_TRACING

    // join with all threads
    for (int tid = 0; tid < NUM_WORKER; ++tid) {
        pthread_join(workers[tid], NULL);
    }

    // result checking
    bool pass = true;
    for (int i = 0; i < N; ++i) {
        if(c[i] != a[i] + b[i]) {
            pass = false;
            printf("[FAILED] C[%u] = %f (exp = %f)\n", i, c[i], a[i] + b[i]);
            break;
        }
    }

    // clean up
#ifdef EMULATION
    munmap(dspm, SPM_SIZE_BYTES);
#endif // EMULATION
    munmap(ram, RAM_SIZE_BYTES);
    delete[] workers;
    delete[] tids;
    __teardown_queues();

    if (pass)
        printf("== Test Passed ==\n");
    else
        printf("== Test Failed ==\n");
    return 0;
} // end of main()
