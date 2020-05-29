/*
 * Copyright 2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <coroutine>
#include <exception>
#include <utility>

#ifndef __cpp_impl_coroutine
#error Need -fcoroutines
#endif

namespace Co {

/**
 * A coroutine task which is suspended initially and returns a value
 * (with support for exceptions).
 */
template<typename T>
class Task {
public:
	struct promise_type {
		std::coroutine_handle<> continuation;

		T value;

		std::exception_ptr error;

		auto initial_suspend() noexcept {
			return std::suspend_always{};
		}

		struct final_awaitable {
			bool await_ready() const noexcept {
				return false;
			}

			template<typename PROMISE>
			std::coroutine_handle<> await_suspend(std::coroutine_handle<PROMISE> coro) noexcept {
				return coro.promise().continuation;
			}

			void await_resume() noexcept {
			}
		};

		auto final_suspend() noexcept {
			return final_awaitable{};
		}

		void return_value(T &&t) noexcept {
			value = std::move(t);
		}

		Task<T> get_return_object() noexcept {
			return Task<T>(std::coroutine_handle<promise_type>::from_promise(*this));
		}

		void unhandled_exception() noexcept {
			error = std::current_exception();
		}
	};

private:
	std::coroutine_handle<promise_type> coroutine;

public:
	Task() = default;

	explicit Task(std::coroutine_handle<promise_type> _coroutine) noexcept
		:coroutine(_coroutine)
	{
	}

	Task(Task &&src) noexcept
		:coroutine(std::exchange(src.coroutine, nullptr))
	{
	}

	~Task() noexcept {
		if (coroutine)
			coroutine.destroy();
	}

	Task &operator=(Task &&src) noexcept {
		coroutine = std::exchange(src.coroutine, nullptr);
		return *this;
	}

	auto operator co_await() const noexcept {
		struct Awaitable final {
			const std::coroutine_handle<promise_type> coroutine;

			bool await_ready() const noexcept {
				return coroutine.done();
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
				coroutine.promise().continuation = continuation;
				return coroutine;
			}

			decltype(auto) await_resume() {
				auto &p = coroutine.promise();

				if (p.error)
					std::rethrow_exception(std::move(p.error));

				return std::move(p.value);
			}
		};

		return Awaitable{coroutine};
	}
};

} // namespace Co
