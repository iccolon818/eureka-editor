//------------------------------------------------------------------------
//  HIGHLIGHT HELPER
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2016 Andrew Apted
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

#include <algorithm>

#include "e_hover.h"
#include "e_main.h"		// Map_bound_xxx
#include "m_game.h"
#include "r_grid.h"


extern int vertex_radius(double scale);


float ApproxDistToLineDef(const LineDef * L, int x, int y)
{
	int x1 = L->Start()->x;
	int y1 = L->Start()->y;
	int x2 = L->End()->x;
	int y2 = L->End()->y;

	int dx = x2 - x1;
	int dy = y2 - y1;

	if (abs(dx) > abs(dy))
	{
		// The linedef is rather horizontal

		// case 1: x is to the left of the linedef
		//         hence return distance to the left-most vertex
		if (x < (dx > 0 ? x1 : x2))
			return hypot(x - (dx > 0 ? x1 : x2),
						 y - (dx > 0 ? y1 : y2));

		// case 2: x is to the right of the linedef
		//         hence return distance to the right-most vertex
		if (x > (dx > 0 ? x2 : x1))
			return hypot(x - (dx > 0 ? x2 : x1),
						 y - (dx > 0 ? y2 : y1));

		// case 3: x is in-between (and not equal to) both vertices
		//         hence use slope formula to get intersection point
		float y3 = y1 + (x - x1) * (float)dy / (float)dx;

		return fabs(y3 - y);
	}
	else
	{
		// The linedef is rather vertical

		if (y < (dy > 0 ? y1 : y2))
			return hypot(x - (dy > 0 ? x1 : x2),
						 y - (dy > 0 ? y1 : y2));

		if (y > (dy > 0 ? y2 : y1))
			return hypot(x - (dy > 0 ? x2 : x1),
						 y - (dy > 0 ? y2 : y1));

		float x3 = x1 + (y - y1) * (float)dx / (float)dy;

		return fabs(x3 - x);
	}
}


int ClosestLine_CastingHoriz(int x, int y, int *side)
{
	int   best_match = -1;
	float best_dist  = 9e9;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		int ly1 = LineDefs[n]->Start()->y;
		int ly2 = LineDefs[n]->End()->y;

		// ignore purely horizontal lines
		if (ly1 == ly2)
			continue;

		// does the linedef cross the horizontal ray?
		if (MIN(ly1, ly2) >= y + 1 || MAX(ly1, ly2) <= y)
			continue;

		int lx1 = LineDefs[n]->Start()->x;
		int lx2 = LineDefs[n]->End()->x;

		float dist = lx1 - (x + 0.5) + (lx2 - lx1) * (y + 0.5 - ly1) / (float)(ly2 - ly1);

		if (fabs(dist) < best_dist)
		{
			best_match = n;
			best_dist  = fabs(dist);

			if (side)
			{
				if (best_dist < 0.2)
					*side = 0;  // on the line
				else if ( (ly1 > ly2) == (dist > 0))
					*side = 1;  // right side
				else
					*side = -1; // left side
			}
		}
	}

	return best_match;
}


int ClosestLine_CastingVert(int x, int y, int *side)
{
	int   best_match = -1;
	float best_dist  = 9e9;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		int lx1 = LineDefs[n]->Start()->x;
		int lx2 = LineDefs[n]->End()->x;

		// ignore purely vertical lines
		if (lx1 == lx2)
			continue;

		// does the linedef cross the vertical ray?
		if (MIN(lx1, lx2) >= x + 1 || MAX(lx1, lx2) <= x)
			continue;

		int ly1 = LineDefs[n]->Start()->y;
		int ly2 = LineDefs[n]->End()->y;

		float dist = ly1 - (y + 0.5) + (ly2 - ly1) * (x + 0.5 - lx1) / (float)(lx2 - lx1);

		if (fabs(dist) < best_dist)
		{
			best_match = n;
			best_dist  = fabs(dist);

			if (side)
			{
				if (best_dist < 0.2)
					*side = 0;  // on the line
				else if ( (lx1 > lx2) == (dist < 0))
					*side = 1;  // right side
				else
					*side = -1; // left side
			}
		}
	}

	return best_match;
}


int ClosestLine_CastAtAngle(int x, int y, float radians)
{
	int   best_match = -1;
	float best_dist  = 9e9;

	double x2 = x + 256 * cos(radians);
	double y2 = y + 256 * sin(radians);

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		const LineDef *L = LineDefs[n];

		float a = PerpDist(L->Start()->x, L->Start()->y,  x, y, x2, y2);
		float b = PerpDist(L->  End()->x, L->  End()->y,  x, y, x2, y2);

		// completely on one side of the vector?
		if (a > 0 && b > 0) continue;
		if (a < 0 && b < 0) continue;

		float c = AlongDist(L->Start()->x, L->Start()->y,  x, y, x2, y2);
		float d = AlongDist(L->  End()->x, L->  End()->y,  x, y, x2, y2);

		float dist;

		if (fabs(a) < 1 && fabs(b) < 1)
			dist = MIN(c, d);
		else if (fabs(a) < 1)
			dist = c;
		else if (fabs(b) < 1)
			dist = d;
		else
		{
			float factor = a / (a - b);
			dist = c * (1 - factor) + d * factor;
		}

		// behind or touching the vector?
		if (dist < 1) continue;

		if (dist < best_dist)
		{
			best_match = n;
			best_dist  = dist;
		}
	}

	return best_match;
}


bool PointOutsideOfMap(int x, int y)
{
	// this keeps track of directions tested
	int dirs = 0;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		int lx1 = LineDefs[n]->Start()->x;
		int ly1 = LineDefs[n]->Start()->y;
		int lx2 = LineDefs[n]->End()->x;
		int ly2 = LineDefs[n]->End()->y;

		// does the linedef cross the horizontal ray?
		if (MIN(ly1, ly2) <= y && MAX(ly1, ly2) >= y + 1)
		{
			float dist = lx1 - (x + 0.5) + (lx2 - lx1) * (y + 0.5 - ly1) / (float)(ly2 - ly1);

			dirs |= (dist < 0) ? 1 : 2;

			if (dirs == 15) return false;
		}

		// does the linedef cross the vertical ray?
		if (MIN(lx1, lx2) <= x && MAX(lx1, lx2) >= x + 1)
		{
			float dist = ly1 - (y - 0.5) + (ly2 - ly1) * (x + 0.5 - lx1) / (float)(lx2 - lx1);

			dirs |= (dist < 0) ? 4 : 8;

			if (dirs == 15) return false;
		}
	}

	return true;
}


//------------------------------------------------------------------------

#define FASTOPP_DIST  320


typedef struct
{
	int ld;
	int ld_side;   // a SIDE_XXX value

	int * result_side;

	int dx, dy;

	// origin of casting line
	float x, y;

	bool is_horizontal;

	int   best_match;
	float best_dist;

public:
	void ComputeCastOrigin()
	{
		// choose a coordinate on the source line near the middle, but make
		// sure the casting line is not integral (i.e. lies between two lines
		// on the unit grid) so that we never directly hit a vertex.

		const LineDef * L = LineDefs[ld];

		dx = L->End()->x - L->Start()->x;
		dy = L->End()->y - L->Start()->y;

		is_horizontal = abs(dy) >= abs(dx);

		x = L->Start()->x + dx * 0.5;
		y = L->Start()->y + dy * 0.5;

		if (is_horizontal && (dy & 1) == 0 && abs(dy) > 0)
		{
			y = y + 0.5;
			x = x + 0.5 * dx / (float)dy;
		}

		if (!is_horizontal && (dx & 1) == 0 && abs(dx) > 0)
		{
			x = x + 0.5;
			y = y + 0.5 * dy / (float)dx;
		}
	}

	void ProcessLine(int n)
	{
		if (ld == n)  // ignore input line
			return;

		int nx1 = LineDefs[n]->Start()->x;
		int ny1 = LineDefs[n]->Start()->y;
		int nx2 = LineDefs[n]->End()->x;
		int ny2 = LineDefs[n]->End()->y;

		if (is_horizontal)
		{
			if (ny1 == ny2)
				return;

			if (MIN(ny1, ny2) > y || MAX(ny1, ny2) < y)
				return;

			float dist = nx1 + (nx2 - nx1) * (y - ny1) / (float)(ny2 - ny1) - x;

			if ( (dy < 0) == (ld_side > 0) )
				dist = -dist;

			if (dist > 0.2 && dist < best_dist)
			{
				best_match = n;
				best_dist  = dist;

				if (result_side)
				{
					if ( (dy > 0) != (ny2 > ny1) )
						*result_side = ld_side;
					else
						*result_side = -ld_side;
				}
			}
		}
		else  // casting a vertical ray
		{
			if (nx1 == nx2)
				return;

			if (MIN(nx1, nx2) > x || MAX(nx1, nx2) < x)
				return;

			float dist = ny1 + (ny2 - ny1) * (x - nx1) / (float)(nx2 - nx1) - y;

			if ( (dx > 0) == (ld_side > 0) )
				dist = -dist;

			if (dist > 0.2 && dist < best_dist)
			{
				best_match = n;
				best_dist  = dist;

				if (result_side)
				{
					if ( (dx > 0) != (nx2 > nx1) )
						*result_side = ld_side;
					else
						*result_side = -ld_side;
				}
			}
		}
	}

} opp_test_state_t;


class fastopp_node_c
{
public:
	int lo, hi;   // coordinate range
	int mid;

	fastopp_node_c * lo_child;
	fastopp_node_c * hi_child;

	std::vector<int> lines;

public:
	fastopp_node_c(int _low, int _high) :
		lo(_low), hi(_high),
		lo_child(NULL), hi_child(NULL),
		lines()
	{
		mid = (lo + hi) / 2;

		Subdivide();
	}

	~fastopp_node_c()
	{
		delete lo_child;
		delete hi_child;
	}

	/* horizontal tree */

	void AddLine_X(int ld, int x1, int x2)
	{
		if (lo_child && (x1 > lo_child->lo) &&
		                (x2 < lo_child->hi))
		{
			lo_child->AddLine_X(ld, x1, x2);
			return;
		}

		if (hi_child && (x1 > hi_child->lo) &&
		                (x2 < hi_child->hi))
		{
			hi_child->AddLine_X(ld, x1, x2);
			return;
		}

		lines.push_back(ld);
	}

	void AddLine_X(int ld)
	{
		const LineDef *L = LineDefs[ld];

		int x1 = MIN(L->Start()->x, L->End()->x);
		int x2 = MAX(L->Start()->x, L->End()->x);

		// can ignore purely vertical lines
		if (x1 == x2) return;

		AddLine_X(ld, x1, x2);
	}

	/* vertical tree */

	void AddLine_Y(int ld, int y1, int y2)
	{
		if (lo_child && (y1 > lo_child->lo) &&
		                (y2 < lo_child->hi))
		{
			lo_child->AddLine_Y(ld, y1, y2);
			return;
		}

		if (hi_child && (y1 > hi_child->lo) &&
		                (y2 < hi_child->hi))
		{
			hi_child->AddLine_Y(ld, y1, y2);
			return;
		}

		lines.push_back(ld);
	}

	void AddLine_Y(int ld)
	{
		const LineDef *L = LineDefs[ld];

		int y1 = MIN(L->Start()->y, L->End()->y);
		int y2 = MAX(L->Start()->y, L->End()->y);

		// can ignore purely horizonal lines
		if (y1 == y2) return;

		AddLine_Y(ld, y1, y2);
	}

	void Process(opp_test_state_t& test, float coord)
	{
		for (unsigned int k = 0 ; k < lines.size() ; k++)
			test.ProcessLine(lines[k]);

		if (! lo_child)
			return;

		// the AddLine() methods ensure that lines are not added
		// into a child bucket unless the end points are completely
		// inside it -- and one unit away from the extremes.
		//
		// hence we never need to recurse down BOTH sides here.

		if (coord < mid)
			lo_child->Process(test, coord);
		else
			hi_child->Process(test, coord);
	}

private:
	void Subdivide()
	{
		if (hi - lo <= FASTOPP_DIST)
			return;

		lo_child = new fastopp_node_c(lo, mid);
		hi_child = new fastopp_node_c(mid, hi);
	}
};


static fastopp_node_c * fastopp_X_tree;
static fastopp_node_c * fastopp_Y_tree;


void FastOpposite_Begin()
{
	CalculateLevelBounds();

	fastopp_X_tree = new fastopp_node_c(Map_bound_x1 - 8, Map_bound_x2 + 8);
	fastopp_Y_tree = new fastopp_node_c(Map_bound_y1 - 8, Map_bound_y2 + 8);

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		fastopp_X_tree->AddLine_X(n);
		fastopp_Y_tree->AddLine_Y(n);
	}
}


void FastOpposite_Finish()
{
	delete fastopp_X_tree;  fastopp_X_tree = NULL;
	delete fastopp_Y_tree;  fastopp_Y_tree = NULL;
}


int OppositeLineDef(int ld, int ld_side, int *result_side)
{
	// ld_side is either SIDE_LEFT or SIDE_RIGHT.
	// result_side uses the same values (never 0).

	opp_test_state_t  test;

	test.ld = ld;
	test.ld_side = ld_side;
	test.result_side = result_side;

	// this sets dx and dy
	test.ComputeCastOrigin();

	if (test.dx == 0 && test.dy == 0)
		return -1;

	test.best_match = -1;
	test.best_dist  = 9e9;

	if (fastopp_X_tree)
	{
		// fast way : use the binary tree

		if (test.is_horizontal)
			fastopp_Y_tree->Process(test, test.y);
		else
			fastopp_X_tree->Process(test, test.x);
	}
	else
	{
		// normal way : test all linedefs

		for (int n = 0 ; n < NumLineDefs ; n++)
			test.ProcessLine(n);
	}

	return test.best_match;
}


int OppositeSector(int ld, int ld_side)
{
	int opp_side;

	int opp = OppositeLineDef(ld, ld_side, &opp_side);

	// can see the void?
	if (opp < 0)
		return -1;

	return LineDefs[opp]->WhatSector(opp_side);
}


// result: -1 for back, +1 for front, 0 for _exactly_on_ the line
int PointOnLineSide(int x, int y, int lx1, int ly1, int lx2, int ly2)
{
	x   -= lx1; y   -= ly1;
	lx2 -= lx1; ly2 -= ly1;

	int tmp = (x * ly2 - y * lx2);

	return (tmp < 0) ? -1 : (tmp > 0) ? +1 : 0;
}


//------------------------------------------------------------------------


class Close_obj
{
public :
	Objid  obj;
	double distance;
	bool   inside;
	int    radius;

	Close_obj()
	{
		clear();
	}

	void clear()
	{
		obj.clear();
		distance = 9e9;
		radius   = (1 << 30);
		inside   = false;
	}

	bool operator== (const Close_obj& other) const
	{
		return (inside == other.inside &&
			    radius == other.radius &&
			    distance == other.distance) ? true : false;
	}

	bool operator< (const Close_obj& other) const
	{
		if (inside && ! other.inside) return true;
		if (! inside && other.inside) return false;

		// Small objects should "mask" large objects
		if (radius < other.radius) return true;
		if (radius > other.radius) return false;

		if (distance < other.distance) return true;
		if (distance > other.distance) return false;

		return radius < other.radius;
	}

	bool operator<= (const Close_obj& other) const
	{
		return *this == other || *this < other;
	}
};


extern int TestAdjoinerLineDef(int ld);


/*
 *  get_cur_linedef - determine which linedef is under the pointer
 */
static void get_cur_linedef(Close_obj& closest, int x, int y)
{
	// slack in map units
	int mapslack = 2 + (int)ceil(16.0f / grid.Scale);

	int lx = x - mapslack;
	int ly = y - mapslack;
	int hx = x + mapslack;
	int hy = y + mapslack;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		int x1 = LineDefs[n]->Start()->x;
		int y1 = LineDefs[n]->Start()->y;
		int x2 = LineDefs[n]->End()->x;
		int y2 = LineDefs[n]->End()->y;

		// Skip all lines of which all points are more than <mapslack>
		// units away from (x,y). In a typical level, this test will
		// filter out all the linedefs but a handful.
		if (MAX(x1,x2) < lx || MIN(x1,x2) > hx ||
		    MAX(y1,y2) < ly || MIN(y1,y2) > hy)
			continue;

		float dist = ApproxDistToLineDef(LineDefs[n], x, y);

		if (dist > mapslack)
			continue;

		// "<=" because if there are superimposed vertices, we want to
		// return the highest-numbered one.
		if (dist > closest.distance)
			continue;

		closest.obj.type = OBJ_LINEDEFS;
		closest.obj.num  = n;
		closest.distance = dist;
	}

#if 0  // TESTING CRUD
	if (closest.obj.type == OBJ_LINEDEFS)
	{
		closest.obj.num = TestAdjoinerLineDef(closest.obj.num);

		if (closest.obj.num < 0)
			closest.clear();
	}

	else if (closest.obj.type == OBJ_LINEDEFS)
	{
		closest.obj.num = OppositeLineDef(closest.obj.num, +1, NULL);

		if (closest.obj.num < 0)
			closest.clear();
	}
#endif
}


/*
 *  get_split_linedef - determine which linedef would be split if a
 *                      new vertex was added to the given point.
 */
static void get_split_linedef(Close_obj& closest, int x, int y, int ignore_vert)
{
	// slack in map units
	int mapslack = 1 + (int)ceil(8.0f / grid.Scale);

	int lx = x - mapslack;
	int ly = y - mapslack;
	int hx = x + mapslack;
	int hy = y + mapslack;

	for (int n = 0 ; n < NumLineDefs ; n++)
	{
		LineDef *L = LineDefs[n];

		if (L->start == ignore_vert || L->end == ignore_vert)
			continue;

		int x1 = L->Start()->x;
		int y1 = L->Start()->y;
		int x2 = L->End()->x;
		int y2 = L->End()->y;

		if (MAX(x1,x2) < lx || MIN(x1,x2) > hx ||
		    MAX(y1,y2) < ly || MIN(y1,y2) > hy)
			continue;

		// skip linedef if point matches a vertex
		if (x == x1 && y == y1) continue;
		if (x == x2 && y == y2) continue;

		// skip linedef if too small to split
		if (abs(L->Start()->x - L->End()->x) < 4 &&
			abs(L->Start()->y - L->End()->y) < 4)
			continue;

		float dist = ApproxDistToLineDef(L, x, y);

		if (dist > mapslack)
			continue;

		if (dist > closest.distance)
			continue;

		closest.obj.type = OBJ_LINEDEFS;
		closest.obj.num  = n;
		closest.distance = dist;
	}
}


/*
 *  get_cur_sector - determine which sector is under the pointer
 */
static void get_cur_sector(Close_obj& closest,int x, int y)
{
	/* hack, hack...  I look for the first LineDef crossing
	   an horizontal half-line drawn from the cursor */

	// -AJA- updated this to look in four directions (N/S/E/W) and
	//       grab the closest linedef.  Now it is possible to access
	//       self-referencing lines, even purely horizontal ones.

	int side1, side2;

	int line1 = ClosestLine_CastingHoriz(x, y, &side1);
  	int line2 = ClosestLine_CastingVert (x, y, &side2);

	if (line2 < 0)
	{
		/* nothing needed */
	}
	else if (line1 < 0 ||
	         ApproxDistToLineDef(LineDefs[line2], x, y) <
	         ApproxDistToLineDef(LineDefs[line1], x, y))
	{
		line1 = line2;
		side1 = side2;
	}

	// grab the sector reference from the appropriate side
	// (Note that side1 = +1 for right, -1 for left, 0 for "on").
	if (line1 >= 0)
	{
		int sd_num = (side1 < 0) ? LineDefs[line1]->left : LineDefs[line1]->right;

		if (sd_num >= 0)
		{
			closest.obj.type = OBJ_SECTORS;
			closest.obj.num  = SideDefs[sd_num]->sector;
		}
	}
}


/*
 *  get_cur_thing - determine which thing is under the pointer
 */
static void get_cur_thing(Close_obj& closest, int x, int y)
{
	int mapslack = 1 + (int)ceil(16.0f / grid.Scale);

	int max_radius = MAX_RADIUS + mapslack;

	int lx = x - max_radius;
	int ly = y - max_radius;
	int hx = x + max_radius;
	int hy = y + max_radius;

	for (int n = 0 ; n < NumThings ; n++)
	{
		int tx = Things[n]->x;
		int ty = Things[n]->y;

		// Filter out things that are farther than <max_radius> units away.
		if (tx < lx || tx > hx || ty < ly || ty > hy)
			continue;

		const thingtype_t *info = M_GetThingType(Things[n]->type);

		// So how far is that thing exactly ?

		int thing_radius = info->radius + mapslack;

		if (x < tx - thing_radius || x > tx + thing_radius ||
		    y < ty - thing_radius || y > ty + thing_radius)
			continue;

		Close_obj current;

		current.obj.type = OBJ_THINGS;
		current.obj.num  = n;
		current.distance = hypot(x - tx, y - ty);
		current.radius   = info->radius;
		current.inside   = x > tx - current.radius
			&& x < tx + current.radius
			&& y > ty - current.radius
			&& y < ty + current.radius;

		// "<=" because if there are superimposed vertices, we want to
		// return the highest-numbered one.
		if (current <= closest)
			closest = current;
	}
}


/*
 *  get_cur_vertex - determine which vertex is under the pointer
 */
static void get_cur_vertex(Close_obj& closest, int x, int y)
{
	const int screen_pix = vertex_radius(grid.Scale);

	int mapslack = 1 + (int)ceil((4 + screen_pix) / grid.Scale);

	int lx = x - mapslack;
	int ly = y - mapslack;
	int hx = x + mapslack;
	int hy = y + mapslack;

	for (int n = 0 ; n < NumVertices ; n++)
	{
		int vx = Vertices[n]->x;
		int vy = Vertices[n]->y;

		/* Filter out objects that are farther than <radius> units away. */
		if (vx < lx || vx > hx || vy < ly || vy > hy)
			continue;

		double dist = hypot(x - vx, y - vy);

		// "<=" because if there are superimposed vertices, we want to
		// return the highest-numbered one.
		if (dist > closest.distance)
			continue;

		closest.obj.type = OBJ_VERTICES;
		closest.obj.num  = n;
		closest.distance = dist;
	}
}


/*
 *  GetNearObject - determine which object is under the pointer
 *
 *  Set <o> to point to the object under the pointer (map
 *  coordinates (<x>, <y>). If several objects are close
 *  enough to the pointer, the smallest object is chosen.
 */
void GetNearObject(Objid& o, obj_type_e objtype, int x, int y)
{
	Close_obj closest;

	switch (objtype)
	{
		case OBJ_THINGS:
		{
			get_cur_thing(closest, x, y);
			o = closest.obj;
			break;
		}

		case OBJ_VERTICES:
		{
			get_cur_vertex(closest, x, y);
			o = closest.obj;
			break;
		}

		case OBJ_LINEDEFS:
		{
			get_cur_linedef(closest, x, y);
			o = closest.obj;
			break;
		}

		case OBJ_SECTORS:
		{
			get_cur_sector(closest, x, y);
			o = closest.obj;
			break;
		}

		default:
			BugError("GetNearObject: bad objtype %d", (int) objtype);
			break; /* NOT REACHED */
	}
}


void GetSplitLineDef(Objid& o, int x, int y, int drag_vert)
{
	Close_obj closest;

	get_split_linedef(closest, x, y, drag_vert);

	o = closest.obj;

	// don't highlight the line if the new vertex would snap onto
	// the same coordinate as the start or end of the linedef.
	// [ I tried a bbox test here, but it's bad for axis-aligned lines ]

	if (o.valid() && grid.snap)
	{
		int snap_x = grid.SnapX(x);
		int snap_y = grid.SnapY(y);

		const LineDef * L = LineDefs[o.num];

		if ( (L->Start()->x == snap_x && L->Start()->y == snap_y) ||
			 (L->  End()->x == snap_x && L->  End()->y == snap_y) )
		{
			o.clear();
		}

		// also require snap coordinate be not TOO FAR from the line
		double len = L->CalcLength();

		double along = AlongDist(snap_x, snap_y, L->Start()->x, L->Start()->y, L->End()->x, L->End()->y);
		double  perp =  PerpDist(snap_x, snap_y, L->Start()->x, L->Start()->y, L->End()->x, L->End()->y);

		if (along <= 0 || along >= len || fabs(perp) > len * 0.2)
		{
			o.clear();
		}
	}
}


void GetSplitLineForDangler(Objid& o, int v_num)
{
	Close_obj closest;

	get_split_linedef(closest, Vertices[v_num]->x, Vertices[v_num]->y, v_num);

	o = closest.obj;
}


bool FindClosestCrossPoint(int v1, int v2, cross_state_t *cross)
{
	SYS_ASSERT(v1 != v2);

	cross->vert = -1;
	cross->line = -1;

	const Vertex *VA = Vertices[v1];
	const Vertex *VB = Vertices[v2];

	int x1 = VA->x;
	int y1 = VA->y;
	int x2 = VB->x;
	int y2 = VB->y;

	int dx = x2 - x1;
	int dy = y2 - y1;

	// same coords?  (generally should not happen, handle it anyway)
	if (dx == 0 && dy == 0)
		return false;

	double length = sqrt(dx*dx + dy*dy);

	double epsilon = 0.4;

	// when zooming out, make it easier to hit a vertex
	double sk = 1.0 / grid.Scale;
	double close_dist = 8 * sqrt(sk);

	close_dist = CLAMP(1.2, close_dist, 24.0);

	double best_dist = 9e9;

	/* try all vertices */

	for (int v = 0 ; v < NumVertices ; v++)
	{
		if (v == v1 || v == v2)
			continue;

		const Vertex * VC = Vertices[v];

		// ignore vertices ar same coordinates as v1 or v2
		if (VC->x == VA->x && VC->y == VA->y) continue;
		if (VC->x == VB->x && VC->y == VB->y) continue;

		// is this vertex sitting on the line?
#if 0
		if (x1 == x2)
		{
			if (VC->x != x1)
				continue;
		}
		else if (y1 == y2)
		{
			if (VC->y != y1)
				continue;
		}
		else
#endif
		{
			double perp = PerpDist(VC->x, VC->y, x1,y1, x2,y2);

			if (fabs(perp) > close_dist)
				continue;
		}

		double along = AlongDist(VC->x, VC->y, x1,y1, x2,y2);

		if (along < epsilon || along > length - epsilon)
			continue;

		// yes it is

		if (along < best_dist)
		{
			best_dist = along;

			cross->vert = v;
			cross->line = -1;

			cross->x = VC->x;
			cross->y = VC->y;
		}
	}


	/* try all linedefs */

	for (int ld = 0 ; ld < NumLineDefs ; ld++)
	{
		const LineDef * L = LineDefs[ld];

		// only need to handle cases where this linedef distinctly crosses
		// the new line (i.e. start and end are clearly on opposite sides).

		double lx1 = L->Start()->x;
		double ly1 = L->Start()->y;
		double lx2 = L->End()->x;
		double ly2 = L->End()->y;

		double a = PerpDist(lx1,ly1, x1,y1, x2,y2);
		double b = PerpDist(lx2,ly2, x1,y1, x2,y2);

		if (! ((a < -epsilon && b > epsilon) || (a > epsilon && b < -epsilon)))
			continue;

		// compute intersection point
		double l_along = a / (a - b);

		double ix = lx1 + l_along * (lx2 - lx1);
		double iy = ly1 + l_along * (ly2 - ly1);

		int new_x = I_ROUND(ix);
		int new_y = I_ROUND(iy);

		// ensure new vertex does not match the start or end points
		if (new_x == x1 && new_y == y1) continue;
		if (new_x == x2 && new_y == y2) continue;

		double along = AlongDist(new_x, new_y,  x1,y1, x2,y2);

		if (along < epsilon || along > length - epsilon)
			continue;

		// OK, this linedef crosses it

fprintf(stderr, "linedef #%d crosses at (%1.3f %1.3f)  along=%1.3f\n", ld, ix, iy, along);

		// allow vertices to win over a nearby linedef
		along += close_dist * 2;

		if (along < best_dist)
		{
			best_dist = along;

			cross->vert = -1;
			cross->line = ld;

			cross->x = new_x;
			cross->y = new_y;
		}
	}


	return (cross->vert >= 0) || (cross->line >= 0);
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab