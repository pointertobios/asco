# Lock-Free Data Structures & Parallel Algorithms

This module provides lock-free data structures and parallel algorithm building blocks for high-throughput, low-latency scenarios. They can be used inside the asco coroutine runtime or in a plain threaded environment.

Design focus:

- Avoid blocking and syscalls as much as possible to reduce context switch overhead.
- Use atomic operations with proper memory ordering (acquire / release / acq_rel) for cross-core visibility and ordering.
- Pay attention to cache friendliness and false-sharing isolation (cache line alignment to reduce jitter).
- Provide predictable semantics for exceptions and resource management, avoiding leaks and ABA issues.

## Current Components

- Lock-Free Continuous Queue `continuous_queue`: An SPSC-ideal message queue that still allows multiple sender/receiver handle copies to be used concurrently.
