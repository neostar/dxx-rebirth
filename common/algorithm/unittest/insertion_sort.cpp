/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/algorithm/insertion_sort.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth insertion sort
#include <boost/test/unit_test.hpp>

namespace {

/* A "must draw before" relation, the shape of `compare_children`: true only
 * for the listed pairs, false (unordered) for every other pair.
 */
struct draw_before
{
	std::span<const std::pair<int, int>> pairs;
	[[nodiscard]]
	bool operator()(const int a, const int b) const
	{
		return std::ranges::find(pairs, std::pair{a, b}) != pairs.end();
	}
};

/* The shape of D2's `render_compare_context_t`: within a window of the two
 * objects' combined size, a fireball sorts before an object that is not
 * one; every other pair sorts by distance, farther first.
 */
enum class object_kind : std::uint8_t
{
	robot,
	fireball,
};

struct distant_object
{
	object_kind kind;
	int distance;
	int size;
	char name;
};

[[nodiscard]]
bool farther_or_fireball_first(const distant_object &a, const distant_object &b)
{
	const int delta{a.distance - b.distance};
	if (std::abs(delta) < a.size + b.size)
	{
		const bool a_fireball{a.kind == object_kind::fireball};
		const bool b_fireball{b.kind == object_kind::fireball};
		if (a_fireball && !b_fireball)
			return true;
		if (b_fireball && !a_fireball)
			return false;
	}
	return delta > 0;
}

template <std::size_t N>
[[nodiscard]]
std::array<char, N> names(const std::array<distant_object, N> &objects)
{
	std::array<char, N> r{};
	std::ranges::transform(objects, r.begin(), &distant_object::name);
	return r;
}

constexpr std::array<std::pair<int, int>, 1> two_before_zero{{{2, 0}}};

}

/* Test that a "must draw before" relation, where 1 is unordered with 0 and
 * with 2 but 2 must be drawn before 0, sorts without a hardening assertion,
 * to the order worked by hand: no adjacent pair is out of order, so nothing
 * moves.
 */
BOOST_AUTO_TEST_CASE(draw_before_relation_with_intransitive_unordered)
{
	const draw_before comp{two_before_zero};
	std::array<int, 3> children{{0, 1, 2}};
	dcx::insertion_sort(children, comp);
	BOOST_TEST((children == std::array<int, 3>{{0, 1, 2}}));
}

/* Test that the relation moves an element past every neighbour it must be
 * drawn before, and stops at the first one it need not be.
 */
BOOST_AUTO_TEST_CASE(draw_before_relation_moves_to_first_unordered_neighbour)
{
	const draw_before comp{two_before_zero};
	std::array<int, 3> children{{1, 0, 2}};
	dcx::insertion_sort(children, comp);
	BOOST_TEST((children == std::array<int, 3>{{1, 2, 0}}));
}

/* Test that the mixed rule and distance comparator sorts without a
 * hardening assertion when it is not transitive: fireball `f` (large) sorts
 * before the farther robot `r`, `r` before the equally near fireball `g`
 * (small, outside the window), and `f` and `g` are equal.
 */
BOOST_AUTO_TEST_CASE(fireball_rule_mixed_with_distance)
{
	const distant_object f{.kind = object_kind::fireball, .distance = 15, .size = 10, .name = 'f'};
	const distant_object g{.kind = object_kind::fireball, .distance = 15, .size = 1, .name = 'g'};
	const distant_object r{.kind = object_kind::robot, .distance = 20, .size = 1, .name = 'r'};
	BOOST_TEST(farther_or_fireball_first(f, r));
	BOOST_TEST(farther_or_fireball_first(r, g));
	BOOST_TEST(!farther_or_fireball_first(f, g));
	BOOST_TEST(!farther_or_fireball_first(g, f));
	std::array<distant_object, 3> objects{{g, r, f}};
	dcx::insertion_sort(objects, farther_or_fireball_first);
	/* r moves before g; f is equal to g and stays after it. */
	BOOST_TEST((names(objects) == std::array<char, 3>{{'r', 'g', 'f'}}));
}

/* Test that equal elements keep their relative order, in a range long
 * enough that libc++'s std::sort does not fall back to its own insertion
 * sort: objects alternate between two distances, and the farther ones must
 * come first, each group in its input order.
 */
BOOST_AUTO_TEST_CASE(equal_elements_keep_their_order)
{
	constexpr std::size_t count{64};
	std::array<std::pair<distant_object, std::size_t>, count> objects{};
	for (std::size_t i{0}; i != count; ++i)
		objects.at(i) = {
		    {.kind = object_kind::robot, .distance = i % 2 ? 10 : 30, .size = 1, .name = 'o'}, i};
	dcx::insertion_sort(objects, [](const auto &a, const auto &b) {
		return farther_or_fireball_first(a.first, b.first);
	});
	for (std::size_t i{0}; i != count / 2; ++i)
	{
		BOOST_TEST(objects.at(i).second == 2 * i);
		BOOST_TEST(objects.at(count / 2 + i).second == 2 * i + 1);
	}
}

/* Test that, for every order of six children under a "must draw before"
 * relation that is not transitive, the result is a permutation of the input
 * with no adjacent pair out of order.
 */
BOOST_AUTO_TEST_CASE(every_order_of_six_children_ends_with_no_adjacent_inversion)
{
	constexpr std::array<std::pair<int, int>, 4> pairs{{{5, 0}, {3, 1}, {1, 4}, {2, 5}}};
	const draw_before comp{pairs};
	std::array<int, 6> input{{0, 1, 2, 3, 4, 5}};
	unsigned orders{0};
	do
	{
		auto children{input};
		dcx::insertion_sort(children, comp);
		BOOST_TEST(std::ranges::is_permutation(children, input));
		const auto inversion{std::ranges::adjacent_find(
		    children, [&comp](const int a, const int b) { return comp(b, a); })};
		BOOST_TEST((inversion == children.end()));
		++orders;
	} while (std::ranges::next_permutation(input).found);
	BOOST_TEST(orders == 720U);
}
