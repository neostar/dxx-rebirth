/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * OGL video functions. - Added 9/15/99 Matthew Mueller
 *
 */

#define DXX_OPENGL_DECLARE_GL_VARIABLES

#ifdef RPI
// extra libraries for the Raspberry Pi
#include  <bcm_host.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <windows.h>
#endif

#if !defined(_MSC_VER) && !defined(macintosh)
#include <unistd.h>
#endif
#if !defined(macintosh)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <algorithm>
#include <errno.h>
#include <SDL.h>
#include "game.h"
#include "gr.h"
#include "gamefont.h"
#include "palette.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "inferno.h"
#include "strutil.h"
#include "args.h"
#include "physfsx.h"
#include "internal.h"
#include "common/render/frame_setup.h"
#include "common/render/gl_device.h"
#include "console.h"
#include "config.h"
#include "vers_id.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/glu.h>
#else
#if DXX_USE_OGLES
#include <EGL/egl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <SDL_syswm.h>
#endif
#endif

#include "ogl_sync.h"
#include <memory>
#if DXX_USE_DBG_HARNESS
#include "common/debug/hidden_window.h"
#endif
#if SDL_MAJOR_VERSION == 3
#include "common/sdl/display.h"
#include "event.h"
#endif

using std::min;
using std::max;

namespace dcx {

static int ogl_brightness_r, ogl_brightness_g, ogl_brightness_b;

#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
static int sdl_video_flags;

#ifdef RPI
static EGL_DISPMANX_WINDOW_T nativewindow;
static DISPMANX_ELEMENT_HANDLE_T dispman_element=DISPMANX_NO_HANDLE;
static DISPMANX_DISPLAY_HANDLE_T dispman_display=DISPMANX_NO_HANDLE;
#endif

#elif !DXX_USE_OGLES || SDL_MAJOR_VERSION >= 2
#if SDL_MAJOR_VERSION == 1
static int sdl_video_flags = SDL_OPENGL;
#elif SDL_MAJOR_VERSION == 2
SDL_Window *g_pRebirthSDLMainWindow;
static int g_iRebirthWindowX, g_iRebirthWindowY;
#elif SDL_MAJOR_VERSION == 3
/* The window of main_display, for the code that takes it from here. */
SDL_Window *g_pRebirthSDLMainWindow;
namespace {
/* The game's window and GL context, from gr_init to gr_close. */
std::optional<sdl::display<sdl::sdl_display_ops>> main_display;
}
#endif
#endif
static ogl_sync sync_helper;
static int gr_installed;
static int gl_initialized;
int linedotscale{1}; // scalar of glLinewidth and glPointSize - only calculated once when resolution changes
#ifdef RPI
static int sdl_no_modeswitch;
#else
enum { sdl_no_modeswitch = 0 };
#endif

namespace {

#if DXX_USE_STEREOSCOPIC_RENDER
void gr_set_stereo_mode_sync()
{
	// calc stereo mode sync interval for above/below format
	VR_sync_width = (VR_sync_param * SHEIGHT) / 480; // normalized for 640x480
}
#endif

}

}

namespace dsx {

#if SDL_MAJOR_VERSION == 1
void gr_set_mode_from_window_size()
{
	gr_set_mode(Game_screen_mode);
}
#elif SDL_MAJOR_VERSION == 2
static void gr_set_mode_from_window_size(SDL_Window *const SDLWindow)
{
	assert(SDLWindow);
	int w, h;
	SDL_GL_GetDrawableSize(SDLWindow, &w, &h);
	gr_set_mode(screen_mode(w, h));
}

void gr_set_mode_from_window_size()
{
	gr_set_mode_from_window_size(g_pRebirthSDLMainWindow);
}
#elif SDL_MAJOR_VERSION == 3
void gr_set_mode_from_window_size()
{
	int w, h;
	if (!SDL_GetWindowSizeInPixels(g_pRebirthSDLMainWindow, &w, &h))
	{
		con_printf(CON_URGENT, "SDL3: no window size (%s), using the configured mode", SDL_GetError());
		gr_set_mode(Game_screen_mode);
		return;
	}
	gr_set_mode(screen_mode(w, h));
}
#endif

}

#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1 
static EGLDisplay eglDisplay=EGL_NO_DISPLAY;
static EGLSurface eglSurface=EGL_NO_SURFACE;
static EGLContext eglContext=EGL_NO_CONTEXT;

static bool TestEGLError(const char* pszLocation)
{
	/*
	 * eglGetError returns the last error that has happened using egl,
	 * not the status of the last called function. The user has to
	 * check after every single egl call or at least once every frame.
	*/
	EGLint iErr = eglGetError();
	if (iErr != EGL_SUCCESS)
	{
		con_printf(CON_URGENT, "%s failed (%d).", pszLocation, iErr);
		return 0;
	}
	
	return 1;
}
#endif

namespace dcx {

void ogl_swap_buffers_internal(void)
{
	sync_helper.before_swap();
#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
	eglSwapBuffers(eglDisplay, eglSurface);
#elif !DXX_USE_OGLES || SDL_MAJOR_VERSION >= 2
#if SDL_MAJOR_VERSION == 1
	SDL_GL_SwapBuffers();
#elif SDL_MAJOR_VERSION >= 2
	SDL_GL_SwapWindow(g_pRebirthSDLMainWindow);
#endif
#endif
	sync_helper.after_swap();
}

}

namespace dsx {

#ifdef RPI

// MH: I got the following constants for vc_dispmanx_element_change_attributes() from:
//     http://qt.gitorious.org/qt/qtbase/commit/5933205cfcd73481cb0645fa6183103063fe3e0d
//     I do not know where they got them from, but OTOH, they are quite obvious.

// these constants are not in any headers (yet)
#define ELEMENT_CHANGE_LAYER          (1<<0)
#define ELEMENT_CHANGE_OPACITY        (1<<1)
#define ELEMENT_CHANGE_DEST_RECT      (1<<2)
#define ELEMENT_CHANGE_SRC_RECT       (1<<3)
#define ELEMENT_CHANGE_MASK_RESOURCE  (1<<4)
#define ELEMENT_CHANGE_TRANSFORM      (1<<5)

static void rpi_destroy_element()
{
	if (dispman_element != DISPMANX_NO_HANDLE) {
		DISPMANX_UPDATE_HANDLE_T dispman_update;
		con_printf(CON_DEBUG, "RPi: destroying display manager element");
		dispman_update = vc_dispmanx_update_start( 0 );
		if (vc_dispmanx_element_remove( dispman_update, dispman_element)) {
			 con_printf(CON_URGENT, "RPi: failed to remove dispmanx element!");
		}
		vc_dispmanx_update_submit_sync( dispman_update );
		dispman_element = DISPMANX_NO_HANDLE;
	}
}

static int rpi_setup_element(int x, int y, Uint32 video_flags, int update)
{
	// this code is based on the work of Ben O'Steen
	// http://benosteen.wordpress.com/2012/04/27/using-opengl-es-2-0-on-the-raspberry-pi-without-x-windows/
	// https://github.com/benosteen/opengles-book-samples/tree/master/Raspi
	DISPMANX_UPDATE_HANDLE_T dispman_update;
	VC_RECT_T dst_rect;
	VC_RECT_T src_rect;
	VC_DISPMANX_ALPHA_T alpha_descriptor;

	uint32_t rpi_display_device=DISPMANX_ID_MAIN_LCD;
	uint32_t display_width;
	uint32_t display_height;
	int success;

	success = graphics_get_display_size(rpi_display_device, &display_width, &display_height);
	if ( success < 0 ) {
		con_printf(CON_URGENT, "Could not get RPi display size, assuming 640x480");
		display_width=640;
		display_height=480;
	}

	if (static_cast<uint32_t>(x) > display_width) {
		con_printf(CON_URGENT, "RPi: Requested width %d exceeds display width %u, scaling down!",
			x,display_width);
		x=static_cast<int>(display_width);
	}
	if (static_cast<uint32_t>(y) > display_height) {
		con_printf(CON_URGENT, "RPi: Requested height %d exceeds display height %u, scaling down!",
			y,display_height);
		y=static_cast<int>(display_height);
	}

	con_printf(CON_DEBUG, "RPi: display resolution %ux%u, game resolution: %dx%d (%s)", display_width, display_height, x, y, (video_flags & SDL_FULLSCREEN)?"fullscreen":"windowed");
	if (video_flags & SDL_FULLSCREEN) {
		/* scale to the full display size... */
		dst_rect.x = 0;
		dst_rect.y = 0;
		dst_rect.width = display_width;
		dst_rect.height= display_height;
	} else {
		/* TODO: we could query the position of the X11 window here
		   and try to place the ovelray exactly above that...,
		   we would have to track window movements, though ... */
		dst_rect.x = 0;
		dst_rect.y = 0;
		dst_rect.width = static_cast<uint32_t>(x);
		dst_rect.height= static_cast<uint32_t>(y);
	}

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = (static_cast<uint32_t>(x))<< 16;
	src_rect.height =(static_cast<uint32_t>(y))<< 16;

	/* we do not want our overlay to be blended against the background */
	alpha_descriptor.flags=DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS;
	alpha_descriptor.opacity=0xffffffff;
	alpha_descriptor.mask=0;

	// open display, if we do not already have one ...
	if (dispman_display == DISPMANX_NO_HANDLE) {
		con_printf(CON_DEBUG, "RPi: opening display: %u",rpi_display_device);
		dispman_display = vc_dispmanx_display_open(rpi_display_device);
		if (dispman_display == DISPMANX_NO_HANDLE) {
			con_printf(CON_URGENT,"RPi: failed to open display: %u",rpi_display_device);
		}
	}

	if (dispman_element != DISPMANX_NO_HANDLE) {
		if (!update) {
			// if the element already exists, and we cannot update it, so recreate it
			rpi_destroy_element();
		}
	} else {
		// if the element does not exist, we cannot do an update
		update=0;
	}

	dispman_update = vc_dispmanx_update_start( 0 );

	if (update) {
		con_printf(CON_DEBUG, "RPi: updating display manager element");
		vc_dispmanx_element_change_attributes ( dispman_update, nativewindow.element,
							ELEMENT_CHANGE_DEST_RECT | ELEMENT_CHANGE_SRC_RECT,
							0 /*layer*/, 0 /*opacity*/,
							&dst_rect, &src_rect,
							0 /*mask*/,
							static_cast<DISPMANX_TRANSFORM_T>(VC_IMAGE_ROT0) /*transform*/);
	} else {
		// create a new element
		con_printf(CON_DEBUG, "RPi: creating display manager element");
		dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
								0 /*layer*/, &dst_rect, 0 /*src*/,
								&src_rect, DISPMANX_PROTECTION_NONE,
								&alpha_descriptor, NULL /*clamp*/,
								static_cast<DISPMANX_TRANSFORM_T>(VC_IMAGE_ROT0) /*transform*/);
		if (dispman_element == DISPMANX_NO_HANDLE) {
			con_printf(CON_URGENT,"RPi: failed to creat display manager elemenr");
		}
		nativewindow.element = dispman_element;
	}
	nativewindow.width = display_width;
	nativewindow.height = display_height;
	vc_dispmanx_update_submit_sync( dispman_update );

	return 0;
}

#endif // RPI

#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
static void ogles_destroy()
{
	if( eglDisplay != EGL_NO_DISPLAY ) {
		eglMakeCurrent(eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
	}

	if (eglContext != EGL_NO_CONTEXT) {
		con_printf(CON_DEBUG, "EGL: destroyig context");
		eglDestroyContext(eglDisplay, eglContext);
		eglContext = EGL_NO_CONTEXT;
	}

	if (eglSurface != EGL_NO_SURFACE) {
		con_printf(CON_DEBUG, "EGL: destroyig surface");
		eglDestroySurface(eglDisplay, eglSurface);
		eglSurface = EGL_NO_SURFACE;
	}

	if (eglDisplay != EGL_NO_DISPLAY) {
		con_printf(CON_DEBUG, "EGL: terminating");
		eglTerminate(eglDisplay);
		eglDisplay = EGL_NO_DISPLAY;
	}
}
#endif

static int ogl_init_window(int w, int h)
{
#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
	SDL_SysWMinfo info;
	Window    x11Window = 0;
	Display*  x11Display = 0;

#endif // OGLES

#if SDL_MAJOR_VERSION == 1
	if (gl_initialized)
		ogl_smash_texture_list_internal();//if we are or were fullscreen, changing vid mode will invalidate current textures

	SDL_WM_SetCaption(DESCENT_VERSION, DXX_SDL_WINDOW_CAPTION);
	if (const auto window_icon = SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP))
		SDL_WM_SetIcon(window_icon, nullptr);

	int use_x = w;
	int use_y = h;
	int use_bpp = CGameArg.DbgBpp;
	int use_flags = sdl_video_flags;
	if (sdl_no_modeswitch) {
		const SDL_VideoInfo *vinfo=SDL_GetVideoInfo();
		if (vinfo) {	
			use_x=vinfo->current_w;
			use_y=vinfo->current_h;
			use_bpp=vinfo->vfmt->BitsPerPixel;
			use_flags=SDL_SWSURFACE | SDL_ANYFORMAT;
		} else {
			con_puts(CON_URGENT, "Could not query video info");
		}
	}

	if (!SDL_SetVideoMode(use_x, use_y, use_bpp, use_flags))
	{
#ifdef RPI
		con_printf(CON_URGENT, "Could not set %dx%dx%d opengl video mode: %s\n (Ignored for RPI)",
			    w, h, CGameArg.DbgBpp, SDL_GetError());
#else
		Error("Could not set %dx%dx%d opengl video mode: %s\n", w, h, CGameArg.DbgBpp, SDL_GetError());
#endif
	}
#elif SDL_MAJOR_VERSION >= 2
	const auto SDLWindow{g_pRebirthSDLMainWindow};
	if (!(SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN))
		SDL_SetWindowSize(SDLWindow, w, h);
#endif

#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
#ifndef RPI
	// NOTE: on the RPi, the EGL stuff is not connected to the X11 window,
	//       so there is no need to destroy and recreate this
	ogles_destroy();
#endif

	SDL_VERSION(&info.version);

	if (SDL_GetWMInfo(&info) > 0) {
		if (info.subsystem == SDL_SYSWM_X11) {
			x11Display = info.info.x11.display;
			x11Window = info.info.x11.window;
			con_printf (CON_DEBUG, "Display: %p, Window: %i ===", x11Display, static_cast<int>(x11Window));
		}
	}

	if (eglDisplay == EGL_NO_DISPLAY) {
		const EGLNativeDisplayType desiredEGLDisplay =
#ifdef RPI
		EGL_DEFAULT_DISPLAY;
#else
		x11Display;
#endif
		eglDisplay = eglGetDisplay(desiredEGLDisplay);
		if (eglDisplay == EGL_NO_DISPLAY) {
			con_printf(CON_URGENT, "EGL: Error querying EGL Display");
		}

		EGLint ver_maj, ver_min;
		if (!eglInitialize(eglDisplay, &ver_maj, &ver_min)) {
			con_printf(CON_URGENT, "EGL: Error initializing EGL");
		} else {
			con_printf(CON_DEBUG, "EGL: Initialized, version: major %i minor %i", ver_maj, ver_min);
		}
	}

	
#ifdef RPI
	if (rpi_setup_element(w,h,sdl_video_flags,1)) {
		Error("RPi: Could not set up a %dx%d element\n", w, h);
	}
#endif

	static EGLConfig eglConfig;
	if (eglSurface == EGL_NO_SURFACE) {
		static const EGLint configAttribs[]{
			EGL_RED_SIZE, 5,
			EGL_GREEN_SIZE, 6,
			EGL_BLUE_SIZE, 5,
			EGL_DEPTH_SIZE, 16,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
			EGL_NONE, EGL_NONE
		};
		int iConfigs;
		if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &iConfigs) || (iConfigs != 1)) {
			con_printf(CON_URGENT, "EGL: Error choosing config");
		} else {
			con_printf(CON_DEBUG, "EGL: config chosen");
		}

		// explicitly request a doublebuffering window
		static const EGLint winAttribs[]{
			EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE, EGL_NONE
		};
#ifdef RPI
		eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, static_cast<EGLNativeWindowType>(&nativewindow), winAttribs);
#else
		eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, x11Window, winAttribs);
#endif
		if ((!TestEGLError("eglCreateWindowSurface")) || eglSurface == EGL_NO_SURFACE) {
			con_printf(CON_URGENT, "EGL: Error creating window surface");
		} else {
			con_printf(CON_DEBUG, "EGL: Created window surface");
		}
	}
	
	if (eglContext == EGL_NO_CONTEXT) {
		// explicitly request an OpenGL ES 1.x context
		static const EGLint contextAttribs[]{
			EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE, EGL_NONE
		};
		eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
		if ((!TestEGLError("eglCreateContext")) || eglContext == EGL_NO_CONTEXT) {
			con_printf(CON_URGENT, "EGL: Error creating context");
		} else {
			con_printf(CON_DEBUG, "EGL: Created context");
		}
	}
	
	eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
	if (!TestEGLError("eglMakeCurrent")) {
		con_printf(CON_URGENT, "EGL: Error making current");
	} else {
		con_printf(CON_DEBUG, "EGL: made context current");
	}
#endif

	const auto w640 = w / 640;
	const auto h480 = h / 480;
	const auto min_wh = std::min(w640, h480);
	linedotscale = min_wh < 1 ? 1 : min_wh;

	gl_initialized=1;
	return 0;
}

}

namespace dcx {

int gr_check_fullscreen(void)
{
#if SDL_MAJOR_VERSION == 1
	return !!(sdl_video_flags & SDL_FULLSCREEN);
#elif SDL_MAJOR_VERSION == 2
	return !!(SDL_GetWindowFlags(g_pRebirthSDLMainWindow) & SDL_WINDOW_FULLSCREEN);
#elif SDL_MAJOR_VERSION == 3
	/* As last requested, or confirmed by SDL (window_state). */
	return main_display && main_display->state.requested_fullscreen();
#endif
}

void gr_toggle_fullscreen()
{
#if SDL_MAJOR_VERSION == 1
	const auto local_sdl_video_flags = (sdl_video_flags ^= SDL_FULLSCREEN);
	if (gl_initialized)
	{
		if (sdl_no_modeswitch == 0) {
			auto gsm = Game_screen_mode;
			const auto DbgBpp{CGameArg.DbgBpp};
			if (!SDL_VideoModeOK(gsm.width, gsm.height, DbgBpp, local_sdl_video_flags))
			{
				con_printf(CON_URGENT, "Cannot set %ix%i. Fallback to 640x480", gsm.width, gsm.height);
				gsm.width = 640;
				gsm.height = 480;
				Game_screen_mode = gsm;
			}
			if (!SDL_SetVideoMode(gsm.width, gsm.height, DbgBpp, local_sdl_video_flags))
			{
				Error("Could not set %dx%dx%d opengl video mode: %s\n", gsm.width, gsm.height, DbgBpp, SDL_GetError());
			}
		}
#ifdef RPI
		if (rpi_setup_element(SM_W(Game_screen_mode), SM_H(Game_screen_mode), local_sdl_video_flags, 1)) {
			 Error("RPi: Could not set up %dx%d element\n", SM_W(Game_screen_mode), SM_H(Game_screen_mode));
		}
#endif
	}

	gr_remap_color_fonts();

	if (gl_initialized) // update viewing values for menus
	{
		ogl_record_2d_state();
		ogl_create_device();
		ogl_smash_texture_list_internal();//if we are or were fullscreen, changing vid mode will invalidate current textures
	}
	CGameCfg.WindowMode = !(local_sdl_video_flags & SDL_FULLSCREEN);
#elif SDL_MAJOR_VERSION == 2
	const auto SDLWindow{g_pRebirthSDLMainWindow};
	const auto is_fullscreen_before_change = SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN;
	CGameCfg.WindowMode = !!is_fullscreen_before_change;
	if (!is_fullscreen_before_change)
		SDL_GetWindowPosition(SDLWindow, &g_iRebirthWindowX, &g_iRebirthWindowY);
	SDL_SetWindowFullscreen(SDLWindow, is_fullscreen_before_change ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (is_fullscreen_before_change)
	{
		const auto mode{Game_screen_mode};
		SDL_SetWindowPosition(SDLWindow, g_iRebirthWindowX, g_iRebirthWindowY);
		SDL_SetWindowSize(SDLWindow, SM_W(mode), SM_H(mode));
	}
	gr_set_mode_from_window_size(SDLWindow);
#elif SDL_MAJOR_VERSION == 3
	/* Borderless desktop fullscreen, asynchronous: SDL confirms the change
	 * with ENTER/LEAVE_FULLSCREEN and the new pixel size, on which
	 * gr_handle_window_event sets CGameCfg.WindowMode and the mode
	 * (docs/agents/sdl-window.md section 3).
	 */
	if (main_display && !main_display->state.toggle_fullscreen([](const bool fullscreen) {
		    return SDL_SetWindowFullscreen(g_pRebirthSDLMainWindow, fullscreen);
	    }))
		con_printf(CON_URGENT, "SDL3: cannot change fullscreen: %s", SDL_GetError());
#endif
}

static void ogl_init_state(void)
{
	/* The clearing (background) colour is the device's. */
	/* initialize viewing values */
	ogl_record_2d_state();
	gr_palette_step_up(0,0,0);//in case its left over from in game

	ogl_init_pixel_buffers(grd_curscreen->get_screen_width(), grd_curscreen->get_screen_height());
}

static void ogl_tune_for_current(void)
{
#if !DXX_USE_OGLES
	const auto names{render::gl_device::context_names()};
	const char *const gl_vendor{names.vendor.c_str()};
	const char *const gl_renderer{names.renderer.c_str()};
	const char *const gl_version{names.version.c_str()};

	con_printf(CON_VERBOSE, "DXX-Rebirth: OpenGL: vendor: %s", gl_vendor);
	con_printf(CON_VERBOSE, "DXX-Rebirth: OpenGL: renderer: %s", gl_renderer);

	if (!names.renderer.empty())
	{
	//add driver specific hacks here.  whee.
	if ((d_stricmp(gl_renderer,"Mesa NVIDIA RIVA 1.0\n")==0 || d_stricmp(gl_renderer,"Mesa NVIDIA RIVA 1.2\n")==0) && d_stricmp(gl_version,"1.2 Mesa 3.0")==0)
	{
		con_puts(CON_VERBOSE, "DXX-Rebirth: OpenGL renderer is blacklisted for GL intensity, GL read pixel, and GL GetTexLevel");
		CGameArg.DbgGlIntensity4Ok = false;	//ignores alpha, always black background instead of transparent.
		CGameArg.DbgGlReadPixelsOk = false;	//either just returns all black, or kills the X server entirely
		CGameArg.DbgGlGetTexLevelParamOk = false;	//returns random data..
	}
	if (d_stricmp(gl_vendor,"Matrox Graphics Inc.")==0)
	{
		con_puts(CON_VERBOSE, "DXX-Rebirth: OpenGL renderer is blacklisted for GL intensity");
		//displays garbage. reported by
		//  redomen@crcwnet.com (render="Matrox G400" version="1.1.3 5.52.015")
		//  orulz (Matrox G200)
		CGameArg.DbgGlIntensity4Ok = 0;
	}
#ifdef macintosh
	if (d_stricmp(gl_renderer,"3dfx Voodoo 3")==0) // strangely, includes Voodoo 2
		CGameArg.DbgGlGetTexLevelParamOk = false;	// Always returns 0
#endif
	}

#ifndef NDEBUG
	con_printf(CON_VERBOSE, "DXX-Rebirth: OpenGL: gl_intensity4:%i gl_luminance4_alpha4:%i gl_rgba2:%i gl_readpixels:%i gl_gettexlevelparam:%i", CGameArg.DbgGlIntensity4Ok, CGameArg.DbgGlLuminance4Alpha4Ok, CGameArg.DbgGlRGBA2Ok, CGameArg.DbgGlReadPixelsOk, CGameArg.DbgGlGetTexLevelParamOk);
#endif
	if (ogl_maxanisotropy < 1.0f && CGameCfg.TexAnisotropy)
	{
		con_puts(CON_VERBOSE, "DXX-Rebirth: OpenGL: anisotropic texture filter not supported");
		CGameCfg.TexAnisotropy = false;
	}
#endif
}

#if SDL_MAJOR_VERSION == 1
// returns possible (fullscreen) resolutions if any.
uint_fast32_t gr_list_modes(std::array<screen_mode, 50> &gsmodes)
{
	int modesnum{0};
	constexpr Uint32 sdl_check_flags{ // always use Fullscreen as lead.
		DXX_USE_OGLES
			? SDL_FULLSCREEN
			: (SDL_OPENGL | SDL_FULLSCREEN)
	};

	if (sdl_no_modeswitch) {
		/* TODO: we could use the tvservice to list resolutions on the RPi */
		return 0;
	}

	SDL_Rect **const modes{SDL_ListModes(NULL, sdl_check_flags)};
	if (modes == nullptr) // check if we get any modes - if not, return 0
		return 0;
	if (modes == reinterpret_cast<SDL_Rect**>(-1))
	{
		return 0; // can obviously use any resolution... strange!
	}
	else
	{
		for (int i = 0; modes[i]; ++i)
		{
			if (modes[i]->w > 0xFFF0 || modes[i]->h > 0xFFF0 // resolutions saved in 32bits. so skip bigger ones (unrealistic in 2010) (changed to 0xFFF0 to fix warning)
				|| modes[i]->w < 320 || modes[i]->h < 200) // also skip everything smaller than 320x200
				continue;
			gsmodes[modesnum].width = modes[i]->w;
			gsmodes[modesnum].height = modes[i]->h;
			modesnum++;
			if (modesnum >= gsmodes.size()) // that really seems to be enough big boy.
				break;
		}
		return modesnum;
	}
}
#endif

}

namespace dsx {

#if SDL_MAJOR_VERSION == 1
static int gr_check_mode(const screen_mode mode)
{
	if (sdl_no_modeswitch == 0) {
		return SDL_VideoModeOK(SM_W(mode), SM_H(mode), CGameArg.DbgBpp, sdl_video_flags);
	} else {
		// just tell the caller that any mode is valid...
		return 32;
	}
}
#endif

int gr_set_mode(screen_mode mode)
{
	unsigned char *gr_bm_data;
#if SDL_MAJOR_VERSION == 1
	if (!gr_check_mode(mode))
	{
		con_printf(CON_URGENT, "Cannot set %ix%i. Fallback to 640x480", mode.width, mode.height);
		mode.width = 640;
		mode.height = 480;
		Game_screen_mode = mode;
	}
#endif
	const uint_fast32_t w = SM_W(mode), h = SM_H(mode);

	gr_bm_data = grd_curscreen->sc_canvas.cv_bitmap.get_bitmap_data();//since we use realloc, we want to keep this pointer around.
	auto gr_new_bm_data = reinterpret_cast<uint8_t *>(d_realloc(gr_bm_data, w * h));
	if (!gr_new_bm_data)
		return 0;
	*grd_curscreen = {};
	grd_curscreen->set_screen_width_height(w, h);
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->get_screen_width() * CGameCfg.AspectX, grd_curscreen->get_screen_height() * CGameCfg.AspectY);
	gr_init_canvas(grd_curscreen->sc_canvas, gr_new_bm_data, bm_mode::ogl, w, h);

	ogl_init_window(w,h);//platform specific code
	ogl_extensions_init();
	ogl_create_device();
	ogl_tune_for_current();
	sync_helper.init(CGameArg.OglSyncMethod, CGameArg.OglSyncWait);

	OGL_VIEWPORT(0,0,w,h);
	ogl_init_state();
	gamefont_choose_game_font(w,h);
	gr_remap_color_fonts();

#if DXX_USE_STEREOSCOPIC_RENDER
	gr_set_stereo_mode_sync();
#endif

	return 0;
}

#define GLstrcmptestr(a,b) if (d_stricmp(a,#b)==0 || d_stricmp(a,"GL_" #b)==0)return GL_ ## b;

#ifdef _WIN32
constexpr char OglLibPath[]="opengl32.dll";

static int ogl_rt_loaded=0;
static int ogl_init_load_library(void)
{
	int retcode{0};
	if (!ogl_rt_loaded)
	{
		retcode = OpenGL_LoadLibrary(true, OglLibPath);
		if(retcode)
		{
			if(!glEnd)
			{
				Error("Opengl: Functions not imported\n");
			}
		}
		else
		{
			Error("Opengl: error loading %s\n", OglLibPath);
		}
		ogl_rt_loaded=1;
	}
	return retcode;
}
#endif

void gr_set_attributes(void)
{
#if !DXX_USE_OGLES
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE,0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
#if SDL_MAJOR_VERSION == 1
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, CGameCfg.VSync);
#elif SDL_MAJOR_VERSION == 2
	SDL_GL_SetSwapInterval(CGameCfg.VSync ? 1 : 0);
#elif SDL_MAJOR_VERSION == 3
	/* SDL_GL_SetSwapInterval needs a current context.  Before gr_init
	 * made one, open_display sets the interval after it does
	 * (docs/agents/sdl-window.md section 2); the graphics menu comes here
	 * with one.
	 */
	if (main_display && main_display->gl && !SDL_GL_SetSwapInterval(CGameCfg.VSync ? 1 : 0))
		con_printf(CON_URGENT, "SDL3: cannot set the swap interval: %s", SDL_GetError());
#endif
	int buffers, samples;
	if (CGameCfg.Multisample)
	{
		buffers = 1;
		samples = 4;
	}
	else
	{
		buffers = samples = 0;
	}
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, buffers);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
#else
#if SDL_MAJOR_VERSION >= 2
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
#endif
	ogl_smash_texture_list_internal();
	gr_remap_color_fonts();
}

int gr_init()
{
	// Only do this function once!
	if (gr_installed==1)
		return -1;

#ifdef RPI
	// Initialize the broadcom host library
	// we have to call this before we can create an OpenGL ES context
	bcm_host_init();

	// Check if we are running with SDL directfb driver ...
	char sdl_driver[32];
	if (auto sdl_driver_ret = SDL_VideoDriverName(sdl_driver, sizeof(sdl_driver)))
	{
		if (strcmp(sdl_driver_ret,"x11")) {
			con_printf(CON_URGENT,"RPi: activating hack for console driver");
			sdl_no_modeswitch=1;
		}
	}
#endif

#ifdef _WIN32
	ogl_init_load_library();
#endif

	gr_set_attributes();

#if SDL_MAJOR_VERSION == 1
#if DXX_USE_DBG_HARNESS
	if (const auto hidden{read_harness_hidden()}; harness_hidden_requested(hidden))
		UserError("DXX_DBG_HIDDEN=\"%s\": needs an SDL 2 build", hidden.value);
#endif
	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_video_flags|=SDL_FULLSCREEN;

	if (CGameArg.SysNoBorders)
		sdl_video_flags|=SDL_NOFRAME;
#elif SDL_MAJOR_VERSION == 2
	assert(!g_pRebirthSDLMainWindow);
	unsigned sdl_window_flags = SDL_WINDOW_OPENGL;
	if (CGameArg.SysNoBorders)
		sdl_window_flags |= SDL_WINDOW_BORDERLESS;
	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#if defined(__APPLE__) && defined(__MACH__)
	sdl_window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
#if DXX_USE_DBG_HARNESS
	/* DXX_DBG_HIDDEN=1: nothing appears on screen.  The game draws into
	 * the hidden window's back buffer, which the frame hash reads back
	 * as it would a shown window's.
	 */
	if (const auto [value, window]{read_harness_hidden()}; !window)
		UserError("DXX_DBG_HIDDEN=\"%s\": expected 1", value);
	else if (window.value() == harness_window::hidden)
	{
		sdl_window_flags |= SDL_WINDOW_HIDDEN;
		con_puts(CON_NORMAL, "DXX_DBG_HIDDEN: the window stays hidden");
	}
#endif
	const auto mode{Game_screen_mode};
	const auto SDLWindow = SDL_CreateWindow(DESCENT_VERSION, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SM_W(mode), SM_H(mode), sdl_window_flags);
	if (!SDLWindow)
		return -1;
	SDL_GetWindowPosition(SDLWindow, &g_iRebirthWindowX, &g_iRebirthWindowY);
	g_pRebirthSDLMainWindow = SDLWindow;
	if (!SDL_GL_CreateContext(SDLWindow))
		Error("Failed to create SDL GL context: %s", SDL_GetError());
	if (const auto window_icon = SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP))
		SDL_SetWindowIcon(SDLWindow, window_icon);
#elif SDL_MAJOR_VERSION == 3
	assert(!g_pRebirthSDLMainWindow);
	const auto mode{Game_screen_mode};
	/* GL until #50 gives the game its SDL_GPU device. */
	sdl::display_request request{
		.title = DESCENT_VERSION,
		.size = {SM_W(mode), SM_H(mode)},
		.fullscreen = !CGameCfg.WindowMode && !CGameArg.SysWindow,
		.borderless = CGameArg.SysNoBorders,
		.vsync = CGameCfg.VSync,
	};
#if DXX_USE_DBG_HARNESS
	if (const auto [value, window]{read_harness_hidden()}; !window)
		UserError("DXX_DBG_HIDDEN=\"%s\": expected 1", value);
	else if (window.value() == harness_window::hidden)
	{
		request.hidden = true;
		con_puts(CON_NORMAL, "DXX_DBG_HIDDEN: the window stays hidden");
	}
#endif
	auto opened{sdl::open_display(request, sdl::sdl_display_ops{})};
	if (opened.fallback_reason)
		con_printf(CON_NORMAL, "SDL3: %s", opened.fallback_reason->c_str());
	if (!opened)
	{
		/* arch_init reports the failure; this says why. */
		con_printf(CON_URGENT, "SDL3: %s", opened.error->c_str());
		return -1;
	}
	if (opened.swap_interval_error)
		con_printf(CON_URGENT, "SDL3: cannot set the swap interval: %s", opened.swap_interval_error->c_str());
	g_pRebirthSDLMainWindow = opened.window.get();
	main_display = std::move(opened);
	if (const auto window_icon = SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP))
	{
		SDL_SetWindowIcon(g_pRebirthSDLMainWindow, window_icon);
		SDL_DestroySurface(window_icon);
	}
#endif

	ogl_init_texture_list_internal();

	grd_curscreen = std::make_unique<grs_screen>();
	*grd_curscreen = {};
	grd_curscreen->sc_canvas.cv_bitmap.bm_data = NULL;

	// Set the mode.
	grd_curscreen->sc_canvas.cv_fade_level = GR_FADE_OFF;
	grd_curscreen->sc_canvas.cv_font = NULL;
	grd_curscreen->sc_canvas.cv_font_fg_color = 0;
	grd_curscreen->sc_canvas.cv_font_bg_color = 0;
	gr_set_current_canvas(grd_curscreen->sc_canvas);

	ogl_init_pixel_buffers(256, 128);       // for gamefont_init

#if DXX_USE_STEREOSCOPIC_RENDER
	// stereo display options
	if (CGameArg.OglStereo)
		VR_stereo = StereoFormat::QuadBuffers;
	if (CGameArg.OglStereoView > static_cast<uint8_t>(StereoFormat::HighestFormat)) {
		VR_sync_param = CGameArg.OglStereoView;
		CGameArg.OglStereoView = static_cast<uint8_t>(StereoFormat::AboveBelowSync);
	}
	if (CGameArg.OglStereoView)
		VR_stereo = static_cast<StereoFormat>(CGameArg.OglStereoView);
	gr_set_stereo_mode_sync();
#endif

	gr_installed = 1;

	return 0;
}

void gr_close()
{
	ogl_brightness_r = ogl_brightness_g = ogl_brightness_b = 0;

	if (gl_initialized)
	{
		ogl_smash_texture_list_internal();
		sync_helper.deinit();
	}

	if (grd_curscreen)
	{
		if (grd_curscreen->sc_canvas.cv_bitmap.bm_mdata)
			d_free(grd_curscreen->sc_canvas.cv_bitmap.bm_mdata);
		/* Resetting grd_curscreen frees the default canvas, so set
		 * grd_curcanv to nullptr since no canvas is available after
		 * this block ends.
		 */
		grd_curcanv = nullptr;
		grd_curscreen.reset();
	}
	ogl_close_pixel_buffers();
#ifdef _WIN32
	if (ogl_rt_loaded)
		OpenGL_LoadLibrary(false, OglLibPath);
#endif

#if SDL_MAJOR_VERSION == 3
	g_pRebirthSDLMainWindow = nullptr;
	main_display.reset();
#endif
#if DXX_USE_OGLES && SDL_MAJOR_VERSION == 1
	ogles_destroy();
#ifdef RPI
	con_printf(CON_DEBUG, "RPi: cleanuing up");
	if (dispman_display != DISPMANX_NO_HANDLE) {
		rpi_destroy_element();
		con_printf(CON_DEBUG, "RPi: closing display");
		vc_dispmanx_display_close(dispman_display);
		dispman_display = DISPMANX_NO_HANDLE;
	}
#endif
#endif
}

}

namespace dcx {

void ogl_upixelc(const grs_bitmap &cv_bitmap, unsigned x, unsigned y, const color_palette_index c)
{
	const std::array<render::vertex, 1> vertices{{{
		.pos = {{(x + cv_bitmap.bm_x) / static_cast<float>(last_width), static_cast<GLfloat>(1.0 - (y + cv_bitmap.bm_y) / static_cast<float>(last_height)), 0}},
		.rgba = {{CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0}},
		.uv = {{0, 0}},
	}}};
	/* A point is linedotscale wide, as glPointSize set it before each. */
	auto call{ogl_ambient().call(render::topology::points, vertices)};
	call.state.line_width = linedotscale;
	ogl_device().draw(call);
}

color_palette_index ogl_ugpixel(const grs_bitmap &bitmap, unsigned x, unsigned y)
{
	std::array<uint8_t, 4> buf;
	ogl_device().read_pixels(render::read_source::drawing, render::ugpixel_rect(bitmap.bm_x, bitmap.bm_y, x, y), render::pixel_format::rgba8, buf);
	return gr_find_closest_color(buf[0]/4, buf[1]/4, buf[2]/4);
}

void ogl_urect(grs_canvas &canvas, const int left, const int top, const int right, const int bot, const color_palette_index c)
{
	const GLfloat xo{(left + canvas.cv_bitmap.bm_x) / static_cast<float>(last_width)};
	const GLfloat xf{(right + 1 + canvas.cv_bitmap.bm_x) / static_cast<float>(last_width)};
	const GLfloat yo{1.0f - (top + canvas.cv_bitmap.bm_y) / static_cast<float>(last_height)};
	const GLfloat yf{1.0f - (bot + 1 + canvas.cv_bitmap.bm_y) / static_cast<float>(last_height)};

	const auto color_r{CPAL2Tr(c)};
	const auto color_g{CPAL2Tg(c)};
	const auto color_b{CPAL2Tb(c)};

	const GLfloat color_a{
		(canvas.cv_fade_level >= GR_FADE_OFF)
			? 1.0f
			: 1.0f - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0f)
	};

	const std::array<float, 4> rgba{{color_r, color_g, color_b, color_a}};
	const std::array<render::vertex, 4> vertices{{
		{.pos = {{xo, yo, 0}}, .rgba = rgba, .uv = {{0, 0}}},
		{.pos = {{xo, yf, 0}}, .rgba = rgba, .uv = {{0, 0}}},
		{.pos = {{xf, yf, 0}}, .rgba = rgba, .uv = {{0, 0}}},
		{.pos = {{xf, yo, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	}};
	ogl_device().draw(ogl_ambient().call(render::topology::fan, vertices));//replaced GL_QUADS
}

void ogl_ulinec(grs_canvas &canvas, const int left, const int top, const int right, const int bot, const int c)
{
	GLfloat xo,yo,xf,yf;
	GLfloat fade_alpha = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0
		: 1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0);

	const std::array<float, 4> rgba{{CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), fade_alpha}};

	xo = (left + canvas.cv_bitmap.bm_x) / static_cast<float>(last_width);
	xf = (right + canvas.cv_bitmap.bm_x) / static_cast<float>(last_width);
	yo = 1.0 - (top + canvas.cv_bitmap.bm_y + 0.5) / static_cast<float>(last_height);
	yf = 1.0 - (bot + canvas.cv_bitmap.bm_y + 0.5) / static_cast<float>(last_height);

	const std::array<render::vertex, 2> vertices{{
		{.pos = {{xo, yo, 0}}, .rgba = rgba, .uv = {{0, 0}}},
		{.pos = {{xf, yf, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	}};
	ogl_device().draw(ogl_ambient().call(render::topology::lines, vertices));
}

static GLfloat last_r, last_g, last_b;
static int do_pal_step;

void ogl_do_palfx(void)
{
	if (const auto flash{render::flash_of(last_r, last_g, last_b, do_pal_step)})
		render::draw_palette_flash(ogl_device(), ogl_ambient(), *flash);
}

static int ogl_brightness_ok;
static int old_b_r, old_b_g, old_b_b;

inline int gr_apply_gamma_clamp(int v)
{
	if (v >= 0)
		return max(v + gr_palette_gamma, 0);
	else
		return min(v + gr_palette_gamma, 0);
}

void gr_palette_step_up(int r, int g, int b)
{
	old_b_r = ogl_brightness_r;
	old_b_g = ogl_brightness_g;
	old_b_b = ogl_brightness_b;

	ogl_brightness_r = gr_apply_gamma_clamp(r);
	ogl_brightness_g = gr_apply_gamma_clamp(g);
	ogl_brightness_b = gr_apply_gamma_clamp(b);

	if (!ogl_brightness_ok)
	{
		last_r = ogl_brightness_r / 63.0;
		last_g = ogl_brightness_g / 63.0;
		last_b = ogl_brightness_b / 63.0;

		do_pal_step = (r || g || b || gr_palette_gamma);
	}
	else
	{
		do_pal_step = 0;
	}
}

void gr_palette_load(const palette_array_t &pal)
{
	copy_bound_palette(gr_current_pal, pal);

	gr_palette_step_up(0, 0, 0); // make ogl_setbrightness_internal get run so that menus get brightened too.
	reset_computed_colors();
}

void gr_read_pixels(const render::read_source source, const render::read_rect &rect, const render::pixel_format format, const std::span<uint8_t> pixels)
{
	ogl_device().read_pixels(source, rect, format, pixels);
}

//writes out an uncompressed RGB .tga file
//if we got really spiffy, we could optionally link in libpng or something, and use that.
#if DXX_USE_SCREENSHOT_FORMAT_LEGACY
void write_bmp(PHYSFS_File *const TGAFile, const unsigned w, const unsigned h)
{
	struct TGA_header
	{
		unsigned char TGAheader[12];
		unsigned char header[6];
	};
	TGA_header TGA;
	GLbyte HeightH,HeightL,WidthH,WidthL;
	const unsigned buffer_size_TGA = w * h * 3;
	const auto buf = std::make_unique<uint8_t[]>(buffer_size_TGA);

	gr_read_pixels(render::read_source::presented, {0, 0, {w, h}}, render::pixel_format::rgb8, {buf.get(), buffer_size_TGA});
	/* TGA stores the bottom row first. */
	render::flip_rows({buf.get(), buffer_size_TGA}, w * 3);
	for (const auto pixel : xrange(w * h))
		std::swap(buf[pixel * 3], buf[pixel * 3 + 2]);

	HeightH = static_cast<GLbyte>(h / 256);
	HeightL = static_cast<GLbyte>(h % 256);
	WidthH  = static_cast<GLbyte>(w / 256);
	WidthL  = static_cast<GLbyte>(w % 256);
	// Write TGA Header
	TGA.TGAheader[0] = 0;
	TGA.TGAheader[1] = 0;
	TGA.TGAheader[2] = 2;
	TGA.TGAheader[3] = 0;
	TGA.TGAheader[4] = 0;
	TGA.TGAheader[5] = 0;
	TGA.TGAheader[6] = 0;
	TGA.TGAheader[7] = 0;
	TGA.TGAheader[8] = 0;
	TGA.TGAheader[9] = 0;
	TGA.TGAheader[10] = 0;
	TGA.TGAheader[11] = 0;
	TGA.header[0] = WidthL;
	TGA.header[1] = WidthH;
	TGA.header[2] = HeightL;
	TGA.header[3] = HeightH;
	TGA.header[4] = static_cast<GLbyte>(24);
	TGA.header[5] = 0;
	PHYSFSX_writeBytes(TGAFile, &TGA, sizeof(TGA_header));
	PHYSFSX_writeBytes(TGAFile, buf, buffer_size_TGA);
}
#endif

}

#if SDL_MAJOR_VERSION == 3
namespace dcx {

void gr_handle_window_event(const SDL_Event &event)
{
	const auto kind{sdl::window_event_of(event)};
	if (!kind || !main_display || event.window.windowID != SDL_GetWindowID(g_pRebirthSDLMainWindow))
		return;
	const sdl::pixel_size canvas{grd_curscreen ? sdl::pixel_size{grd_curscreen->get_screen_width(), grd_curscreen->get_screen_height()} : sdl::pixel_size{}};
	const auto reaction{main_display->state.handle(*kind, {event.window.data1, event.window.data2}, canvas)};
	if (reaction.fullscreen)
		CGameCfg.WindowMode = !*reaction.fullscreen;
	if (const auto size{reaction.resize})
	{
		dsx::gr_set_mode(screen_mode(size->width, size->height));
		event_send(d_window_size_event{size->width, size->height});
	}
}

visibility gr_window_visibility()
{
	return main_display ? main_display->state.vis() : visibility::visible;
}

}
#endif
