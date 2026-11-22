# Scheduler shutdown fix — engineering record

This records a resolved alpha bug, not instructions to apply an older patch.
Windows Release originally compiled successfully but timed out in runtime and `cli.suites`.
The last buffered actor diagnostic did not identify the blocking operation; test progress now flushes output.

`Scheduler::stop` previously changed `_running` without `_queue_mu`. A worker could inspect the
condition-variable predicate, miss notification before waiting and sleep indefinitely, blocking `join`.
The predicate now changes under the same mutex used by `wait`; notification and joining occur after
unlocking. `scheduler.cpp` also includes `unordered_set` directly instead of relying on a transitive include.

The regression `scheduler_stop_waiting_workers` exercises 128 start/stop cycles with two workers and
idempotent stop. Watchdog limits were not increased and tests were not skipped. The runtime suite
now has 31 cases. Earlier Linux stress checks reproduced the old hang and completed 5000 cycles
with the correction; subsequent Windows and baseline CI validation passed.

To verify current code, follow [VALIDATION.md](VALIDATION.md). To verify an extracted release candidate,
follow [PORTABLE.md](PORTABLE.md). Passing a previous build does not validate a newly built archive.
