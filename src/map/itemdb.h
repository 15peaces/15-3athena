// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _ITEMDB_H_
#define _ITEMDB_H_

#include "../common/mmo.h" // ITEM_NAME_LENGTH

///Maximum allowed Item ID (range: 1 ~ 65,534)
#define MAX_ITEMID USHRT_MAX

#define MAX_RANDITEM	11000

// The maximum number of item delays
#define MAX_ITEMDELAYS	9

#define MAX_ROULETTE_LEVEL 7 /** client-defined value **/
#define MAX_ROULETTE_COLUMNS 9 /** client-defined value **/

#define MAX_SEARCH	5  //Designed for search functions, species max number of matches to display.

enum item_itemid
{
 	ITEMID_YELLOW_POTION		= 503,
 	ITEMID_WHITE_POTION			= 504,
 	ITEMID_BLUE_POTION			= 505,
	ITEMID_APPLE				= 512,
	ITEMID_HOLY_WATER			= 523,
	ITEMID_MAGNIFIER			= 611,
	ITEMID_EMPTY_BOTTLE			= 713,
	ITEMID_EMPERIUM				,
	ITEMID_YELLOW_GEMSTONE		,
	ITEMID_RED_GEMSTONE			,
	ITEMID_BLUE_GEMSTONE		,
	ITEMID_TRAP = 1065			,
	ITEMID_FACEPAINTS			= 6120,
	ITEMID_MAKEUPBRUSH			,
	ITEMID_PAINT_BRUSH			,
	ITEMID_SURFACEPAINTS		,
	ITEMID_SPECIAL_ALLOY_TRAP	= 7940,
	ITEMID_ANCILLA				= 12333,
	ITEMID_BOARDING_HALTER		= 12622,
	ITEMID_STONE				= 7049,
	ITEMID_SKULL_				= 7420,
	ITEMID_TOKEN_OF_SIEGFRIED	= 7621,
};

#define itemid_isgemstone(id) ( (id) >= ITEMID_YELLOW_GEMSTONE && (id) <= ITEMID_BLUE_GEMSTONE )
#define itemdb_iscashfood(id) ((id) >= 12202 && (id) <= 12207)
#define itemdb_is_rune(n) (n >= ITEMID_REFRESH && n <= ITEMID_STONEHARDSKIN)
#define itemdb_is_poison(n) (n >= 12717 && n <= 12724)
#define itemdb_is_spellbook(n) (n >= 6188 && n <= 6205)
#define itemdb_is_element(n) (n >= 990 && n <= 993)
#define itemdb_is_GNbomb(n) (n >= 13260 && n <= 13267)

//The only item group required by the code to be known. See const.txt for the full list.
#define IG_FINDINGORE 6
#define IG_POTION 37
//The max. item group count, same for item packages (increase this when needed).
#define MAX_ITEMGROUP 58
#define MAX_ITEMPACKAGE 21

#define CARD0_FORGE 0x00FF
#define CARD0_CREATE 0x00FE
#define CARD0_PET 0x0100

//Marks if the card0 given is "special" (non-item id used to mark pets/created items. [Skotlex]
#define itemdb_isspecial(i) (i == CARD0_FORGE || i == CARD0_CREATE || i == CARD0_PET)

//Use apple for unknown items.
#define UNKNOWN_ITEM_ID 512

/// Struct of Roulette db
struct s_roulette_db
{
	unsigned short *nameid[MAX_ROULETTE_LEVEL], /// Item ID
		           *qty[MAX_ROULETTE_LEVEL]; /// Amount of Item ID
	int *flag[MAX_ROULETTE_LEVEL]; /// Whether the item is for loss or win
	int items[MAX_ROULETTE_LEVEL]; /// Number of items in the list for each
} rd;

struct item_data {
	unsigned short nameid;
	char name[ITEM_NAME_LENGTH],jname[ITEM_NAME_LENGTH];
	//Do not add stuff between value_buy and wlv (see how getiteminfo works)
	int value_buy;
	int value_sell;
	int type;
	int maxchance; //For logs, for external game info, for scripts: Max drop chance of this item (e.g. 0.01% , etc.. if it = 0, then monsters don't drop it, -1 denotes items sold in shops only) [Lupus]
	int sex;
	int equip;
	int weight;
	int atk;
	int def;
	int range;
	int slot;
	int look;
	int elv;
	int wlv;
	int view_id;
	int delay;
//Lupus: I rearranged order of these fields due to compatibility with ITEMINFO script command
//		some script commands should be revised as well...
	unsigned int class_base[3];	//Specifies if the base can wear this item (split in 3 indexes per type: 1-1, 2-1, 2-2)
	unsigned class_upper : 4; //Specifies if the upper-type can equip it (bitfield, 1: normal, 2: upper, 3: baby, 4: third)
	struct {
		unsigned short chance;
		int id;
	} mob[MAX_SEARCH]; //Holds the mobs that have the highest drop rate for this item. [Skotlex]
	struct script_code *script;	//Default script for everything.
	struct script_code *equip_script;	//Script executed once when equipping.
	struct script_code *unequip_script;//Script executed once when unequipping.
	struct {
		unsigned available : 1;
		short no_equip;
		unsigned no_refine : 1;	// [celest]
		unsigned delay_consume : 1;	// Signifies items that are not consumed immediately upon double-click [Skotlex]
		unsigned trade_restriction : 7;	//Item restrictions mask [Skotlex]
		unsigned autoequip: 1;
		unsigned buyingstore : 1;
	} flag;
	short gm_lv_trade_override;	//GM-level to override trade_restriction
};

struct item_group {
	unsigned short nameid[MAX_RANDITEM];
	int qty; //Counts amount of items in the group.
};

struct item_package {
	unsigned short nameid[MAX_RANDITEM];
	int qty; //Counts amount of items in the group.
	unsigned short prob[MAX_RANDITEM];
	unsigned short amount[MAX_RANDITEM];
	unsigned short isrand[MAX_RANDITEM];
	unsigned short announced[MAX_RANDITEM];
	unsigned short max_rand;
};

enum {
	ITEMID_REFRESH = 12725,
	ITEMID_REUSE_CRUSHSTRIKE,
	ITEMID_REUSE_MILLENNIUMSHIELD,
	ITEMID_VITALITYACTIVATION,
	ITEMID_FIGHTINGSPIRIT,
	ITEMID_URUZ,
	ITEMID_GIANTGROWTH,
	ITEMID_REUSE_REFRESH,
	ITEMID_STONEHARDSKIN,
} rune_list;

// Struct for item random option [Secret]
struct s_random_opt_data
{
	unsigned short id;
	struct script_code *script;
};

struct item_data* itemdb_searchname(const char *name);
int itemdb_searchname_array(struct item_data** data, int size, const char *str);
struct item_data* itemdb_load(unsigned short nameid);
struct item_data* itemdb_search(unsigned short nameid);
struct item_data* itemdb_exists(unsigned short nameid);
struct s_random_opt_data* itemdb_randomopt_exists(short id);
#define itemdb_name(n) itemdb_search(n)->name
#define itemdb_jname(n) itemdb_search(n)->jname
#define itemdb_type(n) itemdb_search(n)->type
#define itemdb_atk(n) itemdb_search(n)->atk
#define itemdb_def(n) itemdb_search(n)->def
#define itemdb_look(n) itemdb_search(n)->look
#define itemdb_weight(n) itemdb_search(n)->weight
#define itemdb_equip(n) itemdb_search(n)->equip
#define itemdb_usescript(n) itemdb_search(n)->script
#define itemdb_equipscript(n) itemdb_search(n)->script
#define itemdb_wlv(n) itemdb_search(n)->wlv
#define itemdb_range(n) itemdb_search(n)->range
#define itemdb_slot(n) itemdb_search(n)->slot
#define itemdb_available(n) (itemdb_search(n)->flag.available)
#define itemdb_viewid(n) (itemdb_search(n)->view_id)
#define itemdb_autoequip(n) (itemdb_search(n)->flag.autoequip)
const char* itemdb_typename(int type);

int itemdb_group_bonus(struct map_session_data* sd, int itemid);
int itemdb_searchrandomid(int flags);

#define itemdb_value_buy(n) itemdb_search(n)->value_buy
#define itemdb_value_sell(n) itemdb_search(n)->value_sell
#define itemdb_canrefine(n) (!itemdb_search(n)->flag.no_refine)
//Item trade restrictions [Skotlex]
int itemdb_isdropable_sub(struct item_data *, int, int);
int itemdb_cantrade_sub(struct item_data*, int, int);
int itemdb_canpartnertrade_sub(struct item_data*, int, int);
int itemdb_cansell_sub(struct item_data*,int, int);
int itemdb_cancartstore_sub(struct item_data*, int, int);
int itemdb_canstore_sub(struct item_data*, int, int);
int itemdb_canguildstore_sub(struct item_data*, int, int);
int itemdb_isrestricted(struct item* item, int gmlv, int gmlv2, int (*func)(struct item_data*, int, int));
#define itemdb_isdropable(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_isdropable_sub)
#define itemdb_cantrade(item, gmlv, gmlv2) itemdb_isrestricted(item, gmlv, gmlv2, itemdb_cantrade_sub)
#define itemdb_canpartnertrade(item, gmlv, gmlv2) itemdb_isrestricted(item, gmlv, gmlv2, itemdb_canpartnertrade_sub)
#define itemdb_cansell(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_cansell_sub)
#define itemdb_cancartstore(item, gmlv)  itemdb_isrestricted(item, gmlv, 0, itemdb_cancartstore_sub)
#define itemdb_canstore(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_canstore_sub) 
#define itemdb_canguildstore(item, gmlv) itemdb_isrestricted(item , gmlv, 0, itemdb_canguildstore_sub) 

int itemdb_isequip(unsigned short);
int itemdb_isequip2(struct item_data *);
int itemdb_isidentified(unsigned short);
int itemdb_isstackable(unsigned short);
int itemdb_isstackable2(struct item_data *);

bool itemdb_parse_roulette_db(void);

void itemdb_package_item(struct map_session_data *sd, int packageid);

void itemdb_reload(void);

void do_final_itemdb(void);
int do_init_itemdb(void);

#endif /* _ITEMDB_H_ */
