/* Standalone policy regression: no XDK, SDL, or Xbox hardware required. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../../SDL/src/video/xbox/SDL_xbox_asyncpresent.h"
#include "../../SDL/src/video/xbox/SDL_xbox_ui_recovery.h"

typedef struct Mock {
    unsigned tick, due, fence_id, sync_fence, creates, destroys, normals, swaps, clears;
    int resource, mode, displayed, queued, synchronous, forced, oom, swap_fail, stuck;
    unsigned last_slot;
    XBOX_UiRecovery ui;
    unsigned ui_polls;
} Mock;

static void progress(Mock* m)
{
    if (!m->stuck && (m->queued >= 0 || m->synchronous) &&
        (int)(m->tick - m->due) >= 0) {
        m->displayed = m->synchronous ? 0 : m->queued;
        m->queued = -1;
        m->synchronous = 0;
    }
}
static int create(void* p)
{
    Mock* m = (Mock*)p;
    ++m->creates;
    assert(!m->resource && !m->mode && m->displayed == 0);
    if (m->oom) return 0;
    m->resource = 1;
    return 1;
}
static void destroy(void* p)
{
    Mock* m = (Mock*)p;
    progress(m);
    assert(m->resource && !m->mode && m->queued < 0);
    assert(!m->synchronous && m->displayed == 0);
    m->resource = 0;
    ++m->destroys;
}
static void mode(void* p, int enabled)
{
    Mock* m = (Mock*)p;
    progress(m);
    assert(m->queued < 0);
    if (enabled) assert(m->resource && !m->synchronous);
    m->mode = enabled;
    m->forced = 0;
}
static unsigned pending(void* p)
{
    Mock* m = (Mock*)p;
    progress(m);
    return m->queued >= 0;
}
static int forced(void* p) { return ((Mock*)p)->forced; }
static void normal(void* p)
{
    Mock* m = (Mock*)p;
    progress(m);
    assert(!m->mode && m->queued < 0);
    ++m->normals;
    /* CPU return is NOT scanout retirement. A fence must wait for this
       synchronous pointer switch, even if an earlier async fence passed. */
    if (!m->synchronous) m->due = m->tick + 3;
    m->synchronous = 1;
}
static int asynchronous(void* p, unsigned slot)
{
    Mock* m = (Mock*)p;
    progress(m);
    assert(m->resource && m->mode && slot < 2);
    assert(m->queued < 0 && !m->synchronous && (int)slot != m->displayed);
    if (m->swap_fail) return 0;
    m->queued = (int)slot;
    m->last_slot = slot;
    m->due = m->tick + 3;
    ++m->swaps;
    return 1;
}
static unsigned fence(void* p)
{
    Mock* m = (Mock*)p;
    ++m->fence_id;
    if (m->synchronous) m->sync_fence = m->fence_id;
    return m->fence_id;
}
static int fence_pending(void* p, unsigned f)
{
    Mock* m = (Mock*)p;
    progress(m);
    /* Async GPU completion can precede VBlank: such a fence is deliberately
       complete while queued/displayed still belongs to the extra texture. */
    return f == m->sync_fence && m->synchronous;
}
static unsigned ticks(void* p) { return ((Mock*)p)->tick; }
static void delay(void* p) { ++((Mock*)p)->tick; progress((Mock*)p); }
static void clear(void* p) { ++((Mock*)p)->clears; }
static unsigned recovery_epoch(void* p)
{
    Mock* m = (Mock*)p;
    ++m->ui_polls;
    return XBOX_UiRecoveryEpoch(&m->ui);
}
static const XBOX_AsyncPresentOps op = {
    create, destroy, mode, pending, forced, normal, asynchronous, fence,
    fence_pending, ticks, delay, clear, recovery_epoch
};
static void init(XBOX_AsyncPresent* s, Mock* m)
{
    memset(s, 0, sizeof(*s)); memset(m, 0, sizeof(*m)); m->queued = -1;
    XBOX_UiRecoveryReset(&m->ui);
}
static void frame(XBOX_AsyncPresent* s, Mock* m, int vsync)
{
    XBOX_AsyncPresentFrame(s, &op, m, vsync);
    assert(s->resources == m->resource && s->active == m->mode);
}
static void activate(XBOX_AsyncPresent* s, Mock* m)
{
    XBOX_AsyncRequest(s, 1); frame(s, m, 1);
    assert(m->last_slot == 1 && m->swaps == 1 && s->active);
}
static void lifecycle(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned i;
    init(&s, &m); activate(&s, &m);
    assert(!fence_pending(&m, m.fence_id) && m.queued == 1);
    for (i = 0; i < 20; ++i) {
        XBOX_AsyncRequest(&s, 1); frame(&s, &m, 1);
        assert(m.last_slot == (i & 1));
    }
    assert(m.creates == 1 && m.destroys == 0 && s.activations == 1);
    XBOX_AsyncRequest(&s, 0); frame(&s, &m, 1);
    assert(!s.active && s.retiring && m.resource && !m.destroys);
    assert(!m.clears); /* Regular game frames retain the rendered image. */
    m.tick += 3; frame(&s, &m, 1);
    assert(!m.resource && m.destroys == 1);
}
static void failures(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned i, swaps;
    init(&s, &m); m.oom = 1; XBOX_AsyncRequest(&s, 1);
    for (i = 0; i < 10; ++i) { XBOX_AsyncRequest(&s, 1); frame(&s, &m, 1); m.tick += 10; }
    assert(m.creates == 1 && s.failed && !s.resources);
    m.oom = 0; XBOX_AsyncRequest(&s, 0); XBOX_AsyncRequest(&s, 1);
    frame(&s, &m, 1); assert(s.active && m.creates == 2);
    m.forced = 1; frame(&s, &m, 1);
    assert(s.failed && s.retiring && !s.active && m.resource);
    m.tick += 4; frame(&s, &m, 1);
    assert(!s.resources && m.creates == 2);
    init(&s, &m); activate(&s, &m); m.stuck = 1; swaps = m.swaps;
    frame(&s, &m, 1);
    assert(s.failed && s.active && s.resources && m.swaps == swaps && !m.normals);
    assert(s.timeouts == 1 && m.tick == 100);
    assert(!XBOX_AsyncPrepareReset(&s, &op, &m));
    assert(s.active && m.resource && !m.destroys && !m.clears && !m.normals);
    m.stuck = 0;
    assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    assert(!s.resources && !s.active && m.clears == 1 && m.destroys == 1);
    init(&s, &m); m.swap_fail = 1; XBOX_AsyncRequest(&s, 1); frame(&s, &m, 1);
    assert(s.active && s.failed && s.resources && !m.swaps);
    frame(&s, &m, 1); assert(s.retiring && !s.active);
    m.tick += 4; frame(&s, &m, 1); assert(!s.resources);
}
static void resets(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned i;
    init(&s, &m);
    assert(XBOX_AsyncPrepareReset(&s, &op, &m)); assert(!m.clears && !m.normals);
    XBOX_AsyncRequest(&s, 1); frame(&s, &m, 0);
    assert(!s.active && !s.failed && !m.creates);
    m.tick += 4; activate(&s, &m);
    m.tick += 4; progress(&m); m.stuck = 1;
    assert(!XBOX_AsyncPrepareReset(&s, &op, &m));
    assert(s.retiring && !s.active && m.resource && !m.destroys);
    assert(m.clears == 1 && m.displayed == 1);
    m.stuck = 0; assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    assert(m.clears == 1 && m.destroys == 1 && !s.resources);
    init(&s, &m); m.tick = UINT_MAX - 1; s.sequence = UINT_MAX;
    activate(&s, &m); assert(s.sequence == 0);
    frame(&s, &m, 1); assert(!s.failed && m.last_slot == 0);
    XBOX_AsyncRequest(&s, 0); assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    for (i = 0; i < 3; ++i) {
        XBOX_AsyncRequest(&s, 1); frame(&s, &m, 1);
        assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    }
    assert(m.creates == m.destroys);
}
static void advance_frame(XBOX_AsyncPresent* s, Mock* m, unsigned ms, int vsync)
{
    m->tick += ms; progress(m); frame(s, m, vsync);
}
static void ui_recovery(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned i, start, creates;
    init(&s, &m); activate(&s, &m);
    for (i = 0; i < 8; ++i) advance_frame(&s, &m, 17, 1);
    assert(!m.ui_polls); /* Healthy frames never poll notification state. */
    XBOX_UiRecoveryEvent(&m.ui, 1); m.forced = 1; frame(&s, &m, 1);
    assert(s.failed && s.failure_reason == XBOX_ASYNC_FORCED && s.forced_fallbacks == 1);
    assert(s.retiring && !m.destroys);
    for (i = 0; i < 120; ++i) advance_frame(&s, &m, 17, 1);
    assert(!s.active && !s.resources && m.creates == 1 && !s.recovery_attempts);
    XBOX_UiRecoveryEvent(&m.ui, 0); advance_frame(&s, &m, 17, 1);
    start = m.tick;
    advance_frame(&s, &m, 999, 1); assert(!s.active && m.creates == 1);
    advance_frame(&s, &m, 1, 1);
    assert(s.active && !s.failed && s.recovery_attempts == 1 && s.activations == 2);
    assert((unsigned)(m.tick - start) >= 1000 && m.creates == 2 && m.destroys == 1);
    /* A second forced swap without a NEW close must not become a retry loop. */
    m.forced = 1; frame(&s, &m, 1);
    for (i = 0; i < 150; ++i) advance_frame(&s, &m, 17, 1);
    assert(s.failed && !s.active && m.creates == 2 && s.recovery_attempts == 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); /* duplicate close is not a new token */
    advance_frame(&s, &m, 2000, 1); assert(m.creates == 2);
    XBOX_UiRecoveryEvent(&m.ui, 1); advance_frame(&s, &m, 17, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); advance_frame(&s, &m, 17, 1);
    advance_frame(&s, &m, 1001, 1); assert(s.active && s.recovery_attempts == 2);
    /* Reopening UI cancels an in-progress quiet interval. */
    XBOX_UiRecoveryEvent(&m.ui, 1); m.forced = 1; frame(&s, &m, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); advance_frame(&s, &m, 17, 1);
    advance_frame(&s, &m, 900, 1);
    XBOX_UiRecoveryEvent(&m.ui, 1); advance_frame(&s, &m, 17, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); advance_frame(&s, &m, 17, 1);
    advance_frame(&s, &m, 999, 1); assert(!s.active);
    advance_frame(&s, &m, 1, 1); assert(s.active && s.recovery_attempts == 3);
    assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    /* No resources may be freed/recreated while the old scanout is stalled. */
    init(&s, &m); activate(&s, &m);
    XBOX_UiRecoveryEvent(&m.ui, 1); m.forced = 1; frame(&s, &m, 1);
    m.stuck = 1; XBOX_UiRecoveryEvent(&m.ui, 0);
    for (i = 0; i < 150; ++i) advance_frame(&s, &m, 17, 1);
    assert(s.resources && s.retiring && !s.active && !m.destroys && m.creates == 1);
    m.stuck = 0; advance_frame(&s, &m, 17, 1); assert(s.active && m.creates == 2);
    /* OOM during retry is hard, even with subsequent UI-close notifications. */
    XBOX_UiRecoveryEvent(&m.ui, 1); m.forced = 1; frame(&s, &m, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); m.oom = 1;
    advance_frame(&s, &m, 17, 1); advance_frame(&s, &m, 1001, 1);
    assert(s.failure_reason == XBOX_ASYNC_CREATE && !s.active);
    creates = m.creates; m.oom = 0;
    XBOX_UiRecoveryEvent(&m.ui, 1); XBOX_UiRecoveryEvent(&m.ui, 0);
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 1);
    assert(m.creates == creates && s.failed);
}

static void recovery_faults(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned i, creates;
    init(&s, &m); m.swap_fail = 1; XBOX_AsyncRequest(&s, 1); frame(&s, &m, 1);
    assert(s.failure_reason == XBOX_ASYNC_SUBMIT);
    XBOX_UiRecoveryEvent(&m.ui, 1); XBOX_UiRecoveryEvent(&m.ui, 0);
    m.swap_fail = 0;
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 1);
    assert(s.failed && m.creates == 1 && !s.recovery_attempts && !m.ui_polls);
    init(&s, &m); activate(&s, &m); m.stuck = 1; frame(&s, &m, 1);
    assert(s.failure_reason == XBOX_ASYNC_QUEUE_TIMEOUT);
    m.stuck = 0; XBOX_UiRecoveryEvent(&m.ui, 1); XBOX_UiRecoveryEvent(&m.ui, 0);
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 1);
    assert(s.failed && !s.recovery_attempts && !m.ui_polls);
    /* Timeout in the recovery fence is also sticky; never force activation. */
    init(&s, &m); activate(&s, &m);
    XBOX_UiRecoveryEvent(&m.ui, 1); m.forced = 1; frame(&s, &m, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0); advance_frame(&s, &m, 17, 1);
    advance_frame(&s, &m, 100, 1); assert(!s.resources);
    m.stuck = 1; advance_frame(&s, &m, 1001, 1);
    assert(s.failure_reason == XBOX_ASYNC_RESET_TIMEOUT && !s.active && m.creates == 1);
    m.stuck = 0; advance_frame(&s, &m, 1001, 1); assert(s.failed && m.creates == 1);
    /* No UI callback, unknown state or close-without-open keeps old behavior. */
    init(&s, &m); activate(&s, &m); m.forced = 1; frame(&s, &m, 1);
    XBOX_UiRecoveryEvent(&m.ui, 0);
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 1);
    assert(s.failed && m.creates == 1 && !s.recovery_attempts);
    /* VSync disabled and request cancellation prevent recovery. */
    XBOX_UiRecoveryEvent(&m.ui, 1); XBOX_UiRecoveryEvent(&m.ui, 0);
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 0);
    assert(!s.active && !s.recovery_attempts);
    XBOX_AsyncRequest(&s, 0); creates = m.creates;
    for (i = 0; i < 100; ++i) advance_frame(&s, &m, 17, 1);
    assert(m.creates == creates && !s.active);
    /* Both millisecond and notification epochs may wrap unsigned integers. */
    init(&s, &m); m.tick = UINT_MAX - 500; activate(&s, &m);
    m.ui.epoch = UINT_MAX; XBOX_UiRecoveryEvent(&m.ui, 1);
    m.forced = 1; frame(&s, &m, 1); XBOX_UiRecoveryEvent(&m.ui, 0);
    assert(m.ui.epoch == 1);
    advance_frame(&s, &m, 17, 1); advance_frame(&s, &m, 1001, 1);
    assert(s.active && !s.failed && s.recovery_attempts == 1);
    assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    puts("PASS UI-close recovery: stable closure, one retry/epoch, scanout retirement, hard faults, cancellation and wrap");
}

static void stress(void)
{
    XBOX_AsyncPresent s; Mock m; unsigned rng = 0x832d9247, i;
    int vsync = 1;
    init(&s, &m);
    for (i = 0; i < 100000; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        m.tick += 17; progress(&m);
        switch (rng & 63) {
        case 0: XBOX_AsyncRequest(&s, 0); break;
        case 1: XBOX_AsyncRequest(&s, 1); break;
        case 2: if (s.active) m.forced = 1; break;
        case 3: vsync ^= 1; break;
        case 4: assert(XBOX_AsyncPrepareReset(&s, &op, &m)); break;
        case 5: m.oom ^= 1; break;
        case 6: m.swap_fail ^= 1; break;
        case 7: XBOX_UiRecoveryEvent(&m.ui, 1); break;
        case 8: XBOX_UiRecoveryEvent(&m.ui, 0); break;
        default: break;
        }
        frame(&s, &m, vsync);
    }
    assert(XBOX_AsyncPrepareReset(&s, &op, &m));
    assert(!s.resources && !m.resource);
}
int main(void)
{
    lifecycle(); failures(); resets(); ui_recovery(); recovery_faults(); stress();
    puts("async presentation policy: PASS (lifetime, queue, fallback, timeout, wrap, 100000 events)");
    return 0;
}
