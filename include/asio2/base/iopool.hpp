/*
 * COPYRIGHT (C) 2017-2021, zhllxt
 *
 * author   : zhllxt
 * email    : 37792738@qq.com
 * 
 * Distributed under the GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 * (See accompanying file LICENSE or see <http://www.gnu.org/licenses/>)
 */

#ifndef __ASIO2_IOPOOL_HPP__
#define __ASIO2_IOPOOL_HPP__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <type_traits>
#include <memory>
#include <algorithm>
#include <atomic>
#include <unordered_set>

#include <asio2/3rd/asio.hpp>
#include <asio2/base/error.hpp>
#include <asio2/base/detail/util.hpp>

namespace asio2::detail
{
	// unbelievable :
	// the 1 sfinae need use   std::declval<std::decay_t<T>>()
	// the 2 sfinae need use  (std::declval<std::decay_t<T>>())
	// the 3 sfinae need use ((std::declval<std::decay_t<T>>()))

	//-----------------------------------------------------------------------------------

	template<class T, class R = void>
	struct is_io_context_pointer : std::false_type {};

	template<class T>
	struct is_io_context_pointer<T, std::void_t<decltype(
		std::declval<std::decay_t<T>>()->~io_context()), void>> : std::true_type {};

	template<class T, class R = void>
	struct is_io_context_object : std::false_type {};

	template<class T>
	struct is_io_context_object<T, std::void_t<decltype(
		std::declval<std::decay_t<T>>().~io_context()), void>> : std::true_type {};

	//-----------------------------------------------------------------------------------

	template<class T, class R = void>
	struct is_executor_work_guard_pointer : std::false_type {};

	template<class T>
	struct is_executor_work_guard_pointer<T, std::void_t<decltype(
		(std::declval<std::decay_t<T>>())->~executor_work_guard()), void>> : std::true_type {};

	template<class T, class R = void>
	struct is_executor_work_guard_object : std::false_type {};

	template<class T>
	struct is_executor_work_guard_object<T, std::void_t<decltype(
		(std::declval<std::decay_t<T>>()).~executor_work_guard()), void>> : std::true_type {};

	//-----------------------------------------------------------------------------------

	static_assert(is_io_context_pointer<asio::io_context*  >::value);
	static_assert(is_io_context_pointer<asio::io_context*& >::value);
	static_assert(is_io_context_pointer<asio::io_context*&&>::value);
	static_assert(is_io_context_pointer<std::shared_ptr<asio::io_context>  >::value);
	static_assert(is_io_context_pointer<std::shared_ptr<asio::io_context>& >::value);
	static_assert(is_io_context_pointer<std::shared_ptr<asio::io_context>&&>::value);
	static_assert(is_io_context_pointer<std::shared_ptr<asio::io_context>const&>::value);
	static_assert(is_io_context_object<asio::io_context  >::value);
	static_assert(is_io_context_object<asio::io_context& >::value);
	static_assert(is_io_context_object<asio::io_context&&>::value);

	//-----------------------------------------------------------------------------------

	class io_t
	{
	public:
		io_t(asio::io_context* ioc, std::atomic<std::size_t>& pending)
			: context_(ioc)
			, strand_ (*context_)
			, pending_(pending)
		{
		}
		~io_t()
		{
		}

		inline asio::io_context                        & context() { return (*(this->context_)); }
		inline asio::io_context::strand                & strand () { return    this->strand_   ; }
		inline std::atomic<std::size_t>                & pending() { return    this->pending_  ; }
		inline std::unordered_set<asio::steady_timer*> & timers () { return    this->timers_   ; }

		/**
		 * @function : save the timers that maybe have not been closed properly.
		 * The user should't call this function.
		 */
		inline io_t& save_timer(asio::steady_timer* timer)
		{
			if (timer)
			{
				asio::dispatch(this->strand_, [this, timer]() mutable
				{
					timers_.emplace(timer);
				});
			}
			return (*this);
		}

		/**
		 * @function : cancel the timer that maybe have not been closed properly.
		 * The user should't call this function.
		 */
		inline io_t& exit_timer(asio::steady_timer* timer)
		{
			if (timer)
			{
				asio::dispatch(this->strand_, [this, timer]() mutable
				{
					timers_.erase(timer);
				});
			}
			return (*this);
		}

	protected:
		asio::io_context                       * context_ = nullptr;

		asio::io_context::strand                 strand_;

		// Use this variable to ensure async_send function was executed correctly.
		// see : send_cp.hpp "# issue x:"
		std::atomic<std::size_t>               & pending_;

		// Use this variable to save the timers that have not been closed properly.
		// If we don't do this, the following problem will occurs:
		// user call client.stop, when the code is run to before the iopool's 
		// wait_for_io_context_stopped, and user call client.start_timer at another
		// thread, this will cause the wait_for_io_context_stopped will block forever 
		// until the timer expires.
		// e.g:
		//     {
		//         asio2::timer timer;
		//         timer.post([&]()
		//         {
		//             timer.start_timer(1, std::chrono::seconds(1), []() {});
		//         });
		//     } // the timer's destructor will be called here.
		// when the timer's destructor is called, it will call the "stop_all_timers"
		// function, the "stop_all_timers" will "post a event", this "post a event"
		// will executed before the "timer.start_timer(1,...)", so when the 
		// "timer.start_timer(1,...)" is executed, nobody has a chance to cancel it,
		// and this will cause the iopool's wait_for_io_context_stopped function
		// blocked forever.
		std::unordered_set<asio::steady_timer*>  timers_;
	};

	//-----------------------------------------------------------------------------------

	template<class T, class R = void>
	struct is_io_t_pointer : std::false_type {};

	template<class T>
	struct is_io_t_pointer<T, std::void_t<decltype(
		((std::declval<std::decay_t<T>>()))->~io_t()), void>> : std::true_type {};

	template<class T, class R = void>
	struct is_io_t_object : std::false_type {};

	template<class T>
	struct is_io_t_object<T, std::void_t<decltype(
		((std::declval<std::decay_t<T>>())).~io_t()), void>> : std::true_type {};

	//-----------------------------------------------------------------------------------

	/**
	 * io_context pool
	 */
	class iopool
	{
	public:
		/**
		 * @constructor
		 * @param    : concurrency - the pool size, default is double the number of CPU cores
		 */
		explicit iopool(std::size_t concurrency = default_concurrency())
		{
			if (concurrency == 0)
			{
				concurrency = default_concurrency();
			}

			for (std::size_t i = 0; i < concurrency; ++i)
			{
				this->iocs_.emplace_back(std::make_unique<asio::io_context>(1));
			}

			for (std::size_t i = 0; i < concurrency; ++i)
			{
				this->iots_.emplace_back(std::make_unique<io_t>(this->iocs_[i].get(), this->pending_));
			}

			this->threads_.reserve(this->iots_.size());
			this->guards_.reserve(this->iots_.size());
		}

		/**
		 * @destructor
		 */
		~iopool()
		{
			this->stop();
		}

		/**
		 * @function : run all io_context objects in the pool.
		 */
		bool start()
		{
			clear_last_error();

			std::lock_guard<std::mutex> guard(this->mutex_);

			if (!this->stopped_)
			{
				set_last_error(asio::error::already_started);
				return true;
			}

			if (!this->guards_.empty() || !this->threads_.empty())
			{
				set_last_error(asio::error::already_started);
				return true;
			}

			// Create a pool of threads to run all of the io_contexts. 
			for (auto & ioc : this->iocs_)
			{
				/// Restart the io_context in preparation for a subsequent run() invocation.
				/**
				 * This function must be called prior to any second or later set of
				 * invocations of the run(), run_one(), poll() or poll_one() functions when a
				 * previous invocation of these functions returned due to the io_context
				 * being stopped or running out of work. After a call to restart(), the
				 * io_context object's stopped() function will return @c false.
				 *
				 * This function must not be called while there are any unfinished calls to
				 * the run(), run_one(), poll() or poll_one() functions.
				 */
				ioc->restart();

				this->guards_.emplace_back(ioc->get_executor());

				// start work thread
				this->threads_.emplace_back([&ioc]() mutable
				{
					ioc->run();
				});
			}

			this->stopped_ = false;

			return true;
		}

		/**
		 * @function : stop all io_context objects in the pool
		 * blocking until all posted event has completed already.
		 * After we call iog.reset(), when an asio::post(strand,...) execution ends, the count
		 * of the strand will be checked. If the count equals 0, the strand will be closed. Then 
		 * the subsequent call of asio:: post(strand,...) will fail, and the post event will not
		 * be executed. When our program exits, it will nest call asio:: post (strand...) to post
		 * many events, so when an asio::post(strand,...) inside someone asio::post(strand,...)
		 * has not yet been executed, the strand may have been closed, which will result in the
		 * nested asio::post(strand,...) never being executed.
		 */
		void stop()
		{
			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				if (this->stopped_)
					return;

				if (this->guards_.empty() && this->threads_.empty())
					return;

				if (this->running_in_threads())
					return;

				this->stopped_ = true;
			}

			// Waiting for all nested events to complete.
			// The mutex_ must be released while waiting, otherwise, the stop function may be called
			// in the communication thread and the lock will be requested, which is already held here,
			// so leading to deadlock.
			this->wait_for_io_context_stopped();

			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				// call executor_work_guard reset,and then the io_context working thread will be exited.
				// In fact, the guards has called reset already, but there is no problem with repeated calls
				for (auto & iog : this->guards_)
				{
					ASIO2_ASSERT(iog.owns_work() == false);
					iog.reset();
				}

				// Wait for all threads to exit. 
				for (auto & thread : this->threads_)
				{
					thread.join();
				}

				this->guards_.clear();
				this->threads_.clear();
			}
		}

		/**
		 * @function : check whether the io_context pool is stopped
		 */
		inline bool stopped() const
		{
			return (this->stopped_);
		}

		/**
		 * @function : get an io_t to use
		 */
		inline io_t& get(std::size_t index = static_cast<std::size_t>(-1))
		{
			ASIO2_ASSERT(!this->iots_.empty());

			return *(this->iots_[this->next(index)]);
		}

		/**
		 * @function : get an io_context to use
		 */
		inline asio::io_context& get_context(std::size_t index = static_cast<std::size_t>(-1))
		{
			ASIO2_ASSERT(!this->iots_.empty());

			return this->iots_[this->next(index)]->context();
		}

		/**
		 * @function : get an io_context::strand to use
		 */
		inline asio::io_context::strand& get_strand(std::size_t index = static_cast<std::size_t>(-1))
		{
			ASIO2_ASSERT(!this->iots_.empty());

			return this->iots_[this->next(index)]->strand();
		}

		/**
		 * @function : Determine whether current code is running in the io_context pool threads.
		 */
		inline bool running_in_threads() const
		{
			std::thread::id curr_tid = std::this_thread::get_id();
			for (auto & thread : this->threads_)
			{
				if (curr_tid == thread.get_id())
					return true;
			}
			return false;
		}

		/**
		 * @function : Determine whether current code is running in the io_context thread by index
		 */
		inline bool running_in_thread(std::size_t index) const
		{
			ASIO2_ASSERT(index < this->threads_.size());

			if (!(index < this->threads_.size()))
				return false;

			return (std::this_thread::get_id() == this->threads_[index].get_id());
		}

		/**
		 * @function : get io_context pool size.
		 */
		inline std::size_t size() const
		{
			return this->iots_.size();
		}

		/**
		 * @function : 
		 */
		inline std::atomic<std::size_t>& pending()
		{
			return this->pending_;
		}

		/**
		 * Use to ensure that all nested asio::post(...) events are fully invoked.
		 */
		inline void wait_for_io_context_stopped()
		{
			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				if (this->running_in_threads())
					return;

				// wiat fo all pending events completed.
				while (this->pending_ > std::size_t(0))
					std::this_thread::sleep_for(std::chrono::milliseconds(0));

				// first reset the acceptor io_context work guard
				if (!this->guards_.empty())
					this->guards_.front().reset();
			}

			constexpr auto max = std::chrono::milliseconds(10);
			constexpr auto min = std::chrono::milliseconds(1);

			// second wait indefinitely until the acceptor io_context is stopped
			if (!this->iocs_.empty())
			{
				auto t1 = std::chrono::steady_clock::now();
				auto& ioc = this->iocs_.front();
				auto& iot = this->iots_.front();
				while (!ioc->stopped())
				{
					// the timer may not be canceled successed when using visual
					// studio break point for debugging, so cancel it at each loop
					this->cancel_timers(&(*iot));

					auto t2 = std::chrono::steady_clock::now();
					auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
					std::this_thread::sleep_for(std::clamp(ms, min, max));
				}
				ASIO2_ASSERT(iot->timers().empty());
			}

			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				for (std::size_t i = 1; i < this->guards_.size(); ++i)
				{
					this->guards_[i].reset();
				}
			}

			for (std::size_t i = 1; i < this->iocs_.size(); ++i)
			{
				auto t1 = std::chrono::steady_clock::now();
				auto& ioc = this->iocs_[i];
				auto& iot = this->iots_[i];
				while (!ioc->stopped())
				{
					this->cancel_timers(&(*iot));

					auto t2 = std::chrono::steady_clock::now();
					auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
					std::this_thread::sleep_for(std::clamp(ms, min, max));
				}
				ASIO2_ASSERT(iot->timers().empty());
			}
		}

		/**
		 * @function :
		 */
		inline std::size_t next(std::size_t index)
		{
			// Use a round-robin scheme to choose the next io_context to use. 
			return (index < this->size() ? index : ((++(this->next_)) % this->size()));
		}

		/**
		 * @function :
		 */
		inline void cancel_timers(io_t* io)
		{
			// moust read write the io::timers_ in it's io_context thread by "post"
			// when code run to here, the io_context maybe stopped already.
			asio::post(io->strand(), [io]() mutable
			{
				error_code ec_ignore{};
				for (asio::steady_timer* timer : io->timers())
				{
					// when the timer is canceled, it will erase itself from io::timers_.
					timer->cancel(ec_ignore);
				}
			});
		}

	protected:
		/// threads to run all of the io_context
		std::vector<std::thread>                                     threads_;

		/// The pool of io_context. 
		std::vector<std::unique_ptr<asio::io_context>>               iocs_;

		/// The pool of io_context. 
		std::vector<std::unique_ptr<io_t>>                           iots_;

		/// 
		std::mutex                                                   mutex_;

		/// Flag whether the io_context pool has stopped already
		bool                                                         stopped_  = true;

		/// The next io_context to use for a connection. 
		std::size_t                                                  next_     = 0;

		// Give all the io_contexts executor_work_guard to do so that their run() functions will not 
		// exit until they are explicitly stopped. 
		std::vector<asio::executor_work_guard<asio::io_context::executor_type>> guards_;

		// 
		std::atomic<std::size_t>                                     pending_  = 0;
	};

	class iopool_base
	{
	public:
		iopool_base() = default;
		virtual ~iopool_base() {}

		virtual bool                        start  ()                          = 0;
		virtual void                        stop   ()                          = 0;
		virtual bool                        stopped() const                    = 0;
		virtual io_t                      & get    (std::size_t index)         = 0;
		virtual std::size_t                 size   () const                    = 0;
		virtual std::atomic<std::size_t>  & pending()                          = 0;
	};

	class default_iopool : public iopool_base
	{
	public:
		explicit default_iopool(std::size_t concurrency) : impl_(concurrency)
		{
		}

		/**
		 * @destructor
		 */
		virtual ~default_iopool()
		{
			this->impl_.stop();
		}

		/**
		 * @function : run all io_context objects in the pool.
		 */
		virtual bool start() override
		{
			return this->impl_.start();
		}

		/**
		 * @function : stop all io_context objects in the pool
		 */
		virtual void stop() override
		{
			this->impl_.stop();
		}

		/**
		 * @function : check whether the io_context pool is stopped
		 */
		virtual bool stopped() const override
		{
			return this->impl_.stopped();
		}

		/**
		 * @function : get an io_t to use
		 */
		virtual io_t& get(std::size_t index) override
		{
			return this->impl_.get(index);
		}

		/**
		 * @function : get io_context pool size.
		 */
		virtual std::size_t size() const override
		{
			return this->impl_.size();
		}

		/**
		 * @function : 
		 */
		virtual std::atomic<std::size_t>& pending() override
		{
			return this->impl_.pending();
		}

	protected:
		iopool impl_;
	};

	/**
	 * This io_context pool is passed in by the user
	 */
	template<class Container>
	class user_iopool : public iopool_base
	{
	public:
		using copy_container_type = typename detail::remove_cvref_t<Container>;
		using copy_value_type     = typename copy_container_type::value_type;

		using io_container_type = std::conditional_t<
			is_io_context_pointer<copy_value_type>::value,
			std::vector<std::unique_ptr<io_t>>, std::vector<io_t*>>;
		using io_value_type     = typename io_container_type::value_type;

		/**
		 * @constructor
		 */
		template<class C>
		explicit user_iopool(C&& copy) : copy_(std::forward<C>(copy))
		{
			if constexpr (is_io_context_pointer<copy_value_type>::value)
			{
				// why use &(*ioc) ?
				// the io_context pointer maybe "io_context* , std::shared_ptr<io_context> , ..."
				for (auto& ioc : copy_)
				{
					iots_.emplace_back(std::make_unique<io_t>(&(*ioc), this->pending_));
				}
			}
			else
			{
				for (auto& iot : copy_)
				{
					iots_.emplace_back(&(*iot));
				}
			}
		}

		/**
		 * @destructor
		 */
		virtual ~user_iopool()
		{
			this->stop();
		}

		/**
		 * @function : run all io_context objects in the pool.
		 */
		virtual bool start() override
		{
			clear_last_error();

			std::lock_guard<std::mutex> guard(this->mutex_);

			if (!this->stopped_)
			{
				set_last_error(asio::error::already_started);
				return true;
			}

			this->stopped_ = false;

			return true;
		}

		/**
		 * @function : stop all io_context objects in the pool
		 */
		virtual void stop() override
		{
			std::lock_guard<std::mutex> guard(this->mutex_);

			if (this->stopped_)
				return;

			// wiat fo all pending events completed.
			while (this->pending_ > std::size_t(0))
				std::this_thread::sleep_for(std::chrono::milliseconds(0));

			this->stopped_ = true;
		}

		/**
		 * @function : check whether the io_context pool is stopped
		 */
		virtual bool stopped() const override
		{
			return (this->stopped_);
		}

		/**
		 * @function : get an io_t to use
		 */
		virtual io_t& get(std::size_t index) override
		{
			return *(this->iots_[this->next(index)]);
		}

		/**
		 * @function : get io_context pool size.
		 */
		virtual std::size_t size() const override
		{
			return this->iots_.size();
		}

		/**
		 * @function :
		 */
		virtual std::atomic<std::size_t>& pending() override
		{
			return this->pending_;
		}

		/**
		 * @function :
		 */
		inline std::size_t next(std::size_t index)
		{
			// Use a round-robin scheme to choose the next io_context to use. 
			return (index < this->size() ? index : ((++(this->next_)) % this->size()));
		}

	protected:
		/// user container copy, maybe the user passed shared_ptr, and expect us to keep it
		copy_container_type                      copy_;

		/// The pool of io_t. 
		io_container_type                        iots_;

		/// 
		std::mutex                               mutex_;

		/// Flag whether the io_context pool has stopped already
		bool                                     stopped_  = true;

		/// The next io_context to use for a connection. 
		std::size_t                              next_     = 0;

		/// 
		std::atomic<std::size_t>                 pending_  = 0;
	};

	class iopool_cp
	{
	public:
		template<class T>
		explicit iopool_cp(T&& v)
		{
			using type = typename detail::remove_cvref_t<T>;

			if /**/ constexpr (std::is_integral_v<type>)
			{
				using pool_type = default_iopool;
				this->iopool_ = std::make_unique<pool_type>(v);
			}
			else if constexpr (is_io_context_pointer<type>::value)
			{
				ASIO2_ASSERT(v && "The io_context pointer is nullptr.");

				using container = std::vector<type>;
				container copy{ std::forward<T>(v) };

				using pool_type = user_iopool<container>;
				this->iopool_ = std::make_unique<pool_type>(std::move(copy));
			}
			else if constexpr (is_io_context_object<type>::value)
			{
				static_assert(std::is_reference_v<std::remove_cv_t<T>>);

				using container = std::vector<std::add_pointer_t<type>>;
				container copy{ &v };

				using pool_type = user_iopool<container>;
				this->iopool_ = std::make_unique<pool_type>(std::move(copy));
			}
			else if constexpr (is_io_t_pointer<type>::value)
			{
				ASIO2_ASSERT(v && "The io_t pointer is nullptr.");

				using container = std::vector<type>;
				container copy{ std::forward<T>(v) };

				using pool_type = user_iopool<container>;
				this->iopool_ = std::make_unique<pool_type>(std::move(copy));
			}
			else if constexpr (is_io_t_object<type>::value)
			{
				static_assert(std::is_reference_v<std::remove_cv_t<T>>);

				using container = std::vector<std::add_pointer_t<type>>;
				container copy{ &v };

				using pool_type = user_iopool<container>;
				this->iopool_ = std::make_unique<pool_type>(std::move(copy));
			}
			else
			{
				ASIO2_ASSERT(!v.empty() && "The container is empty.");

				using pool_type = user_iopool<type>;
				this->iopool_ = std::make_unique<pool_type>(std::forward<T>(v));
			}

			for (std::size_t i = 0, size = iopool_->size(); i < size; ++i)
			{
				iots_.emplace_back(&(iopool_->get(i)));
			}
		}

		~iopool_cp() = default;

		inline iopool_base& iopool() { return (*(this->iopool_)); }

	protected:
		inline io_t& _get_io(std::size_t index = static_cast<std::size_t>(-1))
		{
			ASIO2_ASSERT(!iots_.empty());
			std::size_t n = index < iots_.size() ? index : ((++next_) % iots_.size());
			return *(iots_[n]);
		}

	protected:
		/// the io_context pool for socket event
		std::unique_ptr<iopool_base>             iopool_;

		/// Use a copy to avoid calling the virtual function "iopool_base::get"
		std::vector<io_t*>                       iots_;

		/// The next io_context to use for a connection. 
		std::size_t                              next_ = 0;
	};
}

namespace asio2
{
	using io_t   = detail::io_t;
	using iopool = detail::iopool;
}

#endif // !__ASIO2_IOPOOL_HPP__