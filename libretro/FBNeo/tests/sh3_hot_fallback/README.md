# Hot SH3 interpreter fallback regression

Run `python3 run.py --output /tmp/salvia-hot-fallback-tests` from this directory, or use its repository-relative path. Generated translation units, executables and logs are written only to the requested output directory.

The runner extracts the unchanged general `execute_one` decoder and the actual sixteen 0x6 instruction helpers from production `sh4.cpp`. Other instruction families are stubbed to identify the selected handler. The production `sh3_interpreter_hot.h` is then compared with the general decoder for every 16-bit opcode and eight register/callback contexts, totaling 524288 cases per executable. The comparison includes all register fields, T, EA, callback address/width/order and modeled idle-cycle charging. Only 0x60xx may bypass the general decoder. The n == m == 0 postincrement cases are included.

Optimized and ASan/UBSan builds run separately. The sanitizer build excludes **shift-base** only because the unchanged legacy EXTSB/EXTSW implementations use signed-left-shift idioms. This exclusion is recorded in the actual argv; it is not a claim that those old implementations are free of all C++ undefined behavior. All other selected sanitizer checks remain enabled. Production instruction implementations are not rewritten by this test or optimization.

Also run `../sh3_dispatch/run_inline.py` and `../sh3_work_profile/run_outer.py` with the archived pre-instrumentation `sh4.cpp`. That outer-loop fixture abstracts instruction dispatch to a callback; this suite independently checks the actual new selector and the sixteen real helper bodies. These are complementary host checks, not a full CPU/game replay or execution of generated PowerPC instructions. Native XDK disassembly and console comparison are separate evidence.
