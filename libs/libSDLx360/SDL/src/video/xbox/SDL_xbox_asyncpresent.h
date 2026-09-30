/* Bounded, two-front-buffer presentation policy. The platform adapter owns
 * textures and GPU calls; this C controller is also used by the regression.
 * A pending/displayed buffer is never overwritten or released on timeout. */
#ifndef SDL_XBOX_ASYNCPRESENT_H
#define SDL_XBOX_ASYNCPRESENT_H

typedef struct XBOX_AsyncPresentOps {
    int (*create)(void*);
    void (*destroy)(void*);
    void (*mode)(void*, int);
    unsigned (*pending)(void*);
    int (*forced_sync)(void*);
    void (*normal)(void*);
    int (*asynchronous)(void*, unsigned);
    unsigned (*fence)(void*);
    int (*fence_pending)(void*, unsigned);
    unsigned (*ticks)(void*);
    void (*delay)(void*);
    void (*reset_clear)(void*);
    /* Zero blocks retry. A new nonzero token certifies an observed UI close. */
    unsigned (*recovery_epoch)(void*);
} XBOX_AsyncPresentOps;

enum XBOX_AsyncFailure {
    XBOX_ASYNC_OK = 0, XBOX_ASYNC_FORCED = 1, XBOX_ASYNC_CREATE = 2,
    XBOX_ASYNC_QUEUE_TIMEOUT = 3, XBOX_ASYNC_SUBMIT = 4, XBOX_ASYNC_RESET_TIMEOUT = 5
};

typedef struct XBOX_AsyncPresent {
    int requested, failed, active, resources;
    int retiring;
    unsigned retire_fence, next, sequence;
    unsigned activations, fallbacks, timeouts;
    unsigned failure_reason, forced_fallbacks, recovery_attempts, recovered_epoch;
    unsigned quiet_epoch, quiet_since;
    int quiet_valid;
} XBOX_AsyncPresent;

static void XBOX_AsyncRequest(XBOX_AsyncPresent* s, int enabled)
{
    enabled = !!enabled;
    /* Called each batch. Repeated enables cannot clear a fault. An explicit
       off/on starts a new request; forced-sync recovery is separately gated. */
    if (s->requested != enabled) {
        s->requested = enabled;
        s->failed = 0;
        s->failure_reason = XBOX_ASYNC_OK;
        s->quiet_valid = 0;
        s->recovered_epoch = 0;
    }
}

static void XBOX_AsyncFail(XBOX_AsyncPresent* s, unsigned reason)
{
    s->failed = 1;
    s->failure_reason = reason;
    s->quiet_valid = 0;
}

static int XBOX_AsyncWait(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op,
                         void* user, int fence, unsigned value);

/* Only an observed system-UI close can rearm a transient forced-sync fault.
   Wait for stable closure and retirement of the OLD display resource first.
   One close token buys at most one attempt; OOM/submit/timeouts stay sticky. */
static void XBOX_AsyncRecover(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op,
                             void* user, int vsync)
{
    unsigned epoch, now;
    if (!s->requested || !vsync || s->active || !s->failed ||
        s->failure_reason != XBOX_ASYNC_FORCED || !op->recovery_epoch) return;
    epoch = op->recovery_epoch(user);
    if (!epoch || epoch == s->recovered_epoch) { s->quiet_valid = 0; return; }
    now = op->ticks(user);
    if (!s->quiet_valid || epoch != s->quiet_epoch) {
        s->quiet_valid = 1; s->quiet_epoch = epoch; s->quiet_since = now;
        return;
    }
    if ((unsigned)(now - s->quiet_since) < 1000 || s->resources || s->retiring ||
        op->pending(user)) return;
    s->recovered_epoch = epoch;
    ++s->recovery_attempts;
    /* Also retire the most recent normal Present, not just the old texture. */
    if (!XBOX_AsyncWait(s, op, user, 1, op->fence(user))) {
        XBOX_AsyncFail(s, XBOX_ASYNC_RESET_TIMEOUT); return;
    }
    s->failed = 0; s->failure_reason = XBOX_ASYNC_OK; s->quiet_valid = 0;
}

static int XBOX_AsyncWait(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op,
                        void* user, int fence, unsigned value)
{
    unsigned start = op->ticks(user), polls = 0;
    while (fence ? op->fence_pending(user, value) : op->pending(user) != 0) {
        if ((unsigned)(op->ticks(user) - start) >= 100 || ++polls >= 200) {
            ++s->timeouts;
            return 0;
        }
        op->delay(user);
    }
    return 1;
}

static void XBOX_AsyncCollect(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op, void* user)
{
    if (s->retiring && !op->fence_pending(user, s->retire_fence)) {
        /* This fence follows a SYNCHRONOUS Present to the automatic front
           buffer. Unlike a fence after async Swap, it proves extra scanout
           has ended. Keeping the texture until then is mandatory. */
        op->destroy(user);
        s->resources = s->retiring = 0;
    }
}

static int XBOX_AsyncStop(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op, void* user)
{
    if (!s->active) return 1;
    if (!XBOX_AsyncWait(s, op, user, 0, 0)) return 0;
    op->mode(user, 0);
    s->active = 0;
    return 1;
}

static void XBOX_AsyncNormal(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op, void* user)
{
    op->normal(user);
    if (s->resources && !s->retiring) {
        s->retire_fence = op->fence(user);
        s->retiring = 1;
    }
}

/* Return zero only when a frame is safely skipped after a timeout/failure.
   In that case all possibly visible/pending resources remain owned. */
static int XBOX_AsyncPresentFrame(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op,
                                void* user, int vsync)
{
    ++s->sequence;
    XBOX_AsyncCollect(s, op, user);
    if (s->active && op->forced_sync(user) && !s->failed) {
        ++s->fallbacks; ++s->forced_fallbacks;
        XBOX_AsyncFail(s, XBOX_ASYNC_FORCED);
    }
    if (s->active && (!s->requested || !vsync || s->failed)) {
        if (!XBOX_AsyncStop(s, op, user)) {
            XBOX_AsyncFail(s, XBOX_ASYNC_QUEUE_TIMEOUT); return 0;
        }
    }
    XBOX_AsyncRecover(s, op, user, vsync);
    if (s->requested && vsync && !s->failed && !s->resources) {
        if (op->create(user)) {
            s->resources = s->active = 1;
            s->next = 1; /* The automatic buffer (0) is currently displayed. */
            op->mode(user, 1);
            ++s->activations;
        } else {
            XBOX_AsyncFail(s, XBOX_ASYNC_CREATE); ++s->fallbacks;
        }
    }
    if (s->active) {
        if (!XBOX_AsyncWait(s, op, user, 0, 0)) {
            XBOX_AsyncFail(s, XBOX_ASYNC_QUEUE_TIMEOUT); ++s->fallbacks; return 0;
        }
        if (!op->asynchronous(user, s->next)) {
            XBOX_AsyncFail(s, XBOX_ASYNC_SUBMIT); ++s->fallbacks; return 0;
        }
        s->next ^= 1;
        /* Kick the small final draw/resolve/swap packet immediately. Its
           fence is NOT sufficient to release an async display texture. */
        op->fence(user);
    } else XBOX_AsyncNormal(s, op, user);
    return 1;
}

/* Before Reset/resize/teardown: return to the automatic front buffer and
   wait boundedly for its synchronous swap. Caller must abort resource reset
   on zero; a stalled GPU is not permission to free its display textures. */
static int XBOX_AsyncPrepareReset(XBOX_AsyncPresent* s, const XBOX_AsyncPresentOps* op, void* user)
{
    XBOX_AsyncCollect(s, op, user);
    if (!s->resources) return 1;
    if (!XBOX_AsyncStop(s, op, user)) {
        XBOX_AsyncFail(s, XBOX_ASYNC_QUEUE_TIMEOUT); return 0;
    }
    if (!s->retiring) {
        ++s->sequence;
        /* An offscreen core may have borrowed EDRAM since the last frame. */
        op->reset_clear(user);
        XBOX_AsyncNormal(s, op, user);
    }
    if (!XBOX_AsyncWait(s, op, user, 1, s->retire_fence)) {
        XBOX_AsyncFail(s, XBOX_ASYNC_RESET_TIMEOUT); return 0;
    }
    XBOX_AsyncCollect(s, op, user);
    return 1;
}

#endif
