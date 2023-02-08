#pragma once
#include <experimental/coroutine>

template <class T>
struct CoGenerator {
    struct promise_type {
        T data;

        auto get_return_object() -> CoGenerator {
            return CoGenerator(*this);
        }

        auto initial_suspend() -> std::experimental::suspend_always {
            return {};
        }

        auto final_suspend() noexcept -> std::experimental::suspend_always {
            return {};
        }

        auto yield_value(T data) -> std::experimental::suspend_always {
            this->data = std::move(data);
            return {};
        }

        // R == void
        // auto return_void() -> void {
        // }

        auto return_value(T data) -> void {
            this->data = std::move(data);
        }

        auto unhandled_exception() -> void {
            std::terminate();
        }
    };

    std::experimental::coroutine_handle<promise_type> handle;

    auto operator=(CoGenerator&) -> CoGenerator& = delete;

    auto operator=(CoGenerator&& other) -> CoGenerator& {
        handle = std::exchange(other.handle, nullptr);
        return *this;
    }

    CoGenerator() = default;

    CoGenerator(CoGenerator&) = delete;

    CoGenerator(CoGenerator&& o) : handle(std::exchange(o.handle, nullptr)) {
    }

    CoGenerator(promise_type& promise) : handle(std::experimental::coroutine_handle<promise_type>::from_promise(promise)) {}

    ~CoGenerator() {
        if(handle != nullptr) {
            handle.destroy();
        }
    }
};

template <class ResumeType, class YieldType>
struct CoRoutine {
    using Generator = CoGenerator<YieldType>;

    Generator  generator;
    ResumeType resume_arg;

    auto start(auto routine) -> void {
        generator = routine();
    }

    auto resume(ResumeType resume) -> YieldType {
        this->resume_arg = std::move(resume);
        generator.handle.resume();
        return generator.handle.promise().data;
    }

    auto done() const -> bool {
        return generator.handle.done();
    }
};
