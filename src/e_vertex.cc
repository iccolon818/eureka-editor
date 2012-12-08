//------------------------------------------------------------------------
//  VERTEX OPERATIONS
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2012 Andrew Apted
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

#include "editloop.h"
#include "e_linedef.h"
#include "e_vertex.h"
#include "m_dialog.h"
#include "m_bitvec.h"
#include "r_grid.h"
#include "levels.h"
#include "objects.h"
#include "selectn.h"
#include "w_rawdef.h"
#include "x_mirror.h"



// FIXME: InsertPolygonVertices
#if 0

/*
   insert the vertices of a new polygon
*/
void InsertPolygonVertices (int centerx, int centery, int sides, int radius)
{
	for (int n = 0 ; n < sides ; n++)
	{
		DoInsertObject (OBJ_VERTICES, -1,
				centerx + (int) ((double)radius * cos (2*M_PI * (double)n / (double)sides)),
				centery + (int) ((double)radius * sin (2*M_PI * (double)n / (double)sides)));
	}
}
#endif


void MergeVertex(int v1, int v2, bool keep_v1)
{
	SYS_ASSERT(v1 >= 0 && v2 >= 0);
	SYS_ASSERT(v1 != v2);

	// update any linedefs which use V1 to use V2 instead
	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		const LineDef *L = LineDefs[n];

		// handle a line that exists between the two vertices
		if ((L->start == v1 && L->end == v2) ||
			(L->start == v2 && L->end == v1))
		{
			// we simply skip it, hence when V1 is deleted this line
			// will automatically be deleted too (as it refers to V1).
			// Clever huh?
			continue;
		}

		if (L->start == v1)
			BA_ChangeLD(n, LineDef::F_START, v2);

		if (L->end == v1)
			BA_ChangeLD(n, LineDef::F_END, v2);
	}

	// delete V1

	if (! keep_v1)
	{
		BA_Delete(OBJ_VERTICES, v1);
	}
}


int VertexHowManyLineDefs(int v_num)
{
	int count = 0;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		LineDef *L = LineDefs[n];

		if (L->start == v_num || L->end == v_num)
			count++;
	}

	return count;
}


static void CalcDisconnectCoord(const LineDef *L, int v_num, int *x, int *y)
{
	const Vertex * V = Vertices[v_num];

	int dx = L->End()->x - L->Start()->x;
	int dy = L->End()->y - L->Start()->y;

	if (L->end == v_num)
	{
		dx = -dx;
		dy = -dy;
	}

	if (abs(dx) < 4 && abs(dy) < 4)
	{
		dx = dx / 2;
		dy = dy / 2;
	}
	else if (abs(dx) < 16 && abs(dy) < 16)
	{
		dx = dx / 4;
		dy = dy / 4;
	}
	else if (abs(dx) >= abs(dy))
	{
		dy = dy * 8 / abs(dx);
		dx = (dx < 0) ? -8 : 8;
	}
	else
	{
		dx = dx * 8 / abs(dy);
		dy = (dy < 0) ? -8 : 8;
	}

	*x = V->x + dx;
	*y = V->y + dy;
}


static void DoDisconnectVertex(int v_num, int num_lines)
{
	int which = 0;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		LineDef *L = LineDefs[n];

		if (L->start == v_num || L->end == v_num)
		{
			int new_x, new_y;

			CalcDisconnectCoord(L, v_num, &new_x, &new_y);
			
			// the _LAST_ linedef keeps the current vertex, the rest
			// need a new one.
			if (which != num_lines-1)
			{
				int new_v = BA_New(OBJ_VERTICES);

				Vertices[new_v]->x = new_x;
				Vertices[new_v]->y = new_y;

				if (L->start == v_num)
					BA_ChangeLD(n, LineDef::F_START, new_v);
				else
					BA_ChangeLD(n, LineDef::F_END, new_v);
			}
			else
			{
				BA_ChangeVT(v_num, Vertex::F_X, new_x);
				BA_ChangeVT(v_num, Vertex::F_Y, new_y);
			}

			which++;
		}
	}
}


void CMD_MergeVertices()
{
	if (edit.Selected->count_obj() < 2)
	{
		Beep();
		return;
	}

	// the first vertex is kept (but moved to the middle coordinate),
	// all the other vertices are removed.

	int new_x, new_y;

	Objs_CalcMiddle(edit.Selected, &new_x, &new_y);

	int v = edit.Selected->find_first();

	edit.Selected->clear(v);

	BA_Begin();

	BA_ChangeVT(v, Vertex::F_X, new_x);
	BA_ChangeVT(v, Vertex::F_Y, new_y);

	selection_iterator_c it;

	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		MergeVertex(*it, v, true /* keep_v1 */);
	}

	DeleteObjects(edit.Selected);

	BA_End();

	edit.Selected->clear_all();
	edit.Selected->set(v);
}


void CMD_DisconnectVertices()
{
	if (edit.Selected->empty())
	{
		if (! edit.highlighted())
		{
			Beep();
			return;
		}

		edit.Selected->set(edit.highlighted.num);
	}

	bool seen_one = false;

	BA_Begin();

	selection_iterator_c it;

	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		int v_num = *it;

		// nothing to do unless vertex has 2 or more linedefs
		int num_lines = VertexHowManyLineDefs(*it);
		
		if (num_lines < 2)
			continue;

		DoDisconnectVertex(v_num, num_lines);

		seen_one = true;
	}

	if (! seen_one)
		Beep();

	BA_End();

	edit.Selected->clear_all();
}


static void DoDisconnectLineDef(int ld, int which_vert, bool *seen_one)
{
	LineDef *L = LineDefs[ld];

	int v_num = which_vert ? L->end : L->start;

	// see if there are any linedefs NOT in the selection which are
	// connected to this vertex.

	bool touches_non_sel = false;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		if (edit.Selected->get(n))
			continue;

		LineDef *N = LineDefs[n];

		if (N->start == v_num || N->end == v_num)
		{
			touches_non_sel = true;
			break;
		}
	}

	if (! touches_non_sel)
		return;

	int new_x, new_y;

	CalcDisconnectCoord(LineDefs[ld], v_num, &new_x, &new_y);

	int new_v = BA_New(OBJ_VERTICES);

	Vertices[new_v]->x = new_x;
	Vertices[new_v]->y = new_y;

	// fix all linedefs in the selection to use this new vertex

	selection_iterator_c it;

	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		LineDef *L2 = LineDefs[*it];

		if (L2->start == v_num)
			BA_ChangeLD(*it, LineDef::F_START, new_v);

		if (L2->end == v_num)
			BA_ChangeLD(*it, LineDef::F_END, new_v);
	}

	*seen_one = true;
}


void CMD_DisconnectLineDefs()
{
	// Note: the logic here is significantly different than the logic
	//       in CMD_DisconnectVertices, since we want to keep linedefs
	//       in the selection connected, and only disconnect from
	//       linedefs NOT in the selection.
	//
	// Hence need separate code for this.

	bool unselect = false;

	if (edit.Selected->empty())
	{
		if (! edit.highlighted())
		{
			Beep();
			return;
		}

		edit.Selected->set(edit.highlighted.num);
		unselect = true;
	}

	bool seen_one = false;

	BA_Begin();

	selection_iterator_c it;

	for (edit.Selected->begin(&it) ; !it.at_end() ; ++it)
	{
		DoDisconnectLineDef(*it, 0, &seen_one);
		DoDisconnectLineDef(*it, 1, &seen_one);
	}

	BA_End();

	if (! seen_one)
		Beep();

	if (unselect)
		edit.Selected->clear_all();
}


static void VerticesOfDetachableSectors(selection_c &verts)
{
	bitvec_c  in_verts(NumVertices);
	bitvec_c out_verts(NumVertices);

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		const LineDef * L = LineDefs[n];

		bool innie = false;
		bool outie = false;

		// TODO: what about no-sided lines??

		if (L->Right())
		{
			if (edit.Selected->get(L->Right()->sector))
				innie = true;
			else
				outie = true;
		}

		if (L->Left())
		{
			if (edit.Selected->get(L->Left()->sector))
				innie = true;
			else
				outie = true;
		}

		if (innie)
		{
			in_verts.set(L->start);
			in_verts.set(L->end);
		}

		if (outie)
		{
			out_verts.set(L->start);
			out_verts.set(L->end);
		}
	}

	for (int k = 0 ; k < NumVertices ; k++)
	{
		if (in_verts.get(k) && out_verts.get(k))
			verts.set(k);
	}
}


static void DETSEC_DisconnectCoord(int v_num, int *x, int *y)
{
	// FIXME

	*x = Vertices[v_num]->x + 5;
	*y = Vertices[v_num]->y + 9;
}


static void DETSEC_AddNewLine(int ld_num, int start2, int end2, int in_side)
{
	const LineDef * L1 = LineDefs[ld_num];

	int new_ld = BA_New(OBJ_LINEDEFS);

	LineDef * L2 = LineDefs[new_ld];

	L2->flags = MLF_Blocking;

	if (in_side == SIDE_LEFT)
	{
		L2->start = end2;
		L2->end   = start2;
		L2->right = L1->left;

		BA_ChangeLD(ld_num, LineDef::F_LEFT, -1);
	}
	else
	{
		L2->start = start2;
		L2->end   = end2;
		L2->right = L1->right;

		BA_ChangeLD(ld_num, LineDef::F_RIGHT, -1);

		FlipLineDef(ld_num);
	}

	BA_ChangeLD(ld_num, LineDef::F_FLAGS, L1->flags | MLF_Blocking);
}



void CMD_DisconnectSectors()
{
	if (NumVertices == 0)
	{
		Beep();
		return;
	}

	int n;
	bool unselect = false;

	if (edit.Selected->empty())
	{
		if (! edit.highlighted())
		{
			Beep();
			return;
		}

		edit.Selected->set(edit.highlighted.num);
		unselect = true;
	}

	// collect all vertices which need to be detached
	selection_c detach_verts(OBJ_VERTICES);
	selection_iterator_c it;

	VerticesOfDetachableSectors(detach_verts);

	if (detach_verts.empty())
	{
		Beep();
		return;
	}


	BA_Begin();

	// create new vertices, and a mapping from old --> new

	int * mapping = new int[NumVertices];

	for (n = 0 ; n < NumVertices ; n++)
		mapping[n] = -1;

	for (detach_verts.begin(&it) ; !it.at_end() ; ++it)
	{
		int new_v = BA_New(OBJ_VERTICES);

		Vertex *newbie = Vertices[new_v];

		mapping[*it] = new_v;

		DETSEC_DisconnectCoord(*it, &newbie->x, &newbie->y);

fprintf(stderr, "New vertex #%d at (%d %d)   mapped from %d\n",
        new_v, newbie->x, newbie->y, *it);
	}

	// update linedefs, creating new ones where necessary
	// (go backwards so we don't visit newly created lines)

	for (n = NumLineDefs-1 ; n >= 0 ; n--)
	{
		const LineDef * L = LineDefs[n];

		// only process lines which touch a selected sector
		bool  left_in = L->Left()  && edit.Selected->get(L->Left()->sector);
		bool right_in = L->Right() && edit.Selected->get(L->Right()->sector);

		if (! (left_in || right_in))
			continue;

		bool between_two = (left_in && right_in);

		int start2 = mapping[L->start];
		int end2   = mapping[L->end];

fprintf(stderr, "Line #%d : start2 = %d  end2 = %d\n", n, start2, end2);

		if (start2 >= 0 && end2 >= 0 && L->TwoSided() && ! between_two)
		{
			DETSEC_AddNewLine(n, start2, end2, left_in ? SIDE_LEFT : SIDE_RIGHT);
		}
		else if (start2 >= 0)
		{
			BA_ChangeLD(n, LineDef::F_START, start2);
		}
		else if (end2 >= 0)
		{
			BA_ChangeLD(n, LineDef::F_END, end2);
		}
	}

	BA_End();

	if (unselect)
		edit.Selected->clear_all();
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
