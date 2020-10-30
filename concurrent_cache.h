#ifndef cetlib_concurrent_cache_h
#define cetlib_concurrent_cache_h

// ===================================================================
//
// Overview
// --------
//
// The concurrent_cache class template, implemented below, provides a
// means of caching data in a thread-safe manner, using TBB's
// concurrent_(unordered|hash)_map faciliies.
//
// The user interface includes the concurrent_cache and the
// cache_handle templates.  A cache_handle object is used to provid
// immutable access to the cache elements.  The cache entries are
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
// Concurrent operations
// ---------------------
//
// With the exception of shrink_to_fit, all member functions may be
// called concurrently.  Any locking is handled internally by TBB.  In
// order to provide the entry_for(...) functionality and not incur
// locking, an auxiliary data member was introduced that cannot shrink
// during concurrent processing.  This is likely to be a problem only
// if the number of total elements processed is very large.  Once
// serial access can be ensured, shrink_to_fit() may be called, which
// will remove all unused entries from the cache and reset the
// auxiliary data member to the appropriate size.
//
// entry_for(...) -- user-defined key support
// ------------------------------------------
//
// It frequently happens that a set of data may apply for a range of
// values.  Instead of inserting an element into the cache for each
// value, the user may supply their own type as a key (TODO-need blurb
// about hashing), with the following interface (e.g.):
//
//   struct range_of_values {
//     unsigned start;
//     unsigned stop;
//
//     bool supports(unsigned const test_value) const
//     {
//        return start <= test_value && test_value < stop;
//     }
//   };
//
// If the user-defined type provides the 'bool supports(...) const'
// interface, then the cache's entry_for(...) interface is enabled,
// allowing users to retrieve the element (through a handle)
// corresponding to the key that supports a given value (e.g.):
//
//   concurrent_cache<range_of_values, V> cache;
//   auto const my_key = range_of_values{0, 10};
//   cache.emplace(my_key, ...);
//   auto h = cache.entry_for(6); // Returns value for my_key
//
// N.B. The implementation assumes that for each 'entry_for(value)'
//      call, only one cache element's key.supports(...) function may
//      return true.  It is a runtime error for more than one key to
//      support the same value.
//
// Not implemented
// ---------------
//
// The implementation below does not support a bounded cache.  All
// memory management is achieved by calling the drop_unused* and
// shrink_to_fit member functions.
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

#include "cetlib/assert_only_one_thread.h"
#include "cetlib/cache_handle.h"
#include "cetlib/concurrent_cache_entry.h"
#include "cetlib_except/exception.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_unordered_map.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

namespace cet {

  template <typename K, typename V>
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

    using count_map_t = tbb::concurrent_unordered_map<K, detail::entry_count_ptr, std::hash<K>>;
    using count_value_type = typename count_map_t::value_type;

  public:
    using Hasher = tbb::tbb_hash_compare<K>;
    using collection_t = tbb::concurrent_hash_map<K, detail::concurrent_cache_entry<V>, Hasher>;
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
    size_t
    capacity() const
    {
      return std::size(counts_);
    }

    template <typename U = V>
    handle
    emplace(K const& k, U&& value)
    {
      // Lock held on k's map entry until the function returns.
      accessor access_token;
      if (not entries_.insert(access_token, k)) {
        // Entry already exists; return cached entry.
        return handle{access_token->second};
      }

      auto const sequence_number = next_sequence_number_.fetch_add(1);
      auto counter = detail::make_counter(sequence_number);
      access_token->second = mapped_type{std::forward<U>(value), counter};

      auto [it, inserted] = counts_.insert(count_value_type{k, counter});
      if (not inserted) {
        it->second = counter;
      }
      return handle{access_token->second};
    }

    template <typename T>
    std::enable_if_t<key_supports<T>::value, handle>
    entry_for(T const& t) const
    {
      std::vector<K> matching_keys;
      for (auto const& [key, count] : counts_) {
        if (key.supports(t)) {
          matching_keys.push_back(key);
        }
      }

      if (std::empty(matching_keys)) {
        return handle{};
      }

      if (std::size(matching_keys) > 1) {
        throw cet::exception("Data retrieval error.") << "More than one key match.";
      }

      return at(matching_keys[0]);
    }

    handle
    at(K const& k) const
    {
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
      auto entries_to_drop = unused_entries_();
      std::sort(begin(entries_to_drop), end(entries_to_drop), std::greater<>{});

      if (std::size(entries_to_drop) <= keep_last) {
        return;
      }

      auto const erase_begin = cbegin(entries_to_drop) + keep_last;
      auto const erase_end = cend(entries_to_drop);
      for (auto it = erase_begin; it != erase_end; ++it) {
        entries_.erase(it->second);
      }
    }

    void
    shrink_to_fit()
    {
      CET_ASSERT_ONLY_ONE_THREAD();
      drop_unused();
      std::vector<count_value_type> all_key_entries(begin(counts_), end(counts_));
      auto const stale_entries = unused_entries_();
      for (auto const& [sequence_number, key] : stale_entries) {
        auto it = std::find_if(begin(all_key_entries),
                               end(all_key_entries),
                               [&key](auto const& pr) { return pr.second == key; });
        all_key_entries.erase(it);
      }
      counts_ = count_map_t{begin(all_key_entries), end(all_key_entries)};
    }

  private:
    std::vector<std::pair<std::size_t, K>>
    unused_entries_()
    {
      std::vector<std::pair<std::size_t, K>> result;
      for (auto const& [key, count] : counts_) {
        if (count->use_count == 0u) {
          result.emplace_back(count->sequence_number, key);
        }
      }
      return result;
    }

    std::atomic<std::size_t> next_sequence_number_{0ull};
    collection_t entries_;
    count_map_t counts_;
  };
}

#endif /* cetlib_concurrent_cache_h */

// Local Variables:
// mode: c++
// End:
