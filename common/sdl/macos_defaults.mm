/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/macos_defaults.h"

#import <Foundation/Foundation.h>

namespace dcx::sdl {

void register_early_user_defaults()
{
	@autoreleasepool
	{
		[[NSUserDefaults standardUserDefaults] registerDefaults:@{
			@"NSApplicationUpdateCycleEnabled" : @NO
		}];
	}
}

std::optional<bool> update_cycle_default()
{
	@autoreleasepool
	{
		NSNumber *const value{[[NSUserDefaults standardUserDefaults]
		    objectForKey:@"NSApplicationUpdateCycleEnabled"]};
		if (!value)
			return std::nullopt;
		return static_cast<bool>([value boolValue]);
	}
}

}
