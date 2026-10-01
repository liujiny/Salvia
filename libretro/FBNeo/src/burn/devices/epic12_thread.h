// CV1000 keeps the blit list in order: uploads and draws can read back earlier
// results from the same VRAM. Run that list on one persistent physical core
// while the main core emulates the SH3; do not split dependent commands.
#if defined(_XBOX) || defined(FBNEO_RENDER_THREADS_TEST)
#include "render_worker.h"
#include "salvia_fbneo_diagnostics.h"

static void epic12_thread_job(INT32, INT32, INT32);

struct epic12_thread {
	BurnRenderWorker worker;
	void (*our_callback)();
	INT32 startup_frame;
	bool available, enabled, pending;
#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS
	UINT64 diag_wait_ticks, diag_wait_peak;
	unsigned diag_jobs, diag_wait_calls, diag_wait_samples, diag_wait_rng;
#endif

	void init(void (*callback)()) {
		our_callback = callback;
		startup_frame = 0;
		enabled = true;
		pending = false;
#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS
		diag_wait_ticks=diag_wait_peak=0;
		diag_jobs=diag_wait_calls=diag_wait_samples=0;
		diag_wait_rng=0x9e3779b9u;
#endif
		available = worker.init(epic12_thread_job, 1);
	}

	void notify_wait() {
		if (!pending) { worker.finish(); return; }
#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS
		++diag_wait_calls;
		unsigned x=diag_wait_rng;
		x^=x<<13; x^=x>>17; x^=x<<5; diag_wait_rng=x;
		bool sample=!(x&63);
		LARGE_INTEGER begin,end;
		if(sample) QueryPerformanceCounter(&begin);
#endif
		worker.finish();
		pending=false;
#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS
		if(sample) {
			QueryPerformanceCounter(&end);
			UINT64 ticks=end.QuadPart-begin.QuadPart;
			diag_wait_ticks+=ticks;
			if(ticks>diag_wait_peak) diag_wait_peak=ticks;
			++diag_wait_samples;
		}
#endif
	}
	void exit() { notify_wait(); worker.exit(); available = false; pending=false; }
	void reset() { notify_wait(); startup_frame = 180; }
	void scan() { notify_wait(); SCAN_VAR(startup_frame); }

	void set_threading(INT32 value) {
		// A change to single-core mode must finish the previous list first.
		bool requested = value && nBurnRenderCores > 1;
		if (enabled != requested) notify_wait();
		enabled = requested;
	}

	void notify() {
		if (startup_frame > 0) {
			startup_frame--;
			our_callback();
		} else if (available && enabled) {
			worker.start(0, 0);
			pending = true;
#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS
			++diag_jobs;
#endif
		} else {
			our_callback();
		}
	}
};

static epic12_thread thready;

static void epic12_thread_job(INT32, INT32, INT32)
{
	thready.our_callback();
}
#else
#include "thready.h"
#endif
