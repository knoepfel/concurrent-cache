#define BOOST_TEST_MODULE (concurrent_cache_mt test)
#include "cetlib/quiet_unit_test.hpp"

#include "cetlib/concurrent_cache.h"
#include "cetlib/test/interval_of_validity.h"

#include "tbb/parallel_for.h"

#include <chrono>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using cet::test::interval_of_validity;
using namespace std::string_literals;

namespace {
  auto
  make_iov(unsigned const b, unsigned const e)
  {
    return interval_of_validity{b, e};
  }
  std::map<interval_of_validity, std::string> iovs{{make_iov(0, 10), "Good"},
                                                   {make_iov(10, 20), "Bad"}};

  class ConditionsCache {
  public:
    auto
    data_for(unsigned int const event)
    {
      if (auto h = cache_.entry_for(event)) { return h; }

      for (auto const& [iov, value] : iovs) {
        if (iov.supports(event)) { return cache_.emplace(iov, value); }
      }

      throw cet::exception("Data not found");
    }

    void
    drop_unused()
    {
      cache_.drop_unused();
    }

  private:
    cet::concurrent_cache<interval_of_validity, std::string, cet::test::iov_hasher> cache_;
  };

  auto
  make_event_numbers(std::size_t const n)
  {
    std::vector<unsigned> result(n);
    auto b = begin(result);
    auto e = end(result);
    std::iota(b, e, 0);
    std::shuffle(b, e, std::mt19937{std::random_device{}()});
    return result;
  }

  struct value_counter {
    std::atomic<unsigned> the_goods{};
    std::atomic<unsigned> the_bads{};
    std::atomic<unsigned> the_uglies{};

    void
    tally(unsigned const event, std::string const& value)
    {
      if (event < 10 and value == "Good") { ++the_goods; }
      else if (event > 9 and value == "Bad") {
        ++the_bads;
      }
      else {
        ++the_uglies;
      }
    }
  };
}

namespace cet {
  template <typename T>
  std::ostream&
  operator<<(std::ostream& os, cache_handle<T> h)
  {
    if (not h) { return os << "Invalid handle."; }
    return os << *h;
  }
}

BOOST_AUTO_TEST_SUITE(concurrent_cache_mt_test)

BOOST_AUTO_TEST_CASE(user_defined)
{
  ConditionsCache cache;
  value_counter counter;
  for (unsigned const e : make_event_numbers(20)) {
    auto h = cache.data_for(e);
    counter.tally(e, *h);
  }
  BOOST_TEST(counter.the_goods == 10u);
  BOOST_TEST(counter.the_bads == 10u);
  BOOST_TEST(counter.the_uglies == 0u);
}

BOOST_AUTO_TEST_CASE(user_defined_mt)
{
  ConditionsCache cache;
  value_counter counter;

  auto const event_numbers = make_event_numbers(20);

  auto count_data = [&](auto const index) {
    auto const e = event_numbers[index];
    auto const h = cache.data_for(e);
    counter.tally(e, *h);
    cache.drop_unused();
  };

  tbb::parallel_for(0, 20, count_data);
  BOOST_TEST(counter.the_goods == 10u);
  BOOST_TEST(counter.the_bads == 10u);
  BOOST_TEST(counter.the_uglies == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
