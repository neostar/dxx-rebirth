/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/rtrace/startup_options.h"

#include <cctype>
#include <string>

namespace dcx::render::rtrace {

bool says_gpu_based_validation_on(const std::string_view text)
{
	std::string folded;
	for (const char c : text)
		if (c != ' ' && c != '-' && c != '_' && c != '\t')
			folded += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	constexpr std::string_view key{"gpubasedvalidation"};
	const auto at{folded.find(key)};
	if (at == std::string::npos)
		return false;
	/* Only the first match counts, and it must start the text or follow a
	 * non-letter once the separators are dropped: "Disable GPU-Based
	 * Validation" and "Non-GPU-Based Validation" fold to a letter before it.
	 */
	if (at != 0 && std::isalpha(static_cast<unsigned char>(folded[at - 1])))
		return false;
	std::string_view rest{folded};
	rest.remove_prefix(at + key.size());
	if (!rest.empty() && (rest.front() == ':' || rest.front() == '='))
		rest.remove_prefix(1);
	/* The token ends at the first delimiter: "is enabled (disabled by
	 * default)" reads as "enabled", never as a word containing "disabled".
	 */
	rest = rest.substr(0, rest.find_first_of(",;.()\r\n"));
	if (rest.starts_with("is"))
		rest.remove_prefix(2);
	return rest == "enabled" || rest == "on" || rest == "true";
}

}
