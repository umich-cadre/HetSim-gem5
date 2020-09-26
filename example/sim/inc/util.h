/** @file util.hpp
 *  @brief Primitive definitions to be used in user applications.
 *  @author Subhankar Pal
 */

#ifndef __UTIL_HPP__
#define __UTIL_HPP__

#include "defs.h"
#include "gem5/m5ops.h"
#include "params.h"
#include <pthread.h>
#include <stdint.h>

#define __INLINE inline __attribute__((always_inline))

/** Dummy primitives for compatibility with emulation library. **/
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__register_core_id(unsigned id) { }
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__init_queues(unsigned depth) { }
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__teardown_queues(void) { }
/**
 * @brief Pushes an item to the back of the work queue indexed by PE ID.
 * @param pe_id ID of the PE that this work queue is connected to.
 * @param data The item of size 4 bytes to be pushed.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__push_mmap(unsigned pe_id, uint32_t data) {
    void *addr = (void *)(WQ_PUSH_BASE_ADDR + 4 * pe_id);
    __asm__ __volatile__("str %[value], [%[address]]\n"
                         :
                         : [ address ] "l"(addr), [ value ] "l"(data)
                         : "memory");
}
/**
 * @brief Pushes an item to the back of the work queue indexed by PE ID.
 * @param pe_id ID of the PE that this work queue is connected to.
 * @param data The item of size 4 bytes to be pushed.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__push(unsigned pe_id, uint32_t data) {
    // TODO: model should implement as special instruction such as m5op
    return __push_mmap(pe_id, data);
}
/**
 * @brief Pops an item from the front of the PE work queue.
 * @return Popped item of size 4 bytes.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE uint32_t
__pop_mmap(unsigned pe_id) {
    void *addr = (void *)WQ_POP_ADDR;
    uint32_t data = 0x0;
    __asm__ __volatile__("ldr %[value], [%[address]]\n"
                         : [ value ] "=l"(data)
                         : [ address ] "l"(addr)
                         :);
    return data;
}
/**
 * @brief Pops an item from the front of the PE work queue.
 * @return Popped item of size 4 bytes.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE uint32_t
__pop(unsigned pe_id) {
    // TODO: model should implement as special instruction such as m5op
    return __pop_mmap(pe_id);
}
/**
 * @brief Initializes a mutex.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
    __mutex_init(pthread_mutex_t *lock) {
    pthread_mutex_init(lock, NULL);
}
/**
 * @brief Locks a mutex.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__mutex_lock(pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
}
/**
 * @brief Unlocks a mutex.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__mutex_unlock(pthread_mutex_t *lock) {
    pthread_mutex_unlock(lock);
}
/**
 * @brief Initializes a barrier.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__barrier_init(pthread_barrier_t *bar, unsigned count) {
    pthread_barrier_init(bar, NULL, count);
}
/**
 * @brief Waits at a barrier.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__barrier_wait(pthread_barrier_t *bar) {
    pthread_barrier_wait(bar);
}
/**
 * @brief Reset gem5 statistics.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__reset_stats() {
    m5_reset_stats(0 /*delay*/, 0 /*period*/);
}
/**
 * @brief Dump gem5 statistics.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__dump_stats() {
    m5_dump_stats(0 /*delay*/, 0 /*period*/);
}
/**
 * @brief Dump and then reset gem5 statistics.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__dump_reset_stats() {
    m5_dump_reset_stats(0 /*delay*/, 0 /*period*/);
}
/**
 * @brief Print low-overhead debug message with --debug-flags=PseudoInst.
 * @return Void.
 */
extern
#ifdef __cplusplus
"C"
#endif // __cplusplus
__INLINE void
__print_debug(unsigned code) {
    m5_fail(code /*delay*/, 0x80 /*code*/);
}

#endif // __UTIL_HPP__
