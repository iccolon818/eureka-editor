//------------------------------------------------------------------------
//  3D RENDERING
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2019 Andrew Apted
//  Copyright (C) 1997-2003 Andr� Majorel et al
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "main.h"

#include <map>
#include <algorithm>

#ifndef NO_OPENGL
#include "FL/gl.h"
#endif

#include "e_basis.h"
#include "e_cutpaste.h"
#include "e_hover.h"
#include "e_linedef.h"
#include "e_main.h"
#include "m_game.h"
#include "m_events.h"
#include "r_render.h"
#include "ui_window.h"


// config items
rgb_color_t transparent_col = RGB_MAKE(0, 255, 255);

bool render_high_detail    = false;
bool render_lock_gravity   = false;
bool render_missing_bright = true;
bool render_unknown_bright = true;

// in original DOOM pixels were 20% taller than wide, giving 0.83
// as the pixel aspect ratio.
int  render_pixel_aspect = 83;  //  100 * width / height


Render_View_t::Render_View_t() :
	p_type(0), px(), py(),
	x(), y(), z(),
	angle(), Sin(), Cos(),
	screen_w(), screen_h(), screen(NULL),
	texturing(false), sprites(false), lighting(false),
	gravity(true),
	thing_sectors(),
	thsec_sector_num(0),
	thsec_invalidated(false),
	is_scrolling(false),
	nav_time(0),
	current_hl(),
	adjust_sides(), adjust_lines(),
	saved_x_offsets(), saved_y_offsets()
{ }

Render_View_t::~Render_View_t()
{ }

void Render_View_t::SetAngle(float new_ang)
{
	angle = new_ang;

	if (angle >= 2*M_PI)
		angle -= 2*M_PI;
	else if (angle < 0)
		angle += 2*M_PI;

	Sin = sin(angle);
	Cos = cos(angle);
}

void Render_View_t::FindGroundZ()
{
	// test a grid of points on the player's bounding box, and
	// use the maximum floor of all contacted sectors.

	int max_floor = INT_MIN;

	for (int dx = -2 ; dx <= 2 ; dx++)
	for (int dy = -2 ; dy <= 2 ; dy++)
	{
		Objid o;

		GetNearObject(o, OBJ_SECTORS, int(x + dx*8), int(y + dy*8));

		if (o.num >= 0)
			max_floor = MAX(max_floor, Sectors[o.num]->floorh);
	}

	if (max_floor != INT_MIN)
		z = max_floor + Misc_info.view_height;
}

void Render_View_t::CalcAspect()
{
	aspect_sw = screen_w;	 // things break if these are different

	aspect_sh = screen_w / (render_pixel_aspect / 100.0);
}

void Render_View_t::UpdateScreen(int ow, int oh)
{
	// in low detail mode, setup size so that expansion always covers
	// our window (i.e. we draw a bit more than we need).

	int new_sw = render_high_detail ? ow : (ow + 1) / 2;
	int new_sh = render_high_detail ? oh : (oh + 1) / 2;

	if (!screen || screen_w != new_sw || screen_h != new_sh)
	{
		screen_w = new_sw;
		screen_h = new_sh;

		if (screen)
		{
			delete[] screen;
			screen = NULL;
		}
	}

	// we don't need a screen buffer when using OpenGL
#ifdef NO_OPENGL
	if (!screen)
		screen = new img_pixel_t [screen_w * screen_h];
#endif

	CalcAspect();
}

void Render_View_t::FindThingSectors()
{
	thing_sectors.resize(NumThings);

	for (int i = 0 ; i < NumThings ; i++)
	{
		Objid obj;

		GetNearObject(obj, OBJ_SECTORS, Things[i]->x(), Things[i]->y());

		thing_sectors[i] = obj.num;
	}

	thsec_sector_num  = NumSectors;
	thsec_invalidated = false;
}

void Render_View_t::PrepareToRender(int ow, int oh)
{
	if (thsec_invalidated || screen_w == 0 ||
		NumThings  != (int)thing_sectors.size() ||
		NumSectors != thsec_sector_num)
	{
		FindThingSectors();
	}

	UpdateScreen(ow, oh);

	if (gravity)
		FindGroundZ();
}


/* r_editing_info_t stuff */

// FIXME REMOVE THIS?
#if 0
int Render_View_t::GrabClipboard()
{
	obj3d_type_e type = SelectEmpty() ? hl.type : sel_type;

	if (type == OB3D_Thing)
		return r_clipboard.GetThing();

	if (type == OB3D_Floor || type == OB3D_Ceil)
		return r_clipboard.GetFlatNum();

	return r_clipboard.GetTexNum();
	return 0;
}

void Render_View_t::StoreClipboard(int new_val)
{
	obj3d_type_e type = SelectEmpty() ? hl.type : sel_type;

	if (type == OB3D_Thing)
	{
		r_clipboard.SetThing(new_val);
		return;
	}

	const char *name = BA_GetString(new_val);

	if (type == OB3D_Floor || type == OB3D_Ceil)
		r_clipboard.SetFlat(name);
	else
		r_clipboard.SetTex(name);
}
#endif

void Render_View_t::AddAdjustSide(const Objid& obj)
{
#if 0 // FIXME
	const LineDef *L = LineDefs[obj.num];

	int sd = (obj.side < 0) ? L->left : L->right;

	// this should not happen
	if (sd < 0)
		return;

	// ensure it is not already there
	// (e.g. when a line's upper and lower are both selected)
	for (unsigned int k = 0 ; k < adjust_sides.size() ; k++)
		if (adjust_sides[k] == sd)
			return;

	adjust_sides.push_back(sd);
	adjust_lines.push_back(obj.num);
#endif
}

float Render_View_t::AdjustDistFactor(float view_x, float view_y)
{
	if (adjust_lines.empty())
		return 128.0;

	double total = 0;

	for (unsigned int k = 0 ; k < adjust_lines.size() ; k++)
	{
		const LineDef *L = LineDefs[adjust_lines[k]];
		total += ApproxDistToLineDef(L, view_x, view_y);
	}

	return total / (double)adjust_lines.size();
}

void Render_View_t::SaveOffsets()
{
	unsigned int total = static_cast<unsigned>(adjust_sides.size());

	if (total == 0)
		return;

	if (saved_x_offsets.size() != total)
	{
		saved_x_offsets.resize(total);
		saved_y_offsets.resize(total);
	}

	for (unsigned int k = 0 ; k < total ; k++)
	{
		SideDef *SD = SideDefs[adjust_sides[k]];

		saved_x_offsets[k] = SD->x_offset;
		saved_y_offsets[k] = SD->y_offset;

		// change it temporarily (just for the render)
		SD->x_offset += (int)adjust_dx;
		SD->y_offset += (int)adjust_dy;
	}
}

void Render_View_t::RestoreOffsets()
{
	unsigned int total = static_cast<unsigned>(adjust_sides.size());

	for (unsigned int k = 0 ; k < total ; k++)
	{
		SideDef *SD = SideDefs[adjust_sides[k]];

		SD->x_offset = saved_x_offsets[k];
		SD->y_offset = saved_y_offsets[k];
	}
}


#if 0
int Render_View_t::GrabTextureFromObject(const Objid& obj)
{
	if (obj.type == OB3D_Floor)
		return Sectors[obj.num]->floor_tex;

	if (obj.type == OB3D_Ceil)
		return Sectors[obj.num]->ceil_tex;

	if (! obj.isLine())
		return -1;

	const LineDef *LD = LineDefs[obj.num];

	if (LD->OneSided())
	{
		return LD->Right()->mid_tex;
	}

	const SideDef *SD = (obj.side == SIDE_RIGHT) ? LD->Right() : LD->Left();

	if (! SD)
		return -1;

	switch (obj.type)
	{
		case OB3D_Lower:
			return SD->lower_tex;

		case OB3D_Upper:
			return SD->upper_tex;

		case OB3D_Rail:
			return SD->mid_tex;

		default:
			return -1;
	}
	return -1;
}
#endif


//
// grab the texture or flat (as offset into string table) from the
// current 3D selection.  returns -1 if selection is empty, -2 if
// there multiple selected and some were different.
//
#if 0  // FIXME !!
int Render_View_t::GrabTextureFrom3DSel()
{
	if (SelectEmpty())
	{
		return GrabTextureFromObject(hl);
	}

	int result = -1;

	for (unsigned int k = 0 ; k < sel.size() ; k++)
	{
		const Objid& obj = sel[k];

		if (! obj.valid())
			continue;

		int cur_tex = GrabTextureFromObject(obj);
		if (cur_tex < 0)
			continue;

		// more than one distinct texture?
		if (result >= 0 && result != cur_tex)
			return -2;

		result = cur_tex;
	}

	return result;
}
#endif


#if 0  // FIXME !!
void Render_View_t::StoreTextureToObject(const Objid& obj, int new_tex)
{
	if (obj.type == OB3D_Floor)
	{
		BA_ChangeSEC(obj.num, Sector::F_FLOOR_TEX, new_tex);
		return;
	}
	else if (obj.type == OB3D_Ceil)
	{
		BA_ChangeSEC(obj.num, Sector::F_CEIL_TEX, new_tex);
		return;
	}

	if (! obj.isLine())
		return;

	const LineDef *LD = LineDefs[obj.num];

	int sd = LD->WhatSideDef(obj.side);

	if (sd < 0)
		return;

	if (LD->OneSided())
	{
		BA_ChangeSD(sd, SideDef::F_MID_TEX, new_tex);
		return;
	}

	switch (obj.type)
	{
		case OB3D_Lower:
			BA_ChangeSD(sd, SideDef::F_LOWER_TEX, new_tex);
			break;

		case OB3D_Upper:
			BA_ChangeSD(sd, SideDef::F_UPPER_TEX, new_tex);
			break;

		case OB3D_Rail:
			BA_ChangeSD(sd, SideDef::F_MID_TEX,   new_tex);
			break;

		// shut the compiler up
		default: break;
	}
}
#endif


#if 0  // FIXME !!
void Render_View_t::StoreTextureTo3DSel(int new_tex)
{
	BA_Begin();

	if (SelectEmpty())
	{
		StoreTextureToObject(hl, new_tex);
	}
	else
	{
		for (unsigned int k = 0 ; k < sel.size() ; k++)
		{
			const Objid& obj = sel[k];

			if (! obj.valid())
				continue;

			StoreTextureToObject(obj, new_tex);
		}
	}

	BA_Message("pasted texture: %s", BA_GetString(new_tex));
	BA_End();
}
#endif


Render_View_t r_view;


static Thing *FindPlayer(int typenum)
{
	// need to search backwards (to handle Voodoo dolls properly)

	for (int i = NumThings-1 ; i >= 0 ; i--)
		if (Things[i]->type == typenum)
			return Things[i];

	return NULL;  // not found
}


//------------------------------------------------------------------------

static Thing *player;


void Render3D_Draw(int ox, int oy, int ow, int oh)
{
	r_view.PrepareToRender(ow, oh);

#ifdef NO_OPENGL
	SW_RenderWorld(ox, oy, ow, oh);
#else
	RGL_RenderWorld(ox, oy, ow, oh);
#endif
}


bool Render3D_Query(Objid& hl, int sx, int sy)
{
	int ow = main_win->canvas->w();
	int oh = main_win->canvas->h();

#ifdef NO_OPENGL
	// in OpenGL mode, UI_Canvas is a window and that means the
	// event X/Y values are relative to *it* and not the main window.
	// hence the following is only needed in software mode.
	int ox = main_win->canvas->x();
	int oy = main_win->canvas->y();

	sx -= ox;
	sy -= oy;
#endif

	// force high detail mode for OpenGL, so we don't lose
	// precision when performing the query.
#ifndef NO_OPENGL
	render_high_detail = true;
#endif

	hl.clear();

	if (! edit.pointer_in_window)
		return false;

	r_view.PrepareToRender(ow, oh);

	return SW_QueryPoint(hl, sx, sy);
}


void Render3D_Setup()
{
	if (! r_view.p_type)
	{
		r_view.p_type = THING_PLAYER1;
		r_view.px = 99999;
	}

	player = FindPlayer(r_view.p_type);

	if (! player)
	{
		if (r_view.p_type != THING_DEATHMATCH)
			r_view.p_type = THING_DEATHMATCH;

		player = FindPlayer(r_view.p_type);
	}

	if (player && !(r_view.px == player->x() && r_view.py == player->y()))
	{
		// if player moved, re-create view parameters

		r_view.x = r_view.px = player->x();
		r_view.y = r_view.py = player->y();

		r_view.FindGroundZ();

		r_view.SetAngle(player->angle * M_PI / 180.0);
	}
	else
	{
		r_view.x = 0;
		r_view.y = 0;
		r_view.z = 64;

		r_view.SetAngle(0);
	}

	r_view.screen_w = -1;
	r_view.screen_h = -1;

	r_view.texturing  = true;
	r_view.sprites    = true;
	r_view.lighting   = true;
}


void Render3D_Enable(bool _enable)
{
	Editor_ClearAction();

	edit.render3d = _enable;

	// give keyboard focus to the appropriate large widget
	Fl::focus(main_win->canvas);

	main_win->scroll->UpdateRenderMode();

	if (edit.render3d)
	{
		main_win->info_bar->SetMouse(r_view.x, r_view.y);
	}
	else
	{
		main_win->canvas->PointerPos();
		main_win->info_bar->SetMouse(edit.map_x, edit.map_y);
	}

	RedrawMap();
}


void Render3D_RBScroll(int mode, int dx = 0, int dy = 0, keycode_t mod = 0)
{
	// started?
	if (mode < 0)
	{
		r_view.is_scrolling = true;
		main_win->SetCursor(FL_CURSOR_HAND);
		return;
	}

	// finished?
	if (mode > 0)
	{
		r_view.is_scrolling = false;
		main_win->SetCursor(FL_CURSOR_DEFAULT);
		return;
	}

	if (dx == 0 && dy == 0)
		return;

	// we separate the movement into either turning or moving up/down
	// (never both at the same time : CONFIG IT THOUGH).

	bool force_one_dir = true;

	if (force_one_dir)
	{
		if (abs(dx) >= abs(dy))
			dy = 0;
		else
			dx = 0;
	}

	bool is_strafe = (mod & MOD_ALT) ? true : false;

	float mod_factor = 1.0;
	if (mod & MOD_SHIFT)   mod_factor = 0.4;
	if (mod & MOD_COMMAND) mod_factor = 2.5;

	float speed = r_view.scroll_speed * mod_factor;

	if (is_strafe)
	{
		r_view.x += r_view.Sin * dx * mod_factor;
		r_view.y -= r_view.Cos * dx * mod_factor;
	}
	else  // turn camera
	{
		double d_ang = dx * speed * M_PI / 480.0;

		r_view.SetAngle(r_view.angle - d_ang);
	}

	dy = -dy;  //TODO CONFIG ITEM

	if (is_strafe)
	{
		r_view.x += r_view.Cos * dy * mod_factor;
		r_view.y += r_view.Sin * dy * mod_factor;
	}
	else if (! (render_lock_gravity && r_view.gravity))
	{
		r_view.z += dy * speed * 0.75;

		r_view.gravity = false;
	}

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}


void Render3D_AdjustOffsets(int mode, int dx, int dy)
{
	// started?
	if (mode < 0)
	{
		r_view.adjust_sides.clear();
		r_view.adjust_lines.clear();

		r_view.adjust_dx = 0;
		r_view.adjust_dy = 0;

		// find the sidedefs to adjust
		if (! edit.Selected->empty())
		{
//??			if (r_view.sel_type < OB3D_Lower)
//??			{
//??				Beep("cannot adjust that");
//??				return;
//??			}

#if 0  // FIXME
			for (unsigned int k = 0 ; k < r_view.sel.size() ; k++)
			{
				const Objid& obj = r_view.sel[k];

				if (obj.type == OBJ_LINEDEFS)
					r_view.AddAdjustSide(obj);
			}
#endif
		}
		else  // no selection, use the highlight
		{
#if 0  // FIXME
			if (! r_view.hl.valid())
			{
				Beep("nothing to adjust");
				return;
			}
			else if (r_view.hl.type != OBJ_LINEDEFS)
			{
				Beep("cannot adjust that");
				return;
			}

			r_view.AddAdjustSide(r_view.hl);
#endif
		}

		if (r_view.adjust_sides.empty())  // WTF?
			return;

		float dist = r_view.AdjustDistFactor(r_view.x, r_view.y);
		dist = CLAMP(20, dist, 1000);

		r_view.adjust_dx_factor = dist / r_view.aspect_sw;
		r_view.adjust_dy_factor = dist / r_view.aspect_sh;

		Editor_SetAction(ACT_ADJUST_OFS);
		return;
	}


	if (edit.action != ACT_ADJUST_OFS)
		return;


	// finished?
	if (mode > 0)
	{
		// apply the offset deltas now
		dx = (int)r_view.adjust_dx;
		dy = (int)r_view.adjust_dy;

		if (dx || dy)
		{
			BA_Begin();

			for (unsigned int k = 0 ; k < r_view.adjust_sides.size() ; k++)
			{
				int sd = r_view.adjust_sides[k];

				const SideDef * SD = SideDefs[sd];

				BA_ChangeSD(sd, SideDef::F_X_OFFSET, SD->x_offset + dx);
				BA_ChangeSD(sd, SideDef::F_Y_OFFSET, SD->y_offset + dy);
			}

			BA_Message("adjusted offsets");
			BA_End();
		}

		r_view.adjust_sides.clear();
		r_view.adjust_lines.clear();

		Editor_ClearAction();
		return;
	}


	if (dx == 0 && dy == 0)
		return;


	bool force_one_dir = true;

	if (force_one_dir)
	{
		if (abs(dx) >= abs(dy))
			dy = 0;
		else
			dx = 0;
	}


	keycode_t mod = M_ReadLaxModifiers();

	float factor = (mod & MOD_SHIFT) ? 0.25 : 1.0;

	if (render_high_detail)
		factor = factor * 2.0;

	r_view.adjust_dx -= dx * factor * r_view.adjust_dx_factor;
	r_view.adjust_dy -= dy * factor * r_view.adjust_dy_factor;

	RedrawMap();
}


void Render3D_MouseMotion(int x, int y, keycode_t mod, int dx, int dy)
{
	if (r_view.is_scrolling)
	{
		Render3D_RBScroll(0, dx, dy, mod);
		return;
	}
	else if (edit.action == ACT_ADJUST_OFS)
	{
		Render3D_AdjustOffsets(0, dx, dy);
		return;
	}

	Objid old_hl(r_view.current_hl);

	Render3D_Query(r_view.current_hl, x, y);

	if (! (r_view.current_hl == old_hl))
	{
		UpdateHighlight();
	}
}


void Render3D_UpdateHighlight()
{
	// TODO : REVIEW HOW/WHEN pointer_in_window is set

	edit.highlight.clear();

	if (r_view.current_hl.type == edit.mode && edit.pointer_in_window)
	{
		edit.highlight = r_view.current_hl;
	}

	main_win->canvas->redraw();
	main_win->scroll->info3d->redraw();
}


void Render3D_ClearNav()
{
	r_view.nav_fwd  = r_view.nav_back  = 0;
	r_view.nav_left = r_view.nav_right = 0;
	r_view.nav_up   = r_view.nav_down  = 0;

	r_view.nav_turn_L = r_view.nav_turn_R = 0;
}


void Render3D_Navigate()
{
	float delay_ms = Nav_TimeDiff();

	delay_ms = delay_ms / 1000.0;

	keycode_t mod = M_ReadLaxModifiers();

	float mod_factor = 1.0;
	if (mod & MOD_SHIFT)   mod_factor = 0.5;
	if (mod & MOD_COMMAND) mod_factor = 2.0;

	if (r_view.nav_fwd || r_view.nav_back || r_view.nav_right || r_view.nav_left)
	{
		float fwd   = r_view.nav_fwd   - r_view.nav_back;
		float right = r_view.nav_right - r_view.nav_left;

		float dx = r_view.Cos * fwd + r_view.Sin * right;
		float dy = r_view.Sin * fwd - r_view.Cos * right;

		dx = dx * mod_factor * mod_factor;
		dy = dy * mod_factor * mod_factor;

		r_view.x += dx * delay_ms;
		r_view.y += dy * delay_ms;
	}

	if (r_view.nav_up || r_view.nav_down)
	{
		float dz = (r_view.nav_up - r_view.nav_down);

		r_view.z += dz * mod_factor * delay_ms;
	}

	if (r_view.nav_turn_L || r_view.nav_turn_R)
	{
		float dang = (r_view.nav_turn_L - r_view.nav_turn_R);

		dang = dang * mod_factor * delay_ms;
		dang = CLAMP(-90, dang, 90);

		r_view.SetAngle(r_view.angle + dang);
	}

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}


// returns -1 if nothing in selection or highlight, -2 if multiple
// things are selected and they have different types.
static int GrabSelectedThing()
{
	if (edit.Selected->empty())
	{
		if (edit.highlight.is_nil())
		{
			Beep("no things for copy/cut type");
			return -1;
		}

		const Thing *T = Things[edit.highlight.num];
		return T->type;
	}

	int result = -1;

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		const Thing *T = Things[*it];
		if (result >= 0 && T->type != result)
		{
			Beep("multiple thing types");
			return -2;
		}

		result = T->type;
	}

	return result;
}


static void StoreSelectedThing(int new_type)
{
	// this code is similar to code in UI_Thing::type_callback(),
	// but here we must handle a highlighted object.

	soh_type_e unselect = Selection_Or_Highlight();
	if (unselect == SOH_Empty)
	{
		Beep("no things for paste type");
		return;
	}

	BA_Begin();
	BA_MessageForSel("pasted type of", edit.Selected);

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		BA_ChangeTH(*it, Thing::F_TYPE, new_type);
	}

	BA_End();

	if (unselect == SOH_Unselect)
		Selection_Clear(true /* nosave */);
}


static int SEC_GrabFlat(const Sector *S, int part)
{
	if (part & PART_CEIL)
		return S->ceil_tex;

	if (part & PART_FLOOR)
		return S->floor_tex;

	return S->floor_tex;
}


// returns -1 if nothing in selection or highlight, -2 if multiple
// sectors are selected and they have different flats.
static int GrabSelectedFlat()
{
	if (edit.Selected->empty())
	{
		if (edit.highlight.is_nil())
		{
			Beep("no sectors for copy/cut flat");
			return -1;
		}

		const Sector *S = Sectors[edit.highlight.num];
		return SEC_GrabFlat(S, edit.highlight.parts);
	}

	int result = -1;

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		const Sector *S = Sectors[*it];
		byte parts = edit.Selected->get_ext(*it);

		int tex = SEC_GrabFlat(S, parts & ~1);

		if (result >= 0 && tex != result)
		{
			Beep("multiple flats present");
			return -2;
		}

		result = tex;
	}

	return result;
}


static void StoreSelectedFlat(int new_tex)
{
	soh_type_e unselect = Selection_Or_Highlight();
	if (unselect == SOH_Empty)
	{
		Beep("no sectors for paste flat");
		return;
	}

	BA_Begin();
	BA_MessageForSel("pasted flat to", edit.Selected);

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		byte parts = edit.Selected->get_ext(*it);

		if (parts == 1 || (parts & PART_FLOOR))
			BA_ChangeSEC(*it, Sector::F_FLOOR_TEX, new_tex);

		if (parts == 1 || (parts & PART_CEIL))
			BA_ChangeSEC(*it, Sector::F_CEIL_TEX, new_tex);
	}

	BA_End();

	if (unselect == SOH_Unselect)
		Selection_Clear(true /* nosave */);
}


static void StoreDefaultedFlats()
{
	soh_type_e unselect = Selection_Or_Highlight();
	if (unselect == SOH_Empty)
	{
		Beep("no sectors for default");
		return;
	}

	int floor_tex = BA_InternaliseString(default_floor_tex);
	int ceil_tex  = BA_InternaliseString(default_ceil_tex);

	BA_Begin();
	BA_MessageForSel("defaulted flat in", edit.Selected);

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		byte parts = edit.Selected->get_ext(*it);

		if (parts == 1 || (parts & PART_FLOOR))
			BA_ChangeSEC(*it, Sector::F_FLOOR_TEX, floor_tex);

		if (parts == 1 || (parts & PART_CEIL))
			BA_ChangeSEC(*it, Sector::F_CEIL_TEX, ceil_tex);
	}

	BA_End();

	if (unselect == SOH_Unselect)
		Selection_Clear(true /* nosave */);
}


static int LD_GrabTex(const LineDef *L, int part)
{
	if (L->NoSided())
		return BA_InternaliseString(default_wall_tex);

	if (L->OneSided())
		return L->Right()->mid_tex;

	if (part & PART_RT_LOWER) return L->Right()->lower_tex;
	if (part & PART_RT_UPPER) return L->Right()->upper_tex;

	if (part & PART_LF_LOWER) return L->Left()->lower_tex;
	if (part & PART_LF_UPPER) return L->Left()->upper_tex;

	if (part & PART_RT_RAIL)  return L->Right()->mid_tex;
	if (part & PART_LF_RAIL)  return L->Left() ->mid_tex;

	// pick something reasonable for a simply selected line
	if (L->Left()->SecRef()->floorh > L->Right()->SecRef()->floorh)
		return L->Right()->lower_tex;

	if (L->Left()->SecRef()->ceilh < L->Right()->SecRef()->ceilh)
		return L->Right()->upper_tex;

	if (L->Left()->SecRef()->floorh < L->Right()->SecRef()->floorh)
		return L->Left()->lower_tex;

	if (L->Left()->SecRef()->ceilh > L->Right()->SecRef()->ceilh)
		return L->Left()->upper_tex;

	// emergency fallback
	return L->Right()->lower_tex;
}


// returns -1 if nothing in selection or highlight, -2 if multiple
// linedefs are selected and they have different textures.
static int GrabSelectedTexture()
{
	if (edit.Selected->empty())
	{
		if (edit.highlight.is_nil())
		{
			Beep("no linedefs for copy/cut tex");
			return -1;
		}

		const LineDef *L = LineDefs[edit.highlight.num];
		return LD_GrabTex(L, edit.highlight.parts);
	}

	int result = -1;

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		const LineDef *L = LineDefs[*it];
		byte parts = edit.Selected->get_ext(*it);

		int tex = LD_GrabTex(L, parts & ~1);

		if (result >= 0 && tex != result)
		{
			Beep("multiple textures present");
			return -2;
		}

		result = tex;
	}

	return result;
}


static void StoreSelectedTexture(int new_tex)
{
	soh_type_e unselect = Selection_Or_Highlight();
	if (unselect == SOH_Empty)
	{
		Beep("no linedefs for paste tex");
		return;
	}

	BA_Begin();
	BA_MessageForSel("pasted flat to", edit.Selected);

	selection_iterator_c it;
	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		const LineDef *L = LineDefs[*it];
		byte parts = edit.Selected->get_ext(*it);

		if (L->NoSided())
			continue;

		if (L->OneSided())
		{
			BA_ChangeSD(L->right, SideDef::F_MID_TEX, new_tex);
			continue;
		}

		/* right side */
		if (parts == 1 || (parts & PART_RT_LOWER))
			BA_ChangeSD(L->right, SideDef::F_LOWER_TEX, new_tex);

		if (parts == 1 || (parts & PART_RT_UPPER))
			BA_ChangeSD(L->right, SideDef::F_UPPER_TEX, new_tex);

		if (parts & PART_RT_RAIL)
			BA_ChangeSD(L->right, SideDef::F_MID_TEX, new_tex);

		/* left side */
		if (parts == 1 || (parts & PART_LF_LOWER))
			BA_ChangeSD(L->left, SideDef::F_LOWER_TEX, new_tex);

		if (parts == 1 || (parts & PART_LF_UPPER))
			BA_ChangeSD(L->left, SideDef::F_UPPER_TEX, new_tex);

		if (parts & PART_LF_RAIL)
			BA_ChangeSD(L->left, SideDef::F_MID_TEX, new_tex);
	}

	BA_End();

	if (unselect == SOH_Unselect)
		Selection_Clear(true /* nosave */);
}



void Render3D_CB_Copy()
{
	int num;

	switch (edit.mode)
	{
	case OBJ_THINGS:
		num = GrabSelectedThing();
		if (num >= 0)
			Texboard_SetThing(num);
		break;

	case OBJ_SECTORS:
		num = GrabSelectedFlat();
		if (num >= 0)
			Texboard_SetFlat(BA_GetString(num));
		break;

	case OBJ_LINEDEFS:
		num = GrabSelectedTexture();
		if (num >= 0)
			Texboard_SetTex(BA_GetString(num));
		break;

	default:
		break;
	}

#if 0
	int new_tex = r_view.GrabTextureFrom3DSel();
	if (new_tex < 0)
	{
		Beep("multiple textures present");
		return;
	}

	r_view.StoreClipboard(new_tex);

	Status_Set("Copied %s", BA_GetString(new_tex));
#endif
}


void Render3D_CB_Paste()
{
	switch (edit.mode)
	{
	case OBJ_THINGS:
		StoreSelectedThing(Texboard_GetThing());
		break;

	case OBJ_SECTORS:
		StoreSelectedFlat(Texboard_GetFlatNum());
		break;

	case OBJ_LINEDEFS:
		StoreSelectedTexture(Texboard_GetTexNum());
		break;

	default:
		break;
	}

#if 0  // FIXME!!!
	int new_tex = r_view.GrabClipboard();

	r_view.StoreTextureTo3DSel(new_tex);

	Status_Set("Pasted %s", BA_GetString(new_tex));
#endif
}


void Render3D_CB_Cut()
{
	// this is repurposed to set the default texture/thing

	switch (edit.mode)
	{
	case OBJ_THINGS:
		StoreSelectedThing(default_thing);
		break;

	case OBJ_SECTORS:
		StoreDefaultedFlats();
		break;

	case OBJ_LINEDEFS:
		StoreSelectedTexture(BA_InternaliseString(default_wall_tex));
		break;

	default:
		break;
	}

#if 0
	Render3D_CB_Copy();


	obj3d_type_e type = r_view.SelectEmpty() ? r_view.hl.type : r_view.sel_type;

	if (type == OB3D_Thing)
		return;

	const char *name = default_wall_tex;

	if (type == OB3D_Floor)
		name = default_floor_tex;
	else if (type == OB3D_Ceil)
		name = default_ceil_tex;

	r_view.StoreTextureTo3DSel(BA_InternaliseString(name));

#endif
}


void Render3D_SetCameraPos(double new_x, double new_y)
{
	r_view.x = new_x;
	r_view.y = new_y;

	r_view.FindGroundZ();
}


void Render3D_GetCameraPos(double *x, double *y, float *angle)
{
	*x = r_view.x;
	*y = r_view.y;

	// convert angle from radians to degrees
	*angle = r_view.angle * 180.0 / M_PI;
}


bool Render3D_ParseUser(const char ** tokens, int num_tok)
{
	if (strcmp(tokens[0], "camera") == 0 && num_tok >= 5)
	{
		r_view.x = atof(tokens[1]);
		r_view.y = atof(tokens[2]);
		r_view.z = atof(tokens[3]);

		r_view.SetAngle(atof(tokens[4]));
		return true;
	}

	if (strcmp(tokens[0], "r_modes") == 0 && num_tok >= 4)
	{
		r_view.texturing = atoi(tokens[1]) ? true : false;
		r_view.sprites   = atoi(tokens[2]) ? true : false;
		r_view.lighting  = atoi(tokens[3]) ? true : false;

		return true;
	}

	if (strcmp(tokens[0], "r_gravity") == 0 && num_tok >= 2)
	{
		r_view.gravity = atoi(tokens[1]) ? true : false;
		return true;
	}

	if (strcmp(tokens[0], "low_detail") == 0 && num_tok >= 2)
	{
		// ignored for compatibility
		return true;
	}

	if (strcmp(tokens[0], "gamma") == 0 && num_tok >= 2)
	{
		usegamma = MAX(0, atoi(tokens[1])) % 5;

		W_UpdateGamma();
		return true;
	}

	return false;
}


void Render3D_WriteUser(FILE *fp)
{
	fprintf(fp, "camera %1.2f %1.2f %1.2f %1.2f\n",
	        r_view.x, r_view.y, r_view.z, r_view.angle);

	fprintf(fp, "r_modes %d %d %d\n",
	        r_view.texturing  ? 1 : 0,
			r_view.sprites    ? 1 : 0,
			r_view.lighting   ? 1 : 0);

	fprintf(fp, "r_gravity %d\n",
	        r_view.gravity ? 1 : 0);

	fprintf(fp, "gamma %d\n",
	        usegamma);
}


//------------------------------------------------------------------------
//  COMMAND FUNCTIONS
//------------------------------------------------------------------------

void R3D_Forward()
{
	float dist = atof(EXEC_Param[0]);

	r_view.x += r_view.Cos * dist;
	r_view.y += r_view.Sin * dist;

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}

void R3D_Backward()
{
	float dist = atof(EXEC_Param[0]);

	r_view.x -= r_view.Cos * dist;
	r_view.y -= r_view.Sin * dist;

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}

void R3D_Left()
{
	float dist = atof(EXEC_Param[0]);

	r_view.x -= r_view.Sin * dist;
	r_view.y += r_view.Cos * dist;

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}

void R3D_Right()
{
	float dist = atof(EXEC_Param[0]);

	r_view.x += r_view.Sin * dist;
	r_view.y -= r_view.Cos * dist;

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}

void R3D_Up()
{
	if (r_view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	r_view.gravity = false;

	float dist = atof(EXEC_Param[0]);

	r_view.z += dist;

	RedrawMap();
}

void R3D_Down()
{
	if (r_view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	r_view.gravity = false;

	float dist = atof(EXEC_Param[0]);

	r_view.z -= dist;

	RedrawMap();
}


void R3D_Turn()
{
	float angle = atof(EXEC_Param[0]);

	// convert to radians
	angle = angle * M_PI / 180.0;

	r_view.SetAngle(r_view.angle + angle);

	RedrawMap();
}


void R3D_DropToFloor()
{
	r_view.FindGroundZ();

	RedrawMap();
}


static void R3D_NAV_Forward_release()
{
	r_view.nav_fwd = 0;
}

void R3D_NAV_Forward()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_fwd = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Forward_release);
}


static void R3D_NAV_Back_release(void)
{
	r_view.nav_back = 0;
}

void R3D_NAV_Back()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_back = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Back_release);
}


static void R3D_NAV_Right_release(void)
{
	r_view.nav_right = 0;
}

void R3D_NAV_Right()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_right = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Right_release);
}


static void R3D_NAV_Left_release(void)
{
	r_view.nav_left = 0;
}

void R3D_NAV_Left()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_left = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Left_release);
}


static void R3D_NAV_Up_release(void)
{
	r_view.nav_up = 0;
}

void R3D_NAV_Up()
{
	if (! EXEC_CurKey)
		return;

	if (r_view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	r_view.gravity = false;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_up = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Up_release);
}


static void R3D_NAV_Down_release(void)
{
	r_view.nav_down = 0;
}

void R3D_NAV_Down()
{
	if (! EXEC_CurKey)
		return;

	if (r_view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	r_view.gravity = false;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	r_view.nav_down = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Down_release);
}


static void R3D_NAV_TurnLeft_release(void)
{
	r_view.nav_turn_L = 0;
}

void R3D_NAV_TurnLeft()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	float turn = atof(EXEC_Param[0]);

	// convert to radians
	r_view.nav_turn_L = turn * M_PI / 180.0;

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_TurnLeft_release);
}


static void R3D_NAV_TurnRight_release(void)
{
	r_view.nav_turn_R = 0;
}

void R3D_NAV_TurnRight()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	float turn = atof(EXEC_Param[0]);

	// convert to radians
	r_view.nav_turn_R = turn * M_PI / 180.0;

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_TurnRight_release);
}


static void R3D_NAV_MouseMove_release(void)
{
	Render3D_RBScroll(+1);
}

void R3D_NAV_MouseMove()
{
	if (! EXEC_CurKey)
		return;

	r_view.scroll_speed = atof(EXEC_Param[0]);

	if (! edit.is_navigating)
		Editor_ClearNav();

	if (Nav_SetKey(EXEC_CurKey, &R3D_NAV_MouseMove_release))
	{
		Render3D_RBScroll(-1);
	}
}



static void ACT_AdjustOfs_release(void)
{
	// check if cancelled or overridden
	if (edit.action != ACT_ADJUST_OFS)
		return;

	Render3D_AdjustOffsets(+1);
}

void R3D_ACT_AdjustOfs()
{
	if (! EXEC_CurKey)
		return;

	if (Nav_ActionKey(EXEC_CurKey, &ACT_AdjustOfs_release))
	{
		Render3D_AdjustOffsets(-1);
	}
}


void R3D_Set()
{
	const char *var_name = EXEC_Param[0];
	const char *value    = EXEC_Param[1];

	if (! var_name[0])
	{
		Beep("3D_Set: missing var name");
		return;
	}

	if (! value[0])
	{
		Beep("3D_Set: missing value");
		return;
	}

	 int  int_val = atoi(value);
	bool bool_val = (int_val > 0);


	if (y_stricmp(var_name, "tex") == 0)
	{
		r_view.texturing = bool_val;
	}
	else if (y_stricmp(var_name, "obj") == 0)
	{
		r_view.sprites = bool_val;
		r_view.thsec_invalidated = true;
	}
	else if (y_stricmp(var_name, "light") == 0)
	{
		r_view.lighting = bool_val;
	}
	else if (y_stricmp(var_name, "grav") == 0)
	{
		r_view.gravity = bool_val;
	}
	else
	{
		Beep("3D_Set: unknown var: %s", var_name);
		return;
	}

	RedrawMap();
}


void R3D_Toggle()
{
	const char *var_name = EXEC_Param[0];

	if (! var_name[0])
	{
		Beep("3D_Toggle: missing var name");
		return;
	}

	if (y_stricmp(var_name, "tex") == 0)
	{
		r_view.texturing = ! r_view.texturing;
	}
	else if (y_stricmp(var_name, "obj") == 0)
	{
		r_view.sprites = ! r_view.sprites;
		r_view.thsec_invalidated = true;
	}
	else if (y_stricmp(var_name, "light") == 0)
	{
		r_view.lighting = ! r_view.lighting;
	}
	else if (y_stricmp(var_name, "grav") == 0)
	{
		r_view.gravity = ! r_view.gravity;
	}
	else
	{
		Beep("3D_Toggle: unknown var: %s", var_name);
		return;
	}

	RedrawMap();
}


//
// Align texture on sidedef(s)
//
// Parameter:
//     x : align X offset
//     y : align Y offset
//    xy : align both X and Y
//
// Flags:
//    /clear : clear offset(s) instead of aligning
//    /right : align to line on the right of this one (instead of left)
//
void R3D_Align()
{
	if (! edit.render3d)
	{
		Beep("3D mode required");
		return;
	}

	// parse the flags
	bool do_X = Exec_HasFlag("/x");
	bool do_Y = Exec_HasFlag("/y");

	// this is for backwards compatibility
	{
		const char *param = EXEC_Param[0];

		if (strchr(param, 'x')) do_X = true;
		if (strchr(param, 'y')) do_Y = true;
	}

	if (! (do_X || do_Y))
	{
		Beep("3D_Align: need x or y flag");
		return;
	}

	bool do_clear = Exec_HasFlag("/clear");
	bool do_right = Exec_HasFlag("/right");
	bool do_unpeg = true;

	int align_flags = 0;

	if (do_X) align_flags = align_flags | LINALIGN_X;
	if (do_Y) align_flags = align_flags | LINALIGN_Y;

	if (do_right) align_flags |= LINALIGN_Right;
	if (do_unpeg) align_flags |= LINALIGN_Unpeg;
	if (do_clear) align_flags |= LINALIGN_Clear;


	// if selection is empty, add the highlight to it
	// (and clear it when we are done).
	bool did_select = false;

#if 0 // FIXME !!!

	if (r_view.SelectEmpty())
	{
		if (! r_view.hl.valid())
		{
			Beep("nothing to align");
			return;
		}
		else if (r_view.hl.type != OBJ_LINEDEFS)
		{
			Beep("cannot align that");
			return;
		}

		r_view.SelectToggle(r_view.hl);
		did_select = true;
	}
	else
	{
//??		if (r_view.sel_type < OB3D_Lower)
//??		{
//??			Beep("cannot align that");
//??			return;
//??		}
	}


	BA_Begin();

	Line_AlignGroup(r_view.sel, align_flags);

	if (do_clear)
		BA_Message("cleared offsets");
	else
		BA_Message("aligned offsets");

	BA_End();

	if (did_select)
		r_view.sel.clear();

	RedrawMap();
#endif
}


void R3D_WHEEL_Move()
{
	float dx = Fl::event_dx();
	float dy = Fl::event_dy();

	dy = 0 - dy;

	float speed = atof(EXEC_Param[0]);

	if (Exec_HasFlag("/LAX"))
	{
		keycode_t mod = Fl::event_state() & MOD_ALL_MASK;

		if (mod == MOD_SHIFT)
			speed /= 4.0;
		else if (mod == MOD_COMMAND)
			speed *= 4.0;
	}

	r_view.x += speed * (r_view.Cos * dy + r_view.Sin * dx);
	r_view.y += speed * (r_view.Sin * dy - r_view.Cos * dx);

	main_win->info_bar->SetMouse(r_view.x, r_view.y);
	RedrawMap();
}


//------------------------------------------------------------------------

extern void CMD_ACT_Click();


static editor_command_t  render_commands[] =
{
	{	"3D_Set", NULL,
		&R3D_Set,
		/* flags */ NULL,
		/* keywords */ "tex obj light grav"
	},

	{	"3D_Toggle", NULL,
		&R3D_Toggle,
		/* flags */ NULL,
		/* keywords */ "tex obj light grav"
	},

	{	"3D_Align", NULL,
		&R3D_Align,
		/* flags */ "/x /y /right /clear"
	},

	{	"3D_Forward", NULL,
		&R3D_Forward
	},

	{	"3D_Backward", NULL,
		&R3D_Backward
	},

	{	"3D_Left", NULL,
		&R3D_Left
	},

	{	"3D_Right", NULL,
		&R3D_Right
	},

	{	"3D_Up", NULL,
		&R3D_Up
	},

	{	"3D_Down", NULL,
		&R3D_Down
	},

	{	"3D_Turn", NULL,
		&R3D_Turn
	},

	{	"3D_DropToFloor", NULL,
		&R3D_DropToFloor
	},

	{	"3D_ACT_AdjustOfs", NULL,
		&R3D_ACT_AdjustOfs
	},

	{	"3D_WHEEL_Move", NULL,
		&R3D_WHEEL_Move
	},

	{	"3D_NAV_Forward", NULL,
		&R3D_NAV_Forward
	},

	{	"3D_NAV_Back", NULL,
		&R3D_NAV_Back
	},

	{	"3D_NAV_Right", NULL,
		&R3D_NAV_Right
	},

	{	"3D_NAV_Left", NULL,
		&R3D_NAV_Left
	},

	{	"3D_NAV_Up", NULL,
		&R3D_NAV_Up
	},

	{	"3D_NAV_Down", NULL,
		&R3D_NAV_Down
	},

	{	"3D_NAV_TurnLeft", NULL,
		&R3D_NAV_TurnLeft
	},

	{	"3D_NAV_TurnRight", NULL,
		&R3D_NAV_TurnRight
	},

	{	"3D_NAV_MouseMove", NULL,
		&R3D_NAV_MouseMove
	},

	// backwards compatibility
	{	"3D_Click", NULL,
		&CMD_ACT_Click
	},

	// end of command list
	{	NULL, NULL, 0, NULL  }
};


void Render3D_RegisterCommands()
{
	M_RegisterCommandList(render_commands);
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
