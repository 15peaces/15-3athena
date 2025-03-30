// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _UNIT_H_
#define _UNIT_H_

//#include "map.h"
struct block_list;
struct unit_data;
struct map_session_data;

#include "atcommand.h"
#include "clif.h"  // clr_type
#include "map.h" // struct block_list
#include "path.h" // struct walkpath_data
#include "skill.h" // struct skill_timerskill, struct skill_unit_group, struct skill_unit_group_tickset

struct unit_data {
	struct block_list *bl;
	struct walkpath_data walkpath;
	struct skill_timerskill *skilltimerskill[MAX_SKILLTIMERSKILL];
	struct skill_unit_group *skillunit[MAX_SKILLUNITGROUP];
	struct skill_unit_group_tickset skillunittick[MAX_SKILLUNITGROUPTICKSET];
	short attacktarget_lv;
	short to_x,to_y;
	short skillx,skilly;
	short skill_id,skill_lv;
	int   skilltarget;
	int   skilltimer;
	int   target;
	int   target_to;
	int   attacktimer;
	int   walktimer;
	int   chaserange;
	bool  stepaction; //Action should be executed on step [Playtester]
	int   steptimer; //Timer that triggers the action [Playtester]
	uint16 stepskill_id, stepskill_lv; //Remembers skill that should be casted on step [Playtester]
	int64 attackabletime;
	int64 canact_tick;
	int64 canmove_tick;
	bool immune_attack; ///< Whether the unit is immune to attacks
	uint8 dir;
	unsigned char walk_count;
	unsigned char target_count;
	struct {
		unsigned change_walk_target : 1 ;
		unsigned skillcastcancel : 1 ;
		unsigned attack_continue : 1 ;
		unsigned step_attack : 1 ;
		unsigned walk_easy : 1 ;
		unsigned running : 1;
		unsigned speed_changed : 1;
	} state;
};

struct view_data {
	unsigned short class_;
	t_itemid
		weapon,
		shield, //Or left-hand weapon.
		robe,
		head_top,
		head_mid,
		head_bottom;
	uint16
		hair_style,
		hair_color,
		cloth_color,
		body_style;
	uint8 sex;
	unsigned dead_sit : 2;
};

/// Enum for unit_stop_walking
enum e_unit_stop_walking {
	USW_NONE = 0x0, /// Unit will keep walking to their original destination
	USW_FIXPOS = 0x1, /// Issue a fixpos packet afterwards
	USW_MOVE_ONCE = 0x2, /// Force the unit to move one cell if it hasn't yet
	USW_MOVE_FULL_CELL = 0x4, /// Enable moving to the next cell when unit was already half-way there (may cause on-touch/place side-effects, such as a scripted map change)
	USW_FORCE_STOP = 0x8, /// Force stop moving, even if walktimer is currently INVALID_TIMER
	USW_ALL = 0xf,
};

// PC, MOB, PET に共通する処理を１つにまとめる計画

// 歩行開始
//     戻り値は、0 ( 成功 ), 1 ( 失敗 )
static int unit_walktoxy_timer(int tid, int64 tick, int id, intptr_t data);
int unit_walktoxy( struct block_list *bl, short x, short y, int easy);
int unit_walktobl( struct block_list *bl, struct block_list *target, int range, int easy);
int unit_run(struct block_list *bl);
int unit_wugdash(struct block_list *bl, struct map_session_data *sd);// [Jobbie]
int unit_calc_pos(struct block_list *bl, int tx, int ty, int dir);
int unit_delay_walktoxy_timer(int tid, int64 tick, int id, intptr_t data);
int unit_delay_walktobl_timer(int tid, int64 tick, int id, intptr_t data);

// 歩行停止
// typeは以下の組み合わせ : 
//     1: 位置情報の送信( この関数の後に位置情報を送信する場合は不要 )
//     2: ダメージディレイ有り
//     4: 不明(MOBのみ？)
int unit_stop_walking(struct block_list *bl,int type);
int unit_can_move(struct block_list *bl);
int unit_is_walking(struct block_list *bl);
int unit_set_walkdelay(struct block_list *bl, int64 tick, int delay, int type);

int unit_escape(struct block_list *bl, struct block_list *target, short dist);
// 位置の強制移動(吹き飛ばしなど)
bool unit_movepos(struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath);
int unit_warp(struct block_list *bl, short map, short x, short y, clr_type type);
int unit_setdir(struct block_list *bl,unsigned char dir);
uint8 unit_getdir(struct block_list *bl);
int unit_blown(struct block_list* bl, int dx, int dy, int count, int flag);
uint8 unit_blown_immune(struct block_list* bl, uint8 flag);

// そこまで歩行でたどり着けるかの判定
bool unit_can_reach_pos(struct block_list *bl,int x,int y,int easy);
bool unit_can_reach_bl(struct block_list *bl,struct block_list *tbl, int range, int easy, short *x, short *y);

// Unit attack functions
void unit_stop_attack(struct block_list *bl);
int unit_attack(struct block_list *src,int target_id,int continuous);
int unit_cancel_combo(struct block_list *bl);

// スキル使用
int unit_skilluse_id(struct block_list *src, int target_id, uint16 skill_id, uint16 skill_lv);
int unit_skilluse_pos(struct block_list *src, short skill_x, uint16 skill_y, uint16 skill_id, short skill_lv);

// スキル使用( 補正済みキャスト時間、キャンセル不可設定付き )
int unit_skilluse_id2(struct block_list *src, int target_id, uint16 skill_id, uint16 skill_lv, int casttime, int castcancel);
int unit_skilluse_pos2( struct block_list *src, short skill_x, short skill_y, uint16 skill_id, uint16 skill_lv, int casttime, int castcancel);

// Step timer used for delayed attack and skill use
int unit_step_timer(int tid, int64 tick, int id, intptr_t data);
void unit_stop_stepaction(struct block_list *bl);

static int unit_attack_timer(int tid, int64 tick, int id, intptr_t data);

// Cancel unit cast
int unit_skillcastcancel(struct block_list *bl,char type);

int unit_counttargeted(struct block_list *bl);
int unit_set_target(struct unit_data* ud, int target_id);

// unit_data の初期化処理
void unit_dataset(struct block_list *bl);

// その他
struct unit_data* unit_bl2ud(struct block_list *bl);
void unit_refresh(struct block_list *bl);
void unit_remove_map_pc(struct map_session_data *sd, clr_type clrtype);
void unit_free_pc(struct map_session_data *sd);
#define unit_remove_map(bl,clrtype) unit_remove_map_(bl,clrtype,__FILE__,__LINE__,__func__)
int unit_remove_map_(struct block_list *bl, clr_type clrtype, const char* file, int line, const char* func);
int unit_free(struct block_list *bl, clr_type clrtype);
int unit_changeviewsize(struct block_list *bl,short size);

// 初期化ルーチン
void do_init_unit(void);
void do_final_unit(void);

extern const short dirx[8];
extern const short diry[8];

#endif /* _UNIT_H_ */
