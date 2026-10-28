/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * Graphics support functions for OpenGL.
 *
 */

#include "dxxsconf.h"
#include <bit>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#include <windows.h>
#include <stddef.h>
#endif
#include "ogl_init.h"
#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#if DXX_USE_OGLES
#include <GLES/gl.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#endif
#endif
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "3d.h"
#include "piggy.h"
#include "common/3d/globvars.h"
#include "dxxerror.h"
#include "texmap.h"
#include "palette.h"
#include "rle.h"
#include "console.h"
#include "config.h"
#include "u_mem.h"

#include "segment.h"
#include "textures.h"
#include "texmerge.h"
#include "effects.h"
#include "weapon.h"
#include "powerup.h"
#include "laser.h"
#include "player.h"
#include "robot.h"
#include "gamefont.h"
#include "byteutil.h"
#include "internal.h"
#include "common/render/blit.h"
#include "common/render/frame_setup.h"
#include "common/render/gl_device.h"
#include "common/render/overlay_uv.h"
#include "common/render/palette_expand.h"
#include "common/render/texture_slots.h"
#include "gauges.h"
#include "object.h"
#include "args.h"

#include "compiler-range_for.h"
#include "d_levelstate.h"
#include "d_zip.h"
#include "partial_range.h"

#include <algorithm>
#include <memory>
#include <utility>
#if SDL_MAJOR_VERSION == 3
#include "common/sdl/display.h"
#endif
#if DXX_USE_DBG_HARNESS
#include <vector>
#include "common/debug/frame_hash_harness.h"
#include "common/debug/frame_hash_log.h"
#include "common/debug/input_script_harness.h"
#include "newdemo.h"
#endif
using std::max;

//change to 1 for lots of spew.
#if 0
#define glmprintf(A) con_printf A
#else
#define glmprintf(A)
#endif

#ifndef M_PI
#define M_PI 3.14159
#endif

namespace {

template <typename T, std::size_t N1, std::size_t N2>
union flatten_array
{
	std::array<T, N1 * N2> flat;
	std::array<std::array<T, N1>, N2> nested;
	static_assert(sizeof(flat) == sizeof(nested), "array padding error");
};

}

#if defined(_WIN32) || (defined(__APPLE__) && defined(__MACH__)) || defined(__sun__) || defined(macintosh)
#define cosf(a) cos(a)
#define sinf(a) sin(a)
#endif

namespace dcx {

static std::unique_ptr<GLubyte[]> texbuf;

unsigned last_width=~0u,last_height=~0u;
int GL_TEXTURE_2D_enabled=-1;

static int r_texcount = 0, r_cachedtexcount = 0;
#if DXX_USE_OGLES
static int ogl_rgba_internalformat = GL_RGBA;
static int ogl_rgb_internalformat = GL_RGB;
#else
static int ogl_rgba_internalformat = GL_RGBA8;
static int ogl_rgb_internalformat = GL_RGB8;
#endif
static std::unique_ptr<GLfloat[]> sphere_va, circle_va, disk_va;
/* The sides circle_va and disk_va were made with: later circles draw at
 * most that many of them.
 */
static unsigned circle_va_sides, disk_va_sides;
static std::array<std::unique_ptr<GLfloat[]>, 3> secondary_lva;
static int r_polyc,r_tpolyc,r_bitmapc,r_ubitbltc;
#define f2glf(x) (f2fl(x))

#define OGL_BINDTEXTURE(a) glBindTexture(GL_TEXTURE_2D, a);

/* I assume this ought to be >= MAX_BITMAP_FILES in piggy.h? */
static std::array<ogl_texture, 20000> ogl_texture_list;
static int ogl_texture_list_cur;
static std::unique_ptr<render::gl_device> ogl_device_instance;
/* The movie frame ogl_ubitblt_i is drawing, kept outside the list. */
static ogl_texture *ogl_movie_frame;

/* The render device of the current GL context, made by gr_set_mode. */
static render::gl_device &ogl_gl_device()
{
	if (!ogl_device_instance) [[unlikely]]
		throw std::logic_error("ogl_device: no render device before gr_set_mode");
	return *ogl_device_instance;
}

/* The render device's texture slot of t: its place in ogl_texture_list
 * plus one, or for the movie frame, which ogl_ubitblt_i keeps outside the
 * list, the slot after the list's.
 */
static render::texture_slot ogl_texture_slot(const ogl_texture &t)
{
	const std::less<const ogl_texture *> before;
	if (before(&t, ogl_texture_list.data()) || !before(&t, ogl_texture_list.data() + ogl_texture_list.size()))
	{
		if (&t != ogl_movie_frame)
			throw std::logic_error("ogl_texture_slot: a texture outside the texture list that is not the movie frame");
		return render::movie_frame_slot(ogl_texture_list.size());
	}
	return render::pool_slot(static_cast<std::size_t>(&t - ogl_texture_list.data()));
}

/* The texture of slot, as ogl_texture_slot numbers them; null for the
 * movie frame's slot while no movie frame is drawn, and for slots past it.
 */
static ogl_texture *ogl_texture_of(const render::texture_slot slot)
{
	const auto place{render::slot_place_of(slot, ogl_texture_list.size())};
	switch (place.owner)
	{
		case render::slot_owner::pool:
			return &ogl_texture_list[place.index];
		case render::slot_owner::movie_frame:
			return ogl_movie_frame;
		case render::slot_owner::none:
			break;
	}
	return nullptr;
}

/* Deletes the GL texture of t through the render device that made it. */
static void ogl_release_texture(ogl_texture &t)
{
	if (ogl_device_instance)
		ogl_device_instance->texture(ogl_texture_slot(t), nullptr);
	t.handle = 0;
}

/* Deletes every GL texture of the list, keeping the bitmaps' places. */
static void ogl_release_texture_list()
{
	range_for (auto &i, ogl_texture_list)
	{
		if (i.handle>0)
			ogl_release_texture(i);
		i.wrapstate = -1;
	}
}

/* some function prototypes */

#define GL_TEXTURE0_ARB 0x84C0
static int ogl_loadtexture(const palette_array_t &, const uint8_t *data, int dxo, int dyo, ogl_texture &tex, int bm_flags, opengl_texture_filter texfilt, bool texanis, bool edgepad) dxx_compiler_attribute_nonnull();
static void ogl_freetexture(ogl_texture &gltexture);

static void ogl_loadbmtexture(grs_bitmap &bm, bool edgepad)
{
	ogl_loadbmtexture_f(bm, CGameCfg.TexFilt, CGameCfg.TexAnisotropy, edgepad);
}

}

namespace dcx {

static void ogl_init_texture_stats(ogl_texture &t)
{
	t.prio=0.3;//default prio
	t.numrend=0;
}

void ogl_init_texture(ogl_texture &t, int w, int h, int flags)
{
	t.handle = 0;
#if !DXX_USE_OGLES
	if (flags & OGL_FLAG_NOCOLOR)
	{
		// use GL_INTENSITY instead of GL_RGB
		if (flags & OGL_FLAG_ALPHA)
		{
			if (CGameArg.DbgGlIntensity4Ok)
			{
				t.internalformat = GL_INTENSITY4;
				t.format = GL_LUMINANCE;
			}
			else if (CGameArg.DbgGlLuminance4Alpha4Ok)
			{
				t.internalformat = GL_LUMINANCE4_ALPHA4;
				t.format = GL_LUMINANCE_ALPHA;
			}
			else if (CGameArg.DbgGlRGBA2Ok)
			{
				t.internalformat = GL_RGBA2;
				t.format = GL_RGBA;
			}
			else
			{
				t.internalformat = ogl_rgba_internalformat;
				t.format = GL_RGBA;
			}
		}
		else
		{
			// there are certainly smaller formats we could use here, but nothing needs it ATM.
			t.internalformat = ogl_rgb_internalformat;
			t.format = GL_RGB;
		}
	}
	else
	{
#endif
		if (flags & OGL_FLAG_ALPHA)
		{
			t.internalformat = ogl_rgba_internalformat;
			t.format = GL_RGBA;
		}
		else
		{
			t.internalformat = ogl_rgb_internalformat;
			t.format = GL_RGB;
		}
#if !DXX_USE_OGLES
	}
#endif
	t.wrapstate = -1;
	t.lw = t.w = w;
	t.h = h;
	ogl_init_texture_stats(t);
}

static void ogl_reset_texture(ogl_texture &t)
{
	ogl_init_texture(t, 0, 0, 0);
}

static void ogl_reset_texture_stats_internal(void){
	range_for (auto &i, ogl_texture_list)
		if (i.handle>0)
			ogl_init_texture_stats(i);
}

void ogl_init_texture_list_internal(void){
	ogl_texture_list_cur=0;
	range_for (auto &i, ogl_texture_list)
		ogl_reset_texture(i);
}

void ogl_smash_texture_list_internal(void){
	sphere_va.reset();
	circle_va.reset();
	disk_va.reset();
	secondary_lva = {};
	ogl_release_texture_list();
	if (ogl_device_instance)
		if (const auto left{ogl_device_instance->texture_count()})
			con_printf(CON_URGENT, "OpenGL: %zu textures left after deleting them all", left);
}

ogl_texture* ogl_get_free_texture(void){
	for (unsigned i = ogl_texture_list.size(); i--;)
	{
		if (ogl_texture_list[ogl_texture_list_cur].handle<=0 && ogl_texture_list[ogl_texture_list_cur].w==0)
			return &ogl_texture_list[ogl_texture_list_cur];
		if (++ogl_texture_list_cur >= ogl_texture_list.size())
			ogl_texture_list_cur=0;
	}
	throw std::runtime_error("OpenGL: texture list full");
}

static void ogl_texture_stats(grs_canvas &canvas)
{
	int used{0}, usedother = 0, usedidx = 0, usedrgb = 0, usedrgba = 0;
	int databytes{0}, truebytes = 0;
	int res, colorsize, depthsize;
	range_for (auto &i, ogl_texture_list)
	{
		if (i.handle>0){
			used++;
			databytes+=i.bytesu;
			truebytes+=i.bytes;
			if (i.format == GL_RGBA)
				usedrgba++;
			else if (i.format == GL_RGB)
				usedrgb++;
#if !DXX_USE_OGLES
			else if (i.format == GL_COLOR_INDEX)
				usedidx++;
#endif
			else
				usedother++;
		}
	}

	res = SWIDTH * SHEIGHT;
	const auto bits{render::gl_device::framebuffer_bits()};
	const int idx{bits.index}, r{bits.red}, g{bits.green}, b{bits.blue}, a{bits.alpha}, dbl{bits.buffers}, depth{bits.depth};
	const auto &game_font = *GAME_FONT;
	gr_set_fontcolor(canvas, BM_XRGB(255, 255, 255), -1);
	colorsize = (idx * res * dbl) / 8;
	depthsize = res * depth / 8;
	const auto &&fspacx2 = FSPACX(2);
	const auto &&fspacy1 = FSPACY(1);
	const auto &&line_spacing = LINE_SPACING(game_font, game_font);
	gr_printf(canvas, game_font, fspacx2, fspacy1, "%i flat %i tex %i bitmaps", r_polyc, r_tpolyc, r_bitmapc);
	gr_printf(canvas, game_font, fspacx2, fspacy1 + line_spacing, "%i(%i,%i,%i,%i) %iK(%iK wasted) (%i postcachedtex)", used, usedrgba, usedrgb, usedidx, usedother, truebytes / 1024, (truebytes - databytes) / 1024, r_texcount - r_cachedtexcount);
	gr_printf(canvas, game_font, fspacx2, fspacy1 + (line_spacing * 2), "%ibpp(r%i,g%i,b%i,a%i)x%i=%iK depth%i=%iK", idx, r, g, b, a, dbl, colorsize / 1024, depth, depthsize / 1024);
	gr_printf(canvas, game_font, fspacx2, fspacy1 + (line_spacing * 3), "total=%iK", (colorsize + depthsize + truebytes) / 1024);
}

}

/* The texture of bm, uploaded if it holds none; the render device binds it
 * when a draw uses its slot.  nullptr when there is nothing to draw: only if
 * a game data file is missing is `gltexture` nullptr, or holds no GL texture
 * (a stale entry, as after ogl_smash_texture_list_internal); the device draws
 * no slot without a texture.
 */
[[nodiscard]]
static ogl_texture *ogl_loaded_bmtex(grs_bitmap &bm, const bool edgepad)
{
	if (bm.gltexture==NULL || bm.gltexture->handle<=0)
		ogl_loadbmtexture(bm, edgepad);
	const auto gltexture{bm.gltexture};
	if (!gltexture) [[unlikely]]
		return nullptr;
	gltexture->numrend++;
	return gltexture->handle ? gltexture : nullptr;
}

//gltexture MUST be bound first
static void ogl_texwrap(ogl_texture *const gltexture, const int state)
{
	if (gltexture->wrapstate != state || gltexture->numrend < 1)
	{
		gltexture->wrapstate = state;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state);
	}
}

//crude texture precaching
//handles: powerups, walls, weapons, polymodels, etc.
//it is done with the horrid do_special_effects kludge so that sides that have to be texmerged and have animated textures will be correctly cached.
//similarly, with the objects(esp weapons), we could just go through and cache em all instead, but that would get ones that might not even be on the level
//TODO: doors

namespace dsx {

void ogl_cache_polymodel_textures(const polygon_model_index model_num)
{
	auto &Polygon_models = LevelSharedPolygonModelState.Polygon_models;
	if (model_num == polygon_model_index::None)
		return;
	const auto &po = Polygon_models[model_num];
	unsigned i = po.first_texture;
	const unsigned last_texture = i + po.n_textures;
	for (; i != last_texture; ++i)
	{
		const auto objbitmap = ObjBitmaps[ObjBitmapPtrs[i]];
		PIGGY_PAGE_IN(objbitmap);
		ogl_loadbmtexture(GameBitmaps[objbitmap], 1);
	}
}

}

static void ogl_cache_vclip_textures(const vclip &vc)
{
	for (const auto i : partial_const_range(vc.frames, vc.num_frames))
	{
		PIGGY_PAGE_IN(i);
		ogl_loadbmtexture(GameBitmaps[i], 0);
	}
}

static void ogl_cache_vclipn_textures(const d_vclip_array &Vclip, const vclip_index i)
{
	if (Vclip.valid_index(i))
		ogl_cache_vclip_textures(Vclip[i]);
}

static void ogl_cache_weapon_textures(const d_vclip_array &Vclip, const weapon_info_array &Weapon_info, const weapon_id_type weapon_type)
{
	if (weapon_type >= Weapon_info.size())
		return;
	const auto &w = Weapon_info[weapon_type];
	ogl_cache_vclipn_textures(Vclip, w.flash_vclip);
	ogl_cache_vclipn_textures(Vclip, w.robot_hit_vclip);
	ogl_cache_vclipn_textures(Vclip, w.wall_hit_vclip);
	if (w.render == weapon_info::render_type::vclip)
		ogl_cache_vclipn_textures(Vclip, w.weapon_vclip);
	else if (w.render == weapon_info::render_type::polymodel)
	{
		ogl_cache_polymodel_textures(w.model_num);
		ogl_cache_polymodel_textures(w.model_num_inner);
	}
}

namespace dsx {

void ogl_cache_level_textures(void)
{
	auto &Effects = LevelUniqueEffectsClipState.Effects;
	auto &Objects = LevelUniqueObjectState.Objects;
	auto &vcobjptridx = Objects.vcptridx;
	int max_efx{0},ef;
	
	ogl_reset_texture_stats_internal();//loading a new lev should reset textures
	
	range_for (auto &ec, partial_const_range(Effects, Num_effects))
	{
		ogl_cache_vclipn_textures(Vclip, ec.dest_vclip);
		if (ec.changing_wall_texture == texture_index{UINT16_MAX} && ec.changing_object_texture.dsx == object_bitmap_index::None)
			continue;
		if (ec.vc.num_frames>max_efx)
			max_efx=ec.vc.num_frames;
	}
	glmprintf((CON_DEBUG, "max_efx:%i", max_efx));
	for (ef=0;ef<max_efx;ef++){
		range_for (eclip &ec, partial_range(Effects, Num_effects))
		{
			if (ec.changing_wall_texture == texture_index{UINT16_MAX} && ec.changing_object_texture.dsx == object_bitmap_index::None)
				continue;
			ec.time_left=-1;
		}
		do_special_effects();

		range_for (const unique_segment &seg, vcsegptr)
		{
			range_for (auto &side, seg.sides)
			{
				const auto tmap1 = side.tmap_num;
				const auto tmap2 = side.tmap_num2;
				const auto tmap1idx = get_texture_index(tmap1);
				if (tmap1idx >= NumTextures){
					glmprintf((CON_DEBUG, "ogl_cache_level_textures %p %p %i %i", seg.get_unchecked_pointer(), &side, tmap1, NumTextures));
					//				tmap1=0;
					continue;
				}
				if (tmap1idx >= Textures.size()) [[unlikely]]
					/* This should be impossible, since
					 * `NumTextures <= Textures.size()` should always be true.
					 */
					continue;
				const auto texture1{Textures[tmap1idx]};
				if (!GameBitmaps.valid_index(texture1)) [[unlikely]]
					continue;
				PIGGY_PAGE_IN(texture1);
				grs_bitmap *bm = &GameBitmaps[texture1];
				if (tmap2 != texture2_value::None)
				{
					const auto tmap2idx{get_texture_index(tmap2)};
					if (tmap2idx >= Textures.size()) [[unlikely]]
						continue;
					const auto texture2{Textures[tmap2idx]};
					if (!GameBitmaps.valid_index(texture2))
						continue;
					PIGGY_PAGE_IN(texture2);
					auto &bm2 = GameBitmaps[texture2];
					if (CGameArg.DbgUseOldTextureMerge || bm2.get_flag_mask(BM_FLAG_SUPER_TRANSPARENT))
						bm = &texmerge_get_cached_bitmap(GameBitmaps, Textures, tmap1, tmap2);
					else {
						ogl_loadbmtexture(bm2, 1);
					}
				}
				ogl_loadbmtexture(*bm, 0);
			}
		}
		glmprintf((CON_DEBUG, "finished ef:%i", ef));
	}
	reset_special_effects();
	init_special_effects();
	{
		auto &Robot_info = LevelSharedRobotInfoState.Robot_info;
		// always have lasers, concs, flares.  Always shows player appearance, and at least concs are always available to disappear.
		ogl_cache_weapon_textures(Vclip, Weapon_info, Primary_weapon_to_weapon_info[primary_weapon_index::laser]);
		ogl_cache_weapon_textures(Vclip, Weapon_info, Secondary_weapon_to_weapon_info[secondary_weapon_index::concussion]);
		ogl_cache_weapon_textures(Vclip, Weapon_info, weapon_id_type::FLARE_ID);
		ogl_cache_vclipn_textures(Vclip, vclip_index::player_appearance);
		ogl_cache_vclipn_textures(Vclip, vclip_index::powerup_disappearance);
		ogl_cache_polymodel_textures(Player_ship->model_num.dsx);
		ogl_cache_vclipn_textures(Vclip, Player_ship->expl_vclip_num);

		range_for (const auto &&objp, vcobjptridx)
		{
			if (objp->type == object_type::OBJ_POWERUP && objp->render_type == render_type::RT_POWERUP)
			{
				ogl_cache_vclipn_textures(Vclip, objp->rtype.vclip_info.vclip_num);
				const auto id = get_powerup_id(objp);
				primary_weapon_index p;
				secondary_weapon_index s;
				weapon_id_type w;
				if (
					(
						(
							(id == powerup_type_t::POW_VULCAN_WEAPON && (p = primary_weapon_index::vulcan, true)) ||
							(id == powerup_type_t::POW_SPREADFIRE_WEAPON && (p = primary_weapon_index::spreadfire, true)) ||
							(id == powerup_type_t::POW_PLASMA_WEAPON && (p = primary_weapon_index::plasma, true)) ||
							(id == powerup_type_t::POW_FUSION_WEAPON && (p = primary_weapon_index::fusion, true))
						) && (w = Primary_weapon_to_weapon_info[p], true)
					) ||
					(
						(
							(id == powerup_type_t::POW_PROXIMITY_WEAPON && (s = secondary_weapon_index::proximity, true)) ||
							((id == powerup_type_t::POW_HOMING_AMMO_1 || id == powerup_type_t::POW_HOMING_AMMO_4) && (s = secondary_weapon_index::homing, true)) ||
							(id == powerup_type_t::POW_SMARTBOMB_WEAPON && (s = secondary_weapon_index::smart, true)) ||
							(id == powerup_type_t::POW_MEGA_WEAPON && (s = secondary_weapon_index::mega, true))
						) && (w = Secondary_weapon_to_weapon_info[s], true)
					)
				)
				{
					ogl_cache_weapon_textures(Vclip, Weapon_info, w);
				}
			}
			else if (objp->type != object_type::OBJ_NONE && objp->render_type == render_type::RT_POLYOBJ)
			{
				if (objp->type == object_type::OBJ_ROBOT)
				{
					auto &ri = Robot_info[get_robot_id(objp)];
					ogl_cache_vclipn_textures(Vclip, ri.exp1_vclip_num);
					ogl_cache_vclipn_textures(Vclip, ri.exp2_vclip_num);
					ogl_cache_weapon_textures(Vclip, Weapon_info, ri.weapon_type);
				}
				if (const auto tmap_override{objp->rtype.pobj_info.tmap_override}; tmap_override < Textures.size())
				{
					const auto t{Textures[tmap_override]};
					if (!GameBitmaps.valid_index(t)) [[unlikely]]
						continue;
					PIGGY_PAGE_IN(t);
					ogl_loadbmtexture(GameBitmaps[t], 1);
				}
				else if (tmap_override == texture_index{UINT16_MAX}) [[likely]]
					ogl_cache_polymodel_textures(objp->rtype.pobj_info.model_num.dsx);
			}
		}
	}
	glmprintf((CON_DEBUG, "finished caching"));
	r_cachedtexcount = r_texcount;
}

}

namespace dcx {

void g3_draw_line(const g3_draw_line_context &context, const g3_draw_line_point &p0, const g3_draw_line_point &p1)
{
	ogl_ambient().cull = render::cull::none;
	const auto &c{context.color_array};
	const std::array<render::vertex, 2> vertices{{
		{.pos = {{f2glf(p0.p3_vec.x), f2glf(p0.p3_vec.y), -f2glf(p0.p3_vec.z)}}, .rgba = {{c[0], c[1], c[2], c[3]}}, .uv = {{0, 0}}},
		{.pos = {{f2glf(p1.p3_vec.x), f2glf(p1.p3_vec.y), -f2glf(p1.p3_vec.z)}}, .rgba = {{c[4], c[5], c[6], c[7]}}, .uv = {{0, 0}}},
	}};
	/* In the ambient line width: GL's, which glDrawArrays drew with before
	 * the render device.
	 */
	ogl_device().draw(ogl_ambient().call(render::topology::lines, vertices));
}

namespace {

static std::unique_ptr<GLfloat[]> circle_array_init(const unsigned nsides)
{
	auto vertices = std::make_unique_for_overwrite<GLfloat[]>(nsides * 2);
	for (unsigned i = 0; i < nsides; i++)
	{
		const float ang = 2.0 * M_PI * i / nsides;
		vertices[i * 2] = cosf(ang);
		vertices[i * 2 + 1] = sinf(ang);
	}
	return vertices;
}

static std::unique_ptr<GLfloat[]> circle_array_init_2(const unsigned nsides, const float xsc, const float xo, const float ysc, const float yo)
{
	auto vertices = std::make_unique_for_overwrite<GLfloat[]>(nsides * 2);
	for (unsigned i = 0; i < nsides; i++)
	{
		const float ang = 2.0 * M_PI * i / nsides;
		vertices[i * 2] = cosf(ang) * xsc + xo;
		vertices[i * 2 + 1] = sinf(ang) * ysc + yo;
	}
	return vertices;
}

/* The vertices of a flat 2D primitive: x, y pairs at z 0 with four colour
 * floats each, as glVertexPointer(2, ...) and glColorPointer(4, ...) took
 * them before the render device.
 */
template <std::size_t N>
static std::array<render::vertex, N> ogl_vertices_2d(const std::span<const GLfloat, 2 * N> xy, const std::span<const GLfloat, 4 * N> rgba)
{
	std::array<render::vertex, N> vertices;
	for (std::size_t i{0}; i != N; ++i)
		vertices[i] = {.pos = {{xy[2 * i], xy[2 * i + 1], 0}}, .rgba = {{rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2], rgba[4 * i + 3]}}, .uv = {{0, 0}}};
	return vertices;
}

/* The vertices of a circle of sides from xy (x, y pairs) in one colour, as
 * glColor4f gave them before the render device.
 */
static std::vector<render::vertex> ogl_circle_vertices(const std::span<const GLfloat> xy, const std::array<float, 4> &rgba)
{
	std::vector<render::vertex> vertices;
	vertices.reserve(xy.size() / 2);
	for (std::size_t i{0}; i + 1 < xy.size(); i += 2)
		vertices.push_back({.pos = {{xy[i], xy[i + 1], 0}}, .rgba = rgba, .uv = {{0, 0}}});
	return vertices;
}

/* Draws vertices as t through the render device in the ambient state,
 * moved by glTranslatef(translate) and glScalef(scale) from the identity
 * modelview, with line_width for lines.  The next draw call carries the
 * ambient modelview and the state shadow sets it again; nothing outside the
 * device reads GL's matrices.
 */
static void ogl_draw_moved(const render::topology t, const std::span<const render::vertex> vertices, const std::array<float, 3> &translate, const std::array<float, 3> &scale, const float line_width)
{
	auto call{ogl_ambient().call(t, vertices)};
	call.transforms.modelview = render::translate_scale{.translate = translate, .scale = scale};
	call.state.line_width = line_width;
	ogl_device().draw(call);
}

/* Where the vertex reticle draws: its modelview recipe and the width of
 * its lines.
 */
struct reticle_place
{
	std::array<float, 3> translate;
	std::array<float, 3> scale;
	float line_width;
};

/* Draws N vertices of the reticle as t: x, y pairs from xy and four colour
 * floats each from rgba.  Throws std::length_error when either holds fewer.
 */
template <std::size_t N>
static void ogl_draw_reticle_part(const reticle_place &place, const render::topology t, const std::span<const GLfloat> xy, const std::span<const GLfloat> rgba)
{
	if (xy.size() < 2 * N || rgba.size() < 4 * N) [[unlikely]]
		throw std::length_error("ogl_draw_reticle_part: fewer coordinates than vertices");
	const auto vertices{ogl_vertices_2d<N>(xy.first<2 * N>(), rgba.first<4 * N>())};
	ogl_draw_moved(t, vertices, place.translate, place.scale, place.line_width);
}

}

void ogl_draw_vertex_reticle(grs_canvas &canvas, int cross, int primary, int secondary, int color, int alpha, int size_offs)
{
	int size=270+(size_offs*20);
	float scale = (static_cast<float>(SWIDTH)/SHEIGHT);
	auto &&rgb{PAL2T(color)};
	const std::array<float, 4> ret_rgba{{
		rgb.r / 63.0f,
		rgb.g / 63.0f,
		rgb.b / 63.0f,
		float{1.0f - (static_cast<float>(alpha) / (static_cast<float>(GR_FADE_LEVELS)))}
	}}, ret_dark_rgba{{
		ret_rgba[0] / 2,
		ret_rgba[1] / 2,
		ret_rgba[2] / 2,
		ret_rgba[3] / 2
	}};
	std::array<GLfloat, 16 * 4> dark_lca, bright_lca;
	for (uint_fast32_t i = 0; i != dark_lca.size(); i += 4)
	{
		bright_lca[i] = ret_rgba[0];
		dark_lca[i] = ret_dark_rgba[0];
		bright_lca[i+1] = ret_rgba[1];
		dark_lca[i+1] = ret_dark_rgba[1];
		bright_lca[i+2] = ret_rgba[2];
		dark_lca[i+2] = ret_dark_rgba[2];
		bright_lca[i+3] = ret_rgba[3];
		dark_lca[i+3] = ret_dark_rgba[3];
	}

	const std::array<float, 3> translate{{(canvas.cv_bitmap.bm_w / 2 + canvas.cv_bitmap.bm_x) / static_cast<float>(last_width), static_cast<float>(1.0 - (canvas.cv_bitmap.bm_h / 2 + canvas.cv_bitmap.bm_y) / static_cast<float>(last_height)), 0}};
	std::array<float, 3> scale_xyz;
	{
		float gl1, gl2, gl3;
	if (scale >= 1)
	{
		size/=scale;
		gl2 = f2glf(size*scale);
		gl1 = f2glf(size);
		gl3 = gl1;
	}
	else
	{
		size*=scale;
		gl1 = f2glf(size/scale);
		gl2 = f2glf(size);
		gl3 = gl2;
	}
		scale_xyz = {{gl1, gl2, gl3}};
	}

	ogl_ambient().cull = render::cull::none;
	const reticle_place place{.translate = translate, .scale = scale_xyz, .line_width = linedotscale * 2.f};
	
	//cross
	std::array<GLfloat, 8 * 4> cross_lca;
	std::span<const GLfloat> cross_lca_span;
	if (cross)
	{
		for (uint_fast32_t i = 0; i != cross_lca.size(); i += 8)
		{
			cross_lca[i] = ret_dark_rgba[0];
			cross_lca[i+1] = ret_dark_rgba[1];
			cross_lca[i+2] = ret_dark_rgba[2];
			cross_lca[i+3] = ret_dark_rgba[3];
			cross_lca[i+4] = ret_rgba[0];
			cross_lca[i+5] = ret_rgba[1];
			cross_lca[i+6] = ret_rgba[2];
			cross_lca[i+7] = ret_rgba[3];
		}
		cross_lca_span = cross_lca;
	}
	else
	{
		cross_lca_span = dark_lca;
	}
	static const std::array<GLfloat, 8 * 2> cross_lva{{
		-4.0, 2.0, -2.0, 0, -3.0, -4.0, -2.0, -3.0, 4.0, 2.0, 2.0, 0, 3.0, -4.0, 2.0, -3.0,
	}};
	ogl_draw_reticle_part<8>(place, render::topology::lines, cross_lva, cross_lca_span);
	
	std::array<GLfloat, 4 * 4> primary_lca0;
	std::span<const GLfloat> lca0_data;
	//left primary bar
	if(primary == 0)
		lca0_data = dark_lca;
	else
	{
		primary_lca0[0] = primary_lca0[4] = ret_rgba[0];
		primary_lca0[1] = primary_lca0[5] = ret_rgba[1];
		primary_lca0[2] = primary_lca0[6] = ret_rgba[2];
		primary_lca0[3] = primary_lca0[7] = ret_rgba[3];
		primary_lca0[8] = primary_lca0[12] = ret_dark_rgba[0];
		primary_lca0[9] = primary_lca0[13] = ret_dark_rgba[1];
		primary_lca0[10] = primary_lca0[14] = ret_dark_rgba[2];
		primary_lca0[11] = primary_lca0[15] = ret_dark_rgba[3];
		lca0_data = primary_lca0;
	}
	static const std::array<GLfloat, 4 * 2> primary_lva0{{
		-5.5, -5.0, -6.5, -7.5, -10.0, -7.0, -10.0, -8.7
	}};
	static const std::array<GLfloat, 4 * 2> primary_lva1{{
		-10.0, -7.0, -10.0, -8.7, -15.0, -8.5, -15.0, -9.5
	}};
	static const std::array<GLfloat, 4 * 2> primary_lva2{{
		5.5, -5.0, 6.5, -7.5, 10.0, -7.0, 10.0, -8.7
	}};
	static const std::array<GLfloat, 4 * 2> primary_lva3{{
		10.0, -7.0, 10.0, -8.7, 15.0, -8.5, 15.0, -9.5
	}};
	ogl_draw_reticle_part<4>(place, render::topology::strip, primary_lva0, lca0_data);

	std::array<GLfloat, 4 * 4> primary_lca1;
	std::span<const GLfloat> lca1_data;
	if(primary != 2)
		lca1_data = dark_lca;
	else
	{
		primary_lca1[8] = primary_lca1[12] = ret_rgba[0];
		primary_lca1[9] = primary_lca1[13] = ret_rgba[1];
		primary_lca1[10] = primary_lca1[14] = ret_rgba[2];
		primary_lca1[11] = primary_lca1[15] = ret_rgba[3];
		primary_lca1[0] = primary_lca1[4] = ret_dark_rgba[0];
		primary_lca1[1] = primary_lca1[5] = ret_dark_rgba[1];
		primary_lca1[2] = primary_lca1[6] = ret_dark_rgba[2];
		primary_lca1[3] = primary_lca1[7] = ret_dark_rgba[3];
		lca1_data = primary_lca1;
	}
	ogl_draw_reticle_part<4>(place, render::topology::strip, primary_lva1, lca1_data);
	//right primary bar
	ogl_draw_reticle_part<4>(place, render::topology::strip, primary_lva2, lca0_data);
	ogl_draw_reticle_part<4>(place, render::topology::strip, primary_lva3, lca1_data);
	
	/* Each secondary circle holds 16 sides (circle_array_init_2). */
	const auto secondary_circle{[&place](const GLfloat *const xy, const std::span<const GLfloat> rgba) {
		ogl_draw_reticle_part<16>(place, render::topology::line_loop, std::span<const GLfloat>(xy, 16 * 2), rgba);
	}};
	if (secondary<=2){
		//left secondary
		if(!secondary_lva[0])
			secondary_lva[0] = circle_array_init_2(16, 2.0, -10.0, 2.0, -2.0);
		secondary_circle(secondary_lva[0].get(), secondary != 1 ? dark_lca : bright_lca);
		//right secondary
		if(!secondary_lva[1])
			secondary_lva[1] = circle_array_init_2(16, 2.0, 10.0, 2.0, -2.0);
		secondary_circle(secondary_lva[1].get(), secondary != 2 ? dark_lca : bright_lca);
	}
	else {
		//bottom/middle secondary
		if(!secondary_lva[2])
			secondary_lva[2] = circle_array_init_2(16, 2.0, 0.0, 2.0, -8.0);
		secondary_circle(secondary_lva[2].get(), secondary != 4 ? dark_lca : bright_lca);
	}
}

/*
 * Stars on heaven in exit sequence, automap objects
 */
void g3_draw_sphere(grs_canvas &canvas, const g3_draw_sphere_point &pnt, fix rad, const uint8_t c)
{
	const float scale = (static_cast<float>(canvas.cv_bitmap.bm_w) / canvas.cv_bitmap.bm_h);
	const std::array<float, 4> rgba{{CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), 1.0}};
	ogl_ambient().cull = render::cull::none;
	const std::array<float, 3> translate{{f2glf(pnt.p3_vec.x), f2glf(pnt.p3_vec.y), -f2glf(pnt.p3_vec.z)}};
	GLfloat gl1, gl2;
	if (scale >= 1)
	{
		rad/=scale;
		gl1 = f2glf(rad);
		gl2 = f2glf(rad*scale);
	}
	else
	{
		rad*=scale;
		gl1 = f2glf(rad/scale);
		gl2 = f2glf(rad);
	}
	const std::array<float, 3> scale_xyz{{gl1, gl2, f2glf(rad)}};
	if(!sphere_va)
		sphere_va = circle_array_init(20);
	const auto vertices{ogl_circle_vertices(std::span<const GLfloat>(sphere_va.get(), 20 * 2), rgba)};
	ogl_draw_moved(render::topology::fan, vertices, translate, scale_xyz, ogl_ambient().line_width);
}

int gr_ucircle(grs_canvas &canvas, const fix xc1, const fix yc1, const fix r1, const uint8_t c)
{
	int nsides;
	const std::array<float, 4> rgba{{CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), static_cast<float>((canvas.cv_fade_level >= GR_FADE_OFF)?1.0:1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0))}};
	const std::array<float, 3> translate{{
	             static_cast<float>((f2fl(xc1) + canvas.cv_bitmap.bm_x + 0.5) / static_cast<float>(last_width)),
	             static_cast<float>(1.0 - (f2fl(yc1) + canvas.cv_bitmap.bm_y + 0.5) / static_cast<float>(last_height)),0}};
	const std::array<float, 3> scale{{f2fl(r1) / last_width, f2fl(r1) / last_height, 1.0}};
	nsides = 10 + 2 * static_cast<int>(M_PI * f2fl(r1) / 19);
	if(!circle_va)
	{
		circle_va = circle_array_init(nsides);
		circle_va_sides = nsides;
	}
	const auto vertices{ogl_circle_vertices(std::span<const GLfloat>(circle_va.get(), 2 * std::min<unsigned>(nsides, circle_va_sides)), rgba)};
	ogl_draw_moved(render::topology::line_loop, vertices, translate, scale, ogl_ambient().line_width);
	return 0;
}

int gr_disk(grs_canvas &canvas, const fix x, const fix y, const fix r, const uint8_t c)
{
	int nsides;
	const std::array<float, 4> rgba{{CPAL2Tr(c), CPAL2Tg(c), CPAL2Tb(c), static_cast<float>((canvas.cv_fade_level >= GR_FADE_OFF) ? 1.0 : 1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0))}};
	const std::array<float, 3> translate{{
	             static_cast<float>((f2fl(x) + canvas.cv_bitmap.bm_x + 0.5) / static_cast<float>(last_width)),
	             static_cast<float>(1.0 - (f2fl(y) + canvas.cv_bitmap.bm_y + 0.5) / static_cast<float>(last_height)),0}};
	const std::array<float, 3> scale{{f2fl(r) / last_width, f2fl(r) / last_height, 1.0}};
	nsides = 10 + 2 * static_cast<int>(M_PI * f2fl(r) / 19);
	if(!disk_va)
	{
		disk_va = circle_array_init(nsides);
		disk_va_sides = nsides;
	}
	const auto vertices{ogl_circle_vertices(std::span<const GLfloat>(disk_va.get(), 2 * std::min<unsigned>(nsides, disk_va_sides)), rgba)};
	ogl_draw_moved(render::topology::fan, vertices, translate, scale, ogl_ambient().line_width);
	return 0;
}

/*
 * Draw flat-shaded Polygon (Lasers, Drone-arms, Driller-ears)
 */
void _g3_draw_poly(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const uint8_t palette_color_index)
{
	if (pointlist.size() > MAX_POINTS_PER_POLY)
		return;
	std::array<render::vertex, MAX_POINTS_PER_POLY> vertices;

	r_polyc++;
	auto &&rgb{PAL2T(palette_color_index)};
	const auto color_r{rgb.r / 63.0f};
	const auto color_g{rgb.g / 63.0f};
	const auto color_b{rgb.b / 63.0f};

	const float color_a = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0
		: 1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0);

	for (auto &&[p, v] : zip(pointlist, unchecked_partial_range(vertices, pointlist.size())))
	{
		auto &pv = p->p3_vec;
		v = {{{f2glf(pv.x), f2glf(pv.y), -f2glf(pv.z)}}, {{color_r, color_g, color_b, color_a}}, {}};
	}
	ogl_device().draw(ogl_ambient().fan(std::span(vertices).first(pointlist.size()), render::texture_slot::none));
}

/*
 * Everything texturemapped (walls, robots, ship)
 */ 
void _g3_draw_tmap(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const g3s_uvl *const uvl_list, const g3s_lrgb *const light_rgb, grs_bitmap &bm, const tmap_drawer_type tmap_drawer_ptr)
{
	GLfloat color_alpha = 1.0;
	render::texture_slot texture{render::texture_slot::none};
	render::wrap wrap{render::wrap::clamp};

	if (tmap_drawer_ptr == draw_tmap) {
		const auto gltexture{ogl_loaded_bmtex(bm, 0)};
		if (!gltexture) [[unlikely]]
			return;
		texture = ogl_texture_slot(*gltexture);
		wrap = render::wrap::repeat;
		r_tpolyc++;
		color_alpha = (canvas.cv_fade_level >= GR_FADE_OFF) ? 1.0 : (1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0));
	} else if (tmap_drawer_ptr == draw_tmap_flat) {
		/* for cloaked state faces */
		color_alpha = 1.0 - (static_cast<GLfloat>(canvas.cv_fade_level) / static_cast<GLfloat>(NUM_LIGHTING_LEVELS));
	} else {
		glmprintf((CON_DEBUG, "g3_draw_tmap: unhandled tmap_drawer"));
		return;
	}

	std::array<render::vertex, MAX_POINTS_PER_POLY> vertices;

	const auto nv = pointlist.size();
	for (auto &&[point, light, uvl, v] : zip(
			pointlist,
			unchecked_partial_range(light_rgb, nv),
			unchecked_partial_range(uvl_list, nv),
			partial_range(vertices, nv)
		)
	)
	{
		v.pos = {{f2glf(point->p3_vec.x), f2glf(point->p3_vec.y), -f2glf(point->p3_vec.z)}};
		if (tmap_drawer_ptr == draw_tmap_flat) {
			v.rgba = {{0, 0, 0, color_alpha}};
			v.uv = {};
		}
		else
		{
			if (bm.get_flag_mask(BM_FLAG_NO_LIGHTING))
				v.rgba = {{1.0, 1.0, 1.0, color_alpha}};
			else
				v.rgba = {{f2glf(light.r), f2glf(light.g), f2glf(light.b), color_alpha}};
			v.uv = {{f2glf(uvl.u), f2glf(uvl.v)}};
		}
	}
	ogl_device().draw(ogl_ambient().fan(std::span(vertices).first(nv), texture, wrap));
}

/*
 * Everything texturemapped with secondary texture (walls with secondary texture)
 */
void _g3_draw_tmap_2(grs_canvas &canvas, const std::span<g3_draw_tmap_point *const> pointlist, const std::span<const g3s_uvl, 4> uvl_list, const std::span<const g3s_lrgb, 4> light_rgb, grs_bitmap &bmbot, grs_bitmap &bm, const texture2_rotation_low orient, const tmap_drawer_type tmap_drawer_ptr)
{
	_g3_draw_tmap(canvas, pointlist, uvl_list.data(), light_rgb.data(), bmbot, tmap_drawer_ptr);//draw the bottom texture first.. could be optimized with multitexturing..
	r_tpolyc++;
	const auto gltexture{ogl_loaded_bmtex(bm, 1)};
	if (!gltexture) [[unlikely]]
		return;

	const GLfloat alpha = (canvas.cv_fade_level >= GR_FADE_OFF)
		? 1.0
		: (1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0));
	const bool no_lighting{bm.get_flag_mask(BM_FLAG_NO_LIGHTING) != 0};
	/* The cast below relies on both enums numbering the turns alike. */
	static_assert(std::to_underlying(texture2_rotation_low::Normal) == std::to_underlying(render::uv_rotation::none));
	static_assert(std::to_underlying(texture2_rotation_low::_1) == std::to_underlying(render::uv_rotation::quarter));
	static_assert(std::to_underlying(texture2_rotation_low::_2) == std::to_underlying(render::uv_rotation::half));
	static_assert(std::to_underlying(texture2_rotation_low::_3) == std::to_underlying(render::uv_rotation::three_quarters));
	const auto rotation{static_cast<render::uv_rotation>(std::to_underlying(orient))};
	std::array<render::vertex, MAX_POINTS_PER_POLY> vertices;

	const auto nv = pointlist.size();
	for (auto &&[point, uvl, light, v] : zip(
			pointlist,
			unchecked_partial_range(uvl_list, nv),
			unchecked_partial_range(light_rgb, nv),
			partial_range(vertices, nv)
		)
	)
	{
		v.pos = {{f2glf(point->p3_vec.x), f2glf(point->p3_vec.y), -f2glf(point->p3_vec.z)}};
		if (no_lighting)
			v.rgba = {{1.0, 1.0, 1.0, alpha}};
		else
			v.rgba = {{f2glf(light.r), f2glf(light.g), f2glf(light.b), alpha}};
		v.uv = render::overlay_uv({{f2glf(uvl.u), f2glf(uvl.v)}}, rotation);
	}
	ogl_device().draw(ogl_ambient().fan(std::span(vertices).first(nv), ogl_texture_slot(*gltexture), render::wrap::repeat));
}

/*
 * 2d Sprites (Fireaballs, powerups, explosions). NOT hostages
 */
void g3_draw_bitmap(grs_canvas &canvas, const vms_vector &pos, const fix iwidth, const fix iheight, grs_bitmap &bm)
{
	r_bitmapc++;

	const auto gltexture{ogl_loaded_bmtex(bm, 0)};
	if (!gltexture) [[unlikely]]
		return;

	const auto width = fixmul(iwidth, Matrix_scale.x);
	const auto height = fixmul(iheight, Matrix_scale.y);
	constexpr unsigned point_count = 4;
	std::array<render::vertex, point_count> vertices;
	const auto &&rpv{vm_vec_build_rotated(vm_vec_build_sub(pos, View_position), View_matrix)};
	const auto bmglu = bm.gltexture->u;
	const auto bmglv = bm.gltexture->v;
	const GLfloat alpha = canvas.cv_fade_level >= GR_FADE_OFF ? 1.0 : (1.0 - static_cast<float>(canvas.cv_fade_level) / (static_cast<float>(GR_FADE_LEVELS) - 1.0));
	const auto vert_z = -f2glf(rpv.z);
	for (unsigned i = 0; i < point_count; i++){
		auto &v = vertices[i];
		auto pv = rpv;
		switch (i){
			case 0:
				v.uv = {{0.0, 0.0}};
				pv.x+=-width;
				pv.y+=height;
				break;
			case 1:
				v.uv = {{bmglu, 0.0}};
				pv.x+=width;
				pv.y+=height;
				break;
			case 2:
				v.uv = {{bmglu, bmglv}};
				pv.x+=width;
				pv.y+=-height;
				break;
			case 3:
				v.uv = {{0.0, bmglv}};
				pv.x+=-width;
				pv.y+=-height;
				break;
		}

		v.rgba = {{1.0, 1.0, 1.0, alpha}};
		v.pos = {{f2glf(pv.x), f2glf(pv.y), vert_z}};
	}
	ogl_device().draw(ogl_ambient().fan(vertices, ogl_texture_slot(*gltexture), render::wrap::clamp));
}

/*
 * Movies
 * Since this function will create a new texture each call, mipmapping can be very GPU intensive - so it has an optional setting for texture filtering.
 */
bool ogl_ubitblt_i(unsigned dw,unsigned dh,unsigned dx,unsigned dy, unsigned sw, unsigned sh, unsigned sx, unsigned sy, const grs_bitmap &src, grs_bitmap &dest, const opengl_texture_filter texfilt)
{
	struct bitblt_free_ogl_texture
	{
		ogl_texture t;
		~bitblt_free_ogl_texture()
		{
			ogl_freetexture(t);
			ogl_movie_frame = nullptr;
		}
	};
	bitblt_free_ogl_texture frame{};
	ogl_texture &tex = frame.t;
	ogl_movie_frame = &tex;
	r_ubitbltc++;

	ogl_init_texture(tex, sw, sh, OGL_FLAG_ALPHA);
	tex.prio = 0.0;
	tex.lw=src.bm_rowsize;

	dx+=dest.bm_x;
	dy+=dest.bm_y;
	ogl_loadtexture(gr_current_pal, src.get_bitmap_data(), sx, sy, tex, src.get_flags(), texfilt, 0, 0);

	const auto vertices{render::blit_vertices(
		render::movie_rect(dx, dy, dw, dh, {last_width, last_height}),
		{.u1 = 0, .u2 = tex.u, .v1 = 0, .v2 = tex.v},
		ogl_colors::white)};
	ogl_device().draw(ogl_ambient().fan(vertices, ogl_texture_slot(tex), render::wrap::clamp));
	return 0;
}

bool ogl_ubitblt(unsigned w,unsigned h,unsigned dx,unsigned dy, unsigned sx, unsigned sy, const grs_bitmap &src, grs_bitmap &dest){
	return ogl_ubitblt_i(w, h, dx, dy, w, h, sx, sy, src, dest, opengl_texture_filter::classic);
}

/*
 * set depth testing on or off
 */
void ogl_toggle_depth_test(int enable)
{
	ogl_ambient().depth = enable ? render::depth::test_and_write : render::depth::off;
}

/* 
 * set blending function
 */
void ogl_set_blending(const gr_blend cv_blend_func)
{
	render::blend b;
	switch (cv_blend_func)
	{
		case gr_blend::additive_a:
			b = render::blend::additive_a;
			break;
		case gr_blend::additive_c:
			b = render::blend::additive_c;
			break;
		case gr_blend::normal:
		default:
			b = render::blend::normal;
			break;
	}
	ogl_ambient().blend_function = b;
}

void ogl_start_frame(grs_canvas &canvas)
{
	r_polyc=0;r_tpolyc=0;r_bitmapc=0;r_ubitbltc=0;

	OGL_VIEWPORT(canvas.cv_bitmap.bm_x, canvas.cv_bitmap.bm_y, canvas.cv_bitmap.bm_w, canvas.cv_bitmap.bm_h);
	/* The device sets GL to the state of a 3D frame on the next draw, and the
	 * depth clear is also the rear view's, in mid-frame.
	 */
	render::begin_3d_frame(ogl_device(), ogl_ambient(), linedotscale);
}

void ogl_sync_gl_state()
{
	ogl_device().draw(ogl_ambient().call(render::topology::lines, {}));
}

/* ogl_sync_gl_state for glCopyPixels, which runs through GL's blend, alpha
 * and depth tests and also its texturing.  The copy is textured only by
 * upstream's accident: the last blit left GL_TEXTURE_2D on.  Nothing wants
 * it, but the picture stays bit-identical only with it (R1), so as the
 * empty draw turns GL_TEXTURE_2D off, put it back as it was.
 */
[[maybe_unused]]
static void ogl_sync_gl_state_keeping_texturing()
{
	const auto texturing{GL_TEXTURE_2D_enabled};
	ogl_sync_gl_state();
	if (texturing == 1)
		OGL_ENABLE(TEXTURE_2D);
}

#if DXX_USE_STEREOSCOPIC_RENDER
void ogl_stereo_frame(const bool left_eye, const int xoff)
{
	const float dxoff = xoff * 2.0f / grd_curscreen->sc_canvas.cv_bitmap.bm_w;
	float stereo_transform_dxoff;
	auto &device{ogl_device()};
	auto &ambient{ogl_ambient()};

	// query if stereo quad buffering available?
	const bool ogl_stereo_enabled = device.caps().quad_buffer_stereo;

	/* The viewport the game set: x, y, width and height. */
	const auto viewport_of_ambient{[&ambient]() {
		const auto &v{ambient.viewport};
		return std::array<int, 4>{{v.x, v.y, static_cast<int>(v.size.width), static_cast<int>(v.size.height)}};
	}};
	const auto set_ambient_viewport{[&ambient](const std::array<int, 4> &v) {
		ambient.viewport = {v[0], v[1], {static_cast<uint32_t>(v[2]), static_cast<uint32_t>(v[3])}};
	}};
	if (left_eye) {
		// left eye view
		if (!ogl_stereo_enabled)
		{
			[&]() {
				std::array<int, 4> ogl_stereo_viewport{viewport_of_ambient()};
				// center unsqueezed side-by-side format
				switch (VR_stereo) {
					case StereoFormat::None:
					case StereoFormat::QuadBuffers:
						/* Not reached */
					case StereoFormat::AboveBelow:
					case StereoFormat::SideBySideFullHeight:
						/* No modification needed */
						return;
					case StereoFormat::SideBySideHalfHeight:
						ogl_stereo_viewport[1] -= SHEIGHT/4;	// y = h/4
						break;
					case StereoFormat::AboveBelowSync:
						{
							const int dy = VR_sync_width / 2;
							ogl_stereo_viewport[3] -= dy;
							ogl_stereo_viewport[1] += dy;
							break;
						}
				}
				set_ambient_viewport(ogl_stereo_viewport);
			}();
		}
		// rightward image shift adjustment for left eye offset
		stereo_transform_dxoff = -dxoff;		// xoff < 0
	}
	else
	{
		// right eye view
		if (!ogl_stereo_enabled)
		{
			std::array<int, 4> ogl_stereo_viewport{viewport_of_ambient()};
			switch (VR_stereo) {
				case StereoFormat::None:
				case StereoFormat::QuadBuffers:
					/* Not reached */
					break;
					// center unsqueezed side-by-side format
				case StereoFormat::SideBySideHalfHeight:
					ogl_stereo_viewport[1] -= SHEIGHT/4;	// y = h/4
					[[fallthrough]];
					// half-width viewports for side-by-side format
				case StereoFormat::SideBySideFullHeight:
					ogl_stereo_viewport[0] += SWIDTH/2;		// x = w/2
					break;
					// half-height viewports for above/below format
				case StereoFormat::AboveBelowSync:
				case StereoFormat::AboveBelow:
					ogl_stereo_viewport[1] -= SHEIGHT/2;	// y = h/2
					if (VR_stereo == StereoFormat::AboveBelowSync)
						ogl_stereo_viewport[3] -= VR_sync_width / 2;
					break;
			}
			set_ambient_viewport(ogl_stereo_viewport);
		}
		// leftward image shift adjustment for right eye offset
		stereo_transform_dxoff = dxoff;		// xoff < 0
	}
	if (ogl_stereo_enabled)
		device.select_eye(left_eye ? render::eye::left : render::eye::right);
	if (const auto p{std::get_if<render::perspective>(&ambient.transforms.projection)})
		p->stereo_dx += stereo_transform_dxoff;
}
#endif

void ogl_set_alpha_ref(const float ref)
{
	ogl_ambient().alpha_ref = ref;
}

void ogl_enable_blending(const bool enable)
{
	ogl_ambient().blending = enable;
}

render::ambient &ogl_ambient()
{
	static render::ambient ambient;
	return ambient;
}

render::device &ogl_device()
{
	return ogl_gl_device();
}

void ogl_create_device()
{
	const auto context{render::gl_device::read_context()};
	auto device{std::make_unique<render::gl_device>(render::gl_caps({
		.max_anisotropy = ogl_maxanisotropy,
		.stereo_buffers = context.stereo_buffers,
		.desktop = !DXX_USE_OGLES,
	}), render::gl_front_end{
		.swap = ogl_swap_buffers_internal,
		.target = []() -> render::extent { return {SWIDTH, SHEIGHT}; },
		.untextured = []() { OGL_DISABLE(TEXTURE_2D); },
		.textured = [](const render::texture_slot slot, const render::wrap wrap) {
			const auto tex{ogl_texture_of(slot)};
			if (!tex || !tex->handle) [[unlikely]]
				throw std::logic_error("ogl_device: textured draw of a slot that holds no texture");
			OGL_ENABLE(TEXTURE_2D);
			OGL_BINDTEXTURE(tex->handle);
			ogl_texwrap(tex, wrap == render::wrap::repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		},
		.max_anisotropy = ogl_maxanisotropy,
	})};
	/* A device of the same GL context keeps the textures, as upstream did
	 * across an SDL2 mode change; SDL1's mode changes delete them
	 * (ogl_smash_texture_list_internal).
	 */
	if (ogl_device_instance)
		device->take_textures(*ogl_device_instance);
	ogl_device_instance = std::move(device);
	/* A new GL context has the alpha test off and lines 1 pixel wide; one
	 * kept across a mode change keeps what it had.  The code not yet
	 * drawing through the device reads both, so record GL's own.
	 */
	auto &ambient{ogl_ambient()};
	ambient.alpha_test = context.alpha_test;
	ambient.line_width = context.line_width;
}

void ogl_record_2d_state()
{
	auto &ambient{ogl_ambient()};
	ambient.transforms = {render::screen_ortho, render::identity{}};
	ambient.blend_function = render::blend::normal;
	ambient.blending = true;
}

render::caps gr_caps()
{
	return ogl_device().caps();
}

bool ogl_bitmap_has_texture(const grs_bitmap &bm)
{
	const ogl_texture *const gltexture{bm.gltexture};
	return gltexture && gltexture->handle;
}

void ogl_end_frame(void){
	OGL_VIEWPORT(0, 0, grd_curscreen->get_screen_width(), grd_curscreen->get_screen_height());
	render::end_3d_frame(ogl_ambient());
}

#if DXX_USE_DBG_HARNESS
namespace {

/* With DXX_DBG_FRAMEHASH set, read back the finished frame before it is
 * presented and log its hash (frame_hash_log.h).  Any failure ends the run:
 * a log with a gap would compare as a changed frame.
 */
void harness_log_frame_hash()
{
	static const char *const path{read_harness_frame_hash_path()};
	if (!path)
		return;
#if DXX_USE_OGLES
	/* GLES guarantees only a GL_RGBA readback, and the log hashes RGB. */
	UserError("DXX_DBG_FRAMEHASH: OpenGL ES builds do not hash frames; use a desktop OpenGL build");
#endif
	static frame_hash_log log{[]() {
		auto writer{frame_hash_file_writer::open(path)};
		if (!writer)
			UserError("DXX_DBG_FRAMEHASH=\"%s\": cannot write: %s", path, strerror(errno));
		con_printf(CON_NORMAL, "DXX_DBG_FRAMEHASH: one hash per frame to %s", path);
		return std::move(writer.value());
	}()};
#if DXX_USE_STEREOSCOPIC_RENDER
	if (VR_stereo == StereoFormat::QuadBuffers)
		UserError("%s", "DXX_DBG_FRAMEHASH: quad-buffer stereo is not hashed, only one eye would be");
#endif
	const frame_size size{.width = grd_curscreen->get_screen_width(), .height = grd_curscreen->get_screen_height()};
	static std::vector<std::uint8_t> pixels;
	pixels.resize(size.rgb_bytes());
	/* The game never calls glGetError: an error of earlier drawing would
	 * still be pending and fail the readback's check below.  Say once that
	 * there were some, so a real GL error is not hidden.
	 */
	static bool reported_pending_errors{false};
	if (const auto pending{render::gl_device::drain_errors()}; pending.count != 0 && !reported_pending_errors)
	{
		reported_pending_errors = true;
		con_printf(CON_NORMAL, "DXX_DBG_FRAMEHASH: %u GL error(s) were pending before a readback, the first 0x%x; reported only once", pending.count, pending.first);
	}
	/* The frame being drawn, whatever a screenshot left selected. */
	ogl_device().read_pixels(render::read_source::drawing, {.x = 0, .y = 0, .size = {size.width, size.height}}, render::pixel_format::rgb8, pixels);
	/* The log hashes the rows bottom first, as glReadPixels gave them. */
	render::flip_rows(pixels, std::size_t{size.width} * 3);
	if (const auto error{render::gl_device::drain_errors()}; error.count != 0)
		UserError("DXX_DBG_FRAMEHASH: reading back the frame failed: GL error 0x%x", error.first);
	const auto scene{harness_scene_name()};
	const auto section{Newdemo_state == ND_STATE_PLAYBACK ? frame_hash_section::demo_playback(newdemo_harness_playback_file()) : scene ? frame_hash_section::scene(scene) : frame_hash_section::no_demo()};
	if (!log.log_frame(section, size, std::as_bytes(std::span<const std::uint8_t>{pixels})))
		UserError("DXX_DBG_FRAMEHASH=\"%s\": write failed: %s", path, strerror(errno));
}

}
#endif

void gr_flip(void)
{
	if (CGameArg.DbgRenderStats)
	{
		gr_set_default_canvas();
		ogl_texture_stats(*grd_curcanv);
	}

	ogl_do_palfx();
#if DXX_USE_DBG_HARNESS
	harness_log_frame_hash();
#endif
	auto &device{ogl_device()};
	device.present();
	device.clear({.color = true, .depth = false});
}

// Allocate the pixel buffers 'pixels' and 'texbuf' based on current screen resolution
void ogl_init_pixel_buffers(unsigned w, unsigned h)
{
	w = std::bit_ceil(w);	// convert to OpenGL texture size
	h = std::bit_ceil(h);

	texbuf = std::make_unique<GLubyte[]>(max(w, 1024u)*max(h, 256u)*4);	// must also fit big font texture
}

void ogl_close_pixel_buffers(void)
{
	texbuf.reset();
}

/* The render device's format of tex, from the internal format
 * ogl_init_texture chose.
 */
static render::texture_format ogl_texture_format(const ogl_texture &tex)
{
	switch (tex.internalformat)
	{
#if !DXX_USE_OGLES
		case GL_INTENSITY4:
			return render::texture_format::intensity4;
		case GL_LUMINANCE4_ALPHA4:
			return render::texture_format::luminance4_alpha4;
		case GL_RGBA2:
			return render::texture_format::rgba2;
#endif
		default:
			return tex.format == GL_RGBA ? render::texture_format::rgba8 : render::texture_format::rgb8;
	}
}

/* Fills texp, tex's padded size in RGBA pixels, from the palette indices
 * of data whose rows are truewidth long, starting at column dxo, row dyo.
 */
static void ogl_filltexbuf(const palette_array_t &pal, const uint8_t *const data, const std::span<uint8_t> texp, const unsigned truewidth, const ogl_texture &tex, const int dxo, const int dyo, const int bm_flags)
{
	if ((tex.w > max(static_cast<unsigned>(grd_curscreen->get_screen_width()), 1024u)) ||
		(tex.h > max(static_cast<unsigned>(grd_curscreen->get_screen_height()), 256u)))
		Error("Texture is too big: %ix%i", tex.w, tex.h);
	try {
		render::expand_palette(pal, {
			/* The rows of data up to the bitmap's last. */
			.indices = std::span(data, std::size_t{truewidth} * (dyo + tex.h)),
			.stride = truewidth,
			.x = static_cast<uint32_t>(dxo),
			.y = static_cast<uint32_t>(dyo),
			.size = {tex.w, tex.h},
			.transparent = (bm_flags & BM_FLAG_TRANSPARENT) != 0,
			.super_transparent = (bm_flags & BM_FLAG_SUPER_TRANSPARENT) != 0,
		}, ogl_texture_format(tex), {tex.tw, tex.th}, texp);
	} catch (const std::logic_error &e) {
		/* domain_error, out_of_range and length_error: a bitmap the
		 * texture cannot hold ends the game with a message, not abort().
		 */
		Error("ogl_filltexbuf: %s", e.what());
	}
}

static void tex_set_size1(ogl_texture &tex, const unsigned dbits, const unsigned bits, const unsigned w, const unsigned h)
{
	int u;
	if (tex.tw!=w || tex.th!=h){
		u=(tex.w/static_cast<float>(tex.tw)*w) * (tex.h/static_cast<float>(tex.th)*h);
		glmprintf((CON_DEBUG, "shrunken texture?"));
	}else
		u=tex.w*tex.h;
	if (bits<=0){//the beta nvidia GLX server. doesn't ever return any bit sizes, so just use some assumptions.
		tex.bytes=(static_cast<float>(w)*h*dbits)/8.0;
		tex.bytesu=(static_cast<float>(u)*dbits)/8.0;
	}else{
		tex.bytes=(static_cast<float>(w)*h*bits)/8.0;
		tex.bytesu=(static_cast<float>(u)*bits)/8.0;
	}
	glmprintf((CON_DEBUG, "tex_set_size1: %ix%i, %ib(%i) %iB", w, h, bits, dbits, tex.bytes));
}

static void tex_set_size(ogl_texture &tex)
{
	GLint w,h;
	int bi{16},a=0;
#if !DXX_USE_OGLES
	if (CGameArg.DbgGlGetTexLevelParamOk)
	{
		GLint t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_LUMINANCE_SIZE,&t);a+=t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTENSITY_SIZE,&t);a+=t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_RED_SIZE,&t);a+=t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_GREEN_SIZE,&t);a+=t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_BLUE_SIZE,&t);a+=t;
		glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_ALPHA_SIZE,&t);a+=t;
	}
	else
#endif
	{
		w=tex.tw;
		h=tex.th;
	}
	switch (tex.format){
		case GL_LUMINANCE:
			bi=8;
			break;
		case GL_LUMINANCE_ALPHA:
			bi=8;
			break;
		case GL_RGB:
		case GL_RGBA:
			bi=16;
			break;
#if !DXX_USE_OGLES
		case GL_COLOR_INDEX:
			bi = 8;
			break;
#endif
		default:
			throw std::runtime_error("unknown texture format");
	}
	tex_set_size1(tex,bi,a,w,h);
}

//loads a palettized bitmap into a ogl RGBA texture.
//Sizes and pads dimensions to multiples of 2 if necessary.
//In theory this could be a problem for repeating textures, but all real
//textures (not sprites, etc) in descent are 64x64, so we are ok.
//stores OpenGL textured id in *texid and u/v values required to get only the real data in *u/*v
static int ogl_loadtexture(const palette_array_t &pal, const uint8_t *data, const int dxo, int dyo, ogl_texture &tex, const int bm_flags, opengl_texture_filter texfilt, const bool texanis, const bool edgepad)
{
	tex.tw = {std::bit_ceil(tex.w)};
	tex.th = {std::bit_ceil(tex.h)};	//calculate smallest texture size that can accommodate us (must be power of 2)

	//calculate u/v values that would make the resulting texture correctly sized
	tex.u = static_cast<float>(static_cast<double>(tex.w) / static_cast<double>(tex.tw));
	tex.v = static_cast<float>(static_cast<double>(tex.h) / static_cast<double>(tex.th));

	const std::span<uint8_t> texp(texbuf.get(), std::size_t{tex.tw} * tex.th * 4);
	ogl_filltexbuf(pal, data, texp, tex.lw, tex, dxo, dyo, bm_flags);
	const uint8_t *outP = texbuf.get();

	//bleed color (rgb) into the alpha area, to deal with "dark edges problem"
	if (tex.format == GL_RGBA && texfilt != opengl_texture_filter::classic && edgepad && !CGameArg.OglDarkEdges)
		render::bleed_edges(texp, tex.tw);

	int rescale{1};
	if (texfilt == opengl_texture_filter::upscale)
	{
		rescale = 4;
		if ((tex.tw > 256) || (tex.th > 256))
		{
			//don't scale big textures, instead "cheat" and render them as normal (nearest)
			//these are normally 320x200 and 640x480 images.
			//this deals with texture size limits, as well as large textures causing noticeable performance issues/hiccups.
			texfilt = opengl_texture_filter::classic;
			rescale = 1;
		}
	}
	std::unique_ptr<GLubyte[]> buftemp;
	if (rescale > 1)
	{
		const int rebpp{4};
		buftemp = std::make_unique<GLubyte[]>(rescale * tex.tw * rescale * tex.th * rebpp);
		int x,y;
		GLubyte *p = buftemp.get();
		int len = tex.tw*rebpp*rescale;
		int bppscale = (rebpp*rescale);

		for (y = 0; y < tex.th; y++)
		{
			GLubyte	*p1;

			for (x = 0; x < len; x++)
			{
				*(p+x) = *(outP + ((x / bppscale)*rebpp + (x % rebpp)));
			}
			p1 = p;
			p += len;
			int y2;
			for (y2 = 1; y2 < rescale; y2++)
			{
				memcpy (p, p1, len);
				p += len;
			}
			outP += tex.tw*rebpp;
		}
		outP = buftemp.get();
	}

	const std::span<const uint8_t> pixels(outP, std::size_t{tex.tw} * rescale * tex.th * rescale * 4);
	const render::texture_image image{{
		.size = {tex.tw * rescale, tex.th * rescale},
		.format = ogl_texture_format(tex),
		.filter = texfilt == opengl_texture_filter::classic ? render::filter::nearest : render::filter::trilinear,
		.anisotropic = texanis,
		.has_transparency = render::has_transparency(pixels),
	}, pixels};
	const auto slot{ogl_texture_slot(tex)};
	auto &device{ogl_gl_device()};
	device.texture(slot, &image);
	tex.handle = device.gl_name(slot);

	tex_set_size(tex);
	r_texcount++;
	return 0;
}

void ogl_loadbmtexture_f(grs_bitmap &rbm, const opengl_texture_filter texfilt, bool texanis, bool edgepad)
{
	assert(!rbm.get_flag_mask(BM_FLAG_PAGED_OUT));
	assert(rbm.bm_data);
	if (!rbm.bm_data) [[unlikely]]
		/* `rbm.bm_data` will only be `nullptr` if a game data file is missing. */
		return;
#if SDL_MAJOR_VERSION == 3
	/* A level load uploads its textures here. */
	dcx::sdl::pump_during_load();
#endif
	grs_bitmap *bm = &rbm;
	while (const auto bm_parent = bm->bm_parent)
		bm = bm_parent;
	if (bm->gltexture && bm->gltexture->handle > 0)
		return;
	auto buf=bm->get_bitmap_data();
	const unsigned bm_w = bm->bm_w;
	if (bm->gltexture == NULL){
		ogl_init_texture(*(bm->gltexture = ogl_get_free_texture()), bm_w, bm->bm_h, ((bm->get_flag_mask(BM_FLAG_TRANSPARENT | BM_FLAG_SUPER_TRANSPARENT)) ? OGL_FLAG_ALPHA : 0));
	}
	else {
		if (bm->gltexture->handle>0)
			return;
		if (bm->gltexture->w==0){
			bm->gltexture->lw = bm_w;
			bm->gltexture->w = bm_w;
			bm->gltexture->h=bm->bm_h;
		}
	}

	std::array<uint8_t, 300*1024> decodebuf;
	if (bm->get_flag_mask(BM_FLAG_RLE))
	{
		decodebuf = {};
		buf = decodebuf.data();
		if (!bm_rle_expand{*bm}.loop(bm_w, bm_rle_expand_range{decodebuf}))
		{
			con_printf(CON_URGENT, "error: insufficient space to decode %ux%hu bitmap.  Please report this as a bug.", bm_w, bm->bm_h);
		}
	}
	ogl_loadtexture(gr_palette, buf, 0, 0, *bm->gltexture, bm->get_flags(), texfilt, texanis, edgepad);
}

static void ogl_freetexture(ogl_texture &gltexture)
{
	if (gltexture.handle>0) {
		r_texcount--;
		glmprintf((CON_DEBUG, "ogl_freetexture(%p):%i (%i left)", &gltexture, gltexture.handle, r_texcount));
		ogl_release_texture(gltexture);
		ogl_reset_texture(gltexture);
	}
}

void ogl_freebmtexture(grs_bitmap &bm)
{
	if (auto &gltexture = bm.gltexture)
		ogl_freetexture(*std::exchange(gltexture, nullptr));
}

const ogl_colors::array_type ogl_colors::white = {{
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
	1.0, 1.0, 1.0, 1.0,
}};

const ogl_colors::array_type &ogl_colors::init_maybe_white(const int c)
{
	return c == -1 ? white : init_palette(c);
}

const ogl_colors::array_type &ogl_colors::init_palette(const unsigned c)
{
	const auto &rgb = gr_current_pal[c];
	const GLfloat r = rgb.r / 63.0, g = rgb.g / 63.0, b = rgb.b / 63.0;
	a = {{
		r, g, b, 1.0,
		r, g, b, 1.0,
		r, g, b, 1.0,
		r, g, b, 1.0,
	}};
	return a;
}

void ogl_ubitmapm_cs(grs_canvas &canvas, int x, int y,int dw, int dh, grs_bitmap &bm, int c)
{
	ogl_colors color;
	ogl_ubitmapm_cs(canvas, x, y, dw, dh, bm, color.init(c), true);
}

/*
 * Menu / gauges 
 */
void ogl_ubitmapm_cs(grs_canvas &canvas, const int entry_x, const int entry_y, const int entry_dw, const int entry_dh, grs_bitmap &bm, const ogl_colors::array_type &color_array)
{
	const int adjusted_canvas_x = entry_x + canvas.cv_bitmap.bm_x;
	const int adjusted_canvas_y = entry_y + canvas.cv_bitmap.bm_y;

	const int effective_dw = (entry_dw == opengl_bitmap_use_dst_canvas)
		? canvas.cv_bitmap.bm_w
		: ((entry_dw == opengl_bitmap_use_src_bitmap)
			? bm.bm_w
			: entry_dw);
	const int effective_dh = (entry_dh == opengl_bitmap_use_dst_canvas)
		? canvas.cv_bitmap.bm_h
		: ((entry_dh == opengl_bitmap_use_src_bitmap)
			? bm.bm_h
			: entry_dh);

	const auto gltexture{ogl_loaded_bmtex(bm, 0)};
	if (!gltexture) [[unlikely]]
		return;

	const auto vertices{render::blit_vertices(
		render::canvas_rect(adjusted_canvas_x, adjusted_canvas_y, effective_dw, effective_dh, {last_width, last_height}),
		render::bitmap_uv({
			.x = bm.bm_x, .y = bm.bm_y, .w = bm.bm_w, .h = bm.bm_h,
			.tex_w = gltexture->w, .tex_h = gltexture->h,
			.tex_tw = gltexture->tw, .tex_th = gltexture->th,
			.u = gltexture->u, .v = gltexture->v}),
		color_array)};
	ogl_device().draw(ogl_ambient().fan(vertices, ogl_texture_slot(*gltexture), render::wrap::clamp));
}

void ogl_ubitmapm_cs(grs_canvas &canvas, int x0, int y0, int dw, int dh, grs_bitmap &bm, const ogl_colors::array_type &color_array, bool fill)
{
#if DXX_USE_STEREOSCOPIC_RENDER
	// blit bitmap 2x for stereo viewport formats
	if (VR_stereo != StereoFormat::None) {
		int x = x0, y = y0;
		int w = (dw < 0) ? canvas.cv_bitmap.bm_w : (dw == 0) ? bm.bm_w : dw;
		int h = (dh < 0) ? canvas.cv_bitmap.bm_h : (dh == 0) ? bm.bm_h : dh;
		if (fill) {
			const auto &&[rw, rh]{gr_build_stereo_viewport_size(VR_stereo, w, h)};
			w = rw;
			h = rh;
			y = gr_build_stereo_viewport_offset_left_eye(VR_stereo, y);
		}
		const bool quad_buffers{VR_stereo == StereoFormat::QuadBuffers && ogl_device().caps().quad_buffer_stereo};
		if (quad_buffers)
			ogl_device().select_eye(render::eye::left);
		ogl_ubitmapm_cs(canvas, x, y, w, h, bm, color_array);
		const auto &&[vx, vy]{gr_build_stereo_viewport_offset_right_eye(VR_stereo, x, y, 1)};
		x = vx;
		y = vy;
		if (quad_buffers)
			ogl_device().select_eye(render::eye::right);
		ogl_ubitmapm_cs(canvas, x, y, w, h, bm, color_array);
		return;
	}
#else
	(void)fill;
#endif
	ogl_ubitmapm_cs(canvas, x0, y0, dw, dh, bm, color_array);
}

// blit rectangular region from screen
bool ogl_ubitblt_cs(grs_canvas &canvas, int dw, int dh, int dx, int dy, int sx, int sy)
{
#if DXX_USE_STEREOSCOPIC_RENDER
	dy = canvas.cv_bitmap.bm_h - (dy + dh);	// GL y flip
	sy = canvas.cv_bitmap.bm_h - (sy + dh);	// GL y flip
	auto &device{ogl_device()};
	if (device.caps().copy_region)
	{
		ogl_sync_gl_state_keeping_texturing();
		device.copy_region({.src_x = sx, .src_y = sy, .dest_x = dx, .dest_y = dy, .size = {static_cast<uint32_t>(dw), static_cast<uint32_t>(dh)}},
			VR_stereo == StereoFormat::QuadBuffers && device.caps().quad_buffer_stereo);
	}
	return 0;
#else
	return ogl_ubitblt(dw, dh, dx, dy, sx, sy, canvas.cv_bitmap, canvas.cv_bitmap);
#endif
}

}
