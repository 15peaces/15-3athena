// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _HOMUNCULUS_H_
#define _HOMUNCULUS_H_

#include "status.h" // struct status_data, struct status_change
#include "unit.h" // struct unit_data

#define MAX_HOMUN_SPHERES 10

struct h_stats {
	unsigned int HP, SP;
	unsigned short str, agi, vit, int_, dex, luk;
};

struct s_homunculus_db {
	int base_class, evo_class;
	char name[NAME_LENGTH];
	int maxlevel;
	struct h_stats base, gmin, gmax, emin, emax;
	int foodID ;
	int baseASPD ;
	long hungryDelay ;
	unsigned char element, race, base_size, evo_size;
};

extern struct s_homunculus_db homunculus_db[MAX_HOMUNCULUS_CLASS + MAX_MUTATE_HOMUNCULUS_CLASS + 31];
enum { HOMUNCULUS_CLASS, HOMUNCULUS_FOOD };

enum {
	SP_ACK      = 0x0,
	SP_INTIMATE = 0x1,
	SP_HUNGRY   = 0x2,
};

// Eleanor's Fighting Styles
enum {
	GRAPPLER_STYLE = 0,
	FIGHTER_STYLE
};

struct homun_data {
	struct block_list bl;
	struct unit_data  ud;
	struct view_data *vd;
	struct status_data base_status, battle_status;
	struct status_change sc;
	struct regen_data regen;
	struct s_homunculus_db *homunculusDB;	//[orn]
	struct s_homunculus homunculus;	//[orn]

	int masterteleport_timer;
	struct map_session_data *master; //pointer back to its master
	int hungry_timer;	//[orn]
	int int_loss_timer;	//[15peaces]
	unsigned int exp_next;
	char blockskill[MAX_SKILL];	// [orn]
	short hom_spiritball, hom_spiritball_old;
};

#define MAX_HOM_SKILL_REQUIRE 5
struct homun_skill_tree_entry {
	short id;
	unsigned char max;
	unsigned char joblv;
	short intimacylv;
	struct {
		short id;
		unsigned char lv;
	} need[MAX_HOM_SKILL_REQUIRE];
};

// HM stands for HoMunculus and MH stands for Mutated Homunculus
#define homdb_checkid(id) (id >= HM_CLASS_BASE && id <= HM_CLASS_MAX || id >= MH_CLASS_BASE && id <= MH_CLASS_MAX)

// merc_is_hom_alive(struct homun_data *)
#define hom_is_active(x) (x && x->homunculus.vaporize == 0 && x->battle_status.hp > 0)
int do_init_homunculus(void);
void do_final_homunculus(void);
int hom_recv_data(int account_id, struct s_homunculus *sh, int flag); //albator
struct view_data* hom_get_viewdata(int class_);
void hom_damage(struct homun_data *hd,struct block_list *src,int hp,int sp);
int hom_dead(struct homun_data *hd, struct block_list *src);
void hom_skillup(struct homun_data *hd,int skill_id);
int hom_calc_skilltree(struct homun_data *hd,int flag_evolve);
int hom_checkskill(struct homun_data *hd,int skill_id);
int hom_gainexp(struct homun_data *hd,int exp);
int hom_levelup(struct homun_data *hd);
int hom_evolution(struct homun_data *hd) ;
int merc_hom_mutation(struct homun_data *hd, int class_);
void hom_heal(struct homun_data *hd,int hp,int sp);
int hom_vaporize(struct map_session_data *sd, int flag);
int hom_ressurect(struct map_session_data *sd, unsigned char per, short x, short y);
void hom_revive(struct homun_data *hd, unsigned int hp, unsigned int sp);
void merc_reset_stats(struct homun_data *hd);
int hom_shuffle(struct homun_data *hd); // [Zephyrus]
int merc_hom_max(struct homun_data *hd);
void hom_save(struct homun_data *hd);
int hom_call(struct map_session_data *sd);
int hom_create_request(struct map_session_data *sd, int class_);
int hom_search(int key,int type);
int hom_menu(struct map_session_data *sd,int menunum);
int hom_food(struct map_session_data *sd, struct homun_data *hd);
int hom_hungry_timer_delete(struct homun_data *hd);
int hom_change_name(struct map_session_data *sd,char *name);
int hom_change_name_ack(struct map_session_data *sd, char* name, int flag);
#define merc_stop_walking(hd, type) unit_stop_walking(&(hd)->bl, type)
#define merc_stop_attack(hd) unit_stop_attack(&(hd)->bl)
int hom_increase_intimacy(struct homun_data * hd, unsigned int value);
int hom_decrease_intimacy(struct homun_data * hd, unsigned int value);
int hom_skill_tree_get_max(int id, int b_class);
void hom_init_timers(struct homun_data * hd);
void hom_reload_skill(void);
void hom_reload(void);

// Homunculus Spirit Spheres
int merc_hom_addspiritball(struct homun_data *hd, int max);
int merc_hom_delspiritball(struct homun_data *hd, int count);

#endif /* _HOMUNCULUS_H_ */
