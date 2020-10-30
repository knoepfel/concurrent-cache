#ifndef cetlib_concurrent_cache_entry_h
#define cetlib_concurrent_cache_entry_h

// ===================================================================
// The concurrent_cache_entry class is a reference-counted object that
// is used as the value_type of the concurrent_cache.  For more
// details, see notes in concurrent_entry.h
//
// N.B. This is not intended to be user-facing.
// ===================================================================

#include "cetlib_except/exception.h"

#include <atomic>
#include <memory>

namespace cet::detail {
  struct entry_count {
    entry_count(std::size_t id, unsigned int n) : sequence_number{id}, use_count{n} {}
    std::size_t sequence_number;
    std::atomic<unsigned int> use_count;
  };

  using entry_count_ptr = std::shared_ptr<entry_count>;

  auto
  make_counter(std::size_t const sequence_number, unsigned int offset = 0)
  {
    return std::make_shared<entry_count>(sequence_number, offset);
  }
  auto
  make_invalid_counter()
  {
    return make_counter(-1ull, -1u);
  }

  template <typename T>
  class concurrent_cache_entry {
  public:
    concurrent_cache_entry() = default;

    template <typename U = T>
    concurrent_cache_entry(U&& u, entry_count_ptr counter)
      : value_{std::make_unique<T>(std::forward<U>(u))}, count_{std::move(counter)}
    {}

    T const&
    get() const
    {
      if (value_.get() == nullptr) {
        throw cet::exception("Invalid cache entry dereference.")
          << "Cache entry " << count_->sequence_number << " is empty.";
      }
      return *value_;
    }

    void
    increment_reference_count()
    {
      ++count_->use_count;
    }
    void
    decrement_reference_count()
    {
      --count_->use_count;
    }

    std::size_t
    sequence_number() const noexcept
    {
      return count_->sequence_number;
    }

    unsigned int
    reference_count() const noexcept
    {
      return count_->use_count;
    }

  private:
    std::unique_ptr<T> value_{nullptr};
    entry_count_ptr count_{make_invalid_counter()};
  };
}

#endif /* cetlib_concurrent_cache_entry_h */

// Local Variables:
// mode: c++
// End:
