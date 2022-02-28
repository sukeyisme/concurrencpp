#include "concurrencpp/platform_defs.h"
#include "concurrencpp/threads/atomic_wait.h"

#include <cassert>

namespace concurrencpp::details {
}  // namespace concurrencpp::details

#if defined(CRCPP_WIN_OS)

#    include <Windows.h>

#    pragma comment(lib, "Synchronization.lib")

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int old) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(atom), INFINITE);
    }

    void atomic_wait_for_native(void* atom, int old, std::chrono::milliseconds ms) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(atom), static_cast<DWORD>(ms.count()));
    }

    void atomic_notify_one_native(void* atom) noexcept {
        ::WakeByAddressSingle(atom);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        ::WakeByAddressAll(atom);
    }
}  // namespace concurrencpp::details

#elif defined(CRCPP_UNIX_OS) || defined(CRCPP_FREE_BSD_OS)

#    include <ctime>

#    include <unistd.h>
#    include <linux/futex.h>
#    include <sys/syscall.h>

namespace concurrencpp::details {
    int sys_futex(void* addr, std::int32_t op, int x, const timespec* ts) noexcept {
        return ::syscall(SYS_futex, addr, op, x, ts, nullptr, 0);
    }

    timespec ms_to_time_spec(size_t ms) noexcept {
        timespec req;
        req.tv_sec = (time_t)(ms / 1000);
        req.tv_nsec = (ms % 1000) * 1'000'000;
        return req;
    }

    void atomic_wait_native(void* atom, int old) noexcept {
        sys_futex(atom, FUTEX_WAIT_PRIVATE, old, nullptr);
    }

    void atomic_wait_for_native(void* atom, int old, std::chrono::milliseconds ms) noexcept {
        auto spec = ms_to_time_spec(ms.count());
        sys_futex(atom, FUTEX_WAIT_PRIVATE, old, &spec);
    }

    void atomic_notify_one_native(void* atom) noexcept {
        sys_futex(atom, FUTEX_WAKE_PRIVATE, 1, nullptr);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        sys_futex(atom, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr);
    }
}  // namespace concurrencpp::details

#elif defined(CRCPP_MAC_OS)

extern "C" {
    int __ulock_wait(uint32_t operation, void* addr, uint64_t value, uint32_t timeout);
    int __ulock_wake(uint32_t operation, void* addr, uint64_t wake_value);
}  // extern "C"

enum ulock_ops {
    ulock_op_compare_and_wait = 1,
    ulock_op_compare_and_wait_shared = 3,
    ulock_op_compare_and_wait64 = 5,
    ulock_op_compare_and_wait64_shared = 6,
    ulock_flag_wake_all = 0x00000100,
    ulock_flag_no_errno = 0x01000000
};

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int old) noexcept {
        __ulock_wait(ulock_op_compare_and_wait | ulock_flag_no_errno, atom, old, 0);
    }

    void atomic_wait_for_native(void* atom, int old, std::chrono::milliseconds ms) noexcept {
        __ulock_wait(ulock_op_compare_and_wait | ulock_flag_no_errno, atom, old, ms.count() * 1'000'000);
    }

    void atomic_notify_one_native(void* atom) noexcept {
        __ulock_wake(ulock_op_compare_and_wait | ulock_flag_no_errno, atom, 0);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        __ulock_wake(ulock_op_compare_and_wait | ulock_flag_wake_all | ulock_flag_no_errno, atom, 0);
    }
}  // namespace concurrencpp::details

#endif