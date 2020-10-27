#ifndef cetlib_concurrent_cache_h
#define cetlib_concurrent_cache_h

// ===================================================================
//
// Overview
// --------
//
// The concurrent_cache class template, implemented below, provides a
// means of caching data in a thread-safe manner, using TBB's
// concurrent_hash_map facility.
//
// This file contains three class templates:
//   - concurrent_cache<K, V>
//   - cache_handle<T>
//   - cache_entry<T>
//
// The user interface includes the concurrent_cache and the
// cache_handle templates.  A cache_handle object is used to access
// elements of the concurrent_cache.  The cache entries are
// reference-counted so that an entry cannot be removed from the cache
// unless all handles referring to that object have been destroyed or
// invalidated.
//
// Cache cleanup and entry retention
// ---------------------------------
//
// The cache's drop_unused() function may be called to remove all
// entries whose reference counts are zero.  It can be helpful to
// retain some unused entries in case they might be required again.
// In that case, the drop_unused_but_last(n) function can be called,
// where n is an unsigned integer indicating the n "most recently
// created", yet unused, entries that should be retained.
//
// Not implemented
// ---------------
//
// The implementation below does not support a bounded cache.  All
// memory management is achieved by
//
// Technical notes
// ---------------
//
// Each cache entry is constructed with an identifier represented by
// an unsigned integer of type std::size_t. The identifier starts at 0
// and atomically increments by 1 for each new entry throughout the
// lifetime of the cache.  This choice makes it possible to retain n
// unused entries, as described above.
//
// This choice also implies that for each cache object, no more than
// std::numerical_limits<std::size_t>::max() - 1 entries may be
// created, a limit which is unlikely to ever be reached.
//
// ===================================================================

#include "cetlib/metaprogramming.h"
#include "cetlib_except/exception.h"

#include "tbb/concurrent_hash_map.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

namespace cet {

  template <typename T>
  class cache_entry {
  public:
    cache_entry() = default;

    template <typename U = T>
    cache_entry(U&& u, std::size_t const sequence_number)
      : value_{std::make_unique<T>(std::forward<U>(u))}
      , sequence_number_{sequence_number}
      , count_{std::make_unique<std::atomic<unsigned int>>()}
    {}

    T const&
    get() const
    {
      if (value_.get() == nullptr) {
        throw cet::exception("Invalid cache entry dereference.")
          << "Cache entry " << sequence_number_ << " is empty.";
      }
      return *value_;
    }

    void
    increment_reference_count()
    {
      ++(*count_);
    }
    void
    decrement_reference_count()
    {
      --(*count_);
    }

    std::size_t
    sequence_number() const noexcept
    {
      return sequence_number_;
    }

    unsigned int
    reference_count() const noexcept
    {
      return *count_;
    }

  private:
    std::unique_ptr<T> value_{nullptr};
    std::size_t sequence_number_{-1ull};

    // An std::atomic is neither movable nor copyable, so we wrap it
    // in a unique ptr.
    std::unique_ptr<std::atomic<unsigned int>> count_{};
  };

  template <typename V>
  class cache_handle {
  public:
    cache_handle() = default;
    explicit cache_handle(cache_entry<V>& entry) : entry_{&entry}
    {
      if (entry_) { entry_->increment_reference_count(); }
    }

    cache_handle(cache_handle const& other) : entry_{other.entry_}
    {
      if (entry_) { entry_->increment_reference_count(); }
    }

    cache_handle&
    operator=(cache_handle const& other)
    {
      if (entry_ == other.entry_) {
        // When the handle points to the same entry, do not adjust
        // reference count as doing so could result in bringing the
        // count down to 0 and potentially allowing the entry to be
        // erased (by another thread) before the reference count is
        // brought back to 1.
        return *this;
      }
      invalidate();
      entry_ = other.entry_;
      if (entry_) { entry_->increment_reference_count(); }
      return *this;
    }

    explicit operator bool() const noexcept { return entry_ != nullptr; }

    V const&
    operator*() const
    {
      if (entry_ == nullptr) {
        throw cet::exception("Invalid cache handle dereference.")
          << "Handle does not refer to any cache entry.";
      }
      return entry_->get();
    }

    V const*
    operator->() const
    {
      return &this->operator*();
    }

    void
    invalidate()
    {
      if (entry_ == nullptr) { return; }
      entry_->decrement_reference_count();
      entry_ = nullptr;
    }

    ~cache_handle() { invalidate(); }

  private:
    cache_entry<V>* entry_{nullptr};
  };

  template <typename K, typename V, typename Hasher = tbb::tbb_hash_compare<K>>
  class concurrent_cache {
    // For some cases, the user will not know what the key is.  For
    // example, if the key corresponds to an interval of validity
    // represented by a pair of numbers [b, e), the user may want to
    // retrieve the entry for some value that is supported by the
    // range [b, e).
    //
    // The key_supports struct uses SFINAE to determine whether a
    // 'key' of type K, contains a member function that can be called
    // via 'key.supports(t)`, where 't' is of type T.
    template <typename T, typename = void>
    struct key_supports : std::false_type {};

    template <typename T>
    struct key_supports<T, std::void_t<decltype(std::declval<K>().supports(std::declval<T>()))>>
      : std::true_type {};

  public:
    using collection_t = tbb::concurrent_hash_map<K, cache_entry<V>, Hasher>;
    //using counters_t = std::map<K, std::unique_ptr<std::atomic<unsigned int>>;
    using mapped_type = typename collection_t::mapped_type;
    using value_type = typename collection_t::value_type;
    using accessor = typename collection_t::accessor;
    using handle = cache_handle<V>;

    // TODO: Provide boundedness feature ?

    size_t
    size() const
    {
      return std::size(entries_);
    }
    bool
    empty() const
    {
      return std::empty(entries_);
    }

    template <typename U = V>
    handle
    emplace(K const& k, U&& value)
    {
      std::lock_guard sentry{mutex_};
      accessor access_token;
      if (not entries_.insert(access_token, k)) {
        // Entry already exists; return cached entry.
        return handle{access_token->second};
      }

      auto const sequence_number = next_sequence_number_.fetch_add(1);
      access_token->second = mapped_type{std::forward<U>(value), sequence_number};
      return handle{access_token->second};
    }

    template <typename T>
    std::enable_if_t<key_supports<T>::value, handle>
    entry_for(T const& t)
    {
      std::lock_guard sentry{mutex_};
      // An incredibly inefficient way of finding the correct entry.
      for (auto& [key, value] : entries_) {
        if (key.supports(t)) {
          std::ostringstream oss;
          return handle{value};
        }
      }
      return handle{};
    }

    handle
    at(K const& k)
    {
      std::lock_guard sentry{mutex_};
      if (accessor access_token; entries_.find(access_token, k))
        return handle{access_token->second};
      return handle{};
    }

    void
    drop_unused()
    {
      drop_unused_but_last(0);
    }

    void
    drop_unused_but_last(std::size_t const keep_last)
    {
      std::vector<std::pair<std::size_t, K>> entries_to_drop;
      std::lock_guard sentry{mutex_};
      for (auto& [key, value] : entries_) {
        if (value.reference_count() == 0u) {
          entries_to_drop.emplace_back(value.sequence_number(), key);
        }
      }
      std::sort(begin(entries_to_drop), end(entries_to_drop), std::greater<>{});

      if (std::size(entries_to_drop) <= keep_last) { return; }

      auto const erase_begin = cbegin(entries_to_drop) + keep_last;
      auto const erase_end = cend(entries_to_drop);
      for (auto it = erase_begin; it != erase_end; ++it) {
        entries_.erase(it->second);
      }
    }

  private:
    std::atomic<std::size_t> next_sequence_number_{0ull};
    collection_t entries_;
    //auxiliary_t keys_and_counts_;
    std::mutex mutex_;
  };
}

#endif /* cetlib_concurrent_cache_h */

// Local Variables:
// mode: c++
// End:
