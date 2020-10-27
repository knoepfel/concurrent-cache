#ifndef cetlib_test_interval_of_validity_h
#define cetlib_test_interval_of_validity_h

#include "tbb/concurrent_hash_map.h"

#include <utility>

namespace cet::test {

  class interval_of_validity {
  public:
    using value_type = std::pair<unsigned int, unsigned int>;

    interval_of_validity(unsigned int begin, unsigned int end) : range_{begin, end} {}

    bool
    supports(unsigned int const value) const noexcept
    {
      return range_.first <= value && range_.second > value;
    }

    bool
    operator<(interval_of_validity const& other) const noexcept
    {
      return range_ < other.range_;
    }
    friend class iov_hasher;
    friend std::ostream& operator<<(std::ostream&, interval_of_validity const& iov);

  private:
    value_type range_;
  };

  struct iov_hasher {
    std::size_t
    hash(interval_of_validity const& iov) const
    {
      return hasher.hash(iov.range_);
    }

    bool
    equal(interval_of_validity const& lhs, interval_of_validity const& rhs) const
    {
      return hasher.equal(lhs.range_, rhs.range_);
    }
    tbb::tbb_hash_compare<interval_of_validity::value_type> hasher;
  };

  inline std::ostream&
  operator<<(std::ostream& os, interval_of_validity const& iov)
  {
    return os << '[' << iov.range_.first << ", " << iov.range_.second << ')';
  }

}

#endif /* cetlib_test_interval_of_validity_h */

// Local Variables:
// mode: c++
// End:
