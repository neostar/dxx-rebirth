/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>

namespace dcx {

/* Sort `range` in place by `comp`, which need not be a strict weak ordering.
 * `std::sort` requires one, and hardened standard library builds check
 * that requirement.  Neither render comparator satisfies it: in
 * `compare_children` (a "must draw before" relation) and in D2's
 * `render_compare_context_t` (a fireball rule mixed with distance),
 * equivalence is not transitive; the unit test has an example of each.
 * An insertion sort calls `comp` only on elements of the range and stays
 * inside it for any comparator.  When `comp` is irreflexive and
 * asymmetric, the result has no adjacent pair with `comp(next, prev)`.
 * When `comp` is a strict weak ordering, the result is the same as from
 * `std::stable_sort`.  Its cost is quadratic: at most six children, or
 * MAX_OBJECTS objects in a segment, usually few.  `comp` is taken by
 * reference, since the object comparator holds a per-object table.
 */
template <std::ranges::contiguous_range R, typename C>
requires(std::indirect_binary_predicate<const C &, std::ranges::iterator_t<R>,
                                        std::ranges::iterator_t<R>>)
void insertion_sort(R &&range, const C &comp)
{
	const std::span elements{range};
	for (std::size_t i{1}; i < elements.size(); ++i)
		for (std::size_t j{i}; j != 0 && comp(elements[j], elements[j - 1]); --j)
			std::ranges::swap(elements[j], elements[j - 1]);
}

}
