// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _EPISODE_H_
#define _EPISODE_H_

bool EpisodeDBExists(const char* dbpath, const char* filename);
bool episode_sv_readdb(char* directory, char* filename, char delim, int mincols, int maxcols, int maxrows, bool(*parseproc)(char* fields[], int columns, int current));

#endif /* _EPISODE_H_ */
