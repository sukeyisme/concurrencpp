#include "concurrencpp/results/impl/result_state.h"
#include "concurrencpp/results/impl/shared_result_state.h"

using concurrencpp::details::result_state_base;

void result_state_base::assert_done() const noexcept {
    assert(m_pc_state.load(std::memory_order_acquire) == pc_state::producer_done);
}

void result_state_base::wait() {
    const auto state = m_pc_state.load(std::memory_order_acquire);
    if (state == pc_state::producer_done) {
        return;
    }

    auto expected_state = pc_state::idle;
    const auto idle = m_pc_state.compare_exchange_strong(expected_state,
                                                         pc_state::consumer_waiting,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire);

    if (!idle) {
        assert_done();
        return;
    }

    atomic_wait(m_pc_state, pc_state::consumer_waiting, std::memory_order_acquire);
    assert_done();
}

result_state_base::pc_state result_state_base::wait_for(std::chrono::milliseconds ms) {
    const auto state = m_pc_state.load(std::memory_order_acquire);
    if (state == pc_state::producer_done) {
        return pc_state::producer_done;
    }

    auto expected_state = pc_state::idle;
    const auto idle = m_pc_state.compare_exchange_strong(expected_state,
                                                         pc_state::consumer_waiting,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire);

    if (!idle) {
        assert_done();
        return pc_state::producer_done;
    }

    ms += std::chrono::milliseconds(2);
    const auto res = atomic_wait_for(m_pc_state, pc_state::consumer_waiting, ms, std::memory_order_acquire);
    if (res == atomic_wait_status::ok) {
        assert_done();
        return pc_state::producer_done;
    }

    assert(res == atomic_wait_status::timeout);
    // revert m_pc_state to idle
    expected_state = pc_state::consumer_waiting;
    const auto still_idle =
        m_pc_state.compare_exchange_strong(expected_state, pc_state::idle, std::memory_order_acq_rel, std::memory_order_acquire);

    if (still_idle) {
        return pc_state::idle;
    }

    assert_done();
    return pc_state::producer_done;
}

bool result_state_base::await(coroutine_handle<void> caller_handle) noexcept {
    const auto state = m_pc_state.load(std::memory_order_acquire);
    if (state == pc_state::producer_done) {
        return false;  // don't suspend
    }

    m_consumer.set_await_handle(caller_handle);

    auto expected_state = pc_state::idle;
    const auto idle = m_pc_state.compare_exchange_strong(expected_state,
                                                         pc_state::consumer_set,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire);

    if (!idle) {
        assert_done();
    }

    return idle;  // if idle = true, suspend
}

result_state_base::pc_state result_state_base::when_any(const std::shared_ptr<when_any_context>& when_any_state) noexcept {
    const auto state = m_pc_state.load(std::memory_order_acquire);
    if (state == pc_state::producer_done) {
        return state;
    }

    m_consumer.set_when_any_context(when_any_state);

    auto expected_state = pc_state::idle;
    const auto idle = m_pc_state.compare_exchange_strong(expected_state,
                                                         pc_state::consumer_set,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire);

    if (!idle) {
        assert_done();
    }

    return state;
}

void result_state_base::try_rewind_consumer() noexcept {
    const auto pc_state = m_pc_state.load(std::memory_order_acquire);
    if (pc_state != pc_state::consumer_set) {
        return;
    }

    auto expected_consumer_state = pc_state::consumer_set;
    const auto consumer = m_pc_state.compare_exchange_strong(expected_consumer_state,
                                                             pc_state::idle,
                                                             std::memory_order_acq_rel,
                                                             std::memory_order_acquire);

    if (!consumer) {
        assert_done();
        return;
    }

    m_consumer.clear();
}
