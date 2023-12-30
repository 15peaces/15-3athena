// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _ELEMENTAL_H_
#define _ELEMENTAL_H_

#include "map.h" // struct status_data, struct view_data, struct elemental_skill
#include "status.h" // struct status_data, struct status_change
#include "unit.h" // struct unit_data

#define MAX_ELEDISTANCE 5

//Min time between AI executions
#define MIN_ELEMTHINKTIME 100
//Min time before mobs do a check to call nearby friends for help (or for slaves to support their master)
#define MIN_ELEMLINKTIME 1000

//Distance that slaves should keep from their master.
#define ELEM_SLAVEDISTANCE 3

//Used to determine default enemy type of mobs (for use in eachinrange calls)
#define DEFAULT_ELEM_ENEMY_TYPE(ed) (BL_PC | BL_MOB | BL_HOM | BL_MER | BL_ELEM)

enum {
	ELEMTYPE_AGNI = 1,
	ELEMTYPE_AQUA,
	ELEMTYPE_VENTUS,
	ELEMTYPE_TERA,
};

enum {
	CONTROL_WAIT,
	CONTROL_PASSIVE,
	CONTROL_DEFENSIVE,
	CONTROL_OFFENSIVE,
};

struct s_elemental_db {
	int class_;
	char sprite[NAME_LENGTH], name[NAME_LENGTH];
	unsigned short lv;
	short range2, range3;
	struct status_data status;
	struct view_data vd;
};

extern struct s_elemental_db elemental_db[MAX_ELEMENTAL_CLASS];

struct elemental_data {
	struct block_list bl;
	struct unit_data ud;
	struct view_data *vd;
	struct status_data base_status, battle_status;
	struct status_change sc;
	struct regen_data regen;

	struct s_elemental_db *db;
	struct s_elemental elemental;
	char blockskill[MAX_SKILL];

	int masterteleport_timer;
	struct map_session_data *master;
	int summon_timer;
	unsigned water_screen_flag : 1;

	// AI Stuff
	struct {
		enum MobSkillState skillstate;
		unsigned aggressive : 1;
		unsigned spotted: 1;
		unsigned char attacked_count;
		int provoke_flag;
		unsigned alive : 1;// Flag if the elemental is dead or alive.
	} state;
	struct {
		int id;
		unsigned int dmg;
		unsigned flag : 2;
	} dmglog[DAMAGELOG_SIZE];

	unsigned int tdmg;
	int level;
	int target_id,attacked_id;
	int64 next_walktime,last_thinktime,last_linktime,last_pcneartime;
	short min_chase;
	int master_dist;
	
	int64 skilldelay;
};

bool elem_class(int class_);
struct view_data * elem_get_viewdata(int class_);

int elem_create(struct map_session_data *sd, int class_, unsigned int lifetime);
int elem_data_received(struct s_elemental *elem, bool flag);
int elemental_save(struct elemental_data *ed);

void elemental_damage(struct elemental_data *ed, struct block_list *src, int hp, int sp);
void elemental_heal(struct elemental_data *ed, int hp, int sp);
int elemental_dead(struct elemental_data *ed, struct block_list *src);

int elem_delete(struct elemental_data *ed, int reply);
void elem_summon_stop(struct elemental_data *ed);

int elemental_get_lifetime(struct elemental_data *ed);
int elemental_get_type(struct elemental_data *ed);

int elemental_set_control_mode(struct elemental_data *ed, short control_mode);
int elemental_passive_skill(struct elemental_data *ed);
int elemental_defensive_skill(struct elemental_data *ed);
int elemental_offensive_skill(struct elemental_data *ed);

// AI Stuff
int elem_target(struct elemental_data *ed,struct block_list *bl,int dist);
int elem_unlocktarget(struct elemental_data *ed, int64 tick);
int elem_can_reach(struct elemental_data *ed,struct block_list *bl,int range, int state);
void elem_log_damage(struct elemental_data *ed, struct block_list *src, int damage);
int elemskill_use(struct elemental_data *ed, int64 tick, int bypass);

#define elem_stop_walking(ed, type) unit_stop_walking(&(ed)->bl, type)
#define elem_stop_attack(ed) unit_stop_attack(&(ed)->bl)

int do_init_elemental(void);

#endif /* _ELEMENTAL_H_ */