// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "itemdb.h"
#include "map.h"
#include "battle.h" // struct battle_config
#include "script.h" // item script processing
#include "pc.h"     // W_MUSICAL, W_WHIP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DBMap *itemdb;
static DBMap *itemdb_randomopt; /// Random option DB
static DBMap *itemdb_group;
static DBMap *itemdb_package;

struct item_data *dummy_item; //This is the default dummy item used for non-existant items. [Skotlex]

/**
* Check if item group exists
* @param group_id
* @return NULL if not exist, or item_group *
*/
struct item_group *itemdb_group_exists(unsigned short group_id)
{
	return (struct item_group *)uidb_get(itemdb_group, group_id);
}

/**
* Check if item package exists
* @param package_id
* @return NULL if not exist, or item_package *
*/
struct item_package *itemdb_package_exists(unsigned short package_id)
{
	return (struct item_package *)uidb_get(itemdb_package, package_id);
}

/**
 * Check if an item exists from a group in a player's inventory
 * @param group_id: Item Group ID
 * @return Item's index if found or -1 otherwise
 */
int itemdb_group_item_exists_pc(struct map_session_data *sd, unsigned short group_id)
{
	struct item_group *group = (struct item_group *)uidb_get(itemdb_group, group_id);

	if (!group)
		return -1;

	for (int i = 0; i < group->qty; i++) {
		int item_position = pc_search_inventory(sd, group->nameid[i]);
		
		if (item_position != -1)
			return item_position;
	}

	return -1;
}

/*==========================================
 * 名前で検索用
 *------------------------------------------*/
// name = item alias, so we should find items aliases first. if not found then look for "jname" (full name)
static int itemdb_searchname_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *item = db_data2ptr(data), **dst, **dst2;
	char *str;
	str=va_arg(ap,char *);
	dst=va_arg(ap,struct item_data **);
	dst2=va_arg(ap,struct item_data **);

	//Absolute priority to Aegis code name.
	if( strcmpi(item->name,str)==0 )
		*dst = item;

	//Second priority to Client displayed name.
	if( strcmpi(item->jname,str)==0 )
		*dst2=item;
	return 0;
}

/*==========================================
 * Return item data from item name. (lookup)
 * @param str Item Name
 * @return item data
 *------------------------------------------*/
struct item_data* itemdb_searchname(const char *str)
{
	struct item_data *item = NULL, *item2 = NULL;

	itemdb->foreach(itemdb,itemdb_searchname_sub,str,&item,&item2);
	return item?item:item2;
}

static int itemdb_searchname_array_sub(DBKey key, DBData data, va_list ap)
{
	struct item_data *item = db_data2ptr(&data);
	char *str = va_arg(ap, char *);

	if(stristr(item->jname,str))
		return 0;
	if(stristr(item->name,str))
		return 0;
	return strcmpi(item->jname,str);
}

/*==========================================
 * Founds up to N matches. Returns number of matches [Skotlex]
 * @param *data
 * @param size
 * @param str
 * @return Number of matches item
 *------------------------------------------*/
int itemdb_searchname_array(struct item_data** data, int size, const char *str)
{
	DBData *db_data[MAX_SEARCH];
	int i, count = 0, db_count;

	db_count = itemdb->getall(itemdb, (DBData**)&db_data, size, itemdb_searchname_array_sub, str);
	for (i = 0; i < db_count && count < size; i++)
		data[count++] = (struct item_data*)db_data2ptr(db_data[i]);

	return count;
}

void itemdb_package_item(struct map_session_data *sd, int packageid) 
{
	int i = 0, get_count, j, flag;

	nullpo_retv(sd);

	struct item it;
	struct item_package *package = (struct item_package *) uidb_get(itemdb_package, packageid);

	if (!package) {
		ShowWarning("itemdb_package_item: Unable to find package with id %d\n", packageid);
		return;
	}

	memset(&it, 0, sizeof(it));

	// "Must"-Items
	for (i = 0; i < package->count; i++)
	{
		if (package->entry[i]->group == 0)
		{
			it.nameid = package->entry[i]->nameid;
			it.identify = itemdb_isstackable(it.nameid) ? 1 : 0; // should not be identified by default?
			get_count = itemdb_isstackable(it.nameid) ? package->entry[i]->amount : 1;
			it.amount = get_count == 1 ? 1 : get_count;
			it.expire_time = (package->entry[i]->duration) ? (unsigned int)(time(NULL) + package->entry[i]->duration * 60) : 0;
			it.unique_id = package->entry[i]->guid ? pc_generate_unique_id(sd) : 0; // Generate UID

			for (j = 0; j < package->entry[i]->amount; j += get_count)
			{
				if ((flag = pc_additem(sd, &it, get_count, LOG_TYPE_SCRIPT)))
					clif_additem(sd, 0, 0, flag);
				else if (package->entry[i]->announced)
						clif_broadcast_obtain_special_item(sd->status.name, it.nameid, sd->itemid, ITEMOBTAIN_TYPE_BOXITEM);
			}
		}
	}

	// Random Items
	for (int cur_rand = 1; cur_rand <= package->max_rand; cur_rand++)
	{
		int qty = 0;
		uint16 r = 0;
		struct item_package_entry *tmp[MAX_RANDITEM];

		// First, get the current random group separated
		for (i = 0; i < package->count; i++)
		{
			if (package->entry[i]->group == cur_rand)
			{
				for (j = 0; j < package->entry[i]->prob; j++)
				{
					CREATE(tmp[qty], struct item_package_entry, 1);

					tmp[qty]->nameid = package->entry[i]->nameid;
					tmp[qty]->amount = package->entry[i]->amount;
					tmp[qty]->duration = package->entry[i]->duration;
					tmp[qty]->announced = package->entry[i]->announced;
					//tmp[qty]->prob = package->entry[i]->prob; // Not needed here...
					qty++;
				}
			}
		}

		// Now we'll get an random item of the current group.
		if (qty > 0)
		{
			r = rnd() % qty;

			it.nameid = tmp[r]->nameid;
			it.identify = itemdb_isstackable(it.nameid) ? 1 : 0; // should not be identified by default?
			get_count = itemdb_isstackable(it.nameid) ? tmp[r]->amount : 1;
			it.amount = get_count == 1 ? 1 : get_count;
			it.expire_time = (tmp[r]->duration) ? (unsigned int)(time(NULL) + tmp[r]->duration * 60) : 0;
			it.unique_id = tmp[r]->guid ? pc_generate_unique_id(sd) : 0; // Generate UID

			for (j = 0; j < tmp[r]->amount; j += get_count)
			{
				if ((flag = pc_additem(sd, &it, get_count, LOG_TYPE_SCRIPT)))
					clif_additem(sd, 0, 0, flag);
				else if (tmp[r]->announced)
					clif_broadcast_obtain_special_item(sd->status.name, it.nameid, sd->itemid, ITEMOBTAIN_TYPE_BOXITEM);
			}

			for (i = 0; i < qty; i++)
				aFree(tmp[i]);
		}
	}
}



/*==========================================
 * 箱系アイテム検索
 *------------------------------------------*/
int itemdb_searchrandomid(uint16 group_id)
{
	struct item_group *group = (struct item_group *) uidb_get(itemdb_group, group_id);

	if(!group) {
		ShowError("itemdb_searchrandomid: Invalid group id %d\n", group_id);
		return UNKNOWN_ITEM_ID;
	}
	if (group->qty)
		return group->nameid[rnd()%group->qty];
	
	ShowError("itemdb_searchrandomid: No item entries for group id %d\n", group_id);
	return UNKNOWN_ITEM_ID;
}

/*==========================================
 * Searches for the item_data. Use this to check if item exists or not.
 * @param nameid
 * @return *item_data if item is exist, or NULL if not
 *------------------------------------------*/
struct item_data* itemdb_exists(t_itemid nameid) {
	return ((struct item_data*)uidb_get(itemdb,nameid));
}

/// Returns human readable name for given item type.
/// @param type Type id to retrieve name for ( IT_* ).
const char* itemdb_typename(enum item_types type)
{
	switch(type)
	{
		case IT_HEALING:        return "Potion/Food";
		case IT_USABLE:         return "Usable";
		case IT_ETC:            return "Etc.";
		case IT_WEAPON:         return "Weapon";
		case IT_ARMOR:          return "Armor";
		case IT_CARD:           return "Card";
		case IT_PETEGG:         return "Pet Egg";
		case IT_PETARMOR:       return "Pet Accessory";
		case IT_AMMO:           return "Arrow/Ammunition";
		case IT_DELAYCONSUME:   return "Delay-Consume Usable";
		case IT_SHADOWGEAR:     return "Shadow Equipment";
		case IT_CASH:           return "Cash Usable";
	}
	return "Unknown Item Type";
}

/// Returns human readable name for given weapon type.
/// @param type Type id to retrieve name for ( W_* ).
const char* itemdb_weapon_typename(int type)
{
	switch(type)
	{
		case W_FIST:     return "Bare Fist";
		case W_DAGGER:   return "Dagger";
		case W_1HSWORD:  return "One-Handed Sword";
		case W_2HSWORD:  return "Two-Handed Sword";
		case W_1HSPEAR:  return "One-Handed Spear";
		case W_2HSPEAR:  return "Two-Handed Spear";
		case W_1HAXE:    return "One-Handed Axe";
		case W_2HAXE:    return "Two-Handed Axe";
		case W_MACE:     return "Mace";
		case W_2HMACE:   return "Two-Handed Mace";
		case W_STAFF:    return "Staff";
		case W_BOW:      return "Bow";
		case W_KNUCKLE:  return "Knuckle";
		case W_MUSICAL:  return "Musical Instrument";
		case W_WHIP:     return "Whip";
		case W_BOOK:     return "Book";
		case W_KATAR:    return "Katar";
		case W_REVOLVER: return "Revolver";
		case W_RIFLE:    return "Rifle";
		case W_GATLING:  return "Gatling Gun";
		case W_SHOTGUN:  return "Shotgun";
		case W_GRENADE:  return "Grenade Launcher";
		case W_HUUMA:    return "Huuma Shuriken";
		case W_2HSTAFF:  return "Two-Handed Staff";
	}
	return "Unknown Weapon Type";
}

/// Returns human readable name for given armor type.
/// @param type Type id to retrieve name for ( EQP_* ).
const char* itemdb_armor_typename(int type)
{
	switch(type)
	{	// Regular Equips
		case EQP_HEAD_TOP: return "Upper Head";
		case EQP_HEAD_MID: return "Middle Head";
		case EQP_HEAD_LOW: return "Lower Head";
		case EQP_HELM_TM: return "Upper/Middle Head";
		case EQP_HELM_TL: return "Upper/Lower Head";
		case EQP_HELM_ML: return "Middle/Lower Head";
		case EQP_HELM: return "Upper/Middle/Lower Head";
		case EQP_WEAPON: return "Right Hand/Weapon";
		case EQP_SHIELD: return "Left Hand/Shield";
		case EQP_ARMS: return "Left/Right Hand";
		case EQP_ARMOR: return "Body";
		case EQP_GARMENT: return "Robe";
		case EQP_SHOES: return "Shoes";
		case EQP_ACC_R: return "Right Accessory";
		case EQP_ACC_L: return "Left Accessory";
		case EQP_ACC: return "Accessory";
		case EQP_AMMO: return "Ammo";

		// Costume Equips
		case EQP_COSTUME_HEAD_TOP: return "Costume Upper Head";
		case EQP_COSTUME_HEAD_MID: return "Costume Middle Head";
		case EQP_COSTUME_HEAD_LOW: return "Costume Lower Head";
		case EQP_COSTUME_HELM_TM: return "Costume Upper/Middle Head";
		case EQP_COSTUME_HELM_TL: return "Costume Upper/Lower Head";
		case EQP_COSTUME_HELM_ML: return "Costume Middle/Lower Head";
		case EQP_COSTUME_HELM: return "Costume Upper/Middle/Lower Head";
		case EQP_COSTUME_GARMENT: return "Costume Robe";
		case EQP_COSTUME_FLOOR: return "Costume Floor";

		// Shadow Equips
		case EQP_SHADOW_ARMOR: return "Shadow Body";
		case EQP_SHADOW_WEAPON: return "Shadow Weapon";
		case EQP_SHADOW_SHIELD: return "Shadow Shield";
		case EQP_SHADOW_SHOES: return "Shadow Shoes";
		case EQP_SHADOW_ACC_R: return "Shadow Right Accessory";
		case EQP_SHADOW_ACC_L: return "Shadow Left Accessory";
		case EQP_SHADOW_ACC: return "Shadow Accessory";
	}
	return "Unknown Armor Type";
}

/// Returns human readable name for given ammo type.
/// @param type Type id to retrieve name for ( A_* ).
const char* itemdb_ammo_typename(int type)
{
	switch(type)
	{
		case A_ARROW:       return "Arrow";
		case A_DAGGER:      return "Throwing Dagger";
		case A_BULLET:      return "Bullet";
		case A_SHELL:       return "Shell";
		case A_GRENADE:     return "Grenade";
		case A_SHURIKEN:    return "Shuriken";
		case A_KUNAI:       return "Kunai";
		case A_CANNONBALL:  return "Cannon Ball";
		case A_THROWWEAPON: return "Throwable Item";
	}
	return "Unknown Ammo Type";
}

/*==========================================
 * Converts the jobid from the format in itemdb 
 * to the format used by the map server. [Skotlex]
 *------------------------------------------*/
static void itemdb_jobid2mapid(unsigned int *bclass, unsigned int jobmask)
{
	bclass[0] = bclass[1] = bclass[2] = 0;

	// Novice And 1st Jobs
	if (jobmask & 1 << 0)// Novice
	{// Novice and Super Novice share the same job equip mask.
		bclass[0] |= 1 << MAPID_NOVICE;
		bclass[1] |= 1 << MAPID_NOVICE;
	}
	if (jobmask & 1 << 1)// Swordman
		bclass[0] |= 1 << MAPID_SWORDMAN;
	if (jobmask & 1 << 2)// Magician
		bclass[0] |= 1 << MAPID_MAGE;
	if (jobmask & 1 << 3)// Archer
		bclass[0] |= 1 << MAPID_ARCHER;
	if (jobmask & 1 << 4)// Acolyte
	{// Acolyte and Gangsi share the same job equip mask.
		bclass[0] |= 1 << MAPID_ACOLYTE;
		bclass[0] |= 1 << MAPID_GANGSI;
	}
	if (jobmask & 1 << 5)// Merchant
		bclass[0] |= 1 << MAPID_MERCHANT;
	if (jobmask & 1 << 6)// Thief
		bclass[0] |= 1 << MAPID_THIEF;

	// 2nd Jobs (Branch 1)
	if (jobmask & 1 << 7)// Knight
	{// Knight and Death Knight share the same job equip mask.
		bclass[1] |= 1 << MAPID_SWORDMAN;
		bclass[1] |= 1 << MAPID_GANGSI;
	}
	if (jobmask & 1 << 8)// Priest
		bclass[1] |= 1 << MAPID_ACOLYTE;
	if (jobmask & 1 << 9)// Wizard
		bclass[1] |= 1 << MAPID_MAGE;
	if (jobmask & 1 << 10)// Blacksmith
		bclass[1] |= 1 << MAPID_MERCHANT;
	if (jobmask & 1 << 11)// Hunter
		bclass[1] |= 1 << MAPID_ARCHER;
	if (jobmask & 1 << 12)// Assassin
		bclass[1] |= 1 << MAPID_THIEF;

	// 2nd Jobs (Branch 2)
	if (jobmask & 1 << 14)// Crusader
		bclass[2] |= 1 << MAPID_SWORDMAN;
	if (jobmask & 1 << 15)// Monk
		bclass[2] |= 1 << MAPID_ACOLYTE;
	if (jobmask & 1 << 16)// Sage
	{// Sage and Dark Collector share the same job equip mask.
		bclass[2] |= 1 << MAPID_MAGE;
		bclass[2] |= 1 << MAPID_GANGSI;
	}
	if (jobmask & 1 << 17)// Rogue
		bclass[2] |= 1 << MAPID_THIEF;
	if (jobmask & 1 << 18)// Alchemist
		bclass[2] |= 1 << MAPID_MERCHANT;
	if (jobmask & 1 << 19)// Bard/Dancer
		bclass[2] |= 1 << MAPID_ARCHER;

	// Expanded Jobs
	if (jobmask & 1 << 21)// Taekwon
		bclass[0] |= 1 << MAPID_TAEKWON;
	if (jobmask & 1 << 22)// Star Gladiator
		bclass[1] |= 1 << MAPID_TAEKWON;
	if (jobmask & 1 << 23)// Soul Linker
		bclass[2] |= 1 << MAPID_TAEKWON;
	if (jobmask & 1 << 24)// Gunslinger
	{// Gunslinger and Rebellion share the same job equip mask.
		bclass[0] |= 1 << MAPID_GUNSLINGER;
		bclass[1] |= 1 << MAPID_GUNSLINGER;
	}
	if (jobmask & 1 << 25)// Ninja
	{// Ninja and Kagerou/Oboro share the same job equip mask.
		bclass[0] |= 1 << MAPID_NINJA;
		bclass[1] |= 1 << MAPID_NINJA;
	}
	if (jobmask & 1 << 26)// Summoner
		bclass[0] |= 1 << MAPID_SUMMONER;
}


/*==========================================
 * Create dummy item data
 *------------------------------------------*/
static void itemdb_create_dummy(void)
{
	CREATE(dummy_item, struct item_data, 1);
	memset(dummy_item, 0, sizeof(struct item_data));
	dummy_item->nameid = 500;
	dummy_item->weight = 1;
	dummy_item->value_sell = 1;
	dummy_item->type = IT_ETC; //Etc item

	safestrncpy(dummy_item->name, "UNKNOWN_ITEM", sizeof(dummy_item->name));
	safestrncpy(dummy_item->jname, "Unknown Item", sizeof(dummy_item->jname));

	dummy_item->view_id = UNKNOWN_ITEM_ID;
}

/*==========================================
* Create new item data
* @param nameid
 *------------------------------------------*/
static struct item_data *itemdb_create_item(t_itemid nameid)
{
	struct item_data *id;
	CREATE(id, struct item_data, 1);
	memset(id, 0, sizeof(struct item_data));
	id->nameid = nameid;
	id->type = IT_ETC; //Etc item
	idb_put(itemdb, nameid, id);
	return id;
}

/*==========================================
 * Loads an item from the db. If not found, it will return the dummy item.
 * @param nameid
 * @return *item_data or *dummy_item if item not found
 *------------------------------------------*/
struct item_data* itemdb_search(t_itemid nameid) {
	struct item_data* id = NULL;
	if (nameid == dummy_item->nameid)
		id = dummy_item;
	else if (!(id = (struct item_data*)uidb_get(itemdb, nameid))) {
		ShowWarning("itemdb_search: Item ID %u does not exists in the item_db. Using dummy data.\n", nameid);
		id = dummy_item;
	}
	return id;
}

/*==========================================
 * Checks if item is equip type or not
 * @param id Item data
 * @return True if item is equip, false otherwise
 *------------------------------------------*/
bool itemdb_isequip2(struct item_data *id)
{ 
	nullpo_ret(id);
	switch(id->type)
	{
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_AMMO:
		case IT_SHADOWGEAR:
			return true;
		default:
			return false;
	}
}

/*==========================================
 * Checks if item is stackable or not
 * @param id Item data
 * @return True if item is stackable, false otherwise
 *------------------------------------------*/
bool itemdb_isstackable2(struct item_data *id)
{
  nullpo_ret(id);
  switch(id->type) {
	  case IT_WEAPON:
	  case IT_ARMOR:
	  case IT_PETEGG:
	  case IT_PETARMOR:
	  case IT_SHADOWGEAR:
		  return false;
	  default:
		  return true;
  }
}


/*==========================================
 * Trade Restriction functions [Skotlex]
 *------------------------------------------*/
bool itemdb_isdropable_sub(struct item_data *item, int gmlv, int unused)
{
	return (item && (!(item->flag.trade_restriction&1) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_cantrade_sub(struct item_data* item, int gmlv, int gmlv2)
{
	return (item && (!(item->flag.trade_restriction&2) || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

bool itemdb_canpartnertrade_sub(struct item_data* item, int gmlv, int gmlv2)
{
	return (item && (item->flag.trade_restriction&4 || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

bool itemdb_cansell_sub(struct item_data* item, int gmlv, int unused)
{
	return (item && (!(item->flag.trade_restriction&8) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_cancartstore_sub(struct item_data* item, int gmlv, int unused)
{	
	return (item && (!(item->flag.trade_restriction&16) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canstore_sub(struct item_data* item, int gmlv, int unused)
{	
	return (item && (!(item->flag.trade_restriction&32) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canguildstore_sub(struct item_data* item, int gmlv, int unused)
{	
	return (item && (!(item->flag.trade_restriction&64) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canmail_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&128) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_canauction_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction & 256) || gmlv >= item->gm_lv_trade_override));
}

bool itemdb_isrestricted(struct item* item, int gmlv, int gmlv2, bool (*func)(struct item_data*, int, int))
{
	struct item_data* item_data = itemdb_search(item->nameid);
	int i;

	if (!func(item_data, gmlv, gmlv2))
		return false;
	
	if(item_data->slot == 0 || itemdb_isspecial(item->card[0]))
		return true;
	
	for(i = 0; i < item_data->slot; i++) {
		if (!item->card[i]) continue;
		if (!func(itemdb_search(item->card[i]), gmlv, gmlv2))
			return false;
	}
	return true;
}

bool itemdb_ishatched_egg(struct item* item) {
	if (item && item->card[0] == CARD0_PET && item->attribute == 1)
		return true;
	return false;
}

/*==========================================
 *	Specifies if item-type should drop unidentified.
 *------------------------------------------*/
char itemdb_isidentified(t_itemid nameid)
{
	int type=itemdb_type(nameid);
	switch (type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_PETARMOR:
		case IT_SHADOWGEAR:
			return battle_config.item_auto_identify;
		default:
			return 1;
	}
}

/*==========================================
 * アイテム使用可能フラグのオーバーライド
 *------------------------------------------*/
static bool itemdb_read_itemavail(char* str[], int columns, int current)
{// <nameid>,<sprite>
	t_itemid nameid, sprite;
	struct item_data *id;

	nameid = strtoul(str[0], NULL, 10);

	if( ( id = itemdb_exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_itemavail: Invalid item id %u.\n", nameid);
		return false;
	}

	sprite = strtoul(str[1], NULL, 10);

	if( sprite > 0 )
	{
		id->flag.available = 1;
		id->view_id = sprite;
	}
	else
	{
		id->flag.available = 0;
	}

	return true;
}

/*==========================================
 * read item group data
 *------------------------------------------*/
static void itemdb_read_itemgroup_sub(const char* filename)
{
	FILE *fp;
	char line[1024];
	int ln=0, entries=0;
	t_itemid nameid;
	int groupid,j,k;
	uint16 idx = 0;
	char *str[3],*p;
	char w1[1024], w2[1024];
	
	if( (fp=fopen(filename,"r"))==NULL ){
		ShowError("can't read %s\n", filename);
		return;
	}

	while(fgets(line, sizeof(line), fp))
	{
		struct item_group *group = NULL;

		ln++;
		if(line[0]=='/' && line[1]=='/')
			continue;
		if(strstr(line,"import")) {
			if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) == 2 &&
				strcmpi(w1, "import") == 0) {
				itemdb_read_itemgroup_sub(w2);
				continue;
			}
		}
		memset(str,0,sizeof(str));
		for(j=0,p=line;j<3 && p;j++){
			str[j]=p;
			p=strchr(p,',');
			if(p) *p++=0;
		}
		if(str[0]==NULL)
			continue;
		if (j<3) {
			if (j>1) //Or else it barks on blank lines...
				ShowWarning("itemdb_read_itemgroup: Insufficient fields for entry at %s:%d\n", filename, ln);
			continue;
		}
		//Checking groupid
		if (atoi(str[0]))
			groupid = atoi(str[0]);
		else if (!atoi(str[0])) //Try reads group id by const
			script_get_constant(trim(str[0]), &groupid);

		if (groupid < 0) {
			ShowWarning("itemdb_read_itemgroup: Invalid group %d in %s:%d\n", groupid, filename, ln);
			continue;
		}
		nameid = strtoul(str[1], NULL, 10);
		if (!itemdb_exists(nameid)) {
			ShowWarning("itemdb_read_itemgroup: Non-existant Item %u in %s:%d\n", nameid, filename, ln);
			continue;
		}
		k = atoi(str[2]);

		if (!(group = (struct item_group *) uidb_get(itemdb_group, groupid))) {
			CREATE(group, struct item_group, 1);
			group->id = groupid;
		}
		
		for (j = 0; j < k; j++)
			group->nameid[group->qty++] = nameid;

		uidb_put(itemdb_group, group->id, group);
		entries++;
	}
	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entries, filename);
	return;
}

static void itemdb_read_itemgroup(void)
{
	char path[256];
	snprintf(path, 255, "%s/item_group_db.txt", db_path);

	itemdb_read_itemgroup_sub(path);
	return;
}

static int itemdb_group_free(DBKey key, DBData *data, va_list ap) {
	struct item_group *group = db_data2ptr(data);

	if (!group)
		return 0;

	aFree(group);
	return 0;
}

/*==========================================
 * read item package data
 * Structure: PackageID,ItemID,Rate{,Amount,Random,isAnnounced,Duration}
 *------------------------------------------*/
static void itemdb_read_itempackage_sub(const char* filename)
{
	FILE *fp;
	char line[1024];
	int ln=0, entries = 0;
	unsigned short duration = 0;
	t_itemid nameid;
	bool announced = false, guid = false;
	int packageid,amt,j;
	int prob = 1;
	uint8 rand_package = 0;
	char *str[8],*p;
	char w1[1024], w2[1024];
	
	if( (fp=fopen(filename,"r"))==NULL ){
		ShowError("can't read %s\n", filename);
		return;
	}

	while(fgets(line, sizeof(line), fp))
	{
		struct item_package *package = NULL;

		ln++;
		if(line[0]=='/' && line[1]=='/')
			continue;
		if(strstr(line,"import")) {
			if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) == 2 &&
				strcmpi(w1, "import") == 0) {
				itemdb_read_itempackage_sub(w2);
				continue;
			}
		}
		memset(str,0,sizeof(str));
		for(j=0,p=line;j<8 && p;j++){
			str[j]=p;
			p=strchr(p,',');
			if(p) *p++=0;
		}
		if(str[0]==NULL)
			continue;
		if (j<3) {
			if (j>1) //Or else it barks on blank lines...
				ShowWarning("itemdb_read_itempackage: Insufficient fields for entry at %s:%d\n", filename, ln);
			continue;
		}

		//Checking packageid
		if (!atoi(str[0])) //Try reads group id by const
			script_get_constant(trim(str[0]), &packageid);
		else
			packageid = atoi(str[0]);

		if (packageid < 0) {
			ShowWarning("itemdb_read_itempackage: Invalid package %d in %s:%d\n", packageid, filename, ln);
			continue;
		}

		nameid = strtoul(str[1], NULL, 10);
		if (!itemdb_exists(nameid)) {
			ShowWarning("itemdb_read_itempackage: Non-existant Item %u in %s:%d\n", nameid, filename, ln);
			continue;
		}

		prob = atoi(str[2]);

		if (str[3] != NULL)
			amt = atoi(str[3]);
		if (amt <= 0 || amt > MAX_AMOUNT) {
			ShowWarning("itemdb_read_itempackage: Invalid amount ('%d') set for Item %u in %s:%d\nresetting to 1.", amt, str[0], filename, ln);
			amt = 1;
			continue;
		}

		if (str[4] != NULL)
			rand_package = atoi(str[4]);
		if (rand_package < 0 || rand_package > MAX_RANDGROUP) {
			ShowWarning("itemdb_read_itempackage: Invalid sub group '%d' for package '%s' in %s:%d\n", rand_package, str[0], filename, ln);
			continue;
		}

		if (str[5] != NULL) announced = atoi(str[5]);
		if (str[6] != NULL) duration = cap_value(atoi(str[6]), 0, UINT16_MAX);
		if (str[7] != NULL) guid = atoi(str[7]);

		if (rand_package != 0 && prob < 1) {
			ShowWarning("itemdb_read_itempackage: Random item must has probability. Package '%s' in %s:%d\n", str[0], filename, ln);
			continue;
		}

		if (!(package = (struct item_package *) idb_get(itemdb_package, packageid))) {
			CREATE(package, struct item_package, 1);
			package->id = packageid;
			package->count = 0;
		}

		uint16 idx = package->count;

		CREATE(package->entry[idx], struct item_package_entry, 1);

		package->entry[idx]->nameid = nameid;
		package->entry[idx]->prob = prob;
		package->entry[idx]->amount = amt;
		package->entry[idx]->group = rand_package;
		package->entry[idx]->announced = announced;
		package->entry[idx]->duration = duration;
		package->entry[idx]->guid = guid;
		if (rand_package > package->max_rand)
			package->max_rand = rand_package;
		package->count++;

		idb_put(itemdb_package, package->id, package);
		entries++;
	}
	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entries, filename);
	return;
}

static void itemdb_read_itempackage(void)
{
	char path[256];
	snprintf(path, 255, "%s/item_package_db.txt", db_path);

	itemdb_read_itempackage_sub(path);
	return;
}

static int itemdb_package_free(DBKey key, DBData *data, va_list ap) {
	struct item_package *package = db_data2ptr(data);

	if (!package)
		return 0;

	for (int i = 0; i < package->count; i++)
		aFree(package->entry[i]);

	aFree(package);
	return 0;
}

/*==========================================
 * 装備制限ファイル読み出し
 *------------------------------------------*/
static bool itemdb_read_noequip(char* str[], int columns, int current)
{// <nameid>,<mode>
	t_itemid nameid;
	struct item_data *id;

	nameid = strtoul(str[0], NULL, 10);

	if( ( id = itemdb_exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_noequip: Invalid item id %u.\n", nameid);
		return false;
	}

	id->flag.no_equip |= atoi(str[1]);

	return true;
}

/*==========================================
 * Reads item trade restrictions [Skotlex]
 *------------------------------------------*/
static bool itemdb_read_itemtrade(char* str[], int columns, int current)
{// <nameid>,<mask>,<gm level>
	t_itemid nameid;
	int flag, gmlv;
	struct item_data *id;

	nameid = strtoul(str[0], NULL, 10);

	if( ( id = itemdb_exists(nameid) ) == NULL )
	{
		//ShowWarning("itemdb_read_itemtrade: Invalid item id %u.\n", nameid);
		//return false;
		// FIXME: item_trade.txt contains items, which are commented in item database.
		return true;
	}

	flag = atoi(str[1]);
	gmlv = atoi(str[2]);

	if( flag > 511 )
	{//Check range
		ShowWarning("itemdb_read_itemtrade: Invalid trading mask %d for item id %u.\n", flag, nameid);
		return false;
	}

	if( gmlv < 1 )
	{
		ShowWarning("itemdb_read_itemtrade: Invalid override GM level %d for item id %u.\n", gmlv, nameid);
		return false;
	}

	id->flag.trade_restriction = flag;
	id->gm_lv_trade_override = gmlv;

	return true;
}

/*==========================================
 * Reads item delay amounts [Paradox924X]
 *------------------------------------------*/
static bool itemdb_read_itemdelay(char* str[], int columns, int current)
{// <nameid>,<delay>{,<delay sc group>}
	t_itemid nameid;
	int delay;
	struct item_data *id;

	nameid = strtoul(str[0], NULL, 10);

	if( ( id = itemdb_exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_itemdelay: Invalid item id %u.\n", nameid);
		return false;
	}

	delay = atoi(str[1]);

	if( delay < 0 )
	{
		ShowWarning("itemdb_read_itemdelay: Invalid delay %d for item id %u.\n", id->delay, nameid);
		return false;
	}

	id->delay = delay;

	if (columns == 2)
		id->delay_sc = SC_NONE;
	else if (ISDIGIT(str[2][0]))
		id->delay_sc = (sc_type)atoi(str[2]);
	else { // Try read sc group id from const db
		int constant;

		if (!script_get_constant(trim(str[2]), &constant)) {
			ShowWarning("itemdb_read_itemdelay: Invalid sc group \"%s\" for item id %u.\n", str[2], nameid);
			return false;
		}

		id->delay_sc = (short)constant;
	}

	return true;
}


/// Reads items allowed to be sold in buying stores
static bool itemdb_read_buyingstore(char* fields[], int columns, int current)
{// <nameid>
	t_itemid nameid;
	struct item_data* id;

	nameid = strtoul(fields[0], NULL, 10);

	if( ( id = itemdb_exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_buyingstore: Invalid item id %u.\n", nameid);
		return false;
	}

	if( !itemdb_isstackable2(id) )
	{
		ShowWarning("itemdb_read_buyingstore: Non-stackable item id %u cannot be enabled for buying store.\n", nameid);
		return false;
	}

	id->flag.buyingstore = true;

	return true;
}

/**
 * Process Roulette items
 */
bool itemdb_parse_roulette_db(void)
{
#ifndef TXT_ONLY
	int i, j;
	uint32 count = 0;

	// retrieve all rows from the item database
	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT * FROM `%s`", db_roulette_table)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++)
		rd.items[i] = 0;

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		int k, limit = MAX_ROULETTE_COLUMNS - i;

		for (k = 0; k < limit && SQL_SUCCESS == Sql_NextRow(mmysql_handle); k++) {
			char* data;
			t_itemid item_id;
			short amount;
			int level, flag;

			Sql_GetData(mmysql_handle, 1, &data, NULL); level = atoi(data);
			Sql_GetData(mmysql_handle, 2, &data, NULL); item_id = strtoul(data, NULL, 10);
			Sql_GetData(mmysql_handle, 3, &data, NULL); amount = atoi(data);
			Sql_GetData(mmysql_handle, 4, &data, NULL); flag = atoi(data);

			if (!itemdb_exists(item_id)) {
				ShowWarning("itemdb_parse_roulette_db: Unknown item ID '%u' in level '%d'\n", item_id, level);
 				continue;
			}
			if (amount < 1) {
				ShowWarning("itemdb_parse_roulette_db: Unsupported amount '%hu' for item ID '%u' in level '%d'\n", amount, item_id, level);
 				continue;
			}
			if (flag < 0 || flag > 1) {
				ShowWarning("itemdb_parse_roulette_db: Unsupported flag '%d' for item ID '%u' in level '%d'\n", flag, item_id, level);
 				continue;
			}

			j = rd.items[i];
			RECREATE(rd.nameid[i], t_itemid, ++rd.items[i]);
			RECREATE(rd.qty[i], unsigned short, rd.items[i]);
			RECREATE(rd.flag[i], int, rd.items[i]);

			rd.nameid[i][j] = item_id;
			rd.qty[i][j] = amount;
			rd.flag[i][j] = flag;

			++count;
		}
	}

	// free the query result
	Sql_FreeResult(mmysql_handle);

	for (i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		int limit = MAX_ROULETTE_COLUMNS - i;

		if (rd.items[i] == limit)
			continue;

		if (rd.items[i] > limit) {
			ShowWarning("itemdb_parse_roulette_db: level %d has %d items, only %d supported, capping...\n", i + 1, rd.items[i], limit);
			rd.items[i] = limit;
			continue;
		}

		/** this scenario = rd.items[i] < limit **/
		ShowWarning("itemdb_parse_roulette_db: Level %d has %d items, %d are required. Filling with Apples...\n", i + 1, rd.items[i], limit); 

		rd.items[i] = limit;
		RECREATE(rd.nameid[i], t_itemid, rd.items[i]);
		RECREATE(rd.qty[i], unsigned short, rd.items[i]);
		RECREATE(rd.flag[i], int, rd.items[i]);

		for (j = 0; j < MAX_ROULETTE_COLUMNS - i; j++) {
			if (rd.qty[i][j])
				continue;

			rd.nameid[i][j] = ITEMID_APPLE;
			rd.qty[i][j] = 1;
			rd.flag[i][j] = 0;
		}
	}

	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, db_roulette_table);
#endif
	return true;
}

/**
 * Free Roulette items
 */
static void itemdb_roulette_free(void) {
	int i;
	for (i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		if (rd.nameid[i])
			aFree(rd.nameid[i]);
		if (rd.qty[i])
			aFree(rd.qty[i]);
		if (rd.flag[i])
			aFree(rd.flag[i]);
		rd.nameid[i] = NULL;
		rd.qty[i] = NULL;
		rd.flag[i] = NULL;
		rd.items[i] = 0;
	}
}

/*******************************************
** Item usage restriction (item_nouse.txt)
********************************************/
static bool itemdb_read_nouse(char* fields[], int columns, int current)
{// <nameid>,<flag>,<override>
	t_itemid nameid;
	int flag, override;
	struct item_data* id;

	nameid = strtoul(fields[0], NULL, 10);

	if ((id = itemdb_exists(nameid)) == NULL) {
		ShowWarning("itemdb_read_nouse: Invalid item id %d.\n", nameid);
		return false;
	}

	flag = atoi(fields[1]);
	override = atoi(fields[2]);

	id->item_usage.flag = flag;
	id->item_usage.override = override;

	return true;
}

/*======================================
 * Applies gender restrictions according to settings. [Skotlex]
 *======================================*/
static char itemdb_gendercheck(struct item_data *id)
{
	if (id->nameid == WEDDING_RING_M) //Grom Ring
		return 1;
	if (id->nameid == WEDDING_RING_F) //Bride Ring
		return 0;
	if (id->look == W_MUSICAL && id->type == IT_WEAPON) //Musical instruments are always male-only
		return 1;
	if (id->look == W_WHIP && id->type == IT_WEAPON) //Whips are always female-only
		return 0;

	return (battle_config.ignore_items_gender) ? 2 : id->sex;
}

/*==========================================
 * processes one itemdb entry
 *------------------------------------------*/
static bool itemdb_parse_dbrow(char** str, const char* source, int line, int scriptopt)
{
	/*
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-----------------------------+------------+------+--------+--------------+----------------+
		| 00 |      01      |       02      |  03  |     04    |     05     |   06   |   07   |    08   |   09  |   10  |     11     |      12     |       13      |        14       |      15      |               16            |     17     |  18  |   19   |      20      |        21      |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-----------------------------+------------+------+--------+--------------+----------------+
		| id | name_english | name_japanese | type | price_buy | price_sell | weight | attack | defence | range | slots | equip_jobs | equip_upper | equip_genders | equip_locations | weapon_level | equip_level:equip_level_max | refineable | view | script | equip_script | unequip_script |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-----------------------------+------------+------+--------+--------------+----------------+
	*/
	t_itemid nameid;
	struct item_data* id;
	int tmp[2];
	
	nameid = strtoul(str[0], NULL, 10);

	if (nameid == 0 || nameid == dummy_item->nameid)
	{
		ShowWarning("itemdb_parse_dbrow: Invalid id %d in line %d of \"%s\", skipping.\n", atoi(str[0]), line, source);
		return false;
	}

	//ID,Name,Jname,Type,Price,Sell,Weight,ATK,DEF,Range,Slot,Job,Job Upper,Gender,Loc,wLV,eLV:eLV_max,refineable,View
	//id = itemdb_load(nameid);
	if (!(id = itemdb_exists(nameid)))
		id = itemdb_create_item(nameid);

	safestrncpy(id->name, str[1], sizeof(id->name));
	safestrncpy(id->jname, str[2], sizeof(id->jname));

	id->type = atoi(str[3]);

	if( id->type < 0 || id->type == IT_UNKNOWN || id->type == IT_UNKNOWN2 || ( id->type > IT_SHADOWGEAR && id->type < IT_CASH ) || id->type >= IT_MAX )
	{// catch invalid item types
		ShowWarning("itemdb_parse_dbrow: Invalid item type %d for item %u. IT_ETC will be used.\n", id->type, nameid);
		id->type = IT_ETC;
	}

	// Correct item types (backward compatibility, should be changed in databases in future...) [15peaces]
	if (id->type == IT_WEAPON)
		id->type = IT_ARMOR;
	else if (id->type == IT_ARMOR)
		id->type = IT_WEAPON;

	if (id->type == IT_DELAYCONSUME)
	{	//Items that are consumed only after target confirmation
		id->type = IT_USABLE;
		id->flag.delay_consume = 1;
	} else //In case of an itemdb reload and the item type changed.
		id->flag.delay_consume = 0;

	//When a particular price is not given, we should base it off the other one
	//(it is important to make a distinction between 'no price' and 0z)
	if ( str[4][0] )
		id->value_buy = atoi(str[4]);
	else
		id->value_buy = atoi(str[5]) * 2;

	if ( str[5][0] )
		id->value_sell = atoi(str[5]);
	else
		id->value_sell = id->value_buy / 2;
	/* 
	if ( !str[4][0] && !str[5][0])
	{  
		ShowWarning("itemdb_parse_dbrow: No buying/selling price defined for Item %u (%s), using 20/10z\n",       nameid, id->jname);
		id->value_buy = 20;
		id->value_sell = 10;
	} else
	*/
	if (id->value_buy/124. < id->value_sell/75.)
		ShowWarning("itemdb_parse_dbrow: Buying/Selling [%d/%d] price of item %u (%s) allows Zeny making exploit  through buying/selling at discounted/overcharged prices!\n",
			id->value_buy, id->value_sell, nameid, id->jname);

	id->weight = atoi(str[6]);
	id->atk = atoi(str[7]);
	id->def = atoi(str[8]);
	id->range = atoi(str[9]);
	id->slot = atoi(str[10]);

	if (id->slot > MAX_SLOTS)
	{
		ShowWarning("itemdb_parse_dbrow: Item %u (%s) specifies %d slots, but the server only supports up to %d. Using %d slots.\n", nameid, id->jname, id->slot, MAX_SLOTS, MAX_SLOTS);
		id->slot = MAX_SLOTS;
	}

	itemdb_jobid2mapid(id->class_base, (unsigned int)strtoul(str[11],NULL,0));
	id->class_upper = atoi(str[12]);
	id->sex	= atoi(str[13]);
	id->equip = atoi(str[14]);

	if (!id->equip && itemdb_isequip2(id))
	{
		ShowWarning("Item %u (%s) is an equipment with no equip-field! Making it an etc item.\n", nameid, id->jname);
		id->type = IT_ETC;
	}

	if (id->type != IT_SHADOWGEAR && id->equip&EQP_SHADOW_EQUIPS)
	{
		ShowWarning("Item %u (%s) have invalid equipment slot! Making it an etc item.\n", nameid, id->jname);
		id->type = IT_ETC;
	}

	id->wlv = atoi(str[15]);
	pc_split_atoi(str[16], tmp, ':', 2);
	id->elv = tmp[0];
	id->elv_max = tmp[1] > 0 ? tmp[1] : MAX_LEVEL;
	id->flag.no_refine = atoi(str[17]) ? 0 : 1; //FIXME: verify this
	id->look = atoi(str[18]);

	id->flag.available = 1;
	id->view_id = 0;
	id->sex = itemdb_gendercheck(id); //Apply gender filtering.

	if (id->script)
	{
		script_free_code(id->script);
		id->script = NULL;
	}
	if (id->equip_script)
	{
		script_free_code(id->equip_script);
		id->equip_script = NULL;
	}
	if (id->unequip_script)
	{
		script_free_code(id->unequip_script);
		id->unequip_script = NULL;
	}

	if (*str[19])
		id->script = parse_script(str[19], source, line, scriptopt);
	if (*str[20])
		id->equip_script = parse_script(str[20], source, line, scriptopt);
	if (*str[21])
		id->unequip_script = parse_script(str[21], source, line, scriptopt);
	if (!id->nameid) {
		id->nameid = nameid;
		uidb_put(itemdb, nameid, id);
	}
	return true;
}

/**
* Read item from item db
* item_db2 overwriting item_db
*/
static int itemdb_readdb(void)
{
	const char* filename[] = { "item_db.txt", "item_db2.txt" };
	char epfile[256];
	int fi, files;

	bool epdb = battle_config.episode_readdb;

	sprintf(epfile, "item_db_ep%i.txt", battle_config.feature_episode);

	files = ARRAYLENGTH(filename);

	// Testing episode database [15peaces]
	if(epdb){
		if(!EpisodeDBExists(db_path, epfile))
			epdb = false;
		else
			files = 1;
	}

	for( fi = 0; fi < files; ++fi )
	{
		uint32 lines = 0, count = 0;
		char line[1024];

		char path[256];
		FILE* fp;

		if(epdb)
			sprintf(path, "%s/episode/%s", db_path, epfile);
		else
			sprintf(path, "%s/%s", db_path, filename[fi]);
		fp = fopen(path, "r");
		if( fp == NULL )
		{
			ShowWarning("itemdb_readdb: File not found \"%s\", skipping.\n", path);
			continue;
		}

		// process rows one by one
		while(fgets(line, sizeof(line), fp))
		{
			char *str[32], *p;
			int i;

			lines++;
			if(line[0] == '/' && line[1] == '/')
				continue;
			memset(str, 0, sizeof(str));

			p = line;
			while( ISSPACE(*p) )
				++p;
			if( *p == '\0' )
				continue;// empty line
			for( i = 0; i < 19; ++i )
			{
				str[i] = p;
				p = strchr(p,',');
				if( p == NULL )
					break;// comma not found
				*p = '\0';
				++p;
			}

			if( p == NULL )
			{
				ShowError("itemdb_readdb: Insufficient columns in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}

			// Script
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[19] = p;
			p = strstr(p+1,"},");
			if( p == NULL )
			{
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			// OnEquip_Script
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[20] = p;
			p = strstr(p+1,"},");
			if( p == NULL )
			{
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			// OnUnequip_Script (last column)
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (OnUnequip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[21] = p;

			if (str[21][strlen(str[21]) - 2] != '}') {
				/* lets count to ensure it's not something silly e.g. a extra space at line ending */
				int v, lcurly = 0, rcurly = 0;

				for (v = 0; v < strlen(str[21]); v++) {
					if (str[21][v] == '{')
						lcurly++;
					else if (str[21][v] == '}')
						rcurly++;
				}

				if (lcurly != rcurly) {
					ShowError("itemdb_readdb: Mismatching curly braces in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
					continue;
				}
			}

			if (!itemdb_parse_dbrow(str, path, lines, 0))
				continue;

			count++;
		}

		fclose(fp);

		if(epdb)
			ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, epfile);
		else
			ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, filename[fi]);
	}

	return 0;
}

/// Reads one line from database and assigns it to RAM.
static bool itemdb_read_cashshop(char* str[], int columns, int current){
	t_itemid nameid = strtoul(str[1], NULL, 10);

	if( itemdb_exists( nameid ) ){
		uint16 tab = atoi( str[0] );
		uint32 price = atoi( str[2] );
		struct cash_item_data* cid;
		int j;

		if( tab > CASHSHOP_TAB_MAX ){
			ShowWarning( "itemdb_read_cashshop: Invalid tab %d, skipping...\n", tab);
			return false;
		}else if( price < 1 ){
			ShowWarning( "itemdb_read_cashshop: Invalid price %d, skipping...\n", price);
			return false;
		}

		ARR_FIND( 0, cash_shop_items[tab].count, j, nameid == cash_shop_items[tab].item[j]->id );

		if( j == cash_shop_items[tab].count ){
			RECREATE( cash_shop_items[tab].item, struct cash_item_data *, ++cash_shop_items[tab].count );
			CREATE( cash_shop_items[tab].item[ cash_shop_items[tab].count - 1], struct cash_item_data, 1 );
			cid = cash_shop_items[tab].item[ cash_shop_items[tab].count - 1];
		}else
			cid = cash_shop_items[tab].item[j];

		cid->id = nameid;
		cid->price = price;
	}else{
		ShowWarning( "itemdb_read_cashshop: Invalid ID %u, skipping...\n", nameid);
		return false;
	}
	return true;
}

#ifndef TXT_ONLY
/*======================================
 * item_db table reading
 *======================================*/
static int itemdb_read_sqldb(void)
{
	const char* item_db_name[] = { item_db_db, item_db2_db };
	int fi;
	
	for( fi = 0; fi < ARRAYLENGTH(item_db_name); ++fi )
	{
		uint32 lines = 0, count = 0;

		// retrieve all rows from the item database
		if( SQL_ERROR == Sql_Query(mmysql_handle, "SELECT * FROM `%s`", item_db_name[fi]) )
		{
			Sql_ShowDebug(mmysql_handle);
			continue;
		}

		// process rows one by one
		while( SQL_SUCCESS == Sql_NextRow(mmysql_handle) )
		{// wrap the result into a TXT-compatible format
			char* str[22];
			char* dummy = "";
			int i;
			++lines;
			for( i = 0; i < 22; ++i )
			{
				Sql_GetData(mmysql_handle, i, &str[i], NULL);
				if( str[i] == NULL ) str[i] = dummy; // get rid of NULL columns
			}

			if (!itemdb_parse_dbrow(str, item_db_name[fi], lines, SCRIPT_IGNORE_EXTERNAL_BRACKETS))
				continue;
			++count;
		}

		// free the query result
		Sql_FreeResult(mmysql_handle);

		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, item_db_name[fi]);
	}

	return 0;
}
#endif /* not TXT_ONLY */

/**
* Retrieves random option data
*/
struct s_random_opt_data* itemdb_randomopt_exists(short id) {
	return ((struct s_random_opt_data*)uidb_get(itemdb_randomopt, id));
}

/** Random option
* <ID>,<{Script}>
*/
static bool itemdb_read_randomopt() {
	uint32 lines = 0, count = 0;
	char line[1024];

	char path[256];
	FILE* fp;

	snprintf(path, 255, "%s/item_randomopt_db.txt", db_path);

	if ((fp = fopen(path, "r")) == NULL) {
		ShowError("itemdb_read_randomopt: File not found \"%s\".\n", path);
		return false;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *str[2], *p;

		lines++;

		if (line[0] == '/' && line[1] == '/') // Ignore comments
			continue;

		memset(str, 0, sizeof(str));

		p = line;

		p = trim(p);

		if (*p == '\0')
			continue;// empty line

		if (!strchr(p, ','))
		{
			ShowError("itemdb_read_randomopt: Insufficient columns in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}

		str[0] = p;
		p = strchr(p, ',');
		*p = '\0';
		p++;

		str[1] = p;

		if (str[1][0] != '{') {
			ShowError("itemdb_read_randomopt(#1): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}
		/* no ending key anywhere (missing \}\) */
		if (str[1][strlen(str[1]) - 1] != '}') {
			ShowError("itemdb_read_randomopt(#2): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}
		else {
			int id = -1;
			struct s_random_opt_data *data;
			struct script_code *code;

			str[0] = trim(str[0]);
			if (ISDIGIT(str[0][0])) {
				id = atoi(str[0]);
			}
			else {
				script_get_constant(str[0], &id);
			}

			if (id < 0) {
				ShowError("itemdb_read_randomopt: Invalid Random Option ID '%s' in line %d of \"%s\", skipping.\n", str[0], lines, path);
				continue;
			}

			if ((data = itemdb_randomopt_exists(id)) == NULL) {
				CREATE(data, struct s_random_opt_data, 1);
				uidb_put(itemdb_randomopt, id, data);
			}
			data->id = id;
			if ((code = parse_script(str[1], path, lines, 0)) == NULL) {
				ShowWarning("itemdb_read_randomopt: Invalid script on option ID #%d.\n", id);
				continue;
			}
			if (data->script) {
				script_free_code(data->script);
				data->script = NULL;
			}
			data->script = code;
		}
		count++;
	}
	fclose(fp);

	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, path);

	return true;
}

void itemdb_parse_attendance_db(void)
{
	uint32 lines = 0, cnt = 1;
	char line[1024];

	char file[256];
	FILE* fp;

	VECTOR_CLEAR(attendance_data);

	sprintf(file, "db/attendance_db.txt");
	fp = fopen(file, "r");
	if (fp == NULL)
	{
		ShowWarning("itemdb_parse_attendance_db: File not found \"%s\", skipping.\n", file);
		return;
	}

	// process rows one by one
	while (fgets(line, sizeof(line), fp))
	{
		char *str[32], *p;
		int i;
		int day, id, amt;

		struct attendance_entry entry = { 0 };

		lines++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));

		p = line;

		while (ISSPACE(*p))
			++p;

		if (*p == '\0')
			continue;// empty line

		for (i = 0; i < 3; ++i)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p == NULL)
				break;// comma not found
			*p = '\0';
			++p;

			if (str[i] == NULL)
			{
				ShowError("itemdb_parse_attendance_db: Insufficient columns in line %d of \"%s\", skipping.\n", lines, file);
				continue;
			}
		}

		day = atoi(str[0]);
		id = atoi(str[1]);
		amt = atoi(str[2]);

		if (day <= 0) {
			ShowError("itemdb_parse_attendance_db: Day value cannot be < 1 in line %d.\n", lines);
			continue;
		}

		if (!itemdb_exists(id)) {
			ShowWarning("itemdb_parse_attendance_db: unknown Item %u, line #%d, skipping.\n", id, lines);
			continue;
		}

		if (amt < 1) {
			ShowWarning("itemdb_parse_attendance_db: amount cannot be < 1 in line %d, defaulting to 1...\n", lines);
			amt = 1;
		}

		entry.day = day;
		entry.nameid = id;
		entry.qty = amt;

		VECTOR_ENSURE(attendance_data, 1, 1);
		VECTOR_PUSH(attendance_data, entry);

		cnt++;
	}

	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", cnt - 1, file);
}

static bool itemdb_parse_lapineddukddak_sources(int id, struct item_data *data)
{
	nullpo_retr(false, data);
	nullpo_retr(false, data->lapineddukddak);

	uint32 lines = 0, cnt = 1;
	char line[1024];

	char file[256];
	FILE* fp;

	sprintf(file, "db/item_lapineddukddak_source.txt");
	fp = fopen(file, "r");
	if (fp == NULL)
	{
		ShowWarning("itemdb_parse_lapineddukddak_sources: File not found \"%s\", skipping.\n", file);
		return false;
	}

	VECTOR_INIT(data->lapineddukddak->SourceItems);

	// process rows one by one
	while (fgets(line, sizeof(line), fp))
	{
		char *str[32], *p;
		int i, sid, amt;
		t_itemid nameid;

		struct item_data *edata = NULL;
		struct itemlist_entry item = { 0 };

		lines++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));

		p = line;

		while (ISSPACE(*p))
			++p;

		if (*p == '\0')
			continue;// empty line

		for (i = 0; i < 3; ++i)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p == NULL)
				break;// comma not found
			*p = '\0';
			++p;

			if (str[i] == NULL)
			{
				ShowError("itemdb_parse_lapineddukddak_sources: Insufficient columns in line %d of \"%s\", skipping.\n", lines, file);
				continue;
			}
		}

		sid = atoi(str[0]);
		if (sid < 0) {
			ShowWarning("itemdb_parse_lapineddukddak_sources: Invalid id %d in %s:%d\n", id, file, lines);
			continue;
		}

		if (id != sid) // ignore line.
			continue;

		nameid = strtoul(str[1], NULL, 10);
		if (nameid == 0 || nameid == dummy_item->nameid)
		{
			ShowWarning("itemdb_parse_lapineddukddak_sources: Invalid id %d in line %d of \"%s\", skipping.\n", atoi(str[1]), lines, file);
			continue;
		}

		if (!itemdb_exists(nameid)) {
			ShowWarning("itemdb_parse_lapineddukddak_sources: unknown item '%d', skipping.\n", nameid);
			continue;
		}

		edata = itemdb_search(nameid);
		item.id = edata->nameid;

		amt = atoi(str[2]);
		if (amt < 1) {
			ShowWarning("itemdb_parse_lapineddukddak_sources: invalid amount %d in %s:%d. Must be at least 1, skipping.\n", amt, file, lines);
			continue;
		}
		item.amount = amt;

		VECTOR_ENSURE(data->lapineddukddak->SourceItems, 1, 1);
		VECTOR_PUSH(data->lapineddukddak->SourceItems, item);
		cnt++;
	}
	return true;
}

void itemdb_parse_lapineddukddak_db(void)
{
	uint32 lines = 0, cnt = 1;
	char line[1024];

	char file[256];
	FILE* fp;

	sprintf(file, "db/item_lapineddukddak_db.txt");
	fp = fopen(file, "r");
	if (fp == NULL)
	{
		ShowWarning("itemdb_parse_lapineddukddak_db: File not found \"%s\", skipping.\n", file);
		return;
	}

	// process rows one by one
	while (fgets(line, sizeof(line), fp))
	{
		char *str[32], *p;
		int i;
		int id, needcount, refmin, refmax;
		t_itemid nameid;

		struct item_data *data = NULL;

		lines++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));

		p = line;

		while (ISSPACE(*p))
			++p;

		if (*p == '\0')
			continue;// empty line

		for (i = 0; i < 6; ++i)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p == NULL)
				break;// comma not found
			*p = '\0';
			++p;

			if (str[i] == NULL)
			{
				ShowError("itemdb_parse_lapineddukddak_db: Insufficient columns in line %d of \"%s\", skipping.\n", lines, file);
				continue;
			}
		}

		id = atoi(str[0]);
		if (id < 0) {
			ShowWarning("itemdb_parse_lapineddukddak_db: Invalid id %d in %s:%d\n", id, file, lines);
			continue;
		}

		nameid = strtoul(str[1], NULL, 10);
		if (nameid == 0 || nameid == dummy_item->nameid)
		{
			ShowWarning("itemdb_parse_lapineddukddak_db: Invalid id %d in line %d of \"%s\", skipping.\n", atoi(str[1]), lines, file);
			continue;
		}

		if (!itemdb_exists(nameid)) {
			ShowWarning("itemdb_parse_lapineddukddak_db: unknown item '%d', skipping.\n", nameid);
			continue;
		}

		data = itemdb_search(nameid);

		needcount = atoi(str[2]);
		if (needcount < 1) {
			ShowWarning("itemdb_parse_lapineddukddak_db: invalid NeedCount %d in %s:%d. Must be at least 1, skipping.\n", needcount, file, lines);
			continue;
		}

		refmin = atoi(str[3]);
		if (refmin < 0 || refmin > 20)
			refmin = 0;
		
		refmax = atoi(str[4]);
		if (refmax < 0 || refmax > 20)
			refmax = 0;

		data->lapineddukddak = aCalloc(1, sizeof(struct item_lapineddukddak));

		data->lapineddukddak->NeedCount = (int16)needcount;
		data->lapineddukddak->NeedRefineMin = (int8)refmin;
		data->lapineddukddak->NeedRefineMax = (int8)refmax;

		itemdb_parse_lapineddukddak_sources(id, data);

		if (*str[5])
			data->lapineddukddak->script = parse_script(str[5], file, lines, SCRIPT_IGNORE_EXTERNAL_BRACKETS);

		cnt++;
	}

	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", cnt - 1, file);
}

/** Misc Item flags
* <item_id>,<flag>
* &1 - As dead branch item
* &2 - As item container
* &4 - Drop rate is always 1
*/
static bool itemdb_read_flag(char* fields[], int columns, int current) {
	t_itemid nameid = strtoul(fields[0], NULL, 10);
	uint16 flag;
	bool set;
	struct item_data *id;

	if (!(id = itemdb_exists(nameid))) {
		ShowError("itemdb_read_flag: Invalid item item with id %d\n", nameid);
		return true;
	}

	flag = abs(atoi(fields[1]));
	set = atoi(fields[1]) > 0;

	if (flag & 1) id->flag.dead_branch = set ? 1 : 0;
	if (flag & 2) id->flag.group = set ? 1 : 0;
	if (flag & 4 && itemdb_isstackable2(id)) id->flag.guid = set ? 1 : 0;
	if (flag & 8) id->flag.bindOnEquip = true;
	if (flag & 16) id->flag.broadcast = 1;
	if (flag & 32) id->flag.fixed_drop = set ? 1 : 0;

	if (flag & 64) {
		id->flag.dropEffect = 1;
	}
	else if (flag & 128) {
		id->flag.dropEffect = 2;
	}
	else if (flag & 256) {
		id->flag.dropEffect = 3;
	}
	else if (flag & 512) {
		id->flag.dropEffect = 4;
	}
	else if (flag & 1024) {
		id->flag.dropEffect = 5;
	}
	else if (flag & 2048) {
		id->flag.dropEffect = 6;
	}

	return true;
}

/*==========================================
 * Check if the item is restricted by item_noequip.txt (return):
 * true		- can't be used
 * false	- can be used
 *------------------------------------------*/
bool itemdb_isNoEquip(struct item_data *id, uint16 m) {
	if (!id->flag.no_equip)
		return false;

	/* on restricted maps the item is consumed but the effect is not used */
	if ((!map_flag_vs(m) && id->flag.no_equip & 1) || // Normal
		(map[m].flag.pvp && id->flag.no_equip & 2) || // PVP
		(map_flag_gvg(m) && id->flag.no_equip & 4) || // GVG
		(map[m].flag.battleground && id->flag.no_equip & 8) || // Battleground
		(map_flag_gvg2_te(m) && id->flag.no_equip & 16) || // WoE:TE
		(map[m].flag.restricted && id->flag.no_equip&(8*map[m].zone)) // Zone restriction
		)
		return true;
	return false;
}

/*====================================
 * read all item-related databases
 *------------------------------------*/
static void itemdb_read(void)
{
#ifndef TXT_ONLY
	if (db_use_sqldbs)
		itemdb_read_sqldb();
	else
#endif
		itemdb_readdb();

	itemdb_read_itemgroup();
	itemdb_read_itempackage();
	itemdb_read_randomopt();
	sv_readdb(db_path, "item_avail.txt",	',', 2, 2, -1,		&itemdb_read_itemavail);
	sv_readdb(db_path, "item_noequip.txt",	',', 2, 2, -1,		&itemdb_read_noequip);
	sv_readdb(db_path, "item_trade.txt",	',', 3, 3, -1,		&itemdb_read_itemtrade);
	sv_readdb(db_path, "item_delay.txt",	',', 2, 3, -1,		&itemdb_read_itemdelay);
	sv_readdb(db_path, "item_buyingstore.txt", ',', 1, 1, -1,	&itemdb_read_buyingstore);
	sv_readdb(db_path, "cashshop_db.txt",   ',', 3, 3, -1,		&itemdb_read_cashshop);
	sv_readdb(db_path, "item_flag.txt",		',', 2, 2, -1,		&itemdb_read_flag);
	sv_readdb(db_path, "item_nouse.txt",	',', 3, 3, -1,		&itemdb_read_nouse);
}

/*==========================================
 * Initialize / Finalize
 *------------------------------------------*/

/// Destroys the item_data.
static void destroy_item_data(struct item_data* self)
{
	if( self == NULL )
		return;
	// free scripts
	if( self->script )
		script_free_code(self->script);
	if( self->equip_script )
		script_free_code(self->equip_script);
	if( self->unequip_script )
		script_free_code(self->unequip_script);
#if defined(DEBUG)
	// trash item
	memset(self, 0xDD, sizeof(struct item_data));
#endif

	aFree(self);
}

static int itemdb_final_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *id = db_data2ptr(data);

	destroy_item_data(id);

	return 0;
}

static int itemdb_randomopt_free(DBKey key, DBData *data, va_list ap) 
{
	struct s_random_opt_data *opt = db_data2ptr(data);
	if (!opt)
		return 0;
	if (opt->script)
		script_free_code(opt->script);
	opt->script = NULL;
	aFree(opt);
	return 1;
}

void itemdb_reload(void)
{
	struct s_mapiterator* iter;
	struct map_session_data* sd;

	itemdb->clear(itemdb, itemdb_final_sub);
	itemdb_randomopt->clear(itemdb_randomopt, itemdb_randomopt_free);
	itemdb_group->clear(itemdb_group, itemdb_group_free);
	itemdb_package->clear(itemdb_package, itemdb_package_free);

	if (battle_config.feature_roulette)
		itemdb_roulette_free();

	// read new data
	itemdb_read();

	if (battle_config.feature_roulette)
		itemdb_parse_roulette_db();

	// readjust itemdb pointer cache for each player
	iter = mapit_geteachpc();
	for( sd = (struct map_session_data*)mapit_first(iter); mapit_exists(iter); sd = (struct map_session_data*)mapit_next(iter) )
	{
		memset(sd->item_delay, 0, sizeof(sd->item_delay));  // reset item delays
		pc_setinventorydata(sd);
		pc_check_available_item(sd); // Check for invalid(ated) items.
	}
	mapit_free(iter);
}

/**
 * Finalizing Item DB
 */
void do_final_itemdb(void)
{
	itemdb->destroy(itemdb, itemdb_final_sub);
	itemdb_randomopt->destroy(itemdb_randomopt, itemdb_randomopt_free);
	itemdb_group->destroy(itemdb_group, itemdb_group_free);
	itemdb_package->destroy(itemdb_package, itemdb_package_free);

	destroy_item_data(dummy_item);
	if (battle_config.feature_roulette)
		itemdb_roulette_free();
	VECTOR_CLEAR(attendance_data);
}

/**
 * Initializing Item DB
 */
void do_init_itemdb(void)
{
	//memset(itemdb_array, 0, sizeof(itemdb_array));
	itemdb = uidb_alloc(DB_OPT_BASE); 
	itemdb_randomopt = uidb_alloc(DB_OPT_BASE);
	itemdb_group = uidb_alloc(DB_OPT_BASE);
	itemdb_package = uidb_alloc(DB_OPT_BASE);

	itemdb_create_dummy(); //Dummy data item.
	itemdb_read();

	if (battle_config.feature_roulette)
		itemdb_parse_roulette_db();

	VECTOR_INIT(attendance_data);
	itemdb_parse_attendance_db();
	itemdb_parse_lapineddukddak_db();
}
