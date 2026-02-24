// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/util/erased.h>
#include <asco/util/type_id.h>

namespace asco::util {

class safe_erased final {
public:
    template<typename T>
    using ref = erased::ref<T>;

    safe_erased() = default;

    static safe_erased of_void() noexcept { return {type_id::of<void>()}; }

    template<typename T>
    safe_erased(T &&value) noexcept
            : m_erased{std::forward<T>(value)}
            , m_tid{type_id::of<std::remove_cvref_t<T>>()} {}

    template<typename T>
    safe_erased(ref<T> &&value) noexcept
            : m_erased(erased::ref<T>{value.v})
            , m_tid{type_id::of<std::remove_cvref_t<T>>()} {}

    safe_erased(const safe_erased &) = delete;
    safe_erased &operator=(const safe_erased &) = delete;

    safe_erased(safe_erased &&) = default;
    safe_erased &operator=(safe_erased &&rhs) {
        if (this != &rhs) {
            this->~safe_erased();
            new (this) safe_erased(std::move(rhs));
        }
        return *this;
    }

    template<typename T>
    T &get() noexcept {
        asco_assert_lint(
            m_tid == type_id::of<std::remove_cvref_t<T>>(), "访问了错误类型的 safe_erased: 期望 {}, 实际 {}",
            type_id::of<std::remove_cvref_t<T>>().name(), m_tid.name());
        return m_erased.get<T>();
    }

    template<typename T>
    const T &get() const noexcept {
        asco_assert_lint(
            m_tid == type_id::of<std::remove_cvref_t<T>>(), "访问了错误类型的 safe_erased: 期望 {}, 实际 {}",
            type_id::of<std::remove_cvref_t<T>>().name(), m_tid.name());
        return m_erased.get<T>();
    }

private:
    safe_erased(type_id tid) noexcept
            : m_tid{tid} {}

    erased m_erased;
    type_id m_tid;
};

};  // namespace asco::util
