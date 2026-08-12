# Terminal capability spike (plan_platform.md PLAT-129)

Proves, before any `TerminalPlatform` code exists, that a process can determine at runtime what
the terminal it is attached to can actually do.

```bash
g++ -std=c++23 -O2 -Wall -Wextra -o terminal_probe terminal_probe.cpp
./terminal_probe
```

## Why a spike rather than going straight to the implementation

Three Phase 10 tasks branch on answers this program produces, and getting any of them wrong is
invisible until a user is affected:

- **PLAT-134** (colour ladder) picks truecolor / 256 / 16 / monochrome.
- **PLAT-137 vs PLAT-138** (keyboard) — exact key-release events, or synthesised ones. This is the
  single biggest fidelity question in the phase.
- **PLAT-140** (capability profile) must report `exactKeyboardState` honestly, and must refuse
  cleanly when there is no terminal at all.

## Findings

### 1. Absence of a reply is the only "no" a terminal gives

There is no way to ask a terminal whether it supports a feature and receive a negative answer. An
unsupported query produces **silence**. So detection is a write followed by a timed read, and the
timeout is a correctness parameter rather than a tuning knob: too short and a capable terminal
over a slow ssh link is misread as incapable. The probe uses 250 ms for the first byte, then 40 ms
between subsequent chunks since replies arrive split across reads.

### 2. DA1 is the control, and it is what makes the other answers trustworthy

Primary Device Attributes (`ESC [ c`) is answered by every real terminal. Without it, silence from
the Kitty query is ambiguous — it could mean "not supported" or "nothing is listening". With DA1
answered and the Kitty query silent, the negative is real. Verified both ways under a pty:

| Scenario | DA1 | Kitty `ESC [ ? u` | Verdict |
|---|---|---|---|
| Terminal answers both | `\e[?62;c` | `\e[?1u` | PLAT-137 exact; `exactKeyboardState = true` |
| Terminal answers DA1 only | `\e[?62;c` | *(no reply)* | PLAT-138 synthetic; `exactKeyboardState = false` |

### 3. Not being a terminal is a normal outcome, not an error

Run with stdout redirected — a pipe, a file, a CI log — the probe reports `stdout is a tty : no`
and stops. This is the case PLAT-140 must handle: a terminal platform that emits escape sequences
into a build log because it did not check is worse than one that refuses to start.

The environment is checked separately from the terminal itself, because **stdout and stdin can
differ**: output redirected with input still on the tty means queries are impossible and detection
must fall back to environment variables alone. The probe reports that case distinctly.

### 4. Environment variables are a guess, not an answer

`TERM` is routinely wrong — inherited across ssh, rewritten by multiplexers, or set conservatively.
It is used only as a fallback and the code says so. `COLORTERM=truecolor` is the one reasonably
reliable environment signal, and it is what the colour ladder should key off before falling back
to parsing `TERM`.

### 5. SGR mouse has no query

`?1006` cannot be probed. It is enabled optimistically and assumed, which is what PLAT-139 records.
It has been near-universal for years, and a terminal that ignores the enable simply sends no mouse
input — a degradation, not a corruption.

### 6. Queries are written to stdin, not stdout

Both file descriptors refer to the same tty when a terminal is attached, so either works for
*sending*. Writing to stdin is the more robust choice: the reply arrives on stdin regardless, and
if stdout were ever redirected the query would otherwise be written into a file while the code
waited forever for an answer that was never sent.

## Restoration

The probe enters raw mode through an RAII guard whose destructor runs on every exit path. A spike
that left a developer's shell without echo would be demonstrating the exact hostile failure
PLAT-131 exists to prevent.

## Verification

Driven under a real pty by a harness that plays the terminal, answering DA1 and XTVERSION always
and the Kitty query only in the first scenario. Both verdicts came out correct. The non-tty path
was verified by running it normally in a captured-output environment.
