# Spin Lock

Header: `<asco/sync/spin.h>`

Acquisition & release use an RAII guard: call `.lock()`; the guard releases on scope exit. Dereference with `*` / `->` to access protected object.
