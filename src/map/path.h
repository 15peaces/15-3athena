// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _PATH_H_
#define _PATH_H_

#include "map.h" // enum cell_chk

#define MOVE_COST 10
#define MOVE_DIAGONAL_COST 14

#define MAX_WALKPATH 32

struct walkpath_data {
	unsigned char path_len,path_pos;
	unsigned char path[MAX_WALKPATH];
};

struct shootpath_data {
	int rx,ry,len;
	int x[MAX_WALKPATH];
	int y[MAX_WALKPATH];
};

struct path_interface *path;
#define check_distance_bl(bl1, bl2, distance) check_distance((bl1)->x - (bl2)->x, (bl1)->y - (bl2)->y, distance)
#define check_distance_blxy(bl, x1, y1, distance) check_distance((bl)->x-(x1), (bl)->y-(y1), distance)
#define check_distance_xy(x0, y0, x1, y1, distance) check_distance((x0)-(x1), (y0)-(y1), distance)
#define distance_bl(bl1, bl2) distance((bl1)->x - (bl2)->x, (bl1)->y - (bl2)->y)
#define distance_blxy(bl, x1, y1) distance((bl)->x-(x1), (bl)->y-(y1))
#define distance_xy(x0, y0, x1, y1) distance((x0)-(x1), (y0)-(y1))

// calculates destination cell for knockback
int path_blownpos(int m,int x0,int y0,int dx,int dy,int count);

// tries to find a walkable path
bool path_search(struct walkpath_data *wpd,int m,int x0,int y0,int x1,int y1,int flag,cell_chk cell);

// tries to find a shootable path
bool path_search_long(struct shootpath_data *spd,int m,int x0,int y0,int x1,int y1,cell_chk cell);

bool check_distance_client(int dx, int dy, int distance);
bool distance_client(int dx, int dy);

// distance related functions
int check_distance(int dx, int dy, int distance);
unsigned int distance(int dx, int dy);

#define check_distance_client_bl(bl1, bl2, distance) check_distance_client((bl1)->x - (bl2)->x, (bl1)->y - (bl2)->y, distance)
#define check_distance_client_blxy(bl, x1, y1, distance) check_distance_client((bl)->x-(x1), (bl)->y-(y1), distance)
#define check_distance_client_xy(x0, y0, x1, y1, distance) check_distance_client((x0)-(x1), (y0)-(y1), distance)

#define distance_client_bl(bl1, bl2) distance_client((bl1)->x - (bl2)->x, (bl1)->y - (bl2)->y)
#define distance_client_blxy(bl, x1, y1) distance_client((bl)->x-(x1), (bl)->y-(y1))
#define distance_client_xy(x0, y0, x1, y1) distance_client((x0)-(x1), (y0)-(y1))

#endif /* _PATH_H_ */
