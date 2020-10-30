#define BOOST_TEST_MODULE (concurrent_cache test)
#include "cetlib/quiet_unit_test.hpp"

#include "cetlib/concurrent_cache.h"
#include "cetlib/test/interval_of_validity.h"

#include <regex>
#include <utility>

namespace cet {
  template <typename T>
  std::ostream&
  boost_test_print_type(std::ostream& os, cache_handle<T> h)
  {
    if (not h) {
      return os << "Invalid handle.";
    }
    return os << *h;
  }
}

using cet::boost_test_print_type;

BOOST_AUTO_TEST_SUITE(concurrent_cache_test)

BOOST_AUTO_TEST_CASE(simple)
{
  cet::concurrent_cache<std::string, int> cache;
  BOOST_TEST(empty(cache));
  {
    auto h = cache.at("Alice");
    BOOST_TEST(not h);
    BOOST_CHECK_EXCEPTION(*h, cet::exception, [](auto const& e) {
      return std::regex_match(e.category(), std::regex{"Invalid cache handle dereference."});
    });
  }
  cache.emplace("Alice", 97);
  BOOST_TEST(size(cache) == 1ull);
  {
    auto h = cache.at("Alice");
    BOOST_TEST(h);
    BOOST_TEST(*h == 97);
  }
  cache.drop_unused_but_last(1);
  BOOST_TEST(size(cache) == 1ull);
  cache.drop_unused();
  BOOST_TEST(empty(cache));
}

BOOST_AUTO_TEST_CASE(multiple_entries)
{
  cet::concurrent_cache<std::string, int> cache;
  {
    auto handle = cache.emplace("Billy", 14);
    BOOST_TEST(size(cache) == 1ull);
    cache.drop_unused_but_last(1);
    BOOST_TEST(size(cache) == 1ull);
    {
      cache.emplace("Bessie", 19);
      cache.emplace("Jason", 20);
    }
    auto entry = cache.at("Jason");
    BOOST_TEST(entry);
    BOOST_TEST(*entry == 20);
    BOOST_TEST(size(cache) == 3ull);
  }
  cache.drop_unused_but_last(1);
  BOOST_TEST(not cache.at("Billy"));
  BOOST_TEST(not cache.at("Bessie"));
  BOOST_TEST(size(cache) == 1ull);
}

BOOST_AUTO_TEST_CASE(copied_handle)
{
  using cache_t = cet::concurrent_cache<std::string, int>;
  cache_t ages;
  cache_t::handle h;
  BOOST_TEST(not h);
  {
    auto tmp_h = ages.emplace("Bob", 41);
    h = tmp_h;
  }
  ages.drop_unused();
  BOOST_TEST(size(ages) == 1ull);
  h.invalidate();
  ages.drop_unused();
  BOOST_TEST(empty(ages));
}

BOOST_AUTO_TEST_CASE(copy_same_handle)
{
  using cache_t = cet::concurrent_cache<std::string, int>;
  cache_t ages;
  auto tmp_h = ages.emplace("Catherine", 8);
  auto h{tmp_h};
  tmp_h.invalidate();
  for (unsigned int i{}; i != 3; ++i) {
    h = ages.at("Catherine");
  }
  BOOST_TEST(size(ages) == 1ull);
  ages.drop_unused();
  BOOST_TEST(size(ages) == 1ull);
  h.invalidate();
  ages.drop_unused();
  BOOST_TEST(empty(ages));
}

BOOST_AUTO_TEST_CASE(user_defined)
{
  cet::concurrent_cache<cet::test::interval_of_validity, std::string, cet::test::iov_hasher> cache;
  auto const run_1 = "Run 1";
  auto const run_2 = "Run 2";

  auto h = cache.emplace({1, 10}, run_1);
  BOOST_TEST(*h == run_1);
  h = cache.emplace({10, 20}, run_2);
  BOOST_TEST(*h == run_2);
  h.invalidate();
  BOOST_TEST(not cache.entry_for(0));
  BOOST_TEST(*cache.entry_for(1) == run_1);
  BOOST_TEST(*cache.entry_for(10) == run_2);
  BOOST_TEST(not cache.entry_for(20));
  cache.drop_unused_but_last(1);
  BOOST_TEST(size(cache) == 1ull);
  BOOST_TEST(cache.entry_for(10));
}

BOOST_AUTO_TEST_SUITE_END()
