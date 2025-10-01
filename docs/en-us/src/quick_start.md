# Quick Start

This chapter shows how to build and structure an asynchronous program with the ASCO framework.

## Choose How to Start the Async Program

### Async Main Function

In a classic `future/promise` style model, async functions are contagious: you need an async "main" function as the program entry point. Link against the target `asco-main` (see [CMake Targets](#cmake-targets)) to pull in the support code for an async main.

After including `<asco/future.h>`, define in the global namespace an async function `future<int> async_main()` that takes no parameters and `co_return`s an exit status. The program starts there.

For obtaining command‑line arguments and environment variables see the [future\<T\>](./future.md#async-main-function) section.

`asco-main` internally calls `async_main` from a normal `main` and blocks until the coroutine finishes.

```cpp
#include <asco/future.h>
using asco::future;

future<int> async_main() {
    co_return 0;
}
```

### Synchronous Main Function

Real-world constraints may prevent introducing an async main, e.g.:

- Architecture already fixed before adopting async runtime.
- Only a small subset needs async features—rewriting the entire program is unnecessary.
- `main` actually comes from a third-party library and can't be changed.

ASCO therefore also supports keeping a synchronous `main` and calling async functions from that context. You cannot directly get the return value; use [blocking wait](./future.md#async-main-function) or [task composition](./task_composition.md).

Link against `asco-base` to use this style.

## CMake Targets

- `asco`: ASCO runtime environment, static library.
- `asco-main`: link this for the async-main style startup (static).
- `asco-base`: link this for the sync-main style startup (static).

The last two must not be linked together.

- `asco-shared`: shared version of `asco`.
- `asco-main-shared`: depends on `asco-shared`, provides async main support.
- `asco-base-shared`: depends on `asco-shared`, provides sync main support.

Use the `-shared` suffixed targets if you need shared libraries.

More build system integrations may be added later.
