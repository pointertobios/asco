#define FUTURE_IMPL
#include <asco/future.h>

#include <asco/core/runtime.h>

#include <string>
#include <string_view>

namespace asco::base {

template struct future_base<_future_void, false, false>;
template struct future_base<_future_void, true, false>;
template struct future_base<_future_void, false, true>;

template struct future_base<int, false, false>;
template struct future_base<int, true, false>;
template struct future_base<int, false, true>;

template struct future_base<std::string, false, false>;
template struct future_base<std::string, true, false>;
template struct future_base<std::string, false, true>;

template struct future_base<std::string_view, false, false>;
template struct future_base<std::string_view, true, false>;
template struct future_base<std::string_view, false, true>;

};  // namespace asco::base
