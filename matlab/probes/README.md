# MATLAB probes

A development aid from the time the bindings were written, when MATLAB could not be
started on the development machine: it ran only in GitHub Actions.
A probe is a small M file that answers ONE question about the behaviour of the real MATLAB
interpreter that no local gate can answer: how `mexErrMsgIdAndTxt` propagates, `mexLock` and
`clear`, `mexAtExit`, string objects, destructor order, class syntax, validation errors.

Probes are never merged. No `p_*.m` file belongs on `main` or on a feature branch; this
directory holds only `run_probes.m` and this file.

## Rules

- A probe is a file `matlab/probes/p_<topic>.m`, a script or a function without arguments,
  of at most 30 lines.
- One hypothesis per probe. Before the probe is pushed, write down the hypothesis and the
  output expected if it is true and if it is false.
- A probe prints its findings as lines that start with `PROBE `, for example
  `fprintf('PROBE version %s\n', version)`.
- Use a probe only when the behaviour needs the interpreter. A failure that can be
  reproduced with the mock runtime gets a gateway test (`tests/mex/test_*.c`); a question
  about an `mx*` function gets an assertion in `tests/mex/mx_conformance.c`.
- At most three CI round trips per failure; then stop and report the evidence. No
  speculative change is pushed to see whether CI turns green.
- Every answer a probe gives ends as a local test that fails before the repair (a mock test,
  a conformance assertion, a `check_api.py` rule), or as one line in the comment list
  "Known mock gaps" at the end of `tests/mxmock/mxmock.h` that says why no local test
  can exist.

## How a probe is run

1. Commit the probe on a branch `probe/<topic>` and push it. The workflow
   `.github/workflows/matlab-probe.yml` builds the MEX file on every supported MATLAB
   release and runs `addpath('matlab', 'matlab/probes'); run_probes`.
2. Read the result: `gh run view <id> --log | grep PROBE`.
3. Delete the branch.

`run_probes` runs every `p_*.m` in this directory inside `try`/`catch` and prints one line
per probe:

```
PROBE <name> OK
PROBE <name> ERROR <identifier>: <message>
```

`PROBE <name> OK` says only that the probe ran to its end; what it found is in its own
`PROBE` lines.
