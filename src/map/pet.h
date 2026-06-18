// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _PET_H_
#define _PET_H_

#include "pc.h"

#define MAX_PET_DB	300
#define MAX_PETEVOLVE_ITEMS	5
#define MAX_PETLOOT_SIZE	30

/** Pet Evolution [Dastgir/Hercules] */
struct pet_itemlist_entry {
	int id;       ///< Item ID or (inventory) index
	int16 amount; ///< Amount
};

struct pet_evolve_data {
	int petEggId;
	VECTOR_DECL(struct pet_itemlist_entry) items;
};

struct s_pet_db {
	short class_;
	char name[NAME_LENGTH],jname[NAME_LENGTH];
	t_itemid itemID;
	t_itemid EggID;
	t_itemid AcceID;
	t_itemid FoodID;
	int fullness;
	int hungry_delay;
	int hunger_increase;
	int r_hungry;
	int r_full;
	int intimate;
	int die;
	int hungry_intimacy_dec;
	int capture;
	char s_perfor;
	int attack_rate;
	int defense_attack_rate;
	int change_target_rate;
	int allow_autofeed;
	struct script_code *pet_support_script;
	struct script_code *pet_bonus_script;

	/* Pet Evolution */
	VECTOR_DECL(struct pet_evolve_data) evolve_data;
};
extern struct s_pet_db pet_db[MAX_PET_DB];

enum { PET_CLASS,PET_CATCH,PET_EGG,PET_EQUIP,PET_FOOD };

/** Pet intimacy level **/
enum e_pet_intimacy_level {
	PET_INTIMATE_NONE = 0,
	PET_INTIMATE_AWKWARD = 1,
	PET_INTIMATE_SHY = 100,
	PET_INTIMATE_NEUTRAL = 250,
	PET_INTIMATE_CORDIAL = 750,
	PET_INTIMATE_LOYAL = 910,
	PET_INTIMATE_MAX = 1000
};

enum e_pet_hungry {
	PET_HUNGRY_NONE = 0,
	PET_HUNGRY_VERY_HUNGRY = 10,
	PET_HUNGRY_HUNGRY = 25,
	PET_HUNGRY_NEUTRAL = 75,
	PET_HUNGRY_SATISFIED = 90,
	PET_HUNGRY_STUFFED = 100
};

struct pet_recovery { //Stat recovery
	enum sc_type type;	//Status Change id
	unsigned short delay; //How long before curing (secs).
	int timer;
};

struct pet_bonus {
	unsigned short type; //bStr, bVit?
	unsigned short val;	//Qty
	unsigned short duration; //in secs
	unsigned short delay;	//Time before recasting (secs)
	int timer;
};

struct pet_skill_attack { //Attack Skill
	unsigned short id;
	unsigned short lv;
	unsigned short damage; // Fixed damage value of petskillattack2
	unsigned short div_; //0 = Normal skill. >0 = Fixed damage (lv), fixed div_.
	unsigned short rate; //Base chance of skill ocurrance (10 = 10% of attacks)
	unsigned short bonusrate; //How being 100% loyal affects cast rate (10 = At 1000 intimacy->rate+10%
};

struct pet_skill_support { //Support Skill
	unsigned short id;
	unsigned short lv;
	unsigned short hp; //Max HP% for skill to trigger (50 -> 50% for Magnificat)
	unsigned short sp; //Max SP% for skill to trigger (100 = no check)
	unsigned short delay; //Time (secs) between being able to recast.
	int timer;
};

struct pet_loot {
	struct item *item;
	unsigned short count;
	unsigned short weight;
	unsigned short max;
};

struct s_petautobonus {
	int16 rate;
	uint16 atk_type;
	char* bonus_script, * other_script;
	unsigned short duration;
	int32 timer;
};

struct pet_data {
	struct block_list bl;
	struct unit_data ud;
	struct view_data vd;
	struct s_pet pet;
	struct status_data status;
	struct mob_db *db;
	struct s_pet_db *petDB;
	int pet_hungry_timer;
	int target_id;
	struct {
		unsigned skillbonus : 1;
	} state;
	int move_fail_count;
	int64 next_walktime, last_thinktime;
	unsigned short rate_fix;	//Support rate as modified by intimacy (1000 = 100%) [Skotlex]

	struct pet_recovery* recovery;
	struct pet_bonus* bonus;
	struct pet_skill_attack* a_skill;
	struct pet_skill_support* s_skill;
	struct pet_loot* loot;
	struct s_petautobonus autobonus[MAX_PC_BONUS], autobonus2[MAX_PC_BONUS], autobonus3[MAX_PC_BONUS];

	int masterteleport_timer;
	struct map_session_data *master;
};



int pet_create_egg(struct map_session_data *sd, const t_itemid item_id);
int pet_return_egg(struct map_session_data *sd, struct pet_data *pd);
int pet_hungry_val(struct pet_data *pd);
int pet_get_card4_value(const int rename_flag, const int intimacy);
int pet_egg_search(const struct map_session_data* sd, const int pet_id);
void pet_set_intimate(struct pet_data *pd, int value);
int pet_target_check(struct pet_data *pd,struct block_list *bl,int type);
int pet_unlocktarget(struct pet_data *pd);
int pet_sc_check(struct map_session_data *sd, const int type); //Skotlex
int search_petDB_index(int key,int type);
int pet_hungry_timer_delete(struct pet_data *pd);
int pet_data_init(struct map_session_data *sd, struct s_pet *pet);
int pet_birth_process(struct map_session_data *sd, struct s_pet *pet);
int pet_recv_petdata(uint32 account_id,struct s_pet *p,const int flag);
int pet_select_egg(struct map_session_data *sd,const short egg_index);
int pet_catch_process1(struct map_session_data *sd,int target_class);
int pet_catch_process2(struct map_session_data *sd,const int target_id);
int pet_get_egg(const int account_id, const short pet_class, const int pet_id);
int pet_menu(struct map_session_data *sd,const int menunum);
int pet_change_name(struct map_session_data *sd,char *name);
int pet_change_name_ack(struct map_session_data *sd, char* name, const int flag);
int pet_equipitem(struct map_session_data *sd,const int index);
int pet_lootitem_drop(struct pet_data *pd,struct map_session_data *sd);
int pet_attackskill(struct pet_data *pd, int target_id);
int pet_skill_support_timer(const int tid, const int64 tick, const int id, intptr_t data); // [Skotlex]
int pet_skill_bonus_timer(const int tid, const int64 tick, const int id, intptr_t data); // [Valaris]
int pet_recovery_timer(const int tid, int64 tick, const int id, intptr_t data); // [Valaris]
int pet_heal_timer(const int tid, const int64 tick, const int id, intptr_t data); // [Valaris]

bool pet_addautobonus(struct s_petautobonus* bonus, const char* script, int16 rate, uint32 dur, uint16 flag, const char* other_script, bool onskill);
void pet_exeautobonus(struct map_session_data* sd, struct s_petautobonus* autobonus);
void pet_delautobonus(const struct map_session_data* sd, struct s_petautobonus* bonus, bool restore);
int pet_endautobonus(int tid, int64 tick, int id, intptr_t data);

static void free_pet_db(void);
static void read_pet_evolutions(void);
static int pet_unequipitem(struct map_session_data* sd, struct pet_data* pd);
static int pet_food(struct map_session_data* sd, struct pet_data* pd);
static int pet_ai_sub_hard_lootsearch(struct block_list* bl, va_list ap);

#define pet_stop_walking(pd, type) unit_stop_walking(&(pd)->bl, type)
#define pet_stop_attack(pd) unit_stop_attack(&(pd)->bl)

void read_pet_db(void);
void do_init_pet(void);
void do_final_pet(void);

#endif /* _PET_H_ */
