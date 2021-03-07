// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef MAP_ACHIEVEMENTS_H
#define MAP_ACHIEVEMENTS_H

#include "../common/db.h"
#include "map.h" // enum _sp

#define ACHIEVEMENT_NAME_LENGTH 50
#define OBJECTIVE_DESCRIPTION_LENGTH 100

struct achievement;
struct map_session_data;
struct char_achievements;

/**
 * Achievement Types
 */
enum achievement_types {
	// Quest
	ACH_QUEST, // achievement and objective specific
	// PC Kills
	ACH_KILL_PC_TOTAL,
	ACH_KILL_PC_JOB,
	ACH_KILL_PC_JOBTYPE,
	// Mob Kills
	ACH_KILL_MOB_CLASS,
	// PC Damage
	ACH_DAMAGE_PC_MAX,
	ACH_DAMAGE_PC_TOTAL,
	ACH_DAMAGE_PC_REC_MAX,
	ACH_DAMAGE_PC_REC_TOTAL,
	// Mob Damage
	ACH_DAMAGE_MOB_MAX,
	ACH_DAMAGE_MOB_TOTAL,
	ACH_DAMAGE_MOB_REC_MAX,
	ACH_DAMAGE_MOB_REC_TOTAL,
	// Job
	ACH_JOB_CHANGE,
	// Status
	ACH_STATUS,
	ACH_STATUS_BY_JOB,
	ACH_STATUS_BY_JOBTYPE,
	// Chatroom
	ACH_CHATROOM_CREATE_DEAD,
	ACH_CHATROOM_CREATE,
	ACH_CHATROOM_MEMBERS,
	// Friend
	ACH_FRIEND_ADD,
	// Party
	ACH_PARTY_CREATE,
	ACH_PARTY_JOIN,
	// Marriage
	ACH_MARRY,
	// Adoption
	ACH_ADOPT_BABY,
	ACH_ADOPT_PARENT,
	// Zeny
	ACH_ZENY_HOLD,
	ACH_ZENY_GET_ONCE,
	ACH_ZENY_GET_TOTAL,
	ACH_ZENY_SPEND_ONCE,
	ACH_ZENY_SPEND_TOTAL,
	// Equipment
	ACH_EQUIP_REFINE_SUCCESS,
	ACH_EQUIP_REFINE_FAILURE,
	ACH_EQUIP_REFINE_SUCCESS_TOTAL,
	ACH_EQUIP_REFINE_FAILURE_TOTAL,
	ACH_EQUIP_REFINE_SUCCESS_WLV,
	ACH_EQUIP_REFINE_FAILURE_WLV,
	ACH_EQUIP_REFINE_SUCCESS_ID,
	ACH_EQUIP_REFINE_FAILURE_ID,
	// Items
	ACH_ITEM_GET_COUNT,
	ACH_ITEM_GET_COUNT_ITEMTYPE,
	ACH_ITEM_GET_WORTH,
	ACH_ITEM_SELL_WORTH,
	// Monsters
	ACH_PET_CREATE,
	// Achievement
	ACH_ACHIEVE,
	ACH_ACHIEVEMENT_RANK,
	ACH_TYPE_MAX
};

enum unique_criteria_types {
	CRITERIA_UNIQUE_NONE,
	CRITERIA_UNIQUE_ACHIEVE_ID,
	CRITERIA_UNIQUE_ITEM_ID,
	CRITERIA_UNIQUE_MOB_ID,
	CRITERIA_UNIQUE_JOB_ID,
	CRITERIA_UNIQUE_STATUS_TYPE,
	CRITERIA_UNIQUE_ITEM_TYPE,
	CRITERIA_UNIQUE_WEAPON_LV,
};

/**
 * Achievement Objective Structure
 *
 * @see achievement_type_requires_criteria()
 * @see achievement_criteria_mobid()
 * @see achievement_criteria_jobid()
 * @see achievement_criteria_itemid()
 * @see achievement_criteria_stattype()
 * @see achievement_criteria_itemtype()
 * @see achievement_criteria_weaponlv()
 */
struct achievement_objective {
	int goal;
	char description[OBJECTIVE_DESCRIPTION_LENGTH];
	/**
	 * Those criteria that do not make sense if stacked together.
	 * union identifier is set in unique_type. (@see unique_criteria_type)
	 */
	union {
		int achieve_id;
		unsigned short itemid;
		enum _sp status_type;
		int weapon_lv;
	} unique;
	enum unique_criteria_types unique_type;
	/* */
	uint32 item_type;
	int mobid;
	VECTOR_DECL(int) jobid;
};

struct achievement_reward_item {
	int id, amount;
};

struct achievement_rewards {
	int title_id;
	struct script_code *bonus;
	VECTOR_DECL(struct achievement_reward_item) item;
};

/**
 * Achievement Data Structure
 */
struct achievement_data {
	int id;
	int points;
	char name[ACHIEVEMENT_NAME_LENGTH];
	enum achievement_types type;
	VECTOR_DECL(struct achievement_objective) objective;
	struct achievement_rewards rewards;
};

// Achievements types that use Mob ID as criteria.
#define achievement_criteria_mobid(t) ( \
		   (t) == ACH_KILL_MOB_CLASS \
		|| (t) == ACH_PET_CREATE )

// Achievements types that use JobID vector as criteria.
#define achievement_criteria_jobid(t) ( \
		   (t) == ACH_KILL_PC_JOB \
		|| (t) == ACH_KILL_PC_JOBTYPE \
		|| (t) == ACH_JOB_CHANGE \
		|| (t) == ACH_STATUS_BY_JOB \
		|| (t) == ACH_STATUS_BY_JOBTYPE )

// Achievements types that use Item ID as criteria.
#define achievement_criteria_itemid(t) ( \
		   (t) == ACH_ITEM_GET_COUNT \
		|| (t) == ACH_EQUIP_REFINE_SUCCESS_ID \
		|| (t) == ACH_EQUIP_REFINE_FAILURE_ID )

// Achievements types that use status type parameter as criteria.
#define achievement_criteria_stattype(t) ( \
		   (t) == ACH_STATUS \
		|| (t) == ACH_STATUS_BY_JOB \
		|| (t) == ACH_STATUS_BY_JOBTYPE )

// Achievements types that use item type mask as criteria.
#define achievement_criteria_itemtype(t) ( \
		   (t) == ACH_ITEM_GET_COUNT_ITEMTYPE )

// Achievements types that use weapon level as criteria.
#define achievement_criteria_weaponlv(t) ( \
		   (t) == ACH_EQUIP_REFINE_SUCCESS_WLV \
		|| (t) == ACH_EQUIP_REFINE_FAILURE_WLV )

// Valid status types for objective criteria.
#define achievement_valid_status_types(s) ( \
		   (s) ==  SP_STR \
		|| (s) ==  SP_AGI \
		|| (s) ==  SP_VIT \
		|| (s) ==  SP_INT \
		|| (s) ==  SP_DEX \
		|| (s) ==  SP_LUK \
		|| (s) ==  SP_BASELEVEL || (s) ==  SP_JOBLEVEL )


struct DBMap *achievement_db; // int id -> struct achievement_data *
/* */
VECTOR_DECL(int) rank_exp; // Achievement Rank Exp Requirements
VECTOR_DECL(int) category[ACH_TYPE_MAX]; /* A collection of Ids per type for faster processing. */

const struct achievement_data *achievement_get(int aid);
struct achievement *achievement_ensure(struct map_session_data *sd, const struct achievement_data *ad);

void achievement_calculate_totals(const struct map_session_data *sd, int *total_points, int *completed, int *rank, int *curr_rank_points);
bool achievement_check_complete(struct map_session_data *sd, const struct achievement_data *ad);

bool achievement_validate(struct map_session_data *sd, int aid, unsigned int obj_idx, int progress, bool additive);
void achievement_validate_achieve(struct map_session_data *sd, int achid);
void achievement_validate_item_sell(struct map_session_data *sd, int nameid, int amount);
void achievement_validate_item_get(struct map_session_data *sd, int nameid, int amount);
void achievement_validate_stats(struct map_session_data *sd, enum _sp stat_type, int progress);
void achievement_validate_chatroom_create(struct map_session_data *sd);
void achievement_validate_chatroom_members(struct map_session_data *sd, int progress);
void achievement_validate_friend_add(struct map_session_data *sd);
void achievement_validate_achievement_rank(struct map_session_data *sd, int rank);
void achievement_validate_pc_damage(struct map_session_data *sd, struct map_session_data *dstsd, unsigned int damage);
void achievement_validate_pc_kill(struct map_session_data *sd, struct map_session_data *dstsd);
void achievement_validate_mob_damage(struct map_session_data *sd, unsigned int damage, bool received);
void achievement_validate_mob_kill(struct map_session_data *sd, int mob_id);
void achievement_validate_party_create(struct map_session_data *sd);
void achievement_validate_marry(struct map_session_data *sd);
void achievement_validate_adopt(struct map_session_data *sd, bool parent);
void achievement_validate_zeny(struct map_session_data *sd, int amount);
void achievement_validate_jobchange(struct map_session_data *sd);
void achievement_validate_taming(struct map_session_data *sd, int class);
void achievement_validate_refine(struct map_session_data *sd, unsigned int idx, bool success);

void do_init_achievement(void);
void do_final_achievement(void);

#endif /* MAP_ACHIEVEMENTS_H */
