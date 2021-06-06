// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _QUEST_H_
#define _QUEST_H_

struct quest_dropitem {
	uint16 nameid;
	uint16 count;
	uint16 rate;
	uint16 mob_id;
	uint8 bound;
	bool isAnnounced;
};

struct quest_objective {
	uint16 mob;
	uint16 count;
	uint16 min_level, max_level;
	int8 race;
	int8 size;
	int8 element;
};

struct s_quest_db {
	uint16 id;
	unsigned int time;
	uint8 objectives_count;
	struct quest_objective objectives[MAX_QUEST_OBJECTIVES];
	uint8 dropitem_count;
	struct quest_dropitem dropitem[MAX_QUEST_DROPS];
	StringBuf name;
};
extern struct s_quest_db quest_db[MAX_QUEST_DB];

typedef enum quest_check_type { HAVEQUEST, PLAYTIME, HUNTING } quest_check_type;

int quest_pc_login(TBL_PC * sd);

int quest_add(TBL_PC * sd, int quest_id);
int quest_delete(TBL_PC * sd, int quest_id);
int quest_change(TBL_PC * sd, int qid1, int qid2);
int quest_update_objective_sub(struct block_list *bl, va_list ap);
void quest_update_objective(TBL_PC * sd, int mob_id);
int quest_update_status(TBL_PC * sd, int quest_id, quest_state status);
int quest_check(TBL_PC * sd, int quest_id, quest_check_type type);

int quest_search_db(int quest_id);

void do_init_quest();

#endif
