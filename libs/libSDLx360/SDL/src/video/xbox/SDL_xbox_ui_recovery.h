/* Notification state, independent of XDK so its conservative policy is tested.
   A close without an observed open is not evidence permitting recovery. */
#ifndef SDL_XBOX_UI_RECOVERY_H
#define SDL_XBOX_UI_RECOVERY_H
typedef struct XBOX_UiRecovery {
    int visible;
    unsigned epoch;
} XBOX_UiRecovery;
static void XBOX_UiRecoveryReset(XBOX_UiRecovery* s)
{
    s->visible = -1; s->epoch = 0;
}
static void XBOX_UiRecoveryEvent(XBOX_UiRecovery* s, int visible)
{
    visible = !!visible;
    if (!visible && s->visible == 1) {
        ++s->epoch;
        if (!s->epoch) ++s->epoch;
    }
    s->visible = visible;
}
static unsigned XBOX_UiRecoveryEpoch(const XBOX_UiRecovery* s)
{
    return s->visible == 0 ? s->epoch : 0;
}
#endif
