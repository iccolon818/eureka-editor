//------------------------------------------------------------------------
//  LEVEL LOAD / SAVE / NEW
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2015 Andrew Apted
//  Copyright (C)      2015 Ioan Chera
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

#include "e_basis.h"
#include "e_loadsave.h"
#include "e_checks.h"
#include "levels.h"  // CalculateLevelBounds()
#include "lib_adler.h"
#include "m_config.h"
#include "m_files.h"
#include "w_rawdef.h"
#include "w_wad.h"

#include "ui_window.h"
#include "ui_file.h"


int last_given_file;


static void SaveLevel(Wad_file *wad, const char *level);

static const char * overwrite_message =
	"The %s PWAD already contains this map.  "
	"This operation will destroy that map (overwrite it)."
	"\n\n"
	"Are you sure you want to continue?";


static void FreshLevel()
{
	BA_ClearAll();

	Sector *sec = new Sector;
	Sectors.push_back(sec);

	sec->SetDefaults();

	Thing *th = new Thing;
	Things.push_back(th);

	th->x = 512;
	th->y = 512;
	th->type = 1;

	for (int i = 0 ; i < 4 ; i++)
	{
		Vertex *v = new Vertex;
		Vertices.push_back(v);

		v->x = (i >= 2) ? 768 : 256;
		v->y = (i==1 || i==2) ? 768 : 256;

		SideDef *sd = new SideDef;
		SideDefs.push_back(sd);

		sd->SetDefaults(false);

		LineDef *ld = new LineDef;
		LineDefs.push_back(ld);

		ld->start = i;
		ld->end   = (i+1) % 4;
		ld->flags = MLF_Blocking;
		ld->right = i;
	}

	CalculateLevelBounds();
}


extern void CMD_ZoomWholeMap();

void RemoveEditWad();


static bool Project_New()
{
	SYS_ASSERT(! edit_wad);

	// determine map name (same as first level in the IWAD)
	const char *map_name = "MAP01";

	short idx = game_wad->FindFirstLevel();

	if (idx >= 0)
	{
		Lump_c * lump  = game_wad->GetLump(idx);
		map_name = lump->Name();
	}


	Fl_Native_File_Chooser chooser;

	chooser.title("Pick file to create");
	chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
	chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
	chooser.filter("Wads\t*.wad");

	//??  chooser.directory("xxx");

	// Show native chooser
	switch (chooser.show())
	{
		case -1:
			LogPrintf("New Project: error choosing file:\n");
			LogPrintf("   %s\n", chooser.errmsg());

			DLG_Notify("Unable to create a new project:\n\n%s", chooser.errmsg());
			return false;

		case 1:
			LogPrintf("New Project: cancelled by user\n");
			return false;

		default:
			break;  // OK
	}

	// if extension is missing then add ".wad"
	char filename[FL_PATH_MAX];

	strcpy(filename, chooser.filename());

	char *pos = (char *)fl_filename_ext(filename);
	if (! *pos)
		strcat(filename, ".wad");


	// delete the file if it already exists
	// (the file chooser should have asked the user for confirmation)

	if (FileExists(filename))
	{
		// TODO M_BackupWad(wad);

		if (! FileDelete(filename))
		{
			DLG_Notify("Unable to delete the existing file.");
			return false;
		}
	}


	LogPrintf("Creating New File : %s of %s\n", map_name, filename);

	Wad_file * wad = Wad_file::Open(filename, 'w');

	if (! wad)
	{
		DLG_Notify("Unable to create the new WAD file.");
		return false;
	}

	edit_wad = wad;
	Pwad_name = edit_wad->PathName();

	MasterDir_Add(edit_wad);


	FreshLevel();

	CMD_ZoomWholeMap();

	SaveLevel(edit_wad, map_name);

	M_AddRecent(edit_wad->PathName(), Level_name);

	MadeChanges = 0;

	return true;
}


bool ProjectSetup(bool new_project, bool is_startup)
{
	if (new_project)
	{
		if (! Main_ConfirmQuit("create a new project"))
			return false;
	}


	UI_ProjectSetup * dialog = new UI_ProjectSetup(new_project, is_startup);

	bool ok = dialog->Run();

	if (ok)
	{
		// grab new information

		Iwad_name = StringDup(dialog->iwad);
		Port_name = StringDup(dialog->port);

		Resource_list.clear();

		for (int i = 0 ; i < UI_ProjectSetup::RES_NUM ; i++)
		{
			if (dialog->res[i])
				Resource_list.push_back(StringDup(dialog->res[i]));
		}
	}

	delete dialog;

	Fl::wait(0.1);
	Fl::wait(0.1);

	if (! ok)
		return false;

	if (is_startup)
		return true;
	
	if (! new_project)
	{
		Main_LoadResources();
		return true;
	}


	RemoveEditWad();

	Main_LoadResources();

	return Project_New();
}


void CMD_NewMap()
{
	if (! Main_ConfirmQuit("create a new map"))
		return;

	if (! edit_wad || edit_wad->IsReadOnly())
	{
		ProjectSetup(true /* new_project */);
		return;
	}


	UI_ChooseMap * dialog = new UI_ChooseMap(Level_name);

	dialog->PopulateButtons(toupper(Level_name[0]), edit_wad);

	const char *map_name = dialog->Run();

	delete dialog;

	// cancelled?
	if (! map_name)
		return;

	// would this replace an existing map?
	if (edit_wad->FindLevel(map_name) >= 0)
	{
		if (DLG_Confirm("Cancel|&Overwrite",
		                overwrite_message, "current") <= 0)
		{
			return;
		}
	}


	M_BackupWad(edit_wad);

	LogPrintf("Created NEW map : %s\n", map_name);

	FreshLevel();

	CMD_ZoomWholeMap();

	// save it now : sets Level_name and window title
	SaveLevel(edit_wad, map_name);

	M_AddRecent(edit_wad->PathName(), Level_name);

	MadeChanges = 0;
}


//------------------------------------------------------------------------
//  LOADING CODE
//------------------------------------------------------------------------

static Wad_file * load_wad;

static short loading_level;

static int bad_linedef_count;

static int bad_sector_refs;
static int bad_sidedef_refs;



static void UpperCaseShortStr(char *buf, int max_len)
{
	for (int i = 0 ; (i < max_len) && buf[i] ; i++)
	{
		buf[i] = toupper(buf[i]);
	}
}


static void LoadVertices()
{
	Lump_c *lump = load_wad->FindLumpInLevel("VERTEXES", loading_level);
	if (! lump)
		FatalError("No vertex lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to vertex lump!\n");

	int count = lump->Length() / sizeof(raw_vertex_t);

# if DEBUG_LOAD
	PrintDebug("GetVertices: num = %d\n", count);
# endif

	Vertices.reserve(count);

	for (int i = 0 ; i < count ; i++)
	{
		raw_vertex_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading vertices.\n");

		Vertex *vert = new Vertex;

		vert->x = LE_S16(raw.x);
		vert->y = LE_S16(raw.y);

		Vertices.push_back(vert);
	}
}


static void LoadSectors()
{
	Lump_c *lump = load_wad->FindLumpInLevel("SECTORS", loading_level);
	if (! lump)
		FatalError("No sector lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to sector lump!\n");

	int count = lump->Length() / sizeof(raw_sector_t);

# if DEBUG_LOAD
	PrintDebug("GetSectors: num = %d\n", count);
# endif

	Sectors.reserve(count);

	for (int i = 0 ; i < count ; i++)
	{
		raw_sector_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading sectors.\n");

		Sector *sec = new Sector;

		sec->floorh = LE_S16(raw.floorh);
		sec->ceilh  = LE_S16(raw.ceilh);

		UpperCaseShortStr(raw.floor_tex, 8);
		UpperCaseShortStr(raw. ceil_tex, 8);

		sec->floor_tex = BA_InternaliseShortStr(raw.floor_tex, 8);
		sec->ceil_tex  = BA_InternaliseShortStr(raw.ceil_tex,  8);

		sec->light = LE_U16(raw.light);
		sec->type  = LE_U16(raw.type);
		sec->tag   = LE_S16(raw.tag);

		Sectors.push_back(sec);
	}
}


static void CreateFallbackSector()
{
	LogPrintf("Creating a fallback sector.\n");

	Sector *sec = new Sector;

	sec->SetDefaults();

	Sectors.push_back(sec);
}


static void LoadHeader()
{
	Lump_c *lump = load_wad->GetLump(loading_level);

	int length = lump->Length();

	if (length == 0)
		return;

	HeaderData.resize(length);

	if (! lump->Seek())
		FatalError("Error seeking to header lump!\n");

	if (! lump->Read(& HeaderData[0], length))
		FatalError("Error reading header lump.\n");
}


static void LoadBehavior()
{
	// IOANCH 9/2015: support Hexen maps
	Lump_c *lump = load_wad->FindLumpInLevel("BEHAVIOR", loading_level);
	if (! lump)
		FatalError("No BEHAVIOR lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to BEHAVIOR lump!\n");

	int length = lump->Length();

	BehaviorData.resize(length);

	if (length == 0)
		return;

	if (! lump->Read(& BehaviorData[0], length))
		FatalError("Error reading BEHAVIOR.\n");
}


static void LoadThings()
{
	Lump_c *lump = load_wad->FindLumpInLevel("THINGS", loading_level);
	if (! lump)
		FatalError("No things lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to things lump!\n");

	int count = lump->Length() / sizeof(raw_thing_t);

# if DEBUG_LOAD
	PrintDebug("GetThings: num = %d\n", count);
# endif

	for (int i = 0 ; i < count ; i++)
	{
		raw_thing_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading things.\n");

		Thing *th = new Thing;

		th->x = LE_S16(raw.x);
		th->y = LE_S16(raw.y);

		th->angle   = LE_U16(raw.angle);
		th->type    = LE_U16(raw.type);
		th->options = LE_U16(raw.options);

		Things.push_back(th);
	}
}


// IOANCH 9/2015
static void LoadThings_Hexen()
{
	Lump_c *lump = load_wad->FindLumpInLevel("THINGS", loading_level);
	if (! lump)
		FatalError("No things lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to things lump!\n");

	int count = lump->Length() / sizeof(raw_hexen_thing_t);

# if DEBUG_LOAD
	PrintDebug("GetThings: num = %d\n", count);
# endif

	for (int i = 0; i < count; ++i)
	{
		raw_hexen_thing_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading things.\n");

		Thing *th = new Thing;

		th->tid = LE_S16(raw.tid);
		th->x = LE_S16(raw.x);
		th->y = LE_S16(raw.y);
		th->z = LE_S16(raw.height);

		th->angle = LE_U16(raw.angle);
		th->type = LE_U16(raw.type);
		th->options = LE_U16(raw.options);

		th->special = raw.special;
		th->arg1 = raw.args[0];
		th->arg2 = raw.args[1];
		th->arg3 = raw.args[2];
		th->arg4 = raw.args[3];
		th->arg5 = raw.args[4];

		Things.push_back(th);
	}
}


static void LoadSideDefs()
{
	Lump_c *lump = load_wad->FindLumpInLevel("SIDEDEFS", loading_level);
	if (! lump)
		FatalError("No sidedefs lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to sidedefs lump!\n");

	int count = lump->Length() / sizeof(raw_sidedef_t);

# if DEBUG_LOAD
	PrintDebug("GetSidedefs: num = %d\n", count);
# endif

	for (int i = 0 ; i < count ; i++)
	{
		raw_sidedef_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading sidedefs.\n");

		SideDef *sd = new SideDef;

		sd->x_offset = LE_S16(raw.x_offset);
		sd->y_offset = LE_S16(raw.y_offset);

		UpperCaseShortStr(raw.upper_tex, 8);
		UpperCaseShortStr(raw.lower_tex, 8);
		UpperCaseShortStr(raw.  mid_tex, 8);

		sd->upper_tex = BA_InternaliseShortStr(raw.upper_tex, 8);
		sd->lower_tex = BA_InternaliseShortStr(raw.lower_tex, 8);
		sd->  mid_tex = BA_InternaliseShortStr(raw.  mid_tex, 8);

		sd->sector = LE_U16(raw.sector);

		if (sd->sector >= NumSectors)
		{
			LogPrintf("WARNING: sidedef #%d has bad sector ref (%d)\n",
			          i, sd->sector);

			bad_sector_refs++;

			// ensure we have a valid sector
			if (NumSectors == 0)
				CreateFallbackSector();

			sd->sector = 0;
		}

		SideDefs.push_back(sd);
	}
}


static void CreateFallbackSideDef()
{
	LogPrintf("Creating a fallback sidedef.\n");

	SideDef *sd = new SideDef;

	sd->SetDefaults(false);

	SideDefs.push_back(sd);
}


static void ValidateSidedefs(LineDef * ld)
{
	if (ld->right == 0xFFFF) ld->right = -1;
	if (ld-> left == 0xFFFF) ld-> left = -1;

	// validate sidedefs
	if (ld->right >= NumSideDefs || ld->left >= NumSideDefs)
	{
		LogPrintf("WARNING: linedef #%d has bad sidedef ref (%d, %d)\n",
				  ld->right, ld->left);

		bad_sidedef_refs++;

		if (ld->right >= NumSideDefs)
			ld->right = (ld->left == 0) ? 1 : 0;

		if (ld->left >= NumSideDefs)
			ld->left = (ld->right == 1) ? 0 : 1;
	}
}


static void LoadLineDefs()
{
	Lump_c *lump = load_wad->FindLumpInLevel("LINEDEFS", loading_level);
	if (! lump)
		FatalError("No linedefs lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to linedefs lump!\n");

	int count = lump->Length() / sizeof(raw_linedef_t);

# if DEBUG_LOAD
	PrintDebug("GetLinedefs: num = %d\n", count);
# endif

	if (count == 0)
		return;

	if (NumSideDefs < 2) CreateFallbackSideDef();
	if (NumSideDefs < 2) CreateFallbackSideDef();

	for (int i = 0 ; i < count ; i++)
	{
		raw_linedef_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading linedefs.\n");

		LineDef *ld = new LineDef;

		ld->start = LE_U16(raw.start);
		ld->end   = LE_U16(raw.end);

		// validate vertices
		if (ld->start >= NumVertices || ld->end >= NumVertices ||
		    ld->start == ld->end)
		{
			LogPrintf("WARNING: linedef #%d has bad vertex ref (%d, %d)\n",
			          ld->start, ld->end);

			bad_linedef_count++;

			// forget it
			delete ld;

			continue;
		}

		ld->flags = LE_U16(raw.flags);
		ld->type  = LE_U16(raw.type);
		ld->tag   = LE_S16(raw.tag);

		ld->right = LE_U16(raw.right);
		ld->left  = LE_U16(raw.left);

		ValidateSidedefs(ld);

		LineDefs.push_back(ld);
	}
}


// IOANCH 9/2015
static void LoadLineDefs_Hexen()
{
	Lump_c *lump = load_wad->FindLumpInLevel("LINEDEFS", loading_level);
	if (! lump)
		FatalError("No linedefs lump!\n");

	if (! lump->Seek())
		FatalError("Error seeking to linedefs lump!\n");

	int count = lump->Length() / sizeof(raw_hexen_linedef_t);

# if DEBUG_LOAD
	PrintDebug("GetLinedefs: num = %d\n", count);
# endif

	if (count == 0)
		return;

	if (NumSideDefs < 2) CreateFallbackSideDef();
	if (NumSideDefs < 2) CreateFallbackSideDef();

	for (int i = 0 ; i < count ; i++)
	{
		raw_hexen_linedef_t raw;

		if (! lump->Read(&raw, sizeof(raw)))
			FatalError("Error reading linedefs.\n");

		LineDef *ld = new LineDef;

		ld->start = LE_U16(raw.start);
		ld->end   = LE_U16(raw.end);

		// validate vertices
		if (ld->start >= NumVertices || ld->end >= NumVertices ||
			ld->start == ld->end)
		{
			LogPrintf("WARNING: linedef #%d has bad vertex ref (%d, %d)\n",
					  ld->start, ld->end);

			bad_linedef_count++;

			// forget it
			delete ld;

			continue;
		}

		ld->flags = LE_U16(raw.flags);
		ld->type = raw.type;
		ld->tag  = raw.args[0];
		ld->arg2 = raw.args[1];
		ld->arg3 = raw.args[2];
		ld->arg4 = raw.args[3];
		ld->arg5 = raw.args[4];

		ld->right = LE_U16(raw.right);
		ld->left  = LE_U16(raw.left);

		ValidateSidedefs(ld);

		LineDefs.push_back(ld);
	}
}


static void RemoveUnusedVerticesAtEnd()
{
	if (NumVertices == 0)
		return;

	bitvec_c used_verts(NumVertices);

	for (int i = 0 ; i < NumLineDefs ; i++)
	{
		used_verts.set(LineDefs[i]->start);
		used_verts.set(LineDefs[i]->end);
	}

	int new_count = NumVertices;

	while (new_count > 2 && !used_verts.get(new_count-1))
		new_count--;

	// we directly modify the vertex array here (which is not
	// normally kosher, but level loading is a special case).
	if (new_count < NumVertices)
	{
		LogPrintf("Removing %d unused vertices at end\n", new_count);

		for (int i = new_count ; i < NumVertices ; i++)
			delete Vertices[i];

		Vertices.resize(new_count);
	}
}


static void ShowLoadProblem()
{
	LogPrintf("Map load problems:\n");
	LogPrintf("   %d linedefs with bad vertex refs (removed)\n", bad_linedef_count);
	LogPrintf("   %d linedefs with bad sidedef refs\n", bad_sidedef_refs);
	LogPrintf("   %d sidedefs with bad sector refs\n", bad_sector_refs);

	static char message[MSG_BUF_LEN];

	if (bad_linedef_count > 0)
	{
		sprintf(message, "Found %d linedefs with bad vertex references.\n"
		                 "These linedefs have been removed.",
		        bad_linedef_count);
	}
	else
	{
		sprintf(message, "Found %d bad sector refs, %d bad sidedef refs.\n"
		                 "These references have been replaced.",
		        bad_sector_refs, bad_sidedef_refs);
	}

	DLG_Notify("Map validation report:\n\n%s", message);
}


/*
   read in the level data
*/

void LoadLevel(Wad_file *wad, const char *level)
{
	load_wad = wad;

	loading_level = load_wad->FindLevel(level);
	if (loading_level < 0)
		FatalError("No such map: %s\n", level);

	Level_format = load_wad->LevelFormat(loading_level);

	BA_ClearAll();

	bad_linedef_count = 0;
	bad_sector_refs   = 0;
	bad_sidedef_refs  = 0;

	LoadHeader();

	if (Level_format == MAPF_Hexen)
		LoadThings_Hexen();
	else
		LoadThings();

	LoadVertices();
	LoadSectors();
	LoadSideDefs();

	if (Level_format == MAPF_Hexen)
	{
		LoadLineDefs_Hexen();
		LoadBehavior();
	}
	else
	{
		LoadLineDefs();
	}

	if (bad_linedef_count || bad_sector_refs || bad_sidedef_refs)
	{
		ShowLoadProblem();
	}


	// Node builders create a lot of new vertices for segs.
	// However they just get in the way for editing, so remove them.
	RemoveUnusedVerticesAtEnd();

	SideDefs_Unpack(true);  // TODO: CONFIG ITEM?
	SideDefs_NormalizeMiddles();

	CalculateLevelBounds();


	// reset various editor state

	Editor_ClearAction();

	edit.Selected->clear_all();
	edit.highlight.clear();

	RedrawMap();

	main_win->UpdateTotals();
	main_win->UpdateGameInfo();
	main_win->InvalidatePanelObj();
	main_win->redraw();

	MadeChanges = 0;


	Level_name = StringUpper(level);

	Status_Set("Loaded %s", Level_name);

	if (main_win)
	{
		main_win->SetTitle(wad->PathName(), level, load_wad->IsReadOnly());

		// load the user state associated with this map
		crc32_c adler_crc;

		BA_LevelChecksum(adler_crc);

		if (! M_LoadUserState())
		{
			M_DefaultUserState();
		}
	}
}


void RemoveEditWad()
{
	if (! edit_wad)
		return;

	MasterDir_Remove(edit_wad);
	delete edit_wad;

	edit_wad = NULL;
	Pwad_name = NULL;
}


bool CMD_OpenMap()
{
	if (! Main_ConfirmQuit("open another map"))
		return false;


	Wad_file *wad = NULL;
	const char *map_name = NULL;
	bool is_new_pwad = false;

	UI_OpenMap * dialog = new UI_OpenMap();

	dialog->Run(&wad, &is_new_pwad, &map_name);

	delete dialog;


	// cancelled?
	if (! wad)
		return false;


	// this shouldn't happen -- but just in case...
	if (wad->FindLevel(map_name) < 0)
	{
		DLG_Notify("Hmmmm, cannot find that map !?!");
		return false;
	}


	if (is_new_pwad && wad->FindLump(EUREKA_LUMP))
	{
		if (! M_ParseEurekaLump(wad))
			return false;
	}


	// has this removed or replaced the currently edited wad?

	if (edit_wad && (wad != edit_wad))
	{
		RemoveEditWad();
	}

	if (is_new_pwad)
	{
		edit_wad = wad;
		Pwad_name = edit_wad->PathName();

		MasterDir_Add(edit_wad);

		Main_LoadResources();
	}


	LogPrintf("Loading Map : %s of %s\n", map_name, wad->PathName());

	LoadLevel(wad, map_name);

	return true;
}


void CMD_OpenFileMap(const char *filename, const char *map_name)
{
	if (! Main_ConfirmQuit("open another map"))
		return;


	Wad_file *wad = NULL;

	// make sure the file exists [Open with 'a' would create it]

	// TODO: perhaps check this when building the Pwad_list
	//       [however, 'eureka foo.wad' should fatal error if no foo.wad]

	if (FileExists(filename))
	{
		wad = Wad_file::Open(filename, 'a');
	}

	if (! wad)
	{
		// FIXME: get an error message, add it here

		DLG_Notify("Unable to open that WAD file.");
		return;
	}


	int lev_idx = -1;

	if (map_name)
	{
		lev_idx = wad->FindLevel(map_name);

		if (lev_idx < 0)
		{
			// FIXME: WARN ??  ERROR ??
		}
	}

	if (lev_idx < 0)
	{
		map_name = NULL;

		lev_idx = wad->FindFirstLevel();
	}

	if (lev_idx < 0)
	{
		delete wad;

		DLG_Notify("No levels found in that WAD.");
		return;
	}

	// IOANCH 9/2015: support Hexen, too

	if (wad->FindLump(EUREKA_LUMP))
	{
		if (! M_ParseEurekaLump(wad))
			return;
	}


	// OK, open it


	// this wad replaces the current PWAD

	RemoveEditWad();

	edit_wad = wad;
	Pwad_name = edit_wad->PathName();

	MasterDir_Add(edit_wad);

	Main_LoadResources();


	if (! map_name)
	{
		Lump_c *lump = wad->GetLump(lev_idx);

		map_name = lump->Name();
	}

	LogPrintf("Loading Map : %s of %s\n", map_name, wad->PathName());

	LoadLevel(wad, map_name);
}


void CMD_GivenFile()
{
	const char *mode = EXEC_Param[0];

	int index = last_given_file;

	if (! mode[0] || y_stricmp(mode, "current") == 0)
	{
		// index = index + 0;
	}
	else if (y_stricmp(mode, "next") == 0)
	{
		index = index + 1;
	}
	else if (y_stricmp(mode, "prev") == 0)
	{
		index = index - 1;
	}
	else if (y_stricmp(mode, "first") == 0)
	{
		index = 0;
	}
	else if (y_stricmp(mode, "last") == 0)
	{
		index = (int)Pwad_list.size() - 1;
	}
	else
	{
		Beep("GivenFile: unknown keyword: %s", mode);
		return;
	}

	if (index < 0 || index >= (int)Pwad_list.size())
	{
		Beep("No more files");
		return;
	}

	last_given_file = index;

	// TODO: remember last map visited in this wad

	CMD_OpenFileMap(Pwad_list[index], NULL);
}


void CMD_FlipMap()
{
	const char *mode = EXEC_Param[0];

	if (! mode[0])
	{
		Beep("FlipMap: missing keyword");
		return;
	}


	if (! Main_ConfirmQuit("open another map"))
		return;


	Wad_file *wad = edit_wad ? edit_wad : game_wad;

	// the level might not be found (lev_idx < 0) -- that is OK
	int lev_idx = wad->FindLevel_Raw(Level_name);
	int max_idx = wad->NumLevels() - 1;

	if (max_idx < 0)
	{
		Beep("No maps ?!?");
		return;
	}

	SYS_ASSERT(lev_idx <= max_idx);


	if (y_stricmp(mode, "next") == 0)
	{
		if (lev_idx < 0)
			lev_idx = 0;
		else if (lev_idx < max_idx)
			lev_idx++;
		else
		{
			Beep("No more maps");
			return;
		}
	}
	else if (y_stricmp(mode, "prev") == 0)
	{
		if (lev_idx < 0)
			lev_idx = max_idx;
		else if (lev_idx > 0)
			lev_idx--;
		else
		{
			Beep("No more maps");
			return;
		}
	}
	else if (y_stricmp(mode, "first") == 0)
	{
		lev_idx = 0;
	}
	else if (y_stricmp(mode, "last") == 0)
	{
		lev_idx = max_idx;
	}
	else
	{
		Beep("FlipMap: unknown keyword: %s", mode);
		return;
	}

	SYS_ASSERT(lev_idx >= 0);
	SYS_ASSERT(lev_idx <= max_idx);


	short lump_idx = wad->GetLevel(lev_idx);
	Lump_c * lump  = wad->GetLump(lump_idx);
	const char *map_name = lump->Name();

	LogPrintf("Flipping Map to : %s\n", map_name);

	LoadLevel(wad, map_name);
}


//------------------------------------------------------------------------
//  SAVING CODE
//------------------------------------------------------------------------

static Wad_file *save_wad;


static void SaveHeader(const char *level)
{
	int size = (int)HeaderData.size();

	Lump_c *lump = save_wad->AddLevel(level, size);

	if (size > 0)
	{
		lump->Write(& HeaderData[0], size);
	}

	lump->Finish();
}


static void SaveBehavior()
{
	int size = (int)BehaviorData.size();

	Lump_c *lump = save_wad->AddLump("BEHAVIOR", size);

	if (size > 0)
	{
		lump->Write(& BehaviorData[0], size);
	}

	lump->Finish();
}


static void SaveVertices()
{
	int size = NumVertices * (int)sizeof(raw_vertex_t);

	Lump_c *lump = save_wad->AddLump("VERTEXES", size);

	for (int i = 0 ; i < NumVertices ; i++)
	{
		Vertex *vert = Vertices[i];

		raw_vertex_t raw;

		raw.x = LE_S16(vert->x);
		raw.y = LE_S16(vert->y);

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


static void SaveSectors()
{
	int size = NumSectors * (int)sizeof(raw_sector_t);

	Lump_c *lump = save_wad->AddLump("SECTORS", size);

	for (int i = 0 ; i < NumSectors ; i++)
	{
		Sector *sec = Sectors[i];

		raw_sector_t raw;
		
		raw.floorh = LE_S16(sec->floorh);
		raw.ceilh  = LE_S16(sec->ceilh);

		strncpy(raw.floor_tex, sec->FloorTex(), sizeof(raw.floor_tex));
		strncpy(raw.ceil_tex,  sec->CeilTex(),  sizeof(raw.ceil_tex));

		raw.light = LE_U16(sec->light);
		raw.type  = LE_U16(sec->type);
		raw.tag   = LE_U16(sec->tag);

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


static void SaveThings()
{
	int size = NumThings * (int)sizeof(raw_thing_t);

	Lump_c *lump = save_wad->AddLump("THINGS", size);

	for (int i = 0 ; i < NumThings ; i++)
	{
		Thing *th = Things[i];

		raw_thing_t raw;
		
		raw.x = LE_S16(th->x);
		raw.y = LE_S16(th->y);

		raw.angle   = LE_U16(th->angle);
		raw.type    = LE_U16(th->type);
		raw.options = LE_U16(th->options);

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


// IOANCH 9/2015
static void SaveThings_Hexen()
{
	int size = NumThings * (int)sizeof(raw_hexen_thing_t);

	Lump_c *lump = save_wad->AddLump("THINGS", size);

	for (int i = 0 ; i < NumThings ; i++)
	{
		Thing *th = Things[i];

		raw_hexen_thing_t raw;

		raw.tid = LE_S16(th->tid);

		raw.x = LE_S16(th->x);
		raw.y = LE_S16(th->y);
		raw.height = LE_S16(th->z);

		raw.angle   = LE_U16(th->angle);
		raw.type    = LE_U16(th->type);
		raw.options = LE_U16(th->options);

		raw.special = th->special;
		raw.args[0] = th->arg1;
		raw.args[1] = th->arg2;
		raw.args[2] = th->arg3;
		raw.args[3] = th->arg4;
		raw.args[4] = th->arg5;

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


static void SaveSideDefs()
{
	int size = NumSideDefs * (int)sizeof(raw_sidedef_t);

	Lump_c *lump = save_wad->AddLump("SIDEDEFS", size);

	for (int i = 0 ; i < NumSideDefs ; i++)
	{
		SideDef *side = SideDefs[i];

		raw_sidedef_t raw;
		
		raw.x_offset = LE_S16(side->x_offset);
		raw.y_offset = LE_S16(side->y_offset);

		strncpy(raw.upper_tex, side->UpperTex(), sizeof(raw.upper_tex));
		strncpy(raw.lower_tex, side->LowerTex(), sizeof(raw.lower_tex));
		strncpy(raw.mid_tex,   side->MidTex(),   sizeof(raw.mid_tex));

		raw.sector = LE_U16(side->sector);

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


static void SaveLineDefs()
{
	int size = NumLineDefs * (int)sizeof(raw_linedef_t);

	Lump_c *lump = save_wad->AddLump("LINEDEFS", size);

	for (int i = 0 ; i < NumLineDefs ; i++)
	{
		LineDef *ld = LineDefs[i];

		raw_linedef_t raw;
		
		raw.start = LE_U16(ld->start);
		raw.end   = LE_U16(ld->end);

		raw.flags = LE_U16(ld->flags);
		raw.type  = LE_U16(ld->type);
		raw.tag   = LE_S16(ld->tag);

		raw.right = (ld->right >= 0) ? LE_U16(ld->right) : 0xFFFF;
		raw.left  = (ld->left  >= 0) ? LE_U16(ld->left)  : 0xFFFF;

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


// IOANCH 9/2015
static void SaveLineDefs_Hexen()
{
	int size = NumLineDefs * (int)sizeof(raw_hexen_linedef_t);

	Lump_c *lump = save_wad->AddLump("LINEDEFS", size);

	for (int i = 0 ; i < NumLineDefs ; i++)
	{
		LineDef *ld = LineDefs[i];

		raw_hexen_linedef_t raw;

		raw.start = LE_U16(ld->start);
		raw.end   = LE_U16(ld->end);

		raw.flags = LE_U16(ld->flags);
		raw.type  = ld->type;

		raw.args[0] = ld->tag;
		raw.args[1] = ld->arg2;
		raw.args[2] = ld->arg3;
		raw.args[3] = ld->arg4;
		raw.args[4] = ld->arg5;

		raw.right = (ld->right >= 0) ? LE_U16(ld->right) : 0xFFFF;
		raw.left  = (ld->left  >= 0) ? LE_U16(ld->left)  : 0xFFFF;

		lump->Write(&raw, sizeof(raw));
	}

	lump->Finish();
}


static void EmptyLump(const char *name)
{
	save_wad->AddLump(name)->Finish();
}


static void SaveLevel(Wad_file *wad, const char *level)
{
	save_wad = wad;

	save_wad->BeginWrite();

	// remove previous version of level (if it exists)
	int level_lump = save_wad->FindLevel(level);
	if (level_lump >= 0)
		save_wad->RemoveLevel(level_lump);

	save_wad->InsertPoint(level_lump);

	SaveHeader(level);

	// IOANCH 9/2015: save Hexen format maps
	if (Level_format == MAPF_Hexen)
	{
		SaveThings_Hexen();
		SaveLineDefs_Hexen();
	}
	else
	{
		SaveThings();
		SaveLineDefs();
	}

	SaveSideDefs();
	SaveVertices();

	EmptyLump("SEGS");
	EmptyLump("SSECTORS");
	EmptyLump("NODES");

	SaveSectors();

	EmptyLump("REJECT");
	EmptyLump("BLOCKMAP");

	if (Level_format == MAPF_Hexen)
		SaveBehavior();

	// write out the new directory
	save_wad->EndWrite();

	M_WriteEurekaLump(save_wad);

	Level_name = StringUpper(level);

	Status_Set("Saved %s  --  NO NODES", Level_name);

	if (main_win)
	{
		main_win->SetTitle(wad->PathName(), level, false);

		// save the user state associated with this map
		M_SaveUserState();
	}
}


bool CMD_SaveMap()
{
	// we require a wad file to save into.
	// if there is none, then need to create one via Export function.

	if (! edit_wad)
	{
		return CMD_ExportMap();
	}

	if (edit_wad->IsReadOnly())
	{
		if (DLG_Confirm("Cancel|&Export",
		                "The current pwad is a READ-ONLY file. "
						"Do you want to export this map into a new file?") <= 0)
		{
			return false;
		}
		else
			return CMD_ExportMap();
	}


	M_BackupWad(edit_wad);

	LogPrintf("Saving Map : %s of %s\n", Level_name, edit_wad->PathName());

	SaveLevel(edit_wad, Level_name);

	M_AddRecent(edit_wad->PathName(), Level_name);

	MadeChanges = 0;

	return true;
}


bool CMD_ExportMap()
{
	Fl_Native_File_Chooser chooser;

	chooser.title("Pick file to export to");
	chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
	chooser.filter("Wads\t*.wad");

	//??  chooser.directory("xxx");

	// Show native chooser
	switch (chooser.show())
	{
		case -1:
			LogPrintf("Export Map: error choosing file:\n");
			LogPrintf("   %s\n", chooser.errmsg());

			DLG_Notify("Unable to export the map:\n\n%s", chooser.errmsg());
			return false;

		case 1:
			LogPrintf("Export Map: cancelled by user\n");
			return false;

		default:
			break;  // OK
	}

	// if extension is missing then add ".wad"
	char filename[FL_PATH_MAX];

	strcpy(filename, chooser.filename());

	char *pos = (char *)fl_filename_ext(filename);
	if (! *pos)
		strcat(filename, ".wad");


	// does the file already exist?  if not, create it...
	bool exists = FileExists(filename);

	Wad_file *wad;

	if (exists)
	{
		wad = Wad_file::Open(filename, 'a');

		if (wad && wad->IsReadOnly())
		{
			DLG_Notify("Cannot export the map into a READ-ONLY file.");

			delete wad;
			return false;
		}
	}
	else
	{
		wad = Wad_file::Open(filename, 'w');
	}

	if (! wad)
	{
		DLG_Notify("Unable to export the map:\n\n%s",
		           "Error creating output file");
		return false;
	}


	// ask user for map name

	UI_ChooseMap * dialog = new UI_ChooseMap(Level_name);

	dialog->PopulateButtons(toupper(Level_name[0]), wad);

	const char *map_name = dialog->Run();

	delete dialog;


	// cancelled?
	if (! map_name)
	{
		delete wad;
		return false;
	}


	// we will write into the chosen wad.
	// however if the level already exists, get confirmation first

	if (exists && wad->FindLevel(map_name) >= 0)
	{
		if (DLG_Confirm("Cancel|&Overwrite",
		                overwrite_message, "selected") <= 0)
		{
			delete wad;
			return false;
		}
	}

	// back-up an existing wad
	if (exists)
	{
		M_BackupWad(wad);
	}


	LogPrintf("Exporting Map : %s of %s\n", map_name, wad->PathName());

	SaveLevel(wad, map_name);

	M_AddRecent(wad->PathName(), map_name);


	// the new wad replaces the current PWAD

	if (edit_wad)
	{
		MasterDir_Remove(edit_wad);

		delete edit_wad;
	}

	edit_wad = wad;
	Pwad_name = edit_wad->PathName();

	MasterDir_Add(edit_wad);

	MadeChanges = 0;

	return true;
}


//------------------------------------------------------------------------
//  RENAME and DELETE
//------------------------------------------------------------------------

void CMD_RenameMap()
{
	if (! edit_wad)
	{
		DLG_Notify("Cannot rename a map unless editing a PWAD.");
		return;
	}

	if (edit_wad->IsReadOnly())
	{
		DLG_Notify("Cannot rename map : file is read-only.");
		return;
	}


	// ask user for map name

	UI_ChooseMap * dialog = new UI_ChooseMap(Level_name, edit_wad /* rename_wad */);

	// pick level format from the IWAD
	// [ user may be trying to rename map after changing the IWAD ]
	char format = 'M';
	{
		short idx = game_wad->FindFirstLevel();

		if (idx >= 0)
		{
			Lump_c * lump  = game_wad->GetLump(idx);
			const char *name = lump->Name();
			format = toupper(name[0]);
		}
	}

	dialog->PopulateButtons(format, edit_wad);

	const char *new_name = dialog->Run();

	delete dialog;

	// cancelled?
	if (! new_name)
		return;

	// sanity check that the name is different
	// (should be prevented by the choose-map dialog)
	if (y_stricmp(new_name, Level_name) == 0)
	{
		Beep("Name is same!?!");
		return;
	}


	// perform the rename
	short level_lump = edit_wad->FindLevel(Level_name);

	if (level_lump >= 0)
	{
		edit_wad->BeginWrite();

		edit_wad->RenameLump(level_lump, new_name);

		edit_wad->EndWrite();
	}

	Level_name = StringUpper(new_name);

	main_win->SetTitle(edit_wad->PathName(), Level_name, false);

	Status_Set("Renamed to %s", Level_name);
}


void CMD_DeleteMap()
{
	if (! edit_wad)
	{
		DLG_Notify("Cannot delete a map unless editing a PWAD.");
		return;
	}

	if (edit_wad->IsReadOnly())
	{
		DLG_Notify("Cannot delete map : file is read-only.");
		return;
	}

	if (edit_wad->NumLevels() < 2)
	{
		// perhaps ask either to Rename map, or Delete the file (and Eureka will shut down)

		DLG_Notify("Cannot delete the last map in a PWAD.");
		return;
	}

	if (DLG_Confirm("Cancel|&Delete",
	                "Are you sure you want to delete this map? "
					"It will be permanently removed from the current PWAD.") <= 0)
	{
		return;
	}

	LogPrintf("Deleting Map : %s...\n", Level_name);

	short level_lump = edit_wad->FindLevel(Level_name);
	short level_idx  = edit_wad->FindLevel_Raw(Level_name);

	if (level_lump < 0 || level_idx < 0)
	{
		Beep("No such map ?!?");
		return;
	}


	edit_wad->BeginWrite();

	edit_wad->RemoveLevel(level_lump);

	edit_wad->EndWrite();


	// choose a new level to load
	{
		if (level_idx >= edit_wad->NumLevels())
			level_idx =  edit_wad->NumLevels() - 1;

		short lump_idx = edit_wad->GetLevel(level_idx);
		Lump_c * lump  = edit_wad->GetLump(lump_idx);
		const char *map_name = lump->Name();

		LogPrintf("OK.  Loading : %s....\n", map_name);

		LoadLevel(edit_wad, map_name);
	}
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
