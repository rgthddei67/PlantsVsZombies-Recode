---
name: collaboration-style
description: "How this user wants perf/debugging work done — measure-first discipline, steady-state numbers, honest framing, VS build split, Chinese"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 7d6bf991-0204-4927-adcf-b4a0fd3c7e0b
---

# Collaboration style for this user

Rule: run a rigorous measure→experiment→decide loop. Don't propose or stack fixes on
unverified hypotheses; change one variable at a time; be honest when an experiment
falsifies your own theory (say so plainly, don't soften it — but also don't over-state
it; recalibrate when new context arrives).

**Why:** the user steered the whole PvZ perf investigation this way and pushed back
both when I guessed without evidence AND when I was over-pessimistic about a result.
They value intellectual honesty over performative progress.
**How to apply:**
- Use steady-state / warmed-up measurements for performance comparisons and distinguish cold-load cost. Collect measurements autonomously where tooling permits; ask the user only for evidence that cannot be collected locally.
- Before claiming a change helped/didn't, get the real number; recalibrate framing if cold data misled.
- Don't add changes that don't actually help (e.g. cargo-cult `reserve()`); call it out and skip it.
- Offer the next step as an experiment with a clear hypothesis and a data table comparing before/after.

Build workflow (updated 2026-09-12): the former manual-build restriction is superseded. Follow [AGENTS.md](../../AGENTS.md#构建与验证) and the [build guide](../agent-guide/BUILD_AND_DEBUG.md) for autonomous builds and diagnostics. Do not require the user to compile or collect console output when Codex can do it. The user may explicitly choose to build or test personally for a particular task.

Language: the user writes in Chinese; respond in Chinese.

Tooling pattern that worked well: a temporary header-only profiler with RAII scope
timers + per-phase + counters, printing every N frames. The user engages well with
this and with before/after comparison tables.

Related: [pvz-perf-optimization](project_pvz_perf_optimization.md).
