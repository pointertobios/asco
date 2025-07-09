#define FUTURE_IMPL
#include <asco/future.h>

#include <asco/core/runtime.h>

#include <string>
#include <string_view>

namespace asco::base {

using core::runtime;

template struct future_base<_future_void, false, false, runtime>;
template struct future_base<_future_void, true, false, runtime>;
template struct future_base<_future_void, false, true, runtime>;

template struct future_base<int, false, false, runtime>;
template struct future_base<int, true, false, runtime>;
template struct future_base<int, false, true, runtime>;

template struct future_base<std::string, false, false, runtime>;
template struct future_base<std::string, true, false, runtime>;
template struct future_base<std::string, false, true, runtime>;

template struct future_base<std::string_view, false, false, runtime>;
template struct future_base<std::string_view, true, false, runtime>;
template struct future_base<std::string_view, false, true, runtime>;

};  // namespace asco::base
