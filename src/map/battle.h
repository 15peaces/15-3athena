// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _BATTLE_H_
#define _BATTLE_H_

#include "map.h" //ELE_MAX

// state of a single attack attempt; used in flee/def penalty calculations when mobbed
typedef enum damage_lv {
	ATK_NONE,    // not an attack
	ATK_LUCKY,   // attack was lucky-dodged
	ATK_FLEE,    // attack was dodged
	ATK_MISS,    // attack missed because of element/race modifier.
	ATK_BLOCK,   // attack was blocked by some skills.
	ATK_DEF      // attack connected
} damage_lv;

// ダメージ
struct Damage {
	int64 damage,damage2;
	int type,div_;
	int amotion,dmotion;
	int blewcount;
	int flag;
	int miscflag;
	enum damage_lv dmg_lv;	//ATK_LUCKY,ATK_FLEE,ATK_DEF
	bool isspdamage; /// Display blue damage numbers in clif_damage
};

//(Used in read pc.c,) attribute table (battle_attr_fix)
extern int attr_fix_table[4][ELE_MAX][ELE_MAX];

struct map_session_data;
struct mob_data;
struct block_list;

// ダメージ計算

struct Damage battle_calc_attack(int attack_type,struct block_list *bl,struct block_list *target,int skill_id,int skill_lv,int count);

int64 battle_calc_return_damage(struct block_list *src, struct block_list *bl, int64 *damage, int flag, int skill_id, bool status_reflect);

void battle_drain(struct map_session_data *sd, struct block_list *tbl, int64 rdamage, int64 ldamage, int race, int class_);

int battle_attr_ratio(int atk_elem,int def_type, int def_lv);
int64 battle_attr_fix(struct block_list *src, struct block_list *target, int64 damage,int atk_elem,int def_type, int def_lv);

// ダメージ最終計算
int64 battle_calc_cardfix(int attack_type, struct block_list *src, struct block_list *target, int nk, int rh_ele, int lh_ele, int64 damage, int left, int flag);
int64 battle_calc_damage(struct block_list *src,struct block_list *bl,struct Damage *d,int64 damage,int skill_id,int skill_lv);
int64 battle_calc_gvg_damage(struct block_list *src,struct block_list *bl,int64 damage,int skill_id,int flag);
int64 battle_calc_bg_damage(struct block_list *src,struct block_list *bl,int64 damage,int skill_id,int flag);

/// Flag of the final calculation
enum e_battle_flag {
	BF_WEAPON = 0x0001, /// Weapon attack
	BF_MAGIC = 0x0002, /// Magic attack
	BF_MISC = 0x0004, /// Misc attack

	BF_SHORT = 0x0010, /// Short attack
	BF_LONG = 0x0040, /// Long attack

	BF_SKILL = 0x0100, /// Skill attack
	BF_NORMAL = 0x0200, /// Normal attack

	BF_WEAPONMASK = BF_WEAPON | BF_MAGIC | BF_MISC, /// Weapon attack mask
	BF_RANGEMASK = BF_SHORT | BF_LONG, /// Range attack mask
	BF_SKILLMASK = BF_SKILL | BF_NORMAL, /// Skill attack mask
};

/// Battle check target [Skotlex]
enum e_battle_check_target {
	BCT_NOONE = 0x000000, /// No one
	BCT_SELF = 0x010000, /// Self
	BCT_ENEMY = 0x020000, /// Enemy
	BCT_PARTY = 0x040000, /// Party members
	BCT_GUILDALLY = 0x080000, /// Only allies, NOT guildmates
	BCT_NEUTRAL = 0x100000, /// Neutral target
	BCT_SAMEGUILD = 0x200000, /// Guildmates, No Guild Allies

	BCT_ALL = 0x3F0000, /// All targets

	BCT_GUILD = BCT_SAMEGUILD | BCT_GUILDALLY, /// Guild AND Allies (BCT_SAMEGUILD|BCT_GUILDALLY)
	BCT_NOGUILD = BCT_ALL & ~BCT_GUILD, /// Except guildmates
	BCT_NOPARTY = BCT_ALL & ~BCT_PARTY, /// Except party members
	BCT_NOENEMY = BCT_ALL & ~BCT_ENEMY, /// Except enemy
};

void battle_damage(struct block_list *src, struct block_list *target, int64 damage, int delay, uint16 skill_lv, uint16 skill_id, enum damage_lv dmg_lv, unsigned short attack_type, bool additional_effects, int64 tick, bool isspdamage);
int battle_delay_damage (int64 tick, int amotion, struct block_list *src, struct block_list *target, int attack_type, int skill_id, int skill_lv, int64 damage, enum damage_lv dmg_lv, int ddelay, bool additional_effects, bool isspdamage);
int battle_damage_area( struct block_list *bl, va_list ap);
// 通常攻撃処理まとめ
enum damage_lv battle_weapon_attack( struct block_list *bl,struct block_list *target,int64 tick,int flag);

// 各種パラメータを得る
struct block_list* battle_get_master(struct block_list *src);
struct block_list* battle_gettargeted(struct block_list *target);
struct block_list* battle_getenemy(struct block_list *target, int type, int range);
struct block_list* battle_getenemyarea(struct block_list *src, int x, int y, int range, int type, int ignore_id);

int battle_gettarget(struct block_list *bl);
int battle_getcurrentskill(struct block_list *bl);

//#define	is_boss(bl)	(status_get_class_(bl) == CLASS_BOSS)	// Can refine later [Aru]

int battle_check_undead(int race,int element);
int battle_check_target(struct block_list *src, struct block_list *target,int flag);
bool battle_check_range(struct block_list *src,struct block_list *bl,int range);

void battle_consume_ammo(TBL_PC*sd, int skill, int lv);

bool is_infinite_defense(struct block_list *target, int flag);

// 設定

// Human Styles and Colors
#define MIN_HAIR_STYLE battle_config.min_hair_style
#define MAX_HAIR_STYLE battle_config.max_hair_style
#define MIN_HAIR_COLOR battle_config.min_hair_color
#define MAX_HAIR_COLOR battle_config.max_hair_color
#define MIN_CLOTH_COLOR battle_config.min_cloth_color
#define MAX_CLOTH_COLOR battle_config.max_cloth_color
#define MIN_BODY_STYLE battle_config.min_body_style
#define MAX_BODY_STYLE battle_config.max_body_style

// Doram Styles and Colors
#define MIN_DORAM_HAIR_STYLE battle_config.min_doram_hair_style
#define MAX_DORAM_HAIR_STYLE battle_config.max_doram_hair_style
#define MIN_DORAM_HAIR_COLOR battle_config.min_doram_hair_color
#define MAX_DORAM_HAIR_COLOR battle_config.max_doram_hair_color
#define MIN_DORAM_CLOTH_COLOR battle_config.min_doram_cloth_color
#define MAX_DORAM_CLOTH_COLOR battle_config.max_doram_cloth_color

extern struct Battle_Config
{
	int warp_point_debug;
	int enable_critical;
	int mob_critical_rate;
	int critical_rate;
	int enable_baseatk;
	int enable_perfect_flee;
	int cast_rate, delay_rate;
	int delay_dependon_dex, delay_dependon_agi;
	int sdelay_attack_enable;
	int left_cardfix_to_right;
	int skill_add_range;
	int skill_out_range_consume;
	int skillrange_by_distance; //[Skotlex]
	int use_weapon_skill_range; //[Skotlex]
	int pc_damage_delay_rate;
	int defnotenemy;
	int vs_traps_bctall;	
	int traps_setting;
	int summon_flora; //[Skotlex]	
	int clear_unit_ondeath; //[Skotlex]
	int clear_unit_onwarp; //[Skotlex]
	int random_monster_checklv;
	int attr_recover;
	int item_auto_get;
	int flooritem_lifetime;
	int item_first_get_time;
	int item_second_get_time;
	int item_third_get_time;
	int mvp_item_first_get_time;
	int mvp_item_second_get_time;
	int mvp_item_third_get_time;
	int base_exp_rate,job_exp_rate;
	int drop_rate0item;
	int death_penalty_type;
	int death_penalty_base,death_penalty_job;
	int pvp_exp;  // [MouseJstr]
	int gtb_sc_immunity;
	int zeny_penalty;
	int restart_hp_rate;
	int restart_sp_rate;
	int mvp_exp_rate;
	int mvp_hp_rate;
	int monster_hp_rate;
	int monster_max_aspd;
	int view_range_rate;
	int chase_range_rate;
	int lowest_gm_level;
	int atc_gmonly;
	int atc_spawn_quantity_limit;
	int atc_slave_clone_limit;
	int partial_name_scan;
	int gm_allskill;
	int gm_allequip;
	int gm_skilluncond;
	int gm_join_chat;
	int gm_kick_chat;
	int skillfree;
	int skillup_limit;
	int wp_rate;
	int pp_rate;
	int monster_active_enable;
	int monster_damage_delay_rate;
	int monster_loot_type;
	int mob_skill_rate;	//[Skotlex]
	int mob_skill_delay;	//[Skotlex]
	int mob_count_rate;
	int no_spawn_on_player; //[Skotlex]
	int force_random_spawn; //[Skotlex]
	int mob_spawn_delay, plant_spawn_delay, boss_spawn_delay;	// [Skotlex]
	int slaves_inherit_mode;
	int slaves_inherit_speed;
	int summons_trigger_autospells;
	int pc_walk_delay_rate; //Adjusts can't walk delay after being hit for players. [Skotlex]
	int walk_delay_rate; //Adjusts can't walk delay after being hit. [Skotlex]
	int multihit_delay;  //Adjusts can't walk delay per hit on multi-hitting skills. [Skotlex]
	int quest_skill_learn;
	int quest_skill_reset;
	int basic_skill_check;
	int guild_emperium_check;
	int guild_exp_limit;
	int guild_max_castles;
	int guild_skill_relog_delay;
	int emergency_call;
	int guild_aura;
	int pc_invincible_time;

	int pet_catch_rate;
	int pet_rename;
	int pet_friendly_rate;
	int pet_hungry_delay_rate;
	int pet_hungry_friendly_decrease;
	int pet_status_support;
	int pet_attack_support;
	int pet_damage_support;
	int pet_support_min_friendly;	//[Skotlex]
	int pet_equip_min_friendly;
	int pet_support_rate;
	int pet_attack_exp_to_master;
	int pet_attack_exp_rate;
	int pet_lv_rate; //[Skotlex]
	int pet_max_stats; //[Skotlex]
	int pet_max_atk1; //[Skotlex]
	int pet_max_atk2; //[Skotlex]
	int pet_no_gvg; //Disables pets in gvg. [Skotlex]
	int pet_autofeed;
	int feature_petevolution;
	int pet_autofeed_rate;
	int pet_autofeed_always;
	int pet_equip_required;

	int skill_min_damage;
	int finger_offensive_type;
	int heal_exp;
	int max_heal_lv;
	int max_heal; //Mitternacht
	int resurrection_exp;
	int shop_exp;
	int combo_delay_rate;
	int item_check;
	int item_use_interval;	//[Skotlex]
	int cashfood_use_interval;
	int wedding_modifydisplay;
	int wedding_ignorepalette;	//[Skotlex]
	int xmas_ignorepalette;	// [Valaris]
	int summer_ignorepalette; // [Zephyrus]
	int natural_healhp_interval;
	int natural_healsp_interval;
	int natural_heal_skill_interval;
	int natural_heal_weight_rate;
	int arrow_decrement;
	int max_aspd;
	int max_walk_speed;	//Maximum walking speed after buffs [Skotlex]
	int max_hp;
	int max_hp_lv99;
	int max_hp_lv150;
	int max_sp;
	int max_lv, aura_lv;
	int max_parameter, max_baby_parameter;
	int max_cart_weight;
	int skill_log;
	int battle_log;
	int save_log;
	int etc_log;
	int save_clothcolor;
	int undead_detect_type;
	int auto_counter_type;
	int min_hitrate;	//[Skotlex]
	int max_hitrate;	//[Skotlex]
	int agi_penalty_target;
	int agi_penalty_type;
	int agi_penalty_count;
	int agi_penalty_num;
	int vit_penalty_target;
	int vit_penalty_type;
	int vit_penalty_count;
	int vit_penalty_num;
	int weapon_defense_type;
	int magic_defense_type;
	int skill_reiteration;
	int skill_nofootset;
	int pc_cloak_check_type;
	int monster_cloak_check_type;
	int estimation_type;
	int gvg_short_damage_rate;
	int gvg_long_damage_rate;
	int gvg_weapon_damage_rate;
	int gvg_magic_damage_rate;
	int gvg_misc_damage_rate;
	int gvg_flee_penalty;
	int gvg_eliminate_time;
	int gvg_mon_trans_disable;
	int pk_short_damage_rate;
	int pk_long_damage_rate;
	int pk_weapon_damage_rate;
	int pk_magic_damage_rate;
	int pk_misc_damage_rate;
	int mob_changetarget_byskill;
	int attack_direction_change;
	int land_skill_limit;
	int party_skill_penalty;
	int monster_class_change_recover;
	int produce_item_name_input;
	int display_skill_fail;
	int chat_warpportal;
	int mob_warp;
	int dead_branch_active;
	int vending_max_value;
	int vending_over_max;
	int vending_tax;
	int show_steal_in_same_party;
	int party_share_type;
	int party_hp_mode;
	int party_show_share_picker;
	int show_picker_item_type;
	int attack_attr_none;
	int item_rate_mvp, item_rate_common, item_rate_common_boss, item_rate_common_mvp, item_rate_card, item_rate_card_boss, item_rate_card_mvp,
		item_rate_equip, item_rate_equip_boss, item_rate_equip_mvp, item_rate_heal, item_rate_heal_boss, item_rate_heal_mvp, item_rate_use,
		item_rate_use_boss, item_rate_use_mvp, item_rate_treasure, item_rate_adddrop;

	int logarithmic_drops;
	int item_drop_common_min,item_drop_common_max;	// Added by TyrNemesis^
	int item_drop_card_min,item_drop_card_max;
	int item_drop_equip_min,item_drop_equip_max;
	int item_drop_mvp_min,item_drop_mvp_max;	// End Addition
	int item_drop_heal_min,item_drop_heal_max;	// Added by Valatris
	int item_drop_use_min,item_drop_use_max;	//End
	int item_drop_treasure_min,item_drop_treasure_max; //by [Skotlex]
	int item_drop_adddrop_min,item_drop_adddrop_max; //[Skotlex]

	int prevent_logout;	// Added by RoVeRT
	int prevent_logout_trigger;

	int alchemist_summon_reward;	// [Valaris]
	int drops_by_luk;
	int drops_by_luk2;
	int equip_natural_break_rate;	//Base Natural break rate for attacks.
	int equip_self_break_rate; //Natural & Penalty skills break rate
	int equip_skill_break_rate; //Offensive skills break rate
	int multi_level_up;
	int max_exp_gain_rate; //Max amount of exp bar % you can get in one go.
	int pk_mode;
	int pk_level_range;

	int manner_system; // end additions [Valaris]
	int show_mob_info;

	int gx_allhit;
	int gx_disptype;
	int devotion_level_difference;
	int devotion_rdamage;
	int player_skill_partner_check;
	int hide_GM_session;
	int invite_request_check;
	int skill_removetrap_type;
	int disp_experience;
	int disp_zeny;
	int castle_defense_rate;
	int backstab_bow_penalty;
	int hp_rate;
	int sp_rate;
	int gm_cant_drop_min_lv;
	int gm_cant_drop_max_lv;
	int bound_item_drop;
	int disp_hpmeter;
	int bone_drop;
	int buyer_name;
	int gm_cant_party_min_lv;
	int gm_can_party; // [SketchyPhoenix]

	int pc_camouflage_check_type;

// eAthena additions
	int night_at_start; // added by [Yor]
	int day_duration; // added by [Yor]
	int night_duration; // added by [Yor]
	int ban_hack_trade; // added by [Yor]
	int hack_info_GM_level; // added by [Yor]
	int any_warp_GM_min_level; // added by [Yor]
	int packet_ver_flag; // added by [Yor]
	int packet_ver_flag2; // expanded packet_ver_flag
	
	int min_hair_style; // added by [MouseJstr]
	int max_hair_style; // added by [MouseJstr]
	int min_hair_color; // added by [MouseJstr]
	int max_hair_color; // added by [MouseJstr]
	int min_cloth_color; // added by [MouseJstr]
	int max_cloth_color; // added by [MouseJstr]
	int min_body_style;
	int max_body_style;
	int save_body_style;
	int min_doram_hair_style;
	int max_doram_hair_style;
	int min_doram_hair_color;
	int max_doram_hair_color;
	int min_doram_cloth_color;
	int max_doram_cloth_color;

	int pet_hair_style; // added by [Skotlex]

	int castrate_dex_scale; // added by [MouseJstr]
	int area_size; // added by [MouseJstr]

	int max_def, over_def_bonus; //added by [Skotlex]
	
	int zeny_from_mobs; // [Valaris]
	int mobs_level_up; // [Valaris]
	int mobs_level_up_exp_rate; // [Valaris]
	int pk_min_level; // [celest]
	int skill_steal_max_tries; //max steal skill tries on a mob. if 0, then w/o limit [Lupus]
	int motd_type; // [celest]
	int finding_ore_rate; // orn
	int exp_calc_type;
	int exp_bonus_attacker;
	int exp_bonus_max_attacker;
	int min_skill_delay_limit;
	int default_walk_delay;
	int no_skill_delay;
	int attack_walk_delay;
	int require_glory_guild;
	int idle_no_share;
	int party_update_interval;
	int party_even_share_bonus;
	int delay_battle_damage;
	int hide_woe_damage;
	int display_version;
	int who_display_aid;

	int display_hallucination;	// [Skotlex]
	int use_statpoint_table;	// [Skotlex]

	int ignore_items_gender; //[Lupus]

	int berserk_cancels_buffs; // [Aru]
	int debuff_on_logout; // Removes a few "official" negative Scs on logout. [Skotlex]
	int mob_ai; //Configures various mob_ai settings to make them smarter or dumber(official). [Skotlex]
	int hom_setting; //Configures various homunc settings which make them behave unlike normal characters.. [Skotlex]
	int dynamic_mobs; // Dynamic Mobs [Wizputer] - battle_athena flag implemented by [random]
	int mob_remove_damaged; // Dynamic Mobs - Remove mobs even if damaged [Wizputer]
	int mob_remove_delay; // Dynamic Mobs - delay before removing mobs from a map [Skotlex]
	int mob_active_time; //Duration through which mobs execute their Hard AI after players leave their area of sight.
	int boss_active_time;

	int show_hp_sp_drain, show_hp_sp_gain;	//[Skotlex]

	int mob_npc_event_type; //Determines on who the npc_event is executed. [Skotlex]
	int mob_clear_delay; // [Valaris]

	int character_size; // if riders have size=2, and baby class riders size=1 [Lupus]
	int mob_max_skilllvl; // Max possible skill level [Lupus]
	int rare_drop_announce; // chance <= to show rare drops global announces

	int retaliate_to_master;	//Whether when a mob is attacked by another mob, it will retaliate versus the mob or the mob's master. [Skotlex]

	int title_lvl1; // Players titles [Lupus]
	int title_lvl2; // Players titles [Lupus]
	int title_lvl3; // Players titles [Lupus]
	int title_lvl4; // Players titles [Lupus]
	int title_lvl5; // Players titles [Lupus]
	int title_lvl6; // Players titles [Lupus]
	int title_lvl7; // Players titles [Lupus]
	int title_lvl8; // Players titles [Lupus]
	
	int duel_allow_pvp; // [LuzZza]
	int duel_allow_gvg; // [LuzZza]
	int duel_allow_teleport; // [LuzZza]
	int duel_autoleave_when_die; // [LuzZza]
	int duel_time_interval; // [LuzZza]
	int duel_only_on_same_map; // [Toms]
	
	int skip_teleport_lv1_menu; // possibility to disable (skip) Teleport Lv1 menu, that have only two lines `Random` and `Cancel` [LuzZza]

	int allow_skill_without_day; // [Komurka]
	int allow_es_magic_pc; // [Skotlex]
	int skill_wall_check; // [Skotlex]
	int official_cell_stack_limit; // [Playtester]
	int custom_cell_stack_limit; // [Skotlex]
	int skill_caster_check; // [Skotlex]
	int sc_castcancel; // [Skotlex]
	int pc_sc_def_rate; // [Skotlex]
	int mob_sc_def_rate;
	int pc_max_sc_def;
	int mob_max_sc_def;

	int sg_angel_skill_ratio;
	int sg_miracle_skill_ratio;
	int sg_miracle_skill_duration;
	int autospell_stacking; //Enables autospell cards to stack. [Skotlex]
	int override_mob_names; //Enables overriding spawn mob names with the mob_db names. [Skotlex]
	int min_chat_delay; //Minimum time between client messages. [Skotlex]
	int friend_auto_add; //When accepting friends, both get friended. [Skotlex]
	int hvan_explosion_intimate;	// fix [albator]
	int hom_rename;
	int homunculus_show_growth ;	//[orn]
	int homunculus_friendly_rate;
	int quest_exp_rate;
	int autotrade_mapflag;
	int at_timeout;
	int homunculus_autoloot;
	int idle_no_autoloot;
	int max_guild_alliance;
	int ksprotection;
	int auction_feeperhour;
	int auction_maximumprice;
	int gm_viewequip_min_lv;
	int homunculus_auto_vapor;	//Keep Homunculus from Vaporizing when master dies. [L0ne_W0lf]
	int display_status_timers;	//Show or hide skill buff/delay timers in recent clients [Sara]
	int skill_add_heal_rate;	//skills that bHealPower has effect on [Inkfish]
	int eq_single_target_reflectable;
	int invincible_nodamage;
	int mob_slave_keep_target;
	int autospell_check_range;	//Enable range check for autospell bonus. [L0ne_W0lf]
	int knockback_left;
	int client_reshuffle_dice;  // Reshuffle /dice
	int client_sort_storage;
	int gm_check_minlevel;  // min GM level for /check
	int feature_buying_store;
	int feature_search_stores;
	int searchstore_querydelay;
	int searchstore_maxresults;
	int display_party_name;
	int cashshop_show_points;
	int mail_show_status;
	int client_limit_unit_lv;

	// [BattleGround Settings]
	int bg_update_interval;
	int bg_short_damage_rate;
	int bg_long_damage_rate;
	int bg_weapon_damage_rate;
	int bg_magic_damage_rate;
	int bg_misc_damage_rate;
	int bg_flee_penalty;
	int bg_queue;
	int bg_queue_maximum_afk_seconds;

	// 15-3athena
	int load_custom_exp_tables;
	int renewal_casting_renewal_skills;
	int renewal_casting_square_calc;
	int renewal_casting_square_debug;
	int castrate_dex_scale_renewal_jobs;
	int cooldown_rate;
	int min_skill_cooldown_limit;
	int no_skill_cooldown;
	int max_parameter_renewal_jobs;
	int max_baby_parameter_renewal_jobs;
	int renewal_stats_handling;
	int max_aspd_renewal_jobs;
	int hanbok_ignorepalette;
	int oktoberfest_ignorepalette;
	int summer2_ignorepalette;
	int all_riding_speed;
	int falcon_and_wug;
	int transform_end_on_death;
	int renewal_level_effect_skills;
	int base_lv_skill_effect_limit;
	int job_lv_skill_effect_limit;
	int metallicsound_spburn_rate;
	int cashshop_price_rate;
	int death_penalty_maxlv;
	int mado_skill_limit;
	int mado_loss_on_death;
	int mado_cast_skill_on_limit;
	int marionette_renewal_jobs;
	int banana_bomb_sit_duration;
	// Renewal EDP Formula Config For Guillotine Cross Skills
	int gc_skill_edp_boost_formula_a;
	int gc_skill_edp_boost_formula_b;
	int gc_skill_edp_boost_formula_c;
	int mob_spawn_variance;
	int slave_stick_with_master;
	int skill_amotion_leniency;
	// Cell PVP [Napster]
	int cellpvp_deathmatch;
	int cellpvp_deathmatch_delay;
	int deathmatch_hp_rate;
	int deathmatch_sp_rate;
	int cellpvp_autobuff;
	int cellpvp_party_enable;
	int cellpvp_guild_enable;
	int cellpvp_walkout_delay;
	//New Guild Configs
	int guild_create;
	int guild_break;
	int guild_disable_invite;
	int guild_disable_expel;
	//Other (renewal) Features
	int feature_auction;
	int feature_banking;
	int mvp_tomb_enabled;
	int mvp_tomb_delay;
	int feature_roulette;
	int monster_hp_bars_info;
	int costume_refine_def;
	int shadow_refine_def;
	int mail_delay;
	int mail_daily_count;
	int mail_zeny_fee;
	int mail_attachment_price;
	int mail_attachment_weight;
	int feature_achievement;
	int natural_homun_healhp_interval;
	int natural_homun_healsp_interval;
	int max_homunculus_hp;
	int max_homunculus_sp;
	int max_homunculus_parameter;
	int hom_bonus_exp_from_master;
	// Elemental Settings
	int elemental_masters_walk_speed;
	int elem_ai;
	int elem_defensive_support;
	int elem_defensive_attack_skill;
	int elem_offensive_skill_chance;
	int elem_offensive_skill_casttime;
	int elem_offensive_skill_aftercast;
	int natural_elem_heal_interval;
	int max_elemental_hp;
	int max_elemental_sp;
	int max_elemental_def_mdef;
	int feature_equipswitch;
	int mvp_exp_reward_message;
	int feature_attendance_system;
	int attendance_show_window;
	int attendance_starttime;
	int attendance_endtime;
	int player_baselv_req_skill;
	int warmer_show_heal;
	int baby_hp_sp_penalty;
	int baby_crafting_penalty;
	int allow_bloody_lust_on_boss;
	int allow_bloody_lust_on_warp;
	int homunculus_pyroclastic_autocast;
	int pyroclastic_breaks_weapon;
	int millennium_shield_health;
	int giant_growth_behavior;
	int mass_spiral_max_def;
	int rebel_base_lv_skill_effect;
	int hesperuslit_bonus_stack;
	int homun_autofeed;
	int drop_connection_on_quit;
	int cart_revo_knockback;
	int guild_notice_changemap;
	int item_auto_identify;
	int path_blown_halt;
	int taekwon_mission_mobname;
	int teleport_on_portal;
	int min_npc_vendchat_distance;
	int skill_trap_type;
	int item_enabled_npc;
	int item_onfloor;
	int emblem_woe_change;
	int emblem_transparency_limit;
	int discount_item_point_shop;
	int dancing_weaponswitch_fix;
	int allow_consume_restricted_item;
	int allow_equip_restricted_item;
	int guild_leaderchange_delay;
	int guild_leaderchange_woe;
	int default_bind_on_equip;
	int devotion_rdamage_skill_only;
	int mob_chase_refresh;
	int magiccrasher_renewal;
	int arrow_shower_knockback;
	int min_npc_vending_distance;
	int spawn_direction;
	int mob_icewall_walk_block;
	int boss_icewall_walk_block;
	int snap_dodge;
	int stormgust_knockback;
	int monster_loot_search_type;
	int mob_stuck_warning;
	int skill_eightpath_algorithm;
	int can_damage_skill;
	int mob_eye_range_bonus;
	int tarotcard_equal_chance;
	//Episode System [15peaces]
	int feature_episode;
	int episode_readdb;
} battle_config;

void do_init_battle(void);
void do_final_battle(void);
extern int battle_config_read(const char *cfgName);
extern void battle_set_defaults(void);
int battle_set_value(const char* w1, const char* w2);
int battle_get_value(const char* w1);

#endif /* _BATTLE_H_ */
