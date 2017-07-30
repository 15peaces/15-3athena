// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _BATTLEGROUND_H_
#define _BATTLEGROUND_H_

#include "map.h" // EVENT_NAME_LENGTH

#define MAX_BG_MEMBERS 30

enum bg_queue_types
{
	BGQT_INVALID    = 0x0,
	BGQT_INDIVIDUAL = 0x1,
	BGQT_PARTY      = 0x2,
	/* yup no 0x3 */
	BGQT_GUILD      = 0x4,
};

enum bg_team_leave_type
{
	BGTL_LEFT = 0x0,
	BGTL_QUIT = 0x1,
	BGTL_AFK  = 0x2,
};

struct battleground_member_data {
	unsigned short x, y;
	struct map_session_data *sd;
	unsigned afk : 1;
	struct point source;/* where did i come from before i join? */ 
};

struct battleground_arena
{
	unsigned int bg_arena_id;
	char name[24];
	char event_[EVENT_NAME_LENGTH];
	unsigned int min_lv, max_lv;
	unsigned int rew_win;
	unsigned int rew_loss;
	unsigned int rew_draw;
	unsigned int min_players, max_players, min_team_players;
	char delay_var[30];
	unsigned int duration;
	int queue_id;
	int begin_timer;
	int fillup_timer;
	unsigned short fillup_duration;
	unsigned short pregame_duration;
	bool ongoing;
	enum bg_queue_types allowed_types;
};

struct battleground_data {
	unsigned int bg_id;
	unsigned char count;
	struct battleground_member_data members[MAX_BG_MEMBERS];
	struct battleground_arena arena;
	// BG Cementery
	unsigned short mapindex, x, y;
	// Logout Event
	char logout_event[EVENT_NAME_LENGTH];
	char die_event[EVENT_NAME_LENGTH];
};


void do_init_battleground(void);
void do_final_battleground(void);

struct battleground_data* bg_team_search(int bg_id);
struct battleground_arena* bg_arena_search(int arena_id);
struct battleground_arena* bg_name2arena(const char *name);
int bg_send_dot_remove(struct map_session_data *sd);
int bg_team_get_id(struct block_list *bl);
struct map_session_data* bg_getavailablesd(struct battleground_data *bg);

int bg_create(unsigned short mapindex, short rx, short ry, const char *ev, const char *dev);
int bg_team_join(int bg_id, struct map_session_data *sd);
int bg_team_delete(int bg_id);
int bg_team_leave(struct map_session_data *sd, enum bg_team_leave_type flag);
int bg_team_warp(int bg_id, unsigned short mapindex, short x, short y);
int bg_member_respawn(struct map_session_data *sd);
bool bg_send_message(struct map_session_data *sd, const char *mes, int len);

// Battleground Queue
void bg_queue_player_cleanup(struct map_session_data *sd);
void bg_queue_ready_ack(struct battleground_arena *arena, struct map_session_data *sd, bool response);
int bg_id2pos(int queue_id, int account_id);
int bg_begin_timer(int tid, int64 tick, int id, intptr_t data);
void bg_begin(struct battleground_arena *arena);
void bg_queue_add(struct map_session_data *sd, struct battleground_arena *arena, enum bg_queue_types type);
enum BATTLEGROUNDS_QUEUE_ACK bg_can_queue(struct map_session_data *sd, struct battleground_arena *arena, enum bg_queue_types type);

#endif /* _BATTLEGROUND_H_ */
