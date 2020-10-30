#ifndef cetlib_cache_handle_h
#define cetlib_cache_handle_h

// ====================================================================
// The cache handle is the user interface for accessing concurrent
// cache elements.  A handle that points to a specific cache element
// ensures that that element will not be deleted from the cache during
// the lifetime of the handle.
//
// A typical way of using the cache handle looks like:
//
//   concurrent_cache<K, V> cache;
//   if (auto h = cache.at(key)) {
//     auto const& value_for_key = *h;
//     h->some_member_function_of_type_V();
//     ...
//   }
//
// The above example demonstrates three aspects of handles:
//
//   - A valid/invalid handle is convertible to the Boolean values
//     true/false.
//   - Access to the underlying entry's immutable value is provided
//     via operator*.
//   - Access to the underlying entry's const-qualified member
//     functions is provided via operator->.
//
// N.B. A handle cannot in any way adjust the underlying value.  It is
//      considered immutable.
// ====================================================================

#include "cetlib/concurrent_cache_entry.h"
#include "cetlib/exempt_ptr.h"
#include "cetlib_except/exception.h"

namespace cet {

  template <typename V>
  class cache_handle {
  public:
    cache_handle() = default;
    explicit cache_handle(detail::concurrent_cache_entry<V>& entry)
      : entry_{cet::make_exempt_ptr(&entry)}
    {
      if (entry_) {
        entry_->increment_reference_count();
      }
    }

    cache_handle(cache_handle const& other) : entry_{other.entry_}
    {
      if (entry_) {
        entry_->increment_reference_count();
      }
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
      if (entry_) {
        entry_->increment_reference_count();
      }
      return *this;
    }

    explicit operator bool() const noexcept { return entry_ != nullptr; }

    V const&
    operator*() const
    {
      if (entry_ == nullptr) {
        throw exception("Invalid cache handle dereference.")
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
      if (entry_ == nullptr) {
        return;
      }
      entry_->decrement_reference_count();
      entry_ = nullptr;
    }

    ~cache_handle() { invalidate(); }

  private:
    cet::exempt_ptr<detail::concurrent_cache_entry<V>> entry_{nullptr};
  };

}

#endif /* cetlib_cache_handle_h */

// Local Variables:
// mode: c++
// End:
