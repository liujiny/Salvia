/* Controlled notification API around the actual extracted adapter functions.
   Not an implementation of XAM or real system UI scheduling. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef void* HANDLE;
typedef uint32_t DWORD;
typedef uintptr_t ULONG_PTR;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define XNOTIFY_SYSTEM 1
#define XN_SYS_UI 9
static HANDLE g_coreUiListener;
static XBOX_UiRecovery g_coreUiRecovery;
static unsigned creates, closes, polls, head, tail;
static int failure;
static ULONG_PTR queue[256];
static HANDLE XNotifyCreateListener(unsigned areas) {
    assert(areas == XNOTIFY_SYSTEM); ++creates; head = tail = 0;
    return failure == 1 ? NULL : failure == 2 ? INVALID_HANDLE_VALUE : (HANDLE)(uintptr_t)1;
}
static void CloseHandle(HANDLE h) {
    assert(h == (HANDLE)(uintptr_t)1); ++closes;
}
static int XNotifyGetNext(HANDLE h, DWORD filter, DWORD* id, ULONG_PTR* value) {
    assert(h == (HANDLE)(uintptr_t)1 && filter == XN_SYS_UI); ++polls;
    if (head == tail) return 0;
    *id = XN_SYS_UI; *value = queue[head++]; return 1;
}
static void event(int visible) { assert(tail < 256); queue[tail++] = (ULONG_PTR)visible; }
static void XBOX_CoreUiRequest(int enabled);
static unsigned XBOX_CoreRecoveryEpoch(void* unused);
int main(void) {
    unsigned i, before;
    XBOX_CoreUiRequest(0); assert(!XBOX_CoreRecoveryEpoch(NULL) && !polls);
    failure = 1; XBOX_CoreUiRequest(1); assert(!g_coreUiListener);
    failure = 2; XBOX_CoreUiRequest(1); assert(!g_coreUiListener && !closes);
    failure = 0; XBOX_CoreUiRequest(1);
    assert(!XBOX_CoreRecoveryEpoch(NULL));
    event(0); assert(!XBOX_CoreRecoveryEpoch(NULL)); /* initial close is not a cycle */
    event(1); assert(!XBOX_CoreRecoveryEpoch(NULL));
    event(0); assert(XBOX_CoreRecoveryEpoch(NULL) == 1);
    event(0); assert(XBOX_CoreRecoveryEpoch(NULL) == 1);
    event(1); assert(!XBOX_CoreRecoveryEpoch(NULL));
    event(0); assert(XBOX_CoreRecoveryEpoch(NULL) == 2);
    head = tail = 0;
    for (i = 0; i < 70; ++i) event((i & 1) == 0);
    before = polls; assert(!XBOX_CoreRecoveryEpoch(NULL) && polls - before == 32);
    before = polls; assert(!XBOX_CoreRecoveryEpoch(NULL) && polls - before == 32);
    assert(XBOX_CoreRecoveryEpoch(NULL) == 37);
    XBOX_CoreUiRequest(0); assert(!g_coreUiListener && closes == 1);
    before = polls; assert(!XBOX_CoreRecoveryEpoch(NULL) && polls == before);
    XBOX_CoreUiRequest(1); assert(!XBOX_CoreRecoveryEpoch(NULL));
    event(0); assert(!XBOX_CoreRecoveryEpoch(NULL));
    event(1); event(0); assert(XBOX_CoreRecoveryEpoch(NULL) == 1);
    XBOX_CoreUiRequest(0); assert(closes == 2 && creates == 4);
    puts("PASS extracted notification adapter: handle failures, unknown/open/close state, duplicate events, bounded drain and cleanup");
    puts("Scope: real adapter source with modeled XNotify APIs; not real Guide events or FPS");
    return 0;
}
