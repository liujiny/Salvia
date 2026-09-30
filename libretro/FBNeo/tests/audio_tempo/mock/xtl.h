#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdint.h>
typedef long LONG;
typedef unsigned long DWORD;
struct TestEvent { std::mutex mutex; std::condition_variable changed; bool ready; TestEvent():ready(false){} };
typedef TestEvent* HANDLE;
#ifndef FALSE
#define FALSE 0
#endif
inline HANDLE CreateEvent(void*,int,int,void*) { return new TestEvent; }
inline int CloseHandle(HANDLE e) { delete e;return 1; }
inline int SetEvent(HANDLE e) { std::lock_guard<std::mutex> lock(e->mutex);e->ready=true;e->changed.notify_one();return 1; }
inline DWORD WaitForSingleObject(HANDLE e,DWORD ms) {
    std::unique_lock<std::mutex> lock(e->mutex);
    e->changed.wait_for(lock,std::chrono::milliseconds(ms),[&](){return e->ready;});
    e->ready=false;return 0;
}
inline LONG InterlockedExchange(volatile LONG* p,LONG n) { return __atomic_exchange_n(p,n,__ATOMIC_SEQ_CST); }
inline LONG InterlockedExchangeAdd(volatile LONG* p,LONG n) { return __atomic_fetch_add(p,n,__ATOMIC_SEQ_CST); }
inline LONG InterlockedIncrement(volatile LONG* p) { return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST); }
inline LONG InterlockedCompareExchange(volatile LONG* p,LONG value,LONG compare) {
    __atomic_compare_exchange_n(p,&compare,value,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return compare;
}
