// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// Copyright (c) Hercules Dev Team - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef MAP_PACKETS_STRUCT_H
#define MAP_PACKETS_STRUCT_H

#include "../common/cbasetypes.h"
#include "../common/mmo.h"

enum packet_headers {
#if PACKETVER < 4
	idle_unitType = 0x78,
#elif PACKETVER < 7
	idle_unitType = 0x1d8,
#elif PACKETVER < 20080102
	idle_unitType = 0x22a,
#elif PACKETVER < 20091103
	idle_unitType = 0x2ee,
#elif PACKETVER < 20101124
	idle_unitType = 0x7f9,
#elif PACKETVER < 20120221
	idle_unitType = 0x857,
#elif PACKETVER < 20131223
	idle_unitType = 0x915,
#elif PACKETVER < 20150513
	idle_unitType = 0x9dd,
#else
	idle_unitType = 0x9ff,
#endif
#if PACKETVER < 4
	spawn_unitType = 0x79,
#elif PACKETVER < 7
	spawn_unitType = 0x1d9,
#elif PACKETVER < 20080102
	spawn_unitType = 0x22b,
#elif PACKETVER < 20091103
	spawn_unitType = 0x2ed,
#elif PACKETVER < 20101124
	spawn_unitType = 0x7f8,
#elif PACKETVER < 20120221
	spawn_unitType = 0x858,
#elif PACKETVER < 20131223
	spawn_unitType = 0x90f,
#elif PACKETVER < 20150513
	spawn_unitType = 0x9dc,
#else
	spawn_unitType = 0x9fe,
#endif
#if PACKETVER < 4
	unit_walkingType = 0x7b,
#elif PACKETVER < 7
	unit_walkingType = 0x1da,
#elif PACKETVER < 20080102
	unit_walkingType = 0x22c,
#elif PACKETVER < 20091103
	unit_walkingType = 0x2ec,
#elif PACKETVER < 20101124
	unit_walkingType = 0x7f7,
#elif PACKETVER < 20120221
	unit_walkingType = 0x856,
#elif PACKETVER < 20131223
	unit_walkingType = 0x914,
#elif PACKETVER < 20150513
	unit_walkingType = 0x9db,
#else
	unit_walkingType = 0x9fd,
#endif
#if PACKETVER >= 3
	useItemAckType = 0x1c8,
#else
	useItemAckType = 0xa8,
#endif
#if PACKETVER < 20061218
	additemType = 0x0a0,
#elif PACKETVER < 20071002
	additemType = 0x29a,
#elif PACKETVER < 20120925
	additemType = 0x2d4,
#elif PACKETVER < 20150226
	additemType = 0x990,
#elif PACKETVER < 20160921
	additemType = 0xa0c,
#else
	additemType = 0xa37,
#endif
#if PACKETVER >= 20150226
	cartaddType = 0xa0b,
#elif PACKETVER >= 5
	cartaddType = 0x1c5,
#else
	cartaddType = 0x124,
#endif
#if PACKETVER >= 20180418
	dropflooritemType = 0xadd,
#elif PACKETVER > 20130000 /* not sure date */
	dropflooritemType = 0x84b,
#else
	dropflooritemType = 0x9e,
#endif
#if PACKETVER >= 20181002
	inventorylistnormalType = 0xb09,
#elif PACKETVER >= 20120925
	inventorylistnormalType = 0x991,
#elif PACKETVER >= 20080102
	inventorylistnormalType = 0x2e8,
#elif PACKETVER >= 20071002
	inventorylistnormalType = 0x1ee,
#else
	inventorylistnormalType = 0xa3,
#endif
#if PACKETVER >= 20181002
	inventorylistequipType = 0xb0a,
#elif PACKETVER >= 20150226
	inventorylistequipType = 0xa0d,
#elif PACKETVER >= 20120925
	inventorylistequipType = 0x992,
#elif PACKETVER >= 20080102
	inventorylistequipType = 0x2d0,
#elif PACKETVER >= 20071002
	inventorylistequipType = 0x295,
#else
	inventorylistequipType = 0xa4,
#endif
#if PACKETVER >= 20181002
		storageListNormalType = 0xb09,
#elif PACKETVER >= 20120925
		storageListNormalType = 0x995,
#elif PACKETVER >= 20080102
		storageListNormalType = 0x2ea,
#elif PACKETVER >= 20071002
		storageListNormalType = 0x295,
#else
		storageListNormalType = 0xa5,
#endif
#if PACKETVER >= 20181002
		storageListEquipType = 0xb0a,
#elif PACKETVER >= 20150226
		storageListEquipType = 0xa10,
#elif PACKETVER >= 20120925
		storageListEquipType = 0x996,
#elif PACKETVER >= 20080102
		storageListEquipType = 0x2d1,
#elif PACKETVER >= 20071002
		storageListEquipType = 0x296,
#else
		storageListEquipType = 0xa6,
#endif
#if PACKETVER >= 20181002
		cartlistnormalType = 0xb09,
#elif PACKETVER >= 20120925
		cartlistnormalType = 0x993,
#elif PACKETVER >= 20080102
		cartlistnormalType = 0x2e9,
#elif PACKETVER >= 20071002
		cartlistnormalType = 0x1ef,
#else
		cartlistnormalType = 0x123,
#endif
#if PACKETVER >= 20181002
		cartlistequipType = 0xb0a,
#elif PACKETVER >= 20150226
		cartlistequipType = 0xa0f,
#elif PACKETVER >= 20120925
		cartlistequipType = 0x994,
#elif PACKETVER >= 20080102
		cartlistequipType = 0x2d2,
#elif PACKETVER >= 20071002
		cartlistequipType = 0x297,
#else
		cartlistequipType = 0x122,
#endif
#if PACKETVER >= 20150226
		storageaddType = 0xa0a,
#elif PACKETVER >= 5
		storageaddType = 0x1c4,
#else
		storageaddType = 0xf4,
#endif
#if PACKETVER >= 20150226
		tradeaddType = 0xa09,
#elif PACKETVER >= 20100223
		tradeaddType = 0x80f,
#else
		tradeaddType = 0x0e9,
#endif
#if PACKETVER < 20100105
		vendinglistType = 0x133,
#else
		vendinglistType = 0x800,
#endif
#if PACKETVER >= 20141016
		buyingStoreUpdateItemType = 0x9e6,
#else
		buyingStoreUpdateItemType = 0x81b,
#endif
#if PACKETVER >= 4
	sendLookType = 0x1d7,
#else
	sendLookType = 0xc3,
#endif
#if PACKETVER >= 20180801
		viewequipackType = 0xb03,
#elif PACKETVER >= 20150226
		viewequipackType = 0xa2d,
#elif PACKETVER >= 20120925
		viewequipackType = 0x997,
#elif PACKETVER >= 20111207
	viewequipackType = 0x906,
#elif PACKETVER >= 20101124
		viewequipackType = 0x859,
#else
		viewequipackType = 0x2d7,
#endif
};

#if !defined(sun) && (!defined(__NETBSD__) || __NetBSD_Version__ >= 600000000) // NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#pragma pack(push, 1)
#endif // not NetBSD < 6 / Solaris

struct EQUIPSLOTINFO {
#if PACKETVER >= 20181121
	uint32 card[4];
#else
	uint16 card[4];
#endif
} __attribute__((packed));

struct ItemOptions {
	int16 index;
	int16 value;
	uint8 param;
} __attribute__((packed));

struct NORMALITEM_INFO {
	int16 index;
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
	uint8 type;
#if PACKETVER < 20120925
	uint8 IsIdentified;
#endif
	int16 count;
#if PACKETVER >= 20120925
	uint32 WearState;
#else
	uint16 WearState;
#endif
#if PACKETVER >= 5
	struct EQUIPSLOTINFO slot;
#endif
#if PACKETVER >= 20080102
	int32 HireExpireDate;
#endif
#if PACKETVER >= 20120925
	struct {
		uint8 IsIdentified : 1;
		uint8 PlaceETCTab : 1;
		uint8 SpareBits : 6;
	} Flag;
#endif
} __attribute__((packed));

struct EQUIPITEM_INFO {
	int16 index;
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
	uint8 type;
#if PACKETVER < 20120925
	uint8 IsIdentified;
#endif
#if PACKETVER >= 20120925
	uint32 location;
	uint32 WearState;
#else
	uint16 location;
	uint16 WearState;
#endif
#if PACKETVER < 20120925
	uint8 IsDamaged;
#endif
	uint8 RefiningLevel;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20071002
	int32 HireExpireDate;
#endif
#if PACKETVER >= 20080102
	uint16 bindOnEquipType;
#endif
#if PACKETVER >= 20100629
	uint16 wItemSpriteNumber;
#endif
#if PACKETVER >= 20150226
	uint8 option_count;
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
#if PACKETVER >= 20120925
	struct {
		uint8 IsIdentified : 1;
		uint8 IsDamaged : 1;
		uint8 PlaceETCTab : 1;
		uint8 SpareBits : 5;
	} Flag;
#endif
} __attribute__((packed));

struct packet_itemlist_normal {
	int16 PacketType;
	int16 PacketLength;
#if PACKETVER >= 20181002
	uint8 invType;
#endif
	struct NORMALITEM_INFO list[MAX_ITEMLIST];
} __attribute__((packed));

struct packet_itemlist_equip {
	int16 PacketType;
	int16 PacketLength;
#if PACKETVER >= 20181002
	uint8 invType;
#endif
	struct EQUIPITEM_INFO list[MAX_ITEMLIST];
} __attribute__((packed));

struct ZC_STORE_ITEMLIST_NORMAL {
	int16 PacketType;
	int16 PacketLength;
#if PACKETVER >= 20181002
	uint8 invType;
#endif
#if PACKETVER >= 20120925 && PACKETVER < 20181002
	char name[NAME_LENGTH];
#endif
	struct NORMALITEM_INFO list[MAX_ITEMLIST];
} __attribute__((packed));

struct ZC_STORE_ITEMLIST_EQUIP {
	int16 PacketType;
	int16 PacketLength;
#if PACKETVER >= 20181002
	uint8 invType;
#endif
#if PACKETVER >= 20120925 && PACKETVER < 20181002
	char name[NAME_LENGTH];
#endif
	struct EQUIPITEM_INFO list[MAX_ITEMLIST];
} __attribute__((packed));

struct PACKET_ZC_ADD_EXCHANGE_ITEM {
	int16 packetType;
#if PACKETVER >= 20181121
	uint32 itemId;
	uint8 itemType;
	int32 amount;
#elif PACKETVER >= 20100223
	uint16 itemId;
	uint8 itemType;
	int32 amount;
#else
	int32 amount;
	uint16 itemId;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
} __attribute__((packed));

struct PACKET_ZC_ADD_ITEM_TO_STORE {
	int16 packetType;
	int16 index;
	int32 amount;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
#if PACKETVER >= 5
	uint8 itemType;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
} __attribute__((packed));

struct PACKET_ZC_ADD_ITEM_TO_CART {
	int16 packetType;
	int16 index;
	int32 amount;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
#if PACKETVER >= 5
	uint8 itemType;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
} __attribute__((packed));

struct PACKET_ZC_REPAIRITEMLIST_sub {
	int16 index;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 refine;  // unused?
	struct EQUIPSLOTINFO slot;  // unused?
} __attribute__((packed));

struct PACKET_ZC_REPAIRITEMLIST {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_REPAIRITEMLIST_sub items[];
} __attribute__((packed));

struct PACKET_ZC_NOTIFY_WEAPONITEMLIST_sub {
	int16 index;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 refine;  // unused?
	struct EQUIPSLOTINFO slot;  // unused?
} __attribute__((packed));

struct PACKET_ZC_NOTIFY_WEAPONITEMLIST {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_NOTIFY_WEAPONITEMLIST_sub items[];
} __attribute__((packed));

struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC_sub {
	uint32 price;
	uint16 amount;
	int16 index;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
	// [4144] date 20160921 not confirmed. Can be bigger or smaller
#if PACKETVER >= 20160921
	uint32 location;
	uint16 viewSprite;
#endif
} __attribute__((packed));

struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC {
	int16 packetType;
	int16 packetLength;
	uint32 AID;
#if PACKETVER >= 20100105
	uint32 venderId;
#endif
	struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC_sub items[];
} __attribute__((packed));

struct packet_additem {
	int16 PacketType;
	uint16 Index;
	uint16 count;
#if PACKETVER >= 20181121
	uint32 nameid;
#else
	uint16 nameid;
#endif
	uint8 IsIdentified;
	uint8 IsDamaged;
	uint8 refiningLevel;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20120925
	uint32 location;
#else
	uint16 location;
#endif
	uint8 type;
	uint8 result;
#if PACKETVER >= 20061218
	int32 HireExpireDate;
#endif
#if PACKETVER >= 20071002
	uint16 bindOnEquipType;
#endif
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
#if PACKETVER >= 20160921
	uint8 favorite;
	uint16 look;
#endif
} __attribute__((packed));

struct packet_dropflooritem {
	int16 PacketType;
	uint32 ITAID;
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
#if PACKETVER >= 20130000 /* not sure date */
	uint16 type;
#endif
	uint8 IsIdentified;
	int16 xPos;
	int16 yPos;
	uint8 subX;
	uint8 subY;
	int16 count;
#if PACKETVER >= 20180418
	int8 showdropeffect;
	int16 dropeffectmode;
#endif
} __attribute__((packed));

struct PACKET_ZC_FEED_MER {
	int16 packetType;
	uint8 result;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct packet_npc_market_purchase_sub {
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
	int32 qty;
} __attribute__((packed));

struct packet_npc_market_purchase {
	int16 PacketType;
	int16 PacketLength;
	struct packet_npc_market_purchase_sub list[];
} __attribute__((packed));

struct PACKET_ZC_PC_PURCHASE_ITEMLIST_sub {
	uint32 price;
	uint32 discountPrice;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_PC_PURCHASE_ITEMLIST {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_PC_PURCHASE_ITEMLIST_sub items[];
} __attribute__((packed));

#if PACKETVER >= 20131120
/* inner struct figured by Ind after some annoying hour of debugging (data Thanks to Yommy) */
struct PACKET_ZC_NPC_MARKET_OPEN_sub {
#if PACKETVER >= 20181121
	uint32 nameid;
#else
	uint16 nameid;
#endif
	uint8 type;
	uint32 price;
	uint32 qty;
	uint16 weight;
} __attribute__((packed));

struct PACKET_ZC_NPC_MARKET_OPEN {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_NPC_MARKET_OPEN_sub list[];
} __attribute__((packed));
#endif

struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT_sub {
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
	uint16 qty;
	uint32 price;
} __attribute__((packed));

#if PACKETVER >= 20131120
struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT {
	int16 PacketType;
	int16 PacketLength;
	uint8 result;
	struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT_sub list[];
} __attribute__((packed));
#endif

struct PACKET_ZC_PC_PURCHASE_MYITEMLIST_sub {
	uint32 price;
	int16 index;
	int16 amount;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
} __attribute__((packed));

struct PACKET_ZC_PC_PURCHASE_MYITEMLIST {
	int16 packetType;
	int16 packetLength;
	uint32 AID;
	struct PACKET_ZC_PC_PURCHASE_MYITEMLIST_sub items[];
} __attribute__((packed));

struct PACKET_ZC_PC_CASH_POINT_ITEMLIST_sub {
	uint32 price;
	uint32 discountPrice;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_PC_CASH_POINT_ITEMLIST {
	int16 packetType;
	int16 packetLength;
	uint32 cashPoints;
#if PACKETVER >= 20070711
	uint32 kafraPoints;
#endif
	struct PACKET_ZC_PC_CASH_POINT_ITEMLIST_sub items[];
} __attribute__((packed));

struct PACKET_CZ_PC_BUY_CASH_POINT_ITEM_sub {
	uint16 amount;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_CZ_PC_BUY_CASH_POINT_ITEM {
	int16 packetType;
#if PACKETVER >= 20101116
	int16 packetLength;
	uint32 kafraPoints;
	uint16 count;
	struct PACKET_CZ_PC_BUY_CASH_POINT_ITEM_sub items[];
#else
	uint16 itemId;
	uint16 amount;
#if PACKETVER >= 20070711
	uint32 kafraPoints;
#endif
#endif
} __attribute__((packed));

struct PACKET_ZC_ACK_SCHEDULER_CASHITEM_sub {
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint32 price;
} __attribute__((packed));

#if PACKETVER >= 20101123
struct PACKET_ZC_SE_PC_BUY_CASHITEM_RESULT {
	int16 packetType;
	uint32 itemId;  // unused
	uint16 result;
	uint32 cashPoints;
	uint32 kafraPoints;
} __attribute__((packed));
#endif

struct PACKET_ZC_ACK_SCHEDULER_CASHITEM {
	int16 packetType;
	int16 packetLength;
	int16 count;
	int16 tabNum;
	struct PACKET_ZC_ACK_SCHEDULER_CASHITEM_sub items[];
} __attribute__((packed));

struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST_sub {
	uint32 itemId;
	uint32 amount;
	uint16 tab;
} __attribute__((packed));

struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST {
	int16 packetType;
	int16 packetLength;
	uint16 count;
	uint32 kafraPoints;
	struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST_sub items[];
} __attribute__((packed));

struct packet_viewequip_ack {
	int16 PacketType;
	int16 PacketLength;
	char characterName[NAME_LENGTH];
	int16 job;
	int16 head;
	int16 accessory;
	int16 accessory2;
	int16 accessory3;
#if PACKETVER >= 20101124
	int16 robe;
#endif
	int16 headpalette;
	int16 bodypalette;
#if PACKETVER >= 20180801
	int16 body2;
#endif
	uint8 sex;
	// [4144] need remove MAX_INVENTORY from here
	struct EQUIPITEM_INFO list[MAX_INVENTORY];
} __attribute__((packed));

struct PACKET_ZC_SEARCH_STORE_INFO_ACK_sub {
	uint32 storeId;
	uint32 AID;
	char shopName[MESSAGE_SIZE];
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 itemType;
	uint32 price;
	uint16 amount;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
#if PACKETVER >= 20150226
	struct ItemOptions option_data[MAX_ITEM_RDM_OPT];
#endif
} __attribute__((packed));

struct PACKET_ZC_SEARCH_STORE_INFO_ACK {
	int16 packetType;
	int16 packetLength;
	uint8 firstPage;
	uint8 nextPage;
	uint8 usesCount;
	struct PACKET_ZC_SEARCH_STORE_INFO_ACK_sub items[];
} __attribute__((packed));


struct PACKET_ZC_ADD_ITEM_TO_MAIL {
	int16 PacketType;
	int8 result;
	int16 index;
	int16 count;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	int8 type;
	int8 IsIdentified;
	int8 IsDamaged;
	int8 refiningLevel;
	struct EQUIPSLOTINFO slot;
	struct ItemOptions optionData[MAX_ITEM_RDM_OPT];
	int16 weight;
	uint8 favorite;
	uint32 location;
} __attribute__((packed));

struct PACKET_ZC_READ_MAIL {
	int16 PacketType;
	int16 PacketLength;
	int8 opentype;
	int64 MailID;
	int16 TextcontentsLength;
	int64 zeny;
	int8 ItemCnt;
} __attribute__((packed));

struct mail_item {
	int16 count;
#if PACKETVER >= 20181121
	uint32 ITID;
#else
	uint16 ITID;
#endif
	int8 IsIdentified;
	int8 IsDamaged;
	int8 refiningLevel;
	struct EQUIPSLOTINFO slot;
	uint32 location;
	uint8 type;
	uint16 viewSprite;
	uint16 bindOnEquip;
	struct ItemOptions optionData[MAX_ITEM_RDM_OPT];
} __attribute__((packed));

struct PACKET_ZC_ITEM_PICKUP_PARTY {
	int16 packetType;
	uint32 AID;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 identified;
	uint8 damaged;
	uint8 refine;
	struct EQUIPSLOTINFO slot;
	uint16 location;
	uint8 itemType;
} __attribute__((packed));

struct packet_idle_unit2 {
#if PACKETVER < 20091103
	int16 PacketType;
#if PACKETVER >= 20071106
	uint8 objecttype;
#endif
	uint32 GID;
	int16 speed;
	int16 bodyState;
	int16 healthState;
	int16 effectState;
	int16 job;
	uint16 head;
	uint16 weapon;
	uint16 accessory;
	uint16 shield;
	uint16 accessory2;
	uint16 accessory3;
	int16 headpalette;
	int16 bodypalette;
	int16 headDir;
	uint32 GUID;
	int16 GEmblemVer;
	int16 honor;
	int16 virtue;
	uint8 isPKModeON;
	uint8 sex;
	uint8 PosDir[3];
	uint8 xSize;
	uint8 ySize;
	uint8 state;
	int16 clevel;
#else // ! PACKETVER < 20091103
	UNAVAILABLE_STRUCT;
#endif // PACKETVER < 20091103
} __attribute__((packed));

struct packet_spawn_unit2 {
#if PACKETVER < 20091103
	int16 PacketType;
#if PACKETVER >= 20071106
	uint8 objecttype;
#endif
	uint32 GID;
	int16 speed;
	int16 bodyState;
	int16 healthState;
	int16 effectState;
	uint16 head;
	uint16 weapon;
	uint16 accessory;
	int16 job;
	uint16 shield;
	uint16 accessory2;
	uint16 accessory3;
	int16 headpalette;
	int16 bodypalette;
	int16 headDir;
	uint8 isPKModeON;
	uint8 sex;
	uint8 PosDir[3];
	uint8 xSize;
	uint8 ySize;
#else // ! PACKETVER < 20091103
	UNAVAILABLE_STRUCT;
#endif // PACKETVER < 20091103
} __attribute__((packed));

struct packet_spawn_unit {
	int16 PacketType;
#if PACKETVER >= 20091103
	int16 PacketLength;
	uint8 objecttype;
#endif
#if PACKETVER >= 20131223
	uint32 AID;
#endif
	uint32 GID;
	int16 speed;
	int16 bodyState;
	int16 healthState;
#if PACKETVER < 20080102
	int16 effectState;
#else
	int32 effectState;
#endif
	int16 job;
	uint16 head;
#if PACKETVER < 7
	uint16 weapon;
#else
	uint32 weapon;
#endif
#if PACKETVER >= 20181121
	uint32 shield;
#endif
	uint16 accessory;
#if PACKETVER < 7
	uint16 shield;
#endif
	uint16 accessory2;
	uint16 accessory3;
	int16 headpalette;
	int16 bodypalette;
	int16 headDir;
#if PACKETVER >= 20101124
	uint16 robe;
#endif
	uint32 GUID;
	int16 GEmblemVer;
	int16 honor;
#if PACKETVER > 7
	int32 virtue;
#else
	int16 virtue;
#endif
	uint8 isPKModeON;
	uint8 sex;
	uint8 PosDir[3];
	uint8 xSize;
	uint8 ySize;
	int16 clevel;
#if PACKETVER >= 20080102
	int16 font;
#endif
#if PACKETVER >= 20120221
	int32 maxHP;
	int32 HP;
	uint8 isBoss;
#endif
#if PACKETVER >= 20150513
	int16 body;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	char name[NAME_LENGTH];
#endif
} __attribute__((packed));

struct packet_unit_walking {
	int16 PacketType;
#if PACKETVER >= 20091103
	int16 PacketLength;
#endif
#if PACKETVER >= 20071106
	uint8 objecttype;
#endif
#if PACKETVER >= 20131223
	uint32 AID;
#endif
	uint32 GID;
	int16 speed;
	int16 bodyState;
	int16 healthState;
#if PACKETVER < 7
	int16 effectState;
#else
	int32 effectState;
#endif
	int16 job;
	uint16 head;
#if PACKETVER < 7
	uint16 weapon;
#else
	uint32 weapon;
#endif
#if PACKETVER >= 20181121
	uint32 shield;
#endif
	uint16 accessory;
	uint32 moveStartTime;
#if PACKETVER < 7
	uint16 shield;
#endif
	uint16 accessory2;
	uint16 accessory3;
	int16 headpalette;
	int16 bodypalette;
	int16 headDir;
#if PACKETVER >= 20101124
	uint16 robe;
#endif
	uint32 GUID;
	int16 GEmblemVer;
	int16 honor;
#if PACKETVER > 7
	int32 virtue;
#else
	int16 virtue;
#endif
	uint8 isPKModeON;
	uint8 sex;
	uint8 MoveData[6];
	uint8 xSize;
	uint8 ySize;
	int16 clevel;
#if PACKETVER >= 20080102
	int16 font;
#endif
#if PACKETVER >= 20120221
	int32 maxHP;
	int32 HP;
	uint8 isBoss;
#endif
#if PACKETVER >= 20150513
	uint16 body;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	char name[NAME_LENGTH];
#endif
} __attribute__((packed));

struct packet_idle_unit {
	int16 PacketType;
#if PACKETVER >= 20091103
	int16 PacketLength;
	uint8 objecttype;
#endif
#if PACKETVER >= 20131223
	uint32 AID;
#endif
	uint32 GID;
	int16 speed;
	int16 bodyState;
	int16 healthState;
#if PACKETVER < 20080102
	int16 effectState;
#else
	int32 effectState;
#endif
	int16 job;
	uint16 head;
#if PACKETVER < 7
	uint16 weapon;
#else
	uint32 weapon;
#endif
#if PACKETVER >= 20181121
	uint32 shield;
#endif
	uint16 accessory;
#if PACKETVER < 7
	uint16 shield;
#endif
	uint16 accessory2;
	uint16 accessory3;
	int16 headpalette;
	int16 bodypalette;
	int16 headDir;
#if PACKETVER >= 20101124
	uint16 robe;
#endif
	uint32 GUID;
	int16 GEmblemVer;
	int16 honor;
#if PACKETVER > 7
	int32 virtue;
#else
	int16 virtue;
#endif
	uint8 isPKModeON;
	uint8 sex;
	uint8 PosDir[3];
	uint8 xSize;
	uint8 ySize;
	uint8 state;
	int16 clevel;
#if PACKETVER >= 20080102
	int16 font;
#endif
#if PACKETVER >= 20120221
	int32 maxHP;
	int32 HP;
	uint8 isBoss;
#endif
#if PACKETVER >= 20150513
	uint16 body;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	char name[NAME_LENGTH];
#endif
} __attribute__((packed));

struct PACKET_ZC_SPRITE_CHANGE {
	int16 packetType;
	uint32 AID;
	uint8 type;
#if PACKETVER >= 20181121
	uint32 val;
	uint32 val2;
#elif PACKETVER >= 4
	uint16 val;
	uint16 val2;
#else
	uint8 val;
#endif
} __attribute__((packed));

struct PACKET_ZC_ACK_TOUSESKILL {
	int16 packetType;
	uint16 skillId;
#if PACKETVER >= 20181121
	int32 btype;
	uint32 itemId;
#else
	int16 btype;
	uint16 itemId;
#endif
	uint8 flag;
	uint8 cause;
} __attribute__((packed));

#if PACKETVER >= 20131230
// PACKET_ZC_PROPERTY_HOMUN2
struct PACKET_ZC_PROPERTY_HOMUN {
	int16 packetType;
	char name[NAME_LENGTH];
	// Bit field, bit 0 : rename_flag (1 = already renamed), bit 1 : homunc vaporized (1 = true), bit 2 : homunc dead (1 = true)
	uint8 flags;
	uint16 level;
	uint16 hunger;
	uint16 intimacy;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint16 atk2;
	uint16 matk;
	uint16 hit;
	uint16 crit;
	uint16 def;
	uint16 mdef;
	uint16 flee;
	uint16 amotion;
	uint32 hp;
	uint32 maxHp;
	uint16 sp;
	uint16 maxSp;
	uint32 exp;
	uint32 expNext;
	uint16 skillPoints;
	uint16 range;
} __attribute__((packed));
#define HEADER_ZC_PROPERTY_HOMUN 0x09f7;
#elif PACKETVER >= 20101005
// PACKET_ZC_PROPERTY_HOMUN1
struct PACKET_ZC_PROPERTY_HOMUN {
	int16 packetType;
	char name[NAME_LENGTH];
	// Bit field, bit 0 : rename_flag (1 = already renamed), bit 1 : homunc vaporized (1 = true), bit 2 : homunc dead (1 = true)
	uint8 flags;
	uint16 level;
	uint16 hunger;
	uint16 intimacy;
	uint16 itemId;
	uint16 atk2;
	uint16 matk;
	uint16 hit;
	uint16 crit;
	uint16 def;
	uint16 mdef;
	uint16 flee;
	uint16 amotion;
	uint16 hp;
	uint16 maxHp;
	uint16 sp;
	uint16 maxSp;
	uint32 exp;
	uint32 expNext;
	uint16 skillPoints;
	uint16 range;
} __attribute__((packed));
#define HEADER_ZC_PROPERTY_HOMUN 0x022e;
#endif

struct PACKET_ZC_USE_ITEM_ACK {
	int16 packetType;
	int16 index;
#if PACKETVER >= 20181121
	uint32 itemId;
	uint32 AID;
#elif PACKETVER >= 3
	uint16 itemId;
	uint32 AID;
#endif
	int16 amount;
	uint8 result;
} __attribute__((packed));

struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub {
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint16 amount;
	uint32 price;
} __attribute__((packed));

struct PACKET_CZ_REQ_OPEN_BUYING_STORE {
	int16 packetType;
	int16 packetLength;
	uint32 zenyLimit;
	uint8 result;
	char storeName[MESSAGE_SIZE];
	struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub items[];
} __attribute__((packed));

struct PACKET_ZC_MYITEMLIST_BUYING_STORE_sub {
	uint32 price;
	uint16 amount;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_MYITEMLIST_BUYING_STORE {
	int16 packetType;
	int16 packetLength;
	uint32 AID;
	uint32 zenyLimit;
	struct PACKET_ZC_MYITEMLIST_BUYING_STORE_sub items[];
} __attribute__((packed));

struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE_sub {
	uint32 price;
	uint16 amount;
	uint8 itemType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE {
	int16 packetType;
	int16 packetLength;
	uint32 AID;
	uint32 storeId;
	uint32 zenyLimit;
	struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE_sub items[];
} __attribute__((packed));

struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub {
	int16 index;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint16 amount;
} __attribute__((packed));

struct PACKET_CZ_REQ_TRADE_BUYING_STORE {
	int16 packetType;
	int16 packetLength;
	uint32 AID;
	uint32 storeId;
	struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub items[];
} __attribute__((packed));

struct PACKET_ZC_UPDATE_ITEM_FROM_BUYING_STORE {
	int16 packetType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint16 amount;
#if PACKETVER >= 20141016
	uint32 zeny;
	uint32 zenyLimit;
	uint32 charId;
	uint32 updateTime;
#else
	uint32 zenyLimit;
#endif
} __attribute__((packed));

struct PACKET_ZC_FAILED_TRADE_BUYING_STORE_TO_SELLER {
	int16 packetType;
	uint16 result;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_CZ_SEARCH_STORE_INFO_item {
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_CZ_SEARCH_STORE_INFO {
	int16 packetType;
	int16 packetLength;
	uint8 searchType;
	uint32 maxPrice;
	uint32 minPrice;
	uint8 itemsCount;
	uint8 cardsCount;
	struct PACKET_CZ_SEARCH_STORE_INFO_item items[];  // items[itemCount]
/*
	struct PACKET_CZ_SEARCH_STORE_INFO_item cards[cardCount];
*/
} __attribute__((packed));

struct PACKET_CZ_REQMAKINGITEM {
	int16 packetType;
#if PACKETVER >= 20181121
	uint32 itemId;
	uint32 material[3];
#else
	uint16 itemId;
	uint16 material[3];
#endif
} __attribute__((packed));

struct PACKET_CZ_REQ_MAKINGITEM {
	int16 packetType;
	int16 type;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_CZ_REQ_ITEMREPAIR {
	int16 packetType;
	int16 index;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint8 refine;
	struct EQUIPSLOTINFO slot;
} __attribute__((packed));

struct PACKET_CZ_REQ_MAKINGARROW {
	int16 packetType;
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_MAKINGARROW_LIST_sub {
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_MAKINGARROW_LIST {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_MAKINGARROW_LIST_sub items[];
} __attribute__((packed));

struct PACKET_ZC_MAKABLEITEMLIST_sub {
#if PACKETVER >= 20181121
	uint32 itemId;
	uint32 material[3];
#else
	uint16 itemId;
	uint16 material[3];
#endif
} __attribute__((packed));

struct PACKET_ZC_MAKABLEITEMLIST {
	int16 packetType;
	int16 packetLength;
	struct PACKET_ZC_MAKABLEITEMLIST_sub items[];
} __attribute__((packed));

struct PACKET_ZC_MAKINGITEM_LIST_sub {
#if PACKETVER >= 20181121
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_MAKINGITEM_LIST {
	int16 packetType;
	int16 packetLength;
	uint16 makeItem;
	struct PACKET_ZC_MAKINGITEM_LIST_sub items[];
} __attribute__((packed));

#if !defined(sun) && (!defined(__NETBSD__) || __NetBSD_Version__ >= 600000000) // NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#pragma pack(pop)
#endif // not NetBSD < 6 / Solaris

#endif /* MAP_PACKETS_STRUCT_H */
