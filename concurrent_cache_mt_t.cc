#include <catch2/catch.hpp>

#include "cetlib/container_algorithms.h"

#include "cetlib/concurrent_cache.h"
#include "cetlib/test/interval_of_validity.h"

#include "tbb/parallel_for_each.h"

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

  constexpr auto num_events = 20;
  constexpr auto half_of_them = num_events / 2;

  std::map<interval_of_validity, std::string> iovs{{make_iov(0, half_of_them), "Good"},
                                                   {make_iov(half_of_them, num_events), "Bad"}};

  class ConditionsCache {
  public:
    auto
    data_for(unsigned int const event)
    {
      if (auto h = cache_.entry_for(event)) {
        return h;
      }

      for (auto const& [iov, value] : iovs) {
        if (iov.supports(event)) {
          return cache_.emplace(iov, value);
        }
      }

      throw cet::exception("Data not found");
    }

    void
    drop_unused(unsigned const n = 0)
    {
      cache_.drop_unused_but_last(n);
    }

  private:
    cet::concurrent_cache<interval_of_validity, std::string> cache_;
  };

  auto
  event_numbers()
  {
    std::vector<unsigned> result(num_events);
    auto b = begin(result);
    auto e = end(result);
    std::iota(b, e, 0);
    std::shuffle(b, e, std::mt19937{std::random_device{}()});
    return result;
  }

  class value_counter {
  public:
    void
    tally(unsigned const event, std::string const& value)
    {
      if (event < half_of_them and value == "Good") {
        ++the_goods_;
      }
      else if (event >= half_of_them and value == "Bad") {
        ++the_bads_;
      }
      else {
        ++the_uglies_;
      }
    }

    bool
    correct_tally() const
    {
      auto const success =
        the_goods_ == half_of_them and the_bads_ == half_of_them and the_uglies_ == 0u;
      if (not success)
        std::cout << "Uh oh: " << the_goods_ << ", " << the_bads_ << ", " << the_uglies_ << '\n';
      return success;
    }

  private:
    std::atomic<unsigned> the_goods_{};
    std::atomic<unsigned> the_bads_{};
    std::atomic<unsigned> the_uglies_{};
  };

  class count_data {
  public:
    count_data(value_counter& counter, unsigned const drop_n = -1u) : counter_{counter}, n_{drop_n}
    {}

    // This is the function that is potentially called from multiple threads.
    void
    operator()(unsigned const event) const
    {
      auto h = cache_->data_for(event);
      counter_.tally(event, *h);
      if (n_ == -1u) return;
      cache_->drop_unused(n_);
    }

  private:
    std::shared_ptr<ConditionsCache> cache_{std::make_shared<ConditionsCache>()};
    value_counter& counter_;
    unsigned n_;
  };

}

TEST_CASE("User-defined (single-threaded)")
{
  auto const events = event_numbers();
  value_counter counter;

  SECTION("Drop nothing")
  {
    cet::for_all(events, count_data{counter});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all unused")
  {
    cet::for_all(events, count_data{counter, 0});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all but 1 unused")
  {
    cet::for_all(events, count_data{counter, 1});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all but 2 unused")
  {
    cet::for_all(events, count_data{counter, 2});
    CHECK(counter.correct_tally());
  }
}

TEST_CASE("User-defined (multi-threaded)")
{
  auto const events = event_numbers();
  value_counter counter;

  SECTION("Drop nothing")
  {
    tbb::parallel_for_each(events, count_data{counter});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all unused")
  {
    tbb::parallel_for_each(events, count_data{counter, 0});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all but 1 unused")
  {
    tbb::parallel_for_each(events, count_data{counter, 1});
    CHECK(counter.correct_tally());
  }
  SECTION("Drop all but 2 unused")
  {
    tbb::parallel_for_each(events, count_data{counter, 2});
    CHECK(counter.correct_tally());
  }
}
