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
#if PACKETVER >= 20180418
	dropflooritemType = 0xadd,
#elif PACKETVER > 20130000 /* not sure date */
	dropflooritemType = 0x84b,
#else
	dropflooritemType = 0x9e,
#endif
#if PACKETVER >= 4
	sendLookType = 0x1d7,
#else
	sendLookType = 0xc3,
#endif
};

#if !defined(sun) && (!defined(__NETBSD__) || __NetBSD_Version__ >= 600000000) // NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#pragma pack(push, 1)
#endif // not NetBSD < 6 / Solaris

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

#if !defined(sun) && (!defined(__NETBSD__) || __NetBSD_Version__ >= 600000000) // NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#pragma pack(pop)
#endif // not NetBSD < 6 / Solaris

#endif /* MAP_PACKETS_STRUCT_H */
