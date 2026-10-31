#include "detail/events/cells.h"

#include <cstdio>
#include <type_traits>

namespace cells = muypoco::detail::cells;

struct pin_key {};
struct cache_key {};
struct deferred_key {};
struct first {};
struct second {};

inline constexpr auto frozen = [] {};

static_assert(!cells::occupied<pin_key, 0, [] {}>());
static_assert(cells::size<pin_key>() == 0);

constexpr std::size_t first_index = cells::write<pin_key, first>();
static_assert(first_index == 0);
static_assert(cells::occupied<pin_key, 0, [] {}>());
static_assert(!cells::occupied<pin_key, 1, [] {}>());
static_assert(std::is_same_v<cells::at<pin_key, 0>, first>);
static_assert(cells::size<pin_key>() == 1);

constexpr std::size_t second_index = cells::write<pin_key, second>();
static_assert(second_index == 1);
static_assert(std::is_same_v<cells::at<pin_key, 0>, first>);
static_assert(std::is_same_v<cells::at<pin_key, 1>, second>);
static_assert(cells::size<pin_key>() == 2);

constexpr std::size_t duplicate_index = cells::write<pin_key, first>();
static_assert(duplicate_index == 0);
static_assert(cells::size<pin_key>() == 2);

static_assert(cells::size<cache_key, frozen>() == 0);
constexpr std::size_t cache_index = cells::write<cache_key, first>();
static_assert(cache_index == 0);
static_assert(cells::size<cache_key, frozen>() == 0);
static_assert(cells::size<cache_key>() == 1);

template <class Key>
std::size_t deferred_size() {
  return cells::size<Key>();
}

int main() {
  const std::size_t seen = deferred_size<deferred_key>();
  std::printf("cells pinning: deferred instantiation sees %zu cell(s)\n", seen);
  return seen == 2 ? 0 : 1;
}

constexpr std::size_t late_first = cells::write<deferred_key, first>();
constexpr std::size_t late_second = cells::write<deferred_key, second>();
static_assert(late_first == 0 && late_second == 1);
