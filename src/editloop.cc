//------------------------------------------------------------------------
//  EDIT LOOP
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2013 Andrew Apted
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
//
//  Based on Yadex which incorporated code from DEU 5.21 that was put
//  in the public domain in 1994 by Rapha�l Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "main.h"

#include "e_checks.h"
#include "editloop.h"
#include "e_cutpaste.h"
#include "r_grid.h"
#include "e_linedef.h"
#include "e_loadsave.h"
#include "e_sector.h"
#include "e_path.h"
#include "e_vertex.h"
#include "levels.h"
#include "objects.h"
#include "selectn.h"
#include "x_mirror.h"
#include "x_hover.h"
#include "r_render.h"
#include "ui_window.h"


Editor_State_t  edit;


int active_when = 0;  // FIXME MOVE THESE into Editor_State
int active_wmask = 0;



// config items
int default_edit_mode = 0;  // Things

bool digits_set_zoom = false;
bool mouse_wheel_scrolls_map = false;
bool same_mode_clears_selection = false; 

int multi_select_modifier = 0;


/*
 *  zoom_fit - adjust zoom factor to make level fit in window
 *
 *  Return 0 on success, non-zero on failure.
 */
static void zoom_fit()
{
	if (NumVertices == 0)
		return;

	double xzoom = 1;
	double yzoom = 1;

	int ScrMaxX = main_win->canvas->w();
	int ScrMaxY = main_win->canvas->h();

	if (Map_bound_x1 < Map_bound_x2)
		xzoom = ScrMaxX / (double)(Map_bound_x2 - Map_bound_x1);

	if (Map_bound_y1 < Map_bound_y2)
		yzoom = ScrMaxY / (double)(Map_bound_y2 - Map_bound_y1);

	grid.NearestScale(MIN(xzoom, yzoom));

	grid.CenterMapAt((Map_bound_x1 + Map_bound_x2) / 2, (Map_bound_y1 + Map_bound_y2) / 2);
}


static void UpdateSplitLine(int drag_vert = -1)
{
	edit.split_line.clear();

	// usually disabled while dragging stuff
	if (main_win->canvas->isDragActive() && edit.drag_single_vertex < 0)
		return;

	// in vertex mode, see if there is a linedef which would be split by
	// adding a new vertex

	if (edit.mode == OBJ_VERTICES && edit.pointer_in_window &&
	    edit.highlighted.is_nil())
	{
		GetSplitLineDef(edit.split_line, edit.map_x, edit.map_y, edit.drag_single_vertex);

		// NOTE: OK if the split line has one of its vertices selected
		//       (that case is handled by Insert_Vertex)
	}

	if (edit.split_line())
		main_win->canvas->SplitLineSet(edit.split_line.num);
	else
		main_win->canvas->SplitLineForget();
}


static void UpdatePanel()
{
	// -AJA- I think the highlighted object is always the same type as
	//       the current editing mode.  But do this check for safety.
	if (edit.highlighted() && edit.highlighted.type != edit.mode)
		return;


	// Choose object to show in right-hand panel:
	//   - the highlighted object takes priority
	//   - otherwise show the selection (first + count)
	// 
	// It's a little more complicated since highlight may or may not
	// be part of the selection.

	int obj_idx   = edit.highlighted.num;
	int obj_count = edit.Selected->count_obj();

	if (obj_idx >= 0)
	{
		if (! edit.Selected->get(obj_idx))
			obj_count = 0;
	}
	else if (obj_count > 0)
	{
		obj_idx = edit.Selected->find_first();
	}


	switch (edit.mode)
	{
		case OBJ_THINGS:
			main_win->thing_box->SetObj(obj_idx, obj_count);
			break;

		case OBJ_LINEDEFS:
			main_win->line_box->SetObj(obj_idx, obj_count);
			break;

		case OBJ_SECTORS:
			main_win->sec_box->SetObj(obj_idx, obj_count);
			break;

		case OBJ_VERTICES:
			main_win->vert_box->SetObj(obj_idx, obj_count);
			break;

		default: break;
	}
}


void UpdateHighlight()
{
	bool dragging = main_win->canvas->isDragActive();


	// find the object to highlight
	edit.highlighted.clear();

	if (edit.pointer_in_window &&
	    (!dragging || edit.drag_single_vertex >= 0))
	{
		GetCurObject(edit.highlighted, edit.mode, edit.map_x, edit.map_y, grid.snap);

		// guarantee that we cannot drag a vertex onto itself
		if (edit.drag_single_vertex >= 0 && edit.highlighted() &&
			edit.drag_single_vertex == edit.highlighted.num)
		{
			edit.highlighted.clear();
		}
	}


	if (edit.highlighted())
		main_win->canvas->HighlightSet(edit.highlighted);
	else
		main_win->canvas->HighlightForget();


	UpdateSplitLine();

	UpdatePanel();
}


bool GetCurrentObjects(selection_c *list)
{
	// returns false when there are no objects at all

	list->change_type(edit.mode);  // this also clears it

	if (edit.Selected->notempty())
	{
		list->merge(*edit.Selected);
		return true;
	}

	if (edit.highlighted())
	{
		list->set(edit.highlighted.num);
		return true;
	}

	return false;
}


void Editor_ClearErrorMode()
{
	if (edit.error_mode)
	{
		edit.error_mode = false;
		edit.Selected->clear_all();
		edit.RedrawMap = 1;
	}
}


void Editor_ChangeMode(char mode)
{
	obj_type_e  prev_type = edit.mode;

	// Set the object type according to the new mode.
	switch (mode)
	{
		case 't': edit.mode = OBJ_THINGS;   break;
		case 'l': edit.mode = OBJ_LINEDEFS; break;
		case 's': edit.mode = OBJ_SECTORS;  break;
		case 'v': edit.mode = OBJ_VERTICES; break;

		default:
			LogPrintf("INTERNAL ERROR: unknown mode %d\n", mode);
			return;
	}

	Editor_ClearAction();
	Editor_ClearErrorMode();

	edit.highlighted.clear();
	edit.split_line.clear();
	edit.did_a_move = false;

	if (prev_type != edit.mode)
	{
		main_win->NewEditMode(mode);

		// convert the selection
		selection_c *prev_sel = edit.Selected;
		edit.Selected = new selection_c(edit.mode);

		ConvertSelection(prev_sel, edit.Selected);
		delete prev_sel;
	}
	// -AJA- Yadex (DEU?) would clear the selection if the mode didn't
	//       change.  We optionally emulate that behavior here.
	else if (same_mode_clears_selection)
	{
		edit.Selected->clear_all();
	}

	UpdateHighlight();

	edit.RedrawMap = 1;
}


void CMD_Nothing(void)
{
	/* hey jude, don't make it bad */
}


void CMD_EditMode(void)
{
	char mode = tolower(EXEC_Param[0][0]);

	if (! mode || ! strchr("lstvr", mode))
	{
		Beep("Bad parameter for EditMode: '%s'", EXEC_Param[0]);
		return;
	}

	Editor_ChangeMode(mode);
}


void CMD_SelectAll(void)
{
	Editor_ClearErrorMode();

	int total = NumObjects(edit.mode);

	edit.Selected->change_type(edit.mode);
	edit.Selected->frob_range(0, total-1, BOP_ADD);
	edit.RedrawMap = 1;

	UpdateHighlight();
}


void CMD_UnselectAll(void)
{
	Editor_ClearErrorMode();

	edit.Selected->change_type(edit.mode);
	edit.Selected->clear_all();
	edit.RedrawMap = 1;

	UpdateHighlight();
}


void CMD_InvertSelection(void)
{
	Editor_ClearErrorMode();

	int total = NumObjects(edit.mode);

	if (edit.Selected->what_type() != edit.mode)
	{
		// convert the selection
		selection_c *prev_sel = edit.Selected;
		edit.Selected = new selection_c(edit.mode);

		ConvertSelection(prev_sel, edit.Selected);
		delete prev_sel;
	}

	edit.Selected->frob_range(0, total-1, BOP_TOGGLE);
	edit.RedrawMap = 1;

	UpdateHighlight();
}


void CMD_Quit(void)
{
	want_quit = true;
}


void CMD_SetVar(void)
{
	const char *var_name = EXEC_Param[0];
	const char *value    = EXEC_Param[0];

	if (! var_name[0])
	{
		Beep("Set: missing var name");
		return;
	}

	if (! value[0])
	{
		Beep("Set: missing value");
		return;
	}

	 int  int_val = atoi(value);
	bool bool_val = (int_val > 0);


	if (y_stricmp(var_name, "3d") == 0)
	{
		Editor_ClearAction();

		edit.render3d = bool_val;
		main_win->redraw();
	}
	else if (y_stricmp(var_name, "browser") == 0)
	{
		Editor_ClearAction();

		int want_vis   = bool_val ? 1 : 0;
		int is_visible = main_win->browser->visible() ? 1 : 0;

		if (want_vis != is_visible)
			main_win->ShowBrowser('/');
	}
	else if (y_stricmp(var_name, "grid") == 0)
	{
		grid.SetShown(bool_val);
	}
	else if (y_stricmp(var_name, "snap") == 0)
	{
		grid.SetSnap(bool_val);
	}
	else if (y_stricmp(var_name, "obj_nums") == 0)
	{
		edit.show_object_numbers = bool_val;
		edit.RedrawMap = 1;
	}
	else   // TODO: "skills"
	{
		Beep("Set: unknown var: %s", var_name);
	}
}


void CMD_ToggleVar(void)
{
	const char *var_name = EXEC_Param[0];

	if (! var_name[0])
	{
		Beep("Toggle: missing var name");
		return;
	}

	if (y_stricmp(var_name, "3d") == 0)
	{
		Editor_ClearAction();

		edit.render3d = ! edit.render3d;
		main_win->redraw();
	}
	else if (y_stricmp(var_name, "browser") == 0)
	{
		Editor_ClearAction();

		main_win->ShowBrowser('/');
	}
	else if (y_stricmp(var_name, "grid") == 0)
	{
		grid.ToggleShown();
	}
	else if (y_stricmp(var_name, "snap") == 0)
	{
		grid.ToggleSnap();
	}
	else if (y_stricmp(var_name, "obj_nums") == 0)
	{
		edit.show_object_numbers = ! edit.show_object_numbers;
		edit.RedrawMap = 1;
	}
	else if (y_stricmp(var_name, "skills") == 0)
	{
		active_wmask ^= 1;
		active_when = active_wmask;
		edit.RedrawMap = 1;
	}
	else
	{
		Beep("Toggle: unknown var: %s", var_name);
	}
}


static int mouse_last_x;
static int mouse_last_y;


void Editor_ClearAction()
{
	switch (edit.action)
	{
		case ACT_NOTHING:
			return;

		case ACT_WAIT_META:
			Status_Clear();
			break;
	
		case ACT_SCROLL_MAP:
		case ACT_ADJUST_OFS:
			main_win->SetCursor(FL_CURSOR_DEFAULT);
			break;

		default:
			/* no special for the rest */
			break;
	}

	edit.action = ACT_NOTHING;
}


void Editor_SetAction(editor_action_e  new_action)
{
	Editor_ClearAction();

	edit.action = new_action;

	switch (edit.action)
	{
		case ACT_NOTHING:
			return;

		case ACT_WAIT_META:
			Status_Set("META...");
			break;

		case ACT_SCROLL_MAP:
		case ACT_ADJUST_OFS:
			mouse_last_x = Fl::event_x();
			mouse_last_y = Fl::event_y();

			main_win->SetCursor(FL_CURSOR_HAND);
			break;

		default:
			/* no special for the rest */
			break;
	}
}


void CMD_MetaKey(void)
{
	Editor_SetAction(ACT_WAIT_META);
}


void CMD_BrowserMode(void)
{
	if (! EXEC_Param[0][0])
	{
		Beep("Missing parameter to CMD_BrowserMode");
		return;
	}

	char mode = toupper(EXEC_Param[0][0]);

	if (! (mode == 'L' || mode == 'S' || mode == 'O' ||
	       mode == 'T' || mode == 'F'))
	{
		Beep("Unknown browser mode: %s", EXEC_Param[0]);
		return;
	}

	main_win->ShowBrowser(mode);
}


void BR_CycleCategory(void)
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	int dir = (atoi(EXEC_Param[0]) >= 0) ? +1 : -1;

	main_win->browser->CycleCategory(dir);
}

void BR_ClearSearch(void)
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	main_win->browser->ClearSearchBox();
}


void BR_Scroll(void)
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	if (! EXEC_Param[0][0])
	{
		Beep("Missing parameter to BR_Scroll");
		return;
	}

	int delta = atoi(EXEC_Param[0]);

	main_win->browser->Scroll(delta);
}


void CMD_Scroll(void)
{
	// these are percentages
	int delta_x = atoi(EXEC_Param[0]);
	int delta_y = atoi(EXEC_Param[1]);

	if (delta_x == 0 && delta_y == 0)
	{
		Beep("Bad parameter to Scroll: '%s' %s'", EXEC_Param[0], EXEC_Param[1]);
		return;
	}

	delta_x = delta_x * main_win->canvas->w() / 100.0 / grid.Scale;
	delta_y = delta_y * main_win->canvas->h() / 100.0 / grid.Scale;

	grid.Scroll(delta_x, delta_y);
}


void CMD_Merge(void)
{
	switch (edit.mode)
	{
		case OBJ_VERTICES:
			VERT_Merge();
			break;

		case OBJ_LINEDEFS:
			LIN_MergeTwo();
			break;

		case OBJ_SECTORS:
			SEC_Merge();
			break;

		case OBJ_THINGS:
			TH_Merge();
			break;

		default:
			Beep("Cannot merge that");
			break;
	}
}


void CMD_Disconnect(void)
{
	switch (edit.mode)
	{
		case OBJ_VERTICES:
			VERT_Disconnect();
			break;

		case OBJ_LINEDEFS:
			LIN_Disconnect();
			break;

		case OBJ_SECTORS:
			SEC_Disconnect();
			break;

		case OBJ_THINGS:
			TH_Disconnect();
			break;

		default:
			Beep("Cannot disconnect that");
			break;
	}
}


void GRID_Step(void)
{
	int delta = atoi(EXEC_Param[0]);

	delta = (delta >= 0) ? +1 : -1;

	grid.AdjustStep(delta);
}


void Editor_DigitKey(keycode_t key)
{
	// [1] - [9]: set the grid size

	int digit = (key & 127) - '0';

	bool do_zoom = digits_set_zoom;

	if (key & MOD_SHIFT)
		do_zoom = !do_zoom;

	if (do_zoom)
	{
		float S1 = grid.Scale;
		grid.ScaleFromDigit(digit);
		grid.RefocusZoom(edit.map_x, edit.map_y, S1);
	}
	else
	{
		grid.StepFromDigit(digit);
	}
}


void Editor_Zoom(int delta, int mid_x, int mid_y)
{
    float S1 = grid.Scale;

    grid.AdjustScale(delta);

    grid.RefocusZoom(mid_x, mid_y, S1);
}

void CMD_Zoom(void)
{
	int delta = atoi(EXEC_Param[0]);

	if (delta == 0)
	{
		Beep("Bad parameter to CMD_Zoom");
		return;
	}

	Editor_Zoom(delta, edit.map_x, edit.map_y);
}


void CMD_ZoomWholeMap(void)
{
	if (MadeChanges)
		CalculateLevelBounds();

	zoom_fit();

	edit.RedrawMap = 1;
}


void CMD_ZoomSelection(void)
{
	if (edit.Selected->empty())
	{
		Beep("No selection to zoom");
		return;
	}

	GoToSelection();
}


void CMD_GoToCamera(void)
{
	int x, y;
	float angle;

	Render3D_GetCameraPos(&x, &y, &angle);

	grid.CenterMapAt(x, y);

	// FIXME: this is not right, we want to recompute where mouse pointer is
	edit.map_x = x;
	edit.map_y = y;

	edit.RedrawMap = 1;
}


void CMD_PlaceCamera(void)
{
	if (edit.render3d)
	{
		Beep("Not supported in 3D view");
		return;
	}

	if (! edit.pointer_in_window)
	{
		// IDEA: turn cursor into cross, wait for click in map window

		Beep("Mouse is not over map");
		return;
	}

	int x = edit.map_x;
	int y = edit.map_y;

	Render3D_SetCameraPos(x, y);

	if (isalpha(EXEC_Param[0][0]))  // open3d
	{
		edit.render3d = 1;
		main_win->redraw();
	}

	edit.RedrawMap = 1;
}


void CMD_CopyAndPaste(void)
{
	if (! (edit.Selected || edit.highlighted()))
	{
		Beep("Nothing to copy and paste");
		return;
	}

	edit.error_mode = false;

	if (CMD_Copy())
	{
		CMD_Paste();
	}
}


//------------------------------------------------------------------------


static void Editor_ScrollMap(int mode, int dx = 0, int dy = 0)
{
	// started?
	if (mode < 0)
	{
		Editor_SetAction(ACT_SCROLL_MAP);
		return;
	}

	// finished?
	if (mode > 0)
	{
		Editor_ClearAction();
		return;
	}


	keycode_t mod = Fl::event_state() & MOD_ALL_MASK;

	if (edit.render3d)
	{
		Render3D_RBScroll(dx, dy, mod);
	}
	else
	{
		int speed = 8;  // FIXME: CONFIG OPTION

		if (mod == MOD_SHIFT)
			speed /= 2;
		else if (mod == MOD_COMMAND)
			speed *= 2;

		double delta_x = ((double) -dx * speed / 8.0 / grid.Scale);
		double delta_y = ((double)  dy * speed / 8.0 / grid.Scale);

		grid.Scroll(delta_x, delta_y);
	}
}


//------------------------------------------------------------------------

int wheel_dx;
int wheel_dy;


int Editor_RawKey(int event)
{
	if (event == FL_KEYUP)
		return 0;

	bool convert_meta = (edit.action == ACT_WAIT_META);

	if (edit.action == ACT_WAIT_META)
		Editor_ClearAction();

	int raw_key   = Fl::event_key();
	int raw_state = Fl::event_state();

	if (convert_meta)
		raw_state = MOD_META;

	keycode_t key = M_TranslateKey(raw_key, raw_state);

	if (key == 0)
		return convert_meta ? 1 : 0;

	wheel_dx = wheel_dy = 0;

#if 0  // DEBUG
	fprintf(stderr, "Key code: 0x%08x : %s\n", key, M_KeyToString(key));
#endif

	// keyboard propagation logic

	// handle digits specially
	if ('1' <= (key & FL_KEY_MASK) && (key & FL_KEY_MASK) <= '9')
	{
		Editor_DigitKey(key);
		return 1;
	}

	if (main_win->browser->visible() && ExecuteKey(key, KCTX_Browser))
		return 1;

	if (edit.render3d && ExecuteKey(key, KCTX_Render))
		return 1;

	if (ExecuteKey(key, M_ModeToKeyContext(edit.mode)))
		return 1;
	
	if (ExecuteKey(key, KCTX_General))
		return 1;


	// NOTE: the key may still get handled by something (e.g. Menus)
	// fprintf(stderr, "Unknown key %d (0x%04x)\n", key, key);


	// prevent a META-fied key from being sent elsewhere, because it
	// won't really be META-fied anywhere else -- including the case
	// of it being sent back to this function as a SHORTCUT event.
	return convert_meta ? 1 : 0;
}


int Editor_RawWheel(int event)
{
	if (edit.action == ACT_WAIT_META)
		Editor_ClearAction();

	wheel_dx = Fl::event_dx();
	wheel_dy = Fl::event_dy();

	keycode_t mod = Fl::event_state() & MOD_ALL_MASK;

	// TODO: DistributeKey(EU_Wheel | mod)

	if (edit.render3d)
		Render3D_Wheel(0 - wheel_dy, mod);
	else
		Editor_Wheel(wheel_dx, wheel_dy, mod);

	return 1;
}


int Editor_RawButton(int event)
{
	if (edit.action == ACT_WAIT_META)
		Editor_ClearAction();

	int button = Fl::event_button();

	bool down = (event == FL_PUSH);

	// start scrolling the map?  [or moving in 3D view]
	if (button == 3)
	{
		Editor_ScrollMap(down ? -1 : +1);
		return 1;
	}

	// adjust offsets on a sidedef?
	if (edit.render3d && button == 2)
	{
		Render3D_AdjustOffsets(down ? -1 : +1);
		return 1;
	}

	if (! down)
	{
		if (Fl::event_button() == 2)
			Editor_MiddleRelease();
		else if (! edit.render3d)
			Editor_MouseRelease();
		return 1;
	}

	int mod = Fl::event_state() & MOD_ALL_MASK;

	if (Fl::event_button() == 2)
	{
		Editor_MiddlePress(mod);
	}
	else if (! edit.render3d)
	{
		Editor_MousePress(mod);
	}

	return 1;
}


int Editor_RawMouse(int event)
{
	int mod = Fl::event_state() & MOD_ALL_MASK;

	int dx = Fl::event_x() - mouse_last_x;
	int dy = Fl::event_y() - mouse_last_y;


	if (edit.action == ACT_SCROLL_MAP)
	{
		Editor_ScrollMap(0, dx, dy);
	}
	else if (edit.action == ACT_ADJUST_OFS)
	{
		Render3D_AdjustOffsets(0, dx, dy);
	}
	else if (edit.render3d)
	{
		// TODO: maybe highlight things/walls/sectors
	}
	else
	{
		int map_x, map_y;

		main_win->canvas->PointerPos(&map_x, &map_y);

		Editor_MouseMotion(Fl::event_x(), Fl::event_y(), mod,
						   map_x, map_y, event == FL_DRAG);
	}

	mouse_last_x = Fl::event_x();
	mouse_last_y = Fl::event_y();

	return 1;
}


//------------------------------------------------------------------------

void Editor_Wheel(int dx, int dy, keycode_t mod)
{
	if (mouse_wheel_scrolls_map && mod !=
#ifdef __APPLE__
		MOD_ALT)
#else
		MOD_COMMAND)
#endif
	{
		int speed = 12;  // FIXME: CONFIG OPTION

		if (mod == MOD_SHIFT)
			speed = MAX(1, speed / 3);

		grid.Scroll(  dx * (double) speed / grid.Scale,
		            - dy * (double) speed / grid.Scale);
	}
	else
	{
		dy = (dy > 0) ? +1 : -1;

		Editor_Zoom(- dy, edit.map_x, edit.map_y);
	}
}


void Editor_MousePress(keycode_t mod)
{
	if (edit.button_down >= 2)
		return;

	edit.button_down = 1;
	edit.button_mod  = mod;

	Objid object;      // The object under the pointer

	GetCurObject(object, edit.mode, edit.map_x, edit.map_y, grid.snap);

	edit.clicked = object;

	// clicking on an empty space starts a new selection box.

	if (object.is_nil())
	{
		main_win->canvas->SelboxBegin(edit.map_x, edit.map_y);
		return;
	}
}


void Editor_MouseRelease()
{
	edit.button_down = 0;

	Objid click_obj(edit.clicked);
	edit.clicked.clear();

	bool was_did_move = edit.did_a_move;
	edit.did_a_move = false;

	/* Releasing the button while dragging : drop the selection. */
	// FIXME : should call this automatically when switching tool
	if (main_win->canvas->isDragActive())
	{
		int dx, dy;
		main_win->canvas->DragFinish(&dx, &dy);

		if (! (dx==0 && dy==0))
		{
			CMD_MoveObjects(dx, dy);

			// next select action will clear the selection
			edit.did_a_move = true;
		}

		edit.drag_single_vertex = -1;
		edit.RedrawMap = 1;
		return;
	}

	// optional multi-select : require a certain modifier key
	if (multi_select_modifier &&
		edit.button_mod != (multi_select_modifier == 1 ? MOD_SHIFT : MOD_COMMAND))
	{
		was_did_move = true;
	}

	if (click_obj() && was_did_move)
	{
		edit.Selected->clear_all();
	}

	/* Releasing the button while there was a selection box
	   causes all the objects within the box to be selected.
	 */
	if (main_win->canvas->isSelboxActive())
	{
		Editor_ClearErrorMode();

		int x1, y1, x2, y2;
		main_win->canvas->SelboxFinish(&x1, &y1, &x2, &y2);

		// a mere click and release will unselect everything
		if (x1 == x2 && y1 == y2)
			CMD_UnselectAll();
		else
			SelectObjectsInBox(edit.Selected, edit.mode, x1, y1, x2, y2);

		UpdateHighlight();

		edit.RedrawMap = 1;
		return;
	}


	if (! click_obj())
		return;

	Objid object;      // object under the pointer

	GetCurObject(object, edit.mode, edit.map_x, edit.map_y, grid.snap);

	/* select the object if unselected, and vice versa.
	 */
	if (object() && object.num == click_obj.num)
	{
		Editor_ClearErrorMode();

		edit.Selected->toggle(object.num);

		edit.RedrawMap = 1;
		return;
	}
}


void Editor_MiddlePress(keycode_t mod)
{
	if (edit.button_down & 1)  // allow 0 or 2
		return;

	// ability to insert stuff via the mouse
	if (mod == 0)
	{
		EXEC_Param[0] = "";
		CMD_Insert();
		return;
	}

	if (edit.Selected->empty())
	{
		Beep("Nothing to scale");
		return;
	}

	edit.button_down = 2;
	edit.button_mod  = mod;

	int middle_x, middle_y;

	Objs_CalcMiddle(edit.Selected, &middle_x, &middle_y);

	main_win->canvas->ScaleBegin(edit.map_x, edit.map_y, middle_x, middle_y);
}


void Editor_MiddleRelease()
{
	edit.button_down = 0;

	if (main_win->canvas->isScaleActive())
	{
		scale_param_t param;

		main_win->canvas->ScaleFinish(param);

		CMD_ScaleObjects2(param);

		edit.RedrawMap = 1;
	}
}



void Editor_LeaveWindow()
{
	edit.pointer_in_window = false;
	
	UpdateHighlight();
}


void Editor_MouseMotion(int x, int y, keycode_t mod, int map_x, int map_y, bool drag)
{
	edit.map_x = map_x;
	edit.map_y = map_y;
	edit.pointer_in_window = true; // FIXME

	if (edit.pointer_in_window)
		main_win->info_bar->SetMouse(edit.map_x, edit.map_y);

	// fprintf(stderr, "MOUSE MOTION: %d,%d  map: %d,%d\n", x, y, edit.map_x, edit.map_y);

	if (edit.button_down == 2)
	{
		main_win->canvas->ScaleUpdate(edit.map_x, edit.map_y, mod);
		return;
	}

	if (! drag)
	{
		UpdateHighlight();
	}

	/* Moving the pointer with the left button pressed
	   and a selection box exists : move the second
	   corner of the selection box.
	*/
	else if (main_win->canvas->isSelboxActive())
	{
		if (edit.did_a_move)
			edit.Selected->clear_all();

		main_win->canvas->SelboxUpdate(edit.map_x, edit.map_y);
		return;
	}

	/* Moving the pointer with the left button pressed
	   but no selection box exists and [Ctrl] was not
	   pressed when the button was pressed :
	   drag the selection.
	*/
	if (main_win->canvas->isDragActive())
	{
		main_win->canvas->DragUpdate(edit.map_x, edit.map_y);

		// if dragging a single vertex, update the possible split_line
		UpdateHighlight();
		return;
	}

	/*
	   begin dragging?
	   TODO: require pixel dist from click point to be >= THRESHHOLD
	 */
	if (edit.button_down == 1 && edit.clicked())
	{
		if (! edit.Selected->get(edit.clicked.num))
		{
			if (edit.did_a_move)
				edit.Selected->clear_all();

			edit.Selected->set(edit.clicked.num);
			edit.did_a_move = false;
		}

		int focus_x, focus_y;

		GetDragFocus(&focus_x, &focus_y, edit.map_x, edit.map_y);

		main_win->canvas->DragBegin(focus_x, focus_y, edit.map_x, edit.map_y);

		// check for a single vertex
		edit.drag_single_vertex = -1;

		if (edit.mode == OBJ_VERTICES && edit.Selected->find_second() < 0)
		{
			edit.drag_single_vertex = edit.Selected->find_first();
			SYS_ASSERT(edit.drag_single_vertex >= 0);
		}

		// forget the highlight
		edit.highlighted.clear();
		main_win->canvas->HighlightForget();
		return;
	}
}


void Editor_Resize(int is_width, int is_height)
{
	edit.RedrawMap = 1;
}


void Editor_RegisterCommands()
{
	/* global | interface stuff */

	M_RegisterCommand("Nothing", &CMD_Nothing);

	M_RegisterCommand("Quit", &CMD_Quit);
	M_RegisterCommand("EditMode", &CMD_EditMode);
 	M_RegisterCommand("BrowserMode", &CMD_BrowserMode);

	M_RegisterCommand("Set",    &CMD_SetVar);
	M_RegisterCommand("Toggle", &CMD_ToggleVar);

	M_RegisterCommand("Check",   &CMD_CheckMap);
	M_RegisterCommand("MetaKey", &CMD_MetaKey);

	M_RegisterCommand("GivenFile", &CMD_GivenFile);
	M_RegisterCommand("FlipMap",   &CMD_FlipMap);

	M_RegisterCommand("SelectAll", &CMD_SelectAll);
	M_RegisterCommand("UnselectAll", &CMD_UnselectAll);
	M_RegisterCommand("InvertSelection", &CMD_InvertSelection);

	M_RegisterCommand("Scroll", &CMD_Scroll);
	M_RegisterCommand("GoToCamera",  &CMD_GoToCamera);
	M_RegisterCommand("PlaceCamera", &CMD_PlaceCamera);
	M_RegisterCommand("JumpToObject", &CMD_JumpToObject);

	M_RegisterCommand("Zoom",          &CMD_Zoom);
	M_RegisterCommand("ZoomWholeMap",  &CMD_ZoomWholeMap);
	M_RegisterCommand("ZoomSelection", &CMD_ZoomSelection);

	M_RegisterCommand("GRID_Step", &GRID_Step);

	/* global | map stuff */

	M_RegisterCommand("Insert", &CMD_Insert);
	M_RegisterCommand("Delete", &CMD_Delete);

	M_RegisterCommand("Mirror",   &CMD_Mirror);
	M_RegisterCommand("Rotate90", &CMD_Rotate90);
	M_RegisterCommand("Enlarge",  &CMD_Enlarge);
	M_RegisterCommand("Shrink",   &CMD_Shrink);

	M_RegisterCommand("Disconnect", &CMD_Disconnect);
	M_RegisterCommand("Merge",      &CMD_Merge);
	M_RegisterCommand("Quantize",   &CMD_Quantize);

	M_RegisterCommand("CopyAndPaste",   &CMD_CopyAndPaste);
	M_RegisterCommand("CopyProperties", &CMD_CopyProperties);

	M_RegisterCommand("ApplyTag",    &CMD_ApplyTag);
	M_RegisterCommand("PruneUnused", &CMD_PruneUnused);

	/* line */

	M_RegisterCommand("LIN_Flip", &LIN_Flip);
	M_RegisterCommand("LIN_SplitHalf", &LIN_SplitHalf);
	M_RegisterCommand("LIN_SelectPath", &LIN_SelectPath);

	M_RegisterCommand("LIN_AlignX", &LIN_AlignX);
	M_RegisterCommand("LIN_AlignY", &LIN_AlignY);

	/* sector */

	M_RegisterCommand("SEC_Floor", &SEC_Floor);
	M_RegisterCommand("SEC_Ceil",  &SEC_Ceil);
	M_RegisterCommand("SEC_Light", &SEC_Light);
	M_RegisterCommand("SEC_SelectGroup", &SEC_SelectGroup);
	M_RegisterCommand("SEC_SwapFlats", &SEC_SwapFlats);

	/* thing */

	M_RegisterCommand("TH_Spin", &TH_SpinThings);

	/* vertex */

	/* browser */

	M_RegisterCommand("BR_CycleCategory", &BR_CycleCategory);
	M_RegisterCommand("BR_ClearSearch", &BR_ClearSearch);
	M_RegisterCommand("BR_Scroll", &BR_Scroll);
}


void Editor_Init()
{
	memset(&edit, 0, sizeof(edit));  /* Catch-all */

	switch (default_edit_mode)
	{
		case 1:  edit.mode = OBJ_LINEDEFS; break;
		case 2:  edit.mode = OBJ_SECTORS;  break;
		case 3:  edit.mode = OBJ_VERTICES; break;
		default: edit.mode = OBJ_THINGS;   break;
	}

	edit.action = ACT_NOTHING;

	edit.render3d = false;
	edit.error_mode = false;

	edit.show_object_numbers = false;
	edit.show_things_squares = false;
	edit.show_things_sprites = true;

	edit.button_down = 0;
	edit.button_mod  = 0;
	edit.clicked.clear();

	edit.highlighted.clear();
	edit.split_line.clear();
	edit.drag_single_vertex = -1;

	edit.Selected = new selection_c(edit.mode);

	edit.did_a_move = false;

	grid.Init();

	MadeChanges = 0;

	Editor_RegisterCommands();
	Render3D_RegisterCommands();
}


bool Editor_ParseUser(const char ** tokens, int num_tok)
{
	if (strcmp(tokens[0], "edit_mode") == 0 && num_tok >= 2)
	{
		Editor_ChangeMode(tokens[1][0]);
		return true;
	}

	if (strcmp(tokens[0], "render_mode") == 0 && num_tok >= 2)
	{
		edit.render3d = atoi(tokens[1]);
		edit.RedrawMap = 1;
		return true;
	}

	if (strcmp(tokens[0], "show_object_numbers") == 0 && num_tok >= 2)
	{
		edit.show_object_numbers = atoi(tokens[1]);
		edit.RedrawMap = 1;
		return true;
	}

	return false;
}


void Editor_WriteUser(FILE *fp)
{
	switch (edit.mode)
	{
		case OBJ_THINGS:   fprintf(fp, "edit_mode t\n"); break;
		case OBJ_LINEDEFS: fprintf(fp, "edit_mode l\n"); break;
		case OBJ_SECTORS:  fprintf(fp, "edit_mode s\n"); break;
		case OBJ_VERTICES: fprintf(fp, "edit_mode v\n"); break;

		default: break;
	}

	fprintf(fp, "render_mode %d\n", edit.render3d ? 1 : 0);
	fprintf(fp, "show_object_numbers %d\n", edit.show_object_numbers ? 1 : 0);
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
