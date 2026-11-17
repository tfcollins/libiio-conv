# libiio v1 MATLAB Bindings Implementation Plan

> **Status (2026-11-16): executed.** This is the historical implementation plan of the MATLAB bindings, kept as a record of the design decisions. Every task is done; the checkboxes were not ticked. Where the plan differs from the code, the code and `matlab/README.md` are authoritative. "Global Constraints" and its "Working rules" (no local MATLAB, the disk limit, the build directory names, the git rules) describe the environment the plan was executed in: they are not requirements for users or contributors.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Tasks of one wave run in parallel in ONE working tree; each implementer sees only the plan header (everything above "Tasks") and their own task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MATLAB bindings for libiio v1: one MEX gateway plus `+iio` handle classes that reach every public function of `iio.h`, with a one-call receive/transmit path that converts straight into and out of MATLAB arrays through libiio-conv.

**Architecture:** libiio-conv gains a C stream layer (`include/iio/iio-conv-stream.h`) that owns the block bookkeeping for "read one frame / write one frame", so the MEX fast path and any later generated code call the same C functions. One C MEX gateway `iio_mex` (command registry, handle table with generation-checked `uint64` ids, a single error exit) exposes libiio and that layer; handle classes in `matlab/+iio/` hold the ids. Every gateway source is compiled twice, against real MATLAB headers for `matlab/iio_mex.mexa64` and against a mock `mex.h` for sanitized C tests, because MATLAB itself runs only in GitHub Actions.

**Tech Stack:** C11 (gnu11), CMake ≥ 3.20 (FindMatlab, `matlab_add_mex ... R2018a`) + Ninja, GCC 13, CTest, ASan/UBSan, Python 3 standard library (check scripts), MATLAB R2025b and R2023b on GitHub-hosted runners (`matlab-actions/setup-matlab@v2`, `matlab-actions/run-tests@v2`), `matlab.unittest`, libiio v1 pinned commit with the `emu:` and `xml:` backends.

**Spec:** `docs/superpowers/specs/2026-11-15-libiio-conv-design.md` (section "Part 2" and "Changes after the C library landed"). Code generation (`coder.ExternalDependency`, `coder.ceval`) is out of scope for this plan (no MATLAB Coder licence in CI); the C stream layer keeps it possible.

**Branch:** `feat/matlab-bindings` (already checked out). `matlab/api-map.tsv` is committed together with this plan and is a contract, not a deliverable.

**Waves:** 0 = serial foundation, Tasks 1 to 9 in order (stream layer in C; gateway scaffold; the first slice proven in real MATLAB by CI on both releases; data-path foundations). Tasks 2, 4 and 8 are each two tasks done in order (2a, 2b; 4a, 4b; 8a, 8b); "Task 4" without a letter means both parts. 1 = Tasks 10 to 21, one command group and its classes each, disjoint files, in two sub-waves: 1a (pilot) = Tasks 12, 13, 14 in parallel; 1b = Tasks 10, 11, 15 to 21 in parallel. 2a = Task 22 (integration gate), alone. 2b = Tasks 23 to 27 in parallel. 2c = Task 28 (controller). The controller pushes and reads CI (gate G5) after Task 8b, after Task 9, after wave 1a, after wave 1b, after Task 22 and after wave 2b. Nothing of wave 1 starts before the CI run of Task 8b is green on R2025b and R2023b. Wave 1b starts only when the G5 run of wave 1a is green and the controller has written every M idiom that run corrected into "Conventions / MATLAB (M)". The controller gives Tasks 1 to 3 only "Global Constraints" and the sections `include/iio/iio-conv.h ...`, `tests/fixture_util.h`, `tests/fake_backend.h` and "Test data" of this header; they need none of the gateway sections.

**How to read this header:** the sections "Conventions", "Shared contracts", "Command reference" and "MATLAB surface" fix every name that crosses a task boundary. They are part of every task's Interfaces block; a task's own Interfaces block only says which of those names it consumes and produces. Appendix A holds the fixture XML, Appendix B the toolbox requirements behind the tags R1 ... R24 used in test names, Appendix C the procedure for a CI failure that no local gate shows. Evidence tags: **[RUN]** = built and run on the development machine by a throwaway prototype while this plan was written; **[CI]** = can only be confirmed by the `matlab` CI job.

## Global Constraints

Platform and versions
- libiio v1 = `deps/install` only (header `deps/install/include/iio/iio.h`, library `deps/install/lib/libiio.so.1`, commit `cee4310e47198ba2d20e55b482061e3ab30d35d2`). Never the system libiio (v0.26).
- MATLAB: minimum release R2023b; CI runs R2025b and R2023b; Linux x86-64 only (`.mexa64`). Base MATLAB only: no toolbox function, no `matlab.System`, no `loadlibrary` in `+iio`, tests or examples.
- MATLAB cannot be started on the development machine. Never run `matlab`. `/usr/local/MATLAB/R2025b` and `/usr/local/MATLAB/R2023b` provide headers, `libmx.so`/`libmex.so` and `bin/glnxa64/mlint` only. Real MATLAB runs only in GitHub Actions.

MEX build
- API: R2018a interleaved complex (`matlab_add_mex(... R2018a)`, `MATLAB_DEFAULT_RELEASE=R2018a`). Never `R2017b`; `-DMATLAB_DEFAULT_RELEASE=R2025b` does not compile.
- Real-MATLAB objects are compiled with `-fexceptions -fvisibility=default` (the project preset is hidden, which would drop `mexFunction`); the MEX is linked with `-Wl,--no-undefined`, libiio-conv statically, `libiio.so.1` dynamically through RUNPATH.
- `IIO_CONV_BUILD_MATLAB=ON` is never combined with `IIO_CONV_SANITIZE=ON`; the mock tests are sanitized by `IIO_CONV_MEX_SANITIZE` (default ON) in the same build directory.
- Gateway C code may call only the `mx*`/`mex*` functions declared in `tests/mxmock/matrix.h` and `tests/mxmock/mex.h`. Needing another one means adding it to the mock, to `tests/mex/mx_conformance.c` and to `tests/mex/test_mxmock.c` in the same commit (wave 0 only; a wave 1 task that needs one stops and reports). No typed accessors (`mxGetDoubles` ...), no `mxGetPr`, no `printf` (a macro for `mexPrintf` under `mex.h`; for the same reason write `format(__printf__, ...)`), no `__attribute__((cleanup))`.
- Error discipline: a command function returns 0 or a negative errno and never calls `mexErrMsgIdAndTxt`; it frees what it allocated, and every libiio object it created is in the handle table, or destroyed, before it returns or calls any `mx*` function that can fail. The dispatcher raises exactly once, last, with nothing held.
- Out of memory (every `mxCreate*`, `mxMalloc`, `mxArrayToUTF8String` can end the call non-locally): **result first**: a command that creates an owned libiio object calls `gw_ret_id(c, 0, 0)` before it creates the object, registers the object, then stores the id with `gw_set_id()`; no mx allocation follows the creation, so no object can exist whose id MATLAB never received. The same rule keeps the lock right: the dispatcher updates the lock only when a command returns, so a command that creates or destroys a root handle (`create_context`, `scan`, `test_create_fake_context`, and every destroy command) makes no mx allocation after the handle table changed; otherwise an allocation failure leaves the lock stale until the next gateway call returns (teardown stays ordered even then: the at-exit function calls `gw_handle_destroy_all()`). **Parking**: memory that libiio returns from `malloc` (`iio_context_get_xml`, `iio_attr_get_available`) is handed to `gw_park()` at once and never freed by the command (both helpers: `mx_util.h`). **Write, then count**: a command that changes the device and returns a value about that change (`attr_write_raw`, `attr_write_string`; in wave 1b any command that writes to a device or queues data and returns a count) creates its result first with `gw_ret_double(c, 0, 0)`, performs the write, and stores the value with `gw_set_double()`; otherwise a failed allocation of the count reports "out of memory" for a write that took effect, and a retry writes twice. Wave 1b applied the same order to every command that consumes or moves something and then returns a value: `block_dequeue`, `stream_get_next_block`, `event_stream_read`, `conv_stream_read`, `conv_stream_flush`, `device_reg_read` create their results before the libiio call and fill them in afterwards (a logical through `mxGetLogicals`, an id through `gw_set_id`). The fake backend has no write counter: "exactly one" is asserted on its log (one more `D`, `E`, `O`, `B`) or on the content and length of `fake.attr_data`.
- Per-call scratch memory is `mxMalloc`/`mxFree`; everything that outlives a call is `malloc`/`free`.
- Numeric locale: the gateway never relies on the `LC_NUMERIC` of the MATLAB process and never changes it. A libiio call that parses or prints a floating-point number with the C library is bracketed by `gw_c_numeric_enter()` / `gw_c_numeric_leave()` (`mx_util.h`), with no `mx*` call and no `return` in between; the list of such calls is "Conventions / Gateway (C)", **Numeric locale**. No `cmd_<group>.c` file calls `setlocale`, `uselocale`, `newlocale` or `localeconv` itself.

Identifiers and naming
- MATLAB error identifiers are `iio:<ERRNONAME>` and nothing else (`iio:EINVAL`, `iio:ETIMEDOUT`, `iio:ENOSYS`, `iio:EBADF`, `iio:EPERM`, `iio:EBUSY`; errno 95 is `iio:ENOTSUP`, 11 is `iio:EAGAIN`; an errno without a name is `iio:E<number>`). Message text is `"<command>: <text>"`. Tests compare identifiers; message text only with `strstr`/`contains`.
- Gateway command names follow the naming rule in "Conventions"; `matlab/api-map.tsv` and the "Command reference" are the contract for every command name and MATLAB member name. A task that needs a name that is in neither stops and reports.
- MATLAB: package `iio`, classes UpperCamel (`iio.Context`), methods and properties snake_case, named after the libiio C functions without their prefix (`find_device`, `read_longlong`; the Python binding has `find_device` but no `read_longlong`), name-value options UpperCamel (`SamplesPerFrame`). `delete` methods never throw.
- Files: gateway `matlab/mex/cmd_<group>.c`; gateway tests `tests/mex/test_cmd_<group>.c` (other gateway tests `tests/mex/test_<topic>.c`; all are registered automatically); C library tests `tests/test_<unit>.c`; MATLAB tests `matlab/tests/t<Subject>.m` (class name = file name, superclass `IioTestCase`, test methods lowerCamelCase naming the behaviour).

Data path
- The stream layer (`src/stream.c`) uses only public libiio functions and `iio_conv_*`. It never calls `iio_context_set_timeout`, never uses `struct iio_stream`, and never dequeues a block of a buffer that is not started. Its functions take and return pointers and scalars only and report failure as a negative errno: no callbacks, no varargs, no global state (keeps `coder.ceval` possible later).
- "No data in time" is never an error on the fast path: C returns `IIO_CONV_STREAM_NO_FRAME` (0), the gateway returns `valid = false`. Low-level `Block.dequeue` keeps libiio's errors (`iio:ETIMEDOUT`), except "not ready" with `nonblock`, which is `false`.
- The timeout of a context belongs to the whole context (libiio v1). A per-stream time limit exists only as the stream layer's own polling (`iio_conv_stream_set_timeout` > 0).
- The fast path and `Block.read` / `Block.write` always set `saturate = true`. Float to integer rounds half to even (libiio-conv rule), not half away from zero as MATLAB's `int16()` does; documented, not "fixed".
- The gateway never passes a mask, channel, buffer or block to libiio together with an object of another device (libiio does not check and writes out of bounds): it compares devices through `dp_device_of()` and returns `-EINVAL`.
- State misuse is reported by the gateway itself, identically on every backend, as `iio:EPERM`: enqueue of an enqueued block, dequeue of a block that is not enqueued, dequeue while the buffer stream is not started, data access to an enqueued block, any change through a borrowed (stream-owned or read-only) object.
- Options are passed to the gateway as ONE scalar struct argument; a list of channel ids given as text (a cell) is always its own argument (MATLAB's `struct('f', {cell})` builds a struct array); a `uint64` id vector may be an option field (`channels` of `block_read`, `block_write`).

Working rules
- Tests first: write the failing test, run it, implement, run it, commit.
- A task is done when its local gates pass (section "Gates"); M code is proven only by G5. A wave is done when G5 is green.
- Disk (about 2 GB free; a wave 1 build directory is about 30 MB, a full `build-mex` with every sanitized test about 250 MB): one build directory per task, named in the task, removed (`rm -rf`) before the task reports done. Never use `build`, `build-asan`, `build-rel`; never touch `deps/`. Test files go to a `mkdtemp` directory that the test removes. The controller runs at most six wave 1 tasks at the same time.
- Git, shared working tree and therefore ONE shared index: `git add <own paths>` (needed for new files), then always commit with a pathspec, `git commit -s -m "<message>" -- <own paths>` (a plain `git commit` would sweep in whatever another task has staged). Own paths = the files listed under the task's **Files**. Staged files of another task that `git status --short` shows are left alone. Never `git add -A`, `git commit -a`, `git stash`, `git checkout -- .`, `git reset --hard`, `git clean`. On an `index.lock` error wait 2 s and retry, at most 5 times. Implementers never push and never open pull requests.
- A gate message that names only files of another task running in the same wave is not yours: put it in your report, do not edit the file.
- emu traps: never dequeue a block of a buffer that is not started (5 s stall, then `-EBADF`); every `<buffer index>` of a fixture equals its position (else SIGSEGV); RX data comes verbatim from `<dir>/emu-rx_buf0.bin` whatever the mask (section "Test data"); always an absolute `emu:` path to a copy of the fixture in a temporary directory; a second stream on a buffer that already has one opens without `-EBUSY`; an input buffer opens only if its data file exists (`-ENOENT` for buffer 0 of `emu-multi`: the fixture writes only `emu-rx_buf0.bin`); a huge block size is a failed `malloc`.
- No test may take 4 s or more (CTest `TIMEOUT 30`). A slow test is a bug, usually the emu stall. In CI the hard limit of `check_junit.py` is `--max-seconds 4.9` (wave 2b review): the stall is 5 s (`emu_dequeue_block` waits 5000 ms), so nothing is lost, and the first test of a MATLAB session that pays for a cold start on a shared runner does not turn a passing run red. The only exemption is the two lint tests `mex_mlint` and `mex_mlint_r2023b` (`TIMEOUT 120`): the `mlint` of R2023b needs about 0.7 s to start and 0.12 s per file (3.2 s for the 22 files of Task 8a), so they grow with the number of `.m` files.

## Review Focus

Failure modes that cross task boundaries. Each names the tasks that hold the test.

1. **Destruction in the wrong order.** A `Block`, `BufferStream`, `Stream`, `EventStream`, `RxStream`, `Channel` or `Attr` object outlives its `Context` (scope exit, `clear all`, an error in the middle of a constructor). Destroy commands on stale ids are silent; any other use raises `iio:EBADF`; nothing crashes; nothing is destroyed twice. MATLAB turns an error inside `delete` into a warning, so CI runs the suite with `strict: true`. Tasks 5, 9 (mock); `tLifetime` in Task 22.
2. **Unload with streams open.** Each open buffer stream has a libiio thread. `clear mex` must not unload a gateway that holds handles; MATLAB exit and `iio.internal.shutdown()` close streams before contexts. Task 6 (lock), Tasks 9 and 21 (`unload` on the fake log), Task 10 (`closeOrderOnShutdown` in real MATLAB), Task 8 (`tSlice`), Task 22 (`tLifetime.shutdownClosesStreams`).
3. **`string` where `char` is expected**, string arrays and cellstr for channel lists, `string(missing)`, `""`, a name with an embedded NUL, a non-ASCII name. A MATLAB `string` object is not a char array in C (`mxArrayToUTF8String` returns NULL): M code converts, and the gateway answers a non-char argument with `iio:EINVAL`, never a NULL dereference. Text with an embedded NUL is `iio:EINVAL` from `gw_arg_str` (C would cut it there without notice: `find_device(['emu-phy' char(0) 'x'])` would find `emu-phy`, `write_string` would write half the text and report the full count). Task 6 (`mock_string_object()`), each class task for its own methods, `tStrings` in Task 22.
4. **Transmit data that is not what the stream expects**: a complex stream given a real array because the imaginary part was all zero, wrong column count, too many rows, N-D, sparse, empty, logical, char. No read past the end of the input (the mock gives every array an exact-size allocation, so ASan sees it). What is an error must not depend on whether the imaginary part happened to be zero: `block_write` puts the I/Q pair list through the converter of complex data also when the data arrives real. Tasks 3, 17 (`pair_raises` cases of `test_cmd_block.c`), 21 (`tx_shape`, `tx_real_input`); `tToolboxFlow` in Task 23.
5. **Channel order and subsets**: ids passed Q before I, a subset of the scan elements, a duplicate, an id of the other direction. Column k is channel k as passed. Task 2 (`rx_pairing`, `rx_subset`), Task 21, `tToolboxFlow.rxPairingFollowsIdOrder`.
6. **64-bit values**: `write_longlong(int64(9007199254740993))` reads back exactly; `2450000000` round-trips; `uint64` ids keep all bits; a non-integer double for an integer write is an argument error. Task 6 (`gw_arg_int`), Task 13.
7. **Binary attribute payloads**: 47000 bytes with NULs are written by byte count. emu stores attributes as C strings and cannot show it; the fake backend keeps the bytes it received. Tasks 10, 13.
8. **Timeouts**: the context timeout belongs to the whole context; a receive that gets no frame returns `valid = false`, not an error; `Inf` maps to `-1`, never a v0-style `0`. emu never times out; the fake backend reports "not ready". Task 12 (`timeoutIsPerContext`), Tasks 2 and 3 (fake `no_frame`, `tx_timeout`), Task 21 (`timeoutGivesInvalid`).
9. **The effective mask is wider than the requested one** (coupled channels on hardware): frame size, layout and converter come from `iio_buffer_stream_get_channels_mask()`, never from the mask the caller built. The fake backend's `couple` setting enables one more channel in `open_buffer`. Tasks 2, 3, 10 (`coupledChannelWidensSample`).
10. **Release and re-create**: a stream is closed and a new one with another `SamplesPerFrame` opens on the same device; a constructor that fails after the buffer was opened leaves no handle. Task 21 (`reopen`, `rx_errors`), `tToolboxFlow.rxReleaseAndReconfigure`; every MATLAB test ends with `iio.internal.handle_count() == 0` (`IioTestCase`).
11. **The last transmit frame and cyclic transmit**: a frame written just before close must reach the hardware (drain before cancel), and a cyclic block must never be dequeued. emu writes a TX block to its file only when the block is dequeued and ignores `cyclic`; both are asserted on the fake backend's log, and on emu a test calls `flush()` before reading the file. Tasks 3, 21.
12. **Scale and class combinations**: a scaled mode with an integer class, `Raw` with `Scale="iio"`, `Complex` with an odd channel count: each a defined `iio:EINVAL`. Tasks 1, 21; `tToolboxFlow.txDoubleRoundsAndSaturates`.
13. **MATLAB's libraries instead of the system's.** Inside MATLAB, libiio's libxml2 and libzstd resolve to MATLAB's bundled copies (libxml2 2.13.8 in R2025b, 2.10.3 in R2023b). Locally `mex_bundled_libs` runs the slice test with `LD_LIBRARY_PATH` on those libraries; in the MATLAB process Task 8 proves it before anything is built on it, with fallback F1 ready.
14. **Tests that pass by not running.** A missing MEX, fixture or test hook must fail, not skip: `EmuFixture.setup` asserts the MEX exists, `tFixture` (Task 8) checks the data, `check_junit.py` (Task 27) requires a minimum test count and zero skipped tests.
15. **Arguments nobody planned for.** Every registered command is called with no arguments and with random combinations of strings, numbers, empty arrays, cells, structs, live ids and stale ids: it returns or raises an `iio:` identifier, never crashes or leaks. `fuzzNoCrash` in Task 22.
16. **Out of memory inside a command.** Every `mxCreate*` can end the MEX non-locally; a libiio object created just before must already be in the handle table. Memory libiio returned from `malloc` must be parked. Rules "result first" and "parking" of Global Constraints; `mock_fail_alloc` (Task 4a), `mt_oom_sweep` (Task 6), one sweep per such command in the test of the task that owns it ("How tests are written"), all of them again in Task 22.
17. **`''` versus `[]` versus an error**: a device without name or label, a context attribute whose value is `""`, an attribute without a value (`-ENOENT`), a `find_*` that finds nothing: exactly as "Conventions" says. Tasks 12, 13, 14, 15.
18. **A converter that outlives its assumptions**: a converter cached for a buffer stream must not survive a change of a channel's data format (`Block.read` cache key, Task 17); output arrays of `conv_stream_read` and `block_read` are created once with `mxCreateUninitNumericMatrix` and every byte is written before return, also when `valid` is false (Tasks 17, 21: the mock fills uninitialised arrays with `0xA5`).
19. **A dequeue that timed out on libiio's generic block path** leaves the block dequeued; the next read gets `-EPERM`. `iio_conv_stream_read` must then queue the block again and report "no frame", not fail for ever. Not reproducible with `emu:` or the fake: reviewed by reading `src/stream.c` (Task 2b). The low-level `block_dequeue` clears `BLK_ENQUEUED` when libiio answers `-EPERM` (Task 17).
20. **A destructor that blocks.** `delete`, `clear all`, `iio.internal.shutdown()`, context destroy and MATLAB exit all run destroy ops, and a MEX call cannot be interrupted. No destroy op may wait without bound: the close drain of a transmit stream (bounded by the gateway's destroy op in `cmd_conv.c`, not by `iio_conv_stream_close`) and the destroy of an output `iio_stream` are bounded when the context timeout is infinite; a started buffer stream is cancelled and stopped before it is closed. emu and the fake never block: asserted on the fake log and by time limits (Tasks 3, 9, 16, 18, 21).
21. **An error-encoded pointer taken for an object.** Eight libiio functions return `iio_ptr(err)`, not `NULL`, on failure ("Type mapping"). Registering such a pointer gives a live-looking id whose first use dereferences `(void *)-110`. Tasks 16, 17, 18 (a failed `next`), 19, 20.
22. **The numeric locale of the MATLAB process.** libiio parses channel scales (`strtof`), ranges (`sscanf("%lf")`) and prints scales (`"%f"`) with the plain C functions. Under an `LC_NUMERIC` with a decimal comma, and without the gateway's guard, a context made from XML has scale 0 on every scaled channel with no error, `context_get_xml` says `scale="0,250000"`, `attr_get_range` of a fractional range is `iio:ENOTSUP`, and emu refuses a fractional write to an attribute with an `_available` range (all four measured **[RUN]** under `en_DK.UTF-8`, the first on `xml:`; `ip:`, `usb:`, `serial:` and the fake backend build their contexts with the same XML parser). Whether a MATLAB process can have such an `LC_NUMERIC` could not be established, so the gateway does not depend on it: rule **Numeric locale** of "Conventions / Gateway (C)". Tested on the mock under a real decimal-comma locale (`mt_comma_begin`, "How tests are written"): Tasks 6 (`gw_c_numeric_*`), 10, 11, 12, 13, and once more for every group in Task 22.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `include/iio/iio-conv.h`, `src/plan.c` | `raw` flag of the conversion spec | 1 |
| `include/iio/iio-conv-stream.h`, `src/stream.c` | C stream layer: whole frames to and from one buffer | 2, 3 |
| `tests/resources/iio_fixture.xml` | the `emu:` / `xml:` fixture (text in Appendix A) | 2a |
| `tests/fixture_util.h` | ramp data, temporary directory with a fixture copy | 2 |
| `tests/fake_backend.h` | test-only libiio backend: buffers, events, triggers, attributes, timeout | 2 |
| `tests/test_stream.c`, `tests/test_stream_fake.c` | stream layer tests (`emu:`, fake) | 2, 3 |
| `CMakeLists.txt`, `matlab/CMakeLists.txt`, `tests/mex/CMakeLists.txt` | options, mock and real object libraries, MEX target, test registration | 4 (7 adds the gate tests, 22 flips the strict default) |
| `matlab/mex/handles.h`, `handles.c` | handle table | 4, 5 |
| `matlab/mex/iio_mex.h`, `iio_mex.c` | `mexFunction`, dispatch, single error exit, lock, trace, `mex_*` commands | 4, 6 |
| `matlab/mex/mx_util.h`, `mx_util.c` | argument and return helpers, errno names | 4, 6 |
| `matlab/mex/registry.c` | `gw_groups[]` | 4 |
| `matlab/mex/cmd_<group>.c` | the commands of one group | 4 (complete stub tables), 8, 10 to 21 |
| `matlab/mex/data_path.h` | flags, device lookup, registration and option helpers of the data path | 9 |
| `matlab/tools/gen_enums.py` | enum name tables generated from `iio.h` at build time | 4 |
| `matlab/tools/check_mex_exports.sh` | gate G2 | 4 |
| `matlab/tools/check_api.py`, `check_mlint.sh`, `test_tools.py` | gates G4 and G3, and their self-test | 7 |
| `matlab/tools/check_junit.py` | CI result check | 27 |
| `matlab/api-map.tsv` | one row per `iio.h` function (committed with this plan) | 22 may correct rows |
| `tests/mxmock/matrix.h`, `mex.h`, `mxmock.h`, `mxmock.c` | mock MATLAB runtime | 4 |
| `tests/mex/mex_test.h`, `mex_test.c` | gateway test harness | 6 |
| `tests/mex/dp_test.h` | data-path object builders for gateway tests | 9 |
| `tests/mex/test_*.c` | gate G1 tests | each task |
| `matlab/+iio/+internal/*.m` | base class and helpers | 7 |
| `matlab/+iio/*.m` | classes and package functions | 7 (stubs), 8, 11 to 21 |
| `matlab/tests/EmuFixture.m`, `IioTestCase.m`, `ObjModelProbe.m` | shared `matlab.unittest` fixture, base test case, helper class of `tObjectModel` | 8a |
| `matlab/tests/t*.m` | gate G5 tests | each task |
| `matlab/examples/*.m`, `matlab/README.md` | examples, documentation | 25, 26 |
| `.github/workflows/ci.yml`, `matlab-probe.yml`, `matlab/probes/*` | CI job `matlab`, probe workflow | 8, 27 |

## Task Overview

| # | Task | Wave | Depends on | Owns |
|---|---|---|---|---|
| 1 | `raw` flag of `struct iio_conv_spec` | 0 | — | `include/iio/iio-conv.h`, `src/plan.c`, `tests/conv_gen.h`, `tests/test_plan.c`, `tests/test_scalar.c` |
| 2a | Fixture and fake backend | 0 | 1 | `tests/resources/iio_fixture.xml`, `tests/fixture_util.h`, `tests/fake_backend.h`, `tests/test_stream.c`, `tests/test_stream_fake.c` (first cases), `tests/CMakeLists.txt` |
| 2b | Stream layer 1: public header, receive | 0 | 2a | `include/iio/iio-conv-stream.h`, `src/stream.c`, `tests/test_stream*.c`, `CMakeLists.txt` |
| 3 | Stream layer 2: transmit | 0 | 2 | `src/stream.c`, `tests/test_stream*.c`, `README.md` |
| 4a | Mock MATLAB runtime | 0 | 3 | `tests/mxmock/*`, `tests/mex/{test_mxmock.c,mx_conformance.c}` |
| 4b | Build skeleton, contracts, stub registry | 0 | 4a | CMake files, `matlab/mex/*` (skeleton), `matlab/tools/gen_enums.py`, `check_mex_exports.sh`, `tests/mex/{CMakeLists.txt,mex_loadcheck.c,test_enums.c}` |
| 5 | Handle table | 0 | 4 | `matlab/mex/handles.c`, `tests/mex/test_handles.c` |
| 6 | Dispatcher, helpers, test harness | 0 | 5 | `matlab/mex/iio_mex.c`, `mx_util.c`, `tests/mex/mex_test.{h,c}`, `tests/mex/test_{dispatch,mx_util,trace,fixture}.c` |
| 7 | Gate scripts, API map check, M skeleton | 0 | 6 | `matlab/tools/{check_api.py,check_mlint.sh,test_tools.py}`, `matlab/+iio/+internal/*.m`, nine stub classes |
| 8a | Vertical slice: commands, classes, fixtures, tests (local gates) | 0 | 7 | slice commands in `cmd_context.c` / `cmd_attr.c`, minimal `Context.m` / `Attr.m`, `matlab/tests/{EmuFixture,IioTestCase,ObjModelProbe,tSlice,tFixture,tObjectModel}.m`, `tests/mex/test_slice.c`, `matlab/probes/*` |
| 8b | Vertical slice through real MATLAB in CI (controller) | 0 | 8a | `.github/workflows/{ci.yml,matlab-probe.yml}`; fallback only: `deps/build-libxml2.sh`, `deps/build-libiio.sh` |
| 9 | Data-path gateway foundations | 0 | 8 | `matlab/mex/data_path.h`, `tests/mex/dp_test.h`, `tests/mex/test_data_path.c` |
| 10 | Test hooks (fake-backend context) | 1b | 9, wave 1a green | `matlab/mex/cmd_test.c`, `tests/mex/test_cmd_test.c`, `matlab/tests/tFakeBackend.m` |
| 11 | Top level, scan, library queries | 1b | 9, wave 1a green | `matlab/mex/cmd_toplevel.c`, `matlab/+iio/{version,backends,has_backend,strerror,scan_contexts}.m`, `tests/mex/test_cmd_toplevel.c`, `matlab/tests/tTopLevel.m` |
| 12 | Context | 1a | 9 | `matlab/mex/cmd_context.c`, `matlab/+iio/Context.m`, `tests/mex/test_cmd_context.c`, `matlab/tests/tContext.m` |
| 13 | Attributes | 1a | 9 | `matlab/mex/cmd_attr.c`, `matlab/+iio/Attr.m`, `tests/mex/test_cmd_attr.c`, `matlab/tests/tAttr.m` |
| 14 | Device | 1a | 9 | `matlab/mex/cmd_device.c`, `matlab/+iio/Device.m`, `tests/mex/test_cmd_device.c`, `matlab/tests/tDevice.m` |
| 15 | Channels and channel masks | 1b | 9, wave 1a green | `matlab/mex/cmd_channel.c`, `matlab/+iio/{Channel,ChannelsMask}.m`, `tests/mex/test_cmd_channel.c`, `matlab/tests/tChannel.m` |
| 16 | Buffers and buffer streams | 1b | 9, wave 1a green | `matlab/mex/cmd_buffer.c`, `matlab/+iio/{Buffer,BufferStream}.m`, `tests/mex/test_cmd_buffer.c`, `matlab/tests/tBuffer.m` |
| 17 | Blocks | 1b | 9, wave 1a green | `matlab/mex/cmd_block.c`, `matlab/+iio/Block.m`, `tests/mex/test_cmd_block.c`, `matlab/tests/tBlock.m` |
| 18 | `iio_stream` helper | 1b | 9, wave 1a green | `matlab/mex/cmd_stream.c`, `matlab/+iio/Stream.m`, `tests/mex/test_cmd_stream.c`, `matlab/tests/tStream.m` |
| 19 | Events | 1b | 9, wave 1a green | `matlab/mex/cmd_event.c`, `matlab/+iio/EventStream.m`, `tests/mex/test_cmd_event.c`, `matlab/tests/tEventStream.m` |
| 20 | Device low level | 1b | 9, wave 1a green | `matlab/mex/cmd_lowlevel.c`, `tests/mex/test_cmd_lowlevel.c`, `matlab/tests/tDeviceLowLevel.m` |
| 21 | Fast path `RxStream` / `TxStream` | 1b | 9, wave 1a green | `matlab/mex/cmd_conv.c`, `matlab/+iio/{RxStream,TxStream}.m`, `tests/mex/test_cmd_conv.c`, `matlab/tests/{tRxStream,tTxStream}.m` |
| 22 | Integration gate | 2a | 10 to 21 | `tests/mex/test_integration.c`, `matlab/tests/{tApiSurface,tLifetime,tStrings}.m`; extends `tests/mex/test_m_sequences.c`; may repair any file |
| 23 | Toolbox-flow end-to-end test | 2b | 22 | `matlab/tests/{MiniRx,MiniTx,tToolboxFlow}.m` |
| 24 | Timing report | 2b | 22 | `matlab/tests/tTiming.m` |
| 25 | Examples | 2b | 22 | `matlab/examples/*.m`, `matlab/tests/tExamples.m` |
| 26 | Documentation | 2b | 22 | `matlab/README.md`, `README.md` |
| 27 | CI finishing touches | 2b | 22 | `matlab/tools/check_junit.py`, `matlab/tools/test_tools.py`, `.github/workflows/ci.yml` |
| 28 | Release check (controller) | 2c | 23 to 27 | `docs/benchmarks.md`, the spec |

## Conventions

### Gateway (C)

- **Call form:** `[out1, ...] = iio_mex('command', arg1, ...)`. Argument and output positions in this plan are 0-based and do not count the command name.
- **Command naming rule** (one rule for every group):
  1. A command that wraps one `iio.h` function has that function's name without the leading `iio_` (`iio_context_get_name` gives `context_get_name`, `iio_create_context` gives `create_context`, `hwmon_channel_get_type` stays `hwmon_channel_get_type`). Arguments are the C arguments in C order without output pointers; outputs are the C return value, then the C output parameters in C order. Deviations are stated in the "Command reference".
  2. **List form**, the only combining form: wherever `iio.h` has a `<x>_get_<things>_count` function and indexed getters, ONE command `<x>_get_<things>` returns every element (`context_get_devices`, `channel_get_attrs`, `buffer_get_scan_elements`, `get_builtin_backends`, `scan_get_results`). No command takes or returns an element index, so there is no index-base conversion anywhere. (An IIO scan index, `channel_get_index`, is data and is never shifted.) A list of objects is written "result first" as in `context_get_devices` (`cmd_context.c`): `gw_ret_ids(c, 0, NULL, n)` creates the zeroed 1-by-n result, then `ids = n ? mxGetData(c->plhs[0]) : NULL` and one `gw_handle_borrow(type, element, gw_handle_root(owner), &ids[i])` per element; no scratch array, and no mx allocation after the first entry was registered.
  3. **Extra commands** have no `iio.h` function behind them and no row in `matlab/api-map.tsv`: `mex_*` (gateway management), `test_*` (test builds only), `conv_stream_*` (the fast path; named after the `iio_conv_stream_*` function) and the data helpers `channels_mask_get`, `buffer_stream_is_started`, `block_size`, `block_get_bytes`, `block_set_bytes`, `block_read`, `block_write`. They are registered with `GW_XCMD`, all others with `GW_CMD`.
- **Type mapping:**

  | C | MATLAB value at the gateway |
  |---|---|
  | object pointer, input | real `uint64` scalar handle id; must be live and of the expected type, else `iio:EBADF`. `0` is accepted only where the reference says "H0" or "or 0" |
  | object pointer, result | `uint64` scalar; a `NULL` result ("not found") is id `0`, never an error |
  | object pointer, result of `iio_scan`, `iio_create_context`, `iio_buffer_open`, `iio_buffer_stream_create_block`, `iio_buffer_create_stream`, `iio_stream_get_next_block`, `iio_device_create_event_stream`, `iio_device_get_trigger` | error-encoded, never `NULL`: test `iio_err(p)` before any use or registration and return it ("`iio_err` check" in the "Command reference") |
  | list of objects, result | 1-by-N `uint64` (1-by-0 when empty) |
  | `const char *`, input | `char` row vector, `''` allowed; `[]` means `NULL` where the reference says "or `[]`". A MATLAB `string` object is rejected (`iio:EINVAL`): M code converts first. Text with an embedded NUL (`char(0)` anywhere in it) is `iio:EINVAL` too, in every command (`gw_arg_str`) |
  | `const char *`, result | `char` row; C `""` is `''` (0x0 char); C `NULL` is `[]` (0x0 double) |
  | list of text, result | 1-by-N cell of `char` rows |
  | `bool` | `logical` scalar |
  | counts, sizes, enum values, result | `double` scalar |
  | `long long` | result: `int64` scalar. Input: real integer-valued scalar; `int64` and `uint64` are read from the integer data, never through `double` |
  | `double` | result: `double` scalar. Input: real numeric scalar of any class (`gw_arg_double`), except `attr_write_double`, which takes `double` or `single` only (an integer class would be rounded on the way) |
  | raw bytes | input: `uint8` vector, the byte count is its `numel`. Result: 1-by-N `uint8` |
  | enum value with a name | two outputs: name (`char`, the C enumerator such as `'IIO_VOLTAGE'`, `'IIO_MOD_I'`, `'HWMON_TEMP'`, `'IIO_EV_TYPE_THRESH'`; `'UNKNOWN'` when the value has none) and value (`double`). Names come from `gw_enum_name()`, generated from `iio.h` |
  | `struct iio_context_params *` | `[]` (C `NULL`) or a scalar struct with any of the fields `log_level`, `stderr_level`, `timestamp_level`, `timeout_ms`, `flags` (missing = 0) |

- **Optional inputs and outputs:** each command declares `min_rhs`, `max_rhs`, `max_lhs` in its registry entry (columns "Args" and "Out" of the "Command reference") and the dispatcher enforces them before the command runs. An optional argument is absent when it is beyond `nrhs` or is `[]` (0x0 double; a complex 0x0 double counts as `[]` too, a 0x0x0 double does not); `''` is a value, not "absent". A text argument is a `char` row or any 2-D empty `char` (`''`, 1x0, 0x3: all read as `''`); a column, a matrix, an N-D `char` (0x0x0 included) is `iio:EINVAL`. No test may assert the opposite of these edge cases. Output 0 is always set by a command with `max_lhs >= 1` (MATLAB allows it when `nlhs == 0`); output k > 0 is set only when `k < nlhs` (the `gw_ret_*` helpers do this).
- **Errors.** The dispatcher is the only caller of `mexErrMsgIdAndTxt`. Identifier `iio:<ERRNO NAME>` of the negative errno the command returned (from libiio, libiio-conv or the gateway); `iio:E<number>` when the errno has no name in `gw_errno_name()`. Text: `"<command>: "` followed by the command's `gw_fail()` message, else by `iio_strerror()`. The gateway's own uses:

  | errno | Raised by the gateway when |
  |---|---|
  | `ENOSYS` | the first argument is not a `char` row naming a registered command (while the gateway was written also: a command that was still a stub; `gw_stub` is gone now that every command exists) |
  | `EINVAL` | argument or output count outside the command's limits; an argument of the wrong class, shape or range; an object of another device; an unknown option field |
  | `EBADF` | a handle id is zero where not allowed, was destroyed, never existed, or has another type |
  | `EPERM` | state misuse (see Global Constraints) |
  | `ENOENT` | a channel id given as text does not exist (`conv_stream_open`) |
  | `EFAULT` | gateway bug guard: a command returned 0 without setting a requested output |
  | `ENOMEM` | `malloc` or handle-table growth failed |

- **Destructor commands** (`context_destroy`, `scan_destroy`, `channels_mask_destroy`, `buffer_close`, `block_destroy`, `stream_destroy`, `event_stream_destroy`, `conv_stream_close`) take any `uint64` scalar: a zero or stale id is a silent no-op; a live id of another type is `iio:EBADF`; a live borrowed entry of the right type (a stream's own mask, buffer stream or block) is left alone.
- **Handle ownership:** owned objects (context, scan, mask, buffer stream, block, stream, event stream, conv stream) carry a destroy op; borrowed objects (device, channel, attr, buffer) are children of their root context, have no op, and the same C pointer always has the same id (`gw_ret_borrowed`). Destroying an entry first destroys all its descendants, newest first. The MEX file is locked while any handle exists.
- **Numeric locale.** `struct gw_c_numeric loc; ret = gw_c_numeric_enter(&loc); if (ret) return gw_fail(c, ret, "no \"C\" numeric locale"); <the libiio call>; gw_c_numeric_leave(&loc);` Between enter and leave there is no `mx*` call (an allocation that fails ends the call non-locally and would leave the thread in the temporary locale), no `return`, and nothing but the libiio call and plain C; everything allocated with `mx*` is allocated before `enter` or after `leave` (pattern: `create_context` and `attr_write_string` in the pilot files). The pair costs nothing when the decimal separator is already a point. The calls that MUST be bracketed (each parses or prints a double with `strtof`, `strtod`, `sscanf` or `"%f"`; found by `grep -n 'strtof\|strtod\|sscanf\|%f\|%lf' deps/src/libiio/*.c`):

  | libiio call | Why | Command (task) |
  |---|---|---|
  | `iio_create_context`, `iio_create_context_from_xml` (inside `fake_create_context`) | the channel `scale` of the XML is read with `strtof` (`xml.c`; `emu.c` has the same line): `xml:`, the fake backend and every backend that receives its description as XML | `create_context` (12, done), `test_create_fake_context` (10) |
  | `iio_scan` | a backend's scan may build a context description the same way; bracketed so that no backend has to be audited | `scan` (11) |
  | `iio_context_get_xml` | channel scales are printed with `"%f"` (`channel.c`) | `context_get_xml` (12, done) |
  | `iio_attr_get_range` | `sscanf("%lf")` (`attr.c`) | `attr_get_range` (13, done) |
  | `iio_attr_write_raw`, `iio_attr_write_string`, `iio_attr_write_bool`, `iio_attr_write_longlong`, `iio_attr_write_double` | emu checks a written value against the `_available` range with `sscanf("%lf")` (`emu.c`) | the five `attr_write_*` commands (13, done) |
  | `iio_device_reg_read`, `iio_device_reg_write` | without backend register operations (or when they answer `-ENOSYS`) `device.c` falls back to `iio_attr_write_string` / `iio_attr_write_longlong` on the debug attribute `direct_reg_access`, both rows above. Not reached with emu (it has register operations); on the fake the fallback runs but parses nothing, so the comma-locale block of `test_cmd_lowlevel.c` can only assert a correct round trip and a restored locale | `device_reg_read`, `device_reg_write` (20, done) |

  Not needed, because libiio switches the locale itself or parses no floating-point text: `iio_attr_read_double`, `iio_attr_read_bool`, `iio_attr_read_longlong`, `iio_attr_read_raw`, `iio_attr_get_available`, every channel, mask, buffer, block, stream, event and low-level function except the two register functions of the table (`iio_channel_convert*` use the scale as a number; `iio_channel_get_data_format` returns what context creation parsed). Audited in wave 1b and found free of the four patterns: `iio_has_backend` (the pinned build has no backend modules), `iio_get_builtin_backends_count`, `iio_get_builtin_backend`, `iio_strerror`, `iio_scan_get_results_count`, `iio_scan_get_uri`, `iio_scan_get_description`, `iio_scan_destroy`, `iio_channel_refresh_format` (the format text is parsed with `%c` and `%u` only), `iio_event_get_channel` (`strtoul` only), the buffer, block, mask and `iio_stream` functions with emu's operations behind them, and everything `src/stream.c` calls. A wave 1b task whose command calls a libiio function that is not in either list checks the function's source for the four patterns above, brackets the call if one is reached, and reports it. Test helpers that call libiio directly (`mt_open_context`, `dp_open_fake`, the `dp_*` builders, `fixture_*`) have no guard: inside a `mt_comma_begin()` block a test creates contexts with gateway commands only, or creates them before the block.
- **Every gateway C file** includes `"mx_util.h"` first and follows the MEX build rules of Global Constraints.

### Data-path objects (who registers what, and how)

| Object | Type | Registered by | With | Destroyed or invalidated when |
|---|---|---|---|---|
| channels mask | `GW_T_MASK` | `create_channels_mask` | `dp_add_mask(m, dev_id)` | `channels_mask_destroy`; its context is destroyed |
| stream's mask (read-only) | `GW_T_MASK` | `buffer_stream_get_channels_mask` | `gw_handle_borrow(GW_T_MASK, m, bs_id, &id)` | its buffer stream is closed |
| buffer stream | `GW_T_BUFFER_STREAM` | `buffer_open` | `dp_add_bufstream(bs, buf_id)`: `pre_destroy` = `iio_buffer_stream_cancel`, then `iio_buffer_stream_stop` if the entry has `BS_STARTED`; `destroy` = `iio_buffer_close` | `buffer_close`; context destroyed. Order: cancel, stop, its blocks, its mask view and its converter cache, close (fake log `X s F.. X C`, as the stream layer and `iio_stream_destroy` do: a legacy IIOD releases the remote buffer only on stop) |
| block | `GW_T_BLOCK` | `buffer_stream_create_block` | `dp_add_block(b, bs_id)` | `block_destroy`; its buffer stream is closed |
| `iio_stream` | `GW_T_STREAM` | `buffer_create_stream` | `gw_handle_add(GW_T_STREAM, st, buf_id, true, &ops, aux)` (`aux`: a `malloc` struct with the buffer pointer, `nb_blocks` and the ids of the block views registered so far, freed by the destroy op: the handle table cannot list an entry's children and `struct iio_stream` is opaque); `ops.destroy` = `iio_stream_cancel` then `iio_stream_destroy` on an input buffer; on an output buffer `iio_stream_destroy` alone (its blocking dequeues send the queued blocks), except when the context timeout is infinite (`iio_context_get_params(ctx)->timeout_ms < 0`): then `iio_stream_cancel` first, so the destroy cannot block for ever | `stream_destroy`; context destroyed. First its views are invalidated |
| stream's buffer stream / blocks | `GW_T_BUFFER_STREAM` / `GW_T_BLOCK` | `stream_get_next_block` | `gw_handle_borrow(.., st_id, &id)`; the buffer-stream view gets `BS_STARTED` when it is registered (libiio started it in that call) | `stream_destroy` |
| event stream | `GW_T_EVENT_STREAM` | `device_create_event_stream` | `gw_handle_add(GW_T_EVENT_STREAM, es, dev_id, true, &ops, NULL)`; `ops.destroy` = `iio_event_stream_destroy` | `event_stream_destroy`; context destroyed |
| fast-path stream | `GW_T_CONV_STREAM` | `conv_stream_open` | `gw_handle_add(GW_T_CONV_STREAM, s, buf_id, true, &ops, aux)` (`aux`: `malloc` struct `{ctx, timeout_ms}`, freed by the op); `ops.destroy` calls `iio_conv_stream_close(&s)`; for a transmit stream opened with `timeout_ms` -1 on a context whose timeout is infinite it first sets the stream to non-blocking and polls `iio_conv_stream_flush` 1 ms apart for at most 1000 ms (the stream layer's close waits under the stream's own policy and has no bound of its own). In every other case the close waits as that policy says: not at all with `timeout_ms` 0, up to `timeout_ms` when it is positive (701 ms measured with 700 on the fake), and with -1 on a context with a finite timeout one blocking dequeue per frame in flight. A long `Timeout` is therefore a long, uninterruptible `delete`; this is kept (cutting it short would drop the last transmit frame of a slow device, Review Focus 11) and stated in the help of `iio.TxStream` | `conv_stream_close`; context destroyed |

`dev_id`, `buf_id` are the borrowed ids of the device and buffer (children of the context). Because children are always created after their parent, "newest first" gives the libiio-safe order blocks, buffer stream, masks, context, and every worker thread (one per open buffer stream) is gone before `iio_context_destroy` and before the MEX can unload.

A borrowed entry of type mask, buffer stream or block is called a **view** in this plan. Ownership rules every command applies: `gw_handle_owned(id) == false` on a mask, buffer stream or block means "belongs to a stream": `channel_enable`, `channel_disable`, `buffer_stream_start`, `buffer_stream_stop`, `buffer_stream_create_block`, `block_enqueue`, `block_dequeue`, `block_disable_cpu_access` raise `iio:EPERM`; the destroy commands do nothing. The flags word of a `GW_T_BUFFER_STREAM` entry (`BS_STARTED`) is written by `cmd_buffer.c`, and once by `cmd_stream.c` for the view of a stream's buffer stream (set when the view is registered, so `buffer_stream_is_started` is true for the buffer stream of a running `iio.Stream`); its cache slot (`gw_handle_set_cache`) belongs to `cmd_block.c` (converter cache). The flags of a `GW_T_BLOCK` entry (`BLK_ENQUEUED`, `BLK_CPU_OFF`) are written by `cmd_block.c` (owned blocks) and `cmd_stream.c` (views: set on every block of the stream except the one returned last; after a failed `stream_get_next_block` set on every block of the stream, so no data access is possible until a `next` succeeds).

### MATLAB (M)

- Every class is `classdef X < iio.internal.Object` (a `handle`). Hidden properties: `Handle` (`uint64` gateway id, 0 = none) and `Parent` (the MATLAB object it came from, which keeps the parent alive). Every class accepts the internal constructor form `X(parent, handle)` with `handle` of class `uint64`, and `X()` (for object arrays); public constructor forms are listed in "MATLAB surface". Exceptions: `iio.RxStream` and `iio.TxStream` have only their public form (the literal `arguments` block requires `dev` and `channels`), so `iio.RxStream()` is an error and a test that constructs every class without arguments leaves these two out (`X.empty(1, 0)` works for them as for every class). Where the public and the internal form share a position (`iio.BufferStream(buf, arg)`, `iio.Block(parent, arg)`, `iio.Stream(buf, mask, ..)`), the class of that argument selects the form: a `uint64` is always a handle to wrap; `iio.ChannelsMask(dev)` and `iio.EventStream(dev)` select by `nargin == 1`.
- `a == b` and `a ~= b` on iio objects compare `Handle` (same libiio object), element-wise, as Python's `__eq__` does.
- Public members are `snake_case` and are named after the libiio C functions without their prefix; name-value options are `PascalCase`.
- Text arguments are declared `(1,1) string` in an `arguments` block (so `'abc'` and `"abc"` both work) and passed to the gateway as `char(x)`. Text results are `char`; lists of text are 1-by-N cell arrays of `char`; a missing name or label is `''` (`char(iio_mex(...))` turns the gateway's `[]` into `''`).
- Collections are 1-by-N object arrays in libiio order (`ctx.devices`, `dev.channels`, `dev.attrs`); a `find_*` method returns a scalar object or a 1-by-0 empty array of that class (`iio.Attr.empty(1, 0)`).
- A wrapper object stores only facts that cannot change while its context lives (ids and names, as `SetAccess = immutable` properties read once in the constructor); everything else is a `Dependent` property or a method and costs gateway calls per use.
- A constructor calls the gateway only when its `handle` argument is non-zero (`if handle ~= 0, obj.name = iio_mex('attr_get_name', handle); end`, as `Attr.m` and `Device.m` do), so `X()`, `X.empty(1, 0)` and the elements MATLAB creates when an object array grows stay free of gateway calls, and immutable properties of such an object keep their class default (`''`).
- A test never constructs a real iio class with a non-zero handle that the gateway did not return (`iio.Channel([], uint64(5))` raises `iio:EBADF` in the constructor as soon as the class reads its name there: this broke `tObjectModel.empties` after Task 13). Tests of the object model that need made-up handles use `ObjModelProbe("a", Handle = uint64(5))`, which calls no gateway command; real classes appear with made-up state only as `X()` and `X.empty`.
- **No argument is declared with an iio class.** `mask (1,1) iio.ChannelsMask` in an `arguments` block is not a type check: MATLAB converts a value of another class by calling the class constructor with it, and because `iio.ChannelsMask(dev)` and `iio.EventStream(dev)` are public forms that create a libiio object, `ch.enable(dev)`, `ch.is_enabled(dev)` and `dev.sample_size(dev)` worked without error on a new, empty mask that was deleted when the call returned (found by the wave 1b review; every other class accepts `X(parent)` and gave an object without a handle, so the mistake surfaced late as `iio:EBADF`). Every argument that must be an iio object, `obj` included, is therefore declared with the validator that does not convert (base MATLAB since R2020b), and keeps its size: `mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}`, `obj (1,:) {mustBeA(obj, "iio.Channel")}`, `opts.Channels (1,:) {mustBeA(opts.Channels, "iio.Channel")} = iio.Channel.empty(1, 0)`. Where "MATLAB surface" writes `x (1,1) iio.X` for an argument it means this form. A wrong class or size is then an error of the `arguments` block (an identifier starting with `MATLAB:`, exact name **[CI]**) raised before any gateway call; the tests assert it with `tc.verifyValidationError(@() ...)` of `IioTestCase` (`tChannel.maskArgumentIsNotConverted`, `tBuffer.maskArgumentIsNotConverted`, `tBlock.argumentsAreNotConverted`, `tEventStream.decodeArguments`, `openErrors` / `unknownChannel` of the two stream tests). Constructors that select their form by the class of an argument (`iio.Stream`, `iio.BufferStream`, `iio.Block`, `iio.ChannelsMask`, `iio.EventStream`) check with `isa` themselves and raise `iio:EINVAL`. MATLAB uses the same one-argument conversion when classes are mixed in one array, so `[mask dev]` or `masks(2) = dev` creates a second mask and `[es dev]` opens an event stream. Decided in Task 22: no guard (the conversion is indistinguishable from the public call `iio.ChannelsMask(dev)`, which follows libiio's Python binding and stays); the class help of `iio.ChannelsMask` and `iio.EventStream` says to keep masks, event streams and devices in separate arrays, the README repeats it (Task 26), and no test builds such an array.
- `string(missing)` passes a `(1,1) string` validator and fails in `char(x)` with a MATLAB conversion error (identifier starting with `MATLAB:`, exact name **[CI]**). That is accepted as it is: no class adds a validator for it, and `tStrings` (Task 22) asserts only "an identifier starting with `MATLAB:` or `iio:`". A text argument that contains `char(0)` reaches the gateway and raises `iio:EINVAL` there; binary data goes through `write_raw`.
- `delete`, `destroy` and `close` call the destroy command unconditionally when `Handle ~= 0` (it is a no-op for stale ids and for borrowed objects) and never rewrite `Handle`, so M code keeps no ownership or "closed" flag: a later call on the object raises `iio:EBADF` by itself.
- A `Dependent` property documented as "the Parent, after `<command>` confirmed its handle" calls the command and returns `obj.Parent` when the id equals `obj.Parent.Handle`; otherwise it raises `error('iio:EBADF', ...)`.
- `iio_mex` is called only with a single-quoted literal command as first argument (the API check relies on it), from files under `matlab/+iio`; `test_*` and `mex_*` commands may also be called from `matlab/tests`.
- Errors surface as `MException` with the gateway identifiers. M code itself raises only through `arguments` validation or with `error('iio:EINVAL', ...)`.

### Test data

RX: `<dir>/emu-rx_buf0.bin` is 8192 little-endian `uint16` words W(0) ... W(8191), 16384 bytes. For k = 0 ... 4095: I(k) = k - 2048, Q(k) = 2047 - k, W(2k) = (I & 0x0fff) | ((k & 0xf) << 12), W(2k+1) = (Q & 0x0fff) | (((k >> 4) & 0xf) << 12). The upper four bits are junk on purpose, so converted and raw results differ. C: `fixture_ramp_*` of `tests/fixture_util.h`; MATLAB: `EmuFixture.rampWords(4096)`. Both fixtures (`fixture_setup`, `EmuFixture.setup`) write this file. First 32 bytes: `00 08 ff 07 01 18 fe 07 02 28 fd 07 03 38 fc 07 04 48 fb 07 05 58 fa 07 06 68 f9 07 07 78 f8 07`. Anchors: W(0...3) = `0800 07ff 1801 07fe`; W(32), W(33) = `0810 17ef`; W(8190), W(8191) = `f7ff f800`; sum of all words 268431360.

Word-stream rule: emu delivers the file bytes in order, whatever the mask, continuing across block boundaries and wrapping to the start at the end of the file; every start rewinds. All `emu-rx` channels are 16 bit, so with C enabled channels, sample s (counted from the start of the stream), enabled channel position p (index order) holds **W(mod(s·C + p, 8192))**. The value a channel reports is a function of that word only: `le:s12/16>>0` channels (`voltage0_i` ... `voltage1_q`): low 12 bits sign-extended (`EmuFixture.s12`); `voltage2` (`le:s14/16>>2`): bits 15...2 sign-extended (`EmuFixture.s14shift2`); raw mode: the word as `int16`. Full scale divides by 2048 (8192 for `voltage2`); IIO scale on `emu:` is (v - 100)·0.25 for `voltage2` and v for the others.

**A block must not be longer than the file** (16384 bytes: `SamplesPerFrame` 4096 with two channels, 2048 with four, 8192 with `voltage2` alone). The wrap is not a loop: `emu_dequeue_block` of the pinned libiio (`deps/src/libiio/emu.c`) reads once from its place in the file and once more from the start, then returns success whatever that gave. A block longer than the rest of the file plus one file length keeps what its memory held behind that point, and the fast path reports the frame as valid: with the default `SamplesPerFrame = 32768` of `iio.RxStream` on `voltage0_i`, `voltage0_q`, 24570 of the 32768 rows of the first frame are not ramp data (first at row 8192; measured **[RUN]** by the wave 1b review). This is the backend's behaviour and is not patched here. Rules that follow: no C or M test reads a block longer than the file (the longest in the suite is 4096 bytes: 1024 samples of two channels, 512 of four); every `iio.RxStream` in a test, an example run on the fixture or the toolbox-flow test names its `SamplesPerFrame` (the default is for hardware); a test that needs longer frames first writes a longer file with `EmuFixture.writeWords` (the timing test of Task 24 does: one block is exactly one file). The same is stated in the comments of `tests/fixture_util.h` and `matlab/tests/EmuFixture.m`.

- C = 2 (`voltage0_i`, `voltage0_q`), frame f of N rows, row i: k = mod(f·N + i, 4096), value I(k) + jQ(k). In M: `k = (0:N-1).' + f*N; I = int16(mod(k,4096) - 2048); Q = int16(2047 - mod(k,4096));`.
- C = 4 (both pairs): sample s: column 1 = I(2s) + jQ(2s), column 2 = I(2s+1) + jQ(2s+1), indices mod 4096. First row at full scale: -1 + 0.99951171875i, -0.99951171875 + 0.9990234375i.
- C = 1 (`voltage2`), single, IIO scale: 103, 102.75, 359, 102.75, 615, 102.75, ...

TX: `emu-tx_buf0.bin` holds the block dequeued last (`int16` little-endian, I then Q per column); nothing is written at enqueue; every start truncates it. A test calls `flush()` (or closes the stream) before reading it.

Fixture object model (`tests/resources/iio_fixture.xml`, Appendix A): 8 devices in this order: `hwmon0`; `iio:device0` "emu-rx" (inputs `voltage0_i`, `voltage0_q`, `voltage1_i`, `voltage1_q` `le:s12/16>>0`, `voltage2` `le:s14/16>>2` scale 0.25 offset -100; buffer 0); `iio:device1` "emu-tx" (outputs `voltage0_i/q`, `voltage1_i/q` `le:s16/16>>0`, non-scan `altvoltage0`; buffer 0); `iio:device2` "emu-phy" label "phy" (no buffer; 14 attributes, 4 debug attributes, 1 event attribute; channels in order `voltage0` in, `voltage0` out, `altvoltage0`, `altvoltage1`, `temp0`, `accel_x`); `iio:device3` "emu-multi" (buffer 0 in, buffer 1 out `le:s12/16>>4`); `iio:device4` (no name; format zoo); `iio_sysfs_trigger`; `trigger0`. Context attributes in libiio order: `escaped`, `fixture`, `hw_model`, `num`.

What emu cannot test, and what covers it:

| Feature | On emu | Covered by |
|---|---|---|
| event stream create / read / destroy | `-ENOSYS` | fake backend: two events, then "no event" (Tasks 10, 19) |
| `iio_event_get_channel` with nothing matching (libiio returns the last channel) | yes | emu with synthetic ids; the gateway re-checks (Task 19) |
| trigger get / set success | `-ENOSYS`; `-EINVAL` for a non-trigger | fake (Tasks 10, 20); error codes on emu |
| dequeue timeout, non-blocking "not ready", `valid = false` | never (or the 5 s stall) | fake `rx_frames`, `tx_frames`, `busy` (Tasks 2, 3, 21) |
| `cyclic` flag, `bytes_used`, last TX frame, close order | ignored / not observable | fake log, `tx_data`, `tx_cyclic` (Tasks 3, 9, 21) |
| effective mask wider than requested | never | fake `couple` (Tasks 2, 3, 10) |
| binary attribute write by byte count | stored as a C string | fake `attr_data` (Task 10) |
| `iio_context_set_timeout` reaching the backend | value stored only | fake `timeout` (Task 10) |
| scan with results | always 0 results | empty result only; MATLAB tests never scan `local`, `ip` or the default list |

Not built by `deps/build-libiio.sh` (`WITH_USB_BACKEND=OFF`, `WITH_SERIAL_BACKEND=OFF`, `HAVE_DNS_SD=OFF`), and stated so in `matlab/README.md`: the `usb:` and `serial:` backends and the scan of `ip` (the built-in backends are `local`, `ip`, `emu`, `xml`; only `local` can scan). Explicitly untested in this plan, and stated so in `matlab/README.md` (Task 26): the `local:` and `ip:` backends and real hardware; DMABUF success paths and a successful `disable_cpu_access`; a block queued after `cancel` on a backend that uses libiio's enqueue worker; cross-thread `iio_buffer_stream_cancel`; a dequeue that timed out on libiio's generic block path; Ctrl-C during a blocking call; a race at process exit between libiio's exit handler (`xmlCleanupParser()` on MATLAB's libxml2) and other MATLAB threads, beyond the ten exit-check steps of Task 8b (fallback F1 removes it); a v0 `libiio.so.0` already loaded in the MATLAB process by another toolbox; `parfor` workers; a block longer than the emu data file (emu fills it only in part and still reports success: "Test data"); scan results of real backends.

### How tests are written

Gateway test (gate G1), names of "Shared contracts":

```c
#include "mex_test.h"
MT_GROUPS(&gw_group_context, &gw_group_attr);        /* the cmd_<group>.c objects this test links */

int main(int argc, char **argv)
{
	struct mt_fixture fx;
	mxArray *out[2];
	if (argc < 2 || mt_fixture_setup(&fx, argv[1])) return 2;       /* argv[1] = tests/resources/iio_fixture.xml */
	uint64_t ctx = mt_u64_of("create_context", 2, mt_empty(), mt_str(fx.uri));
	CHECK(ctx != 0);
	CHECK(mt_raises("iio:EINVAL", "context_find_attr", 1, mt_u64(ctx)));   /* missing argument */
	CHECK(mt_call(2, out, "some_command", 1, mt_u64(ctx)) == 0);       /* outputs belong to the test */
	mxDestroyArray(out[0]); mxDestroyArray(out[1]);
	mt_ok("context_destroy", 1, mt_u64(ctx));
	mt_fixture_teardown(&fx);
	MT_MAIN_RETURN();     /* MATLAB exit, then: no handle, no live mx memory, nothing auto-freed */
}
```

Arguments the `mt_*` makers do not cover (numeric matrices, cells, structs) are built with the mock's own `mxCreate*` functions. Rules: every test ends with `MT_MAIN_RETURN()`; identifiers are compared with `mt_raises`, messages only with `strstr` on `mock_err_msg()`; a test of group X obtains its prerequisite objects with `mt_open_context` and the `dp_*` builders, never with a sibling group's command; a test never dequeues on a buffer it did not start; every `test_cmd_<group>.c` starts with `mt_check_arities(&gw_group_<group>, arities, n)`, where `arities` is a `static const struct mt_arity[]` copied by hand from the group's "Command reference" table (name, min, max, out: `2-3` in column "Args" is `2, 3`), in table order (a wrong `min_rhs`, `max_rhs` or `max_lhs` in a `GW_CMD` line is caught by nothing else: `check_api.py` compares names only); every `test_cmd_<group>.c` runs one `mt_oom_sweep` (valid arguments) per command of its group whose "Command reference" row says "result first" or "parked", and expects a result ≥ 1 and no leak report at exit (LeakSanitizer sees an object that a failed attempt created and did not register). **Numeric locale in a gateway test:** `if (mt_comma_begin()) { <commands and CHECKs> mt_comma_end(); }` runs the block with a decimal comma as the process's `LC_NUMERIC` (`mex_test.h`). `mt_comma_end()` fails the test when a command left the thread in a locale of its own or changed the process's decimal separator, so no test needs `uselocale` or `localeconv` itself, and no test calls `setlocale`. There is no "skip": without an installed decimal-comma locale the test FAILS (`sudo locale-gen de_DE.UTF-8`; CI does it; the development machine has `en_DK.utf8`), unless `IIO_TEST_NO_COMMA_LOCALE=1` is set. Inside the block, contexts are created with gateway commands only (`mt_open_context` and the `dp_*` builders are not guarded). Every `test_cmd_<group>.c` whose group has a command in the **Numeric locale** table runs that command inside such a block and asserts a value that is wrong without the guard (a channel scale of 0.25, `scale="0.250000"` in the XML, a fractional range). **Text with an embedded NUL:** `a = mt_str("abXcd"); ((mxChar *)mxGetData(a))[2] = 0;` then `mt_raises("iio:EINVAL", cmd, ..., a)`; every `test_cmd_<group>.c` whose group has a command with a text argument has one such check per command (`find_*` commands, `create_context`, `attr_write_string`, `test_fake_set`). A text element of a cell (the channel list of `conv_stream_open`) is checked the same way by the command that converts it: an embedded NUL is `iio:EINVAL` (the option parser `dp_opt_str` of `data_path.h` already does this for text options). Shorthand used in the tasks: "`cmd(args)` → value" is an `mt_call` that must succeed with that result; "`cmd(args)` raises ID" (or "`cmd(args)`: `ID`") is one for which `mt_raises(ID, ...)` must be true.

MATLAB test (gate G5): one file per subject, `matlab/tests/t<Subject>.m`, `classdef t<Subject> < IioTestCase`. `tc.Fx` is a fresh `EmuFixture` per test method (own temporary directory with the fixture XML and the ramp file); a test creates its contexts from `tc.Fx.Uri` (`emu:`), `tc.Fx.XmlUri` (`xml:`) or `tc.Fx.fakeContext()` (fake backend). Tests reach the gateway through `+iio` members; direct `iio_mex(...)` calls are allowed only for `mex_*` and `test_*` commands. A test asserts class, size, complexity and exact values; error tests use `tc.verifyError(@() ..., 'iio:EINVAL')` or `tc.verifyIioError(@() ..., "EINVAL")`. `verifyError` compares the whole identifier; where a task says "throws an identifier starting with `MATLAB:`" (an `arguments` validation error) the test catches the error itself: `id = ''; try, <call>; catch err, id = err.identifier; end; tc.verifyTrue(startsWith(id, 'MATLAB:'))`, or calls `tc.verifyValidationError(@() <call>)` of `IioTestCase`, which is that code. A test keeps iio objects in local variables only (never in a property of the test class), so they are gone when the teardown counts handles. The teardown of `IioTestCase` fails a test that leaves a handle behind.

## Gates

Every task is verified without MATLAB by G1 to G4; G5 is the authority for M code and is run by the controller.

```sh
B=build-mex    # gateway tasks of wave 0 and wave 2; a wave 1 task uses build-mex-<group>; Tasks 1 to 3 use the directory named in the task
cmake -S . -B $B -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR=/usr/local/MATLAB/R2025b
cmake --build $B                                      # wave 1: cmake --build $B --target test_cmd_<group> iio-mex-real-<group>
```

- **G1 mock tests** (ASan + UBSan + LeakSanitizer; `mexFunction()` called directly on the mock runtime; they need one installed locale with a decimal comma, see "How tests are written"; `locale -a` on the development machine lists `en_DK.utf8`): `ctest --test-dir $B -L mex -R '^(test_|mx_conformance_|mex_bundled_libs)' --output-on-failure`; wave 1: `ctest --test-dir $B -R '^test_cmd_<group>$' --output-on-failure`. "Run G1 for `test_x`" in a task means the build line above followed by `ctest --test-dir $B -R '^test_x$' --output-on-failure`.
- **G2 real MEX build:** `ctest --test-dir $B -R '^mex_(exports|loadcheck)$' --output-on-failure` (exports, `_800` imports, `dlopen` against real `libmx`/`libmex`); wave 1: the target `iio-mex-real-<group>` builds (gateway sources are compiled with `-Werror`, so a warning fails the build).
- **G3 lint:** `MATLAB_ROOT=/usr/local/MATLAB/R2025b matlab/tools/check_mlint.sh <your .m files>` and the same with `MATLAB_ROOT=/usr/local/MATLAB/R2023b`. Without file arguments it checks every `.m` file. Expected: `check_mlint: <n> file(s), no errors`.
- **G4 static API check:** `python3 matlab/tools/check_api.py --iio-h deps/install/include/iio/iio.h` (wave 0), with `--group <group>` (wave 1), with `--strict` (from Task 22 on). Expected last line: `check_api: <rows> rows, <commands> commands, 0 error(s)`; in wave 1 additionally no `check_api: pending:` line is about a command your task registers or a member your task writes (a line about another task's stub names your file when your M code calls that stub, e.g. `channel_read: is still a stub` lists `Channel.m`; such a line, and one about a member in another task's file, is that task's).
- **G1 to G4 in one go** (serial tasks, CI): `ctest --test-dir $B -L mex --output-on-failure`.
- **G5 CI, authoritative, controller only:** `git push origin feat/matlab-bindings`, then as ONE background command (never poll in the foreground) `sha=$(git rev-parse HEAD); until id=$(gh run list --workflow CI --commit "$sha" --json databaseId --jq '.[0].databaseId') && [ -n "$id" ]; do sleep 5; done; gh run watch "$id" --exit-status; gh run view "$id" --json headSha,conclusion`. The run is selected by commit, not by "latest on the branch": right after a push the latest run is still the previous one, and a run cancelled by the workflow's `concurrency` setting is not a result. The verdict counts only if `headSha` equals `$sha`. The evidence is the two jobs `MATLAB bindings (<release>)` and the file `matlab.xml` at the top of each artifact `matlab-<release>`. When G5 is red although G1 to G4 pass: Appendix C.
- Finish every task with `rm -rf $B`.

## Shared contracts

The files below are written exactly as shown by the task named in each title; later tasks rely on every declaration and comment.

### `matlab/mex/handles.h` (Task 4b writes it, Task 5 implements it)

```c
/* SPDX-License-Identifier: MIT */
/* Handle table of the iio_mex gateway. No MATLAB and no libiio dependency. */
#ifndef IIO_MEX_HANDLES_H
#define IIO_MEX_HANDLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0 = none. Low 32 bits: slot + 1. High 32 bits: generation of that slot. */
typedef uint64_t gw_id;

enum gw_type {
	GW_T_NONE = 0,
	GW_T_CONTEXT,       /* struct iio_context *, owned */
	GW_T_SCAN,          /* struct iio_scan *, owned */
	GW_T_DEVICE,        /* struct iio_device *, borrowed */
	GW_T_CHANNEL,       /* struct iio_channel *, borrowed */
	GW_T_ATTR,          /* const struct iio_attr *, borrowed */
	GW_T_BUFFER,        /* struct iio_buffer *, borrowed */
	GW_T_MASK,          /* struct iio_channels_mask *, owned (borrowed: a stream's own mask) */
	GW_T_BUFFER_STREAM, /* struct iio_buffer_stream *, owned */
	GW_T_BLOCK,         /* struct iio_block *, owned (borrowed: a block of an iio_stream) */
	GW_T_STREAM,        /* struct iio_stream *, owned */
	GW_T_EVENT_STREAM,  /* struct iio_event_stream *, owned */
	GW_T_CONV_STREAM,   /* the libiio-conv stream object, owned */
	GW_T_COUNT
};

struct gw_ops {
	/* Called first, before any child is destroyed (for example: cancel). May be NULL. The entry
	 * is still live while it runs: gw_handle_find(), gw_handle_ptr() and gw_handle_flags() work on it. */
	void (*pre_destroy)(void *ptr, void *aux);
	/* Called last, after every child is gone. Releases ptr (owned entries) and aux. May be NULL. */
	void (*destroy)(void *ptr, void *aux);
};

/* Forgets everything WITHOUT calling any op and sets the generation every slot starts
 * from (0 is replaced by 1). The gateway calls it once per load with a clock-derived seed,
 * so ids of an earlier load of the MEX file are stale; tests call it with a fixed seed. */
void gw_handle_reset(uint32_t seed);

/* New entry; parent 0 makes a root. aux is gateway-side state of the entry (may be NULL).
 * Returns 0 if ptr is NULL, type is invalid, parent is stale, or memory ran out. */
gw_id gw_handle_add(enum gw_type type, void *ptr, gw_id parent, bool owned,
		const struct gw_ops *ops, void *aux);

/* *id = the live entry (type, ptr) if there is one, whatever parent is given (another one,
 * zero or a stale id), else a new borrowed child of parent (no ops, no aux). ptr == NULL gives
 * *id = 0 and returns 0. Returns 0, -EBADF (a new entry is needed and parent is zero or
 * stale) or -ENOMEM. */
int gw_handle_borrow(enum gw_type type, const void *ptr, gw_id parent, gw_id *id);

/* pre_destroy(entry); destroy every child, newest first, recursively; release the cache slot;
 * destroy(entry); free the slot (its generation increases). Zero, stale and
 * already-being-destroyed ids are ignored. */
void gw_handle_destroy(gw_id id);
void gw_handle_destroy_all(void);                     /* every root, newest first */

void *gw_handle_ptr(gw_id id, enum gw_type type);     /* NULL if zero, stale, being destroyed, or another type */
enum gw_type gw_handle_type(gw_id id);                /* GW_T_NONE if zero or stale */
gw_id gw_handle_find(enum gw_type type, const void *ptr);  /* 0 if none */
gw_id gw_handle_parent(gw_id id);                     /* 0 for a root or a stale id */
gw_id gw_handle_root(gw_id id);                       /* topmost ancestor (id itself for a root); 0 if stale */
bool gw_handle_owned(gw_id id);                       /* false if stale */
void *gw_handle_aux(gw_id id);                        /* NULL if stale */
uint32_t gw_handle_flags(gw_id id);                   /* 0 for a new entry and for a stale id */
void gw_handle_set_flags(gw_id id, uint32_t flags);   /* ignored for a stale id */
/* Cache slot: a second attachment, for state that another source file keeps on an entry
 * (cmd_block.c: the converter cache of a buffer stream). Allowed on borrowed entries too.
 * Setting it first releases the previous cache with its cache_free; cache_free may be NULL.
 * The slot is already empty while cache_free runs. cache_free may attach a cache to the entry
 * (released in turn before a cache being set is stored, and at the latest when the entry is
 * destroyed) and may destroy the entry (a cache being set is then not stored: stale id). */
void *gw_handle_cache(gw_id id);                      /* NULL if none or stale */
void gw_handle_set_cache(gw_id id, void *cache, void (*cache_free)(void *cache));   /* ignored for a stale id */
size_t gw_handle_count(void);                         /* live entries */
const char *gw_type_name(enum gw_type type);          /* "context", "scan", "device", "channel", "attr",
                                                         "buffer", "channels mask", "buffer stream", "block",
                                                         "stream", "event stream", "conv stream"; else "none" */

#endif
```

### `matlab/mex/iio_mex.h` (Task 4b writes it, Task 6 implements it)

```c
/* SPDX-License-Identifier: MIT */
/* Shared declarations of the iio_mex gateway: command signature, registry, error ids. */
#ifndef IIO_MEX_H
#define IIO_MEX_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mex.h"
#include <iio/iio.h>

#include "handles.h"

#if !MX_HAS_INTERLEAVED_COMPLEX
#error "build with the R2018a (interleaved complex) MEX API"
#endif

/* One mexFunction invocation as a command sees it. */
struct gw_call {
	const char *cmd;        /* command name */
	int nlhs;               /* outputs requested; plhs[k] may be set for k < max(nlhs, 1) */
	mxArray **plhs;         /* all NULL on entry */
	int nrhs;               /* arguments after the command name */
	const mxArray **prhs;   /* prhs[0] is the first argument after the command name */
	char err_msg[256];      /* "": the text is iio_strerror() of the return value */
};

/* A command returns 0 or a negative errno. It never raises: it frees what it allocated and
 * returns, with every libiio object it created either registered in the handle table or destroyed. */
typedef int (*gw_fn)(struct gw_call *c);

struct gw_cmd {
	const char *name;
	gw_fn fn;
	int min_rhs, max_rhs;   /* allowed number of arguments after the command name */
	int max_lhs;            /* allowed number of outputs */
};

struct gw_group {
	const char *name;
	const struct gw_cmd *cmds;
	size_t nb_cmds;
};

/* One registration per source line: matlab/tools/check_api.py reads the names from the C files.
 * GW_CMD: the command of a row of matlab/api-map.tsv. GW_XCMD: an extra command (no row). */
#define GW_CMD(name, fn, min_rhs, max_rhs, max_lhs) { name, fn, min_rhs, max_rhs, max_lhs }
#define GW_XCMD(name, fn, min_rhs, max_rhs, max_lhs) { name, fn, min_rhs, max_rhs, max_lhs }
#define GW_GROUP(name, cmds) { name, cmds, sizeof(cmds) / sizeof((cmds)[0]) }

/* NULL-terminated. registry.c lists every group for the MEX file; a test lists what it links. */
extern const struct gw_group *const gw_groups[];

extern const struct gw_group gw_group_mex,   /* iio_mex.c */
	gw_group_toplevel, gw_group_context, gw_group_attr, gw_group_device, gw_group_channel,
	gw_group_buffer, gw_group_block, gw_group_stream, gw_group_event, gw_group_lowlevel,
	gw_group_conv, gw_group_test;            /* cmd_<name>.c; gw_group_test only with IIO_MEX_TEST_HOOKS */

/* Table entry of a command whose task has not landed yet: gw_fail(c, -ENOSYS, "not implemented"). */
int gw_stub(struct gw_call *c);

/* Records the message in c and returns err (negative). The dispatcher raises "iio:<ERRNO NAME>". */
int gw_fail(struct gw_call *c, int err, const char *fmt, ...) __attribute__((format(__printf__, 3, 4)));

#endif
```

### `matlab/mex/mx_util.h` (Task 4b writes it, Task 6 implements it; the enum tables are generated in Task 4b; `gw_set_double`, `gw_c_numeric_*` and the embedded-NUL rule of `gw_arg_str` were added after the wave 1a review)

```c
/* SPDX-License-Identifier: MIT */
/* Argument and return helpers of the iio_mex gateway. */
#ifndef IIO_MEX_MX_UTIL_H
#define IIO_MEX_MX_UTIL_H

#include "iio_mex.h"

#include <locale.h>

/* "EINVAL" for -22 or 22; NULL for 0 and for numbers without a name. 95 is "ENOTSUP", 11 "EAGAIN". */
const char *gw_errno_name(int err);

/* ---- arguments: k is 0-based, counted after the command name. Each returns 0, or the
 * negative errno it passed to gw_fail(): -EINVAL for a wrong class, shape or range, -EBADF for a handle. ---- */
bool gw_arg_given(const struct gw_call *c, int k);   /* k < nrhs and not [] (0x0 double) */
int gw_arg_id(struct gw_call *c, int k, gw_id *id);  /* real uint64 scalar; no liveness check */
/* Live handle of that type. ptr (may be NULL) is the address of a pointer variable; id may be NULL. */
int gw_arg_handle(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id);
/* The same, but id 0 is accepted and gives a NULL pointer. */
int gw_arg_handle0(struct gw_call *c, int k, enum gw_type type, void *ptr, gw_id *id);
/* Destructor argument: zero or stale id: nothing. Live owned id of that type: gw_handle_destroy().
 * Live borrowed id of that type: nothing. Live id of another type: -EBADF. */
int gw_arg_destroy(struct gw_call *c, int k, enum gw_type type);
/* char row vector or empty char -> UTF-8 copy from mxMalloc; the caller mxFree()s it.
 * Text with an embedded NUL is -EINVAL (C would cut it there without notice). */
int gw_arg_str(struct gw_call *c, int k, char **s);
int gw_arg_str0(struct gw_call *c, int k, char **s); /* absent or [] -> *s = NULL */
int gw_arg_bool(struct gw_call *c, int k, bool *v);  /* logical scalar */
/* Real, non-sparse, integer-valued numeric scalar in [lo, hi]. int64/uint64 are read exactly. */
int gw_arg_int(struct gw_call *c, int k, long long lo, long long hi, long long *v);
int gw_arg_index(struct gw_call *c, int k, unsigned int *v);   /* gw_arg_int in [0, UINT_MAX] */
int gw_arg_double(struct gw_call *c, int k, double *v);        /* real numeric scalar */
/* Real uint8 vector (or empty uint8): pointer INTO prhs (read only), and the element count.
 * *p is never NULL: for an empty array (mxGetData is NULL) it points at a static byte, *n = 0. */
int gw_arg_bytes(struct gw_call *c, int k, const uint8_t **p, size_t *n);
/* [] -> *params = NULL. Scalar struct with fields out of log_level, stderr_level,
 * timestamp_level, timeout_ms, flags (each a real integer-valued scalar in int range)
 * -> *storage zeroed, filled, *params = storage. An unknown field is -EINVAL. */
int gw_arg_params(struct gw_call *c, int k, struct iio_context_params *storage,
		const struct iio_context_params **params);

/* ---- results: set plhs[k]; do nothing when k > 0 and k >= c->nlhs. Return 0, or -errno. ---- */
int gw_ret_empty(struct gw_call *c, int k);                    /* [] */
int gw_ret_id(struct gw_call *c, int k, gw_id id);             /* uint64 scalar */
/* Overwrites the uint64 scalar that gw_ret_id() put in plhs[k]; never fails; nothing when k > 0 and
 * k >= nlhs. "Result first": gw_ret_id(c, 0, 0), create the object, register it, gw_set_id(c, 0, id). */
void gw_set_id(struct gw_call *c, int k, gw_id id);
int gw_ret_ids(struct gw_call *c, int k, const gw_id *ids, size_t n);     /* 1-by-n uint64 */
/* Id of ptr as a borrowed child of the ROOT of owner (its context); id 0 for a NULL ptr. */
int gw_ret_borrowed(struct gw_call *c, int k, enum gw_type type, const void *ptr, gw_id owner);
int gw_ret_str(struct gw_call *c, int k, const char *s);       /* NULL -> []; else char row */
int gw_ret_strs(struct gw_call *c, int k, char *const *list, size_t n);   /* 1-by-n cell of char rows */
int gw_ret_bool(struct gw_call *c, int k, bool v);
int gw_ret_double(struct gw_call *c, int k, double v);
/* Overwrites the double scalar that gw_ret_double() put in plhs[k]; never fails; nothing when k > 0
 * and k >= nlhs. "Result first" for a command that writes to the device and returns a count:
 * gw_ret_double(c, 0, 0), the write, gw_set_double(c, 0, count). */
void gw_set_double(struct gw_call *c, int k, double v);
int gw_ret_doubles(struct gw_call *c, int k, const double *v, size_t n);  /* 1-by-n double */
int gw_ret_int64(struct gw_call *c, int k, int64_t v);         /* int64 scalar */
int gw_ret_uint64(struct gw_call *c, int k, uint64_t v);       /* uint64 scalar */
int gw_ret_bytes(struct gw_call *c, int k, const void *p, size_t n);      /* 1-by-n uint8 */

/* ---- memory libiio returned from malloc (iio_context_get_xml, iio_attr_get_available) ----
 * Parks p: the dispatcher calls free_fn(p, n) when the call ends, however it ends: right after the
 * command returned or, when an mx allocation ended the call non-locally, at the start of the next
 * call or in the at-exit function. The command never frees p itself. Up to 4 pointers per call.
 * Returns 0, or -ENOMEM after calling free_fn(p, n) at once when the four slots are taken. */
int gw_park(void *p, size_t n, void (*free_fn)(void *p, size_t n));

/* ---- numeric locale ----
 * libiio parses and prints some doubles with the plain C functions (strtof, sscanf "%lf",
 * "%f"), which follow LC_NUMERIC: under a locale with a decimal comma every channel scale of a
 * context made from XML is cut at the point, the XML text is written with commas and a
 * fractional range is not parsed. A command brackets such a libiio call with this pair:
 *     struct gw_c_numeric loc;
 *     ret = gw_c_numeric_enter(&loc);        (0, or -ENOMEM: nothing to leave)
 *     ... the libiio call ...
 *     gw_c_numeric_leave(&loc);
 * Between the two the decimal separator of the calling thread is a point: when it is not one
 * already, the thread's LC_NUMERIC is "C" and every other category is what it was. Leave
 * restores the thread's locale object exactly. No mx* call and no return between
 * them: an mx allocation that fails ends the call non-locally and would leave the thread in
 * the temporary locale. Which calls: "Conventions / Gateway (C)", "Numeric locale". */
struct gw_c_numeric { locale_t c, old; };
int gw_c_numeric_enter(struct gw_c_numeric *s);
void gw_c_numeric_leave(struct gw_c_numeric *s);     /* nothing after a failed enter or a leave */

/* ---- enum names: tables generated from iio.h at build time by matlab/tools/gen_enums.py ---- */
enum gw_enum {
	GW_ENUM_CHAN_TYPE,        /* enum iio_chan_type */
	GW_ENUM_MODIFIER,         /* enum iio_modifier */
	GW_ENUM_EVENT_TYPE,       /* enum iio_event_type */
	GW_ENUM_EVENT_DIRECTION,  /* enum iio_event_direction */
	GW_ENUM_HWMON_CHAN_TYPE,  /* enum hwmon_chan_type */
	GW_ENUM_COUNT
};
struct gw_enum_entry { const char *name; long long value; };
const struct gw_enum_entry *gw_enum_table(enum gw_enum which, size_t *count);  /* header order */
const char *gw_enum_name(enum gw_enum which, long long value);  /* "IIO_VOLTAGE"; NULL if no such value */

#endif
```

### `tests/mxmock/mxmock.h` (Task 4a)

```c
/* SPDX-License-Identifier: MIT */
/* Test-side control of the mock MATLAB runtime. */
#ifndef MXMOCK_H
#define MXMOCK_H

#include "matrix.h"

/* Runs mexFunction once. Returns 0 on a normal return, 1 when mexErrMsgIdAndTxt was raised
 * (then every plhs[k] is NULL). plhs needs max(nlhs, 1) entries and is pre-filled with a
 * non-NULL poison value, as MATLAB promises nothing. Arrays returned in plhs belong to the
 * test afterwards. Arrays the test creates outside mock_call (inputs) belong to the test. */
int mock_call(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

const char *mock_err_id(void);       /* "" if the last mock_call returned normally */
const char *mock_err_msg(void);
unsigned int mock_warn_count(void);  /* mexWarnMsgIdAndTxt calls since process start */
const char *mock_warn_id(void);      /* identifier of the latest one, "" if none */
const char *mock_output(void);       /* everything mexPrintf wrote so far */

bool mock_is_locked(void);
void mock_clear_mex(void);    /* "clear mex": nothing while locked; else runs the at-exit function once and forgets it */
void mock_exit_matlab(void);  /* MATLAB exit: runs the at-exit function even when locked, lock count becomes 0 */

/* "Auto-freed" = mxArrays and mxMalloc blocks the gateway left behind when a call ended (by
 * return or by error) and that MATLAB's automatic cleanup would have released. */
size_t mock_autofreed(void);  /* cumulative count; a disciplined gateway keeps it at 0 */
size_t mock_live(void);       /* mxArrays (not counting cell/struct members) + mxMalloc blocks alive now */

/* The nth mx allocation from now on (mxCreate*, mxMalloc, mxCalloc, mxDuplicateArray,
 * mxArrayToUTF8String; nth >= 1) ends the running mock_call as MATLAB ends a MEX on out of
 * memory: non-local exit, mock_call returns 1, mock_err_id() is "MATLAB:nomem". It fires once;
 * 0 switches it off. What that call had allocated is released and is NOT counted by
 * mock_autofreed(). */
void mock_fail_alloc(unsigned int nth);
bool mock_alloc_failed(void);                       /* did the last mock_call end that way */
mxArray *mock_string_object(void);                  /* stands for a MATLAB string: class name "string",
                                                       mxIsChar false, mxArrayToUTF8String NULL, mxGetData NULL */
mxArray *mock_sparse_double(size_t m, size_t n);    /* mxIsSparse true, class double */
/* Full numeric d0-by-d1-by-d2 array, zeroed. As in MATLAB: mxGetNumberOfDimensions 3, mxGetM d0,
 * mxGetN d1 * d2 (the product of the trailing dimensions), so only the dimension count tells it
 * from a d0-by-(d1*d2) matrix. */
mxArray *mock_nd_array(mxClassID cls, mxComplexity c, size_t d0, size_t d1, size_t d2);
/* char d0-by-d1-by-d2 array (a matrix when d2 == 1) whose elements, in storage order (first
 * dimension fastest), are the characters of text; strlen(text) must be d0 * d1 * d2. For the
 * char arrays mxCreateString cannot make: a column, a matrix, N-D, 0-by-3. */
mxArray *mock_char_array(size_t d0, size_t d1, size_t d2, const char *text);

/* Known mock gaps (behaviour of real MATLAB that no local test can show): add one line per gap here.
 * - mxArrayToString converts to UTF-8; the real one converts to the locale's character set.
 * - mxGetLogicals of an array that is not logical, mxGetScalar of an empty array, a cell, a
 *   struct or an object, and mxGetField / mxGetCell beyond the end abort; the real ones return
 *   the data, an undefined value, or NULL.
 * - mock_string_object() has class id mxOPAQUE_CLASS; of a real string only the class name is certain.
 * - mock_sparse_double() always has room for exactly one non-zero element.
 * - mock_fail_alloc() counts only allocations made while mock_call() runs.
 * - The stack is filled with 0xA5 before mexFunction runs (uninitialised locals), which has an
 *   effect on the real stack only, not on AddressSanitizer's fake stack.
 * (tests/mxmock/mxmock.c, at its top, gives the reason for each choice.) */

#endif
```

The mock implements exactly this subset of the MEX C API (anything else fails the mock build through `-Werror=implicit-function-declaration`): `mxMalloc mxCalloc mxFree mxDestroyArray mxDuplicateArray mxGetClassID mxGetClassName mxIsClass mxIsChar mxIsCell mxIsStruct mxIsLogical mxIsNumeric mxIsComplex mxIsSparse mxIsEmpty mxIsScalar mxIsLogicalScalarTrue mxGetM mxGetN mxGetNumberOfElements mxGetNumberOfDimensions mxGetElementSize mxGetData mxGetLogicals mxGetScalar mxCreateDoubleScalar mxCreateLogicalScalar mxCreateNumericMatrix mxCreateUninitNumericMatrix mxCreateDoubleMatrix mxCreateLogicalMatrix mxCreateString mxArrayToUTF8String mxArrayToString mxCreateCellMatrix mxGetCell mxSetCell mxCreateStructMatrix mxSetFieldByNumber mxGetField mxGetNumberOfFields mxGetFieldNameByNumber`; `mexFunction mexErrMsgIdAndTxt mexWarnMsgIdAndTxt mexPrintf mexLock mexUnlock mexIsLocked mexAtExit`. `mxCreateUninitNumericMatrix` fills the data with `0xA5`; `mxGetData` of complex arrays is the interleaved data.

### `tests/mex/mex_test.h` (Task 6; `mt_comma_*` added after the wave 1a review)

```c
/* SPDX-License-Identifier: MIT */
/* Helpers for gateway tests that run mexFunction on the mock MATLAB runtime. */
#ifndef MEX_TEST_H
#define MEX_TEST_H

#include <errno.h>
#include <string.h>

#include "mx_util.h"
#include "mxmock.h"
#undef printf
#include "test_util.h"
#include "fixture_util.h"

/* Command groups of this test, in addition to gw_group_mex. Must start a line: tests/mex/
 * CMakeLists.txt reads it to decide which cmd_<group>.c objects to link. */
#define MT_GROUPS(...) const struct gw_group *const gw_groups[] = { &gw_group_mex, __VA_ARGS__, NULL }

struct mt_fixture {
	char dir[PATH_MAX];          /* made by fixture_setup(): holds iio_fixture.xml and emu-rx_buf0.bin (the ramp) */
	char xml[PATH_MAX + 32];     /* <dir>/iio_fixture.xml */
	char uri[PATH_MAX + 40];     /* "emu:<xml>" */
	char xml_uri[PATH_MAX + 40]; /* "xml:<xml>" */
};
int mt_fixture_setup(struct mt_fixture *fx, const char *src_xml);   /* fixture_setup() and the three paths; 0 or -errno */
void mt_fixture_teardown(struct mt_fixture *fx);                    /* fixture_cleanup() */

mxArray *mt_str(const char *s);
mxArray *mt_u64(uint64_t v);
mxArray *mt_i64(int64_t v);
mxArray *mt_dbl(double v);
mxArray *mt_bool(bool v);
mxArray *mt_bytes(const void *p, size_t n);   /* 1-by-n uint8 */
mxArray *mt_empty(void);                      /* [] */

/* mexFunction(nlhs, out, 1 + nargs, {cmd, args...}); args are mxArray * and are destroyed
 * afterwards. Returns mock_call()'s result. out needs max(nlhs, 1) entries; at most 15 args. */
int mt_call(int nlhs, mxArray *out[], const char *cmd, int nargs, ...);

/* One-output shorthands. On a raise or a result of another class they print the error, count
 * a test failure and return 0 / false / NULL. The result array is destroyed. */
uint64_t mt_u64_of(const char *cmd, int nargs, ...);
int64_t mt_i64_of(const char *cmd, int nargs, ...);
double mt_dbl_of(const char *cmd, int nargs, ...);
bool mt_bool_of(const char *cmd, int nargs, ...);
const char *mt_str_of(const char *cmd, int nargs, ...);   /* static 64 KiB buffer; NULL when the result is [] */
void mt_ok(const char *cmd, int nargs, ...);               /* nlhs = 0; must not raise */
bool mt_raises(const char *id, const char *cmd, int nargs, ...);  /* true iff raised with exactly this identifier */
/* Calls cmd with the 1st, then the 2nd, ... mx allocation failing (mock_fail_alloc), until an
 * attempt ends without an injected failure. Returns the number of failed attempts. Arguments
 * as mt_call, reused for every attempt and destroyed at the end; outputs are destroyed. Objects
 * a failed attempt had already registered stay in the handle table: MT_MAIN_RETURN proves they
 * are released. */
int mt_oom_sweep(int nlhs, const char *cmd, int nargs, ...);

/* One row of a group's "Command reference" table, written into the test from the reference. */
struct mt_arity {
	const char *name;
	int min_rhs, max_rhs;   /* column "Args" */
	int max_lhs;            /* column "Out" */
};
/* The registered table of g is exactly want[0 .. n-1]: the same names in the same order, each
 * with these limits. Every difference is printed and counted as a harness failure (exit status
 * 1). The dispatcher enforces what is registered (test_dispatch); this is what proves that the
 * registered limits are the ones of the reference. Every test_cmd_<group>.c calls it once. */
void mt_check_arities(const struct gw_group *g, const struct mt_arity *want, size_t n);

/* iio_create_context(NULL, uri) registered as an owned GW_T_CONTEXT root whose destroy op is
 * iio_context_destroy; 0 on failure. Lets a group's test run without the context commands. */
gw_id mt_open_context(const char *uri);

/* ---- a process whose numeric locale has a decimal comma (MATLAB's may: the gateway must not care) ----
 * mt_comma_locale(): the name of an installed locale whose decimal separator is ",", found with
 * newlocale() and nl_langinfo_l(), which do not depend on the locale in force. NULL when there
 * is none: that is a harness failure (exit status 1; a missing prerequisite fails, it does not
 * skip), unless the environment variable IIO_TEST_NO_COMMA_LOCALE is "1": then one note is
 * printed and the caller leaves its comma checks out. CI generates de_DE.UTF-8.
 * mt_comma_begin(): false when there is no such locale. Else: the thread must be using the
 * global locale (harness failure otherwise) and LC_NUMERIC of the process becomes that locale.
 * mt_comma_end(): harness failure unless the thread still uses the global locale object (what
 * uselocale((locale_t)0) reports: a command that switched the thread locale and did not switch
 * back is caught here, whatever it switched to) and its decimal separator is still ",". Then
 * LC_NUMERIC is "C" again. Call it after every block of commands started with mt_comma_begin().
 * Between the two, objects are made with gateway commands only: mt_open_context and the dp_*
 * builders call libiio without the gateway's guard and would get every channel scale wrong. */
const char *mt_comma_locale(void);
bool mt_comma_begin(void);
void mt_comma_end(void);

/* Ends main(): MATLAB exit, then no live handle, no live mx memory, nothing auto-freed. */
#define MT_MAIN_RETURN() do { \
	mock_exit_matlab(); \
	CHECK(gw_handle_count() == 0); \
	CHECK(mock_live() == 0); \
	CHECK(mock_autofreed() == 0); \
	TEST_MAIN_RETURN(); } while (0)

#endif
```

### `matlab/mex/data_path.h` (Task 9; header only, every function `static inline`, errors are negative errno values)

```c
#include "mx_util.h"
#include <iio/iio-conv.h>

#define BS_STARTED     1u   /* GW_T_BUFFER_STREAM flags: set by buffer_stream_start, cleared by buffer_stream_stop */
#define BLK_ENQUEUED   1u   /* GW_T_BLOCK flags: set by a successful enqueue; cleared by a successful dequeue and
                               by a dequeue for which libiio returned -EPERM (libiio says: not enqueued);
                               for a stream's blocks: set on every block except the one returned last */
#define BLK_CPU_OFF    2u   /* set / cleared by a successful block_disable_cpu_access */

/* Device of a device, channel, buffer, mask, buffer stream, block, stream, event stream or conv
 * stream id: the entry itself (device), iio_channel_get_device, iio_buffer_get_device, else the
 * device of its parent entry. NULL if the id is zero or stale. */
const struct iio_device *dp_device_of(gw_id id);

/* Registration of the three object types that more than one file creates. Owned child of the
 * parent; destroy op iio_channels_mask_destroy / iio_buffer_close / iio_block_destroy. The buffer
 * stream also gets a pre_destroy op: iio_buffer_stream_cancel(), then iio_buffer_stream_stop() if
 * gw_handle_flags(gw_handle_find(GW_T_BUFFER_STREAM, bs)) has BS_STARTED. Returns 0
 * when the table cannot take it (the caller then destroys the object and returns -ENOMEM).
 * The parent must be a live GW_T_DEVICE (mask), GW_T_BUFFER (buffer stream) or
 * GW_T_BUFFER_STREAM (block) entry, else 0 too. */
gw_id dp_add_mask(struct iio_channels_mask *mask, gw_id dev);
gw_id dp_add_bufstream(struct iio_buffer_stream *bs, gw_id buf);
gw_id dp_add_block(struct iio_block *blk, gw_id bs);

int dp_class_from_name(const char *name, enum iio_conv_type *type, mxClassID *cls);
int dp_class_from_id(mxClassID cls, enum iio_conv_type *type);
mxClassID dp_class_of_type(enum iio_conv_type type);     /* IIO_CONV_I16 -> mxINT16_CLASS, ... */
int dp_scale_from_name(const char *name, enum iio_conv_scale *scale);
/* "Absent", for an ARGUMENT (the options argument itself): no array, or [] (0x0 double). */
bool dp_is_absent(const mxArray *a);
/* "Not given", for an option VALUE: no array, or a full 2-D double without elements ([], 1x0, 0x1,
 * 0x3). An N-D, a sparse or a non-double empty array is a value. */
bool dp_is_unset(const mxArray *a);
/* a: not given (dp_is_unset(); then *out = NULL), a real double scalar, or a real double vector with
 * one entry per output column. *out gets nb_chans entries (mxMalloc, the caller mxFree()s it: per-call
 * scratch; the converter and the stream layer copy the values; a complex pair gets its column's value twice). */
int dp_per_channel(const mxArray *a, unsigned int nb_chans, bool is_complex, double **out);
/* full numeric 2-D array of one of the ten classes; tests mxGetNumberOfDimensions(a) == 2 itself
 * (mxGetM / mxGetN alone take a 4-by-1-by-2 array for a 4-by-2 matrix) */
int dp_check_matrix(const mxArray *a, enum iio_conv_type *type, bool *is_complex);
/* opts: [] or a scalar struct. A field outside `allowed` is -EINVAL, its name copied to bad. */
int dp_opt_check(const mxArray *opts, const char *const *allowed, unsigned int nb, char *bad, size_t len);
/* *f = the value of the field, or NULL when it takes its default (missing, or dp_is_unset()). */
int dp_opt_field(const mxArray *opts, const char *field, const mxArray **f);
/* any real numeric scalar as a double, NaN, Inf and fractions included: for values that are a
 * double in C too. Never cast its result to an integer type: use dp_opt_int(). */
int dp_opt_number(const mxArray *opts, const char *field, double dflt, double *out);
/* a real, integer-valued numeric scalar in [lo, hi], else -EINVAL (NaN, Inf, 2.5, 1e300, out of
 * range; *out untouched). int64 / uint64 are read exactly. dflt is returned as it is. THE way
 * from an option to a C integer: samples, nb_blocks, buffer, timeout_ms. */
int dp_opt_int(const mxArray *opts, const char *field, long long lo, long long hi, long long dflt, long long *out);
int dp_opt_bool(const mxArray *opts, const char *field, bool dflt, bool *out);
int dp_opt_string(const mxArray *opts, const char *field, const char *dflt, char *out, size_t len);
/* reads the fields class, complex, scale, raw; sets layout COLUMNS, saturate true, no gains. cls may be NULL. */
int dp_opt_spec(const mxArray *opts, struct iio_conv_spec *spec, mxClassID *cls);
/* reads the fields gain, offset with dp_per_channel(.., nb_chans, spec->complex_iq, ..) into *gain / *offset
 * (mxMalloc, the caller mxFree()s both) and points spec->user_gain / user_offset at them. Call after the
 * caller has fixed spec->type and spec->complex_iq (block_write takes the type from the data array and
 * complex_iq from its option `complex`, else from the data array). */
int dp_opt_gains(const mxArray *opts, unsigned int nb_chans, struct iio_conv_spec *spec,
		double **gain, double **offset);
```

As implemented by Task 9 (the header's comments are the authority): `opts` may also be `NULL` (an argument beyond `nrhs`) and then means `[]`; every `dp_opt_*` function returns `-EINVAL` for an `opts` that is neither (a struct array, an empty struct, a number, a cell); a field that is missing **or holds an empty double matrix** (`dp_is_unset()`: `[]`, `double.empty(1,0)`, `zeros(0,1)`, 0-by-3; not a 0x0x0 double, not a sparse `[]`, not an empty array of another class) takes its default. That rule is wider than the one for arguments (`dp_is_absent()`, "Conventions": only `[]`) because the M classes declare `double.empty(1,0)` as the default of `Gain`, `Offset` and `Samples` and pass the value on unchanged, and a user's `Gain=zeros(1,0)` arrives the same way; the options argument itself is still `[]` or a scalar struct and nothing else (a 1-by-0 double as `opts` is `-EINVAL`). `dp_opt_number` takes a real full scalar of the ten numeric classes (not logical, not char) and returns it as a `double`, NaN, Inf and fractions included; `dp_opt_int` takes the same and returns `-EINVAL` unless the value is finite, integer-valued and in `[lo, hi]` (`int64` / `uint64` are read from the integer data). **Every option that becomes a C integer (`samples`, `nb_blocks`, `buffer`, `timeout_ms`) is read with `dp_opt_int`**: a cast of the `double` from `dp_opt_number` is undefined behaviour for NaN and 1e300, and the sanitizers of the mock tests do not report it. `dp_opt_bool` takes a logical scalar only, `dp_opt_string` a char row or 2-D empty char that fits `len` (nothing fits a `len` of 0) and holds no NUL character anywhere (the characters are scanned: after a non-ASCII character a byte count would miss it). `dp_opt_spec` does not judge field combinations (a scale with an integer class, `raw` with `'iio'`): `iio_conv_create` / `iio_conv_stream_open` do. `dp_per_channel` also returns `-EINVAL` for `nb_chans == 0` and for an odd `nb_chans` with `is_complex`; on any error `*out` is `NULL`, and `dp_opt_gains` leaves all four pointers `NULL` with nothing allocated. Output pointers `type`, `cls`, `is_complex` of the class functions and of `dp_check_matrix` may be `NULL`. The header also exports the helpers these are built from: `dp_is_absent(a)` (`NULL` or `[]`), `dp_is_unset(a)` (`NULL` or an empty double matrix), `dp_opt_field(opts, field, &f)` (the field's array, `NULL` when it takes its default; for fields that are not a number, a logical or text, such as `channels`: a 1-by-0 `uint64` is a value there and the caller reads it as "no ids") and `dp_classes()` (the table of `DP_NB_CLASSES` = 10 `struct dp_class { name, type, cls }`). `dp_add_*` checks the type of the parent entry (`GW_T_DEVICE` for a mask, `GW_T_BUFFER` for a buffer stream, `GW_T_BUFFER_STREAM` for a block) and returns 0 for any other parent, for a stale one and for `0`: under a wrong parent the object would be destroyed in the wrong order (a block after its buffer stream: use after free in `iio_block_destroy`), under `0` it would be a root that outlives its context. A caller treats that 0 like any other: it destroys the object and returns `-ENOMEM` (it cannot happen after `gw_arg_handle` resolved the parent with its type).

### `tests/mex/dp_test.h` (Task 9; header only, `static inline`)

```c
/* Data-path object builders for gateway tests. Include after "mex_test.h", in ONE source file
 * of a test program (it includes tests/fake_backend.h, whose state is static). Objects are
 * registered exactly as the gateway commands register them (dp_add_* of data_path.h), so a
 * group's test never needs a sibling group's command. Each returns the id, or 0 after
 * counting a test failure when the object does not exist. */
#include "mex_test.h"
#include "data_path.h"
#include "fake_backend.h"

gw_id dp_open_fake(const char *xml_path);          /* fake_reset(); fake_create_context(xml_path) as an owned GW_T_CONTEXT
                                                      root whose destroy op is iio_context_destroy. One at a time: while an
                                                      earlier fake context is alive it counts a test failure and returns 0,
                                                      without a reset (test_create_fake_context: iio:EBUSY) */
gw_id dp_dev(gw_id ctx, const char *name);         /* iio_context_find_device; borrowed child of ctx */
gw_id dp_chn(gw_id dev, const char *id, bool output);
gw_id dp_buf(gw_id dev, unsigned int index);
gw_id dp_mask(gw_id dev, bool output, unsigned int nb, ...);   /* owned mask with these channel ids (const char *) enabled */
gw_id dp_bufstream(gw_id buf, gw_id mask);         /* iio_buffer_open, as the command buffer_open leaves it */
gw_id dp_block(gw_id bs, size_t nbytes);           /* as the command buffer_stream_create_block leaves it */
```

A data-path test gets its `emu:` or `xml:` context with `mt_open_context(fx.uri)` / `mt_open_context(fx.xml_uri)`, its fake context with `dp_open_fake(fx.xml)`, destroys a context with `gw_handle_destroy(ctx)`, and imitates "MATLAB unloads the MEX" with `mock_exit_matlab()`.

### `include/iio/iio-conv.h` and `include/iio/iio-conv-stream.h` (Tasks 1 to 3)

`struct iio_conv_spec` gains one member, placed directly after `saturate` so that the size (32 bytes on LP64) and the offset of every existing member stay the same (Task 1):

```c
	/** Use the storage word as it is: bits = length, shift = 0. Byte order
	 * and signedness are still honoured. Not allowed with IIO_CONV_SCALE_IIO. */
	bool raw;
```

`include/iio/iio-conv-stream.h`, complete (Task 2b creates it with exactly these declarations and comments; every prototype also carries `__iio_conv_api`, and the file has the `extern "C"` guards of `iio-conv.h`):

```c
/* SPDX-License-Identifier: MIT */
/*
 * libiio-conv stream layer: whole frames of converted samples to and from one
 * libiio buffer. Built on explicit blocks (iio_buffer_open,
 * iio_buffer_stream_create_block, iio_block_enqueue / iio_block_dequeue), not
 * on struct iio_stream, which can neither transmit cyclically nor submit a
 * frame in the call that received it.
 *
 * Functions take and return only pointers and scalars and report failure as a
 * negative errno. A stream must be used from one thread at a time.
 */
#ifndef IIO_CONV_STREAM_H
#define IIO_CONV_STREAM_H

#include <iio/iio-conv.h>

struct iio_buffer;
struct iio_conv_stream;

/** Results of read, write and flush. Negative values are errno codes. */
#define IIO_CONV_STREAM_NO_FRAME 0 /**< nothing was transferred within the wait policy */
#define IIO_CONV_STREAM_FRAME    1 /**< one frame was transferred, or the flush completed */

/** Wait policies for iio_conv_stream_set_timeout(); a value > 0 is a time in ms */
#define IIO_CONV_STREAM_TIMEOUT_CONTEXT  (-1) /**< block; the context timeout bounds the wait */
#define IIO_CONV_STREAM_TIMEOUT_NONBLOCK 0    /**< never wait */

/**
 * Open a stream on buf for chans. The direction is that of the buffer: an
 * input buffer gives a receive stream, an output buffer a transmit stream.
 *
 * chans: nb_chans distinct scan elements of buf in host column order (with
 *   spec->complex_iq: I then Q of each column). Only these are enabled. The
 *   hardware may enable more; the layout follows the mask actually in use.
 * samples_per_frame: frame length in samples; every block holds one frame.
 * nb_blocks: blocks in rotation. Receive: all are queued for capture at all
 *   times. Transmit: that many frames can be in flight.
 * cyclic: transmit only. The first frame written repeats until the stream is
 *   closed; one block is used whatever nb_blocks says.
 * spec: type, layout, scale, complex_iq, saturate, raw. user_gain and
 *   user_offset (nb_chans entries each) are copied. For transmit, type and
 *   complex_iq describe the array iio_conv_stream_write() takes.
 *
 * A receive stream is started here (blocks queued, buffer enabled): data
 * captured before the first read is delivered first, see
 * iio_conv_stream_flush(). A transmit stream is started by its first write;
 * its blocks start zeroed.
 *
 * Returns 0 and sets *stream, or a negative errno and leaves *stream alone:
 * -EINVAL for a NULL argument; nb_chans, samples_per_frame or nb_blocks of 0;
 * a channel that is not a scan element of buf; a channel listed twice; cyclic
 * on an input buffer; anything iio_conv_create() rejects with -EINVAL.
 * -ENOTSUP for channel formats iio_conv_create() does not support. Otherwise
 * the error of iio_buffer_open(), iio_buffer_stream_create_block(),
 * iio_block_enqueue() or iio_buffer_stream_start(). On failure everything
 * created so far is released, without dequeuing any block.
 */
int iio_conv_stream_open(struct iio_buffer *buf,
		const struct iio_channel *const *chans, unsigned int nb_chans,
		size_t samples_per_frame, unsigned int nb_blocks, bool cyclic,
		const struct iio_conv_spec *spec, struct iio_conv_stream **stream);

/**
 * Close *stream and set it to NULL. NULL and a pointer to NULL are accepted,
 * so closing twice is harmless.
 *
 * Order: (transmit, started, not cyclic) wait for the frames in flight,
 * oldest first, each under the wait policy, stopping at the first that does
 * not complete; iio_buffer_stream_cancel(); iio_buffer_stream_stop() if the
 * buffer was started; iio_block_destroy() for every block;
 * iio_buffer_close(); then the converters. A cyclic block is never dequeued.
 *
 * (As merged, the close has no bound of its own: it waits under the wait
 * policy, which for IIO_CONV_STREAM_TIMEOUT_CONTEXT on a context with an
 * infinite timeout is one blocking dequeue per frame. A caller that must not
 * block sets a policy first: the MEX gateway's destroy op does, see
 * "Data-path objects". tests/test_stream_fake.c: tx_close_policy.)
 */
void iio_conv_stream_close(struct iio_conv_stream **stream);

/**
 * Set how long read, write and flush may wait for a block:
 * IIO_CONV_STREAM_TIMEOUT_CONTEXT (default): one blocking dequeue, bounded by
 *   the context timeout, which is shared by everything on that context;
 * IIO_CONV_STREAM_TIMEOUT_NONBLOCK: one non-blocking dequeue;
 * > 0: non-blocking dequeues 1 ms apart until that many milliseconds have
 *   passed. Independent of the context timeout, which is never changed.
 * Returns 0, or -EINVAL for a NULL stream or a value below -1.
 */
int iio_conv_stream_set_timeout(struct iio_conv_stream *stream, int timeout_ms);

/**
 * Receive one frame into dst, a host array of samples_per_frame samples in
 * the layout of the spec (COLUMNS: samples_per_frame rows per column).
 *
 * Returns IIO_CONV_STREAM_FRAME after writing the whole frame (never part of
 * one) and queuing the block again. Returns IIO_CONV_STREAM_NO_FRAME, with
 * dst untouched, when no block completed within the wait policy (libiio
 * reported -ETIMEDOUT, -EBUSY or -EAGAIN); the call can be repeated.
 * -EBADF on a transmit stream, -EINVAL for NULL, other libiio errors as is.
 */
int iio_conv_stream_read(struct iio_conv_stream *stream, void *dst);

/**
 * Transmit one frame of nb_samples samples (1 .. samples_per_frame) from src,
 * an array of the spec's type and complexity. A shorter frame is sent as a
 * shorter block (partial frame). Bytes of enabled channels that are not in
 * chans are sent as zero.
 *
 * The frame is converted and queued in this call; the first call starts the
 * buffer. If every block is in flight, the call first waits, under the wait
 * policy, for the oldest one.
 *
 * Returns IIO_CONV_STREAM_FRAME when the frame was queued;
 * IIO_CONV_STREAM_NO_FRAME when no block became free in time (nothing was
 * queued; repeat the call); -EBUSY on a cyclic stream that already has its
 * frame; -EBADF on a receive stream; -EINVAL for NULL or for nb_samples of 0
 * or more than samples_per_frame; other libiio errors as is.
 */
int iio_conv_stream_write(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples);

/**
 * As iio_conv_stream_write() for an array of another element type or
 * complexity; scale, gains, saturate and raw stay those of the spec. One
 * converter per (type, src_complex) is created on first use and kept.
 *
 * src_complex false on a complex_iq stream: src has one real column per I/Q
 * pair and the Q channels are sent as zero.
 * src_complex true on a stream without complex_iq: -EINVAL.
 * -EINVAL also when the type cannot be used with the spec's scaling (integer
 * type with a scale mode, gain or offset).
 */
int iio_conv_stream_write_typed(struct iio_conv_stream *stream, const void *src,
		size_t nb_samples, enum iio_conv_type type, bool src_complex);

/**
 * Receive: discard the frames already captured, at most nb_blocks, so that
 * the next read waits for new data. Never waits.
 * Transmit: wait, under the wait policy, until every queued frame has been
 * sent. Returns at once on a cyclic stream and before the first write.
 * Returns IIO_CONV_STREAM_FRAME when done, IIO_CONV_STREAM_NO_FRAME when a
 * transmit wait ran out, or a negative errno.
 */
int iio_conv_stream_flush(struct iio_conv_stream *stream);

/* Getters. stream must not be NULL. */
size_t iio_conv_stream_get_samples(const struct iio_conv_stream *stream);       /* samples per frame */
unsigned int iio_conv_stream_get_columns(const struct iio_conv_stream *stream); /* nb_chans, or nb_chans / 2 with complex_iq */
size_t iio_conv_stream_get_step(const struct iio_conv_stream *stream);          /* storage bytes per sample, mask in use */
unsigned int iio_conv_stream_get_nb_blocks(const struct iio_conv_stream *stream); /* 1 when cyclic */
enum iio_conv_type iio_conv_stream_get_type(const struct iio_conv_stream *stream);
bool iio_conv_stream_is_complex(const struct iio_conv_stream *stream);
bool iio_conv_stream_is_output(const struct iio_conv_stream *stream);

#endif /* IIO_CONV_STREAM_H */
```

### `tests/fixture_util.h` (Task 2a; all `static inline`; `k` is taken modulo 4096)

```c
int16_t  fixture_ramp_i(unsigned int k);        /* I(k) */
int16_t  fixture_ramp_q(unsigned int k);        /* Q(k) */
uint16_t fixture_ramp_word_i(unsigned int k);   /* word 2k */
uint16_t fixture_ramp_word_q(unsigned int k);   /* word 2k+1 */
/* mkdtemp() under $TMPDIR (default /tmp), copy xml_src there as iio_fixture.xml, write
 * emu-rx_buf0.bin (the ramp). dir needs PATH_MAX bytes. Returns 0 or a negative errno. */
int  fixture_setup(const char *xml_src, char *dir);
void fixture_uri(const char *dir, const char *backend, char *uri, size_t len); /* "<backend>:<dir>/iio_fixture.xml" */
long fixture_read_file(const char *dir, const char *name, void *dst, size_t max); /* bytes read, or negative errno */
int  fixture_write_file(const char *dir, const char *name, const void *src, size_t len);
void fixture_cleanup(const char *dir);           /* removes every regular file in dir, then dir */
```

### `tests/fake_backend.h` (Task 2a)

A test-only libiio backend (operations `static`, the three helpers `static inline`; include it in exactly one translation unit per program). It fills `struct iio_backend_ops` of `deps/install/include/iio/iio-backend.h` (the private types `struct iio_buffer_pdata`, `struct iio_block_pdata`, `struct iio_event_stream_pdata` are defined in this header) and a `struct iio_backend` with `.api_version = IIO_BACKEND_API_V1, .name = "fake", .uri_prefix = "fake:", .default_timeout_ms = 1000`. Structure comes from the fixture XML: `fake_create_context(path)` is `iio_create_context_from_xml(&params, "xml:<path>", &fake_backend, "(fake)", NULL, NULL, 0)` with a zeroed `params`; the result has name `fake`, description `Emulated fixture & test context (fake)`, 8 devices and 4 context attributes **[RUN]**. Behaviour:

```c
#define FAKE_NO_TIMEOUT (-12345)
struct fake_state {
	char log[256];        /* one letter per backend call, in call order (below); letters beyond 255 are dropped */
	int rx_frames;        /* input dequeues that deliver data before "not ready"; default 3 */
	int tx_frames;        /* output dequeues that succeed before "not ready"; default 2 */
	int busy;             /* the next `busy` dequeue calls report "not ready"; default 0 */
	const char *couple;   /* id of one more channel the backend enables in open_buffer; default NULL */
	unsigned int rx_count, tx_count;
	unsigned char tx_data[256]; size_t tx_bytes; bool tx_cyclic;  /* most recent output enqueue */
	int ev_none;          /* errno (positive) read_ev reports when there is no event; default 0: EBUSY if
	                       * nonblock, else ETIMEDOUT (added with the wave 1b review; a gateway test sets it) */
	const struct iio_device *trigger;
	int timeout;          /* last value the set_timeout operation received; default FAKE_NO_TIMEOUT */
	unsigned char *attr_data; size_t attr_len;   /* malloc copy of the bytes write_attr received last; default NULL, 0 */
};
static struct fake_state fake;
void fake_reset(void);                       /* free attr_data, zero everything, then the defaults above */
void fake_clear_log(void);
struct iio_context *fake_create_context(const char *xml_path);   /* or a pointer-encoded error */
```

| Backend op | Log | Behaviour |
|---|---|---|
| `open_buffer` | `O` | enables `fake.couple` (found with `iio_device_find_channel(dev, couple, buffer is output)`) in the mask it is given |
| `create_block` | `B` | `malloc`, filled with `0xEE` |
| `enqueue_block` | `E`, or `e` when cyclic | `-EPERM` (no log) if already queued; output: copies the first `bytes_used` bytes to `tx_data`, sets `tx_bytes`, `tx_cyclic` |
| `enable_buffer` | `S` enable, `s` disable | returns 0 |
| `dequeue_block` | `D` done, `d` not ready | `-EPERM` (no log) if not queued. Not ready when `busy > 0` (then `busy--`) or the direction's count has reached its `*_frames`: returns `-EBUSY` if `nonblock`, else `-ETIMEDOUT`, at once. Done: input fills the block with `uint16` LE words `(256 * rx_count + i) & 0x7ff`, `i` = word index; count++ |
| `cancel_buffer` | `X` | — |
| `free_block` | `F` | — |
| `close_buffer` | `C` | — |
| `open_ev` | — | only for device id `iio:device2`, else `-ENODEV` |
| `read_ev` | `n` when `nonblock`, `b` when the read may wait | event 1: id `0x0001000000000000`, timestamp 1000 (THRESH, RISING, VOLTAGE, channel 0); event 2: id `0x0102010300000000`, timestamp 1001 (MAG, FALLING, ACCEL, modifier X); then "no event": `-fake.ev_none` when that is set, else `-EBUSY` if `nonblock`, else `-ETIMEDOUT`. (The log letter and `ev_none` were added with the wave 1b review: nothing showed that the gateway passes `nonblock` on, and `-EAGAIN`, which is what the local and the network backend answer to a non-blocking read without an event, was never returned) |
| `get_trigger` / `set_trigger` | — | returns `fake.trigger`, or pointer-encoded `-ENODEV` when NULL / stores it, returns 0 |
| `read_attr` | — | writes `"fake:<attribute name>"`, returns its length + 1 |
| `write_attr` | — | keeps a `malloc` copy of exactly `len` bytes in `attr_data` / `attr_len` (replacing the previous one), returns `len` |
| `set_timeout` | — | records the value in `timeout`, returns 0 |
| `ping` | — | returns 0 |

`iio_buffer_close()` itself calls the cancel operation, so a correct stream-layer close logs `X s F.. X C`. (The file as Task 2a committed it had neither `timeout`, `attr_data`, `attr_len`, `FAKE_NO_TIMEOUT` nor the operations `read_attr`, `write_attr`, `set_timeout`, `ping`, and `tests/test_stream_fake.c` had no `backend_misc` case; both were added with the review of Task 9. `read_attr` returns `-ENOSPC` when the text does not fit.) Verified **[RUN]**, and now by `test_backend_misc` of `tests/test_stream_fake.c`: a 47000-byte `iio_attr_write_raw` arrives with its full length; `iio_context_set_timeout(ctx, -1)` reaches `set_timeout` as -1; `iio_device_get_trigger` gives `-ENODEV` before a trigger is set (as libiio's local backend does); a second fake context can be created after the first was destroyed; a missing XML file gives `-EINVAL`.

## Command reference

Every gateway command, grouped by the file that registers it. Task 4 writes every table below into its file as stub entries (`gw_stub`); the task named in the heading replaces the stubs. "Args" is `min_rhs` (or `min-max`), "Out" is `max_lhs`. In "Wraps", `=` means the `iio.h` function of the same name with `iio_` in front, "list" the list form of the functions named, `X` an extra command (`GW_XCMD`, no row in `matlab/api-map.tsv`). Argument names: `ctx`, `dev`, `ch`, `attr`, `buf`, `mask`, `bs`, `blk`, `st`, `es`, `cs`, `scan` are live handle ids of that type (`H0`: 0 is allowed; `id`: destructor argument); `name` is a char row; `tf` a logical scalar; `n` a double scalar; `u8` a `uint8` vector (results are 1-by-N); "name, value" an enum pair. All other rules are in "Conventions".

#### Group `mex` (`iio_mex.c`, Task 6)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `mex_shutdown` | 0 | 0 | — | — | X: `gw_handle_destroy_all()` |
| `mex_handle_count` | 0 | 1 | — | n | X: `gw_handle_count()` |

#### Group `toplevel` (`cmd_toplevel.c`, Task 11)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `strerror` | 1 | 1 | err (integer in [-`INT_MAX`, `INT_MAX`]; `INT_MIN` is `iio:EINVAL`, because `iio_strerror` negates the value) | char | =; `iio_strerror(err, buf, 256)` |
| `has_backend` | 2 | 1 | params, backend (char) | tf | = |
| `get_builtin_backends` | 0 | 1 | — | 1-by-N cell of char | list: `iio_get_builtin_backends_count`, `iio_get_builtin_backend` |
| `scan` | 2 | 1 | params, backends (char) or `[]` | scan | =; owned `GW_T_SCAN` root, destroy op `iio_scan_destroy`; result first; `iio_scan` inside `gw_c_numeric_enter` / `_leave`; `iio_err` check before registering; libiio copies the backends text into a 1024-byte buffer and cuts a longer list without notice (not checked) |
| `scan_destroy` | 1 | 0 | id | — | = |
| `scan_get_results` | 1 | 2 | scan | uris, descriptions (each a 1-by-N cell of char) | list: `iio_scan_get_results_count`, `iio_scan_get_uri`, `iio_scan_get_description` |

#### Group `context` (`cmd_context.c`, Task 12; `create_context`, `context_destroy`, `context_find_attr` already by Task 8)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `create_context` | 2 | 1 | params, uri (char) or `[]` | ctx | =; owned `GW_T_CONTEXT` root, destroy op `iio_context_destroy`; result first; `iio_create_context` inside `gw_c_numeric_enter` / `_leave`; `iio_err` check, then register; if registering fails destroy the context and return `-ENOMEM` |
| `context_destroy` | 1 | 0 | id | — | =; descendants and borrowed ids go first (handle table) |
| `context_get_version_major`, `context_get_version_minor`, `context_get_version_patch` | 1 | 1 | ctx H0 | n | =; id 0 passes `NULL`: the library's own version |
| `context_get_version_tag` | 1 | 1 | ctx H0 | char | = |
| `context_get_xml` | 1 | 1 | ctx | char | =; inside `gw_c_numeric_enter` / `_leave`; `iio_err` check, then parked: `gw_park(xml, 0, <adapter calling iio_free>)` before `gw_ret_str`; the command does not free it |
| `context_get_name`, `context_get_description` | 1 | 1 | ctx | char | = |
| `context_get_attrs` | 1 | 1 | ctx | 1-by-N attr | list: `iio_context_get_attrs_count`, `iio_context_get_attr` |
| `context_find_attr` | 2 | 1 | ctx, name | attr or 0 | = |
| `context_get_devices` | 1 | 1 | ctx | 1-by-N dev | list: `iio_context_get_devices_count`, `iio_context_get_device` |
| `context_find_device` | 2 | 1 | ctx, name | dev or 0 | =; matches id, label or name |
| `context_set_timeout` | 2 | 0 | ctx, timeout_ms (integer in `int` range) | — | =; a negative return is the error |
| `context_get_params` | 1 | 1 | ctx | scalar struct, `double` fields `log_level`, `stderr_level`, `timestamp_level`, `timeout_ms`, `flags` | =; a copy; `out` and `err` are not exposed |
| `context_ping` | 1 | 0 | ctx | — | = |

#### Group `attr` (`cmd_attr.c`, Task 13; `attr_read_raw` with one argument already by Task 8)

`attr` is a live attr handle of any of the seven attribute kinds.

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `attr_get_name`, `attr_get_filename` | 1 | 1 | attr | char | = |
| `attr_get_static_value` | 1 | 1 | attr | char, or `[]` when the attribute has none | = |
| `attr_read_raw` | 1-2 | 1 | attr [, as_bytes tf = false] | char, or 1-by-N `uint8` | =; the read buffer grows (Task 13); a value that fits no buffer is `iio:EFBIG`; the char form is not exact for text that is not UTF-8 |
| `attr_write_raw` | 2 | 1 | attr, u8 | n bytes written (libiio's count) | =; result first (`gw_ret_double(c, 0, 0)`, write, `gw_set_double`); the write inside `gw_c_numeric_enter` / `_leave` |
| `attr_write_string` | 2 | 1 | attr, char | n bytes written (libiio's count, with the NUL) | =; result first and bracketed as `attr_write_raw`; text with an embedded NUL is `iio:EINVAL` |
| `attr_read_bool` | 1 | 1 | attr | tf | = |
| `attr_read_longlong` | 1 | 1 | attr | `int64` | = |
| `attr_read_double` | 1 | 1 | attr | `double` | = |
| `attr_write_bool` | 2 | 0 | attr, tf | — | =; inside `gw_c_numeric_enter` / `_leave` |
| `attr_write_longlong` | 2 | 0 | attr, integer (full `long long` range) | — | =; inside `gw_c_numeric_enter` / `_leave` |
| `attr_write_double` | 2-3 | 0 | attr, `double` or `single` scalar (any other class: `iio:EINVAL`) [, exact tf = true] | — | =; inside `gw_c_numeric_enter` / `_leave` (Task 13) |
| `attr_get_range` | 1 | 1 | attr | 1-by-3 `double` `[min step max]` | =; inside `gw_c_numeric_enter` / `_leave` |
| `attr_get_available` | 1 | 1 | attr | 1-by-N cell of char | =; parked: `gw_park(list, count, <adapter calling iio_available_list_free>)` before `gw_ret_strs`; the command does not free it |

#### Group `device` (`cmd_device.c`, Task 14)

Every returned id is a borrowed child of the device's context.

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `device_get_context` | 1 | 1 | dev | ctx | =; `gw_handle_find(GW_T_CONTEXT, iio_device_get_context(dev))` |
| `device_get_id` | 1 | 1 | dev | char | = |
| `device_get_name`, `device_get_label` | 1 | 1 | dev | char, or `[]` when the device has none | = |
| `device_get_channels` | 1 | 1 | dev | 1-by-N ch | list: `iio_device_get_channels_count`, `iio_device_get_channel` |
| `device_get_attrs` | 1 | 1 | dev | 1-by-N attr | list: `iio_device_get_attrs_count`, `iio_device_get_attr` (plain device attributes only) |
| `device_get_buffers` | 1 | 1 | dev | 1-by-N buf | list: `iio_device_get_buffers_count`, `iio_device_get_buffer` |
| `device_find_channel` | 3 | 1 | dev, name, output (tf) | ch or 0 | =; matches id, label or name, in that direction |
| `device_find_attr` | 2 | 1 | dev, name | attr or 0 | = |
| `device_is_trigger`, `device_is_hwmon` | 1 | 1 | dev | tf | = |

#### Group `channel` (`cmd_channel.c`, Task 15)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `channel_get_device` | 1 | 1 | ch | dev | = |
| `channel_get_id` | 1 | 1 | ch | char | = |
| `channel_get_name`, `channel_get_label` | 1 | 1 | ch | char, or `[]` when the channel has none | = |
| `channel_is_output`, `channel_is_scan_element` | 1 | 1 | ch | tf | = |
| `channel_get_index` | 1 | 1 | ch | n (negative when not a scan element) | = |
| `channel_get_type` | 1 | 2 | ch | name, value | =; names of `GW_ENUM_CHAN_TYPE` |
| `channel_get_modifier` | 1 | 2 | ch | name, value | =; names of `GW_ENUM_MODIFIER` |
| `hwmon_channel_get_type` | 1 | 2 | ch | name, value | =; the same value as `channel_get_type` with its name of `GW_ENUM_HWMON_CHAN_TYPE` |
| `channel_get_attrs` | 1 | 1 | ch | 1-by-N attr | list: `iio_channel_get_attrs_count`, `iio_channel_get_attr` |
| `channel_get_event_attrs` | 1 | 1 | ch | 1-by-N attr | list: `iio_channel_get_event_attrs_count`, `iio_channel_get_event_attr` |
| `channel_find_attr`, `channel_find_event_attr` | 2 | 1 | ch, name | attr or 0 | = |
| `channel_get_data_format` | 1 | 1 | ch | scalar struct: `length`, `bits`, `shift`, `repeat` (n), `is_signed`, `is_fully_defined`, `is_be`, `with_scale`, `is_float` (tf), `scale`, `offset` (n) | =; a copy |
| `channel_refresh_format` | 1 | 0 | ch | — | = |
| `channel_convert`, `channel_convert_inverse` | 2 | 1 | ch, u8 | u8, same length | =; a channel whose format `length` is 0 (not a scan element) is `iio:EINVAL`, checked BEFORE the multiple test (which would divide by zero); the length must be a multiple of `length/8 * repeat`, else `iio:EINVAL` (the gateway answers `iio:EINVAL` whenever `length/8 * repeat` is 0, which also covers a storage length below 8 bits); an empty input returns 1-by-0; one C call per sample |
| `channel_enable`, `channel_disable` | 2 | 0 | ch, mask | — | =; `iio:EINVAL` if the channel is not a scan element or the mask is of another device; `iio:EPERM` on a borrowed mask. Order of the checks: other device (`EINVAL`), borrowed mask (`EPERM`), not a scan element (`EINVAL`) |
| `channel_is_enabled` | 2 | 1 | ch, mask | tf | =; same device check |
| `create_channels_mask` | 1 | 1 | dev | mask | result first; =, but takes the device (C takes its channel count); `iio:EINVAL` for a device without channels (libiio returns NULL) |
| `channels_mask_destroy` | 1 | 0 | id | — | = |
| `channels_mask_get` | 1 | 1 | mask | 1-by-N logical, one per channel of the device, device order | X (`iio_channel_is_enabled` per channel) |

#### Group `buffer` (`cmd_buffer.c`, Task 16)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `buffer_get_device` | 1 | 1 | buf | dev | = |
| `buffer_is_output` | 1 | 1 | buf | tf | = |
| `buffer_get_attrs` | 1 | 1 | buf | 1-by-N attr | list: `iio_buffer_get_attrs_count`, `iio_buffer_get_attr` |
| `buffer_find_attr` | 2 | 1 | buf, name | attr or 0 | = |
| `buffer_get_scan_elements` | 1 | 1 | buf | 1-by-N ch | list: `iio_buffer_get_scan_elements_count`, `iio_buffer_get_scan_element`; the ids equal those of the device's channels |
| `buffer_open` | 2 | 1 | buf, mask | bs | =; result first; `iio_err` check; `iio:EINVAL` if the mask is of another device |
| `buffer_close` | 1 | 0 | id | — | = |
| `buffer_stream_start`, `buffer_stream_stop` | 1 | 0 | bs | — | =; no libiio call when already in that state; `iio:EPERM` on a borrowed buffer stream |
| `buffer_stream_cancel` | 1 | 0 | bs | — | =; allowed on a borrowed buffer stream; `BS_STARTED` is unchanged. `iio_buffer_stream_cancel` also stops libiio's enqueue worker of the stream (`iio_task_stop`), and `buffer_stream_start` is then a no-op because the flag is set: only `buffer_stream_stop` followed by `buffer_stream_start` starts the worker again (read from `buffer.c`, not measured; the worker serves only backends without block operations). The class help of `iio.BufferStream` says "after cancel: stop, then start" |
| `buffer_stream_get_channels_mask` | 1 | 1 | bs | mask (borrowed, read-only) | =; the same id on every call |
| `buffer_stream_create_block` | 2 | 1 | bs, nbytes | blk | =; result first; `iio_err` check; `iio:EPERM` on a borrowed buffer stream; nbytes is an integer in 0 .. `LLONG_MAX` and libiio judges it (`-EINVAL` below one sample); there is no upper bound in the gateway, so a huge value is the backend's `malloc` failure (`iio:ENOMEM`; AddressSanitizer aborts on it in a mock test) |
| `buffer_stream_is_started` | 1 | 1 | bs | tf | X: the `BS_STARTED` flag (true for the buffer stream of an `iio_stream` from the first successful `stream_get_next_block` on) |

#### Group `block` (`cmd_block.c`, Task 17)

"Data access" = `block_get_bytes`, `block_set_bytes`, `block_read`, `block_write`, `channel_read`, `channel_write`: `iio:EPERM` while `BLK_ENQUEUED` or `BLK_CPU_OFF`.

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `block_destroy` | 1 | 0 | id | — | = |
| `block_first` | 2 | 1 | blk, ch | n = byte offset of the channel's first sample; equals `block_size` when the channel is not enabled | =, as an offset instead of a pointer; `iio:EINVAL` if the channel is of another device |
| `block_enqueue` | 3 | 0 | blk, bytes_used, cyclic (tf) | — | =; `iio:EPERM` if `BLK_ENQUEUED` or borrowed |
| `block_dequeue` | 2 | 1 | blk, nonblock (tf) | tf: true = dequeued, false = not ready (`-EBUSY` with `nonblock`) | =; result first; `iio:EPERM`, without calling libiio, if not `BLK_ENQUEUED`, if its buffer stream is not `BS_STARTED`, or if borrowed; if libiio returns `-EPERM` the gateway clears `BLK_ENQUEUED` (libiio says the block is not enqueued: a dequeue that failed on its generic block path has consumed it) and raises `iio:EPERM`; other errors raise (`iio:ETIMEDOUT`, ...) and leave `BLK_ENQUEUED` set |
| `block_get_buffer_stream` | 1 | 1 | blk | bs | =; `gw_handle_find(GW_T_BUFFER_STREAM, ..)` |
| `block_get_dmabuf_fd` | 1 | 1 | blk | n | =; a negative result raises |
| `block_disable_cpu_access` | 2 | 0 | blk, tf | — | =; `iio:EPERM`, without calling libiio, on a borrowed block (a change through an object of a stream); neither emu nor the fake has the operation (`iio:ENOSYS`), so only the gating of data access by `BLK_CPU_OFF` is tested, with the flag set by the test |
| `channel_read` | 3 | 1 | ch, blk, raw (tf) | u8: the bytes `iio_channel_read` produced (1-by-0 when the channel is not enabled) | =; channel and block of one device, else `iio:EINVAL`; the gateway tests `iio_channel_is_enabled(ch, <the block's stream mask>)` itself and returns 1-by-0 without calling libiio when it is false (libiio's raw shortcut would copy another channel's data); libiio is offered only the whole samples of the block (its loop runs while a sample STARTS inside the block and would read past a block that ends in half a sample) |
| `channel_write` | 4 | 1 | ch, blk, u8, raw (tf) | n bytes consumed | =; same checks: 0, without calling libiio, for a channel that is not enabled and for an empty `u8`; libiio is offered whole samples of the channel only, of the block and of `u8` (the byte count is rounded down to a multiple of `length / 8 * repeat`: libiio's raw shortcut, taken when the channel fills the whole sample, would copy a trailing part of a sample as it comes) |
| `block_size` | 1 | 1 | blk | n bytes | X (`iio_block_end` - `iio_block_start`) |
| `block_get_bytes` | 1-3 | 1 | blk [, off0, count] | u8 | X; default the whole block; a range outside the block is `iio:EINVAL` |
| `block_set_bytes` | 2-3 | 1 | blk, u8 [, off0] | n copied | X; must fit, else `iio:EINVAL` |
| `block_read` | 2 | 1 | blk, opts | N-by-C numeric | X (`iio_conv_read`) |
| `block_write` | 3 | 1 | blk, data, opts | n samples written = min(rows, capacity); 0, without calling the converter, for zero rows | X (`iio_conv_write`) |

`off0` is a 0-based byte offset. `opts` is `[]` or a scalar struct; an unknown field is `iio:EINVAL`. `block_read` fields: `channels` (`uint64` ids; empty = every scan element enabled in the stream's mask, device order), `class`, `complex`, `scale`, `raw`, `gain`, `offset` (as in `conv_stream_open` below), `samples` (not given = capacity; else an integer ≥ 1, read with `dp_opt_int`: 0, 2.5, NaN, Inf and 1e300 are `iio:EINVAL`); N = min(samples, floor(block_size / step)). In every options struct a field that holds an empty double matrix (`[]`, `double.empty(1,0)`) counts as not given (`dp_is_unset()` of `data_path.h`), so M code passes its `double.empty(1,0)` defaults on unchanged. `block_write` fields: `channels`, `complex`, `scale`, `raw`, `gain`, `offset`; the class comes from `data`; `complex` absent means `mxIsComplex(data)`. `complex` true with real `data` (MATLAB hands a complex array whose imaginary part is all zero to the MEX as a real array): one column per I/Q pair, the Q channels are written as zero (only the bytes of the Q channels in the rows written; other enabled channels of the block keep their data). The column count is checked before the row count: a 0-by-0 array on a two-channel stream is `iio:EINVAL`, 0-by-2 returns 0. `gain`: a value of 0 is `iio:EINVAL` (`iio_conv_create`). **Gain and offset, the rule of the C library:** on read the result is device × gain + offset; on write the device gets (host − offset) / gain, the exact inverse, so a value read and written back with the same options is unchanged. Worked example: gain 2, offset 10; the host value 100 is stored as (100 − 10) / 2 = 45, and 45 is read as 45 × 2 + 10 = 100. With `complex` the offset is applied to the I and to the Q value alike (a value per column is "given to both channels of a pair"): 100 + 50i is stored as I = 45, Q = 20. (This plan said "host / gain − offset" until the wave 1b review; the code never did that. Pinned by `test_transmit_pairs` of `test_cmd_block.c`, `gain_and_offset` of `test_m_sequences.c`, `tTxStream.gainAndOffset` and `tBlock.writeGainOffset`.) `channels` also accepts an empty `uint64` (what `iio.internal.Object.handles(iio.Channel.empty(1,0))` gives) as "all enabled"; a stale or non-channel id in it is `iio:EBADF`, a channel of another device `iio:EINVAL`, and a channel given twice `iio:EINVAL` for `block_read` and `block_write` alike (as on the fast path; on a write the later column would overwrite the earlier one). **Real data on I/Q pairs is checked like complex data:** before the converter for the I channels is made, the full pair list goes through the converter that complex data would use (cached like any other), and its error is returned, so a Q channel that is no scan element, is not enabled in the stream's mask or has another format than its I channel (`iio:EINVAL`; `iio:ENOTSUP` for a channel with `repeat` > 1) is an error whether the imaginary part happens to be zero or not, and nothing is written. As for complex data, no converter is made for an array without rows. N for the output is taken from `iio_device_get_sample_size(dev, <stream mask>)` so that the output exists before a converter is created; `iio_conv_get_step` is compared with it afterwards (a mismatch is the gateway-bug guard `iio:EFAULT`). A block whose buffer stream has no handle-table entry gets a converter for the one call. `complex` false with complex `data` is `iio:EINVAL`. A wrong column count is `iio:EINVAL`.

#### Group `stream` (`cmd_stream.c`, Task 18)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `buffer_create_stream` | 4 | 1 | buf, nb_blocks, samples_count, mask | st | =; result first; `iio_err` check; a mask of another device is `iio:EINVAL`; nb_blocks is an integer from 1 to 4096 (`DP_MAX_NB_BLOCKS` of `data_path.h`, the limit of `conv_stream_open`) and samples_count an integer ≥ 1 (`gw_arg_int`: 0, 2.5, NaN, Inf are `iio:EINVAL`); a mask without a capturable channel and `samples_count * sample size` beyond `SIZE_MAX / 2` are `iio:EINVAL`. A size that still fits `size_t` is not refused: the backend's `malloc` fails (`iio:ENOMEM`; AddressSanitizer aborts on it in a mock test): a test that passes random numbers bounds samples_count |
| `stream_get_next_block` | 1 | 1 | st | blk (borrowed) | =; result first (the call moves blocks); `iio_err` check first: on an error (`iio:ETIMEDOUT`, ...) nothing is registered, `BLK_ENQUEUED` is set on every block view of the stream, and the error is raised. On success: registers the stream's buffer stream as a borrowed child of `st` on first use (with `BS_STARTED`); moves `BLK_ENQUEUED` (other flag bits of a view are kept). After a failed call the next one raises what the backend answers to the enqueue of a block it still holds: `iio:EPERM` on the fake, `iio:EBUSY` on emu (not tested on emu: it needs the 5 s stall) |
| `stream_cancel` | 1 | 0 | st | — | = |
| `stream_destroy` | 1 | 0 | id | — | = |

#### Group `event` (`cmd_event.c`, Task 19)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `device_create_event_stream` | 1 | 1 | dev | es | =; result first; `iio_err` check |
| `event_stream_destroy` | 1 | 0 | id | — | = |
| `event_stream_read` | 2 | 3 | es, nonblock (tf) | tf, id (`uint64`), timestamp (`int64`) | =; result first (the three results exist, as false, 0, 0, before the read, which consumes the event); tf false with id 0 and timestamp 0 when there is no event (`-EBUSY`, `-EAGAIN` or `-ETIMEDOUT`; the local and the network backend answer a non-blocking read with `-EAGAIN`); other errors raise. `nonblock` is passed to libiio as given: the fake logs `n` or `b` for every read |
| `event_get_type` | 1 | 2 | id (`uint64` scalar: the event id) | name, value | =, on the event id; names of `GW_ENUM_EVENT_TYPE` |
| `event_get_direction` | 1 | 2 | id (`uint64` scalar) | name, value | =, on the event id; names of `GW_ENUM_EVENT_DIRECTION` |
| `event_get_channel` | 3 | 1 | id (`uint64` scalar), dev, diff (tf) | ch or 0 | =, on the event id, with the check below |

Channel check: at the pinned commit `iio_event_get_channel` returns the device's last channel when nothing matches. The result is kept only if its type equals bits 39..32 of the event id, its modifier equals bits 47..40, and the decimal number after the leading letters of the channel id (0 when there is none) equals the channel number of the event (bits 15..0 for `diff` false, bits 31..16 for `diff` true); otherwise the command returns 0.

#### Group `lowlevel` (`cmd_lowlevel.c`, Task 20)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `device_get_sample_size` | 2 | 1 | dev, mask | n bytes | =; a mask of another device is `iio:EINVAL` (libiio accepts any mask with the same word count, so this check is what protects it). Known libiio hazard, not guarded: `iio_device_get_sample_size` computes `size % (length / 8)` for every enabled channel with a scan index, a division by zero for a storage length below 8 bits (no such channel in the fixture) |
| `device_get_trigger` | 1 | 1 | dev | dev (the trigger), or 0 when none is assigned (`-ENODEV`) or libiio returns NULL | =; `iio_err` check; other errors raise; a trigger device of another context is `iio:EIO` (it would be registered under the wrong context; not reachable with emu or the fake) |
| `device_set_trigger` | 2 | 0 | dev, trigger dev or 0 (removes it) | — | =; a trigger of another context is `iio:EINVAL` |
| `device_reg_read` | 2 | 1 | dev, addr | `uint32` | =; addr and value: real, integer-valued, 0 .. 4294967295, else `iio:EINVAL`; result first (libiio's attribute fallback writes the address); inside `gw_c_numeric_enter` / `_leave` |
| `device_reg_write` | 3 | 0 | dev, addr, value | — | =; no mx allocation; inside `gw_c_numeric_enter` / `_leave` |
| `device_get_debug_attrs` | 1 | 1 | dev | 1-by-N attr | list: `iio_device_get_debug_attrs_count`, `iio_device_get_debug_attr` |
| `device_get_event_attrs` | 1 | 1 | dev | 1-by-N attr | list: `iio_device_get_event_attrs_count`, `iio_device_get_event_attr` |
| `device_find_debug_attr`, `device_find_event_attr` | 2 | 1 | dev, name | attr or 0 | = |

#### Group `conv` (`cmd_conv.c`, Task 21): the fast path on `iio-conv-stream.h`

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `conv_stream_open` | 3 | 1 | dev, chans, opts | cs | X: `iio_conv_stream_open`, `iio_conv_stream_set_timeout`; result first (every argument is parsed, then `gw_ret_id(c, 0, 0)`, then the open) |
| `conv_stream_read` | 1 | 2 | cs | data (N-by-C of the open class and complexity; all zero when `valid` is false), valid (tf) | X: `iio_conv_stream_read`; both results exist (valid = false) before the frame is taken |
| `conv_stream_write` | 2 | 1 | cs, data | valid (tf) | X: `iio_conv_stream_write_typed` |
| `conv_stream_flush` | 1 | 1 | cs | tf | X: `iio_conv_stream_flush`; result first |
| `conv_stream_close` | 1 | 0 | id | — | X: `iio_conv_stream_close`, after the bounded drain of "Data-path objects" |

`conv_stream_open`: `chans` is a 1-by-N cell of channel ids as char (looked up with `iio_device_find_channel(dev, id, opts.output)`; an unknown id is `iio:ENOENT` and the message names it) or a `uint64` vector of channel handle ids (a handle that is not a live channel is `iio:EBADF`; one of another device or of the other direction is `iio:EINVAL`). Either list may be a row or a column; a matrix, an empty list, a non-char cell element and a char matrix element are `iio:EINVAL`. `complex` with an odd channel count and `cyclic` with `output` false are refused by the gateway (`iio:EINVAL`) before any buffer is opened. The parent of the new handle is the borrowed id of the buffer: that id is registered by the open and stays until its context goes, so the handle count after a close, or after an open that failed once the buffer was chosen, is one above the count before the first open on that buffer. `opts` is a scalar struct; a missing field, and one that holds an empty double matrix (`[]`, `double.empty(1,0)`: `dp_is_unset()`), takes its default (a required one is then missing: `iio:EINVAL`); an unknown field is `iio:EINVAL`. The integer fields `buffer`, `samples`, `nb_blocks`, `timeout_ms` are read with `dp_opt_int` (a real integer-valued scalar in the field's range; NaN, Inf, 2.5, 1e300 are `iio:EINVAL`), never by a cast of a `double`:

| Field | Default | Meaning |
|---|---|---|
| `output` | (required) | true = transmit. Must equal the direction of the buffer, else `iio:EINVAL` |
| `buffer` | -1 | 0-based buffer position on the device; -1 = the first buffer of the device whose scan elements contain every channel (`iio:EINVAL` if none) |
| `samples` | (required) | samples per frame |
| `nb_blocks` | 2 | 1 .. 4096, else `iio:EINVAL`: every block is created inside the open, one MEX call that nothing can interrupt (200000 blocks of one sample took 0.12 s on the fake, `UINT_MAX` would be about 45 minutes and the machine's memory). `buffer_create_stream` has the same limit (`DP_MAX_NB_BLOCKS`) |
| `cyclic` | false | |
| `class` | `'double'` | one of `double single int8 uint8 int16 uint16 int32 uint32 int64 uint64`; for transmit only the class of the first converter |
| `complex` | false | pair channels 2k, 2k+1 into column k |
| `scale` | `'none'` | `none`, `iio`, `fullscale` |
| `raw` | false | spec flag `raw` |
| `gain`, `offset` | `[]` | `[]`, a scalar (all channels) or one value per output column (given to both channels of a pair). Receive: device × gain + offset; transmit: (host − offset) / gain (group `block` above has the rule and an example) |
| `timeout_ms` | -1 | passed to `iio_conv_stream_set_timeout` |

`conv_stream_write(cs, data)`: `data` must be a full numeric matrix of one of the ten classes with 1 .. `samples` rows and exactly `iio_conv_stream_get_columns()` columns; the type comes from `mxGetClassID`, `src_complex` from `mxIsComplex` (MATLAB hands a complex array whose imaginary part is all zero to the MEX as a real array, so a real array on a complex stream is valid and sends Q = 0). Anything else is `iio:EINVAL`; complex data for a stream opened with `complex = false` is `iio:EINVAL`; a second frame on a cyclic stream is `iio:EBUSY`; a receive stream is `iio:EBADF`. `valid` is false when the C call returned `IIO_CONV_STREAM_NO_FRAME`.

#### Group `test` (`cmd_test.c`, Task 10; compiled only with `IIO_MEX_TEST_HOOKS`)

| Command | Args | Out | Arguments | Result | Wraps |
|---|---|---|---|---|---|
| `test_create_fake_context` | 1 | 1 | xml_file (char) | ctx | X: `fake_reset()`, `fake_create_context(xml_file)` inside `gw_c_numeric_enter` / `_leave`; result first; `iio_err` check; owned `GW_T_CONTEXT` root, destroy op `iio_context_destroy`. `iio:EBUSY` while an earlier fake context is alive (the fake's state is one set of statics; the argument is parsed first, so a wrong argument is `iio:EINVAL` even then, and `iio:EBUSY` does not reset the state) |
| `test_fake_last_timeout` | 0 | 1 | — | n: `fake.timeout` (-12345 if `set_timeout` was not called since the context was created) | X |
| `test_fake_last_write` | 0 | 1 | — | 1-by-N `uint8`: `fake.attr_data` (1-by-0 if none) | X |
| `test_fake_set` | 2 | 0 | field (char), value | — | X: `'rx_frames'`, `'tx_frames'`, `'busy'` (whole number 0 .. `INT_MAX`; negative or fractional is `iio:EINVAL`) or `'couple'` (char, `''` = none; the text is copied into a 64-byte static buffer of `cmd_test.c`, 64 characters or more are `iio:EINVAL`); another field is `iio:EINVAL` |
| `test_fake_log` | 0 | 1 | — | char: `fake.log` | X |

The fake's state is reset when a fake context is created, not when it is destroyed, so the log, the last timeout and the last write can be read after the context is gone. (`fake.attr_data`, up to 47000 bytes in the tests, is therefore freed only by the next `fake_reset()` and is still allocated when the MEX unloads; it is reachable through a static, exists only in test builds, and LeakSanitizer does not report it.) Every new fake context starts with the defaults, `couple` included.

#### Functions of `iio.h` without a command

`matlab/api-map.tsv` gives the reason for each. Not exposed: `iio_context_set_data`, `iio_context_get_data`, `iio_device_set_data`, `iio_device_get_data`, `iio_channel_set_data`, `iio_channel_get_data`, `iio_buffer_set_data`, `iio_buffer_get_data` (C user pointers), `iio_block_start`, `iio_block_end` (raw pointers), `iio_block_foreach_sample` (C callback), `iio_attr_get_available_buf` (caller-buffer variant). Used inside the gateway only: `iio_ptr`, `iio_err`, `iio_err_cast`, `iio_free`, `iio_available_list_free`. The two function-like macros `iio_attr_read` and `iio_attr_write` (C11 `_Generic` over the typed readers and writers) are not functions and have no row; `iio.Attr.write` dispatches on the MATLAB class instead.

## MATLAB surface

The outlines below fix every public member (name, arguments, defaults, result class). They are outlines, not literal MATLAB: bodies are one or a few gateway calls and are written by the owning task. Every class derives from `iio.internal.Object` and follows "Conventions / MATLAB (M)". Until its own task lands, a class exists as the stub written by Task 7 (internal constructor only).

### Internal helpers (`matlab/+iio/+internal/`, Task 7)

```matlab
classdef (Abstract) Object < handle     % hidden properties Handle (1,1) uint64, Parent; Object(parent, handle); eq, ne;
                                        % static hidden h = iio.internal.Object.handles(objs)   (uint64, size of objs)
p  = iio.internal.params(opts)          % name-value struct -> [] or the gateway params struct
ms = iio.internal.timeout_ms(t)         % Inf -> -1; whole number in int32 range unchanged; else error('iio:EINVAL', ...)
iio.internal.shutdown()                 % iio_mex('mex_shutdown')
n  = iio.internal.handle_count()        % iio_mex('mex_handle_count')
```

### Package functions (`matlab/+iio/`, Task 11)

```matlab
v  = iio.version()            % struct: major, minor, patch (double), tag (char) of the loaded libiio
                              % (context_get_version_* called with uint64(0))
b  = iio.backends()           % 1-by-N cell of char, libiio order
tf = iio.has_backend(name)    % name (1,1) string
s  = iio.strerror(err)        % err (1,1) double {mustBeInteger}; char. -2^31 and values beyond int are iio:EINVAL
r  = iio.scan_contexts(backends, opts)
%   backends (1,1) string = ""   comma-separated, e.g. "usb,ip" or "usb=0456:b673"; omitted: libiio's default list
%   opts.Timeout  (1,1) double   milliseconds, Inf = no timeout
%   opts.LogLevel (1,1) string {mustBeMember(opts.LogLevel, ["nolog","error","warning","info","debug"])}
%   r: 1-by-N struct array with fields uri and description (char); 1-by-0 when nothing is found
```

`scan_contexts` passes `[]` for `backends` only when the argument was omitted (`nargin == 0`), releases the scan with `onCleanup(@() iio_mex('scan_destroy', h))`, and reads both result lists with one `scan_get_results` call. The `nargin == 0` branch is not executed by any MATLAB test (no test may scan the default list); only the gateway side `scan(params, [])` is tested, in `test_cmd_toplevel.c`.

### `iio.Context` (Task 12; Task 8 writes a first version with the constructor `Context(uri)`, `delete` and `find_attr`)

  ```matlab
  classdef Context < iio.internal.Object
      properties (Constant)
          TIMEOUT_BACKEND = 0             % use the backend's default
          TIMEOUT_INFINITE = -1           % Inf is accepted as well
          TIMEOUT_NONBLOCK = -2147483648
      end
      properties (SetAccess = immutable)
          uri char            % as given; '' when defaulted or adopted
          name char           % backend name: 'local', 'ip', 'usb', 'serial', 'emu', 'xml'
          description char
          version struct      % major, minor, patch (double), tag (char): of the remote side
      end
      properties (Dependent)
          xml         % char                              context_get_xml
          attrs       % 1-by-N iio.Attr                   context_get_attrs
          devices     % 1-by-N iio.Device                 context_get_devices
          params      % struct, see context_get_params
      end
      methods
          function obj = Context(uri, opts)
              arguments
                  uri (1,1) string = ""        % "" : libiio uses $IIOD_REMOTE, else "local:"
                  opts.Timeout (1,1) double    % ms for every I/O of this context; Inf = no timeout
                  opts.LogLevel (1,1) string {mustBeMember(opts.LogLevel, ["nolog", "error", "warning", "info", "debug"])}
                  opts.StderrLevel (1,1) string {mustBeMember(opts.StderrLevel, ["nolog", "error", "warning", "info", "debug"])}
                  opts.TimestampLevel (1,1) string {mustBeMember(opts.TimestampLevel, ["nolog", "error", "warning", "info", "debug"])}
                  opts.XmlIncludeValues (1,1) logical
                  opts.AdoptHandle (1,1) uint64   % internal: wrap a context the gateway already holds
              end
          end
          function delete(obj)                    % context_destroy when Handle ~= 0
          function dev = find_device(obj, name)   % name (1,1) string; iio.Device or empty
          function a = find_attr(obj, name)       % name (1,1) string; iio.Attr or empty
          function set_timeout(obj, timeout)      % timeout (1,1) double, via iio.internal.timeout_ms
          function ping(obj)                      % throws on failure
      end
  end
  ```

  The constructor calls `create_context(iio.internal.params(opts), u)` with `u = []` when `uri == ""` (or adopts `opts.AdoptHandle`), sets `Handle` before any other call so that `delete` releases the context if a later call fails, then fills the immutable properties. The timeout belongs to the context (libiio v1 has no per-buffer or per-call timeout): the class help states that `set_timeout` affects attribute I/O and every stream of that context.

### `iio.Attr` (Task 13; Task 8 writes a first version with `read`)

  ```matlab
  classdef Attr < iio.internal.Object
      properties (SetAccess = immutable)
          name char
      end
      properties (Dependent)
          filename        % char                                  attr_get_filename
          static_value    % char, or [] when there is none        attr_get_static_value
          value           % get: read(); set: write(v)
      end
      methods
          function obj = Attr(parent, handle)     % internal form only; reads name when handle ~= 0
          function s = read(obj)                  % char                         attr_read_raw
          function b = read_raw(obj)              % 1-by-N uint8                 attr_read_raw, as_bytes = true
          function tf = read_bool(obj)            % logical
          function v = read_longlong(obj)         % int64
          function v = read_double(obj)           % double
          function write(obj, v)                  % dispatch on the class of v, see below
          function n = write_raw(obj, data)       % data (1,:) uint8; n = bytes written (double)
          function write_string(obj, s)           % s (1,1) string
          function write_bool(obj, tf)            % tf (1,1) logical
          function write_longlong(obj, v)         % v (1,1) {mustBeNumeric, mustBeReal}, passed to the gateway unchanged
          function write_double(obj, v, opts)     % v (1,1) double; opts.Exact (1,1) logical = true
          function r = get_range(obj)             % [min step max]
          function c = get_available(obj)         % 1-by-N cell of char
      end
  end
  ```

  `write_longlong` does not declare `v (1,1) int64`: that conversion would round `2.5` to `3` and saturate `uint64` values above `intmax('int64')` silently. The gateway reads `int64` and `uint64` exactly and answers a fraction or a value out of range with `iio:EINVAL` (`gw_arg_int`). `write(v)`: `char` or `string` goes to `write_string`; `logical` to `write_bool`; an integer class, or a real floating-point scalar with `v == fix(v)` and `abs(v) < 2^63`, to `write_longlong`; anything else to `write_double`. (A MATLAB literal such as `2400000000` is a `double`; sending it as `2400000000` rather than `2400000000.000000` is what integer attributes expect.)

### `iio.Device` (Task 14)

```matlab
classdef Device < iio.internal.Object
    properties (SetAccess = immutable)
        id char
        name char       % '' when the device has none
        label char      % '' when the device has none
    end
    properties (Dependent)
        context         % iio.Context: the Parent, after device_get_context confirmed its handle
        is_trigger      % logical
        is_hwmon        % logical
        channels        % 1-by-N iio.Channel, libiio order (position k is channels-mask bit k-1)
        attrs           % 1-by-N iio.Attr
        buffers         % 1-by-N iio.Buffer
        debug_attrs     % 1-by-N iio.Attr                      device_get_debug_attrs
        event_attrs     % 1-by-N iio.Attr                      device_get_event_attrs
    end
    methods
        function obj = Device(parent, handle)              % internal form only; reads id, name, label when handle ~= 0
        function ch = find_channel(obj, name, is_output)   % name (1,1) string; is_output (1,1) logical = false; iio.Channel or empty
        function a = find_attr(obj, name)                  % name (1,1) string; iio.Attr or empty
        function a = find_debug_attr(obj, name)            % iio.Attr or empty
        function a = find_event_attr(obj, name)            % iio.Attr or empty
        function n = sample_size(obj, mask)                % mask (1,1) iio.ChannelsMask; double, bytes
        function t = get_trigger(obj)                      % iio.Device (same Parent), or iio.Device.empty(1,0) for id 0
        function set_trigger(obj, trig)                    % trig: an iio.Device, or [] to remove it (sends uint64(0))
        function v = reg_read(obj, addr)                   % uint32
        function reg_write(obj, addr, value)
        function es = create_event_stream(obj)             % iio.EventStream(obj)
    end
end
```

The parent of every `iio.Device` is its `iio.Context`; the parent passed to `iio.Channel`, `iio.Attr` and `iio.Buffer` is the device. `sample_size`, the trigger, register and debug/event attribute members call commands of group `lowlevel` (Task 20), which are stubs raising `iio:ENOSYS` until that task lands; their MATLAB tests are in `tDeviceLowLevel.m` (Task 20).

### `iio.Channel`, `iio.ChannelsMask` (Task 15)

```matlab
classdef Channel < iio.internal.Object
    properties (SetAccess = immutable)       % read once in the constructor
        id char
        name char                            % '' when the channel has none
        label char                           % '' when the channel has none
        output (1,1) logical
        scan_element (1,1) logical
        index (1,1) double                   % scan index; negative when not a scan element
    end
    properties (Dependent)
        device                  % iio.Device: the Parent, after channel_get_device confirmed its handle
        type, type_id           % char such as 'IIO_VOLTAGE' / double; from hwmon_channel_get_type ('HWMON_TEMP')
                                % when device_is_hwmon(channel_get_device(h)) (asked of the gateway, so it does not
                                % depend on the Parent), else from channel_get_type
        modifier, modifier_id   % char such as 'IIO_MOD_I', 'IIO_NO_MOD' / double
        attrs, event_attrs      % 1-by-N iio.Attr (their Parent is the channel)
        data_format             % struct of channel_get_data_format
    end
    methods
        obj = Channel(dev, handle)          % internal form only
        a = find_attr(obj, name)            % iio.Attr or empty
        a = find_event_attr(obj, name)
        enable(obj, mask)                   % obj (1,:) {mustBeA(obj, "iio.Channel")}, mask (1,1) {mustBeA(mask, "iio.ChannelsMask")}
                                            % (never the class itself: Conventions). An empty row of channels, what
                                            % find_channel returns for an unknown id, is error('iio:EINVAL', ...) in enable and disable
        disable(obj, mask)
        tf = is_enabled(obj, mask)          % obj (1,1)
        refresh_format(obj)
        data = read(obj, block, raw)        % raw (1,1) logical = false. Column of intN / uintN (N = storage length,
                                            % sign from the format) when raw is false, repeat is 1, the format is not
                                            % float and length is 8, 16, 32 or 64; else a uint8 column of the bytes
                                            % (a channel that is not enabled: 0-by-1 of that class, made without typecast)
        n = write(obj, block, data, raw)    % raw (1,1) logical = false. data: any numeric vector, sent as
                                            % typecast(data(:), 'uint8'); n = bytes used (whole samples only; an empty data is passed as a 0-by-1 uint8 without typecast)
        out = convert(obj, raw_bytes)       % uint8 in, uint8 column out
        out = convert_inverse(obj, host_bytes)
    end
end
classdef ChannelsMask < iio.internal.Object
    properties (Dependent)
        device        % iio.Device (the Parent for a mask made with ChannelsMask(dev); for a stream's mask: Parent.buffer.device).
                      % No gateway call for a mask made with ChannelsMask(dev) (there is no command for the device of a
                      % mask), so it still answers when the context is gone; for a stream's mask Buffer.device calls
                      % buffer_get_device and raises iio:EBADF then. iio:EBADF without such a Parent
        channels      % get: the enabled iio.Channel objects, device order. set: enable exactly these. The set method
                      % checks every given channel first (scan_element; channel_is_enabled raises iio:EINVAL for another
                      % device), so a failed assignment leaves the mask unchanged; then it disables the enabled positions
                      % (channels_mask_get, ids of device_get_channels) and enables the given ones. [] disables everything;
                      % a value that is not an iio.Channel array is iio:EINVAL
        sample_size   % device_get_sample_size(device, this mask)
    end
    methods
        obj = ChannelsMask(parent, handle)  % ChannelsMask(dev): create_channels_mask; ChannelsMask(bs, handle): wrap a stream's read-only mask
        tf = enabled(obj)                   % 1-by-N logical, one per device channel
        destroy(obj), delete(obj)
    end
end
```

### `iio.Buffer`, `iio.BufferStream` (Task 16)

```matlab
classdef Buffer < iio.internal.Object
    properties (Dependent)
        device          % iio.Device: the Parent, after buffer_get_device confirmed its handle
        output          % logical
        attrs           % 1-by-N iio.Attr
        scan_elements   % 1-by-N iio.Channel (parent: device)
    end
    methods
        obj = Buffer(dev, handle)                          % internal form only
        a = find_attr(obj, name)
        bs = open(obj, mask)                               % iio.BufferStream(obj, mask)
        st = create_stream(obj, mask, samples_count, nb_blocks)   % iio.Stream(obj, mask, samples_count, nb_blocks); nb_blocks = 4
    end
end
classdef BufferStream < iio.internal.Object
    properties (Dependent)
        buffer        % iio.Buffer: the Parent
        started       % logical
        mask          % read-only iio.ChannelsMask(obj, handle): the channels actually enabled
        sample_size   % bytes per sample of that mask
    end
    methods
        obj = BufferStream(buf, arg)                       % arg an iio.ChannelsMask: buffer_open; arg a uint64: wrap that handle
                                                           % sample_size asks the gateway directly (buffer_stream_get_channels_mask,
                                                           % buffer_get_device, device_get_sample_size); after cancel: stop, then start
        start(obj), stop(obj), cancel(obj)
        blk = create_block(obj, nbytes)                    % iio.Block(obj, nbytes)
        close(obj), delete(obj)
    end
end
```

### `iio.Block` (Task 17)

```matlab
classdef Block < iio.internal.Object
    properties (Dependent)
        parent          % the Parent: iio.BufferStream or iio.Stream
        nbytes          % double
        buffer_stream   % Parent when it is an iio.BufferStream with the handle block_get_buffer_stream returns,
                        % else iio.BufferStream(Parent.buffer, handle)
    end
    methods
        obj = Block(parent, arg)                           % arg a uint64: wrap that handle (always: iio.Block(bs, uint64(32)) wraps handle 32);
                                                           % any other real numeric scalar: buffer_stream_create_block with that many bytes
                                                           % (parent must be one iio.BufferStream, else iio:EINVAL). create_block passes a double,
                                                           % Stream.next a uint64
        enqueue(obj, bytes_used, cyclic)                   % defaults 0 (whole block), false
        tf = dequeue(obj, nonblock)                        % default false
        data = read(obj, opts)
        %   opts.Channels = iio.Channel.empty(1,0)   (all enabled, device order)
        %   opts.OutputDataType (1,1) string {mustBeMember: the ten classes of iio.RxStream} = "double"
        %   opts.Complex (1,1) logical = false, opts.Raw (1,1) logical = false
        %   opts.Scale (1,1) string {mustBeMember(opts.Scale, ["none","iio","fullscale"])} = "none"
        %   opts.Gain double = double.empty(1,0), opts.Offset double = double.empty(1,0), opts.Samples = double.empty(1,0)
        %   (declared as on iio.RxStream since Task 22, so the same wrong value has the same answer on both paths;
        %   an empty double field is "not given" in the gateway)
        n = write(obj, data, opts)
        %   opts.Channels, opts.Scale, opts.Raw, opts.Gain, opts.Offset as above
        %   Gain, Offset: read returns sample * Gain + Offset, write stores (DATA - Offset) / Gain (the exact inverse;
        %   with Complex the Offset applies to I and Q alike). opts.Channels is declared with mustBeA; a channel twice is iio:EINVAL
        %   opts.Complex (1,1) logical = ~isreal(data);  passed to block_write as the field complex (real data
        %   with Complex=true: one column per I/Q pair, Q = 0). The data array itself is passed unchanged.
        b = bytes(obj, offset, count)                      % uint8 column; defaults 0, whole block
        n = set_bytes(obj, b, offset)                      % default 0
        off = first(obj, ch)
        fd = dmabuf_fd(obj)
        disable_cpu_access(obj, tf)          % iio:EPERM for a block of an iio.Stream; iio:ENOSYS on emu and the fake
        destroy(obj), delete(obj)
    end
end
```

### `iio.Stream` (Task 18)

```matlab
classdef Stream < iio.internal.Object
    properties (SetAccess = immutable), samples_count, nb_blocks, end
    properties (Dependent), buffer, end                    % iio.Buffer: the Parent
    methods
        obj = Stream(buf, mask, samples_count, nb_blocks)  % nb_blocks (1,1) double = 4; buffer_create_stream. Internal form
                                                           % Stream(parent, handle): a uint64 second argument; samples_count and nb_blocks are then 0
        blk = next(obj)        % iio.Block(obj, handle): owned by the stream; valid until the next call to next()
        cancel(obj), destroy(obj), delete(obj)   % on an output buffer destroy sends the queued blocks, unless the
                                                 % context timeout is Inf: then they are cancelled (class help)
    end
end
```

### `iio.EventStream` (Task 19)

```matlab
classdef EventStream < iio.internal.Object
    properties (Dependent), device, end                    % iio.Device: the Parent
    methods
        obj = EventStream(dev)                             % device_create_event_stream (only when nargin == 1; EventStream() and
                                                           % EventStream(parent, handle) call nothing); device is the Parent, no gateway call
        ev = read(obj, nonblock)      % nonblock (1,1) logical = true (a blocking read has no timeout on real backends).
                                      % [] when there is no event, else decode(id, device) plus the field timestamp (int64)
        destroy(obj), delete(obj)
    end
    methods (Static)
        ev = decode(id, dev)          % id (1,1) uint64, dev (1,1) iio.Device
    end
end
```

Event struct: `id` (`uint64`), `type`, `direction` (char), `type_id`, `direction_id` (double), `channel`, `channel_diff` (`iio.Channel(dev, handle)`, or `iio.Channel.empty(1,0)` where `event_get_channel` returned 0), and from `read` also `timestamp` (`int64`).

### `iio.RxStream`, `iio.TxStream` (Task 21)

```matlab
classdef RxStream < iio.internal.Object                    % Parent: the device
    properties (SetAccess = immutable)                     % the read-only copies of the options
        device, channels, SamplesPerFrame, OutputDataType, Complex, Scale, Gain, Offset, Raw, NumBlocks, Timeout
        % device: the iio.Device given. channels: 1-by-N cell of char channel ids in column order, whatever
        % form the argument had (for iio.Channel objects: their id). The rest: the option values as given, except
        % that OutputDataType and Scale are stored as char ('double', 'none'): text results are char.
    end
    methods
        function obj = RxStream(dev, channels, opts)
            arguments
                dev (1,1) {mustBeA(dev, "iio.Device")}
                channels                      % string array / cellstr / char of channel ids, or iio.Channel array;
                                              % column order; with Complex: I then Q of each column
                opts.SamplesPerFrame (1,1) double {mustBeInteger, mustBePositive} = 32768
                opts.OutputDataType (1,1) string {mustBeMember(opts.OutputDataType, ["double","single","int8","uint8","int16","uint16","int32","uint32","int64","uint64"])} = "double"
                opts.Complex (1,1) logical = false
                opts.Scale (1,1) string {mustBeMember(opts.Scale, ["none","iio","fullscale"])} = "none"
                opts.Gain double = double.empty(1,0)      % scalar, or one value per output column
                opts.Offset double = double.empty(1,0)
                opts.Raw (1,1) logical = false
                opts.NumBlocks (1,1) double {mustBeInteger, mustBePositive} = 2
                opts.Timeout (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Timeout, -1)} = -1   % ms; -1: the context timeout bounds the wait, 0: no wait
                opts.Buffer (1,1) double {mustBeInteger, mustBeGreaterThanOrEqual(opts.Buffer, -1)} = -1     % 0-based buffer position, -1 = automatic
            end
        end
        [data, valid] = read(obj)    % SamplesPerFrame-by-C, class OutputDataType, complex iff Complex; zeros when ~valid
        tf = flush(obj)              % drop the frames captured so far
        close(obj), delete(obj)      % idempotent
    end
end
classdef TxStream < iio.internal.Object                    % the same properties without OutputDataType, plus Cyclic
    methods
        obj = TxStream(dev, channels, opts)   % the arguments block of RxStream, literally, with identical
                                              % validators and defaults, minus opts.OutputDataType,
                                              % plus opts.Cyclic (1,1) logical = false
        valid = write(obj, data)     % 1..SamplesPerFrame rows, C columns, any of the ten classes, real or complex
        tf = flush(obj)              % wait until the queued frames are sent
        close(obj), delete(obj)      % idempotent; a close drains the frames in flight first (for at most 1 s
                                     % when the context timeout is Inf and Timeout is -1: the gateway's bound)
                                     % Gain, Offset: the device gets (DATA - Offset) / Gain, the inverse of RxStream (sample * Gain + Offset);
                                     % NumBlocks above 4096 is iio:EINVAL from the gateway (RxStream too)
    end
end
```

Constructor body: the channel list is `iio.internal.Object.handles(channels)` for an `iio.Channel` array, else `cellstr(channels)`, reshaped to 1-by-N in both cases (a column string array is the same list); `iio_mex('conv_stream_open', dev.Handle, ch, s)` with `s` built by one `struct(...)` call of scalar fields (`'output'`, `'samples'`, `'class', char(opts.OutputDataType)`, ...); `'gain', opts.Gain` and `'offset', opts.Offset` go in as they are (the default `double.empty(1,0)` is "not given" at the gateway, as `[]` is: no `isempty` branch in M code). The toolbox parity configuration is `OutputDataType="int16", Complex=true, Raw=true`. The `arguments` block of `RxStream` above is literal (so there is no `RxStream()` form). `read`, `write` and `flush` have no `arguments` block (fast path): a wrong call is answered by the gateway. Tests of a rejected option value (`OutputDataType="int12"`, `Scale="full"`, `Timeout=-2`) assert "an identifier starting with `MATLAB:`" with try/catch; the exact name `MATLAB:validators:mustBeMember` is **[CI]**.

### Test support (`matlab/tests/`, Task 8)

```matlab
classdef EmuFixture < matlab.unittest.fixtures.Fixture
    properties (SetAccess = private)
        Dir, XmlFile, Uri, XmlUri        % char: fresh tempname directory; <Dir>/iio_fixture.xml; ['emu:' XmlFile]; ['xml:' XmlFile]
    end
    methods
        setup(fixture)                   % asserts exist('iio_mex', 'file') == 3; copies the fixture XML; writes the ramp to emu-rx_buf0.bin
        writeWords(fixture, file, words) % uint16 little-endian to <Dir>/<file>
        v = readInt16(fixture, file)     % <Dir>/<file> as an int16 column, little-endian; error if the file is missing
        ctx = fakeContext(fixture)       % iio.Context(AdoptHandle = iio_mex('test_create_fake_context', fixture.XmlFile))
    end
    methods (Static)
        w = rampWords(nbSamples)         % uint16 row of 2*nbSamples words: W(0), W(1), ... of "Test data" (k modulo 4096)
        v = s12(words)                   % int16, same size: low 12 bits sign-extended
        v = s14shift2(words)             % int16, same size: bits 15..2 sign-extended
    end
end
classdef (Abstract) IioTestCase < matlab.unittest.TestCase
    properties, Fx, end                  % TestMethodSetup: tc.Fx = tc.applyFixture(EmuFixture)
    methods
        verifyIioError(tc, fcn, errnoName)   % tc.verifyError(fcn, "iio:" + errnoName)
        verifyValidationError(tc, fcn)       % fcn must throw an identifier that starts with 'MATLAB:' (an error of an
                                             % arguments block); try/catch + startsWith, since verifyError needs the whole identifier
    end
    % TestMethodTeardown: verifies iio.internal.handle_count() == 0 ('iio objects leaked'), then iio.internal.shutdown()
end
```

---

# Tasks

## Wave 0: serial foundation (Tasks 1 to 9, in order)

### Task 1: `raw` flag of `struct iio_conv_spec`

**Wave 0. Depends on:** nothing. **Build directory:** `build-t1`.

**Files:**
- Modify: `include/iio/iio-conv.h`, `src/plan.c`, `tests/conv_gen.h`, `README.md` (one sentence)
- Test: `tests/test_plan.c`, `tests/test_scalar.c` (and `tests/test_backends.c` through `conv_gen.h`)

**Interfaces:**
- Consumes: `plan_chan()` in `src/plan.c`; `gen_random_case()` and the reference reader `ref_read_one()` in `tests/conv_gen.h`.
- Produces: member `bool raw` of `struct iio_conv_spec` as in "Shared contracts". Semantics: the plan uses `bits = length`, `shift = 0` for every channel (mask, value range, `FULLSCALE` gain all follow); `raw` with `IIO_CONV_SCALE_IIO` returns `-EINVAL`; everything else (byte swap, sign, saturate, user gain and offset) is unchanged. This is the toolboxes' legacy unconverted output (requirement R12 of Appendix B).

- [ ] **Step 1:** Add to `tests/test_plan.c` (descriptor `S12_4` = length 16, bits 12, shift 4, signed, LE):
  `sizeof(struct iio_conv_spec) == 32` and `offsetof(.., raw) == offsetof(.., saturate) + 1` and
  `offsetof(.., user_gain) == 16`; `S12_4` + `raw` + `I16` → plan `bits == 16`, `shift == 0`,
  `mask == 0xffff`, `lo == -32768`, `hi == 32767`, `affine == false`; `raw` + `FULLSCALE` + `F64` →
  `a == 1.0/32768`; `raw` + `SCALE_IIO` → `-EINVAL`; `raw` + `user_gain {2.0}` + `F64` → `a == 2.0`.
- [ ] **Step 2:** Add to `tests/test_scalar.c`, read (all `raw = true`, `saturate = false` unless stated):
  `S12_4` bytes `F0 FF` → I16 `-16`; `F0 7F` → `32752`; `s12/16>>0` bytes `34 12` → `4660` (and `564`
  with `raw = false`); bytes `23 A1` → `-24285` (`291` without); unsigned 12/16 bytes `FF FF` → I16
  `-1`, with `saturate` `32767`, I32 `65535`; `be:u24/32>>8` bytes `12 34 56 78` → U32 `0x12345678`
  (`0x00123456` without); `S12_4` `FULLSCALE` F64 bytes `00 40` → `0.5`. Write: I16 `32767` into
  `S12_4` → bytes `FF 7F` (`F0 7F` with `raw = false, saturate = true`); `-2` → `FE FF`; F64
  `FULLSCALE` `1e9` → `FF 7F`.
- [ ] **Step 3:** In `tests/conv_gen.h`: `gen_random_case` sets `spec.raw` in one case out of four (and then
  replaces `SCALE_IIO` by `SCALE_NONE`); the independent reference uses `length` for `bits` and 0 for
  `shift` when `raw` is set. This puts `raw` into the random read, write, round-trip and
  backend-equality tests.
- [ ] **Step 4:** Run `cmake -S . -B build-t1 -G Ninja -DIIO_CONV_SANITIZE=ON && cmake --build build-t1`.
  Expected: FAIL to compile (`struct iio_conv_spec` has no member `raw`).
- [ ] **Step 5:** Implement (header member; in `plan_chan` effective `bits` / `shift` and the `-EINVAL`).
- [ ] **Step 6:** Run `ctest --test-dir build-t1 --output-on-failure`, then again with `IIO_CONV_FORCE` set to each
  of `scalar sse41 avx2 avx512`. Expected: all tests pass each time, no sanitizer report.
- [ ] **Step 7:** `README.md`: one sentence on `raw` next to the scaling table. `rm -rf build-t1`.
- [ ] **Step 8:** Commit `feat: raw spec flag (storage word as is)`.

### Task 2a: Fixture and fake backend

**Wave 0. Depends on:** Task 1. **Build directory:** `build-t2`.

**Files:**
- Create: `tests/resources/iio_fixture.xml`, `tests/fixture_util.h`, `tests/fake_backend.h`
- Modify: `tests/CMakeLists.txt` (only, inside `if(IIO_CONV_WITH_LIBIIO)`: `iio_conv_test(test_stream ${CMAKE_CURRENT_SOURCE_DIR}/resources/iio_fixture.xml)`, the same for `test_stream_fake`, and `TIMEOUT 30` on both)
- Test: `tests/test_stream.c` (`emu:`), `tests/test_stream_fake.c` (fake backend): created here with one case each; Tasks 2b and 3 add theirs

**Interfaces:**
- Consumes: the pinned libiio (`iio.h`, `iio-backend.h`); `tests/test_util.h`.
- Produces: `tests/resources/iio_fixture.xml`, `tests/fixture_util.h` and `tests/fake_backend.h` as in "Shared contracts" and "Test data". No libiio-conv code.

- [ ] **Step 1:** Create `tests/resources/iio_fixture.xml` from Appendix A of this plan:

  ```sh
  awk '/^<!-- fixture:begin -->$/{f=1;next} /^<!-- fixture:end -->$/{f=0} f' \
      docs/superpowers/plans/2026-11-15-matlab-bindings.md | sed '1d;$d' > tests/resources/iio_fixture.xml
  sha256sum tests/resources/iio_fixture.xml && xmllint --valid --noout tests/resources/iio_fixture.xml
  ```

  Expected: 13033 bytes, `25710f34dd9b9660e0c426af673d24e80e61f355345db219a46dce4f7aab55fe`, and no output from `xmllint`.
- [ ] **Step 2:** Write `tests/test_stream.c` (`argv[1]` = fixture) with the one case:
  - `fixture_selfcheck`: `fixture_ramp_word_i(0) == 0x0800`, `fixture_ramp_word_q(0) == 0x07ff`, `fixture_ramp_word_i(1) == 0x1801`, `fixture_ramp_word_q(1) == 0x07fe`, `fixture_ramp_word_i(16) == 0x0810`, `fixture_ramp_word_q(16) == 0x17ef`, `fixture_ramp_word_i(4095) == 0xf7ff`, `fixture_ramp_word_q(4095) == 0xf800`, `fixture_ramp_word_i(4096) == 0x0800`; the sum of the 8192 words is 268431360; after `fixture_setup` the file `emu-rx_buf0.bin` has 16384 bytes whose first four are `00 08 ff 07`; after `fixture_cleanup` the directory is gone.
- [ ] **Step 3:** Write `tests/test_stream_fake.c` (`fake_reset()`, then `fake_create_context` on the fixture copied by `fixture_setup`) with the one case:
  - `backend_misc` (the operations the gateway tests of Tasks 10, 19 and 20 rely on): `iio_context_get_name` is `"fake"`, 8 devices, 4 context attributes, description `"Emulated fixture & test context (fake)"`; `iio_attr_read_raw` of `emu-phy` attribute `text` gives `"fake:text"`; a 47000-byte buffer with `buf[i] = i % 256` written with `iio_attr_write_raw(text, buf, 47000)` returns 47000, `fake.attr_len == 47000` and `fake.attr_data` equals it; `fake.timeout == FAKE_NO_TIMEOUT`, after `iio_context_set_timeout(ctx, 250)` it is 250, after `(ctx, -1)` it is -1; `iio_device_create_event_stream(emu-rx)` fails with `-ENODEV`, on `emu-phy` it succeeds, two `iio_event_stream_read(es, &ev, true)` return 0 with ids `0x0001000000000000`, `0x0102010300000000` and timestamps 1000, 1001, the third returns `-EBUSY`, a blocking read `-ETIMEDOUT`; `iio_device_get_trigger(emu-rx)` is an error pointer with `-ENODEV`, after `iio_device_set_trigger(emu-rx, trigger0) == 0` it returns `trigger0`, `iio_device_set_trigger(emu-rx, emu-phy) == -EINVAL`; `fake_create_context("/nonexistent.xml")` is an error pointer with `-EINVAL`.
- [ ] **Step 4:** Register both tests in `tests/CMakeLists.txt`. Run `cmake -S . -B build-t2 -G Ninja -DIIO_CONV_SANITIZE=ON && cmake --build build-t2`. Expected: FAIL to compile (`fixture_util.h`, `fake_backend.h` missing).
- [ ] **Step 5:** Write `tests/fixture_util.h` and `tests/fake_backend.h` exactly as specified in "Shared contracts".
- [ ] **Step 6:** Build and run `ctest --test-dir build-t2 -R 'test_stream' --output-on-failure`. Expected: both pass, no sanitizer or leak report. `rm -rf build-t2`.
- [ ] **Step 7:** Commit `test: emu fixture, fixture helpers and fake libiio backend`.

### Task 2b: Stream layer, part 1: public header, receive

**Wave 0. Depends on:** Task 2a. **Build directory:** `build-t2`.

**Files:**
- Create: `include/iio/iio-conv-stream.h`, `src/stream.c`
- Modify: `CMakeLists.txt` (only: append `src/stream.c` where `src/iio_adapter.c` is appended)
- Test: `tests/test_stream.c`, `tests/test_stream_fake.c` (add the cases below to the files of Task 2a)

**Interfaces:**
- Consumes: `iio_conv_create`, `iio_conv_read`, `iio_conv_get_step`, `iio_conv_destroy`; `spec.raw` (Task 1); the fixture, `fixture_util.h` and `fake_backend.h` (Task 2a).
- Produces: `include/iio/iio-conv-stream.h` exactly as in "Shared contracts"; working `iio_conv_stream_open` (both directions), `iio_conv_stream_close`, `iio_conv_stream_set_timeout`, `iio_conv_stream_read`, `iio_conv_stream_flush` (receive part) and the seven getters; `iio_conv_stream_write` and `iio_conv_stream_write_typed` return `-ENOSYS` until Task 3.

- [ ] **Step 1:** Add to `tests/test_stream.c` (`fixture_setup`, context `emu:`; spec
  `{I16, COLUMNS, SCALE_NONE, complex_iq}` and channels `{voltage0_i, voltage0_q}` of `emu-rx`
  buffer 0 unless stated). Requirement numbers (R1 ...) are explained in Appendix B.
  - `rx_frames` (R1, R3, R5, R13): open(1024 samples, 2 blocks) → 0; getters: step 4, columns 1,
    samples 1024, nb_blocks 2, not output, complex, type `IIO_CONV_I16`. Five reads each return 1;
    in read `f`, `dst[2k] == fixture_ramp_i(1024 f + k)` and `dst[2k+1] == fixture_ramp_q(1024 f + k)`
    for all k (read 4 is the wrap: first pair `(-2048, 2047)`).
  - `rx_one_block` (R6): 1 block; two reads give frames 0 and 1 (second starts `(-1024, 1023)`).
  - `rx_raw` (R12a): `spec.raw`; `dst[2k] == (int16_t)fixture_ramp_word_i(k)`,
    `dst[2k+1] == (int16_t)fixture_ramp_word_q(k)`; spot values `(2048, 2047)`, `(6145, 2046)`,
    k = 16 `(2064, 6127)`, k = 1023 `(-1025, -3072)`.
  - `rx_float` (R12c): `{F32, FULLSCALE}`; `dst[2k] == (float)I/2048.0f` exactly; first pair
    `(-1.0f, 0.99951171875f)`. Same spec with `I16` → open returns `-EINVAL`.
  - `rx_pairing` (R4): channels `{voltage1_i, voltage1_q, voltage0_i, voltage0_q}`, 512 samples →
    step 8, columns 2; column 0 sample j `== (ramp_i(2j+1), ramp_q(2j+1))`, column 1
    `== (ramp_i(2j), ramp_q(2j))`; spot: col0[0] `(-2047, 2046)`, col1[0] `(-2048, 2047)`,
    col0[511] `(-1025, 1024)`.
  - `rx_subset` (R4): not complex, channels `{voltage1_q, voltage0_i}`, 8 samples, 1 block →
    `dst[0..7] == 2047 .. 2040`, `dst[8..15] == -2048 .. -2041`. All five channels, 4 samples → step 10, columns 5.
  - `rx_flush` (R7): 2 blocks; read (frame 0), flush → 1, read → frame 3 (first pair `(1024, -1025)`).
  - `open_errors`: each returns the code and leaves `*stream == NULL`: `nb_chans` 0, samples 0,
    blocks 0, NULL spec → `-EINVAL`; `{voltage0_i, voltage0_i}` → `-EINVAL`; an `emu-tx` channel on
    the `emu-rx` buffer → `-EINVAL`; `altvoltage0` on the `emu-tx` buffer → `-EINVAL`; input
    `voltage0` of `emu-multi` on its buffer 1 → `-EINVAL`; complex with 3 channels → `-EINVAL`;
    `cyclic` on `emu-rx` → `-EINVAL`; `rot_quaternion` and `voltage3` of `iio:device4` → `-ENOTSUP`;
    the same open on an `xml:` context → `-ENOSYS`.
  - `rx_missing_file`: delete `emu-rx_buf0.bin`; open → `-ENOENT` in less than 1 s (no dequeue of an
    unstarted buffer). Restore the file.
  - `close_twice` (R21): `iio_conv_stream_close(&s)` sets `s == NULL`; again; `iio_conv_stream_close(NULL)`.
    Then 50 × (open, one read, close): every read is frame 0.
  - `wrong_direction`: `iio_conv_stream_write(rx, buf, 4) == -EBADF`.
- [ ] **Step 2:** Add to `tests/test_stream_fake.c` (same channels;
  4 samples, 2 blocks, `fake_reset()` before each case).
  - `order` (close order): after open `fake.log` is `"OBBEES"`; after one read `"OBBEESDE"` and
    `dst` = `0,1,2,3,4,5,6,7`; frames 2 and 3 start with 256 and 512; `fake_clear_log()`, close →
    log `"XsFFXC"`.
  - `no_frame` (R8): after 3 frames, `dst` prefilled with `0x5555`: read → 0, `dst` unchanged, log
    grew by exactly `"d"`; `set_timeout(s, 0)`: read → 0, exactly one `"d"`; `set_timeout(s, 30)`:
    read → 0 after ≥ 0.030 s and < 1 s; `iio_context_get_params(ctx)->timeout_ms` is the same before
    and after; `set_timeout(s, -2) == -EINVAL`.
  - `late_frame` (R8): `rx_frames = 100`, `busy = 3`, `set_timeout(s, 500)`: read → 1, log grew by `"dddDE"`.
  - `coupled` (review focus): `fake.couple = "voltage1_i"`, 1 block → step 6; first frame
    `dst` = `0,1,3,4,6,7,9,10`.
- [ ] **Step 3:** Run `cmake -S . -B build-t2 -G Ninja -DIIO_CONV_SANITIZE=ON && cmake --build build-t2`.
  Expected: FAIL to link (`iio_conv_stream_*` undefined).
- [ ] **Step 4:** Implement `src/stream.c`. Open: check the channel list against
  `iio_buffer_get_scan_element`; build a mask with exactly these channels, `iio_buffer_open`, destroy
  the mask; create the converter from `iio_buffer_stream_get_channels_mask()`; blocks of
  `samples * step` bytes; receive: enqueue all, start. One internal "wait for block i" implements the
  three wait policies and maps `-ETIMEDOUT`, `-EBUSY`, `-EAGAIN` to "no frame"; read re-queues a block
  that libiio reports as not enqueued (`-EPERM`) and returns "no frame".
- [ ] **Step 5:** Run `ctest --test-dir build-t2 -R 'test_stream' --output-on-failure` and the whole suite.
  Expected: PASS, no sanitizer or leak report; `test_stream` prints libiio's one
  `Rx data file not found` line. `nm -D build-t2/libiio-conv.so | grep -c ' T iio_conv_stream_'` → `14`.
- [ ] **Step 6:** `rm -rf build-t2`. Commit `feat: stream layer on explicit blocks: open, receive, flush, close`.

### Task 3: Stream layer, part 2: transmit

**Wave 0. Depends on:** Task 2. **Build directory:** `build-t3`.

**Files:**
- Modify: `src/stream.c`, `README.md` (section "Stream layer")
- Test: `tests/test_stream.c`, `tests/test_stream_fake.c`

**Interfaces:**
- Consumes: Task 2.
- Produces: working `iio_conv_stream_write`, `iio_conv_stream_write_typed`, the transmit part of `iio_conv_stream_flush`, the transmit drain in `iio_conv_stream_close`, all as documented in `iio-conv-stream.h`.

- [ ] **Step 1:** Add to `tests/test_stream.c` (`emu-tx` buffer 0, channels `{voltage0_i, voltage0_q}`, spec
  `{I16, COLUMNS, NONE, complex_iq, saturate}`, 4 samples; `A` = `100,1,-200,2,300,3,-400,4`,
  `B` = `11..18`, `C` = `21..28` as int16; "file" = `emu-tx_buf0.bin` read as int16; on `emu:` a
  frame reaches the file when its block is dequeued):
  - `tx_one_block` (R2, R10): open → step 4, output; write(A) → 1 and the file has 0 bytes; flush →
    1 and file `== A`; flush again → 1; write(B), write(C) → 1 and file `== B`; close → file `== C`.
  - `tx_two_blocks` (R10): 2 blocks; after write(A) and write(B) the file has 0 bytes; after write(C)
    file `== A`; after close file `== C` (last frame not lost).
  - `tx_partial` (partial frames): write(A, 2 samples), flush → file is `100,1,-200,2` (8 bytes).
    `write(.., 0)` and `write(.., 5)` → `-EINVAL`.
  - `tx_real_on_complex` (a complex array with a zero imaginary part arrives real): after a complex frame, `write_typed(R, 4, IIO_CONV_I16,
    false)` with `R = 7,-8,9,-10`, flush → file `7,0,-8,0,9,0,-10,0`.
  - `tx_types` (R2): `write_typed` F64 complex `100.4,-200.6, 32767.9,-40000, 0.5,1.5,
    2.5,-0.5` → file `100,-201,32767,-32768,0,2,2,0`; I32 complex `40000,-40000, 5,-5, 32768,-32769,
    0,1` → file `32767,-32768,5,-5,32767,-32768,0,1`.
  - `tx_real_stream`: `complex_iq = false`, 2 channels, write(A, 4) (column-major, 2 columns) → file
    `100,300,1,3,-200,-400,2,4`; `write_typed(.., IIO_CONV_I16, true)` → `-EINVAL`.
  - `tx_scaled`: spec `{F64, FULLSCALE, complex_iq}`: `write_typed(.., IIO_CONV_I16, true)` → `-EINVAL`;
    write F64 `1,-1, 0.5,-0.5, 0.25,0, 0,0` → file `32767,-32768,16384,-16384,8192,0,0,0`.
  - `tx_cyclic` (R9): open(4 blocks, cyclic) → `nb_blocks == 1`; write(A) → 1; write(A) → `-EBUSY`;
    flush → 1; close returns in < 1 s and the file exists with 0 bytes (never dequeued).
  - `tx_raw` (R12; `emu-multi` buffer 1, `{voltage0, voltage1}` out, not complex, 2 samples):
    `{F64, FULLSCALE}` columns `[0.5 -0.5]`, `[1 -1]` → `emu-multi_buf1.bin` words
    `4000 7ff0 c000 8000`; `{I16, saturate}` columns `[32767 1]`, `[-32768 -1]` → `7ff0 8000 0010 fff0`;
    the same with `raw` → `7fff 8000 0001 ffff`.
  - `rx_and_tx` (R22): one receive and one transmit stream open at once on the same context; read,
    write, read, flush all succeed with the values above. `iio_conv_stream_read(tx, ..) == -EBADF`.
- [ ] **Step 2:** Add to `tests/test_stream_fake.c` (`emu-tx`, 4 samples, 2 blocks):
  - `tx_order` (R10): log after open `"OBB"`; after write 1 `"OBBES"` with `tx_bytes == 16`,
    `tx_cyclic == false`; after write 2 `"OBBESE"`; write 3 and 4 each add `"DE"`; write 5 → 0, adds
    `"d"`, `tx_data` unchanged; flush → 0; cleared log after close: `"dXsFFXC"`.
  - `tx_drain`: `tx_frames = 100`; write(A, 4), write(A, 2) → `tx_bytes == 8`; cleared log after
    close: `"DDXsFFXC"`. Variant: write, flush → 1 (log `"D"`), close → `"XsFFXC"`.
  - `tx_cyclic` (R9): 4 blocks, cyclic: log `"OB"`; write → `"OBeS"`, `tx_cyclic == true`; second
    write `-EBUSY`; cleared log after close `"XsFXC"` (no `D`, no `d`).
  - `tx_coupled`: `fake.couple = "voltage1_i"`, 1 block → step 6; write(A) → `tx_data` is
    `64 00 01 00 00 00 38 ff 02 00 00 00 2c 01 03 00 00 00 70 fe 04 00 00 00` (unlisted channel zero).
  - `tx_timeout` (R8): `tx_frames = 0`, 1 block, `set_timeout(s, 30)`: write → 1; second write → 0
    after ≥ 0.030 s and < 1 s.
  - `close_bounded` (a destructor never blocks without bound): `tx_frames = 0`, 2 blocks,
    `iio_context_set_timeout(ctx, -1)`, two writes → 1 each; `fake_clear_log()`; close returns after
    ≥ 0.9 s and < 2 s and the log starts with `d` (the log is full: its end is not asserted; the
    sanitizer proves the blocks were freed). The same with the context timeout left at its default:
    close returns in < 0.5 s with the cleared log `"dXsFFXC"`.
- [ ] **Step 3:** Run `cmake -S . -B build-t3 -G Ninja -DIIO_CONV_SANITIZE=ON && cmake --build build-t3 &&
  ctest --test-dir build-t3 -R test_stream --output-on-failure`. Expected: FAIL (writes return `-ENOSYS`).
- [ ] **Step 4:** Implement. Write: wait for the block at the write position only if it is in flight;
  for a real source on a complex stream zero the frame bytes, then convert the I channels with a
  converter built from channels 0, 2, 4, ..; enqueue `nb_samples * step` bytes with the cyclic flag;
  start on the first enqueue. Converters live in a `[10][2]` table. Blocks of an output stream are
  zeroed at open. Close drains as specified in `iio-conv-stream.h`: under the wait policy of the stream. (This step and the case `close_bounded` of Step 2 asked for a 1000 ms bound inside the close when the context timeout is infinite; the stream layer as merged has `tx_close_policy` instead and no such bound. The bound a MATLAB destructor needs is in the gateway, Task 21.)
- [ ] **Step 5:** Run the whole suite in `build-t3`, also with `IIO_CONV_FORCE=scalar`. Expected: PASS, no
  sanitizer or leak report.
- [ ] **Step 6:** `README.md`: section "Stream layer" (15-line receive and transmit example, the three wait
  policies, what `emu:` shows and does not show). `rm -rf build-t3`.
- [ ] **Step 7:** Commit `feat: stream layer transmit: typed writes, cyclic, drain on close`.

### Task 4a: Mock MATLAB runtime

**Wave 0. Depends on:** Task 3. **Build directory:** `build-mex` (plain compiler calls; CMake arrives in Task 4b).

**Files:**
- Create (mock): `tests/mxmock/matrix.h`, `tests/mxmock/mex.h`, `tests/mxmock/mxmock.h`, `tests/mxmock/mxmock.c`
- Test: `tests/mex/test_mxmock.c`, `tests/mex/mx_conformance.c`

**Interfaces:**
- Consumes: `tests/test_util.h` (`CHECK`, `TEST_MAIN_RETURN`); the real `matrix.h` and `libmx.so` of `/usr/local/MATLAB/R2025b` (for the conformance run only).
- Produces: `tests/mxmock/mxmock.h` exactly as in "Shared contracts" and the mock runtime `tests/mxmock/` (the function subset listed below that header). No gateway code.

- [ ] **Step 1:** Write `tests/mex/mx_conformance.c` (includes only `"matrix.h"` and `"test_util.h"`, calls no `mex*` function and nothing mock-specific). Checks: for each of the ten numeric classes `mxCreateUninitNumericMatrix(4, 2, cls, mxREAL)` has `mxGetElementSize` 8, 4, 1, 1, 2, 2, 4, 4, 8, 8 (double, single, int8, uint8, int16, uint16, int32, uint32, int64, uint64), `mxGetM == 4`, `mxGetN == 2`, and the `mxCOMPLEX` one has twice the element size, `mxIsComplex`, 8 elements, non-NULL `mxGetData`; a 0x0 double is `mxIsEmpty` with `mxGetData == NULL`; a `uint64` scalar keeps `0xFFFFFFFFFFFFFFFF` and has class name `"uint64"`; `mxCreateString("")` is a 0x0 char and `mxArrayToUTF8String` of it is `""`; `mxCreateString("voltage0 \xC2\xB5V \xE2\x82\xAC")` is 1-by-13, element size 2, not `mxIsNumeric`, and converts back to the same bytes; `mxCreateString("caf\xE9!")` has 5 elements, the fourth is `0xFFFD`; `mxArrayToUTF8String(double scalar) == NULL`; `mxGetScalar(3.5) == 3.5`; a logical scalar `true` is `mxIsLogical`, not `mxIsNumeric`, element size 1, `mxIsLogicalScalarTrue`; a fresh 1-by-2 cell has `mxGetCell(c, 0) == NULL`; a 1-by-1 struct with fields `a`, `b` has 2 fields, field name 1 is `"b"`, `mxGetField` of an unset or unknown field is `NULL`, and after `mxSetFieldByNumber(s, 0, 0, c)` returns `c`; `mxDestroyArray(NULL)`, `mxFree(NULL)` are no-ops and `mxGetData(NULL) == NULL`.


- [ ] **Step 2:** Write `tests/mex/test_mxmock.c`. It defines its own `mexFunction` that switches on the text of `prhs[0]`, and a helper `aborts(fn)` that forks, runs `fn` in the child and returns true iff the child died by `SIGABRT`. Checks:
  - `"ok"` (sets `plhs[0]` to a double scalar): `mock_call` returns 0, `mock_err_id()` is `""`, the output is usable after the call, `mock_live()` grew by 1, and is back after `mxDestroyArray`.
  - `"raise"` (creates one array and one `mxMalloc` block, then `mexErrMsgIdAndTxt("iio:EINVAL", "bad %d", 7)`): returns 1, id `"iio:EINVAL"`, message `"bad 7"`, `plhs[0] == NULL`, `mock_autofreed()` grew by 2.
  - `"leak"` (returns one array, leaves one more array and one `mxMalloc` block): returns 0, `mock_autofreed()` grew by 2, the returned array is intact.
  - `"warn"`: `mock_warn_count()` grew by 1, `mock_warn_id()` is the id passed, `mock_output()` contains the `mexPrintf` text.
  - lock and exit: after `mexLock` twice `mock_is_locked()`; `mock_clear_mex()` does not run the registered exit function; after two `mexUnlock` it runs it exactly once and a second `mock_clear_mex()` does not; `mock_exit_matlab()` runs a registered exit function even when locked.
  - `mxCreateUninitNumericMatrix` data bytes are all `0xA5`; `mxCreateNumericMatrix` data is zero; `plhs[0]` is non-NULL on entry to `mexFunction`.
  - each of these aborts: an identifier `"iio:22"`; `mexErrMsgIdAndTxt` outside `mock_call`; `mxDestroyArray(prhs[0])`; `mxSetCell` of the same array into two cells; `mxSetCell` of a `prhs` element; `mxGetM` of a destroyed array; `mxIsChar(NULL)`; `plhs[0] = (mxArray *)prhs[1]`; the same array in `plhs[0]` and `plhs[1]`; `nlhs == 2` with only `plhs[0]` set; `mxGetCell` beyond the end; `mxSetFieldByNumber` with a bad field number; `mxCreateStructMatrix` with field name `"in_voltage0-voltage1_scale"`; `mxGetLogicals` of a double array; `mxDestroyArray` of a cell member; `mxFree` of a `malloc` pointer or of a block twice (under ASan the child dies by the sanitizer instead: accept any abnormal exit for these two).
  - allocation failure and special arrays: command `"three"` (creates three arrays, returns the third): after `mock_fail_alloc(2)`, `mock_call` returns 1, `mock_err_id()` is `"MATLAB:nomem"`, `mock_alloc_failed()` is true, `mock_autofreed()` and `mock_live()` are unchanged; the next `mock_call` succeeds (it fires once). `mock_string_object()`: `mxIsChar` false, `strcmp(mxGetClassName(a), "string") == 0`, `mxArrayToUTF8String(a) == NULL`, `mxGetData(a) == NULL`. `mock_sparse_double(2, 2)`: `mxIsSparse`, class id `mxDOUBLE_CLASS`. `mock_nd_array(mxINT16_CLASS, mxCOMPLEX, 4, 1, 2)`: `mxGetNumberOfDimensions` 3, `mxGetM` 4, `mxGetN` 2, `mxGetNumberOfElements` 8, `mxIsComplex`, non-NULL `mxGetData`; a matrix from `mxCreateNumericMatrix(4, 2, ..)` has `mxGetNumberOfDimensions` 2. `mock_char_array(3, 1, 1, "abc")`: `mxIsChar`, 2 dimensions, `mxGetM` 3, `mxGetN` 1, element size 2, elements `'a'`..`'c'`; `mock_char_array(1, 3, 2, "abcdef")`: 3 dimensions, `mxGetM` 1, `mxGetN` 6; `mock_char_array(0, 3, 1, "")`: `mxIsEmpty`, `mxGetN` 3, `mxGetData == NULL`.
- [ ] **Step 3:** Run the three build lines of Step 5. Expected: FAIL to compile (`tests/mxmock/matrix.h` missing; the third line builds and passes: it uses the real `libmx`).
- [ ] **Step 4:** Implement `tests/mxmock/`. `matrix.h`: the types `mxArray` (opaque), `mwSize`, `mwIndex`, `mwSignedIndex`, `mxChar` (`uint16_t`), `mxLogical` (`bool`), `mxClassID` with all 19 enumerators in MATLAB's order (`mxUNKNOWN_CLASS = 0` ... `mxOBJECT_CLASS = 18`), `mxComplexity`, `mxDouble` ... `mxUint64`, the ten `mxComplex*` structs, `MX_HAS_INTERLEAVED_COMPLEX 1`, `MX_TARGET_API_VER 0x08000000`, and prototypes copied from the real header for exactly: the `mx*` functions listed below `mxmock.h` in "Shared contracts". `mex.h`: `#include <stdio.h>` **before** anything else, then `"matrix.h"`, `mex_exit_fn`, the eight `mex*` functions of that list, and `#define printf mexPrintf` last (as the real header does). `mxmock.c` behaviour beyond what Steps 1 and 2 pin down: `mexErrMsgIdAndTxt` leaves by `longjmp` to `mock_call`; every `mx*` function aborts with a one-line message on a `NULL` (except the three no-op cases), destroyed or foreign array; a destroyed array's header stays allocated and reachable from a list (so later use is reported and LeakSanitizer stays quiet) while its data is really freed; at the end of every `mock_call` all arrays and `mxMalloc` blocks created during the call and not reachable from `plhs` are released and counted in `mock_autofreed()`; `mxSetCell`/`mxSetFieldByNumber` transfer ownership to the container and destroying a container destroys its members; a weak `mexFunction` that aborts, so programs that never call `mock_call` link without a gateway; `mock_fail_alloc`, `mock_alloc_failed`, `mock_string_object`, `mock_sparse_double`, `mock_nd_array` and `mock_char_array` as documented in `mxmock.h`; before it calls `mexFunction`, `mock_call` fills 32 KiB of stack below its own frame with `0xA5` (a `noinline` function with a `volatile` array), so that an uninitialised local of the gateway reads as `0xA5` in every call (effective on the real stack; under AddressSanitizer only with `ASAN_OPTIONS=detect_stack_use_after_return=0`, which `tests/mex/CMakeLists.txt` sets for `test_dispatch`).
- [ ] **Step 5:** Build and run **[RUN]** (the third line was run with a stand-in source):

  ```sh
  mkdir -p build-mex; M=/usr/local/MATLAB/R2025b
  F="-std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Werror=implicit-function-declaration -Itests/mxmock -Itests"
  gcc $F tests/mex/test_mxmock.c tests/mxmock/mxmock.c -o build-mex/test_mxmock && build-mex/test_mxmock
  gcc $F tests/mex/mx_conformance.c tests/mxmock/mxmock.c -o build-mex/mx_conformance_mock && build-mex/mx_conformance_mock
  gcc -std=gnu11 -DMATLAB_DEFAULT_RELEASE=R2018a -isystem $M/extern/include -Itests tests/mex/mx_conformance.c \
      -L$M/bin/glnxa64 -lmx -Wl,--allow-shlib-undefined -o build-mex/mx_conformance_real \
      && LD_LIBRARY_PATH=$M/bin/glnxa64:$M/sys/os/glnxa64 build-mex/mx_conformance_real
  ```

  Expected: each program exits 0 with no sanitizer report. `rm -rf build-mex`.
- [ ] **Step 6:** Commit `test: mock MATLAB runtime with its self-test and the conformance test against real libmx`.

### Task 4b: Build skeleton, gateway contracts, stub registry

**Wave 0. Depends on:** Task 4a. **Build directory:** `build-mex`.

**Files:**
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`
- Create: `matlab/CMakeLists.txt`, `tests/mex/CMakeLists.txt`
- Create (contracts, final): `matlab/mex/handles.h`, `matlab/mex/iio_mex.h`, `matlab/mex/mx_util.h`
- Create (skeletons): `matlab/mex/iio_mex.c`, `matlab/mex/handles.c`, `matlab/mex/mx_util.c`, `matlab/mex/registry.c`, `matlab/mex/cmd_{toplevel,context,attr,device,channel,buffer,block,stream,event,lowlevel,conv,test}.c`, `tests/mex/mex_test.c` (a comment only; filled by Task 6)
- Create: `matlab/tools/gen_enums.py`, `matlab/tools/check_mex_exports.sh`, `tests/mex/mex_loadcheck.c`
- Test: `tests/mex/test_enums.c` (and, now through CTest, the two tests of Task 4a)

**Interfaces:**
- Consumes: targets `iio-conv-static`, `libiio1` and variable `LIBIIO_INCLUDE_DIR` of the existing `CMakeLists.txt`; `tests/test_util.h`; the mock runtime and its two tests (Task 4a); the "Command reference" (for the stub tables).
- Produces:
  - CMake options `IIO_CONV_BUILD_MATLAB` (OFF), `IIO_CONV_MEX_SANITIZE` (ON), `IIO_CONV_API_STRICT` (OFF); variables `IIO_MEX_GROUPS`, `IIO_FIXTURE_XML`; targets `iio-mex-mock-<part>`, `iio-mex-real-<part>` (`<part>` = `core`, `registry`, `test`, or a group), `iio-mex-testrt`, `iio_mex` (output `matlab/iio_mex.mexa64`); compile definition `IIO_MEX_TEST_HOOKS` when `IIO_CONV_BUILD_TESTS` is on; CTest label `mex`.
  - Rule: every `tests/mex/test_*.c` is built and registered automatically, with the fixture XML as `argv[1]`; the groups in its `MT_GROUPS(...)` line (at line start) are the `cmd_<group>.c` objects it links.
  - `handles.h`, `iio_mex.h`, `mx_util.h` exactly as in "Shared contracts".
  - The complete command registry: every command of the "Command reference" is registered in its file with its final name and argument limits, bound to `gw_stub`. Later tasks replace `gw_stub` by their function and never add, remove or rename an entry.
  - `gw_enum_table()` / `gw_enum_name()` generated from the pinned `iio.h` at build time.

- [ ] **Step 1:** Edit `CMakeLists.txt`: after the existing options add

  ```cmake
  option(IIO_CONV_BUILD_MATLAB "Build matlab/iio_mex.mexa64 (needs -DMatlab_ROOT_DIR=<MATLAB root>)" OFF)
  option(IIO_CONV_MEX_SANITIZE "ASan and UBSan for the gateway's mock-based tests" ON)
  option(IIO_CONV_API_STRICT "check_api.py: every command of matlab/api-map.tsv must exist" OFF)
  set(IIO_MEX_GROUPS toplevel context attr device channel buffer block stream event lowlevel conv)
  set(IIO_FIXTURE_XML ${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/iio_fixture.xml)
  ```

  and replace the `if(IIO_CONV_BUILD_TESTS) enable_testing() add_subdirectory(tests) endif()` block with

  ```cmake
  if(IIO_CONV_BUILD_TESTS)
  	enable_testing()
  endif()
  if(IIO_CONV_WITH_LIBIIO AND (IIO_CONV_BUILD_TESTS OR IIO_CONV_BUILD_MATLAB))
  	add_subdirectory(matlab)
  endif()
  if(IIO_CONV_BUILD_TESTS)
  	add_subdirectory(tests)
  endif()
  ```

  In `tests/CMakeLists.txt`, inside the existing `if(IIO_CONV_WITH_LIBIIO)` block, add `add_subdirectory(mex)`. In `.gitignore` add `matlab/*.mexa64`.
- [ ] **Step 2:** Write `matlab/CMakeLists.txt` exactly as below (**[RUN]**, except the `-Werror` line and the `tests` include directory, which `cmd_test.c` needs for `fake_backend.h`; the two blocks marked `Task 7` are added by Task 7; leave them out now):

  ```cmake
  # The iio_mex gateway. Every source group is compiled twice, as OBJECT libraries, so that
  # one group can be built and tested while another is still being written:
  #   iio-mex-mock-<part>   against tests/mxmock            (G1; ASan + UBSan)
  #   iio-mex-real-<part>   against the real MATLAB headers (G2; linked into iio_mex.mexa64)
  # <part> is "core", "registry", "test" or a command group of IIO_MEX_GROUPS.
  set(IIO_MEX_PARTS core ${IIO_MEX_GROUPS} registry)
  set(IIO_MEX_DEFS MATLAB_MEX_FILE)
  if(IIO_CONV_BUILD_TESTS)
  	list(APPEND IIO_MEX_PARTS test)
  	list(APPEND IIO_MEX_DEFS IIO_MEX_TEST_HOOKS)
  endif()

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(IIO_MEX_ENUMS_C ${CMAKE_CURRENT_BINARY_DIR}/iio_mex_enums.c)
  add_custom_command(OUTPUT ${IIO_MEX_ENUMS_C}
  	COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_enums.py
  		${LIBIIO_INCLUDE_DIR}/iio/iio.h ${IIO_MEX_ENUMS_C}
  	DEPENDS tools/gen_enums.py ${LIBIIO_INCLUDE_DIR}/iio/iio.h VERBATIM)
  add_custom_target(iio-mex-enums DEPENDS ${IIO_MEX_ENUMS_C})

  function(iio_mex_objects variant runtime)
  	foreach(part ${IIO_MEX_PARTS})
  		if(part STREQUAL core)
  			set(src mex/iio_mex.c mex/handles.c mex/mx_util.c ${IIO_MEX_ENUMS_C})
  		elseif(part STREQUAL registry)
  			set(src mex/registry.c)
  		else()
  			set(src mex/cmd_${part}.c)
  		endif()
  		set(tgt iio-mex-${variant}-${part})
  		add_library(${tgt} OBJECT ${src})
  		add_dependencies(${tgt} iio-mex-enums)
  		target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mex ${PROJECT_SOURCE_DIR}/tests)
  		target_compile_definitions(${tgt} PUBLIC ${IIO_MEX_DEFS})
  		target_compile_options(${tgt} PRIVATE -Werror)   # gate G2 of wave 1: a warning fails the build
  		target_link_libraries(${tgt} PUBLIC iio-conv-static ${runtime} ${CMAKE_DL_LIBS})
  	endforeach()
  endfunction()

  if(IIO_CONV_BUILD_TESTS)
  	# Sanitizers for the mock build only: the same build directory also makes the real MEX.
  	add_library(iio-mex-mockrt INTERFACE)
  	target_include_directories(iio-mex-mockrt INTERFACE ${PROJECT_SOURCE_DIR}/tests/mxmock)
  	if(IIO_CONV_MEX_SANITIZE AND NOT IIO_CONV_SANITIZE)
  		target_compile_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  		target_link_options(iio-mex-mockrt INTERFACE -fsanitize=address,undefined)
  	endif()
  	iio_mex_objects(mock iio-mex-mockrt)

  	# Task 7: G4
  	set(strict)
  	if(IIO_CONV_API_STRICT)
  		set(strict --strict)
  	endif()
  	add_test(NAME mex_api_map COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_api.py
  		--iio-h ${LIBIIO_INCLUDE_DIR}/iio/iio.h ${strict})
  	add_test(NAME mex_tools_selftest COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_tools.py)
  	set_tests_properties(mex_api_map mex_tools_selftest PROPERTIES LABELS mex)
  endif()

  if(IIO_CONV_BUILD_MATLAB)
  	if(IIO_CONV_SANITIZE)
  		message(FATAL_ERROR "IIO_CONV_BUILD_MATLAB cannot be combined with IIO_CONV_SANITIZE: "
  			"MATLAB cannot load a sanitized MEX file. The mock tests are sanitized anyway.")
  	endif()
  	find_package(Matlab REQUIRED)   # needs -DMatlab_ROOT_DIR=<root>; MATLAB is not started
  	add_library(iio-mex-realrt INTERFACE)
  	target_include_directories(iio-mex-realrt SYSTEM INTERFACE ${Matlab_INCLUDE_DIRS})
  	# R2018a = interleaved complex API. (R2025b is not a name matrix.h knows: it does not compile.)
  	target_compile_definitions(iio-mex-realrt INTERFACE MATLAB_DEFAULT_RELEASE=R2018a)
  	# -fexceptions: mexErrMsgIdAndTxt leaves by throwing a C++ exception through these C frames.
  	# -fvisibility=default: the project preset is hidden, which would drop mexFunction.
  	target_compile_options(iio-mex-realrt INTERFACE -fexceptions -fvisibility=default)
  	iio_mex_objects(real iio-mex-realrt)

  	set(objs)
  	foreach(part ${IIO_MEX_PARTS})
  		list(APPEND objs $<TARGET_OBJECTS:iio-mex-real-${part}>)
  	endforeach()
  	# Adds c_mexapi_version.c, the export version script, libmex and libmx.
  	matlab_add_mex(NAME iio_mex MODULE SRC ${objs} R2018a LINK_TO iio-conv-static ${CMAKE_DL_LIBS})
  	target_link_options(iio_mex PRIVATE -Wl,--no-undefined)
  	# matlab/iio_mex.mexa64 by default, for addpath('matlab'). Every build directory writes that
  	# one file: two full builds at the same time (or for two MATLAB releases) each need their own.
  	set(IIO_MEX_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory that receives iio_mex.mexa64")
  	set_target_properties(iio_mex PROPERTIES
  		C_VISIBILITY_PRESET default
  		LIBRARY_OUTPUT_DIRECTORY ${IIO_MEX_OUTPUT_DIR})

  	if(IIO_CONV_BUILD_TESTS)
  		set(mlibs "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64:${Matlab_ROOT_DIR}/sys/os/glnxa64")
  		add_test(NAME mex_exports COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mex_exports.sh $<TARGET_FILE:iio_mex>)
  		add_executable(mex_loadcheck ${PROJECT_SOURCE_DIR}/tests/mex/mex_loadcheck.c)
  		target_link_libraries(mex_loadcheck PRIVATE ${CMAKE_DL_LIBS})
  		add_test(NAME mex_loadcheck COMMAND mex_loadcheck $<TARGET_FILE:iio_mex>)
  		set_tests_properties(mex_loadcheck PROPERTIES ENVIRONMENT "${mlibs}")
  		set_tests_properties(mex_exports mex_loadcheck PROPERTIES LABELS mex)
  		# Task 7: G3
  		add_test(NAME mex_mlint COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
  		# The lint tests are exempt from the 4 s rule: mlint of R2023b needs about 0.12 s per file.
  		set_tests_properties(mex_mlint PROPERTIES ENVIRONMENT "MATLAB_ROOT=${Matlab_ROOT_DIR}" LABELS mex TIMEOUT 120)
  	endif()
  endif()
  ```

- [ ] **Step 3:** Write `tests/mex/CMakeLists.txt` exactly as below **[RUN]**:

  ```cmake
  # Gateway tests on the mock MATLAB runtime (G1). All tests here carry the label "mex".
  add_library(iio-mex-testrt STATIC ../mxmock/mxmock.c mex_test.c $<TARGET_OBJECTS:iio-mex-mock-core>)
  target_include_directories(iio-mex-testrt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(iio-mex-testrt PUBLIC iio-mex-mock-core)

  # Every tests/mex/test_*.c is one test executable, run with the fixture XML as argv[1].
  # Its line
  #     MT_GROUPS(&gw_group_context, &gw_group_attr);
  # names the command groups it needs: exactly those cmd_<group>.c objects are linked, so a
  # test builds while other groups are still being written. Names that are not gateway
  # groups (groups the test defines itself) are ignored here.
  file(GLOB mex_tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
  set(names)
  foreach(src ${mex_tests})
  	get_filename_component(name ${src} NAME_WE)
  	file(STRINGS ${src} line REGEX "^MT_GROUPS\\(" LIMIT_COUNT 1)
  	string(REGEX MATCHALL "gw_group_[a-z]+" syms "${line}")
  	add_executable(${name} ${src})
  	foreach(sym ${syms})
  		string(REPLACE "gw_group_" "" g ${sym})
  		if(TARGET iio-mex-mock-${g})
  			target_sources(${name} PRIVATE $<TARGET_OBJECTS:iio-mex-mock-${g}>)
  		endif()
  	endforeach()
  	target_link_libraries(${name} PRIVATE iio-mex-testrt)
  	add_test(NAME ${name} COMMAND ${name} ${IIO_FIXTURE_XML})
  	list(APPEND names ${name})
  endforeach()

  # The mock agrees with the real libmx: one source, both runtimes.
  add_executable(mx_conformance_mock mx_conformance.c ../mxmock/mxmock.c)
  target_include_directories(mx_conformance_mock PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(mx_conformance_mock PRIVATE iio-mex-mockrt)
  add_test(NAME mx_conformance_mock COMMAND mx_conformance_mock)
  list(APPEND names mx_conformance_mock)

  if(IIO_CONV_BUILD_MATLAB)
  	add_executable(mx_conformance_real mx_conformance.c)
  	target_include_directories(mx_conformance_real PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  	target_include_directories(mx_conformance_real SYSTEM PRIVATE ${Matlab_INCLUDE_DIRS})
  	target_compile_definitions(mx_conformance_real PRIVATE MATLAB_DEFAULT_RELEASE=R2018a)
  	target_link_libraries(mx_conformance_real PRIVATE ${Matlab_MX_LIBRARY})
  	target_link_options(mx_conformance_real PRIVATE -Wl,--allow-shlib-undefined)
  	add_test(NAME mx_conformance_real COMMAND mx_conformance_real)
  	set_tests_properties(mx_conformance_real PROPERTIES ENVIRONMENT
  		"LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64:${Matlab_ROOT_DIR}/sys/os/glnxa64")
  	list(APPEND names mx_conformance_real)
  	if(TARGET test_slice)
  		# libiio bound to MATLAB's bundled libxml2 and libzstd instead of the system ones.
  		add_test(NAME mex_bundled_libs COMMAND test_slice ${IIO_FIXTURE_XML})
  		set_tests_properties(mex_bundled_libs PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64")
  		list(APPEND names mex_bundled_libs)
  	endif()
  endif()
  set_tests_properties(${names} PROPERTIES LABELS mex TIMEOUT 30)
  # The dispatcher's own frame on the real stack, which the mock fills with 0xA5 before every
  # call: an uninitialised field of struct gw_call then fails this test instead of reading as 0
  # from AddressSanitizer's fake stack.
  set_tests_properties(test_dispatch PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_return=0")
  ```

- [ ] **Step 4:** Write the three headers exactly as in "Shared contracts". Write the skeleton sources:
  - `handles.c` and `mx_util.c` contain only their `#include`; `tests/mex/mex_test.c` holds one comment line.
  - Each `cmd_<group>.c` is `#include "mx_util.h"`, one `static const struct gw_cmd cmds[]` with one line per command of its "Command reference" table in table order, `GW_CMD("<name>", gw_stub, <min_rhs>, <max_rhs>, <max_lhs>),` or `GW_XCMD(...)` for the commands marked X (a table row that names several commands gives one line each), and `const struct gw_group gw_group_<group> = GW_GROUP("<group>", cmds);`. `cmd_test.c` is wrapped in `#ifdef IIO_MEX_TEST_HOOKS`.
  - `registry.c` defines `gw_groups[]` = `&gw_group_mex`, the eleven groups in `IIO_MEX_GROUPS` order, `&gw_group_test` under `#ifdef IIO_MEX_TEST_HOOKS`, `NULL`.
  - `iio_mex.c` skeleton: `gw_fail` (`vsnprintf` into `c->err_msg`, returns `err`), `gw_stub`, `gw_group_mex` with its two `GW_XCMD` lines bound to `gw_stub`, a `mexFunction` that raises `iio:ENOSYS` with the text `"not implemented"`, and

  ```c
  /* libiio's exit handler calls xmlCleanupParser(); inside MATLAB that is MATLAB's own
   * libxml2, which libxml2 documents as unsafe while the host still uses the library.
   * Keep libiio loaded until the process ends, so "clear mex" can never trigger it. */
  __attribute__((constructor)) static void pin_libiio(void)
  {
  	(void)dlopen("libiio.so.1", RTLD_NOW | RTLD_NODELETE);
  }
  ```

- [ ] **Step 5:** Write `tests/mex/test_enums.c` (no `MT_GROUPS` line): `gw_enum_name(GW_ENUM_CHAN_TYPE, IIO_VOLTAGE)` is `"IIO_VOLTAGE"`; `(GW_ENUM_MODIFIER, IIO_MOD_Q)` is `"IIO_MOD_Q"`; `(GW_ENUM_EVENT_TYPE, IIO_EV_TYPE_THRESH)` is `"IIO_EV_TYPE_THRESH"`; `(GW_ENUM_EVENT_DIRECTION, IIO_EV_DIR_FALLING)` is `"IIO_EV_DIR_FALLING"`; `(GW_ENUM_HWMON_CHAN_TYPE, HWMON_TEMP)` is `"HWMON_TEMP"`; value `123456` gives `NULL`; `gw_enum_table(GW_ENUM_EVENT_DIRECTION, &n)` gives `n == 7` with entry 0 `{"IIO_EV_DIR_EITHER", IIO_EV_DIR_EITHER}`; `gw_enum_table(GW_ENUM_HWMON_CHAN_TYPE, &n)` gives `n == 10`; the five counts sum to 129. Constants that M code repeats (`iio.internal.params`, `iio.Context`), pinned so that drift in `iio.h` fails the build: `LEVEL_NOLOG == 1`, `LEVEL_ERROR == 2`, `LEVEL_WARNING == 3`, `LEVEL_INFO == 4`, `LEVEL_DEBUG == 5`, `IIO_TIMEOUT_BACKEND == 0`, `IIO_TIMEOUT_INFINITE == -1`, `IIO_TIMEOUT_NONBLOCK == INT_MIN`.
- [ ] **Step 6:** Write `matlab/tools/gen_enums.py <iio.h> <out.c>`: strip comments, find the bodies of the five enums `iio_chan_type`, `iio_modifier`, `iio_event_type`, `iio_event_direction`, `hwmon_chan_type` (the order of `enum gw_enum`), take only the enumerator **names** (text before `=` or `,`), and emit a C file that includes `"mx_util.h"`, holds one `static const struct gw_enum_entry` table per enum with entries `{ "NAME", NAME }` in header order (the compiler supplies the values), and defines `gw_enum_table` and `gw_enum_name`. Exit non-zero with a message if an enum is not found. **[RUN]**: the pinned header yields 129 entries and the output compiles against both runtimes.
- [ ] **Step 7:** Write `matlab/tools/check_mex_exports.sh` exactly as below **[RUN]** and `chmod +x` it:

  ```sh
  #!/bin/sh
  # G2: the MEX file exports exactly mexFunction and mexfilerequiredapiversion (version node
  # MEX), imports only R2018a-API (_800) MATLAB symbols, needs libiio.so.1, and has
  # libiio-conv linked in statically.
  set -eu
  mex=$1
  fail=0
  n=$(nm -D --defined-only "$mex" | grep -c -E ' T (mexFunction|mexfilerequiredapiversion)@@MEX$' || true)
  [ "$n" = 2 ] || { echo "FAIL: expected mexFunction@@MEX and mexfilerequiredapiversion@@MEX, found $n"; fail=1; }
  extra=$(nm -D --defined-only "$mex" | awk '$2 == "T" && $3 !~ /^(mexFunction|mexfilerequiredapiversion)@@MEX$/ {print $3}')
  [ -z "$extra" ] || { echo "FAIL: unexpected exported functions: $extra"; fail=1; }
  old=$(nm -D --undefined-only "$mex" | awk '$2 ~ /^m(x|ex)[A-Z]/ && $2 !~ /_800$/ {print $2}')
  [ -z "$old" ] || { echo "FAIL: non-R2018a MATLAB imports: $old"; fail=1; }
  readelf -d "$mex" | grep -q 'NEEDED.*libiio\.so\.1' || { echo "FAIL: libiio.so.1 is not a dependency"; fail=1; }
  if readelf -d "$mex" | grep -q 'NEEDED.*libiio-conv'; then echo "FAIL: libiio-conv must be linked statically"; fail=1; fi
  [ $fail = 0 ] && echo "ok: $mex"
  exit $fail
  ```

- [ ] **Step 8:** Write `tests/mex/mex_loadcheck.c` (plain C, no sanitizer, argv[1] = MEX path): `dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)` must succeed; `dlsym` finds `mexFunction` and `mexfilerequiredapiversion`; calling the latter gives `target_api_ver == 0x08000000`; print the `r-xp` lines of `/proc/self/maps` whose path contains `/libxml2`, `/libzstd` or `/libiio.so`; `dlclose` must succeed; afterwards `/proc/self/maps` must still contain `/libiio.so.1` (the pin). Exit 0 only if all hold.

- [ ] **Step 9:** Run the build line of "Gates" with `B=build-mex` and `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: `100% tests passed, 0 tests failed out of 6` under `-L mex` (`mex_exports`, `mex_loadcheck`, `test_enums`, `test_mxmock`, `mx_conformance_mock`, `mx_conformance_real`); `matlab/iio_mex.mexa64` exists; `ctest --test-dir build-mex` (no label) still passes all C library tests (Part 1, `test_stream`, `test_stream_fake`). Then `rm -rf build-mex && cmake -S . -B build-mex -G Ninja && cmake --build build-mex && ctest --test-dir build-mex -L mex`: 3 tests pass (the configuration without MATLAB that the C library CI jobs use). `rm -rf build-mex`.
- [ ] **Step 10:** Check the registry: `grep -h -o -E 'GW_X?CMD\("[a-z0-9_]+"' matlab/mex/*.c | sort | uniq -c | awk '$1 != 1' | wc -l` prints `0`; `grep -h -o -E 'GW_CMD\("[a-z0-9_]+"' matlab/mex/*.c | wc -l` prints `110`; `grep -h -o -E 'GW_XCMD\("[a-z0-9_]+"' matlab/mex/*.c | wc -l` prints `19`; `cut -f2 matlab/api-map.tsv | tail -n +2 | grep -v '^-$' | sort -u | while read c; do grep -q "GW_CMD(\"$c\"" matlab/mex/*.c || echo "missing $c"; done` prints nothing. `rm -rf build-mex`.
- [ ] **Step 11:** Commit `build: MEX gateway skeleton, contracts, stub registry and enum tables`.

### Task 5: Handle table

**Wave 0. Depends on:** Task 4. **Build directory:** `build-mex`.

**Files:**
- Modify: `matlab/mex/handles.c`
- Test: `tests/mex/test_handles.c`

**Interfaces:**
- Consumes: `handles.h` ("Shared contracts").
- Produces: the implementation of every function in `handles.h`, with exactly the documented behaviour.

- [ ] **Step 1:** Write `tests/mex/test_handles.c` (no `MT_GROUPS` line; pointers are string literals, `pre_destroy` appends `"p<ptr> "` and `destroy` appends `"d<ptr> "` to a log string):
  - after `gw_handle_reset(7)`: `a = add(CONTEXT, "A", 0, true, ops)`; `b = add(BUFFER_STREAM, "B", a, true, ops)`; `borrow(DEVICE, "C", a, &c)` returns 0 with `c != 0`; `d = add(BLOCK, "D", b, true, ops_destroy_only)`; `e = add(BLOCK, "E", b, ...)`; `f = add(MASK, "F", a, ...)`. Then `gw_handle_count() == 6`; `a >> 32 == 7` and `(uint32_t)a == 1`; `gw_handle_root(e) == a`; `gw_handle_parent(e) == b`; `gw_handle_root(a) == a`; `gw_handle_owned(b)` and `!gw_handle_owned(c)`; `gw_handle_find(GW_T_BLOCK, "E") == e`.
  - de-duplication: borrowing `(DEVICE, "C")` again gives `c`; `(CHANNEL, "C")` gives a different id; `gw_handle_ptr(c, GW_T_CHANNEL) == NULL`; `gw_handle_ptr(c, GW_T_DEVICE)` is `"C"`; `borrow(DEVICE, NULL, a, &x)` returns 0 with `x == 0`; `borrow(DEVICE, "Z", 12345, &x)` returns `-EBADF`.
  - `gw_handle_destroy(a)` produces exactly the log `"pA dF pB dE dD dB dA "` (the extra channel entry has no ops), count 0, and `gw_handle_ptr(c, GW_T_DEVICE) == NULL`.
  - stale ids: a new `add(CONTEXT, "A", ...)` reuses slot 1 with another generation (`(uint32_t)x == (uint32_t)a`, `x != a`); `gw_handle_type(a) == GW_T_NONE`; `gw_handle_destroy(a)` changes nothing; `add(BLOCK, "Q", a, ...)` returns 0 (stale parent); `add(CONTEXT, NULL, 0, ...)` returns 0.
  - `aux`: `add(..., &ops, "AUX")` gives `gw_handle_aux(id)` `"AUX"` and the destroy op receives it.
  - flags and cache (a second build of the same tree): a new entry has `gw_handle_flags(id) == 0`; after `gw_handle_set_flags(id, 3)` it is 3; `gw_handle_flags` of a stale id is 0 and `gw_handle_set_flags` on it does nothing. `gw_handle_set_cache(b, "K1", free1)` then `gw_handle_cache(b)` is `"K1"`; setting `"K2"` calls `free1("K1")` once; a cache can be set on a borrowed entry; `gw_handle_destroy(a)` with a cache on `B` logs `pA dF pB dE dD kK2 dB dA ` (the cache free function appends `"k<cache> "`: after the children, before `destroy`); `gw_handle_cache` of a stale id is `NULL`.
  - the entry is live inside its own `pre_destroy`: a `pre_destroy` op that calls `gw_handle_find(GW_T_BUFFER_STREAM, ptr)` gets its own id, `gw_handle_flags` of it returns the value set before (3) and `gw_handle_ptr` returns `ptr`; inside its `destroy` op `gw_handle_ptr` returns `NULL`.
  - re-entrancy: a `destroy` op that calls `gw_handle_destroy` on its own id and on its (already destroyed) child returns normally and each op ran exactly once.
  - a borrow of an existing `(type, pointer)` with a stale parent (`12345`) or parent `0` returns the existing id and 0, and the entry keeps its parent.
  - a `pre_destroy` op that adds 200 children (the table grows from its first 64 slots) and then calls `gw_handle_find` on its own pointer gets its own id; `gw_handle_ptr` and `gw_handle_flags` still answer; all 200 children are destroyed.
  - a child whose `pre_destroy` destroys its parent: inside the child's `destroy` op `gw_handle_parent(child)` and `gw_handle_root(child)` are 0 (not the id of the destroyed parent).
  - cache re-entrancy: a `destroy` op that calls `gw_handle_set_cache` on its own id: that cache's free function runs exactly once (log `kK1 dB kK3 `). A cache free function that calls `gw_handle_cache` on its entry sees `NULL` (in `gw_handle_set_cache` and in a teardown). A free function that attaches a cache `K2` to its own entry: `set_cache(K3)` logs `kK1 kK2 ` and leaves `K3` in the slot; in a teardown the log is `kK1 dB kK2 `. A free function that destroys its entry and then adds a new entry (which takes over the slot): the cache being set is attached to nothing, `gw_handle_cache(new) == NULL`, and its free function is never called.
  - one pointer borrowed under all 12 types after 100 other entries: 12 different ids, `gw_handle_find(type, ptr)` gives each type its own, and after one of them is destroyed the others are still found. (The type comparison in `gw_handle_find` cannot be isolated by a test: with this hash, entries of one pointer and two types never share a bucket below 2^24 slots.)
  - `gw_handle_destroy_all()` with roots R1, R2, R3 (created in that order, R2 with a child) logs `dR3`, then R2's child, `dR2`, `dR1`; count 0.
  - growth: 10 000 roots added, every `gw_handle_ptr` valid, `gw_handle_destroy_all()`, count 0.
  - `gw_handle_reset(9)` after entries exist calls no op and the next id has generation 9; `gw_handle_reset(0)` starts at generation 1.
  - `gw_type_name(GW_T_ATTR)` is `"attr"`, `gw_type_name(GW_T_COUNT)` is `"none"`.
- [ ] **Step 2:** Run the build line of "Gates" (`B=build-mex`). Expected: `test_handles` fails to link.
- [ ] **Step 3:** Implement `handles.c` (one growable array of entries; `calloc`/`realloc`/`free` only; no MATLAB or libiio include; no locking: the gateway runs on MATLAB's interpreter thread).
- [ ] **Step 4:** Build and run `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: 7 tests pass under `-L mex`, no sanitizer report. `rm -rf build-mex`.
- [ ] **Step 5:** Commit `feat(mex): handle table with generations, ownership and ordered teardown`.

### Task 6: Dispatcher, argument and return helpers, test harness

**Wave 0. Depends on:** Task 5. **Build directory:** `build-mex`.

**Files:**
- Modify: `matlab/mex/iio_mex.c`, `matlab/mex/mx_util.c`, `tests/mex/mex_test.c`
- Create: `tests/mex/mex_test.h`
- Test: `tests/mex/test_dispatch.c`, `tests/mex/test_mx_util.c`, `tests/mex/test_trace.c`, `tests/mex/test_fixture.c`

**Interfaces:**
- Consumes: `iio_mex.h`, `mx_util.h`, `handles.h`, `mxmock.h` ("Shared contracts"); `tests/fixture_util.h` (Task 2).
- Produces:
  - `mexFunction` with this exact sequence: (1) set `plhs[0 .. max(nlhs,1)-1]` to `NULL` and release what an earlier call left parked (`gw_park`); (2) on the first call of a load, and on the first call after the at-exit function ran: `gw_handle_reset(seed)` with `seed` = `tv_nsec` of `CLOCK_REALTIME` XOR `getpid()`, and `mexAtExit(fn)` where `fn` calls `gw_handle_destroy_all()`, releases parked pointers, clears the "initialised" flag and calls nothing from `mx*`/`mex*`; (3) look the command up (exact, case-sensitive match over `gw_groups`), check counts (`-EINVAL`), run it, then release parked pointers; an unknown command is `-ENOSYS` with the text `unknown command`; (4) if the command returned 0 but some `plhs[k]` with `k < nlhs` is still `NULL`: `iio:EFAULT`; (5) `mexLock()` if `gw_handle_count() > 0` and not locked, `mexUnlock()` if the count is 0 and locked; (6) on failure: `mxDestroyArray` every `plhs[k]` and reset it to `NULL`, build identifier and `"<command>: <text>"` in stack buffers, then the single `mexErrMsgIdAndTxt(id, "%s", msg)`.
  - Dispatcher trace: if the environment variable `IIO_MEX_TRACE` names a file when the MEX handles its first call of a load, the dispatcher appends to it, with `fflush` after each line, `> <command>` before a command runs and `< <command> ok` or `< <command> <identifier>` after it (`FILE *` kept in a static, opened with `fopen(path, "a")`, closed in the at-exit function). An unknown command is traced the same way. No variable, no file access.
  - Commands of group `mex` (in `iio_mex.c`): `mex_shutdown` (`gw_handle_destroy_all()`), `mex_handle_count` (`double`). **Commands produced:** `mex_shutdown`, `mex_handle_count`.
  - Every `gw_arg_*`, `gw_ret_*`, `gw_set_id`, `gw_park`, `gw_errno_name` of `mx_util.h`; final `gw_fail` and `gw_stub`.
  - `tests/mex/mex_test.h` exactly as in "Shared contracts", and its implementation `tests/mex/mex_test.c`.

- [ ] **Step 1:** Write `tests/mex/test_dispatch.c`. It defines a group `gw_group_local` and, after it, the line `MT_GROUPS(&gw_group_local);`. The group's commands: `t_ok` (1 output: `gw_ret_double(c, 0, 42)`), `t_two` (max 2 outputs: `gw_ret_double(c, 0, 1)`, `gw_ret_double(c, 1, 2)`), `t_errno` (1 arg: returns minus the integer argument), `t_msg` (`gw_fail(c, -EIO, "disk %d", 3)`), `t_out_then_fail` (sets output 0, returns `-EBUSY`), `t_forget` (max 1 output, sets nothing, returns 0), `t_add` (result first: `gw_ret_id(c, 0, 0)`, adds an owned root whose ops append to a log, `gw_set_id`), `t_park` (1 output: parks a `malloc` block with a free function that appends `"f"` to the log, then `gw_ret_double`), `t_del` (`gw_arg_destroy(c, 0, GW_T_CONTEXT)`), `t_range` (1 to 2 args, returns `c->nrhs`). Checks:
  - `mock_call` with `nrhs == 0`, with a double as first argument, with `"no_such"`, with `"T_OK"`: each `iio:ENOSYS`.
  - `t_range` with 0 or 3 args: `iio:EINVAL`; `t_ok` with `nlhs == 2`: `iio:EINVAL`; `t_ok` with `nlhs == 0` returns 42 in `out[0]`; `t_two` with `nlhs == 1` returns one array and `mock_live()` grew by exactly 1.
  - `t_errno(22)`: `iio:EINVAL` with message `"t_errno: Invalid argument (-22)"`; `(110)`: `iio:ETIMEDOUT`; `(95)`: `iio:ENOTSUP`; `(11)`: `iio:EAGAIN`; `(38)`: `iio:ENOSYS`; `(524)`: `iio:E524`.
  - `t_msg`: `iio:EIO`, message `"t_msg: disk 3"`. Directly after it `t_errno(5)` has exactly the message `"t_errno: Input/output error (-5)"`; `t_msg_long` (`gw_fail(c, -EIO, "%0300d", 7)`) gives a message of `strlen("t_msg_long: ") + 255` characters, and `t_errno(2)` after it exactly `"t_errno: No such file or directory (-2)"` (CTest runs `test_dispatch` with `ASAN_OPTIONS=detect_stack_use_after_return=0`, so an uninitialised `err_msg` reads as the mock's `0xA5` stack fill and fails these).
  - command names that are not a row, built with `mock_char_array` (the group also registers a command `t`): 4x1 `t_ok`, 1x2x2 `t_ok`, 2x2 `t_ok`, 16x1 and 1x8x2 `mex_handle_count`, 0x4: each `iio:ENOSYS` with a message containing `the first argument must be a command name`.
  - `t_out_then_fail`: `iio:EBUSY`, `out[0] == NULL`, `mock_live()` unchanged; `t_forget` with `nlhs == 1`: `iio:EFAULT`.
  - locking: not locked at start; after `t_add` locked and `mex_handle_count` is 1; `mock_clear_mex()` changes nothing; `t_del(id)` runs the destroy op once and unlocks; `t_del(id)` again and `t_del(0)` do nothing; `t_del` with a live id of another type: `iio:EBADF`.
  - result first and parking under allocation failure: `mock_fail_alloc(1)` then `t_add`: `mock_alloc_failed()`, `gw_handle_count()` unchanged, the ops log unchanged, not locked. `t_park`: the log is `"f"` when `mock_call` returns. `mock_fail_alloc(1)` then `t_park`: `mock_alloc_failed()`; after the next call (`t_ok`) the log holds exactly one more `"f"`. A fifth `gw_park` in one call returns `-ENOMEM` after freeing its pointer.
  - `mex_shutdown` with two roots alive: both destroy ops ran, newest first, count 0, unlocked.
  - exit: with one root alive `mock_exit_matlab()` runs its destroy op; a following `t_add` works and its id differs from every earlier one; the earlier id gives nothing in `t_del`.
- [ ] **Step 2:** Write `tests/mex/test_mx_util.c`, also with a local group whose commands each call one helper and return what it produced (argument helpers echo the value through the matching `gw_ret_*`). Checks, each as "input gives output" or "input gives `iio:EINVAL`" unless stated:
  - `gw_errno_name`: `-22` and `22` give `"EINVAL"`; `-110` `"ETIMEDOUT"`; `-95` `"ENOTSUP"`; `-11` `"EAGAIN"`; `-2` `"ENOENT"`; `-13` `"EACCES"`; `-1` `"EPERM"`; `-16` `"EBUSY"`; `-19` `"ENODEV"`; `-6` `"ENXIO"`; `-28` `"ENOSPC"`; `-27` `"EFBIG"`; `-34` `"ERANGE"`; `-32` `"EPIPE"`; `-111` `"ECONNREFUSED"`; `-113` `"EHOSTUNREACH"`; `-9` `"EBADF"`; `-12` `"ENOMEM"`; `-5` `"EIO"`; `-75` `"EOVERFLOW"`; `0` and `-99999` give `NULL`.
  - `gw_arg_id`: `uint64(5)` gives 5; `double(5)`, a 1-by-2 `uint64`, a complex `uint64`: bad.
  - `gw_arg_handle(GW_T_DEVICE)`: live device id gives its pointer; id 0, an unknown id, a destroyed id: `iio:EBADF` with a message containing `"invalid or destroyed device handle"`; a live context id: `iio:EBADF` with `"expected a device handle, got a context"`. `gw_arg_handle0`: id 0 gives `NULL` pointer and success.
  - `gw_arg_str`: `"abc"` gives `"abc"`; `""` gives `""`; `"µV"` (UTF-8) round-trips; a double, a cell: bad; `mock_char_array` 3x1, 2x3, 1x3x2 and 0x0x0: bad; 1x3 gives the text; 0x3 and 1x0 give `''`; text with an embedded NUL (`{'a', 0, 'b'}` written into the `mxChar` data) is bad, and the message says `text with an embedded NUL (character 2 of 3)`. `gw_arg_str0`: `[]` and a missing argument give `NULL`; `""` gives `""`.
  - `gw_set_double`: after `gw_ret_double(c, k, 0)` it stores the value; on an output that was not requested it does nothing. `gw_c_numeric_enter` / `_leave` (called directly, no command): in the `"C"` process nothing is switched (`uselocale((locale_t)0)` stays `LC_GLOBAL_LOCALE`); inside `mt_comma_begin()`: `"%.2f"` of 0.25 is `0,25` before, `0.25` between enter and leave (and `strtod("0.25")` is 0.25), `0,25` after, and the thread is back on `LC_GLOBAL_LOCALE`; with a thread locale of its own (`newlocale(LC_ALL_MASK, mt_comma_locale(), 0)` + `uselocale`): between enter and leave `nl_langinfo(CODESET)` is unchanged (`UTF-8`: only `LC_NUMERIC` was replaced) and afterwards `uselocale((locale_t)0)` is that same object; a second leave and a leave after a failed enter do nothing.
  - `gw_arg_bool`: logical scalar true gives true; `double(1)`, a 1-by-2 logical: bad.
  - `gw_arg_int(lo = -5, hi = LLONG_MAX)`: `double(3)` gives 3; `int8(-5)` gives -5; `int64(9007199254740993)` gives exactly 9007199254740993; `uint64(9223372036854775807)` gives that; `uint64(2^63)`, `double(2.5)`, `double(-6)`, `NaN`, `Inf`, logical `true`, a char, a complex double, a 1-by-2 double: bad. `gw_arg_index`: `double(4294967295)` gives 4294967295; `-1` and `4294967296`: bad.
  - `gw_arg_double`: `double(2.5)`, `single(2.5)`, `int32(2)` give 2.5, 2.5, 2; a char, a logical, a complex double: bad.
  - `gw_arg_bytes`: a 1-by-5 `uint8`, a 5-by-1 `uint8` give the same pointer as `mxGetData` and count 5; an empty `uint8` (0x0, 1x0 and 0x1) gives count 0 and a non-NULL pointer; a `double` vector, a 2-by-2 `uint8`: bad.
  - `gw_arg_params`: `[]` gives `NULL`; a struct `{timeout_ms: 123, log_level: 1, flags: 1}` gives those values and 0 elsewhere (including all 32 reserved bytes); field `bogus`: bad; `timeout_ms: 1.5`: bad; a 1-by-2 struct: bad.
  - `gw_arg_given`: false for a missing argument, for `[]` and for a complex 0x0 double; true for `''`, `0`, an empty `uint8` and a 0x0x0 double.
  - results: `gw_ret_str(NULL)` is a 0x0 double; `("")` a 0x0 char; `("abc")` a 1-by-3 char; `gw_ret_id` a `uint64` scalar, and `gw_set_id(c, 0, UINT64_MAX)` after it makes that scalar `UINT64_MAX`; `gw_ret_ids(n = 0)` a 1-by-0 `uint64`; `gw_ret_int64(INT64_MIN)` an `int64` scalar holding it; `gw_ret_uint64(UINT64_MAX)`; `gw_ret_bool`; `gw_ret_double`; `gw_ret_doubles({1, 2, 3})` a 1-by-3 double; `gw_ret_bytes("a\0b", 3)` a 1-by-3 `uint8` `[97 0 98]`; `gw_ret_bytes(NULL, 0)` a 1-by-0 `uint8`; `gw_ret_strs({"x", "yz"})` a 1-by-2 cell of char; `gw_ret_strs(n = 0)` a 1-by-0 cell; `gw_ret_empty` a 0x0 double; any `gw_ret_*(c, 1, ...)` with `nlhs == 1` creates nothing (`mock_live()` unchanged).
  - `gw_ret_borrowed(c, 0, GW_T_ATTR, p, some_device_id)`: the new id's parent is the device's root context; `p == NULL` returns id 0.
  - `gw_arg_destroy(GW_T_MASK)`: a live owned mask entry is destroyed (its op ran once); a live borrowed mask entry is left alive and no error is raised; a live context id: `iio:EBADF`; 0 and a stale id: nothing.
  - objects that are not what they seem: `gw_arg_str` and `gw_arg_str0` with `mock_string_object()`: `iio:EINVAL`; `gw_arg_double`, `gw_arg_int` and `gw_arg_bytes` with `mock_sparse_double(1, 1)`: `iio:EINVAL`.
- [ ] **Step 3:** Write `tests/mex/test_trace.c` (a local group `gw_group_local` with one command `t_ok`, and the line `MT_GROUPS(&gw_group_local);`): `setenv("IIO_MEX_TRACE", "<mkdtemp dir>/trace.log", 1)` and the line `earlier line` written into that file before the first call; call `mex_handle_count`, `t_ok`, `no_such`; the file is exactly `earlier line` (append mode: it is still the first line) followed by the six lines `> mex_handle_count`, `< mex_handle_count ok`, `> t_ok`, `< t_ok ok`, `> no_such`, `< no_such iio:ENOSYS`. After `mock_exit_matlab()` with the variable still set, the next call appends to the same file; after `mock_exit_matlab()` and `setenv("IIO_MEX_TRACE", "", 1)` calls work and the file does not change. The test removes the file and the directory.
- [ ] **Step 4:** Write `tests/mex/test_fixture.c` (it defines `const struct gw_group *const gw_groups[] = { &gw_group_mex, NULL };`): `mt_fixture_setup(&fx, argv[1]) == 0`; `fx.uri` starts with `"emu:/"` and `fx.xml_uri` with `"xml:/"`; `iio_create_context(NULL, fx.uri)` has 8 devices and `iio_context_find_device(ctx, "emu-phy")` has 14 attributes; `fixture_read_file(fx.dir, "emu-rx_buf0.bin", buf, 20000)` returns 16384 and the first four bytes are `00 08 ff 07`; `id = mt_open_context(fx.uri)` is non-zero, `gw_handle_type(id) == GW_T_CONTEXT`, `mex_handle_count` is 1 and `gw_handle_destroy(id)` leaves it at 0; `mt_open_context("bogus:foo") == 0`; `mt_oom_sweep(1, "mex_handle_count", 0)` returns at least 1 (the last attempt succeeded) and leaves `mock_live()` unchanged; after `mt_fixture_teardown` the directory is gone.
- [ ] **Step 5:** Run the build line of "Gates". Expected: the four new tests fail to link.
- [ ] **Step 6:** Implement `mx_util.c`, the dispatcher in `iio_mex.c`, the trace, and `mex_test.c` (with `mt_oom_sweep`). `gw_errno_name` covers at least every name in Step 2 plus `ESRCH EINTR E2BIG EFAULT EEXIST ENOTDIR EISDIR EMFILE EROFS EDOM ENAMETOOLONG ENODATA ETIME EPROTO EBADMSG EMSGSIZE EPROTOTYPE EADDRINUSE EAFNOSUPPORT ENETDOWN ENETUNREACH ECONNABORTED ECONNRESET ENOBUFS EISCONN ENOTCONN ESHUTDOWN EHOSTDOWN EALREADY EINPROGRESS ECANCELED EREMOTEIO EILSEQ`. Argument error texts name the argument as `"argument <k+1>"`, state what was expected and the class and size that arrived.
- [ ] **Step 7:** Run `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: 11 tests pass (the 7 of Task 5 plus `test_dispatch`, `test_mx_util`, `test_trace`, `test_fixture`), no sanitizer report; `mex_exports` and `mex_loadcheck` still pass. `rm -rf build-mex`.
- [ ] **Step 8:** Commit `feat(mex): command dispatch, error mapping, trace, argument and return helpers, test harness`.

### Task 7: Gate scripts, API map check, M skeleton

**Wave 0. Depends on:** Task 6. **Build directory:** `build-mex`.

**Files:**
- Create: `matlab/tools/check_api.py`, `matlab/tools/check_mlint.sh`, `matlab/tools/test_tools.py`
- Create: `matlab/+iio/+internal/Object.m`, `matlab/+iio/+internal/params.m`, `matlab/+iio/+internal/timeout_ms.m`, `matlab/+iio/+internal/shutdown.m`, `matlab/+iio/+internal/handle_count.m`
- Create (stubs, replaced by their own tasks): `matlab/+iio/{Device,Channel,Attr,ChannelsMask,Buffer,BufferStream,Block,Stream,EventStream}.m`
- Modify: `matlab/CMakeLists.txt` (add the two blocks marked `Task 7` and the test `mex_mlint_r2023b`)
- Test: `matlab/tools/test_tools.py`

**Interfaces:**
- Consumes: `matlab/api-map.tsv` (in the repository since this plan was committed; read-only here); the registry lines `GW_CMD("name", ...)` / `GW_XCMD("name", ...)` of Task 4; commands `mex_shutdown`, `mex_handle_count` (Task 6).
- Produces:
  - `matlab/api-map.tsv` format, enforced: tab-separated, UTF-8, first line the header `c_function	command	matlab	status	notes`, then exactly one row per public function of `iio.h`.

    | Column | Content |
    |---|---|
    | `c_function` | name in `iio.h`: an `__api` function or a `static inline` helper |
    | `command` | the gateway command that reaches it, or `-` |
    | `matlab` | the MATLAB members that reach it, separated by `, `: `iio.Class.member`, `iio.Class` (constructor), `iio.function`; `-` if none |
    | `status` | `exposed` (has a command), `internal` (used inside the gateway only), `not_exposed` |
    | `notes` | required for `internal` and `not_exposed`: the reason and, for `not_exposed`, the MATLAB alternative |

  - `check_api.py --iio-h PATH [--root DIR] [--group NAME | --strict]`, exit 0 or 1, one `check_api: <message>` line per violation, one `check_api: pending: <message>` line per pending item, and a last line `check_api: <rows> rows, <commands> commands, <n> error(s)` (followed by `; pending: <k>` when k > 0). `<commands>` counts the registered commands. Rules:
    1. the names extracted from `iio.h` are exactly the `c_function` values, each once. Extraction, after stripping `/* */` and `//` comments: `__api\b[^;{]*?\b(\w+)\s*\(` (134 names) and `static\s+inline\b[^;{]*?\b(\w+)\s*\(` (7 names): 141 names **[RUN]**.
    2. row shape: five fields; `exposed` has a command matching `[a-z][a-z0-9_]*` and a `matlab` field other than `-`; `internal` and `not_exposed` have `-` for command and `matlab`, and a note.
    3. registry: command names are read from `GW_CMD("name"` and `GW_XCMD("name"` in `matlab/mex/*.c` (`iio_mex.c` is group `mex`, `cmd_<g>.c` is group `<g>`). No name is registered twice; every command of an `exposed` row is a `GW_CMD`; every `GW_CMD` name occurs in at least one row; no `GW_XCMD` name occurs in a row.
    4. every `iio_mex(` call in a `.m` file under `matlab/` (comments stripped) has a single-quoted literal first argument that is a registered command.
    5. every registered command is called from a file under `matlab/+iio`; a command of group `test` from a file under `matlab/tests`.
    6. no registration line names `gw_stub`.
    7. every MATLAB member of a row exists: `matlab/+iio/<Class or function>.m` is a file, and for `iio.Class.member` the file contains `member` as a whole word.

    Rules 1 to 4 are always errors. Rules 5 to 7 are errors with `--strict`; with `--group G` only rule 6 for the file of group G is an error and the rest of 5 to 7 is pending; with neither option all of 5 to 7 is pending. A pending line names the command, the row's MATLAB members and the file concerned, so that its owner can be found.
  - `check_mlint.sh [file.m ...]` (default: every `.m` under `matlab/`): exit 0 clean, 1 if any message of level ML2 or ML3 or any `L 0` line, or if `mlint` did not print one `========== <file> ==========` header per file (a program that prints nothing has checked nothing), 77 if `mlint` is not found (`$MLINT`, else `$MATLAB_ROOT/bin/glnxa64/mlint`).
  - CTest tests `mex_api_map`, `mex_tools_selftest`, `mex_mlint`, `mex_mlint_r2023b` (label `mex`).
  - M: `iio.internal.Object` (below), `iio.internal.params(opts)`, `iio.internal.timeout_ms(t)`, `iio.internal.shutdown()`, `n = iio.internal.handle_count()`, and nine stub classes with the internal constructor.

`matlab/+iio/+internal/Object.m` (mlint-clean **[RUN]**; behaviour **[CI]**):

```matlab
classdef (Abstract) Object < handle
    %OBJECT Base of every iio class: a gateway handle plus the MATLAB object that keeps it valid.

    properties (SetAccess = protected, Hidden)
        Handle (1,1) uint64 = uint64(0)   % gateway handle id, 0 = none
        Parent = []                       % iio object this one came from; keeps it alive
    end

    methods
        function obj = Object(parent, handle)
            if nargin >= 2
                obj.Parent = parent;
                obj.Handle = handle;
            end
        end

        function tf = eq(a, b)
            %EQ True where both wrap the same libiio object.
            tf = iio.internal.Object.handles(a) == iio.internal.Object.handles(b);
        end

        function tf = ne(a, b)
            tf = ~eq(a, b);
        end
    end

    methods (Static, Hidden)
        function h = handles(objs)
            %HANDLES uint64 array, same size as OBJS, of their gateway ids.
            h = zeros(size(objs), 'uint64');
            for k = 1:numel(objs)
                h(k) = objs(k).Handle;
            end
        end
    end
end
```

Stub class (shown for `Channel`; the other eight differ only in the name):

```matlab
classdef Channel < iio.internal.Object
    %CHANNEL Placeholder until its own task replaces this file.
    methods
        function obj = Channel(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
        end
    end
end
```

`matlab/tools/check_mlint.sh` **[RUN]**:

```sh
#!/bin/sh
# G3: no MATLAB Code Analyzer error (level ML2 or ML3) in any .m file.
# usage: check_mlint.sh [file.m ...]      default: every .m file under matlab/
# mlint is taken from $MLINT, else $MATLAB_ROOT/bin/glnxa64/mlint. It needs no licence.
# Exit: 0 clean, 1 errors found, 77 mlint not available.
set -u
here=$(cd "$(dirname "$0")" && pwd)
mlint=${MLINT:-${MATLAB_ROOT:+$MATLAB_ROOT/bin/glnxa64/mlint}}
if [ -z "$mlint" ] || [ ! -x "$mlint" ]; then
	echo "check_mlint: mlint not found (set MATLAB_ROOT or MLINT)"
	exit 77
fi
if [ $# -eq 0 ]; then
	set -- $(find "$here/.." -name '*.m' | sort)
fi
[ $# -gt 0 ] || { echo "check_mlint: no .m files"; exit 1; }
# mlint always exits 0 and writes to stderr. The extra /dev/null argument forces the
# "========== file ==========" header even when only one file is checked.
out=$("$mlint" -id -severity -m2 "$@" /dev/null 2>&1 | grep -v '^========== /dev/null' | grep -v "'/dev/null'")
# Proof that mlint ran: one header per file. A program that prints nothing has checked nothing.
n=$(printf '%s\n' "$out" | grep -c '^========== ')
[ "$n" -eq $# ] || { echo "check_mlint: mlint reported on $n of $# file(s)"; exit 1; }
if printf '%s\n' "$out" | grep -q '^L [0-9]'; then
	printf '%s\n' "$out" | awk '/^==========/ { file = $2; next } /^L [0-9]/ { print file ": " $0 }'
	echo "check_mlint: FAILED"
	exit 1
fi
echo "check_mlint: $# file(s), no errors"
```

- [ ] **Step 1:** Write `matlab/tools/test_tools.py` (`unittest`; each case builds a tiny tree in a temporary directory with a three-function fake `iio.h`, an `api-map.tsv`, `matlab/mex/cmd_x.c` and `matlab/+iio/*.m`, and runs `check_api.py --root`): a correct tree exits 0 with `--strict`. Then one case per line below; each exits 1 and prints a line containing the quoted text:
  - rule 1: a function of the fake `iio.h` has no row: `without a row`; a row names a function that is not in it: `not in iio.h`.
  - rule 2: a `not_exposed` row with a command: `must not have a command`.
  - rule 3: an `exposed` row whose command is not registered: `is not registered`; a `GW_XCMD` name used by a row: `is an extra command`.
  - rule 4: `iio_mex(cmd, x)` with a variable: `without a literal command`; `iio_mex('nope')`: `unknown command`.
  - rule 5, with `--strict`: a registered command no file under `matlab/+iio` calls: `is not called from`; the same tree without `--strict` exits 0 and prints `pending: 1`.
  - rule 6, with `--group x`: a `GW_CMD` line bound to `gw_stub`: `is still a stub`; the same tree with `--group y` exits 0.
  - rule 7, with `--strict`: a member that the class file does not contain: `not found in`.

  Three more cases run `check_mlint.sh` and are skipped when `mlint` is not found: a file containing `x = 1 +;` exits 1 and prints `SYNER`; a file with only an unused variable exits 0; with `MLINT=/bin/true` (a program that runs and reports nothing) it exits 1 and prints `mlint reported on 0 of 1 file(s)`.
- [ ] **Step 2:** Run `python3 matlab/tools/test_tools.py`. Expected: errors (scripts missing).
- [ ] **Step 3:** Write `check_api.py` and `check_mlint.sh` (`chmod +x` both). Add the two `Task 7` blocks of Task 4's listing to `matlab/CMakeLists.txt`, and directly after the `mex_mlint` test:

  ```cmake
  		set(IIO_MLINT_MIN_ROOT /usr/local/MATLAB/R2023b CACHE PATH "MATLAB root of the minimum supported release (lint only)")
  		if(EXISTS ${IIO_MLINT_MIN_ROOT}/bin/glnxa64/mlint)
  			add_test(NAME mex_mlint_r2023b COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_mlint.sh)
  			set_tests_properties(mex_mlint_r2023b PROPERTIES ENVIRONMENT "MATLAB_ROOT=${IIO_MLINT_MIN_ROOT}" LABELS mex TIMEOUT 120)
  		endif()
  ```

- [ ] **Step 4:** Run `python3 matlab/tools/test_tools.py` (expected: `OK`) and `python3 matlab/tools/check_api.py --iio-h deps/install/include/iio/iio.h`. Expected last line: `check_api: 141 rows, 129 commands, 0 error(s); pending: <k>` with k > 0 (stubs, commands no M file calls yet, members that do not exist yet). If it reports an error in `matlab/api-map.tsv` or in the registry, the registry of Task 4 is wrong unless the "Command reference" and the map disagree; in that case stop and report.
- [ ] **Step 5:** Write the M skeleton. `params(opts)`: `opts` is the name-value struct of an `arguments` block; returns `[]` when it has none of the fields `Timeout`, `LogLevel`, `StderrLevel`, `TimestampLevel`, `XmlIncludeValues`, else a struct with `timeout_ms = iio.internal.timeout_ms(opts.Timeout)`, `log_level` / `stderr_level` / `timestamp_level` = 1..5 for `"nolog" "error" "warning" "info" "debug"`, `flags = 1` when `XmlIncludeValues` is true; only the given fields are set. `timeout_ms(t)`: `Inf` gives -1; a whole number in `[-2147483648, 2147483647]` is returned unchanged; anything else is `error('iio:EINVAL', ...)`.
- [ ] **Step 6:** Run the build line of "Gates" and `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: 15 tests pass (the 11 of Task 6 plus `mex_api_map`, `mex_tools_selftest`, `mex_mlint`, `mex_mlint_r2023b`); `MATLAB_ROOT=/usr/local/MATLAB/R2025b matlab/tools/check_mlint.sh` prints `check_mlint: 14 file(s), no errors`. `rm -rf build-mex`.
- [ ] **Step 7:** Commit `build: API map, lint and API gates; feat(matlab): object base class and stubs`.

### Task 8a: Vertical slice: commands, classes, fixtures and tests

**Wave 0. Depends on:** Task 7. **Build directory:** `build-mex`. The smallest end-to-end slice (create an `emu:` context, read one attribute, destroy, `clear mex`), plus a MATLAB test of the object-model idioms that every wave 1 class will copy. Verified here by the local gates; Task 8b pushes it through real MATLAB.

**Files:**
- Modify: `matlab/mex/cmd_context.c`, `matlab/mex/cmd_attr.c`, `matlab/+iio/Attr.m`
- Create: `matlab/+iio/Context.m`, `matlab/tests/EmuFixture.m`, `matlab/tests/IioTestCase.m`, `matlab/tests/ObjModelProbe.m`, `matlab/probes/run_probes.m`, `matlab/probes/README.md`
- Test: `tests/mex/test_slice.c`, `matlab/tests/tSlice.m`, `matlab/tests/tFixture.m`, `matlab/tests/tObjectModel.m`

**Interfaces:**
- Consumes: everything of Tasks 4 to 7.
- Produces:
  - Four commands with the final signatures of the "Command reference" (Tasks 12 and 13 keep them; Task 13 adds the optional second argument of the last one). **Commands produced:** `create_context`, `context_destroy`, `context_find_attr`, `attr_read_raw`.
  - `iio.Context(uri)` with `delete` and `find_attr(name)`; `iio.Attr(parent, handle)` with `read()`. Tasks 12 and 13 replace both files.
  - `matlab/tests/EmuFixture.m` and `matlab/tests/IioTestCase.m` as in "MATLAB surface / Test support". The fixture XML is found relative to the class file: `fullfile(fileparts(mfilename('fullpath')), '..', '..', 'tests', 'resources', 'iio_fixture.xml')`. `writeWords` uses `fwrite(fid, words, 'uint16', 0, 'ieee-le')`. Every later test class derives from `IioTestCase`.
  - `matlab/tests/ObjModelProbe.m`, a test-only class that calls no gateway command: `classdef ObjModelProbe < iio.internal.Object`; immutable property `tag char`; Dependent properties `parent` (returns the hidden base property `Parent`) and `label` (`['L' obj.tag]`); constructor `ObjModelProbe(tag, opts)` with the `arguments` block `tag (1,1) string = ""`, `opts.Parent = []`, `opts.Handle (1,1) uint64 = uint64(0)`, `opts.AdoptHandle (1,1) uint64`, `opts.Note (1,1) string = "note-" + tag` (a default that refers to an earlier argument); `AdoptHandle`, when given, replaces `Handle`; public property `note char`; `delete` appends `[obj.tag ' ']` to the text that the static method `s = ObjModelProbe.log(reset)` returns (and clears when `reset` is true).
  - `matlab/probes/run_probes.m`; output lines `PROBE <name> OK` / `PROBE <name> ERROR <identifier>: <message>` (procedure in Appendix C).

- [ ] **Step 1:** Write `tests/mex/test_slice.c` with `MT_GROUPS(&gw_group_context, &gw_group_attr);` **[RUN]**:
  - `create_context` with 1 argument: `iio:EINVAL`; with `(mt_empty(), mt_dbl(3))`: `iio:EINVAL`; with `([], "bogus:foo")`: `iio:ENOSYS` and a message starting `"create_context: "`. Still not locked, `mex_handle_count` 0.
  - `ctx = create_context([], fx.uri)` is non-zero and the mock is locked.
  - `a = context_find_attr(ctx, "hw_model")` is non-zero and differs from `ctx`; the same call again returns `a`; `"nope"` returns 0.
  - `attr_read_raw(a)` is `"Emulated Test Board"`; `attr_read_raw(ctx)`: `iio:EBADF`; `mex_handle_count` is 2.
  - `context_destroy(ctx)`: unlocked; `attr_read_raw(a)`: `iio:EBADF`; `context_destroy(ctx)` again and `context_destroy(0)`: no error.
  - a second `create_context` gives an id different from `ctx`; `mock_clear_mex()` while it lives leaves `mex_handle_count` at 1; `mock_exit_matlab()` leaves `gw_handle_count() == 0`.
- [ ] **Step 2:** Write `matlab/tests/tSlice.m` (`classdef tSlice < IioTestCase`), test methods:
  - `readContextAttribute`: `ctx = iio.Context(tc.Fx.Uri)`; `ctx.find_attr("hw_model").read()` equals `'Emulated Test Board'`; `ctx.find_attr('nope')` is empty; `delete(ctx)`; `isvalid(ctx)` is false; `iio.internal.handle_count()` is 0.
  - `errorIdentifiers`: `iio.Context("bogus:foo")` throws `iio:ENOSYS`; `feval('iio_mex', 'no_such_command')` throws `iio:ENOSYS` (through `feval`, so that `check_api.py` rule 4 does not see an unknown literal command); an attribute read after `delete(ctx)` throws `iio:EBADF`.
  - `staysLoadedWhileInUse`: with a context alive, `clear mex`; `[~, loaded] = inmem`; `ismember('iio_mex', loaded)` is true and `find_attr` still works.
  - `clearMexAndReload`: no object alive; call `iio.internal.handle_count()` (loads the MEX); `clear mex`; `iio_mex` is no longer in `inmem`; then create a context, read `hw_model`, delete it: same value.
  - `libiioStaysMapped`: after `clear mex`, `fileread('/proc/self/maps')` still contains `libiio.so.1`. Log (`tc.log`) every distinct mapped path containing `libxml2`, `libzstd` or `libiio`.
  - `matlabXmlStillWorks`: after a context was created and destroyed and `clear mex` ran: `writestruct(struct('a', 1), f)` then `readstruct(f)` on a temporary `.xml` file returns field `a == 1`, and `parseFile(matlab.io.xml.dom.Parser, f)` returns a document.
  - `destructorOrder`: `a = iio.Context(tc.Fx.Uri).find_attr("num")` (the context is referenced only by `a.Parent`); `a.read()` is `'42'`; `clear a`; `iio.internal.handle_count()` is 0.
  - `fiftyContexts`: create and delete 50 contexts in a loop; the count is 0 and the last read still returns `'42'`.
- [ ] **Step 3:** Write `matlab/tests/tFixture.m` (`< IioTestCase`): `rampAnchors` (`w = EmuFixture.rampWords(4096)`: `numel(w) == 8192`, class `uint16`, `w(1:4)` is `[2048 2047 6145 2046]`, `w(end-1:end)` is `[63487 63488]`, `sum(double(w)) == 268431360`; `EmuFixture.rampWords(4097)` has 8194 elements and `w(8193) == 2048`); `defaultFile` (`dir` of `fullfile(tc.Fx.Dir, 'emu-rx_buf0.bin')` reports 16384 bytes; `fread` of the first four bytes gives `[0 8 255 7]`); `converters` (`EmuFixture.s12(uint16([2048 2047 6145 2046]))` is `int16([-2048 2047 -2047 2046])`; `EmuFixture.s14shift2(uint16([2048 2047 6145]))` is `int16([512 511 1536])`); `readInt16RoundTrip` (`tc.Fx.writeWords('x.bin', uint16([1 65535 32768]))`, `tc.Fx.readInt16('x.bin')` is `int16([1; -1; -32768])`); `leakCheckIsLive` (`iio.internal.handle_count()` is 0 at the start, 1 with a context, 0 after `delete`); `verifyIioErrorWorks` (`tc.verifyIioError(@() iio.Context("bogus:foo"), "ENOSYS")` passes).
- [ ] **Step 3a:** Write `matlab/tests/ObjModelProbe.m` (Interfaces above) and `matlab/tests/tObjectModel.m` (`< IioTestCase`; all **[CI]**; objects with a made-up handle are `ObjModelProbe` objects and real iio classes appear only as `X()` and `X.empty`, so no gateway handle is involved and the test keeps passing when a real class starts to read its name in the constructor. As first written it used `iio.Channel([], uint64(5))` and `iio.Attr([], uint64(1))`; corrected after the wave 1a review):
  - `equality`: `a = ObjModelProbe("a", Handle = uint64(5))`, `b = ObjModelProbe("b", Handle = uint64(5))`, `c = ObjModelProbe("c", Handle = uint64(6))`: `a == b` is true; `a ~= c` is true; `[a c] == a` equals `[true false]`; `[a c] ~= a` equals `[false true]`; `[a c] == [b c]` equals `[true true]`; all results are `logical`.
  - `objectArrays`: `arr = ObjModelProbe.empty(1, 0); arr(3) = c;` has size `[1 3]` and class `ObjModelProbe` (elements 1 and 2 come from the constructor form `X()`); `iio.internal.Object.handles(arr)` equals `uint64([0 0 6])`. With a real class: `chans = iio.Channel.empty(1, 0); chans(2) = iio.Channel();` has size `[1 2]`, class `iio.Channel` and handles `uint64([0 0])`.
  - `empties`: `e = iio.Attr.empty(1, 0)`: size `[1 0]`, class `iio.Attr`, `isempty(e)`; `size(e == e)` is `[1 0]`; `[e iio.Attr()]` has size `[1 1]`.
  - `validity` (the three probe objects of `equality`): `isvalid(a)`; after `delete(a)`: `~isvalid(a)`; `isvalid([b c])` equals `[true true]`.
  - `parentKeepsAliveAndDiesLast`: `ObjModelProbe.log(true)`; `p = ObjModelProbe("p")`; `k = ObjModelProbe("k", Parent = p)`; `clear p`; the log is `''` (the child keeps its parent); `k.parent.tag` is `'p'`; `isprop(k, 'parent')` and `isprop(k, 'Parent')` are both true; `clear k`; the log is `'k p '` (child first, then parent).
  - `argumentForms`: `ObjModelProbe("x").note` is `'note-x'`; `ObjModelProbe("x", Note = "n").note` is `'n'`; `ObjModelProbe(AdoptHandle = uint64(9)).Handle` is `uint64(9)` with `tag` equal to `''` (the form `iio.Context(AdoptHandle = h)` relies on); `ObjModelProbe("x", Handle = uint64(4)).Handle` is `uint64(4)`.
  - `listExpansion`: `objs = [ObjModelProbe("a") ObjModelProbe("b")]`; `{objs.tag}` equals `{'a', 'b'}` (immutable property) and `{objs.label}` equals `{'La', 'Lb'}` (Dependent property).
- [ ] **Step 4:** Run the build line of "Gates" and G1 for `test_slice`. Expected: `test_slice` fails (`iio:ENOSYS`: the four commands are stubs).
- [ ] **Step 5:** Implement the four commands, the minimal `Context.m` (constructor `Context(uri)` with `uri (1,1) string`, `delete`, `find_attr`) and `Attr.m` (`read`), `EmuFixture.m`, `IioTestCase.m`. Write `matlab/probes/run_probes.m` (runs every `matlab/probes/p_*.m` inside `try`/`catch` and prints `PROBE <name> OK` or `PROBE <name> ERROR <identifier>: <message>`) and `matlab/probes/README.md` (the rules of Appendix C, step 5).
- [ ] **Step 6:** Run `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: 17 tests pass, including `test_slice` and `mex_bundled_libs` (the same test with libiio bound to MATLAB's bundled libxml2 2.13.8 and libzstd **[RUN]**); `check_mlint.sh` reports no errors for 22 files with both MATLAB roots; `check_api.py` still ends in `0 error(s)`. `rm -rf build-mex`. Then once against the minimum release, in a fresh directory: the build line of "Gates" with `-DMatlab_ROOT_DIR=/usr/local/MATLAB/R2023b`, `ctest --test-dir build-mex -L mex --output-on-failure`: all pass (`mex_bundled_libs` now binds libiio to libxml2 2.10.3; R2023b bundles no libzstd). `rm -rf build-mex`.
- [ ] **Step 6a:** Commit `feat(matlab): context and attribute slice through the MEX; object-model tests`.

### Task 8b: Vertical slice through real MATLAB in CI (controller)

**Wave 0. Depends on:** Task 8a. No build directory. Steps are numbered 7 to 10, continuing Task 8a. **Nothing of wave 1 starts until this task's CI run is green on both releases.** This task retires the hazard that, inside MATLAB, libiio's libxml2 and libzstd resolve to MATLAB's bundled copies (libxml2 2.13.8 and libzstd in R2025b; libxml2 2.10.3 and no libzstd in R2023b), and proves the M idioms of `tObjectModel` before twelve tasks copy them.

**Files:**
- Modify: `.github/workflows/ci.yml`
- Create: `.github/workflows/matlab-probe.yml`
- Fallback only: `deps/build-libxml2.sh`, `deps/build-libiio.sh`

**Interfaces:**
- Consumes: Task 8a.
- Produces: the `matlab` CI job, one per release (names `MATLAB bindings (R2025b)`, `MATLAB bindings (R2023b)`; artifacts `matlab-R2025b`, `matlab-R2023b`, each holding the content of `test-results/` at its top level: `matlab.xml`, `matlab.log`, `iio-mex-trace.log`, and `matlab_crash_dump.*` when MATLAB crashed); environment variables `IIO_MEX_TRACE` and `IIO_TEST_RESULTS` (folder for result files of tests) set for the MATLAB tests; workflow `.github/workflows/matlab-probe.yml` on branches `probe/**`.

- [ ] **Step 7:** Edit `.github/workflows/ci.yml`: change `branches: [main]` to `branches: [main, 'feat/**']` and append this job:

  ```yaml
    matlab:
      name: MATLAB bindings (${{ matrix.release }})
      runs-on: ubuntu-24.04
      timeout-minutes: 30
      strategy:
        fail-fast: false
        matrix:
          release: [R2025b, R2023b]
      steps:
        - uses: actions/checkout@v4

        - name: Install build dependencies
          run: |
            sudo apt-get update
            sudo apt-get install -y --no-install-recommends \
              ninja-build libxml2-dev libzstd-dev bison flex locales

        # Added after the wave 1a review, to this job and to `c-library` (both run tests/mex):
        # the gateway tests fail without an installed locale that has a decimal comma.
        - name: Generate a locale with a decimal comma
          run: |
            sudo locale-gen de_DE.UTF-8
            locale -a | grep -i -x 'de_DE\.utf-\?8'

        - name: Cache pinned libiio v1
          uses: actions/cache@v4
          with:
            path: deps/install
            key: libiio-${{ runner.os }}-${{ runner.arch }}-${{ hashFiles('deps/build-libiio.sh') }}

        - name: Build pinned libiio v1
          run: deps/build-libiio.sh

        - name: Set up MATLAB
          id: setup-matlab
          uses: matlab-actions/setup-matlab@v2
          with:
            release: ${{ matrix.release }}
            cache: true

        - name: Locate MATLAB
          run: |
            root="${{ steps.setup-matlab.outputs.matlabroot }}"
            [ -n "$root" ] || root=$(dirname "$(dirname "$(readlink -f "$(command -v matlab)")")")
            test -f "$root/extern/include/mex.h"
            test -x "$root/bin/glnxa64/mlint" || echo "::warning::no mlint in $root (Step 10 has a row for this)"
            echo "MATLAB_ROOT=$root" >> "$GITHUB_ENV"

        - name: Configure and build (mock tests with sanitizers, real MEX)
          run: |
            cmake -S . -B build-mex -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR="$MATLAB_ROOT"
            cmake --build build-mex

        - name: Gateway checks without MATLAB (G1 to G4)
          run: ctest --test-dir build-mex -L mex --output-on-failure

        - name: Prepare the result folder
          run: mkdir -p test-results

        - name: MATLAB tests (G5)
          uses: matlab-actions/run-tests@v2
          env:
            IIO_MEX_TRACE: ${{ github.workspace }}/test-results/iio-mex-trace.log
            IIO_TEST_RESULTS: ${{ github.workspace }}/test-results
          with:
            source-folder: matlab
            select-by-folder: matlab/tests
            test-results-junit: test-results/matlab.xml
            strict: true
            startup-options: -logfile test-results/matlab.log

        - name: Process exit with libiio loaded (run 1 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 2 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 3 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 4 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 5 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 6 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 7 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 8 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 9 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: Process exit with libiio loaded (run 10 of 10)
          uses: matlab-actions/run-command@v2
          with:
            command: addpath('matlab'); c = iio.Context(['emu:' fullfile(pwd, 'tests', 'resources', 'iio_fixture.xml')]); a = c.find_attr('hw_model'); disp(a.read()); p = matlab.io.xml.dom.Parser; exit

        - name: No crash dump after the exit checks
          run: |
            if ls ~/matlab_crash_dump.* >/dev/null 2>&1; then echo "exit check: crash dump found"; exit 1; fi

        - name: Collect crash dumps
          if: always()
          run: mkdir -p test-results && cp ~/matlab_crash_dump.* test-results/ 2>/dev/null || true

        - name: Upload MATLAB test results
          if: always()
          uses: actions/upload-artifact@v4
          with:
            name: matlab-${{ matrix.release }}
            path: test-results/
  ```

  The exit check leaves MATLAB with a context still open, ten times per release: libiio's exit handler calls `xmlCleanupParser()` on MATLAB's own libxml2 at process exit, which the `clear mex` tests cannot reach. Each run is its own `matlab-actions/run-command@v2` step (one MATLAB process per step), never `matlab -batch` in a shell `run:` step: with `setup-matlab@v2` on a public repository only the `run-command`, `run-tests` and `run-build` actions license MATLAB, and a bare `matlab -batch` ends with `License checkout failed / License Manager Error -1` (setup-matlab README, section Licensing, and its issue 89). The ten steps are written out because a workflow cannot loop over steps; the step that follows them keeps the crash-dump check. Crash dumps are copied into `test-results/` so that the artifact keeps a single root.

  The existing `c-library` job needs no change: its builds now also compile and run the mock gateway tests and `mex_api_map` on x86-64 and AArch64.
- [ ] **Step 8:** Write `.github/workflows/matlab-probe.yml`: `on.push.branches: ['probe/**']`, one job with the steps of job `matlab` (same release matrix) up to and including the build, then `matlab-actions/run-command@v2` with `command: addpath('matlab', 'matlab/probes'); run_probes`. Run `actionlint .github/workflows/ci.yml .github/workflows/matlab-probe.yml` if `actionlint` is installed. Expected: no message.
- [ ] **Step 9:** Commit `ci: matlab job on R2025b and R2023b, probe workflow`. Push the branch and wait for the `CI` run with the background command of gate G5. Expected: jobs `C library (ubuntu-24.04)`, `C library (ubuntu-24.04-arm)`, `MATLAB bindings (R2025b)` and `MATLAB bindings (R2023b)` green; in both MATLAB jobs the `MATLAB tests (G5)` step reports 8 passed tests of `tSlice`, 6 of `tFixture` and 7 of `tObjectModel`, and the exit check passes; each artifact holds `iio-mex-trace.log`. A red `tObjectModel` test is an M idiom that does not work as this plan assumes: correct `Object.m`, the stub pattern or "Conventions / MATLAB (M)" and the "MATLAB surface" outlines that use the idiom, and re-run, before wave 1 starts. Then push a branch `probe/selftest` holding `matlab/probes/p_selftest.m` (`fprintf('PROBE version %s\n', version)`), expect the log lines `PROBE version 25.2...` and `PROBE p_selftest OK`, and delete the branch.
- [ ] **Step 10:** If the `matlab` job is red, act on the first matching line; change nothing else:

  | Symptom in the job log | Cause | Action |
  |---|---|---|
  | `Gateway checks` fails only in `mx_conformance_real` or `mex_loadcheck` | the hosted runner needs more than `bin/glnxa64` and `sys/os/glnxa64` to load `libmx` outside MATLAB (both pass on the development machine) | print `ldd` of the binary in the job, add the missing directory to the test's `LD_LIBRARY_PATH`; if it cannot be made to load, restrict these two tests to `if(NOT DEFINED ENV{GITHUB_ACTIONS})` and record it in the plan's notes |
  | `Invalid MEX-file`, text names `libiio.so.1` or an undefined symbol | run path or link problem | `readelf -d matlab/iio_mex.mexa64`; the `RUNPATH` must contain `deps/install/lib` |
  | `tSlice` fails in `iio.Context(...)` with `iio:EINVAL`, or MATLAB crashes inside `xmlReadFile` / `xmlParse*` | MATLAB's bundled libxml2 does not work for libiio in-process | fallback F1 below |
  | every test passes but the step fails with a MATLAB crash at exit, or `matlabXmlStillWorks` fails | libiio's exit handler ran `xmlCleanupParser()` on MATLAB's libxml2 | fallback F1 below |
  | `libiioStaysMapped` fails | the pin did not take effect | check that `dlopen("libiio.so.1", RTLD_NOW \| RTLD_NODELETE)` resolves (print `dlerror()`), fix, re-run |
  | `clearMexAndReload` finds `iio_mex` still in `inmem` | the MEX is still locked | a handle leaked: `iio.internal.handle_count()` must be 0 before `clear mex` |
  | an exit-check step (`run k of 10`) fails, or `No crash dump after the exit checks` finds a dump, although every test passed | libiio's exit handler raced MATLAB's own use of libxml2 at process exit | fallback F1 below |
  | `mex_mlint` fails with exit code 77 (and the `Locate MATLAB` warning) | the runner's installation has no `mlint` | add `SKIP_RETURN_CODE 77` to the properties of `mex_mlint` and `mex_mlint_r2023b` and record it in `matlab/README.md` (G3 then runs only on the development machine) |
  | `Set up MATLAB` fails for R2023b only | that release cannot be installed on `ubuntu-24.04` | set `runs-on: ubuntu-22.04` for that matrix entry (`include:`), re-run |

**Fallback F1: private static libxml2 inside libiio** (verified on the development machine **[RUN]**: the resulting `libiio.so.1` has no `libxml2.so.2` dependency, exports no `xml*` symbol because `libiio.map` ends in `local: *`, and passes all 1305 checks of the research fixture checker). Apply only if Step 10 says so (it removes both the parse-time and the exit-time hazard on both releases):

1. Create `deps/build-libxml2.sh`: download `https://download.gnome.org/sources/libxml2/2.13/libxml2-2.13.8.tar.xz` (sha256 `277294cb33119ab71b2bc81f2f445e9bc9435b893ad15bb2cd2b0e859a0ee84a`) into `deps/src/`, unpack, and

   ```sh
   cmake -S "$src" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
   	-DCMAKE_INSTALL_PREFIX="$here/xml2" -DCMAKE_INSTALL_LIBDIR=lib \
   	-DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
   	-DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF \
   	-DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_HTTP=OFF \
   	-DLIBXML2_WITH_FTP=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF
   cmake --build "$src/build" && cmake --install "$src/build"
   ```

   Idempotent (skip when `deps/xml2/lib/libxml2.a` exists). Add `deps/xml2/` to `.gitignore`.
2. In `deps/build-libiio.sh`: call `"$here/build-libxml2.sh"` first, add `-DCMAKE_PREFIX_PATH="$here/xml2"` to the libiio configure line, change the stamp to `"$COMMIT cfg3"`.
3. In both CI jobs change the cache key to `hashFiles('deps/build-libiio.sh', 'deps/build-libxml2.sh')`.
4. Verify: `readelf -d deps/install/lib/libiio.so.1 | grep -c libxml2` prints `0`; `nm -D --defined-only deps/install/lib/libiio.so.1 | grep -c -i ' xml'` prints `0`; the whole `ctest` passes; push and re-run CI.
5. If MATLAB's `libzstd.so.1` is implicated instead (not expected: 1.5.5 on both sides), the same pattern applies with a static, position-independent libzstd passed as `-DLIBZSTD_LIBRARIES=<path>/libzstd.a` (not verified).

### Task 9: Data-path gateway foundations

**Wave 0. Depends on:** Task 8 (may be implemented while its CI run is in progress). **Build directory:** `build-mex`.

**Files:**
- Create: `matlab/mex/data_path.h` (header only), `tests/mex/dp_test.h` (header only)
- Test: `tests/mex/test_data_path.c`

**Interfaces:**
- Consumes: the handle table, `mx_util.h`, `mex_test.h` and the mock (Tasks 4 to 6); `tests/fixture_util.h`, `tests/fake_backend.h` (Task 2); `iio-conv.h` with `raw` (Task 1).
- Produces: `matlab/mex/data_path.h` and `tests/mex/dp_test.h` exactly as in "Shared contracts", with the registrations of "Conventions / Data-path objects". They are the contract of every wave 1 task of the data path.

- [ ] **Step 1:** Write `tests/mex/test_data_path.c`:
  - names: `"double"` → (`IIO_CONV_F64`, `mxDOUBLE_CLASS`), `"single"`, `"int8"` .. `"uint64"` each to
    its pair; `"int12"`, `""`, `"Double"`, `"logical"` → `-EINVAL`; `dp_class_from_id` of char,
    logical, cell → `-EINVAL`; scales `"none"`, `"iio"`, `"fullscale"`; `"full"` → `-EINVAL`.
  - `dp_per_channel`: `[]` → 0 and NULL; scalar 2, 4 channels, complex → `{2,2,2,2}`; `[2 3]`,
    4 channels, complex → `{2,2,3,3}`; `[2 3]`, 2 channels, real → `{2,3}`; 3 values for 2 columns,
    an int16 scalar, a complex value → `-EINVAL`. The test releases each result with `mxFree`.
  - `dp_check_matrix`: int16 complex 4×2 → (`I16`, true); double real 3×1; 0×2 accepted; char,
    logical, cell, sparse → `-EINVAL`; `mock_nd_array(mxINT16_CLASS, mxREAL, 4, 1, 2)` (3-D; `mxGetN` is 2) → `-EINVAL`.
    `dp_class_of_type(IIO_CONV_I16) == mxINT16_CLASS`, and for each of the ten names
    `dp_class_of_type(type) == cls` of `dp_class_from_name`.
  - `dp_opt_spec` + `dp_opt_gains`: `struct(class 'single', complex true, scale 'fullscale', gain 2)`,
    2 channels → `{F32, COLUMNS, FULLSCALE, complex_iq, saturate, !raw}`, `cls == mxSINGLE_CLASS`,
    `user_gain {2,2}`, `user_offset NULL`; `[]` → `{F64, COLUMNS, NONE}`, no gain; `dp_opt_check`
    with a field `colour` → `-EINVAL` and `bad == "colour"`; a 1×2 struct or a double as opts → `-EINVAL`.
  - `dp_test.h` on `emu` (`ctx = mt_open_context(fx.uri)`): `dp_dev(ctx, "emu-rx")` twice gives the same non-zero id; `dp_device_of` of the ids from `dp_chn`, `dp_buf`, `dp_mask(.., 2, "voltage0_i", "voltage0_q")`, `dp_bufstream`, `dp_block(bs, 32)` all equal the device pointer; the mask, the buffer stream and the block are owned, `gw_handle_parent(mask) == dev`, `gw_handle_parent(bs) == buf`, `gw_handle_parent(block) == bs`, `gw_handle_root(block) == ctx`; `gw_handle_destroy(bs)` makes the block id stale; `gw_handle_destroy(ctx)` with a started buffer stream and a block left open: no leak report. `dp_device_of(0) == NULL`; `dp_add_block(ptr, <stale id>) == 0`.
  - `dp_test.h` on `fake`: `ctx = dp_open_fake(fx.xml)`, `dp_bufstream`, `dp_block` twice, `mock_exit_matlab()` → `fake.log` is `"OBBXFFXC"` (cancel, blocks, close: nothing of an open buffer stream survives an unload). The same with `iio_buffer_stream_start` and `gw_handle_set_flags(bs, BS_STARTED)` before the exit → `"OBBSXsFFXC"` (a started buffer stream is stopped before it is closed).
- [ ] **Step 2:** Run G1 for `test_data_path` (it defines `gw_groups[]` as `{ &gw_group_mex, NULL }`). Expected: FAIL to compile.
- [ ] **Step 3:** Write the two headers.
- [ ] **Step 4:** Run G1 for `test_data_path`, then `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: 18 tests pass, no sanitizer report. `rm -rf build-mex`.
- [ ] **Step 5:** Commit `feat(mex): data-path conventions, option parsing and test builders`. Controller: push; gate G5 green (C library jobs on both architectures run `test_data_path`; the `matlab` job is unchanged) before wave 1 starts.

## Wave 1: one command group each (Tasks 10 to 21), in two sub-waves

Wave 1a (pilot): Tasks 12, 13 and 14 in parallel, then the "Wave 1a gate" below. Wave 1b: Tasks 10, 11 and 15 to 21 in parallel, then the "Wave 1 gate". The pilot puts the first three complete classes and their MATLAB tests through real MATLAB before nine more tasks copy the same idioms.

Every wave 1 task: build directory `build-mex-<group>`; builds only `test_cmd_<group>` and `iio-mex-real-<group>`; replaces the `gw_stub` entries of its own `cmd_<group>.c` and replaces or creates its own `.m` files; commits only the files under its **Files**. A MATLAB class of another task may still be a stub and a command of another group may still raise `iio:ENOSYS`: MATLAB tests are written against "MATLAB surface" and first run at the gate of their sub-wave.

### Task 10: Test hooks: a context with fake backend operations

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `test`. **Build directory:** `build-mex-test`.

**Files:**
- Modify: `matlab/mex/cmd_test.c`
- Test: `tests/mex/test_cmd_test.c`, `matlab/tests/tFakeBackend.m`

**Interfaces:**
- Consumes: `mx_util.h`; `tests/fake_backend.h` (Task 2), included by `cmd_test.c` and by no other file of the MEX; in the M test `EmuFixture.fakeContext` (Task 8), `iio.Context(AdoptHandle = h)`, `ctx.set_timeout` (Task 12), `dev.find_attr`, `dev.find_channel`, `dev.buffers` (Task 14), `attr.read`, `attr.write_raw` (Task 13), `iio.ChannelsMask`, `mask.channels`, `mask.sample_size` (Task 15), `buf.open`, `bs.sample_size`, `bs.mask` (Task 16), `iio.RxStream` (Task 21).
- Produces: the commands of group `test` in the "Command reference" (compiled only with `IIO_MEX_TEST_HOOKS`). **Commands produced:** `test_create_fake_context`, `test_fake_last_timeout`, `test_fake_last_write`, `test_fake_set`, `test_fake_log`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_test.c` with `MT_GROUPS(&gw_group_test);`, using the libiio C API directly on `gw_handle_ptr(id, GW_T_CONTEXT)`:
  - `id = test_create_fake_context(fx.xml)` is non-zero, `gw_handle_type(id) == GW_T_CONTEXT`, owned, the mock is locked; `iio_context_get_name` is `"fake"`; 8 devices.
  - `test_fake_last_timeout` is -12345; after `iio_context_set_timeout(ctx, 250)` it is 250.
  - `test_fake_last_write` is a 1-by-0 `uint8`; after a 47 000 byte buffer with `buf[i] = i % 256` was written with `iio_attr_write_raw(text of emu-phy, buf, 47000)` it is a 1-by-47000 `uint8` equal to the buffer.
  - `test_fake_log` is `""` (0x0 char) after creation; after `iio_buffer_open` of `emu-rx` buffer 0 with the mask `{voltage0_i, voltage0_q}` and `iio_buffer_close` it is `"OXC"`.
  - `test_fake_set("couple", "voltage1_i")`: a buffer stream opened with the same mask has `iio_device_get_sample_size(dev, iio_buffer_stream_get_channels_mask(bs)) == 6`; after `test_fake_set("couple", "")` it is 4. `test_fake_set("rx_frames", 1)`: with a 16-byte block on a new stream: enqueue, start, `iio_block_dequeue(blk, true) == 0`, enqueue again, `iio_block_dequeue(blk, true) == -EBUSY`. `test_fake_set("bogus", 1)` and `test_fake_set("busy", "x")`: `iio:EINVAL`.
  - a second `test_create_fake_context` raises `iio:EBUSY`; `gw_handle_destroy(id)`; `test_fake_log` still returns the log of the destroyed context (it ends in `C`); then creating succeeds again and `test_fake_last_timeout` is -12345, `test_fake_last_write` is 1-by-0 and `test_fake_log` is `""`.
  - `test_create_fake_context("/nonexistent.xml")` raises `iio:EINVAL` and leaves no handle; `test_create_fake_context(3)`: `iio:EINVAL`.
  - numeric locale (rule **Numeric locale**; `fake_create_context` is bracketed by `gw_c_numeric_enter` / `_leave`): after `gw_handle_destroy(id)`, inside `if (mt_comma_begin()) { ... mt_comma_end(); }`: `id = test_create_fake_context(fx.xml)`; `iio_channel_get_data_format(iio_device_find_channel(iio_context_find_device(ctx, "emu-rx"), "voltage2", false))` has `with_scale` true and `scale == 0.25` (it is 0 without the bracket); `gw_handle_destroy(id)`.
  - text with an embedded NUL: `test_create_fake_context` and `test_fake_set` (first argument, and a text value) with `char(0)` inside: `iio:EINVAL` ("How tests are written").
- [ ] **Step 2:** Write `matlab/tests/tFakeBackend.m` (`< IioTestCase`; no skip logic: a MEX built without test hooks must fail these tests). `ctx = tc.Fx.fakeContext()`, `rx = ctx.find_device("emu-rx")`:
  - `adoptFakeContext`: `ctx.name` is `'fake'`; `ctx.description` is `'Emulated fixture & test context (fake)'`; `numel(ctx.devices)` is 8.
  - `timeoutReachesBackend`: `ctx.set_timeout(250)`; `iio_mex('test_fake_last_timeout')` is 250; `ctx.set_timeout(Inf)`; it is -1.
  - `attributeReadThroughFake`: `ctx.find_device("emu-phy").find_attr("text").read()` is `'fake:text'`.
  - `binaryAttributeWrite`: `p = uint8(mod(0:46999, 256))`; `n = ctx.find_device("emu-phy").find_attr("text").write_raw(p)`; `n` is 47000; `iio_mex('test_fake_last_write')` equals `p` (class `uint8`, size 1-by-47000).
  - `coupledChannelWidensSample`: `iio_mex('test_fake_set', 'couple', 'voltage1_i')`; `mask = iio.ChannelsMask(rx)`; `mask.channels = [rx.find_channel("voltage0_i") rx.find_channel("voltage0_q")]`; `mask.sample_size` is 4; `bs = rx.buffers(1).open(mask)`; `bs.sample_size` is 6 and `numel(bs.mask.channels)` is 3; `bs.close()`. Then `r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame=4, OutputDataType="int16", NumBlocks=1, Timeout=0)`; `[d, v] = r.read()`: `v` is true and `d` equals `int16([0 1; 3 4; 6 7; 9 10])`.
  - `closeOrderOnShutdown`: `r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame=4, NumBlocks=2, OutputDataType="int16", Complex=true)`; `iio_mex('test_fake_log')` is `'OBBEES'`; `iio.internal.shutdown()`; `iio_mex('test_fake_log')` is `'OBBEESXsFFXC'`; `iio.internal.handle_count()` is 0; `r.read()` throws `iio:EBADF`; a new `tc.Fx.fakeContext()` works.
- [ ] **Step 3:** Run G1 for `test_cmd_test`. Expected: FAIL (`iio:ENOSYS`: stubs).
- [ ] **Step 4:** Implement `cmd_test.c`: one `static` "alive" flag set by `test_create_fake_context` and cleared by the context's destroy op (after `iio_context_destroy`); `fake_reset()` only on creation.
- [ ] **Step 5:** Run G1 for `test_cmd_test`, G2 (`iio-mex-real-test`), G3 (`tFakeBackend.m`), G4 (`--group test`). Expected: all pass, no sanitizer report. `rm -rf build-mex-test`.
- [ ] **Step 6:** Commit `test(mex): fake-backend context for events, triggers, timeouts, coupled channels and binary attribute writes`.

### Task 11: Top level, scan and library queries

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `toplevel`. **Build directory:** `build-mex-toplevel`.

**Files:**
- Modify: `matlab/mex/cmd_toplevel.c`
- Create: `matlab/+iio/version.m`, `matlab/+iio/backends.m`, `matlab/+iio/has_backend.m`, `matlab/+iio/strerror.m`, `matlab/+iio/scan_contexts.m`
- Test: `tests/mex/test_cmd_toplevel.c`, `matlab/tests/tTopLevel.m`

**Interfaces:**
- Consumes: `mx_util.h`; `iio.internal.params` (Task 7); commands `context_get_version_major`, `context_get_version_minor`, `context_get_version_patch`, `context_get_version_tag` of Task 12 (called with `uint64(0)` by `iio.version`).
- Produces: the commands of group `toplevel` in the "Command reference" and the package functions of "MATLAB surface". **Commands produced:** `strerror`, `has_backend`, `get_builtin_backends`, `scan`, `scan_destroy`, `scan_get_results`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_toplevel.c` with `MT_GROUPS(&gw_group_toplevel);`:
  - `strerror(-22)` is `"Invalid argument (-22)"`; `(0)` is `"Success (0)"`; `(-99999)` is `"Unknown error -99999"`; `strerror("x")`: `iio:EINVAL`.
  - `has_backend([], "emu")`, `"xml"`, `"local"`, `"ip"` are true; `"usb"`, `"bogus"`, `""` are false; `has_backend([], 3)`: `iio:EINVAL`.
  - `get_builtin_backends()` is a 1-by-4 cell of char `{"local", "ip", "emu", "xml"}`; with one argument: `iio:EINVAL`.
  - `s = scan({log_level: 1}, "emu")` is non-zero, `gw_handle_type(s) == GW_T_SCAN`, the mock is locked; `scan_get_results(s)` with two outputs gives two 1-by-0 cells; with one output a 1-by-0 cell and `mock_live()` grew by exactly 1; `scan_destroy(s)`; count 0 and unlocked; `scan_destroy(s)` again and `scan_destroy(0)`: no error; `scan_get_results(s)`: `iio:EBADF`.
  - `s = scan({log_level: 1}, [])` (libiio's default list, `"local"` in the pinned build; the number of results depends on the machine): succeeds; both outputs of `scan_get_results(s)` are cells of the same size whose elements are all char rows; `scan_destroy(s)`.
  - `scan_destroy(id of an mt_open_context context)`: `iio:EBADF`, and the context is still alive.
  - numeric locale (`iio_scan` is bracketed by `gw_c_numeric_enter` / `_leave`): inside `if (mt_comma_begin()) { ... mt_comma_end(); }`: `s = scan({log_level: 1}, "emu")` succeeds, `scan_get_results(s)` gives two 1-by-0 cells, `scan_destroy(s)`. (No scan result depends on the locale on this machine; the block proves that the command restores the locale: `mt_comma_end` fails otherwise.)
  - text with an embedded NUL: `scan([], <"emu" with a NUL>)` and `has_backend([], <"emu" with a NUL>)`: `iio:EINVAL`.
- [ ] **Step 2:** Write `matlab/tests/tTopLevel.m` (`< IioTestCase`):
  - `version`: `v = iio.version()`; `v.major` is 1; `v.minor`, `v.patch` are non-negative doubles; `v.tag` is a non-empty `char`.
  - `backends`: `iio.backends()` equals `{'local', 'ip', 'emu', 'xml'}`.
  - `hasBackend`: true for `"emu"` and `'xml'`, false for `"usb"`.
  - `strerror`: `iio.strerror(-22)` equals `'Invalid argument (-22)'`.
  - `scanEmpty`: `r = iio.scan_contexts("emu", LogLevel = "nolog")`; `size(r)` is `[1 0]`; `fieldnames(r)` is `{'uri'; 'description'}`; `iio.internal.handle_count()` is 0.
  - `scanArguments`: `iio.scan_contexts("emu", Timeout = 1.5)` throws `iio:EINVAL`; `iio.scan_contexts("emu", LogLevel = "loud")` throws an identifier starting with `MATLAB:`; `iio.scan_contexts('emu', LogLevel = 'nolog')` (char arguments) returns a 1-by-0 struct.
- [ ] **Step 3:** Run G1 for `test_cmd_toplevel`. Expected: `test_cmd_toplevel` fails.
- [ ] **Step 4:** Implement the commands and the five functions.
- [ ] **Step 5:** Run again. Expected: `test_cmd_toplevel` passes; `iio-mex-real-toplevel` compiles; mlint clean; `check_api.py --group toplevel` prints `0 error(s)`. `rm -rf build-mex-toplevel`.
- [ ] **Step 6:** Commit `feat(matlab): library queries and context scan`.

### Task 12: Context

**Wave 1a (pilot). Depends on:** Task 9. **Group:** `context`. **Build directory:** `build-mex-context`.

**Files:**
- Modify: `matlab/mex/cmd_context.c`, `matlab/+iio/Context.m` (replaces the Task 8 version)
- Test: `tests/mex/test_cmd_context.c`, `matlab/tests/tContext.m`

**Interfaces:**
- Consumes: `mx_util.h`; `iio.internal.params`, `iio.internal.timeout_ms` (Task 7); `iio.Device(parent, handle)` and `iio.Attr(parent, handle)` (stubs from Task 7, real classes from Tasks 14 and 13).
- Produces: the commands of group `context` in the "Command reference" (the three of Task 8 keep their behaviour) and `iio.Context` as in "MATLAB surface". **Commands produced:** `context_get_version_major`, `context_get_version_minor`, `context_get_version_patch`, `context_get_version_tag`, `context_get_xml`, `context_get_name`, `context_get_description`, `context_get_attrs`, `context_get_devices`, `context_find_device`, `context_set_timeout`, `context_get_params`, `context_ping`. The three commands Task 8 implemented in this file stay as they are.

- [ ] **Step 1:** Write `tests/mex/test_cmd_context.c` with `MT_GROUPS(&gw_group_context);`:
  - errors: `create_context([], "bogus:foo")`: `iio:ENOSYS`; `([], "emu:/nonexistent.xml")`: `iio:EINVAL`; `({timeout_ms: -5}, fx.uri)`: `iio:EINVAL`; `({bogus: 1}, fx.uri)`: `iio:EINVAL`; after each, `gw_handle_count() == 0`.
  - `mt_check_arities` with the sixteen rows of the reference. The uri `[]` (C `NULL`): with `setenv("IIOD_REMOTE", fx.uri, 1)`, `create_context([], [])` gives a context whose name is `"emu"`; with `IIOD_REMOTE=bogus:foo` it raises `iio:ENOSYS`; `unsetenv` afterwards (the `"local:"` default depends on the machine and is not tested). `create_context([], "")` (a value, not "absent"): `iio:ENOSYS`. The branch "`gw_handle_add` failed" of `create_context` cannot be reached from a test (the handle table has no failure injection): reviewed by reading, and the test file says so.
  - `ctx = create_context([], fx.uri)`: name `"emu"`; description `"Emulated fixture & test context"`; version 1, 2, 3 and tag `"v1.2.3-fixture"`; `context_get_version_major(0)` is 1 and `context_get_version_tag(0)` is a non-empty string different from the fixture's.
  - attributes: `context_get_attrs(ctx)` is a 1-by-4 `uint64` whose elements (through `iio_attr_get_name` on `gw_handle_ptr`) are `escaped`, `fixture`, `hw_model`, `num`; a second call returns the same four ids; `context_find_attr(ctx, "num")` equals element 3 (0-based); every attr id has `gw_handle_parent == ctx`.
  - devices: `context_get_devices(ctx)` is a 1-by-8 `uint64` whose device ids are `hwmon0`, `iio:device0`, `iio:device1`, `iio:device2`, `iio:device3`, `iio:device4`, `iio_sysfs_trigger`, `trigger0`; `context_find_device` with `"iio:device2"`, `"emu-phy"` and `"phy"` gives element 3; `"nope"` gives 0.
  - `context_get_params(ctx)`: struct with exactly the five fields, values 4, 3, 5, 1000, 0.
  - `context_set_timeout(ctx, 2500)` then `timeout_ms` is 2500; `(ctx, 0)` gives 1000; `(ctx, -1)` gives -1; `(ctx, -2147483648)` gives 0; `(ctx, -5)`: `iio:EINVAL` and the value is unchanged; `(ctx, 2.5)`: `iio:EINVAL`.
  - two contexts (the timeout belongs to one context): a second `create_context({log_level: 1, timeout_ms: 123, flags: 1}, fx.uri)` has params 1, 3, 5, 123, 1 while the first still reports its own timeout.
  - `context_get_xml(ctx)`: starts with `<?xml version="1.0" encoding="utf-8"?><!DOCTYPE context [`, contains `<context name="emu"` and `emu-phy`, is longer than 9000 characters and does not contain `value="auto"`; for the second context (flags 1) it contains `value="auto"`.
  - `context_ping(ctx)` succeeds; on `create_context([], "xml:<fx.xml>")`: `iio:ENOTSUP`, and that context's name is `"xml"` and `timeout_ms` 0.
  - text with an embedded NUL: `context_find_device(ctx, <"phy", NUL, "junk">)`, `context_find_attr(ctx, <"num", NUL>)` and `create_context([], <fx.uri with its last character replaced by a NUL>)`: `iio:EINVAL`.
  - numeric locale (added after the wave 1a review; `iio_create_context` and `iio_context_get_xml` are bracketed by `gw_c_numeric_enter` / `_leave`): inside `if (mt_comma_begin()) { ... mt_comma_end(); }`: `context_get_xml(ctx)` (the context was made under `"C"`) contains `scale="0.250000"` and no `scale="0,`; for `create_context([], fx.xml_uri)` and `create_context([], fx.uri)`: the data format of `emu-rx`/`voltage2` (libiio C API on `gw_handle_ptr`) has `with_scale` and `scale == 0.25`, and its XML contains `scale="0.250000"` and no `scale="0,`; `context_destroy`. Without the brackets the `xml:` scale is 0 and every XML says `0,250000` **[RUN]**. (The `xml:` backend has no channel offset: only the scale is asserted.)
  - out of memory: `mt_oom_sweep(1, "create_context", 2, mt_empty(), mt_str(fx.uri))` and `mt_oom_sweep(1, "context_get_xml", 1, mt_u64(ctx))` each return ≥ 1; `mex_shutdown` afterwards; no leak report at exit.
  - teardown: `context_destroy(ctx)`: every device and attr id obtained above gives `iio:EBADF` in `context_get_name` / as argument; `context_destroy` of a stale id and of 0: no error; `context_destroy(id of a scan-typed entry added with gw_handle_add)`: `iio:EBADF`.
- [ ] **Step 2:** Write `matlab/tests/tContext.m` (`< IioTestCase`):
  - `identity`: `ctx = iio.Context(tc.Fx.Uri)`: `ctx.uri` equals `tc.Fx.Uri`; `name` `'emu'`; `description` `'Emulated fixture & test context'`; `version` equals `struct('major', 1, 'minor', 2, 'patch', 3, 'tag', 'v1.2.3-fixture')`.
  - `charAndStringUri`: `iio.Context(char(tc.Fx.Uri))` and `iio.Context(string(tc.Fx.Uri))` both work.
  - `attrs`: `{ctx.attrs.name}` equals `{'escaped', 'fixture', 'hw_model', 'num'}`; class `iio.Attr`; size `[1 4]`; `ctx.find_attr("num") == ctx.attrs(4)`; `isempty(ctx.find_attr("nope"))`.
  - `devices`: `numel(ctx.devices)` is 8, class `iio.Device`; `ctx.find_device("emu-phy") == ctx.find_device("iio:device2")` and `== ctx.find_device("phy")` and `== ctx.devices(4)`; `ctx.find_device("emu-phy") ~= ctx.devices(1)`; `isempty(ctx.find_device("nope"))` with class `iio.Device`.
  - `params`: default context: `ctx.params.timeout_ms` 1000, `log_level` 4; `iio.Context(uri, Timeout = 123, LogLevel = "nolog", XmlIncludeValues = true).params` has `timeout_ms` 123, `log_level` 1, `flags` 1; `Timeout = Inf` gives -1; `iio.Context(uri, StderrLevel = "debug", TimestampLevel = "error").params` has `stderr_level` 5, `timestamp_level` 2 and `log_level` 4.
  - `timeoutIsPerContext` (the timeout belongs to one context): two contexts on the same URI; `a.set_timeout(2500)`; `a.params.timeout_ms` is 2500 and `b.params.timeout_ms` is 1000; `a.set_timeout(Inf)` gives -1; `a.set_timeout(iio.Context.TIMEOUT_NONBLOCK)` gives 0; `a.set_timeout(iio.Context.TIMEOUT_INFINITE)` gives -1; `a.set_timeout(iio.Context.TIMEOUT_BACKEND)` gives 1000; `a.set_timeout(-5)` throws `iio:EINVAL`; `a.set_timeout(1.5)` throws `iio:EINVAL`.
  - `xml`: `startsWith(ctx.xml, '<?xml version="1.0"')`; contains `'emu-phy'`; with `XmlIncludeValues = true` contains `'value="auto"'`.
  - `ping`: no error on emu; `iio.Context(tc.Fx.XmlUri).ping()` throws `iio:ENOTSUP`, and that context's `name` is `'xml'`.
  - `createErrors`: `iio.Context("bogus:foo")` throws `iio:ENOSYS`; `iio.Context("emu:/nonexistent.xml")` throws `iio:EINVAL`; `iio.Context(tc.Fx.Uri, LogLevel = "loud")` throws an identifier starting with `MATLAB:`; afterwards `iio.internal.handle_count()` is 0.
  - `deleteInvalidatesChildren`: `d = ctx.find_device("emu-phy")`; `delete(ctx)`; `isvalid(ctx)` is false; `d.is_trigger` throws `iio:EBADF`; `delete(ctx)` again does not throw.
  - `childKeepsContextAlive`: `d = iio.Context(tc.Fx.Uri).find_device("emu-phy")`; `d.id` equals `'iio:device2'`; `iio.internal.handle_count()` is 2; `clear d`; the count is 0.
- [ ] **Step 3:** Run G1 for `test_cmd_context`. Expected: `test_cmd_context` fails.
- [ ] **Step 4:** Implement the commands and `Context.m`.
- [ ] **Step 5:** Run again. Expected: `test_cmd_context` and `test_slice` pass (build both targets); `iio-mex-real-context` compiles; mlint clean; `check_api.py --group context` prints `0 error(s)`. `rm -rf build-mex-context`.
- [ ] **Step 6:** Commit `feat(matlab): iio.Context and the context commands`.

### Task 13: Attributes

**Wave 1a (pilot). Depends on:** Task 9. **Group:** `attr`. **Build directory:** `build-mex-attr`.

**Files:**
- Modify: `matlab/mex/cmd_attr.c`, `matlab/+iio/Attr.m` (replaces the Task 8 version)
- Test: `tests/mex/test_cmd_attr.c`, `matlab/tests/tAttr.m`

**Interfaces:**
- Consumes: `mx_util.h`; `mt_open_context`; in the M test `ctx.find_device` (Task 12) and `dev.find_attr` (Task 14).
- Produces: the commands of group `attr` in the "Command reference" and `iio.Attr` as in "MATLAB surface". **Commands produced:** `attr_get_name`, `attr_get_filename`, `attr_get_static_value`, `attr_write_raw`, `attr_write_string`, `attr_read_bool`, `attr_read_longlong`, `attr_read_double`, `attr_write_bool`, `attr_write_longlong`, `attr_write_double`, `attr_get_range`, `attr_get_available`. It also completes the command Task 8 implemented in this file with its optional second argument (`as_bytes`). Behaviour beyond the reference:

  | Command | Arguments | Result | Behaviour |
  |---|---|---|---|
  | `attr_get_name`, `attr_get_filename` | H | `char` | |
  | `attr_get_static_value` | H | `char`, or `[]` when the attribute has none | only context attributes have one; `''` is a valid value |
  | `attr_read_raw` | H [, `as_bytes` logical = false] | `char`, or 1-by-N `uint8` | read into an `mxMalloc` buffer of 32768 bytes; while the return value is `>= len` or is `-EFBIG` and `len < 16 MiB`, retry with `4 * len` (sizes 32768, 131072, 524288, 2097152, 8388608, 33554432). If the value still does not fit the last buffer (`-EFBIG`, or a return value `>= len`): `iio:EFBIG` on every backend, never a truncated value. emu returns exactly `len` both for a truncated value and for one of `len - 1` characters, so a value of 32767 characters is read twice, and one of exactly 32 MiB - 1 characters is refused. `char`: the text up to the first NUL, wherever the backend left the rest of the buffer (the command writes the NUL at `ret`), read as UTF-8: text that is not UTF-8 (a Latin-1 degree sign, binary data) is not preserved, because `mxCreateString` puts U+FFFD where a byte sequence is invalid (measured on the real `libmx` of both releases); the byte form is exact. Bytes: the `ret` bytes exactly as libiio counted them (emu, local and network attributes include the terminating NUL; context attributes do not). A negative return value outside the errno range (below -4095; a faulty backend) is `iio:EIO`, here and in the two write commands below, never "0 = success". **Limit, by decision:** the legacy text protocol of iiod reports a too-small buffer as `-EIO` (`iiod_client_attr_read`, `deps/src/libiio/iiod-client.c`), which cannot be told from a real I/O error; `-EIO` is not retried, so over such a server an attribute of 32767 bytes or more is `iio:EIO` (sysfs attributes are at most one page; stated in `matlab/README.md`, Task 26) |
  | `attr_write_raw` | H, `uint8` vector | `double`: bytes written, as libiio counted them (not `numel`) | `iio_attr_write_raw(attr, data, numel)` straight from the input array: no copy, no `strlen`; an empty input passes `gw_arg_bytes`' non-NULL pointer and length 0. Result first ("write, then count" of Global Constraints): `gw_ret_double(c, 0, 0)`, the write, `gw_set_double` |
  | `attr_write_string` | H, `char` | `double`: bytes written, as libiio counted them | `iio_attr_write_string` (sends the NUL). Result first, as `attr_write_raw`. Text with an embedded NUL is `iio:EINVAL` (`gw_arg_str`): binary data is written with `attr_write_raw` |
  | `attr_read_bool` | H | `logical` | |
  | `attr_read_longlong` | H | `int64` | never through `double` |
  | `attr_read_double` | H | `double` | |
  | `attr_write_bool` | H, `logical` | none | |
  | `attr_write_longlong` | H, integer | none | `gw_arg_int` over the full `long long` range |
  | `attr_write_double` | H, `double` or `single` scalar [, `exact` logical = true] | none | An integer class is `iio:EINVAL` (the message names `attr_write_longlong`): `int64(2^53 + 1)` would become another number on the way through `double`. `exact = false`: `iio_attr_write_double` (libiio's `"%f"`, six decimals). `exact = true`: under the `"C"` numeric locale (`gw_c_numeric_enter`), the text is `"%.*f"` with the smallest number of decimals d in 0..40 for which `strtod` returns the same double, or `"%.17g"` if there is none; written with `iio_attr_write_string`. Non-finite values: `iio:EINVAL` |
  | `attr_get_range` | H | 1-by-3 `double` `[min step max]` | inside `gw_c_numeric_enter` / `_leave` (libiio parses with `sscanf("%lf")`) |
  | all five `attr_write_*` | | | each libiio write call is inside `gw_c_numeric_enter` / `_leave` (emu checks the value against an `_available` range with `sscanf`); a write the backend refuses is the command's error |
  | `attr_get_available` | H | 1-by-N cell of `char` | parked (`gw_park`) before the cell is built; never freed by the command |

  Fixed-notation text is used for exact doubles because the kernel's IIO fixed-point parser does not accept exponents. Measured **[RUN]**: 0.1 gives `0.1`; 1e-9 gives `0.000000001`; pi gives `3.141592653589793`; 2.4e9 gives `2400000000`; 123456789.125 gives itself; 5e-324 falls back to `4.9406564584124654e-324`; 1e-30 gives `0.000000000000000000000000000001`; 1e-40 gives 40 decimals; 1.5e-40 falls back to `1.5000000000000001e-40`. Two consequences of `exact = true` being the default, stated in the class help and in `matlab/README.md` (Task 26): (1) every digit is sent, and the kernel's fixed-point parser truncates extra decimals where libiio's `"%f"` rounds to six, so a computed value just below a grid point lands one step lower than through libiio or pyadi-iio (`2.9999999999999996` is sent as such; `"%f"` sends `3.000000`): round in MATLAB first, or use `Exact = false`; (2) a value that needs more than 40 decimals is sent with an exponent, which a kernel driver is expected to refuse (an error from the driver, not a wrong value).

- [ ] **Step 1:** Write `tests/mex/test_cmd_attr.c` with `MT_GROUPS(&gw_group_attr);`. Handles come from `mt_open_context(fx.uri)` and `gw_handle_borrow(GW_T_ATTR, <attr from the libiio C API>, ctx, &id)`; `phy` = device `emu-phy`:
  - names: `attr_get_name` and `attr_get_filename` of `phy` attribute `calib_mode` are both `"calib_mode"`; for attribute `gain_control_mode` of `phy` input channel `voltage0` the name is `"gain_control_mode"` and the filename `"in_voltage0_gain_control_mode"`.
  - static value: context attribute `hw_model` gives `"Emulated Test Board"`; `phy` attribute `calib_mode` gives `[]`.
  - reads: `attr_read_raw(calib_mode)` is `"auto"`; with `as_bytes = true` a 1-by-5 `uint8` `[97 117 116 111 0]`; context attribute `hw_model` as bytes is 1-by-19 (no NUL); `empty` is `""` (a 0x0 char); `novalue`: `iio:ENOENT`; `attr_read_raw(a, 3)`: `iio:EINVAL`.
  - typed reads: `attr_read_bool(flag)` true; `attr_read_bool(frac)` false (text `0.5`, libiio's rule); `attr_read_longlong(hex)` is `int64` 16; `attr_read_longlong(gain)` 10; `attr_read_double(dbl)` 3.25; `attr_read_double(frac)` 0.5; `attr_read_longlong(text)`, `attr_read_double(text)`, `attr_read_bool(text)`: `iio:EINVAL`; `attr_read_longlong(novalue)`: `iio:ENOENT`.
  - string writes: `attr_write_string(calib_mode, "manual")` returns 7 and the attribute reads `"manual"`; `"bogus"`: `iio:EINVAL` and it still reads `"manual"`; `attr_write_string(calib_mode_available, "x")`: `iio:EACCES`; `attr_write_string(hw_model, "x")` and `attr_write_raw(hw_model, 'x')`: `iio:EPERM`; `attr_write_string(text, 5)`: `iio:EINVAL`; `attr_write_string(text, <"ab", NUL, "cd">)`: `iio:EINVAL`, the message contains `embedded NUL` and the attribute is unchanged.
  - typed writes: `attr_write_bool(flag, false)` then `"0"`; `attr_write_longlong(hex, int64 9007199254740993)` then the text is `"9007199254740993"` and `attr_read_longlong` returns exactly that; `(hex, int64 minimum)` reads back `INT64_MIN`; `(hex, double 17)` then `"17"`; `(hex, double 2.5)`: `iio:EINVAL`; `attr_write_longlong(gain, 999)`: `iio:EINVAL` (emu range check against `gain_available`).
  - doubles: `attr_write_double(dbl, 1e-9)` then `"0.000000001"`; `(dbl, 0.1)` then `"0.1"`; `(dbl, 2.4e9)` then `"2400000000"`; `(dbl, pi)` then `"3.141592653589793"`; `(dbl, -0.5)` then `"-0.5"`; `(dbl, 1e-9, false)` then `"0.000000"`; `(dbl, 5.0, false)` then `"5.000000"`; `(dbl, NaN)` and `(dbl, Inf)`: `iio:EINVAL`; `(dbl, 1e-30)` then `"0.000000000000000000000000000001"`; `(dbl, 1e-40)` then `"0.0000000000000000000000000000000000000001"` (exactly 40 decimals); `(dbl, 1.5e-40)` then `"1.5000000000000001e-40"` (the fallback); `(dbl, 2.9999999999999996)` then `"2.9999999999999996"`; `(dbl, int64 9007199254740993)`, `(dbl, uint64 1)`, `(dbl, int32 3)`: `iio:EINVAL`; `(dbl, single 0.5)` then `"0.5"`.
  - raw writes: `attr_write_raw(text, bytes "abcdef")` returns 6 and the text is `"abcdef"`; a 47 000 byte payload `p[i] = 'a' + i % 26` returns 47000, `attr_read_raw(text)` returns 47000 characters equal to it (buffer growth), and as bytes a 1-by-47001 `uint8` ending in 0; the same payload with `p[10] = p[20000] = 0` still returns 47000 (emu keeps only the first 10 characters: the count proves the length was passed); a `double` vector as payload: `iio:EINVAL`; an empty `uint8` payload (0x0 and 1x0): no sanitizer report, and the call returns `double` 0 (measured on emu).
  - available: `mt_oom_sweep(1, "attr_get_available", 1, mt_u64(calib_mode_available))` returns ≥ 1 (the list is parked: no leak report); `attr_get_available(calib_mode_available)` is a 1-by-4 cell `auto manual tx_quad rf_dc_offs`; `attr_get_range(gain_available)` is `[0 2 70]`; `(frac_available)` is `[0 0.25 1]`; `attr_get_range(calib_mode_available)` and `attr_get_available(gain_available)`: `iio:ENOTSUP`; `attr_get_available(calib_mode)` and `attr_get_range(calib_mode)`: `iio:ENXIO`.
  - on an `mt_open_context("xml:<fx.xml>")` context: `attr_read_raw` of a device attribute: `iio:ENOSYS`; `attr_get_name` works.
  - a device handle where an attr is expected: `iio:EBADF`.
  - `mt_check_arities` with the fourteen rows of the reference.
  - read-buffer growth beyond what emu can show, on a backend the test defines itself (`struct iio_backend` with `read_attr` only, context from `iio_create_context_from_xml(&params, fx.xml_uri, &probe_backend, "(probe)", NULL, NULL, 0)`, registered with `gw_handle_add`); its `read_attr` records every `len` it is called with: (a) `-EFBIG` while `len < 40001`, then a value of 40000 characters: the text has 40000 characters and the calls were 32768, 131072; (b) always `-EFBIG`: `iio:EFBIG` after exactly the six sizes of the table above; (c) always fills the buffer and returns `len`: `iio:EFBIG` after the same six sizes, also with `as_bytes`; (d) `-EIO`: `iio:EIO` after exactly one call; (e) fills the whole buffer with `'z'` (no NUL anywhere) and returns 5: the text is `"zzzzz"` and the bytes are 1-by-5; returns `len - 1`: 32767 characters after one call; returns 0: `''` and a 1-by-0 `uint8`; (f) returns -4294967296: `iio:EIO` (not the dispatcher's `iio:EFAULT`). On emu: a 32767 byte payload written to `text` reads back as 32767 characters.
  - writes on the same backend, whose `write_attr` records the bytes, counts its calls and returns a settable value: `attr_write_raw` of `"ab\0def"` (6 bytes) returns 6 and the backend received those 6 bytes; `attr_write_string("xyz")` returns 4; `attr_write_double(0.1)` sends `"0.1"` and its NUL; `attr_write_longlong(-7)` sends `"-7"`; `attr_write_bool(true)` sends `"1"`. With the backend returning 2: `attr_write_raw` of 6 bytes and `attr_write_string("abcdef")` return 2. With `-EBUSY`: each of `attr_write_raw`, `attr_write_string`, `attr_write_bool`, `attr_write_longlong`, `attr_write_double` (exact and not) raises `iio:EBUSY`. With -4294967296: `attr_write_raw`, `attr_write_string`, `attr_write_double`: `iio:EIO`. Result first: `mt_oom_sweep(1, "attr_write_raw", ...)` returns ≥ 1 and `mt_oom_sweep(1, "attr_write_string", ...)` ≥ 2, and after each sweep the backend was written exactly once.
  - numeric locale (rewritten after the wave 1a review: the first version detected the locale with `localeconv()`, which reads the thread locale, so a command that failed to restore the locale switched the check off): inside `if (mt_comma_begin()) { ... mt_comma_end(); }`, on the emu context made before the block: `attr_write_double(dbl, 0.1)` reads `"0.1"`; `(dbl, -1234.5)` reads `"-1234.5"` and `attr_read_double` returns -1234.5; `(dbl, 2.5, false)` reads `"2.500000"`; `attr_get_range(frac_available)` is `[0 0.25 1]` and `(gain_available)` `[0 2 70]`; `attr_write_double(frac, 0.75)` reads `"0.75"`; `attr_write_string(frac, "0.25")` returns 5; `attr_write_raw(frac, "0.5" with its NUL)` returns 4; `attr_write_string(frac, "0.3")` and `attr_write_double(frac, 1.25)`: `iio:EINVAL` (emu's range check, working). `uselocale((locale_t)0) == LC_GLOBAL_LOCALE` before and after the block.
- [ ] **Step 2:** Write `matlab/tests/tAttr.m` (`< IioTestCase`; helper `a = tc.phyAttr(ctx, name)` is `ctx.find_device("emu-phy").find_attr(name)`):
  - `names`: `a.name` and `a.filename` are `'calib_mode'`; `a.static_value` is `[]` (`isa(..., 'double')` and empty); `ctx.find_attr("hw_model").static_value` is `'Emulated Test Board'`.
  - `read`: `read()` and `value` are `'auto'`; `read_raw()` is `uint8([97 117 116 111 0])`; attribute `empty` reads `''`; `novalue` throws `iio:ENOENT`.
  - `typedRead`: `flag.read_bool()` is `true` (class `logical`); `hex.read_longlong()` is `int64(16)` (class `int64`); `dbl.read_double()` is 3.25; `text.read_longlong()` throws `iio:EINVAL`.
  - `writeString`: `a.write_string("manual")`; `a.read()` is `'manual'`; `a.value = 'auto'`; `a.read()` is `'auto'`; `a.write_string("bogus")` throws `iio:EINVAL`; the `_available` attribute throws `iio:EACCES`; a context attribute throws `iio:EPERM`.
  - `int64Exact` (no value passes through double): `hex.write_longlong(int64(9007199254740993))`; `hex.read()` is `'9007199254740993'`; `hex.read_longlong()` equals `int64(9007199254740993)`; `hex.write_longlong(intmin('int64'))` reads back `intmin('int64')`. `hex.write_longlong(uint64(2450000000))` reads `'2450000000'`; `hex.write_longlong(2.5)` and `hex.write_longlong(intmax('uint64'))` throw `iio:EINVAL`.
  - `doubleExact`: `dbl.write_double(1e-9)` reads `'0.000000001'`; `0.1` reads `'0.1'`; `2.4e9` reads `'2400000000'`; `dbl.write_double(1e-9, Exact = false)` reads `'0.000000'`; `dbl.write_double(NaN)` throws `iio:EINVAL`.
  - `writeDispatch`: on `text`: `write("abc")` reads `'abc'`; `write(true)` reads `'1'`; `write_bool(false)` reads `'0'`; `write(17)` reads `'17'`; `write(int32(-5))` reads `'-5'`; `write(0.25)` reads `'0.25'`; `write(2400000000)` reads `'2400000000'`.
  - `writeRaw` (binary payload by byte count, emu half): `text.write_raw(uint8('abcdef'))` returns 6; `p = uint8(97 + mod(0:46999, 26))`; `text.write_raw(p)` returns 47000; `text.read()` equals `char(p)`; `numel(text.read_raw())` is 47001; `text.write_raw([65 66 67])` (a `double` row, converted by the `arguments` block) returns 3 and `text.read()` is `'ABC'`; `text.write_raw(uint8([68; 69]))` (a column) returns 2.
  - `available`: `get_available()` of `calib_mode_available` equals `{'auto', 'manual', 'tx_quad', 'rf_dc_offs'}`; `get_range()` of `gain_available` equals `[0 2 70]`; `get_range()` of `calib_mode_available` throws `iio:ENOTSUP`; `get_available()` of `calib_mode` throws `iio:ENXIO`.
  - `xmlBackend`: on `iio.Context(tc.Fx.XmlUri)` a device attribute `read()` throws `iio:ENOSYS` while `name` works.
  - `stale`: after `delete(ctx)`, `a.read()` throws `iio:EBADF` and `a.name` is still `'calib_mode'`.
- [ ] **Step 3:** Run G1 for `test_cmd_attr`. Expected: `test_cmd_attr` fails.
- [ ] **Step 4:** Implement the commands and `Attr.m`.
- [ ] **Step 5:** Run again. Expected: `test_cmd_attr` passes, no sanitizer report; `iio-mex-real-attr` compiles; mlint clean; `check_api.py --group attr` prints `0 error(s)`. `rm -rf build-mex-attr`.
- [ ] **Step 6:** Commit `feat(matlab): iio.Attr with typed, exact and binary-safe attribute I/O`.

### Task 14: Device

**Wave 1a (pilot). Depends on:** Task 9. **Group:** `device`. **Build directory:** `build-mex-device`.

**Files:**
- Modify: `matlab/mex/cmd_device.c`, `matlab/+iio/Device.m` (replaces the Task 7 stub)
- Test: `tests/mex/test_cmd_device.c`, `matlab/tests/tDevice.m`

**Interfaces:**
- Consumes: `mx_util.h`; `mt_open_context`; `iio.Channel(parent, handle)`, `iio.Buffer(parent, handle)`, `iio.Attr(parent, handle)`, `iio.EventStream(dev)` (stubs from Task 7 until their tasks land); `ctx.find_device`, `ctx.devices` (Task 12) in the M test; by name, from `Device.m` only: the commands `device_get_sample_size`, `device_get_trigger`, `device_set_trigger`, `device_reg_read`, `device_reg_write`, `device_get_debug_attrs`, `device_get_event_attrs`, `device_find_debug_attr`, `device_find_event_attr` of Task 20 (signatures in the "Command reference").
- Produces: the commands of group `device` in the "Command reference" and the complete `iio.Device` of "MATLAB surface", including the members that call group `lowlevel` (their MATLAB tests are in `tDeviceLowLevel.m`, Task 20). **Commands produced:** `device_get_context`, `device_get_id`, `device_get_name`, `device_get_label`, `device_get_channels`, `device_get_attrs`, `device_get_buffers`, `device_find_channel`, `device_find_attr`, `device_is_trigger`, `device_is_hwmon`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_device.c` with `MT_GROUPS(&gw_group_device);`. `ctx = mt_open_context(fx.uri)`; device ids come from `gw_handle_borrow(GW_T_DEVICE, iio_context_find_device(c, ...), ctx, &id)`:
  - identity: `emu-phy`: id `"iio:device2"`, name `"emu-phy"`, label `"phy"`; `iio:device4`: name `[]`, label `[]`; `emu-tx`: label `[]`; `device_get_context(phy) == ctx`.
  - list sizes (`device_get_channels`, `device_get_attrs`, `device_get_buffers`, each a 1-by-N `uint64`): `emu-phy` 6, 14, 0; `emu-rx` 5, 0, 1; `emu-multi` 4, 0, 2; `trigger0` 0, 1, 0.
  - channels: element k of `device_get_channels(rx)` has `gw_handle_ptr(.., GW_T_CHANNEL) == iio_device_get_channel(rx_dev, k)` for k = 0 .. 4; `device_find_channel(phy, "voltage0", false)` and `(phy, "voltage0", true)` are non-zero and different; `(phy, "RX1", false)` and `(phy, "rx1-port", false)` equal the input one; `(rx, "voltage0_i", false)` equals element 0 of the list; `(rx, "voltage0_i", true)` is 0; `(phy, "nope", false)` is 0; the third argument as `double(0)`: `iio:EINVAL`; `device_find_channel(phy, <"voltage0", NUL, "x">, false)` and `device_find_attr(phy, <"calib_mode", NUL>)`: `iio:EINVAL` (not the channel or attribute the cut text names).
  - attributes: the pointer of element 0 of `device_get_attrs(phy)` has name `calib_mode`, of element 13 `text`; `device_find_attr(phy, "calib_mode")` equals element 0; `device_find_attr(phy, "bist_prbs")` (a debug attribute) is 0.
  - buffers: element 1 of `device_get_buffers(multi)` has `gw_handle_ptr(.., GW_T_BUFFER) == iio_device_get_buffer(multi_dev, 1)`; `device_get_buffers(phy)` is a 1-by-0 `uint64`.
  - flags: `device_is_trigger`: `trigger0` true; `iio_sysfs_trigger`, `emu-phy` false. `device_is_hwmon`: `hwmon0` true; `emu-rx` false.
  - every id returned above has `gw_handle_parent == ctx`; an attr id where a device is expected: `iio:EBADF`; after `gw_handle_destroy(ctx)` a device id gives `iio:EBADF`.
  - `mt_check_arities` with the eleven rows of the reference.
- [ ] **Step 2:** Write `matlab/tests/tDevice.m` (`< IioTestCase`):
  - `identity`: `d = ctx.find_device("emu-phy")`: `id` `'iio:device2'`, `name` `'emu-phy'`, `label` `'phy'`; `ctx.find_device("iio:device4")` has `name` and `label` equal to `''` with class `char`.
  - `order`: `{ctx.devices.id}` equals `{'hwmon0', 'iio:device0', 'iio:device1', 'iio:device2', 'iio:device3', 'iio:device4', 'iio_sysfs_trigger', 'trigger0'}`.
  - `context`: `d.context == ctx` and `isa(d.context, 'iio.Context')`.
  - `flags`: `ctx.find_device("trigger0").is_trigger` is true; `iio_sysfs_trigger` false; `ctx.find_device("hwmon0").is_hwmon` true; `emu-rx` false; both are `logical`.
  - `channels`: `ctx.find_device("emu-rx").channels` has class `iio.Channel` and size `[1 5]`; `emu-phy` has 6; `trigger0` has size `[1 0]`.
  - `findChannel`: on `emu-phy`: `find_channel("voltage0")` and `find_channel("voltage0", true)` are non-empty and `~=`; `find_channel("RX1") == find_channel("voltage0")`; `find_channel("rx1-port") == find_channel("voltage0", false)`; `find_channel("nope")` is empty with class `iio.Channel`; on `emu-rx` (scan-element channels, whose order is defined): `rx.find_channel("voltage0_i") == rx.channels(1)` and `rx.find_channel("voltage2") == rx.channels(5)`.
  - `attrs`: `{d.attrs.name}` equals `{'calib_mode', 'calib_mode_available', 'dbl', 'empty', 'ensm_mode', 'ensm_mode_available', 'flag', 'frac', 'frac_available', 'gain', 'gain_available', 'hex', 'novalue', 'text'}`; `d.find_attr("gain") == d.attrs(10)`; `d.find_attr("bist_prbs")` is empty; `ctx.find_device("emu-rx").attrs` has size `[1 0]` and class `iio.Attr`.
  - `buffers`: `emu-multi` has 2 of class `iio.Buffer`; `emu-phy` has size `[1 0]`.
  - `equality`: `ctx.devices(4) == d`; `ctx.devices == d` equals `logical([0 0 0 1 0 0 0 0])`; `d ~= ctx.devices(1)`.
  - `stale`: after `delete(ctx)`: `d.id` is still `'iio:device2'`; `d.channels` throws `iio:EBADF`.
  - `lowLevelMembersExist`: `ismethod(d, 'sample_size')`, `ismethod(d, 'get_trigger')`, `ismethod(d, 'set_trigger')`, `ismethod(d, 'reg_read')`, `ismethod(d, 'reg_write')`, `ismethod(d, 'find_debug_attr')`, `ismethod(d, 'find_event_attr')`, `ismethod(d, 'create_event_stream')`, `isprop(d, 'debug_attrs')` and `isprop(d, 'event_attrs')` are all true.
- [ ] **Step 3:** Run G1 for `test_cmd_device`. Expected: `test_cmd_device` fails.
- [ ] **Step 4:** Implement the commands and `Device.m`.
- [ ] **Step 5:** Run again. Expected: `test_cmd_device` passes; `iio-mex-real-device` compiles; mlint clean; `check_api.py --group device` prints `0 error(s)`. `rm -rf build-mex-device`.
- [ ] **Step 6:** Commit `feat(matlab): iio.Device identity, channels, attributes and buffers`.

### Task 15: Channels and channel masks

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `channel`. **Build directory:** `build-mex-channel`.

**Files:**
- Modify: `matlab/mex/cmd_channel.c`, `matlab/+iio/Channel.m`, `matlab/+iio/ChannelsMask.m` (replace the Task 7 stubs)
- Test: `tests/mex/test_cmd_channel.c`, `matlab/tests/tChannel.m`

**Interfaces:**
- Consumes: `mx_util.h`, `data_path.h` (`dp_device_of`, `dp_add_mask`), `dp_test.h`, `gw_enum_name`; by name, from M code only: commands `channel_read`, `channel_write` (Task 17) and `device_get_sample_size` (Task 20); classes `iio.Attr` (Task 13), `iio.Device` (Task 14), `iio.Block` (Task 17).
- Produces: the commands of group `channel` in the "Command reference", registered as "Conventions / Data-path objects" says; `iio.Channel` and `iio.ChannelsMask` as in "MATLAB surface". **Commands produced:** `channel_get_device`, `channel_get_id`, `channel_get_name`, `channel_get_label`, `channel_is_output`, `channel_is_scan_element`, `channel_get_index`, `channel_get_type`, `channel_get_modifier`, `hwmon_channel_get_type`, `channel_get_attrs`, `channel_get_event_attrs`, `channel_find_attr`, `channel_find_event_attr`, `channel_get_data_format`, `channel_refresh_format`, `channel_convert`, `channel_convert_inverse`, `channel_enable`, `channel_disable`, `channel_is_enabled`, `create_channels_mask`, `channels_mask_destroy`, `channels_mask_get`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_channel.c` with `MT_GROUPS(&gw_group_channel);` (`ctx = mt_open_context(fx.uri)`; `rx`, `tx`, `phy`, `d4`, `hw` = `dp_dev` of `emu-rx`, `emu-tx`, `emu-phy`, `iio:device4`, `hwmon0`; channel ids from `dp_chn`):
  - identity, one command per value: `voltage2` of rx: `channel_get_device` → `rx`, `channel_get_id` → `'voltage2'`, `channel_get_name` → `'aux'`, `channel_get_label` → `'aux-adc'`, `channel_is_output` → false, `channel_is_scan_element` → true, `channel_get_index` → 4, `channel_get_type` with two outputs → `'IIO_VOLTAGE'`, 0, `channel_get_modifier` → `'IIO_NO_MOD'`, 0. `voltage0_i`: name `[]` (0×0 double), label `[]`, modifier `'IIO_MOD_I'`, 32; `voltage0_q`: `'IIO_MOD_Q'`, 33. `altvoltage0` of tx: index -2, scan element false, type `'IIO_ALTVOLTAGE'`, 15, name `'TX1_I_F1'`, output true. `accel_x` of phy: type `'IIO_ACCEL'`, 3, modifier `'IIO_MOD_X'`, 1. `temp1` of hw: `hwmon_channel_get_type` → `'HWMON_TEMP'`, 3 (and `channel_get_type` gives the same value 3 under its IIO name `'IIO_ACCEL'`, which is why M code asks the hwmon command for hwmon devices); `in1`: `'HWMON_VOLTAGE'`, 0. `channel_get_type` with one output returns the name only.
  - attrs: `channel_get_attrs(voltage0_i)` → 1×2, element 1 (0-based) is the id of
    `iio_channel_get_attr(.., 1)` (compare `gw_handle_ptr`); `voltage0_q` → 1×0 `uint64`.
    `channel_find_attr(voltage2, 'scale')` ≠ 0 and equals the third element of its attr list;
    `'nope'` → 0. On phy `voltage0` (input): `find_attr 'RX1_hardwaregain'` equals
    `find_attr 'hardwaregain'`; `channel_get_event_attrs` → 1×3; `find_event_attr 'thresh_rising_en'`
    ≠ 0; `find_attr 'thresh_rising_en'` → 0.
  - `channel_get_data_format(voltage0_i)` → length 16, bits 12, shift 0, repeat 1, `is_signed` true,
    `is_fully_defined` false, `is_be` false, `with_scale` false, scale 0, offset 0, `is_float` false;
    `voltage2`: bits 14, shift 2, `with_scale` true, scale 0.25, offset -100.
    `channel_refresh_format(voltage0_i)` raises `iio:ENOSYS`.
  - convert: d4 `voltage2`: `[00 F0]` → `[00 FF]`; inverse `[00 FF]` → `[00 F0]`; `[00 F0 00 F0]` →
    `[00 FF 00 FF]`; 3 bytes raises `iio:EINVAL`. d4 `voltage1`: `[12 34 56 78]` → `[56 34 12 00]`;
    inverse of that → `[12 34 56 00]`. An empty `uint8` → 1×0 `uint8`, for both commands. A channel that
    is not a scan element (format length 0): `channel_convert(dp_chn(phy, "voltage0", false), uint8 [0 1])`
    and `channel_convert_inverse` of the same raise `iio:EINVAL` (no division by zero), also for a 4-byte and an empty input.
  - mask: `m = create_channels_mask(rx)`; `channels_mask_get(m)` → 1×5 all false; enable
    `voltage0_i`, `voltage0_q` → `[1 1 0 0 0]`; `channel_is_enabled(voltage0_i, m)` true, `voltage2`
    false; disable `voltage0_i` → `[0 1 0 0 0]`. `channel_enable(tx voltage0_i, m)` raises
    `iio:EINVAL`; `channel_enable(tx altvoltage0, tx mask)` raises `iio:EINVAL`;
    `create_channels_mask(dp_dev(ctx, "trigger0"))` raises `iio:EINVAL`; `mt_oom_sweep(1, "create_channels_mask", 1, mt_u64(rx))` returns ≥ 1. A view made by the test with
    `gw_handle_borrow(GW_T_MASK, iio_buffer_stream_get_channels_mask(..), bs, &id)` on a `dp_bufstream`:
    `channel_enable` raises `iio:EPERM`, `channels_mask_destroy` leaves the id valid. `channels_mask_destroy(m)` twice, then `channels_mask_get(m)` raises `iio:EBADF`.
  - `gw_handle_destroy(ctx)` with one mask left open: no leak report.
  - Measured: a channel that is not a scan element (`emu-phy` `voltage0`) has format length 0, bits 0, shift 0 and repeat 1 (not 0). The group has no bracketed command, so the test has no `mt_comma` block.
- [ ] **Step 2:** Run G1 for `test_cmd_channel` in `build-mex-channel`. Expected: FAIL (commands return `-ENOSYS`).
- [ ] **Step 3:** Implement `cmd_channel.c`.
- [ ] **Step 4:** Run G1 for `test_cmd_channel`. Expected: PASS, no sanitizer report.
- [ ] **Step 5:** Write `Channel.m`, `ChannelsMask.m` as in "MATLAB surface".
- [ ] **Step 6:** Write `matlab/tests/tChannel.m` (context from `tc.Fx.Uri`):
  `identity` (the `voltage2` and `altvoltage0` values of Step 1 as properties; `class(ch.id)` is
  `'char'`); `hwmonType` (`'HWMON_TEMP'`); `attrs` (`numel(v0i.attrs) == 2`, `isempty(v0q.attrs)`,
  `isempty(v2.find_attr("nope"))`, `v2.find_attr("scale") == v2.attrs(3)`); `dataFormat`
  (struct values of Step 1); `refreshFormat` (`verifyError(.., 'iio:ENOSYS')`); `convert`
  (d4 `voltage2`: `convert(uint8([0 240]))` → `uint8([0;255])`; `convert_inverse(uint8([0 255]))` → `uint8([0;240])`;
  `convert` on `emu-phy` `voltage0` → `'iio:EINVAL'`); `maskRoundTrip` (new mask: `enabled` all false,
  `sample_size == 0`; `mask.channels = [v0i v0q]` → `[true true false false false]`, `sample_size == 4`,
  `isequal(mask.channels == [v0i v0q], [true true])`; `v0i.disable(mask)` → `is_enabled` false);
  `maskWrongDevice` (`'iio:EINVAL'`); `maskDestroyTwice` (no error; then `mask.enabled` → `'iio:EBADF'`);
  `stringInput` (`find_attr` accepts `"scale"` and `'scale'`).
- [ ] **Step 7:** Run G2, G3 (`Channel.m`, `ChannelsMask.m`, `tChannel.m`), G4 (group `channel`). Expected: all
  pass. `rm -rf build-mex-channel`.
- [ ] **Step 8:** Commit `feat(matlab): channels and channel masks`.

### Task 16: Buffers and buffer streams

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `buffer`. **Build directory:** `build-mex-buffer`.

**Files:**
- Modify: `matlab/mex/cmd_buffer.c`, `matlab/+iio/Buffer.m`, `matlab/+iio/BufferStream.m` (replace the Task 7 stubs)
- Test: `tests/mex/test_cmd_buffer.c`, `matlab/tests/tBuffer.m`

**Interfaces:**
- Consumes: `mx_util.h`, `data_path.h` (`dp_device_of`, `dp_add_bufstream`, `dp_add_block`, `BS_*`), `dp_test.h`; by name, from M code only: `iio.Channel`, `iio.ChannelsMask` (Task 15), `iio.Block` (Task 17), `iio.Stream` (Task 18), `iio.Attr` (Task 13), command `device_get_sample_size` (Task 20).
- Produces: the commands of group `buffer` in the "Command reference" with the registrations of "Conventions / Data-path objects"; `iio.Buffer` and `iio.BufferStream` as in "MATLAB surface". **Commands produced:** `buffer_get_device`, `buffer_is_output`, `buffer_get_attrs`, `buffer_find_attr`, `buffer_get_scan_elements`, `buffer_open`, `buffer_close`, `buffer_stream_start`, `buffer_stream_stop`, `buffer_stream_cancel`, `buffer_stream_get_channels_mask`, `buffer_stream_create_block`, `buffer_stream_is_started`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_buffer.c` with `MT_GROUPS(&gw_group_buffer);` (`emu`: `ctx = mt_open_context(fx.uri)`, `rx = dp_dev(ctx, "emu-rx")` and so on; `rxb = dp_buf(rx, 0)`, `txb = dp_buf(tx, 0)`,
  `m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q")`):
  - `buffer_get_device(rxb)` → `rx`; `buffer_is_output(rxb)` → false; `txb` → true; `dp_buf(multi, 1)` → true.
    `buffer_get_attrs(rxb)` → 1×3 whose ids are those of `data_available`, `length_align_bytes`,
    `watermark` (compare pointers with `iio_buffer_get_attr`); `buffer_find_attr(rxb, 'watermark')`
    equals element 3; `'nope'` → 0. `buffer_get_scan_elements(rxb)` → 1×5, element 5 equals
    `dp_chn(rx, "voltage2", false)`; `txb` → 1×4.
  - `bs = buffer_open(rxb, m)`: non-zero, `gw_handle_owned`, parent `rxb`. Empty mask → `iio:EINVAL`;
    `buffer_open(rxb, mask of tx)` → `iio:EINVAL`; on an `xml` context → `iio:ENOSYS`.
    `channels_mask_destroy`-equivalent `gw_handle_destroy(m)` right after the open: the stream keeps working.
  - `v = buffer_stream_get_channels_mask(bs)`: `v != m`, same `v` on a second call, `!gw_handle_owned(v)`,
    `gw_handle_parent(v) == bs`, and `iio_channel_is_enabled(voltage0_q, gw_handle_ptr(v, GW_T_MASK))`.
  - `buffer_stream_start(bs)` before any block raises `iio:EINVAL`; `buffer_stream_create_block(bs, 2)`
    raises `iio:EINVAL`; `blk = buffer_stream_create_block(bs, 32)`: owned, parent `bs`.
    `buffer_stream_is_started` false → `buffer_stream_start` → true.
  - start is idempotent: with libiio directly, enqueue and dequeue `blk` (first word
    `fixture_ramp_word_i(0)`); `buffer_stream_start(bs)` again; enqueue and dequeue again → first word
    `fixture_ramp_word_i(8)` (a second libiio start would rewind to 0).
  - `buffer_stream_cancel(bs)` while started: no error and `buffer_stream_is_started` still true.
    `buffer_stream_stop` → `is_started` false; again: no error.
  - `buffer_close(bs)`: `gw_handle_ptr(blk, GW_T_BLOCK) == NULL`, `gw_handle_ptr(v, GW_T_MASK) == NULL`; again and
    `buffer_close(0)`: no error; `buffer_stream_start(bs)` raises `iio:EBADF`.
  - a view registered by the test (`gw_handle_borrow(GW_T_BUFFER_STREAM, ptr, <any owned id>, &id)`, with a `ptr` the table does not hold yet: for a registered pointer the call returns the existing owned entry; the test opens a second buffer with libiio directly): `buffer_stream_start`,
    `_stop`, `_create_block` raise `iio:EPERM`; `buffer_close` leaves it valid.
  - `fake` context (`ctx = dp_open_fake(fx.xml)`): `buffer_open`, two `buffer_stream_create_block`, `buffer_stream_start`, then
    `gw_handle_destroy(ctx)` (context destroyed with everything open) → `fake.log == "OBBSXsFFXC"` (stopped before closed); no leak report.
    A second fake context: `buffer_open`, one block, `buffer_stream_start`, `buffer_close(bs)` → log `"OBSXsFXC"`; the same
    without the start → `"OBXFXC"`. On a third fake context (no stream open on the buffer): `mt_oom_sweep(1, "buffer_open", 2, ..)`, then, with a `bs` from a
    normal `buffer_open` of its `emu-tx` buffer, `mt_oom_sweep(1, "buffer_stream_create_block", 2, mt_u64(bs), mt_dbl(16))`:
    each returns ≥ 1, and the log then holds exactly one more `O` / one more `B` than before the sweep.
- [ ] **Step 2:** Run G1 for `test_cmd_buffer` in `build-mex-buffer`. Expected: FAIL.
- [ ] **Step 3:** Implement `cmd_buffer.c`.
- [ ] **Step 4:** Run G1 for `test_cmd_buffer`. Expected: PASS; libiio prints its one
  `Cannot start buffer before creating blocks.` line.
- [ ] **Step 5:** Write `Buffer.m`, `BufferStream.m`.
- [ ] **Step 6:** Write `matlab/tests/tBuffer.m`: `info` (`rx.buffers` has 1 element, `output` false; `emu-tx`
  true; `emu-multi` 2 buffers `[false true]`); `attrs` (`string({buf.attrs.name})` is
  `["data_available" "length_align_bytes" "watermark"]`; `buf.find_attr("watermark").read()` is
  `'1024'`); `scanElements` (5; `all(buf.scan_elements == rx.channels)`); `openClose`
  (`bs.sample_size == 4`; `all(bs.mask.channels == [v0i v0q])`; `bs.started` false;
  `bs.close(); bs.close();` no error); `startNeedsBlock` (`'iio:EINVAL'`); `startStop` (create a
  32-byte block, `start`, `started` true, `stop`, false); `cancel` (after `start`: `bs.cancel()` raises nothing and
  `bs.started` is still true); `emptyMask` (`'iio:EINVAL'`);
  `maskOfOtherDevice` (`'iio:EINVAL'`); `maskIsReadOnly` (`v0i.disable(bs.mask)` → `'iio:EPERM'`);
  `closeInvalidatesBlocks` (`blk.nbytes` after `bs.close()` → `'iio:EBADF'`);
  `contextDeleteWithOpenStream` (delete every object in creation order instead of reverse: no error).
- [ ] **Step 7:** Run G2, G3, G4 (group `buffer`). `rm -rf build-mex-buffer`.
- [ ] **Step 8:** Commit `feat(matlab): buffers and buffer streams`.

### Task 17: Blocks

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `block`. **Build directory:** `build-mex-block`.

**Files:**
- Modify: `matlab/mex/cmd_block.c`, `matlab/+iio/Block.m` (replaces the Task 7 stub)
- Test: `tests/mex/test_cmd_block.c`, `matlab/tests/tBlock.m`

**Interfaces:**
- Consumes: `mx_util.h`, `data_path.h` (`dp_opt_*`, `dp_check_matrix`, `dp_device_of`, `BS_STARTED`, `BLK_*`), `dp_test.h`; `iio_conv_create`, `iio_conv_read`, `iio_conv_write`, `iio_conv_get_step`, `iio_conv_destroy`; by name, from M code only: `iio.BufferStream` (Task 16), `iio.Channel` (Task 15).
- Produces: the commands of group `block` in the "Command reference"; `iio.Block` as in "MATLAB surface". **Commands produced:** `block_destroy`, `block_first`, `block_enqueue`, `block_dequeue`, `block_get_buffer_stream`, `block_get_dmabuf_fd`, `block_disable_cpu_access`, `channel_read`, `channel_write`, `block_size`, `block_get_bytes`, `block_set_bytes`, `block_read`, `block_write`.
- Converter cache of `block_read` / `block_write`: the cache slot (`gw_handle_set_cache`) of the block's `GW_T_BUFFER_STREAM` entry (found with `gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(blk))`), 4 entries, least recently used replaced; key = the channel pointers in order, a copy of each channel's `struct iio_data_format`, type, complex, scale, raw, gain and offset vectors; released by the slot's `cache_free`. Layout always comes from `iio_buffer_stream_get_channels_mask()`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_block.c` with `MT_GROUPS(&gw_group_block);` (`emu`; `bs = dp_bufstream(rxb, mask{voltage0_i, voltage0_q})`,
  `blk = dp_block(bs, 32)`; the test starts the stream by `iio_buffer_stream_start` + `gw_handle_set_flags(bs, BS_STARTED)`):
  - `block_size(blk)` → 32; `block_first(blk, voltage0_i)` → 0, `voltage0_q` → 2, `voltage2` → 32;
    a tx channel raises `iio:EINVAL`. `block_get_dmabuf_fd` raises `iio:EINVAL`;
    `block_disable_cpu_access(blk, true)` raises `iio:ENOSYS`.
  - traps (emu traps), each in < 1 s: `block_dequeue(blk, false)` before any enqueue raises `iio:EPERM`;
    `block_enqueue(blk, 0, false)`; `block_dequeue` while the stream is not started raises `iio:EPERM`.
    `block_enqueue` again raises `iio:EPERM`; on a fresh block `block_enqueue(.., 33, false)` raises `iio:EINVAL`.
  - data access while enqueued: `block_get_bytes`, `block_read`, `channel_read` raise `iio:EPERM`.
  - after start, `block_dequeue(blk, false)` → true; `block_get_bytes(blk)` → the first 32 bytes of "Test data";
    `block_get_bytes(blk, 4, 4)` → `01 18 fe 07`; `(blk, 30, 4)` raises `iio:EINVAL`.
  - `channel_read(voltage0_i, blk, false)` → 16 bytes `00 f8 01 f8 02 f8 03 f8 04 f8 05 f8 06 f8 07 f8`
    (int16 -2048 .. -2041); `raw = true` → `00 08 01 18 02 28 03 38 04 48 05 58 06 68 07 78`;
    `voltage0_q` → int16 2047 .. 2040; `voltage2` → 1×0 `uint8`.
  - a channel that is not enabled, raw, same storage size (libiio's shortcut would return the other channel's
    data): on a second `mt_open_context(fx.uri)` context, `bs1 = dp_bufstream(<its emu-rx buffer 0>, mask{voltage0_i})`,
    16-byte block, enqueue / start / dequeue with libiio directly: `channel_read(voltage0_q, blk1, true)` → 1×0 `uint8`;
    `channel_write(voltage0_q, blk1, uint8 [1 2], true)` → 0 and `block_get_bytes(blk1)` is unchanged.
  - `block_read(blk, struct(class 'int16', complex true))` → 8×1 complex int16, element k
    `(k-2048) + (2047-k)i`; no byte equals the mock's uninitialised-memory pattern.
    `class 'double', scale 'fullscale', complex true` → first element `-1 + 0.99951171875i`.
    `raw true, class 'int16', complex true` → `2048+2047i`, `6145+2046i`. `samples 3` → 3×1;
    `samples 100` → 8×1. `samples` is read with `dp_opt_int(opts, "samples", 1, LLONG_MAX, 0, &n)` (0 = not given):
    `samples 0`, `2.5`, `NaN`, `Inf`, `1e300`, `-1` → `iio:EINVAL` each;
    `struct(samples 1×0 double, gain 1×0 double, offset 1×0 double)` (the defaults `iio.Block.read` passes) → 8×2
    double as with `[]`; `block_write(blk, data, struct(gain 1×0 double, offset 1×0 double))` → as with `[]`. `channels [voltage0_q]`, int16 → 8×1 real `2047 .. 2040`. `[]` opts → 8×2
    double, column 1 `-2048 ..`, column 2 `2047 ..`. `complex true` with `channels [voltage0_q]` →
    `iio:EINVAL`; `class 'int16', scale 'fullscale'` → `iio:EINVAL`; `class 'int12'` → `iio:EINVAL`;
    a field `colour` → `iio:EINVAL`; a channel of tx → `iio:EINVAL`.
  - cache: six different option sets, each read twice in rotation, values correct every time; no leak
    report when the context is destroyed. (Which slot is replaced, "least recently used", cannot be seen in results: a wrong eviction order still returns correct values. It is reviewed, not tested.)
  - transmit (`emu-tx`, 16-byte block): `block_write(blk, complex int16 [100+1i; -200+2i; 300+3i;
    -400+4i], [])` → 4; `block_enqueue(blk, 16, false)`; start; `block_dequeue` → true; file
    `emu-tx_buf0.bin` is `100,1,-200,2,300,3,-400,4`. `block_write` of a 6×1 → 4 (capacity); of a 4×2
    complex → `iio:EINVAL`; of a char → `iio:EINVAL`. Option `complex`: `block_write(blk, real int16 [7;-8;9;-10],
    struct(complex true))` → 4 and, after enqueue and dequeue, the file is `7,0,-8,0,9,0,-10,0`; the same data with
    `[]` → `iio:EINVAL` (message contains `columns`); complex data with `struct(complex false)` → `iio:EINVAL`.
    Empty inputs, no sanitizer report: `block_write(blk, 0×1 complex int16, [])` → 0; `block_set_bytes(blk, empty uint8)`
    → 0; `channel_write(voltage0_i, blk, empty uint8, false)` → 0. `block_set_bytes(blk, uint8 1..4, 12)` → 4;
    `(.., 14)` raises `iio:EINVAL`. `channel_write(voltage0_i, blk, uint8 bytes of int16 [7 8 9 10], false)` → 8.
  - `block_get_buffer_stream(blk)` → `bs`. `block_destroy(blk)` twice: no error; `block_size(blk)`
    raises `iio:EBADF`. A view (`gw_handle_borrow(GW_T_BLOCK, ptr, <the buffer-stream or buffer id>, &id)` with a block made by `iio_buffer_stream_create_block` directly, which the test destroys itself: a registered pointer would return its owned entry, and under a context id the block has no device for `dp_device_of()`): `block_enqueue`, `block_dequeue` raise
    `iio:EPERM`; `block_destroy` leaves it valid; with `BLK_ENQUEUED` set by the test `block_read`
    raises `iio:EPERM`.
  - `fake` context (hazard: not ready): enqueue, start, 3 dequeues succeed with re-enqueue; then
    `block_dequeue(blk, true)` → false; `block_dequeue(blk, false)` raises `iio:ETIMEDOUT` and `BLK_ENQUEUED` is still set.
  - `fake` context, a block that libiio no longer holds (what a failed dequeue leaves on libiio's generic path):
    `fake.rx_frames = 100`; `block_enqueue` through the gateway, then `iio_block_dequeue(ptr, false) == 0` directly, behind
    the gateway's back; `block_dequeue(blk, false)` raises `iio:EPERM`; after it `block_get_bytes(blk)` and
    `block_enqueue(blk, 0, false)` succeed (the flag was cleared).
- [ ] **Step 2:** Run G1 for `test_cmd_block` in `build-mex-block`. Expected: FAIL.
- [ ] **Step 3:** Implement `cmd_block.c`. `block_read`: compute N and C first, create the output with
  `mxCreateUninitNumericMatrix(N, C, cls, complex ? mxCOMPLEX : mxREAL)`, skip the converter call
  when `N * C == 0`. `block_write`, `block_set_bytes`, `channel_write`: return 0 before touching any data
  pointer when there is nothing to write (`mxGetData` of an empty array is `NULL`). Gains: `mxMalloc`, freed
  with `mxFree` on every return path.
- [ ] **Step 4:** Run G1 for `test_cmd_block`. Expected: PASS, no sanitizer report, total time < 5 s.
- [ ] **Step 5:** Write `Block.m`.
- [ ] **Step 6:** Write `matlab/tests/tBlock.m` (helper: open `emu-rx` with `[v0i v0q]`, block of
  `8 * bs.sample_size` bytes, `enqueue`, `start`, `dequeue`): `sizes` (`nbytes == 32`, `first(v0q) == 2`);
  `bytes` (`blk.bytes(0, 4)` is `uint8([0;8;255;7])`); `readInt16Complex`
  (`blk.read(OutputDataType="int16", Complex=true)` is 8×1, class `int16`, `~isreal`, equals
  `complex(I, Q)` of "Test data"); `readDefaults` (8×2 double); `readFullscale` (single, first element
  `single(-1) + single(2047/2048)*1i`); `readRaw` (first element `int16(2048) + int16(2047)*1i`);
  `readSubset` (`Channels=v0q`); `readWhileEnqueued` (`'iio:EPERM'`); `dequeueBeforeStart`
  (`'iio:EPERM'`, and `toc < 1`); `enqueueTwice` (`'iio:EPERM'`); `scaleNeedsFloat` (`'iio:EINVAL'`);
  `channelRead` (`v0i.read(blk)` is `int16((-2048:-2041).')`; `v0i.read(blk, true)` is 16 `uint8`);
  `write` (`emu-tx`: `n = blk.write(complex(int16([100;-200;300;-400]), int16([1;2;3;4])))` is 4;
  `enqueue(n * bs.sample_size)`, `start`, `dequeue`; `tc.Fx.readInt16("emu-tx_buf0.bin")` is
  `int16([100;1;-200;2;300;3;-400;4])`); `writeRealAsComplex` (`blk.write(int16([7;-8;9;-10]),
  Complex=true)` and `blk.write(complex(int16([7;-8;9;-10]), int16(0)))` each give the file `7,0,-8,0,9,0,-10,0`:
  the option, not the complexity of the array as the MEX sees it, selects the layout); `channelWrite` (`emu-tx`,
  16-byte block: `v0i.write(blk, int16([7 8 9 10]))` returns 8; after `enqueue(16)`, `start`, `dequeue` the odd elements
  of the file are `int16([7;8;9;10])`); `setBytes` (`blk.set_bytes(uint8(1:4), 12)` returns 4; `blk.bytes(12, 4)` is
  `uint8((1:4).')`); `parent` (`blk.parent == bs` and `blk.buffer_stream == bs`); `dmabuf` (`'iio:EINVAL'`); `cpuAccess`
  (`'iio:ENOSYS'`); `destroyTwice`.
- [ ] **Step 7:** Run G2, G3, G4 (group `block`). `rm -rf build-mex-block`.
- [ ] **Step 8:** Commit `feat(matlab): blocks with converted read and write`.

### Task 18: `iio_stream` helper

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `stream`. **Build directory:** `build-mex-stream`.

**Files:**
- Modify: `matlab/mex/cmd_stream.c`, `matlab/+iio/Stream.m` (replaces the Task 7 stub)
- Test: `tests/mex/test_cmd_stream.c`, `matlab/tests/tStream.m`

**Interfaces:**
- Consumes: `mx_util.h`, `data_path.h` (`dp_device_of`, `BLK_ENQUEUED`), `dp_test.h`; by name, from M code only: `iio.Block` (Task 17), `iio.Buffer` (Task 16), `iio.ChannelsMask` (Task 15).
- Produces: the commands of group `stream` in the "Command reference"; `iio.Stream` as in "MATLAB surface". **Commands produced:** `buffer_create_stream`, `stream_get_next_block`, `stream_cancel`, `stream_destroy`.

  Documented in the class help: for an output buffer the block returned by `next()` is submitted by
  the following `next()`; the block filled last is never sent; use `iio.TxStream` for transmit.
  The RX data file must exist before the first `next()`: after a failed `next()` on `emu:`,
  `iio_stream_destroy` stalls 5 s per block.

- [ ] **Step 1:** Write `tests/mex/test_cmd_stream.c` with `MT_GROUPS(&gw_group_stream);` (`emu`; `m = dp_mask(rx, false, 2, "voltage0_i", "voltage0_q")`):
  - `st = buffer_create_stream(rxb, 3, 16, m)`: owned, parent `rxb`. `(rxb, 0, 16, m)` and
    `(rxb, 3, 0, m)` raise `iio:EINVAL`; a mask of tx raises `iio:EINVAL`.
  - five `stream_get_next_block(st)` → ids `a, b, c, a, b` (three distinct); each `!gw_handle_owned`, parent
    `st`; block n has 64 bytes and its first word is `fixture_ramp_word_i(16 n)` (read through
    `gw_handle_ptr`). After call 2: `gw_handle_flags(a) & BLK_ENQUEUED`, and `b` has it clear.
  - `gw_handle_find(GW_T_BUFFER_STREAM, iio_block_get_buffer_stream(gw_handle_ptr(a, GW_T_BLOCK)))` is non-zero, not owned,
    parent `st`.
  - `stream_cancel(st)`: no error. `stream_destroy(st)`: `a`, `b`, `c` and the view are stale; again:
    no error; `stream_get_next_block(st)` raises `iio:EBADF`.
  - transmit (`emu-tx`, 1 block, 2 samples): `next`, copy int16 `11,12,13,14` into the block, `next` →
    file `11,12,13,14`; copy `21,22,23,24`, `next` → file `21,22,23,24`; `stream_destroy`.
  - `gw_handle_destroy(ctx)` with a stream left open after two `next`: no leak report, < 1 s.
  - a `next` that fails (`ctx = dp_open_fake(fx.xml)`, `fake.rx_frames = 1`, `st = buffer_create_stream(rxb, 1, 4, m)`): the
    first `stream_get_next_block` → `a`; `n = gw_handle_count()`; the second raises `iio:ETIMEDOUT`,
    `gw_handle_count() == n` (the error pointer was not registered), `gw_handle_flags(a) & BLK_ENQUEUED` is set;
    `stream_destroy(st)` returns in < 1 s.
  - destroy never blocks for ever (fake, `emu-tx`, 1 block, 2 samples, `fake.tx_frames = 0`): `next`, then a second `next`
    that raises `iio:ETIMEDOUT` (the block stays queued), `fake_clear_log()`, `stream_destroy(st)` → the log starts with `d` (default context timeout: the queued block is
    awaited). The same after `iio_context_set_timeout(ctx, -1)` → the log starts with `X` (cancelled first).
  - `mt_oom_sweep(1, "buffer_create_stream", 4, ..)` on the fake returns ≥ 1 and the log holds exactly one more `O`; `mt_oom_sweep(1, "stream_get_next_block", 1, ..)` on the fake: exactly one more `D`.
  - the view of the stream's buffer stream has `BS_STARTED` (`gw_handle_flags`).
- [ ] **Step 2:** Run G1 for `test_cmd_stream` in `build-mex-stream`. Expected: FAIL.
- [ ] **Step 3:** Implement `cmd_stream.c` (destroy op of "Conventions / Data-path objects", chosen by `iio_buffer_is_output`).
- [ ] **Step 4:** Run G1 for `test_cmd_stream`. Expected: PASS, no sanitizer report.
- [ ] **Step 5:** Write `Stream.m`.
- [ ] **Step 6:** Write `matlab/tests/tStream.m`: `rxBlocks` (`st = iio.Stream(buf, mask, 16, 3)`; four `next()`:
  the 4th block `==` the 1st; `blk.nbytes == 64`; `blk.read(OutputDataType="int16",
  Complex=true)` of call n equals the ramp for `k = 16(n-1) .. 16n-1`); `viaBuffer`
  (`buf.create_stream(mask, 16)` has `nb_blocks == 4`); `staleBlock` (reading the first block after a
  second `next()` → `'iio:EPERM'`); `blockIsBorrowed` (`blk.enqueue()` → `'iio:EPERM'`;
  `blk.destroy()` no error and `blk.nbytes` still 64); `bufferStreamOfBlock`
  (`blk.buffer_stream.sample_size == 4`, `blk.buffer_stream.start()` → `'iio:EPERM'`); `tx`
  (the 11..14 / 21..24 sequence with `blk.write(int16 complex ..)`); `badArguments` (`'iio:EINVAL'`
  for 0 blocks); `cancel` (after two `next()`: `st.cancel()` raises nothing; `st.destroy()` raises nothing); `destroyTwice`.
- [ ] **Step 7:** Run G2, G3, G4 (group `stream`). `rm -rf build-mex-stream`.
- [ ] **Step 8:** Commit `feat(matlab): iio_stream helper`.

### Task 19: Events

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `event`. **Build directory:** `build-mex-event`.

**Files:**
- Modify: `matlab/mex/cmd_event.c`, `matlab/+iio/EventStream.m` (replaces the Task 7 stub)
- Test: `tests/mex/test_cmd_event.c`, `matlab/tests/tEventStream.m`

**Interfaces:**
- Consumes: `mx_util.h`, `dp_test.h` (fake backend), `gw_enum_name` (`GW_ENUM_EVENT_TYPE`, `GW_ENUM_EVENT_DIRECTION`); by name, from M code only: `iio.Channel` (Task 15), `iio.Device` (Task 14); `EmuFixture.fakeContext` (Tasks 8, 10) in the M test.
- Produces: the commands of group `event` in the "Command reference"; `iio.EventStream` as in "MATLAB surface". **Commands produced:** `device_create_event_stream`, `event_stream_destroy`, `event_stream_read`, `event_get_type`, `event_get_direction`, `event_get_channel`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_event.c` with `MT_GROUPS(&gw_group_event);`:
  - `emu` (`ctx = mt_open_context(fx.uri)`, `phy = dp_dev(ctx, "emu-phy")`): `device_create_event_stream(phy)` raises `iio:ENOSYS`.
  - decoding on `emu`, ids as `uint64`. For each row: `event_get_type(id)` with two outputs gives the type pair, `event_get_direction(id)` the direction pair, `event_get_channel(id, phy, false)` the column "channel" and `event_get_channel(id, phy, true)` the column "channel_diff" (0 = id 0):

    | id | type, type_id | direction, direction_id | channel | channel_diff |
    |---|---|---|---|---|
    | `0x0001000000000000` | `IIO_EV_TYPE_THRESH`, 0 | `IIO_EV_DIR_RISING`, 1 | `voltage0` input | 0 |
    | `0x0102010300000000` | `IIO_EV_TYPE_MAG`, 1 | `IIO_EV_DIR_FALLING`, 2 | `accel_x` | 0 |
    | `0x0000000900000000` | `IIO_EV_TYPE_THRESH`, 0 | `IIO_EV_DIR_EITHER`, 0 | `temp0` | 0 |
    | `0x0000001100000000` (pressure: no such channel) | THRESH | EITHER | 0 | 0 |
    | `0x0000000000000005` (voltage 5) | THRESH | EITHER | 0 | 0 |
    | `0x0000000000000007` (number ≥ channel count) | THRESH | EITHER | 0 | 0 |
    | `0x0080000f00010000` (differential altvoltage 0 / 1) | THRESH | EITHER | `altvoltage0` | `altvoltage1` |
    | `0xC800000000000000` (type 200) | `UNKNOWN`, 200 | EITHER | `voltage0` input | 0 |

    Channel ids are compared with `dp_chn(phy, ..)`. A double as id raises `iio:EINVAL`.
  - `fake` (`ctx = dp_open_fake(fx.xml)`): `device_create_event_stream(rx)` raises `iio:ENODEV`; `es = device_create_event_stream(phy)`: owned, parent `phy`. `event_stream_read(es, true)` with three outputs → logical `true`, an id of class `uint64` equal to `0x0001000000000000`, a timestamp of class `int64` equal to 1000; the second read → `true`, `0x0102010300000000`, 1001; the third → `false`, `uint64(0)`, `int64(0)`; `event_stream_read(es, false)` → `false` (the fake's `-ETIMEDOUT`). `event_stream_destroy(es)` twice: no error; then a read raises `iio:EBADF`. A second event stream left open when the context is destroyed: no leak report.
- [ ] **Step 2:** Run G1 for `test_cmd_event` in `build-mex-event`. Expected: FAIL.
- [ ] **Step 3:** Implement `cmd_event.c` (channel check of the "Command reference").
- [ ] **Step 4:** Run G1 for `test_cmd_event`. Expected: PASS; libiio prints one
  `Unexpected IIO event channel ID` warning.
- [ ] **Step 5:** Write `EventStream.m`.
- [ ] **Step 6:** Write `matlab/tests/tEventStream.m`: `notSupportedOnEmu` (`'iio:ENOSYS'`); `decode` (rows 1, 2, 4
  and 7 of the table through `iio.EventStream.decode(uint64(..), phy)`: `ev.channel.id`, `isempty`,
  `ev.channel_diff.id`); `readFake` (`ctx = tc.Fx.fakeContext()`; two events with the values above,
  `class(ev.id)` `'uint64'`, `class(ev.timestamp)` `'int64'`, `ev.channel.id` `'voltage0'`; third
  `isempty`); `wrongDeviceFake` (`'iio:ENODEV'`); `destroyTwice`.
- [ ] **Step 7:** Run G2, G3, G4 (group `event`). `rm -rf build-mex-event`.
- [ ] **Step 8:** Commit `feat(matlab): event streams`.

### Task 20: Device low level

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate. **Group:** `lowlevel`. **Build directory:** `build-mex-lowlevel`.

**Files:**
- Modify: `matlab/mex/cmd_lowlevel.c`
- Test: `tests/mex/test_cmd_lowlevel.c`, `matlab/tests/tDeviceLowLevel.m`
- Not touched: `matlab/+iio/Device.m` (Task 14 writes the members that call these commands)

**Interfaces:**
- Consumes: `mx_util.h`, `data_path.h` (`dp_device_of`), `dp_test.h`; in the M test the `iio.Device` members `sample_size`, `get_trigger`, `set_trigger`, `reg_read`, `reg_write`, `debug_attrs`, `event_attrs`, `find_debug_attr`, `find_event_attr` (Task 14), `mask.sample_size` (Task 15), `bs.sample_size` (Task 16), `EmuFixture.fakeContext`.
- Produces: the commands of group `lowlevel` in the "Command reference". **Commands produced:** `device_get_sample_size`, `device_get_trigger`, `device_set_trigger`, `device_reg_read`, `device_reg_write`, `device_get_debug_attrs`, `device_get_event_attrs`, `device_find_debug_attr`, `device_find_event_attr`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_lowlevel.c` with `MT_GROUPS(&gw_group_lowlevel);` (`emu`: `ctx = mt_open_context(fx.uri)`; `fake`: `ctx = dp_open_fake(fx.xml)`):
  - `device_get_sample_size(rx, mask)` on `emu`: empty mask → 0; `{voltage0_i, voltage0_q}` → 4; the
    first four → 8; all five → 10; `iio:device4` with `{voltage0, voltage1, timestamp}` → 16; all
    eight → 56; `(rx, mask of tx)` raises `iio:EINVAL` (libiio alone would answer 0).
  - registers on `emu` `phy`: `device_reg_read(phy, 16)` raises `iio:ENOENT`;
    `device_reg_write(phy, 16, 51966)`; `device_reg_read(phy, 16)` → 51966 of class `uint32`;
    `4294967295` round trip at address `4294967295`; address or value `4294967296`, `-1`, `1.5`, a
    char → `iio:EINVAL`.
  - attrs on `emu` `phy`: `device_get_debug_attrs` → 1×4 (ids of `bist_prbs`, `direct_reg_access`,
    `loopback`, `loopback_available`, compared by pointer); `device_find_debug_attr(phy, 'loopback')`
    equals element 3; `'nope'` → 0; `device_get_event_attrs` → 1×1; `device_find_event_attr(phy,
    'events_enabled')` equals it; `device_get_debug_attrs(rx)` → 1×0.
  - trigger on `emu`: `device_get_trigger(rx)` raises `iio:ENOSYS`; `device_set_trigger(rx, phy)`
    raises `iio:EINVAL`; `(rx, 0)` raises `iio:ENOSYS`.
  - trigger on `fake`: `device_get_trigger(rx)` → 0; `device_set_trigger(rx, trigger0)`;
    `device_get_trigger(rx)` → `dp_dev(ctx, "trigger0")`; `device_set_trigger(rx, 0)`; → 0. With a
    second context open: `device_set_trigger(rx, trigger0 of the other context)` raises `iio:EINVAL`.
- [ ] **Step 2:** Run G1 for `test_cmd_lowlevel` in `build-mex-lowlevel`. Expected: FAIL.
- [ ] **Step 3:** Implement `cmd_lowlevel.c`.
- [ ] **Step 4:** Run G1 for `test_cmd_lowlevel`. Expected: PASS, no sanitizer report.
- [ ] **Step 5:** Write `matlab/tests/tDeviceLowLevel.m` (uses the `iio.Device` members of "MATLAB surface", which Task 14 writes): `sampleSize` (4, 10,
  0 for a new mask; `mask.sample_size` and `bs.sample_size` agree); `registers`
  (`verifyError(@() phy.reg_read(16), 'iio:ENOENT')`; write `0xCAFE`; `reg_read` is
  `uint32(51966)`); `registerRange` (`'iio:EINVAL'` for `2^32`); `debugAttrs`
  (`string({phy.debug_attrs.name})` is `["bist_prbs" "direct_reg_access" "loopback"
  "loopback_available"]`; `isempty(phy.find_debug_attr("nope"))`; `isempty(rx.debug_attrs)`);
  `eventAttrs` (`phy.event_attrs.name` is `'events_enabled'`); `triggerOnEmu` (`'iio:ENOSYS'`, and
  `'iio:EINVAL'` for a non-trigger); `triggerFake` (on `tc.Fx.fakeContext()`: `isempty(rx.get_trigger())`;
  `rx.set_trigger(trg)`; `rx.get_trigger().id` is `'trigger0'`; `rx.set_trigger([])`; empty again).
- [ ] **Step 6:** Run G2, G3 (`tDeviceLowLevel.m`), G4 (`--group lowlevel`: `0 error(s)`; pending lines about `iio.Device` members name `Device.m`, a file of Task 14, and are not yours). `rm -rf build-mex-lowlevel`.
- [ ] **Step 7:** Commit `feat(mex): device low-level commands (sample size, trigger, registers, debug and event attrs)`.

### Task 21: Fast path `RxStream` / `TxStream`

**Wave 1b. Depends on:** Task 9 and the green wave 1a gate (and Tasks 2 and 3 through it). **Group:** `conv`. **Build directory:** `build-mex-conv`.

**Files:**
- Modify: `matlab/mex/cmd_conv.c`
- Create: `matlab/+iio/RxStream.m`, `matlab/+iio/TxStream.m`
- Test: `tests/mex/test_cmd_conv.c`, `matlab/tests/tRxStream.m`, `matlab/tests/tTxStream.m`

**Interfaces:**
- Consumes: `iio-conv-stream.h` (Tasks 2 and 3), `mx_util.h`, `data_path.h` (`dp_opt_*`, `dp_check_matrix`, `dp_class_from_id`), `dp_test.h`; by name, from M code only: `iio.Device` (Task 14), `iio.Channel` (Task 15); `EmuFixture.fakeContext` in the M tests.
- Produces: the commands of group `conv` in the "Command reference"; `iio.RxStream` and `iio.TxStream` as in "MATLAB surface". **Commands produced:** `conv_stream_open`, `conv_stream_read`, `conv_stream_write`, `conv_stream_flush`, `conv_stream_close`.

- [ ] **Step 1:** Write `tests/mex/test_cmd_conv.c` with `MT_GROUPS(&gw_group_conv);`, receive on `emu` (`chans` = cell `{'voltage0_i','voltage0_q'}`
  unless stated; `opts` always has `output false`):
  - `rx_int16` (R1, R3, R5, R13): `samples 1024, class 'int16', complex true` → `cs`: owned, parent
    = the id of buffer 0 of `emu-rx`. `conv_stream_read(cs)` with 2 outputs → `data` 1024×1,
    `mxINT16_CLASS`, complex, element k `(k-2048) + (2047-k)i` (every element is compared with its value: a byte scan for the mock's
    uninitialised pattern `0xA5` cannot be used, it is a legitimate byte of the ramp); `valid` a logical scalar true. Second read starts `-1024 + 1023i`.
  - `rx_defaults`: only `samples 8` → 8×2 real double, column 1 `-2048 .. -2041`, column 2 `2047 .. 2040`.
  - `rx_single_fullscale` (R12c): `class 'single', scale 'fullscale', complex true` → first element
    `-1 + 0.99951171875i`. `rx_raw` (R12a): `raw true, class 'int16', complex true` → `2048+2047i`,
    `6145+2046i`.
  - `rx_pairing` (R4): chans `{'voltage1_i','voltage1_q','voltage0_i','voltage0_q'}`, `samples 512,
    class 'int16', complex true` → 512×2; (1,1) `-2047+2046i`, (1,2) `-2048+2047i`, (512,1) `-1025+1024i`.
    The same list given as a `uint64` vector of channel ids gives the same data.
  - `rx_gain`: `class 'double', complex true, gain 2` → first `-4096 + 4094i`; pairing list with
    `gain [2 3]` → (1,1) `-4094+4092i`, (1,2) `-6144+6141i`; `gain [1 2 3]` → `iio:EINVAL`;
    `class 'int16', gain 2` → `iio:EINVAL`.
  - `rx_errors`: `class 'int12'`, `scale 'full'`, a field `colour`, `samples 0`, `nb_blocks 0`,
    each of `samples`, `nb_blocks`, `buffer`, `timeout_ms` as `NaN`, `Inf`, `2.5` and `1e300`, `buffer -2`,
    `timeout_ms -2`, `nb_blocks 2^32`, `samples` as a 1×0 double (not given, and it is required),
    missing `samples`, missing `output`, `complex true` with 3 channels, `cyclic true`, `buffer 3`,
    `output true` with handles of input channels → `iio:EINVAL`; id `'voltage9'` → `iio:ENOENT` and
    `mock_err_msg()` contains `voltage9`; `iio:device4` `{'rot_quaternion'}` → `iio:ENOTSUP`; an `xml`
    context → `iio:ENOSYS`; a stale device id → `iio:EBADF`. After each, no handle of type
    `GW_T_CONV_STREAM` exists and no leak is reported (an open that fails after the buffer was chosen leaves the borrowed buffer id; the test registers the buffers with `dp_buf` first and then counts exactly).
  - `rx_flush_close` (R7, R21): 2 blocks; read, `conv_stream_flush(cs)` → true, read starts
    `1024 - 1025i`. `conv_stream_close(cs)` twice and `conv_stream_close(0)`: no error; then
    `conv_stream_read(cs)` raises `iio:EBADF`. `conv_stream_write(rx cs, data)` raises `iio:EBADF`.
- [ ] **Step 2:** Add transmit on `emu` (`emu-tx`, chans `{'voltage0_i','voltage0_q'}`, `output true, samples 4,
  nb_blocks 1, complex true`; `A` = complex int16 `[100+1i; -200+2i; 300+3i; -400+4i]`; "file" as in Task 3):
  - `tx_basic` (R2, R10): `conv_stream_write(cs, A)` → true; `conv_stream_flush(cs)` → true; file
    `100,1,-200,2,300,3,-400,4`.
  - `tx_real_input` (a complex array with a zero imaginary part arrives real): real int16 `[7;-8;9;-10]` → true; flush; file `7,0,-8,0,9,0,-10,0`.
  - `tx_classes`: complex double `[100.4-200.6i; 32767.9-40000i; 0.5+1.5i; 2.5-0.5i]` → file
    `100,-201,32767,-32768,0,2,2,0`; complex int32 `[40000-40000i; 5-5i; 32768-32769i; 0+1i]` → file
    `32767,-32768,5,-5,32767,-32768,0,1`; a complex single and a complex uint8 frame are accepted.
  - `tx_shape` (R2, exact identifiers): 4×2 → `iio:EINVAL`; 5×1 → `iio:EINVAL`; 0×1 → `iio:EINVAL`;
    4×1×2 (`mock_nd_array(mxINT16_CLASS, mxCOMPLEX, 4, 1, 2)`) → `iio:EINVAL`; char, logical, cell, sparse double → `iio:EINVAL`; each message names what
    was expected (`mock_err_msg()` contains `columns` for the first, `rows` for the second and third).
    2×1 → true and, after flush, a file of 8 bytes. After every rejected call the file is unchanged.
  - `tx_real_stream`: `complex false`, 2 channels: real int16 4×2 `[100 1; -200 2; 300 3; -400 4]` →
    file `100,1,-200,2,300,3,-400,4`; complex 4×2 → `iio:EINVAL`.
  - `tx_scaled`: `scale 'fullscale'`: complex double `[1-1i; 0.5-0.5i; 0.25; 0]` → file
    `32767,-32768,16384,-16384,8192,0,0,0`; complex int16 → `iio:EINVAL`.
  - `tx_cyclic` (R9): `cyclic true, nb_blocks 4`: write → true; write → `iio:EBUSY`; close in < 1 s.
  - `tx_buffer_choice` (R12; transmit storage word when bits < length): `emu-multi`, chans `{'voltage0','voltage1'}`, `output true,
    samples 2` (buffer picked: index 1). With `scale 'fullscale'`: real double `[0.5 1; -0.5 -1]` →
    file `emu-multi_buf1.bin` words `4000 7ff0 c000 8000`. With no scale: int16 `[32767 -32768; 1 -1]`
    → `7ff0 8000 0010 fff0`; the same with `raw true` → `7fff 8000 0001 ffff`.
  - `rx_and_tx` (R22): both streams open on one context, alternate read and write: values as above.
- [ ] **Step 3:** Add the `fake` cases (R8; `ctx = dp_open_fake(fx.xml)`):
  - `rx_no_frame`: `samples 4, nb_blocks 2, class 'int16', complex true, timeout_ms 0`: three reads
    are valid, data `[0+1i; 2+3i; 4+5i; 6+7i]`, then first elements `256+257i`, `512+513i`; fourth
    read: no error, `valid` false, `data` 4×1 complex int16 with every byte 0. With `timeout_ms 30`
    the fourth read takes ≥ 0.030 s and < 1 s.
  - `tx_no_block`: `emu-tx`, `nb_blocks 2, timeout_ms 0`: writes 1..4 → true; write 5 → false and
    `fake.tx_data` still holds frame 4.
  - `unload` (unload with open streams): one receive and one transmit stream left open, `mock_exit_matlab()` →
    `fake.log` ends with `C`, contains two `C`, as many `F` as `B` and exactly one `D` (the transmit frame is awaited before the cancel); no leak report.
  - `unload_tx_infinite` (a destructor never blocks without bound): `iio_context_set_timeout(ctx, -1)` with libiio
    directly, `fake.tx_frames = 0`, a transmit stream with `nb_blocks 2` and no `timeout_ms`, two writes → true;
    `mock_exit_matlab()` returns in < 2 s; no leak report. (The bounded poll fills the 256-character log with `d`, so the letters after it are lost: the case asserts the time, at least 3 `d` and zero handles.)
  - `mt_oom_sweep(1, "conv_stream_open", 3, ..)` on the fake returns ≥ 1 and the log holds exactly one `O`.
- [ ] **Step 4:** Run G1 for `test_cmd_conv` in `build-mex-conv`. Expected: FAIL.
- [ ] **Step 5:** Implement `cmd_conv.c`. Order inside `conv_stream_read`: resolve the handle, create the output
  with `mxCreateUninitNumericMatrix(samples, columns, dp_class_of_type(iio_conv_stream_get_type(s)), complex ? mxCOMPLEX : mxREAL)`,
  store it in `c->plhs[0]` at once (the dispatcher destroys `plhs` on failure), call
  `iio_conv_stream_read(s, mxGetData(out))`, on "no frame" `memset` the output to 0, on error return
  it through `gw_fail`. `conv_stream_open`: the integer options with `dp_opt_int` (`samples` 1 .. `LLONG_MAX` with default 0 = missing (the stream layer judges the size), `nb_blocks` 1 .. 4096 default 2 (`UINT_MAX` until the wave 1b review), `buffer` -1 .. `UINT_MAX` default -1, `timeout_ms` -1 .. `INT_MAX` default -1; never a cast of the `double` of `dp_opt_number`); a successful open with `struct(.., gain 1×0 double, offset 1×0 double)` (what `iio.RxStream` passes by default) is part of the `rx` test; gains with `mxMalloc`, freed with `mxFree` on every return path. `conv_stream_write` reads
  `mxGetData(prhs)` in place.
- [ ] **Step 6:** Run G1 for `test_cmd_conv`. Expected: PASS, no sanitizer report, < 10 s.
  `grep -c mxCreateUninitNumericMatrix matlab/mex/cmd_conv.c` → `1`.
- [ ] **Step 7:** Write `RxStream.m`, `TxStream.m`.
- [ ] **Step 8:** Write `matlab/tests/tRxStream.m`: `parityConfig` (R1, R12: `iio.RxStream(rx,
  ["voltage0_i","voltage0_q"], SamplesPerFrame=1024, OutputDataType="int16", Complex=true,
  Raw=true)`; `[d, v] = r.read()`; `v` true; `size(d)` `[1024 1]`; `class(d)` `'int16'`; `~isreal(d)`;
  `d` equals `complex(typecast(w(1:2:2048),'int16'), typecast(w(2:2:2048),'int16'))` with `w` the
  first 2048 `uint16` words of `emu-rx_buf0.bin`); `converted` (`Raw=false`: equals `complex(I, Q)`);
  `consecutiveFrames` (second read is `k = 1024..2047`); `defaults` (`OutputDataType` `"double"`,
  real, N×2); `singleFullscale`; `twoComplexChannelsOrder` (R4: the swapped list of Step 1, 512×2);
  `channelObjects` (same result with `[v1i v1q v0i v0q]`, and `r.channels` is
  `{'voltage1_i','voltage1_q','voltage0_i','voltage0_q'}`); `singleFullscale` asserts class `single` and
  `d(1) == single(-1) + single(0.99951171875)*1i` (mock case `rx_single_fullscale`); `charAndCellInput` (`'voltage2'`
  as a char row gives the same data as `"voltage2"`; `{'voltage0_i','voltage0_q'}` the same as the string array;
  `r.channels` is a 1-by-2 cell of char); `gainPerColumn` (values of mock case `rx_gain`: `Gain=2` → `d(1)` is
  `-4096 + 4094i`; the swapped list with `Gain=[2 3]` → `d(1,:)` is `[-4094+4092i, -6144+6141i]`; `Gain=[1 2 3]` →
  `'iio:EINVAL'`); `flush` (mock case `rx_flush_close`: `NumBlocks=2`, read, `flush()` is true, the next frame starts
  with `complex(int16(1024), int16(-1025))`); `closeTwiceAndDelete` asserts no error from `r.close(); r.close();
  delete(r)` and `iio.internal.handle_count()` one below its value with the stream open (not "back at its value before the stream was made": the open registers the borrowed buffer id, which stays until the context goes); `scaleWithIntegerClass` (`'iio:EINVAL'`); `unknownChannel` (`'iio:ENOENT'`);
  `badOptionValue` (`OutputDataType="int12"` → an identifier starting with `MATLAB:`, by try/catch);
  `timeoutGivesInvalid` (R8: `tc.Fx.fakeContext()`, `Timeout=0`, `SamplesPerFrame=4`: three valid reads,
  then `v` false, `d` is `complex(zeros(4,1,'int16'))` in size and class, `real(d)` and `imag(d)` all zero);
  `closeTwiceAndDelete` (R21); `readAfterClose` (`'iio:EBADF'`); `reopen` (R21: close, new object,
  first frame again); `contextSurvives` (`clear` of the context variable while the stream exists:
  `read` still valid).
- [ ] **Step 9:** Write `matlab/tests/tTxStream.m`: `complexInt16` (`t.write(A)` true, `t.flush()` true,
  `tc.Fx.readInt16("emu-tx_buf0.bin")` is `int16([100;1;-200;2;300;3;-400;4])`); `realInputOnComplex`
  (a complex array with a zero imaginary part arrives real: `t.write(complex(int16([7;-8;9;-10]), int16(0)))` and `t.write(int16([7;-8;9;-10]))`
  both give `7,0,-8,0,9,0,-10,0`); `doubleInputSaturates` and `int32InputSaturates` (the two frames and files of mock case `tx_classes`);
  `partialFrame` (2 rows → 8 bytes); `lastFrameSentOnClose` (R10: `NumBlocks=2`, three writes, close →
  file is the third frame); `wrongColumns`, `tooManyRows`, `emptyData`, `charData` (each
  `'iio:EINVAL'`); `complexDataOnRealStream` (`'iio:EINVAL'`); `cyclicSecondWrite` (R9:
  `'iio:EBUSY'`); `fullscale` (frame and file of mock case `tx_scaled`); `multiBufferDevice` (`emu-multi`, words `4000 7ff0 c000 8000` read as
  `uint16`); `noFreeBlockGivesInvalid` (R8, fake: fifth write returns false); `rxAndTxTogether` (R22: an `RxStream` and a
  `TxStream` on one context; read, write `A`, read, flush: the two frames of `tRxStream/consecutiveFrames` and the
  file of `complexInt16`).
- [ ] **Step 10:** Run G2, G3, G4 (group `conv`). `rm -rf build-mex-conv`.
- [ ] **Step 11:** Commit `feat(matlab): RxStream and TxStream on the C stream layer`.

### Wave 1a gate (controller, after Tasks 12, 13 and 14 are committed; before any wave 1b task starts)

- [ ] Build everything (`B=build-mex`) and run `ctest --test-dir build-mex -L mex --output-on-failure`; `rm -rf build-mex`. Push and wait for CI (gate G5). Expected in both `matlab` jobs: `tSlice`, `tFixture`, `tObjectModel`, `tContext`, `tAttr`, `tDevice` green.
- [ ] For every red MATLAB test: repair it (Appendix C). If the cause is an M idiom (class syntax, `arguments` behaviour, object arrays, empties, property access) rather than a slip of one file, write the corrected rule into "Conventions / MATLAB (M)" and correct the "MATLAB surface" outlines that use it, commit the plan, and only then start wave 1b.

### Wave 1 gate (controller, after Tasks 10 to 21 are committed)

- [ ] Build everything (`B=build-mex`, build line of "Gates") and run `ctest --test-dir build-mex -L mex --output-on-failure`. Failures here are integration defects for Task 22; do not start wave 2b before Task 22 is done. Done at the integration of wave 1b: 40 tests pass, `check_api.py --strict` reports 0 errors and no pending line. `tests/mex/test_m_sequences.c` was added there: it links every group and replays, with gateway commands only, the call sequences of the M classes that cross command groups, with the values `matlab/tests` asserts (fake context adoption, coupled channel, `'OBBEESXsFFXC'` at shutdown, the blocks and the buffer stream of an `iio_stream` in groups block, buffer, channel and lowlevel, transmit through stream blocks, masks and sample sizes, registers and triggers, context destroyed first). Task 22 extends it rather than repeating it in `test_integration.c`. `tests/fake_backend.h` now sizes its URI buffer for `struct mt_fixture.xml` (the `-Wformat-truncation` that three wave 1b tests worked around).
- [ ] Push and wait for CI (gate G5). Expected in both `matlab` jobs: `tSlice`, `tFixture`, `tObjectModel`, `tTopLevel`, `tContext`, `tAttr`, `tDevice`, `tFakeBackend`, `tChannel`, `tBuffer`, `tBlock`, `tStream`, `tEventStream`, `tDeviceLowLevel`, `tRxStream`, `tTxStream` run. Hand every red test to Task 22 with the evidence of Appendix C, step 1.

### Wave 1b review (four verifiers on the committed wave; done)

No blocker was found. What the review changed (commits after `5dd7c54`), so that Task 22 does not repeat it:

- **Transmit gain and offset.** The C library is the authority: device = (host − offset) / gain, the exact inverse of receive. The plan and the help texts said otherwise; the code was right and is unchanged. Rule and example: group `block` of the "Command reference". Help of `iio.TxStream`, `iio.Block.write`, `iio.RxStream`, `iio.Block.read`.
- **Arguments of an iio class** are declared with `mustBeA`, never with the class ("Conventions / MATLAB (M)").
- **`block_write` with real data on I/Q pairs** validates the full pair list as for complex data; a channel twice in `channels` is `iio:EINVAL` in `block_read` and `block_write`; `block_disable_cpu_access` on a stream's block is `iio:EPERM`; `channel_write` consumes whole samples only; `conv_stream_open` takes at most 4096 blocks; `enable` / `disable` of an empty row of channels is `iio:EINVAL`.
- **emu and frames longer than its data file:** "Test data".
- **Tests added for mutants that survived** (each was applied again and is now killed by `test_cmd_<group>`): the converter cache key item by item (channel order, `complex`, offset alone, gain vectors with the same first value, `scale` and `shift` of the data format), a block that ends in half a sample on every path, the `complex`-false check on its own, a 0-by-0 array, gain and offset per column for real data on pairs, `stream_cancel` on the fake log, the other flag bits of a stream's block view, the zero fill of a frame that did not come for `single` and `int8`, the two conditions of the bounded close drain, the automatic buffer choice, `nonblock` and `-EAGAIN` of `event_stream_read` (the fake logs `n` / `b` and has `ev_none`), the type and the modifier check of the event channel each on its own, EPERM before EINVAL in `channel_enable`, the 63/64-character limit of `test_fake_set('couple', ..)`.

Left as it is, on purpose, and to be decided or stated by the task named:

- **Not observable with the two test backends** (the code was read; a mutant survives): the `params` argument reaching `iio_has_backend` and `iio_scan`, and the numeric guard and the `iio_err` check of `scan` (`iio_scan` fails only on `calloc`); the numeric guard of `device_reg_read` / `device_reg_write` (above); the upper-case letters and the 16-bit bound in the channel number of `event_get_channel` (no fixture channel has such an id); the order in which the converter cache replaces entries (performance only). Task 22 does not have to find these again.
- **`Block.read` / `Block.write` and the fast path answer the same mistake differently** (`OutputDataType = "int12"` is `iio:EINVAL` from the gateway on a block and an `arguments` error on `iio.RxStream`; `Gain = single(2)` is converted by `RxStream` and refused by `Block.read`), and gateway messages name the gateway fields (`timeout_ms`, `nb_blocks`, `samples`) instead of the M options (`Timeout`, `NumBlocks`, `SamplesPerFrame`). Decided in Task 22: the M options of `Block.read` / `Block.write` are declared exactly as those of the fast path (`OutputDataType` and `Scale` with `mustBeMember`, `Gain` and `Offset` as `double`), so `"int12"` and an unknown scale are errors of the `arguments` block on both paths and `Gain = single(2)` converts on both (`tBlock.optionValuesAsOnTheFastPath`; `tBlock.scaleNeedsFloat` keeps the two cases only the gateway can judge). The gateway commands are unchanged (`block_read` still answers `class = 'int12'` with `iio:EINVAL`, `test_cmd_block.c`). The gateway messages keep the gateway's field names: the M validators stop most wrong values before the gateway, and the README (Task 26, step 1b, fact 12) gives the three names that can still appear.
- **Mixing classes in one array** converts through the one-argument constructors of `iio.ChannelsMask` and `iio.EventStream` ("Conventions / MATLAB (M)"). Decided in Task 22: no guard; stated in the help of the two classes and in the README (Task 26, step 1b, fact 11).
- **The close of a transmit stream with a long `Timeout`** waits that long and cannot be interrupted ("Data-path objects"): kept, stated in the help of `iio.TxStream` and in the README (Task 26).
- **Every MEX built today registers the `test_*` commands**: `IIO_MEX_TEST_HOOKS` follows `IIO_CONV_BUILD_TESTS`, whose default is ON, and both CI workflows use the default (`strings matlab/iio_mex.mexa64 | grep -c test_create_fake_context` is not 0). The commands touch only the fake's own state, so a session that never calls them is unaffected, but a MEX that is handed out is built with `-DIIO_CONV_BUILD_TESTS=OFF`: Task 26 states it under "Build", and Task 28 checks it (step 1a).

## Wave 2: integration, end-to-end tests, examples, documentation, CI

Order: Task 22 alone (wave 2a) → push, G5 → Tasks 23 to 27 in parallel (wave 2b, disjoint files) → push, G5 → Task 28 (controller).

### Task 22: Integration gate

**Wave 2a. Depends on:** every wave 1 task. **Build directory:** `build-mex`. Runs alone and may edit any file to repair an integration defect.

**Files:**
- Create: `tests/mex/test_integration.c`, `matlab/tests/tApiSurface.m`, `matlab/tests/tLifetime.m`, `matlab/tests/tStrings.m`
- Modify: `CMakeLists.txt` (default of `IIO_CONV_API_STRICT` becomes `ON`), `tests/mex/CMakeLists.txt` (one more bundled-library test), `tests/mex/test_m_sequences.c` (the gateway calls of `tLifetime` and `tStrings`, with the values those two assert), `matlab/api-map.tsv` (only rows the strict check reports; none was), any file with an integration defect. Done with the decisions the wave 1b review left: `matlab/+iio/Block.m` and `matlab/tests/tBlock.m` (options as on the fast path), help of `matlab/+iio/ChannelsMask.m` and `matlab/+iio/EventStream.m`

**Interfaces:**
- Consumes: `mex_test.h` with `mt_oom_sweep`, `dp_test.h`, `fake_backend.h`, `EmuFixture`, `IioTestCase`, "MATLAB surface"; `gw_groups[]`, `struct gw_group`, `struct gw_cmd` of `iio_mex.h` (to enumerate commands).
- Produces: G4 strict by default; test `test_integration` and `mex_bundled_libs_integration` (the same executable with `LD_LIBRARY_PATH=${Matlab_ROOT_DIR}/bin/glnxa64`).

- [ ] **Step 1:** Build everything (`B=build-mex`, header build line) and run `ctest --test-dir build-mex --output-on-failure`. Expected: every test passes, Part 1 tests included. Repair each failure in the file that owns the defect; one commit per repair, `fix(<group>): <what>`.
- [ ] **Step 2:** Run `python3 matlab/tools/check_api.py --iio-h deps/install/include/iio/iio.h --strict`. Expected last line: `check_api: 141 rows, 129 commands, 0 error(s)` with no `pending` part (110 `GW_CMD` and 19 `GW_XCMD` registrations). For each reported line correct the code, or the row if the code is right and the "Command reference" agrees with the code; never delete a row. The twelve `not_exposed` rows and the five `internal` rows keep their status.
- [ ] **Step 3:** Set `option(IIO_CONV_API_STRICT ... ON)`. Run `ctest --test-dir build-mex -R '^mex_api_map$'` after re-configuring. Expected: pass.
- [ ] **Step 4:** Write `tests/mex/test_integration.c` (`MT_GROUPS` naming all eleven groups and `gw_group_test`):
  - `staleIdsEverywhere`: on emu build one object of every handle type (context, device, channel, attr, buffer, mask, buffer stream with one started block on `emu-rx` and the read-only mask of that stream, `iio_stream` on `emu-tx` with its block view and its buffer-stream view, conv stream on `emu-multi`: one stream per buffer) and keep the ids, plus the id of a scan that `scan_destroy` released; `context_destroy`; `mex_handle_count` is 0. Then call every command of `gw_groups` with each stale id as first argument and `mt_empty()` for the remaining arguments up to its `min_rhs`, except `strerror`, `has_backend`, `get_builtin_backends`, `scan`, `create_context`, `event_get_type`, `event_get_direction` and names starting `mex_` or `test_`: a destructor command (`*_destroy`, `buffer_close`, `conv_stream_close`) returns normally; every other raises `iio:EBADF` or `iio:EINVAL`; none succeeds.
  - `fuzzNoCrash`: seed `0x1105`, 3000 calls; command drawn from all of `gw_groups` except `create_context`, `scan`, `context_destroy`, `mex_shutdown` and names starting `test_` (120 commands); 0 to 6 arguments drawn from `mt_str("")`, `mt_str("emu-rx")`, `mt_str("voltage0_i")`, `mt_dbl(0)`, `mt_dbl(-1)`, `mt_dbl(1e300)`, `mt_bool(true)`, `mt_empty()`, `mt_u64(0)`, `mt_u64(<destroyed context>)`, `mt_u64(<live emu context>)`, `mt_u64(<its emu-phy device>)`, `mt_u64(<a channel of emu-phy>)`, `mt_u64(<an attribute of emu-phy>)`, a 1-by-2 cell of ids, a scalar struct with one field, `mock_string_object()`, `mock_sparse_double(2,2)`, `mt_bytes` of 4 bytes; `nlhs` 0 to 3. `emu-phy` has no buffer and no stream or block id is in the pool, so no call can reach a dequeue, and none can reach `buffer_create_stream` or `buffer_stream_create_block` with `1e300`-like sizes (if a buffer or buffer-stream id is ever added to the pool, the numbers for these two commands must be bounded: a huge size that fits `size_t` is a `malloc` that AddressSanitizer aborts on). Before the random calls, one deterministic pass (the random draw would rarely hit it): every command with `min_rhs >= 2` is called with `(mt_u64(<the non-scan channel voltage0 of emu-phy>), mt_bytes of 4 bytes, mt_empty() ...)` up to its `min_rhs`, and with an empty `uint8` in place of the bytes. Each call: `mock_call` returns 0, or `mock_err_id()` starts with `iio:`. Measured when the test was written: of the 3000 calls drawn like this 7 return, because nearly all end at the argument count or at the first argument. The test therefore draws 3000 more from the same pool with an argument count the command accepts (`min_rhs` .. `max_rhs`), at most `max_lhs` outputs and, for half of the calls, one of the six ids (0, stale, context, device, channel, attribute) as first argument: 243 of those return, and the test asserts at least 200.
  - `oomSweep`: `mt_oom_sweep` over `create_context`, `context_find_device`, `device_find_channel`, `device_find_attr`, `context_get_devices`, `device_get_channels`, `context_get_xml`, `attr_read_raw`, `attr_get_available`, `create_channels_mask`, `buffer_open`, `buffer_stream_create_block`, `conv_stream_open`, `conv_stream_read`, each with valid arguments: every sweep returns ≥ 1 and the final attempt succeeds. For `create_context`, `create_channels_mask`, `buffer_open`, `buffer_stream_create_block` and `conv_stream_open`, `gw_handle_count()` after the sweep is exactly one more than before it (result first: a failed attempt registers nothing and creates nothing; the buffer's borrowed id is registered before the count is taken). `mt_oom_sweep` does not say how its last attempt ended, so the test also checks that `mock_err_id()` is empty after every sweep. The object of the last attempt has an id nobody received: `buffer_open` is swept on `emu-tx` and `conv_stream_open` on `emu-multi`, the streams whose ids the test needs are on `emu-rx`, and the frame after the `conv_stream_read` sweep is the third one (the sweep took one frame, not one per attempt).
  - `leakLoop`: 200 times: context, device, mask, buffer stream, block, then `context_destroy`; `mex_handle_count` is 0 after iteration 1 and after iteration 200; `mock_live()` is the same after both.
  - `clearWhileStreaming`: conv stream open on emu and one frame read; `mock_clear_mex()`; `mock_is_locked()` still true and a second read is valid with the next frame.
  - `xmlOnBothBackends`: `create_context` on `xml:` and on `emu:`; `context_get_xml` longer than 9000 characters for both.
  - `commaLocale` (Review Focus 22): inside ONE `if (mt_comma_begin()) { ... mt_comma_end(); }` block, with every object made by gateway commands inside the block: `create_context` on `xml:`, on `emu:` and `test_create_fake_context`; for each, `channel_get_data_format` of `emu-rx`/`voltage2` reports scale 0.25, `channel_convert` of the raw `int16` 4 on that channel gives the `int16` 1 (libiio's convert shifts and sign-extends, `>>2`; it never applies the scale, so the value the format implies is 1 × 0.25, and the scale itself is asserted through `channel_get_data_format`), and `context_get_xml` contains `scale="0.250000"`; on emu: `attr_get_range(frac_available)` is `[0 0.25 1]`, `attr_write_double(frac, 0.75)` reads `"0.75"`, one `conv_stream_open` + `conv_stream_read` with `scale "iio"` on `voltage2` gives the values of "Test data" (103, 102.75, ...); a `scan` on `"emu"`. `mt_comma_end()` proves that no command left the locale changed.
- [ ] **Step 5:** Register `mex_bundled_libs_integration` next to `mex_bundled_libs` (same `if(TARGET ...)` pattern). Run `ctest --test-dir build-mex -R 'integration' --output-on-failure`. Expected: 2 tests pass. A failing case is a gateway defect: repair the gateway, not the test.
- [ ] **Step 6:** Write `matlab/tests/tApiSurface.m`: `everyMemberExists`: for each row of `matlab/api-map.tsv` with a MATLAB column: `iio.<Class>.<member>` → `meta.class.fromName("iio.<Class>")` is not empty and `<member>` is among the names of its `MethodList` or `PropertyList`; `iio.<Class>` → the class exists; `iio.<function>` and `iio.internal.<function>` → `which` is not empty. `rowCount`: the file has a header line and 141 rows (it is found relative to the test file: `fullfile(fileparts(mfilename('fullpath')), '..', 'api-map.tsv')`).
- [ ] **Step 7:** Write `matlab/tests/tLifetime.m`: `childAfterContextDelete` (`delete(ctx)`; `attr.read()` of an attribute object made before raises `iio:EBADF`; `delete` of the children afterwards raises nothing); `scopeExitAnyOrder` (a local function builds context, device, mask, buffer stream, block, `RxStream` and returns nothing; `iio.internal.handle_count()` is 0); `clearMexKeepsObjects` (`clear mex` with a context and the attribute `emu-phy`/`text` alive; `attr.read()` is `'hello world'`); `shutdownClosesStreams` (a started `RxStream`; `iio.internal.shutdown()`; count 0; `rx.read()` raises `iio:EBADF`; a new `iio.Context` works); `constructorFailureLeavesNoHandle` (`iio.RxStream(dev, "nope")` raises `iio:ENOENT`; with n = the handle count after that first failure, 50 more failing constructions leave the count at n; after `delete(ctx)` it is 0); `errorInDeleteIsNotSwallowed` (delete the context first, then `clear` the stream variable: no warning is issued, `lastwarn` unchanged). As written: the low-level objects (mask, buffer stream, block) are on `emu-tx` and the `RxStream` on `emu-rx` (one stream per buffer); `constructorFailureLeavesNoHandle` also repeats a failure that comes after the buffer was chosen (`Gain = 0`, `iio:EINVAL`) and a failing `iio.TxStream`; "raises nothing" in a `delete` is asserted with `verifyWarningFree`. The gateway side of every case is in `lifetime_and_strings` of `test_m_sequences.c`.
- [ ] **Step 8:** Write `matlab/tests/tStrings.m`: for `iio.Context`, `find_device`, `find_channel`, `find_attr`, `attr.write` and the channel list of `RxStream`, the `char` and `string` forms (for the list also a string array, a cellstr and a `char` row for one channel) give `isequal` results; `string(missing)` raises an error whose identifier starts with `MATLAB:` or `iio:`; `""` and `"µ"` as a device name return an empty array; `['emu-phy' char(0) 'x']` as a device name, `['calib_mode' char(0)]` as an attribute name and `write_string(['a' char(0) 'b'])` raise `iio:EINVAL` (text with an embedded NUL is refused by the gateway, never cut). As written, also: `char([181 86])` written to an attribute reads back as the same two characters, and `read_raw` gives `uint8([194 181 86 0])` (UTF-8, and the NUL that ends every attribute value on emu, as in `tAttr`); the channel list of `iio.TxStream` in two forms; a NUL in a channel id of the list (`iio:EINVAL`). `tApiSurface` has a third method, `namesThatDoNotExist`, which shows that its own check tells a missing name from an existing one.
- [ ] **Step 9:** Run `ctest --test-dir build-mex -L mex --output-on-failure`. Expected: all pass, including `mex_mlint` and `mex_mlint_r2023b` for the three new `.m` files.
- [ ] **Step 10:** Check the minimum release once, in a fresh directory: `rm -rf build-mex && cmake -S . -B build-mex -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR=/usr/local/MATLAB/R2023b && cmake --build build-mex && ctest --test-dir build-mex -L mex --output-on-failure`. Expected: all pass (headers, `libmx`, `mlint` and libxml2 2.10.3 of R2023b). `rm -rf build-mex`. Commit `test: integration gate, strict API map, lifetime and string sweeps`. (Done: 33 tests of label `mex` with either root, 42 tests in all. The MATLAB suite grows by 15 test methods, 189 to 204: `tApiSurface` 3, `tLifetime` 6, `tStrings` 5, `tBlock.optionValuesAsOnTheFastPath`; Task 27 takes its minimum from the green run. The suite at `e25b813`, the base of wave 2b, has 211 test methods: commits after this count added the rest.)

### Task 23: Toolbox-flow end-to-end test

**Wave 2b. Depends on:** Task 22. No build directory (no C code).

**Files:**
- Create: `matlab/tests/MiniRx.m`, `matlab/tests/MiniTx.m`, `matlab/tests/tToolboxFlow.m`

**Interfaces:**
- Consumes: "MATLAB surface" (`iio.Context`, `iio.Device`, `iio.Channel`, `iio.Attr`, `iio.RxStream`, `iio.TxStream`), `EmuFixture`, `IioTestCase`, "Test data".
- Produces: `MiniRx(uri, Device=, Phy=, Channels=, SamplesPerFrame=, OutputDataType=, Complex=, Raw=, Scale=)` with `setup()`, `[data, valid] = step()`, `release()`; `MiniTx(uri, Device=, Channels=, SamplesPerFrame=, Cyclic=)` with `setup()`, `valid = step(data)`, `flush()`, `release()`. Plain handle classes that perform the steps of `adi.common.Rx`/`Tx` in the same order: context, timeout, device lookup, attribute writes each followed by a read-back comparison, stream creation, step, release (idempotent, safe on a half-built object). As built: the constructors take exactly these options; further settings are public properties, set before `setup()`: `Timeout` (1000 ms, the context timeout) on both, `CenterFrequency`, `RFBandwidth` and `GainControlMode` on `MiniRx` (the attribute writes on the `Phy` device), `NumBlocks` (default 2) and `ToneFrequency` on `MiniTx` (its read-back write goes to `altvoltage0`/`frequency` of the transmit device, since it has no `Phy` option). `step()` and `flush()` on an object that is not set up raise `MiniRx:notSetUp` / `MiniTx:notSetUp`; a `flush` that returns false raises `MiniTx:flush`.

- [ ] **Step 1:** Write `MiniRx.m`, `MiniTx.m` and `tToolboxFlow.m`. N = 1000 unless stated; `w = EmuFixture.rampWords(4096)`, `wi = w(1:2:end)`, `wq = w(2:2:end)`; I(k) = `mod(k, 4096) - 2048`, Q(k) = `2047 - mod(k, 4096)`:
  - `controlPath` (requirements R14 in its `int64` and string forms, R15, R16, R18; the other typed forms of R14 and the large raw I/O of R17 are not observable here and are tested in `tAttr` and `test_cmd_attr`): on `emu-phy`: channel `voltage0` (input) attribute `rf_bandwidth`: `write(int64(20000000))`, `read()` is `'20000000'`; `write(100)` raises `iio:EINVAL` and the value stays; `gain_control_mode`: write `"manual"`, read `'manual'`; write `"bogus"` raises `iio:EINVAL`; channel `altvoltage0` (output) `frequency`: `write(int64(2450000000))`, `read_longlong()` is `int64(2450000000)`; debug attribute `loopback`: write `1`, read `'1'`; `reg_write(16, 51966)`, then `reg_read(16)` is `uint32(51966)`; `ctx.find_device("hmc425a")` is empty and raises nothing.
  - `rxParityRaw` (R1, R12): channels `["voltage0_i" "voltage0_q"]`, `OutputDataType="int16"`, `Complex=true`, `Raw=true`: frames f = 0…5 are `int16`, size `[1000 1]`, not real, `valid` true, equal to `complex(typecast(wi(k+1), 'int16'), typecast(wq(k+1), 'int16')).'` with k = `mod(f*1000 + (0:999), 4096)`.
  - `rxConverted`: the same with `Raw=false`: frame 0 equals `complex(int16(I(k)), int16(Q(k))).'`, k = 0…999.
  - `rxTwoComplexColumnsDouble` (R4): four ids `voltage0_i`, `voltage0_q`, `voltage1_i`, `voltage1_q`, `OutputDataType="double"`, `Scale="fullscale"`: size `[1000 2]`, class `double`; with s = `(0:999).'`: column 1 is `complex(I(2*s), Q(2*s))/2048`, column 2 `complex(I(2*s+1), Q(2*s+1))/2048`, exactly; `data(1,:)` is `[-1 + 0.99951171875i, -0.99951171875 + 0.9990234375i]`.
  - `rxPairingFollowsIdOrder`: ids `["voltage0_q" "voltage0_i"]`: real part Q(k), imaginary part I(k).
  - `rxRealIioScale`: id `"voltage2"`, `OutputDataType="single"`, `Complex=false`, `Scale="iio"`: class `single`, equal to `single((double(EmuFixture.s14shift2(w(1:1000))).' - 100) * 0.25)`; the first six values are `[103 102.75 359 102.75 615 102.75]`.
  - `rxSurvivesWrap`: frame 4 (samples 4000…4999) equals the definition with k = `mod(4000 + (0:999), 4096)`.
  - `rxReleaseAndReconfigure` (R21): `release()`, then `SamplesPerFrame=256` and `setup()`: the first frame starts at k = 0 again; `iio.internal.handle_count()` after the second `release()` is 0; a third `release()` raises nothing.
  - `txComplexInt16` (R2, R10): `emu-tx`, ids `["voltage0_i" "voltage0_q"]`, `SamplesPerFrame=256`; A = `complex(int16(100 + (0:255)), int16(-(100 + (0:255)))).'`; `step(A)` is true; `flush()`; `tc.Fx.readInt16("emu-tx_buf0.bin")` has 512 elements equal to `reshape([real(A) imag(A)].', [], 1)`.
  - `txDoubleRoundsAndSaturates`: `SamplesPerFrame=9`; `step(complex([0.4 0.5 1.5 2.5 -0.5 -1.5 40000 -40000 NaN].', 0))`, `flush()`: the file's odd elements (I) are `int16([0 0 2 2 0 -2 32767 -32768 0])`, the even elements (Q) all 0.
  - `txRealInputToComplexStream` (a complex array with a zero imaginary part arrives real): `step(double((1:256).'))` is accepted; after `flush()` I is 1…256 and Q is 0.
  - `txLastFrameOnRelease` (R10): `NumBlocks=2` (the property of `MiniTx`, not a constructor option); `step(A)`, `step(A + 1000)`, `release()` without `flush`: the file is the second frame. (`A + 1000` adds 1000 to the real part; the test builds that frame from its parts, without arithmetic on a complex integer array.)
  - `txWrongShape`: 2 columns for 1 expected raises `iio:EINVAL`; 257 rows with `SamplesPerFrame=256` raises `iio:EINVAL`.
  - `txCyclicSecondWrite` (R9): `Cyclic=true`: the first `step` is true; the second raises `iio:EBUSY`; `release()` raises nothing.
  - `rxAndTxTogether` (R22): one `MiniRx` and one `MiniTx` on the same URI (two contexts), three steps each, alternating: values as in `rxConverted` and `txComplexInt16`.
- [ ] **Step 2:** Run G3 for the three files with both MATLAB roots and G4 with `--strict`. Expected: no message, `0 error(s)`. Commit `test: toolbox Rx/Tx flow end to end on the emu fixture`. (No C build: the gateway side of every case is already covered by `test_cmd_conv`, `test_cmd_attr` and `test_cmd_lowlevel`.) As built: 16 test methods, the 15 above and `setupFailureReleases` (`release()` on an object never set up; a `setup()` that fails at the device lookup and one that fails when the stream opens leave `handle_count` 0, and the object works afterwards; the same for `MiniTx`). `txWrongShape` also sends a real 256-by-2 array (`iio:EINVAL`) and checks that a valid frame after the refused calls is the file content. Every expected value was first reproduced through the gateway on the mock runtime with a throw-away probe; since the wave 2b review the same sequences are permanent: `toolbox_flow` of `tests/mex/test_m_sequences.c`. Observed there and not asserted: `Raw=true` with `Scale="fullscale"` opens without error (only `Raw` with `Scale="iio"` is `iio:EINVAL`, Review Focus 12).

### Task 24: Timing report

**Wave 2b. Depends on:** Task 22. No build directory.

**Files:**
- Create: `matlab/tests/tTiming.m`

**Interfaces:**
- Consumes: `EmuFixture.writeWords`, `iio.RxStream`; environment variable `IIO_TEST_RESULTS` (folder; `tempdir` when unset).
- Produces: `<IIO_TEST_RESULTS>/timing.txt`.

- [ ] **Step 1:** Write `tTiming.m`, one test `fastPathVersusMCode`. N = 262144. Data without junk bits (what HDL delivers), so raw and converted agree: `k = 0:N-1; iq = [int16(mod(k,4096) - 2048); int16(2047 - mod(k,4096))]; tc.Fx.writeWords('emu-rx_buf0.bin', typecast(iq(:).', 'uint16'))` (1048576 bytes; one block is exactly one file). Four timed paths (three streams: the M-code form of (c) is a measurement of its own on the stream of (b), so the two ratios have four medians), each 5 warm-up frames then 30 timed frames (`tic`/`toc` per frame, median): (a) `RxStream` `OutputDataType="int16"`, `Complex=true`; (b) the transpose and pairing passes of `adi.common.Rx` (`Rx.m:78-86`; a lower bound of the toolbox step, which also preallocates a complex `double` array and assigns each column into it, so its result is `double`, not `int16`): `RxStream` `OutputDataType="int16"`, `Complex=false`, `Raw=true` (N-by-2), then `r = d.';` (the C-by-N layout the old base class delivered) and `out = complex(r(1,:), r(2,:)).';`; (c) `RxStream` `OutputDataType="double"`, `Complex=true`, `Scale="fullscale"` against (b) followed by `double(out)/2048`. Assertions: only `verifyEqual` (value and class) of the first frame of (a) with (b), and of (c) with its M-code form. Numbers are reported, never asserted: `tc.log` and one line per path in `timing.txt` (`<path> <median ms> <MS/s>`) plus the two ratios. As built: `timing.txt` has two comment lines starting with `#` (what was measured; and, since the wave 2b review, that every time includes the emu file read, so no MS/s figure is a device throughput and the two ratios are the result: the job summary shows the file without the help text of `tTiming.m`), the four path lines `fast_int16_complex`, `mcode_int16_complex`, `fast_double_fullscale`, `mcode_double_fullscale` and two lines `ratio <M-code path>/<fast path> <quotient>`. Every time includes the emu backend's read of the 1 MiB frame from the data file, the same in all paths, so the ratios understate the ratio of the conversions alone: whoever quotes them (Task 28) says so. The test also asserts that every frame is valid, that the first frame of (a) is the data written, and that `timing.txt` could be written (an unwritable folder fails the test). The gateway side (a file of exactly one frame, the three stream configurations, every frame equal to the file) is `timing_frames` of `tests/mex/test_m_sequences.c`.
- [ ] **Step 2:** Run G3 for the file with both MATLAB roots. Expected: no message. Commit `test: timing report, fast path against M-code conversion`.

### Task 25: Examples

**Wave 2b. Depends on:** Task 22. No build directory.

**Files:**
- Create: `matlab/examples/rx_capture.m`, `matlab/examples/tx_cyclic.m`, `matlab/examples/attributes.m`, `matlab/examples/low_level_blocks.m`, `matlab/examples/list_contexts.m`, `matlab/tests/tExamples.m`

**Interfaces:**
- Produces: functions with `arguments` blocks whose defaults address an ADALM-Pluto (`Uri="ip:192.168.2.1"`, `Device="cf-ad9361-lpc"` or `"cf-ad9361-dds-core-lpc"`, `Phy="ad9361-phy"`, `Channels=["voltage0" "voltage1"]`) and which return their result, so a test can run them on the fixture. (`tExamples` adds `matlab/examples` to the path itself, with a `PathFixture` in a `TestClassSetup` method, so it does not depend on what `source-folder: matlab` adds.)

- [ ] **Step 1:** Write `tExamples.m`: `rxCapture` (`data = rx_capture(Uri=tc.Fx.Uri, Device="emu-rx", Channels=["voltage0_i" "voltage0_q"], SamplesPerFrame=1000, Frames=3)` is 3000-by-1 complex `int16` with `data(1) == complex(int16(-2048), int16(2047))` and `data(3000) == complex(int16(951), int16(-952))`); `txCyclic` (on `emu-tx`: returns the 1024-by-1 complex frame it wrote; no error on return. Nothing can be asserted on `emu-tx_buf0.bin`: the stream layer never dequeues a cyclic block, and emu writes a transmit block only at its dequeue); `attributes` (on `emu-phy`: returns a table whose row `ensm_mode` has value `"fdd"`); `lowLevelBlocks` (on `emu-rx`: returns the 4000 raw bytes of one block; bytes 1:4 are `uint8([0; 8; 255; 7])`: `iio.Block.bytes` returns a column); `listContexts` (`list_contexts(Backends="emu")` returns a 0-by-2 table).
- [ ] **Step 2:** Write the five examples, 20 to 40 lines each, a help block first (what it shows, the Pluto call, the fixture call). `low_level_blocks` shows mask, `BufferStream`, `Block`, enqueue, start, dequeue, read, and releases in order.
- [ ] **Step 3:** Run G3 for the six files with both MATLAB roots and G4 with `--strict`. Expected: no message. Commit `docs: MATLAB examples, with a test that runs them on the fixture`. As built: 9 test methods (`rxCapture`, `rxCaptureUnknownDevice`, `txCyclic`, `attributesTable` (not `attributes`: a test method does not carry the name of the function it calls), `attributesWrite`, `attributesUnknownNames`, `lowLevelBlocks`, `lowLevelBlocksUnknownChannel`, `listContexts`). `attributes` has the options `Set` and `Value` (one write before the listing) and returns a table with the row names of the attributes and one string variable `value` (`<missing>` for an attribute that cannot be read); `low_level_blocks` returns `[raw, data]`; `tx_cyclic` has `Cycles`, `Amplitude` and `Seconds` (default 5; the test passes 0); `list_contexts` scans with `LogLevel = "error"`. `low_level_blocks.m` is 41 lines. Corrected by the wave 2b review: the default of `list_contexts` was `"usb,ip"`, which can never return a row with the libiio of `deps/build-libiio.sh` (no usb backend, no DNS-SD: `iio_scan("usb,ip")` has 0 results); it is `"local"` now, the only backend of this build that scans (0 or 1 row with the uri `local:`, by the machine: test `listContextsDefault`), and the help says what `"usb,ip"` needs. `attributes` with `Set` and no `Value` raises `attributes:noValue` before it opens the context (it used to end in an argument error of `write_double`). The suite has 10 test methods for the examples. The gateway calls of the examples are `examples` of `tests/mex/test_m_sequences.c` (attribute list order and values, the three classes of `Value`, the block bytes and the stale destroys of `low_level_blocks`, the cyclic frame, the three scans).

### Task 26: Documentation

**Wave 2b. Depends on:** Task 22. No build directory.

**Files:**
- Create: `matlab/README.md`
- Modify: `README.md` (replace "MATLAB bindings are the next part of the project." and add a section "MATLAB bindings")

**Interfaces:**
- Consumes: `matlab/api-map.tsv`, the "Command reference", "MATLAB surface" and "Test data" of this plan.

- [ ] **Step 1:** Write `matlab/README.md` with these sections: Requirements (Linux x86-64, MATLAB R2023b or later, GCC, CMake, Ninja); Build (`deps/build-libiio.sh`; `cmake -S . -B build -G Ninja -DIIO_CONV_BUILD_MATLAB=ON -DMatlab_ROOT_DIR=<root>`; `cmake --build build`; `addpath('<repo>/matlab')`); Quick start (receive, transmit, attribute: 15 lines); Object model and lifetime (who owns what, `delete`/`close`, `iio.internal.shutdown()`, why `clear mex` does not unload while objects exist); Fast path (every option of `RxStream`/`TxStream`, the `valid` output, `flush`, the timeout being a property of the context plus the per-stream `Timeout`); Errors (`iio:<ERRNONAME>`, `try`/`catch` on the identifier); Class reference (one table row per row of `api-map.tsv`: C function, MATLAB member, note; 141 C functions); Coming from libiio v0 (one row per v0 call the toolboxes use: `iio_create_context_from_uri`; `iio_context_set_timeout` (0 meant "no timeout" in v0); the per-type `iio_channel_attr_*`, `iio_device_attr_*`, `iio_device_debug_attr_*` calls; `iio_channel_enable`; `iio_device_set_kernel_buffers_count` → `NumBlocks`; `iio_device_create_buffer` → `RxStream`/`TxStream`; `iio_buffer_refill` → `read`; `iio_buffer_push` → `write`; `iio_buffer_get_poll_fd` → none; `iio_buffer_set_blocking_mode` → `Timeout=0`; `iio_channel_convert` → `Scale="none"`); Differences from the toolboxes' data path (the storage word needs `Raw=true`; transmit rounds half to even where `cast` rounds half away from zero; integer input saturates; scaled modes need a floating-point class); Limitations (no code generation yet; Linux x86-64 only; an attribute of 32767 bytes or more cannot be read from an iiod that speaks the legacy text protocol (`iio:EIO`), and none that does not fit a buffer of 32 MiB anywhere (`iio:EFBIG`; the read buffer starts at 32768 bytes and grows by four up to 32 MiB, so a value of 32 MiB − 1 characters is refused as well); a blocking call cannot be interrupted with Ctrl-C; no C pointer leaves the gateway, so libad9361 functions that take an `iio_device *` or `iio_context *` (`ad9361_set_bb_rate`, `ad9361_fmcomms5_multichip_sync`) cannot be called and must be reimplemented on attributes by a toolbox port; the untested list of "Conventions / Test data"); Testing (the five gates; the MATLAB suite needs a MEX built with tests enabled, which is the default); Troubleshooting (`Invalid MEX-file`: check `readelf -d matlab/iio_mex.mexa64` for `libiio.so.1` and its RUNPATH; rebuilding while MATLAB holds the MEX: `iio.internal.shutdown()`, then `clear mex`).
- [ ] **Step 1a:** The README's section "Attributes" states these four facts, each found by the wave 1a review: (1) `read()` returns text and is not exact for a value that is not UTF-8 (invalid bytes become U+FFFD, the text ends at the first zero byte): use `read_raw()` for bytes; (2) `write_double` with `Exact = true` (the default) sends every digit and the kernel truncates, where libiio's `"%f"` (`Exact = false`) rounds to six decimals: round in MATLAB first when a value was computed; a value that needs more than 40 decimals is sent with an exponent and a kernel driver refuses it; (3) text with a zero character (`char(0)`) is refused with `iio:EINVAL` everywhere: binary data goes through `write_raw`; (4) the bindings do not depend on the numeric locale of the MATLAB process (decimal comma) and do not change it.
- [ ] **Step 1b:** Facts found in wave 1b that the README states: (1) `Gain = 0` is `iio:EINVAL`; on receive the result is sample × Gain + Offset, on transmit the device gets (DATA − Offset) / Gain, the exact inverse (example: Gain 2, Offset 10: 100 is sent as 45, and 45 is received as 100), and with `Complex` the Offset applies to the real and to the imaginary part alike; (2) `BufferStream.cancel` leaves the stream "started" while libiio has stopped moving its blocks: call `stop`, then `start`, before queueing again; (3) `iio.Block(bs, n)` creates a block of `n` bytes for any numeric class except `uint64`, which is a handle; (4) with `iio.Stream` on an output buffer the block filled last is never sent, and after a failed `next()` every block is unreadable until a `next()` succeeds; (5) `TxStream.close` (and `delete`, `clear all`, MATLAB exit) waits for the frames in flight as `write` does and cannot be interrupted: up to `Timeout` ms; with `Timeout` -1 as long as the context timeout allows for each frame, and at most 1 s when the context timeout is `Inf`; (6) `handle_count` after an `RxStream` / `TxStream` was closed stays one above the count before the first open on that buffer (the buffer's own entry) until the context goes; (7) an argument that must be an iio object is checked, not converted: a device given where a mask belongs is an argument error (an identifier that starts with `MATLAB:`; the exact name `MATLAB:validators:mustBeA` is **[CI]** and no test asserts it, so the README does not give it); (8) on the `emu:` backend a frame must not be longer than the data file, or it is only partly data although `valid` is true ("Test data"): the receive examples name the frame size when they run on the fixture (`rx_capture`: `SamplesPerFrame = 1000`; `low_level_blocks`: `Samples = 1000`; `tx_cyclic` keeps its default, a transmit frame reads no file); (9) `NumBlocks` is at most 4096; a MEX file that is handed out is built with `-DIIO_CONV_BUILD_TESTS=OFF` (the default build registers the `test_*` commands of the test suite); (10) `enable` / `disable` of an empty row of channels (what `find_channel` returns for an unknown id) is `iio:EINVAL`; a channel may be listed once in `Channels` of `Block.read` / `Block.write`; (11) masks, event streams and devices are kept in separate arrays: `[mask dev]` and `masks(2) = dev` are `iio.ChannelsMask(dev)` to MATLAB and create a second mask, `[es dev]` opens another event stream; (12) an error message of the gateway names the gateway's fields: `samples` is `SamplesPerFrame`, `nb_blocks` is `NumBlocks`, `timeout_ms` is `Timeout`, `class` is `OutputDataType`; `Block.read` / `Block.write` check `OutputDataType`, `Scale`, `Gain` and `Offset` as `iio.RxStream` / `iio.TxStream` do.
- [ ] **Step 2:** Check the reference against the map: `cut -f1 matlab/api-map.tsv | grep -c -E '^(iio|hwmon)_'` prints `141`, and every one of those names occurs in `matlab/README.md` (`for f in $(cut -f1 matlab/api-map.tsv | grep -E '^(iio|hwmon)_'); do grep -q -w "$f" matlab/README.md || echo "missing $f"; done` prints nothing).
- [ ] **Step 3:** Edit `README.md`: status line; a "MATLAB bindings" section of at most 30 lines (what it is, the three build commands, a 10-line receive example, link to `matlab/README.md`, the limitations in one sentence).
- [ ] **Step 4:** Every class and option named in the two files exists: `grep -o -h 'iio\.[A-Z][A-Za-z]*' README.md matlab/README.md | sort -u` lists only names with a file `matlab/+iio/<Name>.m`. Commit `docs: MATLAB bindings README, class reference, v0 mapping, limitations`. As built: the reference table is generated from `matlab/api-map.tsv` (a copy: regenerate it when the map changes); facts 2, 3, 4, 10 and part of 12 of step 1b are in a section "Low-level data path"; the README gives no performance number (Task 28 adds the MATLAB figures to `docs/benchmarks.md`). Corrected by the wave 2b review: Requirements says which releases are tested (R2023b, R2025b) and which backends the pinned libiio has; Limitations separates "not built" (usb, serial, network scan) from "not tested"; a context has neither `close` nor `destroy`; `Buffer` is not a property of the two streams; G2 and G3 need a MATLAB installation and `IIO_CONV_BUILD_MATLAB=ON`; `IIO_TEST_NO_COMMA_LOCALE=1`; the root `README.md` names the prerequisites of the default build (Python 3, a decimal-comma locale for `ctest`) and gives the scalar range (4x to 18x) beside the SIMD range.

### Task 27: CI finishing touches

**Wave 2b. Depends on:** Task 22 and the green G5 run after it (it gives the test count). No build directory.

**Files:**
- Create: `matlab/tools/check_junit.py`
- Modify: `.github/workflows/ci.yml` (job `matlab` only), `.github/workflows/matlab-probe.yml` (action versions only), `matlab/tools/test_tools.py` (one more case)

**Interfaces:**
- Produces: `python3 matlab/tools/check_junit.py <junit.xml> --min-tests <n> --max-seconds <s> --max-skipped <n>`: exit 1 with one line per violation (fewer `testcase` elements than the minimum; any `failure` or `error`; more `skipped` than allowed; a `testcase` whose `time` ≥ the limit, unless its `classname` is `tTiming`); exit 2 when the file is missing or cannot be parsed; exit 0 with a last line `check_junit: <n> tests, 0 violation(s)`.

- [ ] **Step 1:** Add to `test_tools.py` a case that feeds four JUnit documents to `check_junit.py --min-tests 2 --max-seconds 4 --max-skipped 0`: two passing tests → 0; one `failure` → 1; one test with `time="5.2"` → 1; one `skipped` → 1; and a missing file → 2. Run `python3 matlab/tools/test_tools.py`. Expected: the new case errors (script missing).
- [ ] **Step 2:** Write `check_junit.py`. Run again. Expected: all cases pass. (This task compiles nothing; no build directory.)
- [ ] **Step 3 (controller: it needs a push and a CI run, so the implementer left it; open):** (The release matrix, the artifact names and the crash-dump collection exist since Task 8b.) Node 20, which the `@v2` MATLAB actions declare, is no longer on hosted runners: on a branch `probe/actions-v3` change `matlab-actions/setup-matlab`, `run-tests` and `run-command` to `@v3` in both workflows and push. If the probe run is green on both releases, apply the same change to `ci.yml` and `matlab-probe.yml` here; if not, stay on `@v2` and record the log line in `matlab/probes/README.md`. Delete the branch.
- [ ] **Step 4:** After `run-tests`, with `if: always()`: `python3 matlab/tools/check_junit.py test-results/matlab.xml --min-tests <N> --max-seconds 4 --max-skipped 0`, N = the test count of the green run after Task 22 rounded down to a multiple of 10; and a last step that appends `test-results/timing.txt` to `"$GITHUB_STEP_SUMMARY"` when the file exists, under a heading and inside a fenced code block (the summary is Markdown: plain lines would be joined into one paragraph and the comment line starting with `#` would be a heading). As built: N is 230 after the wave 2b merge (238 test methods: 211 at `e25b813`, plus 16 of `tToolboxFlow`, 1 of `tTiming`, 10 of `tExamples` with `listContextsDefault` of the wave 2b review; the 237 of the first count held before that method). Changed by the wave 2b review: the limit is `--max-seconds 4.9` ("Global Constraints"); the check step stands after "No crash dump after the exit checks", still with `if: always()`, so a duration violation does not skip the ten exit checks; the timing summary ends with a line `Runner CPU: <model name>` from `lscpu` of the MATLAB job's own runner (the only `lscpu` step was in the job `c-library`, another machine). `check_junit.py` also reports a `testcase` whose `time` is missing or not a finite number, and prints its last line with the violation count on failure too. Known noise: when the job fails before the tests run, this step is red as well (exit 2, no `matlab.xml`).
- [ ] **Step 5:** `actionlint .github/workflows/ci.yml` if `actionlint` is installed. Expected: no output. Commit `ci: JUnit check and timing summary for the MATLAB jobs`.

### Task 28: Release check (controller)

**Wave 2c. Depends on:** Tasks 23 to 27. Not an implementer task.

- [ ] **Step 1:** Push; watch the run in the background. First run with a duration gate and with two assumptions no green run has proven yet: (a) if `check_junit` reports only `took N s` lines on tests that are first of the run or reload the MEX (`tApiSurface.rowCount`, `tExamples.attributesTable`, the `clear mex` tests of `tSlice` and `tLifetime`) and no failure, it is cold start, not an emu stall: add an exemption list of `classname.name` to `check_junit.py` with a `test_tools.py` case, do not raise the limit to 5 s or more; (b) `tExamples.rxCaptureUnknownDevice` and `attributesUnknownNames` give an empty (1-by-0) iio object to a `(1,1) {mustBeA}` argument and expect an identifier that starts with `MATLAB:` (the precedent, `tChannel` line 274, is a 1-by-2 value): if one fails, `verifyValidationError` prints the identifier raised; adjust to it. Expected: `C library (ubuntu-24.04)`, `C library (ubuntu-24.04-arm)`, `MATLAB bindings (R2025b)`, `MATLAB bindings (R2023b)` green.
- [ ] **Step 1a:** A MEX without test hooks: in a fresh directory configure with `-DIIO_CONV_BUILD_MATLAB=ON -DIIO_CONV_BUILD_TESTS=OFF`, build the MEX target, and confirm `strings <the .mexa64> | grep -c test_create_fake_context` prints `0` (the test commands are then not registered: `iio_mex('test_fake_log')` would be `iio:ENOSYS`). Remove the directory. If the build needs the tests ON to produce the MEX, add a separate option for the hooks and report it.
- [ ] **Step 2:** Download both `matlab-*` artifacts; confirm `matlab.xml` has no skipped test; copy the four medians and two ratios of `timing.txt` into `docs/benchmarks.md` under "MATLAB fast path (emu, GitHub-hosted runner)" with the runner's CPU model (the line `Runner CPU:` of the job summary "MATLAB fast path timing" of the same job, never the `Show CPU` step of the job `c-library`), and state there that each time includes the emu backend's file read and that `mcode_int16_complex` is a lower bound of the toolbox step (Task 24). Commit `docs: MATLAB timing from CI`.
- [ ] **Step 3:** Add to the spec's "Changes after the C library landed" what differs from its Part 2 text (the C stream layer; `RxStream`/`TxStream` and `Block.read`/`Block.write` instead of `Buffer.read`/`Stream.read`; `iio.scan_contexts` instead of a `Scan` class; events as structs; 141 public functions in the pinned `iio.h`; no code generation yet; the gates). Commit.
- [ ] **Step 4:** Open a pull request to `main`. Merging and tagging wait for the user.

---

# Appendices

## Appendix A: `tests/resources/iio_fixture.xml`

The fixture verified by the research experiments (13033 bytes, sha256 `25710f34dd9b9660e0c426af673d24e80e61f355345db219a46dce4f7aab55fe`). Task 2 extracts it with the command in its Step 1; the two marker lines and the fence lines are not part of the file. Do not reformat this block.

<!-- fixture:begin -->
```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE context [
<!ELEMENT context (device | context-attribute)*>
<!ELEMENT context-attribute EMPTY>
<!ELEMENT device (channel | buffer | attribute | debug-attribute | event-attribute | buffer-attribute)*>
<!ELEMENT channel (scan-element?, attribute*, event-attribute*)>
<!ELEMENT buffer (attribute*, channel*)>
<!ELEMENT attribute EMPTY>
<!ELEMENT scan-element EMPTY>
<!ELEMENT debug-attribute EMPTY>
<!ELEMENT event-attribute EMPTY>
<!ELEMENT buffer-attribute EMPTY>
<!ATTLIST context name CDATA #REQUIRED version-major CDATA #REQUIRED version-minor CDATA #REQUIRED version-patch CDATA #REQUIRED version-git CDATA #REQUIRED description CDATA #IMPLIED>
<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>
<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED label CDATA #IMPLIED>
<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED label CDATA #IMPLIED>
<!ATTLIST buffer index CDATA #REQUIRED direction (in|out) #IMPLIED>
<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>
<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED value CDATA #IMPLIED>
<!ATTLIST debug-attribute name CDATA #REQUIRED value CDATA #IMPLIED>
<!ATTLIST event-attribute name CDATA #REQUIRED filename CDATA #IMPLIED value CDATA #IMPLIED>
<!ATTLIST buffer-attribute name CDATA #REQUIRED value CDATA #IMPLIED>
]>
<!--
  Hardware-free fixture for libiio v1 "emu:" and "xml:" backends.
  Rules this file obeys (all verified by experiment, see emu-backend.md):
   * valid against the DTD libiio itself emits (context.c), so libxml2 prints nothing;
   * in every device the scan-element channels come first, in increasing index
     order, then the channels without scan-element: "emu:" sorts channels that
     way, "xml:" keeps file order, so only this order gives the same channel
     numbering (and a correct iio_device_get_sample_size) on both backends;
   * every channel attribute named "scale" or "offset" has a numeric value
     ("emu:" reads them at context creation and fails otherwise);
   * a device uses either <buffer> elements or legacy <buffer-attribute>, never both.
  "emu:" data files live next to this file: <device name or id, ':' -> '_'>_buf<idx>.bin
-->
<context name="xml" version-major="1" version-minor="2" version-patch="3" version-git="v1.2.3-fixture" description="Emulated fixture &amp; test context">
  <context-attribute name="hw_model" value="Emulated Test Board"/>
  <context-attribute name="fixture" value="emu-backend-reference"/>
  <context-attribute name="num" value="42"/>
  <context-attribute name="escaped" value="a &amp; b &lt;c&gt;"/>

  <!-- RX: 2 complex channels, signed 12 bits in 16 (not fully defined), one scaled channel.
       Modern <buffer> form with direction. emu data file: emu-rx_buf0.bin -->
  <device id="iio:device0" name="emu-rx" label="rx-frontend">
    <channel id="voltage0_i" type="input">
      <scan-element index="0" format="le:s12/16&gt;&gt;0"/>
      <attribute name="sampling_frequency" filename="in_voltage_sampling_frequency" value="30720000"/>
      <attribute name="sampling_frequency_available" filename="in_voltage_sampling_frequency_available" value="[1000000 1000 61440000]"/>
    </channel>
    <channel id="voltage0_q" type="input">
      <scan-element index="1" format="le:s12/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage1_i" type="input">
      <scan-element index="2" format="le:s12/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage1_q" type="input">
      <scan-element index="3" format="le:s12/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage2" type="input" name="aux" label="aux-adc">
      <scan-element index="4" format="le:s14/16&gt;&gt;2" scale="0.250000"/>
      <attribute name="offset" filename="in_voltage2_aux_offset" value="-100"/>
      <attribute name="raw" filename="in_voltage2_aux_raw" value="1234"/>
      <attribute name="scale" filename="in_voltage2_aux_scale" value="0.25"/>
    </channel>
    <buffer index="0" direction="in">
      <attribute name="data_available" value="0"/>
      <attribute name="length_align_bytes" value="8"/>
      <attribute name="watermark" value="1024"/>
      <channel id="voltage0_i" type="input"/>
      <channel id="voltage0_q" type="input"/>
      <channel id="voltage1_i" type="input"/>
      <channel id="voltage1_q" type="input"/>
      <channel id="voltage2" type="input"/>
    </buffer>
  </device>

  <!-- TX: 2 complex channels, 16 bits fully defined, plus a DDS tone channel.
       Legacy form: no <buffer>, buffer 0 is created from the scan-elements and its
       direction is inferred from the output scan-elements. emu data file: emu-tx_buf0.bin -->
  <device id="iio:device1" name="emu-tx">
    <channel id="voltage0_i" type="output">
      <scan-element index="0" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage0_q" type="output">
      <scan-element index="1" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage1_i" type="output">
      <scan-element index="2" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage1_q" type="output">
      <scan-element index="3" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="altvoltage0" type="output" name="TX1_I_F1">
      <attribute name="frequency" filename="out_altvoltage0_TX1_I_F1_frequency" value="1000000"/>
      <attribute name="phase" filename="out_altvoltage0_TX1_I_F1_phase" value="90000"/>
      <attribute name="raw" filename="out_altvoltage0_TX1_I_F1_raw" value="1"/>
      <attribute name="scale" filename="out_altvoltage0_TX1_I_F1_scale" value="0.500000"/>
    </channel>
    <buffer-attribute name="data_available" value="0"/>
    <buffer-attribute name="watermark" value="2048"/>
  </device>

  <!-- PHY: no buffer. Attribute playground: typed values, *_available lists and
       ranges, debug attributes, event attributes, same channel id in both directions. -->
  <device id="iio:device2" name="emu-phy" label="phy">
    <attribute name="calib_mode" value="auto"/>
    <attribute name="calib_mode_available" value="auto manual tx_quad rf_dc_offs"/>
    <attribute name="dbl" value="3.25 dB"/>
    <attribute name="empty" value=""/>
    <attribute name="ensm_mode" value="fdd"/>
    <attribute name="ensm_mode_available" value="sleep wait alert fdd pinctrl"/>
    <attribute name="flag" value="1"/>
    <attribute name="frac" value="0.5"/>
    <attribute name="frac_available" value="[0 0.25 1]"/>
    <attribute name="gain" value="10"/>
    <attribute name="gain_available" value="[0 2 70]"/>
    <attribute name="hex" value="0x10"/>
    <attribute name="novalue"/>
    <attribute name="text" value="hello world"/>
    <debug-attribute name="bist_prbs" value="0"/>
    <debug-attribute name="direct_reg_access" value="0x0"/>
    <debug-attribute name="loopback" value="0"/>
    <debug-attribute name="loopback_available" value="0 1 2"/>
    <event-attribute name="events_enabled" value="1"/>
    <channel id="voltage0" type="input" name="RX1" label="rx1-port">
      <attribute name="gain_control_mode" filename="in_voltage0_gain_control_mode" value="slow_attack"/>
      <attribute name="gain_control_mode_available" filename="in_voltage_gain_control_mode_available" value="manual fast_attack slow_attack hybrid"/>
      <attribute name="hardwaregain" filename="in_voltage0_hardwaregain" value="71.000000 dB"/>
      <attribute name="rf_bandwidth" filename="in_voltage_rf_bandwidth" value="18000000"/>
      <attribute name="rf_bandwidth_available" filename="in_voltage_rf_bandwidth_available" value="[200000 1 56000000]"/>
      <event-attribute name="thresh_rising_en" filename="events/in_voltage0_thresh_rising_en" value="0"/>
      <event-attribute name="thresh_rising_value" filename="events/in_voltage0_thresh_rising_value" value="1000"/>
      <event-attribute name="thresh_rising_value_available" filename="events/in_voltage0_thresh_rising_value_available" value="[0 100 4000]"/>
    </channel>
    <channel id="voltage0" type="output" name="TX1">
      <attribute name="hardwaregain" filename="out_voltage0_hardwaregain" value="-10.000000 dB"/>
    </channel>
    <channel id="altvoltage0" type="output" name="RX_LO">
      <attribute name="frequency" filename="out_altvoltage0_RX_LO_frequency" value="2400000000"/>
      <attribute name="powerdown" filename="out_altvoltage0_RX_LO_powerdown" value="0"/>
    </channel>
    <channel id="altvoltage1" type="output" name="TX_LO">
      <attribute name="frequency" filename="out_altvoltage1_TX_LO_frequency" value="2450000000"/>
      <attribute name="powerdown" filename="out_altvoltage1_TX_LO_powerdown" value="1"/>
    </channel>
    <channel id="temp0" type="input">
      <attribute name="input" filename="in_temp0_input" value="35123"/>
    </channel>
    <channel id="accel_x" type="input">
      <attribute name="raw" filename="in_accel_x_raw" value="-17"/>
      <event-attribute name="mag_falling_en" filename="events/in_accel_x_mag_falling_en" value="1"/>
    </channel>
  </device>

  <!-- Two buffers on one device. emu data files: emu-multi_buf0.bin (in), emu-multi_buf1.bin (out) -->
  <device id="iio:device3" name="emu-multi">
    <channel id="voltage0" type="input">
      <scan-element index="0" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage1" type="input">
      <scan-element index="1" format="le:s16/16&gt;&gt;0"/>
    </channel>
    <channel id="voltage0" type="output">
      <scan-element index="2" format="le:s12/16&gt;&gt;4"/>
    </channel>
    <channel id="voltage1" type="output">
      <scan-element index="3" format="le:s12/16&gt;&gt;4"/>
    </channel>
    <buffer index="0" direction="in">
      <attribute name="watermark" value="16"/>
      <channel id="voltage0" type="input"/>
      <channel id="voltage1" type="input"/>
    </buffer>
    <buffer index="1" direction="out">
      <attribute name="watermark" value="32"/>
      <channel id="voltage0" type="output"/>
      <channel id="voltage1" type="output"/>
    </buffer>
  </device>

  <!-- No name, no label. Scan-element format zoo: shift, big-endian, repeat, float,
       1-bit, 64-bit timestamp (forces alignment padding). emu data file: iio_device4_buf0.bin -->
  <device id="iio:device4">
    <channel id="voltage0" type="input">
      <scan-element index="0" format="le:U8/8&gt;&gt;0"/>
    </channel>
    <channel id="voltage1" type="input">
      <scan-element index="1" format="be:u24/32&gt;&gt;8"/>
    </channel>
    <channel id="voltage2" type="input">
      <scan-element index="2" format="le:s12/16&gt;&gt;4"/>
    </channel>
    <channel id="rot_quaternion" type="input">
      <scan-element index="3" format="le:s16/16X4&gt;&gt;0"/>
    </channel>
    <channel id="voltage3" type="input">
      <scan-element index="4" format="le:f32/32&gt;&gt;0"/>
    </channel>
    <channel id="voltage4" type="input">
      <scan-element index="5" format="be:F64/64&gt;&gt;0"/>
    </channel>
    <channel id="voltage5" type="input">
      <scan-element index="6" format="le:u1/8&gt;&gt;7"/>
    </channel>
    <channel id="timestamp" type="input">
      <scan-element index="7" format="le:s64/64&gt;&gt;0"/>
    </channel>
  </device>

  <!-- hwmon: any device whose id starts with 'h' is hwmon; channel types use the hwmon table -->
  <device id="hwmon0" name="emu-hwmon">
    <channel id="in1" type="input">
      <attribute name="input" filename="in1_input" value="12000"/>
      <attribute name="label" filename="in1_label" value="VCC12"/>
    </channel>
    <channel id="fan1" type="input">
      <attribute name="input" filename="fan1_input" value="3200"/>
    </channel>
    <channel id="pwm1" type="output">
      <attribute name="pwm" filename="pwm1" value="128"/>
    </channel>
    <channel id="temp1" type="input">
      <attribute name="crit" filename="temp1_crit" value="100000"/>
      <attribute name="input" filename="temp1_input" value="45500"/>
    </channel>
    <channel id="curr1" type="input">
      <attribute name="input" filename="curr1_input" value="1500"/>
    </channel>
    <channel id="power1" type="input">
      <attribute name="input" filename="power1_input" value="18000000"/>
    </channel>
    <channel id="energy1" type="input">
      <attribute name="input" filename="energy1_input" value="123456789"/>
    </channel>
    <channel id="humidity1" type="input">
      <attribute name="input" filename="humidity1_input" value="40000"/>
    </channel>
    <channel id="intrusion0" type="input">
      <attribute name="alarm" filename="intrusion0_alarm" value="0"/>
    </channel>
  </device>

  <!-- not a trigger: id does not start with "trigger" -->
  <device id="iio_sysfs_trigger">
    <attribute name="add_trigger" value="0"/>
    <attribute name="remove_trigger" value="0"/>
  </device>

  <!-- a trigger: id starts with "trigger", has a name, has no channel -->
  <device id="trigger0" name="emu-trigger">
    <attribute name="sampling_frequency" value="100"/>
  </device>
</context>
```
<!-- fixture:end -->

## Appendix B: Toolbox requirements and where they are tested

The Analog Devices MATLAB toolboxes (`adi.common.Rx` / `Tx` and their device classes) are the first user of these bindings. The numbers used in the tasks:

| # | Requirement |
|---|---|
| R1 | One-call receive: an N-by-M array, column k = channel k as listed, class, complexity and scaling fixed at stream creation. Parity default: `int16`, complex, no scaling |
| R2 | One-call transmit of an N-by-M array of any numeric class, complex or real; cast, saturation, I/Q interleave and submission in C; a clear error for a wrong size |
| R3 | Stream creation in one call from device, ordered channel id strings, direction, frame length in samples, number of blocks, cyclic flag, class, complex flag, scale mode |
| R4 | Arbitrary channel subsets; I/Q pairing by the order of the id list, not by scan index |
| R5 | Fixed-size full frames (2^12 to 2^23 samples, up to 32 scan elements) |
| R6 | Configurable number of kernel blocks, default 2, 1 allowed |
| R7 | Discard stale data (`flush`) |
| R8 | A time limit per stream and a non-throwing "no data" result (`valid = false`) |
| R9 | Cyclic transmit; a second write while cyclic is a defined error |
| R10 | Transmit submits in the call that received the data; the last frame is not lost on release |
| R11 | Devices usable without a buffer (control only) |
| R12 | Raw-integer parity (the storage word, no shift or mask) and conversions as options: (a) raw, (b) converted integers, (c) scaled floating point |
| R13 | Layout contract: column-major N-by-M, interleaved complex |
| R14 | Typed attribute read/write by name (bool, `int64`, double, string) |
| R15 | "Not found" is testable without an error (empty result) |
| R16 | The control device differs from the streaming device; one object may use many devices |
| R17 | Binary-safe, large raw attribute I/O (47 KB profiles, 32768-byte images) |
| R18 | Register access: 32-bit address and value |
| R19 | Enumeration of devices, channels and attribute names |
| R20 | Byte counts and errno values are recoverable (bytes written; the errno name in the error identifier) |
| R21 | Usable from setup / step / release, re-entrant across release, idempotent teardown |
| R22 | Several objects on one URI at once (an Rx and a Tx object) |
| R23 | Raw `iio_device *` / `iio_context *` handed to libad9361. **Not provided**: no C pointer leaves the gateway; `ad9361_set_bb_rate` and the FMComms5 multichip sync must be reimplemented on attributes by the toolbox port (stated in `matlab/README.md`, Task 26) |
| R24 | Every fast-path feature exists as a plain C function taking pointers and scalars (code generation later) |

| Requirement and where it is tested | C stream layer (Task 2, Task 3) | Gateway mock (Task 21 unless stated) | MATLAB (G5) |
|---|---|---|---|
| R1 one-call receive, N×M | `rx_frames` | `rx_int16` | `tRxStream/parityConfig`, `converted` |
| R2 one-call transmit, any class, clear size error | `tx_one_block`, `tx_types` | `tx_basic`, `tx_classes`, `tx_shape` | `tTxStream/complexInt16`, `doubleInputSaturates`, `wrongColumns` |
| R3 creation in one call from ids | `rx_frames` (open) | `rx_int16` (ids as char) | constructors |
| R4 subsets, caller-defined pairing | `rx_pairing`, `rx_subset` | `rx_pairing` | `twoComplexChannelsOrder` |
| R5 fixed full frames | `rx_frames`, `tx_partial` (limits) | `tx_shape` | `tooManyRows` |
| R6 number of blocks, 1 allowed, default 2 | `rx_one_block` | `rx_flush_close` | `defaults` |
| R7 discard stale data | `rx_flush` | `rx_flush_close` | `flush` |
| R8 per-stream timeout, "no data" is not an error | fake `no_frame`, `late_frame`, `tx_timeout` | `rx_no_frame`, `tx_no_block` | `timeoutGivesInvalid`, `noFreeBlockGivesInvalid` |
| R9 cyclic transmit, defined error on a second frame | `tx_cyclic` (both files) | `tx_cyclic` | `cyclicSecondWrite` |
| R10 submit in the same call, last frame not lost | fake `tx_order`, `tx_drain`; `tx_two_blocks` | `tx_basic` | `lastFrameSentOnClose` |
| R12 raw parity, converted, scaled float | Task 1; `rx_raw`, `rx_float`, `tx_raw` | `rx_raw`, `rx_single_fullscale`, `tx_buffer_choice` | `parityConfig`, `singleFullscale` |
| R13 column-major interleaved complex | `rx_pairing` | `rx_int16` (mxArray element order) | `parityConfig` |
| R15 "not found" without abort (this section's finders) | — | Task 15, Task 20 `find_*` → 0 | `isempty(..find_attr("nope"))` |
| R18 register access | — | Task 20 registers | `tDeviceLowLevel/registers` |
| R19 enumeration of channels | — | Task 15 identity | `tChannel/identity` |
| R20 byte counts and errno recoverable | — | Task 13 (`attr_write_raw` → 47000), Task 6 (`t_errno`) | `tAttr/writeRaw`, every `verifyError` on an `iio:` identifier |
| R21 re-entrant across release, idempotent teardown | `close_twice` | `rx_flush_close`, `unload` | `closeTwiceAndDelete`, `reopen` |
| R22 Rx and Tx objects on one context | `rx_and_tx` | `rx_and_tx` | `rxAndTxTogether` |
| R24 plain-C layer usable from generated code | the API shape of `iio-conv-stream.h` (review) | — | out of scope (no Coder licence) |

Not testable without hardware and left to the hardware validation milestone: frame sizes up to 2^23 and 32 scan elements (R5), real `-ETIMEDOUT` timing, a cyclic block on a real DMA, cross-thread cancel. Requirement R14 asks for one MEX call per typed attribute access by name. Here a caller that keeps the `iio.Attr` object pays one gateway call per access; by name from a device, `dev.find_channel(id, out).find_attr(name).write(v)` costs ten gateway calls and two object constructions (`find_channel` 1, the six immutable properties of `iio.Channel` 6, `find_attr` 1, the name of `iio.Attr` 1, the write 1). A toolbox port should therefore look its `iio.Attr` objects up once in setup; one combined identity command per class is the first optimisation if profiling asks for it (not planned).

## Appendix C: When CI fails in MATLAB and every local gate passes

Implementers cannot push. An implementer's product is a diagnosis package (class, local reproduction or probe, expected outcomes); the controller pushes. For the first slice, the symptom table and fallback F1 of Task 8 come first.

1. **Get the evidence**: `gh run view <id> --log-failed | tail -200`; `gh run download <id> -n matlab-<release> -D <scratch dir>` gives `matlab.xml` (which test, which identifier), `matlab.log` (console), `iio-mex-trace.log` (after a crash its last `>` line without a matching `<` line is the command that did not return), `matlab_crash_dump.*` if MATLAB crashed.
2. **Classify.**

| Symptom | Class | First action |
|---|---|---|
| `Invalid MEX-file`, `undefined symbol`, `cannot open shared object file`, `version 'GLIBCXX_…' not found` | load failure | tests `mex_exports` and `mex_loadcheck` (gate G2); `readelf -d matlab/iio_mex.mexa64` for `NEEDED` and `RUNPATH` |
| no `matlab.xml`, a crash dump, exit code 139 | memory fault | the command from the trace; its `test_cmd_<group>` and `mex_bundled_libs*`; write a mock test with the same arguments |
| `MATLAB:UndefinedFunction`, `MATLAB:noSuchMethodOrField`, `MATLAB:class:…`, `MATLAB:validators:…`, `MATLAB:TooManyInputs` | M-code error that `mlint` cannot see | correct the M file; if a static rule could have caught it, add the rule to `check_api.py` with a case in `test_tools.py` |
| `verifyError`: another identifier than expected | gateway or errno naming | mock test asserting the identifier with the same inputs |
| wrong values, class or size | data path, or the MATLAB form of "Test data" | does `tFixture` pass? if not, the fixture M code is wrong; else a mock test with the same options |
| a test of 4.9 s or more (`check_junit`), job timeout | emu stall | the trace: a dequeue before start, or a blocking read on the fake after its frames ran out |
| red on R2023b only | syntax newer than R2023b, or libxml2 2.10.3 | G3 with `MATLAB_ROOT=/usr/local/MATLAB/R2023b`; rebuild with `-DMatlab_ROOT_DIR=/usr/local/MATLAB/R2023b` and run `ctest -L mex` |
| failure reported by `strict` | an unexpected warning | usually an error inside a `delete` method: read the warning text in `matlab.log`; `mock_warn_count()` for gateway warnings |
| `Gateway checks` step fails only in `mx_conformance_real` or `mex_loadcheck` | the runner's MATLAB layout | Task 8 Step 10, first row |

3. **Reproduce locally before changing code.** Translate the failing MATLAB test into a gateway test: the same commands (from the trace) through `mt_call`. If it fails locally, repair normally (test first).
4. **If it passes locally, the mock differs from MATLAB.** For `mx*` behaviour add the assertion to `tests/mex/mx_conformance.c`: `mx_conformance_real` runs it against the real `libmx.so` and shows which side is wrong; correct the mock so the gateway test of step 3 fails, then repair the gateway.
5. **If the behaviour needs the interpreter** (`mexErrMsgIdAndTxt` propagation, `mexLock` and `clear`, `mexAtExit`, string objects, destructor order, class syntax, validation errors): write a probe `matlab/probes/p_<topic>.m`, at most 30 lines, that prints `PROBE` lines, and state the hypothesis and the output expected if it is true and if it is false. The controller commits it on a branch `probe/<topic>`, pushes, reads `gh run view <id> --log | grep PROBE`, and deletes the branch. Probes are never merged.
6. **Limits.** One hypothesis per probe; at most three CI round trips per failure, then stop and report to the user with the evidence. No speculative change is pushed to see whether CI turns green.
7. **Close the gap.** Every CI-only failure ends with a local test that fails before the repair (mock test, conformance assertion, `check_api.py` rule), or with one line in a comment list "Known mock gaps" at the end of `tests/mxmock/mxmock.h` saying why no local test can exist.
