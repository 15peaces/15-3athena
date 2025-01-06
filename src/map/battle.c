// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/random.h"
#include "../common/ers.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "map.h"
#include "path.h"
#include "pc.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "pet.h"
#include "guild.h"
#include "party.h"
#include "battleground.h"

#include <stdlib.h>
#include <math.h>

int attr_fix_table[4][ELE_MAX][ELE_MAX];

struct Battle_Config battle_config;
static struct eri *delay_damage_ers; //For battle delay damage structures.

/**
 * Returns the current/list skill used by the bl
 * @param bl
 * @return skill_id
 */
int battle_getcurrentskill(struct block_list *bl)
{	//Returns the current/last skill in use by this bl.
	struct unit_data *ud;

	if( bl->type == BL_SKILL )
	{
		struct skill_unit * su = (struct skill_unit*)bl;
		return (su && su->group ? su->group->skill_id : 0);
	}
	ud = unit_bl2ud(bl);
	return (ud ? ud->skill_id : 0);
}

/*==========================================
 * Get random targetting enemy
 *------------------------------------------*/
static int battle_gettargeted_sub(struct block_list *bl, va_list ap)
{
	struct block_list **bl_list;
	struct unit_data *ud;
	int target_id;
	int *c;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	target_id = va_arg(ap, int);

	if (bl->id == target_id)
		return 0;
	if (*c >= 24)
		return 0;

	ud = unit_bl2ud(bl);
	if (!ud) return 0;

	if (ud->target == target_id || ud->skilltarget == target_id) {
		bl_list[(*c)++] = bl;
		return 1;
	}
	return 0;	
}

struct block_list* battle_gettargeted(struct block_list *target)
{
	struct block_list *bl_list[24];
	int c = 0;
	nullpo_retr(NULL, target);

	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinrange(battle_gettargeted_sub, target, AREA_SIZE, BL_CHAR, bl_list, &c, target->id);
	if (c == 0 || c > 24)
		return NULL;
	return bl_list[rnd()%c];
}

static int battle_getenemyarea_sub(struct block_list *bl, va_list ap)
{
	struct block_list **bl_list, *src;
	int *c, ignore_id;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	src = va_arg(ap, struct block_list *);
	ignore_id = va_arg(ap, int);

	if( bl->id == src->id || bl->id == ignore_id )
		return 0; // Ignores Caster and a possible pre-target
	if( *c >= 24 )
		return 0;
	if( status_isdead(bl) )
		return 0;
	if( battle_check_target(src, bl, BCT_ENEMY) > 0 )
	{ // Is Enemy!...
		bl_list[(*c)++] = bl;
		return 1;
	}
	return 0;	
}

// Pick a random enemy
struct block_list* battle_getenemyarea(struct block_list *src, int x, int y, int range, int type, int ignore_id)
{
	struct block_list *bl_list[24];
	int c = 0;
	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinarea(battle_getenemyarea_sub, src->m, x - range, y - range, x + range, y + range, type, bl_list, &c, src, ignore_id);
	if( c == 0 || c > 24 )
		return NULL;
	return bl_list[rnd()%c];
}


//Returns the id of the current targetted character of the passed bl. [Skotlex]
int battle_gettarget(struct block_list* bl)
{
	switch (bl->type)
	{
		case BL_PC:  return ((struct map_session_data*)bl)->ud.target;
		case BL_MOB: return ((struct mob_data*)bl)->target_id;
		case BL_PET: return ((struct pet_data*)bl)->target_id;
		case BL_HOM: return ((struct homun_data*)bl)->ud.target;
		case BL_MER: return ((struct mercenary_data*)bl)->ud.target;
		case BL_ELEM: return ((struct elemental_data*)bl)->ud.target;
	}
	return 0;
}

static int battle_getenemy_sub(struct block_list *bl, va_list ap)
{
	struct block_list **bl_list;
	struct block_list *target;
	int *c;

	bl_list = va_arg(ap, struct block_list **);
	c = va_arg(ap, int *);
	target = va_arg(ap, struct block_list *);

	if (bl->id == target->id)
		return 0;
	if (*c >= 24)
		return 0;
	if (status_isdead(bl))
		return 0;
	if (battle_check_target(target, bl, BCT_ENEMY) > 0) {
		bl_list[(*c)++] = bl;
		return 1;
	}
	return 0;	
}

// Picks a random enemy of the given type (BL_PC, BL_CHAR, etc) within the range given. [Skotlex]
struct block_list* battle_getenemy(struct block_list *target, int type, int range)
{
	struct block_list *bl_list[24];
	int c = 0;
	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinrange(battle_getenemy_sub, target, range, type, bl_list, &c, target);
	if (c == 0 || c > 24)
		return NULL;
	return bl_list[rnd()%c];
}

/*========================================== [Playtester]
* Deals damage without delay, applies additional effects and triggers monster events
* This function is called from battle_delay_damage or battle_delay_damage_sub
* @param src: Source of damage
* @param target: Target of damage
* @param damage: Damage to be dealt
* @param delay: Damage delay
* @param skill_lv: Level of skill used
* @param skill_id: ID o skill used
* @param dmg_lv: State of the attack (miss, etc.)
* @param attack_type: Damage delay
* @param additional_effects: Whether additional effect should be applied
* @param tick: Current tick
*------------------------------------------*/
void battle_damage(struct block_list *src, struct block_list *target, int64 damage, int delay, uint16 skill_lv, uint16 skill_id, enum damage_lv dmg_lv, unsigned short attack_type, bool additional_effects, int64 tick) {
	map_freeblock_lock();
	status_fix_damage(src, target, damage, delay); // We have to separate here between reflect damage and others [icescope]
	if (attack_type && !status_isdead(target) && additional_effects)
		skill_additional_effect(src, target, skill_id, skill_lv, attack_type, dmg_lv, tick);
	if (dmg_lv > ATK_BLOCK && attack_type)
		skill_counter_additional_effect(src, target, skill_id, skill_lv, attack_type, tick);
	// This is the last place where we have access to the actual damage type, so any monster events depending on type must be placed here
	if (target->type == BL_MOB && damage && (attack_type&BF_NORMAL)) {
		// Monsters differentiate whether they have been attacked by a skill or a normal attack
		struct mob_data* md = BL_CAST(BL_MOB, target);
		md->norm_attacked_id = md->attacked_id;
	}
	map_freeblock_unlock();
}

// Dammage delayed info
struct delay_damage {
	struct block_list *src;
	int target;
	int64 damage;
	int delay;
	unsigned short distance;
	unsigned short skill_lv;
	unsigned short skill_id;
	enum damage_lv dmg_lv;
	unsigned short attack_type;
	bool additional_effects;
	enum bl_type src_type;
};

int battle_delay_damage_sub(int tid, int64 tick, int id, intptr_t data)
{
	struct delay_damage *dat = (struct delay_damage *)data;

	if (dat)
	{
		struct block_list *target = map_id2bl(dat->target);

		if (!target || status_isdead(target)) { /* Nothing we can do */
			if (dat->src_type == BL_PC &&
				--((TBL_PC*)dat->src)->delayed_damage == 0 && ((TBL_PC*)dat->src)->state.hold_recalc) {
				((TBL_PC*)dat->src)->state.hold_recalc = 0;
				status_calc_pc(((TBL_PC*)dat->src), SCO_FORCE);
			}
			ers_free(delay_damage_ers, dat);
			return 0;
		}

		if (map_id2bl(id) == dat->src && target->prev != NULL && !status_isdead(target) &&
			target->m == dat->src->m &&
			(target->type != BL_PC || ((TBL_PC*)target)->invincible_timer == INVALID_TIMER) &&
			check_distance_bl(dat->src, target, dat->distance)) //Check to see if you haven't teleported. [Skotlex]
		{
			//Deal damage
			battle_damage(dat->src, target, dat->damage, dat->delay, dat->skill_lv, dat->skill_id, dat->dmg_lv, dat->attack_type, dat->additional_effects, tick);
		}

		if (dat->src && dat->src->type == BL_PC && --((TBL_PC*)dat->src)->delayed_damage == 0 && ((TBL_PC*)dat->src)->state.hold_recalc) {
			((TBL_PC*)dat->src)->state.hold_recalc = 0;
			status_calc_pc(((TBL_PC*)dat->src), SCO_FORCE);
		}
	}
	ers_free(delay_damage_ers, dat);
	return 0;
}

int battle_delay_damage (int64 tick, int amotion, struct block_list *src, struct block_list *target, int attack_type, int skill_id, int skill_lv, int64 damage, enum damage_lv dmg_lv, int ddelay, bool additional_effects)
{
	struct delay_damage *dat;
	struct status_change *sc;
	struct block_list *d_tbl = NULL;
	nullpo_ret(src);
	nullpo_ret(target);

	sc = status_get_sc(target);

  	if (sc && sc->data[SC_DEVOTION] && sc->data[SC_DEVOTION]->val1)
 		d_tbl = map_id2bl(sc->data[SC_DEVOTION]->val1);

	if (d_tbl && check_distance_bl(target, d_tbl, sc->data[SC_DEVOTION]->val3) &&
		damage > 0 && skill_id != PA_PRESSURE && skill_id != CR_REFLECTSHIELD)
		damage = 0;

	if (!battle_config.delay_battle_damage || amotion <= 1) {
		//Deal damage
		battle_damage(src, target, damage, ddelay, skill_lv, skill_id, dmg_lv, attack_type, additional_effects, gettick());
		return 0;
	}
	dat = ers_alloc(delay_damage_ers, struct delay_damage);
	dat->src = src;
	dat->target = target->id;
	dat->skill_id = skill_id;
	dat->skill_lv = skill_lv;
	dat->attack_type = attack_type;
	dat->damage = damage;
	dat->dmg_lv = dmg_lv;
	dat->delay = ddelay;
	dat->distance = distance_bl(src, target) + (battle_config.snap_dodge ? 10 : AREA_SIZE);
	dat->additional_effects = additional_effects;
	dat->src_type = src->type;
	if (src->type != BL_PC && amotion > 1000)
		amotion = 1000; //Aegis places a damage-delay cap of 1 sec to non player attacks. [Skotlex]

	if (src->type == BL_PC)
		((TBL_PC*)src)->delayed_damage++;

	add_timer(tick+amotion, battle_delay_damage_sub, src->id, (intptr_t)dat);
	
	return 0;
}

int battle_attr_ratio(int atk_elem,int def_type, int def_lv)
{
	
	if (!CHK_ELEMENT(atk_elem) || !CHK_ELEMENT(def_type) || def_lv < 1 || def_lv > 4)
		return 100;

	return attr_fix_table[def_lv-1][atk_elem][def_type];
}

/*==========================================
 * Does attribute fix modifiers. 
 * Added passing of the chars so that the status changes can affect it. [Skotlex]
 * Note: Passing src/target == NULL is perfectly valid, it skips SC_ checks.
 *------------------------------------------*/
int64 battle_attr_fix(struct block_list *src, struct block_list *target, int64 damage,int atk_elem,int def_type, int def_lv)
{
	struct map_session_data *sd, *tsd;
	struct status_change *sc=NULL, *tsc=NULL;
	int ratio;

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	
	if (src) sc = status_get_sc(src);
	if (target) tsc = status_get_sc(target);
	
	if (!CHK_ELEMENT(atk_elem))
		atk_elem = rnd()%ELE_ALL;

	if (!CHK_ELEMENT(def_type) || def_lv < 1 || def_lv > 4) {
		ShowError("battle_attr_fix: unknown attr type: atk=%d def_type=%d def_lv=%d\n",atk_elem,def_type,def_lv);
		return damage;
	}

	ratio = attr_fix_table[def_lv-1][atk_elem][def_type];
	if (sc && sc->count) { //increase dmg by src status
		switch (atk_elem) {
			case ELE_FIRE:
				if (sc->data[SC_VOLCANO]) ratio += sc->data[SC_VOLCANO]->val3;
				break;
			case ELE_WIND:
				if (sc->data[SC_VIOLENTGALE]) ratio += sc->data[SC_VIOLENTGALE]->val3;
				break;
			case ELE_WATER:
				if (sc->data[SC_DELUGE]) ratio += sc->data[SC_DELUGE]->val3;
				break;
			case ELE_GHOST:
				if (sc->data[SC_TELEKINESIS_INTENSE])  ratio += (sc->data[SC_TELEKINESIS_INTENSE]->val2);
				break;
		}
	}
	if ( tsd && tsd->charmball > 0 && atk_elem == tsd->charmball_type )
		ratio -= 2 * tsd->charmball;

	if (tsc && tsc->count) { //increase dmg by target status
		switch (atk_elem) {
			case ELE_NEUTRAL:
				if (tsc->data[SC_ANTI_M_BLAST])
					damage += (int64)(damage * tsc->data[SC_ANTI_M_BLAST]->val2/100);
				break;
			case ELE_WATER:
				if (tsc->data[SC_FIRE_INSIGNIA])
					ratio += 50;
				break;
			case ELE_EARTH:
				if (tsc->data[SC_WIND_INSIGNIA])
					ratio += 50;
				status_change_end(target, SC_MAGNETICFIELD, INVALID_TIMER); //freed if received earth dmg
				break;
			case ELE_FIRE:
				if (tsc->data[SC_SPIDERWEB]) {
					tsc->data[SC_SPIDERWEB]->val1 = 0; // free to move now
					if (tsc->data[SC_SPIDERWEB]->val2-- > 0)
						damage <<= 1; // double damage
					if (tsc->data[SC_SPIDERWEB]->val2 == 0)
						status_change_end(target, SC_SPIDERWEB, INVALID_TIMER);
				}
				if (tsc->data[SC_THORNS_TRAP])
					status_change_end(target, SC_THORNS_TRAP, INVALID_TIMER);
				if (tsc->data[SC_CRYSTALIZE] && target->type != BL_MOB) {
					status_change_end(target, SC_CRYSTALIZE, INVALID_TIMER);
				}
				if (tsc->data[SC_VOLCANIC_ASH] && atk_elem == ELE_FIRE)
					damage += damage * 50 / 100;
				if (tsc->data[SC_EARTH_INSIGNIA])
					ratio += 50;
				break;
			case ELE_HOLY:
				if (tsc->data[SC_ORATIO])
					ratio += tsc->data[SC_ORATIO]->val1 * 2;
				break;
			case ELE_POISON:
				if (tsc->data[SC_VENOMIMPRESS])
					ratio += tsc->data[SC_VENOMIMPRESS]->val2;
				break;
			case ELE_WIND:
				if (tsc->data[SC_CRYSTALIZE] && target->type != BL_MOB)
					damage = damage * 150 / 100;
				if (tsc->data[SC_WATER_INSIGNIA])
					ratio += 50;
				break;

		}
	} //end tsc check

	if( target && target->type == BL_SKILL )
	{
		if( atk_elem == ELE_FIRE && battle_getcurrentskill(target) == GN_WALLOFTHORN )
		{
			struct skill_unit *su = (struct skill_unit*)target;
			struct skill_unit_group *sg;
			struct block_list *src2;

			if( !su || !su->alive || (sg = su->group) == NULL || !sg || sg->val3 == -1 ||
				(src2 = map_id2bl(su->val2)) == NULL || status_isdead(src2) )
				return 0;
			
			if( sg->unit_id != UNT_FIREWALL ){
				int x, y;

				x = sg->val3 >> 16;
				y = sg->val3 & 0xffff;
				skill_unitsetting(src2,su->group->skill_id,su->group->skill_lv,x,y,1);
				sg->val3 = -1;
				sg->limit = DIFF_TICK32(gettick(),sg->tick)+300;
			}
		}
	}
	return damage*ratio/100;
}

/*==========================================
 * Calculates card bonuses damage adjustments.
 * @param attack_type @see enum e_battle_flag
 * @param src Attacker
 * @param target Target
 * @param nk Skill's nk @see enum e_skill_nk [NK_NO_CARDFIX_ATK|NK_NO_ELEFIX|NK_NO_CARDFIX_DEF]
 * @param rh_ele Right-hand weapon element
 * @param lh_ele Left-hand weapon element (BF_MAGIC and BF_MISC ignore this value)
 * @param damage Original damage
 * @param left Left hand flag (BF_MISC and BF_MAGIC ignore flag value)
 *         3: Calculates attacker bonuses in both hands.
 *         2: Calculates attacker bonuses in right-hand only.
 *         0 or 1: Only calculates target bonuses.
 * @param flag Misc value of skill & damage flags
 * @return damage Damage diff between original damage and after calculation
 *------------------------------------------*/
int64 battle_calc_cardfix(int attack_type, struct block_list *src, struct block_list *target, int nk, int rh_ele, int lh_ele, int64 damage, int left, int flag) {
	struct map_session_data *sd, *tsd;
	short cardfix = 1000, t_class, s_class, s_race2, t_race2, s_defele;
	struct status_data *sstatus, *tstatus;
	int i;
	int64 original_damage;

	if (!damage)
		return 0;

	original_damage = damage;

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	t_class = status_get_class(target);
	s_class = status_get_class(src);
	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);
	s_race2 = status_get_race2(src);
	s_defele = (tsd) ? status_get_element(src) : ELE_NONE;

//Official servers apply the cardfix value on a base of 1000 and round down the reduction/increase
#define APPLY_CARDFIX(damage, fix) { (damage) = (damage) - (int64)(((damage) * (1000 - (fix))) / 1000); }

	switch (attack_type) {
	case BF_MAGIC:
		// Affected by attacker ATK bonuses
		if (sd && !(nk&NK_NO_CARDFIX_ATK)) {
			cardfix = cardfix * (100 + sd->magic_addrace[tstatus->race] + sd->magic_addrace[RC_ALL]) / 100;
			if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
				cardfix = cardfix * (100 + sd->magic_addele[tstatus->def_ele] + sd->magic_addele[ELE_ALL]) / 100;
				cardfix = cardfix * (100 + sd->magic_atk_ele[rh_ele] + sd->magic_atk_ele[ELE_ALL]) / 100;
			}
			cardfix = cardfix * (100 + sd->magic_addsize[tstatus->size] + sd->magic_addsize[SZ_ALL]) / 100;
			cardfix = cardfix * (100 + sd->magic_addclass[tstatus->class_] + sd->magic_addclass[CLASS_ALL]) / 100;
			for (i = 0; i < ARRAYLENGTH(sd->add_mdmg) && sd->add_mdmg[i].rate; i++) {
				if (sd->add_mdmg[i].class_ == t_class) {
					cardfix = cardfix * (100 + sd->add_mdmg[i].rate) / 100;
					break;
				}
			}
			APPLY_CARDFIX(damage, cardfix);
		}
		
		// Affected by target DEF bonuses
		if (tsd && !(nk&NK_NO_CARDFIX_DEF)) {
			cardfix = 1000; // reset var for target

			if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
				int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

				for (i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate != 0; i++) {
					if (tsd->subele2[i].ele != rh_ele)
						continue;
					if (!(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
						((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
						((tsd->subele2[i].flag)&flag)&BF_SKILLMASK))
						continue;
					ele_fix += tsd->subele2[i].rate;
				}
				if (s_defele != ELE_NONE)
					ele_fix += tsd->subdefele[s_defele] + tsd->subdefele[ELE_ALL];
				cardfix = cardfix * (100 - ele_fix) / 100;
			}
			cardfix = cardfix * (100 - tsd->subsize[sstatus->size] - tsd->subsize[SZ_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
			cardfix = cardfix * (100 - tsd->subrace[sstatus->race] - tsd->subrace[RC_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subclass[sstatus->class_] - tsd->subclass[CLASS_ALL]) / 100;

			for (i = 0; i < ARRAYLENGTH(tsd->add_mdef) && tsd->add_mdef[i].rate; i++) {
				if (tsd->add_mdef[i].class_ == s_class) {
					cardfix = cardfix * (100 - tsd->add_mdef[i].rate) / 100;
					break;
				}
			}
			
			//It was discovered that ranged defense also counts vs magic! [Skotlex]
			if (flag&BF_SHORT)
				cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
			else
				cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;

			cardfix = cardfix * (100 - tsd->bonus.magic_def_rate) / 100;

			if (tsd->sc.data[SC_MDEF_RATE])
				cardfix = cardfix * (100 - tsd->sc.data[SC_MDEF_RATE]->val1) / 100;
			APPLY_CARDFIX(damage, cardfix);
		}
		break;
	case BF_WEAPON:
		t_race2 = status_get_race2(target);
		// Affected by attacker ATK bonuses
		if (sd && !(nk&NK_NO_CARDFIX_ATK) && (left & 2)) {
			short cardfix_ = 1000;

			if (sd->state.arrow_atk) { // Ranged attack
				cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->arrow_addrace[tstatus->race] +
					sd->right_weapon.addrace[RC_ALL] + sd->arrow_addrace[RC_ALL]) / 100;
				if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
					int ele_fix = sd->right_weapon.addele[tstatus->def_ele] + sd->arrow_addele[tstatus->def_ele] +
						sd->right_weapon.addele[ELE_ALL] + sd->arrow_addele[ELE_ALL];

					for (i = 0; ARRAYLENGTH(sd->right_weapon.addele2) > i && sd->right_weapon.addele2[i].rate != 0; i++) {
						if (sd->right_weapon.addele2[i].ele != tstatus->def_ele)
							continue;
						if (!(((sd->right_weapon.addele2[i].flag)&flag)&BF_WEAPONMASK &&
							((sd->right_weapon.addele2[i].flag)&flag)&BF_RANGEMASK &&
							((sd->right_weapon.addele2[i].flag)&flag)&BF_SKILLMASK))
							continue;
						ele_fix += sd->right_weapon.addele2[i].rate;
					}
					cardfix = cardfix * (100 + ele_fix) / 100;
				}
				cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->arrow_addsize[tstatus->size] +
					sd->right_weapon.addsize[SZ_ALL] + sd->arrow_addsize[SZ_ALL]) / 100;
				cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2]) / 100;
				cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->arrow_addclass[tstatus->class_] +
					sd->right_weapon.addclass[CLASS_ALL] + sd->arrow_addclass[CLASS_ALL]) / 100;
			}
			else { // Melee attack
				int skill = 0;

				// Calculates each right & left hand weapon bonuses separatedly
				if (!battle_config.left_cardfix_to_right) {
					// Right-handed weapon
					cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->right_weapon.addrace[RC_ALL]) / 100;
					if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
						int ele_fix = sd->right_weapon.addele[tstatus->def_ele] + sd->right_weapon.addele[ELE_ALL];

						for (i = 0; ARRAYLENGTH(sd->right_weapon.addele2) > i && sd->right_weapon.addele2[i].rate != 0; i++) {
							if (sd->right_weapon.addele2[i].ele != tstatus->def_ele)
								continue;
							if (!(((sd->right_weapon.addele2[i].flag)&flag)&BF_WEAPONMASK &&
								((sd->right_weapon.addele2[i].flag)&flag)&BF_RANGEMASK &&
								((sd->right_weapon.addele2[i].flag)&flag)&BF_SKILLMASK))
								continue;
							ele_fix += sd->right_weapon.addele2[i].rate;
						}
						cardfix = cardfix * (100 + ele_fix) / 100;
					}
					cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->right_weapon.addsize[SZ_ALL]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->right_weapon.addclass[CLASS_ALL]) / 100;

					if (left & 1) { // Left-handed weapon
						cardfix_ = cardfix_ * (100 + sd->left_weapon.addrace[tstatus->race] + sd->left_weapon.addrace[RC_ALL]) / 100;
						if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
							int ele_fix_lh = sd->left_weapon.addele[tstatus->def_ele] + sd->left_weapon.addele[ELE_ALL];

							for (i = 0; ARRAYLENGTH(sd->left_weapon.addele2) > i && sd->left_weapon.addele2[i].rate != 0; i++) {
								if (sd->left_weapon.addele2[i].ele != tstatus->def_ele)
									continue;
								if (!(((sd->left_weapon.addele2[i].flag)&flag)&BF_WEAPONMASK &&
									((sd->left_weapon.addele2[i].flag)&flag)&BF_RANGEMASK &&
									((sd->left_weapon.addele2[i].flag)&flag)&BF_SKILLMASK))
									continue;
								ele_fix_lh += sd->left_weapon.addele2[i].rate;
							}
							cardfix_ = cardfix_ * (100 + ele_fix_lh) / 100;
						}
						cardfix_ = cardfix_ * (100 + sd->left_weapon.addsize[tstatus->size] + sd->left_weapon.addsize[SZ_ALL]) / 100;
						cardfix_ = cardfix_ * (100 + sd->left_weapon.addrace2[t_race2]) / 100;
						cardfix_ = cardfix_ * (100 + sd->left_weapon.addclass[tstatus->class_] + sd->left_weapon.addclass[CLASS_ALL]) / 100;
					}
				}
				// Calculates right & left hand weapon as unity
				else {
					//! CHECKME: If 'left_cardfix_to_right' is yes, doesn't need to check NK_NO_ELEFIX?
					//if( !(nk&NK_NO_ELEFIX) ) { // Affected by Element modifier bonuses
					int ele_fix = sd->right_weapon.addele[tstatus->def_ele] + sd->left_weapon.addele[tstatus->def_ele]
						+ sd->right_weapon.addele[ELE_ALL] + sd->left_weapon.addele[ELE_ALL];

					for (i = 0; ARRAYLENGTH(sd->right_weapon.addele2) > i && sd->right_weapon.addele2[i].rate != 0; i++) {
						if (sd->right_weapon.addele2[i].ele != tstatus->def_ele)
							continue;
						if (!(((sd->right_weapon.addele2[i].flag)&flag)&BF_WEAPONMASK &&
							((sd->right_weapon.addele2[i].flag)&flag)&BF_RANGEMASK &&
							((sd->right_weapon.addele2[i].flag)&flag)&BF_SKILLMASK))
							continue;
						ele_fix += sd->right_weapon.addele2[i].rate;
					}
					for (i = 0; ARRAYLENGTH(sd->left_weapon.addele2) > i && sd->left_weapon.addele2[i].rate != 0; i++) {
						if (sd->left_weapon.addele2[i].ele != tstatus->def_ele)
							continue;
						if (!(((sd->left_weapon.addele2[i].flag)&flag)&BF_WEAPONMASK &&
							((sd->left_weapon.addele2[i].flag)&flag)&BF_RANGEMASK &&
							((sd->left_weapon.addele2[i].flag)&flag)&BF_SKILLMASK))
							continue;
						ele_fix += sd->left_weapon.addele2[i].rate;
					}
					cardfix = cardfix * (100 + ele_fix) / 100;
					//}
					cardfix = cardfix * (100 + sd->right_weapon.addrace[tstatus->race] + sd->left_weapon.addrace[tstatus->race] +
						sd->right_weapon.addrace[RC_ALL] + sd->left_weapon.addrace[RC_ALL]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addsize[tstatus->size] + sd->left_weapon.addsize[tstatus->size] +
						sd->right_weapon.addsize[SZ_ALL] + sd->left_weapon.addsize[SZ_ALL]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addrace2[t_race2] + sd->left_weapon.addrace2[t_race2]) / 100;
					cardfix = cardfix * (100 + sd->right_weapon.addclass[tstatus->class_] + sd->left_weapon.addclass[tstatus->class_] +
						sd->right_weapon.addclass[CLASS_ALL] + sd->left_weapon.addclass[CLASS_ALL]) / 100;
				}
				if (sd->status.weapon == W_KATAR && (skill = pc_checkskill(sd, ASC_KATAR)) > 0) // Adv. Katar Mastery functions similar to a +%ATK card on official [helvetica]
					cardfix = cardfix * (100 + (10 + 2 * skill)) / 100;
			}

			//! CHECKME: These right & left hand weapon ignores 'left_cardfix_to_right'?
			for (i = 0; i < ARRAYLENGTH(sd->right_weapon.add_dmg) && sd->right_weapon.add_dmg[i].rate; i++) {
				if (sd->right_weapon.add_dmg[i].class_ == t_class) {
					cardfix = cardfix * (100 + sd->right_weapon.add_dmg[i].rate) / 100;
					break;
				}
			}
			if (left & 1) {
				for (i = 0; i < ARRAYLENGTH(sd->left_weapon.add_dmg) && sd->left_weapon.add_dmg[i].rate; i++) {
					if (sd->left_weapon.add_dmg[i].class_ == t_class) {
						cardfix_ = cardfix_ * (100 + sd->left_weapon.add_dmg[i].rate) / 100;
						break;
					}
				}
			}
			
			if (flag&BF_LONG)
				cardfix = cardfix * (100 + sd->bonus.long_attack_atk_rate) / 100;

			if (left & 1) {
				APPLY_CARDFIX(damage, cardfix_);
			}
			else {
				APPLY_CARDFIX(damage, cardfix);
			}
		}
		// Affected by target DEF bonuses
		else if( tsd && !(nk&NK_NO_CARDFIX_DEF) && !(left&2) ) {
				if( !(nk&NK_NO_ELEFIX) ) { // Affected by Element modifier bonuses
					int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

				for (i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate != 0; i++) {
					if (tsd->subele2[i].ele != rh_ele)
						continue;
					if (!(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
						((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
						((tsd->subele2[i].flag)&flag)&BF_SKILLMASK))
						continue;
					ele_fix += tsd->subele2[i].rate;
				}
				cardfix = cardfix * (100 - ele_fix) / 100;
				
				if (left & 1 && lh_ele != rh_ele) {
					int ele_fix_lh = tsd->subele[lh_ele] + tsd->subele[ELE_ALL];

					for (i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate != 0; i++) {
						if (tsd->subele2[i].ele != lh_ele)
							continue;
						if (!(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
							((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
							((tsd->subele2[i].flag)&flag)&BF_SKILLMASK))
							continue;
						ele_fix_lh += tsd->subele2[i].rate;
					}
					cardfix = cardfix * (100 - ele_fix_lh) / 100;
				}

				cardfix = cardfix * (100 - tsd->subdefele[s_defele] - tsd->subdefele[ELE_ALL]) / 100;
			}
			cardfix = cardfix * (100 - tsd->subsize[sstatus->size] - tsd->subsize[SZ_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
			cardfix = cardfix * (100 - tsd->subrace[sstatus->race] - tsd->subrace[RC_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subclass[sstatus->class_] - tsd->subclass[CLASS_ALL]) / 100;
			for (i = 0; i < ARRAYLENGTH(tsd->add_def) && tsd->add_def[i].rate; i++) {
				if (tsd->add_def[i].class_ == s_class) {
					cardfix = cardfix * (100 - tsd->add_def[i].rate) / 100;
					break;
				}
			}
			if (flag&BF_SHORT)
				cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
			else	// BF_LONG (there's no other choice)
				cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;
			if (tsd->sc.data[SC_DEF_RATE])
				cardfix = cardfix * (100 - tsd->sc.data[SC_DEF_RATE]->val1) / 100;
			APPLY_CARDFIX(damage, cardfix);
		}
		break;

	case BF_MISC:
		// Affected by target DEF bonuses
		if (tsd && !(nk&NK_NO_CARDFIX_DEF)) {
			if (!(nk&NK_NO_ELEFIX)) { // Affected by Element modifier bonuses
				int ele_fix = tsd->subele[rh_ele] + tsd->subele[ELE_ALL];

				for (i = 0; ARRAYLENGTH(tsd->subele2) > i && tsd->subele2[i].rate != 0; i++) {
					if (tsd->subele2[i].ele != rh_ele)
						continue;
					if (!(((tsd->subele2[i].flag)&flag)&BF_WEAPONMASK &&
						((tsd->subele2[i].flag)&flag)&BF_RANGEMASK &&
						((tsd->subele2[i].flag)&flag)&BF_SKILLMASK))
						continue;
					ele_fix += tsd->subele2[i].rate;
				}
				if (s_defele != ELE_NONE)
					ele_fix += tsd->subdefele[s_defele] + tsd->subdefele[ELE_ALL];
				cardfix = cardfix * (100 - ele_fix) / 100;
			}
			cardfix = cardfix * (100 - tsd->subsize[sstatus->size] - tsd->subsize[SZ_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subrace2[s_race2]) / 100;
			cardfix = cardfix * (100 - tsd->subrace[sstatus->race] - tsd->subrace[RC_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->subclass[sstatus->class_] - tsd->subclass[CLASS_ALL]) / 100;
			cardfix = cardfix * (100 - tsd->bonus.misc_def_rate) / 100;
			if (flag&BF_SHORT)
				cardfix = cardfix * (100 - tsd->bonus.near_attack_def_rate) / 100;
			else	// BF_LONG (there's no other choice)
				cardfix = cardfix * (100 - tsd->bonus.long_attack_def_rate) / 100;
			APPLY_CARDFIX(damage, cardfix);
		}
		break;
	}

#undef APPLY_CARDFIX

	return damage - original_damage;
}

/**
 * Check Safety Wall and Pneuma effect.
 * Maybe expand this to move checks the target's SC from battle_calc_damage?
 * @param src Attacker
 * @param target Target of attack
 * @param sc STatus Change
 * @param d Damage data
 * @param damage Damage received
 * @param skill_id
 * @param skill_lv
 * @return True:Damage inflicted, False:Missed
 **/
bool battle_check_sc(struct block_list *src, struct block_list *target, struct status_change *sc, struct Damage *d, int64 damage, uint16 skill_id, uint16 skill_lv) {
	if (!sc)
		return true;
	if (sc->data[SC_SAFETYWALL] && (d->flag&(BF_SHORT | BF_MAGIC)) == BF_SHORT) {
		struct skill_unit_group* group = skill_id2group(sc->data[SC_SAFETYWALL]->val3);
		uint16 skill_id_val = sc->data[SC_SAFETYWALL]->val2;
		if (group) {
			if (skill_id_val == MH_STEINWAND) {
				if (--group->val2 <= 0)
					skill_delunitgroup(group);
				d->dmg_lv = ATK_BLOCK;
				if ((group->val3 - damage) > 0)
					group->val3 -= (int)cap_value(damage, INT_MIN, INT_MAX);
				else
					skill_delunitgroup(group);
				return false;
			}
			//in RE, SW possesses a lifetime equal to group val2, (3x caster hp, or homon formula)
			d->dmg_lv = ATK_BLOCK;
			if (--group->val2 <= 0)
				skill_delunitgroup(group);
			return false;
		}
		status_change_end(target, SC_SAFETYWALL, INVALID_TIMER);
	}
	if ((sc->data[SC_PNEUMA] || sc->data[SC_NEUTRALBARRIER] || sc->data[SC_ZEPHYR]) && (d->flag&(BF_MAGIC | BF_LONG)) == BF_LONG) {
		d->dmg_lv = ATK_BLOCK;
		skill_blown(src, target, skill_get_blewcount(skill_id, skill_lv), -1, 0);
		return false;
	}
	return true;
}

/*==========================================
 * Check dammage trough status.
 * ATK may be MISS, BLOCKED FAIL, reduc, increase, end status...
 * After this we apply bg/gvg reduction
 *------------------------------------------*/
int64 battle_calc_damage(struct block_list *src,struct block_list *bl,struct Damage *d,int64 damage,int skill_id,int skill_lv){
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct homun_data *hd = NULL, *thd = NULL;
	struct status_change *sc, *tsc;
	struct status_change_entry *sce;
	int div_ = d->div_, flag = d->flag, element;

	nullpo_ret(src);
	nullpo_ret(bl);

	element = skill_get_ele(skill_id, skill_lv);

	if( !damage )
		return 0;
	if(battle_config.ksprotection && mob_ksprotected(src, bl) )
		return 0;

	if (bl->type == BL_PC) {
		sd=(struct map_session_data *)bl;
		//Special no damage states
		if(flag&BF_WEAPON && sd->special_state.no_weapon_damage)
			damage -= damage*sd->special_state.no_weapon_damage/100;

		if(flag&BF_MAGIC && sd->special_state.no_magic_damage)
			damage -= damage*sd->special_state.no_magic_damage/100;

		if(flag&BF_MISC && sd->special_state.no_misc_damage)
			damage -= damage*sd->special_state.no_misc_damage/100;

		if(!damage) return 0;
	}
	else if (bl->type == BL_HOM)
		hd = (struct homun_data *)bl;

	if (src->type == BL_PC)
		tsd=(struct map_session_data *)src;
	else if ( src->type == BL_HOM )
		thd=(struct homun_data *)src;

	sc = status_get_sc(bl);
	tsc = status_get_sc(src);

	if( sc && sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF] )
		return 1;

	if (skill_id == PA_PRESSURE || skill_id == HW_GRAVITATION  || skill_id == SP_SOULEXPLOSION)
		return damage; //These skills bypass everything else.

	// Nothing can reduce the damage, but Safety Wall and Millennium Shield can block it completely.
	// So can defense sphere's but what the heck is that??? [Rytech]
	if ( skill_id == SJ_NOVAEXPLOSING && !(sc && (sc->data[SC_SAFETYWALL] || sc->data[SC_MILLENNIUMSHIELD])) )
		return damage;

	if( sc && sc->count )
	{
		//First, sc_*'s that reduce damage to 0.
		if (sc->data[SC_BASILICA] && !(status_get_mode(src)&MD_BOSS) && skill_id != PA_PRESSURE && skill_id != SP_SOULEXPLOSION)
		{
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( sc->data[SC_KINGS_GRACE] )
		{
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( (sce=sc->data[SC_ZEPHYR]) && rnd()%100 < sce->val2 )
		{
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if (!battle_check_sc(src, bl, sc, d, damage, skill_id, skill_lv))
			return 0;

		if (sc->data[SC__MANHOLE] || (src->type == BL_PC && sc->data[SC_KINGS_GRACE])) {
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if( map_getcell(bl->m,bl->x,bl->y,CELL_CHKMAELSTROM) && (flag&BF_MAGIC) && skill_id && (skill_get_inf(skill_id)&INF_GROUND_SKILL) ) {
			int64 sp = damage * 20 / 100; // Steel need official value.
			status_heal(bl,0,sp,3);
			d->dmg_lv = ATK_BLOCK;
			return 0;
		}

		if (sc->data[SC_WEAPONBLOCKING] && flag&(BF_SHORT|BF_WEAPON) && rnd()%100 < sc->data[SC_WEAPONBLOCKING]->val2)
		{
			clif_skill_nodamage(bl, src, GC_WEAPONBLOCKING, 1, 1);
			d->dmg_lv = ATK_NONE;
			sc_start2(bl, SC_COMBO, 100, GC_WEAPONBLOCKING, src->id, 2000);
			return 0;
		}

		if( (sce=sc->data[SC_AUTOGUARD]) && flag&BF_WEAPON && !(skill_get_nk(skill_id)&NK_NO_CARDFIX_ATK) && rnd()%100 < sce->val2 ) {
			int delay;
			struct status_change_entry *sce_d = sc->data[SC_DEVOTION];
			struct block_list *d_bl = NULL;

			// different delay depending on skill level [celest]
			if (sce->val1 <= 5)
				delay = 300;
			else if (sce->val1 > 5 && sce->val1 <= 9)
				delay = 200;
			else
				delay = 100;

			if (sd && pc_issit(sd))
				pc_setstand(sd, true);

			// Devo fix [15peaces]
			if( sce_d && (d_bl = map_id2bl(sce_d->val1)) &&
				((d_bl->type == BL_MER && ((TBL_MER*)d_bl)->master && ((TBL_MER*)d_bl)->master->bl.id == bl->id) ||
				(d_bl->type == BL_PC && ((TBL_PC*)d_bl)->devotion[sce_d->val2] == bl->id)) &&
				check_distance_bl(bl,d_bl,sce_d->val3) )
			{ //If player is target of devotion, show guard effect on the devotion caster rather than the target
				clif_skill_nodamage(d_bl,d_bl,CR_AUTOGUARD,sce->val1,1);
				unit_set_walkdelay(d_bl,gettick(),delay,1);
				d->dmg_lv = ATK_MISS;
				return 0;
			} else {
				clif_skill_nodamage(bl,bl,CR_AUTOGUARD,sce->val1,1);
				unit_set_walkdelay(bl,gettick(),delay,1);
				if( sc->data[SC_SHRINK] && rnd()%100 < 5 * sce->val1 )
					skill_blown(bl,src,skill_get_blewcount(CR_SHRINK,1),-1,0);
				d->dmg_lv = ATK_MISS;
				return 0;
			}
		}

		if( (sce=sc->data[SC_PARRYING]) && flag&BF_WEAPON && skill_id != WS_CARTTERMINATION && rnd()%100 < sce->val2 )
		{ // attack blocked by Parrying
			clif_skill_nodamage(bl, bl, LK_PARRYING, sce->val1,1);
			return 0;
		}
		
		if(sc->data[SC_DODGE] && (!sc->opt1 || sc->opt1 == OPT1_BURNING) &&
			(flag&BF_LONG || sc->data[SC_SPURT])
		&& rnd()%100 < 20) {
			if (sd && pc_issit(sd))
				pc_setstand(sd, false); //Stand it to dodge. rAthena forces it, should it really ignore SC_SITDOWN_FORCE? [15peaces]
			clif_skill_nodamage(bl,bl,TK_DODGE,1,1);
			if (!sc->data[SC_COMBO])
				sc_start4(bl, SC_COMBO, 100, TK_JUMPKICK, src->id, 1, 0, 2000);
			return 0;
		}

		if(sc->data[SC_HERMODE] && flag&BF_MAGIC)
			return 0;

		if(sc->data[SC_TATAMIGAESHI] && (flag&(BF_MAGIC|BF_LONG)) == BF_LONG)
			return 0;

		if((sce=sc->data[SC_KAUPE]) && rnd()%100 < sce->val2)
		{	//Kaupe blocks damage (skill or otherwise) from players, mobs, homuns, mercenaries.
			clif_specialeffect(bl, 462, AREA);
			//Shouldn't end until Breaker's non-weapon part connects.
			if (skill_id != ASC_BREAKER || !(flag&BF_WEAPON))
				if (--(sce->val3) <= 0) //We make it work like Safety Wall, even though it only blocks 1 time.
					status_change_end(bl, SC_KAUPE, INVALID_TIMER);
			return 0;
		}

		if((sce=sc->data[SC_MEIKYOUSISUI]) && rnd()%100 < sce->val2)
		{// Animation is unofficial, but it allows players to know when the nullify chance was successful. [Rytech]
			clif_specialeffect(bl, 462, AREA);
			return 0;
		}

		if (flag&BF_MAGIC && (sce = sc->data[SC_PRESTIGE]) && rnd() % 100 < sce->val3)
		{
			clif_specialeffect(bl, 462, AREA); // Still need confirm it.
 			return 0;
 		}

		if (((sce=sc->data[SC_UTSUSEMI]) || sc->data[SC_BUNSINJYUTSU])
		&& 
			flag&BF_WEAPON && !(skill_get_nk(skill_id)&NK_NO_CARDFIX_ATK))
		{
			if (sce) {
				clif_specialeffect(bl, 462, AREA);
				skill_blown(src,bl,sce->val3,-1,0);
			}
			//Both need to be consumed if they are active.
			if (sce && --(sce->val2) <= 0)
				status_change_end(bl, SC_UTSUSEMI, INVALID_TIMER);
			if ((sce=sc->data[SC_BUNSINJYUTSU]) && --(sce->val2) <= 0)
				status_change_end(bl, SC_BUNSINJYUTSU, INVALID_TIMER);
			return 0;
		}

		//Now damage increasing effects
		if( sc->data[SC_AETERNA] && skill_id != PF_SOULBURN )
		{
			if( src->type != BL_MER || skill_id == 0 )
				damage <<= 1; // Lex Aeterna only doubles damage of regular attacks from mercenaries

			if( skill_id != ASC_BREAKER || !(flag&BF_WEAPON) )
				status_change_end(bl, SC_AETERNA, INVALID_TIMER); //Shouldn't end until Breaker's non-weapon part connects.
		}

		if( sc->data[SC_IMPRISON] )
		{
			if ( element == ELE_GHOST )
			{
				if( skill_id == MG_NAPALMBEAT || skill_id == MG_SOULSTRIKE || skill_id == HW_NAPALMVULCAN || skill_id == WL_SOULEXPANSION )
					damage <<= 1;// Deals double damage with Napalm Beat, Soul Strike, Napalm Vulcan, and Soul Expansion
				status_change_end(bl, SC_IMPRISON, INVALID_TIMER);
			}
			else
			{
				d->dmg_lv = ATK_BLOCK;
				return 0;
			}
		}

		if (damage)
		{
			if (sc->data[SC_DEEPSLEEP]) {
				damage += damage / 2; // 1.5 times more damage while in Deep Sleep.
				status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
			}

			if (sc->data[SC_SIREN])
				status_change_end(bl, SC_SIREN, INVALID_TIMER);
		}

		if ( sc->data[SC_CRYSTALIZE] && flag&BF_WEAPON )
		{
			if ( ((TBL_PC *)src)->status.weapon == W_DAGGER || ((TBL_PC *)src)->status.weapon == W_1HSWORD ||
				((TBL_PC *)src)->status.weapon == W_2HSWORD || ((TBL_PC *)src)->status.weapon == W_BOW )
				damage -= damage * 50 / 100;
			else if ( ((TBL_PC *)src)->status.weapon == W_1HAXE || ((TBL_PC *)src)->status.weapon == W_2HAXE ||
				((TBL_PC *)src)->status.weapon == W_MACE || ((TBL_PC *)src)->status.weapon == W_2HMACE )
				damage += damage * 50 / 100;
		}

		if( sc->data[SC_DEVOTION] ) {
			struct status_change_entry *sce_d = sc->data[SC_DEVOTION];
			struct block_list *d_bl = map_id2bl(sce_d->val1);

			if( d_bl &&
				((d_bl->type == BL_MER && ((TBL_MER*)d_bl)->master && ((TBL_MER*)d_bl)->master->bl.id == bl->id) ||
				(d_bl->type == BL_PC && ((TBL_PC*)d_bl)->devotion[sce_d->val2] == bl->id)) &&
				check_distance_bl(bl,d_bl,sce_d->val3) )
			{
				struct status_change *d_sc = status_get_sc(d_bl);

				if( d_sc && d_sc->data[SC_DEFENDER] && (flag&(BF_LONG|BF_MAGIC)) == BF_LONG && skill_id != ASC_BREAKER && skill_id != CR_ACIDDEMONSTRATION && skill_id != NJ_ZENYNAGE && skill_id != KO_MUCHANAGE )
					damage -= damage * d_sc->data[SC_DEFENDER]->val2 / 100;
			}
		}

		if (sc->data[SC_DARKCROW] && (flag&(BF_SHORT | BF_WEAPON)) == (BF_SHORT | BF_WEAPON))
			damage += damage * sc->data[SC_DARKCROW]->val2 / 100;

		//Finally damage reductions....
		if( sc->data[SC_PAIN_KILLER] )
		{
			damage -= sc->data[SC_PAIN_KILLER]->val4;

			if ( damage < 1 )
				damage = 1;
		}

		if( sc->data[SC_ASSUMPTIO] )
		{
			if( map_flag_vs(bl->m) )
				damage = damage*2/3; //Receive 66% damage
			else
				damage >>= 1; //Receive 50% damage
		}

		if(sc->data[SC_DEFENDER] &&
			(flag&(BF_LONG|BF_WEAPON)) == (BF_LONG|BF_WEAPON))
			damage=damage*(100-sc->data[SC_DEFENDER]->val2)/100;

		if(sc->data[SC_ADJUSTMENT] &&
			(flag&(BF_LONG|BF_WEAPON)) == (BF_LONG|BF_WEAPON))
			damage -= 20*damage/100;

		if(sc->data[SC_FOGWALL]) {
			if(flag&BF_SKILL) //25% reduction
				damage -= 25*damage/100;
			else if ((flag&(BF_LONG|BF_WEAPON)) == (BF_LONG|BF_WEAPON))
				damage >>= 2; //75% reduction
		}

		if (sc->data[SC_ARMORCHANGE]) {
			//On official servers, SC_ARMORCHANGE does not change DEF/MDEF but rather increases/decreases the damage
			if (flag&BF_WEAPON)
				damage = damage * (sc->data[SC_ARMORCHANGE]->val2) / 100;
			else if (flag&BF_MAGIC)
				damage = damage * (sc->data[SC_ARMORCHANGE]->val3) / 100;
		}

		if( sc->data[SC_FIRE_EXPANSION_SMOKE_POWDER] )
		{
			if( (flag&(BF_SHORT|BF_WEAPON)) == (BF_SHORT|BF_WEAPON) )
				damage -= 15 * damage / 100;//15% reduction to physical melee attacks
			else if( (flag&(BF_LONG|BF_WEAPON)) == (BF_LONG|BF_WEAPON) )
				damage -= 50 * damage / 100;//50% reduction to physical ranged attacks
		}

		if ( sc->data[SC_SU_STOOP] )
			damage -= damage * 90 / 100;

		if (sc->data[SC_GRANITIC_ARMOR])
			damage -= damage * sc->data[SC_GRANITIC_ARMOR]->val2 / 100;

		// Compressed code, fixed by map.h [Epoque]
		if (src->type == BL_MOB) {
			int i;
			if (sc->data[SC_MANU_DEF])
				for (i=0;ARRAYLENGTH(mob_manuk)>i;i++)
					if (mob_manuk[i]==((TBL_MOB*)src)->mob_id) {
						damage -= sc->data[SC_MANU_DEF]->val1*damage/100;
						break;
					}
			if (sc->data[SC_SPL_DEF])
				for (i=0;ARRAYLENGTH(mob_splendide)>i;i++)
					if (mob_splendide[i]==((TBL_MOB*)src)->mob_id) {
						damage -= sc->data[SC_SPL_DEF]->val1*damage/100;
						break;
					}
		}

		if((sce=sc->data[SC_ARMOR]) && //NPC_DEFENDER
			sce->val3&flag && sce->val4&flag)
			damage -= damage*sc->data[SC_ARMOR]->val2/100;

		if(sc->data[SC_ENERGYCOAT] && flag&BF_WEAPON
			&& skill_id != WS_CARTTERMINATION)
		{
			struct status_data *status = status_get_status_data(bl);
			int per = 100*status->sp / status->max_sp -1; //100% should be counted as the 80~99% interval
			per /=20; //Uses 20% SP intervals.
			//SP Cost: 1% + 0.5% per every 20% SP
			if (!status_charge(bl, 0, (10+5*per)*status->max_sp/1000))
				status_change_end(bl, SC_ENERGYCOAT, INVALID_TIMER);
			//Reduction: 6% + 6% every 20%
			damage -= damage * 6 * (1+per) / 100;
		}

		if ((sce = sc->data[SC_STONEHARDSKIN]) && damage > 0)
		{
			if ((flag&(BF_SHORT | BF_WEAPON)) == (BF_SHORT | BF_WEAPON))
				skill_break_equip(src, EQP_WEAPON, 3000, BCT_SELF);// Need to code something that will lower a monster's attack by 25%. [Rytech]

			sce->val3 -= (int)damage;

			if (sce->val3 <= 0)
				status_change_end(bl, SC_STONEHARDSKIN, INVALID_TIMER);
		}

		//Finally added to remove the status of immobile when aimedbolt is used. [Jobbie]
		if (skill_id == RA_AIMEDBOLT && (sc->data[SC_WUGBITE] || sc->data[SC_ANKLE] || sc->data[SC_ELECTRICSHOCKER]))
		{
			status_change_end(bl, SC_WUGBITE, INVALID_TIMER);
			status_change_end(bl, SC_ANKLE, INVALID_TIMER);
			status_change_end(bl, SC_ELECTRICSHOCKER, INVALID_TIMER);
		}

		//Finally Kyrie because it may, or not, reduce damage to 0.
		if((sce = sc->data[SC_KYRIE]) && damage > 0) {
			sce->val2-=(int)cap_value(damage, INT_MIN, INT_MAX);
			if(flag&BF_WEAPON || skill_id == TF_THROWSTONE) {
				if(sce->val2>=0)
					damage=0;
				else
				  	damage=-sce->val2;
			}
			if((--sce->val3)<=0 || (sce->val2<=0) || skill_id == AL_HOLYLIGHT)
				status_change_end(bl, SC_KYRIE, INVALID_TIMER);
		}

		// Millennium Shield
		if ( sd && sd->shieldball > 0 && damage > 0 )
		{
			sd->shieldball_health-=(int)cap_value(damage, INT_MIN, INT_MAX);
			damage = 0;

			if ( sd->shieldball_health < 1 )
			{
				pc_delshieldball(sd, 1, 0);
				status_change_start(src, bl, SC_STUN, 10000, 0, 0, 0, 0, 1000, 2);
			}
		}

		if ( (sce = sc->data[SC_LIGHTNINGWALK]) && damage > 0 && (flag&BF_WEAPON && flag&BF_LONG) && rnd()%100 < sce->val2 )
		{
			if( unit_movepos(bl, src->x, src->y, 1, 1) )
			{	// Self knock back 1 cell to make it appear you warped
				// next to the enemy you targeted from the direction
				// you attacked from.
				skill_blown(src,bl,1,unit_getdir(bl),0);
				unit_setdir(bl, map_calc_dir(bl, src->x, src->y));
			}
			status_change_end(bl, SC_LIGHTNINGWALK, INVALID_TIMER);
			return 0;
		}

		// Tuna Party protects from damage or at least reduces it if possible.
		// Kyrie takes priority before Tuna Party.
		if((sce = sc->data[SC_TUNAPARTY]) && damage > 0)
		{
			sce->val2 -= (int)cap_value(damage, INT_MIN, INT_MAX);

			// Does it protect from certain attack types or all types? [Rytech]
			// Does it display any kind of animation at all when hit?
			// Does the character flinch when hit?
			//if(flag&BF_WEAPON)
				if(sce->val2 >= 0)
					damage = 0;
				else
				  	damage =- sce->val2;

			if(sce->val2 <= 0)
				status_change_end(bl, SC_TUNAPARTY, INVALID_TIMER);
		}

		if ( (sce=sc->data[SC_CRESCENTELBOW]) && damage > 0 && !is_boss(bl) && (flag&BF_WEAPON && flag&BF_SHORT) && rnd()%100 < sce->val2 )
		{
			struct status_data *tstatus = status_get_status_data(bl);
			// Ratio part of the damage is reduceable and affected by other means. Additional damage after that is not.
			struct Damage ced = battle_calc_attack(BF_WEAPON, bl, src, SR_CRESCENTELBOW_AUTOSPELL, sce->val1, 0);
			int64 elbow_damage = ced.damage + damage * ( 100 + 20 * sce->val1 ) / 100;

			// Attacker gets the full damage.
			clif_damage(bl, src, gettick(), tstatus->amotion, 0, elbow_damage, 1, 4, 0, false);
			status_zap(src, elbow_damage, 0);

			// Caster takes 10% of the damage.
			clif_damage(bl, bl, gettick(), tstatus->amotion, 0, elbow_damage / 10, 1, 4, 0, false);
			status_zap(bl, elbow_damage / 10, 0);

			// Activate the passive part of the skill to show skill animation, deal knockback, and do damage if pushed into a wall.
			skill_castend_nodamage_id(bl, src, SR_CRESCENTELBOW_AUTOSPELL, sce->val1, gettick(), 0);

			return 0;
		}

		if (!damage)
			return 0;

		//Probably not the most correct place, but it'll do here
		//(since battle_drain is strictly for players currently)
		if ((sce=sc->data[SC_BLOODLUST]) && flag&BF_WEAPON && damage > 0 && rnd()%100 < sce->val3)
			status_heal(src, damage*sce->val4/100, 0, 3);

		if( sd && (sce = sc->data[SC_FORCEOFVANGUARD]) && rnd()%100 < sce->val2 )
			pc_addrageball(sd, skill_get_time(LG_FORCEOFVANGUARD,sce->val1), sce->val3);

		if( sd && (sce = sc->data[SC_GENTLETOUCH_ENERGYGAIN]) && rnd()%100 < sce->val2 )
		{
			short spheremax = 5;

			if ( sc->data[SC_RAISINGDRAGON] )
				spheremax += sc->data[SC_RAISINGDRAGON]->val1;

			pc_addspiritball(sd, skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sce->val1), spheremax);
		}

		if (sc->data[SC__DEADLYINFECT] && flag&BF_SHORT && damage > 0 && rnd() % 100 < 30 + 10 * sc->data[SC__DEADLYINFECT]->val1)
			status_change_spread(bl, src); // Deadly infect attacked side

		if ( hd && (sce = sc->data[SC_STYLE_CHANGE]) && sce->val1 == GRAPPLER_STYLE && rnd()%100 < sce->val2 )
			merc_hom_addspiritball(hd,MAX_HOMUN_SPHERES);

		// Magma Flow autotriggers a splash AoE around self by chance when hit.
		if ((sce = sc->data[SC_MAGMA_FLOW]) && rnd() % 100 < 3 * sce->val1)
			skill_castend_nodamage_id(bl, bl, MH_MAGMA_FLOW, sce->val1, 0, flag | 2);

		// Circle of Fire autotriggers a splash AoE around self by chance when hit.
		if ( sc->data[SC_CIRCLE_OF_FIRE_OPTION] && rnd()%100 < 25)
			skill_castend_nodamage_id(bl,bl,EL_CIRCLE_OF_FIRE,1,0,flag|2);
	}

	// Storm Gust doubles it's damage every 3 hits against' boss monsters
	if (sc && skill_id == WZ_STORMGUST && sc->sg_counter % 3 == 0 && (status_get_mode(bl)&MD_BOSS)) {
		damage += damage;
	}

	if (tsc && tsc->count)
	{
		if (tsc->data[SC_INVINCIBLE] && !tsc->data[SC_INVINCIBLEOFF])
			damage += damage * 75 / 100;

		if ( tsd && (sce = tsc->data[SC_SOULREAPER]) && rnd()%100 < sce->val2)
		{
			clif_specialeffect(src, 1208, AREA);
			pc_addsoulball(tsd, skill_get_time2(SP_SOULREAPER, sce->val1), 5+3*pc_checkskill(tsd, SP_SOULENERGY));
		}

		// [Epoque]
		if (bl->type == BL_MOB)
		{
			int i;

			if (((sce=tsc->data[SC_MANU_ATK]) && (flag&BF_WEAPON)) || ((sce=tsc->data[SC_MANU_MATK]) && (flag&BF_MAGIC)))
				for (i=0; ARRAYLENGTH(mob_manuk) > i; i++)
					if (((TBL_MOB*)bl)->mob_id==mob_manuk[i])
					{
						damage += damage*sce->val1/100;
						break;
					}
			if (((sce=tsc->data[SC_SPL_ATK]) && (flag&BF_WEAPON)) || ((sce=tsc->data[SC_SPL_MATK]) && (flag&BF_MAGIC)))
				for (i = 0; ARRAYLENGTH(mob_splendide) > i; i++)
					if (((TBL_MOB*)bl)->mob_id==mob_splendide[i])
					{
						damage += damage*sce->val1/100;
						break;
					}
		}

		if( tsc->data[SC_POISONINGWEAPON] )
		{
			struct status_data *tstatus;
			short rate = 100;
			tstatus = status_get_status_data(bl);
			if ( !(flag&BF_SKILL) && (flag&BF_WEAPON) && damage > 0 && rnd()%100 < tsc->data[SC_POISONINGWEAPON]->val3 )
			{
				if ( tsc->data[SC_POISONINGWEAPON]->val1 == 9 )//Oblivion Curse gives a 2nd success chance after the 1st one passes which is reduceable. [Rytech]
					rate = 100 - tstatus->int_ * 4 / 5 ;
				sc_start(bl,(sc_type)tsc->data[SC_POISONINGWEAPON]->val2,rate,tsc->data[SC_POISONINGWEAPON]->val1,skill_get_time2(GC_POISONINGWEAPON,1) - (tstatus->vit + tstatus->luk) / 2 * 1000);
			}
		}
		if (tsc->data[SC__DEADLYINFECT] && flag&BF_SHORT && damage > 0 && rnd() % 100 < 30 + 10 * tsc->data[SC__DEADLYINFECT]->val1)
			status_change_spread(src, bl);
	}

	if (battle_config.pk_mode && sd && bl->type == BL_PC && damage)
  	{
		if (flag & BF_SKILL) { //Skills get a different reduction than non-skills. [Skotlex]
			if (flag&BF_WEAPON)
				damage = damage * battle_config.pk_weapon_damage_rate/100;
			if (flag&BF_MAGIC)
				damage = damage * battle_config.pk_magic_damage_rate/100;
			if (flag&BF_MISC)
				damage = damage * battle_config.pk_misc_damage_rate/100;
		} else { //Normal attacks get reductions based on range.
			if (flag & BF_SHORT)
				damage = damage * battle_config.pk_short_damage_rate/100;
			if (flag & BF_LONG)
				damage = damage * battle_config.pk_long_damage_rate/100;
		}
		if(!damage) damage  = 1;
	}

	if(battle_config.skill_min_damage && damage > 0 && damage < div_)
	{
		if ((flag&BF_WEAPON && battle_config.skill_min_damage&1)
			|| (flag&BF_MAGIC && battle_config.skill_min_damage&2)
			|| (flag&BF_MISC && battle_config.skill_min_damage&4)
		)
			damage = div_;
	}

	if( bl->type == BL_MOB && !status_isdead(bl) && src != bl) {
	  if (damage > 0 )
			mobskill_event((TBL_MOB*)bl,src,gettick(),flag);
	  if (skill_id)
			mobskill_event((TBL_MOB*)bl,src,gettick(),MSC_SKILLUSED|(skill_id<<16));
	}

	if( sd && pc_ismadogear(sd) && (element == ELE_FIRE || element == ELE_WATER) && rnd()%100 < 50 )
		pc_overheat(sd,element == ELE_FIRE ? 1 : -1);

	if (sc && sc->data[SC__SHADOWFORM])
	{
		struct block_list *s_bl = map_id2bl(sc->data[SC__SHADOWFORM]->val2);
		if (!s_bl || s_bl->m != bl->m) // If the shadow form target is not present remove the sc.
			status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
		else if (status_isdead(s_bl) || !battle_check_target(src, s_bl, BCT_ENEMY))
		{ // If the shadow form target is dead or not your enemy remove the sc in both.
			status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
			if (s_bl->type == BL_PC)
				((TBL_PC*)s_bl)->shadowform_id = 0;
		}
		else
		{
			if ((--sc->data[SC__SHADOWFORM]->val3) < 0)
			{ // If you have exceded max hits supported, remove the sc in both.
				status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
				if (s_bl->type == BL_PC)
					((TBL_PC*)s_bl)->shadowform_id = 0;
			}
			else
			{
				status_damage(src, s_bl, damage, 0, clif_damage(s_bl, s_bl, gettick(), 500, 500, damage, -1, 0, 0,false), 0);
				return ATK_NONE;
			}
		}
	}

	return damage;
}

/*==========================================
 * Calculates BG related damage adjustments.
 *------------------------------------------*/
int64 battle_calc_bg_damage(struct block_list *src, struct block_list *bl, int64 damage, int skill_id, int flag)
{
	if( !damage )
		return 0;

	if( bl->type == BL_MOB )
	{
		struct mob_data* md = BL_CAST(BL_MOB, bl);
		if( map[bl->m].flag.battleground && (md->mob_id == 1914 || md->mob_id == 1915) && flag&BF_SKILL )
			return 0; // Crystal cannot receive skill damage on battlegrounds
	}

	switch (skill_id)
	{
		case PA_PRESSURE:
		case HW_GRAVITATION:
		case NJ_ZENYNAGE:
		//case RK_DRAGONBREATH:
		//case GN_HELLS_PLANT_ATK:
		case SJ_NOVAEXPLOSING:
		case SP_SOULEXPLOSION:
		case KO_MUCHANAGE:
			break;
		default:
			if (flag&BF_SKILL)
			{ //Skills get a different reduction than non-skills. [Skotlex]
				if (flag&BF_WEAPON)
					damage = damage * battle_config.bg_weapon_damage_rate / 100;
				if (flag&BF_MAGIC)
					damage = damage * battle_config.bg_magic_damage_rate / 100;
				if (flag&BF_MISC)
					damage = damage * battle_config.bg_misc_damage_rate / 100;
			}
			else
			{ //Normal attacks get reductions based on range.
				if (flag&BF_SHORT)
					damage = damage * battle_config.bg_short_damage_rate / 100;
				if (flag&BF_LONG)
					damage = damage * battle_config.bg_long_damage_rate / 100;
			}
			
			if( !damage ) damage = 1;
	}

	return damage;
}

bool battle_can_hit_gvg_target(struct block_list *src, struct block_list *bl, uint16 skill_id, int flag)
{
	struct mob_data* md = BL_CAST(BL_MOB, bl);
	int class_ = status_get_class(bl);

	if (md && md->guardian_data) {
		if (class_ == MOBID_EMPERIUM && flag&BF_SKILL) {
			//Skill immunity.
			switch (skill_id) {
			case MO_TRIPLEATTACK:
			case HW_GRAVITATION:
				break;
			default:
				return false;
			}
		}

		if (src->type != BL_MOB) {
			struct guild *g = src->type == BL_PC ? ((TBL_PC *)src)->guild : guild_search(status_get_guild_id(src));
			if (!g) return false;
			if (class_ == MOBID_EMPERIUM && guild_checkskill(g, GD_APPROVAL) <= 0)
				return false;
			if (battle_config.guild_max_castles && guild_castle_count(g->guild_id) >= battle_config.guild_max_castles)
				return false; // [MouseJstr]
		}
	}
	return true;
}

/*==========================================
 * Calculates GVG related damage adjustments.
 *------------------------------------------*/
int64 battle_calc_gvg_damage(struct block_list *src,struct block_list *bl,int64 damage,int skill_id,int flag)
{
	struct mob_data* md = BL_CAST(BL_MOB, bl);
	int class_ = status_get_class(bl);

	if (!damage) //No reductions to make.
		return 0;
	
	if (!battle_can_hit_gvg_target(src, bl, skill_id, flag))
		return 0;

	switch (skill_id) {
	//Skills with no damage reduction.
	case PA_PRESSURE:
	case HW_GRAVITATION:
	case NJ_ZENYNAGE:
	//case RK_DRAGONBREATH:
	//case GN_HELLS_PLANT_ATK:
	case SJ_NOVAEXPLOSING:
	case SP_SOULEXPLOSION:
	case KO_MUCHANAGE:
		break;
	default:
		/* Uncomment if you want god-mode Emperiums at 100 defense. [Kisuka]
		if (md && md->guardian_data) {
			damage -= damage * (md->guardian_data->castle->defense/100) * battle_config.castle_defense_rate/100;
		}
		*/
		if (flag & BF_SKILL) { //Skills get a different reduction than non-skills. [Skotlex]
			if (flag&BF_WEAPON)
				damage = damage * battle_config.gvg_weapon_damage_rate/100;
			if (flag&BF_MAGIC)
				damage = damage * battle_config.gvg_magic_damage_rate/100;
			if (flag&BF_MISC)
				damage = damage * battle_config.gvg_misc_damage_rate/100;
		} else { //Normal attacks get reductions based on range.
			if (flag & BF_SHORT)
				damage = damage * battle_config.gvg_short_damage_rate/100;
			if (flag & BF_LONG)
				damage = damage * battle_config.gvg_long_damage_rate/100;
		}
		if(!damage) damage  = 1;
	}
	return damage;
}

/*==========================================
 * HP/SP drain calculation
 *------------------------------------------*/
static int battle_calc_drain(int64 damage, int rate, int per)
{
	int64 diff = 0;

	if (per && (rate > 1000 || rnd() % 1000 < rate)) {
		diff = (damage * per) / 100;
		if (diff == 0) {
			if (per > 0)
				diff = 1;
			else
				diff = -1;
		}
	}
	return (int)cap_value(diff, INT_MIN, INT_MAX);
}

/*==========================================
 * ?C練ダ??[ジ
 *------------------------------------------*/
int64 battle_addmastery(struct map_session_data *sd,struct block_list *target,int64 dmg,int type)
{
	int64 damage;
	struct status_data *status = status_get_status_data(target);
	int weapon, skill;
	damage = dmg;

	nullpo_ret(sd);

	if((skill = pc_checkskill(sd,AL_DEMONBANE)) > 0 &&
		target->type == BL_MOB && //This bonus doesnt work against players.
		(battle_check_undead(status->race,status->def_ele) || status->race==RC_DEMON) )
		damage += (skill*(int)(3+(sd->status.base_level+1)*0.05));	// submitted by orn
		//damage += (skill * 3);

	if((skill = pc_checkskill(sd,HT_BEASTBANE)) > 0 && (status->race==RC_BRUTE || status->race==RC_INSECT)) {
		damage += (skill * 4);
		if (sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_HUNTER)
			damage += sd->status.str;
	}

	if (sd->sc.data[SC_GN_CARTBOOST])
		damage += sd->sc.data[SC_GN_CARTBOOST]->val3;

	if ((skill = pc_checkskill(sd, RA_RANGERMAIN)) > 0 && (status->race == RC_BRUTE || status->race == RC_PLANT || status->race == RC_FISH))
		damage += skill * 5;

	if( (skill = pc_checkskill(sd,NC_MADOLICENCE)) > 0 && pc_ismadogear(sd) )
		damage += (skill * 15);

	if((skill = pc_checkskill(sd,NC_RESEARCHFE)) > 0 &&
		target->type == BL_MOB && //This bonus doesnt work against players.
		(status->def_ele == ELE_EARTH || status->def_ele == ELE_FIRE) )
		damage += (skill * 10);

	if(type == 0)
		weapon = sd->weapontype1;
	else
		weapon = sd->weapontype2;
	switch(weapon) {
		case W_DAGGER:
		case W_1HSWORD:
			if((skill = pc_checkskill(sd,SM_SWORD)) > 0)
				damage += (skill * 4);
			if((skill = pc_checkskill(sd,GN_TRAINING_SWORD)) > 0)
				damage += skill * 10;
			break;
		case W_2HSWORD:
			if((skill = pc_checkskill(sd,SM_TWOHAND)) > 0)
				damage += (skill * 4);
			break;
		case W_1HSPEAR:
		case W_2HSPEAR:
			if((skill = pc_checkskill(sd,KN_SPEARMASTERY)) > 0) {
				if(pc_isdragon(sd))
					damage += (skill * 10);
				else if(pc_isriding(sd))
					damage += (skill * 5);
				else
					damage += (skill * 4);
			}
			break;
		case W_1HAXE:
		case W_2HAXE:
			if((skill = pc_checkskill(sd,AM_AXEMASTERY)) > 0)
				damage += (skill * 3);
			if((skill = pc_checkskill(sd,NC_TRAININGAXE)) > 0)
				damage += (skill * 5);
			break;
		case W_MACE:
		case W_2HMACE:
			if((skill = pc_checkskill(sd,PR_MACEMASTERY)) > 0)
				damage += (skill * 3);
			if ((skill = pc_checkskill(sd, NC_TRAININGAXE)) > 0)
				damage += (skill * 4);
			break;
		case W_FIST:
			if((skill = pc_checkskill(sd,TK_RUN)) > 0)
				damage += (skill * 10);
			// No break, fallthrough to Knuckles
		case W_KNUCKLE:
			if((skill = pc_checkskill(sd,MO_IRONHAND)) > 0)
				damage += (skill * 3);
			break;
		case W_MUSICAL:
			if((skill = pc_checkskill(sd,BA_MUSICALLESSON)) > 0)
				damage += (skill * 3);
			break;
		case W_WHIP:
			if((skill = pc_checkskill(sd,DC_DANCINGLESSON)) > 0)
				damage += (skill * 3);
			break;
		case W_BOOK:
			if((skill = pc_checkskill(sd,SA_ADVANCEDBOOK)) > 0)
				damage += (skill * 3);
			break;
		case W_KATAR:
			if((skill = pc_checkskill(sd,AS_KATAR)) > 0)
				damage += (skill * 3);
			break;
	}

	if (sd->sc.data[SC_GN_CARTBOOST]) // cart boost adds mastery type damage
		damage += 10 * sd->sc.data[SC_GN_CARTBOOST]->val1;

	return damage;
}

/*==========================================
 * Calculates the standard damage of a normal attack assuming it hits,
 * it calculates nothing extra fancy, is needed for magnum break's WATK_ELEMENT bonus. [Skotlex]
 *------------------------------------------
 * Pass damage2 as NULL to not calc it.
 * Flag values:
 * &1: Critical hit
 * &2: Arrow attack
 * &4: Skill is Magic Crasher
 * &8: Skip target size adjustment (Extremity Fist?)
 *&16: Arrow attack but BOW, REVOLVER, RIFLE, SHOTGUN, GATLING or GRENADE type weapon not equipped (i.e. shuriken, kunai and venom knives not affected by DEX)
 */
static int battle_calc_base_damage(struct status_data *status, struct weapon_atk *wa, struct status_change *sc, unsigned short t_size, struct map_session_data *sd, int flag)
{
	unsigned short atkmin=0, atkmax=0;
	short type = 0;
	int damage = 0;

	if (!sd)
	{	//Mobs/Pets
		if(flag&4)
		{		  
			atkmin = status->matk_min;
			atkmax = status->matk_max;
		} else {
			atkmin = wa->atk;
			atkmax = wa->atk2;
		}
		if (atkmin > atkmax)
			atkmin = atkmax;
	} else {	//PCs
		atkmax = wa->atk;
		type = (wa == &status->lhw)?EQI_HAND_L:EQI_HAND_R;

		if (!(flag&1) || (flag&2))
		{	//Normal attacks
			atkmin = status->dex;
			
			if (sd->equip_index[type] >= 0 && sd->inventory_data[sd->equip_index[type]])
				atkmin = atkmin*(80 + sd->inventory_data[sd->equip_index[type]]->wlv*20)/100;

			if (atkmin > atkmax)
				atkmin = atkmax;
			
			if(flag&2 && !(flag&16))
			{	//Bows
				atkmin = atkmin*atkmax/100;
				if (atkmin > atkmax)
					atkmax = atkmin;
			}
		}
	}
	
	if (sc && sc->data[SC_MAXIMIZEPOWER])
		atkmin = atkmax;
	
	//Weapon Damage calculation
	if (!(flag&1))
		damage = (atkmax>atkmin? rnd()%(atkmax-atkmin):0)+atkmin;
	else 
		damage = atkmax;
	
	if (sd)
	{
		//rodatazone says the range is 0~arrow_atk-1 for non crit
		if (flag&2 && sd->bonus.arrow_atk)
			damage += ((flag&1)?sd->bonus.arrow_atk:rnd()%sd->bonus.arrow_atk);

		//SizeFix only for players
		if (!(sd->special_state.no_sizefix || (flag&8)))
			damage = damage*(type==EQI_HAND_L?
				sd->left_weapon.atkmods[t_size]:
				sd->right_weapon.atkmods[t_size])/100;
	}
	
	//Finally, add baseatk
	if(flag&4)
		damage += status->matk_min;
	else
		damage += status->batk;
	
	//rodatazone says that Overrefine bonuses are part of baseatk
	//Here we also apply the weapon_atk_rate bonus so it is correctly applied on left/right hands.
	if(sd) {
		if (type == EQI_HAND_L) {
			if(sd->left_weapon.overrefine)
				damage += rnd()%sd->left_weapon.overrefine+1;
			if (sd->weapon_atk_rate[sd->weapontype2])
				damage += damage*sd->weapon_atk_rate[sd->weapontype2]/100;;
		} else { //Right hand
			if(sd->right_weapon.overrefine)
				damage += rnd()%sd->right_weapon.overrefine+1;
			if (sd->weapon_atk_rate[sd->weapontype1])
				damage += damage*sd->weapon_atk_rate[sd->weapontype1]/100;;
		}
	}
	return damage;
}

/*==========================================
 * Consumes ammo for the given skill.
 *------------------------------------------*/
void battle_consume_ammo(TBL_PC*sd, int skill, int lv)
{
	int qty=1;
	if (!battle_config.arrow_decrement)
		return;
	
	if (skill)
	{
		qty = skill_get_ammo_qty(skill, lv);
		if (!qty) qty = 1;
	}

	if(sd->equip_index[EQI_AMMO]>=0) //Qty check should have been done in skill_check_condition
		pc_delitem(sd,sd->equip_index[EQI_AMMO],qty,0,1,LOG_TYPE_CONSUME);

	sd->state.arrow_atk = 0;
}

static int battle_range_type(struct block_list *src, struct block_list *target, int skill_id, int skill_lv)
{	
	// Traps are always short range.
	if (skill_get_inf2(skill_id) & INF2_TRAP)
		return BF_SHORT;

	// Skill Range Criteria
	if (battle_config.skillrange_by_distance &&(src->type&battle_config.skillrange_by_distance)) 
	{ //based on distance between src/target [Skotlex]
		if (check_distance_bl(src, target, 5))
			return BF_SHORT;

		return BF_LONG;
	}
	
	// Forces damage to ranged no matter the set skill range.
	// Ground targeted skills must be placed here since NK settings don't work with them.
	if (skill_get_nk(skill_id)&NK_FORCE_RANGED_DAMAGE ||
		//skill_id == RL_R_TRIP ||// Set to deal ranged damage in official? Is this a bug?
		skill_id == RL_FIRE_RAIN)
		return BF_LONG;

	// Based on used skill's range
	if (skill_get_range2(src, skill_id, skill_lv) < 5)
		return BF_SHORT;

	return BF_LONG;
}

static int battle_blewcount_bonus(struct map_session_data *sd, int skill_id)
{
	uint8 i;
	if (!sd->skillblown[0].id)
		return 0;
	//Apply the bonus blewcount. [Skotlex]
	for (i = 0; i < ARRAYLENGTH(sd->skillblown) && sd->skillblown[i].id; i++) {
		if (sd->skillblown[i].id == skill_id)
			return sd->skillblown[i].val;
	}
	return 0;
}

struct Damage battle_calc_magic_attack(struct block_list *src,struct block_list *target,int skill_id,int skill_lv,int mflag);
struct Damage battle_calc_misc_attack(struct block_list *src,struct block_list *target,int skill_id,int skill_lv,int mflag);

/*=======================================================
 * Should infinite defense be applied on target? (plant)
 *-------------------------------------------------------*/
bool is_infinite_defense(struct block_list *target, int flag)
{
	struct status_data *tstatus = status_get_status_data(target);

	if (target->type == BL_SKILL) {
		TBL_SKILL *su = (TBL_SKILL*)target;
		if (su && su->group && (su->group->skill_id == WM_REVERBERATION || su->group->skill_id == WM_POEMOFNETHERWORLD))
			return true;
	}
	if (tstatus->mode&MD_IGNOREMELEE && (flag&(BF_WEAPON | BF_SHORT)) == (BF_WEAPON | BF_SHORT))
		return true;
	if (tstatus->mode&MD_IGNOREMAGIC && flag&(BF_MAGIC))
		return true;
	if (tstatus->mode&MD_IGNORERANGED && (flag&(BF_WEAPON | BF_LONG)) == (BF_WEAPON | BF_LONG))
		return true;
	if (tstatus->mode&MD_IGNOREMISC && flag&(BF_MISC))
		return true;

	return (tstatus->mode&MD_PLANT);
}

/*========================
 * Is attack arrow based?
 *------------------------*/
static bool is_skill_using_arrow(struct block_list *src, int skill_id)
{
	if (src != NULL) {
		struct status_data *sstatus = status_get_status_data(src);
		struct map_session_data *sd = BL_CAST(BL_PC, src);
		return ((sd && sd->state.arrow_atk) || (!sd && ((skill_id && skill_get_ammotype(skill_id)) || sstatus->rhw.range > 3)) || (skill_id == HT_PHANTASMIC) || (skill_id == GS_GROUNDDRIFT));
	}
	else
		return false;
}

/*=========================================
 * Is attack right handed? By default yes.
 *-----------------------------------------*/
static bool is_attack_right_handed(struct block_list *src, int skill_id)
{
	if (src != NULL) {
		struct map_session_data *sd = BL_CAST(BL_PC, src);
		//Skills ALWAYS use ONLY your right-hand weapon (tested on Aegis 10.2)
		if (!skill_id && sd && sd->weapontype1 == 0 && sd->weapontype2 > 0)
			return false;
	}
	return true;
}

/*=======================================
 * Is attack left handed? By default no.
 *---------------------------------------*/
static bool is_attack_left_handed(struct block_list *src, int skill_id)
{
	if (src != NULL) {
		struct status_data *sstatus = status_get_status_data(src);
		struct map_session_data *sd = BL_CAST(BL_PC, src);
		if (!skill_id)
		{	//Skills ALWAYS use ONLY your right-hand weapon (tested on Aegis 10.2)
			if (sd && sd->weapontype1 == 0 && sd->weapontype2 > 0)
				return true;
			if (sstatus->lhw.atk)
				return true;
			if (sd && sd->status.weapon == W_KATAR)
				return true;
		}
	}
	return false;
}

/*=============================
 * Do we score a critical hit?
 *-----------------------------*/
static bool is_attack_critical(struct Damage wd, struct block_list *src, struct block_list *target, int skill_id, int skill_lv, bool first_call)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);

	if (!first_call)
		return (wd.type == 0x0a);
	if (skill_id == NPC_CRITICALSLASH || skill_id == LG_PINPOINTATTACK) //Always critical skills
		return true;

	if (!(wd.type & 0x08) && sstatus->cri &&
		(!skill_id ||
			skill_id == KN_AUTOCOUNTER ||
			skill_id == SN_SHARPSHOOTING || skill_id == MA_SHARPSHOOTING ||
			skill_id == NJ_KIRIKAGE))
	{
		short cri = sstatus->cri;

		if (sd)
		{
			cri += sd->critaddrace[tstatus->race] + sd->critaddrace[RC_ALL];
			if (is_skill_using_arrow(src, skill_id))
				cri += sd->bonus.arrow_cri;
		}

		if (sc && sc->data[SC_CAMOUFLAGE])
			cri += 100 * min(10, sc->data[SC_CAMOUFLAGE]->val3); //max 100% (1K)
		//The official equation is *2, but that only applies when sd's do critical.
		//Therefore, we use the old value 3 on cases when an sd gets attacked by a mob
		cri -= tstatus->luk*((!sd&&tsd) ? 3 : 2);

		if (tsc && tsc->data[SC_SLEEP]) {
			cri <<= 1;
		}
		switch (skill_id) {
		case 0:
			if (sc && !sc->data[SC_AUTOCOUNTER])
				break;
			clif_specialeffect(src, 131, AREA);
			status_change_end(src, SC_AUTOCOUNTER, INVALID_TIMER);
		case KN_AUTOCOUNTER:
			if (battle_config.auto_counter_type &&
				(battle_config.auto_counter_type&src->type))
				return true;
			else
				cri <<= 1;
			break;
		case SN_SHARPSHOOTING:
		case MA_SHARPSHOOTING:
			cri += 200;
			break;
		case NJ_KIRIKAGE:
			cri += 250 + 50 * skill_lv;
			break;
		}
		if (tsd && tsd->bonus.critical_def)
			cri = cri * (100 - tsd->bonus.critical_def) / 100;

		// Underflow check, critical rate should not be < 1.0
		if (cri < 10)
			cri = 10;

		return (rnd() % 1000 < cri);
	}
	return 0;
}

/*==========================================================
 * Is the attack piercing? (Investigate/Ice Pick)
 *----------------------------------------------------------*/
static int is_attack_piercing(struct Damage wd, struct block_list *src, struct block_list *target, int skill_id, int skill_lv, short weapon_position)
{
	if (skill_id == MO_INVESTIGATE || skill_id == RL_MASS_SPIRAL)
		return 2;
	if (src != NULL) {
		struct map_session_data *sd = BL_CAST(BL_PC, src);
		struct status_data *tstatus = status_get_status_data(target);

		if (skill_id != PA_SACRIFICE && skill_id != CR_GRANDCROSS && skill_id != NPC_GRANDDARKNESS
			&& skill_id != PA_SHIELDCHAIN && !is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		{ //Elemental/Racial adjustments
			if (sd && (sd->right_weapon.def_ratio_atk_ele & (1 << tstatus->def_ele) || sd->right_weapon.def_ratio_atk_ele & (1 << ELE_ALL) ||
				sd->right_weapon.def_ratio_atk_race & (1 << tstatus->race) || sd->right_weapon.def_ratio_atk_race & (1 << RC_ALL) ||
				sd->right_weapon.def_ratio_atk_class & (1 << tstatus->class_) || sd->right_weapon.def_ratio_atk_class & (1 << CLASS_ALL))
				)
				if (weapon_position == EQI_HAND_R)
					return 1;

			if (sd && (sd->left_weapon.def_ratio_atk_ele & (1 << tstatus->def_ele) || sd->left_weapon.def_ratio_atk_ele & (1 << ELE_ALL) ||
				sd->left_weapon.def_ratio_atk_race & (1 << tstatus->race) || sd->left_weapon.def_ratio_atk_race & (1 << RC_ALL) ||
				sd->left_weapon.def_ratio_atk_class & (1 << tstatus->class_) || sd->left_weapon.def_ratio_atk_class & (1 << CLASS_ALL))
			) { //Pass effect onto right hand if configured so. [Skotlex]
				if (battle_config.left_cardfix_to_right && is_attack_right_handed(src, skill_id)) {
					if (weapon_position == EQI_HAND_R)
						return 1;
				}
				else if (weapon_position == EQI_HAND_L)
					return 1;
			}
		}
	}
	return 0;
}

static bool battle_skill_get_damage_properties(uint16 skill_id, int is_splash)
{
	int nk = skill_get_nk(skill_id);
	if (!skill_id && is_splash) //If flag, this is splash damage from Baphomet Card and it always hits.
		nk |= NK_NO_CARDFIX_ATK | NK_IGNORE_FLEE;
	return nk;
}

/*=============================
 * Checks if attack is hitting
 *-----------------------------*/
static bool is_attack_hitting(struct Damage wd, struct block_list *src, struct block_list *target, int skill_id, int skill_lv, bool first_call)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int nk;
	short flee, hitrate;

	if (!first_call)
		return (wd.dmg_lv != ATK_FLEE);

	nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	if (is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		return true;
	else if (sd && sd->bonus.perfect_hit > 0 && rnd() % 100 < sd->bonus.perfect_hit)
		return true;
	else if (sc && sc->data[SC_FUSION])
		return true;
	else if (skill_id == AS_SPLASHER && !wd.miscflag)
		return true;
	else if (skill_id == CR_SHIELDBOOMERANG && sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_CRUSADER)
		return true;
	else if (tsc && tsc->opt1 && tsc->opt1 != OPT1_STONEWAIT && tsc->opt1 != OPT1_BURNING)
		return true;
	else if (nk&NK_IGNORE_FLEE)
		return true;

	if (sc && (sc->data[SC_NEUTRALBARRIER] || sc->data[SC_NEUTRALBARRIER_MASTER]) && wd.flag&BF_LONG)
		return false;

	flee = tstatus->flee;
	hitrate = 80; //Default hitrate

	if (battle_config.agi_penalty_type && battle_config.agi_penalty_target&target->type) {
		unsigned char attacker_count = unit_counttargeted(target);
		if (attacker_count >= battle_config.agi_penalty_count) {
			if (battle_config.agi_penalty_type == 1)
				flee = (flee * (100 - (attacker_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num)) / 100;
			else //asume type 2: absolute reduction
				flee -= (attacker_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
			if (flee < 1) flee = 1;
		}
	}

	hitrate += sstatus->hit - flee;

	//Fogwall's hit penalty is only for normal ranged attacks.
	if ((wd.flag&(BF_LONG | BF_MAGIC)) == BF_LONG && !skill_id && tsc && tsc->data[SC_FOGWALL])
		hitrate -= 50;

	if (sd && is_skill_using_arrow(src, skill_id))
		hitrate += sd->bonus.arrow_hit;

	if (skill_id)
		switch (skill_id)
		{	//Hit skill modifiers
			//It is proven that bonus is applied on final hitrate, not hit.
		case SM_BASH:
		case MS_BASH:
			hitrate += hitrate * 5 * skill_lv / 100;
			break;
		case MS_MAGNUM:
		case SM_MAGNUM:
			hitrate += hitrate * 10 * skill_lv / 100;
			break;
		case KN_AUTOCOUNTER:
		case PA_SHIELDCHAIN:
		case NPC_WATERATTACK:
		case NPC_GROUNDATTACK:
		case NPC_FIREATTACK:
		case NPC_WINDATTACK:
		case NPC_POISONATTACK:
		case NPC_HOLYATTACK:
		case NPC_DARKNESSATTACK:
		case NPC_UNDEADATTACK:
		case NPC_TELEKINESISATTACK:
		case NPC_BLEEDING:
			hitrate += hitrate * 20 / 100;
			break;
		case KN_PIERCE:
		case ML_PIERCE:
			hitrate += hitrate * 5 * skill_lv / 100;
			break;
		case AS_SONICBLOW:
			if (sd && pc_checkskill(sd, AS_SONICACCEL) > 0)
				hitrate += hitrate * 50 / 100;
			break;
		case MC_CARTREVOLUTION:
		case GN_CART_TORNADO:
		case GN_CARTCANNON:
			if (sd && pc_checkskill(sd, GN_REMODELING_CART))
				hitrate += pc_checkskill(sd, GN_REMODELING_CART) * 4;
			break;
		case GC_VENOMPRESSURE:
			hitrate += 10 + 4 * skill_lv;
			break;

		case RL_SLUGSHOT:
		{
			int8 dist = distance_bl(src, target);
			if (dist > 3) {
				// Reduce n hitrate for each cell after initial 3 cells. Different each level
				// -10:-9:-8:-7:-6
				dist -= 3;
				hitrate -= ((11 - skill_lv) * dist);
			}
		}
			break;
		}
	else if (sd && wd.type & 0x08 && wd.div_ == 2) // +1 hit per level of Double Attack on a successful double attack (making sure other multi attack skills do not trigger this)
		hitrate += pc_checkskill(sd, TF_DOUBLE);

	if (sd) {
		int skill = 0;
		// Weaponry Research hidden bonus
		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)
			hitrate += hitrate * (2 * skill) / 100;

		if ((sd->status.weapon == W_1HSWORD || sd->status.weapon == W_DAGGER) &&
			(skill = pc_checkskill(sd, GN_TRAINING_SWORD)) > 0)
			hitrate += 3 * skill;
	}

	if (sc) {
		if (sc->data[SC_MTF_ASPD])
			hitrate += sc->data[SC_MTF_ASPD]->val2;
		if (sc->data[SC_MTF_ASPD2])
			hitrate += sc->data[SC_MTF_ASPD2]->val2;
	}

	hitrate = cap_value(hitrate, battle_config.min_hitrate, battle_config.max_hitrate);
	return (rnd() % 100 < hitrate);
}

/*==========================================
 * If attack ignores def..
 *------------------------------------------*/
static bool attack_ignores_def(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, short weapon_position)
{
	struct status_data *tstatus = status_get_status_data(target);
	struct status_change *sc = status_get_sc(src);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	if (is_attack_critical(wd, src, target, skill_id, skill_lv, false))
		return true;
	else if (sc && sc->data[SC_FUSION])
			return true;
	else if (skill_id != CR_GRANDCROSS && skill_id != NPC_GRANDDARKNESS)
	{	//Ignore Defense?
		if (sd && (sd->right_weapon.ignore_def_ele & (1 << tstatus->def_ele) || sd->right_weapon.ignore_def_ele & (1 << ELE_ALL) ||
			sd->right_weapon.ignore_def_race & (1 << tstatus->race) || sd->right_weapon.ignore_def_race & (1 << RC_ALL) ||
			sd->right_weapon.ignore_def_class & (1 << tstatus->class_) || sd->right_weapon.ignore_def_class & (1 << CLASS_ALL)))
			if (weapon_position == EQI_HAND_R)
				return true;
		if (sd && (sd->left_weapon.ignore_def_ele & (1 << tstatus->def_ele) || sd->left_weapon.ignore_def_ele & (1 << ELE_ALL) ||
			sd->left_weapon.ignore_def_race & (1 << tstatus->race) || sd->left_weapon.ignore_def_race & (1 << RC_ALL) ||
			sd->left_weapon.ignore_def_class & (1 << tstatus->class_) || sd->left_weapon.ignore_def_class & (1 << CLASS_ALL)))
		{
			if (battle_config.left_cardfix_to_right && is_attack_right_handed(src, skill_id)) {//Move effect to right hand. [Skotlex]
				if (weapon_position == EQI_HAND_R)
					return true;
			}
			else if (weapon_position == EQI_HAND_L)
				return true;
		}
	}

	return (nk&NK_IGNORE_DEF);
}

/*================================================
 * Should skill attack consider VVS and masteries?
 *------------------------------------------------*/
static bool battle_skill_stacks_masteries_vvs(uint16 skill_id)
{
	if (
		skill_id == PA_SHIELDCHAIN || skill_id == CR_SHIELDBOOMERANG ||
		skill_id == LG_SHIELDPRESS || skill_id == LG_EARTHDRIVE || 
		skill_id == RK_DRAGONBREATH_WATER)
		return false;
	return true;
}

/*========================================
 * Returns the element type of attack
 *----------------------------------------*/
static int battle_get_weapon_element(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, short weapon_position, int wflag)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);
	int element = skill_get_ele(skill_id, skill_lv);

	if (!skill_id || element == -1)
	{ //Take weapon's element
		if (weapon_position == EQI_HAND_R)
			element = sstatus->rhw.ele;
		else
			element = sstatus->lhw.ele;
		if (is_skill_using_arrow(src, skill_id) && sd && sd->bonus.arrow_ele && weapon_position == EQI_HAND_R)
			element = sd->bonus.arrow_ele;
		// endows override all other elements
		if (sd) { //Summoning 10 talisman will endow your weapon.
			if (sd->charmball >= 10 && sd->charmball_type < 5)
				element = sd->charmball_type;
			if (sc) { // check for endows
				if (sc->data[SC_ENCHANTARMS])
					element = sc->data[SC_ENCHANTARMS]->val2;
			}
		}
	}
	else if (element == -2) //Use enchantment's element
		element = status_get_attack_sc_element(src, sc);
	else if (element == -3) //Use random element
		element = rnd() % ELE_ALL;

	switch (skill_id)
	{
	case GS_GROUNDDRIFT:
		element = wd.miscflag; //element comes in flag.
		break;
	case LK_SPIRALPIERCE:
		if (!sd)
			element = ELE_NEUTRAL; //forced neutral for monsters
		break;
	case RL_H_MINE:
	case SJ_PROMINENCEKICK:
		if (wflag & 8)// Explosion damage for howling mine deals fire damage according to description.
			element = ELE_FIRE;
		break;
	case KO_KAIHOU:
		if (sd)
			element = sd->charmball_type;
		break;
	}

	if (!(nk & NK_NO_ELEFIX) && element != ELE_NONE)
		if (src->type == BL_HOM)
			element = ELE_NONE; //skill is "not elemental"

	if (sc && sc->data[SC_GOLDENE_FERSE] && ((!skill_id && (rnd() % 100 < sc->data[SC_GOLDENE_FERSE]->val4)) || skill_id == MH_STAHL_HORN)) {
		element = ELE_HOLY;
	}

	return element;
}

/*========================================
 * Do element damage modifier calculation
 *----------------------------------------*/
static struct Damage battle_calc_element_damage(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int flag)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int element = skill_get_ele(skill_id, skill_lv);
	int left_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_L, flag);
	int right_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_R, flag);

	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	//Elemental attribute fix
	if (!(nk&NK_NO_ELEFIX)) {
		//Non-pc physical melee attacks (mob, pet, homun) are "non elemental", they deal 100% to all target elements
		//However the "non elemental" attacks still get reduced by "Neutral resistance"
		//Also non-pc units have only a defending element, but can inflict elemental attacks using skills [exneval]
		if (battle_config.attack_attr_none&src->type)
			if (((!skill_id && !right_element) || (skill_id && (element == -1 || !right_element))) &&
				(wd.flag&(BF_SHORT | BF_WEAPON)) == (BF_SHORT | BF_WEAPON))
				return wd;
		if (wd.damage > 0) {
			//Forced to its element
			wd.damage = battle_attr_fix(src, target, wd.damage, right_element, tstatus->def_ele, tstatus->ele_lv);

			switch (skill_id) {
				case MC_CARTREVOLUTION:
				case SR_GATEOFHELL:
				case KO_BAKURETSU:
					//Forced to neutral element
					wd.damage = battle_attr_fix(src, target, wd.damage, ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
					break;
				case GN_CARTCANNON:
				case KO_HAPPOKUNAI:
					//Forced to ammo's element
					wd.damage = battle_attr_fix(src, target, wd.damage, (sd && sd->bonus.arrow_ele) ? sd->bonus.arrow_ele : ELE_NEUTRAL, tstatus->def_ele, tstatus->ele_lv);
					break;
			}

		}
		if (is_attack_left_handed(src, skill_id) && wd.damage2 > 0)
			wd.damage2 = battle_attr_fix(src, target, wd.damage2, left_element, tstatus->def_ele, tstatus->ele_lv);
		if (sc && sc->data[SC_WATK_ELEMENT] && (wd.damage || wd.damage2))
		{ // Descriptions indicate this means adding a percent of a normal attack in another element. [Skotlex]
			int damage = battle_calc_base_damage(sstatus, &sstatus->rhw, sc, tstatus->size, sd, (is_skill_using_arrow(src, skill_id) ? 2 : 0)) * sc->data[SC_WATK_ELEMENT]->val2 / 100;
			wd.damage += battle_attr_fix(src, target, damage, sc->data[SC_WATK_ELEMENT]->val1, tstatus->def_ele, tstatus->ele_lv);

			if (is_attack_left_handed(src, skill_id))
			{
				damage = battle_calc_base_damage(sstatus, &sstatus->lhw, sc, tstatus->size, sd, (is_skill_using_arrow(src, skill_id) ? 2 : 0)) * sc->data[SC_WATK_ELEMENT]->val2 / 100;
				wd.damage2 += battle_attr_fix(src, target, damage, sc->data[SC_WATK_ELEMENT]->val1, tstatus->def_ele, tstatus->ele_lv);
			}
		}
	}
	return wd;
}

#define ATK_RATE(damage, damage2, a ) {damage= damage*(a)/100 ; if(is_attack_left_handed(src, skill_id)) damage2= damage2*(a)/100; }
#define ATK_RATE2(damage, damage2, a , b ) { damage= damage*(a)/100 ; if(is_attack_left_handed(src, skill_id)) damage2= damage2*(b)/100; }
#define ATK_RATER(damage, a){ damage = (int64)damage*(a)/100;}
#define ATK_RATEL(damage2, a){ damage2 = (int64)damage2*(a)/100;}
//Adds dmg%. 100 = +100% (double) damage. 10 = +10% damage
#define ATK_ADDRATE(damage, damage2, a ) { damage+= damage*(a)/100 ; if(is_attack_left_handed(src, skill_id)) damage2+= damage2*(a)/100; }
#define ATK_ADDRATE2(damage, damage2, a , b ) { damage+= damage*(a)/100 ; if(is_attack_left_handed(src, skill_id)) damage2+= damage2*(b)/100; }
//Adds an absolute value to damage. 100 = +100 damage
#define ATK_ADD(damage, damage2, a ) { damage+= a; if (is_attack_left_handed(src, skill_id)) damage2+= a; }
#define ATK_ADD2(damage, damage2, a , b ) { damage+= a; if (is_attack_left_handed(src, skill_id)) damage2+= b; }

static struct Damage battle_calc_attack_masteries(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);

	int t_class = status_get_class(target);

	if (sd && battle_skill_stacks_masteries_vvs(skill_id) &&
		skill_id != MO_INVESTIGATE &&
		skill_id != MO_EXTREMITYFIST &&
		skill_id != CR_GRANDCROSS)
	{	//Add mastery damage
		int skill;
		uint8 i;

		wd.damage = battle_addmastery(sd, target, wd.damage, 0);
		if (is_attack_left_handed(src, skill_id)) {
			wd.damage2 = battle_addmastery(sd, target, wd.damage2, 1);
		}

		if (sc) {
			if (sc->data[SC_MIRACLE])
				i = 2; //Star anger
			else
				ARR_FIND(0, MAX_PC_FEELHATE, i, t_class == sd->hate_mob[i]);

			if (i < MAX_PC_FEELHATE && (skill = pc_checkskill(sd, sg_info[i].anger_id)))
			{
				int skillratio = sd->status.base_level + sstatus->dex + sstatus->luk;
				if (i == 2) 
					skillratio += sstatus->str; //Star Anger
				if (skill < 4)
					skillratio /= 12 - 3 * skill;
				ATK_ADDRATE(wd.damage, wd.damage2, skillratio);
			}

			if (sc->data[SC_CAMOUFLAGE]) {
				ATK_ADD(wd.damage, wd.damage2, 30 * min(10, sc->data[SC_CAMOUFLAGE]->val3)); //max +300atk
			}
		}

		if (skill_id == NJ_SYURIKEN && (skill = pc_checkskill(sd, NJ_TOBIDOUGU)) > 0) {
			ATK_ADD(wd.damage, wd.damage2, 3 * skill);
		}
	}
	return wd;
}

/*==========================================================
 * Calculate basic ATK that goes into the skill ATK formula
 *----------------------------------------------------------*/
struct Damage battle_calc_skill_base_damage(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	uint16 i;

	int nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

	switch (skill_id)
	{	//Calc base damage according to skill
	case PA_SACRIFICE:
		wd.damage = (int64)sstatus->max_hp * 9 / 100;
		wd.damage2 = 0;
		break;
	case NJ_ISSEN:
		wd.damage = 40 * sstatus->str + skill_lv * (sstatus->hp / 10 + 35);
		wd.damage2 = 0;
		break;
	case LK_SPIRALPIERCE:
	case ML_SPIRALPIERCE:
		if (sd) {
			short index = sd->equip_index[EQI_HAND_R];
			if (index >= 0 &&
				sd->inventory_data[index] &&
				sd->inventory_data[index]->type == IT_WEAPON)
				wd.damage = sd->inventory_data[index]->weight * 8 / 100; //80% of weight

			ATK_ADDRATE(wd.damage, wd.damage2, 50 * skill_lv); //Skill modifier applies to weight only.
		}
		else {
			wd.damage = battle_calc_base_damage(sstatus, &sstatus->rhw, sc, tstatus->size, sd, 0); //Monsters have no weight and use ATK instead
		}

		i = sstatus->str / 10;
		i *= i;
		ATK_ADD(wd.damage, wd.damage2, i); //Add str bonus.
		switch (tstatus->size) { //Size-fix. Is this modified by weapon perfection?
		case SZ_SMALL: //Small: 125%
			ATK_RATE(wd.damage, wd.damage2, 125);
			break;
			//case SZ_MEDIUM: //Medium: 100%
		case SZ_BIG: //Large: 75%
			ATK_RATE(wd.damage, wd.damage2, 75);
			break;
		}
		break;
	case CR_SHIELDBOOMERANG:
	case PA_SHIELDCHAIN:
	case LG_SHIELDPRESS:
	case LG_EARTHDRIVE:
		wd.damage = sstatus->batk;
		if (sd) {
			short index = sd->equip_index[EQI_HAND_L];
			if (index >= 0 &&
				sd->inventory_data[index] &&
				sd->inventory_data[index]->type == IT_ARMOR)
				ATK_ADD(wd.damage, wd.damage2, sd->inventory_data[index]->weight / 10);
		}
		else
			break;
	
	case RK_DRAGONBREATH:
	case RK_DRAGONBREATH_WATER:
	{
		int damagevalue = (sstatus->hp / 50 + status_get_max_sp(src) / 4) * skill_lv;
		if (status_get_lv(src) > 100)
			damagevalue = damagevalue * status_get_lv(src) / 100;
		if (sd) {
			damagevalue = damagevalue * (90 + 10 * pc_checkskill(sd, RK_DRAGONTRAINING)) / 100;
		}
		ATK_ADD(wd.damage, wd.damage2, damagevalue);
		wd.flag |= BF_LONG;
	}
	break;
	case NC_SELFDESTRUCTION: {
		int damagevalue = (skill_lv + 1) * ((sd ? pc_checkskill(sd, NC_MAINFRAME) : 0) + 8) * (status_get_sp(src) + sstatus->vit);

		if (status_get_lv(src) > 100)
			damagevalue = damagevalue * status_get_lv(src) / 100;
		damagevalue = damagevalue + sstatus->hp;
		ATK_ADD(wd.damage, wd.damage2, damagevalue);
	}
							 break;
	case KO_HAPPOKUNAI:
		if (sd) {
			short index = sd->equip_index[EQI_AMMO];
			int damagevalue = 3 * (sstatus->batk + sstatus->rhw.atk + (index >= 0 && sd->inventory_data[index] ? sd->inventory_data[index]->atk : 0)) * (skill_lv + 5) / 5;
			if (sc && sc->data[SC_KAGEMUSYA])
				damagevalue += damagevalue * sc->data[SC_KAGEMUSYA]->val2 / 100;
			ATK_ADD(wd.damage, wd.damage2, damagevalue);
		}
		else
			ATK_ADD(wd.damage, wd.damage2, 5000);
		break;

	case HFLI_SBR44:	//[orn]
		if (src->type == BL_HOM) {
			wd.damage = ((TBL_HOM*)src)->homunculus.intimacy;
		}
		break;

	default:
	{
		i = (is_attack_critical(wd, src, target, skill_id, skill_lv, false) ? 1 : 0) |
			(is_skill_using_arrow(src, skill_id) ? 2 : 0) |
			((!battle_config.magiccrasher_renewal && skill_id == HW_MAGICCRASHER) ? 4 : 0) |
			(!skill_id && sc && sc->data[SC_CHANGE] ? 4 : 0) |
			(skill_id == MO_EXTREMITYFIST ? 8 : 0) |
			(sc && sc->data[SC_WEAPONPERFECTION] ? 8 : 0);
		if (is_skill_using_arrow(src, skill_id) && sd)
			switch (sd->status.weapon) {
			case W_BOW:
			case W_REVOLVER:
			case W_GATLING:
			case W_SHOTGUN:
			case W_GRENADE:
				break;
			default:
				i |= 16; // for ex. shuriken must not be influenced by DEX
			}
		wd.damage = battle_calc_base_damage(sstatus, &sstatus->rhw, sc, tstatus->size, sd, i);
		if (is_attack_left_handed(src, skill_id))
			wd.damage2 = battle_calc_base_damage(sstatus, &sstatus->lhw, sc, tstatus->size, sd, i);

		if (nk&NK_SPLASHSPLIT) { // Divide ATK among targets
			if (wd.miscflag > 0) {
				wd.damage /= wd.miscflag;
			}
			else
				ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
		}

		//Add any bonuses that modify the base atk (pre-skills)
		if (sd) {
			int skill;
			if (sd->bonus.atk_rate) {
				ATK_ADDRATE(wd.damage, wd.damage2, sd->bonus.atk_rate);
			}

			if (is_attack_critical(wd, src, target, skill_id, skill_lv, false) && sd->bonus.crit_atk_rate) {
				ATK_ADDRATE(wd.damage, wd.damage2, sd->bonus.crit_atk_rate);
			}

			if (sd->status.party_id && (skill = pc_checkskill(sd, TK_POWER)) > 0) {
				if ((i = party_foreachsamemap(party_sub_count, sd, 0)) > 1) { // exclude the player himself [Inkfish]
					ATK_ADDRATE(wd.damage, wd.damage2, 2 * skill*i);
				}
			}
		}
		break;
	}	//End default case
	} //End switch(skill_id)
return wd;
}

//For quick div adjustment.
#define DAMAGE_DIV_FIX(dmg, div) { if (div < 0) { (div)*=-1; (dmg)/=div; } (dmg)*=div; }
#define DAMAGE_DIV_FIX2(dmg, div) { if (div > 1) (dmg)*=div; }

/*=================================================
 * Applies DAMAGE_DIV_FIX and checks for min damage
 *-------------------------------------------------
 * Credits:
 *	Original coder Playtester
 */
static struct Damage battle_apply_div_fix(struct Damage d)
{
	if (d.damage) {
		DAMAGE_DIV_FIX(d.damage, d.div_);
		//Min damage
		if ((battle_config.skill_min_damage&d.flag) && d.damage < d.div_)
			d.damage = d.div_;
	}
	else if (d.div_ < 0) {
		d.div_ *= -1;
	}

	return d;
}
/*=======================================
 * Check for and calculate multi attacks
 *---------------------------------------*/
static struct Damage battle_calc_multi_attack(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *tstatus = status_get_status_data(target);

	if (sd && !skill_id) {	//If no skill_id passed, check for double attack.
		short i;
		if (((skill_lv = pc_checkskill(sd, TF_DOUBLE)) > 0 && sd->weapontype1 == W_DAGGER)
			|| (sd->bonus.double_rate > 0 && sd->weapontype1 != W_FIST) //Will fail bare-handed
			|| (sc && sc->data[SC_KAGEMUSYA] && sd->weapontype1 != W_FIST)) // Need confirmation
		{	//Success chance is not added, the higher one is used [Skotlex]
			if (rnd() % 100 < (5 * skill_lv > sd->bonus.double_rate ? 5 * skill_lv : sc && sc->data[SC_KAGEMUSYA] ? sc->data[SC_KAGEMUSYA]->val1 * 3 : sd->bonus.double_rate))
			{
				wd.div_ = skill_get_num(TF_DOUBLE, skill_lv ? skill_lv : 1);
				wd.type = 0x08;
			}
		}
		else if (((sd->weapontype1 == W_REVOLVER && (skill_lv = pc_checkskill(sd, GS_CHAINACTION)) > 0) //Normal Chain Action effect
			|| (sd && sc->count && sc->data[SC_E_CHAIN] && (skill_lv = sc->data[SC_E_CHAIN]->val2) > 0)) //Chain Action of ETERNAL_CHAIN
			&& rnd() % 100 < 5 * skill_lv) //Sucess rate
		{
			wd.div_ = skill_get_num(GS_CHAINACTION, skill_lv);
			wd.type = 0x08;
			sc_start(src, SC_E_QD_SHOT_READY, 100, target->id, skill_get_time(RL_QD_SHOT, 1));
		}
		else if (sc && sc->data[SC_FEARBREEZE] && sd->weapontype1 == W_BOW
			&& (i = sd->equip_index[EQI_AMMO]) >= 0 && sd->inventory_data[i] && sd->inventory.u.items_inventory[i].amount > 1) {
			int chance = rnd() % 100;
			wd.type = 0x08;
			switch (sc->data[SC_FEARBREEZE]->val1) {
			case 5: if (chance < 4) { wd.div_ = 5; break; } // 3 % chance to attack 5 times.
			case 4: if (chance < 7) { wd.div_ = 4; break; } // 6 % chance to attack 4 times.
			case 3: if (chance < 10) { wd.div_ = 3; break; } // 9 % chance to attack 3 times.
			case 2:
			case 1: if (chance < 13) { wd.div_ = 2; break; } // 12 % chance to attack 2 times.
			}
			wd.div_ = min(wd.div_, sd->inventory.u.items_inventory[i].amount);
			sc->data[SC_FEARBREEZE]->val4 = wd.div_ - 1;
		}
	}

	switch (skill_id) {
	case RA_AIMEDBOLT:
		if (tsc && (tsc->data[SC_WUGBITE] || tsc->data[SC_ANKLE] || tsc->data[SC_ELECTRICSHOCKER]))
			wd.div_ = tstatus->size + 2 + ((rnd() % 100 < 50 - tstatus->size * 10) ? 1 : 0);
		break;
	case SC_KO_JYUMONJIKIRI:
		if (tsc && tsc->data[SC_KO_JYUMONJIKIRI])
			wd.div_ = wd.div_ * -1;// needs more info
		break;
	}

	return wd;
}

/*==================================================================================================
 * Constant skill damage additions are added before SC modifiers and after skill base ATK calculation
 *--------------------------------------------------------------------------------------------------*/
static int battle_calc_attack_skill_ratio(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int skillratio = 100;
	int i;

	bool level_effect_bonus = battle_config.renewal_level_effect_skills;// Base/Job level effect on formula's.
	bool rebel_level_effect = battle_config.rebel_base_lv_skill_effect;// Base level effect on Rebellion skills.

	//Skill damage modifiers that stack linearly
	if (sc && skill_id != PA_SACRIFICE)
	{
		if (sc->data[SC_OVERTHRUST])
			skillratio += sc->data[SC_OVERTHRUST]->val3;
		if (sc->data[SC_MAXOVERTHRUST])
			skillratio += sc->data[SC_MAXOVERTHRUST]->val2;
		if (sc->data[SC_BERSERK] || sc->data[SC__BLOODYLUST])
			skillratio += 100;
		if (sc->data[SC_CRUSHSTRIKE] && (!skill_id || skill_id == KN_AUTOCOUNTER)) {
			if (sd) { //ATK [{Weapon Level * (Weapon Upgrade Level + 6) * 100} + (Weapon ATK) + (Weapon Weight)]%
				short index = sd->equip_index[EQI_HAND_R];
				if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
					skillratio += -100 + sd->inventory_data[index]->weight / 10 + sstatus->rhw.atk +
					100 * sd->inventory_data[index]->wlv * (sd->inventory.u.items_inventory[index].refine + 6);
			}
			status_change_end(src, SC_CRUSHSTRIKE, INVALID_TIMER);
			skill_break_equip(src, EQP_WEAPON, 2000, BCT_SELF);
		}
		if (sc->data[SC_EXEEDBREAK] && !skill_id) {
			skillratio += -100 + sc->data[SC_EXEEDBREAK]->val2;
			status_change_end(src, SC_EXEEDBREAK, INVALID_TIMER);
		}
		if (sc->data[SC_HEAT_BARREL])
			skillratio += 200;
		if (sc->data[SC_P_ALTER])
			skillratio += sc->data[SC_P_ALTER]->val2;
	}

	switch (skill_id)
	{
	case SM_BASH:
	case MS_BASH:
		skillratio += 30 * skill_lv;
		break;
	case SM_MAGNUM:
	case MS_MAGNUM:
		if (wd.miscflag == 1)
			skillratio += 20 * skill_lv; //Inner 3x3 circle takes 100%+20%*level damage [Playtester]
		else
			skillratio += 10 * skill_lv; //Outer 5x5 circle takes 100%+10%*level damage [Playtester]
		break;
	case MC_MAMMONITE:
		skillratio += 50 * skill_lv;
		break;
	case HT_POWER:
		skillratio += -50 + 8 * sstatus->str;
		break;
	case AC_DOUBLE:
	case MA_DOUBLE:
		skillratio += 10 * (skill_lv - 1);
		break;
	case AC_SHOWER:
	case MA_SHOWER:
		skillratio += -25 + 5 * skill_lv;
		break;
	case AC_CHARGEARROW:
	case MA_CHARGEARROW:
		skillratio += 50;
		break;
	case HT_FREEZINGTRAP:
	case MA_FREEZINGTRAP:
		skillratio += -50 + 10 * skill_lv;
		break;
	case KN_PIERCE:
	case ML_PIERCE:
		skillratio += 10 * skill_lv;
		break;
	case MER_CRASH:
		skillratio += 10 * skill_lv;
		break;
	case KN_SPEARSTAB:
		skillratio += 15 * skill_lv;
		break;
	case KN_SPEARBOOMERANG:
		skillratio += 50 * skill_lv;
		break;
	case KN_BRANDISHSPEAR:
	case ML_BRANDISH:
	{
		int ratio = 100 + 20 * skill_lv;
		skillratio += ratio - 100;
		if (skill_lv > 3 && wd.miscflag == 1) skillratio += ratio / 2;
		if (skill_lv > 6 && wd.miscflag == 1) skillratio += ratio / 4;
		if (skill_lv > 9 && wd.miscflag == 1) skillratio += ratio / 8;
		if (skill_lv > 6 && wd.miscflag == 2) skillratio += ratio / 2;
		if (skill_lv > 9 && wd.miscflag == 2) skillratio += ratio / 4;
		if (skill_lv > 9 && wd.miscflag == 3) skillratio += ratio / 2;
		break;
	}
	case KN_BOWLINGBASH:
	case MS_BOWLINGBASH:
		skillratio += 40 * skill_lv;
		break;
	case AS_GRIMTOOTH:
		skillratio += 20 * skill_lv;
		break;
	case AS_POISONREACT:
		skillratio += 30 * skill_lv;
		break;
	case AS_SONICBLOW:
		skillratio += 300 + 40 * skill_lv;
		break;
	case TF_SPRINKLESAND:
		skillratio += 30;
		break;
	case MC_CARTREVOLUTION:
		skillratio += 50;
		if (sd && sd->cart_weight)
			skillratio += 100 * sd->cart_weight / sd->cart_weight_max; // +1% every 1% weight
		else if (!sd)
			skillratio += 100; //Max damage for non players.
		break;
	case NPC_PIERCINGATT:
		skillratio += -25; //75% base damage
		break;
	case NPC_COMBOATTACK:
		skillratio += 25 * skill_lv;
		break;
	case NPC_RANDOMATTACK:
	case NPC_WATERATTACK:
	case NPC_GROUNDATTACK:
	case NPC_FIREATTACK:
	case NPC_WINDATTACK:
	case NPC_POISONATTACK:
	case NPC_HOLYATTACK:
	case NPC_DARKNESSATTACK:
	case NPC_UNDEADATTACK:
	case NPC_TELEKINESISATTACK:
	case NPC_BLOODDRAIN:
	case NPC_ACIDBREATH:
	case NPC_DARKNESSBREATH:
	case NPC_FIREBREATH:
	case NPC_ICEBREATH:
	case NPC_THUNDERBREATH:
	case NPC_HELLJUDGEMENT:
	case NPC_PULSESTRIKE:
		skillratio += 100 * (skill_lv - 1);
		break;
	case RG_BACKSTAP:
		if (sd && sd->status.weapon == W_BOW && battle_config.backstab_bow_penalty)
			skillratio += (200 + 40 * skill_lv) / 2;
		else
			skillratio += 200 + 40 * skill_lv;
		break;
	case RG_RAID:
		skillratio += 40 * skill_lv;
		break;
	case RG_INTIMIDATE:
		skillratio += 30 * skill_lv;
		break;
	case CR_SHIELDCHARGE:
		skillratio += 20 * skill_lv;
		break;
	case CR_SHIELDBOOMERANG:
		skillratio += 30 * skill_lv;
		break;
	case NPC_DARKCROSS:
	case CR_HOLYCROSS:
	{
		skillratio += 35 * skill_lv;
		break;
	}
	case AM_DEMONSTRATION:
		skillratio += 20 * skill_lv;
		break;
	case AM_ACIDTERROR:
		skillratio += 40 * skill_lv;
		break;
	case MO_FINGEROFFENSIVE:
		skillratio += 50 * skill_lv;
		break;
	case MO_INVESTIGATE:
		skillratio += 75 * skill_lv;
		break;
	case MO_EXTREMITYFIST:
		skillratio += 100 * (7 + sstatus->sp / 10);
		skillratio = min(500000, skillratio); //We stop at roughly 50k SP for overflow protection
		break;
	case MO_TRIPLEATTACK:
		skillratio += 20 * skill_lv;
		break;
	case MO_CHAINCOMBO:
		skillratio += 50 + 50 * skill_lv;
		break;
	case MO_COMBOFINISH:
		skillratio += 140 + 60 * skill_lv;
		break;
	case BA_MUSICALSTRIKE:
	case DC_THROWARROW:
		skillratio += 25 + 25 * skill_lv;
		break;
	case CH_TIGERFIST:
		skillratio += 100 * skill_lv - 60;
		break;
	case CH_CHAINCRUSH:
		skillratio += 300 + 100 * skill_lv;
		break;
	case CH_PALMSTRIKE:
		skillratio += 100 + 100 * skill_lv;
		break;
	case LK_HEADCRUSH:
		skillratio += 40 * skill_lv;
		break;
	case LK_JOINTBEAT:
		i = 10 * skill_lv - 50;
		// Although not clear, it's being assumed that the 2x damage is only for the break neck ailment.
		if (wd.miscflag&BREAK_NECK) i *= 2;
		skillratio += i;
		break;
	case LK_SPIRALPIERCE:
	case ML_SPIRALPIERCE:
		skillratio += 50 * skill_lv;
		break;
	case ASC_METEORASSAULT:
		skillratio += 40 * skill_lv - 60;
		break;
	case SN_SHARPSHOOTING:
	case MA_SHARPSHOOTING:
		skillratio += 100 + 50 * skill_lv;
		break;
	case CG_ARROWVULCAN:
		skillratio += 100 + 100 * skill_lv;
		break;
	case AS_SPLASHER:
		skillratio += 400 + 50 * skill_lv;
		if (sd)
			skillratio += 20 * pc_checkskill(sd, AS_POISONREACT);
		break;
	case ASC_BREAKER:
		skillratio += 100 * skill_lv - 100;
		break;
	case PA_SACRIFICE:
		skillratio += 10 * skill_lv - 10;
		break;
	case PA_SHIELDCHAIN:
		skillratio += 30 * skill_lv;
		break;
	case WS_CARTTERMINATION:
		i = 10 * (16 - skill_lv);
		if (i < 1) i = 1;
		//Preserve damage ratio when max cart weight is changed.
		if (sd && sd->cart_weight)
			skillratio += sd->cart_weight / i * 80000 / battle_config.max_cart_weight - 100;
		else if (!sd)
			skillratio += 80000 / i - 100;
		break;
	case TK_DOWNKICK:
		skillratio += 60 + 20 * skill_lv;
		break;
	case TK_STORMKICK:
		skillratio += 60 + 20 * skill_lv;
		break;
	case TK_TURNKICK:
		skillratio += 90 + 30 * skill_lv;
		break;
	case TK_COUNTER:
		skillratio += 90 + 30 * skill_lv;
		break;
	case TK_JUMPKICK:
		skillratio += -70 + 10 * skill_lv;
		if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == skill_id)
			skillratio += 10 * status_get_lv(src) / 3; //Tumble bonus
		if (wd.miscflag)
		{
			skillratio += 10 * status_get_lv(src) / 3; //Running bonus (TODO: What is the real bonus?)
			if (sc && sc->data[SC_SPURT])  // Spurt bonus
				skillratio *= 2;
		}
		break;
	case GS_TRIPLEACTION:
		skillratio += 50 * skill_lv;
		break;
	case GS_BULLSEYE:
		//Only works well against brute/demihumans non bosses.
		if ((tstatus->race == RC_BRUTE || tstatus->race == RC_DEMIHUMAN || tstatus->race == RC_PLAYER)
			&& !(tstatus->mode&MD_BOSS))
			skillratio += 400;
		break;
	case GS_TRACKING:
		skillratio += 100 * (skill_lv + 1);
		break;
	case GS_PIERCINGSHOT:
		skillratio += 20 * skill_lv;
		break;
	case GS_RAPIDSHOWER:
		skillratio += 400 + 50 * skill_lv;
		break;
	case GS_DESPERADO:
		skillratio += 50 * (skill_lv - 1);
		break;
	case GS_DUST:
		skillratio += 50 * skill_lv;
		break;
	case GS_FULLBUSTER:
		skillratio += 100 * (skill_lv + 2);
		break;
	case GS_SPREADATTACK:
		skillratio += 20 * (skill_lv - 1);
		break;
	case NJ_HUUMA:
		skillratio += 50 + 150 * skill_lv;
		break;
	case NJ_TATAMIGAESHI:
		skillratio += 10 * skill_lv;
		break;
	case NJ_KASUMIKIRI:
		skillratio += 10 * skill_lv;
		break;
	case NJ_KIRIKAGE:
		skillratio += 100 * (skill_lv - 1);
		break;
	case KN_CHARGEATK: { // +100% every 3 cells of distance but hard-limited to 500%
		unsigned int k = wd.miscflag / 3;

		if (k < 2)
			k = 0;
		else if (k > 1 && k < 3)
			k = 1;
		else if (k > 2 && k < 4)
			k = 2;
		else if (k > 3 && k < 5)
			k = 3;
		else
			k = 4;
		skillratio += 100 * k;
	}
	break;
	case HT_PHANTASMIC:
		skillratio += 50;
		break;
	case MO_BALKYOUNG:
		skillratio += 200;
		break;
	case HFLI_MOON:	//[orn]
		skillratio += 10 + 110 * skill_lv;
		break;
	case HFLI_SBR44:	//[orn]
		skillratio += 100 * (skill_lv - 1);
		break;
	case NPC_VAMPIRE_GIFT:
		skillratio += ((skill_lv - 1) % 5 + 1) * 100;
		break;
	case RK_SONICWAVE:
		skillratio = (skill_lv + 5) * 100; // ATK = {((Skill Level + 5) x 100) x (1 + [(Caster's Base Level - 100) / 200])} %
		skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
		break;
	case RK_HUNDREDSPEAR:
		skillratio += 500 + (80 * skill_lv);
		if (sd) {
			short index = sd->equip_index[EQI_HAND_R];
			if (index >= 0 && sd->inventory_data[index]
				&& sd->inventory_data[index]->type == IT_WEAPON)
				skillratio += max(10000 - sd->inventory_data[index]->weight, 0) / 10;
			skillratio += 50 * pc_checkskill(sd, LK_SPIRALPIERCE);
		} // (1 + [(Casters Base Level - 100) / 200])
		skillratio = skillratio * (100 + (status_get_lv(src) - 100) / 2) / 100;
		break;
	case RK_WINDCUTTER:
		skillratio = (skill_lv + 2) * 50;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case RK_IGNITIONBREAK: 
		{
			// 3x3 cell Damage = ATK [{(Skill Level x 300) x (1 + [(Caster's Base Level - 100) / 100])}] %
			// 7x7 cell Damage = ATK [{(Skill Level x 250) x (1 + [(Caster's Base Level - 100) / 100])}] %
			// 11x11 cell Damage = ATK [{(Skill Level x 200) x (1 + [(Caster's Base Level - 100) / 100])}] %
			int dmg = 300; // Base maximum damage at less than 3 cells.
			i = distance_bl(src, target);
			if (i > 7)
				dmg -= 100; // Greather than 7 cells. (200 damage)
			else if (i > 3)
				dmg -= 50; // Greater than 3 cells, less than 7. (250 damage)
			dmg = (dmg * skill_lv) * (100 + (status_get_lv(src) - 100) / 12) / 100;
			// Elemental check, 1.5x damage if your element is fire.
			if (sstatus->rhw.ele == ELE_FIRE)
				dmg += dmg / 2;
			skillratio = dmg;
		}
		break;
	case RK_STORMBLAST:
		skillratio = ((sd ? pc_checkskill(sd, RK_RUNEMASTERY) : 1) + (sstatus->int_ / 8)) * 100; // ATK = [{Rune Mastery Skill Level + (Caster's INT / 8)} x 100] %
		break;
	case RK_PHANTOMTHRUST: // ATK = [{(Skill Level x 50) + (Spear Master Level x 10)} x Caster's Base Level / 150] %
		skillratio = 50 * skill_lv + 10 * (sd ? pc_checkskill(sd, KN_SPEARMASTERY) : 5);
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
		/**
			* GC Guilotine Cross
			**/
	case GC_CROSSIMPACT:
		skillratio += 900 + 100 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case GC_PHANTOMMENACE:
		skillratio += 200;
		break;
	case GC_COUNTERSLASH:
		skillratio += 200 + 100 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		if (level_effect_bonus == 1)
			skillratio += 2 * sstatus->agi + 4 * status_get_job_lv_effect(src);
		else
			skillratio += 2 * sstatus->agi + 200;
		break;
	case GC_ROLLINGCUTTER:
		skillratio += -50 + 50 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case GC_CROSSRIPPERSLASHER:
		skillratio += 300 + 80 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		if (sc && sc->data[SC_ROLLINGCUTTER])
			skillratio += sc->data[SC_ROLLINGCUTTER]->val1 * sstatus->agi;
		break;
	case AB_DUPLELIGHT_MELEE:
		skillratio += 10 * skill_lv;
		break;
	case RA_ARROWSTORM:
		skillratio += 900 + 80 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case RA_AIMEDBOLT:
		skillratio += 400 + 50 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case RA_CLUSTERBOMB:
		skillratio += 100 + 100 * skill_lv;
		break;
	case RA_WUGDASH:// ATK 300%
		skillratio += 200;
		break;
	case RA_WUGSTRIKE:
		skillratio = 200 * skill_lv;
		break;
	case RA_WUGBITE:
		skillratio += 300 + 200 * skill_lv;
		if (skill_lv == 5) skillratio += 100;
		break;
	case RA_SENSITIVEKEEN:
		skillratio += 50 * skill_lv;
		break;
	case NC_BOOSTKNUCKLE:
		skillratio += 100 + 100 * skill_lv + sstatus->dex;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case NC_PILEBUNKER:
		skillratio += 200 + 100 * skill_lv + sstatus->str;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case NC_VULCANARM:
		skillratio = 70 * skill_lv + sstatus->dex;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case NC_FLAMELAUNCHER:
	case NC_COLDSLOWER:
		skillratio += 200 + 300 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case NC_ARMSCANNON:
		switch (tstatus->size) {
		case SZ_SMALL: skillratio += 100 + 500 * skill_lv; break;// Small
		case SZ_MEDIUM: skillratio += 100 + 400 * skill_lv; break;// Medium
		case SZ_BIG: skillratio += 100 + 300 * skill_lv; break;// Large
		}
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		//NOTE: Their's some other factors that affects damage, but not sure how exactly. Will recheck one day. [Rytech]
		break;
	case NC_AXEBOOMERANG:
		skillratio += 60 + 40 * skill_lv;
		if (sd) {
			short index = sd->equip_index[EQI_HAND_R];
			if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
				skillratio += sd->inventory_data[index]->weight / 10;// Weight is divided by 10 since 10 weight in coding make 1 whole actural weight. [Rytech]
		}
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case NC_POWERSWING:
		skillratio += 200 + 100 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio += (sstatus->str + sstatus->dex) * status_get_base_lv_effect(src) / 100;
		else
			skillratio += sstatus->str + sstatus->dex;
		break;
	case NC_AXETORNADO:
		skillratio += 100 + 100 * skill_lv + sstatus->vit;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SC_FATALMENACE:
		skillratio += 100 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SC_TRIANGLESHOT:
		skillratio += 200 + (skill_lv - 1) * sstatus->agi / 2;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case SC_FEINTBOMB:
		skillratio = (1 + skill_lv) * sstatus->dex / 2;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_job_lv_effect(src) / 10;
		else
			skillratio = skillratio * 5;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case LG_CANNONSPEAR:
		skillratio = (50 + sstatus->str) * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_BANISHINGPOINT:
		skillratio = (50 * skill_lv) + 30 * (sd ? pc_checkskill(sd, SM_BASH) : 10);
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_SHIELDPRESS:
		skillratio = 150 * skill_lv + sstatus->str;
		if (sd)
		{
			short index = sd->equip_index[EQI_HAND_L];
			if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
				skillratio += sd->inventory_data[index]->weight / 10;
		}
		else// Monster and clone use.
			skillratio += 250;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_PINPOINTATTACK:
		skillratio = 100 * skill_lv + 5 * sstatus->agi;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case LG_RAGEBURST:
		skillratio = 200 * (sd ? sd->rageball_old : 15) + (sstatus->max_hp - sstatus->hp) / 100;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_SHIELDSPELL:// [(Casters Base Level x 4) + (Shield DEF x 10) + (Casters VIT x 2)] %
		if (sd) {
			struct item_data *shield_data = sd->inventory_data[sd->equip_index[EQI_HAND_L]];
			skillratio = status_get_lv(src) * 4 + status_get_vit(src) * 2;
			if (shield_data)
				skillratio += shield_data->def * 10;
		}
		else
			skillratio = 0; // Prevent damage since level 2 is MATK. [Aleos]
		break;
	case LG_OVERBRAND:
		skillratio = 400 * skill_lv + 50 * (sd ? pc_checkskill(sd, CR_SPEARQUICKEN) : 10);
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case LG_OVERBRAND_BRANDISH:
		skillratio = 300 * skill_lv + sstatus->str + sstatus->dex;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case LG_OVERBRAND_PLUSATK:
		skillratio = 200 * skill_lv;
		break;
	case LG_MOONSLASHER:
		skillratio = 120 * skill_lv + 80 * (sd ? pc_checkskill(sd, LG_OVERBRAND) : 5);
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_RAYOFGENESIS:
		skillratio += 200 + 300 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_EARTHDRIVE:
		if (sd)
		{
			short index = sd->equip_index[EQI_HAND_L];
			if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
				skillratio = (1 + skill_lv) * sd->inventory_data[index]->weight / 10;
		}
		else// Monster and clone use.
			skillratio += (1 + skill_lv) * 250;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case LG_HESPERUSLIT:
		skillratio = 120 * skill_lv;
		if (sc && sc->data[SC_BANDING])
		{
			skillratio += 200 * sc->data[SC_BANDING]->val2;
			if ((battle_config.hesperuslit_bonus_stack == 1 && sc->data[SC_BANDING]->val2 >= 6 || sc->data[SC_BANDING]->val2 == 6))
				skillratio = skillratio * 150 / 100;
		}
		if (sc && sc->data[SC_INSPIRATION])
			skillratio += 600;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_DRAGONCOMBO:
		skillratio += 40 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_SKYNETBLOW:
		if (sc && sc->data[SC_COMBO])
			skillratio = 100 * skill_lv + sstatus->agi + 150;
		else
			skillratio = 80 * skill_lv + sstatus->agi;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_EARTHSHAKER:
		if (tsc && (tsc->data[SC_HIDING] || tsc->data[SC_CLOAKING] || // [(Skill Level x 150) x (Caster Base Level / 100) + (Caster INT x 3)] %
			tsc->data[SC_CHASEWALK] || tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC__INVISIBILITY])) {
			skillratio = 150 * skill_lv;
			if (level_effect_bonus == 1)
				skillratio = skillratio * status_get_base_lv_effect(src) / 100;
			skillratio += sstatus->int_ * 3;
		}
		else { //[(Skill Level x 50) x (Caster Base Level / 100) + (Caster INT x 2)] %
			skillratio += 50 * (skill_lv - 2);
			if (level_effect_bonus == 1)
				skillratio = skillratio * status_get_base_lv_effect(src) / 100;
			skillratio += sstatus->int_ * 2;
		}
		break;
	case SR_FALLENEMPIRE:
		skillratio += 150 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case SR_TIGERCANNON:
		if (sc && sc->data[SC_COMBO])
			skillratio = (sstatus->max_hp * (10 + 2 * skill_lv) / 100 + sstatus->max_sp * (5 + skill_lv) / 100) / 2;
		else
			skillratio = (sstatus->max_hp * (10 + 2 * skill_lv) / 100 + sstatus->max_sp * (5 + skill_lv) / 100) / 4;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_RAMPAGEBLASTER:
		if (sc && sc->data[SC_EXPLOSIONSPIRITS])
		{
			skillratio = 20 * (skill_lv + sc->data[SC_EXPLOSIONSPIRITS]->val1) * (sd ? sd->spiritball_old : 15);
			if (level_effect_bonus == 1)
				skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		}
		else
		{
			skillratio = 20 * skill_lv * (sd ? sd->spiritball_old : 15);
			if (level_effect_bonus == 1)
				skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		}
		break;
	case SR_KNUCKLEARROW:
		if (wd.miscflag & 4) {  // ATK [(Skill Level x 150) + (1000 x Target current weight / Maximum weight) + (Target Base Level x 5) x (Caster Base Level / 150)] %
			skillratio = 150 * skill_lv + status_get_lv(target) * 5 * (status_get_lv(src) / 100);
			if (tsd && tsd->weight)
				skillratio += 100 * (tsd->weight / tsd->max_weight);
		}
		else // ATK [(Skill Level x 100 + 500) x Caster Base Level / 100] %
			skillratio += 400 + (100 * skill_lv);
		if (level_effect_bonus == 1)
			skillratio += 5 * status_get_base_lv_effect(target) * status_get_base_lv_effect(src) / 150;
		else
			skillratio += 5 * status_get_base_lv_effect(target);
		break;
	case SR_WINDMILL:
		skillratio = status_get_base_lv_effect(src) + sstatus->dex;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_GATEOFHELL:
		if (sc && sc->data[SC_COMBO])
			skillratio = 800 * skill_lv;
		else
			skillratio = 500 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case SR_GENTLETOUCH_QUIET:
		skillratio = 100 * skill_lv + sstatus->dex;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case SR_HOWLINGOFLION:
		skillratio = 300 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case SR_RIDEINLIGHTNING:
		skillratio = 200 * skill_lv;
		if (sstatus->rhw.ele == ELE_WIND)
			skillratio += 50 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case WM_REVERBERATION_MELEE:
		// ATK [{(Skill Level x 100) + 300} x Caster Base Level / 100]
		skillratio += 200 + 100 * pc_checkskill(sd, WM_REVERBERATION);
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case WM_SEVERE_RAINSTORM_MELEE:
		skillratio = (sstatus->agi + sstatus->dex) * skill_lv / 5;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case WM_GREAT_ECHO:
		skillratio += 800 + 100 * skill_lv;
		if (sd) {	// Still need official value [pakpil]
			short lv = (short)skill_lv;
			skillratio += 100 * skill_check_pc_partner(sd, skill_id, &lv, skill_get_splash(skill_id, skill_lv), 0);
		}
		break;
	case WM_SOUND_OF_DESTRUCTION:
		skillratio += 400;
		break;
	case GN_CART_TORNADO:
		// ATK [( Skill Level x 50 ) + ( Cart Weight / ( 150 - Caster Base STR ))] + ( Cart Remodeling Skill Level x 50 )] %
		skillratio = 50 * skill_lv;
		if (sd && sd->cart_weight)
			skillratio += sd->cart_weight / 10 / max(150 - sd->status.str, 1) + pc_checkskill(sd, GN_REMODELING_CART) * 50;
		break;
	case GN_CARTCANNON:
		// ATK [{( Cart Remodeling Skill Level x 50 ) x ( INT / 40 )} + ( Cart Cannon Skill Level x 60 )] %
		skillratio = 60 * skill_lv;
		if (sd) skillratio += pc_checkskill(sd, GN_REMODELING_CART) * 50 * (sstatus->int_ / 40);
		break;
	case GN_SPORE_EXPLOSION:
		skillratio += 200 + 100 * skill_lv;
		break;
	case GN_CRAZYWEED_ATK:
		skillratio += 400 + 100 * skill_lv;
		break;
	case GN_SLINGITEM_RANGEMELEEATK:
		if (sd) {
			switch (sd->itemid) {
			case 13260: // Apple Bomob
			case 13261: // Coconut Bomb
			case 13262: // Melon Bomb
			case 13263: // Pinapple Bomb
				skillratio += 400;	// Unconfirded
				break;
			case 13264: // Banana Bomb 2000%
				skillratio += 1900;
				break;
			case 13265: skillratio -= 75; break; // Black Lump 25%
			case 13266: skillratio -= 25; break; // Hard Black Lump 75%
			case 13267: skillratio += 100; break; // Extremely Hard Black Lump 200%
			}
		}
		else
			skillratio += 300;	// Bombs
		break;
	case SO_VARETYR_SPEAR://ATK [{( Striking Level x 50 ) + ( Varetyr Spear Skill Level x 50 )} x Caster Base Level / 100 ] %
		skillratio = 50 * skill_lv + (sd ? pc_checkskill(sd, SO_STRIKING) * 50 : 0);
		if (sc && sc->data[SC_BLAST_OPTION])
			skillratio += sd ? sd->status.job_level * 5 : 0;
		break;
		// Physical Elemantal Spirits Attack Skills
	case EL_CIRCLE_OF_FIRE:
	case EL_FIRE_BOMB_ATK:
	case EL_STONE_RAIN:
		skillratio += 200;
		break;
	case EL_FIRE_WAVE_ATK:
		skillratio += 500;
		break;
	case EL_TIDAL_WEAPON:
		skillratio += 1400;
		break;
	case EL_WIND_SLASH:
		skillratio += 100;
		break;
	case EL_HURRICANE:
		skillratio += 600;
		break;
	case EL_TYPOON_MIS:
	case EL_WATER_SCREW_ATK:
		skillratio += 900;
		break;
	case EL_STONE_HAMMER:
		skillratio += 400;
		break;
	case EL_ROCK_CRUSHER:
		skillratio += 700;
		break;
	case KO_JYUMONJIKIRI:
		skillratio += -100 + 150 * skill_lv;
		break;
	case KO_HUUMARANKA:
		skillratio += -100 + 150 * skill_lv + sstatus->dex / 2 + sstatus->agi / 2; // needs more info
		break;
	case KO_SETSUDAN:
		skillratio += 100 * (skill_lv - 1);
		break;
	case KO_BAKURETSU:
		skillratio = 50 * skill_lv * (sd ? pc_checkskill(sd, NJ_TOBIDOUGU) : 10);
		break;
	case MH_NEEDLE_OF_PARALYZE:
		skillratio = 700 + 100 * skill_lv;
		break;
	case MH_SONIC_CRAW:
		skillratio = 40 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case MH_SILVERVEIN_RUSH:
		skillratio = 150 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case MH_MIDNIGHT_FRENZY:
		skillratio = 300 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 150;
		break;
	case MH_TINDER_BREAKER:
		skillratio = 100 * skill_lv + 3 * sstatus->str;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		break;
	case MH_STAHL_HORN:
		skillratio = 500 + 100 * skill_lv;
		if (level_effect_bonus == 1)
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		break;
	case MH_MAGMA_FLOW:
		skillratio = 100 * skill_lv;
		if (level_effect_bonus == 1)
		{
			skillratio += 3 * status_get_base_lv_effect(src);
			skillratio = skillratio * status_get_base_lv_effect(src) / 120;
		}
		else
			skillratio += 450;
		break;
	case MH_LAVA_SLIDE:
		if (level_effect_bonus == 1)
		{
			skillratio = 20 * skill_lv + 2 * status_get_base_lv_effect(src);
			skillratio = skillratio * status_get_base_lv_effect(src) / 100;
		}
		else
			skillratio = 20 * skill_lv + 300;
		break;
	case RL_MASS_SPIRAL:
		{
			short target_def = tstatus->def;

			if (target_def < 0)
				target_def = 0;

			// Damage increase from target's DEF is capped.
			// By default its 500 in renewal. Setting of 0 disables the limit.
			if (battle_config.mass_spiral_max_def != 0 && target_def > battle_config.mass_spiral_max_def)
				target_def = battle_config.mass_spiral_max_def;

			// Official formula.
			// Formula found is correct and weapon type is used. So does DEF reduce the damage?
			// If it does, target's DEF affects the damage ratio so strongly that the ratio out weight's the damage reduction.
			// Target's DEF multiplied by 10 to balance it out for pre-re mechanics. In Renewal its just straight DEF.
			// With this, thanatos (ice pick) effect might work but no confirm on this yet.
			// Still need to test if it works like the Monk's Investigate. [Rytech]
			skillratio = (200 + 10 * target_def) * skill_lv;
		}
		break;
	case RL_BANISHING_BUSTER:
		if (rebel_level_effect == 1)
			skillratio = 1000 + 200 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 2000 + 300 * skill_lv;
		break;
	case RL_S_STORM:
		if (rebel_level_effect == 1)
			skillratio = 1000 + 100 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 1700 + 200 * skill_lv;
		break;
	case RL_FIREDANCE:
		skillratio = 200 * skill_lv + 50 * (sd ? pc_checkskill(sd, GS_DESPERADO) : 10);
		break;
	case RL_H_MINE:
		skillratio = 200 + 200 * skill_lv;
		break;
	case RL_R_TRIP:
		skillratio = 1000 + 300 * skill_lv;
		break;
	case RL_D_TAIL:
		if (rebel_level_effect == 1)
			skillratio = 2500 + 500 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 4000 + 1000 * skill_lv;
		break;
	case RL_FIRE_RAIN:
		if (rebel_level_effect == 1)// Switched out caster's DEX for a fixed amount to keep ratio around newer formula.
			skillratio = 2000 + 160 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 3500 + 300 * skill_lv;
		break;
	case RL_AM_BLAST:
		if (rebel_level_effect == 1)
			skillratio = 2000 + 200 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 3500 + 300 * skill_lv;
		break;
	case RL_SLUGSHOT:
		if (tsd)// Damage on players.
			skillratio = 2000 * skill_lv;
		else// Damage on monsters.
			skillratio = 1200 * skill_lv;
		// Damage multiplied depending on size.
		skillratio *= 2 + tstatus->size;
		break;
	case RL_HAMMER_OF_GOD:
		if (rebel_level_effect == 1)
			skillratio = 1600 + 800 * skill_lv * status_get_base_lv_effect(src) / 100;
		else
			skillratio = 2800 + 1400 * skill_lv;
		if (tsc && tsc->data[SC_C_MARKER])
			skillratio += 100 * (sd ? sd->spiritball_old : 10);
		else
			skillratio += 10 * (sd ? sd->spiritball_old : 10);
		break;
	case RL_R_TRIP_PLUSATK:// Need to confirm if level 5 is really 2700% and not a typo. [Rytech]
		skillratio = 500 + 100 * skill_lv;
		break;
	}
	return skillratio;
}

/*==================================================================================================
 * Constant skill damage additions are added before SC modifiers and after skill base ATK calculation
 *--------------------------------------------------------------------------------------------------*/
static int64 battle_calc_skill_constant_addition(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int64 atk = 0;

	//Constant/misc additions from skills
	switch (skill_id) {
	case MO_EXTREMITYFIST:
		atk = 250 + 150 * skill_lv;
		break;
	case GS_MAGICALBULLET:
		if (sstatus->matk_max > sstatus->matk_min) {
			atk = sstatus->matk_min + rnd() % (sstatus->matk_max - sstatus->matk_min);
		}
		else {
			atk = sstatus->matk_min;
		}
		break;
	case NJ_SYURIKEN:
		atk = 4 * skill_lv;
		break;
	case RA_WUGDASH://(Caster Current Weight x 10 / 8)
		if (sd && sd->weight)
			atk = (sd->weight / 8);
	case RA_WUGSTRIKE:
	case RA_WUGBITE:
		if (sd)
			atk = (30 * pc_checkskill(sd, RA_TOOTHOFWUG));
		break;
	case SR_GATEOFHELL:
		atk = (sstatus->max_hp - status_get_hp(src));
		if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE) {
			atk = (((int64)sstatus->max_sp * (1 + skill_lv * 2 / 10)) + 40 * status_get_lv(src));
		}
		else {
			atk = (((int64)sstatus->sp * (1 + skill_lv * 2 / 10)) + 10 * status_get_lv(src));
		}
		break;
	case SR_TIGERCANNON: // (Tiger Cannon skill level x 240) + (Target Base Level x 40)
		atk = (skill_lv * 240 + status_get_lv(target) * 40);
		if (sc && sc->data[SC_COMBO]
			&& sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE) // (Tiger Cannon skill level x 500) + (Target Base Level x 40)
			atk = (skill_lv * 500 + status_get_lv(target) * 40);
		break;
	case SR_FALLENEMPIRE:// [(Target Size value + Skill Level - 1) x Caster STR] + [(Target current weight x Caster DEX / 120)]
		atk = (((tstatus->size + 1) * 2 + skill_lv - 1) * sstatus->str);
		if (tsd && tsd->weight) {
			atk = ((tsd->weight / 10) * sstatus->dex / 120);
		}
		else {
			atk = (status_get_lv(target) * 50); //mobs
		}
		break;
	case KO_SETSUDAN:
		if (tsc && tsc->data[SC_SPIRIT]) {
			atk = ((wd.damage) * (10 * tsc->data[SC_SPIRIT]->val1)) / 100;// +10% custom value.
			status_change_end(target, SC_SPIRIT, INVALID_TIMER);
		}
		break;
	case KO_KAIHOU:
		if (sd) {
			if (sd->charmball_type < 5) {
				atk = ((wd.damage) * (10 * tsc->data[SC_SPIRIT]->val1)) / 100;// +10% custom value.
				sd->charmball_old = 0;
			}
		}
		break;
	}
	return atk;
}

/*==============================================================
 * Stackable SC bonuses added on top of calculated skill damage
 *--------------------------------------------------------------*/
struct Damage battle_attack_sc_bonus(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);
	//The following are applied on top of current damage and are stackable.
	if (sc) {
		//ATK percent modifier (in pre-renewal, it's applied multiplicatively after the skill ratio)
		if (sc->data[SC_TRUESIGHT])
			ATK_ADDRATE(wd.damage, wd.damage2, 2 * sc->data[SC_TRUESIGHT]->val1);
		if (sc->data[SC_GLOOMYDAY] &&
			(skill_id == LK_SPIRALPIERCE || skill_id == KN_BRANDISHSPEAR ||
				skill_id == CR_SHIELDBOOMERANG || skill_id == PA_SHIELDCHAIN ||
				skill_id == LG_SHIELDPRESS)) {
			ATK_ADDRATE(wd.damage, wd.damage2, sc->data[SC_GLOOMYDAY]->val2);
		}
		if (sc->data[SC_SPIRIT]) {
			if (skill_id == AS_SONICBLOW && sc->data[SC_SPIRIT]->val2 == SL_ASSASIN) {
				ATK_ADDRATE(wd.damage, wd.damage2, map_flag_gvg(src->m) ? 25 : 100); //+25% dmg on woe/+100% dmg on nonwoe
			}
			else if (skill_id == CR_SHIELDBOOMERANG && (sc->data[SC_SPIRIT]->val2 == SL_CRUSADER)) {
				ATK_ADDRATE(wd.damage, wd.damage2, 100);
			}
		}
		if (sc->data[SC_EDP]) {
			switch (skill_id) {
			case AS_SPLASHER:
			case AS_VENOMKNIFE:
			case AS_GRIMTOOTH:
			case ASC_BREAKER:
			case ASC_METEORASSAULT:
				break; //skills above have no effect with edp
			default:
				ATK_ADDRATE(wd.damage, wd.damage2, sc->data[SC_EDP]->val3);
			}
		}
		if (sc->data[SC_STYLE_CHANGE]) {
			TBL_HOM *hd = BL_CAST(BL_HOM, src);
			if (hd) ATK_ADD(wd.damage, wd.damage2, hd->hom_spiritball * 3);
		}
		if (sc->data[SC_FLASHCOMBO]) {
			ATK_ADD(wd.damage, wd.damage2, sc->data[SC_FLASHCOMBO]->val2);
		}
		if (sc->data[SC_HEAT_BARREL]) {
			ATK_ADD(wd.damage, wd.damage2, sc->data[SC_HEAT_BARREL]->val2);
		}
		if (sc->data[SC_P_ALTER]) {
			ATK_ADD(wd.damage, wd.damage2, sc->data[SC_P_ALTER]->val2);
		}
		if (sc->data[SC_ZENKAI] && sstatus->rhw.ele == sc->data[SC_ZENKAI]->val2) {
			ATK_ADD(wd.damage, wd.damage2, 200);
		}
		if ((wd.flag&(BF_LONG | BF_MAGIC)) == BF_LONG) {
			if (sc->data[SC_UNLIMIT]) {
				switch (skill_id) {
				case RA_WUGDASH:
				case RA_WUGSTRIKE:
				case RA_WUGBITE:
					break;
				default:
					ATK_ADDRATE(wd.damage, wd.damage2, sc->data[SC_UNLIMIT]->val2);
					break;
				}
			}
			// Monster Transformation bonus
			if (sc->data[SC_MTF_RANGEATK]) {
				ATK_ADDRATE(wd.damage, wd.damage2, sc->data[SC_MTF_RANGEATK]->val1);
			}
			if (sc->data[SC_MTF_RANGEATK2]) {
				ATK_ADDRATE(wd.damage, wd.damage2, sc->data[SC_MTF_RANGEATK2]->val1);
			}
			if (sc->data[SC_ARCLOUSEDASH]) {
				ATK_ADDRATE(wd.damage, wd.damage2, 10);
			}
			if (sd && skill_summoner_power(sd, POWER_OF_LIFE) == 1) {
				ATK_ADDRATE(wd.damage, wd.damage2, 20);
			}
		}
	}
	return wd;
}

/*====================================
 * Calc defense damage reduction
 *------------------------------------*/
struct Damage battle_calc_defense_reduction(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	int i;

	//Defense reduction
	short vit_def;
	char def1 = status_get_def(target); //Don't use tstatus->def1 due to skill timer reductions.
	short def2 = tstatus->def2;

	if (sd)
	{
		i = sd->ignore_def_by_race[tstatus->race] + sd->ignore_def_by_race[RC_ALL];
		if (i)
		{
			if (i > 100) i = 100;
			def1 -= def1 * i / 100;
			def2 -= def2 * i / 100;
		}
	}

	if (sc && sc->data[SC_EXPIATIO]) {
		i = 5 * sc->data[SC_EXPIATIO]->val1; // 5% per level
		i = min(i, 100); //cap it to 100 for 0 def min
		def1 = (def1*(100 - i)) / 100;
		def2 = (def2*(100 - i)) / 100;
	}

	if (tsc && tsc->data[SC_GENTLETOUCH_REVITALIZE] && tsc->data[SC_GENTLETOUCH_REVITALIZE]->val4)
		def2 += 2 * tsc->data[SC_GENTLETOUCH_REVITALIZE]->val4;

	if (tsc && tsc->data[SC_CAMOUFLAGE]) {
		i = 5 * tsc->data[SC_CAMOUFLAGE]->val3; //5% per second
		i = min(i, 100); //cap it to 100 for 0 def min
		def1 = (def1*(100 - i)) / 100;
		def2 = (def2*(100 - i)) / 100;
	}

	if (battle_config.vit_penalty_type && battle_config.vit_penalty_target&target->type) {
		unsigned char target_count; //256 max targets should be a sane max
		target_count = unit_counttargeted(target);
		if (target_count >= battle_config.vit_penalty_count) {
			if (battle_config.vit_penalty_type == 1) {
				if (!tsc || !tsc->data[SC_STEELBODY])
					def1 = (def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num)) / 100;
				def2 = (def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num)) / 100;
			}
			else { //Assume type 2
				if (!tsc || !tsc->data[SC_STEELBODY])
					def1 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
				def2 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
			}
		}
		if (skill_id == AM_ACIDTERROR) def1 = 0; //Acid Terror ignores only armor defense. [Skotlex]
		if (def2 < 1) def2 = 1;
	}
	//Vitality reduction from rodatazone: http://rodatazone.simgaming.net/mechanics/substats.php#def
	if (tsd)	//Sd vit-eq
	{
		int skill;

		//[VIT*0.5] + rnd([VIT*0.3], max([VIT*0.3],[VIT^2/150]-1))
		vit_def = def2 * (def2 - 15) / 150;
		vit_def = def2 / 2 + (vit_def > 0 ? rnd() % vit_def : 0);
		if (src->type == BL_MOB && (battle_check_undead(sstatus->race, sstatus->def_ele) || sstatus->race == RC_DEMON) && //This bonus already doesnt work vs players
			(skill = pc_checkskill(tsd, AL_DP)) > 0)
			vit_def += skill * (int)(3 + (tsd->status.base_level + 1)*0.04);   // submitted by orn
		if (src->type == BL_MOB && (skill = pc_checkskill(tsd, RA_RANGERMAIN)) > 0 &&
			(sstatus->race == RC_BRUTE || sstatus->race == RC_FISH || sstatus->race == RC_PLANT))
			vit_def += skill * 5;
		if (src->type == BL_MOB && //Only affected from mob
			tsc && tsc->count && tsc->data[SC_P_ALTER] && //If the Platinum Alter is activated
			(battle_check_undead(sstatus->race, sstatus->def_ele) || sstatus->race == RC_UNDEAD))	//Undead attacker
			vit_def += tsc->data[SC_P_ALTER]->val3;
	}
	else { //Mob-Pet vit-eq
			//VIT + rnd(0,[VIT/20]^2-1)
		vit_def = (def2 / 20)*(def2 / 20);
		vit_def = def2 + (vit_def > 0 ? rnd() % vit_def : 0);
	}
	if (battle_config.weapon_defense_type) {
		vit_def += def1 * battle_config.weapon_defense_type;
		def1 = 0;
	}
	if (def1 > 100) def1 = 100;
	ATK_RATE2(wd.damage, wd.damage2,
		attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_R) ? 100 : (is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R) ? (int64)is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R)*(def1 + vit_def) : (100 - def1)),
		attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_L) ? 100 : (is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L) ? (int64)is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L)*(def1 + vit_def) : (100 - def1))
	);
	ATK_ADD2(wd.damage, wd.damage2,
		attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_R) || is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_R) ? 0 : -vit_def,
		attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_L) || is_attack_piercing(wd, src, target, skill_id, skill_lv, EQI_HAND_L) ? 0 : -vit_def
	);
	return wd;
}

/*====================================
 * Modifiers ignoring DEF
 *------------------------------------*/
struct Damage battle_calc_attack_post_defense(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_change *sc = status_get_sc(src);
	struct status_data *sstatus = status_get_status_data(src);

	//Post skill/vit reduction damage increases
	if (sc)
	{	//SC skill damages
		if (sc->data[SC_AURABLADE]
			&& skill_id != LK_SPIRALPIERCE && skill_id != ML_SPIRALPIERCE) {
			int lv = sc->data[SC_AURABLADE]->val1;
			ATK_ADD(wd.damage, wd.damage2, 20 * lv);
		}

		if (sc->data[SC_GENTLETOUCH_CHANGE] && sc->data[SC_GENTLETOUCH_CHANGE]->val2) {
			struct block_list *bl; // ATK increase: ATK [{(Caster DEX / 4) + (Caster STR / 2)} x Skill Level / 5]
			if ((bl = map_id2bl(sc->data[SC_GENTLETOUCH_CHANGE]->val2)))
				ATK_ADD(wd.damage, wd.damage2, (status_get_dex(bl) / 4 + status_get_str(bl) / 2) * sc->data[SC_GENTLETOUCH_CHANGE]->val1 / 5);
		}
	}

	wd = battle_calc_attack_masteries(wd, src, target, skill_id, skill_lv);

	//Refine bonus
	if (sd && battle_skill_stacks_masteries_vvs(skill_id) && skill_id != MO_INVESTIGATE && skill_id != MO_EXTREMITYFIST)
	{ // Counts refine bonus multiple times
		if (skill_id == MO_FINGEROFFENSIVE)
		{
			ATK_ADD2(wd.damage, wd.damage2, wd.div_*sstatus->rhw.atk2, wd.div_*sstatus->lhw.atk2);
		}
		else {
			ATK_ADD2(wd.damage, wd.damage2, sstatus->rhw.atk2, sstatus->lhw.atk2);
		}
	}

	//Set to min of 1
	if (is_attack_right_handed(src, skill_id) && wd.damage < 1) wd.damage = 1;
	if (is_attack_left_handed(src, skill_id) && wd.damage2 < 1) wd.damage2 = 1;

	switch (skill_id) {

	case AS_SONICBLOW:
		if (sd && pc_checkskill(sd, AS_SONICACCEL) > 0)
			ATK_ADDRATE(wd.damage, wd.damage2, 10);
		break;

	case NC_AXETORNADO:
		if ((sstatus->rhw.ele) == ELE_WIND || (sstatus->lhw.ele) == ELE_WIND)
			ATK_ADDRATE(wd.damage, wd.damage2, 50);
		break;
	}

	return wd;
}

/*====================================
 * Plant damage calculation
 *------------------------------------*/
struct Damage battle_calc_attack_plant(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int wflag)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct status_data *tstatus = status_get_status_data(target);
	bool attack_hits = is_attack_hitting(wd, src, target, skill_id, skill_lv, false);

	int right_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_R, wflag);
	int left_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_L, wflag);

	//Plants receive 1 damage when hit
	short class_ = status_get_class(target);
	if (attack_hits || wd.damage > 0)
		wd.damage = 1; //In some cases, right hand no need to have a weapon to deal a damage
	if (is_attack_left_handed(src, skill_id) && (attack_hits || wd.damage2 > 0)) {
		if (sd->status.weapon == W_KATAR)
			wd.damage2 = 0; //No backhand damage against plants
		else {
			wd.damage2 = 1; //Deal 1 HP damage as long as there is a weapon in the left hand
		}
	}

	if (attack_hits && target->type == BL_MOB) {
		struct status_change *sc = status_get_sc(target);
		struct mob_data *md = BL_CAST(BL_MOB, target);
		if (sc &&
			class_ != MOBID_EMPERIUM && !mob_is_battleground(md) &&
			!battle_check_sc(src, target, sc, &wd, 1, skill_id, skill_lv))
		{
			wd.damage = wd.damage2 = 0;
			return wd;
		}
	}

	if (attack_hits && class_ == MOBID_EMPERIUM) {
		if (target && map_flag_gvg2(target->m) && !battle_can_hit_gvg_target(src, target, skill_id, (skill_id) ? BF_SKILL : 0)) {
			wd.damage = wd.damage2 = 0;
			return wd;
		}
		if (wd.damage2 > 0) {
			wd.damage2 = battle_attr_fix(src, target, wd.damage2, left_element, tstatus->def_ele, tstatus->ele_lv);
			wd.damage2 = battle_calc_gvg_damage(src, target, wd.damage2, skill_id, wd.flag);
		}
		else if (wd.damage > 0) {
			wd.damage = battle_attr_fix(src, target, wd.damage, right_element, tstatus->def_ele, tstatus->ele_lv);
			wd.damage = battle_calc_gvg_damage(src, target, wd.damage, skill_id, wd.flag);
		}
		return wd;
	}
	
	//For plants we don't continue with the weapon attack code, so we have to apply DAMAGE_DIV_FIX here
	wd = battle_apply_div_fix(wd);

	//If there is left hand damage, total damage can never exceed 2, even on multiple hits
	if (wd.damage > 1 && wd.damage2 > 0) {
		wd.damage = 1;
		wd.damage2 = 1;
	}
	return wd;
}

/*========================================================================================
 * Perform left/right hand weapon damage calculation based on previously calculated damage
 *----------------------------------------------------------------------------------------*/
struct Damage battle_calc_attack_left_right_hands(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	if (sd) {
		int skill;

		if (!is_attack_right_handed(src, skill_id) && is_attack_left_handed(src, skill_id)) {
			wd.damage = wd.damage2;
			wd.damage2 = 0;
		}
		else if (sd->status.weapon == W_KATAR && !skill_id) { //Katars (offhand damage only applies to normal attacks, tested on Aegis 10.2)
			skill = pc_checkskill(sd, TF_DOUBLE);
			wd.damage2 = (int64)wd.damage * (1 + (skill * 2)) / 100;
		}
		else if (is_attack_right_handed(src, skill_id) && is_attack_left_handed(src, skill_id)) {	//Dual-wield
			if (wd.damage) {
				if ((sd->class_&MAPID_BASEMASK) == MAPID_THIEF) {
					skill = pc_checkskill(sd, AS_RIGHT);
					ATK_RATER(wd.damage, 50 + (skill * 10))
				}
				else if (sd->class_ == MAPID_KAGEROUOBORO) {
					skill = pc_checkskill(sd, KO_RIGHT);
					ATK_RATER(wd.damage, 70 + (skill * 10))
				}
				if (wd.damage < 1) wd.damage = 1;
			}
			if (wd.damage2) {
				if ((sd->class_&MAPID_BASEMASK) == MAPID_THIEF) {
					skill = pc_checkskill(sd, AS_LEFT);
					ATK_RATEL(wd.damage2, 30 + (skill * 10))
				}
				else if (sd->class_ == MAPID_KAGEROUOBORO) {
					skill = pc_checkskill(sd, KO_LEFT);
					ATK_RATEL(wd.damage2, 50 + (skill * 10))
				}
				if (wd.damage2 < 1) wd.damage2 = 1;
			}
		}
	}
	
	if (!is_attack_right_handed(src, skill_id) && !is_attack_left_handed(src, skill_id) && wd.damage)
		wd.damage = 0;

	if (!is_attack_left_handed(src, skill_id) && wd.damage2)
		wd.damage2 = 0;

	return wd;
}

/*==========================================
 * BG/GvG attack modifiers
 *------------------------------------------*/
struct Damage battle_calc_attack_gvg_bg(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	if (wd.damage + wd.damage2) { //There is a total damage value
		if (!wd.damage2)
		{
			wd.damage = battle_calc_damage(src, target, &wd, wd.damage, skill_id, skill_lv);
			if (map_flag_gvg2(target->m))
				wd.damage = battle_calc_gvg_damage(src, target, wd.damage, skill_id, wd.flag);
			else if (map[target->m].flag.battleground)
				wd.damage = battle_calc_bg_damage(src, target, wd.damage, skill_id, wd.flag);
		}
		else if (!wd.damage)
		{
			wd.damage2 = battle_calc_damage(src, target, &wd, wd.damage2, skill_id, skill_lv);
			if (map_flag_gvg2(target->m))
				wd.damage2 = battle_calc_gvg_damage(src, target, wd.damage2, skill_id, wd.flag);
			else if (map[target->m].flag.battleground)
				wd.damage2 = battle_calc_bg_damage(src, target, wd.damage2, skill_id, wd.flag);
		}
		else
		{
			int64 d1 = wd.damage + wd.damage2, d2 = wd.damage2;
			wd.damage = battle_calc_damage(src, target, &wd, d1, skill_id, skill_lv);
			if (map_flag_gvg2(target->m))
				wd.damage = battle_calc_gvg_damage(src, target, wd.damage, skill_id, wd.flag);
			else if (map[target->m].flag.battleground)
				wd.damage = battle_calc_bg_damage(src, target, wd.damage, skill_id, wd.flag);
			wd.damage2 = (int64)d2 * 100 / d1 * wd.damage / 100;
			if (wd.damage > 1 && wd.damage2 < 1) wd.damage2 = 1;
			wd.damage -= wd.damage2;
		}
	}
	return wd;
}

/*==========================================
 * final ATK modifiers - after BG/GvG calc
 *------------------------------------------*/
struct Damage battle_calc_weapon_final_atk_modifiers(struct Damage wd, struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv)
{
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);

	//Reject Sword bugreport:4493 by Daegaladh
	if (wd.damage && tsc && tsc->data[SC_REJECTSWORD] &&
		(src->type != BL_PC || (
		((TBL_PC *)src)->weapontype1 == W_DAGGER ||
			((TBL_PC *)src)->weapontype1 == W_1HSWORD ||
			((TBL_PC *)src)->status.weapon == W_2HSWORD
			)) &&
		rnd() % 100 < tsc->data[SC_REJECTSWORD]->val2
		) {
		ATK_RATER(wd.damage, 50)
			status_fix_damage(target, src, wd.damage, clif_damage(target, src, gettick(), 0, 0, wd.damage, 0, 0, 0, false));
		clif_skill_nodamage(target, target, ST_REJECTSWORD, tsc->data[SC_REJECTSWORD]->val1, 1);
		if (--(tsc->data[SC_REJECTSWORD]->val3) <= 0)
			status_change_end(target, SC_REJECTSWORD, INVALID_TIMER);
	}
	if (tsc && tsc->data[SC_CRESCENTELBOW] && !is_boss(src) && rnd() % 100 < tsc->data[SC_CRESCENTELBOW]->val2) {
		//ATK [{(Target HP / 100) x Skill Level} x Caster Base Level / 125] % + [Received damage x {1 + (Skill Level x 0.2)}]
		int64 rdamage = 0;
		int ratio = (int64)(status_get_hp(src) / 100) * tsc->data[SC_CRESCENTELBOW]->val1 * status_get_lv(target) / 125;
		if (ratio > 5000) ratio = 5000; // Maximum of 5000% ATK
		rdamage = battle_calc_base_damage(tstatus, &tstatus->rhw, tsc, sstatus->size, tsd, 0);
		rdamage = (int64)rdamage * ratio / 100 + wd.damage * (10 + tsc->data[SC_CRESCENTELBOW]->val1 * 20 / 10) / 10;
		skill_blown(target, src, skill_get_blewcount(SR_CRESCENTELBOW_AUTOSPELL, tsc->data[SC_CRESCENTELBOW]->val1), unit_getdir(src), 0);
		clif_skill_damage(target, src, gettick(), status_get_amotion(src), 0, rdamage,
			1, SR_CRESCENTELBOW_AUTOSPELL, tsc->data[SC_CRESCENTELBOW]->val1, 6); // This is how official does
		clif_damage(src, target, gettick(), status_get_amotion(src) + 1000, 0, rdamage / 10, 1, 0, 0, false);
		status_damage(target, src, rdamage, 0, 0, 0);
		status_damage(src, target, rdamage / 10, 0, 0, 1);
		status_change_end(target, SC_CRESCENTELBOW, INVALID_TIMER);
	}

	if (sc) {
		//SC_FUSION hp penalty [Komurka]
		if (sc->data[SC_FUSION]) {
			int hp = sstatus->max_hp;
			if (sd && tsd) {
				hp = 8 * hp / 100;
				if (((int64)sstatus->hp * 100) <= ((int64)sstatus->max_hp * 20))
					hp = sstatus->hp;
			}
			else
				hp = 2 * hp / 100; //2% hp loss per hit
			status_zap(src, hp, 0);
		}
		/**
		 * affecting non-skills
		 **/
		if (!skill_id) {
			/**
			 * RK Enchant Blade
			 **/
			if (sc->data[SC_ENCHANTBLADE] && sd && ((is_attack_right_handed(src, skill_id) && sd->weapontype1) || (is_attack_left_handed(src, skill_id) && sd->weapontype2))) {
				//[( ( Skill Lv x 20 ) + 100 ) x ( casterBaseLevel / 150 )] + casterInt
				ATK_ADD(wd.damage, wd.damage2, (sc->data[SC_ENCHANTBLADE]->val1 * 20 + 100) * status_get_lv(src) / 150 + status_get_int(src));
			}
		}
		status_change_end(src, SC_CAMOUFLAGE, INVALID_TIMER);
	}
	switch (skill_id) {
		case LG_RAYOFGENESIS:
		{
			struct Damage md = battle_calc_magic_attack(src, target, skill_id, skill_lv, wd.miscflag);
			wd.damage += md.damage;
		}
		break;
		case ASC_BREAKER:
		{	//Breaker's int-based damage (a misc attack?)
			struct Damage md = battle_calc_misc_attack(src, target, skill_id, skill_lv, wd.miscflag);
			wd.damage += md.damage;
		}
		break;
	}
	return wd;
}

/*====================================================
 * Basic wd init - not influenced by HIT/MISS/DEF/etc.
 *----------------------------------------------------*/
static struct Damage initialize_weapon_data(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int wflag)
{
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct Damage wd;

	wd.type = 0; //Normal attack
	wd.div_ = skill_id ? skill_get_num(skill_id, skill_lv) : 1;
	wd.amotion = (skill_id && skill_get_inf(skill_id)&INF_GROUND_SKILL) ? 0 : sstatus->amotion; //Amotion should be 0 for ground skills.
	/*if(skill_id == KN_AUTOCOUNTER) // counter attack DOES obey ASPD delay on AEGIS
		wd.amotion >>= 1; */
	wd.dmotion = tstatus->dmotion;
	wd.blewcount = skill_get_blewcount(skill_id, skill_lv);
	wd.miscflag = wflag;
	wd.flag = BF_WEAPON; //Initial Flag
	wd.flag |= (skill_id || wd.miscflag) ? BF_SKILL : BF_NORMAL; // Baphomet card's splash damage is counted as a skill. [Inkfish]
	wd.isspdamage = false;
	wd.damage = wd.damage2 = 0;

	wd.dmg_lv = ATK_DEF;	//This assumption simplifies the assignation later

	if (sd)
		wd.blewcount += battle_blewcount_bonus(sd, skill_id);

	if (skill_id) {
		wd.flag |= battle_range_type(src, target, skill_id, skill_lv);
		switch (skill_id)
		{
		case MH_SONIC_CRAW: {
			TBL_HOM *hd = BL_CAST(BL_HOM, src);
			wd.div_ = hd->hom_spiritball;
		}
							break;
		case MO_FINGEROFFENSIVE:
			if (sd) {
				if (battle_config.finger_offensive_type)
					wd.div_ = 1;
				else
					wd.div_ = sd->spiritball_old;
			}
			break;

		case KN_PIERCE:
		case ML_PIERCE:
			wd.div_ = (wd.div_ > 0 ? tstatus->size + 1 : -(tstatus->size + 1));
			break;

		case TF_DOUBLE: //For NPC used skill.
		case GS_CHAINACTION:
			wd.type = 0x08;
			break;

		case GS_GROUNDDRIFT:
			wd.amotion = sstatus->amotion;
			//Fall through
		case KN_SPEARSTAB:
		case KN_BOWLINGBASH:
		case MS_BOWLINGBASH:
		case MO_BALKYOUNG:
		case TK_TURNKICK:
			wd.blewcount = 0;
			break;

		case KN_AUTOCOUNTER:
			wd.flag = (wd.flag&~BF_SKILLMASK) | BF_NORMAL;
			break;
		case LK_SPIRALPIERCE:
			if (!sd) wd.flag = (wd.flag&~(BF_RANGEMASK | BF_WEAPONMASK)) | BF_LONG | BF_MISC;
			break;
		}
	}
	else {
		wd.flag |= is_skill_using_arrow(src, skill_id) ? BF_LONG : BF_SHORT;
	}

	return wd;
}

/**
* Check if bl is devoted by someone
* @param bl
* @return 'd_bl' if devoted or NULL if not devoted
*/
struct block_list *battle_check_devotion(struct block_list *bl) {
	struct block_list *d_bl = NULL;

	if (battle_config.devotion_rdamage && battle_config.devotion_rdamage > rnd() % 100) {
		struct status_change *sc = status_get_sc(bl);
		if (sc && sc->data[SC_DEVOTION])
			d_bl = map_id2bl(sc->data[SC_DEVOTION]->val1);
	}
	return d_bl;
}

/*
 * Check if we should reflect the dammage and calculate it if so
 * @param attack_type : BL_WEAPON,BL_MAGIC or BL_MISC
 * @param wd : weapon damage
 * @param src : bl who did the attack
 * @param target : target of the attack
 * @parem skill_id : id of casted skill, 0 = basic atk
 * @param skill_lv : lvl of skill casted
 */
void battle_do_reflect(int attack_type, struct Damage *wd, struct block_list* src, struct block_list* target, uint16 skill_id, uint16 skill_lv) {

	if ((wd->damage + wd->damage2) && src && target && src != target &&
		(src->type != BL_SKILL ||
		(src->type == BL_SKILL && (skill_id == SG_SUN_WARM || skill_id == SG_MOON_WARM || skill_id == SG_STAR_WARM)))
		) { //don't reflect to ourself
		int64 damage = wd->damage + wd->damage2, rdamage = 0;
		struct map_session_data *tsd = BL_CAST(BL_PC, target);
		struct status_change *tsc = status_get_sc(target);
		struct status_data *sstatus = status_get_status_data(src);
		int64 tick = gettick();
		int rdelay = 0;

		// Item reflect gets calculated first
		rdamage = battle_calc_return_damage(target, src, &damage, wd->flag, skill_id, false);
		if (rdamage > 0) {
			struct block_list *d_bl = battle_check_devotion(src);

			rdelay = clif_damage(src, (!d_bl) ? src : d_bl, tick, wd->amotion, sstatus->dmotion, rdamage, 1, DMG_ENDURE, 0, false);
			if (tsd) battle_drain(tsd, src, rdamage, rdamage, sstatus->race, sstatus->class_, is_infinite_defense(src, wd->flag));
			//Use Reflect Shield to signal this kind of skill trigger. [Skotlex]
			battle_delay_damage(tick, wd->amotion, target, (!d_bl) ? src : d_bl, 0, CR_REFLECTSHIELD, 0, rdamage, ATK_DEF, rdelay, true);
			skill_additional_effect(target, (!d_bl) ? src : d_bl, CR_REFLECTSHIELD, 1, BF_WEAPON | BF_SHORT | BF_NORMAL, ATK_DEF, tick);
		}

		if (!tsc)
			return;

		// Calculate skill reflect damage separately
		rdamage = battle_calc_return_damage(target, src, &damage, wd->flag, skill_id, true);
		if (rdamage > 0) {
			struct block_list *d_bl = battle_check_devotion(src);

			if (attack_type == BF_WEAPON && tsc->data[SC_LG_REFLECTDAMAGE]) // Don't reflect your own damage (Grand Cross)
				map_foreachinshootrange(battle_damage_area, target, skill_get_splash(LG_REFLECTDAMAGE, 1), BL_CHAR, tick, target, wd->amotion, sstatus->dmotion, rdamage, status_get_race(target));
			else if (attack_type == BF_WEAPON || attack_type == BF_MISC) {
				rdelay = clif_damage(src, (!d_bl) ? src : d_bl, tick, wd->amotion, sstatus->dmotion, rdamage, 1, DMG_ENDURE, 0, false);
				if (tsd)
					battle_drain(tsd, src, rdamage, rdamage, sstatus->race, sstatus->class_, is_infinite_defense(src, wd->flag));
				// It appears that official servers give skill reflect damage a longer delay
				battle_delay_damage(tick, wd->amotion, target, (!d_bl) ? src : d_bl, 0, CR_REFLECTSHIELD, 0, rdamage, ATK_DEF, rdelay, true);
				skill_additional_effect(target, (!d_bl) ? src : d_bl, CR_REFLECTSHIELD, 1, BF_WEAPON | BF_SHORT | BF_NORMAL, ATK_DEF, tick);
			}
		}
	}
}

/*==========================================
 * battle_calc_weapon_attack (by Skotlex)
 *------------------------------------------*/
static struct Damage battle_calc_weapon_attack(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int wflag)
{
	struct map_session_data *sd, *tsd;
	struct Damage wd;
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *tstatus = status_get_status_data(target);
	int right_element, left_element;
	bool infdef = false;

	memset(&wd, 0, sizeof(wd));

	nullpo_retr(wd, src);
	nullpo_retr(wd, target);

	wd = initialize_weapon_data(src, target, skill_id, skill_lv, wflag);

	right_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_R, wflag);
	left_element = battle_get_weapon_element(wd, src, target, skill_id, skill_lv, EQI_HAND_L, wflag);

	if (sc && !sc->count)
		sc = NULL; //Skip checking as there are no status changes active.
	if (tsc && !tsc->count)
		tsc = NULL; //Skip checking as there are no status changes active.

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);

	if ((!skill_id || skill_id == PA_SACRIFICE) && tstatus->flee2 && rnd() % 1000 < tstatus->flee2)
	{	//Check for Lucky Dodge
		wd.type = 0x0b;
		wd.dmg_lv = ATK_LUCKY;
		if (wd.div_ < 0) wd.div_ *= -1;
		return wd;
	}

	wd = battle_calc_multi_attack(wd, src, target, skill_id, skill_lv);

	if (is_attack_critical(wd, src, target, skill_id, skill_lv, true))
		wd.type = 0x0a;

	if (!is_attack_hitting(wd, src, target, skill_id, skill_lv, true))
		wd.dmg_lv = ATK_FLEE;
	else if (!(infdef = is_infinite_defense(target, wd.flag))) { //no need for math against plants
		int ratio;
		int i;

		wd = battle_calc_skill_base_damage(wd, src, target, skill_id, skill_lv);
		ratio = battle_calc_attack_skill_ratio(wd, src, target, skill_id, skill_lv);

		ATK_RATE(wd.damage, wd.damage2, ratio);

		int64 bonus_damage = battle_calc_skill_constant_addition(wd, src, target, skill_id, skill_lv); // other skill bonuses

		ATK_ADD(wd.damage, wd.damage2, bonus_damage);

		if (battle_config.magiccrasher_renewal && skill_id == HW_MAGICCRASHER) { // Add weapon attack for MATK onto Magic Crasher
			struct status_data *sstatus = status_get_status_data(src);

			if (sstatus->matk_max > sstatus->matk_min) {
				ATK_ADD(wd.damage, wd.damage2, sstatus->matk_min + rnd() % (sstatus->matk_max - sstatus->matk_min));
			}
			else
				ATK_ADD(wd.damage, wd.damage2, sstatus->matk_min);
		}
		
		// add any miscellaneous player ATK bonuses	
		if (sd && skill_id && (i = pc_skillatk_bonus(sd, skill_id))) {
			ATK_ADDRATE(wd.damage, wd.damage2, i);
		}

		// final attack bonuses that aren't affected by cards
		wd = battle_attack_sc_bonus(wd, src, target, skill_id, skill_lv);

		// check if attack ignores DEF
		if (!attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_L) || !attack_ignores_def(wd, src, target, skill_id, skill_lv, EQI_HAND_R))
			wd = battle_calc_defense_reduction(wd, src, target, skill_id, skill_lv);

		wd = battle_calc_attack_post_defense(wd, src, target, skill_id, skill_lv);
	}

		wd = battle_calc_element_damage(wd, src, target, skill_id, skill_lv, wflag);

	if (skill_id == CR_GRANDCROSS || skill_id == NPC_GRANDDARKNESS)
		return wd; //Enough, rest is not needed.

	if (skill_id == NJ_KUNAI) {
		short nk = battle_skill_get_damage_properties(skill_id, wd.miscflag);

		ATK_ADD(wd.damage, wd.damage2, 90);
		nk |= NK_IGNORE_DEF;
	}

	switch (skill_id) 
	{
		case TK_DOWNKICK:
		case TK_STORMKICK:
		case TK_TURNKICK:
		case TK_COUNTER:
		case TK_JUMPKICK:
			if (sd && pc_checkskill(sd, TK_RUN)) {
				uint8 i;
				uint16 skill = pc_checkskill(sd, TK_RUN);

				switch (skill) {
				case 1: case 4: case 7: case 10: i = 1; break;
				case 2: case 5: case 8: i = 2; break;
				default: i = 0; break;
				}
				if (sd->weapontype1 == W_FIST && sd->weapontype2 == W_FIST)
					ATK_ADD(wd.damage, wd.damage2, 10 * skill - i);
			}
			break;
		case SR_GATEOFHELL:
			{
				struct status_data *sstatus = status_get_status_data(src);

				ATK_ADD(wd.damage, wd.damage2, sstatus->max_hp - status_get_hp(src));
				if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE) {
					ATK_ADD(wd.damage, wd.damage2, (sstatus->max_sp * (1 + skill_lv * 2 / 10)) + 40 * status_get_lv(src));
				}
				else
					ATK_ADD(wd.damage, wd.damage2, (sstatus->sp * (1 + skill_lv * 2 / 10)) + 10 * status_get_lv(src));
			}
			break;
	}

	if (sd)
	{
		uint16 skill;

		if ((skill = pc_checkskill(sd, BS_WEAPONRESEARCH)) > 0)
			ATK_ADD(wd.damage, wd.damage2, skill * 2);
		if (skill_id == TF_POISON)
			ATK_ADD(wd.damage, wd.damage2, 15 * skill_lv);
		if (skill_id == GS_GROUNDDRIFT)
			ATK_ADD(wd.damage, wd.damage2, 50 * skill_lv);
		if (skill_id != CR_SHIELDBOOMERANG) //Only Shield boomerang doesn't takes the Star Crumbs bonus.
			ATK_ADD2(wd.damage, wd.damage2, ((wd.div_ < 1) ? 1 : wd.div_) * sd->right_weapon.star, ((wd.div_ < 1) ? 1 : wd.div_) * sd->left_weapon.star);
		if (skill_id != MC_CARTREVOLUTION && pc_checkskill(sd, BS_HILTBINDING) > 0)
			ATK_ADD(wd.damage, wd.damage2, 4);
		if (skill_id == MO_FINGEROFFENSIVE) { //The finger offensive spheres on moment of attack do count. [Skotlex]
			ATK_ADD(wd.damage, wd.damage2, wd.div_*sd->spiritball_old * 3);
		}
		else {
			ATK_ADD(wd.damage, wd.damage2, wd.div_*sd->spiritball * 3);
		}

		if (skill_id == CR_SHIELDBOOMERANG || skill_id == PA_SHIELDCHAIN)
		{ //Refine bonus applies after cards and elements.
			short index = sd->equip_index[EQI_HAND_L];
			if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_ARMOR)
				ATK_ADD(wd.damage, wd.damage2, 10 * sd->inventory.u.items_inventory[index].refine);
		}

		//Card Fix for attacker (sd), 2 is added to the "left" flag meaning "attacker cards only"
		switch (skill_id) {
		case RK_DRAGONBREATH:
		case RK_DRAGONBREATH_WATER:
			if (wd.flag&BF_LONG) {
				wd.damage = wd.damage * (100 + sd->bonus.long_attack_atk_rate) / 100;
				if (is_attack_left_handed(src, skill_id))
					wd.damage2 = wd.damage2 * (100 + sd->bonus.long_attack_atk_rate) / 100;
			}
			break;
		default:
			wd.damage += battle_calc_cardfix(BF_WEAPON, src, target, battle_skill_get_damage_properties(skill_id, wd.miscflag), right_element, left_element, wd.damage, 2, wd.flag);
			if (is_attack_left_handed(src, skill_id))
				wd.damage2 += battle_calc_cardfix(BF_WEAPON, src, target, battle_skill_get_damage_properties(skill_id, wd.miscflag), right_element, left_element, wd.damage2, 3, wd.flag);
			break;
		}
	}

	if (tsd) { // Card Fix for target (tsd), 2 is not added to the "left" flag meaning "target cards only"
		switch (skill_id) { // These skills will do a card fix later
		case SO_VARETYR_SPEAR:
			break;
		default:
			wd.damage += battle_calc_cardfix(BF_WEAPON, src, target, battle_skill_get_damage_properties(skill_id, wd.miscflag), right_element, left_element, wd.damage, 0, wd.flag);
			if (is_attack_left_handed(src, skill_id))
				wd.damage2 += battle_calc_cardfix(BF_WEAPON, src, target, battle_skill_get_damage_properties(skill_id, wd.miscflag), right_element, left_element, wd.damage2, 1, wd.flag);
			break;
		}
	}

	// only do 1 dmg to plant, no need to calculate rest
	if (infdef)
		return battle_calc_attack_plant(wd, src, target, skill_id, skill_lv, wflag);


	//Apply DAMAGE_DIV_FIX and check for min damage
	wd = battle_apply_div_fix(wd);

	wd = battle_calc_attack_left_right_hands(wd, src, target, skill_id, skill_lv);

	switch (skill_id) { // These skills will do a GVG fix later
	case SO_VARETYR_SPEAR:
		return wd;
	default:
		wd = battle_calc_attack_gvg_bg(wd, src, target, skill_id, skill_lv);
		break;
	}

	wd = battle_calc_weapon_final_atk_modifiers(wd, src, target, skill_id, skill_lv);

	battle_do_reflect(BF_WEAPON, &wd, src, target, skill_id, skill_lv); //WIP

	return wd;
}

/*==========================================
 * battle_calc_magic_attack [DracoRPG]
 *------------------------------------------*/
struct Damage battle_calc_magic_attack(struct block_list *src,struct block_list *target,int skill_id,int skill_lv,int mflag)
{
	int i, nk;
	short s_ele = 0;
	bool level_effect_bonus = battle_config.renewal_level_effect_skills;// Base/Job level effect on formula's.

	TBL_PC *sd;
	//TBL_PC *tsd;
	struct Damage ad;
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);
	struct {
		unsigned imdef : 1;
		unsigned infdef : 1;
	}	flag;

	memset(&ad,0,sizeof(ad));
	memset(&flag,0,sizeof(flag));

	nullpo_retr(ad, src);
	nullpo_retr(ad, target);

	sd = BL_CAST(BL_PC, src);
	//tsd = BL_CAST(BL_PC, target);

	//Initial Values
	ad.damage = 1;
	ad.div_ = skill_get_num(skill_id, skill_lv);
	ad.amotion=skill_get_inf(skill_id)&INF_GROUND_SKILL?0:sstatus->amotion; //Amotion should be 0 for ground skills.
	ad.dmotion=tstatus->dmotion;
	ad.blewcount = skill_get_blewcount(skill_id, skill_lv);
	ad.flag=BF_MAGIC|BF_SKILL;
	ad.dmg_lv=ATK_DEF;
	nk = skill_get_nk(skill_id);
	flag.imdef = nk&NK_IGNORE_DEF?1:0;

	if (sc && !sc->count)
		sc = NULL; //Skip checking as there are no status changes active.
	if (tsc && !tsc->count)
		tsc = NULL; //Skip checking as there are no status changes active.

	//Initialize variables that will be used afterwards
	s_ele = skill_get_ele(skill_id, skill_lv);

	if (s_ele == -1) // pl=-1 : the skill takes the weapon's element
		s_ele = sstatus->rhw.ele;
	else if (s_ele == -2) //Use status element
		s_ele = status_get_attack_sc_element(src,status_get_sc(src));
	else if( s_ele == -3 ) //Use random element
		s_ele = rnd()%ELE_ALL;

	switch (skill_id)
	{
		case WL_HELLINFERNO:
			if (mflag & 8)// 2nd Hit - The shadow damage.
				s_ele = ELE_DARK;
			break;

		case KO_KAIHOU:
			if (sd)// Take the element of the charms.
				s_ele = sd->charmball_type;
			break;

		case SO_PSYCHIC_WAVE:
			if ( sc )
			{
				if ( sc->data[SC_HEATER_OPTION] )
					s_ele = ELE_FIRE;
				else if (sc->data[SC_COOLER_OPTION])
					s_ele = ELE_WATER;
				else if (sc->data[SC_BLAST_OPTION])
					s_ele = ELE_WIND;
				else if (sc->data[SC_CURSED_SOIL_OPTION])
					s_ele = ELE_EARTH;
			}
			break;
	}
	
	//Set miscellaneous data that needs be filled
	if(sd) {
		sd->state.arrow_atk = 0;
		ad.blewcount += battle_blewcount_bonus(sd, skill_id);
	}

	//Skill Range Criteria
	ad.flag |= battle_range_type(src, target, skill_id, skill_lv);
	//Infinite defense (plant mode)
	flag.infdef = is_infinite_defense(target, ad.flag) ? 1 : 0;
		
	switch(skill_id)
	{
		case MG_FIREWALL:
		case EL_FIRE_MANTLE:
			if (tstatus->def_ele == ELE_FIRE || battle_check_undead(tstatus->race, tstatus->def_ele))
				ad.blewcount = 0; //No knockback
			//Fall through
		case NJ_KAENSIN:
		case PR_SANCTUARY:
			ad.dmotion = 1; //No flinch animation.
			break;
		case EL_STONE_RAIN:
			ad.div_ = 1;// Magic version only hits once.
			break;
	}

	if (!flag.infdef) //No need to do the math for plants
	{
		unsigned int skillratio = 100;	//Skill dmg modifiers.

//MATK_RATE scales the damage. 100 = no change. 50 is halved, 200 is doubled, etc
#define MATK_RATE( a ) { ad.damage= ad.damage*(a)/100; }
//Adds dmg%. 100 = +100% (double) damage. 10 = +10% damage
#define MATK_ADDRATE( a ) { ad.damage+= ad.damage*(a)/100; }
//Adds an absolute value to damage. 100 = +100 damage
#define MATK_ADD( a ) { ad.damage+= a; }

		switch (skill_id)
		{	//Calc base damage according to skill
			case AL_HEAL:
			case PR_BENEDICTIO:
			case PR_SANCTUARY:
				ad.damage = skill_calc_heal(src, target, skill_id, skill_lv, false);
				break;
			case PR_ASPERSIO:
				ad.damage = 40;
				break;
			case ALL_RESURRECTION:
			case PR_TURNUNDEAD:
				//Undead check is on skill_castend_damageid code.
				i = 20 * skill_lv + sstatus->luk + sstatus->int_ + status_get_base_lv_effect(src)
				  	+ 200 - 200*tstatus->hp/tstatus->max_hp;
				if(i > 700) i = 700;
				if(rnd()%1000 < i && !(tstatus->mode&MD_BOSS))
					ad.damage = tstatus->hp;
				else
					ad.damage = status_get_base_lv_effect(src) + sstatus->int_ + skill_lv * 10;
				break;
			case PF_SOULBURN:
				ad.damage = tstatus->sp * 2;
				break;
			default:
			{
				if( skill_id == RK_ENCHANTBLADE ){
					if( sc && sc->data[SC_ENCHANTBLADE] )
						ad.damage += sc->data[SC_ENCHANTBLADE]->val2;
					else
						return ad;
				}
				if (sc && sc->data[SC_RECOGNIZEDSPELL]) {
					MATK_ADD(sstatus->matk_max);//Recognized Spell makes you deal your maximum MATK on all magic attacks.
				} else if (sstatus->matk_max > sstatus->matk_min) {
					MATK_ADD(sstatus->matk_min+rnd()%(sstatus->matk_max-sstatus->matk_min));
				} else {
					MATK_ADD(sstatus->matk_min);
				}

				if (sd)
				{// Soul energy spheres adds MATK.
					MATK_ADD(3*sd->soulball);
				}

				if(nk&NK_SPLASHSPLIT){ // Divide MATK in case of multiple targets skill
					if(mflag>0)
						ad.damage/= mflag;
					else
						ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
				}

				// Enchant Blade's MATK damage is calculated before
				// being sent off to combine with the weapon attack.
				if ( sc && sc->data[SC_ENCHANTBLADE] && !skill_id )
					MATK_ADD( sc->data[SC_ENCHANTBLADE]->val2 );

				switch( skill_id ) {
					case MG_NAPALMBEAT:
					case MG_FIREBALL:
						skillratio += skill_lv*10-30;
						break;
					case MG_SOULSTRIKE:
						if (battle_check_undead(tstatus->race,tstatus->def_ele))
							skillratio += 5*skill_lv;
						break;
					case MG_COLDBOLT:
						if (sc) 
						{
							if ( sc->data[SC_SPELLFIST] && (!sd || !sd->state.autocast))
							{
								skillratio = sc->data[SC_SPELLFIST]->val2 * 50 + sc->data[SC_SPELLFIST]->val4 * 100;//val2 = used spellfist level and val4 = used bolt level. [Rytech]
								ad.div_ = 1; // ad mods, to make it work similar to regular hits [Xazax]
 								ad.flag = BF_WEAPON|BF_SHORT; // ad mods, to make it work similar to regular hits [Xazax]
								ad.type = 0;
							}
							if (sc->data[SC_AQUAPLAY_OPTION])
								skillratio += sc->data[SC_AQUAPLAY_OPTION]->val2;
						}
						break;
					case MG_FIREWALL:
						skillratio -= 50;
						if (sc && sc->data[SC_PYROTECHNIC_OPTION])
							skillratio += sc->data[SC_PYROTECHNIC_OPTION]->val2;
						break;
					case MG_FIREBOLT:
						if (sc) 
						{
							if ( sc->data[SC_SPELLFIST] && (!sd || !sd->state.autocast))
							{
								skillratio = sc->data[SC_SPELLFIST]->val2 * 50 + sc->data[SC_SPELLFIST]->val4 * 100;
								ad.div_ = 1;
								ad.flag = BF_WEAPON|BF_SHORT;
								ad.type = 0;
							}
							if (sc->data[SC_PYROTECHNIC_OPTION])
								skillratio += sc->data[SC_PYROTECHNIC_OPTION]->val2;
						}
						break;
					case MG_LIGHTNINGBOLT:
						if (sc) 
						{
							if ( sc->data[SC_SPELLFIST] && (!sd || !sd->state.autocast))
							{
								skillratio = sc->data[SC_SPELLFIST]->val2 * 50 + sc->data[SC_SPELLFIST]->val4 * 100;
								ad.div_ = 1;
								ad.flag = BF_WEAPON|BF_SHORT;
								ad.type = 0;
							}

							if (sc->data[SC_GUST_OPTION])
								skillratio += sc->data[SC_GUST_OPTION]->val2;
						}
						break;
					case MG_THUNDERSTORM:
						skillratio -= 20;
						if (sc && sc->data[SC_GUST_OPTION])
							skillratio += sc->data[SC_GUST_OPTION]->val2;
						break;
					case MG_FROSTDIVER:
						skillratio += 10*skill_lv;
						if (sc && sc->data[SC_AQUAPLAY_OPTION])
							skillratio += sc->data[SC_AQUAPLAY_OPTION]->val2;
						break;
					case AL_HOLYLIGHT:
						skillratio += 25;
						if (sd && sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_PRIEST)
							skillratio *= 5; //Does 5x damage include bonuses from other skills?
						break;
					case AL_RUWACH:
						skillratio += 45;
						break;
					case WZ_FROSTNOVA:
						skillratio += (100 + skill_lv * 10) * 2 / 3 - 100;
						break;
					case WZ_FIREPILLAR:
						if (skill_lv > 10)
							skillratio += 2300; //200% MATK each hit
						else
							skillratio += -60 + 20 * skill_lv; //20% MATK each hit
						break;
					case WZ_SIGHTRASHER:
						skillratio += 20*skill_lv;
						break;
					case WZ_VERMILION:
						skillratio += 20*skill_lv-20;
						break;
					case WZ_WATERBALL:
						skillratio += 30*skill_lv;
						break;
					case WZ_STORMGUST:
						skillratio += 40*skill_lv;
						break;
					case WZ_EARTHSPIKE:
					case WZ_HEAVENDRIVE:
						if (sc && sc->data[SC_PETROLOGY_OPTION])
							skillratio += sc->data[SC_PETROLOGY_OPTION]->val2;
						break;
					case HW_NAPALMVULCAN:
						skillratio += 25;
						break;
					case SL_STIN:
						skillratio += (tstatus->size?-99:10*skill_lv); //target size must be small (0) for full damage.
						break;
					case SL_STUN:
						skillratio += (tstatus->size!=2?5*skill_lv:-99); //Full damage is dealt on small/medium targets
						break;
					case SL_SMA:
						skillratio += -60 + status_get_base_lv_effect(src); //Base damage is 40% + lv%
						break;
					case NJ_KOUENKA:// Crimson Fire Petal
						skillratio -= 10;
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_FIRE )
							skillratio += 20 * sd->charmball;
						break;
					case NJ_KAENSIN:// Crimson Fire Formation
						skillratio -= 50;
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_FIRE )
							skillratio += 10 * sd->charmball;
						break;
					case NJ_BAKUENRYU:// Raging Fire Dragon
						skillratio += 50 + 150 * skill_lv;
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_FIRE )
							skillratio += 45 * sd->charmball;
						break;
					case NJ_HYOUSENSOU:// Spear of Ice
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_WATER )
							skillratio += 5 * sd->charmball;
						break;
					case NJ_HYOUSYOURAKU:// Ice Meteor
						skillratio += 50*skill_lv;
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_WATER )
							skillratio += 25 * sd->charmball;
						break;
					case NJ_HUUJIN:// Wind Blade
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_WIND )
							skillratio += 20 * sd->charmball;
						break;
					case NJ_RAIGEKISAI:// Lightning Strike of Destruction
						skillratio += 60 + 40*skill_lv;
						if( sd && sd->charmball > 0 && sd->charmball_type == CHARM_WIND )
							skillratio += 15 * sd->charmball;
						break;
					case NJ_KAMAITACHI:// Kamaitachi
						skillratio += 100 * skill_lv;
						if (sd && sd->charmball > 0 && sd->charmball_type == CHARM_WIND)
							skillratio += 10 * sd->charmball;
						break;
					case NPC_ENERGYDRAIN:
						skillratio += 100*skill_lv;
						break;
					case NPC_EARTHQUAKE:
						skillratio += 100 + 100*skill_lv + 100*(skill_lv/2);
						break;

					case AB_JUDEX:
						skillratio = 300 + 70 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case AB_ADORAMUS:
						skillratio = 330 + 70 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case AB_RENOVATIO:
						if( sd )
							skillratio = (int)((15*sd->status.base_level)+(1.5*sd->status.int_));//Damage calculation from iRO wiki. [Jobbie]
						break;
					case AB_DUPLELIGHT_MAGIC:
						skillratio = 400 + 40 * skill_lv;
						break;

					case WL_SOULEXPANSION:
						skillratio += 300 + 100 * skill_lv + sstatus->int_;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_FROSTMISTY:
						skillratio += 100 + 100 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_JACKFROST:
						if (tsc && tsc->data[SC_FROST])
						{
							skillratio += 900 + 300 * skill_lv;
							if (level_effect_bonus == 1)
								skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						}
						else
						{
							skillratio += 400 + 100 * skill_lv;
							if (level_effect_bonus == 1)
								skillratio = skillratio * status_get_base_lv_effect(src) / 150;
						}
						break;
					case WL_DRAINLIFE:
						skillratio = 200 * skill_lv + sstatus->int_;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_CRIMSONROCK:
						skillratio = 300 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						skillratio += 1300;
						break;
					case WL_HELLINFERNO:
						if (s_ele == ELE_DARK)
							skillratio = 240 * skill_lv;
						else
							skillratio = 60 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_COMET:
						i = (sc ? distance_xy(target->x, target->y, sc->comet_x, sc->comet_y) : 8);
						if( i < 4 ) skillratio = 2500 + 500 * skill_lv;
						else
						if( i < 6 ) skillratio = 2000 + 500 * skill_lv;
						else
						if( i < 8 ) skillratio = 1500 + 500 * skill_lv;
						else
						skillratio = 1000 + 500 * skill_lv;
						break;
					case WL_CHAINLIGHTNING_ATK:
						skillratio += 400 + 100 * skill_lv + 500;//Will replace the 500 with the bounce part to the formula soon. [Rytech]
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_EARTHSTRAIN:
						skillratio += 1900 + 100 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case WL_TETRAVORTEX_FIRE:
					case WL_TETRAVORTEX_WATER:
					case WL_TETRAVORTEX_WIND:
					case WL_TETRAVORTEX_GROUND:
						skillratio += 400 + 500 * skill_lv;
						break;
					case WL_SUMMON_ATK_FIRE:
					case WL_SUMMON_ATK_WATER:
					case WL_SUMMON_ATK_WIND:
					case WL_SUMMON_ATK_GROUND:
						skillratio = (1 + skill_lv) / 2 * (status_get_base_lv_effect(src) + status_get_job_lv_effect(src));
						if (level_effect_bonus == 1)
							skillratio = skillratio * (200 + status_get_base_lv_effect(src) - 100) / 200;
						break;
					case LG_SHIELDSPELL:
					if ( sd && skill_lv == 2 )
						skillratio = 4 * status_get_base_lv_effect(src) + 100 * sd->bonus.shieldmdef + 2 * sstatus->int_;
					else
						skillratio = 0;//Prevents MATK damage from being done on LV 1 usage since LV 1 us ATK. [Rytech]
					break;
					case LG_RAYOFGENESIS:
						skillratio = 300 * skill_lv;
						if (sc && sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1)
							skillratio += 200 * sc->data[SC_BANDING]->val2;
						if (sc && sc->data[SC_INSPIRATION])
							skillratio += 600;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_job_lv_effect(src) / 25;
						break;
					case WM_METALICSOUND:
						skillratio = 120 * skill_lv + 60 * (sd ? pc_checkskill(sd, WM_LESSON) : 10);
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (tsc && (tsc->data[SC_SLEEP] || tsc->data[SC_DEEPSLEEP]))
							skillratio += skillratio * 50 / 100;
						break;
					case WM_REVERBERATION_MAGIC:
						skillratio += 100 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (mflag <= 0)// mflag holds the number of enemy targets in the AoE to divide the damage between.
							mflag = 1;// Make sure its always at least 1 to avoid divide by 0 crash.
						skillratio = skillratio / mflag;
						break;
					case SO_FIREWALK:
						skillratio = 60 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_HEATER_OPTION])
							skillratio += sc->data[SC_HEATER_OPTION]->val2;
						break;
					case SO_ELECTRICWALK:
						skillratio = 60 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_BLAST_OPTION])
							skillratio += sc->data[SC_BLAST_OPTION]->val2;
						break;
					case SO_EARTHGRAVE:
						skillratio = sstatus->int_ * skill_lv + 200 * (sd ? pc_checkskill(sd, SA_SEISMICWEAPON) : 5);
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += sc->data[SC_CURSED_SOIL_OPTION]->val2;
						break;
					case SO_DIAMONDDUST:
						skillratio = sstatus->int_ * skill_lv + 200 * (sd ? pc_checkskill(sd, SA_FROSTWEAPON) : 5);
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_COOLER_OPTION])
							skillratio += sc->data[SC_COOLER_OPTION]->val2;
						break;
					case SO_POISON_BUSTER:
						skillratio += 900 + 300 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 120;
						if (sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += sc->data[SC_CURSED_SOIL_OPTION]->val2;
						break;
					case SO_PSYCHIC_WAVE:
						skillratio = 70 * skill_lv + 3 * sstatus->int_;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case SO_CLOUD_KILL:
						skillratio = 40 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_CURSED_SOIL_OPTION])
							skillratio += sc->data[SC_CURSED_SOIL_OPTION]->val3;
						break;
					case SO_VARETYR_SPEAR:
						skillratio = sstatus->int_ * skill_lv + 50 * (sd ? pc_checkskill(sd, SA_LIGHTNINGLOADER) : 5);
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						if (sc && sc->data[SC_BLAST_OPTION])
							skillratio += sc->data[SC_BLAST_OPTION]->val3;
						break;
					case GN_DEMONIC_FIRE:
						if ( skill_lv > 20 )// Fire Expansion Level 2
							skillratio += 10 + 20 * (skill_lv - 20) + 10 * sstatus->int_;
						else if ( skill_lv > 10 )// Fire Expansion Level 1
							skillratio += 10 + 20 * (skill_lv - 10) + sstatus->int_ + status_get_job_lv_effect(src);
						else// Normal Demonic Fire Damage
							skillratio += 10 + 20 * skill_lv;
						break;
					case SP_CURSEEXPLOSION:
						if ( tsc && tsc->data[SC_CURSE] )
							skillratio = 1500 + 200 * skill_lv;
						else
							skillratio = 400 + 100 * skill_lv;
						break;
					case SP_SPA:
						skillratio = 500 + 250 * skill_lv;
						if( level_effect_bonus == 1 )
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case SP_SHA:
						skillratio = 5 * skill_lv;
						break;
					case SP_SWHOO:
						skillratio = 1100 + 200 * skill_lv;
						if( level_effect_bonus == 1 )
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case KO_KAIHOU:
						skillratio = 200 * (sd ? sd->charmball_old : 10);
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					case SU_SV_STEMSPEAR:
						skillratio = 700;
						break;
					case SU_CN_METEOR:
					case SU_CN_METEOR2:
						skillratio = 200 + 100 * skill_lv;
						break;
					case MH_POISON_MIST:
						skillratio = 40 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 100;
						break;
					//Lv 1 - 600, Lv 2 - 1000, Lv 3 - 800, Lv 4 1200, Lv 5 - 1000
					case MH_ERASER_CUTTER:
						if ( skill_lv % 2 == 0 )//Even levels 2, 4, 6, 8, etc.
							skillratio = 800 + 100 * skill_lv;
						else//Odd levels 1, 3, 5, 7, etc.
							skillratio = 500 + 100 * skill_lv;
						break;
					//Lv 1 - 500, Lv 2 - 700, Lv 3 - 600, Lv 4 900, Lv 5 - 700
					case MH_XENO_SLASHER:
						if ( skill_lv % 2 == 0 )//Even levels 2, 4, 6, 8, etc.
							skillratio = 500 + 100 * skill_lv;
						else//Odd levels 1, 3, 5, 7, etc.
							skillratio = 450 + 50 * skill_lv;
						break;
					case MH_HEILIGE_STANGE:
						skillratio = 500 + 250 * skill_lv;
						if (level_effect_bonus == 1)
							skillratio = skillratio * status_get_base_lv_effect(src) / 150;
						break;
					case EL_FIRE_MANTLE:
						skillratio = 1000;
						break;
					case EL_FIRE_ARROW:
					case EL_ROCK_CRUSHER_ATK:
						skillratio = 300;
						break;
					case EL_FIRE_BOMB:
					case EL_ICE_NEEDLE:
					case EL_HURRICANE_ATK:
						skillratio = 500;
						break;
					case EL_FIRE_WAVE:
					case EL_TYPOON_MIS_ATK:
						skillratio = 1200;
						break;
					case EL_WATER_SCREW:
					case EL_WATER_SCREW_ATK:
						skillratio = 1000;
						break;
					case EL_STONE_RAIN:
						skillratio = 900;
						break;
				}

				if ( sc )
				{// Insignia's increases the damage of offensive magic by a fixed percentage depending on the element.
					if ((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3 && s_ele == ELE_FIRE) ||
						(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 3 && s_ele == ELE_WATER) ||
						(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 3 && s_ele == ELE_WIND) ||
						(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3 && s_ele == ELE_EARTH))
						skillratio += 25;
				}

				// The code below is disabled until I can get more accurate information on the skills that insignias
				// increase the damage on. Right now it appears that certain skills get the increase, but need more info.
				/*if ( sc )
				{// Insignia's appear to only increase the damage of certain magic skills and not all of a certain
				//  element like it claims to. Need a confirm on Ninja skills for sure. [Rytech]
					if (( sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3 && (
						skill_id == MG_FIREBALL ||
						skill_id == MG_FIREWALL ||
						skill_id == MG_FIREBOLT ||
						skill_id == WZ_FIREPILLAR ||
						skill_id == WZ_SIGHTRASHER ||
						skill_id == WZ_METEOR ||
						skill_id == WL_CRIMSONROCK ||
						skill_id == WL_HELLINFERNO ||
						skill_id == WL_SUMMON_ATK_FIRE ||
						skill_id == SO_FIREWALK ))
						||
					( sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 3 && (
						skill_id == MG_COLDBOLT ||
						skill_id == MG_FROSTDIVER ||
						skill_id == WZ_WATERBALL ||
						skill_id == WZ_FROSTNOVA ||
						skill_id == WZ_STORMGUST ||
						skill_id == WL_FROSTMISTY ||
						skill_id == WL_JACKFROST ||
						skill_id == WL_SUMMON_ATK_WATER ||
						skill_id == SO_DIAMONDDUST ))
						||
					( sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 3 && (
						skill_id == MG_LIGHTNINGBOLT ||
						skill_id == MG_THUNDERSTORM ||
						skill_id == WZ_JUPITEL ||
						skill_id == WZ_VERMILION ||
						skill_id == WL_CHAINLIGHTNING_ATK ||
						skill_id == WL_SUMMON_ATK_WIND ||
						skill_id == SO_ELECTRICWALK ||
						skill_id == SO_VARETYR_SPEAR ))
						||
					( sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3 && (
						skill_id == WZ_EARTHSPIKE ||
						skill_id == WZ_HEAVENDRIVE ||
						skill_id == WL_EARTHSTRAIN ||
						skill_id == WL_SUMMON_ATK_GROUND ||
						skill_id == SO_EARTHGRAVE )))
						skillratio += 25;
				}*/

				MATK_RATE(skillratio);
			
				//Constant/misc additions from skills
				if (skill_id == WZ_FIREPILLAR)
					MATK_ADD(100 + 50 * skill_lv);
			}
		}

		if(sd) {
			//Damage bonuses
			if ((i = pc_skillatk_bonus(sd, skill_id)))
				ad.damage += ad.damage*i/100;

			//Ignore Defense?
			if (!flag.imdef && (
				sd->bonus.ignore_mdef_ele & (1 << tstatus->def_ele) || sd->bonus.ignore_mdef_ele & (1 << ELE_ALL) ||
				sd->bonus.ignore_mdef_race & (1 << tstatus->race) || sd->bonus.ignore_mdef_race & (1 << RC_ALL) ||
				sd->bonus.ignore_mdef_class & (1 << tstatus->class_) || sd->bonus.ignore_mdef_class & (1 << CLASS_ALL)
			))
				flag.imdef = 1;
		}

		if(!flag.imdef){
			short mdef = tstatus->mdef;
			int mdef2= tstatus->mdef2;
			if(sd)
			{
				i = sd->ignore_mdef_by_race[tstatus->race] + sd->ignore_mdef_by_race[RC_ALL];
				i += sd->ignore_mdef_by_class[tstatus->class_] + sd->ignore_mdef_by_class[CLASS_ALL];
				if (i)
				{
					if (i > 100) i = 100;
					mdef -= mdef * i/100;
					//mdef2-= mdef2* i/100;
				}
			}

			if ( sc && sc->data[SC_EXPIATIO] )
			{
				mdef -= mdef * sc->data[SC_EXPIATIO]->val2 / 100;
			}

			if(battle_config.magic_defense_type)
				ad.damage = ad.damage - mdef*battle_config.magic_defense_type - mdef2;
			else
				ad.damage = ad.damage * (100-mdef)/100 - mdef2;
		}
		
		if (skill_id == NPC_EARTHQUAKE)
		{	//Adds atk2 to the damage, should be influenced by number of hits and skill-ratio, but not mdef reductions. [Skotlex]
			//Also divide the extra bonuses from atk2 based on the number in range [Kevin]
			if(mflag>0)
				ad.damage+= (sstatus->rhw.atk2*skillratio/100)/mflag;
			else
				ShowError("Zero range by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
		}

		if(ad.damage<1)
			ad.damage=1;

		if (!(nk&NK_NO_ELEFIX))
			ad.damage=battle_attr_fix(src, target, ad.damage, s_ele, tstatus->def_ele, tstatus->ele_lv);

		//Apply the physical part of the skill's damage. [Skotlex]
		switch (skill_id)
		{
			case CR_GRANDCROSS:
			case NPC_GRANDDARKNESS:
				{
					struct Damage wd = battle_calc_weapon_attack(src, target, skill_id, skill_lv, mflag);

					ad.damage = battle_attr_fix(src, target, wd.damage + ad.damage, s_ele, tstatus->def_ele, tstatus->ele_lv) * (100 + 40 * skill_lv) / 100;
					if (src == target) {
						if (src->type == BL_PC)
							ad.damage = ad.damage / 2;
						else
							ad.damage = 0;
					}
				}
				break;
			case SO_VARETYR_SPEAR:
				{
					struct Damage wd = battle_calc_weapon_attack(src, target, skill_id, skill_lv, mflag);

					ad.damage += wd.damage;
				}
				break;
		}

		ad.damage += battle_calc_cardfix(BF_MAGIC, src, target, nk, s_ele, 0, ad.damage, 0, ad.flag);
	} //Hint: Against plants damage will still be 1 at this point
	
	//Apply DAMAGE_DIV_FIX and check for min damage
	ad = battle_apply_div_fix(ad);

	switch (skill_id) { // These skills will do a GVG fix later
	default:
		ad.damage = battle_calc_damage(src, target, &ad, ad.damage, skill_id, skill_lv);
		if (map_flag_gvg2(target->m))
			ad.damage = battle_calc_gvg_damage(src, target, ad.damage, skill_id, ad.flag);
		else if (map[target->m].flag.battleground)
			ad.damage = battle_calc_bg_damage(src, target, ad.damage, skill_id, ad.flag);
	}

	//battle_do_reflect(BF_MAGIC,&ad, src, target, skill_id, skill_lv); //WIP - Magic skill has own handler at skill_attack
	return ad;
}

/*==========================================
 * その他ダ??[ジ計算
 *------------------------------------------*/
struct Damage battle_calc_misc_attack(struct block_list *src,struct block_list *target,int skill_id,int skill_lv,int mflag)
{
	short i, nk;
	short s_ele;
	bool level_effect_bonus = battle_config.renewal_level_effect_skills;// Base/Job level effect on formula's.

	struct map_session_data *sd, *tsd;
	struct Damage md; //DO NOT CONFUSE with md of mob_data!
	struct status_change *sc = status_get_sc(src);
	struct status_change *tsc = status_get_sc(target);
	struct status_data *sstatus = status_get_status_data(src);
	struct status_data *tstatus = status_get_status_data(target);

	memset(&md,0,sizeof(md));

	nullpo_retr(md, src);
	nullpo_retr(md, target);

	//Some initial values
	md.amotion=skill_get_inf(skill_id)&INF_GROUND_SKILL?0:sstatus->amotion;
	md.dmotion=tstatus->dmotion;
	md.div_=skill_get_num( skill_id,skill_lv );
	md.blewcount=skill_get_blewcount(skill_id,skill_lv);
	md.dmg_lv=ATK_DEF;
	md.flag=BF_MISC|BF_SKILL;

	nk = skill_get_nk(skill_id);

	if (sc && !sc->count)
		sc = NULL; //Skip checking as there are no status changes active.
	if (tsc && !tsc->count)
		tsc = NULL; //Skip checking as there are no status changes active.
	
	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	
	if(sd) {
		sd->state.arrow_atk = 0;
		md.blewcount += battle_blewcount_bonus(sd, skill_id);
	}

	s_ele = skill_get_ele(skill_id, skill_lv);
	if (s_ele < 0 && s_ele != -3) //Attack that takes weapon's element for misc attacks? Make it neutral [Skotlex]
		s_ele = ELE_NEUTRAL;
	else if (s_ele == -3) { //Use random element
		if (skill_id == SN_FALCONASSAULT) {
			if (sstatus->rhw.ele && !status_get_attack_sc_element(src, status_get_sc(src)))
				s_ele = sstatus->rhw.ele;
			else
				s_ele = status_get_attack_sc_element(src, status_get_sc(src));
		}
		else
			s_ele = rnd() % ELE_ALL;
	}

	//Skill Range Criteria
	md.flag |= battle_range_type(src, target, skill_id, skill_lv);

	switch( skill_id )
	{
	case HT_LANDMINE:
	case MA_LANDMINE:
		md.damage=skill_lv*(sstatus->dex+75)*(100+sstatus->int_)/100;
		break;
	case HT_BLASTMINE:
		md.damage=skill_lv*(sstatus->dex/2+50)*(100+sstatus->int_)/100;
		break;
	case HT_CLAYMORETRAP:
		md.damage=skill_lv*(sstatus->dex/2+75)*(100+sstatus->int_)/100;
		break;
	case HT_BLITZBEAT:
	case SN_FALCONASSAULT:
	{
		uint8 skill;
		//Blitz-beat Damage.
		if (!sd || (skill = pc_checkskill(sd, HT_STEELCROW)) <= 0)
			skill = 0;
		md.damage = (sstatus->dex / 10 + sstatus->int_ / 2 + skill * 3 + 40) * 2;
		if (mflag > 1) //Autocasted Blitz.
			nk |= NK_SPLASHSPLIT;

		if (skill_id == SN_FALCONASSAULT)
		{
			//Div fix of Blitzbeat
			DAMAGE_DIV_FIX2(md.damage, skill_get_num(HT_BLITZBEAT, 5));

			//Falcon Assault Modifier
			md.damage = md.damage*(150 + 70 * skill_lv) / 100;
		}
		break;
	}
	case TF_THROWSTONE:
		md.damage=50;
		break;
	case BA_DISSONANCE:
		md.damage=30+skill_lv*10;
		if (sd)
			md.damage+= 3*pc_checkskill(sd,BA_MUSICALLESSON);
		break;
	case NPC_SELFDESTRUCTION:
		md.damage = sstatus->hp;
		break;
	case NPC_SMOKING:
		md.damage=3;
		break;
	case NPC_DARKBREATH:
		md.damage = tstatus->max_hp * (skill_lv * 10) / 100;
		break;
	case PA_PRESSURE:
		md.damage=500+300*skill_lv;
		break;
	case PA_GOSPEL:
		md.damage = 1+rnd()%9999;
		break;
	case CR_ACIDDEMONSTRATION: // updated the formula based on a Japanese formula found to be exact [Reddozen]
		if(tstatus->vit+sstatus->int_) //crash fix
			md.damage = (int)((int64)7*tstatus->vit*sstatus->int_*sstatus->int_ / (10*(tstatus->vit+sstatus->int_)));
		else
			md.damage = 0;
		if (tsd) md.damage>>=1;
		break;
	case NJ_ZENYNAGE:
		md.damage = skill_get_zeny(skill_id ,skill_lv);
		if (!md.damage) md.damage = 2;
		md.damage = md.damage + rnd()%md.damage;
		if (is_boss(target))
			md.damage=md.damage/3;
		else if (tsd)
			md.damage=md.damage/2;
		break;
	case GS_FLING:
		md.damage = status_get_job_lv(src);
		break;
	case HVAN_EXPLOSION:	//[orn]
		md.damage = (int64)sstatus->max_hp * (50 + 50 * skill_lv) / 100 ;
		break ;
	case ASC_BREAKER:
		md.damage = 500+rnd()%500 + 5*skill_lv * sstatus->int_;
		nk|=NK_IGNORE_FLEE|NK_NO_ELEFIX; //These two are not properties of the weapon based part.
		break;
	case HW_GRAVITATION:
		md.damage = 200+200*skill_lv;
		md.dmotion = 0; //No flinch animation.
		break;
	case NPC_EVILLAND:
		md.damage = skill_calc_heal(src,target,skill_id,skill_lv,false);
		break;
	case RA_CLUSTERBOMB:
	case RA_FIRINGTRAP:
	case RA_ICEBOUNDTRAP:
		md.damage = skill_lv * sstatus->dex + 5 * sstatus->int_;
		if (level_effect_bonus == 1)
			md.damage = md.damage * 150 / 100 + md.damage * status_get_base_lv_effect(src) / 100;
		else
			md.damage = md.damage * 250 / 100;
		md.damage = md.damage * (20 * (sd ? pc_checkskill(sd, RA_RESEARCHTRAP) : 5)) / 50;
		break;
	case WM_SOUND_OF_DESTRUCTION:
		md.damage = 1000 * skill_lv + sstatus->int_ * (sd ? pc_checkskill(sd, WM_LESSON) : 10);
		md.damage += md.damage * ( 10 * skill_chorus_count(sd)) / 100;
		break;
	//case WM_SATURDAY_NIGHT_FEVER://Test me in official if possiable.
	//	md.damage = 9999;//To enable when I figure how it exactly applies the damage. For now clif damage will deal 9999 damage and display it. [Rytech]
	//	break;
	case GN_THORNS_TRAP:
		md.damage = 100 + 200 * skill_lv + sstatus->int_;
		break;
	case GN_BLOOD_SUCKER:
		md.damage = 200 + 100 * skill_lv + sstatus->int_;
		break;
	case GN_HELLS_PLANT_ATK:
		md.damage = 10 * skill_lv * status_get_base_lv_effect(target) + 7 * sstatus->int_ / 2 * (status_get_job_lv_effect(src) / 4 + 18) * 5 / (10 - (sd ? pc_checkskill(sd, AM_CANNIBALIZE) : 5));
		break;
	case SP_SOULEXPLOSION:
		md.damage = tstatus->hp * (20 + 10 * skill_lv) / 100;
		break;
	case SJ_NOVAEXPLOSING:
		{
			short hp_skilllv = skill_lv;

			if ( hp_skilllv > 5 )
				hp_skilllv = 5;// Prevents dividing the MaxHP by 0 on levels higher then 5.

			// (Base ATK + Weapon ATK) * Ratio
			md.damage = (sstatus->batk + sstatus->rhw.atk) * (200 + 100 * skill_lv) / 100;

			// Additional Damage
			md.damage += sstatus->max_hp / (6 - hp_skilllv) + status_get_max_sp(src) * (2 * skill_lv);
		}
		break;
	case KO_MUCHANAGE:
		md.damage = skill_get_zeny(skill_id, skill_lv);
		if (!md.damage) md.damage = 10;
			md.damage =  md.damage * rnd_value( 50, 100) / 100;// Random damage should be calculated on skill use and then sent here. But can't do so through the flag due to the split damage flag.
		if ( (sd?pc_checkskill(sd, NJ_TOBIDOUGU):10) < 10 )// Best to just do all the damage calculations here for now and figure it out later. [Rytech]
			md.damage = md.damage / 2;// Damage halved if Throwing Mastery is not mastered.
		if (is_boss(target))// Data shows damage is halved on boss monsters but not on players.
			md.damage = md.damage / 2;
 		break;
	case KO_HAPPOKUNAI:
		{
			struct Damage wd = battle_calc_attack(BF_WEAPON, src, target, 0, 1, mflag);

			short totaldef = tstatus->def2 + (short)status_get_def(target);

			md.damage = 3 * wd.damage * (5 + skill_lv) / 5;
			if ( sd ) wd.damage += sd->bonus.arrow_atk;
				md.damage = (int)(3 * (1 + wd.damage) * (5 + skill_lv) / 5.0f);
				md.damage -= totaldef;
		}
		break;
	case NC_MAGMA_ERUPTION_DOTDAMAGE:
		md.damage = 800 + 200 * skill_lv;
		break;
	case SU_SV_ROOTTWIST_ATK:
		md.damage = 100;
		break;
	case MH_EQC:
		md.damage = tstatus->hp - sstatus->hp;
		// Officially, if damage comes out <= 0,
		// the damage will equal to the homunculus MaxHP.
		// Bug? I think so and im not adding that.
		if (md.damage < 0)
			md.damage = 1;
		break;

	case RL_B_TRAP:
		md.damage = (200 + status_get_int(src) + status_get_dex(src)) * skill_lv * 10; //(custom)
		break;
	}

	if (nk&NK_SPLASHSPLIT){ // Divide ATK among targets
		if(mflag>0)
			md.damage/= mflag;
		else
			ShowError("0 enemies targeted by %d:%s, divide per 0 avoided!\n", skill_id, skill_get_name(skill_id));
	}
	
	if (!(nk&NK_IGNORE_FLEE))
	{
		struct status_change *sc = status_get_sc(target);
		i = 0; //Temp for "hit or no hit"
		if(sc && sc->opt1 && sc->opt1 != OPT1_STONEWAIT && sc->opt1 != OPT1_BURNING)
			i = 1;
		else {
			short
				flee = tstatus->flee,
				hitrate=80; //Default hitrate

			if(battle_config.agi_penalty_type && 
				battle_config.agi_penalty_target&target->type)
			{	
				unsigned char attacker_count; //256 max targets should be a sane max
				attacker_count = unit_counttargeted(target);
				if (attacker_count >= battle_config.agi_penalty_count) {
					if (battle_config.agi_penalty_type == 1)
						flee = (flee * (100 - (attacker_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num))/100;
					else //asume type 2: absolute reduction
						flee -= (attacker_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
					if(flee < 1) flee = 1;
				}
			}

			hitrate+= sstatus->hit - flee;
			hitrate = cap_value(hitrate, battle_config.min_hitrate, battle_config.max_hitrate);

			if(rnd()%100 < hitrate)
				i = 1;
		}
		if (!i) {
			md.damage = 0;
			md.dmg_lv=ATK_FLEE;
		}
	}

	md.damage += battle_calc_cardfix(BF_MISC, src, target, nk, s_ele, 0, md.damage, 0, md.flag);

	if (sd && (i = pc_skillatk_bonus(sd, skill_id)))
		md.damage += md.damage*i/100;

	if (!(nk&NK_NO_ELEFIX))
		md.damage = battle_attr_fix(src, target, md.damage, s_ele, tstatus->def_ele, tstatus->ele_lv);

	//Plant damage
	if(md.damage < 0)
		md.damage = 0;
	else if (md.damage && is_infinite_defense(target, md.flag))
		md.damage = 1;

	//Apply DAMAGE_DIV_FIX and check for min damage
	md = battle_apply_div_fix(md);

	md.damage = battle_calc_damage(src,target,&md,md.damage,skill_id,skill_lv);
	if( map_flag_gvg2(target->m) )
		md.damage = battle_calc_gvg_damage(src,target,md.damage,skill_id,md.flag);
	else if( map[target->m].flag.battleground )
		md.damage = battle_calc_bg_damage(src,target,md.damage,skill_id,md.flag);

	switch( skill_id )
	{
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
 		case RA_ICEBOUNDTRAP:
			{
				struct Damage wd;

				wd = battle_calc_weapon_attack(src,target,skill_id,skill_lv,mflag);
				md.damage += wd.damage;
			}
			break;
		case NJ_ZENYNAGE:
			if( sd )
			{
				if ( md.damage > sd->status.zeny )
					md.damage = sd->status.zeny;
				pc_payzeny(sd, (int)cap_value(md.damage, INT_MIN, INT_MAX), LOG_TYPE_CONSUME, NULL);
			}
			break;
	}

	battle_do_reflect(BF_MISC, &md, src, target, skill_id, skill_lv); //WIP

	return md;
}
/*==========================================
 * Battle main entry, from skill_attack
 *------------------------------------------*/
struct Damage battle_calc_attack(int attack_type,struct block_list *bl,struct block_list *target,int skill_id,int skill_lv,int count)
{
	struct Damage d;
	switch(attack_type) {
	case BF_WEAPON: d = battle_calc_weapon_attack(bl,target,skill_id,skill_lv,count); break;
	case BF_MAGIC:  d = battle_calc_magic_attack(bl,target,skill_id,skill_lv,count);  break;
	case BF_MISC:   d = battle_calc_misc_attack(bl,target,skill_id,skill_lv,count);   break;
	default:
		ShowError("battle_calc_attack: unknown attack type! %d (skill_id=%d, skill_lv=%d)\n", attack_type, skill_id, skill_lv);
		memset(&d,0,sizeof(d));
		break;
	}

	if( d.damage + d.damage2 < 1 )
	{	//Miss/Absorbed
		//Weapon attacks should go through to cause additional effects.
		if (d.dmg_lv == ATK_DEF /*&& attack_type&(BF_MAGIC|BF_MISC)*/) // Isn't it that additional effects don't apply if miss?
			d.dmg_lv = ATK_MISS;
		d.dmotion = 0;
	}
	else // Some skills like Weaponry Research will cause damage even if attack is dodged
		d.dmg_lv = ATK_DEF;
	return d;
}

//Calculates BF_WEAPON returned damage.
int64 battle_calc_return_damage(struct block_list* bl, struct block_list *src, int64 *dmg, int flag, int skill_id, bool status_reflect) {
	struct map_session_data* sd;
	int64 rdamage = 0, damage = *dmg;
	int max_damage = status_get_max_hp(bl);
	struct status_change* sc;

	sd = BL_CAST(BL_PC, bl);
	sc = status_get_sc(bl);

	if (flag & BF_SHORT) {//Bounces back part of the damage.
		if (!status_reflect && sd && sd->bonus.short_weapon_damage_return) {
			rdamage += damage * sd->bonus.short_weapon_damage_return / 100;
			if (rdamage < 1) rdamage = 1;
		}
		else if (status_reflect && sc && sc->count) {
			if (sc->data[SC_LG_REFLECTDAMAGE] && !(skill_get_inf2(skill_id)&INF2_TRAP)) {
				if (rnd() % 100 <= sc->data[SC_LG_REFLECTDAMAGE]->val1 * 10 + 30) {
					max_damage = (int64)max_damage * status_get_base_lv_effect(bl) / 100;
					rdamage = (*dmg) * sc->data[SC_LG_REFLECTDAMAGE]->val2 / 100;
					if (--(sc->data[SC_LG_REFLECTDAMAGE]->val3) < 1)
						status_change_end(bl, SC_LG_REFLECTDAMAGE, INVALID_TIMER);
				}
			}
			else {
				if (sc->data[SC_REFLECTSHIELD] && skill_id != WS_CARTTERMINATION) {
					// Don't reflect non-skill attack if has SC_REFLECTSHIELD from Devotion bonus inheritance
					if (!skill_id && battle_config.devotion_rdamage_skill_only && sc->data[SC_REFLECTSHIELD]->val4)
						rdamage = 0;
					else {
						rdamage += damage * sc->data[SC_REFLECTSHIELD]->val2 / 100;
						if (rdamage < 1)
							rdamage = 1;
					}
				}

				if (sc->data[SC_DEATHBOUND] && skill_id != WS_CARTTERMINATION && !(src->type == BL_MOB && is_boss(src))) {
					uint8 dir = map_calc_dir(bl, src->x, src->y),
						t_dir = unit_getdir(bl);

					if (distance_bl(src, bl) <= 0 || !map_check_dir(dir, t_dir)) {
						int64 rd1 = 0;
						rd1 = min(damage, status_get_max_hp(bl)) * sc->data[SC_DEATHBOUND]->val2 / 100; // Amplify damage.
						*dmg = rd1 * 30 / 100; // Received damage = 30% of amplifly damage.
						clif_skill_damage(src, bl, gettick(), status_get_amotion(src), 0, -30000, 1, RK_DEATHBOUND, sc->data[SC_DEATHBOUND]->val1, 6);
						status_change_end(bl, SC_DEATHBOUND, INVALID_TIMER);
						rdamage += rd1 * 70 / 100; // Target receives 70% of the amplified damage. [Rytech]
					}
				}
				if (sc->data[SC_SHIELDSPELL_DEF] && sc->data[SC_SHIELDSPELL_DEF]->val1 == 2 && !(src->type == BL_MOB && is_boss(src))) {
					rdamage += damage * sc->data[SC_SHIELDSPELL_DEF]->val2 / 100;
					if (rdamage < 1) rdamage = 1;
				}
			}
		}
	}
	else {
		if (!status_reflect && sd && sd->bonus.long_weapon_damage_return) {
			rdamage += damage * sd->bonus.long_weapon_damage_return / 100;
			if (rdamage < 1) rdamage = 1;
		}
	}

	if (sc && sc->data[SC_KYOMU]) // Nullify reflecting ability
		rdamage = 0;

	return cap_value(min(rdamage, max_damage), INT_MIN, INT_MAX);
}

void battle_drain(struct map_session_data *sd, struct block_list *tbl, int64 rdamage, int64 ldamage, int race, int class_, bool infdef)
{
	struct weapon_data *wd;
	int64 *damage;
	int thp = 0, tsp = 0, hp = 0, sp = 0;
	uint8 i = 0;
	short vrate_hp = 0, vrate_sp = 0, v_hp = 0, v_sp = 0;

	if (!CHK_RACE(race) && !CHK_CLASS(class_))
		return;

	// Check for vanish HP/SP. !CHECKME: Which first, drain or vanish?
	vrate_hp = cap_value(sd->bonus.hp_vanish_rate + sd->hp_vanish_race[race].rate + sd->hp_vanish_race[RC_ALL].rate, SHRT_MIN, SHRT_MAX);
	v_hp = cap_value(sd->bonus.hp_vanish_per + sd->hp_vanish_race[race].per + sd->hp_vanish_race[RC_ALL].per, SHRT_MIN, SHRT_MAX);
	vrate_sp = cap_value(sd->bonus.sp_vanish_rate + sd->sp_vanish_race[race].rate + sd->sp_vanish_race[RC_ALL].rate, SHRT_MIN, SHRT_MAX);
	v_sp = cap_value(sd->bonus.sp_vanish_per + sd->sp_vanish_race[race].per + sd->sp_vanish_race[RC_ALL].per, SHRT_MIN, SHRT_MAX);
	if (v_hp > 0 && vrate_hp > 0 && (vrate_hp >= 10000 || rnd() % 10000 < vrate_hp))
		i |= 1;
	if (v_sp > 0 && vrate_sp > 0 && (vrate_sp >= 10000 || rnd() % 10000 < vrate_sp))
		i |= 2;
	if (i) {
		if (infdef)
			status_zap(tbl, v_hp ? v_hp / 100 : 0, v_sp ? v_sp / 100 : 0);
		else
			status_percent_damage(&sd->bl, tbl, (i & 1 ? (int8)(-v_hp) : 0), (i & 2 ? (int8)(-v_sp) : 0), false);
	}
	i = 0;

	for (i = 0; i < 4; i++) {
		//First two iterations: Right hand
		if (i < 2) { wd = &sd->right_weapon; damage = &rdamage; }
		else { wd = &sd->left_weapon; damage = &ldamage; }
		if (*damage <= 0) continue;
		if (i == 1 || i == 3) {
			hp = wd->hp_drain_class[class_] + wd->hp_drain_class[CLASS_ALL];
			hp += battle_calc_drain(*damage, wd->hp_drain_rate.rate, wd->hp_drain_rate.per);
			sp = wd->sp_drain_class[class_] + wd->sp_drain_class[CLASS_ALL];
			sp += battle_calc_drain(*damage, wd->sp_drain_rate.rate, wd->sp_drain_rate.per);

			if (hp) {
				//rhp += hp;
				thp += hp;
			}

			if (sp) {
				//rsp += sp;
				tsp += sp;
			}
		}
		else {
			hp = wd->hp_drain_race[race] + wd->hp_drain_race[RC_ALL];
			sp = wd->sp_drain_race[race] + wd->sp_drain_race[RC_ALL];

			if (hp) {
				//rhp += hp;
				thp += hp;
			}

			if (sp) {
				//rsp += sp;
				tsp += sp;
			}
		}
	}

	if (!thp && !tsp) return;

	status_heal(&sd->bl, thp, tsp, battle_config.show_hp_sp_drain?3:1);
	
	//if (rhp || rsp)
		//status_zap(tbl, rhp, rsp);
}

// Deals the same damage to targets in area. [pakpil]
int battle_damage_area( struct block_list *bl, va_list ap)
{
	int64 tick;
	int amotion, dmotion, damage, flag;
	struct block_list *src;

	nullpo_retr(0, bl);
	
	tick=va_arg(ap, int64);
	src=va_arg(ap,struct block_list *);
	amotion=va_arg(ap,int);
	dmotion=va_arg(ap,int);
	damage=va_arg(ap,int);
	flag = va_arg(ap, int);

	if (bl != src && battle_check_target(src, bl, BCT_ENEMY) > 0 && !(bl->type == BL_MOB && ((TBL_MOB*)bl)->mob_id == MOBID_EMPERIUM))
	{
		map_freeblock_lock();
		if (src->type == BL_PC)
			battle_drain((TBL_PC*)src, bl, damage, damage, status_get_race(bl), status_get_class_(bl), is_infinite_defense(bl, flag));
		if (amotion)
			battle_delay_damage(tick, amotion, src, bl, 0, CR_REFLECTSHIELD, 0, damage, ATK_DEF, 0, true);
		else
			status_fix_damage(src, bl, damage, 0);
		clif_damage(bl, bl, tick, amotion, dmotion, damage, 1, ATK_BLOCK, 0, false);
		skill_additional_effect(src, bl, CR_REFLECTSHIELD, 1, BF_WEAPON|BF_SHORT|BF_NORMAL,ATK_DEF, tick);
		map_freeblock_unlock();
	}
	
	return 0;
}

// Triggers aftercast delay for autocasted skills.
void battle_autocast_aftercast(struct block_list* src, short skill_id, short skill_lv, int64 tick)
{
	struct map_session_data *sd = NULL;
	struct unit_data *ud;
	int delay;

	sd = BL_CAST(BL_PC, src);
	ud = unit_bl2ud(src);

	if (ud)
	{
		delay = skill_delayfix(src, skill_id, skill_lv);
		if (DIFF_TICK(ud->canact_tick, tick + delay) < 0)
		{
			ud->canact_tick = tick + delay;
			if (battle_config.display_status_timers && sd && skill_get_delay(skill_id, skill_lv))
				clif_status_change(src, SI_ACTIONDELAY, 1, delay, 0, 0, 1);
		}
	}
}


/*==========================================
 * Do a basic physical attack (call through unit_attack_timer)
 *------------------------------------------*/
enum damage_lv battle_weapon_attack(struct block_list* src, struct block_list* target, int64 tick, int flag)
{
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct homun_data *hd = NULL;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *tsc;
	struct status_change_entry *sce;
	int64 damage;
	int skillv;
	struct Damage wd;

	nullpo_retr(ATK_NONE, src);
	nullpo_retr(ATK_NONE, target);

	if (src->prev == NULL || target->prev == NULL)
		return ATK_NONE;

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, target);
	hd = BL_CAST(BL_HOM, src);

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(target);

	sc = status_get_sc(src);
	tsc = status_get_sc(target);

	if (sc && !sc->count) //Avoid sc checks when there's none to check for. [Skotlex]
		sc = NULL;
	if (tsc && !tsc->count)
		tsc = NULL;

	if (sd)
	{
		sd->state.arrow_atk = (sd->status.weapon == W_BOW || (sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE));
		if (sd->state.arrow_atk)
		{
			int index = sd->equip_index[EQI_AMMO];
			if (index<0) {
				if (sd->weapontype1 > W_KATAR && sd->weapontype1 < W_HUUMA)
					clif_skill_fail(sd, 0, USESKILL_FAIL_NEED_MORE_BULLET, 0, 0);
				else
					clif_arrow_fail(sd, 0);
				return ATK_NONE;
			}
			//Ammo check by Ishizu-chan
			if (sd->inventory_data[index])
			switch (sd->status.weapon) {
			case W_BOW:
				if (sd->inventory_data[index]->look != A_ARROW) {
					clif_arrow_fail(sd,0);
					return ATK_NONE;
				}
			break;
			case W_REVOLVER:
			case W_RIFLE:
			case W_GATLING:
			case W_SHOTGUN:
				if (sd->inventory_data[index]->look != A_BULLET) {
					clif_skill_fail(sd, 0, USESKILL_FAIL_NEED_MORE_BULLET, 0, 0);
					return ATK_NONE;
				}
			break;
			case W_GRENADE:
				if (sd->inventory_data[index]->look != A_GRENADE) {
					clif_skill_fail(sd, 0, USESKILL_FAIL_NEED_MORE_BULLET, 0, 0);
					return ATK_NONE;
				}
			break;
			}
		}
	}

	if (sc && sc->count) 
	{
		if (sc->data[SC_CLOAKING] && !(sc->data[SC_CLOAKING]->val4 & 2))
			status_change_end(src, SC_CLOAKING, INVALID_TIMER);
		else if (sc->data[SC_CLOAKINGEXCEED] && !(sc->data[SC_CLOAKINGEXCEED]->val4 & 2))
			status_change_end(src, SC_CLOAKINGEXCEED, INVALID_TIMER);
		else if (sc->data[SC_NEWMOON] && (--sc->data[SC_NEWMOON]->val2) <= 0)
			status_change_end(src, SC_NEWMOON, INVALID_TIMER);
	}

	if( tsc && tsc->data[SC_AUTOCOUNTER] && status_check_skilluse(target, src, KN_AUTOCOUNTER, tsc->data[SC_AUTOCOUNTER]->val1, 1) ){
		int dir = map_calc_dir(target,src->x,src->y);
		int t_dir = unit_getdir(target);
		int dist = distance_bl(src, target);
		if(dist <= 0 || (!map_check_dir(dir,t_dir) && dist <= tstatus->rhw.range+1)){
			int skill_lv = tsc->data[SC_AUTOCOUNTER]->val1;
			clif_skillcastcancel(target); //Remove the casting bar. [Skotlex]
			clif_damage(src, target, tick, sstatus->amotion, 1, 0, 1, 0, 0, false); //Display MISS.
			status_change_end(target, SC_AUTOCOUNTER, INVALID_TIMER);
			skill_attack(BF_WEAPON,target,target,src,KN_AUTOCOUNTER,skill_lv,tick,0);
			return ATK_NONE;
		}
	}

	if( tsc && tsc->data[SC_BLADESTOP_WAIT] && !is_boss(src) && (src->type == BL_PC || tsd == NULL || distance_bl(src, target) <= (tsd->status.weapon == W_FIST ? 1U : 2U)) ){
		int skill_lv = tsc->data[SC_BLADESTOP_WAIT]->val1;
		int duration = skill_get_time2(MO_BLADESTOP,skill_lv);
		status_change_end(target, SC_BLADESTOP_WAIT, INVALID_TIMER);
		if(sc_start4(src, SC_BLADESTOP, 100, sd?pc_checkskill(sd, MO_BLADESTOP):5, 0, 0, target->id, duration))
	  	{	//Target locked.
			clif_damage(src, target, tick, sstatus->amotion, 1, 0, 1, 0, 0, false); //Display MISS.
			clif_bladestop(target, src->id, 1);
			sc_start4(target, SC_BLADESTOP, 100, skill_lv, 0, 0, src->id, duration);
			return ATK_NONE;
		}
	}

	if(sd && (skillv = pc_checkskill(sd,MO_TRIPLEATTACK)) > 0){
		int triple_rate= 30 - skillv; //Base Rate
		if (sc && sc->data[SC_SKILLRATE_UP] && sc->data[SC_SKILLRATE_UP]->val1 == MO_TRIPLEATTACK)
		{
			triple_rate+= triple_rate*(sc->data[SC_SKILLRATE_UP]->val2)/100;
			status_change_end(src, SC_SKILLRATE_UP, INVALID_TIMER);
		}
		if (rnd() % 100 < triple_rate) {
			//Need to apply canact_tick here because it doesn't go through skill_castend_id
			sd->ud.canact_tick = tick + skill_delayfix(src, MO_TRIPLEATTACK, skillv);
			if (skill_attack(BF_WEAPON, src, src, target, MO_TRIPLEATTACK, skillv, tick, 0))
				return ATK_DEF;
			return ATK_MISS;
		}
	}

	if (sc)
	{
		if (sc->data[SC_SACRIFICE])
		{
			int skill_lv = sc->data[SC_SACRIFICE]->val1;
			damage_lv ret_val;

			if( --sc->data[SC_SACRIFICE]->val2 <= 0 )
				status_change_end(src, SC_SACRIFICE, INVALID_TIMER);

			// We need to calculate the DMG before the hp reduction, because it can kill the source.
			ret_val = (damage_lv)skill_attack(BF_WEAPON,src,src,target,PA_SACRIFICE,skill_lv,tick,0);

			status_zap(src, sstatus->max_hp*9/100, 0);//Damage to self is always 9%

			if (ret_val == ATK_NONE)
				return ATK_MISS;

			return ret_val;
		}
		if (sc->data[SC_MAGICALATTACK]) {
			if (skill_attack(BF_MAGIC, src, src, target, NPC_MAGICALATTACK, sc->data[SC_MAGICALATTACK]->val1, tick, 0))
				return ATK_DEF;
			return ATK_MISS;
		}
		if ( sd && (sce = sc->data[SC_GENTLETOUCH_ENERGYGAIN]) && rnd()%100 < sce->val2 )
		{
			short spheremax = 5;

			if ( sc->data[SC_RAISINGDRAGON] )
				spheremax += sc->data[SC_RAISINGDRAGON]->val1;

			pc_addspiritball(sd, skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sce->val1), spheremax);
		}
		if ( hd && (sce = sc->data[SC_STYLE_CHANGE]) && sce->val1 == FIGHTER_STYLE && rnd()%100 < sce->val2 )
			merc_hom_addspiritball(hd,MAX_HOMUN_SPHERES);
	}

	if (tsc && tsc->data[SC_MTF_MLEATKED] && rnd() % 100 < tsc->data[SC_MTF_MLEATKED]->val2)
		clif_skill_nodamage(target, target, SM_ENDURE, tsc->data[SC_MTF_MLEATKED]->val1, status_change_start(src, target, SC_ENDURE, 10000, tsc->data[SC_MTF_MLEATKED]->val1, 0, 0, 0, skill_get_time(SM_ENDURE, tsc->data[SC_MTF_MLEATKED]->val1), 0));

	if (tsc && tsc->data[SC_KAAHI] && tstatus->hp < tstatus->max_hp && status_charge(target, 0, tsc->data[SC_KAAHI]->val3)) {
		int hp_heal = tstatus->max_hp - tstatus->hp;
		if (hp_heal > tsc->data[SC_KAAHI]->val2)
			hp_heal = tsc->data[SC_KAAHI]->val2;
		if (hp_heal)
			status_heal(target, hp_heal, 0, 2);
	}

	wd = battle_calc_attack( BF_WEAPON, src, target, 0, 0, flag );

	if(sc) 
	{
		if( sc->data[SC_SPELLFIST] ) 
		{
			if (--(sc->data[SC_SPELLFIST]->val1) >= 0) {
				wd = battle_calc_attack(BF_MAGIC, src, target, sc->data[SC_SPELLFIST]->val3, sc->data[SC_SPELLFIST]->val4, flag);
				DAMAGE_DIV_FIX(wd.damage, wd.div_); // Double the damage for multiple hits.
			}
			else
				status_change_end(src,SC_SPELLFIST,-1);
		}
	}

	// This skill once only triggered on regular attacks. But aegis code appears to trigger this on skill attacks too. Disable for now. [Rytech]
	/*if ( tsc && tsc->data[SC_CRESCENTELBOW] && !is_boss(src) && (wd.flag&BF_WEAPON && wd.flag&BF_SHORT) && rnd()%100 < tsc->data[SC_CRESCENTELBOW]->val2 )
	{
		// Ratio part of the damage is reduceable and affected by other means. Additional damage after that is not.
		struct Damage ced = battle_calc_weapon_attack(target, src, SR_CRESCENTELBOW_AUTOSPELL, tsc->data[SC_CRESCENTELBOW]->val1, 0);
		int elbow_damage = ced.damage + wd.damage * ( 100 + 20 * tsc->data[SC_CRESCENTELBOW]->val1 ) / 100;

		//clif_damage(src, target, tick, sstatus->amotion, 1, 0, 1, 0, 0); //Display MISS.

		// Attacker gets the full damage.
		clif_damage(target, src, tick, tstatus->amotion, 0, elbow_damage, 1, 4, 0);
		status_zap(src, elbow_damage, 0);

		// Caster takes 10% of the damage.
		clif_damage(target, target, tick, tstatus->amotion, 0, elbow_damage / 10, 1, 4, 0);
		status_zap(target, elbow_damage / 10, 0);

		// Activate the passive part of the skill to show skill animation, deal knockback, and do damage if pushed into a wall.
		skill_castend_nodamage_id(target, src, SR_CRESCENTELBOW_AUTOSPELL, tsc->data[SC_CRESCENTELBOW]->val1, tick, 0);

		return ATK_NONE;
	}*/

	if( sd && sc && sc->data[SC_GIANTGROWTH] && (wd.flag&BF_SHORT) && rnd()%100 < sc->data[SC_GIANTGROWTH]->val2 )
	{
		if ( battle_config.giant_growth_behavior == 1 )
			wd.damage *= 2;// Double Damage - 2017 Behavior
		else
			wd.damage *= 3;// Triple Damage - Pre 2017 Behavior
		
		// 0.1% Chance of breaking with each damage burst.
		skill_break_equip(src, EQP_WEAPON, 10, BCT_SELF);
	}

	if (sd && sd->state.arrow_atk) //Consume arrow.
		battle_consume_ammo(sd, 0, 0);

	damage = wd.damage + wd.damage2;
	if( damage > 0 && src != target )
	{
		
		if( sc && sc->data[SC_DUPLELIGHT] && (wd.flag&BF_SHORT) )
		{// Activates only from regular melee damage. Success chance is seperate for both duple light attacks.
			if ( rnd()%100 <= sc->data[SC_DUPLELIGHT]->val2 )
				skill_attack(skill_get_type(AB_DUPLELIGHT_MELEE), src, src, target, AB_DUPLELIGHT_MELEE, sc->data[SC_DUPLELIGHT]->val1, tick, SD_LEVEL);

			if ( rnd()%100 <= sc->data[SC_DUPLELIGHT]->val2 )
				skill_attack(skill_get_type(AB_DUPLELIGHT_MAGIC), src, src, target, AB_DUPLELIGHT_MAGIC, sc->data[SC_DUPLELIGHT]->val1, tick, SD_LEVEL);
		}
	}

	wd.dmotion = clif_damage(src, target, tick, wd.amotion, wd.dmotion, wd.damage, wd.div_ , wd.type, wd.damage2, wd.isspdamage);

	if (sd && sd->bonus.splash_range > 0 && damage > 0)
		skill_castend_damage_id(src, target, 0, 1, tick, 0);

	if (target->type == BL_SKILL && damage > 0) {
		TBL_SKILL *su = (TBL_SKILL*)target;
		if (su->group && su->group->skill_id == HT_BLASTMINE)
			skill_blown(src, target, 3, -1, 0);
	}

	map_freeblock_lock();

	battle_delay_damage(tick, wd.amotion, src, target, wd.flag, 0, 0, damage, wd.dmg_lv, wd.dmotion, true);

	if( tsc && tsc->data[SC_DEVOTION] )
	{
		struct status_change_entry *sce = tsc->data[SC_DEVOTION];
		struct block_list *d_bl = map_id2bl(sce->val1);

		if( d_bl && (
			(d_bl->type == BL_MER && ((TBL_MER*)d_bl)->master && ((TBL_MER*)d_bl)->master->bl.id == target->id) ||
			(d_bl->type == BL_PC && ((TBL_PC*)d_bl)->devotion[sce->val2] == target->id)
			) && check_distance_bl(target, d_bl, sce->val3) )
		{
			clif_damage(d_bl, d_bl, gettick(), 0, 0, damage, 0, 0, 0, false);
			status_fix_damage(NULL, d_bl, damage, 0);
		}
		else
			status_change_end(target, SC_DEVOTION, INVALID_TIMER);
	}

	if( tsc && tsc->data[SC_WATER_SCREEN_OPTION] )
	{
		struct status_change_entry *sce = tsc->data[SC_WATER_SCREEN_OPTION];
		struct block_list *d_bl = map_id2bl(sce->val1);

		if ( d_bl && (d_bl->type == BL_ELEM && ((TBL_ELEM*)d_bl)->master && ((TBL_ELEM*)d_bl)->master->bl.id == target->id) )
		{
			clif_damage(d_bl, d_bl, gettick(), 0, 0, damage, 0, 0, 0, false);
			status_fix_damage(NULL, d_bl, damage, 0);
		}
	}

	if (sc && sc->data[SC_AUTOSPELL] && rnd()%100 < sc->data[SC_AUTOSPELL]->val4) {
		int sp = 0;
		int skill_id = sc->data[SC_AUTOSPELL]->val2;
		int skill_lv = sc->data[SC_AUTOSPELL]->val3;
		int i = rnd()%100;
		
		if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_SAGE)
			i = 0; //Max chance, no skill_lv reduction. [Skotlex]
		//reduction only for skill_lv > 1
		if (skill_lv > 1) {
			if (i >= 50) skill_lv /= 2;
			else if (i >= 15) skill_lv--;
		}
		sp = skill_get_sp(skill_id,skill_lv) * 2 / 3;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, sp)) {
			switch (skill_get_casttype(skill_id)) {
				case CAST_GROUND:
					skill_castend_pos2(src, target->x, target->y, skill_id, skill_lv, tick, flag);
					break;
				case CAST_NODAMAGE:
					skill_castend_nodamage_id(src, target, skill_id, skill_lv, tick, flag);
					break;
				case CAST_DAMAGE:
					skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
					break;
			}

			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}
	if (wd.flag&BF_WEAPON && sc && sc->data[SC__AUTOSHADOWSPELL] && rnd() % 100 < sc->data[SC__AUTOSHADOWSPELL]->val4)
	{
		short skill_id = sc->data[SC__AUTOSHADOWSPELL]->val2;
		short skill_lv = sc->data[SC__AUTOSHADOWSPELL]->val3;
		
		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			switch (skill_get_casttype(skill_id))
		{
				case CAST_GROUND:
					skill_castend_pos2(src, target->x, target->y, skill_id, skill_lv, tick, flag);
					break;
				case CAST_NODAMAGE:
					skill_castend_nodamage_id(src, target, skill_id, skill_lv, tick, flag);
					break;
				case CAST_DAMAGE:
					skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
					break;
			}

			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if (wd.flag&BF_WEAPON && sc && sc->data[SC_FALLINGSTAR] && rnd() % 100 < sc->data[SC_FALLINGSTAR]->val2)
	{
		short skill_id = SJ_FALLINGSTAR_ATK;
		short skill_lv = sc->data[SC_FALLINGSTAR]->val1;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id, skill_lv)))
			skill_castend_nodamage_id(src, src, skill_id, skill_lv, tick, flag);
		if (sd) sd->state.autocast = 0;
	}


	if ((sd || hd && battle_config.homunculus_pyroclastic_autocast == 1) && wd.flag&BF_SHORT && sc && sc->data[SC_PYROCLASTIC] && rnd()%100 < sc->data[SC_PYROCLASTIC]->val3)
	{
		short skill_id = BS_HAMMERFALL;
		short skill_lv = sc->data[SC_PYROCLASTIC]->val1;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			skill_castend_pos2(src, target->x, target->y, skill_id, skill_lv, tick, flag);

			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if ( sc && sc->data[SC_TROPIC_OPTION] && rnd()%100 < sc->data[SC_TROPIC_OPTION]->val2 )
	{
		short skill_id = MG_FIREBOLT;
		short skill_lv = sc->data[SC_TROPIC_OPTION]->val3;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if ( sc && sc->data[SC_CHILLY_AIR_OPTION] && rnd()%100 < sc->data[SC_CHILLY_AIR_OPTION]->val2 )
	{
		short skill_id = MG_COLDBOLT;
		short skill_lv = sc->data[SC_CHILLY_AIR_OPTION]->val3;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if ( sc && sc->data[SC_WILD_STORM_OPTION] && rnd()%100 < sc->data[SC_WILD_STORM_OPTION]->val2 )
	{
		short skill_id = MG_LIGHTNINGBOLT;
		short skill_lv = sc->data[SC_WILD_STORM_OPTION]->val3;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if ( sc && sc->data[SC_UPHEAVAL_OPTION] && rnd()%100 < sc->data[SC_UPHEAVAL_OPTION]->val2 )
	{
		short skill_id = WZ_EARTHSPIKE;
		short skill_lv = sc->data[SC_UPHEAVAL_OPTION]->val3;

		if (sd) sd->state.autocast = 1;
		if (status_charge(src, 0, skill_get_sp(skill_id,skill_lv)))
		{
			skill_castend_damage_id(src, target, skill_id, skill_lv, tick, flag);
			battle_autocast_aftercast(src, skill_id, skill_lv, tick);
		}
		if (sd) sd->state.autocast = 0;
	}

	if (sd) {
		if (wd.flag & BF_WEAPON && src != target && damage > 0) {
			if (battle_config.left_cardfix_to_right)
				battle_drain(sd, target, wd.damage, wd.damage, tstatus->race, tstatus->class_, is_infinite_defense(target, wd.flag));
			else
				battle_drain(sd, target, wd.damage, wd.damage2, tstatus->race, tstatus->class_, is_infinite_defense(target, wd.flag));
		}
	}

	// Allow the ATK and CRIT bonus to be applied to the next regular attack before ending the status.
	if (sc && sc->data[SC_CAMOUFLAGE] && !(sc->data[SC_CAMOUFLAGE]->val3 & 2))
		status_change_end(src, SC_CAMOUFLAGE, INVALID_TIMER);

	if (tsc) 
	{
		if (damage > 0 && tsc->data[SC_POISONREACT] &&
			(rnd()%100 < tsc->data[SC_POISONREACT]->val3
			|| sstatus->def_ele == ELE_POISON) &&
//			check_distance_bl(src, target, tstatus->rhw.range+1) && Doesn't checks range! o.O;
			status_check_skilluse(target, src, TF_POISON, tsc->data[SC_POISONREACT]->val1, 0)
		) {	//Poison React
			struct status_change_entry *sce = tsc->data[SC_POISONREACT];
			if (sstatus->def_ele == ELE_POISON) {
				sce->val2 = 0;
				skill_attack(BF_WEAPON,target,target,src,AS_POISONREACT,sce->val1,tick,0);
			} else {
				skill_attack(BF_WEAPON,target,target,src,TF_POISON, 5, tick, 0);
				--sce->val2;
			}
			if (sce->val2 <= 0)
				status_change_end(target, SC_POISONREACT, INVALID_TIMER);
		}

		// Working only for regular ranged physical attacks. Need to fix this code later. [Rytech]
		if ((wd.flag&BF_WEAPON && wd.flag&BF_LONG || wd.flag&BF_MAGIC) && tsc->data[SC_SPIRITOFLAND_AUTOCAST] && rnd()%100 < 20 && status_check_skilluse(target, src, SU_SV_STEMSPEAR, 1, 0))
			skill_attack(BF_MAGIC,target,target,src,SU_SV_STEMSPEAR, rnd()%5+1, tick, 2);// What level does it officially autocast??? [Rytech]
	}
	map_freeblock_unlock();
	return wd.dmg_lv;
}

int battle_check_undead(int race,int element)
{
	if(battle_config.undead_detect_type == 0) {
		if(element == ELE_UNDEAD)
			return 1;
	}
	else if(battle_config.undead_detect_type == 1) {
		if(race == RC_UNDEAD)
			return 1;
	}
	else {
		if(element == ELE_UNDEAD || race == RC_UNDEAD)
			return 1;
	}
	return 0;
}

//Returns the upmost level master starting with the given object
struct block_list* battle_get_master(struct block_list *src)
{
	struct block_list *prev; //Used for infinite loop check (master of yourself?)
	do {
		prev = src;
		switch (src->type) {
			case BL_PET:
				if (((TBL_PET*)src)->master)
					src = (struct block_list*)((TBL_PET*)src)->master;
				break;
			case BL_MOB:
				if (((TBL_MOB*)src)->master_id)
					src = map_id2bl(((TBL_MOB*)src)->master_id);
				break;
			case BL_HOM:
				if (((TBL_HOM*)src)->master)
					src = (struct block_list*)((TBL_HOM*)src)->master;
				break;
			case BL_MER:
				if (((TBL_MER*)src)->master)
					src = (struct block_list*)((TBL_MER*)src)->master;
				break;
			case BL_ELEM:
				if (((TBL_ELEM*)src)->master)
					src = (struct block_list*)((TBL_ELEM*)src)->master;
				break;
			case BL_SKILL:
				if (((TBL_SKILL*)src)->group && ((TBL_SKILL*)src)->group->src_id)
					src = map_id2bl(((TBL_SKILL*)src)->group->src_id);
				break;
		}
	} while (src && src != prev);
	return prev;
}

/*==========================================
 * Checks the state between two targets (rewritten by Skotlex)
 * (enemy, friend, party, guild, etc)
 * See battle.h for possible values/combinations
 * to be used here (BCT_* constants)
 * Return value is:
 * 1: flag holds true (is enemy, party, etc)
 * -1: flag fails
 * 0: Invalid target (non-targetable ever)
 *------------------------------------------*/
int battle_check_target( struct block_list *src, struct block_list *target,int flag)
{
	int m,state = 0; //Initial state none
	int strip_enemy = 1; //Flag which marks whether to remove the BCT_ENEMY status if it's also friend/ally.
	struct block_list *s_bl = src, *t_bl = target;
	struct unit_data *ud = NULL;

	nullpo_ret(src);
	nullpo_ret(target);

	ud = unit_bl2ud(target);
	m = target->m;

	//t_bl/s_bl hold the 'master' of the attack, while src/target are the actual
	//objects involved.
	if( (t_bl = battle_get_master(target)) == NULL )
		t_bl = target;

	if( (s_bl = battle_get_master(src)) == NULL )
		s_bl = src;

	switch( target->type )
	{ // Checks on actual target
		case BL_PC:
			if (((TBL_PC*)target)->invincible_timer != INVALID_TIMER || pc_isinvisible((TBL_PC*)target))
				return -1; //Cannot be targeted yet.
			break;
		case BL_MOB:
			if (ud && ud->immune_attack)
				return 0;

			{
				struct mob_data *md = ((TBL_MOB*)target);
				if (((md->special_state.ai == AI_SPHERE || //Marine Spheres
					(md->special_state.ai == AI_FLORA && battle_config.summon_flora & 1)) && s_bl->type == BL_PC && src->type != BL_MOB) || //Floras
					(md->special_state.ai == AI_ZANZOU && t_bl->id != s_bl->id) || //Zanzoe
					(md->special_state.ai == AI_FAW && (t_bl->id != s_bl->id || (s_bl->type == BL_PC && src->type != BL_MOB)))
					) {	//Targettable by players
					state |= BCT_ENEMY;
					strip_enemy = 0;
				}
				break;
			}
		case BL_SKILL:
		{
			TBL_SKILL *su = (TBL_SKILL*)target;
			if (!su || !su->group)
				return 0;
			if (skill_get_inf2(su->group->skill_id)&INF2_TRAP && su->group->unit_id != UNT_USED_TRAPS) {
				uint16 skill_id = battle_getcurrentskill(src);
				if (!skill_id || su->group->skill_id == WM_REVERBERATION || su->group->skill_id == WM_POEMOFNETHERWORLD) {
					;
				}
				else if (skill_get_inf2(skill_id)&INF2_HIT_TRAP) { // Only a few skills can target traps
					switch (skill_id) {
					case RK_DRAGONBREATH:
					case RK_DRAGONBREATH_WATER:
						// Can only hit traps in PVP/GVG maps
						if (!map[m].flag.pvp && !map[m].flag.gvg)
							return 0;
					}
				}
				else
					return 0;
				state |= BCT_ENEMY;
				strip_enemy = 0;
			}
			else if (su->group->skill_id == WZ_ICEWALL || su->group->skill_id == GN_WALLOFTHORN)
			{
				state |= BCT_ENEMY;
				strip_enemy = 0;
			} else	//Excepting traps and icewall, you should not be able to target skills.
				return 0;
		}
			break;
		case BL_HOM:
		case BL_MER:
		case BL_ELEM:
			if (ud && ud->immune_attack)
				return 0;
			break;
		//All else not specified is an invalid target.
		default:	
			return 0;
	}

	switch( t_bl->type )
	{	//Checks on target master
		case BL_PC:
		{
			struct map_session_data *sd;
			if( t_bl == s_bl ) break;
			sd = BL_CAST(BL_PC, t_bl);

			if( sd->state.monster_ignore && flag&BCT_ENEMY )
				return 0; // Global inminuty only to Attacks
			if( sd->status.karma && s_bl->type == BL_PC && ((TBL_PC*)s_bl)->status.karma )
				state |= BCT_ENEMY; // Characters with bad karma may fight amongst them
			if( sd->state.killable ) {
				state |= BCT_ENEMY; // Everything can kill it
				strip_enemy = 0;
			}
			break;
		}
		case BL_MOB:
		{
			struct mob_data *md = BL_CAST(BL_MOB, t_bl);

			if( !map_flag_gvg(m) && md->guardian_data && md->guardian_data->guild_id )
				return 0; // Disable guardians/emperiums owned by Guilds on non-woe times.
			break;
		}
	}

	switch( src->type )
  	{	//Checks on actual src type
		case BL_PET:
			if (t_bl->type != BL_MOB && flag&BCT_ENEMY)
				return 0; //Pet may not attack non-mobs.
			if (t_bl->type == BL_MOB && ((TBL_MOB*)t_bl)->guardian_data && flag&BCT_ENEMY)
				return 0; //pet may not attack Guardians/Emperium
			break;
		case BL_SKILL:
		{
			struct skill_unit *su = (struct skill_unit *)src;
			struct status_change* sc = status_get_sc(target);

			if (!su || !su->group)
				return 0;
			if (su->group->src_id == target->id) {
				int inf2;
				inf2 = skill_get_inf2(su->group->skill_id);
				if (inf2&INF2_NO_TARGET_SELF)
					return -1;
				if (inf2&INF2_TARGET_SELF)
					return 1;
			}
			//Status changes that prevent traps from triggering
			if (sc && sc->count && skill_get_inf2(su->group->skill_id)&INF2_TRAP) {
				if (sc->data[SC_SIGHTBLASTER] && sc->data[SC_SIGHTBLASTER]->val2 > 0 && sc->data[SC_SIGHTBLASTER]->val4 % 2 == 0)
					return -1;
			}
			break;
		}
	}

	switch( s_bl->type )
	{	//Checks on source master
		case BL_PC:
		{
			struct map_session_data *sd = BL_CAST(BL_PC, s_bl);
			if( s_bl != t_bl )
			{
				if( sd->state.killer )
				{
					state |= BCT_ENEMY; // Can kill anything
					strip_enemy = 0;
				}
				else if( sd->duel_group && !((!battle_config.duel_allow_pvp && map[m].flag.pvp) || (!battle_config.duel_allow_gvg && map_flag_gvg(m))) )
		  		{
					if( t_bl->type == BL_PC && (sd->duel_group == ((TBL_PC*)t_bl)->duel_group) )
						return (BCT_ENEMY&flag)?1:-1; // Duel targets can ONLY be your enemy, nothing else.
					else
						return 0; // You can't target anything out of your duel
				}
				else if( map_getcell( s_bl->m, s_bl->x, s_bl->y, CELL_CHKPVP ) && map_getcell( t_bl->m, t_bl->x, t_bl->y, CELL_CHKPVP ) )	// Cell PVP [Napster]
				{
					state |= BCT_ENEMY;
				}
			}
			if( map_flag_gvg(m) && !sd->status.guild_id && t_bl->type == BL_MOB && ((TBL_MOB*)t_bl)->guardian_data )
				return 0; //If you don't belong to a guild, can't target guardians/emperium.
			if( t_bl->type != BL_PC )
				state |= BCT_ENEMY; //Natural enemy.
			break;
		}
		case BL_MOB:
		{
			struct mob_data *md = BL_CAST(BL_MOB, s_bl);
			if( !((agit_flag || agit2_flag) && map[m].flag.gvg_castle) && md->guardian_data && md->guardian_data->guild_id )
				return 0; // Disable guardians/emperium owned by Guilds on non-woe times.

			if( !md->special_state.ai )
			{ //Normal mobs.
				if( t_bl->type == BL_MOB && !((TBL_MOB*)t_bl)->special_state.ai )
					state |= BCT_PARTY; //Normal mobs with no ai are friends.
				else
					state |= BCT_ENEMY; //However, all else are enemies.
			}
			else
			{
				if( t_bl->type == BL_MOB && !((TBL_MOB*)t_bl)->special_state.ai )
					state |= BCT_ENEMY; //Natural enemy for AI mobs are normal mobs.
			}
			break;
		}
		default:
		//Need some sort of default behaviour for unhandled types.
			if (t_bl->type != s_bl->type)
				state |= BCT_ENEMY;
			break;
	}
	
	if( (flag&BCT_ALL) == BCT_ALL )
	{ //All actually stands for all attackable chars 
		if( target->type&BL_CHAR )
			return 1;
		else
			return -1;
	}
	if( flag == BCT_NOONE ) //Why would someone use this? no clue.
		return -1;
	
	if( t_bl == s_bl )
	{ //No need for further testing.
		state |= BCT_SELF|BCT_PARTY|BCT_GUILD;
		if( state&BCT_ENEMY && strip_enemy )
			state&=~BCT_ENEMY;
		return (flag&state)?1:-1;
	}
	
	if( map_flag_vs(m) )
	{ //Check rivalry settings.
		int sbg_id = 0, tbg_id = 0;
		if( map[m].flag.battleground )
		{
			sbg_id = bg_team_get_id(s_bl);
			tbg_id = bg_team_get_id(t_bl);
		}
		if( flag&(BCT_PARTY|BCT_ENEMY) )
		{
			int s_party = status_get_party_id(s_bl);
			if( s_party && s_party == status_get_party_id(t_bl) && !(map[m].flag.pvp && map[m].flag.pvp_noparty) && !(map_flag_gvg(m) && map[m].flag.gvg_noparty) && (!map[m].flag.battleground || sbg_id == tbg_id) )
				state |= BCT_PARTY;
			else
				state |= BCT_ENEMY;
		}
		if( flag&(BCT_GUILD|BCT_ENEMY) )
		{
			int s_guild = status_get_guild_id(s_bl);
			int t_guild = status_get_guild_id(t_bl);
			if( !(map[m].flag.pvp && map[m].flag.pvp_noguild) && s_guild && t_guild && (s_guild == t_guild || (!(flag&BCT_SAMEGUILD) && guild_isallied(s_guild, t_guild))) && (!map[m].flag.battleground || sbg_id == tbg_id) )
				state |= BCT_GUILD;
			else
				state |= BCT_ENEMY;
		}
		if( state&BCT_ENEMY && map[m].flag.battleground && sbg_id && sbg_id == tbg_id )
			state &= ~BCT_ENEMY;

		if( state&BCT_ENEMY && battle_config.pk_mode && !map_flag_gvg(m) && s_bl->type == BL_PC && t_bl->type == BL_PC )
		{ // Prevent novice engagement on pk_mode (feature by Valaris)
			TBL_PC *sd = (TBL_PC*)s_bl, *sd2 = (TBL_PC*)t_bl;
			if (
				(sd->class_&MAPID_UPPERMASK) == MAPID_NOVICE ||
				(sd2->class_&MAPID_UPPERMASK) == MAPID_NOVICE ||
				(int)sd->status.base_level < battle_config.pk_min_level ||
			  	(int)sd2->status.base_level < battle_config.pk_min_level ||
				(battle_config.pk_level_range && abs((int)sd->status.base_level - (int)sd2->status.base_level) > battle_config.pk_level_range)
			)
				state &= ~BCT_ENEMY;
		}
	}
	else
	{ //Non pvp/gvg, check party/guild settings.
		if( flag&BCT_PARTY || state&BCT_ENEMY )
		{
			int s_party = status_get_party_id(s_bl);
			if(s_party && s_party == status_get_party_id(t_bl) && !(battle_config.cellpvp_party_enable && map_getcell( t_bl->m, t_bl->x, t_bl->y, CELL_CHKPVP )) )		// Cell PVP [Napster]
 				state |= BCT_PARTY;
			else
				state |= BCT_ENEMY;
		}
		if( flag&BCT_GUILD || state&BCT_ENEMY )
		{
			int s_guild = status_get_guild_id(s_bl);
			int t_guild = status_get_guild_id(t_bl);
			if(s_guild && t_guild && (s_guild == t_guild || (!(flag&BCT_SAMEGUILD) && guild_isallied(s_guild, t_guild))) && !(battle_config.cellpvp_guild_enable && map_getcell( t_bl->m, t_bl->x, t_bl->y, CELL_CHKPVP )) )		// Cell PVP [Napster]
				state |= BCT_GUILD;
			else
				state |= BCT_ENEMY;
		}
	}
	
	if( !state ) //If not an enemy, nor a guild, nor party, nor yourself, it's neutral.
		state = BCT_NEUTRAL;
	//Alliance state takes precedence over enemy one.
	else if( state&BCT_ENEMY && strip_enemy && state&(BCT_SELF|BCT_PARTY|BCT_GUILD) )
		state&=~BCT_ENEMY;

	return (flag&state)?1:-1;
}
/*==========================================
 * 射程判定
 *------------------------------------------*/
bool battle_check_range(struct block_list *src, struct block_list *bl, int range)
{
	int d;
	nullpo_retr(false, src);
	nullpo_retr(false, bl);

	if( src->m != bl->m )
		return false;

#ifndef CIRCULAR_AREA
	if( src->type == BL_PC )
	{ // Range for players' attacks and skills should always have a circular check. [Inkfish]
		int dx = src->x - bl->x, dy = src->y - bl->y;
		if( !check_distance_client(dx*dx + dy*dy, 0, range*range+(dx&&dy?1:0)) )
			return false;
	}
	else
#endif
	if( !check_distance_bl(src, bl, range) )
		return false;

	if( (d = distance_bl(src, bl)) < 2 )
		return true;  // No need for path checking.

	if( d > AREA_SIZE )
		return false; // Avoid targetting objects beyond your range of sight.

	return path_search_long(NULL,src->m,src->x,src->y,bl->x,bl->y,CELL_CHKWALL);
}

static const struct _battle_data {
	const char* str;
	int* val;
	int defval;
	int min;
	int max;
} battle_data[] = {
	{ "warp_point_debug",                   &battle_config.warp_point_debug,                0,      0,      1,              },
	{ "enable_critical",                    &battle_config.enable_critical,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "mob_critical_rate",                  &battle_config.mob_critical_rate,               100,    0,      INT_MAX,        },
	{ "critical_rate",                      &battle_config.critical_rate,                   100,    0,      INT_MAX,        },
	{ "enable_baseatk",                     &battle_config.enable_baseatk,                  BL_PC|BL_HOM, BL_NUL, BL_ALL,   },
	{ "enable_perfect_flee",                &battle_config.enable_perfect_flee,             BL_PC|BL_PET, BL_NUL, BL_ALL,   },
	{ "casting_rate",                       &battle_config.cast_rate,                       100,    0,      INT_MAX,        },
	{ "delay_rate",                         &battle_config.delay_rate,                      100,    0,      INT_MAX,        },
	{ "delay_dependon_dex",                 &battle_config.delay_dependon_dex,              0,      0,      1,              },
	{ "delay_dependon_agi",                 &battle_config.delay_dependon_agi,              0,      0,      1,              },
	{ "skill_delay_attack_enable",          &battle_config.sdelay_attack_enable,            0,      0,      1,              },
	{ "left_cardfix_to_right",              &battle_config.left_cardfix_to_right,           0,      0,      1,              },
	{ "skill_add_range",                    &battle_config.skill_add_range,                 0,      0,      INT_MAX,        },
	{ "skill_out_range_consume",            &battle_config.skill_out_range_consume,         1,      0,      1,              },
	{ "skillrange_by_distance",             &battle_config.skillrange_by_distance,          ~BL_PC, BL_NUL, BL_ALL,         },
	{ "skillrange_from_weapon",             &battle_config.use_weapon_skill_range,          ~BL_PC, BL_NUL, BL_ALL,         },
	{ "player_damage_delay_rate",           &battle_config.pc_damage_delay_rate,            100,    0,      INT_MAX,        },
	{ "defunit_not_enemy",                  &battle_config.defnotenemy,                     0,      0,      1,              },
	{ "gvg_traps_target_all",               &battle_config.vs_traps_bctall,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "traps_setting",                      &battle_config.traps_setting,                   0,      0,      1,              },
	{ "summon_flora_setting",               &battle_config.summon_flora,                    1|2,    0,      1|2,            },
	{ "clear_skills_on_death",              &battle_config.clear_unit_ondeath,              BL_NUL, BL_NUL, BL_ALL,         },
	{ "clear_skills_on_warp",               &battle_config.clear_unit_onwarp,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "random_monster_checklv",             &battle_config.random_monster_checklv,          0,      0,      1,              },
	{ "attribute_recover",                  &battle_config.attr_recover,                    1,      0,      1,              },
	{ "flooritem_lifetime",                 &battle_config.flooritem_lifetime,              60000,  1000,   INT_MAX,        },
	{ "item_auto_get",                      &battle_config.item_auto_get,                   0,      0,      1,              },
	{ "item_first_get_time",                &battle_config.item_first_get_time,             3000,   0,      INT_MAX,        },
	{ "item_second_get_time",               &battle_config.item_second_get_time,            1000,   0,      INT_MAX,        },
	{ "item_third_get_time",                &battle_config.item_third_get_time,             1000,   0,      INT_MAX,        },
	{ "mvp_item_first_get_time",            &battle_config.mvp_item_first_get_time,         10000,  0,      INT_MAX,        },
	{ "mvp_item_second_get_time",           &battle_config.mvp_item_second_get_time,        10000,  0,      INT_MAX,        },
	{ "mvp_item_third_get_time",            &battle_config.mvp_item_third_get_time,         2000,   0,      INT_MAX,        },
	{ "drop_rate0item",                     &battle_config.drop_rate0item,                  0,      0,      1,              },
	{ "base_exp_rate",                      &battle_config.base_exp_rate,                   100,    0,      INT_MAX,        },
	{ "job_exp_rate",                       &battle_config.job_exp_rate,                    100,    0,      INT_MAX,        },
	{ "pvp_exp",                            &battle_config.pvp_exp,                         1,      0,      1,              },
	{ "death_penalty_type",                 &battle_config.death_penalty_type,              0,      0,      2,              },
	{ "death_penalty_base",                 &battle_config.death_penalty_base,              0,      0,      INT_MAX,        },
	{ "death_penalty_job",                  &battle_config.death_penalty_job,               0,      0,      INT_MAX,        },
	{ "zeny_penalty",                       &battle_config.zeny_penalty,                    0,      0,      INT_MAX,        },
	{ "hp_rate",                            &battle_config.hp_rate,                         100,    1,      INT_MAX,        },
	{ "sp_rate",                            &battle_config.sp_rate,                         100,    1,      INT_MAX,        },
	{ "restart_hp_rate",                    &battle_config.restart_hp_rate,                 0,      0,      100,            },
	{ "restart_sp_rate",                    &battle_config.restart_sp_rate,                 0,      0,      100,            },
	{ "guild_aura",                         &battle_config.guild_aura,                      31,     0,      31,             },
	{ "mvp_hp_rate",                        &battle_config.mvp_hp_rate,                     100,    1,      INT_MAX,        },
	{ "mvp_exp_rate",                       &battle_config.mvp_exp_rate,                    100,    0,      INT_MAX,        },
	{ "monster_hp_rate",                    &battle_config.monster_hp_rate,                 100,    1,      INT_MAX,        },
	{ "monster_max_aspd",                   &battle_config.monster_max_aspd,                199,    100,    199,            },
	{ "view_range_rate",                    &battle_config.view_range_rate,                 100,    0,      INT_MAX,        },
	{ "chase_range_rate",                   &battle_config.chase_range_rate,                100,    0,      INT_MAX,        },
	{ "gtb_sc_immunity",                    &battle_config.gtb_sc_immunity,                 50,     0,      INT_MAX,        },
	{ "guild_max_castles",                  &battle_config.guild_max_castles,               0,      0,      INT_MAX,        },
	{ "guild_skill_relog_delay",            &battle_config.guild_skill_relog_delay,         300000, 0,      INT_MAX,        },
	{ "emergency_call",                     &battle_config.emergency_call,                  11,     0,      31,             },
	{ "lowest_gm_level",                    &battle_config.lowest_gm_level,                 1,      0,      99,             },
	{ "atcommand_gm_only",                  &battle_config.atc_gmonly,                      0,      0,      1,              },
	{ "atcommand_spawn_quantity_limit",     &battle_config.atc_spawn_quantity_limit,        100,    0,      INT_MAX,        },
	{ "atcommand_slave_clone_limit",        &battle_config.atc_slave_clone_limit,           25,     0,      INT_MAX,        },
	{ "partial_name_scan",                  &battle_config.partial_name_scan,               0,      0,      1,              },
	{ "gm_all_skill",                       &battle_config.gm_allskill,                     0,      0,      100,            },
	{ "gm_all_equipment",                   &battle_config.gm_allequip,                     0,      0,      100,            },
	{ "gm_skill_unconditional",             &battle_config.gm_skilluncond,                  0,      0,      100,            },
	{ "gm_join_chat",                       &battle_config.gm_join_chat,                    0,      0,      100,            },
	{ "gm_kick_chat",                       &battle_config.gm_kick_chat,                    0,      0,      100,            },
	{ "gm_can_party",                       &battle_config.gm_can_party,                    0,      0,      1,              },
	{ "gm_cant_party_min_lv",               &battle_config.gm_cant_party_min_lv,            20,     0,      100,            },
	{ "player_skillfree",                   &battle_config.skillfree,                       0,      0,      1,              },
	{ "player_skillup_limit",               &battle_config.skillup_limit,                   1,      0,      1,              },
	{ "weapon_produce_rate",                &battle_config.wp_rate,                         100,    0,      INT_MAX,        },
	{ "potion_produce_rate",                &battle_config.pp_rate,                         100,    0,      INT_MAX,        },
	{ "monster_active_enable",              &battle_config.monster_active_enable,           1,      0,      1,              },
	{ "monster_damage_delay_rate",          &battle_config.monster_damage_delay_rate,       100,    0,      INT_MAX,        },
	{ "monster_loot_type",                  &battle_config.monster_loot_type,               0,      0,      1,              },
//	{ "mob_skill_use",                      &battle_config.mob_skill_use,                   1,      0,      1,              }, //Deprecated
	{ "mob_skill_rate",                     &battle_config.mob_skill_rate,                  100,    0,      INT_MAX,        },
	{ "mob_skill_delay",                    &battle_config.mob_skill_delay,                 100,    0,      INT_MAX,        },
	{ "mob_count_rate",                     &battle_config.mob_count_rate,                  100,    0,      INT_MAX,        },
	{ "mob_spawn_delay",                    &battle_config.mob_spawn_delay,                 100,    0,      INT_MAX,        },
	{ "plant_spawn_delay",                  &battle_config.plant_spawn_delay,               100,    0,      INT_MAX,        },
	{ "boss_spawn_delay",                   &battle_config.boss_spawn_delay,                100,    0,      INT_MAX,        },
	{ "no_spawn_on_player",                 &battle_config.no_spawn_on_player,              0,      0,      100,            },
	{ "force_random_spawn",                 &battle_config.force_random_spawn,              0,      0,      1,              },
	{ "slaves_inherit_mode",                &battle_config.slaves_inherit_mode,             2,      0,      3,              },
	{ "slaves_inherit_speed",               &battle_config.slaves_inherit_speed,            3,      0,      3,              },
	{ "summons_trigger_autospells",         &battle_config.summons_trigger_autospells,      1,      0,      1,              },
	{ "pc_damage_walk_delay_rate",          &battle_config.pc_walk_delay_rate,              20,     0,      INT_MAX,        },
	{ "damage_walk_delay_rate",             &battle_config.walk_delay_rate,                 100,    0,      INT_MAX,        },
	{ "multihit_delay",                     &battle_config.multihit_delay,                  80,     0,      INT_MAX,        },
	{ "quest_skill_learn",                  &battle_config.quest_skill_learn,               0,      0,      1,              },
	{ "quest_skill_reset",                  &battle_config.quest_skill_reset,               0,      0,      1,              },
	{ "basic_skill_check",                  &battle_config.basic_skill_check,               1,      0,      1,              },
	{ "guild_emperium_check",               &battle_config.guild_emperium_check,            1,      0,      1,              },
	{ "guild_exp_limit",                    &battle_config.guild_exp_limit,                 50,     0,      99,             },
	{ "player_invincible_time",             &battle_config.pc_invincible_time,              5000,   0,      INT_MAX,        },
	{ "pet_catch_rate",                     &battle_config.pet_catch_rate,                  100,    0,      INT_MAX,        },
	{ "pet_rename",                         &battle_config.pet_rename,                      0,      0,      1,              },
	{ "pet_friendly_rate",                  &battle_config.pet_friendly_rate,               100,    0,      INT_MAX,        },
	{ "pet_hungry_delay_rate",              &battle_config.pet_hungry_delay_rate,           100,    10,     INT_MAX,        },
	{ "pet_hungry_friendly_decrease",       &battle_config.pet_hungry_friendly_decrease,    5,      0,      INT_MAX,        },
	{ "pet_status_support",                 &battle_config.pet_status_support,              0,      0,      1,              },
	{ "pet_attack_support",                 &battle_config.pet_attack_support,              0,      0,      1,              },
	{ "pet_damage_support",                 &battle_config.pet_damage_support,              0,      0,      1,              },
	{ "pet_support_min_friendly",           &battle_config.pet_support_min_friendly,        900,    0,      950,            },
	{ "pet_equip_min_friendly",             &battle_config.pet_equip_min_friendly,          900,    0,      950,            },
	{ "pet_support_rate",                   &battle_config.pet_support_rate,                100,    0,      INT_MAX,        },
	{ "pet_attack_exp_to_master",           &battle_config.pet_attack_exp_to_master,        0,      0,      1,              },
	{ "pet_attack_exp_rate",                &battle_config.pet_attack_exp_rate,             100,    0,      INT_MAX,        },
	{ "pet_lv_rate",                        &battle_config.pet_lv_rate,                     0,      0,      INT_MAX,        },
	{ "pet_max_stats",                      &battle_config.pet_max_stats,                   99,     0,      INT_MAX,        },
	{ "pet_max_atk1",                       &battle_config.pet_max_atk1,                    750,    0,      INT_MAX,        },
	{ "pet_max_atk2",                       &battle_config.pet_max_atk2,                    1000,   0,      INT_MAX,        },
	{ "pet_disable_in_gvg",                 &battle_config.pet_no_gvg,                      0,      0,      1,              },
	{ "pet_autofeed",						&battle_config.pet_autofeed,					1,      0,      1,				},
	{ "skill_min_damage",                   &battle_config.skill_min_damage,                2|4,    0,      1|2|4,          },
	{ "finger_offensive_type",              &battle_config.finger_offensive_type,           0,      0,      1,              },
	{ "heal_exp",                           &battle_config.heal_exp,                        0,      0,      INT_MAX,        },
	{ "resurrection_exp",                   &battle_config.resurrection_exp,                0,      0,      INT_MAX,        },
	{ "shop_exp",                           &battle_config.shop_exp,                        0,      0,      INT_MAX,        },
	{ "max_heal_lv",                        &battle_config.max_heal_lv,                     11,     1,      INT_MAX,        },
	{ "max_heal",                           &battle_config.max_heal,                        9999,   0,      INT_MAX,        },
	{ "combo_delay_rate",                   &battle_config.combo_delay_rate,                100,    0,      INT_MAX,        },
	{ "item_check",                         &battle_config.item_check,                      0,      0,      7,              },
	{ "item_use_interval",                  &battle_config.item_use_interval,               100,    0,      INT_MAX,        },
	{ "cashfood_use_interval",              &battle_config.cashfood_use_interval,           60000,  0,      INT_MAX,        },
	{ "wedding_modifydisplay",              &battle_config.wedding_modifydisplay,           0,      0,      1,              },
	{ "wedding_ignorepalette",              &battle_config.wedding_ignorepalette,           0,      0,      1,              },
	{ "xmas_ignorepalette",                 &battle_config.xmas_ignorepalette,              0,      0,      1,              },
	{ "summer_ignorepalette",               &battle_config.summer_ignorepalette,            0,      0,      1,              },
	{ "natural_healhp_interval",            &battle_config.natural_healhp_interval,         6000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_healsp_interval",            &battle_config.natural_healsp_interval,         8000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_heal_skill_interval",        &battle_config.natural_heal_skill_interval,     10000,  NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_heal_weight_rate",           &battle_config.natural_heal_weight_rate,        50,     50,     101             },
	{ "arrow_decrement",                    &battle_config.arrow_decrement,                 1,      0,      2,              },
	{ "max_aspd",                           &battle_config.max_aspd,                        199,    100,    199,            },
	{ "max_walk_speed",                     &battle_config.max_walk_speed,                  300,    100,    100*DEFAULT_WALK_SPEED, },
	{ "max_lv",                             &battle_config.max_lv,                          99,       0,    150,            },
	{ "aura_lv",                            &battle_config.aura_lv,                         99,       0,    INT_MAX,        },
	{ "max_hp",                             &battle_config.max_hp,                          32500,  100,    1000000000,     },
	{ "max_sp",                             &battle_config.max_sp,                          32500,  100,    1000000000,     },
	{ "max_cart_weight",                    &battle_config.max_cart_weight,                 8000,   100,    1000000,        },
	{ "max_parameter",                      &battle_config.max_parameter,                   99,     10,     10000,          },
	{ "max_baby_parameter",                 &battle_config.max_baby_parameter,              80,     10,     10000,          },
	{ "max_def",                            &battle_config.max_def,                         99,     0,      SHRT_MAX,       },
	{ "over_def_bonus",                     &battle_config.over_def_bonus,                  0,      0,      1000,           },
	{ "skill_log",                          &battle_config.skill_log,                       BL_NUL, BL_NUL, BL_ALL,         },
	{ "battle_log",                         &battle_config.battle_log,                      0,      0,      1,              },
	{ "save_log",                           &battle_config.save_log,                        0,      0,      1,              },
	{ "etc_log",                            &battle_config.etc_log,                         1,      0,      1,              },
	{ "save_clothcolor",                    &battle_config.save_clothcolor,                 1,      0,      1,              },
	{ "undead_detect_type",                 &battle_config.undead_detect_type,              0,      0,      2,              },
	{ "auto_counter_type",                  &battle_config.auto_counter_type,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "min_hitrate",                        &battle_config.min_hitrate,                     5,      0,      100,            },
	{ "max_hitrate",                        &battle_config.max_hitrate,                     100,    0,      100,            },
	{ "agi_penalty_target",                 &battle_config.agi_penalty_target,              BL_PC,  BL_NUL, BL_ALL,         },
	{ "agi_penalty_type",                   &battle_config.agi_penalty_type,                1,      0,      2,              },
	{ "agi_penalty_count",                  &battle_config.agi_penalty_count,               3,      2,      INT_MAX,        },
	{ "agi_penalty_num",                    &battle_config.agi_penalty_num,                 10,     0,      INT_MAX,        },
	{ "vit_penalty_target",                 &battle_config.vit_penalty_target,              BL_PC,  BL_NUL, BL_ALL,         },
	{ "vit_penalty_type",                   &battle_config.vit_penalty_type,                1,      0,      2,              },
	{ "vit_penalty_count",                  &battle_config.vit_penalty_count,               3,      2,      INT_MAX,        },
	{ "vit_penalty_num",                    &battle_config.vit_penalty_num,                 5,      0,      INT_MAX,        },
	{ "weapon_defense_type",                &battle_config.weapon_defense_type,             0,      0,      INT_MAX,        },
	{ "magic_defense_type",                 &battle_config.magic_defense_type,              0,      0,      INT_MAX,        },
	{ "skill_reiteration",                  &battle_config.skill_reiteration,               BL_NUL, BL_NUL, BL_ALL,         },
	{ "skill_nofootset",                    &battle_config.skill_nofootset,                 BL_PC,  BL_NUL, BL_ALL,         },
	{ "player_cloak_check_type",            &battle_config.pc_cloak_check_type,             1,      0,      1|2|4,          },
	{ "player_camouflage_check_type",		&battle_config.pc_camouflage_check_type,		1,		0,		1|2|4,			},
	{ "monster_cloak_check_type",           &battle_config.monster_cloak_check_type,        4,      0,      1|2|4,          },
	{ "sense_type",                         &battle_config.estimation_type,                 1|2,    0,      1|2,            },
	{ "gvg_eliminate_time",                 &battle_config.gvg_eliminate_time,              7000,   0,      INT_MAX,        },
	{ "gvg_short_attack_damage_rate",       &battle_config.gvg_short_damage_rate,           80,     0,      INT_MAX,        },
	{ "gvg_long_attack_damage_rate",        &battle_config.gvg_long_damage_rate,            80,     0,      INT_MAX,        },
	{ "gvg_weapon_attack_damage_rate",      &battle_config.gvg_weapon_damage_rate,          60,     0,      INT_MAX,        },
	{ "gvg_magic_attack_damage_rate",       &battle_config.gvg_magic_damage_rate,           60,     0,      INT_MAX,        },
	{ "gvg_misc_attack_damage_rate",        &battle_config.gvg_misc_damage_rate,            60,     0,      INT_MAX,        },
	{ "gvg_flee_penalty",                   &battle_config.gvg_flee_penalty,                20,     0,      INT_MAX,        },
	{ "gvg_mon_trans_disable",              &battle_config.gvg_mon_trans_disable,           0,      0,      1,              },
	{ "pk_short_attack_damage_rate",        &battle_config.pk_short_damage_rate,            80,     0,      INT_MAX,        },
	{ "pk_long_attack_damage_rate",         &battle_config.pk_long_damage_rate,             70,     0,      INT_MAX,        },
	{ "pk_weapon_attack_damage_rate",       &battle_config.pk_weapon_damage_rate,           60,     0,      INT_MAX,        },
	{ "pk_magic_attack_damage_rate",        &battle_config.pk_magic_damage_rate,            60,     0,      INT_MAX,        },
	{ "pk_misc_attack_damage_rate",         &battle_config.pk_misc_damage_rate,             60,     0,      INT_MAX,        },
	{ "mob_changetarget_byskill",           &battle_config.mob_changetarget_byskill,        0,      0,      1,              },
	{ "attack_direction_change",            &battle_config.attack_direction_change,         BL_ALL, BL_NUL, BL_ALL,         },
	{ "land_skill_limit",                   &battle_config.land_skill_limit,                BL_ALL, BL_NUL, BL_ALL,         },
	{ "monster_class_change_full_recover",  &battle_config.monster_class_change_recover,    1,      0,      1,              },
	{ "produce_item_name_input",            &battle_config.produce_item_name_input,         0x1|0x2, 0,     0x9F,           },
	{ "display_skill_fail",                 &battle_config.display_skill_fail,              2,      0,      1|2|4|8,        },
	{ "chat_warpportal",                    &battle_config.chat_warpportal,                 0,      0,      1,              },
	{ "mob_warp",                           &battle_config.mob_warp,                        0,      0,      1|2|4|8,        },
	{ "dead_branch_active",                 &battle_config.dead_branch_active,              1,      0,      1,              },
	{ "vending_max_value",                  &battle_config.vending_max_value,               10000000, 1,    MAX_ZENY,       },
	{ "vending_over_max",                   &battle_config.vending_over_max,                1,      0,      1,              },
	{ "show_steal_in_same_party",           &battle_config.show_steal_in_same_party,        0,      0,      1,              },
	{ "party_hp_mode",                      &battle_config.party_hp_mode,                   0,      0,      1,              },
	{ "show_party_share_picker",            &battle_config.party_show_share_picker,         1,      0,      1,              },
	{ "show_picker.item_type",              &battle_config.show_picker_item_type,           112,    0,      INT_MAX,        },
	{ "party_update_interval",              &battle_config.party_update_interval,           1000,   100,    INT_MAX,        },
	{ "party_item_share_type",              &battle_config.party_share_type,                0,      0,      1|2|3,          },
	{ "attack_attr_none",                   &battle_config.attack_attr_none,                ~BL_PC, BL_NUL, BL_ALL,         },
	{ "gx_allhit",                          &battle_config.gx_allhit,                       0,      0,      1,              },
	{ "gx_disptype",                        &battle_config.gx_disptype,                     1,      0,      1,              },
	{ "devotion_level_difference",          &battle_config.devotion_level_difference,       10,     0,      INT_MAX,        },
	{ "devotion_rdamage",					&battle_config.devotion_rdamage,				0,		0,		100,			},
	{ "player_skill_partner_check",         &battle_config.player_skill_partner_check,      1,      0,      1,              },
	{ "hide_GM_session",                    &battle_config.hide_GM_session,                 0,      0,      1,              },
	{ "invite_request_check",               &battle_config.invite_request_check,            1,      0,      1,              },
	{ "skill_removetrap_type",              &battle_config.skill_removetrap_type,           0,      0,      1,              },
	{ "disp_experience",                    &battle_config.disp_experience,                 0,      0,      1,              },
	{ "disp_zeny",                          &battle_config.disp_zeny,                       0,      0,      1,              },
	{ "castle_defense_rate",                &battle_config.castle_defense_rate,             100,    0,      100,            },
	{ "gm_cant_drop_min_lv",                &battle_config.gm_cant_drop_min_lv,             1,      0,      100,            },
	{ "gm_cant_drop_max_lv",                &battle_config.gm_cant_drop_max_lv,             0,      0,      100,            },
	{ "bound_item_drop",	                &battle_config.bound_item_drop,		            0,      0,      100,            },
	{ "disp_hpmeter",                       &battle_config.disp_hpmeter,                    0,      0,      100,            },
	{ "bone_drop",                          &battle_config.bone_drop,                       0,      0,      2,              },
	{ "buyer_name",                         &battle_config.buyer_name,                      1,      0,      1,              },
	{ "skill_wall_check",                   &battle_config.skill_wall_check,                1,      0,      1,              },
	{ "official_cell_stack_limit",          &battle_config.official_cell_stack_limit,       1,      0,      255,			},
	{ "custom_cell_stack_limit",            &battle_config.custom_cell_stack_limit,         1,      1,      255,			},
	{ "dancing_weaponswitch_fix",			&battle_config.dancing_weaponswitch_fix,		1,      0,      1,				},
// eAthena additions
	{ "item_logarithmic_drops",             &battle_config.logarithmic_drops,               0,      0,      1,              },
	{ "item_drop_common_min",               &battle_config.item_drop_common_min,            1,      1,      10000,          },
	{ "item_drop_common_max",               &battle_config.item_drop_common_max,            10000,  1,      10000,          },
	{ "item_drop_equip_min",                &battle_config.item_drop_equip_min,             1,      1,      10000,          },
	{ "item_drop_equip_max",                &battle_config.item_drop_equip_max,             10000,  1,      10000,          },
	{ "item_drop_card_min",                 &battle_config.item_drop_card_min,              1,      1,      10000,          },
	{ "item_drop_card_max",                 &battle_config.item_drop_card_max,              10000,  1,      10000,          },
	{ "item_drop_mvp_min",                  &battle_config.item_drop_mvp_min,               1,      1,      10000,          },
	{ "item_drop_mvp_max",                  &battle_config.item_drop_mvp_max,               10000,  1,      10000,          },
	{ "item_drop_heal_min",                 &battle_config.item_drop_heal_min,              1,      1,      10000,          },
	{ "item_drop_heal_max",                 &battle_config.item_drop_heal_max,              10000,  1,      10000,          },
	{ "item_drop_use_min",                  &battle_config.item_drop_use_min,               1,      1,      10000,          },
	{ "item_drop_use_max",                  &battle_config.item_drop_use_max,               10000,  1,      10000,          },
	{ "item_drop_add_min",                  &battle_config.item_drop_adddrop_min,           1,      1,      10000,          },
	{ "item_drop_add_max",                  &battle_config.item_drop_adddrop_max,           10000,  1,      10000,          },
	{ "item_drop_treasure_min",             &battle_config.item_drop_treasure_min,          1,      1,      10000,          },
	{ "item_drop_treasure_max",             &battle_config.item_drop_treasure_max,          10000,  1,      10000,          },
	{ "item_rate_mvp",                      &battle_config.item_rate_mvp,                   100,    0,      1000000,        },
	{ "item_rate_common",                   &battle_config.item_rate_common,                100,    0,      1000000,        },
	{ "item_rate_common_boss",              &battle_config.item_rate_common_boss,           100,    0,      1000000,        },
	{ "item_rate_common_mvp",              &battle_config.item_rate_common_mvp,				100,    0,      1000000,        },
	{ "item_rate_equip",                    &battle_config.item_rate_equip,                 100,    0,      1000000,        },
	{ "item_rate_equip_boss",               &battle_config.item_rate_equip_boss,            100,    0,      1000000,        },
	{ "item_rate_equip_mvp",               &battle_config.item_rate_equip_mvp,				100,    0,      1000000,        },
	{ "item_rate_card",                     &battle_config.item_rate_card,                  100,    0,      1000000,        },
	{ "item_rate_card_boss",                &battle_config.item_rate_card_boss,             100,    0,      1000000,        },
	{ "item_rate_card_mvp",                &battle_config.item_rate_card_mvp,				100,    0,      1000000,        },
	{ "item_rate_heal",                     &battle_config.item_rate_heal,                  100,    0,      1000000,        },
	{ "item_rate_heal_boss",                &battle_config.item_rate_heal_boss,             100,    0,      1000000,        },
	{ "item_rate_heal_mvp",                &battle_config.item_rate_heal_mvp,				100,    0,      1000000,        },
	{ "item_rate_use",                      &battle_config.item_rate_use,                   100,    0,      1000000,        },
	{ "item_rate_use_boss",                 &battle_config.item_rate_use_boss,              100,    0,      1000000,        },
	{ "item_rate_use_mvp",                 &battle_config.item_rate_use_mvp,				100,    0,      1000000,        },
	{ "item_rate_adddrop",                  &battle_config.item_rate_adddrop,               100,    0,      1000000,        },
	{ "item_rate_treasure",                 &battle_config.item_rate_treasure,              100,    0,      1000000,        },
	{ "prevent_logout",                     &battle_config.prevent_logout,                  10000,  0,      60000,          },
	{ "alchemist_summon_reward",            &battle_config.alchemist_summon_reward,         1,      0,      2,              },
	{ "drops_by_luk",                       &battle_config.drops_by_luk,                    0,      0,      INT_MAX,        },
	{ "drops_by_luk2",                      &battle_config.drops_by_luk2,                   0,      0,      INT_MAX,        },
	{ "equip_natural_break_rate",           &battle_config.equip_natural_break_rate,        0,      0,      INT_MAX,        },
	{ "equip_self_break_rate",              &battle_config.equip_self_break_rate,           100,    0,      INT_MAX,        },
	{ "equip_skill_break_rate",             &battle_config.equip_skill_break_rate,          100,    0,      INT_MAX,        },
	{ "pk_mode",                            &battle_config.pk_mode,                         0,      0,      1,              },
	{ "pk_level_range",                     &battle_config.pk_level_range,                  0,      0,      INT_MAX,        },
	{ "manner_system",                      &battle_config.manner_system,                   0xFFF,  0,      0xFFF,          },
	{ "pet_equip_required",                 &battle_config.pet_equip_required,              0,      0,      1,              },
	{ "multi_level_up",                     &battle_config.multi_level_up,                  0,      0,      1,              },
	{ "max_exp_gain_rate",                  &battle_config.max_exp_gain_rate,               0,      0,      INT_MAX,        },
	{ "backstab_bow_penalty",               &battle_config.backstab_bow_penalty,            0,      0,      1,              },
	{ "night_at_start",                     &battle_config.night_at_start,                  0,      0,      1,              },
	{ "show_mob_info",                      &battle_config.show_mob_info,                   0,      0,      1|2|4,          },
	{ "ban_hack_trade",                     &battle_config.ban_hack_trade,                  0,      0,      INT_MAX,        },
	{ "hack_info_GM_level",                 &battle_config.hack_info_GM_level,              60,     0,      100,            },
	{ "any_warp_GM_min_level",              &battle_config.any_warp_GM_min_level,           20,     0,      100,            },
	{ "who_display_aid",                    &battle_config.who_display_aid,                 40,     0,      100,            },
	{ "packet_ver_flag",                    &battle_config.packet_ver_flag,                 0x7FFFFFFF,0,   INT_MAX,		},
	{ "packet_ver_flag2",                   &battle_config.packet_ver_flag2,                0x7FFFFFFF,0,   INT_MAX,		},
	{ "min_hair_style",                     &battle_config.min_hair_style,                  0,      0,      INT_MAX,        },
	{ "max_hair_style",                     &battle_config.max_hair_style,                  31,     0,      INT_MAX,		},
	{ "min_hair_color",                     &battle_config.min_hair_color,                  0,      0,      INT_MAX,        },
	{ "max_hair_color",                     &battle_config.max_hair_color,                  8,      0,      INT_MAX,        },
	{ "min_cloth_color",                    &battle_config.min_cloth_color,                 0,      0,      INT_MAX,        },
	{ "max_cloth_color",                    &battle_config.max_cloth_color,                 4,      0,      INT_MAX,        },
	{ "min_body_style",                     &battle_config.min_body_style,                  0,      0,      SHRT_MAX,       },
	{ "max_body_style",                     &battle_config.max_body_style,                  4,      0,      SHRT_MAX,       },
	{ "save_body_style",                    &battle_config.save_body_style,                 0,      0,      1,              },
	{ "min_doram_hair_style",               &battle_config.min_doram_hair_style,            0,      0,      SHRT_MAX,       },
	{ "max_doram_hair_style",               &battle_config.max_doram_hair_style,            6,      0,      SHRT_MAX,       },
	{ "min_doram_hair_color",               &battle_config.min_doram_hair_color,            0,      0,      SHRT_MAX,       },
	{ "max_doram_hair_color",               &battle_config.max_doram_hair_color,            8,      0,      SHRT_MAX,       },
	{ "min_doram_cloth_color",              &battle_config.min_doram_cloth_color,           0,      0,      SHRT_MAX,       },
	{ "max_doram_cloth_color",              &battle_config.max_doram_cloth_color,           3,      0,      SHRT_MAX,       },
	{ "pet_hair_style",                     &battle_config.pet_hair_style,                  100,    0,      INT_MAX,        },
	{ "castrate_dex_scale",                 &battle_config.castrate_dex_scale,              150,    1,      INT_MAX,        },
	{ "area_size",                          &battle_config.area_size,                       14,     0,      INT_MAX,        },
	{ "zeny_from_mobs",                     &battle_config.zeny_from_mobs,                  0,      0,      1,              },
	{ "mobs_level_up",                      &battle_config.mobs_level_up,                   0,      0,      1,              },
	{ "mobs_level_up_exp_rate",             &battle_config.mobs_level_up_exp_rate,          1,      1,      INT_MAX,        },
	{ "pk_min_level",                       &battle_config.pk_min_level,                    55,     1,      INT_MAX,        },
	{ "skill_steal_max_tries",              &battle_config.skill_steal_max_tries,           0,      0,      UCHAR_MAX,      },
	{ "motd_type",                          &battle_config.motd_type,                       0,      0,      1,              },
	{ "finding_ore_rate",                   &battle_config.finding_ore_rate,                100,    0,      INT_MAX,        },
	{ "exp_calc_type",                      &battle_config.exp_calc_type,                   0,      0,      1,              },
	{ "exp_bonus_attacker",                 &battle_config.exp_bonus_attacker,              25,     0,      INT_MAX,        },
	{ "exp_bonus_max_attacker",             &battle_config.exp_bonus_max_attacker,          12,     2,      INT_MAX,        },
	{ "min_skill_delay_limit",              &battle_config.min_skill_delay_limit,           100,    10,     INT_MAX,        },
	{ "default_walk_delay",                 &battle_config.default_walk_delay,              300,    0,      INT_MAX,        },
	{ "no_skill_delay",                     &battle_config.no_skill_delay,                  BL_MOB, BL_NUL, BL_ALL,         },
	{ "attack_walk_delay",                  &battle_config.attack_walk_delay,               BL_ALL, BL_NUL, BL_ALL,         },
	{ "require_glory_guild",                &battle_config.require_glory_guild,             0,      0,      1,              },
	{ "idle_no_share",                      &battle_config.idle_no_share,                   0,      0,      INT_MAX,        },
	{ "party_even_share_bonus",             &battle_config.party_even_share_bonus,          0,      0,      INT_MAX,        }, 
	{ "delay_battle_damage",                &battle_config.delay_battle_damage,             1,      0,      1,              },
	{ "hide_woe_damage",                    &battle_config.hide_woe_damage,                 0,      0,      1,              },
	{ "display_version",                    &battle_config.display_version,                 1,      0,      1,              },
	{ "display_hallucination",              &battle_config.display_hallucination,           1,      0,      1,              },
	{ "use_statpoint_table",                &battle_config.use_statpoint_table,             1,      0,      1,              },
	{ "ignore_items_gender",                &battle_config.ignore_items_gender,             1,      0,      1,              },
	{ "berserk_cancels_buffs",              &battle_config.berserk_cancels_buffs,           0,      0,      1,              },
	{ "debuff_on_logout",                   &battle_config.debuff_on_logout,                1|2,    0,      1|2,            },
	{ "monster_ai",                         &battle_config.mob_ai,                          0x000,  0x000,  0x7FF,          },
	{ "hom_setting",                        &battle_config.hom_setting,                     0xFFFF, 0x0000, 0xFFFF,         },
	{ "dynamic_mobs",                       &battle_config.dynamic_mobs,                    1,      0,      1,              },
	{ "mob_remove_damaged",                 &battle_config.mob_remove_damaged,              1,      0,      1,              },
	{ "show_hp_sp_drain",                   &battle_config.show_hp_sp_drain,                0,      0,      1,              },
	{ "show_hp_sp_gain",                    &battle_config.show_hp_sp_gain,                 1,      0,      1,              },
	{ "mob_npc_event_type",                 &battle_config.mob_npc_event_type,              1,      0,      1,              },
	{ "mob_clear_delay",                    &battle_config.mob_clear_delay,                 0,      0,      INT_MAX,        },
	{ "character_size",                     &battle_config.character_size,                  1|2,    0,      1|2,            },
	{ "mob_max_skilllvl",                   &battle_config.mob_max_skilllvl,                MAX_SKILL_LEVEL, 1, MAX_SKILL_LEVEL, },
	{ "retaliate_to_master",                &battle_config.retaliate_to_master,             1,      0,      1,              },
	{ "rare_drop_announce",                 &battle_config.rare_drop_announce,              0,      0,      10000,          },
	{ "title_lvl1",                         &battle_config.title_lvl1,                      1,      0,      100,            },
	{ "title_lvl2",                         &battle_config.title_lvl2,                      10,     0,      100,            },
	{ "title_lvl3",                         &battle_config.title_lvl3,                      20,     0,      100,            },
	{ "title_lvl4",                         &battle_config.title_lvl4,                      40,     0,      100,            },
	{ "title_lvl5",                         &battle_config.title_lvl5,                      50,     0,      100,            },
	{ "title_lvl6",                         &battle_config.title_lvl6,                      60,     0,      100,            },
	{ "title_lvl7",                         &battle_config.title_lvl7,                      80,     0,      100,            },
	{ "title_lvl8",                         &battle_config.title_lvl8,                      99,     0,      100,            },
	{ "duel_allow_pvp",                     &battle_config.duel_allow_pvp,                  0,      0,      1,              },
	{ "duel_allow_gvg",                     &battle_config.duel_allow_gvg,                  0,      0,      1,              },
	{ "duel_allow_teleport",                &battle_config.duel_allow_teleport,             0,      0,      1,              },
	{ "duel_autoleave_when_die",            &battle_config.duel_autoleave_when_die,         1,      0,      1,              },
	{ "duel_time_interval",                 &battle_config.duel_time_interval,              60,     0,      INT_MAX,        },
	{ "duel_only_on_same_map",              &battle_config.duel_only_on_same_map,           0,      0,      1,              },
	{ "skip_teleport_lv1_menu",             &battle_config.skip_teleport_lv1_menu,          0,      0,      1,              },
	{ "allow_skill_without_day",            &battle_config.allow_skill_without_day,         0,      0,      1,              },
	{ "allow_es_magic_player",              &battle_config.allow_es_magic_pc,               0,      0,      1,              },
	{ "skill_caster_check",                 &battle_config.skill_caster_check,              1,      0,      1,              },
	{ "status_cast_cancel",                 &battle_config.sc_castcancel,                   BL_NUL, BL_NUL, BL_ALL,         },
	{ "pc_status_def_rate",                 &battle_config.pc_sc_def_rate,                  100,    0,      INT_MAX,        },
	{ "mob_status_def_rate",                &battle_config.mob_sc_def_rate,                 100,    0,      INT_MAX,        },
	{ "pc_max_status_def",                  &battle_config.pc_max_sc_def,                   100,    0,      INT_MAX,        },
	{ "mob_max_status_def",                 &battle_config.mob_max_sc_def,                  100,    0,      INT_MAX,        },
	{ "sg_miracle_skill_ratio",             &battle_config.sg_miracle_skill_ratio,          1,      0,      10000,          },
	{ "sg_angel_skill_ratio",               &battle_config.sg_angel_skill_ratio,            10,     0,      10000,          },
	{ "autospell_stacking",                 &battle_config.autospell_stacking,              0,      0,      1,              },
	{ "override_mob_names",                 &battle_config.override_mob_names,              0,      0,      2,              },
	{ "min_chat_delay",                     &battle_config.min_chat_delay,                  0,      0,      INT_MAX,        },
	{ "friend_auto_add",                    &battle_config.friend_auto_add,                 1,      0,      1,              },
	{ "hom_rename",                         &battle_config.hom_rename,                      0,      0,      1,              },
	{ "homunculus_show_growth",             &battle_config.homunculus_show_growth,          0,      0,      1,              },
	{ "homunculus_friendly_rate",           &battle_config.homunculus_friendly_rate,        100,    0,      INT_MAX,        },
	{ "vending_tax",                        &battle_config.vending_tax,                     0,      0,      10000,          },
	{ "day_duration",                       &battle_config.day_duration,                    0,      0,      INT_MAX,        },
	{ "night_duration",                     &battle_config.night_duration,                  0,      0,      INT_MAX,        },
	{ "mob_remove_delay",                   &battle_config.mob_remove_delay,                60000,  1000,   INT_MAX,        },
	{ "mob_active_time",                    &battle_config.mob_active_time,                 0,      0,      INT_MAX,        },
	{ "boss_active_time",                   &battle_config.boss_active_time,                0,      0,      INT_MAX,        },
	{ "sg_miracle_skill_duration",          &battle_config.sg_miracle_skill_duration,       3600000, 0,     INT_MAX,        },
	{ "hvan_explosion_intimate",            &battle_config.hvan_explosion_intimate,         45000,  0,      100000,         },
	{ "quest_exp_rate",                     &battle_config.quest_exp_rate,                  100,    0,      INT_MAX,        },
	{ "at_mapflag",                         &battle_config.autotrade_mapflag,               0,      0,      1,              },
	{ "at_timeout",                         &battle_config.at_timeout,                      0,      0,      INT_MAX,        },
	{ "homunculus_autoloot",                &battle_config.homunculus_autoloot,             0,      0,      1,              },
	{ "idle_no_autoloot",                   &battle_config.idle_no_autoloot,                0,      0,      INT_MAX,        },
	{ "max_guild_alliance",                 &battle_config.max_guild_alliance,              3,      0,      3,              },
	{ "ksprotection",                       &battle_config.ksprotection,                    5000,   0,      INT_MAX,        },
	{ "auction_feeperhour",                 &battle_config.auction_feeperhour,              12000,  0,      INT_MAX,        },
	{ "auction_maximumprice",               &battle_config.auction_maximumprice,            500000000, 0,   MAX_ZENY,       },
	{ "gm_viewequip_min_lv",                &battle_config.gm_viewequip_min_lv,             0,      0,      99,             },
	{ "homunculus_auto_vapor",              &battle_config.homunculus_auto_vapor,           1,      0,      1,              },
	{ "display_status_timers",              &battle_config.display_status_timers,           1,      0,      1,              },
	{ "skill_add_heal_rate",                &battle_config.skill_add_heal_rate,            39,      0,      INT_MAX,        },
	{ "eq_single_target_reflectable",       &battle_config.eq_single_target_reflectable,    1,      0,      1,              },
	{ "invincible.nodamage",                &battle_config.invincible_nodamage,             0,      0,      1,              },
	{ "mob_slave_keep_target",              &battle_config.mob_slave_keep_target,           0,      0,      1,              },
	{ "autospell_check_range",              &battle_config.autospell_check_range,           0,      0,      1,              },
	{ "knockback_left",                     &battle_config.knockback_left,                  1,      0,      1,				},
	{ "client_reshuffle_dice",              &battle_config.client_reshuffle_dice,           0,      0,      1,              },
	{ "client_sort_storage",                &battle_config.client_sort_storage,             0,      0,      1,              },
	{ "gm_check_minlevel",                  &battle_config.gm_check_minlevel,               60,     0,      100,            },
	{ "feature.buying_store",               &battle_config.feature_buying_store,            1,      0,      1,              },
	{ "feature.search_stores",              &battle_config.feature_search_stores,           1,      0,      1,              },
	{ "searchstore_querydelay",             &battle_config.searchstore_querydelay,         10,      0,      INT_MAX,        },
	{ "searchstore_maxresults",             &battle_config.searchstore_maxresults,         30,      1,      INT_MAX,        },
	{ "display_party_name",                 &battle_config.display_party_name,              0,      0,      1,              },
	{ "cashshop_show_points",               &battle_config.cashshop_show_points,            0,      0,      1,              },
	{ "mail_show_status",                   &battle_config.mail_show_status,                0,      0,      2,              },
	{ "client_limit_unit_lv",               &battle_config.client_limit_unit_lv,            0,      0,      BL_ALL,         },
// BattleGround Settings
	{ "bg_update_interval",                 &battle_config.bg_update_interval,              1000,   100,    INT_MAX,        },
	{ "bg_short_attack_damage_rate",        &battle_config.bg_short_damage_rate,            80,     0,      INT_MAX,        },
	{ "bg_long_attack_damage_rate",         &battle_config.bg_long_damage_rate,             80,     0,      INT_MAX,        },
	{ "bg_weapon_attack_damage_rate",       &battle_config.bg_weapon_damage_rate,           60,     0,      INT_MAX,        },
	{ "bg_magic_attack_damage_rate",        &battle_config.bg_magic_damage_rate,            60,     0,      INT_MAX,        },
	{ "bg_misc_attack_damage_rate",         &battle_config.bg_misc_damage_rate,             60,     0,      INT_MAX,        },
	{ "bg_flee_penalty",                    &battle_config.bg_flee_penalty,                 20,     0,      INT_MAX,        },
	{ "bg_queue",                           &battle_config.bg_queue,                        1,      0,      1,              },
	{ "bg_queue_maximum_afk_seconds",       &battle_config.bg_queue_maximum_afk_seconds,    30,     0,      INT_MAX,        },
// 15-3athena
	{ "renewal_casting_renewal_skills",		&battle_config.renewal_casting_renewal_skills,	1,		0,		1,				},
	{ "renewal_casting_square_calc",        &battle_config.renewal_casting_square_calc,     1,      0,      1,              },
	{ "renewal_casting_square_debug",       &battle_config.renewal_casting_square_debug,    0,      0,      1,              },
	{ "castrate_dex_scale_renewal_jobs",	&battle_config.castrate_dex_scale_renewal_jobs,	150,	1,		INT_MAX,		},
	{ "cooldown_rate",                      &battle_config.cooldown_rate,                   100,    0,      INT_MAX,        },
	{ "min_skill_cooldown_limit",           &battle_config.min_skill_cooldown_limit,        0,      0,      INT_MAX,        },
	{ "no_skill_cooldown",                  &battle_config.no_skill_cooldown,               BL_MOB, BL_NUL, BL_ALL,         },
	{ "max_parameter_renewal_jobs",         &battle_config.max_parameter_renewal_jobs,      130,    10,     10000,          },
	{ "max_baby_parameter_renewal_jobs",    &battle_config.max_baby_parameter_renewal_jobs, 117,    10,     10000,          },
	{ "renewal_stats_handling",             &battle_config.renewal_stats_handling,          1,      0,      1,              },
	{ "max_aspd_renewal_jobs",              &battle_config.max_aspd_renewal_jobs,           193,    100,    199,            },
	{ "hanbok_ignorepalette",				&battle_config.hanbok_ignorepalette,			0,		0,		1,				},
	{ "oktoberfest_ignorepalette",          &battle_config.oktoberfest_ignorepalette,       0,      0,      1,				},
	{ "summer2_ignorepalette",				&battle_config.summer2_ignorepalette,			0,		0,		1,				},
	{ "all_riding_speed",					&battle_config.all_riding_speed,				25,     0,      100,            },
	{ "falcon_and_wug",                     &battle_config.falcon_and_wug,                  0,      0,      1,              },
	{ "transform_end_on_death",             &battle_config.transform_end_on_death,          1,      0,      1,              },
	{ "renewal_level_effect_skills",		&battle_config.renewal_level_effect_skills,		1,		0,		1,				},
	{ "load_custom_exp_tables",             &battle_config.load_custom_exp_tables,          0,      0,      1,              },
	{ "base_lv_skill_effect_limit",         &battle_config.base_lv_skill_effect_limit,      175,    1,      SHRT_MAX,       },
	{ "job_lv_skill_effect_limit",          &battle_config.job_lv_skill_effect_limit,       60,     1,      SHRT_MAX,       },
	{ "metallicsound_spburn_rate",          &battle_config.metallicsound_spburn_rate,       100,    0,      INT_MAX,        },
	{ "cashshop_price_rate",                &battle_config.cashshop_price_rate,             100,    0,      INT_MAX,        },
	{ "death_penalty_maxlv",                &battle_config.death_penalty_maxlv,             0,      0,      3,              }, 
	{ "mado_skill_limit",                   &battle_config.mado_skill_limit,                0,      0,      1,              },
	{ "mado_loss_on_death",                 &battle_config.mado_loss_on_death,              1,      0,      1,              },
	{ "mado_cast_skill_on_limit",           &battle_config.mado_cast_skill_on_limit,        0,      0,      1,              },
	{ "marionette_renewal_jobs",            &battle_config.marionette_renewal_jobs,         0,      0,      1,              },
	{ "banana_bomb_sit_duration",           &battle_config.banana_bomb_sit_duration,        1,      0,      1,				},
	{ "gc_skill_edp_boost_formula_a",       &battle_config.gc_skill_edp_boost_formula_a,    0,      0,      1000,			},
	{ "gc_skill_edp_boost_formula_b",       &battle_config.gc_skill_edp_boost_formula_b,    20,     0,      1000,			},
	{ "gc_skill_edp_boost_formula_c",       &battle_config.gc_skill_edp_boost_formula_c,    1,      0,      1,				},
	{ "mob_spawn_variance",                 &battle_config.mob_spawn_variance,              1,      0,      3,              },
	{ "slave_stick_with_master",            &battle_config.slave_stick_with_master,         0,      0,      1,              },
	{ "skill_amotion_leniency",             &battle_config.skill_amotion_leniency,          0,      0,      300,			},
	// Cell PVP [Napster]
	{ "cellpvp_deathmatch",					&battle_config.cellpvp_deathmatch,              1,      0,      1,              },
	{ "cellpvp_deathmatch_delay",           &battle_config.cellpvp_deathmatch_delay,        1000,   0,      INT_MAX,        },
	{ "deathmatch_hp_rate",					&battle_config.deathmatch_hp_rate,				0,		0,		100,			},
	{ "deathmatch_sp_rate",					&battle_config.deathmatch_sp_rate,				0,		0,		100,			},
	{ "cellpvp_autobuff",					&battle_config.cellpvp_autobuff,                1,      0,      1,              },
	{ "cellpvp_party_enable",               &battle_config.cellpvp_party_enable,            1,      0,      1,              },
	{ "cellpvp_guild_enable",               &battle_config.cellpvp_guild_enable,            1,      0,      1,              },
	{ "cellpvp_walkout_delay",              &battle_config.cellpvp_walkout_delay,           5000,   0,      INT_MAX,        },
	//New Guild configs
	{ "create_guild",                       &battle_config.guild_create,                    3,      0,      3,              },
	{ "break_guild",                        &battle_config.guild_break,                     3,      0,      3,              },
	{ "disable_invite",                     &battle_config.guild_disable_invite,           -1,     -1,      15,             },
	{ "disable_expel",                      &battle_config.guild_disable_expel,            -1,     -1,      15,             },
	//Other (renewal) Features
	{ "feature.auction",                    &battle_config.feature_auction,                 0,      0,      2,				},
	{ "feature.banking",                    &battle_config.feature_banking,                 1,      0,      1,              },
	{ "mvp_tomb_enabled",					&battle_config.mvp_tomb_enabled,				1,      0,      1				}, 
	{ "mvp_tomb_delay",                     &battle_config.mvp_tomb_delay,                  9000,   0,      INT_MAX,		},
	{ "feature.roulette",                   &battle_config.feature_roulette,                1,      0,      1,              },
	{ "monster_hp_bars_info",               &battle_config.monster_hp_bars_info,            1,      0,      1,              },
	{ "save_body_style",                    &battle_config.save_body_style,                 0,      0,      1,              },
	{ "costume_refine_def",                 &battle_config.costume_refine_def,              0,      0,      1,              },
	{ "shadow_refine_def",                  &battle_config.shadow_refine_def,               0,      0,      1,              },
	{ "mail_delay",                         &battle_config.mail_delay,                      1000,   1000,   INT_MAX,        },
	{ "mail_daily_count",					&battle_config.mail_daily_count,				100,	0,		INT32_MAX,		},
	{ "mail_zeny_fee",						&battle_config.mail_zeny_fee,					2,		0,		100,			},
	{ "mail_attachment_price",				&battle_config.mail_attachment_price,			2500,	0,		INT32_MAX,		},
	{ "mail_attachment_weight",				&battle_config.mail_attachment_weight,			2000,	0,		INT32_MAX,		},
	{ "feature.achievement",				&battle_config.feature_achievement,				1,		0,		1,				},
	{ "max_homunculus_hp",                  &battle_config.max_homunculus_hp,               32767,  100,    INT_MAX,        },
	{ "max_homunculus_sp",                  &battle_config.max_homunculus_sp,               32767,  100,    INT_MAX,        },
	{ "max_homunculus_parameter",           &battle_config.max_homunculus_parameter,        175,    10,     SHRT_MAX,       },
	{ "hom_bonus_exp_from_master",          &battle_config.hom_bonus_exp_from_master,       10,     0,      100,            },
	{ "feature.equipswitch",                &battle_config.feature_equipswitch,             1,      0,      1,              },
	{ "mvp_exp_reward_message",             &battle_config.mvp_exp_reward_message,          0,      0,      1,				},
	{ "attendance_system",					&battle_config.feature_attendance_system,		1,		0,      1,				},
	{ "attendance_show_window",				&battle_config.attendance_show_window,			1,		0,      1,				},
	{ "attendance_starttime",				&battle_config.attendance_starttime,			1,      0,      99999999,		},
	{ "attendance_endtime",					&battle_config.attendance_endtime,				1,      0,      99999999,		},
	{ "player_baselv_req_skill",            &battle_config.player_baselv_req_skill,         1,      0,      1,              },
	{ "warmer_show_heal",                   &battle_config.warmer_show_heal,                0,      0,      1,              },
	{ "baby_hp_sp_penalty",                 &battle_config.baby_hp_sp_penalty,              1,      0,      1,              },
	{ "baby_crafting_penalty",              &battle_config.baby_crafting_penalty,           1,      0,      1,              },
	{ "allow_bloody_lust_on_boss",          &battle_config.allow_bloody_lust_on_boss,       1,      0,      1,              },
	{ "allow_bloody_lust_on_warp",          &battle_config.allow_bloody_lust_on_warp,       1,      0,      1,              },
	{ "homunculus_pyroclastic_autocast",    &battle_config.homunculus_pyroclastic_autocast, 0,      0,      1,              },
	{ "pyroclastic_breaks_weapon",          &battle_config.pyroclastic_breaks_weapon,       1,      0,      1,              },
	{ "millennium_shield_health",           &battle_config.millennium_shield_health,        1000,   1,      INT_MAX,		},
	{ "giant_growth_behavior",              &battle_config.giant_growth_behavior,           1,      0,      1,              },
	{ "mass_spiral_max_def",                &battle_config.mass_spiral_max_def,             50,     0,      SHRT_MAX,       },
	{ "rebel_base_lv_skill_effect",         &battle_config.rebel_base_lv_skill_effect,      1,      0,      1,              },
	{ "hesperuslit_bonus_stack",            &battle_config.hesperuslit_bonus_stack,         0,      0,      1,              },
	{ "homun_autofeed",						&battle_config.homun_autofeed,					1,      0,      1,				},
	{ "drop_connection_on_quit",            &battle_config.drop_connection_on_quit,         0,      0,      1,				},
	{ "cart_revo_knockback",                &battle_config.cart_revo_knockback,             1,      0,      1,				},
	{ "guild_notice_changemap",             &battle_config.guild_notice_changemap,          2,      0,      2,				},
	{ "item_auto_identify",                 &battle_config.item_auto_identify,              0,      0,      1,              },
	{ "natural_homun_healhp_interval",      &battle_config.natural_homun_healhp_interval,   3000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "natural_homun_healsp_interval",      &battle_config.natural_homun_healsp_interval,   4000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "elemental_ai",                       &battle_config.elem_ai,                         0x000,  0x000,  0x77F,          },
	{ "elem_defensive_support",             &battle_config.elem_defensive_support,          0,      0,      1,              },
	{ "elem_defensive_attack_skill",        &battle_config.elem_defensive_attack_skill,     0,      0,      1,              },
	{ "elem_offensive_skill_chance",        &battle_config.elem_offensive_skill_chance,     5,      0,      100,            },
	{ "elem_offensive_skill_casttime",      &battle_config.elem_offensive_skill_casttime,   1000,   0,      60000,          },
	{ "elem_offensive_skill_aftercast",     &battle_config.elem_offensive_skill_aftercast,  10000,  0,      60000,          },
	{ "elemental_masters_walk_speed",       &battle_config.elemental_masters_walk_speed,    1,      0,      1,              },
	{ "natural_elem_heal_interval",         &battle_config.natural_elem_heal_interval,      3000,   NATURAL_HEAL_INTERVAL, INT_MAX, },
	{ "max_elemental_hp",                   &battle_config.max_elemental_hp,                1000000,100,    1000000000,     },
	{ "max_elemental_sp",                   &battle_config.max_elemental_sp,                1000000,100,    1000000000,     },
	{ "max_elemental_def_mdef",             &battle_config.max_elemental_def_mdef,          99,     0,      99,             },
	{ "path_blown_halt",                    &battle_config.path_blown_halt,                 1,      0,      1,				},
	{ "taekwon_mission_mobname",            &battle_config.taekwon_mission_mobname,         0,      0,      2,				},
	{ "teleport_on_portal",                 &battle_config.teleport_on_portal,              0,      0,      1,				},
	{ "min_npc_vendchat_distance",           &battle_config.min_npc_vendchat_distance,	    3,      0,      100,            },
	{ "skill_trap_type",                    &battle_config.skill_trap_type,                 0,      0,      3,				},
	{ "item_enabled_npc",					&battle_config.item_enabled_npc,				1,      0,      1,				},
	{ "item_flooritem_check",               &battle_config.item_onfloor,                   1,      0,      1,				},
	{ "emblem_woe_change",                  &battle_config.emblem_woe_change,                0,     0,      1,				},
	{ "emblem_transparency_limit",          &battle_config.emblem_transparency_limit,       80,     0,    100,				},
	{ "discount_item_point_shop",			&battle_config.discount_item_point_shop,		0,		0,		3,				},
	{ "allow_consume_restricted_item",      &battle_config.allow_consume_restricted_item,   1,      0,      1,				},
	{ "allow_equip_restricted_item",        &battle_config.allow_equip_restricted_item,     1,      0,      1,				},
	{ "guild_leaderchange_delay",			&battle_config.guild_leaderchange_delay,		1440,	0,		INT32_MAX,		},
	{ "guild_leaderchange_woe",				&battle_config.guild_leaderchange_woe,			0,		0,		1,				},
	{ "default_bind_on_equip",              &battle_config.default_bind_on_equip,           BOUND_CHAR, BOUND_NONE, BOUND_MAX - 1, },
	{ "devotion_rdamage_skill_only",        &battle_config.devotion_rdamage_skill_only,     1,      0,      1,				},
	{ "monster_chase_refresh",              &battle_config.mob_chase_refresh,               1,      0,      30,				},
	{ "magiccrasher_renewal",				&battle_config.magiccrasher_renewal,			0,      0,      1,				},
	{ "arrow_shower_knockback",             &battle_config.arrow_shower_knockback,          1,      0,      1,				},
	{ "min_npc_vending_distance",           &battle_config.min_npc_vending_distance,		3,      0,      100				},
	{ "spawn_direction",                    &battle_config.spawn_direction,                 0,      0,      1,				},
	{ "mob_icewall_walk_block",             &battle_config.mob_icewall_walk_block,         75,      0,      255,			},
	{ "boss_icewall_walk_block",            &battle_config.boss_icewall_walk_block,         0,      0,      255,			},
	{ "snap_dodge",                         &battle_config.snap_dodge,                      0,      0,      1,				},
	{ "stormgust_knockback",                &battle_config.stormgust_knockback,             1,      0,      1,				},
	{ "monster_loot_search_type",           &battle_config.monster_loot_search_type,        1,      0,      1,				},
	//Episode System [15peaces]
	{ "feature.episode",					&battle_config.feature_episode,		           152,    10,      152,            },
	{ "episode.readdb",						&battle_config.episode_readdb,		           0,		0,      1,              },
};


int battle_set_value(const char* w1, const char* w2)
{
	int val = config_switch(w2);

	int i;
	ARR_FIND(0, ARRAYLENGTH(battle_data), i, strcmpi(w1, battle_data[i].str) == 0);
	if (i == ARRAYLENGTH(battle_data))
		return 0; // not found

	if (val < battle_data[i].min || val > battle_data[i].max)
	{
		ShowWarning("Value for setting '%s': %s is invalid (min:%i max:%i)! Defaulting to %i...\n", w1, w2, battle_data[i].min, battle_data[i].max, battle_data[i].defval);
		val = battle_data[i].defval;
	}

	*battle_data[i].val = val;
	return 1;
}

int battle_get_value(const char* w1)
{
	int i;
	ARR_FIND(0, ARRAYLENGTH(battle_data), i, strcmpi(w1, battle_data[i].str) == 0);
	if (i == ARRAYLENGTH(battle_data))
		return 0; // not found
	else
		return *battle_data[i].val;
}

void battle_set_defaults()
{
	int i;
	for (i = 0; i < ARRAYLENGTH(battle_data); i++)
		*battle_data[i].val = battle_data[i].defval;
}

void battle_adjust_conf()
{
	battle_config.monster_max_aspd = 2000 - battle_config.monster_max_aspd*10;
	battle_config.max_aspd = 2000 - battle_config.max_aspd*10;
	battle_config.max_aspd_renewal_jobs = 2000 - battle_config.max_aspd_renewal_jobs*10;
	battle_config.max_walk_speed = 100*DEFAULT_WALK_SPEED/battle_config.max_walk_speed;	
	battle_config.max_cart_weight *= 10;
	
	if(battle_config.max_def > 100 && !battle_config.weapon_defense_type)	 // added by [Skotlex]
		battle_config.max_def = 100;

	if(battle_config.min_hitrate > battle_config.max_hitrate)
		battle_config.min_hitrate = battle_config.max_hitrate;
		
	if(battle_config.pet_max_atk1 > battle_config.pet_max_atk2)	//Skotlex
		battle_config.pet_max_atk1 = battle_config.pet_max_atk2;
	
	if (battle_config.day_duration && battle_config.day_duration < 60000) // added by [Yor]
		battle_config.day_duration = 60000;
	if (battle_config.night_duration && battle_config.night_duration < 60000) // added by [Yor]
		battle_config.night_duration = 60000;
	
#if PACKETVER > 20120000 && PACKETVER < 20130515 /* Exact date (when it started) not known */
	if (battle_config.feature_auction) {
		ShowWarning("conf/battle/feature.conf:feature.auction is enabled but it is not stable on PACKETVER " EXPAND_AND_QUOTE(PACKETVER) ", disabling...\n");
		ShowWarning("conf/battle/feature.conf:feature.auction change value to '2' to silence this warning and maintain it enabled\n");
		battle_config.feature_auction = 0;
}
#elif PACKETVER >= 20141112
	if (battle_config.feature_auction) {
		ShowWarning("conf/battle/feature.conf:feature.auction is enabled but it is not available for clients from 2014-11-12 on, disabling...\n");
		ShowWarning("conf/battle/feature.conf:feature.auction change value to '2' to silence this warning and maintain it enabled\n");
		battle_config.feature_auction = 0;
	}
#endif

#if PACKETVER < 20130724
	if( battle_config.feature_banking ) {
		ShowWarning("conf/battle/feature.conf banking is enabled but it requires PACKETVER 2013-07-24 or newer, disabling...\n");
		battle_config.feature_banking = 0;
	}
#endif 

#if PACKETVER < 20141022
	if (battle_config.feature_roulette) {
		ShowWarning("conf/battle/feature.conf roulette is enabled but it requires PACKETVER 2014-10-22 or newer, disabling...\n");
		battle_config.feature_roulette = 0;
	}
#endif

#if PACKETVER < 20150513
	if (battle_config.feature_achievement) {
		ShowWarning("conf/battle/feature.conf achievement is enabled but it requires PACKETVER 2015-05-13 or newer, disabling...\n");
		battle_config.feature_achievement = 0;
	}
#endif

#if PACKETVER < 20170208
	if (battle_config.feature_equipswitch) {
		ShowWarning("conf/battle/feature.conf equip switch is enabled but it requires PACKETVER 2017-02-08 or newer, disabling...\n");
		battle_config.feature_equipswitch = 0;
	}
#endif

#ifndef CELL_NOSTACK
	if (battle_config.custom_cell_stack_limit != 1)
		ShowWarning("Battle setting 'custom_cell_stack_limit ' takes no effect as this server was compiled without Cell Stack Limit support.\n");
#endif
}

int battle_config_read(const char* cfgName)
{
	FILE* fp;
	static int count = 0;

	if (count == 0)
		battle_set_defaults();

	count++;

	fp = fopen(cfgName,"r");
	if (fp == NULL)
		ShowError("File not found: %s\n", cfgName);
	else
	{
		char line[1024], w1[1024], w2[1024];

		while(fgets(line, sizeof(line), fp))
		{
			if (line[0] == '/' && line[1] == '/')
				continue;
			if (sscanf(line, "%1023[^:]:%1023s", w1, w2) != 2)
				continue;
			if (strcmpi(w1, "import") == 0)
				battle_config_read(w2);
			else
			if (battle_set_value(w1, w2) == 0)
				ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
		}

		fclose(fp);
	}

	count--;

	if (count == 0)
		battle_adjust_conf();

	return 0;
}

void do_init_battle(void)
{
	delay_damage_ers = ers_new(sizeof(struct delay_damage), "battle.c::delay_damage_ers", ERS_OPT_CLEAR);
	add_timer_func_list(battle_delay_damage_sub, "battle_delay_damage_sub");
}

void do_final_battle(void)
{
	ers_destroy(delay_damage_ers);
}
