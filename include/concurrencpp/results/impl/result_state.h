#ifndef CONCURRENCPP_RESULT_STATE_H
#define CONCURRENCPP_RESULT_STATE_H

#include "concurrencpp/threads/atomic_wait.h"
#include "concurrencpp/results/impl/consumer_context.h"
#include "concurrencpp/results/impl/producer_context.h"

#include <atomic>
#include <type_traits>

#include <cassert>

namespace concurrencpp::details {
    class result_state_base {

       public:
        enum class pc_state : int { idle, consumer_set, consumer_waiting, consumer_done, producer_done };

       protected:
        std::atomic<pc_state> m_pc_state {pc_state::idle};
        consumer_context m_consumer;
        coroutine_handle<void> m_done_handle;

        void assert_done() const noexcept;

       public:
        void wait();
        pc_state wait_for(std::chrono::milliseconds ms);
        bool await(coroutine_handle<void> caller_handle) noexcept;
        pc_state when_any(const std::shared_ptr<when_any_context>& when_any_state) noexcept;

        void try_rewind_consumer() noexcept;
    };

    template<class type>
    class result_state : public result_state_base {

       private:
        producer_context<type> m_producer;

        static void delete_self(result_state<type>* state) noexcept {
            auto done_handle = state->m_done_handle;
            if (static_cast<bool>(done_handle)) {
                assert(done_handle.done());
                return done_handle.destroy();
            }

            delete state;
        }

        template<class callable_type>
        void from_callable(std::true_type /*is_void_type*/, callable_type&& callable) {
            callable();
            set_result();
        }

        template<class callable_type>
        void from_callable(std::false_type /*is_void_type*/, callable_type&& callable) {
            set_result(callable());
        }

       public:
        template<class... argument_types>
        void set_result(argument_types&&... arguments) noexcept(noexcept(type(std::forward<argument_types>(arguments)...))) {
            m_producer.build_result(std::forward<argument_types>(arguments)...);
        }

        void set_exception(const std::exception_ptr& error) noexcept {
            assert(error != nullptr);
            m_producer.build_exception(error);
        }

        // Consumer-side functions
        result_status status() const noexcept {
            const auto state = m_pc_state.load(std::memory_order_acquire);
            assert(state != pc_state::consumer_set);

            if (state == pc_state::idle) {
                return result_status::idle;
            }

            return m_producer.status();
        }

        template<class duration_unit, class ratio>
        result_status wait_for(std::chrono::duration<duration_unit, ratio> duration) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            const auto res = result_state_base::wait_for(ms);
            if (res == pc_state::producer_done) {
                return m_producer.status();
            }

            return result_status::idle;
        }

        template<class clock, class duration>
        result_status wait_until(const std::chrono::time_point<clock, duration>& timeout_time) {
            const auto now = clock::now();
            if (timeout_time <= now) {
                return status();
            }

            const auto diff = timeout_time - now;
            return wait_for(diff);
        }

        type get() {
            assert_done();
            return m_producer.get();
        }

        template<class callable_type>
        void from_callable(callable_type&& callable) {
            using is_void = std::is_same<type, void>;

            try {
                from_callable(is_void {}, std::forward<callable_type>(callable));
            } catch (...) {
                set_exception(std::current_exception());
            }
        }

        void complete_producer(coroutine_handle<void> done_handle = {}) {
            m_done_handle = done_handle;

            const auto state_before = this->m_pc_state.exchange(pc_state::producer_done, std::memory_order_acq_rel);
            assert(state_before != pc_state::producer_done);

            switch (state_before) {
                case pc_state::consumer_set: {
                    return m_consumer.resume_consumer(*this);
                }

                case pc_state::idle: {
                    return;
                }

                case pc_state::consumer_waiting: {
                    return atomic_notify_one(m_pc_state);
                }

                case pc_state::consumer_done: {
                    return delete_self(this);
                }

                default: {
                    break;
                }
            }

            assert(false);
        }

        void complete_consumer() noexcept {
            const auto pc_state = this->m_pc_state.load(std::memory_order_acquire);
            if (pc_state == pc_state::producer_done) {
                return delete_self(this);
            }

            const auto pc_state1 = this->m_pc_state.exchange(pc_state::consumer_done, std::memory_order_acq_rel);
            assert(pc_state1 != pc_state::consumer_set);

            if (pc_state1 == pc_state::producer_done) {
                return delete_self(this);
            }

            assert(pc_state1 == pc_state::idle);
        }

        void complete_joined_consumer() noexcept {
            assert_done();
            delete_self(this);
        }
    };

    template<class type>
    struct consumer_result_state_deleter {
        void operator()(result_state<type>* state_ptr) const noexcept {
            assert(state_ptr != nullptr);
            state_ptr->complete_consumer();
        }
    };

    template<class type>
    struct joined_consumer_result_state_deleter {
        void operator()(result_state<type>* state_ptr) const noexcept {
            assert(state_ptr != nullptr);
            state_ptr->complete_joined_consumer();
        }
    };

    template<class type>
    struct producer_result_state_deleter {
        void operator()(result_state<type>* state_ptr) const {
            assert(state_ptr != nullptr);
            state_ptr->complete_producer();
        }
    };

    template<class type>
    using consumer_result_state_ptr = std::unique_ptr<result_state<type>, consumer_result_state_deleter<type>>;

    template<class type>
    using joined_consumer_result_state_ptr = std::unique_ptr<result_state<type>, joined_consumer_result_state_deleter<type>>;

    template<class type>
    using producer_result_state_ptr = std::unique_ptr<result_state<type>, producer_result_state_deleter<type>>;
}  // namespace concurrencpp::details

#endif
