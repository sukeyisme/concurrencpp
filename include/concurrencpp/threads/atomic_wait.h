#ifndef CONCURRENCPP_ATOMIC_WAIT_H
#define CONCURRENCPP_ATOMIC_WAIT_H

#include <atomic>
#include <chrono>

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int old) noexcept;
    void atomic_wait_for_native(void* atom, int old, std::chrono::milliseconds ms) noexcept;
    void atomic_notify_one_native(void* atom) noexcept;
    void atomic_notify_all_native(void* atom) noexcept;

    enum class atomic_wait_status { ok, timeout };

    template<class type>
    void atomic_wait(std::atomic<type>& atom, type old, std::memory_order order) noexcept {
        static_assert(std::is_standard_layout_v<std::atomic<type>>, "std::atom<type> is not standard-layout");
 //       static_assert(std::is_convertible_v<type, int>, "type is not convertible to int");

        while (true) {
            const auto val = atom.load(order);
            if (val != old) {
                return;
            }

            atomic_wait_native(&atom, static_cast<int>(old));
        }
    }

    template<class type>
    atomic_wait_status atomic_wait_for(std::atomic<type>& atom,
                                       type old,
                                       std::chrono::milliseconds ms,
                                       std::memory_order order) noexcept {
        static_assert(std::is_standard_layout_v<std::atomic<type>>, "std::atom<type> is not standard-layout");
//        static_assert(std::is_convertible_v<type, int>, "type is not convertible to int");

        const auto deadline = std::chrono::system_clock::now() + ms;

        while (true) {
            const auto val = atom.load(order);
            if (val != old) {
                return atomic_wait_status::ok;
            }

            const auto now = std::chrono::system_clock::now();
            if (now >= deadline) {
                return atomic_wait_status::timeout;
            }

            const auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            atomic_wait_for_native(&atom, static_cast<int>(old), time_diff);
        }
    }

    template<class type>
    void atomic_notify_one(std::atomic<type>& atom) noexcept {
        atomic_notify_one_native(&atom);
    }

    template<class type>
    void atomic_notify_all(std::atomic<type>& atom) noexcept {
        atomic_notify_all_native(&atom);
    }
}  // namespace concurrencpp::details

#endif