/* Per-tick trace of car 0 for comparing a replay between builds
 * (docs/ATTRACT_REPLAY_DEBUG_PLAN.md, step 1). Domain A: it reads the game's
 * Car and ReplayStream records with the game's layout.
 *
 * On with the ini option [debug] replay_trace = <file>. Every simulation tick
 * appends one record of plain integers to a preallocated buffer; the buffer is
 * written as CSV once, when the replay of car 0 stops, when it is full, or at
 * ReplayTrace_Stop. No file I/O per tick. tools/replay_trace_diff.py compares
 * two traces. */
#ifndef OPENCMR2_DIAGNOSTICS_REPLAY_TRACE_H
#define OPENCMR2_DIAGNOSTICS_REPLAY_TRACE_H

namespace Diagnostics {
/* Installs the trace as the observer when the option is set and no observer
 * is installed yet. */
void ReplayTrace_StartFromOptions();
/* Writes what was recorded and uninstalls the trace. */
void ReplayTrace_Stop();
}

#endif
