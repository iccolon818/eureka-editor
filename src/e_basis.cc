//------------------------------------------------------------------------
//  BASIC OBJECT HANDLING
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2019 Andrew Apted
//  Copyright (C) 1997-2003 André Majorel et al
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
//  in the public domain in 1994 by Raphaël Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "e_basis.h"

#include "Errors.h"
#include "Instance.h"
#include "main.h"

// need these for the XXX_Notify() prototypes
#include "r_render.h"

int global::default_floor_h		=   0;
int global::default_ceil_h		= 128;
int global::default_light_level	= 176;

namespace global
{
	static StringTable basis_strtab;
}

const char *NameForObjectType(ObjType type, bool plural)
{
	switch (type)
	{
	case ObjType::things:   return plural ? "things" : "thing";
	case ObjType::linedefs: return plural ? "linedefs" : "linedef";
	case ObjType::sidedefs: return plural ? "sidedefs" : "sidedef";
	case ObjType::vertices: return plural ? "vertices" : "vertex";
	case ObjType::sectors:  return plural ? "sectors" : "sector";

	default:
		BugError("NameForObjectType: bad type: %d\n", (int)type);
		return "XXX"; /* NOT REACHED */
	}
}

int BA_InternaliseString(const SString &str)
{
	return global::basis_strtab.add(str);
}

SString BA_GetString(int offset)
{
	return global::basis_strtab.get(offset);
}


fixcoord_t Instance::MakeValidCoord(double x) const
{
	if (loaded.levelFormat == MapFormat::udmf)
		return TO_COORD(x);

	// in standard format, coordinates must be integral
	return TO_COORD(I_ROUND(x));
}

//
// Set raw x/y/height
//
void Thing::SetRawX(const Instance &inst, double x)
{
	raw_x = inst.MakeValidCoord(x);
}
void Thing::SetRawY(const Instance &inst, double y)
{
	raw_y = inst.MakeValidCoord(y);
}
void Thing::SetRawH(const Instance &inst, double h)
{
	raw_h = inst.MakeValidCoord(h);
}
void Vertex::SetRawX(const Instance &inst, double x)
{
	raw_x = inst.MakeValidCoord(x);
}
void Vertex::SetRawY(const Instance &inst, double y)
{
	raw_y = inst.MakeValidCoord(y);
}

SString Sector::FloorTex() const
{
	return global::basis_strtab.get(floor_tex);
}

SString Sector::CeilTex() const
{
	return global::basis_strtab.get(ceil_tex);
}

void Sector::SetDefaults(const ConfigData &config)
{
	floorh = global::default_floor_h;
	 ceilh = global::default_ceil_h;

	floor_tex = BA_InternaliseString(config.default_floor_tex);
	 ceil_tex = BA_InternaliseString(config.default_ceil_tex);

	light = global::default_light_level;
}


SString SideDef::UpperTex() const
{
	return global::basis_strtab.get(upper_tex);
}

SString SideDef::MidTex() const
{
	return global::basis_strtab.get(mid_tex);
}

SString SideDef::LowerTex() const
{
	return global::basis_strtab.get(lower_tex);
}

void SideDef::SetDefaults(const Instance &inst, bool two_sided, int new_tex)
{
	if (new_tex < 0)
		new_tex = BA_InternaliseString(inst.conf.default_wall_tex);

	lower_tex = new_tex;
	upper_tex = new_tex;

	if (two_sided)
		mid_tex = BA_InternaliseString("-");
	else
		mid_tex = new_tex;
}


Sector * SideDef::SecRef(const Document &doc) const
{
	return doc.sectors[sector].get();
}

Vertex * LineDef::Start(const Document &doc) const
{
	return doc.vertices[start].get();
}

Vertex * LineDef::End(const Document &doc) const
{
	return doc.vertices[end].get();
}

SideDef * LineDef::Right(const Document &doc) const
{
	return (right >= 0) ? doc.sidedefs[right].get() : nullptr;
}

SideDef * LineDef::Left(const Document &doc) const
{
	return (left >= 0) ? doc.sidedefs[left].get() : nullptr;
}


bool LineDef::TouchesSector(int sec_num, const Document &doc) const
{
	if (right >= 0 && doc.sidedefs[right]->sector == sec_num)
		return true;

	if (left >= 0 && doc.sidedefs[left]->sector == sec_num)
		return true;

	return false;
}


int LineDef::WhatSector(Side side, const Document &doc) const
{
	switch (side)
	{
		case Side::left:
			return Left(doc) ? Left(doc)->sector : -1;

		case Side::right:
			return Right(doc) ? Right(doc)->sector : -1;

		default:
			BugError("bad side : %d\n", (int)side);
			return -1;
	}
}


int LineDef::WhatSideDef(Side side) const
{
	switch (side)
	{
		case Side::left:
			return left;
		case Side::right:
			return right;

		default:
			BugError("bad side : %d\n", (int)side);
			return -1;
	}
}

bool LineDef::IsSelfRef(const Document &doc) const
{
	return (left >= 0) && (right >= 0) &&
		doc.sidedefs[left]->sector == doc.sidedefs[right]->sector;
}

double LineDef::CalcLength(const Document &doc) const
{
	double dx = Start(doc)->x() - End(doc)->x();
	double dy = Start(doc)->y() - End(doc)->y();

	return hypot(dx, dy);
}


//------------------------------------------------------------------------

//------------------------------------------------------------------------
//  BASIS API IMPLEMENTATION
//------------------------------------------------------------------------

//
// Begin a group of operations that will become a single undo/redo
// step.  Any stored _redo_ steps will be forgotten.  The BA_New,
// BA_Delete, BA_Change and BA_Message functions must only be called
// between BA_Begin() and BA_End() pairs.
//
void Basis::begin()
{
	if(mCurrentGroup.isActive())
		BugError("Basis::begin called twice without Basis::end\n");
	while(!mRedoFuture.empty())
		mRedoFuture.pop();
	mCurrentGroup.activate();
	doClearChangeStatus();
}

//
// Set the status
//
void Instance::basisSetStatus(const SString &text)
{
	Status_Set("%s", text.c_str());
}

//
// finish a group of operations.
//
void Basis::end()
{
	if(!mCurrentGroup.isActive())
		BugError("Basis::end called without a previous Basis::begin\n");
	mCurrentGroup.end();

	if(mCurrentGroup.isEmpty())
		mCurrentGroup.reset();
	else
	{
		SString message = mCurrentGroup.getMessage();
		mUndoHistory.push(std::move(mCurrentGroup));
		listener.basisSetStatus(message);
	}
	doProcessChangeStatus();
}

//
// abort the group of operations -- the undo/redo history is not
// modified and any changes since BA_Begin() are undone except
// when 'keep_changes' is true.
//
void Basis::abort(bool keepChanges)
{
	if(!mCurrentGroup.isActive())
		BugError("Basis::abort called without a previous Basis::begin\n");

	mCurrentGroup.end();

	if(!keepChanges && !mCurrentGroup.isEmpty())
		mCurrentGroup.reapply(*this);

	mCurrentGroup.reset();
	mDidMakeChanges = false;
	doProcessChangeStatus();
}

//
// assign a message to the current operation.
// this can be called multiple times.
//
void Basis::setMessage(EUR_FORMAT_STRING(const char *format), ...)
{
	SYS_ASSERT(format);
	SYS_ASSERT(mCurrentGroup.isActive());

	va_list arg_ptr;
	va_start(arg_ptr, format);
	mCurrentGroup.setMessage(SString::vprintf(format, arg_ptr));
	va_end(arg_ptr);
}

//
// Set a message for the selection
//
void Basis::setMessageForSelection(const char *verb, const selection_c &list, const char *suffix)
{
	// utility for creating messages like "moved 3 things"

	int total = list.count_obj();

	if(total < 1)  // oops
		return;

	if(total == 1)
	{
		setMessage("%s %s #%d%s", verb, NameForObjectType(list.what_type()), list.find_first(), suffix);
	}
	else
	{
		setMessage("%s %d %s%s", verb, total, NameForObjectType(list.what_type(), true /* plural */), suffix);
	}
}

//
// create a new object, returning its objnum.  It is safe to
// directly set the new object's fields after calling BA_New().
//
int Basis::addNew(ObjType type)
{
	SYS_ASSERT(mCurrentGroup.isActive());

	EditOperation op;

	op.action = EditType::insert;
	op.objtype = type;

	switch(type)
	{
	case ObjType::things:
		op.objnum = doc.numThings();
		op.thing = new Thing;
		break;

	case ObjType::vertices:
		op.objnum = doc.numVertices();
		op.vertex = new Vertex;
		break;

	case ObjType::sidedefs:
		op.objnum = doc.numSidedefs();
		op.sidedef = new SideDef;
		break;

	case ObjType::linedefs:
		op.objnum = doc.numLinedefs();
		op.linedef = new LineDef;
		break;

	case ObjType::sectors:
		op.objnum = doc.numSectors();
		op.sector = new Sector;
		break;

	default:
		BugError("Basis::addNew: unknown type\n");
	}

	mCurrentGroup.addApply(op, *this);

	return op.objnum;
}

//
// deletes the given object, and in certain cases other types of
// objects bound to it (e.g. deleting a vertex will cause all
// bound linedefs to also be deleted).
//
void Basis::del(ObjType type, int objnum)
{
	SYS_ASSERT(mCurrentGroup.isActive());

	EditOperation op;

	op.action = EditType::del;
	op.objtype = type;
	op.objnum = objnum;

	// this must happen _before_ doing the deletion (otherwise
	// when we undo, the insertion will mess up the references).
	if(type == ObjType::sidedefs)
	{
		// unbind sidedef from any linedefs using it
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->right == objnum)
				changeLinedef(n, &LineDef::right, -1);

			if(L->left == objnum)
				changeLinedef(n, &LineDef::left, -1);
		}
	}
	else if(type == ObjType::vertices)
	{
		// delete any linedefs bound to this vertex
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->start == objnum || L->end == objnum)
				del(ObjType::linedefs, n);
		}
	}
	else if(type == ObjType::sectors)
	{
		// delete the sidedefs bound to this sector
		for(int n = doc.numSidedefs() - 1; n >= 0; n--)
			if(doc.sidedefs[n]->sector == objnum)
				del(ObjType::sidedefs, n);
	}

	SYS_ASSERT(mCurrentGroup.isActive());

	mCurrentGroup.addApply(op, *this);
}

//
// change a field of an existing object.  If the value was the
// same as before, nothing happens and false is returned.
// Otherwise returns true.
//
void Basis::change(ObjType type, int objnum, ItemField field, int value)
{
	// TODO: optimise, check whether value actually changes

	EditOperation op;

	op.action = EditType::change;
	op.objtype = type;
	op.field = field;
	op.objnum = objnum;
	op.value = value;

	SYS_ASSERT(mCurrentGroup.isActive());

	mCurrentGroup.addApply(op, *this);
}

//
// Called when change is called
//
void Instance::basisOnChangeItem(ObjType type, ItemField field, int value)
{
	switch(type)
	{
		case ObjType::things:
			if(field.thing == &Thing::type)
				recent_things.insert_number(value);
			break;
		case ObjType::sectors:
			if(field.sector == &Sector::floor_tex ||
               field.sector == &Sector::ceil_tex)
            {
				recent_flats.insert(BA_GetString(value));
            }
			break;
		case ObjType::sidedefs:
			if(field.side == &SideDef::lower_tex ||
               field.side == &SideDef::upper_tex ||
               field.side == &SideDef::mid_tex)
			{
				recent_textures.insert(BA_GetString(value));
			}
			break;
		default:
			break;
	}
}

//
// Change thing
//
void Basis::changeThing(int thing, int Thing::*field, int value)
{
	SYS_ASSERT(thing >= 0 && thing < doc.numThings());

    ItemField ife;
    ife.thing = field;
	listener.basisOnChangeItem(ObjType::things, ife, value);

	change(ObjType::things, thing, ife, value);
}

//
// Change vertex
//
void Basis::changeVertex(int vert, int Vertex::*field, int value)
{
	SYS_ASSERT(vert >= 0 && vert < doc.numVertices());

    ItemField ife;
    ife.vertex = field;
//	listener.basisOnChangeItem(ObjType::vertices, field, value);

	change(ObjType::vertices, vert, ife, value);
}

//
// Change sector
//
void Basis::changeSector(int sec, int Sector::*field, int value)
{
	SYS_ASSERT(sec >= 0 && sec < doc.numSectors());

    ItemField ife;
    ife.sector = field;
	listener.basisOnChangeItem(ObjType::sectors, ife, value);

	change(ObjType::sectors, sec, ife, value);
}

//
// Change sidedef
//
void Basis::changeSidedef(int side, int SideDef::*field, int value)
{
	SYS_ASSERT(side >= 0 && side < doc.numSidedefs());

    ItemField ife;
    ife.side = field;
	listener.basisOnChangeItem(ObjType::sidedefs, ife, value);

	change(ObjType::sidedefs, side, ife, value);
}

//
// Change linedef
//
void Basis::changeLinedef(int line, int LineDef::*field, int value)
{
	SYS_ASSERT(line >= 0 && line < doc.numLinedefs());

    ItemField ife;
    ife.line = field;
//	listener.basisOnChangeItem(ObjType::linedefs, field, value);

	change(ObjType::linedefs, line, ife, value);
}

//
// attempt to undo the last normal or redo operation.  Returns
// false if the undo history is empty.
//
bool Basis::undo()
{
	if(mUndoHistory.empty())
		return false;

	doClearChangeStatus();

	UndoGroup grp = std::move(mUndoHistory.top());
	mUndoHistory.pop();

	listener.basisSetStatus(SString::printf("UNDO: %s",
											grp.getMessage().c_str()));

	grp.reapply(*this);

	mRedoFuture.push(std::move(grp));

	doProcessChangeStatus();
	return true;
}

//
// attempt to re-do the last undo operation.  Returns false if
// there is no stored redo steps.
//
bool Basis::redo()
{
	if(mRedoFuture.empty())
		return false;

	doClearChangeStatus();

	UndoGroup grp = std::move(mRedoFuture.top());
	mRedoFuture.pop();

	listener.basisSetStatus(SString::printf("Redo: %s",
											grp.getMessage().c_str()));

	grp.reapply(*this);

	mUndoHistory.push(std::move(grp));

	doProcessChangeStatus();
	return true;
}

//
// clear everything (before loading a new level).
//
void Basis::clearAll()
{
	doc.things.clear();
	doc.vertices.clear();
	doc.sectors.clear();
	doc.sidedefs.clear();
	doc.linedefs.clear();

	doc.headerData.clear();
	doc.behaviorData.clear();
	doc.scriptsData.clear();

	while(!mUndoHistory.empty())
		mUndoHistory.pop();
	while(!mRedoFuture.empty())
		mRedoFuture.pop();

	// Note: we don't clear the string table, since there can be
	//       string references in the clipboard.

	// TODO: other modules
	Clipboard_ClearLocals();
}

//
// Execute the operation
//
void Basis::EditOperation::apply(Basis &basis)
{
	switch(action)
	{
	case EditType::change:
		rawChange(basis);
		return;
	case EditType::del:
		ptr = static_cast<int *>(rawDelete(basis));
		action = EditType::insert;	// reverse the operation
		return;
	case EditType::insert:
		rawInsert(basis);
		ptr = nullptr;
		action = EditType::del;	// reverse the operation
		return;
	default:
		BugError("Basis::EditOperation::apply\n");
	}
}

//
// Destroy an inst.inst.edit operation
//
void Basis::EditOperation::destroy()
{
	switch(action)
	{
	case EditType::insert:
		SYS_ASSERT(ptr);
		deleteFinally();
		break;
	case EditType::del:
		SYS_ASSERT(!ptr);
		break;
	default:
		break;
	}
}

//
// Notify change
//
void Instance::basisNotifyChange(ObjType objtype, int objnum, ItemField field)
{
	Clipboard_NotifyChange(objtype, objnum, field);
	Selection_NotifyChange(objtype, objnum, field);
	MapStuff_NotifyChange(objtype, objnum, field);
	Render3D_NotifyChange(objtype, objnum, field);
	ObjectBox_NotifyChange(objtype, objnum, field);
}

//
// Execute the raw change
//
void Basis::EditOperation::rawChange(Basis &basis)
{
	switch(objtype)
	{
	case ObjType::things:
		SYS_ASSERT(0 <= objnum && objnum < basis.doc.numThings());
        std::swap(basis.doc.things[objnum].get()->*field.thing, value);
		break;
	case ObjType::vertices:
		SYS_ASSERT(0 <= objnum && objnum < basis.doc.numVertices());
        std::swap(basis.doc.vertices[objnum].get()->*field.vertex, value);
		break;
	case ObjType::sectors:
		SYS_ASSERT(0 <= objnum && objnum < basis.doc.numSectors());
        std::swap(basis.doc.sectors[objnum].get()->*field.sector, value);
		break;
	case ObjType::sidedefs:
		SYS_ASSERT(0 <= objnum && objnum < basis.doc.numSidedefs());
        std::swap(basis.doc.sidedefs[objnum].get()->*field.side, value);
		break;
	case ObjType::linedefs:
		SYS_ASSERT(0 <= objnum && objnum < basis.doc.numLinedefs());
        std::swap(basis.doc.linedefs[objnum].get()->*field.line, value);
		break;
	default:
		BugError("Basis::EditOperation::rawChange: bad objtype %u\n", (unsigned)objtype);
		return; /* NOT REACHED */
	}
	// TODO: CHANGE THIS TO A SAFER WAY!
	basis.mDidMakeChanges = true;

	// TODO: their modules
	basis.listener.basisNotifyChange(objtype, objnum, field);
}

//
// Notify delete
//
void Instance::basisNotifyDelete(ObjType objtype, int objnum)
{
	Clipboard_NotifyDelete(objtype, objnum);
	Selection_NotifyDelete(objtype, objnum);
	MapStuff_NotifyDelete(objtype, objnum);
	Render3D_NotifyDelete(level, objtype, objnum);
	ObjectBox_NotifyDelete(objtype, objnum);
}

//
// Deletion operation
//
void *Basis::EditOperation::rawDelete(Basis &basis) const
{
	basis.mDidMakeChanges = true;

	// TODO: their own modules
	basis.listener.basisNotifyDelete(objtype, objnum);

	switch(objtype)
	{
	case ObjType::things:
		return rawDeleteThing(basis.doc);

	case ObjType::vertices:
		return rawDeleteVertex(basis.doc);

	case ObjType::sectors:
		return rawDeleteSector(basis.doc);

	case ObjType::sidedefs:
		return rawDeleteSidedef(basis.doc);

	case ObjType::linedefs:
		return rawDeleteLinedef(basis.doc);

	default:
		BugError("Basis::EditOperation::rawDelete: bad objtype %u\n", (unsigned)objtype);
		return NULL; /* NOT REACHED */
	}
}

//
// Thing deletion
//
Thing *Basis::EditOperation::rawDeleteThing(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum < doc.numThings());

	Thing *result = doc.things[objnum].release();
	doc.things.erase(doc.things.begin() + objnum);

	return result;
}

//
// Vertex deletion (and update linedef refs)
//
Vertex *Basis::EditOperation::rawDeleteVertex(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum < doc.numVertices());

	Vertex *result = doc.vertices[objnum].release();
	doc.vertices.erase(doc.vertices.begin() + objnum);

	// fix the linedef references

	if(objnum < doc.numVertices())
	{
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->start > objnum)
				L->start--;

			if(L->end > objnum)
				L->end--;
		}
	}

	return result;
}

//
// Raw delete sector (and update sidedef refs)
//
Sector *Basis::EditOperation::rawDeleteSector(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum < doc.numSectors());

	Sector *result = doc.sectors[objnum].release();
	doc.sectors.erase(doc.sectors.begin() + objnum);

	// fix sidedef references

	if(objnum < doc.numSectors())
	{
		for(int n = doc.numSidedefs() - 1; n >= 0; n--)
		{
			SideDef *S = doc.sidedefs[n].get();

			if(S->sector > objnum)
				S->sector--;
		}
	}

	return result;
}

//
// Delete sidedef (and update linedef references)
//
SideDef *Basis::EditOperation::rawDeleteSidedef(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum < doc.numSidedefs());

	SideDef *result = doc.sidedefs[objnum].release();
	doc.sidedefs.erase(doc.sidedefs.begin() + objnum);

	// fix the linedefs references

	if(objnum < doc.numSidedefs())
	{
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->right > objnum)
				L->right--;

			if(L->left > objnum)
				L->left--;
		}
	}

	return result;
}

//
// Raw delete linedef
//
LineDef *Basis::EditOperation::rawDeleteLinedef(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum < doc.numLinedefs());

	LineDef *result = doc.linedefs[objnum].release();
	doc.linedefs.erase(doc.linedefs.begin() + objnum);

	return result;
}

void Instance::basisNotifyInsert(ObjType objtype, int objnum)
{
	Clipboard_NotifyInsert(level, objtype, objnum);
	Selection_NotifyInsert(objtype, objnum);
	MapStuff_NotifyInsert(objtype, objnum);
	Render3D_NotifyInsert(objtype, objnum);
	ObjectBox_NotifyInsert(objtype, objnum);
}

//
// Insert operation
//
void Basis::EditOperation::rawInsert(Basis &basis) const
{
	basis.mDidMakeChanges = true;

	// TODO: their module
	basis.listener.basisNotifyInsert(objtype, objnum);

	switch(objtype)
	{
	case ObjType::things:
		rawInsertThing(basis.doc);
		break;

	case ObjType::vertices:
		rawInsertVertex(basis.doc);
		break;

	case ObjType::sidedefs:
		rawInsertSidedef(basis.doc);
		break;

	case ObjType::sectors:
		rawInsertSector(basis.doc);
		break;

	case ObjType::linedefs:
		rawInsertLinedef(basis.doc);
		break;

	default:
		BugError("Basis::EditOperation::rawInsert: bad objtype %u\n", (unsigned)objtype);
	}
}

//
// Thing insertion
//
void Basis::EditOperation::rawInsertThing(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum <= doc.numThings());
	doc.things.insert(doc.things.begin() + objnum,
					  std::unique_ptr<Thing>(thing));
}

//
// Vertex insertion
//
void Basis::EditOperation::rawInsertVertex(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum <= doc.numVertices());
	doc.vertices.insert(doc.vertices.begin() + objnum,
						std::unique_ptr<Vertex>(vertex));

	// fix references in linedefs

	if(objnum + 1 < doc.numVertices())
	{
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->start >= objnum)
				L->start++;

			if(L->end >= objnum)
				L->end++;
		}
	}
}

//
// Sector insertion
//
void Basis::EditOperation::rawInsertSector(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum <= doc.numSectors());
	doc.sectors.insert(doc.sectors.begin() + objnum,
					   std::unique_ptr<Sector>(sector));

	// fix all sidedef references

	if(objnum + 1 < doc.numSectors())
	{
		for(int n = doc.numSidedefs() - 1; n >= 0; n--)
		{
			SideDef *S = doc.sidedefs[n].get();

			if(S->sector >= objnum)
				S->sector++;
		}
	}
}

//
// Sidedef insertion
//
void Basis::EditOperation::rawInsertSidedef(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum <= doc.numSidedefs());
	doc.sidedefs.insert(doc.sidedefs.begin() + objnum,
						std::unique_ptr<SideDef>(sidedef));

	// fix the linedefs references

	if(objnum + 1 < doc.numSidedefs())
	{
		for(int n = doc.numLinedefs() - 1; n >= 0; n--)
		{
			LineDef *L = doc.linedefs[n].get();

			if(L->right >= objnum)
				L->right++;

			if(L->left >= objnum)
				L->left++;
		}
	}
}

//
// Linedef insertion
//
void Basis::EditOperation::rawInsertLinedef(Document &doc) const
{
	SYS_ASSERT(0 <= objnum && objnum <= doc.numLinedefs());
	doc.linedefs.insert(doc.linedefs.begin() + objnum,
						std::unique_ptr<LineDef>(linedef));
}

//
// Action to do on destruction of insert operation
//
void Basis::EditOperation::deleteFinally()
{
	switch(objtype)
	{
	case ObjType::things:   delete thing; break;
	case ObjType::vertices: delete vertex; break;
	case ObjType::sectors:  delete sector; break;
	case ObjType::sidedefs: delete sidedef; break;
	case ObjType::linedefs: delete linedef; break;

	default:
		BugError("DeleteFinally: bad objtype %d\n", (int)objtype);
	}
}

//
// Move operator
//
Basis::UndoGroup &Basis::UndoGroup::operator = (UndoGroup &&other) noexcept
{
	mOps = std::move(other.mOps);
	mDir = other.mDir;
	mMessage = std::move(other.mMessage);

	other.reset();	// ensure the other goes into the default state
	return *this;
}

//
// Reset to initial, inactive state
//
void Basis::UndoGroup::reset()
{
	mOps.clear();
	mDir = 0;
	mMessage = DEFAULT_UNDO_GROUP_MESSAGE;
}

//
// Add and apply
//
void Basis::UndoGroup::addApply(const EditOperation &op, Basis &basis)
{
	mOps.push_back(op);
	mOps.back().apply(basis);
}

//
// Reapply
//
void Basis::UndoGroup::reapply(Basis &basis)
{
	if(mDir > 0)
		for(auto it = mOps.begin(); it != mOps.end(); ++it)
			it->apply(basis);
	else if(mDir < 0)
		for(auto it = mOps.rbegin(); it != mOps.rend(); ++it)
			it->apply(basis);

	// reverse the order for next time
	mDir = -mDir;
}

//
// Start of operation
//
void Instance::basisNotifyBegin()
{
	Clipboard_NotifyBegin();
	Selection_NotifyBegin();
	MapStuff_NotifyBegin();
	Render3D_NotifyBegin();
	ObjectBox_NotifyBegin();
}

//
// Clear change status
//
void Basis::doClearChangeStatus()
{
	mDidMakeChanges = false;

	listener.basisNotifyBegin();
}

//
// End of operation
//
void Instance::basisNotifyEnd()
{
	Clipboard_NotifyEnd();
	Selection_NotifyEnd();
	MapStuff_NotifyEnd();
	Render3D_NotifyEnd(*this);
	ObjectBox_NotifyEnd();
}

//
// Mark changes
//
void Instance::basisMadeChanges()
{
	MadeChanges = true;
	RedrawMap();
}

//
// If we made changes, notify the others
//
void Basis::doProcessChangeStatus() const
{
	if(mDidMakeChanges)
		listener.basisMadeChanges();

	listener.basisNotifyEnd();
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
