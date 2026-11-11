# DDPDFK Background Music Issue Context for Codex

The full investigation and handover document for the *DoDonPachi DaiFukkatsu* (`ddpdfk`) background music (BGM) disappearance bug has been generated and tracked under:

👉 [`docs/optimization/2026-11-11-cv1k-ddpdfk-bgm-investigation-and-handover-for-codex.md`](docs/optimization/2026-11-11-cv1k-ddpdfk-bgm-investigation-and-handover-for-codex.md)

### Key Summary for Codex
1. **Symptom**: During gameplay on Xbox 360 Jasper, BGM cuts out after a while, but sound effects (SFX) continue playing normally.
2. **Key Clues**:
   - Simple Pause menu and resume does NOT restore music.
   - Saving progress to USB Slot 0 (`saveState()`) and resuming immediately restores music.
   - User confirmed this is a regression ("这个音乐问题以前没有，不知道是哪个提交造成的").
   - Commit `6e1fb9e2` (blitter busy clear on frame boundaries) was tested on real console and did NOT fix the problem ("仍然没有解决").
3. **Core Hypotheses to Investigate**:
   - **Hypothesis A**: YMZ770 MPEG decoder buffer error in `ymz770.cpp:355-368` clearing `channel.is_playing = false` when `decode_buffer()` fails or runs dry.
   - **Hypothesis B**: `Sh3BurnCycles` in `speedhack_read_long` (`26adabb1`) or `gfx_exec_write` (`8d571ced`) causing `Sh3TotalCycles()` to leap forward, causing `stream.update()` to fast-forward Sequencer 0 by hundreds of cycles in a tight host burst and killing phrase/stream timing.
   - **Hypothesis C**: Salvia frontend `StateAudioPause` (`gameMenu->g_audioBuffer.Clear()`) resetting audio ring buffer state.
   - **Recommended Approach**: Run `git bisect` between good baseline (around Nov 3 ~ Nov 4, before `638f80dc` / `779a9a16`) and current HEAD to pinpoint the exact commit that broke BGM.
