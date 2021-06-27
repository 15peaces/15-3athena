// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
#include "../common/cbasetypes.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "battle.h"
#include "episode.h"

#include <stdio.h>

/// Check if episode specific db-file exists [15peaces]
bool EpisodeDBExists(const char* dbpath, const char* filename) {
	FILE* tfp;
	char tpath[256];

	sprintf(tpath, "%s/episode/%s", dbpath, filename);
	tfp = fopen(tpath, "r");

	if (tfp == NULL) {
		ShowWarning("EpisodeDBExists: File not found \"%s\", skipping.\n", tpath);
		ShowWarning("EpisodeDBExists: Please create episode specific file or disable episode.readdb. Loading default database...\n");
		return false;
	}
	return true;
}

/// this function will change the needed variables for episode databases and pass them to sv_readdb function.
bool episode_sv_readdb(char* directory, char* filename, char delim, int mincols, int maxcols, int maxrows, bool(*parseproc)(char* fields[], int columns, int current)) {
	char epfile[256], def_file[256];
	
	sprintf(def_file, "%s.txt", filename);
	
	if (battle_config.episode_readdb) {
		sprintf(epfile, "%s_ep%i.txt", filename, battle_config.feature_episode);

		// Testing episode database and manipulate variables [15peaces]
		if (EpisodeDBExists(directory, epfile))
			sprintf(filename, "episode/%s", epfile);
		else
			filename = def_file;
	} else
		filename = def_file;

	return sv_readdb(directory, filename, delim, mincols, maxcols, maxrows, parseproc);
}
