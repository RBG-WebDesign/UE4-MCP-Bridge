"""Python-side GC hygiene around map loads.

UE4 hard-asserts ("World Memory Leaks: N leaks objects and packages",
EditorServer.cpp) if the outgoing world is still referenced when a new map
loads. Live Python wrapper objects root their UObjects through
FPyReferenceCollector, and a dead exec namespace can linger in cyclic
garbage until CPython's generational collector happens to run. The crash
signature in the log is a LogReferenceChain entry ending in
FPyReferenceCollector -> Package <old level>.

Call purge_before_map_load() immediately before any
EditorLevelLibrary.load_level / load_map so stale wrappers unroot
deterministically.
"""

import gc


def purge_before_map_load() -> int:
    """Run full cyclic collections so stale unreal wrappers unroot.

    Returns:
        Total number of objects collected.
    """
    collected = 0
    # Two passes: the first can free finalizer-bearing cycles that only
    # become collectable on the second.
    for _ in range(2):
        collected += gc.collect()
    return collected
