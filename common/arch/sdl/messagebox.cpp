/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *  Display an error or warning messagebox using the SDL2 messagebox function.
 *
 */

#include <SDL.h>
#include "window.h"
#include "event.h"
#include "messagebox.h"

namespace dcx {

static void display_sdl_alert(const char *message, int error)
{
	window	*wind;

	// Handle Descent's windows properly
	if ((wind = window_get_front()))
		wind->send_event(d_event{event_type::window_deactivated});

	int fullscreen = (grd_curscreen && gr_check_fullscreen());
	if (fullscreen)
		gr_toggle_fullscreen();

#if SDL_MAJOR_VERSION == 3
	/* With the game's window as its parent, the alert stays above it even
	 * while the window is still leaving fullscreen (X11).
	 */
	SDL_ShowSimpleMessageBox(error?SDL_MESSAGEBOX_ERROR:SDL_MESSAGEBOX_WARNING, error?"Sorry, a critical error has occurred.":"Attention!", message, g_pRebirthSDLMainWindow);
#else
	SDL_ShowSimpleMessageBox(error?SDL_MESSAGEBOX_ERROR:SDL_MESSAGEBOX_WARNING, error?"Sorry, a critical error has occurred.":"Attention!", message, NULL);
#endif

	if ((wind = window_get_front()))
		wind->send_event(d_event{event_type::window_activated});

	if (!error && fullscreen)
		gr_toggle_fullscreen();
}

void msgbox_warning(const std::span<const char> message)
{
	display_sdl_alert(message.data(), 0);
}

void msgbox_error(const char *message)
{
	display_sdl_alert(message, 1);
}

}
