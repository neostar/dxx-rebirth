/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <utility>

namespace dcx::render::trace {

namespace {

enum class kind : std::uint8_t
{
	clear = 1,
	present = 2,
};

/* A clear's flags byte: bit 0 colour, bit 1 depth, the others zero. */
constexpr unsigned clear_color{1};
constexpr unsigned clear_depth{2};

/* Appends little-endian integers to a byte buffer. */
class sink
{
	std::vector<std::uint8_t> &out_;

  public:
	explicit sink(std::vector<std::uint8_t> &out) : out_{out}
	{}
	template <typename U>
	void le(const U v)
	{
		for (unsigned shift{0}; shift < 8 * sizeof(U); shift += 8)
			out_.push_back(static_cast<std::uint8_t>(v >> shift));
	}
};

}

writer::writer()
{
	bytes_.assign(magic.begin(), magic.end());
	sink s{bytes_};
	s.le(version);
	s.le(std::uint16_t{0});
}

void writer::clear(const trace::clear &c)
{
	sink s{bytes_};
	s.le(std::to_underlying(kind::clear));
	s.le(std::uint32_t{1});
	s.le(static_cast<std::uint8_t>((c.color ? clear_color : 0u) | (c.depth ? clear_depth : 0u)));
}

void writer::present()
{
	sink s{bytes_};
	s.le(std::to_underlying(kind::present));
	s.le(std::uint32_t{0});
}

std::vector<std::uint8_t> writer::take()
{
	return std::exchange(bytes_, {});
}

}
