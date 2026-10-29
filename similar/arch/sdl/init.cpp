/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
// Holds the main init and de-init functions for arch-related program parts

#include <SDL.h>
#include "songs.h"
#include "key.h"
#include "digi.h"
#include "mouse.h"
#include "joy.h"
#include "gr.h"
#include "dxxerror.h"
#include "text.h"
#include "args.h"
#include "window.h"
#include "dxxsconf.h"
#if SDL_MAJOR_VERSION == 3
#include "vers_id.h"
#include "common/sdl/sdl_init.h"
#endif
#if DXX_USE_DBG_HARNESS
#include "common/debug/harness_joysticks.h"
#endif

#if DXX_USE_SDLIMAGE
#include <SDL_image.h>
#endif

namespace dsx {

static void arch_close(void)
{
	songs_uninit();

	gr_close();

#if DXX_MAX_JOYSTICKS
	if (!CGameArg.CtlNoJoystick)
	{
		joy_close();
#if SDL_MAJOR_VERSION >= 2
		gamecontroller_close();
#endif
	}
#endif

	if (!CGameArg.CtlNoMouse)
		mouse_close();

	if (!CGameArg.SndNoSound)
	{
		digi_close();
	}
#if DXX_USE_SDLIMAGE
#if SDL_MAJOR_VERSION == 3
	/* SDL3_image needs neither IMG_Init nor IMG_Quit. */
#else
	IMG_Quit();
#endif
#endif
	SDL_Quit();
}

arch_atexit::~arch_atexit()
{
	arch_close();
}

arch_atexit arch_init()
{
	int t;

#if DXX_USE_DBG_HARNESS && SDL_MAJOR_VERSION >= 2
	dcx::harness_turn_off_real_joysticks();
#endif
#if SDL_MAJOR_VERSION == 3
	/* The identifier is the bundle identifier of common/arch/cocoa/Info.plist
	 * and, on Wayland, the application id.
	 */
	const dcx::sdl::app_metadata metadata{
		.name = "D" DXX_STRINGIZE(DXX_BUILD_DESCENT) "X-Rebirth",
		.version = DXX_VERSION_STR,
		.identifier = "com.dxx-rebirth.d" DXX_STRINGIZE(DXX_BUILD_DESCENT) "x-rebirth",
	};
	const auto report{dcx::sdl::init_video(metadata)};
	if (report.metadata_error)
		con_printf(CON_NORMAL, "SDL refused the application metadata: %s", report.metadata_error->c_str());
	for (const auto name : report.refused_hints)
		con_printf(CON_NORMAL, "SDL refused the hint %s", name);
	if (!report.initialised)
		Error("SDL library initialisation failed: %s.",SDL_GetError());
#else
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		Error("SDL library initialisation failed: %s.",SDL_GetError());
#endif
#if DXX_USE_SDLIMAGE
#if SDL_MAJOR_VERSION == 3
	/* SDL3_image needs neither IMG_Init nor IMG_Quit. */
#else
	IMG_Init(0);
#endif
#endif
#if SDL_MAJOR_VERSION == 2
	/* In SDL1, grabbing input grabbed both the keyboard and the mouse.
	 * Many game management keys assume a keyboard grab.
	 * Tell SDL2 to grab the keyboard.
	 *
	 * Unlike with SDL1, players have the option of overriding this grab
	 * by setting an environment variable.  In SDL1, the only choice was
	 * to skip both the keyboard grab and the mouse grab.  Now, players
	 * can enable grabbing in the UI, but disable keyboard grab with the
	 * environment variable.
	 */
	SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
	/* Gameplay continues regardless of focus, so keep the window
	 * visible.
	 */
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	/* Support the Alt+Shift+F4 hotkey for renaming the Guide-Bot
	 */
	SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1");
#elif SDL_MAJOR_VERSION == 3
	/* The hints are set before SDL_Init, in dcx::sdl::init_video. */
#endif

	key_init();

	digi_select_system();

	if (!CGameArg.SndNoSound)
		digi_init();

	if (!CGameArg.CtlNoMouse)
		mouse_init();

#if DXX_MAX_JOYSTICKS
	if (!CGameArg.CtlNoJoystick)
	{
		joy_init();
#if SDL_MAJOR_VERSION >= 2
		gamecontroller_init();
#endif
	}
#endif

	if ((t = gr_init()) != 0)
		Error(TXT_CANT_INIT_GFX,t);

	return {};
}

}
