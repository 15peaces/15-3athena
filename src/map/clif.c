// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/grfio.h"
#include "../common/malloc.h"

#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/ers.h"

#include "map.h"
#include "chrif.h"
#include "pc.h"
#include "status.h"
#include "npc.h"
#include "itemdb.h"
#include "chat.h"
#include "trade.h"
#include "storage.h"
#include "script.h"
#include "skill.h"
#include "atcommand.h"
#include "intif.h"
#include "battle.h"
#include "battleground.h"
#include "mob.h"
#include "party.h"
#include "unit.h"
#include "guild.h"
#include "vending.h"
#include "pet.h"
#include "homunculus.h"
#include "instance.h"
#include "mercenary.h"
#include "elemental.h"
#include "log.h"
#include "clif.h"
#include "mail.h"
#include "quest.h"
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

// for clif_clearunit_delayed
static struct eri *delay_clearunit_ers;

//#define DUMP_UNKNOWN_PACKET
//#define DUMP_INVALID_PACKET

struct Clif_Config {
	int packet_db_ver;	//Preferred packet version.
	int connect_cmd[MAX_PACKET_VER + 1]; //Store the connect command for all versions. [Skotlex]
} clif_config;

struct s_packet_db packet_db[MAX_PACKET_VER + 1][MAX_PACKET_DB + 1];
int packet_db_ack[MAX_PACKET_VER + 1][MAX_ACK_FUNC + 1];

#ifdef PACKET_OBFUSCATION
static struct s_packet_keys *packet_keys[MAX_PACKET_VER + 1];
static unsigned int clif_cryptKey[3]; // Used keys
#endif
static unsigned short clif_parse_cmd(int fd, struct map_session_data *sd);

#if PACKETVER >= 20150513
enum mail_type {
	MAIL_TYPE_TEXT = 0x0,
	MAIL_TYPE_ZENY = 0x2,
	MAIL_TYPE_ITEM = 0x4,
	MAIL_TYPE_NPC = 0x8
};
#endif

enum e_inventory_type {
	INVTYPE_INVENTORY = 0,
	INVTYPE_CART = 1,
	INVTYPE_STORAGE = 2,
	INVTYPE_GUILD_STORAGE = 3,
};

const int16 MAX_INVENTORY_ITEM_PACKET_NORMAL = ((INT16_MAX - (sizeof(struct packet_itemlist_normal) - (sizeof(struct NORMALITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct NORMALITEM_INFO));
const int16 MAX_INVENTORY_ITEM_PACKET_EQUIP = ((INT16_MAX - (sizeof(struct packet_itemlist_equip) - (sizeof(struct EQUIPITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct EQUIPITEM_INFO));

const int16 MAX_STORAGE_ITEM_PACKET_NORMAL = ((INT16_MAX - (sizeof(struct ZC_STORE_ITEMLIST_NORMAL) - (sizeof(struct NORMALITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct NORMALITEM_INFO));
const int16 MAX_STORAGE_ITEM_PACKET_EQUIP = ((INT16_MAX - (sizeof(struct ZC_STORE_ITEMLIST_EQUIP) - (sizeof(struct EQUIPITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct EQUIPITEM_INFO));

/** Converts item type to display it on client if necessary.
* @param nameid: Item ID
* @return item type. For IT_PETEGG will be displayed as IT_ARMOR. If Shadow Weapon of IT_SHADOWGEAR as IT_WEAPON and else as IT_ARMOR
*/
static inline int itemtype(t_itemid nameid) {
	struct item_data* id = itemdb_search(nameid); //Use itemdb_search, so non-existance item will use dummy data and won't crash the server. bugreport:8468
	int type = id->type;
	if( type == IT_SHADOWGEAR ) {
		if( id->equip&EQP_SHADOW_WEAPON )
			return IT_WEAPON;
		else
			return IT_ARMOR;
	}
	return ( type == IT_PETEGG ) ? IT_ARMOR : type;
}

// TODO: doc
static inline uint16 client_index(uint16 server_index) {
	return server_index + 2;
}

static inline uint16 server_index(uint16 client_index) {
	return client_index - 2;
}

static inline uint16 client_storage_index(uint16 server_index) {
	return server_index + 1;
}

static inline uint16 server_storage_index(uint16 client_index) {
	return client_index - 1;
}

#if PACKETVER >= 20181121
static inline t_itemid client_nameid(t_itemid server_nameid) {
	t_itemid view = itemdb_viewid(server_nameid);

	if (view > 0) {
		return view;
	}
	else {
		return server_nameid;
	}
}
#else
static inline t_itemid client_nameid(t_itemid server_nameid) {
	t_itemid view = itemdb_viewid(server_nameid);

	if (view > 0) {
		if (view > UINT16_MAX) {
			return (uint16)UNKNOWN_ITEM_ID;
		}
	}
	else {
		if (server_nameid > UINT16_MAX) {
			return (uint16)UNKNOWN_ITEM_ID;
		}
	}
}
#endif

static inline void WBUFPOS(uint8* p, unsigned short pos, short x, short y, unsigned char dir)
{
	p += pos;
	p[0] = (uint8)(x>>2);
	p[1] = (uint8)((x<<6) | ((y>>4)&0x3f));
	p[2] = (uint8)((y<<4) | (dir&0xf));
}


// client-side: x0+=sx0*0.0625-0.5 and y0+=sy0*0.0625-0.5
static inline void WBUFPOS2(uint8* p, unsigned short pos, short x0, short y0, short x1, short y1, unsigned char sx0, unsigned char sy0)
{
	p += pos;
	p[0] = (uint8)(x0>>2);
	p[1] = (uint8)((x0<<6) | ((y0>>4)&0x3f));
	p[2] = (uint8)((y0<<4) | ((x1>>6)&0x0f));
	p[3] = (uint8)((x1<<2) | ((y1>>8)&0x03));
	p[4] = (uint8)y1;
	p[5] = (uint8)((sx0<<4) | (sy0&0x0f));
}


static inline void WFIFOPOS(int fd, unsigned short pos, short x, short y, unsigned char dir)
{
	WBUFPOS(WFIFOP(fd,pos), 0, x, y, dir);
}


static inline void WFIFOPOS2(int fd, unsigned short pos, short x0, short y0, short x1, short y1, unsigned char sx0, unsigned char sy0)
{
	WBUFPOS2(WFIFOP(fd,pos), 0, x0, y0, x1, y1, sx0, sy0);
}


static inline void RBUFPOS(const uint8* p, unsigned short pos, short* x, short* y, unsigned char* dir)
{
	p += pos;

	if( x )
	{
		x[0] = ( ( p[0] & 0xff ) << 2 ) | ( p[1] >> 6 );
	}

	if( y )
	{
		y[0] = ( ( p[1] & 0x3f ) << 4 ) | ( p[2] >> 4 );
	}

	if( dir )
	{
		dir[0] = ( p[2] & 0x0f );
	}
}


static inline void RBUFPOS2(const uint8* p, unsigned short pos, short* x0, short* y0, short* x1, short* y1, unsigned char* sx0, unsigned char* sy0)
{
	p += pos;

	if( x0 )
	{
		x0[0] = ( ( p[0] & 0xff ) << 2 ) | ( p[1] >> 6 );
	}

	if( y0 )
	{
		y0[0] = ( ( p[1] & 0x3f ) << 4 ) | ( p[2] >> 4 );
	}

	if( x1 )
	{
		x1[0] = ( ( p[2] & 0x0f ) << 6 ) | ( p[3] >> 2 );
	}

	if( y1 )
	{
		y1[0] = ( ( p[3] & 0x03 ) << 8 ) | ( p[4] >> 0 );
	}

	if( sx0 )
	{
		sx0[0] = ( p[5] & 0xf0 ) >> 4;
	}

	if( sy0 )
	{
		sy0[0] = ( p[5] & 0x0f ) >> 0;
	}
}


static inline void RFIFOPOS(int fd, unsigned short pos, short* x, short* y, unsigned char* dir)
{
	RBUFPOS(RFIFOP(fd,pos), 0, x, y, dir);
}


static inline void RFIFOPOS2(int fd, unsigned short pos, short* x0, short* y0, short* x1, short* y1, unsigned char* sx0, unsigned char* sy0)
{
	RBUFPOS2(WFIFOP(fd,pos), 0, x0, y0, x1, y1, sx0, sy0);
}


//To idenfity disguised characters.
static inline bool disguised(struct block_list* bl)
{
	return (bool)( bl->type == BL_PC && ((TBL_PC*)bl)->disguise );
}


//Guarantees that the given string does not exceeds the allowed size, as well as making sure it's null terminated. [Skotlex]
static inline unsigned int mes_len_check(char* mes, unsigned int len, unsigned int max)
{
	if( len > max )
		len = max;

	mes[len-1] = '\0';

	return len;
}


static char map_ip_str[128];
static uint32 map_ip;
static uint32 bind_ip = INADDR_ANY;
static uint16 map_port = 5121;
static bool clif_ally_only = false;
int map_fd;

static int clif_parse (int fd);

/*==========================================
 * map鯖のip設定
 *------------------------------------------*/
int clif_setip(const char* ip)
{
	char ip_str[16];
	map_ip = host2ip(ip);
	if (!map_ip) {
		ShowWarning("Failed to Resolve Map Server Address! (%s)\n", ip);
		return 0;
	}

	safestrncpy(map_ip_str, ip, sizeof(map_ip_str));
	ShowInfo("Map Server IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, ip2str(map_ip, ip_str));
	return 1;
}

void clif_setbindip(const char* ip)
{
	bind_ip = host2ip(ip);
	if (bind_ip) {
		char ip_str[16];

		ShowInfo("Map Server Bind IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, ip2str(bind_ip, ip_str));
	} else {
		ShowWarning("Failed to Resolve Map Server Address! (%s)\n", ip);
	}
}

/*==========================================
 * map鯖のport設定
 *------------------------------------------*/
void clif_setport(uint16 port)
{
	map_port = port;
}

/*==========================================
 * map鯖のip読み出し
 *------------------------------------------*/
uint32 clif_getip(void)
{
	return map_ip;
}

//Refreshes map_server ip, returns the new ip if the ip changed, otherwise it returns 0.
uint32 clif_refresh_ip(void)
{
	uint32 new_ip;

	new_ip = host2ip(map_ip_str);
	if (new_ip && new_ip != map_ip) {
		map_ip = new_ip;
		ShowInfo("Updating IP resolution of [%s].\n", map_ip_str);
		return map_ip;
	}
	return 0;
}

/*==========================================
 * map鯖のport読み出し
 *------------------------------------------*/
uint16 clif_getport(void) {
	return map_port;
}

#if PACKETVER >= 20071106
static inline unsigned char clif_bl_type(struct block_list *bl) {
	switch (bl->type) {
	case BL_PC:    return (disguised(bl) && !pcdb_checkid(status_get_viewdata(bl)->class_)) ? 0x1:0x0; //PC_TYPE
	case BL_ITEM:  return 0x2; //ITEM_TYPE
	case BL_SKILL: return 0x3; //SKILL_TYPE
	case BL_CHAT:  return 0x4; //UNKNOWN_TYPE
	case BL_MOB:   return pcdb_checkid(status_get_viewdata(bl)->class_)?0x0:0x5; //NPC_MOB_TYPE
	case BL_NPC:   return pcdb_checkid(status_get_viewdata(bl)->class_)?0x0:0x6; //NPC_EVT_TYPE
	case BL_PET:   return pcdb_checkid(status_get_viewdata(bl)->class_)?0x0:0x7; //NPC_PET_TYPE
	case BL_HOM:   return 0x8; //NPC_HOM_TYPE
	case BL_MER:   return 0x9; //NPC_MERSOL_TYPE
	case BL_ELEM:  return 0xa; //NPC_ELEMENTAL_TYPE
	default:       return 0x1; //NPC_TYPE
	}
}
#endif

static bool clif_session_isValid(struct map_session_data *sd) {
	return (sd != NULL && session_isActive(sd->fd));
}

/*==========================================
 * clif_sendでAREA*指定時用
 *------------------------------------------*/
static int clif_send_sub(struct block_list *bl, va_list ap)
{
	struct block_list *src_bl;
	struct map_session_data *sd;
	unsigned char *buf;
	int len, type, fd;

	nullpo_ret(bl);
	nullpo_ret(sd = (struct map_session_data *)bl);

	// Don't send to disconnected clients.
	if (!session_isActive(fd = sd->fd)) {
		return 0;
	}

	buf = va_arg(ap,unsigned char*);
	len = va_arg(ap,int);
	nullpo_ret(src_bl = va_arg(ap,struct block_list*));
	type = va_arg(ap,int);

	switch(type)
	{
	case AREA_WOS:
		if (bl == src_bl)
			return 0;
	break;
	case AREA_WOC:
		if (sd->chatID || bl == src_bl)
			return 0;
	break;
	case AREA_WOSC:
	{
		if (src_bl->type == BL_PC) {
			struct map_session_data *ssd = (struct map_session_data *)src_bl;
			if (ssd && sd->chatID && (sd->chatID == ssd->chatID))
				return 0;
		}
		else if (src_bl->type == BL_NPC) {
			struct npc_data *nd = (struct npc_data *)src_bl;
			if (nd && sd->chatID && (sd->chatID == nd->chat_id))
				return 0;
		}
	}
	break;
	}

	/* unless visible, hold it here */
	if (clif_ally_only && !sd->special_state.intravision && battle_check_target(src_bl, &sd->bl, BCT_ENEMY) > 0)
		return 0;

	WFIFOHEAD(fd, len);
	if (WFIFOP(fd,0) == buf) {
		ShowError("WARNING: Invalid use of clif_send function\n");
		ShowError("         Packet x%4x use a WFIFO of a player instead of to use a buffer.\n", WBUFW(buf,0));
		ShowError("         Please correct your code.\n");
		// don't send to not move the pointer of the packet for next sessions in the loop
		//WFIFOSET(fd,0);//## TODO is this ok?
		//NO. It is not ok. There is the chance WFIFOSET actually sends the buffer data, and shifts elements around, which will corrupt the buffer.
		return 0;
	}

	if (packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
		memcpy(WFIFOP(fd,0), buf, len);
		WFIFOSET(fd,len);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int clif_send(const void* buf, int len, struct block_list* bl, enum send_target type)
{
	int i;
	struct map_session_data *sd, *tsd;
	struct party_data *p = NULL;
	struct guild *g = NULL;
	struct battleground_data *bg = NULL;
	int x0 = 0, x1 = 0, y0 = 0, y1 = 0, fd;
	struct s_mapiterator* iter;

	if( type != ALL_CLIENT && type != CHAT_MAINCHAT )
		nullpo_ret(bl);

	sd = BL_CAST(BL_PC, bl);

	switch(type) {

	case ALL_CLIENT: //All player clients.
		iter = mapit_getallusers();
		while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
		{
			if (session_isActive(fd = tsd->fd) && packet_db[tsd->packet_ver][RBUFW(buf, 0)].len)
			{ // packet must exist for the client version
				WFIFOHEAD(tsd->fd, len);
				memcpy(WFIFOP(tsd->fd, 0), buf, len);
				WFIFOSET(tsd->fd, len);
			}
		}
		mapit_free(iter);
		break;

	case ALL_SAMEMAP: //All players on the same map
		iter = mapit_getallusers();
		while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
		{
			if( bl->m == tsd->bl.m && session_isActive(fd = tsd->fd) && packet_db[tsd->packet_ver][RBUFW(buf,0)].len )
			{ // packet must exist for the client version
				WFIFOHEAD(tsd->fd, len);
				memcpy(WFIFOP(tsd->fd,0), buf, len);
				WFIFOSET(tsd->fd,len);
			}
		}
		mapit_free(iter);
		break;

	case AREA:
	case AREA_WOSC:
		if (sd && bl->prev == NULL) //Otherwise source misses the packet.[Skotlex]
			clif_send (buf, len, bl, SELF);
	case AREA_WOC:
	case AREA_WOS:
		map_foreachinarea(clif_send_sub, bl->m, bl->x-AREA_SIZE, bl->y-AREA_SIZE, bl->x+AREA_SIZE, bl->y+AREA_SIZE,
			BL_PC, buf, len, bl, type);
		break;
	case AREA_CHAT_WOC:
		map_foreachinarea(clif_send_sub, bl->m, bl->x-(AREA_SIZE-5), bl->y-(AREA_SIZE-5),
			bl->x+(AREA_SIZE-5), bl->y+(AREA_SIZE-5), BL_PC, buf, len, bl, AREA_WOC);
		break;

	case CHAT:
	case CHAT_WOS:
		{
			struct chat_data *cd;
			if (sd) {
				cd = (struct chat_data*)map_id2bl(sd->chatID);
			} else if (bl->type == BL_CHAT) {
				cd = (struct chat_data*)bl;
			} else break;
			if (cd == NULL)
				break;
			for(i = 0; i < cd->users; i++) {
				if (type == CHAT_WOS && cd->usersd[i] == sd)
					continue;
				if (session_isActive(fd = cd->usersd[i]->fd) && packet_db[cd->usersd[i]->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
					WFIFOHEAD(fd,len);
					memcpy(WFIFOP(fd,0), buf, len);
					WFIFOSET(fd,len);
				}
			}
		}
		break;

	case CHAT_MAINCHAT: //[LuzZza]
		iter = mapit_getallusers();
		while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
		{
			if( tsd->state.mainchat && tsd->chatID == 0 && packet_db[tsd->packet_ver][RBUFW(buf,0)].len )
			{ // packet must exist for the client version
				WFIFOHEAD(tsd->fd, len);
				memcpy(WFIFOP(tsd->fd,0), buf, len);
				WFIFOSET(tsd->fd,len);
			}
		}
		mapit_free(iter);
		break;

	case PARTY_AREA:
	case PARTY_AREA_WOS:
		x0 = bl->x - AREA_SIZE;
		y0 = bl->y - AREA_SIZE;
		x1 = bl->x + AREA_SIZE;
		y1 = bl->y + AREA_SIZE;
	case PARTY:
	case PARTY_WOS:
	case PARTY_SAMEMAP:
	case PARTY_SAMEMAP_WOS:
		if (sd && sd->status.party_id)
			p = party_search(sd->status.party_id);
			
		if (p) {
			for(i=0;i<MAX_PARTY;i++){
				if( (sd = p->data[i].sd) == NULL )
					continue;

				if (!session_isActive(fd = sd->fd))
					continue;

				if( sd->bl.id == bl->id && (type == PARTY_WOS || type == PARTY_SAMEMAP_WOS || type == PARTY_AREA_WOS) )
					continue;
				
				if( type != PARTY && type != PARTY_WOS && bl->m != sd->bl.m )
					continue;
				
				if( (type == PARTY_AREA || type == PARTY_AREA_WOS) && (sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1) )
					continue;
				
				if( packet_db[sd->packet_ver][RBUFW(buf,0)].len )
				{ // packet must exist for the client version
					WFIFOHEAD(fd,len);
					memcpy(WFIFOP(fd,0), buf, len);
					WFIFOSET(fd,len);
				}
			}
			if (!enable_spy) //Skip unnecessary parsing. [Skotlex]
				break;

			iter = mapit_getallusers();
			while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
			{
				if( tsd->partyspy == p->party.party_id && session_isActive(fd = tsd->fd) && packet_db[tsd->packet_ver][RBUFW(buf,0)].len )
				{ // packet must exist for the client version
					WFIFOHEAD(tsd->fd, len);
					memcpy(WFIFOP(tsd->fd,0), buf, len);
					WFIFOSET(tsd->fd,len);
				}
			}
			mapit_free(iter);
		}
		break;

	case DUEL:
	case DUEL_WOS:
		if (!sd || !sd->duel_group) break; //Invalid usage.

		iter = mapit_getallusers();
		while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
		{
			if( type == DUEL_WOS && bl->id == tsd->bl.id )
				continue;
			if( sd->duel_group == tsd->duel_group && session_isActive(fd = tsd->fd) && packet_db[tsd->packet_ver][RBUFW(buf,0)].len )
			{ // packet must exist for the client version
				WFIFOHEAD(tsd->fd, len);
				memcpy(WFIFOP(tsd->fd,0), buf, len);
				WFIFOSET(tsd->fd,len);
			}
		}
		mapit_free(iter);
		break;

	case SELF:
		if (sd && session_isActive(fd = sd->fd) && packet_db[sd->packet_ver][RBUFW(buf,0)].len) { // packet must exist for the client version
			WFIFOHEAD(fd,len);
			memcpy(WFIFOP(fd,0), buf, len);
			WFIFOSET(fd,len);
		}
		break;

	// New definitions for guilds [Valaris] - Cleaned up and reorganized by [Skotlex]
	case GUILD_AREA:
	case GUILD_AREA_WOS:
		x0 = bl->x - AREA_SIZE;
		y0 = bl->y - AREA_SIZE;
		x1 = bl->x + AREA_SIZE;
		y1 = bl->y + AREA_SIZE;
	case GUILD_SAMEMAP:
	case GUILD_SAMEMAP_WOS:
	case GUILD:
	case GUILD_WOS:
	case GUILD_NOBG:
		if (sd && sd->status.guild_id)
			g = sd->guild;

		if (g) {
			for(i = 0; i < g->max_member; i++) {
				if( (sd = g->member[i].sd) != NULL )
				{
					if (!session_isActive(fd = sd->fd))
						continue;
					
					if( type == GUILD_NOBG && sd->bg_id )
						continue;

					if( sd->bl.id == bl->id && (type == GUILD_WOS || type == GUILD_SAMEMAP_WOS || type == GUILD_AREA_WOS) )
						continue;
					
					if( type != GUILD && type != GUILD_NOBG && type != GUILD_WOS && sd->bl.m != bl->m )
						continue;

					if( (type == GUILD_AREA || type == GUILD_AREA_WOS) && (sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1) )
						continue;

					if( packet_db[sd->packet_ver][RBUFW(buf,0)].len )
					{ // packet must exist for the client version
						WFIFOHEAD(fd,len);
						memcpy(WFIFOP(fd,0), buf, len);
						WFIFOSET(fd,len);
					}
				}
			}
			if (!enable_spy) //Skip unnecessary parsing. [Skotlex]
				break;

			iter = mapit_getallusers();
			while( (tsd = (TBL_PC*)mapit_next(iter)) != NULL )
			{
				if( tsd->guildspy == g->guild_id && session_isActive(fd = tsd->fd) && packet_db[tsd->packet_ver][RBUFW(buf,0)].len )
				{ // packet must exist for the client version
					WFIFOHEAD(tsd->fd, len);
					memcpy(WFIFOP(tsd->fd,0), buf, len);
					WFIFOSET(tsd->fd,len);
				}
			}
			mapit_free(iter);
		}
		break;

	case BG_AREA:
	case BG_AREA_WOS:
		x0 = bl->x - AREA_SIZE;
		y0 = bl->y - AREA_SIZE;
		x1 = bl->x + AREA_SIZE;
		y1 = bl->y + AREA_SIZE;
	case BG_SAMEMAP:
	case BG_SAMEMAP_WOS:
	case BG:
	case BG_WOS:
		if( sd && sd->bg_id && (bg = bg_team_search(sd->bg_id)) != NULL )
		{
			for( i = 0; i < MAX_BG_MEMBERS; i++ )
			{
				if( (sd = bg->members[i].sd) == NULL || !session_isActive(fd = sd->fd) )
					continue;
				if( sd->bl.id == bl->id && (type == BG_WOS || type == BG_SAMEMAP_WOS || type == BG_AREA_WOS) )
					continue;
				if( type != BG && type != BG_WOS && sd->bl.m != bl->m )
					continue;
				if( (type == BG_AREA || type == BG_AREA_WOS) && (sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1) )
					continue;
				if( packet_db[sd->packet_ver][RBUFW(buf,0)].len )
				{ // packet must exist for the client version
					WFIFOHEAD(fd,len);
					memcpy(WFIFOP(fd,0), buf, len);
					WFIFOSET(fd,len);
				}
			}
		}
		break;

		case BG_QUEUE:
			if( sd && sd->bg_queue.arena ) {
				struct script_queue *queue = script_queue_get(sd->bg_queue.arena->queue_id);

				for (i = 0; i < VECTOR_LENGTH(queue->entries); i++) {
					struct map_session_data *qsd = map_id2sd(VECTOR_INDEX(queue->entries, i));

					if (qsd != NULL) {
						WFIFOHEAD(qsd->fd,len);
						memcpy(WFIFOP(qsd->fd,0), buf, len);
						WFIFOSET(qsd->fd,len);
					}
				}
			}
			break;

	case CLAN:
		if (sd && sd->clan)
		{
			struct clan* clan = sd->clan;

			for (i = 0; i < clan->max_member; i++)
			{
				if ((sd = clan->members[i]) == NULL || !session_isActive(fd = sd->fd) )
					continue;

				if (packet_db[sd->packet_ver][RBUFW(buf,0)].len)
				{ // packet must exist for the client version
					WFIFOHEAD(fd, len);
					memcpy(WFIFOP(fd, 0), buf, len);
					WFIFOSET(fd, len);
				}
			}
		}
		break;

	default:
		ShowError("clif_send: Unrecognized type %d\n", type);
		return -1;
	}

	return 0;
}


/// Notifies the client, that it's connection attempt was accepted.
/// 0073 <start time>.L <position>.3B <x size>.B <y size>.B (ZC_ACCEPT_ENTER)
/// 02eb <start time>.L <position>.3B <x size>.B <y size>.B <font>.W (ZC_ACCEPT_ENTER2)
/// 0a18 <start time>.L <position>.3B <x size>.B <y size>.B <font>.W <sex>.B (ZC_ACCEPT_ENTER3)
void clif_authok(struct map_session_data *sd)
{
#if PACKETVER < 20080102
	const int cmd = 0x73;
#elif PACKETVER < 20141022 || PACKETVER >= 20160330
	const int cmd = 0x2eb;
#else
	const int cmd = 0xa18;
#endif
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd, 0) = cmd;
	WFIFOL(fd, 2) = (unsigned int)gettick();
	WFIFOPOS(fd, 6, sd->bl.x, sd->bl.y, sd->ud.dir);
	WFIFOB(fd, 9) = 5; // ignored
	WFIFOB(fd,10) = 5; // ignored
#if PACKETVER >= 20080102
	WFIFOW(fd,11) = sd->status.font;
#endif
#if PACKETVER >= 20141016 && PACKETVER < 20160330
	WFIFOB(fd,13) = sd->status.sex;
#endif
	WFIFOSET(fd,packet_len(cmd));
}

/// [4144] Packet not using error_code anymore. Works for fixed error only (MsgString: 9 - Rejected from Server)
/// Notifies the client, that it's connection attempt was refused (ZC_REFUSE_ENTER).
/// 0074 <error code>.B
/// error code:
///     0 = client type mismatch
///     1 = ID mismatch
///     2 = mobile - out of available time
///     3 = mobile - already logged in
///     4 = mobile - waiting state
void clif_authrefuse(int fd, uint8 error_code)
{
	WFIFOHEAD(fd,packet_len(0x74));
	WFIFOW(fd,0) = 0x74;
	WFIFOB(fd,2) = error_code;
	WFIFOSET(fd,packet_len(0x74));
}


/// Notifies the client of a ban or forced disconnect (SC_NOTIFY_BAN).
/// 0081 <error code>.B
/// error code:
///     0 = BAN_UNFAIR
///     1 = server closed -> MsgStringTable[4]
///     2 = ID already logged in -> MsgStringTable[5]
///     3 = timeout/too much lag -> MsgStringTable[241]
///     4 = server full -> MsgStringTable[264]
///     5 = underaged -> MsgStringTable[305]
///     8 = Server sill recognizes last connection -> MsgStringTable[441]
///     9 = too many connections from this ip -> MsgStringTable[529]
///     10 = out of available time paid for -> MsgStringTable[530]
///     11 = BAN_PAY_SUSPEND
///     12 = BAN_PAY_CHANGE
///     13 = BAN_PAY_WRONGIP
///     14 = BAN_PAY_PNGAMEROOM
///     15 = disconnected by a GM -> if( servicetype == taiwan ) MsgStringTable[579]
///     16 = BAN_JAPAN_REFUSE1
///     17 = BAN_JAPAN_REFUSE2
///     18 = BAN_INFORMATION_REMAINED_ANOTHER_ACCOUNT
///     100 = BAN_PC_IP_UNFAIR
///     101 = BAN_PC_IP_COUNT_ALL
///     102 = BAN_PC_IP_COUNT
///     103 = BAN_GRAVITY_MEM_AGREE
///     104 = BAN_GAME_MEM_AGREE
///     105 = BAN_HAN_VALID
///     106 = BAN_PC_IP_LIMIT_ACCESS
///     107 = BAN_OVER_CHARACTER_LIST
///     108 = BAN_IP_BLOCK
///     109 = BAN_INVALID_PWD_CNT
///     110 = BAN_NOT_ALLOWED_JOBCLASS
///     113 = access is restricted between the hours of midnight to 6:00am.
///     115 = You are in game connection ban period.
///     ? = disconnected -> MsgStringTable[3]
void clif_authfail_fd(int fd, int type)
{
	if (!fd || !session[fd] || session[fd]->func_parse != clif_parse) //clif_authfail should only be invoked on players!
		return;

	WFIFOHEAD(fd, packet_len(0x81));
	WFIFOW(fd,0) = 0x81;
	WFIFOB(fd,2) = type;
	WFIFOSET(fd,packet_len(0x81));
	set_eof(fd);
}


/// Notifies the client, whether it can disconnect and change servers (ZC_RESTART_ACK).
/// 00b3 <type>.B
/// type:
///     1 = disconnect, char-select
///     ? = nothing
void clif_charselectok(int id, uint8 ok)
{
	struct map_session_data* sd;
	int fd;

	if ((sd = map_id2sd(id)) == NULL || !sd->fd)
		return;

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xb3));
	WFIFOW(fd,0) = 0xb3;
	WFIFOB(fd,2) = ok;
	WFIFOSET(fd,packet_len(0xb3));
}

/// Makes an item appear on the ground.
/// 009E <id>.L <name id>.W <identified>.B <x>.W <y>.W <subX>.B <subY>.B <amount>.W (ZC_ITEM_FALL_ENTRY)
/// 084B <id>.L <name id>.W <type>.W <identified>.B <x>.W <y>.W <subX>.B <subY>.B <amount>.W (ZC_ITEM_FALL_ENTRY4)
/// 0ADD <id>.L <name id>.W <type>.W <identified>.B <x>.W <y>.W <subX>.B <subY>.B <amount>.W <show drop effect>.B <drop effect mode>.W (ZC_ITEM_FALL_ENTRY5)
void clif_dropflooritem(struct flooritem_data* fitem, bool canShowEffect) {
	nullpo_retv(fitem);

	if (fitem->item_data.nameid == 0) {
		return;
	}

	struct packet_dropflooritem p;

	p.PacketType = dropflooritemType;
	p.ITAID = fitem->bl.id;
	p.ITID = client_nameid(fitem->item_data.nameid);
#if PACKETVER >= 20130000 /* not sure date */
	p.type = itemtype(fitem->item_data.nameid);
#endif
	p.IsIdentified = fitem->item_data.identify ? 1 : 0;
	p.xPos = fitem->bl.x;
	p.yPos = fitem->bl.y;
	p.subX = fitem->subx;
	p.subY = fitem->suby;
	p.count = fitem->item_data.amount;
#if PACKETVER >= 20180418
	if (canShowEffect) {
		uint8 dropEffect = itemdb_dropeffect(fitem->item_data.nameid);

		if (dropEffect > 0) {
			p.showdropeffect = 1;
			p.dropeffectmode = dropEffect - 1;
		}
		else {
			p.showdropeffect = 0;
			p.dropeffectmode = 0;
		}
	}
	else {
		p.showdropeffect = 0;
		p.dropeffectmode = 0;
	}
#endif
	clif_send(&p, sizeof(p), &fitem->bl, AREA);
}



/// Makes an item disappear from the ground.
/// 00a1 <id>.L (ZC_ITEM_DISAPPEAR)
void clif_clearflooritem(struct flooritem_data *fitem, int fd)
{
	unsigned char buf[16];

	nullpo_retv(fitem);

	WBUFW(buf,0) = 0xa1;
	WBUFL(buf,2) = fitem->bl.id;

	if (fd == 0) {
		clif_send(buf, packet_len(0xa1), &fitem->bl, AREA);
	} else {
		WFIFOHEAD(fd,packet_len(0xa1));
		memcpy(WFIFOP(fd,0), buf, packet_len(0xa1));
		WFIFOSET(fd,packet_len(0xa1));
	}
}


/// Makes a unit (char, npc, mob, homun) disappear to one client (ZC_NOTIFY_VANISH).
/// 0080 <id>.L <type>.B
/// type:
///     0 = out of sight
///     1 = died
///     2 = logged out
///     3 = teleport
///     4 = trickdead
void clif_clearunit_single(int id, clr_type type, int fd)
{
	WFIFOHEAD(fd, packet_len(0x80));
	WFIFOW(fd,0) = 0x80;
	WFIFOL(fd,2) = id;
	WFIFOB(fd,6) = type;
	WFIFOSET(fd, packet_len(0x80));
}

/// Makes a unit (char, npc, mob, homun) disappear to all clients in area (ZC_NOTIFY_VANISH).
/// 0080 <id>.L <type>.B
/// type:
///     0 = out of sight
///     1 = died
///     2 = logged out
///     3 = teleport
///     4 = trickdead
void clif_clearunit_area(struct block_list* bl, clr_type type)
{
	unsigned char buf[8];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x80;
	WBUFL(buf,2) = bl->id;
	WBUFB(buf,6) = type;

	clif_send(buf, packet_len(0x80), bl, type == CLR_DEAD ? AREA : AREA_WOS);

	if(disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x80), bl, SELF);
	}
}


/// Used to make monsters with player-sprites disappear after dying
/// like normal monsters, because the client does not remove those
/// automatically.
static int clif_clearunit_delayed_sub(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl = (struct block_list *)data;
	clif_clearunit_area(bl, (clr_type)id);
	ers_free(delay_clearunit_ers, bl);
	return 0;
}
void clif_clearunit_delayed(struct block_list* bl, clr_type type, int64 tick)
{
	struct block_list *tbl = ers_alloc(delay_clearunit_ers, struct block_list);
	memcpy (tbl, bl, sizeof (struct block_list));
	add_timer(tick, clif_clearunit_delayed_sub, (int)type, (intptr_t)tbl);
}

void clif_get_weapon_view(struct map_session_data* sd, t_itemid *rhand, t_itemid *lhand)
{
	if (sd->sc.option&OPTION_COSTUME)
	{
		*rhand = *lhand = 0;
		return;
	}

#if PACKETVER < 4
	*rhand = sd->status.weapon;
	*lhand = sd->status.shield;
#else
	if (sd->equip_index[EQI_HAND_R] >= 0 &&
		sd->inventory_data[sd->equip_index[EQI_HAND_R]]) 
	{
		struct item_data* id = sd->inventory_data[sd->equip_index[EQI_HAND_R]];
		if (id->view_id > 0)
			*rhand = id->view_id;
		else
			*rhand = id->nameid;
	} else
		*rhand = 0;

	if (sd->equip_index[EQI_HAND_L] >= 0 &&
		sd->equip_index[EQI_HAND_L] != sd->equip_index[EQI_HAND_R] &&
		sd->inventory_data[sd->equip_index[EQI_HAND_L]]) 
	{
		struct item_data* id = sd->inventory_data[sd->equip_index[EQI_HAND_L]];
		if (id->view_id > 0)
			*lhand = id->view_id;
		else
			*lhand = id->nameid;
	} else
		*lhand = 0;
#endif
}

//To make the assignation of the level based on limits clearer/easier. [Skotlex]
static int clif_setlevel_sub(int lv)
{
	if( lv < battle_config.max_lv )
	{
		;
	}
	else if( lv < battle_config.aura_lv )
	{
		lv = battle_config.max_lv - 1;
	}
	else
	{
		lv = battle_config.max_lv;
	}

	return lv;
}

static int clif_setlevel(struct block_list* bl)
{
	int lv = status_get_lv(bl);
	if( battle_config.client_limit_unit_lv&bl->type )
		return clif_setlevel_sub(lv);
	switch( bl->type )
	{
		case BL_NPC:
		case BL_PET:
			// npcs and pets do not have level
			return 0;
	}
	return lv;
}

/*==========================================
 * Prepares 'unit standing/spawning' packet
 *------------------------------------------*/
static void clif_set_unit_idle(struct block_list* bl, bool walking, send_target target, struct block_list* tbl) {
	nullpo_retv(bl);

	struct map_session_data* sd = BL_CAST(BL_PC, bl);
	struct status_change* sc = status_get_sc(bl);
	struct view_data* vd = status_get_viewdata(bl);
	int g_id = status_get_guild_id(bl);

#if PACKETVER < 20091103
	if (!pcdb_checkid(vd->class_)) {
		struct packet_idle_unit2 p;

		p.PacketType = idle_unit2Type;
#if PACKETVER >= 20071106
		p.objecttype = clif_bl_type(bl, walking);
#endif
		p.GID = bl->id;
		p.speed = status_get_speed(bl);
		p.bodyState = (sc) ? sc->opt1 : 0;
		p.healthState = (sc) ? sc->opt2 : 0;
		p.effectState = (sc) ? sc->option : 0;
		p.job = vd->class_;
		p.head = vd->hair_style;
		p.weapon = vd->weapon;
		p.accessory = vd->head_bottom;
		if (bl->type == BL_NPC && vd->class_ == FLAG_CLASS) {
			// The hell, why flags work like this?
			p.shield = status_get_emblem_id(bl);
			p.accessory2 = GetWord(g_id, 1);
			p.accessory3 = GetWord(g_id, 0);
		}
		else {
			p.shield = vd->shield;
			p.accessory2 = vd->head_top;
			p.accessory3 = vd->head_mid;
		}
		p.headpalette = vd->hair_color;
		p.bodypalette = vd->cloth_color;
		p.headDir = (sd) ? sd->head_dir : 0;
		p.GUID = g_id;
		p.GEmblemVer = status_get_emblem_id(bl);
		p.honor = (sd) ? sd->status.manner : 0;
		p.virtue = (sc) ? sc->opt3 : 0;
		p.isPKModeON = (sd && sd->status.karma) ? 1 : 0;
		p.sex = vd->sex;
		WBUFPOS(&p.PosDir[0], 0, bl->x, bl->y, unit_getdir(bl));
		p.xSize = p.ySize = (sd) ? 5 : 0;
		p.state = vd->dead_sit;
		p.clevel = clif_setlevel(bl);

		clif_send(&p, sizeof(p), tbl, target);

		return;
}
#endif

	struct packet_idle_unit p;

	p.PacketType = idle_unitType;
#if PACKETVER >= 20091103
	p.PacketLength = sizeof(p);
	p.objecttype = clif_bl_type(bl);
#endif
#if PACKETVER >= 20131223
	p.AID = bl->id;
	p.GID = (sd) ? sd->status.char_id : 0; // CCODE
#else
	p.GID = bl->id;
#endif
	p.speed = status_get_speed(bl);
	p.bodyState = (sc) ? sc->opt1 : 0;
	p.healthState = (sc) ? sc->opt2 : 0;

	// npc option changed?
	if (tbl && tbl->type == BL_PC && bl->type == BL_NPC) {
		struct map_session_data* sd = (struct map_session_data*)tbl;
		struct npc_data* nd = (struct npc_data*)bl;
		int option = (sc) ? sc->option : 0;

		p.effectState = option;
	}
	else {
		p.effectState = (sc) ? sc->option : 0;
	}
	p.job = vd->class_;
	p.head = vd->hair_style;
	p.weapon = vd->weapon;
#if PACKETVER < 7 || PACKETVER >= 20181121
	p.shield = vd->shield;
#endif
	if (bl->type == BL_NPC && vd->class_ == FLAG_CLASS) {
		// The hell, why flags work like this?
		p.accessory = status_get_emblem_id(bl);
		p.accessory2 = GetWord(g_id, 1);
		p.accessory3 = GetWord(g_id, 0);
	}
	else {
		p.accessory = vd->head_bottom;
		p.accessory2 = vd->head_top;
		p.accessory3 = vd->head_mid;
	}
	p.headpalette = vd->hair_color;
	p.bodypalette = vd->cloth_color;
	p.headDir = (sd) ? sd->head_dir : 0;
#if PACKETVER >= 20101124
	p.robe = vd->robe;
#endif
	p.GUID = g_id;
	p.GEmblemVer = status_get_emblem_id(bl);
	p.honor = (sd) ? sd->status.manner : 0;
	p.virtue = (sc) ? sc->opt3 : 0;
	p.isPKModeON = (sd && sd->status.karma) ? 1 : 0;
	p.sex = vd->sex;
	WBUFPOS(&p.PosDir[0], 0, bl->x, bl->y, unit_getdir(bl));
	p.xSize = p.ySize = (sd) ? 5 : 0;
	p.state = vd->dead_sit;
	p.clevel = clif_setlevel(bl);
#if PACKETVER >= 20080102
	p.font = (sd) ? sd->status.font : 0;
#endif
#if PACKETVER >= 20120221
	if (battle_config.monster_hp_bars_info && !map[bl->m].flag.hidemobhpbar && bl->type == BL_MOB && (status_get_hp(bl) < status_get_max_hp(bl))) {
		p.maxHP = status_get_max_hp(bl);
		p.HP = status_get_hp(bl);
	}
	else {
		p.maxHP = -1;
		p.HP = -1;
	}
	p.isBoss = (bl->type == BL_MOB && (((TBL_MOB *)bl)->db->mexp > 0)) ? 1 : 0;
#endif
#if PACKETVER >= 20150513
	p.body = vd->body_style;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	safestrncpy(p.name, status_get_name(bl), NAME_LENGTH);
#endif

	if (disguised(bl)) {
#if PACKETVER >= 20091103
		p.objecttype = pcdb_checkid(status_get_viewdata(bl)->class_) ? 0x0 : 0x5; //PC_TYPE : NPC_MOB_TYPE
#if PACKETVER >= 20131223
		p.AID = -bl->id;
#else
		p.GID = -bl->id;
#endif
#else
		p.GID = -bl->id;
#endif
	}

	clif_send(&p, sizeof(p), tbl, target);
}

static void clif_spawn_unit(struct block_list *bl, enum send_target target) {
	nullpo_retv(bl);

	struct map_session_data* sd = BL_CAST(BL_PC, bl);
	struct status_change* sc = status_get_sc(bl);
	struct view_data* vd = status_get_viewdata(bl);
	int g_id = status_get_guild_id(bl);

#if PACKETVER < 20091103
	if (!pcdb_checkid(vd->class_)) {
		struct packet_spawn_unit2 p;

		p.PacketType = spawn_unit2Type;
#if PACKETVER >= 20071106
		p.objecttype = clif_bl_type(bl, false);
#endif
		p.GID = bl->id;
		p.speed = status_get_speed(bl);
		p.bodyState = (sc) ? sc->opt1 : 0;
		p.healthState = (sc) ? sc->opt2 : 0;
		p.effectState = (sc) ? sc->option : 0;
		p.head = vd->hair_style;
		p.weapon = vd->weapon;
		p.accessory = vd->head_bottom;
		p.job = vd->class_;
		if (bl->type == BL_NPC && vd->class_ == FLAG_CLASS) {
			// The hell, why flags work like this?
			p.shield = status_get_emblem_id(bl);
			p.accessory2 = GetWord(g_id, 1);
			p.accessory3 = GetWord(g_id, 0);
		}
		else {
			p.shield = vd->shield;
			p.accessory2 = vd->head_top;
			p.accessory3 = vd->head_mid;
		}
		p.headpalette = vd->hair_color;
		p.bodypalette = vd->cloth_color;
		p.headDir = (sd) ? sd->head_dir : 0;
		p.isPKModeON = (sd && sd->status.karma) ? 1 : 0;
		p.sex = vd->sex;
		WBUFPOS(&p.PosDir[0], 0, bl->x, bl->y, unit_getdir(bl));
		p.xSize = p.ySize = (sd) ? 5 : 0;

		clif_send(&p, sizeof(p), bl, target);
		return;
	}
#endif

	struct packet_spawn_unit p;

	p.PacketType = spawn_unitType;
#if PACKETVER >= 20091103
	p.PacketLength = sizeof(p);
	p.objecttype = clif_bl_type(bl);
#endif
#if PACKETVER >= 20131223
	p.AID = bl->id;
	p.GID = (sd) ? sd->status.char_id : 0; // CCODE
#else
	p.GID = bl->id;
#endif
	p.speed = status_get_speed(bl);
	p.bodyState = (sc) ? sc->opt1 : 0;
	p.healthState = (sc) ? sc->opt2 : 0;
	p.effectState = (sc) ? sc->option : 0;
	p.job = vd->class_;
	p.head = vd->hair_style;
	p.weapon = vd->weapon;
#if PACKETVER < 7 || PACKETVER >= 20181121
	p.shield = vd->shield;
#endif
	if (bl->type == BL_NPC && vd->class_ == FLAG_CLASS) {
		// The hell, why flags work like this?
		p.accessory = status_get_emblem_id(bl);
		p.accessory2 = GetWord(g_id, 1);
		p.accessory3 = GetWord(g_id, 0);
	}
	else {
		p.accessory = vd->head_bottom;
		p.accessory2 = vd->head_top;
		p.accessory3 = vd->head_mid;
	}
	p.headpalette = vd->hair_color;
	p.bodypalette = vd->cloth_color;
	p.headDir = (sd) ? sd->head_dir : 0;
#if PACKETVER >= 20101124
	p.robe = vd->robe;
#endif
	p.GUID = g_id;
	p.GEmblemVer = status_get_emblem_id(bl);
	p.honor = (sd) ? sd->status.manner : 0;
	p.virtue = (sc) ? sc->opt3 : 0;
	p.isPKModeON = (sd && sd->status.karma) ? 1 : 0;
	p.sex = vd->sex;
	WBUFPOS(&p.PosDir[0], 0, bl->x, bl->y, unit_getdir(bl));
	p.xSize = p.ySize = (sd) ? 5 : 0;
	p.clevel = clif_setlevel(bl);
#if PACKETVER >= 20080102
	p.font = (sd) ? sd->status.font : 0;
#endif
#if PACKETVER >= 20120221
	if (battle_config.monster_hp_bars_info && bl->type == BL_MOB && !map[bl->m].flag.hidemobhpbar && (status_get_hp(bl) < status_get_max_hp(bl))) {
		p.maxHP = status_get_max_hp(bl);
		p.HP = status_get_hp(bl);
	}
	else {
		p.maxHP = -1;
		p.HP = -1;
	}

	p.isBoss = (bl->type == BL_MOB && (((TBL_MOB *)bl)->db->mexp > 0)) ? 1 : 0;
#endif
#if PACKETVER >= 20150513
	p.body = vd->body_style;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	safestrncpy(p.name, status_get_name(bl), NAME_LENGTH);
#endif

	if (disguised(bl)) {
		nullpo_retv(sd);

		if (sd->status.class_ != sd->disguise) {
			clif_send(&p, sizeof(p), bl, target);
		}

#if PACKETVER >= 20091103
		p.objecttype = pcdb_checkid(status_get_viewdata(bl)->class_) ? 0x0 : 0x5; //PC_TYPE : NPC_MOB_TYPE
#if PACKETVER >= 20131223
		p.AID = -bl->id;
#else
		p.GID = -bl->id;
#endif
#else
		p.GID = -bl->id;
#endif
		clif_send(&p, sizeof(p), bl, SELF);
	}
	else {
		clif_send(&p, sizeof(p), bl, target);
	}
}

/*==========================================
 * Prepares 'unit walking' packet
 *------------------------------------------*/
static void clif_set_unit_walking(struct block_list *bl, struct map_session_data *tsd, struct unit_data *ud, enum send_target target) {
	nullpo_retv(bl);
	nullpo_retv(ud);

	struct map_session_data* sd;
	struct status_change* sc = status_get_sc(bl);
	struct view_data* vd = status_get_viewdata(bl);
	struct packet_unit_walking p;
	int g_id = status_get_guild_id(bl);

	sd = BL_CAST(BL_PC, bl);

	p.PacketType = unit_walkingType;
#if PACKETVER >= 20091103
	p.PacketLength = sizeof(p);
#endif
#if PACKETVER >= 20071106
	p.objecttype = clif_bl_type(bl);
#endif
#if PACKETVER >= 20131223
	p.AID = bl->id;
	p.GID = (sd) ? sd->status.char_id : 0; // CCODE
#else
	p.GID = bl->id;
#endif
	p.speed = status_get_speed(bl);
	p.bodyState = (sc) ? sc->opt1 : 0;
	p.healthState = (sc) ? sc->opt2 : 0;
	p.effectState = (sc) ? sc->option : 0;
	p.job = vd->class_;
	p.head = vd->hair_style;
	p.weapon = vd->weapon;
	p.accessory = vd->head_bottom;
	p.moveStartTime = (unsigned int)gettick();
#if PACKETVER < 7 || PACKETVER >= 20181121
	p.shield = vd->shield;
#endif
	p.accessory2 = vd->head_top;
	p.accessory3 = vd->head_mid;
	p.headpalette = vd->hair_color;
	p.bodypalette = vd->cloth_color;
	p.headDir = (sd) ? sd->head_dir : 0;
#if PACKETVER >= 20101124
	p.robe = vd->robe;
#endif
	p.GUID = g_id;
	p.GEmblemVer = status_get_emblem_id(bl);
	p.honor = (sd) ? sd->status.manner : 0;
	p.virtue = (sc) ? sc->opt3 : 0;
	p.isPKModeON = (sd && sd->status.karma) ? 1 : 0;
	p.sex = vd->sex;
	WBUFPOS2(&p.MoveData[0], 0, bl->x, bl->y, ud->to_x, ud->to_y, 8, 8);
	p.xSize = p.ySize = (sd) ? 5 : 0;
	p.clevel = clif_setlevel(bl);
#if PACKETVER >= 20080102
	p.font = (sd) ? sd->status.font : 0;
#endif
#if PACKETVER >= 20120221
	if (battle_config.monster_hp_bars_info && !map[bl->m].flag.hidemobhpbar && bl->type == BL_MOB && (status_get_hp(bl) < status_get_max_hp(bl))) {
		p.maxHP = status_get_max_hp(bl);
		p.HP = status_get_hp(bl);
	}
	else {
		p.maxHP = -1;
		p.HP = -1;
	}

	p.isBoss = (bl->type == BL_MOB && (((TBL_MOB*)bl)->db->mexp > 0)) ? 1 : 0;
#endif
#if PACKETVER >= 20150513
	p.body = vd->body_style;
#endif
	/* Might be earlier, this is when the named item bug began */
#if PACKETVER >= 20131223
	safestrncpy(p.name, status_get_name(bl), NAME_LENGTH);
#endif

	clif_send(&p, sizeof(p), tsd ? &tsd->bl : bl, target);

	if (disguised(bl)) {
#if PACKETVER >= 20091103
		p.objecttype = pcdb_checkid(status_get_viewdata(bl)->class_) ? 0x0 : 0x5; //PC_TYPE : NPC_MOB_TYPE
#if PACKETVER >= 20131223
		p.AID = -bl->id;
#else
		p.GID = -bl->id;
#endif
#else
		p.GID = -bl->id;
#endif
		clif_send(&p, sizeof(p), bl, SELF);
	}
	}

//Modifies the buffer for disguise characters and sends it to self.
//Used for spawn/walk packets, where the ID offset changes for packetver >=9
static void clif_setdisguise(struct block_list *bl, unsigned char *buf,int len)
{
#if PACKETVER >= 20091103
	WBUFB(buf,4)= pcdb_checkid(status_get_viewdata(bl)->class_) ? 0x0 : 0x5; //PC_TYPE : NPC_MOB_TYPE
	WBUFL(buf,5)=-bl->id;
#elif PACKETVER >= 20071106
	WBUFB(buf,2)= pcdb_checkid(status_get_viewdata(bl)->class_) ? 0x0 : 0x5; //PC_TYPE : NPC_MOB_TYPE
	WBUFL(buf,3)=-bl->id;
#else
	WBUFL(buf,2)=-bl->id;
#endif
	clif_send(buf, len, bl, SELF);
}

/// Changes sprite of an NPC object (ZC_NPCSPRITE_CHANGE).
/// 01b0 <id>.L <type>.B <value>.L
/// type:
///     unused
void clif_class_change_target(struct block_list *bl, int class_, int type, enum send_target target, struct map_session_data *sd)
{
	nullpo_retv(bl);

	if (!pcdb_checkid(class_))
	{// player classes yield missing sprites
		unsigned char buf[16];
		WBUFW(buf, 0) = 0x1b0;
		WBUFL(buf, 2) = bl->id;
		WBUFB(buf, 6) = type;
		WBUFL(buf, 7) = class_;
		clif_send(buf, packet_len(0x1b0), (sd == NULL ? bl : &(sd->bl)), target);
	}
}

#define clif_spiritball_single(sd)	clif_spiritball_sub(&sd->bl, &sd->bl, SELF)
#define clif_hom_spiritball_single(hd)	clif_spiritball_sub(&hd->bl, &hd->bl, SELF)

/*==========================================
 * ZC_SPIRITS_ATTRIBUTE =  0x8cf
 * this+0x0 / short PacketType
 * this+0x2 / unsigned long AID
 * this+0x6 / short SpritsType
 * this+0x8 / short Num
 *
 * SpiritsType
 * SPIRITS_TYPE_NONE =  0x0
 * SPIRITS_TYPE_CHARM_WATER =  0x1
 * SPIRITS_TYPE_CHARM_LAND =  0x2
 * SPIRITS_TYPE_CHARM_FIRE =  0x3
 * SPIRITS_TYPE_CHARM_WIND =  0x4
 * SPIRTIS_TYPE_SPHERE =  0x5
 *------------------------------------------*/
static void clif_spiritball_attribute_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd, packet_len(0x08cf));
	WFIFOW(fd,0) = 0x08cf;
	WFIFOL(fd,2) = sd->bl.id;
	WFIFOW(fd, 6) = sd->charmball_type;
	WFIFOW(fd, 8) = sd->charmball;
	WFIFOSET(fd, packet_len(0x08cf));
}

/// Notifies the client of an object's souls.
/// Note: Spirit spheres and Soul spheres work on
/// seprate systems officially, but both send out
/// the same packet which leads to confusion on how
/// much soul energy a Soul Reaper acturally has
/// should the player also have spirit spheres.
/// They will likely create a new packet for this soon
/// to seprate the animations for spirit and soul spheres.
/// For now well use this and replace it later when possible. [Rytech]
/// 
/// 01d0 <id>.L <amount>.W (ZC_SPIRITS)
/// 01e1 <id>.L <amount>.W (ZC_SPIRITS2)
static void clif_soulball_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd, packet_len(0x1d0));
	WFIFOW(fd, 0) = 0x1d0;
	WFIFOL(fd, 2) = sd->bl.id;
	WFIFOW(fd, 6) = sd->soulball;
	WFIFOSET(fd, packet_len(0x1d0));
}

/*==========================================
 * ZC_MILLENNIUMSHIELD =  0x440
 * this+0x0 / short PacketType
 * this+0x2 / unsigned long AID
 * this+0x6 / short num
 * this+0x8 / short state
 *
 * State - How the heck does the state work?
 * MILLENNIUMSHIELD_STATE_STAND =  0x0
 * MILLENNIUMSHIELD_STATE_MOVE =  0x1
 *-----------------------------------------*/
static void clif_millenniumshield_single(int fd, struct block_list *bl, short shield_count)
{
	WFIFOHEAD(fd, packet_len(0x440));
	WFIFOW(fd,0)=0x440;
	WFIFOL(fd,2)=bl->id;
	WFIFOW(fd,6)=shield_count;
	WFIFOW(fd,8)=0;
	WFIFOSET(fd, packet_len(0x440));
}


/*==========================================
 *
 *------------------------------------------*/
static void clif_weather_check(struct map_session_data *sd)
{
	int m = sd->bl.m, fd = sd->fd;
	
	if (map[m].flag.snow
		|| map[m].flag.clouds
		|| map[m].flag.fog
		|| map[m].flag.fireworks
		|| map[m].flag.sakura
		|| map[m].flag.leaves
		|| map[m].flag.rain
		|| map[m].flag.clouds2)
	{
		if (map[m].flag.snow)
			clif_specialeffect_single(&sd->bl, 162, fd);
		if (map[m].flag.clouds)
			clif_specialeffect_single(&sd->bl, 233, fd);
		if (map[m].flag.clouds2)
			clif_specialeffect_single(&sd->bl, 516, fd);
		if (map[m].flag.fog)
			clif_specialeffect_single(&sd->bl, 515, fd);
		if (map[m].flag.fireworks) {
			clif_specialeffect_single(&sd->bl, 297, fd);
			clif_specialeffect_single(&sd->bl, 299, fd);
			clif_specialeffect_single(&sd->bl, 301, fd);
		}
		if (map[m].flag.sakura)
			clif_specialeffect_single(&sd->bl, 163, fd);
		if (map[m].flag.leaves)
			clif_specialeffect_single(&sd->bl, 333, fd);
		if (map[m].flag.rain)
			clif_specialeffect_single(&sd->bl, 161, fd);
	}
}

void clif_weather(int m)
{
	struct s_mapiterator* iter;
	struct map_session_data *sd=NULL;

	iter = mapit_getallusers();
	for( sd = (struct map_session_data*)mapit_first(iter); mapit_exists(iter); sd = (struct map_session_data*)mapit_next(iter) )
	{
		if( sd->bl.m == m )
			clif_weather_check(sd);
	}
	mapit_free(iter);
}

int clif_spawn(struct block_list *bl)
{
	struct view_data *vd;
	short i;

	vd = status_get_viewdata(bl);
	if (!vd || vd->class_ == INVISIBLE_CLASS ||
		bl->type == BL_NPC && !((TBL_NPC*)bl)->chat_id && ((TBL_NPC*)bl)->sc.option&OPTION_INVISIBLE)
		return 0;

	if (bl->type == BL_NPC && !vd->dead_sit) {
		clif_set_unit_idle(bl, false, AREA_WOS, bl);
	}
	else {
		clif_spawn_unit(bl, AREA_WOS);
	}

	if (vd->cloth_color)
		clif_refreshlook(bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,AREA_WOS);
	if (vd->body_style)
		clif_refreshlook(bl,bl->id,LOOK_BODY2,vd->body_style,AREA_WOS);
		
	switch (bl->type)
	{
	case BL_PC:
		{
			TBL_PC *sd = ((TBL_PC*)bl);
			if (sd->spiritball > 0)
				clif_spiritball(sd);
			if (sd->shieldball > 0)
				clif_millenniumshield(&sd->bl, sd->shieldball);
			if (sd->rageball > 0)
				clif_millenniumshield(&sd->bl, sd->rageball);
			if (sd->charmball > 0)
				clif_spiritball_attribute(sd);
			if (sd->soulball > 0)
				clif_soulball(sd);
			if(sd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect(bl,423,AREA);
			else if(sd->state.size==1)
				clif_specialeffect(bl,421,AREA);
			if( sd->bg_id && map[sd->bl.m].flag.battleground )
				clif_sendbgemblem_area(sd);
			if (sd->status.robe)
				clif_refreshlook(bl, bl->id, LOOK_ROBE, sd->status.robe, AREA);

			clif_hat_effects(sd, bl, AREA);
#if PACKETVER >= 20111108
			if (sd->sc.count)
			{// Starting the check at SC_ALL_RIDING since all SC's after this don't use OPT3.
			//  Starting at 0 would just waste CPU cycles. [Rytech]
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{// If SC is not active, check for the next one.
					if (!sd->sc.data[i])
						continue;

					// Don't bother sending the packet if the SC has no icon data.
					if (status_sc2icon(i) == SI_BLANK)
						continue;

					// Status active and has a icon for showing possible animation? Send it.
					clif_efst_status_change_sub(bl, bl, AREA);
				}
			}
#endif
		}
		break;
	case BL_MOB:
		{
			TBL_MOB *md = ((TBL_MOB*)bl);
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect(&md->bl,423,AREA);
			else if(md->special_state.size==1)
				clif_specialeffect(&md->bl,421,AREA);
#if PACKETVER >= 20111108
			if (md->sc.count)
			{
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{
					if (!md->sc.data[i])
						continue;

					if (status_sc2icon(i) == SI_BLANK)
						continue;

					clif_efst_status_change_sub(bl, bl, AREA);
				}
			}
#endif
		}
		break;
	case BL_NPC:
		{
			TBL_NPC *nd = ((TBL_NPC*)bl);
			if( nd->size == 2 )
				clif_specialeffect(&nd->bl,423,AREA);
			else if( nd->size == 1 )
				clif_specialeffect(&nd->bl,421,AREA);
#if PACKETVER >= 20111108
			if (nd->sc.count)
			{
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{
					if (!nd->sc.data[i])
						continue;

					if (status_sc2icon(i) == SI_BLANK)
						continue;

					clif_efst_status_change_sub(bl, bl, AREA);
				}
			}
#endif

		}
		break;
	case BL_PET:
		if (vd->head_bottom)
			clif_pet_equip_area((TBL_PET*)bl); // needed to display pet equip properly
		break;
	case BL_HOM:
	{
		TBL_HOM *hd = ((TBL_HOM*)bl);
		if (hd->hom_spiritball > 0)
			clif_hom_spiritball(hd);
#if PACKETVER >= 20111108
		if (hd->sc.count)
		{
			for (i = SC_ALL_RIDING; i < SC_MAX; i++)
			{
				if (!hd->sc.data[i])
					continue;

				if (status_sc2icon(i) == SI_BLANK)
					continue;

				clif_efst_status_change_sub(bl, bl, AREA);
			}
		}
#endif
	}
	break;
	}
	return 0;
}

/// Sends information about owned homunculus to the client (ZC_PROPERTY_HOMUN). [orn] [15peaces]
/// 022e <name>.24B <modified>.B <level>.W <hunger>.W <intimacy>.W <equip id>.W <atk>.W <matk>.W <hit>.W <crit>.W <def>.W <mdef>.W <flee>.W <aspd>.W <hp>.W <max hp>.W <sp>.W <max sp>.W <exp>.L <max exp>.L <skill points>.W <atk range>.W	(ZC_PROPERTY_HOMUN)
/// 09f7 <name>.24B <modified>.B <level>.W <hunger>.W <intimacy>.W <equip id>.W <atk>.W <matk>.W <hit>.W <crit>.W <def>.W <mdef>.W <flee>.W <aspd>.W <hp>.L <max hp>.L <sp>.W <max sp>.W <exp>.L <max exp>.L <skill points>.W <atk range>.W (ZC_PROPERTY_HOMUN_2)
void clif_hominfo(struct map_session_data *sd, struct homun_data *hd, int flag) {
#if PACKETVER >= 20101005
	nullpo_retv(sd);
	nullpo_retv(hd);

	struct status_data *status = &hd->battle_status;
	struct PACKET_ZC_PROPERTY_HOMUN p;

	p.packetType = HEADER_ZC_PROPERTY_HOMUN;
	safestrncpy(p.name, hd->homunculus.name, sizeof(p.name));
	// Bit field, bit 0 : rename_flag (1 = already renamed), bit 1 : homunc vaporized (1 = true), bit 2 : homunc dead (1 = true)
	p.flags = (battle_config.hom_rename ? 0 : hd->homunculus.rename_flag) | (hd->homunculus.vaporize << 1) | (hd->homunculus.hp ? 0 : 4);
	p.level = hd->homunculus.level;
	p.hunger = hd->homunculus.hunger;
	p.intimacy = hd->homunculus.intimacy / 100;
	p.atk2 = cap_value(status->rhw.atk2 + status->batk, 0, INT16_MAX);
	p.matk = i16min(status->matk_max, INT16_MAX); //FIXME capping to INT16 here is too late
	p.hit = status->hit;
	if (battle_config.hom_setting&0x10) {
		p.crit = status->luk / 3 + 1;	//crit is a +1 decimal value! Just display purpose.[Vicious]
	}
	else {
		p.crit = status->cri / 10;
	}
	p.def = status->def + status->vit;
	p.mdef = status->mdef;
	p.flee = status->flee;
	p.amotion = (flag) ? 0 : status->amotion;
#if PACKETVER >= 20141016
	// Homunculus HP bar will screw up if the percentage calculation exceeds signed values
	// Tested maximum: 21474836(=INT32_MAX/100), any value above will screw up the HP bar
	if (status->max_hp > (INT32_MAX / 100)) {
		p.hp = status->hp / (status->max_hp / 100);
		p.maxHp = 100;
	}
	else {
		p.hp = status->hp;
		p.maxHp = status->max_hp;
	}
#else
	if (status->max_hp > INT16_MAX) {
		p.hp = status->hp / (status->max_hp / 100);
		p.maxHp = 100;
	}
	else {
		p.hp = status->hp;
		p.maxHp = status->max_hp;
	}
#endif
	if (status->max_sp > INT16_MAX) {
		p.sp = status->sp / (status->max_sp / 100);
		p.maxSp = 100;
	}
	else {
		p.sp = status->sp;
		p.maxSp = status->max_sp;
	}
	p.exp = hd->homunculus.exp;
	p.expNext = hd->exp_next;
	if (hd->homunculus.level >= hd->homunculusDB->maxlevel)
		p.expNext = 0;
	else
		p.expNext = hd->exp_next;
	p.skillPoints = hd->homunculus.skillpts;
	p.range = status_get_range(&hd->bl);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
#endif
}

/// Notification about a change in homunuculus' state (ZC_CHANGESTATE_MER).
/// 0230 <type>.B <state>.B <id>.L <data>.L
/// type:
///     unused
/// state:
///     0 = pre-init
///     1 = intimacy
///     2 = hunger
///     3 = accessory?
///     ? = ignored
void clif_send_homdata(struct map_session_data *sd, int state, int param)
{	//[orn]
	int fd = sd->fd;

	if ((state == SP_INTIMATE) && (param >= 910) && (sd->hd->homunculus.class_ == sd->hd->homunculusDB->evo_class))
		hom_calc_skilltree(sd->hd, 0);

	WFIFOHEAD(fd, packet_len(0x230));
	WFIFOW(fd,0)=0x230;
	WFIFOB(fd,2)=0;
	WFIFOB(fd,3)=state;
	WFIFOL(fd,4)=sd->hd->bl.id;
	WFIFOL(fd,8)=param;
	WFIFOSET(fd,packet_len(0x230));
}


int clif_homskillinfoblock(struct map_session_data *sd)
{	//[orn]
	struct homun_data *hd;
	int fd = sd->fd;
	int i,j,len=4,id;

	nullpo_ret(sd);

	WFIFOHEAD(fd, 4+37*MAX_HOMUNSKILL);

	hd = sd->hd;
	if ( !hd ) 
		return 0 ;

	WFIFOW(fd,0)=0x235;
	for ( i = 0; i < MAX_HOMUNSKILL; i++){
		if( (id = hd->homunculus.hskill[i].id) != 0 ){
			int combo = (hd->homunculus.hskill[i].flag)&SKILL_FLAG_TMP_COMBO;
			j = id - HM_SKILLBASE;
			WFIFOW(fd,len  ) = id;
			WFIFOL(fd,len+2) = ((combo) ? INF_SELF_SKILL : skill_get_inf(id));
			WFIFOW(fd,len+6) = hd->homunculus.hskill[j].lv;
			WFIFOW(fd,len+8) = skill_get_sp(id,hd->homunculus.hskill[j].lv);
			WFIFOW(fd,len+10)= skill_get_range2(&sd->hd->bl, id,hd->homunculus.hskill[j].lv,false);
			safestrncpy((char*)WFIFOP(fd,len+12), skill_get_name(id), NAME_LENGTH);
			WFIFOB(fd,len+36) = (hd->homunculus.hskill[j].lv < hom_skill_tree_get_max(id, hd->homunculus.class_))?1:0;
			len+=37;
		}
	}
	WFIFOW(fd,2)=len;
	WFIFOSET(fd,len);

	return 0;
}

void clif_homskillup(struct map_session_data *sd, int skill_id)
{	//[orn]
	struct homun_data *hd;
	int fd, skillid;
	nullpo_retv(sd);
	skillid = skill_id - HM_SKILLBASE;

	fd=sd->fd;
	hd=sd->hd;

	WFIFOHEAD(fd, packet_len(0x239));
	WFIFOW(fd,0) = 0x239;
	WFIFOW(fd,2) = skill_id;
	WFIFOW(fd,4) = hd->homunculus.hskill[skillid].lv;
	WFIFOW(fd,6) = skill_get_sp(skill_id,hd->homunculus.hskill[skillid].lv);
	WFIFOW(fd,8) = skill_get_range2(&hd->bl, skill_id,hd->homunculus.hskill[skillid].lv,false);
	WFIFOB(fd,10) = (hd->homunculus.hskill[skillid].lv < skill_get_max(hd->homunculus.hskill[skillid].id)) ? 1 : 0;
	WFIFOSET(fd,packet_len(0x239));
}

/// Result of request to feed a homun/merc.
/// 022f <result>.B <name id>.W (ZC_FEED_MER)
/// result:
///     0 = failure
///     1 = success
void clif_hom_food(struct map_session_data *sd, int foodid, int fail) {
	nullpo_retv(sd);

	struct PACKET_ZC_FEED_MER p;

	p.packetType = 0x22f;
	p.result = fail;
	p.itemId = client_nameid(foodid);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}


/// Notifies the client, that it is walking (ZC_NOTIFY_PLAYERMOVE).
/// 0087 <walk start time>.L <walk data>.6B
void clif_walkok(struct map_session_data *sd)
{
	int fd=sd->fd;

	WFIFOHEAD(fd, packet_len(0x87));
	WFIFOW(fd,0)=0x87;
	WFIFOL(fd,2)=(unsigned int)gettick();
	WFIFOPOS2(fd,6,sd->bl.x,sd->bl.y,sd->ud.to_x,sd->ud.to_y,8,8);
	WFIFOSET(fd,packet_len(0x87));
}


static void clif_move2(struct block_list *bl, struct view_data *vd, struct unit_data *ud)
{
	struct status_change *sc = NULL;

	if ((sc = status_get_sc(bl)) && sc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_INVISIBLE | OPTION_CHASEWALK))
		clif_ally_only = true;
	
	clif_set_unit_walking(bl, NULL, ud, AREA_WOS);
		
	if(vd->cloth_color)
		clif_refreshlook(bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,AREA_WOS);
	if(vd->body_style)
		clif_refreshlook(bl,bl->id,LOOK_BODY2,vd->body_style,AREA_WOS);

	switch(bl->type)
	{
	case BL_PC:
		{
			TBL_PC *sd = ((TBL_PC*)bl);
//			clif_movepc(sd);
			if(sd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect(&sd->bl,423,AREA);
			else if(sd->state.size==1)
				clif_specialeffect(&sd->bl,421,AREA);
		}
		break;
	case BL_MOB:
		{
			TBL_MOB *md = ((TBL_MOB*)bl);
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect(&md->bl,423,AREA);
			else if(md->special_state.size==1)
				clif_specialeffect(&md->bl,421,AREA);
		}
		break;
	case BL_PET:
		if( vd->head_bottom )
		{// needed to display pet equip properly
			clif_pet_equip_area((TBL_PET*)bl); 
		}
		break;
	}
	clif_ally_only = false;
}


/// Notifies clients in an area, that an other visible object is walking (ZC_NOTIFY_PLAYERMOVE).
/// 0086 <id>.L <walk data>.6B <walk start time>.L
/// Note: unit must not be self
void clif_move(struct unit_data *ud)
{
	unsigned char buf[16];
	struct view_data* vd;
	struct block_list* bl = ud->bl;
	struct status_change *sc = NULL;

	vd = status_get_viewdata(bl);
	if (!vd || vd->class_ == INVISIBLE_CLASS ||
		bl->type == BL_NPC && !((TBL_NPC*)bl)->chat_id && ((TBL_NPC*)bl)->sc.option&OPTION_INVISIBLE)
		return; //This performance check is needed to keep GM-hidden objects from being notified to bots.
	
	if (ud->state.speed_changed) {
		// Since we don't know how to update the speed of other objects,
		// use the old walk packet to update the data.
		ud->state.speed_changed = 0;
		clif_move2(bl, vd, ud);
		return;
	}

	if ((sc = status_get_sc(bl)) && sc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_INVISIBLE | OPTION_CHASEWALK))
		clif_ally_only = true;

	WBUFW(buf,0)=0x86;
	WBUFL(buf,2)=bl->id;
	WBUFPOS2(buf,6,bl->x,bl->y,ud->to_x,ud->to_y,8,8);
	WBUFL(buf,12)=(unsigned int)gettick();
	clif_send(buf, packet_len(0x86), bl, AREA_WOS);
	if (disguised(bl))
	{
		WBUFL(buf,2)=-bl->id;
		clif_send(buf, packet_len(0x86), bl, SELF);
	}
	clif_ally_only = false;
}


/*==========================================
 * Delays the map_quit of a player after they are disconnected. [Skotlex]
 *------------------------------------------*/
static int clif_delayquit(int tid, int64 tick, int id, intptr_t data)
{
	struct map_session_data *sd = NULL;

	//Remove player from map server
	if ((sd = map_id2sd(id)) != NULL && sd->fd == 0) //Should be a disconnected player.
		map_quit(sd);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
void clif_quitsave(int fd,struct map_session_data *sd)
{
	if (!battle_config.prevent_logout ||
		DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout)
		map_quit(sd);
	else if (sd->fd)
	{	//Disassociate session from player (session is deleted after this function was called)
		//And set a timer to make him quit later.
		session[sd->fd]->session_data = NULL;
		sd->fd = 0;
		add_timer(gettick() + 10000, clif_delayquit, sd->bl.id, 0);
	}
}

/// Notifies the client of a position change to coordinates on given map (ZC_NPCACK_MAPMOVE).
/// 0091 <map name>.16B <x>.W <y>.W
void clif_changemap(struct map_session_data *sd, short m, int x, int y)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x91));
	WFIFOW(fd,0) = 0x91;
	mapindex_getmapname_ext(map[m].name, (char*)WFIFOP(fd,2));
	WFIFOW(fd,18) = x;
	WFIFOW(fd,20) = y;
	WFIFOSET(fd,packet_len(0x91));
}

/// Notifies the client of a position change to coordinates on given map, which is on another map-server (ZC_NPCACK_SERVERMOVE).
/// 0092 <map name>.16B <x>.W <y>.W <ip>.L <port>.W
/// 0ac7 <map name>.16B <x>.W <y>.W <ip>.L <port>.W <zero>.128B
void clif_changemapserver(struct map_session_data* sd, unsigned short map_index, int x, int y, uint32 ip, uint16 port)
{
	int fd;
#if PACKETVER >= 20170315
	const int cmd = 0xac7;
#else
	const int cmd = 0x92;
#endif
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd,0) = cmd;
	mapindex_getmapname_ext(mapindex_id2name(map_index), (char*)WFIFOP(fd,2));
	WFIFOW(fd,18) = x;
	WFIFOW(fd,20) = y;
	WFIFOL(fd,22) = htonl(ip);
	WFIFOW(fd,26) = ntows(htons(port)); // [!] LE byte order here [!]
	WFIFOSET(fd,packet_len(cmd));
}

/// In many situations (knockback, backslide, etc.) Aegis sends both clif_slide and clif_fixpos
/// This function combines both calls and allows to simplify the calling code
void clif_blown(struct block_list *bl)
{
	clif_slide(bl, bl->x, bl->y);
	clif_fixpos(bl);
}


/// Visually moves(slides) a character to x,y. If the target cell
/// isn't walkable, the char doesn't move at all. If the char is
/// sitting it will stand up (ZC_STOPMOVE).
/// 0088 <id>.L <x>.W <y>.W
void clif_fixpos(struct block_list *bl)
{
	unsigned char buf[10];
	nullpo_retv(bl);

	WBUFW(buf,0) = 0x88;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = bl->x;
	WBUFW(buf,8) = bl->y;
	clif_send(buf, packet_len(0x88), bl, AREA);

	if( disguised(bl) )
	{
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x88), bl, SELF);
	}
}


/// Displays the buy/sell dialog of an NPC shop (ZC_SELECT_DEALTYPE).
/// 00c4 <shop id>.L
void clif_npcbuysell(struct map_session_data* sd, int id)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xc4));
	WFIFOW(fd,0)=0xc4;
	WFIFOL(fd,2)=id;
	WFIFOSET(fd,packet_len(0xc4));
}


/// Presents list of items, that can be bought in an NPC shop.
/// 00c6 <packet len>.W { <price>.L <discount price>.L <item type>.B <name id>.W }* (ZC_PC_PURCHASE_ITEMLIST)
void clif_buylist(struct map_session_data *sd, struct npc_data *nd) {
	nullpo_retv(sd);
	nullpo_retv(nd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	uint16 len = sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST) + nd->u.shop.count * sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST_sub);
	WFIFOHEAD(fd, len);
	struct PACKET_ZC_PC_PURCHASE_ITEMLIST *p = (struct PACKET_ZC_PC_PURCHASE_ITEMLIST *)WFIFOP(fd, 0);
	p->packetType = 0xc6;

	int count = 0;
	for (int i = 0, discount = nd->u.shop.discount; i < nd->u.shop.count; i++) {
		int val = nd->u.shop.shop_item[i].value;

		p->items[count].price = val;
		p->items[count].discountPrice = (discount) ? pc_modifybuyvalue(sd, val) : val;
		p->items[count].itemType = itemtype(nd->u.shop.shop_item[i].nameid);
		p->items[count].itemId = client_nameid(nd->u.shop.shop_item[i].nameid);
		count++;
	}

	p->packetLength = sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST) + count * sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST_sub);
	WFIFOSET(fd, p->packetLength);
}


/// Presents list of items, that can be sold to an NPC shop (ZC_PC_SELL_ITEMLIST).
/// 00c7 <packet len>.W { <index>.W <price>.L <overcharge price>.L }*
void clif_selllist(struct map_session_data *sd)
{
	int fd,i,c=0,val;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, MAX_INVENTORY * 10 + 4);
	WFIFOW(fd,0)=0xc7;
	for( i = 0; i < MAX_INVENTORY; i++ )
	{
		if( sd->inventory.u.items_inventory[i].nameid > 0 && sd->inventory_data[i] )
		{
			if( !itemdb_cansell(&sd->inventory.u.items_inventory[i], pc_isGM(sd)) )
				continue;

			if (sd->inventory.u.items_inventory[i].favorite != 0)
				continue; // Cannot Sell Favorite item

			if( sd->inventory.u.items_inventory[i].expire_time || (sd->inventory.u.items_inventory[i].bound && !pc_can_give_bounded_items(sd->gmlevel)) ) 
				continue; // Cannot Sell Rental Items or Account Bounded Items 
	 
			if( sd->inventory.u.items_inventory[i].bound && !pc_can_give_bounded_items(sd->gmlevel)) 
				continue; // Don't allow sale of bound items

			if (sd->inventory.u.items_inventory[i].equipSwitch)
				continue;

			val=sd->inventory_data[i]->value_sell;
			if( val < 0 )
				continue;
			WFIFOW(fd,4+c*10)=i+2;
			WFIFOL(fd,6+c*10)=val;
			WFIFOL(fd,10+c*10)=pc_modifysellvalue(sd,val);
			c++;
		}
	}
	WFIFOW(fd,2)=c*10+4;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Displays an NPC dialog message (ZC_SAY_DIALOG).
/// 00b4 <packet len>.W <npc id>.L <message>.?B
/// Client behavior (dialog window):
/// - disable mouse targeting
/// - open the dialog window
/// - set npcid of dialog window (0 by default)
/// - if set to clear on next mes, clear contents
/// - append this text
void clif_scriptmes(struct map_session_data *sd, int npcid, const char *mes)
{
	int fd = sd->fd;
	int slen = strlen(mes) + 9;

	WFIFOHEAD(fd, slen);
	WFIFOW(fd,0)=0xb4;
	WFIFOW(fd,2)=slen;
	WFIFOL(fd,4)=npcid;
	memcpy((char*)WFIFOP(fd,8), mes, slen-8);
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Adds a 'next' button to an NPC dialog (ZC_WAIT_DIALOG).
/// 00b5 <npc id>.L
/// Client behavior (dialog window):
/// - disable mouse targeting
/// - open the dialog window
/// - add 'next' button
/// When 'next' is pressed:
/// - 00B9 <npcid of dialog window>.L
/// - set to clear on next mes
/// - remove 'next' button
void clif_scriptnext(struct map_session_data *sd,int npcid)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xb5));
	WFIFOW(fd,0)=0xb5;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0xb5));
}


/// Adds a 'close' button to an NPC dialog (ZC_CLOSE_DIALOG).
/// 00b6 <npc id>.L
/// Client behavior:
/// - if dialog window is open:
///   - remove 'next' button
///   - add 'close' button
/// - else:
///   - enable mouse targeting
///   - close the dialog window
///   - close the menu window
/// When 'close' is pressed:
/// - enable mouse targeting
/// - close the dialog window
/// - close the menu window
/// - 0146 <npcid of dialog window>.L
void clif_scriptclose(struct map_session_data *sd, int npcid)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xb6));
	WFIFOW(fd,0)=0xb6;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0xb6));
}

/// Close script when player is idle
/// 08d6 <npc id>.L (ZC_CLEAR_DIALOG)
void clif_scriptclear(struct map_session_data *sd, int npcid)
{
	struct s_packet_db* info;
	int16 len;
	int cmd = 0;
	int fd;

	nullpo_retv(sd);

	cmd = packet_db_ack[sd->packet_ver][ZC_CLEAR_DIALOG];
	if (!cmd) cmd = 0x8d6; //default
	info = &packet_db[sd->packet_ver][cmd];
	len = info->len;
	fd = sd->fd;

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0x8d6;
	WFIFOL(fd, info->pos[0]) = npcid;
	WFIFOSET(fd, len);
}

/*==========================================
 *
 *------------------------------------------*/
void clif_sendfakenpc(struct map_session_data *sd, int npcid)
{
	unsigned char *buf;
	int fd = sd->fd;
	sd->state.using_fake_npc = 1;

	WFIFOHEAD(fd, packet_len(0x78));
	buf = WFIFOP(fd,0);
	memset(WBUFP(buf,0), 0, packet_len(0x78));
	WBUFW(buf,0)=0x78;
#if PACKETVER >= 20071106
	WBUFB(buf,2) = 0; // object type
	buf = WFIFOP(fd,1);
#endif
	WBUFL(buf,2)=npcid;
	WBUFW(buf,14)=111;
	WBUFPOS(buf,46,sd->bl.x,sd->bl.y,sd->ud.dir);
	WBUFB(buf,49)=5;
	WBUFB(buf,50)=5;
	WFIFOSET(fd, packet_len(0x78));
}


/// Displays an NPC dialog menu (ZC_MENU_LIST).
/// 00b7 <packet len>.W <npc id>.L <menu items>.?B
/// Client behavior:
/// - disable mouse targeting
/// - close the menu window
/// - open the menu window
/// - add options to the menu (separated in the text by ":")
/// - set npcid of menu window
/// - if dialog window is open:
///   - remove 'next' button
/// When 'ok' is pressed:
/// - 00B8 <npcid of menu window>.L <selected option>.B
/// - close the menu window
/// When 'cancel' is pressed:
/// - 00B8 <npcid of menu window>.L <-1>.B
/// - enable mouse targeting
/// - close a bunch of windows...
/// WARNING: the 'cancel' button closes other windows besides the dialog window and the menu window.
///    Which suggests their have intertwined behavior. (probably the mouse targeting)
/// TODO investigate behavior of other windows [FlavioJS]
void clif_scriptmenu(struct map_session_data* sd, int npcid, const char* mes)
{
	int fd = sd->fd;
	int slen = strlen(mes) + 9;
	struct block_list *bl = NULL;

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);

	WFIFOHEAD(fd, slen);
	WFIFOW(fd,0)=0xb7;
	WFIFOW(fd,2)=slen;
	WFIFOL(fd,4)=npcid;
	memcpy((char*)WFIFOP(fd,8), mes, slen-8);
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Displays an NPC dialog input box for numbers (ZC_OPEN_EDITDLG).
/// 0142 <npc id>.L
/// Client behavior (inputnum window):
/// - if npcid exists in the client:
///   - open the inputnum window
///   - set npcid of inputnum window
/// When 'ok' is pressed:
/// - if inputnum window has text:
///   - if npcid exists in the client:
///     - 0143 <npcid of inputnum window>.L <atoi(text)>.L
///   - close inputnum window
void clif_scriptinput(struct map_session_data *sd, int npcid)
{
	int fd;
	struct block_list *bl = NULL;

	nullpo_retv(sd);

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);
	
	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x142));
	WFIFOW(fd,0)=0x142;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0x142));
}


/// Displays an NPC dialog input box for numbers (ZC_OPEN_EDITDLGSTR).
/// 01d4 <npc id>.L
/// Client behavior (inputstr window):
/// - if npcid is 0 or npcid exists in the client:
///   - open the inputstr window
///   - set npcid of inputstr window
/// When 'ok' is pressed:
/// - if inputstr window has text and isn't an insult(manner.txt):
///   - if npcid is 0 or npcid exists in the client:
///     - 01d5 <packetlen>.W <npcid of inputstr window>.L <text>.?B
///   - close inputstr window
void clif_scriptinputstr(struct map_session_data *sd, int npcid)
{
	int fd;
	struct block_list *bl = NULL;

	nullpo_retv(sd);

	if (!sd->state.using_fake_npc && (npcid == fake_nd->bl.id || ((bl = map_id2bl(npcid)) && (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1))))
	   clif_sendfakenpc(sd, npcid);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x1d4));
	WFIFOW(fd,0)=0x1d4;
	WFIFOL(fd,2)=npcid;
	WFIFOSET(fd,packet_len(0x1d4));
}


/// Marks a position on client's minimap (ZC_COMPASS).
/// 0144 <npc id>.L <type>.L <x>.L <y>.L <id>.B <color>.L
/// npc id:
///     is ignored in the client
/// type:
///     0 = display mark for 15 seconds
///     1 = display mark until dead or teleported
///     2 = remove mark
/// color:
///     0x00RRGGBB
void clif_viewpoint(struct map_session_data *sd, int npc_id, int type, int x, int y, int id, int color)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x144));
	WFIFOW(fd,0)=0x144;
	WFIFOL(fd,2)=npc_id;
	WFIFOL(fd,6)=type;
	WFIFOL(fd,10)=x;
	WFIFOL(fd,14)=y;
	WFIFOB(fd,18)=id;
	WFIFOL(fd,19)=color;
	WFIFOSET(fd,packet_len(0x144));
}


/// Displays an illustration image.
/// 0145 <image name>.16B <type>.B (ZC_SHOW_IMAGE)
/// 01b3 <image name>.64B <type>.B (ZC_SHOW_IMAGE2)
/// type:
///     0 = bottom left corner
///     1 = bottom middle
///     2 = bottom right corner
///     3 = middle of screen, inside a movable window
///     4 = middle of screen, movable with a close button, chrome-less
///   255 = hide
void clif_cutin(struct map_session_data* sd, const char* image, int type)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x1b3));
	WFIFOW(fd,0)=0x1b3;
	strncpy((char*)WFIFOP(fd,2),image,64);
	WFIFOB(fd,66)=type;
	WFIFOSET(fd,packet_len(0x1b3));
}


/*==========================================
 * Fills in card data from the given item and into the buffer. [Skotlex]
 *------------------------------------------*/
static void clif_addcards(struct EQUIPSLOTINFO* buf, struct item* item) {
	nullpo_retv(buf);

	// Blank data
	if (item == NULL) {
		buf->card[0] = 0;
		buf->card[1] = 0;
		buf->card[2] = 0;
		buf->card[3] = 0;
		return;
	}

	// Pet eggs
	if (item->card[0] == CARD0_PET) {
		buf->card[0] = 0;
		buf->card[1] = 0;
		buf->card[2] = 0;
		buf->card[3] = item->card[3]; //Pet renamed flag.
		return;
	}

	// Forged/created items
	if (item->card[0] == CARD0_FORGE || item->card[0] == CARD0_CREATE) {
		buf->card[0] = item->card[0];
		buf->card[1] = item->card[1];
		buf->card[2] = item->card[2];
		buf->card[3] = item->card[3];
		return;
	}

	int i = 0, j;

	// Client only receives four cards.. so randomly send them a set of cards. [Skotlex]
	if (MAX_SLOTS > 4 && (j = itemdb_slot(item->nameid)) > 4) {
		i = rnd() % (j - 3); //eg: 6 slots, possible i values: 0->3, 1->4, 2->5 => i = rnd()%3;
	}

	// Normal items.
	for (int k = 0; k < 4; k++, i++) {
		if (item->card[i] > 0) {
			buf->card[k] = client_nameid(item->card[i]);
		}
		else {
			buf->card[k] = 0;
		}
	}
}

/// Fills in part of the item buffers that calls for variable bonuses data. [Napster]
/// A maximum of 5 random options can be supported.
static uint8 clif_add_random_options(struct ItemOptions buf[MAX_ITEM_RDM_OPT], struct item* it) {
	nullpo_retr(0, it);

	uint8 count = 0;

	for (int i = 0; i < MAX_ITEM_RDM_OPT; i++) {
		if (it->option[i].id) {
			buf[i].index = it->option[i].id;	// OptIndex
			buf[i].value = it->option[i].value;	// Value
			buf[i].param = it->option[i].param;	// Param1
			count++;
		}
		else {
			buf[i].index = 0;
			buf[i].value = 0;
			buf[i].param = 0;
		}
	}

#if MAX_ITEM_RDM_OPT < 5
	for (; i < 5; i++) {
		buf[i].index = 0;	// OptIndex
		buf[i].value = 0;	// Value
		buf[i].param = 0;	// Param1
	}
#endif

	return count;
}

/// Notifies the client, about a received inventory item or the result of a pick-up request.
/// 00a0 <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.W <item type>.B <result>.B (ZC_ITEM_PICKUP_ACK)
/// 029a <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.W <item type>.B <result>.B <expire time>.L (ZC_ITEM_PICKUP_ACK2)
/// 02d4 <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.W <item type>.B <result>.B <expire time>.L <bindOnEquipType>.W (ZC_ITEM_PICKUP_ACK3)
/// 0990 <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.L <item type>.B <result>.B <expire time>.L <bindOnEquipType>.W (ZC_ITEM_PICKUP_ACK_V5)
/// 0a0c <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.L <item type>.B <result>.B <expire time>.L <bindOnEquipType>.W (ZC_ITEM_PICKUP_ACK_V6)
/// 0a37 <index>.W <amount>.W <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.L <item type>.B <result>.B <expire time>.L <bindOnEquipType>.W <favorite>.B <view id>.W (ZC_ITEM_PICKUP_ACK_V7)
void clif_additem(struct map_session_data *sd, int n, int amount, unsigned char fail) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	struct packet_additem p;

	if (fail) {
		p.nameid = 0;
		p.IsIdentified = 0;
		p.IsDamaged = 0;
		p.refiningLevel = 0;
		clif_addcards(&p.slot, NULL);
		p.location = 0;
		p.type = 0;
#if PACKETVER >= 20061218
		p.HireExpireDate = 0;
#endif
#if PACKETVER >= 20071002
		p.bindOnEquipType = 0;
#endif
#if PACKETVER >= 20150226
		clif_add_random_options(p.option_data, NULL);
#endif
#if PACKETVER >= 20160921
		p.favorite = 0;
		p.look = 0;
#endif
	}
	else {
		if (n < 0 || n >= MAX_INVENTORY || sd->inventory.u.items_inventory[n].nameid == 0 || sd->inventory_data[n] == NULL) {
			return;
		}

		p.nameid = client_nameid(sd->inventory.u.items_inventory[n].nameid);
		p.IsIdentified = sd->inventory.u.items_inventory[n].identify ? 1 : 0;
		p.IsDamaged = (sd->inventory.u.items_inventory[n].attribute) != 0 ? 1 : 0;
		p.refiningLevel = sd->inventory.u.items_inventory[n].refine;
		clif_addcards(&p.slot, &sd->inventory.u.items_inventory[n]);
		p.location = pc_equippoint(sd, n);
		p.type = itemtype(sd->inventory.u.items_inventory[n].nameid);
#if PACKETVER >= 20061218
		p.HireExpireDate = sd->inventory.u.items_inventory[n].expire_time;
#endif
#if PACKETVER >= 20071002
		/* why restrict the flag to non-stackable? because this is the only packet allows stackable to,
		 * show the color, and therefore it'd be inconsistent with the rest (aka it'd show yellow, you relog/refresh and boom its gone)
		 */
		p.bindOnEquipType = sd->inventory.u.items_inventory[n].bound && !itemdb_isstackable2(sd->inventory_data[n]) ? 2 : sd->inventory_data[n]->flag.bindOnEquip ? 1 : 0;
#endif
#if PACKETVER >= 20150226
		clif_add_random_options(p.option_data, &sd->inventory.u.items_inventory[n]);
#endif
#if PACKETVER >= 20160921
		p.favorite = sd->inventory.u.items_inventory[n].favorite;
		p.look = sd->inventory_data[n]->look;
#endif
	}

	p.PacketType = additemType;
	p.Index = client_index(n);
	p.count = amount;
	p.result = fail;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Notifies the client, that an inventory item was deleted or dropped (ZC_ITEM_THROW_ACK).
/// 00af <index>.W <amount>.W
void clif_dropitem(struct map_session_data *sd,int n,int amount) {
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0xaf));
	WFIFOW(fd,0)=0xaf;
	WFIFOW(fd,2)=n+2;
	WFIFOW(fd,4)=amount;
	WFIFOSET(fd,packet_len(0xaf));
}


/// Notifies the client, that an inventory item was deleted (ZC_DELETE_ITEM_FROM_BODY).
/// 07fa <delete type>.W <index>.W <amount>.W
/// delete type:
///     0 = Normal
///     1 = Item used for a skill
///     2 = Refine failed
///     3 = Material changed
///     4 = Moved to storage
///     5 = Moved to cart
///     6 = Item sold
///     7 = Consumed by Four Spirit Analysis (SO_EL_ANALYSIS) skill
void clif_delitem(struct map_session_data *sd,int n,int amount, short reason) {
#if PACKETVER < 20091117
	clif_dropitem(sd,n,amount);
#else
	int fd;

	nullpo_retv(sd);
	
	fd=sd->fd;
	
	WFIFOHEAD(fd, packet_len(0x7fa));
	WFIFOW(fd,0)=0x7fa;
	WFIFOW(fd,2)=reason;
	WFIFOW(fd,4)=n+2;
	WFIFOW(fd,6)=amount;
	WFIFOSET(fd,packet_len(0x7fa));
#endif
}

static void clif_item_equip(short idx, struct EQUIPITEM_INFO *p, struct item *it, struct item_data *id, int eqp_pos) {
	nullpo_retv(p);
	nullpo_retv(it);
	nullpo_retv(id);

	p->index = idx;

	p->ITID = client_nameid(it->nameid);

	p->type = itemtype(it->nameid);

#if PACKETVER < 20120925
	p->IsIdentified = it->identify ? 1 : 0;
#endif

	p->location = eqp_pos;
	p->WearState = it->equip;
#if PACKETVER < 20120925
	p->IsDamaged = it->attribute != 0 ? 1 : 0;
#endif
	p->RefiningLevel = it->refine;

	clif_addcards(&p->slot, it);

#if PACKETVER >= 20071002
	p->HireExpireDate = it->expire_time;
#endif

#if PACKETVER >= 20080102
	p->bindOnEquipType = it->bound ? 2 : id->flag.bindOnEquip ? 1 : 0;
#endif

#if PACKETVER >= 20100629
	// TODO: WBUFW(buf,n+8) = (equip == -2 && id->equip == EQP_AMMO) ? id->equip : 0;
	p->wItemSpriteNumber = (id->equip&EQP_VISIBLE) ? id->look : 0;
#endif

#if PACKETVER >= 20120925
	p->Flag.IsIdentified = it->identify ? 1 : 0;
	p->Flag.IsDamaged = it->attribute ? 1 : 0;
	p->Flag.PlaceETCTab = it->favorite ? 1 : 0;
	p->Flag.SpareBits = 0;
#endif

#if PACKETVER >= 20150226
	p->option_count = clif_add_random_options(p->option_data, it);
#endif
}

static void clif_item_normal(short idx, struct NORMALITEM_INFO *p, struct item *i, struct item_data *id) {
	nullpo_retv(p);
	nullpo_retv(i);
	nullpo_retv(id);

	p->index = idx;

	p->ITID = client_nameid(i->nameid);

	p->type = itemtype(i->nameid);

#if PACKETVER < 20120925
	p->IsIdentified = i->identify ? 1 : 0;
#endif

	p->count = i->amount;
	p->WearState = id->equip;

#if PACKETVER >= 5
	clif_addcards(&p->slot, i);
#endif

#if PACKETVER >= 20080102
	p->HireExpireDate = i->expire_time;
#endif

#if PACKETVER >= 20120925
	p->Flag.IsIdentified = i->identify ? 1 : 0;
	p->Flag.PlaceETCTab = i->favorite ? 1 : 0;
	p->Flag.SpareBits = 0;
#endif
}

static void clif_inventoryStart(struct map_session_data *sd, enum e_inventory_type type, const char *name) {
#if PACKETVER >= 20181002
	nullpo_retv(sd);
	nullpo_retv(name);

	char buf[29];
	memset(buf, 0, sizeof(buf));

	int strLen = (int)safestrnlen(name, 24) + 1;
	if (strLen > 24)
		strLen = 24;
	const int len = 5+strLen;

	WBUFW(buf, 0) = 0x0b08;
	WBUFW(buf, 2) = len;
	WBUFB(buf, 4) = type;
	safestrncpy(WBUFP(buf, 5), name, strLen);
	clif_send(buf, len, &sd->bl, SELF);
#endif
}

static void clif_inventoryEnd(struct map_session_data *sd, enum e_inventory_type type) {
#if PACKETVER >= 20181002
	nullpo_retv(sd);

	char buf[4];

	WBUFW(buf, 0) = 0x0b0b;
	WBUFB(buf, 2) = type;
	WBUFB(buf, 3) = 0;
	clif_send(buf, packet_len(0x0b0b), &sd->bl, SELF);
#endif
}

void clif_favorite_item(struct map_session_data* sd, unsigned short index);

//Unified inventory function which sends all of the inventory (requires two packets, one for equipable items and one for stackable ones. [Skotlex]
void clif_inventorylist(struct map_session_data *sd) {
	nullpo_retv(sd);

#if PACKETVER >= 20181002
	enum e_inventory_type type = INVTYPE_INVENTORY;

	clif_inventoryStart(sd, type, "");
#endif
	static struct packet_itemlist_normal itemlist_normal;
	static struct packet_itemlist_equip itemlist_equip;
	int equip = 0;
	int normal = 0;

	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].nameid == 0 || sd->inventory_data[i] == NULL) {
			continue;
		}

		// Non-stackable (Equippable)
		if (!itemdb_isstackable2(sd->inventory_data[i])) {
			clif_item_equip(client_index(i), &itemlist_equip.list[equip++], &sd->inventory.u.items_inventory[i], sd->inventory_data[i], pc_equippoint(sd, i));

			if (equip == MAX_INVENTORY_ITEM_PACKET_NORMAL) {
				itemlist_equip.PacketType = inventorylistequipType;
				itemlist_equip.PacketLength = (sizeof(itemlist_equip) - sizeof(itemlist_equip.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);
#if PACKETVER >= 20181002
				itemlist_equip.invType = type;
#endif

				clif_send(&itemlist_equip, itemlist_equip.PacketLength, &sd->bl, SELF);

				equip = 0;
			}

			// Stackable (Normal)
		}
		else {
			clif_item_normal(client_index(i), &itemlist_normal.list[normal++], &sd->inventory.u.items_inventory[i], sd->inventory_data[i]);

			if (normal == MAX_INVENTORY_ITEM_PACKET_NORMAL) {
				itemlist_normal.PacketType = inventorylistnormalType;
				itemlist_normal.PacketLength = (sizeof(itemlist_normal) - sizeof(itemlist_normal.list)) + (sizeof(struct NORMALITEM_INFO) * normal);
#if PACKETVER >= 20181002
				itemlist_normal.invType = type;
#endif

				clif_send(&itemlist_normal, itemlist_normal.PacketLength, &sd->bl, SELF);

				normal = 0;
			}
		}
	}

	if (normal) {
		itemlist_normal.PacketType = inventorylistnormalType;
		itemlist_normal.PacketLength = (sizeof(itemlist_normal) - sizeof(itemlist_normal.list)) + (sizeof(struct NORMALITEM_INFO) * normal);
#if PACKETVER >= 20181002
		itemlist_normal.invType = type;
#endif

		clif_send(&itemlist_normal, itemlist_normal.PacketLength, &sd->bl, SELF);
		}

	if (sd->equip_index[EQI_AMMO] >= 0)
		clif_arrowequip(sd, sd->equip_index[EQI_AMMO]);

	if (equip) {
		itemlist_equip.PacketType = inventorylistequipType;
		itemlist_equip.PacketLength = (sizeof(itemlist_equip) - sizeof(itemlist_equip.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);
#if PACKETVER >= 20181002
		itemlist_equip.invType = type;
#endif

		clif_send(&itemlist_equip, itemlist_equip.PacketLength, &sd->bl, SELF);
	}

#if PACKETVER >= 20181002
	clif_inventoryEnd(sd, type);
#endif
	/* on 20120925 onwards this is a field on clif_item_equip/normal */
#if PACKETVER >= 20111122 && PACKETVER < 20120925
	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].nameid == 0 || sd->inventory_data[i] == NULL)
			continue;

		if (sd->inventory.u.items_inventory[i].favorite)
			clif_favorite_item(sd, i);
	}
#endif
}

//Required when items break/get-repaired. Only sends equippable item list.
// TODO: update for newer clients later...
void clif_equiplist(struct map_session_data *sd) {
	// TODO: implement again => only send equip part
	clif_inventorylist(sd);
}

void clif_storagelist(struct map_session_data* sd, struct item* items, int items_length, const char *storename) {
#if PACKETVER >= 20181002
	enum e_inventory_type type = INVTYPE_STORAGE;

	clif_inventoryStart(sd, type, storename);
#endif
	static struct ZC_STORE_ITEMLIST_NORMAL storage_itemlist_normal;
	static struct ZC_STORE_ITEMLIST_EQUIP storage_itemlist_equip;
	int equip = 0;
	int normal = 0;

	for (int i = 0; i < items_length; i++) {
		if (items[i].nameid == 0) {
			continue;
		}

		struct item_data* id = itemdb_search(items[i].nameid);

		// Non-stackable (Equippable)
		if (!itemdb_isstackable2(id)) {
			clif_item_equip(client_storage_index(i), &storage_itemlist_equip.list[equip++], &items[i], id, pc_equippoint_sub(sd, id));

			if (equip == MAX_STORAGE_ITEM_PACKET_EQUIP) {
				storage_itemlist_equip.PacketType = storageListEquipType;
				storage_itemlist_equip.PacketLength = (sizeof(storage_itemlist_equip) - sizeof(storage_itemlist_equip.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);
#if PACKETVER >= 20181002
				storage_itemlist_equip.invType = type;
#elif PACKETVER >= 20120925
				safestrncpy(storage_itemlist_equip.name, storename, NAME_LENGTH);
#endif

				clif_send(&storage_itemlist_equip, storage_itemlist_equip.PacketLength, &sd->bl, SELF);

				equip = 0;
			}

			// Stackable (Normal)
		}
		else {
			clif_item_normal(client_storage_index(i), &storage_itemlist_normal.list[normal++], &items[i], id);

			if (normal == MAX_STORAGE_ITEM_PACKET_NORMAL) {
				storage_itemlist_normal.PacketType = storageListNormalType;
				storage_itemlist_normal.PacketLength = (sizeof(storage_itemlist_normal) - sizeof(storage_itemlist_normal.list)) + (sizeof(struct NORMALITEM_INFO) * normal);
#if PACKETVER >= 20181002
				storage_itemlist_normal.invType = type;
#elif PACKETVER >= 20120925
				safestrncpy(storage_itemlist_normal.name, storename, NAME_LENGTH);
#endif

				clif_send(&storage_itemlist_normal, storage_itemlist_normal.PacketLength, &sd->bl, SELF);

				normal = 0;
			}
		}
	}

	if (normal) {
		storage_itemlist_normal.PacketType = storageListNormalType;
		storage_itemlist_normal.PacketLength = (sizeof(storage_itemlist_normal) - sizeof(storage_itemlist_normal.list)) + (sizeof(struct NORMALITEM_INFO) * normal);
#if PACKETVER >= 20181002
		storage_itemlist_normal.invType = type;
#elif PACKETVER >= 20120925
		safestrncpy(storage_itemlist_normal.name, storename, NAME_LENGTH);
#endif

		clif_send(&storage_itemlist_normal, storage_itemlist_normal.PacketLength, &sd->bl, SELF);
	}

	if (equip) {
		storage_itemlist_equip.PacketType = storageListEquipType;
		storage_itemlist_equip.PacketLength = (sizeof(storage_itemlist_equip) - sizeof(storage_itemlist_equip.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);
#if PACKETVER >= 20181002
		storage_itemlist_equip.invType = type;
#elif PACKETVER >= 20120925
		safestrncpy(storage_itemlist_equip.name, storename, NAME_LENGTH);
#endif

		clif_send(&storage_itemlist_equip, storage_itemlist_equip.PacketLength, &sd->bl, SELF);
	}

#if PACKETVER >= 20181002
	clif_inventoryEnd(sd, type);
#endif
}

void clif_cartlist(struct map_session_data *sd) {
	nullpo_retv(sd);

#if PACKETVER >= 20181002
	enum e_inventory_type type = INVTYPE_CART;

	clif_inventoryStart(sd, type, "");
#endif
	static struct packet_itemlist_normal itemlist_normal;
	static struct packet_itemlist_equip itemlist_equip;
	int normal = 0;
	int equip = 0;

	for (int i = 0; i < MAX_CART; i++) {
		if (sd->cart.u.items_cart[i].nameid == 0) {
			continue;
		}

		struct item_data* id = itemdb_search(sd->cart.u.items_cart[i].nameid);

		// Non-stackable (Equippable)
		if (!itemdb_isstackable2(id)) {
			clif_item_equip(client_index(i), &itemlist_equip.list[equip++], &sd->cart.u.items_cart[i], id, id->equip);
			// Stackable (Normal)
	}
		else {
			clif_item_normal(client_index(i), &itemlist_normal.list[normal++], &sd->cart.u.items_cart[i], id);
		}
}

	if (normal) {
		itemlist_normal.PacketType = cartlistnormalType;
		itemlist_normal.PacketLength = (sizeof(itemlist_normal) - sizeof(itemlist_normal.list)) + (sizeof(struct NORMALITEM_INFO) * normal);
#if PACKETVER >= 20181002
		itemlist_normal.invType = type;
#endif

		clif_send(&itemlist_normal, itemlist_normal.PacketLength, &sd->bl, SELF);
	}

	if (equip) {
		itemlist_equip.PacketType = cartlistequipType;
		itemlist_equip.PacketLength = (sizeof(itemlist_equip) - sizeof(itemlist_equip.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);
#if PACKETVER >= 20181002
		itemlist_equip.invType = type;
#endif

		clif_send(&itemlist_equip, itemlist_equip.PacketLength, &sd->bl, SELF);
	}

#if PACKETVER >= 20181002
	clif_inventoryEnd(sd, type);
#endif
}

/// Removes cart (ZC_CARTOFF).
/// 012b
/// Client behaviour:
/// Closes the cart storage and removes all it's items from memory.
/// The Num & Weight values of the cart are left untouched and the cart is NOT removed.
void clif_clearcart(int fd)
{
	WFIFOHEAD(fd, packet_len(0x12b));
	WFIFOW(fd,0) = 0x12b;
	WFIFOSET(fd, packet_len(0x12b));

}


/// Guild XY locators (ZC_NOTIFY_POSITION_TO_GUILDM) [Valaris]
/// 01eb <account id>.L <x>.W <y>.W
void clif_guild_xy(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_retv(sd);

	WBUFW(buf,0)=0x1eb;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=sd->bl.x;
	WBUFW(buf,8)=sd->bl.y;
	clif_send(buf,packet_len(0x1eb),&sd->bl,GUILD_SAMEMAP_WOS);
}

/*==========================================
 * Sends x/y dot to a single fd. [Skotlex]
 *------------------------------------------*/
void clif_guild_xy_single(int fd, struct map_session_data *sd)
{
	if( sd->bg_id )
		return;

	WFIFOHEAD(fd,packet_len(0x1eb));
	WFIFOW(fd,0)=0x1eb;
	WFIFOL(fd,2)=sd->status.account_id;
	WFIFOW(fd,6)=sd->bl.x;
	WFIFOW(fd,8)=sd->bl.y;
	WFIFOSET(fd,packet_len(0x1eb));
}

// Guild XY locators [Valaris]
void clif_guild_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_retv(sd);

	WBUFW(buf,0)=0x1eb;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=-1;
	WBUFW(buf,8)=-1;
	clif_send(buf,packet_len(0x1eb),&sd->bl,GUILD_SAMEMAP_WOS);
}


/// Notifies client of a character parameter change.
/// 00b0 <var id>.W <value>.L (ZC_PAR_CHANGE)
/// 00b1 <var id>.W <value>.L (ZC_LONGPAR_CHANGE)
/// 00be <status id>.W <value>.B (ZC_STATUS_CHANGE)
/// 0121 <current count>.W <max count>.W <current weight>.L <max weight>.L (ZC_NOTIFY_CARTITEM_COUNTINFO)
/// 013a <atk range>.W (ZC_ATTACK_RANGE)
/// 0141 <status id>.L <base status>.L <plus status>.L (ZC_COUPLESTATUS)
/// 0acb <var id>.W <value>.Q (ZC_LONGPAR_CHANGE2)
int clif_updatestatus(struct map_session_data *sd,int type)
{
	int fd,len=8;

	nullpo_ret(sd);

	fd=sd->fd;

	if ( !session_isActive(fd) ) // Invalid pointer fix, by sasuke [Kevin]
		return 0;
 
	WFIFOHEAD(fd, 14);
	WFIFOW(fd,0)=0xb0;
	WFIFOW(fd,2)=type;
	switch(type){
		// 00b0
	case SP_WEIGHT:
		pc_updateweightstatus(sd);
		WFIFOW(fd,0)=0xb0;	//Need to re-set as pc_updateweightstatus can alter the buffer. [Skotlex]
		WFIFOW(fd,2)=type;
		WFIFOL(fd,4)=sd->weight;
		break;
	case SP_MAXWEIGHT:
		WFIFOL(fd,4)=sd->max_weight;
		break;
	case SP_SPEED:
		WFIFOL(fd,4)=sd->battle_status.speed;
		break;
	case SP_BASELEVEL:
		WFIFOL(fd,4)=sd->status.base_level;
		break;
	case SP_JOBLEVEL:
		WFIFOL(fd,4)=sd->status.job_level;
		break;
	case SP_KARMA: // Adding this back, I wonder if the client intercepts this - [Lance]
		WFIFOL(fd,4)=sd->status.karma;
		break;
	case SP_MANNER:
		WFIFOL(fd,4)=sd->status.manner;
		break;
	case SP_STATUSPOINT:
		WFIFOL(fd,4)=sd->status.status_point;
		break;
	case SP_SKILLPOINT:
		WFIFOL(fd,4)=sd->status.skill_point;
		break;
	case SP_HIT:
		WFIFOL(fd,4)=sd->battle_status.hit;
		break;
	case SP_FLEE1:
		WFIFOL(fd,4)=sd->battle_status.flee;
		break;
	case SP_FLEE2:
		WFIFOL(fd,4)=sd->battle_status.flee2/10;
		break;
	case SP_MAXHP:
		WFIFOL(fd,4)=sd->battle_status.max_hp;
		break;
	case SP_MAXSP:
		WFIFOL(fd,4)=sd->battle_status.max_sp;
		break;
	case SP_HP:
		// On officials the HP never go below 1, even if you die [Lemongrass]
		// On officials the HP Novice class never go below 50%, even if you die [Napster]
		WFIFOL(fd,4)= sd->battle_status.hp ? sd->battle_status.hp : (sd->class_&MAPID_UPPERMASK) != MAPID_NOVICE ? 1 : sd->battle_status.max_hp/2;
		break;
	case SP_SP:
		WFIFOL(fd,4)=sd->battle_status.sp;
		break;
	case SP_ASPD:
		WFIFOL(fd,4)=sd->battle_status.amotion;
		break;
	case SP_ATK1:
		WFIFOL(fd,4)=sd->battle_status.batk +sd->battle_status.rhw.atk +sd->battle_status.lhw.atk;
		break;
	case SP_DEF1:
		WFIFOL(fd,4)=sd->battle_status.def;
		break;
	case SP_MDEF1:
		WFIFOL(fd,4)=sd->battle_status.mdef;
		break;
	case SP_ATK2:
		WFIFOL(fd,4)=sd->battle_status.rhw.atk2 + sd->battle_status.lhw.atk2;
		break;
	case SP_DEF2:
		WFIFOL(fd,4)=sd->battle_status.def2;
		break;
	case SP_MDEF2:
		//negative check (in case you have something like Berserk active)
		len = sd->battle_status.mdef2 - (sd->battle_status.vit>>1);
		if (len < 0) len = 0;
		WFIFOL(fd,4)= len;
		len = 8;
		break;
	case SP_CRITICAL:
		WFIFOL(fd,4)=sd->battle_status.cri/10;
		break;
	case SP_MATK1:
		WFIFOL(fd,4)=sd->battle_status.matk_max;
		break;
	case SP_MATK2:
		WFIFOL(fd,4)=sd->battle_status.matk_min;
		break;
	case SP_ZENY:
		WFIFOW(fd, 0) = 0xb1;
		WFIFOL(fd, 4) = sd->status.zeny;
		break;
#if PACKETVER >= 20170830
	case SP_BASEEXP:
		WFIFOW(fd, 0) = 0xacb;
		WFIFOQ(fd, 4) = sd->status.base_exp;
		len = packet_len(0xacb);
		break;
	case SP_JOBEXP:
		WFIFOW(fd, 0) = 0xacb;
		WFIFOQ(fd, 4) = sd->status.job_exp;
		len = packet_len(0xacb);
		break;
	case SP_NEXTBASEEXP:
		WFIFOW(fd, 0) = 0xacb;
		WFIFOQ(fd, 4) = pc_nextbaseexp(sd);
		len = packet_len(0xacb);
		break;
	case SP_NEXTJOBEXP:
		WFIFOW(fd, 0) = 0xacb;
		WFIFOQ(fd, 4) = pc_nextjobexp(sd);
		len = packet_len(0xacb);
		break;
#else
	case SP_BASEEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=sd->status.base_exp;
		break;
	case SP_JOBEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=sd->status.job_exp;
		break;
	case SP_NEXTBASEEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=pc_nextbaseexp(sd);
		break;
	case SP_NEXTJOBEXP:
		WFIFOW(fd,0)=0xb1;
		WFIFOL(fd,4)=pc_nextjobexp(sd);
		break;
#endif
	
	// SP_U<STAT> are used to update the amount of points necessary to increase that stat
	case SP_USTR:
	case SP_UAGI:
	case SP_UVIT:
	case SP_UINT:
	case SP_UDEX:
	case SP_ULUK:
		WFIFOW(fd,0)=0xbe;
		WFIFOB(fd,4)=pc_need_status_point(sd,type-SP_USTR+SP_STR,1);
		len=5;
		break;

		// 013a 終了
	case SP_ATTACKRANGE:
		WFIFOW(fd,0)=0x13a;
		WFIFOW(fd,2)=sd->battle_status.rhw.range;
		len=4;
		break;

		// 0141 終了
	case SP_STR:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.str;
		WFIFOL(fd,10)=sd->battle_status.str - sd->status.str;
		len=14;
		break;
	case SP_AGI:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.agi;
		WFIFOL(fd,10)=sd->battle_status.agi - sd->status.agi;
		len=14;
		break;
	case SP_VIT:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.vit;
		WFIFOL(fd,10)=sd->battle_status.vit - sd->status.vit;
		len=14;
		break;
	case SP_INT:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.int_;
		WFIFOL(fd,10)=sd->battle_status.int_ - sd->status.int_;
		len=14;
		break;
	case SP_DEX:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.dex;
		WFIFOL(fd,10)=sd->battle_status.dex - sd->status.dex;
		len=14;
		break;
	case SP_LUK:
		WFIFOW(fd,0)=0x141;
		WFIFOL(fd,2)=type;
		WFIFOL(fd,6)=sd->status.luk;
		WFIFOL(fd,10)=sd->battle_status.luk - sd->status.luk;
		len=14;
		break;

	case SP_CARTINFO:
		WFIFOW(fd,0)=0x121;
		WFIFOW(fd,2)=sd->cart_num;
		WFIFOW(fd,4)=MAX_CART;
		WFIFOL(fd,6)=sd->cart_weight;
		WFIFOL(fd, 10) = sd->cart_weight_max;
		len=14;
		break;

	default:
		ShowError("clif_updatestatus : unrecognized type %d\n",type);
		return 1;
	}
	WFIFOSET(fd,len);

	// Additional update packets that should be sent right after
	switch (type) 
	{
		case SP_BASELEVEL:
			pc_update_job_and_level(sd);
			break;
		case SP_HP:
			if (battle_config.disp_hpmeter)
				clif_hpmeter(sd);
			if (!battle_config.party_hp_mode && sd->status.party_id)
				clif_party_hp(sd);
			if (sd->bg_id)
				clif_bg_hp(sd);
			break;
	}

	return 0;
}

void clif_changestatus(struct map_session_data* sd,int type,int val)
{
	unsigned char buf[12];

	nullpo_retv(sd);

	WBUFW(buf, 0) = 0x1ab;
	WBUFL(buf, 2) = sd->bl.id;
	WBUFW(buf, 6) = type;

	switch (type)
	{
		case SP_MANNER:
			WBUFL(buf,8)=val;
			break;
		default:
			ShowError("clif_changestatus : unrecognized type %d.\n",type);
			return;
	}
	clif_send(buf, packet_len(0x1ab), &sd->bl, AREA_WOS);
}

/// Notifies client of a change in a long parameter (ZC_LONGPAR_CHANGE).
/// 00b1 <var id>.W <value>.L
void clif_updatelongparam(struct map_session_data* sd, short type, int value)
{
	int fd;

	switch( type )
	{
	case SP_BASEEXP:
	case SP_JOBEXP:
	case SP_ZENY: // cancel trade
	case SP_NEXTBASEEXP:
	case SP_NEXTJOBEXP:
		// expected
	break;
	default:
		ShowWarning("clif_updatelongparam: unexpected type (type=%d, value=%d)", type, value);
	break;
	}

	if( sd == NULL || !session_isActive(sd->fd) )
		return; // no client

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0xb1));
	WFIFOW(fd,0) = 0xb1;
	WFIFOW(fd,2) = type;
	WFIFOL(fd,4) = value;
	WFIFOSET(fd, packet_len(0xb1));
}


/// Notifies client of a change in the number of status points needed (ZC_STATUS_CHANGE).
/// 00be <status id>.W <value>.B
void clif_updatestatuspointsneeded(struct map_session_data* sd, short type, unsigned char value)
{
	int fd;

	switch( type )
	{
	case SP_USTR:
	case SP_UAGI:
	case SP_UVIT:
	case SP_UINT:
	case SP_UDEX:
	case SP_ULUK:
		// expected
	break;
	default:
		ShowWarning("clif_updatestatuspointsneeded: unexpected type (type=%d, value=%d)", type, value);
	break;
	}

	if( sd == NULL || !session_isActive(sd->fd) )
		return; // no client

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0xbe));
	WFIFOW(fd,0) = 0xbe;
	WFIFOW(fd,2) = type;
	WFIFOB(fd,4) = value;
	WFIFOSET(fd, packet_len(0xbe));
}


/// Notifies client of a change in the cart info (ZC_NOTIFY_CARTITEM_COUNTINFO).
/// 0121 <current count>.W <max count>.W <current weight>.L <max weight>.L
void clif_updatecartinfo(struct map_session_data* sd, short count, short maxcount, int weight, int maxweight)
{
	int fd;

	if( sd == NULL || !session_isActive(sd->fd) )
		return; // no client

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x121));
	WFIFOW(fd,0) = 0x121;
	WFIFOW(fd,2) = count;
	WFIFOW(fd,4) = maxcount;
	WFIFOL(fd,6) = weight;
	WFIFOL(fd,10) = maxweight;
	WFIFOSET(fd, packet_len(0x121));
}


/// Notifies client of a change in the attack range (ZC_ATTACK_RANGE).
/// 013a <atk range>.W
void clif_updateattackrange(struct map_session_data* sd, short range)
{
	int fd;

	if( sd == NULL || !session_isActive(sd->fd) )
		return; // no client

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x13a));
	WFIFOW(fd,0) = 0x13a;
	WFIFOW(fd,2) = range;
	WFIFOSET(fd, packet_len(0x13a));
}


/// Notifies client of a change in a stat (ZC_COUPLESTATUS).
/// 0141 <status id>.L <base status>.L <plus status>.L
void clif_updatestat(struct map_session_data* sd, int type, int value, int plusvalue)
{
	int fd;

	switch( type )
	{
	case SP_STR:
	case SP_AGI:
	case SP_VIT:
	case SP_INT:
	case SP_DEX:
	case SP_LUK:
		// expected
	break;
	default:
		ShowWarning("clif_updatestat: unexpected type (type=%d, value=%d, plusvalue=%d)", type, value, plusvalue);
	break;
	}

	if( sd == NULL || !session_isActive(sd->fd) )
		return; // no client

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x141));
	WFIFOW(fd,0) = 0x141;
	WFIFOL(fd,2) = type;
	WFIFOL(fd,6) = value;
	WFIFOL(fd,10) = plusvalue;
	WFIFOSET(fd, packet_len(0x141));
}


/// Notifies clients in the area of a change in a parameter of an another player (ZC_PAR_CHANGE_USER).
/// 01ab <account id>.L <var id>.W <value>.L
void clif_updateparam_area(struct map_session_data* sd, short type, int value)
{
	unsigned char buf[12];

	nullpo_retv(sd);

	switch(type)
	{
	case SP_MANNER:
		// expected
	break;
	default:
		ShowWarning("clif_updateparam_area: unexpected type (type=%d, value=&d).\n", type, value);
	break;
	}

	WBUFW(buf,0) = 0x1ab;
	WBUFL(buf,2) = sd->bl.id;
	WBUFW(buf,6) = type;
	WBUFL(buf,8) = value;

	clif_send(buf,packet_len(0x1ab),&sd->bl,AREA_WOS);
}


/// 00c3 <id>.L <type>.B <value>.B (ZC_SPRITE_CHANGE)
/// 01d7 <id>.L <type>.B <value>.L (ZC_SPRITE_CHANGE2)
void clif_sprite_change(struct block_list *bl, int id, int type, int val, int val2, enum send_target target) {
	struct PACKET_ZC_SPRITE_CHANGE p;

	p.packetType = sendLookType;
	p.AID = id;
	p.type = type;
	p.val = val;
#if PACKETVER >= 4
	p.val2 = val2;
#endif

	clif_send(&p, sizeof(p), bl, target);
}


/// Updates sprite/style properties of an object.
void clif_changelook(struct block_list *bl, int type, int val) {
	struct map_session_data* sd;
	struct status_change* sc;
	struct view_data* vd;
	enum send_target target = AREA;
	int val2 = 0;
	nullpo_retv(bl);

	sd = BL_CAST(BL_PC, bl);
	sc = status_get_sc(bl);
	vd = status_get_viewdata(bl);
	if( vd ) //temp hack to let Warp Portal change appearance
	switch(type)
	{
		case LOOK_WEAPON:
			if (sd)
			{
				clif_get_weapon_view(sd, &vd->weapon, &vd->shield);
				val = vd->weapon;
			}
			else vd->weapon = val;
		break;
		case LOOK_SHIELD:
			if (sd)
			{
				clif_get_weapon_view(sd, &vd->weapon, &vd->shield);
				val = vd->shield;
			}
			else vd->shield = val;
		break;
		case LOOK_BASE:
			if (!sd) break;

			if (val == INVISIBLE_CLASS)
				return;

			if (sd->sc.option&OPTION_COSTUME)
				vd->weapon = vd->shield = 0;

			if (!vd->cloth_color)
				break;

			if (sd) {
				if (sd->sc.option&OPTION_WEDDING && battle_config.wedding_ignorepalette)
					vd->cloth_color = 0;
				if (sd->sc.option&OPTION_XMAS && battle_config.xmas_ignorepalette)
					vd->cloth_color = 0;
				if (sd->sc.option&OPTION_SUMMER && battle_config.summer_ignorepalette)
					vd->cloth_color = 0;
				if (sd->sc.option&OPTION_HANBOK && battle_config.hanbok_ignorepalette)
					vd->cloth_color = 0;
				if (sd->sc.option&OPTION_OKTOBERFEST && battle_config.oktoberfest_ignorepalette)
					vd->cloth_color = 0;
				if (sd->sc.option&OPTION_SUMMER2 && battle_config.summer2_ignorepalette)
					vd->cloth_color = 0;
				if (vd->body_style && (
					sd->sc.option&OPTION_WEDDING || sd->sc.option&OPTION_XMAS ||
					sd->sc.option&OPTION_SUMMER || sd->sc.option&OPTION_HANBOK ||
					sd->sc.option&OPTION_OKTOBERFEST))
					vd->body_style = 0;
			}
		break;
		case LOOK_HAIR:
			vd->hair_style = val;
		break;
		case LOOK_HEAD_BOTTOM:
			vd->head_bottom = val;
		break;
		case LOOK_HEAD_TOP:
			vd->head_top = val;
		break;	
		case LOOK_HEAD_MID:
			vd->head_mid = val;
		break;
		case LOOK_HAIR_COLOR:
			vd->hair_color = val;
		break;
		case LOOK_CLOTHES_COLOR:
			if (val && sd) {
				if (sd->sc.option&OPTION_WEDDING && battle_config.wedding_ignorepalette)
					val = 0;
				if (sd->sc.option&OPTION_XMAS && battle_config.xmas_ignorepalette)
					val = 0;
				if (sd->sc.option&OPTION_SUMMER && battle_config.summer_ignorepalette)
					val = 0;
				if (sd->sc.option&OPTION_HANBOK && battle_config.hanbok_ignorepalette)
					val = 0;
				if (sd->sc.option&OPTION_OKTOBERFEST && battle_config.oktoberfest_ignorepalette)
					val = 0;
				if (sd->sc.option&OPTION_SUMMER2 && battle_config.summer2_ignorepalette)
					val = 0;
			}
			vd->cloth_color = val;
		break;
		case LOOK_SHOES:
#if PACKETVER > 3
			if (sd) {
				int n;
				if((n = sd->equip_index[EQI_SHOES]) >= 0 && sd->inventory_data[n]) {
					if(sd->inventory_data[n]->view_id > 0)
						val = sd->inventory_data[n]->view_id;
					else
						val = sd->inventory.u.items_inventory[n].nameid;
				} else
					val = 0;
			}
#endif
			//Shoes? No packet uses this....
		break;
		case LOOK_COSTUMEBODY:
		case LOOK_RESET_COSTUMES:
			// unknown purpose
		break;
		case LOOK_ROBE:
#if PACKETVER < 20110111
			return;
#else
			vd->robe = val;
#endif
		break;
		case LOOK_BODY2:
#if PACKETVER < 20150513
			return;
#else
 			vd->body_style = val;
#endif
		break;
	}

	// prevent leaking the presence of GM-hidden objects
	if( sc && sc->option&OPTION_INVISIBLE )
		target = SELF;

#if PACKETVER < 4
	clif_sprite_change(bl, bl->id, type, val, 0, target);
#else
	if(type == LOOK_WEAPON || type == LOOK_SHIELD) {
		nullpo_retv(vd);
		type = LOOK_WEAPON;
		val = vd->weapon;
		val2 = vd->shield;
	}
	if (disguised(bl)) {
		clif_sprite_change(bl, bl->id, type, val, val2, AREA_WOS);
		clif_sprite_change(bl, -bl->id, type, val, val2, SELF);
	}
	else
		clif_sprite_change(bl, bl->id, type, val, val2, target);
	unit_refresh(bl);
#endif
}

//Sends a change-base-look packet required for traps as they are triggered.
void clif_changetraplook(struct block_list *bl, int val) {
	clif_sprite_change(bl, bl->id, LOOK_BASE, val, 0, AREA);
}

/// For the stupid cloth-dye bug. Resends the given view data to the area specified by bl.
void clif_refreshlook(struct block_list *bl, int id, int type, int val, enum send_target target) {
	clif_sprite_change(bl, id, type, val, 0, target);
}

/// Character status (ZC_STATUS).
/// 00bd <stpoint>.W <str>.B <need str>.B <agi>.B <need agi>.B <vit>.B <need vit>.B
///     <int>.B <need int>.B <dex>.B <need dex>.B <luk>.B <need luk>.B <atk>.W <atk2>.W
///     <matk min>.W <matk max>.W <def>.W <def2>.W <mdef>.W <mdef2>.W <hit>.W
///     <flee>.W <flee2>.W <crit>.W <aspd>.W <aspd2>.W
void clif_initialstatus(struct map_session_data *sd)
{
	int fd, mdef2;
	unsigned char *buf;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xbd));
	buf=WFIFOP(fd,0);

	WBUFW(buf,0)=0xbd;
	WBUFW(buf,2)=min(sd->status.status_point, INT16_MAX);
	WBUFB(buf,4)=min(sd->status.str, UINT8_MAX);
	WBUFB(buf,5)=pc_need_status_point(sd,SP_STR,1);
	WBUFB(buf,6)=min(sd->status.agi, UINT8_MAX);
	WBUFB(buf,7)=pc_need_status_point(sd,SP_AGI,1);
	WBUFB(buf,8)=min(sd->status.vit, UINT8_MAX);
	WBUFB(buf,9)=pc_need_status_point(sd,SP_VIT,1);
	WBUFB(buf,10)=min(sd->status.int_, UINT8_MAX);
	WBUFB(buf,11)=pc_need_status_point(sd,SP_INT,1);
	WBUFB(buf,12)=min(sd->status.dex, UINT8_MAX);
	WBUFB(buf,13)=pc_need_status_point(sd,SP_DEX,1);
	WBUFB(buf,14)=min(sd->status.luk, UINT8_MAX);
	WBUFB(buf,15)=pc_need_status_point(sd,SP_LUK,1);

	WBUFW(buf,16) = sd->battle_status.batk + sd->battle_status.rhw.atk + sd->battle_status.lhw.atk;
	WBUFW(buf,18) = sd->battle_status.rhw.atk2 + sd->battle_status.lhw.atk2; //atk bonus
	WBUFW(buf,20) = sd->battle_status.matk_max;
	WBUFW(buf,22) = sd->battle_status.matk_min;
	WBUFW(buf,24) = sd->battle_status.def; // def
	WBUFW(buf,26) = sd->battle_status.def2;
	WBUFW(buf,28) = sd->battle_status.mdef; // mdef
	mdef2 = sd->battle_status.mdef2 - (sd->battle_status.vit>>1);
	WBUFW(buf,30) = ( mdef2 < 0 ) ? 0 : mdef2;  //Negative check for Frenzy'ed characters.
	WBUFW(buf,32) = sd->battle_status.hit;
	WBUFW(buf,34) = sd->battle_status.flee;
	WBUFW(buf,36) = sd->battle_status.flee2/10;
	WBUFW(buf,38) = sd->battle_status.cri/10;
	WBUFW(buf,40) = sd->battle_status.amotion; // aspd
	WBUFW(buf,42) = 0;  // always 0 (plusASPD)

	WFIFOSET(fd,packet_len(0xbd));

	clif_updatestatus(sd,SP_STR);
	clif_updatestatus(sd,SP_AGI);
	clif_updatestatus(sd,SP_VIT);
	clif_updatestatus(sd,SP_INT);
	clif_updatestatus(sd,SP_DEX);
	clif_updatestatus(sd,SP_LUK);

	clif_updatestatus(sd,SP_ATTACKRANGE);
	clif_updatestatus(sd,SP_ASPD);
}


/// Marks an ammunition item in inventory as equipped (ZC_EQUIP_ARROW).
/// 013c <index>.W
void clif_arrowequip(struct map_session_data *sd,int val)
{
	int fd;

	nullpo_retv(sd);

#if PACKETVER >= 20121128
	clif_status_change(&sd->bl, SI_CLIENT_ONLY_EQUIP_ARROW, 1, INVALID_TIMER, 0, 0, 0);
#endif

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x013c));
	WFIFOW(fd,0)=0x013c;
	WFIFOW(fd,2)=val+2;//Item ID of the arrow
	WFIFOSET(fd,packet_len(0x013c));
}


/// Ammunition action message (ZC_ACTION_FAILURE).
/// 013b <type>.W
/// type:
///     0 = MsgStringTable[242]="Please equip the proper ammunition first."
///     1 = MsgStringTable[243]="You can't Attack or use Skills because your Weight Limit has been exceeded."
///     2 = MsgStringTable[244]="You can't use Skills because Weight Limit has been exceeded."
///     3 = assassin, baby_assassin, assassin_cross => MsgStringTable[1040]="You have equipped throwing daggers."
///         gunslinger => MsgStringTable[1175]="Bullets have been equipped."
///         NOT ninja => MsgStringTable[245]="Ammunition has been equipped."
void clif_arrow_fail(struct map_session_data *sd,int type)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd, packet_len(0x013b));
	WFIFOW(fd,0)=0x013b;
	WFIFOW(fd,2)=type;
	WFIFOSET(fd,packet_len(0x013b));
}


/// Presents a list of items, that can be processed by Arrow Crafting (ZC_MAKINGARROW_LIST).
/// 01ad <packet len>.W { <name id>.W }*
void clif_arrow_create_list(struct map_session_data *sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + MAX_SKILL_ARROW_DB * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub));
	struct PACKET_ZC_MAKINGARROW_LIST *p = (struct PACKET_ZC_MAKINGARROW_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x01ad;

	int count = 0;
	for (int i = 0; i < MAX_SKILL_ARROW_DB; i++) {
		t_itemid nameid = skill_arrow_db[i].nameid;

		if (!itemdb_exists(nameid)) {
			continue;
		}

		int index = pc_search_inventory(sd, nameid);

		if (index < 0) {
			continue;
		}

		if (sd->inventory.u.items_inventory[index].equip) {
			continue;
		}

		if (!sd->inventory.u.items_inventory[index].identify) {
			continue;
		}

		p->items[count].itemId = client_nameid(nameid);
		count++;
	}

	p->packetLength = sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + count * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub);
	WFIFOSET(fd, p->packetLength);

	if (count > 0) {
		sd->menuskill_id = AC_MAKINGARROW;
		sd->menuskill_val = count;
	}
}

/*==========================================
 * Server tells client to display a window similar to Magnifier (item) one
 * Server populates the window with avilable elemental converter options according to player's inventory
 *------------------------------------------*/
void clif_elementalconverter_list(struct map_session_data *sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + MAX_SKILL_ARROW_DB * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub));
	struct PACKET_ZC_MAKINGARROW_LIST *p = (struct PACKET_ZC_MAKINGARROW_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x01ad;

	int count = 0;
	for (int i = 0; i < MAX_SKILL_PRODUCE_DB; i++) {
		if (skill_can_produce_mix(sd, skill_produce_db[i].nameid, 23, 1)) {
			p->items[count].itemId = client_nameid(skill_produce_db[i].nameid);
			count++;
		}
	}

	p->packetLength = sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + count * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub);
	WFIFOSET(fd, p->packetLength);

	if (count > 0) {
		sd->menuskill_id = SA_CREATECON;
		sd->menuskill_val = count;
	}
}

/*==========================================
 * Guillotine Cross Poisons List
 *------------------------------------------*/
void clif_poison_list(struct map_session_data *sd, uint16 skill_lv) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + MAX_INVENTORY * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub));
	struct PACKET_ZC_MAKINGARROW_LIST *p = (struct PACKET_ZC_MAKINGARROW_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x01ad;

	int count = 0;
	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (itemid_is_guillotine_poison(sd->inventory.u.items_inventory[i].nameid)) {
			p->items[count].itemId = client_nameid(sd->inventory.u.items_inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		p->packetLength = sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + count * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub);
		WFIFOSET(fd, p->packetLength);
		sd->menuskill_id = GC_POISONINGWEAPON;
		sd->menuskill_val = skill_lv;
	}
	else {
		clif_skill_fail(sd, GC_POISONINGWEAPON, USESKILL_FAIL_GUILLONTINE_POISON, 0, 0);
	}
}

/*==========================================
 * Magic Decoy Material List
 *------------------------------------------*/
void clif_magicdecoy_list(struct map_session_data *sd, uint16 skill_lv, short x, short y) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + MAX_INVENTORY * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub));
	struct PACKET_ZC_MAKINGARROW_LIST *p = (struct PACKET_ZC_MAKINGARROW_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x01ad;

	int count = 0;
	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (itemid_is_element_point(sd->inventory.u.items_inventory[i].nameid)) {
			p->items[count].itemId = client_nameid(sd->inventory.u.items_inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		p->packetLength = sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + count * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub);
		WFIFOSET(fd, p->packetLength);
		sd->menuskill_id = NC_MAGICDECOY;
		sd->menuskill_val = skill_lv;
		sd->sc.comet_x = x;
		sd->sc.comet_y = y;
	}
	else {
		clif_skill_fail(sd, NC_MAGICDECOY, USESKILL_FAIL_LEVEL, 0, 0);
	}
}

/*==========================================
 * Spellbook list [LimitLine]
 *------------------------------------------*/
void clif_spellbook_list(struct map_session_data *sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + MAX_INVENTORY * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub));
	struct PACKET_ZC_MAKINGARROW_LIST *p = (struct PACKET_ZC_MAKINGARROW_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x01ad;

	int count = 0;
	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (itemid_is_spell_book(sd->inventory.u.items_inventory[i].nameid)) {
			p->items[count].itemId = client_nameid(sd->inventory.u.items_inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		p->packetLength = sizeof(struct PACKET_ZC_MAKINGARROW_LIST) + count * sizeof(struct PACKET_ZC_MAKINGARROW_LIST_sub);
		WFIFOSET(fd, p->packetLength);
		sd->menuskill_id = WL_READING_SB;
		sd->menuskill_val = count;
	}
	else {
		status_change_end(&sd->bl, SC_STOP, INVALID_TIMER);
		clif_skill_fail(sd, WL_READING_SB, USESKILL_FAIL_SPELLBOOK, 0, 0);
	}
}

/*===========================================
 * Skill list for Auto Shadow Spell
 *------------------------------------------*/
int clif_skill_select_request(struct map_session_data *sd)
{
	int fd, i, c;
	nullpo_ret(sd);
	fd = sd->fd;
	if( !fd ) return 0;

	if( sd->menuskill_id == SC_AUTOSHADOWSPELL )
		return 0;

	WFIFOHEAD(fd, 2 * 6 + 4);
	WFIFOW(fd,0) = 0x442;
	for( i = 0, c = 0; i < MAX_SKILL; i++ )
		if ((sd->status.skill[i].id == sd->status.skill[sd->cloneskill_idx].id || sd->status.skill[i].id == sd->status.skill[sd->reproduceskill_idx].id) && 
			(sd->status.skill[i].id == MG_NAPALMBEAT || 
			sd->status.skill[i].id >= MG_SOULSTRIKE && sd->status.skill[i].id <= MG_FROSTDIVER || 
			sd->status.skill[i].id >= MG_FIREBALL && sd->status.skill[i].id <= MG_THUNDERSTORM || 
			sd->status.skill[i].id == AL_HEAL || sd->status.skill[i].id == WZ_FIREPILLAR || sd->status.skill[i].id == WZ_SIGHTRASHER || 
			sd->status.skill[i].id >= WZ_METEOR && sd->status.skill[i].id <= WZ_WATERBALL || 
			sd->status.skill[i].id >= WZ_FROSTNOVA && sd->status.skill[i].id <= WZ_HEAVENDRIVE))
		{
			WFIFOW(fd,8+c*2) = sd->status.skill[i].id;
			c++;
		}

	if( c > 0 )
	{
		WFIFOW(fd,2) = 8 + c * 2;
		WFIFOL(fd,4) = c;
		WFIFOSET(fd,WFIFOW(fd,2));
		sd->menuskill_id = SC_AUTOSHADOWSPELL;
		sd->menuskill_val = c;
	}
	else
	{
		status_change_end(&sd->bl,SC_STOP,-1);
		clif_skill_fail(sd,SC_AUTOSHADOWSPELL,USESKILL_FAIL_LEVEL,0,0);
	}

	return 1;
}

/*===========================================
 * Skill list for Four Elemental Analysis
 * and Change Material skills.
 *------------------------------------------*/
int clif_skill_itemlistwindow( struct map_session_data *sd, int skill_id, int skill_lv )
{
#if PACKETVER >= 20090922
	int fd;

	nullpo_retr(0,sd);

	sd->menuskill_id = skill_id; // To prevent hacking.
	sd->menuskill_val = skill_lv;

	if( skill_id == GN_CHANGEMATERIAL )
		skill_lv = 0; // Changematerial

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x7e3));
	WFIFOW(fd,0) = 0x7e3;
	WFIFOL(fd,2) = skill_lv;
	WFIFOL(fd,4) = 0;
	WFIFOSET(fd,packet_len(0x7e3));

#endif

	return 1;

}

/// Notifies the client, about the result of an status change request (ZC_STATUS_CHANGE_ACK).
/// 00bc <status id>.W <result>.B <value>.B
/// status id:
///     SP_STR ~ SP_LUK
/// result:
///     0 = failure
///     1 = success
void clif_statusupack(struct map_session_data *sd,int type,int ok,int val)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xbc));
	WFIFOW(fd,0)=0xbc;
	WFIFOW(fd,2)=type;
	WFIFOB(fd,4)=ok;
	WFIFOB(fd,5)=cap_value(val,0,UINT8_MAX);
	WFIFOSET(fd,packet_len(0xbc));
}


/// Notifies the client about the result of a request to equip an item (ZC_REQ_WEAR_EQUIP_ACK).
/// 00aa <index>.W <equip location>.W <result>.B
/// 00aa <index>.W <equip location>.W <view id>.W <result>.B (PACKETVER >= 20100629)
/// 08d0 <index>.W <equip location>.W <view id>.W <result>.B (ZC_REQ_WEAR_EQUIP_ACK2)
/// 0999 <index>.W <equip location>.L <view id>.W <result>.B (ZC_ACK_WEAR_EQUIP_V5)
/// 0xaa Result Table:
///     0 = failure
///     1 = success
///     2 = failure due to low level
/// --------------------------------
/// 0x999 / 0x8d0 Result Table:
///     0 = SUCCESS
///     1 = FAIL_FORBID
///     2 = FAILURE
void clif_equipitemack(struct map_session_data *sd,int n,int pos,uint8 flag)
{
	int fd, header, offs = 0;
#if PACKETVER < 20110824
	header = 0xaa;
#elif PACKETVER < 20120925
	header = 0x8d0;
#else
	header = 0x999;
#endif
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(header));
	WFIFOW(fd, offs + 0) = header;
	WFIFOW(fd, offs + 2) = n + 2;
#if PACKETVER >= 20120925
	WFIFOL(fd, offs + 4) = pos;
	offs += 2;
#else
	WFIFOW(fd, offs + 4) = (int)pos;
#endif
#if PACKETVER < 20100629
	WFIFOB(fd, offs + 6) = flag;
#else
	if (flag == ITEM_EQUIP_ACK_OK && sd->inventory_data[n]->equip&EQP_VISIBLE)
		WFIFOW(fd, offs + 6) = sd->inventory_data[n]->look;
	else
		WFIFOW(fd, offs + 6) = 0;
	WFIFOB(fd, offs + 8) = flag;
#endif
	WFIFOSET(fd, packet_len(header));
}

/// Notifies the client about the result of a request to take off an item.
/// 00ac <index>.W <equip location>.W <result>.B (ZC_REQ_TAKEOFF_EQUIP_ACK)
/// 08d1 <index>.W <equip location>.W <result>.B (ZC_REQ_TAKEOFF_EQUIP_ACK2)
/// 099a <index>.W <equip location>.L <result>.B (ZC_ACK_TAKEOFF_EQUIP_V5)
/// @ok : //inversed for v2 v5
///     0 = failure
///     1 = success
void clif_unequipitemack(struct map_session_data *sd,int n,int pos,int ok) {
	int fd, cmd, offs = 0;
#if PACKETVER >= 20130000
	cmd = 0x99a;
	ok = ok ? 0 : 1;
#elif PACKETVER >= 20110824
	cmd = 0x8d1;
	ok = ok ? 0 : 1;
#else
	cmd = 0xac;
#endif
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd,0)=cmd;
	WFIFOW(fd,2)=n+2;
#if PACKETVER >= 20130000
	WFIFOL(fd,4)=pos;
	offs += 2;
#else
	WFIFOW(fd,4)=pos;
#endif
	WFIFOB(fd,offs+6)=ok;
	WFIFOSET(fd,packet_len(cmd));
}

/// Notifies clients in the area about an special/visual effect (ZC_NOTIFY_EFFECT).
/// 019b <id>.L <effect id>.L
/// effect id:
///     0 = base level up
///     1 = job level up
///     2 = refine failure
///     3 = refine success
///     4 = game over
///     5 = pharmacy success
///     6 = pharmacy failure
///     7 = base level up (super novice)
///     8 = job level up (super novice)
///     9 = base level up (taekwon)
void clif_misceffect(struct block_list* bl,int type)
{
	unsigned char buf[32];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x19b;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = type;

	clif_send(buf,packet_len(0x19b),bl,AREA);
}


/// Notifies clients in the area of a state change.
/// 0119 <id>.L <body state>.W <health state>.W <effect state>.W <pk mode>.B (ZC_STATE_CHANGE)
/// 0229 <id>.L <body state>.W <health state>.W <effect state>.L <pk mode>.B (ZC_STATE_CHANGE3)
void clif_changeoption(struct block_list* bl)
{
	unsigned char buf[32];
	struct map_session_data* sd;

	nullpo_retv(bl);
	
	struct status_change *sc = status_get_sc(bl);
	
	if (sc == NULL && bl->type != BL_NPC) // How can an option change if there's no sc?
		return;

	sd = BL_CAST(BL_PC, bl);
	
#if PACKETVER >= 7
	WBUFW(buf,0) = 0x229;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf, 6) = (sc != NULL) ? sc->opt1 : 0;
	WBUFW(buf, 8) = (sc != NULL) ? sc->opt2 : 0;
	WBUFL(buf, 10) = (sc != NULL) ? sc->option : BL_UCCAST(BL_NPC, bl)->sc.option;
	WBUFB(buf,14) = (sd)? sd->status.karma : 0;
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x229),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x229),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFL(buf,10) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x229),bl,SELF);
	} else
		clif_send(buf,packet_len(0x229),bl,AREA);
#else
	WBUFW(buf,0) = 0x119;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = (sc != NULL) ? sc->opt1 : 0;
	WBUFW(buf,8) = (sc != NULL) ? sc->opt2 : 0;
	WBUFW(buf,10) = (sc != NULL) ? sc->option : BL_UCCAST(BL_NPC, bl)->sc.option;
	WBUFB(buf,12) = (sd)? sd->status.karma : 0;
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x119),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x119),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFW(buf,10) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x119),bl,SELF);
	} else
		clif_send(buf,packet_len(0x119),bl,AREA);
#endif

	//Whenever we send "changeoption" to the client, the provoke icon is lost
	//There is probably an option for the provoke icon, but as we don't know it, we have to do this for now
	if (sc->data[SC_PROVOKE]) {
		const struct TimerData *td = get_timer(sc->data[SC_PROVOKE]->timer);

		clif_status_change(bl, StatusIconChangeTable[SC_PROVOKE], 1, (!td ? -1 : DIFF_TICK(td->tick, gettick())), 0, 0, 0);
	}
}

void clif_changeoption_target(struct block_list *bl, struct block_list *target_bl, enum send_target target)
{
	unsigned char buf[32];
	struct map_session_data* sd;

	nullpo_retv(bl);

	struct status_change *sc = status_get_sc(bl);

	if (sc == NULL && bl->type != BL_NPC) // How can an option change if there's no sc?
		return;

	sd = BL_CAST(BL_PC, bl);
#if PACKETVER >= 7
	WBUFW(buf, 0) = 0x229;
	WBUFL(buf, 2) = bl->id;
	WBUFW(buf, 6) = (sc != NULL) ? sc->opt1 : 0;
	WBUFW(buf, 8) = (sc != NULL) ? sc->opt2 : 0;
	WBUFL(buf, 10) = (sc != NULL) ? sc->option : BL_UCCAST(BL_NPC, bl)->sc.option;
	WBUFB(buf, 14) = (sd) ? sd->status.karma : 0;
	if (disguised(bl)) {
		WBUFL(buf, 2) = -bl->id;
		clif_send(buf, packet_len(0x229), target_bl, target);
		WBUFL(buf, 2) = bl->id;
		WBUFL(buf, 10) = OPTION_INVISIBLE;
	}
	clif_send(buf, packet_len(0x229), target_bl, target);
#else
	WBUFW(buf, 0) = 0x119;
	WBUFL(buf, 2) = bl->id;
	WBUFW(buf, 6) = (sc != NULL) ? sc->opt1 : 0;
	WBUFW(buf, 8) = (sc != NULL) ? sc->opt2 : 0;
	WBUFW(buf, 10) = (sc != NULL) ? sc->option : BL_UCCAST(BL_NPC, bl)->sc.option;
	WBUFB(buf, 12) = (sd) ? sd->status.karma : 0;
	if (disguised(bl)) {
		WBUFL(buf, 2) = -bl->id;
		clif_send(buf, packet_len(0x119), target_bl, target);
		WBUFL(buf, 2) = bl->id;
		WBUFL(buf, 10) = OPTION_INVISIBLE;
	}
	clif_send(buf, packet_len(0x119), target_bl, target);
#endif
}


/// Displays status change effects on NPCs/monsters (ZC_NPC_SHOWEFST_UPDATE).
/// 028a <id>.L <effect state>.L <level>.L <showEFST>.L
void clif_changeoption2(struct block_list* bl)
{
	unsigned char buf[20];
	struct status_change *sc;
	
	sc = status_get_sc(bl);
	if (!sc) return; //How can an option change if there's no sc?

	WBUFW(buf,0) = 0x28a;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = sc->option;
	WBUFL(buf,10) = clif_setlevel(bl);
	WBUFL(buf,14) = sc->opt3;
	if(disguised(bl)) {
		clif_send(buf,packet_len(0x28a),bl,AREA_WOS);
		WBUFL(buf,2) = -bl->id;
		clif_send(buf,packet_len(0x28a),bl,SELF);
		WBUFL(buf,2) = bl->id;
		WBUFL(buf,6) = OPTION_INVISIBLE;
		clif_send(buf,packet_len(0x28a),bl,SELF);
	} else
		clif_send(buf,packet_len(0x28a),bl,AREA);
}


/// Notifies the client about the result of an item use request.
/// 00a8 <index>.W <amount>.W <result>.B (ZC_USE_ITEM_ACK)
/// 01c8 <index>.W <name id>.W <id>.L <amount>.W <result>.B (ZC_USE_ITEM_ACK2)
void clif_useitemack(struct map_session_data *sd, int index, int amount, bool ok) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	if (index < 0 || index >= MAX_INVENTORY || sd->inventory.u.items_inventory[index].nameid == 0 || sd->inventory_data[index] == NULL) {
		return;
	}

	struct PACKET_ZC_USE_ITEM_ACK p;

	p.packetType = useItemAckType;
	p.index = index + 2;
#if PACKETVER > 3
	p.itemId = client_nameid(sd->inventory.u.items_inventory[index].nameid);
	p.AID = sd->bl.id;
#endif
	p.amount = amount;
	p.result = ok;

	if (!ok) {
		clif_send(&p, sizeof(p), &sd->bl, SELF);
	}
	else {
		clif_send(&p, sizeof(p), &sd->bl, AREA);
	}
}


/// Inform client whether chatroom creation was successful or not (ZC_ACK_CREATE_CHATROOM).
/// 00d6 <flag>.B
/// flag:
///     0 = Room has been successfully created (opens chat room)
///     1 = Room limit exceeded
///     2 = Same room already exists
void clif_createchat(struct map_session_data* sd, int flag)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xd6));
	WFIFOW(fd,0) = 0xd6;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,packet_len(0xd6));
}


/// Display a chat above the owner (ZC_ROOM_NEWENTRY).
/// 00d7 <packet len>.W <owner id>.L <char id>.L <limit>.W <users>.W <type>.B <title>.?B
/// type:
///     0 = private (password protected)
///     1 = public
///     2 = arena (npc waiting room)
///     3 = PK zone (non-clickable)
void clif_dispchat(struct chat_data* cd, int fd)
{
	unsigned char buf[128];
	uint8 type;

	if( cd == NULL || cd->owner == NULL )
		return;

	type = (cd->owner->type == BL_PC ) ? (cd->pub) ? 1 : 0
	     : (cd->owner->type == BL_NPC) ? (cd->limit) ? 2 : 3
	     : 1;

	WBUFW(buf, 0) = 0xd7;
	WBUFW(buf, 2) = 17 + (uint16)strlen(cd->title);
	WBUFL(buf, 4) = cd->owner->id;
	WBUFL(buf, 8) = cd->bl.id;
	WBUFW(buf,12) = cd->limit;
	WBUFW(buf,14) = (cd->owner->type == BL_NPC) ? cd->users + 1 : cd->users;
	WBUFB(buf,16) = type;
	memcpy((char*)WBUFP(buf,17), cd->title, strlen(cd->title)); // not zero-terminated

	if( fd ) {
		WFIFOHEAD(fd,WBUFW(buf,2));
		memcpy(WFIFOP(fd,0),buf,WBUFW(buf,2));
		WFIFOSET(fd,WBUFW(buf,2));
	} else {
		clif_send(buf,WBUFW(buf,2),cd->owner,AREA_WOSC);
	}
}


/// Chatroom properties adjustment (ZC_CHANGE_CHATROOM).
/// 00df <packet len>.W <owner id>.L <chat id>.L <limit>.W <users>.W <type>.B <title>.?B
/// type:
///     0 = private (password protected)
///     1 = public
///     2 = arena (npc waiting room)
///     3 = PK zone (non-clickable)
void clif_changechatstatus(struct chat_data* cd)
{
	unsigned char buf[128];
	uint8 type;

	if( cd == NULL || cd->usersd[0] == NULL )
		return;

	type = (cd->owner->type == BL_PC ) ? (cd->pub) ? 1 : 0
	     : (cd->owner->type == BL_NPC) ? (cd->limit) ? 2 : 3
	     : 1;

	WBUFW(buf, 0) = 0xdf;
	WBUFW(buf, 2) = 17 + (uint16)strlen(cd->title);
	WBUFL(buf, 4) = cd->owner->id;
	WBUFL(buf, 8) = cd->bl.id;
	WBUFW(buf,12) = cd->limit;
	WBUFW(buf,14) = (cd->owner->type == BL_NPC) ? cd->users + 1 : cd->users;
	WBUFB(buf,16) = type;
	memcpy((char*)WBUFP(buf,17), cd->title, strlen(cd->title)); // not zero-terminated

	clif_send(buf,WBUFW(buf,2),cd->owner,CHAT);
}


/// Removes the chatroom (ZC_DESTROY_ROOM).
/// 00d8 <chat id>.L
void clif_clearchat(struct chat_data *cd,int fd)
{
	unsigned char buf[32];

	nullpo_retv(cd);

	WBUFW(buf,0) = 0xd8;
	WBUFL(buf,2) = cd->bl.id;
	if( fd ) {
		WFIFOHEAD(fd,packet_len(0xd8));
		memcpy(WFIFOP(fd,0),buf,packet_len(0xd8));
		WFIFOSET(fd,packet_len(0xd8));
	} else {
		clif_send(buf,packet_len(0xd8),cd->owner,AREA_WOSC);
	}
}


/// Displays messages regarding join chat failures (ZC_REFUSE_ENTER_ROOM).
/// 00da <result>.B
/// result:
///     0 = room full
///     1 = wrong password
///     2 = kicked
///     3 = success (no message)
///     4 = no enough zeny
///     5 = too low level
///     6 = too high level
///     7 = unsuitable job class
void clif_joinchatfail(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xda));
	WFIFOW(fd,0) = 0xda;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,packet_len(0xda));
}


/// Notifies the client about entering a chatroom (ZC_ENTER_ROOM).
/// 00db <packet len>.W <chat id>.L { <role>.L <name>.24B }*
/// role:
///     0 = owner (menu)
///     1 = normal
void clif_joinchatok(struct map_session_data *sd,struct chat_data* cd)
{
	int fd;
	int i,t;

	nullpo_retv(sd);
	nullpo_retv(cd);

	fd = sd->fd;
	if (!session_isActive(fd))
		return;
	t = (int)(cd->owner->type == BL_NPC);
	WFIFOHEAD(fd, 8 + (28 * (cd->users + t)));
	WFIFOW(fd, 0) = 0xdb;
	WFIFOW(fd, 2) = 8 + (28 * (cd->users + t));
	WFIFOL(fd, 4) = cd->bl.id;

	if (cd->owner->type == BL_NPC) {
		WFIFOL(fd, 30) = 1;
		WFIFOL(fd, 8) = 0;
		safestrncpy(WFIFOP(fd, 12), ((struct npc_data *)cd->owner)->name, NAME_LENGTH);
		for (i = 0; i < cd->users; i++) {
			WFIFOL(fd, 8 + (i + 1) * 28) = 1;
			safestrncpy(WFIFOP(fd, 8 + (i + t) * 28 + 4), cd->usersd[i]->status.name, NAME_LENGTH);
		}
	}
	else {
		for (i = 0; i < cd->users; i++) {
			WFIFOL(fd, 8 + i * 28) = (i != 0 || cd->owner->type == BL_NPC);
			safestrncpy(WFIFOP(fd, 8 + (i + t) * 28 + 4), cd->usersd[i]->status.name, NAME_LENGTH);
		}
	}
	WFIFOSET(fd, WFIFOW(fd, 2));
}


/// Notifies clients in a chat about a new member (ZC_MEMBER_NEWENTRY).
/// 00dc <users>.W <name>.24B
void clif_addchat(struct chat_data* cd,struct map_session_data *sd)
{
	unsigned char buf[32];

	nullpo_retv(sd);
	nullpo_retv(cd);

	WBUFW(buf, 0) = 0xdc;
	WBUFW(buf, 2) = cd->users;
	safestrncpy(WBUFP(buf, 4),sd->status.name,NAME_LENGTH);
	clif_send(buf,packet_len(0xdc),&sd->bl,CHAT_WOS);
}


/// Announce the new owner (ZC_ROLE_CHANGE).
/// 00e1 <role>.L <nick>.24B
/// role:
///     0 = owner (menu)
///     1 = normal
void clif_changechatowner(struct chat_data* cd, struct map_session_data* sd)
{
	unsigned char buf[64];

	nullpo_retv(sd);
	nullpo_retv(cd);

	WBUFW(buf, 0) = 0xe1;
	WBUFL(buf, 2) = 1;
	safestrncpy(WBUFP(buf,6),cd->usersd[0]->status.name,NAME_LENGTH);

	WBUFW(buf,30) = 0xe1;
	WBUFL(buf,32) = 0;
	safestrncpy(WBUFP(buf,36),sd->status.name,NAME_LENGTH);

	clif_send(buf,packet_len(0xe1)*2,&sd->bl,CHAT);
}


/// Notify about user leaving the chatroom (ZC_MEMBER_EXIT).
/// 00dd <users>.W <nick>.24B <flag>.B
/// flag:
///     0 = left
///     1 = kicked
void clif_leavechat(struct chat_data* cd, struct map_session_data* sd, bool flag)
{
	unsigned char buf[32];

	nullpo_retv(sd);
	nullpo_retv(cd);

	WBUFW(buf, 0) = 0xdd;
	WBUFW(buf, 2) = cd->users-1;
	safestrncpy(WBUFP(buf,4),sd->status.name,NAME_LENGTH);
	WBUFB(buf,28) = flag;

	clif_send(buf,packet_len(0xdd),&sd->bl,CHAT);
}


/// Opens a trade request window from char 'name'.
/// 00e5 <nick>.24B (ZC_REQ_EXCHANGE_ITEM)
/// 01f4 <nick>.24B <charid>.L <baselvl>.W (ZC_REQ_EXCHANGE_ITEM2)
void clif_traderequest(struct map_session_data* sd, const char* name)
{
	int fd = sd->fd;

#if PACKETVER < 6
	WFIFOHEAD(fd,packet_len(0xe5));
	WFIFOW(fd,0) = 0xe5;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0xe5));
#else
	struct map_session_data* tsd = map_id2sd(sd->trade_partner);
	if( !tsd ) return;
	
	WFIFOHEAD(fd,packet_len(0x1f4));
	WFIFOW(fd,0) = 0x1f4;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOL(fd,26) = tsd->status.char_id;
	WFIFOW(fd,30) = tsd->status.base_level;
	WFIFOSET(fd,packet_len(0x1f4));
#endif
}


/// Reply to a trade-request.
/// 00e7 <result>.B (ZC_ACK_EXCHANGE_ITEM)
/// 01f5 <result>.B <charid>.L <baselvl>.W (ZC_ACK_EXCHANGE_ITEM2)
/// result:
///     0 = Char is too far
///     1 = Character does not exist
///     2 = Trade failed
///     3 = Accept
///     4 = Cancel
///     5 = Busy
void clif_tradestart(struct map_session_data* sd, uint8 type)
{
	int fd = sd->fd;
	struct map_session_data* tsd = map_id2sd(sd->trade_partner);
	if( PACKETVER < 6 || !tsd ) {
		WFIFOHEAD(fd,packet_len(0xe7));
		WFIFOW(fd,0) = 0xe7;
		WFIFOB(fd,2) = type;
		WFIFOSET(fd,packet_len(0xe7));
	} else {
		WFIFOHEAD(fd,packet_len(0x1f5));
		WFIFOW(fd,0) = 0x1f5;
		WFIFOB(fd,2) = type;
		WFIFOL(fd,3) = tsd->status.char_id;
		WFIFOW(fd,7) = tsd->status.base_level;
		WFIFOSET(fd,packet_len(0x1f5));
	}
}


/// Notifies the client about an item from other player in current trade.
/// 00e9 <amount>.L <nameid>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_EXCHANGE_ITEM)
/// 080f <nameid>.W <item type>.B <amount>.L <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_EXCHANGE_ITEM2)
/// 0a09 <nameid>.W <item type>.B <amount>.L <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_EXCHANGE_ITEM3) 
void clif_tradeadditem(struct map_session_data* sd, struct map_session_data* tsd, int index, int amount) {
	nullpo_retv(sd);
	nullpo_retv(tsd);

	int fd = tsd->fd;

	if (!session_isActive(fd)) {
		return;
}

	struct PACKET_ZC_ADD_EXCHANGE_ITEM p;

	if (index) {
		index = server_index(index);

		if (index >= MAX_INVENTORY || sd->inventory.u.items_inventory[index].nameid == 0 || sd->inventory_data[index] == NULL) {
			return;
		}

		p.itemId = client_nameid(sd->inventory.u.items_inventory[index].nameid);
#if PACKETVER >= 20100223
		p.itemType = sd->inventory_data[index]->type;
#endif
		p.identified = sd->inventory.u.items_inventory[index].identify;
		p.damaged = sd->inventory.u.items_inventory[index].attribute;
		p.refine = sd->inventory.u.items_inventory[index].refine;
		clif_addcards(&p.slot, &sd->inventory.u.items_inventory[index]);
#if PACKETVER >= 20150226
		clif_add_random_options(p.option_data, &sd->inventory.u.items_inventory[index]);
#endif
	}
	else {
		memset(&p, 0, sizeof(p));
	}

	p.packetType = tradeaddType;
	p.amount = amount;

	clif_send(&p, sizeof(p), &tsd->bl, SELF);
}


/// Notifies the client about the result of request to add an item to the current trade (ZC_ACK_ADD_EXCHANGE_ITEM).
/// 00ea <index>.W <result>.B
/// result:
///     0 = success
///     1 = overweight
///     2 = trade canceled
void clif_tradeitemok(struct map_session_data* sd, int index, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xea));
	WFIFOW(fd,0) = 0xea;
	WFIFOW(fd,2) = index;
	WFIFOB(fd,4) = fail;
	WFIFOSET(fd,packet_len(0xea));
}


/// Notifies the client about finishing one side of the current trade (ZC_CONCLUDE_EXCHANGE_ITEM).
/// 00ec <who>.B
/// who:
///     0 = self
///     1 = other player
void clif_tradedeal_lock(struct map_session_data* sd, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xec));
	WFIFOW(fd,0) = 0xec;
	WFIFOB(fd,2) = fail;
	WFIFOSET(fd,packet_len(0xec));
}


/// Notifies the client about the trade being canceled (ZC_CANCEL_EXCHANGE_ITEM).
/// 00ee
void clif_tradecancelled(struct map_session_data* sd)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xee));
	WFIFOW(fd,0) = 0xee;
	WFIFOSET(fd,packet_len(0xee));
}


/// Result of a trade (ZC_EXEC_EXCHANGE_ITEM).
/// 00f0 <result>.B
/// result:
///     0 = success
///     1 = failure
void clif_tradecompleted(struct map_session_data* sd, int fail)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xf0));
	WFIFOW(fd,0) = 0xf0;
	WFIFOB(fd,2) = fail;
	WFIFOSET(fd,packet_len(0xf0));
}


/// Resets the trade window on the send side (ZC_EXCHANGEITEM_UNDO).
/// 00f1
/// NOTE: Unknown purpose. Items are not removed until the window is
///       refreshed (ex. by putting another item in there).
void clif_tradeundo(struct map_session_data* sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xf1));
	WFIFOW(fd,0) = 0xf1;
	WFIFOSET(fd,packet_len(0xf1));
}


/// Updates storage total amount (ZC_NOTIFY_STOREITEM_COUNTINFO).
/// 00f2 <current count>.W <max count>.W
void clif_updatestorageamount(struct map_session_data* sd, int amount, int max_amount)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf2));
	WFIFOW(fd,0) = 0xf2;
	WFIFOW(fd,2) = amount;
	WFIFOW(fd,4) = max_amount;
	WFIFOSET(fd,packet_len(0xf2));
}


/// Notifies the client of an item being added to the storage.
/// 00f4 <index>.W <amount>.L <nameid>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_STORE)
/// 01c4 <index>.W <amount>.L <nameid>.W <type>.B <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_STORE2)
/// 0a0a <index>.W <amount>.L <nameid>.W <type>.B <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_STORE3)
void clif_storageitemadded(struct map_session_data* sd, struct item* i, int index, int amount) {
	nullpo_retv(sd);
	nullpo_retv(i);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	struct PACKET_ZC_ADD_ITEM_TO_STORE p;

	p.packetType = storageaddType; // Storage item added
	p.index = client_storage_index(index); // index
	p.amount = amount; // amount
	p.itemId = client_nameid(i->nameid); // id
#if PACKETVER >= 5
	p.itemType = itemtype(i->nameid); //type
#endif
	p.identified = i->identify; //identify flag
	p.damaged = i->attribute; // attribute
	p.refine = i->refine; //refine
	clif_addcards(&p.slot, i);
#if PACKETVER >= 20150226
	clif_add_random_options(p.option_data, i);
#endif

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}


/// Notifies the client of an item being deleted from the storage (ZC_DELETE_ITEM_FROM_STORE).
/// 00f6 <index>.W <amount>.L
void clif_storageitemremoved(struct map_session_data* sd, int index, int amount)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf6));
	WFIFOW(fd,0)=0xf6; // Storage item removed
	WFIFOW(fd,2)=index+1;
	WFIFOL(fd,4)=amount;
	WFIFOSET(fd,packet_len(0xf6));
}


/// Closes storage (ZC_CLOSE_STORE).
/// 00f8
void clif_storageclose(struct map_session_data* sd)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xf8));
	WFIFOW(fd,0) = 0xf8; // Storage Closed
	WFIFOSET(fd,packet_len(0xf8));
}

/*==========================================
 * PC表示
 *------------------------------------------*/
static void clif_getareachar_pc(struct map_session_data* sd,struct map_session_data* dstsd)
{
	int gmlvl;
	struct block_list *d_bl;
	int i;

	if(dstsd->chatID)
	{
		struct chat_data *cd = NULL;
		if ((cd = (struct chat_data*)map_id2bl(dstsd->chatID)) && cd->usersd[0] == dstsd)
			clif_dispchat(cd,sd->fd);
	}

	else if( dstsd->state.vending )
		clif_showvendingboard(&dstsd->bl,dstsd->message,sd->fd);

	else if( dstsd->state.buyingstore )
		clif_buyingstore_entry_single(sd, dstsd);

	if(dstsd->spiritball > 0)
		clif_spiritball_single(dstsd);

	if (dstsd->shieldball > 0)
		clif_millenniumshield_single(sd->fd, &dstsd->bl, dstsd->shieldball);

	if (dstsd->rageball > 0)
		clif_millenniumshield_single(sd->fd, &dstsd->bl, dstsd->rageball);

	if (dstsd->charmball > 0)
		clif_spiritball_attribute_single(sd->fd, dstsd);

	if (dstsd->soulball > 0)
		clif_soulball_single(sd->fd, dstsd);

	if( (sd->status.party_id && dstsd->status.party_id == sd->status.party_id) || //Party-mate, or hpdisp setting.
		(sd->bg_id && sd->bg_id == dstsd->bg_id) || //BattleGround
		(battle_config.disp_hpmeter && (gmlvl = pc_isGM(sd)) >= battle_config.disp_hpmeter && gmlvl >= pc_isGM(dstsd)) )
		clif_hpmeter_single(sd->fd, dstsd->bl.id, dstsd->battle_status.hp, dstsd->battle_status.max_hp);

	// display link (sd - dstsd) to sd
	ARR_FIND( 0, 5, i, sd->devotion[i] == dstsd->bl.id );
	if( i < 5 ) clif_devotion(&sd->bl, sd);
	// display links (dstsd - devotees) to sd
	ARR_FIND( 0, 5, i, dstsd->devotion[i] > 0 );
	if( i < 5 ) clif_devotion(&dstsd->bl, sd);
	// display link (dstsd - crusader) to sd
	if( dstsd->sc.data[SC_DEVOTION] && (d_bl = map_id2bl(dstsd->sc.data[SC_DEVOTION]->val1)) != NULL )
		clif_devotion(d_bl, sd);
	// display link (dstsd - elemental) to sd
	if( dstsd->sc.data[SC_WATER_SCREEN_OPTION] && (d_bl = map_id2bl(dstsd->sc.data[SC_WATER_SCREEN_OPTION]->val1)) != NULL )
		clif_devotion(d_bl, sd);
}

void clif_getareachar_unit(struct map_session_data* sd,struct block_list *bl)
{
	struct unit_data *ud;
	struct view_data *vd;
	int i;
	
	vd = status_get_viewdata(bl);
	if (!vd || vd->class_ == INVISIBLE_CLASS ||
		bl->type == BL_NPC && !((TBL_NPC*)bl)->chat_id && ((TBL_NPC*)bl)->sc.option&OPTION_INVISIBLE)
		return;

	ud = unit_bl2ud(bl);

	if (ud && ud->walktimer != INVALID_TIMER) {
		clif_set_unit_walking(bl, sd, ud, SELF);
	}
	else
		clif_set_unit_idle(bl, false, SELF, &sd->bl);

	if (vd->cloth_color)
		clif_refreshlook(&sd->bl,bl->id,LOOK_CLOTHES_COLOR,vd->cloth_color,SELF);
	if (vd->body_style)
		clif_refreshlook(&sd->bl,bl->id,LOOK_BODY2,vd->body_style,SELF);

	switch (bl->type)
	{
	case BL_PC:
		{
			TBL_PC* tsd = (TBL_PC*)bl;
			clif_getareachar_pc(sd, tsd);
			if(tsd->state.size==2) // tiny/big players [Valaris]
				clif_specialeffect_single(bl,423,sd->fd);
			else if(tsd->state.size==1)
				clif_specialeffect_single(bl,421,sd->fd);
			if( tsd->bg_id && map[tsd->bl.m].flag.battleground )
				clif_sendbgemblem_single(sd->fd,tsd);
			if (tsd->status.robe)
				clif_refreshlook(&sd->bl, bl->id, LOOK_ROBE, tsd->status.robe, SELF);
			if (tsd->sc.data[SC_MONSTER_TRANSFORM])
				clif_status_change(bl, SI_MONSTER_TRANSFORM, 1, 0, tsd->sc.data[SC_MONSTER_TRANSFORM]->val1, 0, 0);

			clif_hat_effects(sd, bl, SELF);
#if PACKETVER >= 20111108
			if (tsd->sc.count)
			{// Starting the check at SC_ALL_RIDING since all SC's after this don't use OPT3.
			//  Starting at 0 would just waste CPU cycles. [Rytech]
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{// If SC is not active, check for the next one.
					if (!tsd->sc.data[i])
						continue;

					// Don't bother sending the packet if the SC has no icon data.
					if (status_sc2icon(i) == SI_BLANK)
						continue;

					// Status active and has a icon for showing possible animation? Send it.
					clif_efst_status_change_sub(&sd->bl, bl, SELF);
				}
			}
#endif
		}
		break;
	case BL_MER: // Devotion Effects
		if( ((TBL_MER*)bl)->devotion_flag )
			clif_devotion(bl, sd);
		break;
	case BL_ELEM: // Water Screen's Effect. Its basicly devotion.
		if( ((TBL_ELEM*)bl)->water_screen_flag )
			clif_devotion(bl, sd);
		break;
	case BL_NPC:
		{
			TBL_NPC* nd = (TBL_NPC*)bl;
			if( nd->chat_id )
				clif_dispchat((struct chat_data*)map_id2bl(nd->chat_id),sd->fd);
			if( nd->size == 2 )
				clif_specialeffect_single(bl,423,sd->fd);
			else if( nd->size == 1 )
				clif_specialeffect_single(bl,421,sd->fd);
#if PACKETVER >= 20111108
			if (nd->sc.count)
			{// Starting the check at SC_ALL_RIDING since all SC's after this don't use OPT3.
			//  Starting at 0 would just waste CPU cycles. [Rytech]
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{// If SC is not active, check for the next one.
					if (!nd->sc.data[i])
						continue;

					// Don't bother sending the packet if the SC has no icon data.
					if (status_sc2icon(i) == SI_BLANK)
						continue;

					// Status active and has a icon for showing possible animation? Send it.
					clif_efst_status_change_sub(&sd->bl, bl, SELF);
				}
			}
#endif
		}
		break;
	case BL_MOB:
		{
			TBL_MOB* md = (TBL_MOB*)bl;
			if(md->special_state.size==2) // tiny/big mobs [Valaris]
				clif_specialeffect_single(bl,423,sd->fd);
			else if(md->special_state.size==1)
				clif_specialeffect_single(bl,421,sd->fd);
#if PACKETVER >= 20111108
			if (md->sc.count)
			{
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{
					if (!md->sc.data[i])
						continue;

					if (status_sc2icon(i) == SI_BLANK)
						continue;

					clif_efst_status_change_sub(&sd->bl, bl, SELF);
				}
			}
#endif
#if PACKETVER >= 20120404
			if (!(md->status.mode&MD_STATUS_IMMUNE)) {
				int i;
				for (i = 0; i < DAMAGELOG_SIZE; i++)// must show hp bar to all char who already hit the mob.
					if (md->dmglog[i].id == sd->status.char_id)
						clif_monster_hp_bar(md, sd->fd);
			}
#endif
		}
		break;
	case BL_PET:
		if (vd->head_bottom)
			clif_pet_equip(sd, (TBL_PET*)bl); // needed to display pet equip properly
		break;
	case BL_HOM:
		{
			TBL_HOM* hd = (TBL_HOM*)bl;
			if (hd->hom_spiritball > 0)
				clif_hom_spiritball_single(hd);
#if PACKETVER >= 20111108
			if (hd->sc.count)
			{
				for (i = SC_ALL_RIDING; i < SC_MAX; i++)
				{
					if (!hd->sc.data[i])
						continue;

					if (status_sc2icon(i) == SI_BLANK)
						continue;

					clif_efst_status_change_sub(&sd->bl, bl, SELF);
				}
			}
#endif
		}
		break;
	}
}

//Modifies the type of damage according to status changes [Skotlex]
//Aegis data specifies that: 4 endure against single hit sources, 9 against multi-hit.
static inline int clif_calc_delay(int type, int div, int damage, int delay)
{
	return ( delay == 0 && damage > 0 ) ? ( div > 1 ? 9 : 4 ) : type;
}

/*==========================================
 * Estimates walk delay based on the damage criteria. [Skotlex]
 *------------------------------------------*/
static int clif_calc_walkdelay(struct block_list *bl,int delay, int type, int damage, int div_)
{
	if (type == 4 || type == 9 || damage <=0)
		return 0;
	
	if (bl->type == BL_PC) {
		if (battle_config.pc_walk_delay_rate != 100)
			delay = delay*battle_config.pc_walk_delay_rate/100;
	} else
		if (battle_config.walk_delay_rate != 100)
			delay = delay*battle_config.walk_delay_rate/100;
	
	if (div_ > 1) //Multi-hit skills mean higher delays.
		delay += battle_config.multihit_delay*(div_-1);

	return delay>0?delay:1; //Return 1 to specify there should be no noticeable delay, but you should stop walking.
}

/*========================================== [Playtester]
 * Returns hallucination damage the client displays
 *------------------------------------------*/
static int clif_hallucination_damage()
{
	int digit = rnd() % 5 + 1;
	switch (digit)
	{
	case 1:
		return (rnd() % 10);
	case 2:
		return (rnd() % 100);
	case 3:
		return (rnd() % 1000);
	case 4:
		return (rnd() % 10000);
	}
	return (rnd() % 32767);
}

/// Sends a 'damage' packet (src performs action on dst)
/// 008a <src ID>.L <dst ID>.L <server tick>.L <src speed>.L <dst speed>.L <damage>.W <div>.W <type>.B <damage2>.W (ZC_NOTIFY_ACT)
/// 02e1 <src ID>.L <dst ID>.L <server tick>.L <src speed>.L <dst speed>.L <damage>.L <div>.W <type>.B <damage2>.L (ZC_NOTIFY_ACT2)
/// 08c8 <src ID>.L <dst ID>.L <server tick>.L <src speed>.L <dst speed>.L <damage>.L <IsSPDamage>.B <div>.W <type>.B <damage2>.L (ZC_NOTIFY_ACT3)
/// type:
/// 0 = ATTACK - damage [ damage: total damage, div: amount of hits, damage2: assassin dual-wield damage ]
/// 1 = ITEMPICKUP - pick up item
/// 2 = SIT - sit down
/// 3 = STAND - stand up
/// 4 = ATTACK_NOMOTION - damage (endure)
/// 5 = SPLASH - (splash?)
/// 6 = SKILL - (skill?)
/// 7 = ATTACK_REPEAT - (repeat damage?)
/// 8 = ATTACK_MULTIPLE - multi-hit damage
/// 9 = ATTACK_MULTIPLE_NOMOTION - multi-hit damage (endure)
/// 10 = ATTACK_CRITICAL - critical hit
/// 11 = ATTACK_LUCKY - lucky dodge
/// 12 = TOUCHSKILL - (touch skill?)
/// 13 = ATTACK_MULTIPLE_CRITICAL - multi-hit critical damage
int clif_damage(struct block_list* src, struct block_list* dst, int64 tick, int sdelay, int ddelay, int64 sdamage, int div, int type, int64 sdamage2, bool spdamage)
{
	unsigned char buf[34];
	struct status_change *sc;
	int damage = (int)cap_value(sdamage,INT_MIN,INT_MAX);
	int damage2 = (int)cap_value(sdamage2,INT_MIN,INT_MAX);
#if PACKETVER < 20071113
	const int cmd = 0x8a;
	int offset = 0;
#elif PACKETVER < 20131223
	const int cmd = 0x2e1;
	int offset = 2;
#else
	const int cmd = 0x8c8;
	int offset = 3;
#endif

	nullpo_ret(src);
	nullpo_ret(dst);

	type = clif_calc_delay(type,div,damage+damage2,ddelay);
	sc = status_get_sc(dst);
	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION]) {
			if (damage) damage = clif_hallucination_damage();
			if (damage2) damage2 = clif_hallucination_damage();
		}
	}

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = src->id;
	WBUFL(buf,6) = dst->id;
	WBUFL(buf,10) = (uint32)tick;
	WBUFL(buf,14) = sdelay;
	WBUFL(buf,18) = ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
#if PACKETVER < 20071113
		WBUFW(buf,22) = damage ? div : 0;
		WBUFW(buf,27+offset) = damage2 ? div : 0;
#else
		WBUFL(buf, 22) = damage ? div : 0;
		WBUFL(buf, 27 + offset) = damage2 ? div : 0;
#endif
	} else {
#if PACKETVER < 20071113
		WBUFW(buf,22) = min(damage, INT16_MAX);
		WBUFW(buf,27+offset) = damage2;
#else
		WBUFL(buf,22) = damage;
		WBUFL(buf,27+offset) = damage2;
#endif
	}
#if PACKETVER >= 20131223
	WBUFB(buf,26) = (spdamage) ? 1 : 0; // IsSPDamage - Displays blue digits.
#endif
	WBUFW(buf,24+offset) = div;
	WBUFB(buf,26+offset) = type;

	if(disguised(dst)) {
		clif_send(buf, packet_len(cmd), dst, AREA_WOS);
		WBUFL(buf,6) = -dst->id;
		clif_send(buf, packet_len(cmd), dst, SELF);
	} else
		clif_send(buf, packet_len(cmd), dst, AREA);

	if(disguised(src)) {
		WBUFL(buf,2) = -src->id;
		if (disguised(dst))
			WBUFL(buf,6) = dst->id;
#if PACKETVER < 20071113
		if(damage > 0) WBUFW(buf,22) = -1;
		if(damage2 > 0) WBUFW(buf,27) = -1;
#else
		if(damage > 0) WBUFL(buf,22) = -1;
		if(damage2 > 0) WBUFL(buf,27+offset) = -1;
#endif
		clif_send(buf,packet_len(cmd),src,SELF);
	}

	if(src == dst) {
		unit_setdir(src, unit_getdir(src));
	}
	//Return adjusted can't walk delay for further processing.
	return clif_calc_walkdelay(dst, ddelay, type, damage+damage2, div);
}

/*==========================================
 * src picks up dst
 *------------------------------------------*/
void clif_takeitem(struct block_list* src, struct block_list* dst)
{
	//clif_damage(src,dst,0,0,0,0,0,1,0);
	unsigned char buf[32];

	nullpo_retv(src);
	nullpo_retv(dst);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = src->id;
	WBUFL(buf, 6) = dst->id;
	WBUFB(buf,26) = 1;
	clif_send(buf, packet_len(0x8a), src, AREA);

}

/*==========================================
 * inform clients in area that `bl` is sitting
 *------------------------------------------*/
void clif_sitting(struct block_list* bl, bool area)
{
	unsigned char buf[32];
	nullpo_retv(bl);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = bl->id;
	WBUFB(buf,26) = 2;
	if (area)
		clif_send(buf, packet_len(0x8a), bl, AREA);
	else
		clif_send(buf, packet_len(0x8a), bl, SELF);

	if(disguised(bl))
	{
		WBUFL(buf, 2) = - bl->id;
		clif_send(buf, packet_len(0x8a), bl, SELF);
	}
}

/*==========================================
 * inform clients in area that `bl` is standing
 *------------------------------------------*/
void clif_standing(struct block_list* bl, bool area)
{
	unsigned char buf[32];
	nullpo_retv(bl);

	WBUFW(buf, 0) = 0x8a;
	WBUFL(buf, 2) = bl->id;
	WBUFB(buf,26) = 3;
	if (area)
		clif_send(buf, packet_len(0x8a), bl, AREA);
	else
		clif_send(buf, packet_len(0x8a), bl, SELF);

	if(disguised(bl))
	{
		WBUFL(buf, 2) = - bl->id;
		clif_send(buf, packet_len(0x8a), bl, SELF);
	}
}


/// Inform client(s) about a map-cell change (ZC_UPDATE_MAPINFO).
/// 0192 <x>.W <y>.W <type>.W <map name>.16B
void clif_changemapcell(int fd, int m, int x, int y, int type, enum send_target target)
{
	unsigned char buf[32];

	WBUFW(buf,0) = 0x192;
	WBUFW(buf,2) = x;
	WBUFW(buf,4) = y;
	WBUFW(buf,6) = type;
	mapindex_getmapname_ext(map[m].name,(char*)WBUFP(buf,8));

	if( fd )
	{
		WFIFOHEAD(fd,packet_len(0x192));
		memcpy(WFIFOP(fd,0), buf, packet_len(0x192));
		WFIFOSET(fd,packet_len(0x192));
	}
	else
	{
		struct block_list dummy_bl;
		dummy_bl.type = BL_NUL;
		dummy_bl.x = x;
		dummy_bl.y = y;
		dummy_bl.m = m;
		clif_send(buf,packet_len(0x192),&dummy_bl,target);
	}
}


/// Notifies the client about an item on floor (ZC_ITEM_ENTRY).
/// 009d <id>.L <name id>.W <identified>.B <x>.W <y>.W <amount>.W <subX>.B <subY>.B
void clif_getareachar_item(struct map_session_data* sd,struct flooritem_data* fitem)
{
	nullpo_retv(sd);
	nullpo_retv(fitem);

	struct PACKET_ZC_ITEM_ENTRY p;

	p.packetType = 0x9d;
	p.AID = fitem->bl.id;
	p.itemId = client_nameid(fitem->item_data.nameid);
	p.identify = fitem->item_data.identify;
	p.x = fitem->bl.x;
	p.y = fitem->bl.y;
	p.amount = fitem->item_data.amount;
	p.subX = fitem->subx;
	p.subY = fitem->suby;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Notifes client about Graffiti
/// 01c9 <id>.L <creator id>.L <x>.W <y>.W <unit id>.B <visible>.B <has msg>.B <msg>.80B (ZC_SKILL_ENTRY2)
static void clif_graffiti(struct block_list *bl, struct skill_unit *unit, enum send_target target) {
	unsigned char buf[128];

	nullpo_retv(bl);
	nullpo_retv(unit);

	WBUFW(buf, 0) = 0x1c9;
	WBUFL(buf, 2) = unit->bl.id;
	WBUFL(buf, 6) = unit->group->src_id;
	WBUFW(buf, 10) = unit->bl.x;
	WBUFW(buf, 12) = unit->bl.y;
	WBUFB(buf, 14) = unit->group->unit_id;
	WBUFB(buf, 15) = 1;
	WBUFB(buf, 16) = 1;
	safestrncpy((char*)WBUFP(buf, 17), unit->group->valstr, MESSAGE_SIZE);
	clif_send(buf, packet_len(0x1c9), bl, target);
}

/// Notifies the client of a skill unit.
/// 011f <id>.L <creator id>.L <x>.W <y>.W <unit id>.B <visible>.B (ZC_SKILL_ENTRY)
/// 08c7 <lenght>.W <id> L <creator id>.L <x>.W <y>.W <unit id>.B <range>.W <visible>.B (ZC_SKILL_ENTRY3)
/// 099f <lenght>.W <id> L <creator id>.L <x>.W <y>.W <unit id>.L <range>.W <visible>.B (ZC_SKILL_ENTRY4)
/// 09ca <lenght>.W <id> L <creator id>.L <x>.W <y>.W <unit id>.L <range>.B <visible>.B <skill level>.B (ZC_SKILL_ENTRY5)
void clif_getareachar_skillunit(struct block_list *bl, struct skill_unit *unit, enum send_target target, bool visible) {
	int header = 0, unit_id = 0, pos = 0, fd = 0, len = -1;
	unsigned char buf[128];
	
	nullpo_retv(bl);
	nullpo_retv(unit);

	if (bl->type == BL_PC)
		fd = ((TBL_PC*)bl)->fd;

	if (unit->group->state.guildaura)
		return;

	if (unit->group->state.song_dance & 0x1 && unit->val2&UF_ENSEMBLE)
		unit_id = unit->val2&UF_SONG ? UNT_DISSONANCE : UNT_UGLYDANCE;
	else if (skill_get_unit_flag(unit->group->skill_id) & UF_SINGLEANIMATION && !(unit->val2 & UF_SINGLEANIMATION))
		unit_id = UNT_DUMMYSKILL; //Use invisible unit id for other case of rangedsingle unit
	else
		unit_id = unit->group->unit_id;

	if (!visible)
		unit_id = UNT_DUMMYSKILL; // Hack to makes hidden trap really hidden!

#if PACKETVER >= 3
	if (unit_id == UNT_GRAFFITI) { // Graffiti [Valaris]
		clif_graffiti(bl, unit, target);
		return;
	}
#endif

#if PACKETVER <= 20120702
	header = 0x011f;
//#if PACKETVER < 20110718
//	header = 0x011f;
//#elif PACKETVER < 20121212
//	header = 0x08c7;
#elif PACKETVER < 20130731
	header = 0x099f;
#else
	header = 0x09ca;
#endif

	len = packet_len(header);
	WBUFW(buf,pos) = header;
	if (header != 0x011f) {
		WBUFW(buf, pos+2) = len;
		pos += 2;
	}
	WBUFL(buf,pos+2) = unit->bl.id;
	WBUFL(buf,pos+6) = unit->group->src_id;
	WBUFW(buf,pos+10) = unit->bl.x;
	WBUFW(buf,pos+12) = unit->bl.y;
	switch (header) {
		case 0x011f:
			WBUFB(buf,pos+14) = unit_id;
			WBUFB(buf,pos+15) = visible;
			break;
		case 0x08c7:
			WBUFB(buf,pos+14) = unit_id;
			WBUFW(buf,pos+15) = unit->range;
			WBUFB(buf,pos+17) = visible;
			break;
		case 0x099f:
			WBUFL(buf,pos+14) = unit_id;
			WBUFW(buf,pos+18) = unit->range;
			WBUFB(buf,pos+20) = visible;
			break;
		case 0x09ca:
			WBUFL(buf,pos+14) = unit_id;
			WBUFB(buf,pos+18) = (unsigned char)unit->range;
			WBUFB(buf,pos+19) = visible;
			WBUFB(buf,pos+20) = (unsigned char)unit->group->skill_lv;
			break;
	}
	clif_send(buf, len, bl, target);

	if (unit->group->skill_id == WZ_ICEWALL)
		clif_changemapcell(fd, unit->bl.m, unit->bl.x, unit->bl.y, 5, SELF);
}

/*==========================================
 * 場所スキルエフェクトが視界から消える
 *------------------------------------------*/
static void clif_clearchar_skillunit(struct skill_unit *unit, int fd)
{
	nullpo_retv(unit);

	WFIFOHEAD(fd,packet_len(0x120));
	WFIFOW(fd, 0)=0x120;
	WFIFOL(fd, 2)=unit->bl.id;
	WFIFOSET(fd,packet_len(0x120));

	if(unit->group && unit->group->skill_id == WZ_ICEWALL)
		clif_changemapcell(fd,unit->bl.m,unit->bl.x,unit->bl.y,unit->val2,SELF);
}


/// Removes a skill unit (ZC_SKILL_DISAPPEAR).
/// 0120 <id>.L
void clif_skill_delunit(struct skill_unit *unit)
{
	unsigned char buf[16];

	nullpo_retv(unit);

	WBUFW(buf, 0)=0x120;
	WBUFL(buf, 2)=unit->bl.id;
	clif_send(buf,packet_len(0x120),&unit->bl,AREA);
}


/// Sent when an object gets ankle-snared (ZC_SKILL_UPDATE).
/// 01ac <id>.L
/// Only affects units with class [139,153] client-side.
void clif_skillunit_update(struct block_list* bl)
{
	unsigned char buf[6];
	nullpo_retv(bl);

	WBUFW(buf,0) = 0x1ac;
	WBUFL(buf,2) = bl->id;

	clif_send(buf,packet_len(0x1ac),bl,AREA);
}


/*==========================================
 *
 *------------------------------------------*/
static int clif_getareachar(struct block_list* bl,va_list ap)
{
	struct map_session_data *sd;

	nullpo_ret(bl);

	sd=va_arg(ap,struct map_session_data*);

	if (sd == NULL || !sd->fd)
		return 0;

	switch(bl->type){
	case BL_ITEM:
		clif_getareachar_item(sd,(struct flooritem_data*) bl);
		break;
	case BL_SKILL:
		skill_getareachar_skillunit_visibilty_single((TBL_SKILL*)bl, &sd->bl);
		break;
	default:
		if(&sd->bl == bl)
			break;
		clif_getareachar_unit(sd,bl);
		break;
	}
	return 0;
}

/*==========================================
 * tbl has gone out of view-size of bl
 *------------------------------------------*/
int clif_outsight(struct block_list *bl,va_list ap)
{
	struct block_list *tbl;
	struct view_data *vd;
	TBL_PC *sd, *tsd;
	tbl=va_arg(ap,struct block_list*);
	if(bl == tbl) return 0;
	sd = BL_CAST(BL_PC, bl);
	tsd = BL_CAST(BL_PC, tbl);

	if (tsd && tsd->fd)
	{	//tsd has lost sight of the bl object.
		nullpo_ret(bl);
		switch(bl->type) {
		case BL_PC:
			nullpo_ret(sd);
			if (sd->vd.class_ != INVISIBLE_CLASS)
				clif_clearunit_single(bl->id,CLR_OUTSIGHT,tsd->fd);
			if(sd->chatID){
				struct chat_data *cd;
				cd=(struct chat_data*)map_id2bl(sd->chatID);
				if(cd->usersd[0]==sd)
					clif_dispchat(cd,tsd->fd);
			}
			if( sd->state.vending )
				clif_closevendingboard(bl,tsd->fd);
			if( sd->state.buyingstore )
				clif_buyingstore_disappear_entry_single(tsd, sd);
			break;
		case BL_ITEM:
			clif_clearflooritem((struct flooritem_data*)bl,tsd->fd);
			break;
		case BL_SKILL:
			clif_clearchar_skillunit((struct skill_unit *)bl,tsd->fd);
			break;
		case BL_NPC:
			if (!(((TBL_NPC*)bl)->sc.option&OPTION_INVISIBLE))
				clif_clearunit_single(bl->id, CLR_OUTSIGHT, tsd->fd);
			break;
		default:
			if ((vd=status_get_viewdata(bl)) && vd->class_ != INVISIBLE_CLASS)
				clif_clearunit_single(bl->id,CLR_OUTSIGHT,tsd->fd);
			break;
		}
	}
	if (sd && sd->fd)
	{	//sd is watching tbl go out of view.
		nullpo_ret(tbl);
		if (tbl->type == BL_SKILL) //Trap knocked out of sight
			clif_clearchar_skillunit((struct skill_unit *)tbl, sd->fd);
		else if (((vd = status_get_viewdata(tbl)) && vd->class_ != INVISIBLE_CLASS) &&
			!(bl->type == BL_NPC && (((TBL_NPC*)bl)->sc.option&OPTION_INVISIBLE)))
			clif_clearunit_single(tbl->id,CLR_OUTSIGHT,sd->fd);
	}
	return 0;
}

/*==========================================
 * tbl has come into view of bl
 *------------------------------------------*/
int clif_insight(struct block_list *bl,va_list ap)
{
	struct block_list *tbl;
	TBL_PC *sd, *tsd;
	tbl=va_arg(ap,struct block_list*);

	if (bl == tbl) return 0;
	
	sd = BL_CAST(BL_PC, bl);
	tsd = BL_CAST(BL_PC, tbl);

	if (tsd && tsd->fd)
	{	//Tell tsd that bl entered into his view
		switch(bl->type){
		case BL_ITEM:
			clif_getareachar_item(tsd,(struct flooritem_data*)bl);
			break;
		case BL_SKILL:
			skill_getareachar_skillunit_visibilty_single((TBL_SKILL*)bl, &tsd->bl);
			break;
		default:
			clif_getareachar_unit(tsd,bl);
			break;
		}
	}
	if (sd && sd->fd)
	{	//Tell sd that tbl walked into his view
		clif_getareachar_unit(sd,tbl);
	}
	return 0;
}


/// Updates whole skill tree (ZC_SKILLINFO_LIST).
/// 010f <packet len>.W { <skill id>.W <type>.L <level>.W <sp cost>.W <attack range>.W <skill name>.24B <upgradable>.B }*
void clif_skillupdateinfoblock(struct map_session_data *sd)
{
	int fd;
	int i,len,id;

	nullpo_retv(sd);

	fd=sd->fd;
	if (!fd) return;

	WFIFOHEAD(fd, MAX_SKILL * 37 + 4);
	WFIFOW(fd,0) = 0x10f;
	for ( i = 0, len = 4; i < MAX_SKILL; i++)
	{
		if( (id = sd->status.skill[i].id) != 0 )
		{
			// workaround for gm_all_skill
			if (len + 37 > 8192)
				break;

			WFIFOW(fd,len)   = id;
			WFIFOL(fd, len + 2) = skill_get_inf(id);
			WFIFOW(fd,len+6) = sd->status.skill[i].lv;
			WFIFOW(fd,len+8) = skill_get_sp(id,sd->status.skill[i].lv);
			WFIFOW(fd,len+10)= skill_get_range2(&sd->bl, id,sd->status.skill[i].lv, false);
			safestrncpy((char*)WFIFOP(fd,len+12), skill_get_name(id), NAME_LENGTH);
			if(sd->status.skill[i].flag == SKILL_FLAG_PERMANENT)
				WFIFOB(fd,len+36) = (sd->status.skill[i].lv < skill_tree_get_max(id, sd->status.class_))? 1:0;
			else
				WFIFOB(fd,len+36) = 0;
			len += 37;
		}
	}
	WFIFOW(fd,2)=len;
	WFIFOSET(fd,len);

	// workaround for gm_all_skill; send the remaining skills one by one to bypass packet size limit
	for (; i < MAX_SKILL; i++)
	{
		if ((id = sd->status.skill[i].id) != 0)
		{
			clif_addskill(sd, id);
			clif_skillupdateinfo(sd, id, 0, 0);
		}
	}
}

/// Adds new skill to the skill tree (ZC_ADD_SKILL).
/// 0111 <skill id>.W <type>.L <level>.W <sp cost>.W <attack range>.W <skill name>.24B <upgradable>.B
void clif_addskill(struct map_session_data *sd, int id)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	if (!fd)
		return;

	if( sd->status.skill[id].id <= 0 )
		return;

	//ShowDebug("clif_addskill: id: %d  level: %d  flag: %d", id, sd->status.skill[id].lv, sd->status.skill[id].flag);

	WFIFOHEAD(fd, packet_len(0x111));
	WFIFOW(fd,0) = 0x111;
	WFIFOW(fd,2) = id;
	WFIFOL(fd, 4) = skill_get_inf(id);
	WFIFOW(fd,8) = sd->status.skill[id].lv;
	WFIFOW(fd,10) = skill_get_sp(id,sd->status.skill[id].lv);
	WFIFOW(fd,12)= skill_get_range2(&sd->bl, id,sd->status.skill[id].lv,false);
	safestrncpy((char*)WFIFOP(fd,14), skill_get_name(id), NAME_LENGTH);
	if( sd->status.skill[id].flag == SKILL_FLAG_PERMANENT )
		WFIFOB(fd,38) = (sd->status.skill[id].lv < skill_tree_get_max(id, sd->status.class_))? 1:0;
	else
		WFIFOB(fd,38) = 0;
	WFIFOSET(fd,packet_len(0x111));
}


/// Deletes a skill from the skill tree (ZC_SKILLINFO_DELETE).
/// 0441 <skill id>.W
void clif_deleteskill(struct map_session_data *sd, int id)
{
#if PACKETVER >= 20081217
	int fd;

	nullpo_retv(sd);
	fd = sd->fd;
	if( !fd ) return;

	WFIFOHEAD(fd,packet_len(0x441));
	WFIFOW(fd,0) = 0x441;
	WFIFOW(fd,2) = id;
	WFIFOSET(fd,packet_len(0x441));
#endif

	clif_skillupdateinfoblock(sd);
}


/// Updates a skill in the skill tree (ZC_SKILLINFO_UPDATE).
/// 010e <skill id>.W <level>.W <sp cost>.W <attack range>.W <upgradable>.B
void clif_skillup(struct map_session_data *sd, uint16 skill_id, int lv, int range, int upgradable)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0x10e));
	WFIFOW(fd, 0) = 0x10e;
	WFIFOW(fd, 2) = skill_id;
	WFIFOW(fd, 4) = lv;
	WFIFOW(fd, 6) = skill_get_sp(skill_id, lv);
	WFIFOW(fd, 8) = range;
	WFIFOB(fd, 10) = upgradable;
	WFIFOSET(fd, packet_len(0x10e));
}


/// Updates a skill in the skill tree (ZC_SKILLINFO_UPDATE2).
/// 07e1 <skill id>.W <type>.L <level>.W <sp cost>.W <attack range>.W <upgradable>.B
void clif_skillupdateinfo(struct map_session_data *sd,int skill_id,int type,int range) {
	int fd, id, cmd, offs = 0;
	
#if PACKETVER < 20090715
	cmd = 0x147;
#else
	cmd = 0x7e1;
#endif
	
	nullpo_retv(sd);
	fd = sd->fd;
 	if( (id=sd->status.skill[skill_id].id) <= 0 )
 		return;

	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd,0) = cmd;
	WFIFOW(fd,2) = id;
	WFIFOL(fd,4) = type?type:skill_get_inf(id);
	WFIFOW(fd,8) = sd->status.skill[skill_id].lv;
	WFIFOW(fd,10) = skill_get_sp(id,sd->status.skill[skill_id].lv);
	WFIFOW(fd,12) = range?range:skill_get_range2(&sd->bl,id,sd->status.skill[skill_id].lv, false);
#if PACKETVER < 20090715
	safestrncpy((char*)WFIFOP(fd,14), skill_get_name(id), NAME_LENGTH);
	offs = 24;
#endif
	if( sd->status.skill[skill_id].flag == SKILL_FLAG_PERMANENT )
		WFIFOB(fd,14+offs) = (sd->status.skill[skill_id].lv < skill_tree_get_max(id, sd->status.class_))? 1:0;
	else
		WFIFOB(fd,14+offs) = 0;
	WFIFOSET(fd,packet_len(cmd));
}

/*==========================================
 * Updates settings for homunculus skills.
 * Needed for Midnight Frenzy -> Sonic Claw
 * combo.
 *------------------------------------------*/
int clif_hom_skillupdateinfo(struct map_session_data *sd, int skillid, int type, int range)
{
	struct homun_data *hd;
	int fd, id, skill_id;

	nullpo_ret(sd);

	fd = sd->fd;
	hd = sd->hd;

	skill_id = skillid - HM_SKILLBASE;

	if ((id = hd->homunculus.hskill[skill_id].id) <= 0)
		return 0;

	WFIFOHEAD(fd, packet_len(0x7e1));
	WFIFOW(fd, 0) = 0x7e1;
	WFIFOW(fd, 2) = id;
	if (type)
		WFIFOL(fd, 4) = type;
	else
		WFIFOL(fd, 4) = skill_get_inf(id);
	WFIFOW(fd, 8) = hd->homunculus.hskill[skill_id].lv;
	WFIFOW(fd, 10) = skill_get_sp(id, hd->homunculus.hskill[skill_id].lv);
	if (range)
		WFIFOW(fd, 12) = range;
	else
		WFIFOW(fd, 12) = skill_get_range2(&hd->bl, id, hd->homunculus.hskill[skill_id].lv, false);

	if (hd->homunculus.hskill[id - HM_SKILLBASE].flag == 0)
		WFIFOB(fd, 14) = (hd->homunculus.hskill[skill_id].lv < hom_skill_tree_get_max(id, hd->homunculus.class_)) ? 1 : 0;
	else
		WFIFOB(fd, 14) = 0;
	WFIFOSET(fd, packet_len(0x7e1));

	return 0;
}

/// Notifies clients in area, that an object is about to use a skill.
/// 013e <src id>.L <dst id>.L <x>.W <y>.W <skill id>.W <property>.L <delaytime>.L (ZC_USESKILL_ACK)
/// 07fb <src id>.L <dst id>.L <x>.W <y>.W <skill id>.W <property>.L <delaytime>.L <is disposable>.B (ZC_USESKILL_ACK2)
/// property:
///     0 = Yellow cast aura
///     1 = Water elemental cast aura
///     2 = Earth elemental cast aura
///     3 = Fire elemental cast aura
///     4 = Wind elemental cast aura
///     5 = Poison elemental cast aura
///     6 = Holy elemental cast aura
///     ? = like 0
/// is disposable:
///     0 = yellow chat text "[src name] will use skill [skill name]."
///     1 = no text
void clif_skillcasting(struct block_list* bl, int src_id, int dst_id, int dst_x, int dst_y, int skill_id, int property, int casttime)
{
#if PACKETVER < 20091124
	const int cmd = 0x13e;
#else
	const int cmd = 0x7fb;
#endif
	unsigned char buf[32];

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = src_id;
	WBUFL(buf,6) = dst_id;
	WBUFW(buf,10) = dst_x;
	WBUFW(buf,12) = dst_y;
	WBUFW(buf,14) = skill_id;
	WBUFL(buf,16) = property<0?0:property; //Avoid sending negatives as element [Skotlex]
	WBUFL(buf,20) = casttime;
#if PACKETVER >= 20091124
	WBUFB(buf,24) = 0;  // isDisposable
#endif

	if (disguised(bl)) {
		clif_send(buf,packet_len(cmd), bl, AREA_WOS);
		WBUFL(buf,2) = -src_id;
		clif_send(buf,packet_len(cmd), bl, SELF);
	} else
		clif_send(buf,packet_len(cmd), bl, AREA);
}

/// Notifies clients in area, that an object canceled casting (ZC_DISPEL).
/// 01b9 <id>.L
void clif_skillcastcancel(struct block_list* bl)
{
	unsigned char buf[16];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x1b9;
	WBUFL(buf,2) = bl->id;
	clif_send(buf,packet_len(0x1b9), bl, AREA);
}

/// Notifies the client about the result of a skill use request (ZC_ACK_TOUSESKILL).
/// 0110 <skill id>.W <num>.L <result>.B <cause>.B
/// num (only used when skill id = NV_BASIC and cause = 0):
///     0 = "skill failed" MsgStringTable[159]
///     1 = "no emotions" MsgStringTable[160]
///     2 = "no sit" MsgStringTable[161]
///     3 = "no chat" MsgStringTable[162]
///     4 = "no party" MsgStringTable[163]
///     5 = "no shout" MsgStringTable[164]
///     6 = "no PKing" MsgStringTable[165]
///     7 = "no alligning" MsgStringTable[383]
///     ? = ignored
/// cause:
///     0 = "not enough skill level" MsgStringTable[214] (AL_WARP)
///         "steal failed" MsgStringTable[205] (TF_STEAL)
///         "envenom failed" MsgStringTable[207] (TF_POISON)
///         "skill failed" MsgStringTable[204] (otherwise)
///   ... = @see enum useskill_fail_cause
///     ? = ignored
///
/// if(result!=0) doesn't display any of the previous messages
/// Note: when this packet is received an unknown flag is always set to 0,
/// suggesting this is an ACK packet for the UseSkill packets and should be sent on success too [FlavioJS]
void clif_skill_fail(struct map_session_data *sd,int skill_id, enum useskill_fail_cause type,int btype, t_itemid itemId)
{
	int fd;

	if (!sd) {	//Since this is the most common nullpo.... 
		ShowDebug("clif_skill_fail: Error, received NULL sd for skill %d\n", skill_id);
		return;
	}
	
	fd=sd->fd;
	if (!fd) return;

	if(battle_config.display_skill_fail&1)
		return; //Disable all skill failed messages

	if(type==USESKILL_FAIL_SKILLINTERVAL && !sd->state.showdelay)
		return; //Disable delay failed messages
	
	if(skill_id == RG_SNATCHER && battle_config.display_skill_fail&4)
		return;

	if(skill_id == TF_POISON && battle_config.display_skill_fail&8)
		return;

	struct PACKET_ZC_ACK_TOUSESKILL p;

	p.packetType = 0x110;
	p.skillId = skill_id;
	p.btype = btype;
	p.itemId = itemId;
	p.flag = 0; // 0 - failed
	p.cause = type;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Skill cooldown display icon (ZC_SKILL_POSTDELAY).
/// 043d <skill ID>.W <tick>.L
/// Note: This is now often used to start cooldown times for renewal skills.
/// Because of this and its effect on the display of skill icons in the shortcut bar,
/// the ZC_SKILL_POSTDELAY_LIST and ZC_SKILL_POSTDELAY_LIST2 will need to be supported
/// soon to tell the client which skills are currently in cooldown when a player logs on
/// and display them in the shortcut bar. [Rytech]
void clif_skill_cooldown(struct map_session_data *sd, int skill_id, unsigned int duration)
{
#if PACKETVER>=20081112
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x43d));
	WFIFOW(fd,0) = 0x43d;
	WFIFOW(fd,2) = skill_id;
	WFIFOL(fd,4) = duration;
	WFIFOSET(fd,packet_len(0x43d));
#endif
}

/// Skill attack effect and damage.
/// 0114 <skill id>.W <src id>.L <dst id>.L <tick>.L <src delay>.L <dst delay>.L <damage>.W <level>.W <div>.W <type>.B (ZC_NOTIFY_SKILL)
/// 01de <skill id>.W <src id>.L <dst id>.L <tick>.L <src delay>.L <dst delay>.L <damage>.L <level>.W <div>.W <type>.B (ZC_NOTIFY_SKILL2)
int clif_skill_damage(struct block_list *src,struct block_list *dst,int64 tick,int sdelay,int ddelay,int64 sdamage,int div,int skill_id,int skill_lv,int type)
{
	unsigned char buf[64];
	struct status_change *sc;

	int damage = (int)cap_value(sdamage, INT_MIN, INT_MAX);

	nullpo_ret(src);
	nullpo_ret(dst);

	type = clif_calc_delay(type,div,damage,ddelay);
	sc = status_get_sc(dst);
	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION] && damage)
			damage = clif_hallucination_damage();
	}

#if PACKETVER < 3
	WBUFW(buf,0)=0x114;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=(uint32)tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFW(buf,24)=damage?div:0;
	} else {
		WBUFW(buf,24)=damage;
	}
	WBUFW(buf,26)=skill_lv;
	WBUFW(buf,28)=div;
	WBUFB(buf,30)=type;
	if (disguised(dst)) {
		clif_send(buf,packet_len(0x114),dst,AREA_WOS);
		WBUFL(buf,8)=-dst->id;
		clif_send(buf,packet_len(0x114),dst,SELF);
	} else
		clif_send(buf,packet_len(0x114),dst,AREA);

	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if (disguised(dst)) 
			WBUFL(buf,8)=dst->id;
		if(damage > 0)
			WBUFW(buf,24)=-1;
		clif_send(buf,packet_len(0x114),src,SELF);
	}
#else
	WBUFW(buf,0)=0x1de;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=(uint32)tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFL(buf,24)=damage?div:0;
	} else {
		WBUFL(buf,24)=damage;
	}
	WBUFW(buf,28)=skill_lv;
	WBUFW(buf,30)=div;
	// For some reason, late 2013 and newer clients have
	// a issue that causes players and monsters to endure
	// type 6 (ACTION_SKILL) skills. So we have to do a small
	// hack to set all type 6 to be sent as type 8 ACTION_ATTACK_MULTIPLE
#if PACKETVER < 20131223
 	WBUFB(buf,32)=type;
#else
	WBUFB(buf,32)=(type==6)?8:type;
#endif
	if (disguised(dst)) {
		clif_send(buf,packet_len(0x1de),dst,AREA_WOS);
		WBUFL(buf,8)=-dst->id;
		clif_send(buf,packet_len(0x1de),dst,SELF);
	} else
		clif_send(buf,packet_len(0x1de),dst,AREA);

	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if (disguised(dst))
			WBUFL(buf,8)=dst->id;
		if(damage > 0)
			WBUFL(buf,24)=-1;
		clif_send(buf,packet_len(0x1de),src,SELF);
	}
#endif

	//Because the damage delay must be synced with the client, here is where the can-walk tick must be updated. [Skotlex]
	return clif_calc_walkdelay(dst,ddelay,type,damage,div);
}

/// Ground skill attack effect and damage (ZC_NOTIFY_SKILL_POSITION).
/// 0115 <skill id>.W <src id>.L <dst id>.L <tick>.L <src delay>.L <dst delay>.L <x>.W <y>.W <damage>.W <level>.W <div>.W <type>.B
/*
int clif_skill_damage2(struct block_list *src,struct block_list *dst,int64 tick,int sdelay,int ddelay,int damage,int div,int skill_id,int skill_lv,int type)
{
	unsigned char buf[64];
	struct status_change *sc;

	nullpo_ret(src);
	nullpo_ret(dst);

	type = (type>0)?type:skill_get_hit(skill_id);
	type = clif_calc_delay(type,div,damage,ddelay);
	sc = status_get_sc(dst);

	if(sc && sc->count) {
		if(sc->data[SC_HALLUCINATION] && damage)
			damage = clif_hallucination_damage();
	}

	WBUFW(buf,0)=0x115;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFL(buf,8)=dst->id;
	WBUFL(buf,12)=(uint32)tick;
	WBUFL(buf,16)=sdelay;
	WBUFL(buf,20)=ddelay;
	WBUFW(buf,24)=dst->x;
	WBUFW(buf,26)=dst->y;
	if (battle_config.hide_woe_damage && map_flag_gvg(src->m)) {
		WBUFW(buf,28)=damage?div:0;
	} else {
		WBUFW(buf,28)=damage;
	}
	WBUFW(buf,30)=skill_lv;
	WBUFW(buf,32)=div;
	WBUFB(buf,34)=type;
	clif_send(buf,packet_len(0x115),src,AREA);
	if(disguised(src)) {
		WBUFL(buf,4)=-src->id;
		if(damage > 0)
			WBUFW(buf,28)=-1;
		clif_send(buf,packet_len(0x115),src,SELF);
	}
	if (disguised(dst)) {
		WBUFL(buf,8)=-dst->id;
		if (disguised(src))
			WBUFL(buf,4)=src->id;
		else if(damage > 0)
			WBUFW(buf,28)=-1;
		clif_send(buf,packet_len(0x115),dst,SELF);
	}

	//Because the damage delay must be synced with the client, here is where the can-walk tick must be updated. [Skotlex]
	return clif_calc_walkdelay(dst,ddelay,type,damage,div);
}
*/

/// Non-damaging skill effect.
/// 011a <skill id>.W <skill lv>.W <dst id>.L <src id>.L <result>.B (ZC_USE_SKILL)
/// 09cb <skill id>.W <skill lv>.L <dst id>.L <src id>.L <result>.B (ZC_USE_SKILL2)
int clif_skill_nodamage(struct block_list *src,struct block_list *dst,int skill_id,int heal, int64 tick)
{
	unsigned char buf[32];
	short offset = 0;
#if PACKETVER < 20131223
	short packet_num = 0x11a;
#else
	short packet_num = 0x9cb;
#endif

	bool success = (tick != 0);

	nullpo_ret(dst);

	WBUFW(buf, 0) = packet_num;
	WBUFW(buf,2)=skill_id;
#if PACKETVER < 20131223
	WBUFW(buf,4)=min(heal, INT16_MAX);
#else
	WBUFL(buf,4)=min(heal, INT_MAX);
	offset += 2;
#endif
	WBUFL(buf,6+offset)=dst->id;
	WBUFL(buf,10+offset)=src?src->id:0;
	WBUFB(buf,14+offset)=success;

	if (disguised(dst)) {
		clif_send(buf, packet_len(packet_num), dst, AREA_WOS);
		WBUFL(buf, 6 + offset) = -dst->id;
		clif_send(buf, packet_len(packet_num), dst, SELF);
	} else
		clif_send(buf, packet_len(packet_num), dst, AREA);

	if(src && disguised(src)) {
		WBUFL(buf, 10 + offset) = -src->id;
		if (disguised(dst))
			WBUFL(buf, 6 + offset) = dst->id;
		clif_send(buf, packet_len(packet_num), src, SELF);
	}

	return success;
}

/// Non-damaging ground skill effect (ZC_NOTIFY_GROUNDSKILL).
/// 0117 <skill id>.W <src id>.L <level>.W <x>.W <y>.W <tick>.L
void clif_skill_poseffect(struct block_list *src,int skill_id,int val,int x,int y,int64 tick)
{
	unsigned char buf[32];

	nullpo_retv(src);

	WBUFW(buf,0)=0x117;
	WBUFW(buf,2)=skill_id;
	WBUFL(buf,4)=src->id;
	WBUFW(buf,8)=val;
	WBUFW(buf,10)=x;
	WBUFW(buf,12)=y;
	WBUFL(buf,14)=(uint32)tick;
	if(disguised(src)) {
		clif_send(buf,packet_len(0x117),src,AREA_WOS);
		WBUFL(buf,4)=-src->id;
		clif_send(buf,packet_len(0x117),src,SELF);
	} else
		clif_send(buf,packet_len(0x117),src,AREA);
}

/// Presents a list of available warp destinations (ZC_WARPLIST).
/// 011c <skill id>.W { <map name>.16B }*4
void clif_skill_warppoint(struct map_session_data* sd, short skill_id, short skill_lv, unsigned short map1, unsigned short map2, unsigned short map3, unsigned short map4) {
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x11c));
	WFIFOW(fd,0) = 0x11c;
	WFIFOW(fd,2) = skill_id;
	memset(WFIFOP(fd,4), 0x00, 4*MAP_NAME_LENGTH_EXT);
	if (map1 == (unsigned short)-1) strcpy((char*)WFIFOP(fd,4), "Random");
	else // normal map name
	if (map1 > 0) mapindex_getmapname_ext(mapindex_id2name(map1), (char*)WFIFOP(fd,4));
	if (map2 > 0) mapindex_getmapname_ext(mapindex_id2name(map2), (char*)WFIFOP(fd,20));
	if (map3 > 0) mapindex_getmapname_ext(mapindex_id2name(map3), (char*)WFIFOP(fd,36));
	if (map4 > 0) mapindex_getmapname_ext(mapindex_id2name(map4), (char*)WFIFOP(fd,52));
	WFIFOSET(fd,packet_len(0x11c));

	sd->menuskill_id = skill_id;
	if (skill_id == AL_WARP) {
		sd->menuskill_val = (sd->ud.skillx<<16)|sd->ud.skilly; //Store warp position here.
		sd->state.workinprogress = WIP_DISABLE_ALL;
	} else
		sd->menuskill_val = skill_lv;
}


/// Memo message (ZC_ACK_REMEMBER_WARPPOINT).
/// 011e <type>.B
/// type:
///     0 = "Saved location as a Memo Point for Warp skill." in color 0xFFFF00 (cyan)
///     1 = "Skill Level is not high enough." in color 0x0000FF (red)
///     2 = "You haven't learned Warp." in color 0x0000FF (red)
///
/// @param sd Who receives the message
/// @param type What message
void clif_skill_memomessage(struct map_session_data* sd, int type)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x11e));
	WFIFOW(fd,0)=0x11e;
	WFIFOB(fd,2)=type;
	WFIFOSET(fd,packet_len(0x11e));
}


/// Teleport message (ZC_NOTIFY_MAPINFO).
/// 0189 <type>.W
/// type:
///     0 = "Unable to Teleport in this area" in color 0xFFFF00 (cyan)
///     1 = "Saved point cannot be memorized." in color 0x0000FF (red)
///
/// @param sd Who receives the message
/// @param type What message
void clif_skill_teleportmessage(struct map_session_data *sd, int type)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x189));
	WFIFOW(fd,0)=0x189;
	WFIFOW(fd,2)=type;
	WFIFOSET(fd,packet_len(0x189));
}


/// Displays Sense (WZ_ESTIMATION) information window (ZC_MONSTER_INFO).
/// 018c <class>.W <level>.W <size>.W <hp>.L <def>.W <race>.W <mdef>.W <element>.W
///     <water%>.B <earth%>.B <fire%>.B <wind%>.B <poison%>.B <holy%>.B <shadow%>.B <ghost%>.B <undead%>.B
void clif_skill_estimation(struct map_session_data *sd,struct block_list *dst)
{
	struct status_data *status;
	unsigned char buf[64];
	int i,fix;

	nullpo_retv(sd);
	nullpo_retv(dst);

	if( dst->type != BL_MOB )
		return;

	status = status_get_status_data(dst);

	WBUFW(buf, 0)=0x18c;
	WBUFW(buf, 2)=status_get_class(dst);
	WBUFW(buf, 4)=status_get_lv(dst);
	WBUFW(buf, 6)=status->size;
	WBUFL(buf, 8)=status->hp;
	WBUFW(buf,12)= (battle_config.estimation_type&1?status->def:0)
		+(battle_config.estimation_type&2?status->def2:0);
	WBUFW(buf,14)=status->race;
	WBUFW(buf,16)= (battle_config.estimation_type&1?status->mdef:0)
		+(battle_config.estimation_type&2?status->mdef2:0);
	WBUFW(buf,18)= status->def_ele;
	for(i=0;i<9;i++)
//		The following caps negative attributes to 0 since the client displays them as 255-fix. [Skotlex]
		WBUFB(buf,20+i)= (unsigned char)((fix=battle_attr_ratio(i+1,status->def_ele, status->ele_lv))<0?0:fix);

	clif_send(buf,packet_len(0x18c),&sd->bl,sd->status.party_id>0?PARTY_SAMEMAP:SELF);
}


/// Presents a textual list of producable items (ZC_MAKABLEITEMLIST).
/// 018d <packet len>.W { <name id>.W { <material id>.W }*3 }*
/// material id:
///     unused by the client
void clif_skill_produce_mix_list(struct map_session_data *sd, int skill_id, int trigger) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	if (sd->menuskill_id == skill_id)
		return; //Avoid resending the menu twice or more times...
	if (skill_id == GC_CREATENEWPOISON)
		skill_id = GC_RESEARCHNEWPOISON;

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKABLEITEMLIST) + MAX_SKILL_PRODUCE_DB * sizeof(struct PACKET_ZC_MAKABLEITEMLIST_sub));
	struct PACKET_ZC_MAKABLEITEMLIST *p = (struct PACKET_ZC_MAKABLEITEMLIST *)WFIFOP(fd, 0);
	p->packetType = 0x18d;

	int count = 0;
	for (int i = 0; i < MAX_SKILL_PRODUCE_DB; i++) {
		if (skill_can_produce_mix(sd, skill_produce_db[i].nameid, trigger, 1) &&
			(skill_id <= 0 || (skill_id > 0 && skill_produce_db[i].req_skill == skill_id))
			)
		{
			p->items[count].itemId = client_nameid(skill_produce_db[i].nameid);
			p->items[count].material[0] = 0;
			p->items[count].material[1] = 0;
			p->items[count].material[2] = 0;
			count++;
		}
	}

	p->packetLength = sizeof(struct PACKET_ZC_MAKABLEITEMLIST) + count * sizeof(struct PACKET_ZC_MAKABLEITEMLIST_sub);
	WFIFOSET(fd, p->packetLength);

	if (count > 0) {
		sd->menuskill_id = skill_id;
		sd->menuskill_val = trigger;
		return;
	}
}

/// Present a list of producable items (ZC_MAKINGITEM_LIST).
/// 025a <packet len>.W <mk type>.W { <name id>.W }*
/// mk type:
///     1 = cooking
///     2 = arrow
///     3 = elemental
///     4 = GN_MIX_COOKING
///     5 = GN_MAKEBOMB
///     6 = GN_S_PHARMACY
void clif_cooking_list(struct map_session_data *sd, int trigger, uint16 skill_id, int qty, int list_type) {
	nullpo_retv(sd);

	// Avoid resending the menu twice or more times...
	if (sd->menuskill_id == skill_id) {
		return;
	}

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_MAKINGITEM_LIST) + MAX_SKILL_PRODUCE_DB * sizeof(struct PACKET_ZC_MAKINGITEM_LIST_sub));
	struct PACKET_ZC_MAKINGITEM_LIST *p = (struct PACKET_ZC_MAKINGITEM_LIST *)WFIFOP(fd, 0);
	p->packetType = 0x25a;
	p->makeItem = list_type; // list type

	int count = 0;

	for (int i = 0; i < MAX_SKILL_PRODUCE_DB; i++) {
		if (!skill_can_produce_mix(sd, skill_produce_db[i].nameid, trigger, qty)) {
			continue;
		}

		p->items[count].itemId = client_nameid(skill_produce_db[i].nameid);
		count++;
	}

	if (count > 0 || skill_id == AM_PHARMACY) {
		p->packetLength = sizeof(struct PACKET_ZC_MAKINGITEM_LIST) + count * sizeof(struct PACKET_ZC_MAKINGITEM_LIST_sub);
		WFIFOSET(fd, p->packetLength);

		sd->menuskill_id = skill_id;
		sd->menuskill_val = trigger;
		sd->menuskill_val2 = qty; // amount.
	}
	else {
		sd->menuskill_id = sd->menuskill_val = 0;
#if PACKETVER >= 20090922
		clif_msg_skill(sd, skill_id, INVENTORY_SPACE_FULL);
#else
		p->packetLength = sizeof(struct PACKET_ZC_MAKINGITEM_LIST) + count * sizeof(struct PACKET_ZC_MAKINGITEM_LIST_sub);
		WFIFOSET(fd, p->packetLength);
#endif
	}
}

/* Sends status effect to clients around the bl
 * @param bl Object that has the effect
 * @param type Status icon see enum si_type
 * @param flag 1:Active, 0:Deactive
 * @param tick Duration in ms
 * @param val1
 * @param val2
 * @param val3
 */
void clif_status_change(struct block_list *bl, int type, int flag, int64 tick, int val1, int val2, int val3) {
	struct map_session_data *sd = NULL;

	if (type == SI_BLANK)  //It shows nothing on the client...
		return;

	if (type == SI_ACTIONDELAY && tick == 0)
		return;

	if (type == SI_HALLUCINATION && !battle_config.display_hallucination) // Disable Hallucination.
		return;

	nullpo_retv(bl);

	sd = BL_CAST(BL_PC, bl);

	if (!(status_type2relevant_bl_types(type)&bl->type)) // only send status changes that actually matter to the client
		return;

	clif_status_change_sub(bl, bl->id, type, flag, tick, val1, val2, val3, ((sd ? (pc_isinvisible(sd) ? SELF : AREA) : AREA_WOS)));
}

/// Notifies clients of a status change.
/// 0196 <index>.W <id>.L <state>.B (ZC_MSG_STATE_CHANGE) [used for ending status changes and starting them on non-pc units (when needed)]
/// 043f <index>.W <id>.L <state>.B <remain msec>.L { <val>.L }*3 (ZC_MSG_STATE_CHANGE2) [used exclusively for starting statuses on pcs]
/// 0983 <index>.W <id>.L <state>.B <total msec>.L <remain msec>.L { <val>.L }*3 (ZC_MSG_STATE_CHANGE3) (PACKETVER >= 20120618)
/// @param bl Sends packet to clients around this object
/// @param id ID of object that has this effect
/// @param type Status icon see enum si_type
/// @param flag 1:Active, 0:Deactive
/// @param tick Duration in ms
/// @param val1
/// @param val2
/// @param val3
void clif_status_change_sub(struct block_list *bl, int id, int type, int flag, int64 tick, int val1, int val2, int val3, enum send_target target_type)
{
	unsigned char buf[32];

	if (type == SI_BLANK)  //It shows nothing on the client...
		return;

	if (type == SI_ACTIONDELAY && tick == 0)
		return;

	nullpo_retv(bl);

#if PACKETVER >= 20120618
	if (flag && battle_config.display_status_timers)
		WBUFW(buf, 0) = 0x983;
	else
#elif PACKETVER >= 20090121
	if (flag && battle_config.display_status_timers)
		WBUFW(buf, 0) = 0x43f;
	else
#endif
		WBUFW(buf, 0) = 0x196;
	WBUFW(buf, 2) = type;
	WBUFL(buf, 4) = id;
	WBUFB(buf, 8) = flag;
#if PACKETVER >= 20120618
	if (flag && battle_config.display_status_timers) {
		if (tick <= 0)
			tick = 9999; // this is indeed what official servers do

		WBUFL(buf, 9) = (unsigned int)tick;/* at this stage remain and total are the same value I believe */
		WBUFL(buf, 13) = (unsigned int)tick;
		WBUFL(buf, 17) = val1;
		WBUFL(buf, 21) = val2;
		WBUFL(buf, 25) = val3;
	}
#elif PACKETVER >= 20090121
	if (flag && battle_config.display_status_timers) {
		if (tick <= 0)
			tick = 9999; // this is indeed what official servers do

		WBUFL(buf, 9) = (unsigned int)tick;
		WBUFL(buf, 13) = val1;
		WBUFL(buf, 17) = val2;
		WBUFL(buf, 21) = val3;
	}
#endif
	clif_send(buf, packet_len(WBUFW(buf, 0)), bl, target_type);
}

/**
 * Send any active EFST to those around.
 * @param tbl: Unit to send the packet to
 * @param bl: Objects walking into view
 * @param target: Client send type
 */
void clif_efst_status_change_sub(struct block_list *tbl, struct block_list *bl, enum send_target target) {
	unsigned char i;
	struct sc_display_entry **sc_display;
	unsigned char sc_display_count;

	nullpo_retv(bl);

	switch( bl->type ){
		case BL_PC: {
			struct map_session_data* sd = (struct map_session_data*)bl;

			sc_display = sd->sc_display;
			sc_display_count = sd->sc_display_count;
			}
			break;
		case BL_NPC: {
			struct npc_data* nd = (struct npc_data*)bl;

			sc_display = nd->sc_display;
			sc_display_count = nd->sc_display_count;
			}
			break;
		default:
			return;
	}

	for (i = 0; i < sc_display_count; i++) {
		enum sc_type type = sc_display[i]->type;
		struct status_change *sc = status_get_sc(bl);
		const struct TimerData *td = (sc && sc->data[type] ? get_timer(sc->data[type]->timer) : NULL);
		int64 tick = 0;

		if (td)
			tick = DIFF_TICK(td->tick, gettick());
#if PACKETVER > 20120418
		clif_efst_status_change(tbl, bl->id, target, StatusIconChangeTable[type], tick, sc_display[i]->val1, sc_display[i]->val2, sc_display[i]->val3);
#else
		clif_status_change_sub(tbl, bl->id, StatusIconChangeTable[type], 1, tick, sc_display[i]->val1, sc_display[i]->val2, sc_display[i]->val3, target);
#endif
	}
}

/// Notifies clients in a area of a player entering the screen with a active EFST status. (Need A Confirm. [Rytech])
/// 08ff <id>.L <index>.W <remain msec>.L { <val>.L }*3 (ZC_EFST_SET_ENTER) (PACKETVER >= 20111108)
/// 0984 <id>.L <index>.W <total msec>.L <remain msec>.L { <val>.L }*3 (ZC_EFST_SET_ENTER2) (PACKETVER >= 20120618)

void clif_efst_status_change(struct block_list *bl, int tid, enum send_target target, int type, int64 tick, int val1, int val2, int val3)
{
	unsigned char buf[32];

	if (type == SI_BLANK) //It shows nothing on the client...
		return;

	nullpo_retv(bl);

	if (tick <= 0)
		tick = 9999;

#if PACKETVER >= 20130320
	WBUFW(buf, 0) = 0x984;
#else
	WBUFW(buf, 0) = 0x8ff;
#endif
	WBUFL(buf, 2) = tid;
	WBUFW(buf, 6) = type;
#if PACKETVER >= 20130320
	WBUFL(buf, 8) = (unsigned int)tick;
	WBUFL(buf, 12) = (unsigned int)tick;
	WBUFL(buf, 16) = val1;
	WBUFL(buf, 20) = val2;
	WBUFL(buf, 24) = val3;
#else
	WBUFL(buf, 8) = (unsigned int)tick;
	WBUFL(buf, 12) = val1;
	WBUFL(buf, 16) = val2;
	WBUFL(buf, 20) = val3;
#endif
	clif_send(buf, packet_len(WBUFW(buf, 0)), bl, target);
}

/// Notification about an another object's chat message (ZC_NOTIFY_CHAT).
/// 008d <packet len>.W <id>.L <message>.?B
void clif_notify_chat(struct block_list* bl, const char* message, send_target target)
{
	uint8 buf[8 + CHAT_SIZE_MAX];
	uint16 length;

	if( !message[0] )
	{// empty message
		return;
	}

	length = (uint16)strlen(message);

	if( length > sizeof(buf)-9 )
	{
		ShowWarning("clif_notify_chat: Truncated message '%s' (len=%u, max=%u, id=%d, target=%d).\n", message, length, sizeof(buf)-9, bl ? bl->id : 0, target);
		length = sizeof(buf)-9;
	}

	WBUFW(buf,0) = 0x8d;
	WBUFW(buf,2) = 8+length+1;  // header + message + NUL
	WBUFL(buf,4) = bl ? bl->id : 0;
	safestrncpy((char*)WBUFP(buf,8), message, length+1);
	clif_send(buf, WBUFW(buf,2), bl, target);
}

/// Notification about player's own chat message (ZC_NOTIFY_PLAYERCHAT).
/// 008e <packet len>.W <message>.?B
void clif_notify_playerchat(struct map_session_data* sd, const char* message)
{
	int fd = sd->fd;
	uint16 length;

	if( !message[0] )
	{// don't send a void message (it's not displaying on the client chat). @help can send void line.
		return;
	}

	length = (uint16)strlen(message);

	if( length > 255 )
	{// message is limited to 255+1 characters by the client-side buffer
		ShowWarning("clif_notify_playerchat: Truncated message '%s' (len=%u, max=%d, char_id=%d).\n", message, length, 255, sd->status.char_id);
		length = 255;
	}

	WFIFOHEAD(fd,4+length+1);
	WFIFOW(fd,0) = 0x8e;
	WFIFOW(fd,2) = 4+length+1;  // header + message + NUL
	safestrncpy((char*)WFIFOP(fd,4), message, length+1);
	WFIFOSET(fd,WFIFOW(fd,2));
}

/// Send message (modified by [Yor]) (ZC_NOTIFY_PLAYERCHAT).
/// 008e <packet len>.W <message>.?B
void clif_displaymessage(const int fd, const char* mes)
{
	nullpo_retv(mes);

	if (session_isActive(fd)) {
		char *message, *line;

		message = aStrdup(mes);
		line = strtok(message, "\n");

		while (line != NULL) {
#if PACKETVER == 20141022
			/** for some reason game client crashes depending on message pattern (only for this packet) **/
			/** so we redirect to ZC_NPC_CHAT **/
			clif_disp_overheadcolor_self(fd, COLOR_DEFAULT, line);
#else
			// Limit message to 255+1 characters (otherwise it causes a buffer overflow in the client)
			int len = strnlen(line, CHAT_SIZE_MAX);

			if (len > 0) { // don't send a void message (it's not displaying on the client chat). @help can send void line.
				WFIFOHEAD(fd, 5 + len);
				WFIFOW(fd, 0) = 0x8e;
				WFIFOW(fd, 2) = 5 + len; // 4 + len + NULL teminate
				safestrncpy((char *)WFIFOCP(fd, 4), line, len + 1);
				WFIFOSET(fd, 5 + len);
			}
#endif
			line = strtok(NULL, "\n");
		}
		aFree(message);
	}
}

/// Monster/NPC color chat [SnakeDrak] (ZC_NPC_CHAT).
/// 02c1 <packet len>.W <id>.L <color>.L <message>.?B
void clif_displaymessagecolor_target(struct block_list *bl, unsigned long color, const char *msg, bool rgb2bgr, enum send_target type, struct map_session_data *sd)
{
	unsigned short msg_len = (unsigned short)(strlen(msg) + 1);
	uint8 buf[CHAT_SIZE_MAX];

	nullpo_retv(bl);

	if (msg_len > sizeof(buf) - 12) {
		ShowWarning("clif_displaymessagecolor_target: Truncating too long message '%s' (len=%u).\n", msg, msg_len);
		msg_len = sizeof(buf) - 12;
	}
	if (rgb2bgr)
		color = (color & 0x0000FF) << 16 | (color & 0x00FF00) | (color & 0xFF0000) >> 16; // RGB to BGR
	WBUFW(buf, 0) = 0x2C1;
	WBUFW(buf, 2) = msg_len + 12;
	WBUFL(buf, 4) = bl->id;
	WBUFL(buf, 8) = color;
	memcpy(WBUFCP(buf, 12), msg, msg_len);
	clif_send(buf, WBUFW(buf, 2), (sd == NULL ? bl : &(sd->bl)), type);
}

/// Send colored message to self
void clif_displaymessagecolor(struct map_session_data *sd, const char* msg, unsigned long color)
{
	if (sd==NULL)
		return;

	clif_displaymessagecolor_target(&sd->bl, color, msg, true, SELF, NULL);
}


/// Send formatted message
void clif_displayformatted(struct map_session_data* sd, const char* fmt, ...)
{
	int n;
	char buf[255+1];
	va_list args;

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if( n < 1 || n >= sizeof(buf) )
	{
		ShowError("Too long message with format '%s' (n=%d).\n", fmt, n);
		return;
	}

	clif_notify_playerchat(sd, buf);
}


/// Send broadcast message in yellow or blue without font formatting (ZC_BROADCAST).
/// 009a <packet len>.W <message>.?B
void clif_broadcast(struct block_list* bl, const char* mes, int len, int type, enum send_target target)
{
	int lp = (type&BC_COLOR_MASK) ? 4 : 0;
	unsigned char *buf = (unsigned char*)aMalloc((4 + lp + len)*sizeof(unsigned char));

	WBUFW(buf,0) = 0x9a;
	WBUFW(buf,2) = 4 + lp + len;
	if (type&BC_BLUE)
		WBUFL(buf,4) = 0x65756c62; //If there's "blue" at the beginning of the message, game client will display it in blue instead of yellow.
	else if (type&BC_WOE)
		WBUFL(buf,4) = 0x73737373; //If there's "ssss", game client will recognize message as 'WoE broadcast'.
	memcpy(WBUFP(buf, 4 + lp), mes, len);
	clif_send(buf, WBUFW(buf,2), bl, target);
	
	if (buf)
		aFree(buf);
}

/*==========================================
 * Displays a message on a 'bl' to all it's nearby clients
 * Used by npc_globalmessage
 *------------------------------------------*/
void clif_GlobalMessage(struct block_list* bl, const char* message)
{
	clif_notify_chat(bl, message, ALL_CLIENT);
}

/*==========================================
 * Send main chat message [LuzZza]
 *------------------------------------------*/
void clif_MainChatMessage(const char* message)
{
	clif_notify_chat(NULL, message, CHAT_MAINCHAT);
}

/// Send broadcast message with font formatting (ZC_BROADCAST2).
/// 01c3 <packet len>.W <fontColor>.L <fontType>.W <fontSize>.W <fontAlign>.W <fontY>.W <message>.?B
void clif_broadcast2(struct block_list* bl, const char* mes, int len, unsigned long fontColor, short fontType, short fontSize, short fontAlign, short fontY, enum send_target target)
{
	unsigned char *buf = (unsigned char*)aMalloc((16 + len)*sizeof(unsigned char));

	WBUFW(buf,0)  = 0x1c3;
	WBUFW(buf,2)  = len + 16;
	WBUFL(buf,4)  = fontColor;
	WBUFW(buf,8)  = fontType;
	WBUFW(buf,10) = fontSize;
	WBUFW(buf,12) = fontAlign;
	WBUFW(buf,14) = fontY;
	memcpy(WBUFP(buf,16), mes, len);
	clif_send(buf, WBUFW(buf,2), bl, target);

	if (buf)
		aFree(buf);
}


/// Displays heal effect.
/// 013d <var id>.W <amount>.W (ZC_RECOVERY)
/// 0a27 <var id>.W <amount>.L (ZC_RECOVERY2)
/// var id:
///     5 = HP (SP_HP)
///     7 = SP (SP_SP)
///     ? = ignored
void clif_heal(int fd,int type,int val) {
#if PACKETVER < 20150513
	const int cmd = 0x13d;
#else
	const int cmd = 0xa27;
#endif

	WFIFOHEAD(fd, packet_len(cmd));
	WFIFOW(fd,0) = cmd;
	WFIFOW(fd,2) = type;
#if PACKETVER < 20150513
	WFIFOW(fd,4) = min(val, INT16_MAX);
#else
	WFIFOL(fd,4) = min(val, INT32_MAX);
#endif
	WFIFOSET(fd, packet_len(cmd));
}


/// Displays resurrection effect (ZC_RESURRECTION).
/// 0148 <id>.L <type>.W
/// type:
///     ignored
void clif_resurrection(struct block_list *bl,int type)
{
	unsigned char buf[16];

	nullpo_retv(bl);

	WBUFW(buf,0)=0x148;
	WBUFL(buf,2)=bl->id;
	WBUFW(buf,6)=0;

	clif_send(buf,packet_len(0x148),bl,type==1 ? AREA : AREA_WOS);
	if (disguised(bl))
		clif_spawn(bl);
}


/// Sets the map property.
/// 0199 <type>.W (ZC_NOTIFY_MAPPROPERTY)
/// 099b <type>.W <flags>.L (ZC_MAPPROPERTY_R2)
void clif_map_property(struct block_list *bl, enum map_property property, enum send_target target)
{
#if PACKETVER >= 20121010
	short cmd = 0x99b;
	unsigned char buf[8];
#else
	short cmd = 0x199;
	unsigned char buf[4];
#endif

	WBUFW(buf, 0) = cmd;
	WBUFW(buf, 2) = property;

#if PACKETVER >= 20121010
	WBUFL(buf, 4) = 
		((map[bl->m].flag.pvp ? 1 : 0) << 0) |// PARTY - Show attack cursor on non-party members (PvP)
		((map[bl->m].flag.battleground ? 1 : 0) || (map_flag_gvg2(bl->m) ? 1 : 0) << 1) |// GUILD - Show attack cursor on non-guild members (GvG)
		((map[bl->m].flag.battleground ? 1 : 0) || (map_flag_gvg2(bl->m) ? 1 : 0) << 2) |// SIEGE - Show emblem over characters heads when in GvG (WoE castle)
		(!(map_flag_gvg2(bl->m) ? 1 : 0) << 3) |// USE_SIMPLE_EFFECT - Automatically enable /mineffect
		((map_flag_vs(bl->m) ? 1 : 0) << 4) |// DISABLE_LOCKON - Unknown (By the name it might disable cursor lock-on)
		((map[bl->m].flag.pvp ? 1 : 0) << 5) |// COUNT_PK - Show the PvP counter
		((map[bl->m].flag.partylock ? 1 : 0) << 6) |// NO_PARTY_FORMATION - Prevents party creation/modification (Might be used for instance dungeons)
		((map[bl->m].flag.battleground ? 1 : 0) << 7) |// BATTLEFIELD - Unknown (Does something for battlegrounds areas)
		((map[bl->m].flag.nocostume ? 1 : 0) << 8) |// DISABLE_COSTUMEITEM - Disable costume sprites
		(1 << 9) |// USECART - Allow opening cart inventory (Well force it to always allow it)
		((map[bl->m].flag.nosunmoonstarmiracle ? 0 : 1) << 10);// SUNMOONSTAR_MIRACLE - Blocks Star Gladiator's Miracle from activating
				  //(1<<11);// Unused bits. 1 - 10 is 0x1 length and 11 is 0x15 length. May be used for future settings.
#endif
	clif_send(buf, packet_len(cmd), bl, target);
}

/// Set the map type (ZC_NOTIFY_MAPPROPERTY2).
/// 01d6 <type>.W
void clif_map_type(struct map_session_data* sd, enum map_type type)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1D6));
	WFIFOW(fd,0)=0x1D6;
	WFIFOW(fd,2)=type;
	WFIFOSET(fd,packet_len(0x1D6));
}

/// Updates PvP ranking (ZC_NOTIFY_RANKING).
/// 019a <id>.L <ranking>.L <total>.L
void clif_pvpset(struct map_session_data *sd,int pvprank,int pvpnum,int type)
{
	if(type == 2) {
		int fd = sd->fd;
		WFIFOHEAD(fd,packet_len(0x19a));
		WFIFOW(fd,0) = 0x19a;
		WFIFOL(fd,2) = sd->bl.id;
		WFIFOL(fd,6) = pvprank;
		WFIFOL(fd,10) = pvpnum;
		WFIFOSET(fd,packet_len(0x19a));
	} else {
		unsigned char buf[32];
		WBUFW(buf,0) = 0x19a;
		WBUFL(buf,2) = sd->bl.id;
		if(sd->sc.option&(OPTION_HIDE|OPTION_CLOAK))
			WBUFL(buf,6) = UINT32_MAX; //On client displays as --
		else
			WBUFL(buf,6) = pvprank;
		WBUFL(buf,10) = pvpnum;
		if(sd->sc.option&OPTION_INVISIBLE || sd->disguise) //Causes crashes when a 'mob' with pvp info dies.
			clif_send(buf,packet_len(0x19a),&sd->bl,SELF);
		else if(!type)
			clif_send(buf,packet_len(0x19a),&sd->bl,AREA);
		else
			clif_send(buf,packet_len(0x19a),&sd->bl,ALL_SAMEMAP);
	}
}


/*==========================================
 *
 *------------------------------------------*/
void clif_map_property_mapall(int map, enum map_property property)
{
	struct block_list bl;

	bl.id = 0;
	bl.type = BL_NUL;
	bl.m = map;

	clif_map_property(&bl, property, ALL_SAMEMAP);
}


/// Notifies the client about the result of a refine attempt (ZC_ACK_ITEMREFINING).
/// 0188 <result>.W <index>.W <refine>.W
/// result:
///     0 = success
///     1 = failure
///     2 = downgrade
void clif_refine(int fd, int fail, int index, int val)
{
	WFIFOHEAD(fd,packet_len(0x188));
	WFIFOW(fd,0)=0x188;
	WFIFOW(fd,2)=fail;
	WFIFOW(fd,4)=index+2;
	WFIFOW(fd,6)=val;
	WFIFOSET(fd,packet_len(0x188));
}


/// Notifies the client about the result of a weapon refine attempt (ZC_ACK_WEAPONREFINE).
/// 0223 <result>.L <nameid>.W
/// result:
///     0 = "weapon upgraded: %s" MsgStringTable[911] in rgb(0,255,255)
///     1 = "weapon upgraded: %s" MsgStringTable[912] in rgb(0,205,205)
///     2 = "cannot upgrade %s until you level up the upgrade weapon skill" MsgStringTable[913] in rgb(255,200,200)
///     3 = "you lack the item %s to upgrade the weapon" MsgStringTable[914] in rgb(255,200,200)
void clif_upgrademessage(int fd, int result, t_itemid item_id)
{
	struct PACKET_ZC_ACK_WEAPONREFINE p;

	p.packetType = 0x223;
	p.result = result;
	p.itemId = client_nameid(item_id);
}


/// Whisper is transmitted to the destination player (ZC_WHISPER).
/// 0097 <packet len>.W <nick>.24B <message>.?B
/// 0097 <packet len>.W <nick>.24B <isAdmin>.L <message>.?B (PACKETVER >= 20091104)
void clif_wis_message(int fd, const char* nick, const char* mes, int mes_len)
{
#if PACKETVER < 20091104
	WFIFOHEAD(fd, mes_len + NAME_LENGTH + 4);
	WFIFOW(fd,0) = 0x97;
	WFIFOW(fd,2) = mes_len + NAME_LENGTH + 4;
	safestrncpy((char*)WFIFOP(fd,4), nick, NAME_LENGTH);
	safestrncpy((char*)WFIFOP(fd,28), mes, mes_len);
	WFIFOSET(fd,WFIFOW(fd,2));
#else
	struct map_session_data *ssd = map_nick2sd(nick);

	WFIFOHEAD(fd, mes_len + NAME_LENGTH + 8);
	WFIFOW(fd,0) = 0x97;
	WFIFOW(fd,2) = mes_len + NAME_LENGTH + 8;
	safestrncpy((char*)WFIFOP(fd,4), nick, NAME_LENGTH);
	WFIFOL(fd, 28) = (ssd && pc_isGM(ssd) == 99) ? 1 : 0; // isAdmin; if nonzero, also displays text above char
	safestrncpy((char*)WFIFOP(fd,32), mes, mes_len);
	WFIFOSET(fd,WFIFOW(fd,2));
#endif
}


/// Inform the player about the result of his whisper action.
/// 0098 <result>.B (ZC_ACK_WHISPER)
/// 09df <result>.B <ReceiverGID>.L (ZC_ACK_WHISPER02)
/// result:
///     0 = success to send wisper
///     1 = target character is not loged in
///     2 = ignored by target
///     3 = everyone ignored by target
///     other = target character is not logged in
void clif_wis_end(int fd, int flag)
{
#if PACKETVER < 20131223
	short packet = 0x98;
#else
	struct map_session_data *sd = session_isValid(fd) ? session[fd]->session_data : NULL;
	short packet = 0x9df;
#endif

	WFIFOHEAD(fd, packet_len(packet));
	WFIFOW(fd, 0) = packet;
	WFIFOB(fd, 2) = (char)flag;
#if PACKETVER >= 20131223
	WFIFOL(fd, 3) = sd->bl.id;
#endif
	WFIFOSET(fd, packet_len(packet));
	return;
}


/// Returns character name requested by char_id (ZC_ACK_REQNAME_BYGID).
/// 0194 <char id>.L <name>.24B
/// 0af7 <flag>.W <char id>.L <name>.24B
void clif_solved_charname(int fd, int charid, const char* name)
{
	nullpo_retv(name);
#if PACKETVER > 20180307
	WFIFOHEAD(fd, packet_len(0x0af7));
	WFIFOW(fd, 0) = 0xaf7;
	WFIFOW(fd, 2) = name[0] ? 3 : 2;
	WFIFOL(fd, 4) = charid;
	safestrncpy(WFIFOCP(fd, 8), name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x0af7));
#else
	WFIFOHEAD(fd,packet_len(0x194));
	WFIFOW(fd,0)=0x194;
	WFIFOL(fd,2)=charid;
	safestrncpy(WFIFOCP(fd,6), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x194));
#endif
}


/// Presents a list of items that can be carded/composed (ZC_ITEMCOMPOSITION_LIST).
/// 017b <packet len>.W { <name id>.W }*
void clif_use_card(struct map_session_data *sd,int idx)
{
	int i,c,ep;
	int fd=sd->fd;

	nullpo_retv(sd);
	if (idx < 0 || idx >= MAX_INVENTORY) //Crash-fix from bad packets.
		return;

	if (!sd->inventory_data[idx] || sd->inventory_data[idx]->type != IT_CARD)
		return; //Avoid parsing invalid item indexes (no card/no item)
			
	ep=sd->inventory_data[idx]->equip;
	WFIFOHEAD(fd,MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x17b;

	for(i=c=0;i<MAX_INVENTORY;i++){
		int j;

		if(sd->inventory_data[i] == NULL)
			continue;
		if(sd->inventory_data[i]->type!=IT_WEAPON && sd->inventory_data[i]->type!=IT_ARMOR)
			continue;
		if(itemdb_isspecial(sd->inventory.u.items_inventory[i].card[0])) //Can't slot it
			continue;

		if(sd->inventory.u.items_inventory[i].identify==0 )	//Not identified
			continue;

		if((sd->inventory_data[i]->equip&ep)==0)	//Not equippable on this part.
			continue;

		if(sd->inventory_data[i]->type==IT_WEAPON && ep==EQP_SHIELD) //Shield card won't go on left weapon.
			continue;

		ARR_FIND( 0, sd->inventory_data[i]->slot, j, sd->inventory.u.items_inventory[i].card[j] == 0 );
		if( j == sd->inventory_data[i]->slot )	// No room
			continue;

		if (sd->inventory.u.items_inventory[i].equip > 0)	// Do not check items that are already equipped
			continue;

		WFIFOW(fd,4+c*2)=i+2;
		c++;
	}

	if (!c) return;	// no item is available for card insertion

	WFIFOW(fd,2)=4+c*2;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Notifies the client about the result of item carding/composition (ZC_ACK_ITEMCOMPOSITION).
/// 017d <equip index>.W <card index>.W <result>.B
/// result:
///     0 = success
///     1 = failure
void clif_insert_card(struct map_session_data *sd,int idx_equip,int idx_card,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x17d));
	WFIFOW(fd,0)=0x17d;
	WFIFOW(fd,2)=idx_equip+2;
	WFIFOW(fd,4)=idx_card+2;
	WFIFOB(fd,6)=flag;
	WFIFOSET(fd,packet_len(0x17d));
}


/// Presents a list of items that can be identified (ZC_ITEMIDENTIFY_LIST).
/// 0177 <packet len>.W { <name id>.W }*
void clif_item_identify_list(struct map_session_data *sd) {
	int i,c;
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;

	WFIFOHEAD(fd,MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x177;
	for(i=c=0;i<MAX_INVENTORY;i++){
		if(sd->inventory.u.items_inventory[i].nameid > 0 && !sd->inventory.u.items_inventory[i].identify){
			WFIFOW(fd,c*2+4)=client_index(i);
			c++;
		}
	}
	if(c > 0) {
		WFIFOW(fd,2)=c*2+4;
		WFIFOSET(fd,WFIFOW(fd,2));
		sd->menuskill_id = MC_IDENTIFY;
		sd->menuskill_val = c;
		sd->state.workinprogress = WIP_DISABLE_ALL;
	}
}


/// Notifies the client about the result of a item identify request (ZC_ACK_ITEMIDENTIFY).
/// 0179 <index>.W <result>.B
void clif_item_identified(struct map_session_data *sd,int idx,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x179));
	WFIFOW(fd, 0)=0x179;
	WFIFOW(fd, 2)=idx+2;
	WFIFOB(fd, 4)=flag;
	WFIFOSET(fd,packet_len(0x179));
}


/// Presents a list of items that can be repaired (ZC_REPAIRITEMLIST).
/// 01fc <packet len>.W { <index>.W <name id>.W <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }*
void clif_item_repair_list(struct map_session_data *sd, struct map_session_data *dstsd, int lv) {
	nullpo_retv(sd);
	nullpo_retv(dstsd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	int len = MAX_INVENTORY * sizeof(struct PACKET_ZC_REPAIRITEMLIST_sub) + sizeof(struct PACKET_ZC_REPAIRITEMLIST);

	// Preallocate the maximum size
	WFIFOHEAD(fd, len);

	struct PACKET_ZC_REPAIRITEMLIST *p = (struct PACKET_ZC_REPAIRITEMLIST *)WFIFOP(fd, 0);

	int c = 0;

	for (int i = 0; i < MAX_INVENTORY; i++) {
		if (dstsd->inventory.u.items_inventory[i].nameid > 0 && dstsd->inventory.u.items_inventory[i].attribute != 0 && !itemdb_ishatched_egg(&dstsd->inventory.u.items_inventory[i])) {
			p->items[c].index = i;
			p->items[c].itemId = client_nameid(dstsd->inventory.u.items_inventory[i].nameid);
			p->items[c].refine = dstsd->inventory.u.items_inventory[i].refine;
			clif_addcards(&p->items[c].slot, &dstsd->inventory.u.items_inventory[i]);
			c++;
		}
	}

	if (c > 0) {
		p->packetType = 0x1fc;

		// Recalculate real length
		len = c * sizeof(struct PACKET_ZC_REPAIRITEMLIST_sub) + sizeof(struct PACKET_ZC_REPAIRITEMLIST);
		p->packetLength = len;

		WFIFOSET(fd, len);

		sd->menuskill_id = BS_REPAIRWEAPON;
		sd->menuskill_val = dstsd->bl.id;
		sd->menuskill_val2 = lv;
	}
	else {
		clif_skill_fail(sd, sd->ud.skill_id, USESKILL_FAIL_LEVEL, 0, 0);
	}
}


/// Notifies the client about the result of a item repair request (ZC_ACK_ITEMREPAIR).
/// 01fe <index>.W <result>.B
/// index:
///     ignored (inventory index)
/// result:
///     0 = Item repair success.
///     1 = Item repair failure.
void clif_item_repaireffect(struct map_session_data *sd,int idx,int flag)
{
	int fd;

	nullpo_retv(sd);
	fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x1fe));
	WFIFOW(fd, 0)=0x1fe;
	WFIFOW(fd, 2)=idx+2;
	WFIFOB(fd, 4)=flag;
	WFIFOSET(fd,packet_len(0x1fe));
}


/// Displays a message, that an equipment got damaged (ZC_EQUIPITEM_DAMAGED).
/// 02bb <equip location>.W <account id>.L
void clif_item_damaged(struct map_session_data* sd, unsigned short position)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x2bb));
	WFIFOW(fd,0) = 0x2bb;
	WFIFOW(fd,2) = position;
	WFIFOL(fd,4) = sd->bl.id;  // TODO: the packet seems to be sent to other people as well, probably party and/or guild.
	WFIFOSET(fd,packet_len(0x2bb));
}


/// Presents a list of weapon items that can be refined [Taken from jAthena] (ZC_NOTIFY_WEAPONITEMLIST).
/// 0221 <packet len>.W { <index>.W <name id>.W <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }*
void clif_item_refine_list(struct map_session_data *sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_NOTIFY_WEAPONITEMLIST) + sizeof(struct PACKET_ZC_NOTIFY_WEAPONITEMLIST_sub) * MAX_INVENTORY);
	struct PACKET_ZC_NOTIFY_WEAPONITEMLIST *p = (struct PACKET_ZC_NOTIFY_WEAPONITEMLIST *)WFIFOP(fd, 0);
	p->packetType = 0x221;

	uint16 skill_lv = pc_checkskill(sd, WS_WEAPONREFINE);

	int refine_item[5];

	refine_item[0] = -1;
	refine_item[1] = pc_search_inventory(sd, ITEMID_PHRACON);
	refine_item[2] = pc_search_inventory(sd, ITEMID_EMVERETARCON);
	refine_item[3] = refine_item[4] = pc_search_inventory(sd, ITEMID_ORIDECON);

	int count = 0;
	for (int i = 0; i < MAX_INVENTORY; i++) {
		unsigned char wlv;

		if (sd->inventory.u.items_inventory[i].nameid > 0 && sd->inventory.u.items_inventory[i].refine < skill_lv &&
			sd->inventory.u.items_inventory[i].identify && (wlv = itemdb_wlv(sd->inventory.u.items_inventory[i].nameid)) >= 1 &&
			refine_item[wlv] != -1 && !(sd->inventory.u.items_inventory[i].equip & EQP_ARMS)) {

			p->items[count].index = client_index(i);
			p->items[count].itemId = client_nameid(sd->inventory.u.items_inventory[i].nameid);
			p->items[count].refine = sd->inventory.u.items_inventory[i].refine;
			clif_addcards(&p->items[count].slot, &sd->inventory.u.items_inventory[i]);
			count++;
		}
	}

	p->packetLength = sizeof(struct PACKET_ZC_NOTIFY_WEAPONITEMLIST) + sizeof(struct PACKET_ZC_NOTIFY_WEAPONITEMLIST_sub) * count;
	WFIFOSET(fd, p->packetLength);

	if (count > 0) {
		sd->menuskill_id = WS_WEAPONREFINE;
		sd->menuskill_val = skill_lv;
	}
}


/// Notification of an auto-casted skill (ZC_AUTORUN_SKILL).
/// 0147 <skill id>.W <type>.L <level>.W <sp cost>.W <atk range>.W <skill name>.24B <upgradable>.B
void clif_item_skill(struct map_session_data *sd,int skill_id,int skill_lv)
{
	nullpo_retv(sd);

	int fd = sd->fd;

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_AUTORUN_SKILL));

	struct PACKET_ZC_AUTORUN_SKILL *p = (struct PACKET_ZC_AUTORUN_SKILL *)WFIFOP(fd, 0);
	int type = skill_get_inf(skill_id);

	if (sd->state.itemskill_castonself == 1 && skill_is_item_skill(sd, skill_id, skill_lv))
		type = INF_SELF_SKILL;

	p->packetType = 0x0147;
	p->skill_id = skill_id;
	p->skill_type = type;
	p->skill_lv = skill_lv;
	p->skill_sp = skill_get_sp(skill_id, skill_lv);
	p->skill_range = skill_get_range2(&sd->bl, skill_id, skill_lv, false);
	safestrncpy(p->skill_name, skill_get_name(skill_id), NAME_LENGTH);
	p->up_flag = 0;

	WFIFOSET(fd, sizeof(struct PACKET_ZC_AUTORUN_SKILL));
}


/// Adds an item to character's cart.
/// 0124 <index>.W <amount>.L <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_CART)
/// 01c5 <index>.W <amount>.L <name id>.W <type>.B <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_CART2)
/// 0a0b <index>.W <amount>.L <name id>.W <type>.B <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W (ZC_ADD_ITEM_TO_CART3)
void clif_cart_additem(struct map_session_data *sd, int n, int amount) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
}

	if (n < 0 || n >= MAX_CART || sd->cart.u.items_cart[n].nameid == 0) {
		return;
	}

	struct PACKET_ZC_ADD_ITEM_TO_CART p;

	p.packetType = cartaddType;
	p.index = client_index(n);
	p.amount = amount;
	p.itemId = client_nameid(sd->cart.u.items_cart[n].nameid);
#if PACKETVER >= 5
	p.itemType = itemdb_type(sd->cart.u.items_cart[n].nameid);
#endif
	p.identified = sd->cart.u.items_cart[n].identify;
	p.damaged = sd->cart.u.items_cart[n].attribute;
	p.refine = sd->cart.u.items_cart[n].refine;
	clif_addcards(&p.slot, &sd->cart.u.items_cart[n]);
#if PACKETVER >= 20150226
	clif_add_random_options(p.option_data, &sd->cart.u.items_cart[n]);
#endif

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

// [Ind/Hercules] - Data Thanks to Yommy (ZC_ACK_ADDITEM_TO_CART)
/* Acknowledge an item have been added to cart
 * 012c <result>B
 * result :
 * 0 = ADDITEM_TO_CART_FAIL_WEIGHT
 * 1 = ADDITEM_TO_CART_FAIL_COUNT
 */
void clif_cart_additem_ack(struct map_session_data *sd, uint8 flag)
{
	int fd;
	unsigned char *buf;
	nullpo_retv(sd);

	fd = sd->fd;
	buf = WFIFOP(fd, 0);
	WBUFW(buf, 0) = 0x12c;
	WBUFB(buf, 2) = flag;
	clif_send(buf, packet_len(0x12c), &sd->bl, SELF);
}

// 09B7 <unknow data> (ZC_ACK_OPEN_BANKING) 
void clif_bank_open(struct map_session_data *sd){
	int fd;

	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,4);
	WFIFOW(fd,0) = 0x09b7;
	WFIFOW(fd,2) = 0;
	WFIFOSET(fd,4);
}

/*
 * Request to Open the banking system
 * 09B6 <aid>L ??? (dunno just wild guess checkme)
 */
void clif_parse_BankOpen(int fd, struct map_session_data* sd) {
	//TODO check if preventing trade or stuff like that
	//also mark something in case char ain't available for saving, should we check now ?
	nullpo_retv(sd);
	if( !battle_config.feature_banking ) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,723)); //Banking is disabled
		return;
	}
	else {
		struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
		int aid = RFIFOL(fd,info->pos[0]); //unused should we check vs fd ?
		if(sd->status.account_id == aid){
			sd->state.banking = 1;
			//request save ?
			//chrif_bankdata_request(sd->status.account_id, sd->status.char_id);
			//on succes open bank ?
			clif_bank_open(sd);
		}
	}
}

// 09B9 <unknow data> (ZC_ACK_CLOSE_BANKING)

void clif_bank_close(struct map_session_data *sd){
	int fd;

	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,4);
	WFIFOW(fd,0) = 0x09B9;
	WFIFOW(fd,2) = 0;
	WFIFOSET(fd,4);
}

/*
 * Request to close the banking system
 * 09B8 <aid>L ??? (dunno just wild guess checkme)
 */
void clif_parse_BankClose(int fd, struct map_session_data* sd) {
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
	int aid = RFIFOL(fd,info->pos[0]); //unused should we check vs fd ?

	nullpo_retv(sd);
	if( !battle_config.feature_banking ) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,723)); //Banking is disabled
		//still allow to go trough to not stuck player if we have disable it while they was in
	}
	if(sd->status.account_id == aid){
		sd->state.banking = 0;
		clif_bank_close(sd);
	}
}

/*
 * Display how much we got in bank (I suppose)
  09A6 <Bank_Vault>Q <Reason>W (PACKET_ZC_BANKING_CHECK)
 */
void clif_Bank_Check(struct map_session_data* sd) {
	unsigned char buf[13];
	struct s_packet_db* info;
	int16 len;
	int cmd = 0;

	nullpo_retv(sd);

	cmd = packet_db_ack[sd->packet_ver][ZC_BANKING_CHECK];
	if(!cmd) cmd = 0x09A6; //default
	info = &packet_db[sd->packet_ver][cmd];
	len = info->len;
	if(!len) return; //version as packet disable
	// sd->state.banking = 1; //mark opening and closing

	WBUFW(buf,0) = cmd;
	WBUFQ(buf,info->pos[0]) = sd->bank_vault; //value
	WBUFW(buf,info->pos[1]) = 0; //reason
	clif_send(buf,len,&sd->bl,SELF);
}

/*
 * Requesting the data in bank
 * 09AB <aid>L (PACKET_CZ_REQ_BANKING_CHECK)
 */
void clif_parse_BankCheck(int fd, struct map_session_data* sd) {
	nullpo_retv(sd);

	if( !battle_config.feature_banking ) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,723)); //Banking is disabled
		return;
	}
	else {
		struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
		int aid = RFIFOL(fd,info->pos[0]); //unused should we check vs fd ?
		if(sd->status.account_id == aid) //since we have it let check it for extra security
			clif_Bank_Check(sd);
	}
}

/*
 * Acknowledge of deposit some money in bank
  09A8 <Reason>W <Money>Q <balance>L (PACKET_ZC_ACK_BANKING_DEPOSIT)
 */
void clif_bank_deposit(struct map_session_data *sd, enum e_BANKING_DEPOSIT_ACK reason) {
	unsigned char buf[17];
	struct s_packet_db* info;
	int16 len;
	int cmd =0;

	nullpo_retv(sd);

	cmd = packet_db_ack[sd->packet_ver][ZC_ACK_BANKING_DEPOSIT];
	if(!cmd) cmd = 0x09A8;
	info = &packet_db[sd->packet_ver][cmd];
	len = info->len;
	if(!len) return; //version as packet disable

	WBUFW(buf,0) = cmd;
	WBUFW(buf,info->pos[0]) = (short)reason;
	WBUFQ(buf,info->pos[1]) = sd->bank_vault;/* money in the bank */
	WBUFL(buf,info->pos[2]) = sd->status.zeny;/* how much zeny char has after operation */
	clif_send(buf,len,&sd->bl,SELF);
}

/*
 * Request saving some money in bank
 * @author : original [Yommy/Hercules]
 * 09A7 <AID>L <Money>L (PACKET_CZ_REQ_BANKING_DEPOSIT)
 */
void clif_parse_BankDeposit(int fd, struct map_session_data* sd) {
	nullpo_retv(sd);
	if( !battle_config.feature_banking ) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,723)); //Banking is disabled
		return;
	}
	else {
		struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
		int aid = RFIFOL(fd,info->pos[0]); //unused should we check vs fd ?
		int money = RFIFOL(fd,info->pos[1]);

		if(sd->status.account_id == aid){
			enum e_BANKING_DEPOSIT_ACK reason = pc_bank_deposit(sd,max(0,money));
			clif_bank_deposit(sd,reason);
		}
	}
}

/*
 * Acknowledge of withdrawing some money from bank
  09AA <Reason>W <Money>Q <balance>L (PACKET_ZC_ACK_BANKING_WITHDRAW)
 */
void clif_bank_withdraw(struct map_session_data *sd,enum e_BANKING_WITHDRAW_ACK reason) {
	unsigned char buf[17];
	struct s_packet_db* info;
	int16 len;
	int cmd;

	nullpo_retv(sd);

	cmd = packet_db_ack[sd->packet_ver][ZC_ACK_BANKING_WITHDRAW];
	if(!cmd) cmd = 0x09AA;
	info = &packet_db[sd->packet_ver][cmd];
	len = info->len;
	if(!len) return; //version as packet disable

	WBUFW(buf,0) = cmd;
	WBUFW(buf,info->pos[0]) = (short)reason;
	WBUFQ(buf,info->pos[1]) = sd->bank_vault;/* money in the bank */
	WBUFL(buf,info->pos[2]) = sd->status.zeny;/* how much zeny char has after operation */

	clif_send(buf,len,&sd->bl,SELF);
}

/*
 * Request Withdrawing some money from bank
 * 09A9 <AID>L <Money>L (PACKET_CZ_REQ_BANKING_WITHDRAW)
 */
void clif_parse_BankWithdraw(int fd, struct map_session_data* sd) {
        nullpo_retv(sd);
	if( !battle_config.feature_banking ) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,723)); //Banking is disabled
		return;
	}
	else {
		struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
		int aid = RFIFOL(fd,info->pos[0]); //unused should we check vs fd ?
		int money = RFIFOL(fd,info->pos[1]);
		if(sd->status.account_id == aid){
			enum e_BANKING_WITHDRAW_ACK reason = pc_bank_withdraw(sd,max(0,money));
			clif_bank_withdraw(sd,reason);
		}
	}
}

/// Deletes an item from character's cart (ZC_DELETE_ITEM_FROM_CART).
/// 0125 <index>.W <amount>.L
void clif_cart_delitem(struct map_session_data *sd,int n,int amount)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;

	WFIFOHEAD(fd,packet_len(0x125));
	WFIFOW(fd,0)=0x125;
	WFIFOW(fd,2)=n+2;
	WFIFOL(fd,4)=amount;
	WFIFOSET(fd,packet_len(0x125));
}


/// Opens the shop creation menu (ZC_OPENSTORE).
/// 012d <num>.W
/// num:
///     number of allowed item slots
void clif_openvendingreq(struct map_session_data* sd, int num)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x12d));
	WFIFOW(fd,0) = 0x12d;
	WFIFOW(fd,2) = num;
	WFIFOSET(fd,packet_len(0x12d));
}


/// Displays a vending board to target/area (ZC_STORE_ENTRY).
/// 0131 <owner id>.L <message>.80B
void clif_showvendingboard(struct block_list* bl, const char* message, int fd)
{
	unsigned char buf[128];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x131;
	WBUFL(buf,2) = bl->id;
	safestrncpy((char*)WBUFP(buf,6), message, 80);

	if( fd ) {
		WFIFOHEAD(fd,packet_len(0x131));
		memcpy(WFIFOP(fd,0),buf,packet_len(0x131));
		WFIFOSET(fd,packet_len(0x131));
	} else {
		clif_send(buf,packet_len(0x131),bl,AREA_WOS);
	}
}


/// Removes a vending board from screen (ZC_DISAPPEAR_ENTRY).
/// 0132 <owner id>.L
void clif_closevendingboard(struct block_list* bl, int fd)
{
	unsigned char buf[16];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x132;
	WBUFL(buf,2) = bl->id;
	if( fd ) {
		WFIFOHEAD(fd,packet_len(0x132));
		memcpy(WFIFOP(fd,0),buf,packet_len(0x132));
		WFIFOSET(fd,packet_len(0x132));
	} else {
		clif_send(buf,packet_len(0x132),bl,AREA_WOS);
	}
}


/// Sends a list of items in a shop.
/// R 0133 <packet len>.W <owner id>.L { <price>.L <amount>.W <index>.W <type>.B <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }* (ZC_PC_PURCHASE_ITEMLIST_FROMMC)
/// R 0800 <packet len>.W <owner id>.L <unique id>.L { <price>.L <amount>.W <index>.W <type>.B <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }* (ZC_PC_PURCHASE_ITEMLIST_FROMMC2)
void clif_vendinglist(struct map_session_data* sd, struct map_session_data* vsd) {
	nullpo_retv(sd);
	nullpo_retv(vsd);
	nullpo_retv(vsd->vending);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
}

	int len = sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC) + vsd->vend_num * sizeof(struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC_sub);

	WFIFOHEAD(fd, len);

	struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC* p = (struct PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC*)WFIFOP(fd, 0);

	p->packetType = vendinglistType;
	p->packetLength = len;
	p->AID = vsd->status.account_id;
#if PACKETVER >= 20100105
	p->venderId = vsd->vender_id;
#endif

	for (int i = 0; i < vsd->vend_num; i++) {
		int index = vsd->vending[i].index;
		struct item_data* data = itemdb_search(vsd->cart.u.items_cart[index].nameid);

		p->items[i].price = vsd->vending[i].value;
		p->items[i].amount = vsd->vending[i].amount;
		p->items[i].index = client_index(index);
		p->items[i].itemType = itemtype(vsd->cart.u.items_cart[index].nameid);
		p->items[i].itemId = client_nameid(vsd->cart.u.items_cart[index].nameid);
		p->items[i].identified = vsd->cart.u.items_cart[index].identify;
		p->items[i].damaged = vsd->cart.u.items_cart[index].attribute;
		p->items[i].refine = vsd->cart.u.items_cart[index].refine;
		clif_addcards(&p->items[i].slot, &vsd->cart.u.items_cart[index]);
#if PACKETVER >= 20150226
		clif_add_random_options(p->items[i].option_data, &vsd->cart.u.items_cart[index]);
#if PACKETVER >= 20160921
		p->items[i].location = pc_equippoint_sub(sd, data);
		p->items[i].viewSprite = data->look;
#endif
#endif
	}

	WFIFOSET(fd, len);
}

/// Shop purchase failure (ZC_PC_PURCHASE_RESULT_FROMMC).
/// 0135 <index>.W <amount>.W <result>.B
/// result:
///     0 = success
///     1 = not enough zeny
///     2 = overweight
///     4 = out of stock
///     5 = "cannot use an npc shop while in a trade"
///     6 = Because the store information was incorrect the item was not purchased.
///     7 = No sales information.
void clif_buyvending(struct map_session_data* sd, int index, int amount, int fail)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x135));
	WFIFOW(fd,0) = 0x135;
	WFIFOW(fd,2) = index+2;
	WFIFOW(fd,4) = amount;
	WFIFOB(fd,6) = fail;
	WFIFOSET(fd,packet_len(0x135));
}

/// Show's vending player its list of items for sale (ZC_ACK_OPENSTORE2).
/// 0a28 <Result>.B
/// result:
///     0 = Successed
///     1 = Failed
void clif_openvending_ack(struct map_session_data* sd, int result)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd, 3);
	WFIFOW(fd,0) = 0xa28;
	WFIFOB(fd,2) = result;
	WFIFOSET(fd, 3);
}

/// Shop creation success (ZC_PC_PURCHASE_MYITEMLIST).
/// 0136 <packet len>.W <owner id>.L { <price>.L <index>.W <amount>.W <type>.B <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }*
void clif_openvending(struct map_session_data* sd, int id, struct s_vending* vending) {
	nullpo_retv(sd);
	nullpo_retv(vending);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
}

	int len = sizeof(struct PACKET_ZC_PC_PURCHASE_MYITEMLIST) + sd->vend_num * sizeof(struct PACKET_ZC_PC_PURCHASE_MYITEMLIST_sub);

	WFIFOHEAD(fd, len);

	struct PACKET_ZC_PC_PURCHASE_MYITEMLIST *p = (struct PACKET_ZC_PC_PURCHASE_MYITEMLIST *)WFIFOP(fd, 0);

	p->packetType = 0x136;
	p->packetLength = len;
	p->AID = id;

	for (int i = 0; i < sd->vend_num; i++) {
		int index = vending[i].index;

		p->items[i].price = vending[i].value;
		p->items[i].index = client_index(index);
		p->items[i].amount = vending[i].amount;
		p->items[i].itemType = itemtype(sd->cart.u.items_cart[index].nameid);
		p->items[i].itemId = client_nameid(sd->cart.u.items_cart[index].nameid);
		p->items[i].identified = sd->cart.u.items_cart[index].identify;
		p->items[i].damaged = sd->cart.u.items_cart[index].attribute;
		p->items[i].refine = sd->cart.u.items_cart[index].refine;
		clif_addcards(&p->items[i].slot, &sd->cart.u.items_cart[index]);
#if PACKETVER >= 20150226
		clif_add_random_options(p->items[i].option_data, &sd->cart.u.items_cart[index]);
#endif
	}

	WFIFOSET(fd, len);

#if PACKETVER >= 20141022
	///     0 = Successed
	///     1 = Failed
	clif_openvending_ack(sd, 0);
#endif
}

/// Inform merchant that someone has bought an item.
/// 0137 <index>.W <amount>.W (ZC_DELETEITEM_FROM_MCSTORE).
/// 09e5 <index>.W <amount>.W <GID>.L <Date>.L <zeny>.L (ZC_DELETEITEM_FROM_MCSTORE2).
void clif_vendingreport(struct map_session_data* sd, int index, int amount, uint32 char_id, int zeny)
{
#if PACKETVER < 20141016 // TODO : not sure for client date [Napster]
	const int cmd = 0x137;
#else
	const int cmd = 0x9e5;
#endif
	int fd = sd->fd;

	nullpo_retv(sd);

	WFIFOHEAD(fd, packet_len(cmd));
	WFIFOW(fd, 0) = cmd;
	WFIFOW(fd, 2) = index+2;
	WFIFOW(fd, 4) = amount;
#if PACKETVER >= 20141016
	WFIFOL(fd, 6) = char_id;	// GID
	WFIFOL(fd, 10) = (int)time(NULL);	// Date
	WFIFOL(fd, 14) = zeny;		// zeny
#endif
	WFIFOSET(fd,packet_len(cmd));
}


/// Result of organizing a party (ZC_ACK_MAKE_GROUP).
/// 00fa <result>.B
/// result:
///     0 = opens party window and shows MsgStringTable[77]="party successfully organized"
///     1 = MsgStringTable[78]="party name already exists"
///     2 = MsgStringTable[79]="already in a party"
///     3 = cannot organize parties on this map
///     ? = nothing
void clif_party_created(struct map_session_data *sd,int result)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xfa));
	WFIFOW(fd,0)=0xfa;
	WFIFOB(fd,2)=result;
	WFIFOSET(fd,packet_len(0xfa));
}


/// Adds new member to a party.
/// Other behaviours:
///     updates item share options without a message
///     replaces member if the charname matches
///     ignores position fields // TODO check on different clients [flaviojs]
/// 0104 <account id>.L <role>.L <x>.W <y>.W <state>.B <party name>.24B <char name>.24B <map name>.16B (ZC_ADD_MEMBER_TO_GROUP)
/// 01e9 <account id>.L <role>.L <x>.W <y>.W <state>.B <party name>.24B <char name>.24B <map name>.16B <item pickup rule>.B <item share rule>.B (ZC_ADD_MEMBER_TO_GROUP2)
/// 0a43 <account id>.L <role>.L <class>.W <base level>.W <x>.W <y>.W <state>.B <party name>.24B <char name>.24B <map name>.16B <item pickup rule>.B <item share rule>.B (ZC_ADD_MEMBER_TO_GROUP3)
/// role:
///     0 = leader
///     1 = normal
/// state:
///     0 = connected
///     1 = disconnected
void clif_party_member_info(struct party_data *p, int member_id, send_target type)
{
#if PACKETVER < 20170502
	unsigned char buf[81];
	short packet_num = 0x1e9;
	unsigned char offset = 0;
#else
	unsigned char buf[85];
	short packet_num = 0xa43;
	unsigned char offset = 4;
#endif

	struct map_session_data* sd;
	struct party_member* m;

	nullpo_retv(p);

	if( member_id < 0 || member_id >= MAX_PARTY )
		return;// out of range
	m = &p->party.member[member_id];
	sd = p->data[member_id].sd;
	if( sd == NULL && type != SELF )
		sd = party_getavailablesd(p);// can use any party member
	if( sd == NULL )
		return;// not online

	WBUFW(buf, 0) = packet_num;
	WBUFL(buf, 2) = m->account_id;
	WBUFL(buf, 6) = ( m->leader ) ? 0 : 1;// role: 0-leader 1-member
#if PACKETVER >= 20170502
	WBUFW(buf,10) = sd->status.class_;
	WBUFW(buf,12) = sd->status.base_level;
#endif
	WBUFW(buf,10 + offset) = p->data[member_id].x;
	WBUFW(buf,12 + offset) = p->data[member_id].y;
	WBUFB(buf,14 + offset) = ( m->online ) ? 0 : 1;// state: 0-online 1-offline
	safestrncpy(WBUFP(buf,15 + offset), p->party.name, NAME_LENGTH);
	safestrncpy(WBUFP(buf,39 + offset), m->name, NAME_LENGTH);
	mapindex_getmapname_ext(mapindex_id2name(m->map), (char*)WBUFP(buf,63 + offset));
	WBUFB(buf,79 + offset) = (p->party.item&1)?1:0;
	WBUFB(buf,80 + offset) = (p->party.item&2)?1:0;
	clif_send(buf,packet_len(packet_num),&sd->bl,type);
}


/// Sends party information (ZC_GROUP_LIST).
/// 00fb <packet len>.W <party name>.24B { <account id>.L <nick>.24B <map name>.16B <role>.B <state>.B }*
/// role:
///     0 = leader
///     1 = normal
/// state:
///     0 = connected
///     1 = disconnected
/// 0a44 <packet len>.W <party name>.24B { <account id>.L <nick>.24B <map name>.16B <role>.B <state>.B <class>.W <base level>.W }* <item pickup rule>.B <item share rule>.B <unknown>.L
void clif_party_info(struct party_data* p, struct map_session_data *sd)
{
	unsigned char party_info = 2+2+NAME_LENGTH;
#if PACKETVER < 20170502
	short packet_num = 0xfb;
	unsigned char member_info = 4+NAME_LENGTH+MAP_NAME_LENGTH_EXT+1+1;
	unsigned char extra_info = 0;
	unsigned char buf[2+2+NAME_LENGTH+(4+NAME_LENGTH+MAP_NAME_LENGTH_EXT+1+1)*MAX_PARTY];
#else
	short packet_num = 0xa44;
	unsigned char member_info = 4+NAME_LENGTH+MAP_NAME_LENGTH_EXT+1+1+2+2;
	unsigned char extra_info = 1+1+4;
	unsigned char buf[2+2+NAME_LENGTH+(4+NAME_LENGTH+MAP_NAME_LENGTH_EXT+1+1+2+2)*MAX_PARTY+1+1+4];
#endif
	struct map_session_data* party_sd = NULL;
	int i, c;

	nullpo_retv(p);

	WBUFW(buf, 0) = packet_num;
	safestrncpy(WBUFP(buf,4), p->party.name, NAME_LENGTH);
	for(i = 0, c = 0; i < MAX_PARTY; i++)
	{
		struct party_member* m = &p->party.member[i];
		if(!m->account_id) continue;

		if(party_sd == NULL) party_sd = p->data[i].sd;

		WBUFL(buf,party_info+c*member_info) = m->account_id;
		safestrncpy(WBUFP(buf,party_info+c*member_info+4), m->name, NAME_LENGTH);
		mapindex_getmapname_ext(mapindex_id2name(m->map), (char*)WBUFP(buf,party_info+c*member_info+28));
		WBUFB(buf,party_info+c*member_info+44) = (m->leader) ? 0 : 1;
		WBUFB(buf,party_info+c*member_info+45) = (m->online) ? 0 : 1;
#if PACKETVER >= 20170502
		WBUFW(buf,party_info+c*member_info+46) = m->class_;
		WBUFW(buf,party_info+c*member_info+48) = m->lv;
#endif
		c++;
	}
#if PACKETVER >= 20170502
	WBUFB(buf,party_info+c*member_info) = (p->party.item&1) ? 1 :0;
	WBUFB(buf,party_info+c*member_info+1) = (p->party.item&2) ? 1 : 0;
	WBUFL(buf,party_info+c*member_info+2) = 0;
#endif
	WBUFW(buf,2) = party_info+c*member_info+extra_info;

	if(sd) { // send only to self
		clif_send(buf, WBUFW(buf,2), &sd->bl, SELF);
	} else if (party_sd) { // send to whole party
		clif_send(buf, WBUFW(buf,2), &party_sd->bl, PARTY);
	}
}

/// Request to change party invitation tick.
/// value:
///	 0 = disabled
///	 1 = enabled
void clif_parse_PartyTick(int fd, struct map_session_data* sd)
{
	bool flag = RFIFOB(fd, 6) ? true : false;
	sd->status.allow_party = flag;
	clif_partyinvitationstate(sd, flag);
}

/// The player's 'party invite' state, sent during login (ZC_PARTY_CONFIG).
/// 02c9 <flag>.B
/// flag:
///     0 = allow party invites
///     1 = auto-deny party invites
void clif_partyinvitationstate(struct map_session_data* sd, bool flag)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x2c9));
	WFIFOW(fd, 0) = 0x2c9;
	WFIFOB(fd, 2) = flag;
	WFIFOSET(fd, packet_len(0x2c9));
}


/// Party invitation request.
/// 00fe <party id>.L <party name>.24B (ZC_REQ_JOIN_GROUP)
/// 02c6 <party id>.L <party name>.24B (ZC_PARTY_JOIN_REQ)
void clif_party_invite(struct map_session_data *sd,struct map_session_data *tsd)
{
#if PACKETVER < 20070821
	const int cmd = 0xfe;
#else
	const int cmd = 0x2c6;
#endif
	int fd;
	struct party_data *p;

	nullpo_retv(sd);
	nullpo_retv(tsd);

	fd=tsd->fd;

	if( (p=party_search(sd->status.party_id))==NULL )
		return;

	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd,0)=cmd;
	WFIFOL(fd,2)=sd->status.party_id;
	safestrncpy(WFIFOP(fd,6),p->party.name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(cmd));
}


/// Party invite result.
/// 00fd <nick>.24S <result>.B (ZC_ACK_REQ_JOIN_GROUP)
/// 02c5 <nick>.24S <result>.L (ZC_PARTY_JOIN_REQ_ACK)
/// result=0 : char is already in a party -> MsgStringTable[80]
/// result=1 : party invite was rejected -> MsgStringTable[81]
/// result=2 : party invite was accepted -> MsgStringTable[82]
/// result=3 : party is full -> MsgStringTable[83]
/// result=4 : char of the same account already joined the party -> MsgStringTable[608]
/// result=5 : char blocked party invite -> MsgStringTable[1324] (since 20070904)
/// result=7 : char is not online or doesn't exist -> MsgStringTable[71] (since 20070904)
/// result=8 : (%s) TODO instance related? -> MsgStringTable[1388] (since 20080527)
/// return=9 : TODO map prohibits party joining? -> MsgStringTable[1871] (since 20110205)
void clif_party_inviteack(struct map_session_data* sd, const char* nick, int result)
{
	int fd;
	nullpo_retv(sd);
	fd=sd->fd;

#if PACKETVER < 20070904
	if( result == 7 ) {
		clif_displaymessage(fd, msg_txt(sd,3));
		return;
	}
#endif

#if PACKETVER < 20070821
	WFIFOHEAD(fd,packet_len(0xfd));
	WFIFOW(fd,0) = 0xfd;
	safestrncpy((char*)WFIFOP(fd,2),nick,NAME_LENGTH);
	WFIFOB(fd,26) = result;
	WFIFOSET(fd,packet_len(0xfd));
#else
	WFIFOHEAD(fd,packet_len(0x2c5));
	WFIFOW(fd,0) = 0x2c5;
	safestrncpy((char*)WFIFOP(fd,2),nick,NAME_LENGTH);
	WFIFOL(fd,26) = result;
	WFIFOSET(fd,packet_len(0x2c5));
#endif
}


/// Updates party settings.
/// Other behaviour:
///     notifies the user about the current options
/// 0101 <exp option>.L (ZC_GROUPINFO_CHANGE)
/// 07d8 <exp option>.L <item pick rule>.B <item share rule>.B (ZC_REQ_GROUPINFO_CHANGE_V2)
/// exp option:
///     0 = exp sharing disabled
///     1 = exp sharing enabled
///     2 = cannot change exp sharing
void clif_party_option(struct party_data* p, int member_id, send_target type)
{
	struct map_session_data* sd;
	unsigned char buf[16];
#if PACKETVER < 20090603
	const int cmd = 0x101;
#else
	const int cmd = 0x7d8;
#endif

	nullpo_retv(p);

	if( member_id == PARTY_MEMBER_NOTFOUND && type != SELF )
		member_id = party_getanymemberid(p);// can use any party member
	if( member_id < 0 || member_id >= MAX_PARTY )
		return;// out of range
	sd = p->data[member_id].sd;
	if( sd == NULL )
		return;// not online

	WBUFW(buf,0)=cmd;
	WBUFL(buf,2)=p->party.exp;
#if PACKETVER >= 20090603
	WBUFB(buf,6)=(p->party.item&1)?1:0;
	WBUFB(buf,7)=(p->party.item&2)?1:0;
#else
	// item changes are not notified in older clients
	clif_party_member_info(p, member_id, type);
#endif
	clif_send(buf,packet_len(cmd),&sd->bl,type);
}


/// Notify the user that it cannot change exp sharing.
/// 0101 <exp option>.L (ZC_GROUPINFO_CHANGE)
/// exp option:
///     0 = exp sharing disabled
///     1 = exp sharing enabled
///     2 = cannot change exp sharing
void clif_party_option_failexp(struct map_session_data* sd)
{
	unsigned char buf[16];

	if( sd == NULL )
		return;

	WBUFW(buf,0) = 0x101;
	WBUFL(buf,2) = 2;// cannot change exp sharing

	clif_send(buf,packet_len(0x101),&sd->bl,SELF);
}


/// 0105 <account id>.L <char name>.24B <result>.B (ZC_DELETE_MEMBER_FROM_GROUP).
///		sd Send the notification for this player
///		account_id Account ID of kicked member
///		name Name of kicked member
///		result Party leave result @see PARTY_MEMBER_WITHDRAW
///		target Send target
void clif_party_withdraw(struct map_session_data *sd, uint32 account_id, const char* name, enum e_party_member_withdraw result, enum send_target target) {
	unsigned char buf[2+4+NAME_LENGTH+1];

	if (!sd)
		return;

	WBUFW(buf,0) = 0x0105;
	WBUFL(buf,2) = account_id;
	safestrncpy(WBUFP(buf,6), name, NAME_LENGTH);
	WBUFB(buf,6+NAME_LENGTH) = result;

	clif_send(buf, 2+4+NAME_LENGTH+1, &sd->bl, target);
}


/// Party chat message (ZC_NOTIFY_CHAT_PARTY).
/// 0109 <packet len>.W <account id>.L <message>.?B
void clif_party_message(struct party_data* p, uint32 account_id, const char* mes, int len)
{
	struct map_session_data *sd;
	int i;

	nullpo_retv(p);

	for(i=0; i < MAX_PARTY && !p->data[i].sd;i++);
	if(i < MAX_PARTY){
		unsigned char buf[1024];

		if( len > sizeof(buf)-8 )
		{
			ShowWarning("clif_party_message: Truncated message '%s' (len=%d, max=%d, party_id=%d).\n", mes, len, sizeof(buf)-8, p->party.party_id);
			len = sizeof(buf)-8;
		}

		sd = p->data[i].sd;
		WBUFW(buf,0)=0x109;
		WBUFW(buf,2)=len+8;
		WBUFL(buf,4)=account_id;
		safestrncpy((char*)WBUFP(buf,8), mes, len);
		clif_send(buf,len+8,&sd->bl,PARTY);
	}
}


/// Updates the position of a party member on the minimap (ZC_NOTIFY_POSITION_TO_GROUPM).
/// 0107 <account id>.L <x>.W <y>.W
void clif_party_xy(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retv(sd);

	WBUFW(buf,0)=0x107;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=sd->bl.x;
	WBUFW(buf,8)=sd->bl.y;
	clif_send(buf,packet_len(0x107),&sd->bl,PARTY_SAMEMAP_WOS);
}


/*==========================================
 * Sends x/y dot to a single fd. [Skotlex]
 *------------------------------------------*/
void clif_party_xy_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd,packet_len(0x107));
	WFIFOW(fd,0)=0x107;
	WFIFOL(fd,2)=sd->status.account_id;
	WFIFOW(fd,6)=sd->bl.x;
	WFIFOW(fd,8)=sd->bl.y;
	WFIFOSET(fd,packet_len(0x107));
}


/// Updates HP bar of a party member.
/// 0106 <account id>.L <hp>.W <max hp>.W (ZC_NOTIFY_HP_TO_GROUPM)
/// 080e <account id>.L <hp>.L <max hp>.L (ZC_NOTIFY_HP_TO_GROUPM_R2)
void clif_party_hp(struct map_session_data *sd)
{
	unsigned char buf[16];
#if PACKETVER < 20100126
	const int cmd = 0x106;
#else
	const int cmd = 0x80e;
#endif

	nullpo_retv(sd);

	WBUFW(buf,0)=cmd;
	WBUFL(buf,2)=sd->status.account_id;
#if PACKETVER < 20100126
	if (sd->battle_status.max_hp > INT16_MAX) { //To correctly display the %hp bar. [Skotlex]
		WBUFW(buf,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WBUFW(buf,8) = 100;
	} else {
		WBUFW(buf,6) = sd->battle_status.hp;
		WBUFW(buf,8) = sd->battle_status.max_hp;
	}
#else
	WBUFL(buf,6) = sd->battle_status.hp;
	WBUFL(buf,10) = sd->battle_status.max_hp;
#endif
	clif_send(buf,packet_len(cmd),&sd->bl,PARTY_AREA_WOS);
}

/// Notifies the party members of a character's death or revival.
/// 0AB2 <GID>.L <dead>.B
void clif_party_dead(struct map_session_data *sd) {
#if PACKETVER >= 20170502
	unsigned char buf[7];

	nullpo_retv(sd);

	WBUFW(buf, 0) = 0xab2;
	WBUFL(buf, 2) = sd->status.account_id;
	WBUFB(buf, 6) = pc_isdead(sd);

	clif_send(buf, packet_len(0xab2), &sd->bl, PARTY);
#endif
}

/// Updates job and level info of a party member.
/// 0abd <account id>.L <job>.W <level>.W (ZC_)
int clif_party_job_and_level(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_ret(sd);

	WBUFW(buf,0)=0xabd;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=sd->status.class_;
	WBUFW(buf,8)=sd->status.base_level;
	clif_send(buf,packet_len(0xabd),&sd->bl,PARTY_AREA_WOS);
	return 0;
}

/*==========================================
 * Sends HP bar to a single fd. [Skotlex]
 *------------------------------------------*/
void clif_hpmeter_single(int fd, int id, unsigned int hp, unsigned int maxhp)
{
#if PACKETVER < 20100126
	const int cmd = 0x106;
#else
	const int cmd = 0x80e;
#endif
	WFIFOHEAD(fd,packet_len(cmd));
	WFIFOW(fd,0) = cmd;
	WFIFOL(fd,2) = id;
#if PACKETVER < 20100126
	if( maxhp > INT16_MAX )
	{// To correctly display the %hp bar. [Skotlex]
		WFIFOW(fd,6) = hp/(maxhp/100);
		WFIFOW(fd,8) = 100;
	} else {
		WFIFOW(fd,6) = hp;
		WFIFOW(fd,8) = maxhp;
	}
#else
	WFIFOL(fd,6) = hp;
	WFIFOL(fd,10) = maxhp;
#endif
	WFIFOSET(fd, packet_len(cmd));
}

/*==========================================
 *
 *------------------------------------------*/
int clif_hpmeter_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd, *tsd;
	int level;
#if PACKETVER < 20100126
	const int cmd = 0x106;
#else
	const int cmd = 0x80e;
#endif

	sd = va_arg(ap, struct map_session_data *);
	tsd = (TBL_PC *)bl;

	nullpo_ret(sd);
	nullpo_ret(tsd);

	if( !tsd->fd || tsd == sd )
		return 0;

	if( (level = pc_isGM(tsd)) < battle_config.disp_hpmeter || level < pc_isGM(sd) )
		return 0;
	WFIFOHEAD(tsd->fd,packet_len(cmd));
	WFIFOW(tsd->fd,0) = cmd;
	WFIFOL(tsd->fd,2) = sd->status.account_id;
#if PACKETVER < 20100126
	if( sd->battle_status.max_hp > INT16_MAX )
	{ //To correctly display the %hp bar. [Skotlex]
		WFIFOW(tsd->fd,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WFIFOW(tsd->fd,8) = 100;
	} else {
		WFIFOW(tsd->fd,6) = sd->battle_status.hp;
		WFIFOW(tsd->fd,8) = sd->battle_status.max_hp;
	}
#else
	WFIFOL(tsd->fd,6) = sd->battle_status.hp;
	WFIFOL(tsd->fd,10) = sd->battle_status.max_hp;
#endif
	WFIFOSET(tsd->fd,packet_len(cmd));
	return 0;
}

/*==========================================
 * GMへ場所とHP通知
 *------------------------------------------*/
int clif_hpmeter(struct map_session_data *sd)
{
	nullpo_ret(sd);

	if( battle_config.disp_hpmeter )
		map_foreachinarea(clif_hpmeter_sub, sd->bl.m, sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE, sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE, BL_PC, sd);

	return 0;
}


/// Notifies the client, that it's attack target is too far (ZC_ATTACK_FAILURE_FOR_DISTANCE).
/// 0139 <target id>.L <target x>.W <target y>.W <x>.W <y>.W <atk range>.W
void clif_movetoattack(struct map_session_data *sd,struct block_list *bl)
{
	int fd;

	nullpo_retv(sd);
	nullpo_retv(bl);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x139));
	WFIFOW(fd, 0)=0x139;
	WFIFOL(fd, 2)=bl->id;
	WFIFOW(fd, 6)=bl->x;
	WFIFOW(fd, 8)=bl->y;
	WFIFOW(fd,10)=sd->bl.x;
	WFIFOW(fd,12)=sd->bl.y;
	WFIFOW(fd,14)=sd->battle_status.rhw.range;
	WFIFOSET(fd,packet_len(0x139));
}


/// Notifies the client about the result of an item produce request (ZC_ACK_REQMAKINGITEM).
/// 018f <result>.W <name id>.W
/// result:
/// 0 = Success (Blacksmith)
/// 1 = Failure (Blacksmith)
/// 2 = Success (Alchemist)
/// 3 = Failure (Alchemist)
/// 4 = Success (Rune Knight)
/// 5 = Failure (Rune Knight)
/// 6 = Success (Genetic)
/// 7 = Failure (Genetic)
void clif_produceeffect(struct map_session_data* sd,int flag, t_itemid nameid)
{
	nullpo_retv(sd);

	clif_solved_charname(sd->fd, sd->status.char_id, sd->status.name);

	struct PACKET_ZC_ACK_REQMAKINGITEM p;

	p.packetType = 0x18f;
	p.result = flag;
	p.itemId = client_nameid(nameid);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Initiates the pet taming process (ZC_START_CAPTURE).
/// 019e
void clif_catch_process(struct map_session_data *sd)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x19e));
	WFIFOW(fd,0)=0x19e;
	WFIFOSET(fd,packet_len(0x19e));
}


/// Displays the result of a pet taming attempt (ZC_TRYCAPTURE_MONSTER).
/// 01a0 <result>.B
///     0 = failure
///     1 = success
void clif_pet_roulette(struct map_session_data *sd,int data)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1a0));
	WFIFOW(fd,0)=0x1a0;
	WFIFOB(fd,2)=data;
	WFIFOSET(fd,packet_len(0x1a0));
}


/// Presents a list of pet eggs that can be hatched (ZC_PETEGG_LIST).
/// 01a6 <packet len>.W { <index>.W }*
void clif_sendegg(struct map_session_data *sd)
{
	int i,n=0,fd;

	nullpo_retv(sd);

	fd=sd->fd;
	if (battle_config.pet_no_gvg && map_flag_gvg2(sd->bl.m))
	{	//Disable pet hatching in GvG grounds during Guild Wars [Skotlex]
		clif_displaymessage(fd, "Pets are not allowed in Guild Wars.");
		return;
	}

	WFIFOHEAD(fd, MAX_INVENTORY * 2 + 4);
	WFIFOW(fd,0)=0x1a6;
	if(sd->status.pet_id <= 0) {
		for(i=0,n=0;i<MAX_INVENTORY;i++){
			if(sd->inventory.u.items_inventory[i].nameid==0 || sd->inventory_data[i] == NULL ||
			   sd->inventory_data[i]->type!=IT_PETEGG ||
			   sd->inventory.u.items_inventory[i].amount<=0)
				continue;
			WFIFOW(fd,n*2+4)=client_index(i);
			n++;
		}
	}
	WFIFOW(fd,2)=4+n*2;
	WFIFOSET(fd,WFIFOW(fd,2));

	sd->menuskill_id = SA_TAMINGMONSTER;
	sd->menuskill_val = -1;
}


/// Sends a specific pet data update (ZC_CHANGESTATE_PET).
/// 01a4 <type>.B <id>.L <data>.L
/// type:
///     0 = pre-init (data = 0)
///     1 = intimacy (data = 0~4)
///     2 = hunger (data = 0~4)
///     3 = accessory
///     4 = performance (data = 1~3: normal, 4: special)
///     5 = hairstyle
///     6 = close egg selection ui and update egg in inventory (PACKETVER >= 20180704)
///
/// If sd is null, the update is sent to nearby objects, otherwise it is sent only to that player.
void clif_send_petdata(struct map_session_data* sd, struct pet_data* pd, int type, int param)
{
	uint8 buf[16];
	nullpo_retv(pd);

	WBUFW(buf,0) = 0x1a4;
	WBUFB(buf,2) = type;
	WBUFL(buf,3) = pd->bl.id;
	WBUFL(buf,7) = param;
	if (sd)
		clif_send(buf, packet_len(0x1a4), &sd->bl, SELF);
	else
		clif_send(buf, packet_len(0x1a4), &pd->bl, AREA);
}


/// Pet's base data (ZC_PROPERTY_PET).
/// 01a2 <name>.24B <renamed>.B <level>.W <hunger>.W <intimacy>.W <accessory id>.W <class>.W
void clif_send_petstatus(struct map_session_data *sd)
{
	int fd;
	struct s_pet *pet;

	nullpo_retv(sd);
	nullpo_retv(sd->pd);

	fd=sd->fd;
	pet = &sd->pd->pet;
	WFIFOHEAD(fd,packet_len(0x1a2));
	WFIFOW(fd,0)=0x1a2;
	safestrncpy(WFIFOP(fd,2),pet->name,NAME_LENGTH);
	WFIFOB(fd,26)=battle_config.pet_rename?0:pet->rename_flag;
	WFIFOW(fd,27)=pet->level;
	WFIFOW(fd,29)=pet->hungry;
	WFIFOW(fd,31)=pet->intimate;
	WFIFOW(fd,33)=pet->equip;
#if PACKETVER >= 20081126
	WFIFOW(fd,35)=pet->class_;
#endif
	WFIFOSET(fd,packet_len(0x1a2));
}


/// Notification about a pet's emotion/talk (ZC_PET_ACT).
/// 01aa <id>.L <data>.L
/// data:
///     @see CZ_PET_ACT.
void clif_pet_emotion(struct pet_data *pd,int param)
{
	unsigned char buf[16];

	nullpo_retv(pd);

	memset(buf,0,packet_len(0x1aa));

	WBUFW(buf,0)=0x1aa;
	WBUFL(buf,2)=pd->bl.id;
	if(param >= 100 && pd->petDB->talk_convert_class) {
		if(pd->petDB->talk_convert_class < 0)
			return;
		else if(pd->petDB->talk_convert_class > 0) {
			// replace mob_id component of talk/act data
			param -= (pd->pet.class_ - 100)*100;
			param += (pd->petDB->talk_convert_class - 100)*100;
		}
	}
	WBUFL(buf,6)=param;

	clif_send(buf,packet_len(0x1aa),&pd->bl,AREA);
}


/// Result of request to feed a pet (ZC_FEED_PET).
/// 01a3 <result>.B <name id>.W
/// result:
///     0 = failure
///     1 = success
void clif_pet_food(struct map_session_data *sd,int foodid,int fail)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1a3));
	WFIFOW(fd,0)=0x1a3;
	WFIFOB(fd,2)=fail;
	WFIFOW(fd,3)=foodid;
	WFIFOSET(fd,packet_len(0x1a3));
}


/// Presents a list of skills that can be auto-spelled (ZC_AUTOSPELLLIST).
/// 01cd { <skill id>.L }*7
void clif_autospell(struct map_session_data *sd,int skill_lv)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1cd));
	WFIFOW(fd, 0)=0x1cd;

	if(skill_lv>0 && pc_checkskill(sd,MG_NAPALMBEAT)>0)
		WFIFOL(fd,2)= MG_NAPALMBEAT;
	else
		WFIFOL(fd,2)= 0x00000000;
	if(skill_lv>1 && pc_checkskill(sd,MG_COLDBOLT)>0)
		WFIFOL(fd,6)= MG_COLDBOLT;
	else
		WFIFOL(fd,6)= 0x00000000;
	if(skill_lv>1 && pc_checkskill(sd,MG_FIREBOLT)>0)
		WFIFOL(fd,10)= MG_FIREBOLT;
	else
		WFIFOL(fd,10)= 0x00000000;
	if(skill_lv>1 && pc_checkskill(sd,MG_LIGHTNINGBOLT)>0)
		WFIFOL(fd,14)= MG_LIGHTNINGBOLT;
	else
		WFIFOL(fd,14)= 0x00000000;
	if(skill_lv>4 && pc_checkskill(sd,MG_SOULSTRIKE)>0)
		WFIFOL(fd,18)= MG_SOULSTRIKE;
	else
		WFIFOL(fd,18)= 0x00000000;
	if(skill_lv>7 && pc_checkskill(sd,MG_FIREBALL)>0)
		WFIFOL(fd,22)= MG_FIREBALL;
	else
		WFIFOL(fd,22)= 0x00000000;
	if(skill_lv>9 && pc_checkskill(sd,MG_FROSTDIVER)>0)
		WFIFOL(fd,26)= MG_FROSTDIVER;
	else
		WFIFOL(fd,26)= 0x00000000;

	WFIFOSET(fd,packet_len(0x1cd));
	sd->menuskill_id = SA_AUTOSPELL;
	sd->menuskill_val = skill_lv;
}


/// Devotion's visual effect (ZC_DEVOTIONLIST).
/// 01cf <devoter id>.L { <devotee id>.L }*5 <max distance>.W
void clif_devotion(struct block_list *src, struct map_session_data *tsd)
{
	unsigned char buf[56];
	
	nullpo_retv(src);
	memset(buf,0,packet_len(0x1cf));

	WBUFW(buf,0) = 0x1cf;
	WBUFL(buf,2) = src->id;
	if( src->type == BL_MER )
	{// Mercenary's Devotion
		struct mercenary_data *md = BL_CAST(BL_MER,src);
		if( md && md->master && md->devotion_flag )
			WBUFL(buf,6) = md->master->bl.id;

		WBUFW(buf,26) = skill_get_range2(src, ML_DEVOTION, mercenary_checkskill(md, ML_DEVOTION), false);
	}
	else if (src->type == BL_ELEM)
	{// Aqua's Water Screen. Works like devotion.
		struct elemental_data *ed = BL_CAST(BL_ELEM,src);
		if (ed && ed->master && ed->water_screen_flag)
			WBUFL(buf,6) = ed->master->bl.id;

		WBUFW(buf, 26) = 11;// Doesn't appear to have a actural range but gotta give it something.
	}
	else
	{// Player's Devotion
		int i;
		struct map_session_data *sd = BL_CAST(BL_PC,src);
		if( sd == NULL )
			return;

		for( i = 0; i < 5; i++ )
			WBUFL(buf,6+4*i) = sd->devotion[i];
		WBUFW(buf,26) = skill_get_range2(src, CR_DEVOTION, pc_checkskill(sd, CR_DEVOTION), false);
	}

	if( tsd )
		clif_send(buf, packet_len(0x1cf), &tsd->bl, SELF);
	else
		clif_send(buf, packet_len(0x1cf), src, AREA);
}


/// Notifies clients in an area of an object's spirits.
/// 01d0 <id>.L <amount>.W (ZC_SPIRITS)
/// 01e1 <id>.L <amount>.W (ZC_SPIRITS2)
void clif_spiritball_sub(struct block_list *bl, struct block_list* target, enum send_target send_target)
{
	nullpo_retv(bl);

	unsigned char buf[8];

	WBUFW(buf, 0) = 0x1d0;
	WBUFL(buf,2)=bl->id;

	switch (bl->type) {
		case BL_PC:
			WBUFW(buf, 6) = ((struct map_session_data*)bl)->spiritball;
			break;
		case BL_HOM:
			WBUFW(buf, 6) = ((struct homun_data*)bl)->hom_spiritball;
			break;
	}

	clif_send(buf, packet_len(0x1d0), target == NULL ? bl : target, send_target);
}

void clif_spiritball(struct map_session_data *sd)
{
	clif_spiritball_sub(&sd->bl, NULL, AREA);
}

/*==========================================
 * ZC_MILLENNIUMSHIELD =  0x440
 * this+0x0 / short PacketType
 * this+0x2 / unsigned long AID
 * this+0x6 / short num
 * this+0x8 / short state
 *
 * State - How the heck does the state work?
 * MILLENNIUMSHIELD_STATE_STAND =  0x0
 * MILLENNIUMSHIELD_STATE_MOVE =  0x1
 *-----------------------------------------*/
int clif_millenniumshield(struct block_list* bl, short shield_count )
{
	unsigned char buf[10];

	WBUFW(buf,0) = 0x440;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = shield_count;
	WBUFW(buf,8) = 0;
	clif_send(buf,packet_len(0x440),bl,AREA);
	return 0;
}

/// Notifies clients in an area of an object's souls.
/// Note: Spirit spheres and Soul spheres work on
/// seprate systems officially, but both send out
/// the same packet which leads to confusion on how
/// much soul energy a Soul Reaper acturally has
/// should the player also have spirit spheres.
/// They will likely create a new packet for this soon
/// to seprate the animations for spirit and soul spheres.
/// For now well use this and replace it later when possible. [Rytech]
/// 
/// 01d0 <id>.L <amount>.W (ZC_SPIRITS)
/// 01e1 <id>.L <amount>.W (ZC_SPIRITS2)
int clif_soulball(struct map_session_data *sd)
{
	unsigned char buf[8];

	nullpo_ret(sd);

	WBUFW(buf,0)=0x1d0;
	WBUFL(buf,2)=sd->bl.id;
	WBUFW(buf,6)=sd->soulball;
	clif_send(buf,packet_len(0x1d0),&sd->bl,AREA);
	return 0;
}

/*==========================================
 * Homunculus Spirit Spheres
 *------------------------------------------*/
void clif_hom_spiritball(struct homun_data *hd)
{
	unsigned char buf[16];

	nullpo_retv(hd);

	WBUFW(buf,0)=0x1d0;
	WBUFL(buf,2)=hd->bl.id;
	WBUFW(buf,6)=hd->hom_spiritball;
	clif_send(buf,packet_len(0x1d0),&hd->bl,AREA);
}

/*==========================================
 * ZC_SPIRITS_ATTRIBUTE =  0x8cf
 * this+0x0 / short PacketType
 * this+0x2 / unsigned long AID
 * this+0x6 / short SpritsType
 * this+0x8 / short Num
 *
 * SpiritsType
 * SPIRITS_TYPE_NONE =  0x0
 * SPIRITS_TYPE_CHARM_WATER =  0x1
 * SPIRITS_TYPE_CHARM_LAND =  0x2
 * SPIRITS_TYPE_CHARM_FIRE =  0x3
 * SPIRITS_TYPE_CHARM_WIND =  0x4
 * SPIRTIS_TYPE_SPHERE =  0x5
 *------------------------------------------*/
int clif_spiritball_attribute(struct map_session_data *sd)
{
	unsigned char buf[10];

	nullpo_ret(sd);

	WBUFW(buf, 0) = 0x08cf;
	WBUFL(buf, 2) = sd->bl.id;
	WBUFW(buf, 6) = sd->charmball_type;
	WBUFW(buf, 8) = sd->charmball;
	clif_send(buf, packet_len(0x08cf),&sd->bl,AREA);
	return 0;
}

/// Notifies clients in area of a character's combo delay (ZC_COMBODELAY).
/// 01d2 <account id>.L <delay>.L
void clif_combo_delay(struct block_list *bl,int wait)
{
	unsigned char buf[32];

	nullpo_retv(bl);

	WBUFW(buf,0)=0x1d2;
	WBUFL(buf,2)=bl->id;
	WBUFL(buf,6)=wait;
	clif_send(buf,packet_len(0x1d2),bl,AREA);
}


/// Notifies clients in area that a character has blade-stopped another (ZC_BLADESTOP).
/// 01d1 <src id>.L <dst id>.L <flag>.L
/// flag:
///     0 = inactive
///     1 = active
void clif_bladestop(struct block_list *src, int dst_id, int active)
{
	unsigned char buf[32];

	nullpo_retv(src);

	WBUFW(buf,0)=0x1d1;
	WBUFL(buf,2)=src->id;
	WBUFL(buf,6)=dst_id;
	WBUFL(buf,10)=active;

	clif_send(buf,packet_len(0x1d1),src,AREA);
}


/// MVP effect (ZC_MVP).
/// 010c <account id>.L
void clif_mvp_effect(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retv(sd);

	WBUFW(buf,0)=0x10c;
	WBUFL(buf,2)=sd->bl.id;
	clif_send(buf,packet_len(0x10c),&sd->bl,AREA);
}


/// MVP item reward message (ZC_MVP_GETTING_ITEM).
/// 010a <name id>.W
void clif_mvp_item(struct map_session_data *sd, t_itemid nameid)
{
	struct PACKET_ZC_MVP_GETTING_ITEM p;

	p.packetType = 0x10a;
	p.itemId = client_nameid(nameid);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}


/// MVP EXP reward message (ZC_MVP_GETTING_SPECIAL_EXP).
/// 010b <exp>.L
void clif_mvp_exp(struct map_session_data *sd, unsigned int exp)
{
#if PACKETVER >= 20131223		// Kro remove this packet [Napster]
	if (battle_config.mvp_exp_reward_message) {
		char e_msg[CHAT_SIZE_MAX];
		sprintf(e_msg, msg_txt(sd,743), exp);
		clif_disp_overheadcolor_self(sd->fd, COLOR_CYAN, e_msg);	// Congratulations! You are the MVP! Your reward EXP Points are %u !!
	}
#else
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x10b));
	WFIFOW(fd,0)=0x10b;
	WFIFOL(fd,2)=cap_value(exp,0,INT32_MAX);
	WFIFOSET(fd,packet_len(0x10b));
#endif
}


/// Dropped MVP item reward message (ZC_THROW_MVPITEM).
/// 010d
///
/// "You are the MVP, but cannot obtain the reward because
///     you are overweight."
void clif_mvp_noitem(struct map_session_data* sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x10d));
	WFIFOW(fd,0) = 0x10d;
	WFIFOSET(fd,packet_len(0x10d));
}


/// Guild creation result (ZC_RESULT_MAKE_GUILD).
/// 0167 <result>.B
/// result:
///     0 = "Guild has been created."
///     1 = "You are already in a Guild."
///     2 = "That Guild Name already exists."
///     3 = "You need the neccessary item to create a Guild."
void clif_guild_created(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x167));
	WFIFOW(fd,0)=0x167;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x167));
}


/// Notifies the client that it is belonging to a guild (ZC_UPDATE_GDID).
/// 016c <guild id>.L <emblem id>.L <mode>.L <ismaster>.B <inter sid>.L <guild name>.24B
/// mode:
///     &0x001 = allow invite
///     &0x010 = allow expel
///     &0x100 = allow guild storage access
void clif_guild_belonginfo(struct map_session_data *sd)
{
	int ps,fd;
	struct guild* g;
	nullpo_retv(sd);
	nullpo_retv(g = sd->guild);

	fd=sd->fd;
	ps=guild_getposition(sd);
	WFIFOHEAD(fd,packet_len(0x16c));
	WFIFOW(fd,0)=0x16c;
	WFIFOL(fd,2)=g->guild_id;
	WFIFOL(fd,6)=g->emblem_id;
	WFIFOL(fd,10)=g->position[ps].mode;
	WFIFOB(fd, 14) = (bool)(sd->state.gmaster_flag == 1);
	WFIFOL(fd,15)=0;  // InterSID (unknown purpose)
	safestrncpy(WFIFOP(fd,19),g->name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x16c));
}


/// Guild member login notice.
/// 016d <account id>.L <char id>.L <status>.L (ZC_UPDATE_CHARSTAT)
/// 01f2 <account id>.L <char id>.L <status>.L <gender>.W <hair style>.W <hair color>.W (ZC_UPDATE_CHARSTAT2)
/// status:
///     0 = offline
///     1 = online
void clif_guild_memberlogin_notice(struct guild *g,int idx,int flag)
{
	unsigned char buf[64];
	struct map_session_data* sd;

	nullpo_retv(g);

	WBUFW(buf, 0)=0x1f2;
	WBUFL(buf, 2)=g->member[idx].account_id;
	WBUFL(buf, 6)=g->member[idx].char_id;
	WBUFL(buf,10)=flag;

	if( ( sd = g->member[idx].sd ) != NULL )
	{
		WBUFW(buf,14) = sd->status.sex;
		WBUFW(buf,16) = sd->status.hair;
		WBUFW(buf,18) = sd->status.hair_color;
		clif_send(buf,packet_len(0x1f2),&sd->bl,GUILD_WOS);
	}
	else if( ( sd = guild_getavailablesd(g) ) != NULL )
	{
		WBUFW(buf,14) = 0;
		WBUFW(buf,16) = 0;
		WBUFW(buf,18) = 0;
		clif_send(buf,packet_len(0x1f2),&sd->bl,GUILD);
	}
}

// Function `clif_guild_memberlogin_notice` sends info about
// logins and logouts of a guild member to the rest members.
// But at the 1st time (after a player login or map changing)
// the client won't show the message.
// So I suggest use this function for sending "first-time-info"
// to some player on entering the game or changing location. 
// At next time the client would always show the message.
// The function sends all the statuses in the single packet 
// to economize traffic. [LuzZza]
void clif_guild_send_onlineinfo(struct map_session_data *sd)
{
	struct guild *g;
	unsigned char buf[14*128];
	int i, count=0, p_len;
	
	nullpo_retv(sd);

	p_len = packet_len(0x16d);

	if(!(g = sd->guild))
		return;
	
	for(i=0; i<g->max_member; i++) {

		if(g->member[i].account_id > 0 &&
			g->member[i].account_id != sd->status.account_id) {

			WBUFW(buf,count*p_len) = 0x16d;
			WBUFL(buf,count*p_len+2) = g->member[i].account_id;
			WBUFL(buf,count*p_len+6) = g->member[i].char_id;
			WBUFL(buf,count*p_len+10) = g->member[i].online;
			count++;
		}
	}
	
	clif_send(buf, p_len*count, &sd->bl, SELF);
}


/// Bitmask of enabled guild window tabs (ZC_ACK_GUILD_MENUINTERFACE).
/// 014e <menu flag>.L
/// menu flag:
///      0x00 = Basic Info (always on)
///     &0x01 = Member manager
///     &0x02 = Positions
///     &0x04 = Skills
///     &0x10 = Expulsion list
///     &0x40 = Unknown (GMENUFLAG_ALLGUILDLIST)
///     &0x80 = Notice
void clif_guild_masterormember(struct map_session_data *sd)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x14e));
	WFIFOW(fd,0) = 0x14e;
	WFIFOL(fd,2) = (sd->state.gmaster_flag) ? 0xd7 : 0x57;
	WFIFOSET(fd,packet_len(0x14e));
}


/// Guild basic information (Territories [Valaris])
/// 0150 <guild id>.L <level>.L <member num>.L <member max>.L <exp>.L <max exp>.L <points>.L <honor>.L <virtue>.L <emblem id>.L <name>.24B <master name>.24B <manage land>.16B (ZC_GUILD_INFO)
/// 01b6 <guild id>.L <level>.L <member num>.L <member max>.L <exp>.L <max exp>.L <points>.L <honor>.L <virtue>.L <emblem id>.L <name>.24B <master name>.24B <manage land>.16B <zeny>.L (ZC_GUILD_INFO2)
/// 0a84 <guild id>.L <level>.L <member num>.L <member max>.L <exp>.L <max exp>.L <points>.L <honor>.L <virtue>.L <emblem id>.L <name>.24B <manage land>.16B <zeny>.L <master char id>.L (ZC_GUILD_INFO3)
void clif_guild_basicinfo(struct map_session_data *sd)
{
	int fd;
	struct guild *g;

#if PACKETVER < 20161228
	short packet_num = 0x1b6;
	short offset = NAME_LENGTH;
#else
	short packet_num = 0xa84;
	short offset = 0;
#endif

	nullpo_retv(sd);
	fd = sd->fd;

	if ((g = sd->guild) == NULL)
		return;

	WFIFOHEAD(fd, packet_len(packet_num));
	WFIFOW(fd, 0) = packet_num;
	WFIFOL(fd, 2)=g->guild_id;
	WFIFOL(fd, 6)=g->guild_lv;
	WFIFOL(fd,10)=g->connect_member;
	WFIFOL(fd,14)=g->max_member;
	WFIFOL(fd,18)=g->average_lv;
	WFIFOL(fd,22)=(uint32)cap_value(g->exp,0,INT32_MAX);
	WFIFOL(fd,26)= (uint32)cap_value(g->next_exp,0,INT32_MAX);
	WFIFOL(fd,30)=0;	// Tax Points
	WFIFOL(fd,34)=0;	// Honor: (left) Vulgar [-100,100] Famed (right)
	WFIFOL(fd,38)=0;	// Virtue: (down) Wicked [-100,100] Righteous (up)
	WFIFOL(fd,42)=g->emblem_id;
	safestrncpy(WFIFOCP(fd,46),g->name, NAME_LENGTH);
#if PACKETVER < 20161228
	safestrncpy(WFIFOCP(fd,70),g->master, NAME_LENGTH);
#endif
	// Calculate the number of castles owned.
	safestrncpy(WFIFOCP(fd,70+offset),msg_txt(sd,300+guild_castle_count(g->guild_id)),MAP_NAME_LENGTH_EXT); // "'N' castles"
	WFIFOL(fd,70+offset+16) = 0;  // zeny
#if PACKETVER >= 20161228
	WFIFOL(fd,70+offset+20) = g->member[0].char_id;
#endif
	WFIFOSET(fd,packet_len(packet_num));
}


/// Guild alliance and opposition list (ZC_MYGUILD_BASIC_INFO).
/// 014c <packet len>.W { <relation>.L <guild id>.L <guild name>.24B }*
void clif_guild_allianceinfo(struct map_session_data *sd)
{
	int fd,i,c;
	struct guild *g;

	nullpo_retv(sd);
	if( (g = sd->guild) == NULL )
		return;

	fd = sd->fd;
	WFIFOHEAD(fd, MAX_GUILDALLIANCE * 32 + 4);
	WFIFOW(fd, 0)=0x14c;
	for(i=c=0;i<MAX_GUILDALLIANCE;i++){
		struct guild_alliance *a=&g->alliance[i];
		if(a->guild_id>0){
			WFIFOL(fd,c*32+4)=a->opposition;
			WFIFOL(fd,c*32+8)=a->guild_id;
			safestrncpy(WFIFOP(fd,c*32+12),a->name,NAME_LENGTH);
			c++;
		}
	}
	WFIFOW(fd, 2)=c*32+4;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Guild member manager information.
/// 0154 <packet len>.W { <account>.L <char id>.L <hair style>.W <hair color>.W <gender>.W <class>.W <level>.W <contrib exp>.L <state>.L <position>.L <memo>.50B <name>.24B }* (ZC_MEMBERMGR_INFO)
/// 0aa5 <packet len>.W { <account>.L <char id>.L <hair style>.W <hair color>.W <gender>.W <class>.W <level>.W <contrib exp>.L <state>.L <position>.L <last login>.L }* (ZC_MEMBERMGR_INFO2)
/// state:
///     0 = offline
///     1 = online
/// memo:
///     probably member's self-introduction (unused, no client UI/packets for editing it)
void clif_guild_memberlist(struct map_session_data *sd)
{
	int fd;
	int i,c;
	struct guild *g;

#if PACKETVER < 20161228
	short packet_num = 0x154;
	short size = 104;
#else
	short packet_num = 0xaa5;
	short size = 34;
#endif

	nullpo_retv(sd);

	if( (fd = sd->fd) == 0 )
		return;
	if( (g = sd->guild) == NULL )
		return;

	WFIFOHEAD(fd, g->max_member * size + 4);
	WFIFOW(fd, 0) = packet_num;
	for(i=0,c=0;i<g->max_member;i++){
		struct guild_member *m=&g->member[i];
		if(m->account_id==0)
			continue;
		WFIFOL(fd,c*size+ 4)=m->account_id;
		WFIFOL(fd,c*size+ 8)=m->char_id;
		WFIFOW(fd,c*size+12)=m->hair;
		WFIFOW(fd,c*size+14)=m->hair_color;
		WFIFOW(fd,c*size+16)=m->gender;
		WFIFOW(fd,c*size+18)=m->class_;
		WFIFOW(fd,c*size+20)=m->lv;
		WFIFOL(fd,c*size+22)=(int)cap_value(m->exp,0,INT_MAX);
		WFIFOL(fd,c*size+26)=m->online;
		WFIFOL(fd,c*size+30)=m->position;
#if PACKETVER < 20161228
		memset(WFIFOP(fd,c*size+34),0,50);
		safestrncpy(WFIFOCP(fd,c*size+84),m->name,NAME_LENGTH);
#else
		WFIFOL(fd, c*size + 34) = m->last_login;
#endif
		c++;
	}
	WFIFOW(fd, 2) = c*size + 4;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Guild position name information (ZC_POSITION_ID_NAME_INFO).
/// 0166 <packet len>.W { <position id>.L <position name>.24B }*
void clif_guild_positionnamelist(struct map_session_data *sd)
{
	int i,fd;
	struct guild *g;

	nullpo_retv(sd);
	if( (g = sd->guild) == NULL )
		return;

	fd = sd->fd;
	WFIFOHEAD(fd, MAX_GUILDPOSITION * 28 + 4);
	WFIFOW(fd, 0)=0x166;
	for(i=0;i<MAX_GUILDPOSITION;i++){
		WFIFOL(fd,i*28+4)=i;
		safestrncpy(WFIFOP(fd,i*28+8),g->position[i].name,NAME_LENGTH);
	}
	WFIFOW(fd,2)=i*28+4;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Guild position information (ZC_POSITION_INFO).
/// 0160 <packet len>.W { <position id>.L <mode>.L <ranking>.L <pay rate>.L }*
/// mode:
///     &0x001 = allow invite
///     &0x010 = allow expel
///     &0x100 = allow guild storage access
/// ranking:
///     TODO
void clif_guild_positioninfolist(struct map_session_data *sd)
{
	int i,fd;
	struct guild *g;

	nullpo_retv(sd);
	if( (g = sd->guild) == NULL )
		return;

	fd = sd->fd;
	WFIFOHEAD(fd, MAX_GUILDPOSITION * 16 + 4);
	WFIFOW(fd, 0)=0x160;
	for(i=0;i<MAX_GUILDPOSITION;i++){
		struct guild_position *p=&g->position[i];
		WFIFOL(fd,i*16+ 4)=i;
		WFIFOL(fd,i*16+ 8)=p->mode;
		WFIFOL(fd,i*16+12)=i;
		WFIFOL(fd,i*16+16)=p->exp_mode;
	}
	WFIFOW(fd, 2)=i*16+4;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Notifies clients in a guild about updated position information (ZC_ACK_CHANGE_GUILD_POSITIONINFO).
/// 0174 <packet len>.W { <position id>.L <mode>.L <ranking>.L <pay rate>.L <position name>.24B }*
/// mode:
///     &0x001 = allow invite
///     &0x010 = allow expel
///     &0x100 = allow guild storage access
/// ranking:
///     TODO
void clif_guild_positionchanged(struct guild *g,int idx)
{
	// FIXME: This packet is intended to update the clients after a
	// commit of position info changes, not sending one packet per
	// position.
	struct map_session_data *sd;
	unsigned char buf[128];

	nullpo_retv(g);

	WBUFW(buf, 0)=0x174;
	WBUFW(buf, 2)=44;  // packet len
	// GUILD_REG_POSITION_INFO{
	WBUFL(buf, 4)=idx;
	WBUFL(buf, 8)=g->position[idx].mode;
	WBUFL(buf,12)=idx;
	WBUFL(buf,16)=g->position[idx].exp_mode;
	safestrncpy(WBUFP(buf,20),g->position[idx].name,NAME_LENGTH);
	// }*
	if( (sd=guild_getavailablesd(g))!=NULL )
		clif_send(buf,WBUFW(buf,2),&sd->bl,GUILD);
}


/// Notifies clients in a guild about updated member position assignments (ZC_ACK_REQ_CHANGE_MEMBERS).
/// 0156 <packet len>.W { <account id>.L <char id>.L <position id>.L }*
void clif_guild_memberpositionchanged(struct guild *g,int idx)
{
	// FIXME: This packet is intended to update the clients after a
	// commit of member position assignment changes, not sending one
	// packet per position.
	struct map_session_data *sd;
	unsigned char buf[64];

	nullpo_retv(g);

	WBUFW(buf, 0)=0x156;
	WBUFW(buf, 2)=16;  // packet len
	// MEMBER_POSITION_INFO{
	WBUFL(buf, 4)=g->member[idx].account_id;
	WBUFL(buf, 8)=g->member[idx].char_id;
	WBUFL(buf,12)=g->member[idx].position;
	// }*
	if( (sd=guild_getavailablesd(g))!=NULL )
		clif_send(buf,WBUFW(buf,2),&sd->bl,GUILD);
}


/// Sends emblems bitmap data to the client that requested it (ZC_GUILD_EMBLEM_IMG).
/// 0152 <packet len>.W <guild id>.L <emblem id>.L <emblem data>.?B
void clif_guild_emblem(struct map_session_data *sd,struct guild *g)
{
	int fd;
	nullpo_retv(sd);
	nullpo_retv(g);

	fd = sd->fd;
	if( g->emblem_len <= 0 )
		return;

	WFIFOHEAD(fd,g->emblem_len+12);
	WFIFOW(fd,0)=0x152;
	WFIFOW(fd,2)=g->emblem_len+12;
	WFIFOL(fd,4)=g->guild_id;
	WFIFOL(fd,8)=g->emblem_id;
	memcpy(WFIFOP(fd,12),g->emblem_data,g->emblem_len);
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Sends update of the guild id/emblem id to everyone in the area (ZC_CHANGE_GUILD).
/// 01b4 <id>.L <guild id>.L <emblem id>.W
void clif_guild_emblem_area(struct block_list* bl)
{
	uint8 buf[12];

	nullpo_retv(bl);

	// TODO this packet doesn't force the update of ui components that have the emblem visible
	//      (emblem in the flag npcs and emblem over the head in agit maps) [FlavioJS]
	WBUFW(buf,0) = 0x1b4;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = status_get_guild_id(bl);
	WBUFW(buf,10) = status_get_emblem_id(bl);
	clif_send(buf, 12, bl, AREA_WOS);
}


/// Sends guild skills (ZC_GUILD_SKILLINFO).
/// 0162 <packet len>.W <skill points>.W { <skill id>.W <type>.L <level>.W <sp cost>.W <atk range>.W <skill name>.24B <upgradable>.B }*
void clif_guild_skillinfo(struct map_session_data* sd)
{
	int fd;
	struct guild* g;
	int i,c;

	nullpo_retv(sd);
	if( (g = sd->guild) == NULL )
		return;

	fd = sd->fd;
	WFIFOHEAD(fd, 6 + MAX_GUILDSKILL*37);
	WFIFOW(fd,0) = 0x0162;
	WFIFOW(fd,4) = g->skill_point;
	for(i = 0, c = 0; i < MAX_GUILDSKILL; i++)
	{
		if(g->skill[i].id > 0 && guild_check_skill_require(g, g->skill[i].id))
		{
			int id = g->skill[i].id;
			int p = 6 + c*37;
			WFIFOW(fd,p+0) = id; 
			WFIFOL(fd,p+2) = skill_get_inf(id);
			WFIFOW(fd,p+6) = g->skill[i].lv;
			WFIFOW(fd,p+8) = skill_get_sp(id, g->skill[i].lv);
			WFIFOW(fd,p+10) = skill_get_range(id, g->skill[i].lv);
			safestrncpy((char*)WFIFOP(fd,p+12), skill_get_name(id), NAME_LENGTH);
			WFIFOB(fd,p+36)= (g->skill[i].lv < guild_skill_get_max(id) && sd == g->member[0].sd) ? 1 : 0;
			c++;
		}
	}
	WFIFOW(fd,2) = 6 + c*37;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Sends guild notice to client (ZC_GUILD_NOTICE).
/// 016f <subject>.60B <notice>.120B
void clif_guild_notice(struct map_session_data* sd)
{
	int fd;
	struct guild* g;

	nullpo_retv(sd);
	nullpo_retv(g = sd->guild);

	fd = sd->fd;

	if ( !session_isActive(fd) )
		return;
 
	if(g->mes1[0] == '\0' && g->mes2[0] == '\0')
		return;

	WFIFOHEAD(fd,packet_len(0x16f));
	WFIFOW(fd,0) = 0x16f;
	memcpy(WFIFOP(fd,2), g->mes1, MAX_GUILDMES1);
	memcpy(WFIFOP(fd,62), g->mes2, MAX_GUILDMES2);
	WFIFOSET(fd,packet_len(0x16f));
}


/// Guild invite (ZC_REQ_JOIN_GUILD).
/// 016a <guild id>.L <guild name>.24B
void clif_guild_invite(struct map_session_data *sd,struct guild *g)
{
	int fd;

	nullpo_retv(sd);
	nullpo_retv(g);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x16a));
	WFIFOW(fd,0)=0x16a;
	WFIFOL(fd,2)=g->guild_id;
	safestrncpy(WFIFOP(fd,6),g->name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x16a));
}


/// Reply to invite request (ZC_ACK_REQ_JOIN_GUILD).
/// 0169 <answer>.B
/// answer:
///     0 = Already in guild.
///     1 = Offer rejected.
///     2 = Offer accepted.
///     3 = Guild full.
void clif_guild_inviteack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x169));
	WFIFOW(fd,0)=0x169;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x169));
}


/// Notifies clients of a guild of a leaving member (ZC_ACK_LEAVE_GUILD).
/// 015a <char name>.24B <reason>.40B
void clif_guild_leave(struct map_session_data *sd,const char *name,const char *mes)
{
	unsigned char buf[128];

	nullpo_retv(sd);

	WBUFW(buf, 0)=0x15a;
	safestrncpy(WBUFP(buf, 2),name,NAME_LENGTH);
	memcpy(WBUFP(buf,26),mes,40);
	clif_send(buf,packet_len(0x15a),&sd->bl,GUILD_NOBG);
}


/// Notifies clients of a guild of an expelled member.
/// 015c <char name>.24B <reason>.40B <account name>.24B (ZC_ACK_BAN_GUILD)
/// 0839 <char name>.24B <reason>.40B (ZC_ACK_BAN_GUILD_SSO)
void clif_guild_expulsion(struct map_session_data* sd, const char* name, const char* mes, uint32 account_id)
{
	unsigned char buf[128];
#if PACKETVER < 20100803
	const unsigned short cmd = 0x15c;
#else
	const unsigned short cmd = 0x839;
#endif

	nullpo_retv(sd);

	WBUFW(buf,0) = cmd;
	safestrncpy((char*)WBUFP(buf,2), name, NAME_LENGTH);
	safestrncpy((char*)WBUFP(buf,26), mes, 40);
#if PACKETVER < 20100803
	memset(WBUFP(buf,66), 0, NAME_LENGTH); // account name (not used for security reasons)
#endif
	clif_send(buf, packet_len(cmd), &sd->bl, GUILD_NOBG);
}


/// Guild expulsion list (ZC_BAN_LIST).
/// 0163 <packet len>.W { <char name>.24B <account name>.24B <reason>.40B }*
/// 0163 <packet len>.W { <char name>.24B <reason>.40B }* (PACKETVER >= 20100803)
void clif_guild_expulsionlist(struct map_session_data* sd)
{
#if PACKETVER < 20100803
	const int offset = NAME_LENGTH*2+40;
#else
	const int offset = NAME_LENGTH+40;
#endif
	int fd, i, c = 0;
	struct guild* g;

	nullpo_retv(sd);

	if( (g = sd->guild) == NULL )
		return;

	fd = sd->fd;

	WFIFOHEAD(fd,4 + MAX_GUILDEXPULSION * offset);
	WFIFOW(fd,0) = 0x163;

	for( i = 0; i < MAX_GUILDEXPULSION; i++ )
	{
		struct guild_expulsion* e = &g->expulsion[i];

		if( e->account_id > 0 )
		{
			safestrncpy(WFIFOP(fd,4 + c*offset), e->name, NAME_LENGTH);
#if PACKETVER < 20100803
			memset(WFIFOP(fd,4 + c*offset+24), 0, NAME_LENGTH); // account name (not used for security reasons)
			memcpy(WFIFOP(fd,4 + c*offset+48), e->mes, 40);
#else
			memcpy(WFIFOP(fd,4 + c*offset+24), e->mes, 40);
#endif
			c++;
		}
	}
	WFIFOW(fd,2) = 4 + c*offset;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Guild chat message (ZC_GUILD_CHAT).
/// 017f <packet len>.W <message>.?B
void clif_guild_message(struct guild *g,uint32 account_id,const char *mes,int len)
{// TODO: account_id is not used, candidate for deletion? [Ai4rei]
	struct map_session_data *sd;
	uint8 buf[256];

	if( len == 0 )
	{
		return;
	}
	else if( len > sizeof(buf)-5 )
	{
		ShowWarning("clif_guild_message: Truncated message '%s' (len=%d, max=%d, guild_id=%d).\n", mes, len, sizeof(buf)-5, g->guild_id);
		len = sizeof(buf)-5;
	}

	WBUFW(buf, 0) = 0x17f;
	WBUFW(buf, 2) = len + 5;
	safestrncpy((char*)WBUFP(buf,4), mes, len+1);

	if ((sd = guild_getavailablesd(g)) != NULL)
		clif_send(buf, WBUFW(buf,2), &sd->bl, GUILD_NOBG);
}

/// Request for guild alliance (ZC_REQ_ALLY_GUILD).
/// 0171 <inviter account id>.L <guild name>.24B
void clif_guild_reqalliance(struct map_session_data *sd,uint32 account_id,const char *name)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x171));
	WFIFOW(fd,0)=0x171;
	WFIFOL(fd,2)=account_id;
	safestrncpy(WFIFOP(fd,6),name,NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x171));
}


/// Notifies the client about the result of a alliance request (ZC_ACK_REQ_ALLY_GUILD).
/// 0173 <answer>.B
/// answer:
///     0 = Already allied.
///     1 = You rejected the offer.
///     2 = You accepted the offer.
///     3 = They have too any alliances.
///     4 = You have too many alliances.
///     5 = Alliances are disabled.
void clif_guild_allianceack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x173));
	WFIFOW(fd,0)=0x173;
	WFIFOL(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x173));
}


/// Notifies the client that a alliance or opposition has been removed (ZC_DELETE_RELATED_GUILD).
/// 0184 <other guild id>.L <relation>.L
/// relation:
///     0 = Ally
///     1 = Enemy
void clif_guild_delalliance(struct map_session_data *sd,int guild_id,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	if (fd <= 0)
		return;
	WFIFOHEAD(fd,packet_len(0x184));
	WFIFOW(fd,0)=0x184;
	WFIFOL(fd,2)=guild_id;
	WFIFOL(fd,6)=flag;
	WFIFOSET(fd,packet_len(0x184));
}


/// Notifies the client about the result of a opposition request (ZC_ACK_REQ_HOSTILE_GUILD).
/// 0181 <result>.B
/// result:
///     0 = Antagonist has been set.
///     1 = Guild has too many Antagonists.
///     2 = Already set as an Antagonist.
///     3 = Antagonists are disabled.
void clif_guild_oppositionack(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x181));
	WFIFOW(fd,0)=0x181;
	WFIFOB(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x181));
}


/// Adds alliance or opposition (ZC_ADD_RELATED_GUILD).
/// 0185 <relation>.L <guild id>.L <guild name>.24B
/*
void clif_guild_allianceadded(struct guild *g,int idx)
{
	unsigned char buf[64];
	WBUFW(buf,0)=0x185;
	WBUFL(buf,2)=g->alliance[idx].opposition;
	WBUFL(buf,6)=g->alliance[idx].guild_id;
	safestrncpy(WBUFP(buf,10),g->alliance[idx].name,NAME_LENGTH);
	clif_send(buf,packet_len(0x185),guild_getavailablesd(g),GUILD);
}
*/


/// Notifies the client about the result of a guild break (ZC_ACK_DISORGANIZE_GUILD_RESULT).
/// 015e <reason>.L
///     0 = success
///     1 = invalid key (guild name, @see clif_parse_GuildBreak)
///     2 = there are still members in the guild
void clif_guild_broken(struct map_session_data *sd,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x15e));
	WFIFOW(fd,0)=0x15e;
	WFIFOL(fd,2)=flag;
	WFIFOSET(fd,packet_len(0x15e));
}


/// Displays emotion on an object (ZC_EMOTION).
/// 00c0 <id>.L <type>.B
/// type:
///     enum emotion_type
void clif_emotion(struct block_list *bl,int type)
{
	unsigned char buf[8];

	nullpo_retv(bl);

	WBUFW(buf,0)=0xc0;
	WBUFL(buf,2)=bl->id;
	WBUFB(buf,6)=type;
	clif_send(buf,packet_len(0xc0),bl,AREA);
}


/// Displays the contents of a talkiebox trap (ZC_TALKBOX_CHATCONTENTS).
/// 0191 <id>.L <contents>.80B
void clif_talkiebox(struct block_list* bl, const char* talkie)
{
	unsigned char buf[MESSAGE_SIZE+6];
	nullpo_retv(bl);

	WBUFW(buf,0) = 0x191;
	WBUFL(buf,2) = bl->id;
	safestrncpy((char*)WBUFP(buf,6),talkie,MESSAGE_SIZE);
	clif_send(buf,packet_len(0x191),bl,AREA);
}


/// Displays wedding effect centered on an object (ZC_CONGRATULATION).
/// 01ea <id>.L
void clif_wedding_effect(struct block_list *bl)
{
	unsigned char buf[6];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x1ea;
	WBUFL(buf,2) = bl->id;
	clif_send(buf, packet_len(0x1ea), bl, AREA);
}


/// Notifies the client of the name of the partner character (ZC_COUPLENAME).
/// 01e6 <partner name>.24B
void clif_callpartner(struct map_session_data *sd)
{
	unsigned char buf[26];

	nullpo_retv(sd);

	WBUFW(buf,0) = 0x1e6;

	if (sd->status.partner_id) {
		const char *p = map_charid2nick(sd->status.partner_id);
		struct map_session_data *p_sd = pc_get_partner(sd);
		if (p != NULL && p_sd != NULL && !p_sd->state.autotrade)
			safestrncpy(WBUFP(buf,2), p, NAME_LENGTH);
		else
			WBUFB(buf,2) = 0;
	}
	else
	{// Send zero-length name if no partner, to initialize the client buffer.
		WBUFB(buf,2) = 0;
	}

	clif_send(buf, packet_len(0x1e6), &sd->bl, AREA);
}


/// Initiates the partner "taming" process [DracoRPG] (ZC_START_COUPLE).
/// 01e4
/// This packet while still implemented by the client is no longer being officially used.
/*
void clif_marriage_process(struct map_session_data *sd)
{
	int fd;
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x1e4));
	WFIFOW(fd,0)=0x1e4;
	WFIFOSET(fd,packet_len(0x1e4));
}
*/


/// Notice of divorce (ZC_DIVORCE).
/// 0205 <partner name>.24B
void clif_divorced(struct map_session_data* sd, const char* name)
{
	int fd;
	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x205));
	WFIFOW(fd,0)=0x205;
	safestrncpy(WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x205));
}


/// Marriage proposal (ZC_REQ_COUPLE).
/// 01e2 <account id>.L <char id>.L <char name>.24B
/// This packet while still implemented by the client is no longer being officially used.
/*
void clif_marriage_proposal(int fd, struct map_session_data *sd, struct map_session_data* ssd)
{
	nullpo_retv(sd);

	WFIFOHEAD(fd,packet_len(0x1e2));
	WFIFOW(fd,0) = 0x1e2;
	WFIFOL(fd,2) = ssd->status.account_id;
	WFIFOL(fd,6) = ssd->status.char_id;
	safestrncpy((char*)WFIFOP(fd,10), ssd->status.name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x1e2));
}
*/


/*==========================================
 *
 *------------------------------------------*/
void clif_disp_onlyself(struct map_session_data *sd, const char *mes, int len)
{
	clif_disp_message(&sd->bl, mes, len, SELF);
}

/*==========================================
 * Displays a message using the guild-chat colors to the specified targets. [Skotlex]
 *------------------------------------------*/
void clif_disp_message(struct block_list* src, const char* mes, int len, enum send_target target)
{
	unsigned char buf[256];

	if( len == 0 )
	{
		return;
	}
	else if( len > sizeof(buf)-5 )
	{
		ShowWarning("clif_disp_message: Truncated message '%s' (len=%d, max=%d, aid=%d).\n", mes, len, sizeof(buf)-5, src->id);
		len = sizeof(buf)-5;
	}

	WBUFW(buf, 0) = 0x17f;
	WBUFW(buf, 2) = len + 5;
	safestrncpy((char*)WBUFP(buf,4), mes, len+1);
	clif_send(buf, WBUFW(buf,2), src, target);
}


/// Notifies the client about the result of a request to disconnect another player (ZC_ACK_DISCONNECT_CHARACTER).
/// 00cd <result>.L (unknown packet version or invalid information at packet_len_table)
/// 00cd <result>.B
/// result:
///     0 = failure
///     1 = success
void clif_GM_kickack(struct map_session_data *sd, int id)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0xcd));
	WFIFOW(fd,0) = 0xcd;
	WFIFOB(fd,2) = id;  // FIXME: this is not account id
	WFIFOSET(fd, packet_len(0xcd));
}


void clif_GM_kick(struct map_session_data *sd, struct map_session_data *tsd)
{
	int fd;

	nullpo_retv(tsd);

	fd = tsd->fd;

	if (sd == NULL)
		tsd->state.keepshop = true;

	if( fd > 0 )
		clif_authfail_fd(fd, 15);
	else {
		// Close vending/buyingstore
		if (sd) {
			if (tsd->state.vending)
				vending_closevending(tsd);
			else if (tsd->state.buyingstore)
				buyingstore_close(tsd);
		}
		map_quit(tsd);
	}

	if (sd)
		clif_GM_kickack(sd, tsd->status.account_id);
}


/// Displays various manner-related status messages (ZC_ACK_GIVE_MANNER_POINT).
/// 014a <result>.L
/// result:
///     0 = "A manner point has been successfully aligned."
///     1 = MP_FAILURE_EXHAUST
///     2 = MP_FAILURE_ALREADY_GIVING
///     3 = "Chat Block has been applied by GM due to your ill-mannerous action."
///     4 = "Automated Chat Block has been applied due to Anti-Spam System."
///     5 = "You got a good point from %s."
void clif_manner_message(struct map_session_data* sd, uint32 type)
{
	int fd;
	nullpo_retv(sd);
	
	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x14a));
	WFIFOW(fd,0) = 0x14a;
	WFIFOL(fd,2) = type;
	WFIFOSET(fd, packet_len(0x14a));
}


/// Followup to 0x14a type 3/5, informs who did the manner adjustment action (ZC_NOTIFY_MANNER_POINT_GIVEN).
/// 014b <type>.B <GM name>.24B
/// type:
///     0 = positive (unmute)
///     1 = negative (mute)
void clif_GM_silence(struct map_session_data* sd, struct map_session_data* tsd, uint8 type)
{
	int fd;	
	nullpo_retv(sd);
	nullpo_retv(tsd);

	fd = tsd->fd;
	WFIFOHEAD(fd,packet_len(0x14b));
	WFIFOW(fd,0) = 0x14b;
	WFIFOB(fd,2) = type;
	safestrncpy((char*)WFIFOP(fd,3), sd->status.name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x14b));
}


/// Notifies the client about the result of a request to allow/deny whispers from a player (ZC_SETTING_WHISPER_PC).
/// 00d1 <type>.B <result>.B
/// type:
///     0 = /ex (deny)
///     1 = /in (allow)
/// result:
///     0 = success
///     1 = failure
///     2 = too many blocks
void clif_wisexin(struct map_session_data *sd,int type,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xd1));
	WFIFOW(fd,0)=0xd1;
	WFIFOB(fd,2)=type;
	WFIFOB(fd,3)=flag;
	WFIFOSET(fd,packet_len(0xd1));
}

/// Notifies the client about the result of a request to allow/deny whispers from anyone (ZC_SETTING_WHISPER_STATE).
/// 00d2 <type>.B <result>.B
/// type:
///     0 = /exall (deny)
///     1 = /inall (allow)
/// result:
///     0 = success
///     1 = failure
void clif_wisall(struct map_session_data *sd,int type,int flag)
{
	int fd;

	nullpo_retv(sd);

	fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0xd2));
	WFIFOW(fd,0)=0xd2;
	WFIFOB(fd,2)=type;
	WFIFOB(fd,3)=flag;
	WFIFOSET(fd,packet_len(0xd2));
}


/// Play a BGM! [Rikter/Yommy] (ZC_PLAY_NPC_BGM).
/// 07fe <bgm>.24B
void clif_playBGM(struct map_session_data* sd, const char* name)
{
	int fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x7fe));
	WFIFOW(fd,0) = 0x7fe;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x7fe));
}


/// Plays/stops a wave sound (ZC_SOUND).
/// 01d3 <file name>.24B <act>.B <term>.L <npc id>.L
/// file name:
///     relative to data\wav
/// act:
///     0 = play (once)
///     1 = play (repeat, does not work)
///     2 = stops all sound instances of file name (does not work)
/// term:
///     unknown purpose, only relevant to act = 1
/// npc id:
///     The accustic direction of the sound is determined by the
///     relative position of the NPC to the player (3D sound).
void clif_soundeffect(struct map_session_data* sd, struct block_list* bl, const char* name, int type)
{
	int fd;

	nullpo_retv(sd);
	nullpo_retv(bl);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x1d3));
	WFIFOW(fd,0) = 0x1d3;
	safestrncpy((char*)WFIFOP(fd,2), name, NAME_LENGTH);
	WFIFOB(fd,26) = type;
	WFIFOL(fd,27) = 0;
	WFIFOL(fd,31) = bl->id;
	WFIFOSET(fd,packet_len(0x1d3));
}

void clif_soundeffectall(struct block_list* bl, const char* name, int type, enum send_target coverage)
{
	unsigned char buf[40];

	nullpo_retv(bl);

	WBUFW(buf,0) = 0x1d3;
	safestrncpy((char*)WBUFP(buf,2), name, NAME_LENGTH);
	WBUFB(buf,26) = type;
	WBUFL(buf,27) = 0;
	WBUFL(buf,31) = bl->id;
	clif_send(buf, packet_len(0x1d3), bl, coverage);
}


/// Displays special effects (npcs, weather, etc) [Valaris] (ZC_NOTIFY_EFFECT2).
/// 01f3 <id>.L <effect id>.L
/// effect id:
///     @see doc/effect_list.txt
void clif_specialeffect(struct block_list* bl, int type, enum send_target target)
{
	unsigned char buf[24];

	nullpo_retv(bl);

	memset(buf, 0, packet_len(0x1f3));

	WBUFW(buf,0) = 0x1f3;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = type;

	clif_send(buf, packet_len(0x1f3), bl, target);

	if (disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x1f3), bl, SELF);
	}
}

void clif_specialeffect_single(struct block_list* bl, int type, int fd)
{
	WFIFOHEAD(fd,10);
	WFIFOW(fd,0) = 0x1f3;
	WFIFOL(fd,2) = bl->id;
	WFIFOL(fd,6) = type;
	WFIFOSET(fd,10);
}


/// Notifies clients of an special/visual effect that accepts an value (ZC_NOTIFY_EFFECT3).
/// 0284 <id>.L <effect id>.L <num data>.L
/// effect id:
///     @see doc/effect_list.txt
/// num data:
///     effect-dependent value
void clif_specialeffect_value(struct block_list* bl, int effect_id, int num, send_target target)
{
	uint8 buf[14];

	WBUFW(buf,0) = 0x284;
	WBUFL(buf,2) = bl->id;
	WBUFL(buf,6) = effect_id;
	WBUFL(buf,10) = num;

	clif_send(buf, packet_len(0x284), bl, target);

	if( disguised(bl) )
	{
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x284), bl, SELF);
	}
}

/**
 * Modification of clif_disp_overheadcolor to send colored messages to players to chat log only (doesn't display overhead).
 *
 * 02c1 <packet len>.W <id>.L <color>.L <message>.?B
 *
 * @param fd    Target fd to send the message to
 * @param color Message color (RGB format: 0xRRGGBB)
 * @param msg   Message text
 */
void clif_disp_overheadcolor_self(int fd, uint32 color, const char *msg)
{
	uint16 msg_len = (uint16)strlen(msg) + 1;

	WFIFOHEAD(fd,msg_len + 12);
	WFIFOW(fd,0) = 0x2C1;
	WFIFOW(fd,2) = msg_len + 12;
	WFIFOL(fd,4) = 0;
	WFIFOL(fd,8) = RGB2BGR(color);
	safestrncpy((char*)WFIFOP(fd,12), msg, msg_len);
	WFIFOSET(fd, msg_len + 12);
}

/**
 * Monster/NPC color chat [SnakeDrak] (ZC_NPC_CHAT).
 *
 * 02c1 <packet len>.W <id>.L <color>.L <message>.?B
 *
 * @param bl    Source block list.
 * @param color Message color (RGB format: 0xRRGGBB)
 * @param msg   Message text
 */
void clif_disp_overheadcolor(struct block_list* bl, uint32 color, const char *msg)
{
	uint16 msg_len = (uint16)strlen(msg) + 1;
	uint8 buf[256];

	nullpo_retv(bl);

	if (msg_len > sizeof(buf)-12) {
		ShowWarning("clif_disp_overheadcolor: Truncating too long message '%s'.\n", msg);
		msg_len = sizeof(buf)-12;
	}

	WBUFW(buf,0) = 0x2C1;
	WBUFW(buf,2) = msg_len + 12;
	WBUFL(buf,4) = bl->id;
	WBUFL(buf,8) = RGB2BGR(color);
	memcpy(WBUFP(buf,12), msg, msg_len);

	clif_send(buf, WBUFW(buf,2), bl, AREA_CHAT_WOC);
}

/**
 * Notifies the client that the storage window is still open
 *
 * Should only be used in cases where the client closed the
 * storage window without server's consent
 */
void clif_refresh_storagewindow(struct map_session_data *sd) {
	// Notify the client that the storage is open
	if (sd->state.storage_flag == 1) {
		storage_sortitem(sd->storage.u.items_storage, ARRAYLENGTH(sd->storage.u.items_storage));
		clif_storagelist(sd, sd->storage.u.items_storage, ARRAYLENGTH(sd->storage.u.items_storage), "Storage");
		clif_updatestorageamount(sd, sd->storage.amount, MAX_STORAGE);
	}
	// Notify the client that the gstorage is open otherwise it will
	// remain locked forever and nobody will be able to access it
	if (sd->state.storage_flag == 2) {
		struct s_storage *gstor = guild2storage2(sd->status.guild_id);

		if (!gstor) // Shouldn't happen. The information should already be at the map-server
			intif_request_guild_storage(sd->status.account_id, sd->status.guild_id);
		else {
			storage_sortitem(gstor->u.items_guild, ARRAYLENGTH(gstor->u.items_guild));
			clif_storagelist(sd, gstor->u.items_guild, ARRAYLENGTH(gstor->u.items_guild), "Storage");
			clif_updatestorageamount(sd, gstor->amount, MAX_GUILD_STORAGE);
		}
	}
}

// refresh the client's screen, getting rid of any effects
void clif_refresh(struct map_session_data *sd)
{
	nullpo_retv(sd);

	clif_changemap(sd,sd->bl.m,sd->bl.x,sd->bl.y);
	clif_inventorylist(sd);
	clif_equipswitch_list(sd);
	if(pc_iscarton(sd)) {
		clif_cartlist(sd);
		clif_updatestatus(sd,SP_CARTINFO);
	}
	clif_updatestatus(sd,SP_WEIGHT);
	clif_updatestatus(sd,SP_MAXWEIGHT);
	clif_updatestatus(sd,SP_STR);
	clif_updatestatus(sd,SP_AGI);
	clif_updatestatus(sd,SP_VIT);
	clif_updatestatus(sd,SP_INT);
	clif_updatestatus(sd,SP_DEX);
	clif_updatestatus(sd,SP_LUK);
	if (sd->spiritball)
		clif_spiritball_single(sd);
	if (sd->shieldball)
		clif_millenniumshield_single(sd->fd, &sd->bl, sd->shieldball);
	if (sd->rageball)
		clif_millenniumshield_single(sd->fd, &sd->bl, sd->rageball);
	if (sd->charmball)
		clif_spiritball_attribute_single(sd->fd, sd);
	if (sd->soulball)
		clif_soulball_single(sd->fd, sd);
	if (sd->vd.cloth_color)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_CLOTHES_COLOR,sd->vd.cloth_color,SELF);
	if (sd->vd.body_style)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_BODY2,sd->vd.body_style,SELF);
	if(hom_is_active(sd->hd))
		clif_send_homdata(sd,SP_ACK,0);
	if( sd->md )
	{
		clif_mercenary_info(sd);
		clif_mercenary_skillblock(sd);
	}
	if( sd->ed )
		clif_elemental_info(sd);
	map_foreachinrange(clif_getareachar,&sd->bl,AREA_SIZE,BL_ALL,sd);
	clif_weather_check(sd);
	if( sd->chatID )
		chat_leavechat(sd,0);
	if( sd->state.vending )
		clif_openvending(sd, sd->bl.id, sd->vending);
	if( pc_issit(sd) )
		clif_sitting(&sd->bl, false); // just send to self, not area
	if( pc_isdead(sd) ) //When you refresh, resend the death packet.
		clif_clearunit_single(sd->bl.id,CLR_DEAD,sd->fd);
	else
		clif_changed_dir(&sd->bl, SELF);
	clif_efst_status_change_sub(&sd->bl, &sd->bl, SELF);

	//Cancel Trading State 
	if (sd->state.trading)
		trade_tradecancel(sd);
	//Cancel Buying/Selling State
	if (sd->state.buyingstore)
		buyingstore_close(sd);

#ifndef TXT_ONLY
	mail_clear(sd);
#endif

	if (disguised(&sd->bl)) {/* refresh-da */
		short disguise = sd->disguise;
		pc_disguise(sd, 0);
		pc_disguise(sd, disguise);
	}
	clif_refresh_storagewindow(sd);
}


/// Updates the object's (bl) name on client.
/// Used to update when a char leaves a party/guild. [Skotlex]
/// Needed because when you send a 0x95 packet, the client will not remove the cached party/guild info that is not sent.
/// 0095 <id>.L <char name>.24B (ZC_ACK_REQNAME)
/// 0195 <id>.L <char name>.24B <party name>.24B <guild name>.24B <position name>.24B (ZC_ACK_REQNAMEALL)
/// 0a30 <id>.L <char name>.24B <party name>.24B <guild name>.24B <position name>.24B <title ID>.L (ZC_ACK_REQNAMEALL2)
void clif_name(struct block_list* src, struct block_list *bl, send_target target)
{
	unsigned char buf[106];
	int cmd = 0x95, ps = -1;

	nullpo_retv(src);
	nullpo_retv(bl);

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = bl->id;

	switch( bl->type )
	{
	case BL_PC:
		{
			struct map_session_data *sd = (struct map_session_data *)bl;
			struct party_data *p = NULL;

#if PACKETVER >= 20150513
			WBUFW(buf, 0) = cmd = 0xa30;
#else
			WBUFW(buf, 0) = cmd = 0x195;
#endif
			
			//Requesting your own "shadow" name. [Skotlex]
			if (src == bl && target == SELF && sd->disguise)
			{
				WBUFL(buf,2) = -bl->id;
			}

			if( sd->fakename[0] )
			{
				safestrncpy((char*)WBUFP(buf,6), sd->fakename, NAME_LENGTH);
				WBUFB(buf,30) = WBUFB(buf,54) = WBUFB(buf,78) = 0;
#if PACKETVER >= 20150513
				WBUFL(buf,102) = sd->status.title_id; // Title ID
#endif
				break;
			}
			safestrncpy((char*)WBUFP(buf,6), sd->status.name, NAME_LENGTH);

			if( sd->status.party_id )
			{
				p = party_search(sd->status.party_id);
			}

			// do not display party unless the player is also in a guild
			if( p && (sd->guild || battle_config.display_party_name ) )
			{
				safestrncpy((char*)WBUFP(buf,30), p->party.name, NAME_LENGTH);
			}
			else
			{
				WBUFB(buf,30) = 0;
			}

			if (sd->guild)
			{
				int position;
			
				// Will get the position of the guild the player is in
				position = guild_getposition(sd);
			
				safestrncpy(WBUFCP(buf, 54), sd->guild->name, NAME_LENGTH);
				safestrncpy(WBUFCP(buf, 78), sd->guild->position[position].name, NAME_LENGTH);
			}
			else if (sd->clan)
			{
				WBUFB(buf, 54) = 0;
				safestrncpy(WBUFCP(buf, 78), sd->clan->name, NAME_LENGTH);
			}
			else { //Assume no guild and no clan
				WBUFB(buf,54) = 0;
				WBUFB(buf,78) = 0;
			}

#if PACKETVER >= 20150513
			WBUFL(buf, 102) = sd->status.title_id; // Title ID
#endif
		}
		break;
	//[blackhole89]
	case BL_HOM:
		safestrncpy(WBUFP(buf,6), ((TBL_HOM*)bl)->homunculus.name, NAME_LENGTH);
		break;
	case BL_MER:
		safestrncpy(WBUFP(buf,6), ((TBL_MER*)bl)->db->name, NAME_LENGTH);
		break;
	case BL_ELEM:
		safestrncpy(WBUFP(buf,6), ((TBL_ELEM*)bl)->db->name, NAME_LENGTH);
		break;
	case BL_PET:
		safestrncpy(WBUFP(buf,6), ((TBL_PET*)bl)->pet.name, NAME_LENGTH);
		break;
	case BL_NPC:
		safestrncpy(WBUFP(buf,6), ((TBL_NPC*)bl)->name, NAME_LENGTH);
		break;
	case BL_MOB:
		{
			struct mob_data *md = (struct mob_data *)bl;
			nullpo_retv(md);

			safestrncpy(WBUFP(buf,6), md->name, NAME_LENGTH);
			if( md->guardian_data && md->guardian_data->guild_id )
			{
#if PACKETVER >= 20150513
				WBUFW(buf, 0) = cmd = 0xa30;
#else
				WBUFW(buf, 0) = cmd = 0x195;
#endif
				WBUFB(buf,30) = 0;
				safestrncpy(WBUFP(buf,54), md->guardian_data->guild_name, NAME_LENGTH);
				safestrncpy(WBUFP(buf,78), md->guardian_data->castle->castle_name, NAME_LENGTH);
			}
			else if( battle_config.show_mob_info )
			{
				char mobhp[50], *str_p = mobhp;
#if PACKETVER >= 20150513
				WBUFW(buf, 0) = cmd = 0xa30;
#else
				WBUFW(buf, 0) = cmd = 0x195;
#endif
				if( battle_config.show_mob_info&4 )
					str_p += sprintf(str_p, "Lv. %d | ", md->level);
				if( battle_config.show_mob_info&1 )
					str_p += sprintf(str_p, "HP: %u/%u | ", md->status.hp, md->status.max_hp);
				if( battle_config.show_mob_info&2 )
					str_p += sprintf(str_p, "HP: %u%% | ", get_percentage(md->status.hp, md->status.max_hp));
				//Even thought mobhp ain't a name, we send it as one so the client
				//can parse it. [Skotlex]
				if( str_p != mobhp )
				{
					*(str_p-3) = '\0'; //Remove trailing space + pipe.
					safestrncpy(WBUFP(buf,30), mobhp, NAME_LENGTH);
					WBUFB(buf,54) = 0;
					WBUFB(buf,78) = 0;
				}
			}
#if PACKETVER >= 20150513
			WBUFL(buf, 102) = 0; // Title ID
#endif
		}
		break;
	case BL_CHAT:	//FIXME: Clients DO request this... what should be done about it? The chat's title may not fit... [Skotlex]
//		safestrncpy(WBUFP(buf,6), (struct chat*)->title, NAME_LENGTH);
//		break;
		return;
	default:
		ShowError("clif_name: bad type %d(%d)\n", bl->type, bl->id);
		return;
	}

	clif_send(buf, packet_len(cmd), src, target);
}

/// Taekwon Jump (TK_HIGHJUMP) effect (ZC_HIGHJUMP).
/// 01ff <id>.L <x>.W <y>.W
///
/// Visually moves(instant) a character to x,y. The char moves even
/// when the target cell isn't walkable. If the char is sitting it
/// stays that way.
void clif_slide(struct block_list *bl, int x, int y)
{
	unsigned char buf[10];
	nullpo_retv(bl);

	WBUFW(buf, 0) = 0x01ff;
	WBUFL(buf, 2) = bl->id;
	WBUFW(buf, 6) = x;
	WBUFW(buf, 8) = y;
	clif_send(buf, packet_len(0x1ff), bl, AREA);

	if( disguised(bl) )
	{
		WBUFL(buf,2) = -bl->id;
		clif_send(buf, packet_len(0x1ff), bl, SELF);
	}
}


/// Public chat message (ZC_NOTIFY_CHAT). lordalfa/Skotlex - used by @me as well
/// 008d <packet len>.W <id>.L <message>.?B
void clif_disp_overhead_(struct block_list *bl, const char* mes, enum send_target flag)
{
	unsigned char buf[256]; //This should be more than sufficient, the theoretical max is CHAT_SIZE + 8 (pads and extra inserted crap)
	uint16 len_mes;

	nullpo_retv(bl);
	nullpo_retv(mes);
	len_mes = (uint16)strlen(mes)+1; //Account for \0

	if (len_mes > sizeof(buf)-8) {
		ShowError("clif_disp_overhead: Message too long\n");
		len_mes = sizeof(buf)-8; //Trunk it to avoid problems.
	}
	// send message to others
	if (flag == AREA) {
		WBUFW(buf, 0) = 0x8d;
		WBUFW(buf, 2) = len_mes + 8; // len of message + 8 (command+len+id)
		WBUFL(buf, 4) = bl->id;
		safestrncpy(WBUFCP(buf, 8), mes, len_mes);
		clif_send(buf, WBUFW(buf, 2), bl, AREA_CHAT_WOC);
	}

	// send back message to the speaker
	if( bl->type == BL_PC ) {
		WBUFW(buf,0) = 0x8e;
		WBUFW(buf, 2) = len_mes + 4;
		safestrncpy((char*)WBUFP(buf,4), mes, len_mes);
		clif_send(buf, WBUFW(buf,2), bl, SELF);
	}

}

/*==========================
 * Minimap fix [Kevin]
 * Remove dot from minimap 
 *--------------------------*/
void clif_party_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[16];
	nullpo_retv(sd);
	WBUFW(buf,0)=0x107;
	WBUFL(buf,2)=sd->status.account_id;
	WBUFW(buf,6)=-1;
	WBUFW(buf,8)=-1;
	clif_send(buf,packet_len(0x107),&sd->bl,PARTY_SAMEMAP_WOS);
}


/// Displays a skill message (thanks to Rayce) (ZC_SKILLMSG).
/// 0215 <msg id>.L
/// msg id:
///     0x15 = End all negative status (PA_GOSPEL)
///     0x16 = Immunity to all status (PA_GOSPEL)
///     0x17 = MaxHP +100% (PA_GOSPEL)
///     0x18 = MaxSP +100% (PA_GOSPEL)
///     0x19 = All stats +20 (PA_GOSPEL)
///     0x1c = Enchant weapon with Holy element (PA_GOSPEL)
///     0x1d = Enchant armor with Holy element (PA_GOSPEL)
///     0x1e = DEF +25% (PA_GOSPEL)
///     0x1f = ATK +100% (PA_GOSPEL)
///     0x20 = HIT/Flee +50 (PA_GOSPEL)
///     0x28 = Full strip failed because of coating (ST_FULLSTRIP)
///     ? = nothing
void clif_gospel_info(struct map_session_data *sd, int type)
{
	int fd=sd->fd;
	WFIFOHEAD(fd,packet_len(0x215));
	WFIFOW(fd,0)=0x215;
	WFIFOL(fd,2)=type;
	WFIFOSET(fd, packet_len(0x215));

}


/// Multi-purpose mission information packet (ZC_STARSKILL).
/// 020e <mapname>.24B <monster_id>.L <star>.B <result>.B
/// result:
///      0 = Star Gladiator %s has designed <mapname>'s as the %s.
///      star:
///          0 = Place of the Sun
///          1 = Place of the Moon
///          2 = Place of the Stars
///      1 = Star Gladiator %s's %s: <mapname>
///      star:
///          0 = Place of the Sun
///          1 = Place of the Moon
///          2 = Place of the Stars
///      10 = Star Gladiator %s has designed <mapname>'s as the %s.
///      star:
///          0 = Target of the Sun
///          1 = Target of the Moon
///          2 = Target of the Stars
///      11 = Star Gladiator %s's %s: <mapname used as monster name>
///      star:
///          0 = Monster of the Sun
///          1 = Monster of the Moon
///          2 = Monster of the Stars
///      20 = [TaeKwon Mission] Target Monster : <mapname used as monster name> (<star>%)
///      21 = [Taming Mission] Target Monster : <mapname used as monster name>
///      22 = [Collector Rank] Target Item : <monster_id used as item id>
///      30 = [Sun, Moon and Stars Angel] Designed places and monsters have been reset.
///      40 = Target HP : <monster_id used as HP>
void clif_starskill(struct map_session_data* sd, const char* mapname, int monster_id, unsigned char star, unsigned char result)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x20e));
	WFIFOW(fd,0) = 0x20e;
	safestrncpy((char*)WFIFOP(fd,2), mapname, NAME_LENGTH);
	WFIFOL(fd,26) = monster_id;
	WFIFOB(fd,30) = star;
	WFIFOB(fd,31) = result;
	WFIFOSET(fd,packet_len(0x20e));
}

/*==========================================
 * Info about Star Glaldiator save map [Komurka]
 * type: 1: Information, 0: Map registered
 *------------------------------------------*/
void clif_feel_info(struct map_session_data* sd, unsigned char feel_level, unsigned char type)
{
	char mapname[MAP_NAME_LENGTH_EXT];

	mapindex_getmapname_ext(mapindex_id2name(sd->feel_map[feel_level].index), mapname);
	clif_starskill(sd, mapname, 0, feel_level, type ? 1 : 0);
}

/*==========================================
 * Info about Star Glaldiator hate mob [Komurka]
 * type: 1: Register mob, 0: Information.
 *------------------------------------------*/
void clif_hate_info(struct map_session_data *sd, unsigned char hate_level,int class_, unsigned char type)
{
	if( pcdb_checkid(class_) )
	{
		clif_starskill(sd, job_name(class_), class_, hate_level, type ? 10 : 11);
	}
	else if( mobdb_checkid(class_) )
	{
		clif_starskill(sd, mob_db(class_)->jname, class_, hate_level, type ? 10 : 11);
	}
	else
	{
		ShowWarning("clif_hate_info: Received invalid class %d for this packet (char_id=%d, hate_level=%u, type=%u).\n", class_, sd->status.char_id, (unsigned int)hate_level, (unsigned int)type);
	}
}

/*==========================================
 * Info about TaeKwon Do TK_MISSION mob [Skotlex]
 *------------------------------------------*/
void clif_mission_info(struct map_session_data *sd, int mob_id, unsigned char progress)
{
	clif_starskill(sd, mob_db(mob_id)->jname, mob_id, progress, 20);
}

/*==========================================
 * Feel/Hate reset (thanks to Rayce) [Skotlex]
 *------------------------------------------*/
void clif_feel_hate_reset(struct map_session_data *sd)
{
	clif_starskill(sd, "", 0, 0, 30);
}


/// Send configurations (ZC_CONFIG).
/// 02d9 <type>.L <value>.L
/// type:
///     0 = open equip window
///     value:
///         0 = disabled
///         1 = enabled
void clif_zc_config(struct map_session_data* sd, enum CZ_CONFIG type, int flag) {
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x2d9));
	WFIFOW(fd, 0) = 0x2d9;
	WFIFOL(fd, 2) = type;
	WFIFOL(fd, 6) = flag;
	WFIFOSET(fd, packet_len(0x2d9));
}


/// The player's 'view equip' state, sent during login (ZC_CONFIG_NOTIFY).
/// 02da <open equip window>.B
/// open equip window:
///     0 = disabled
///     1 = enabled
void clif_equipcheckbox(struct map_session_data* sd) {
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x2da));
	WFIFOW(fd, 0) = 0x2da;
	WFIFOB(fd, 2) = (sd->status.show_equip ? 1 : 0);
	WFIFOSET(fd, packet_len(0x2da));
}


/// Sends info about a player's equipped items.
/// 02d7 <packet len>.W <name>.24B <class>.W <hairstyle>.W <up-viewid>.W <mid-viewid>.W <low-viewid>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.26B* (ZC_EQUIPWIN_MICROSCOPE)
/// 02d7 <packet len>.W <name>.24B <class>.W <hairstyle>.W <bottom-viewid>.W <mid-viewid>.W <up-viewid>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.28B* (ZC_EQUIPWIN_MICROSCOPE, PACKETVER >= 20100629)
/// 0859 <packet len>.W <name>.24B <class>.W <hairstyle>.W <bottom-viewid>.W <mid-viewid>.W <up-viewid>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.28B* (ZC_EQUIPWIN_MICROSCOPE2, PACKETVER >= 20101124)
/// 0859 <packet len>.W <name>.24B <class>.W <hairstyle>.W <bottom-viewid>.W <mid-viewid>.W <up-viewid>.W <robe>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.28B* (ZC_EQUIPWIN_MICROSCOPE2, PACKETVER >= 20110111)
/// 0997 <packet len>.W <name>.24B <class>.W <hairstyle>.W <bottom-viewid>.W <mid-viewid>.W <up-viewid>.W <robe>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.31B* (ZC_EQUIPWIN_MICROSCOPE_V5)
/// 0a2d <packet len>.W <name>.24B <class>.W <hairstyle>.W <bottom-viewid>.W <mid-viewid>.W <up-viewid>.W <robe>.W <haircolor>.W <cloth-dye>.W <gender>.B {equip item}.57B* (ZC_EQUIPWIN_MICROSCOPE_V6, PACKETVER >= 20150226)
void clif_viewequip_ack(struct map_session_data* sd, struct map_session_data* tsd) {
	nullpo_retv(sd);
	nullpo_retv(tsd);

	struct packet_viewequip_ack packet;
	int equip = 0;

	for (int i = 0; i < EQI_MAX; i++) {
		int k = tsd->equip_index[i];

		if (k >= 0) {
			if (tsd->inventory.u.items_inventory[k].nameid == 0 || tsd->inventory_data[k] == NULL) { // Item doesn't exist
				continue;
			}

			if (!tsd->inventory.u.items_inventory[k].equip) {
				continue;
			}

			if (!itemdb_isequip2(tsd->inventory_data[k])) { // Is not equippable
				continue;
			}

			clif_item_equip(client_index(k), &packet.list[equip++], &tsd->inventory.u.items_inventory[k], tsd->inventory_data[k], pc_equippoint(tsd, k));
		}
		}

	packet.PacketType = viewequipackType;
	packet.PacketLength = (sizeof(packet) - sizeof(packet.list)) + (sizeof(struct EQUIPITEM_INFO) * equip);

	safestrncpy(packet.characterName, tsd->status.name, NAME_LENGTH);

	packet.job = tsd->status.class_;
	packet.head = tsd->vd.hair_style;
	packet.accessory = tsd->vd.head_bottom;
	packet.accessory2 = tsd->vd.head_mid;
	packet.accessory3 = tsd->vd.head_top;
#if PACKETVER >= 20110111
	packet.robe = tsd->vd.robe;
#endif
	packet.headpalette = tsd->vd.hair_color;
	packet.bodypalette = tsd->vd.cloth_color;
#if PACKETVER_MAIN_NUM >= 20180801 || PACKETVER_RE_NUM >= 20180801 || PACKETVER_ZERO_NUM >= 20180808
	packet.body2 = tsd->vd.body_style;
#endif
	packet.sex = tsd->vd.sex;

	clif_send(&packet, packet.PacketLength, &sd->bl, SELF);
}

/// Display msgstringtable.txt string (ZC_MSG).
/// 0291 <message>.W
void clif_msg(struct map_session_data* sd, unsigned short id)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x291));
	WFIFOW(fd, 0) = 0x291;
	WFIFOW(fd, 2) = id;  // zero-based msgstringtable.txt index
	WFIFOSET(fd, packet_len(0x291));
}

/// Display msgstringtable.txt string and fill in a valid for %d format (ZC_MSG_VALUE).
/// 0x7e2 <message>.W <value>.L
void clif_msg_value(struct map_session_data* sd, unsigned short id, int value)
{
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x7e2));
	WFIFOW(fd,0) = 0x7e2;
	WFIFOW(fd,2) = id;
	WFIFOL(fd,4) = value;
	WFIFOSET(fd, packet_len(0x7e2));
}


/// Displays msgstringtable.txt string, prefixed with a skill name. (ZC_MSG_SKILL).
/// 07e6 <skill id>.W <msg id>.L
///
/// NOTE: Message has following format and is printed in color 0xCDCDFF (purple):
///       "[SkillName] Message"
void clif_msg_skill(struct map_session_data* sd, unsigned short skill_id, int msg_id)
{
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x7e6));
	WFIFOW(fd,0) = 0x7e6;
	WFIFOW(fd,2) = skill_id;
	WFIFOL(fd,4) = msg_id;
	WFIFOSET(fd, packet_len(0x7e6));
}

/// Displays msgstringtable.txt string in a color. (ZC_MSG_COLOR).
/// 09cd <msg id>.W <color>.L
void clif_msg_color(struct map_session_data *sd, uint16 msg_id, uint32 color) {
	nullpo_retv(sd);

	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x9cd));
	WFIFOW(fd, 0) = 0x9cd;
	WFIFOW(fd, 2) = msg_id;
	WFIFOL(fd, 4) = RGB2BGR(color);

	WFIFOSET(fd, packet_len(0x9cd));
}

/// Validates one global/guild/party/whisper message packet and tries to recognize its components.
/// Returns true if the packet was parsed successfully.
/// Formats: 0 - <packet id>.w <packet len>.w (<name> : <message>).?B 00
///          1 - <packet id>.w <packet len>.w <name>.24B <message>.?B 00
static bool clif_process_message(struct map_session_data* sd, bool whisperFormat, char* out_name, char* out_message, char* out_full_message)
{
	const char* seperator = " : ";
	uint16 packetLength, inputLength;
	const char *input, *name, *message;
	size_t nameLength, messageLength;
	int fd = sd->fd;

	packetLength = RFIFOW(fd,2);
	// basic structure checks
	if(packetLength < 4 )
	{	// 4-byte header and at least an empty string is expected
		ShowWarning("clif_process_message: Received malformed packet from player '%s' (no message data)!\n", sd->status.name);
		return false;
	}

	input = (char*)RFIFOP(fd,4);
	inputLength = packetLength - 4;

	// process <name> part of the packet
	if (whisperFormat) {
		// name has fixed width
		if (inputLength < NAME_LENGTH + 1) {
			ShowWarning("clif_process_message: Received malformed packet from player '%s' (packet length is incorrect)!\n", sd->status.name);
			return false;
		}

		name = input;

		// validate name
		nameLength = strnlen(name, NAME_LENGTH - 1);

		// only restriction is that the name must be zero-terminated
		if (name[nameLength] != '\0') {
			ShowWarning("clif_process_message: Player '%s' sent an unterminated name!\n", sd->status.name);
			return false;
		}

		message = input + NAME_LENGTH;
		messageLength = inputLength - NAME_LENGTH;
	}
	else {
		// name and message are separated by ' : '
		size_t seperatorLength = strnlen(seperator, NAME_LENGTH);

		nameLength = strnlen(sd->status.name, NAME_LENGTH - 1); // name length (w/o zero byte)

		// check if there's enough data provided
		if (inputLength < nameLength + seperatorLength + 1) {
			ShowWarning("clif_process_message: Received malformed packet from player '%s' (no username data)!\n", sd->status.name);
			return false;
		}

		name = input;

		// validate name
		if (strncmp(name, sd->status.name, nameLength) || // the text must start with the speaker's name
			strncmp(name + nameLength, seperator, seperatorLength)) // followed by the seperator
		{
			//Hacked message, or infamous "client desynch" issue where they pick one char while loading another.
			ShowWarning("clif_process_message: Player '%s' sent a message using an incorrect name! Forcing a relog...\n", sd->status.name);
			set_eof(sd->fd); // Just kick them out to correct it.
			return false;
		}

		message = input + nameLength + seperatorLength;
		messageLength = inputLength - nameLength - seperatorLength;
	}

	// October 2015 and newer clients don't have the zero byte.
	// But trying to adjust the entire chat system to work without it causes too many problems.
	// Best to just add it onto the end of the incoming packet and adjust lengths in chat packets
	// as needed to prevent breaking anything.
	// Disabled this temp-fix for now for testing purposes. [15peaces]
	//if( message[messagelen-1] != '\0' )
	//	message[messagelen++] = '\0';
#if PACKETVER < 20151029

	if( messagelen != strnlen(message, messagelen)+1 )
	{	// the declared length must match real length
		ShowWarning("clif_process_message: Received malformed packet from player '%s' (length is incorrect)!\n", sd->status.name);
		return false;		
	}
	// verify <message> part of the packet
	if( message[messagelen-1] != '\0' )
	{	// message must be zero-terminated
		ShowWarning("clif_process_message: Player '%s' sent an unterminated message string!\n", sd->status.name);
		return false;		
	}
#else
	// No zero termination anymore
	messageLength += 1;
#endif

	if(messageLength > CHAT_SIZE_MAX-1 )
	{	// messages mustn't be too long
		// Normally you can only enter CHATBOX_SIZE-1 letters into the chat box, but Frost Joke / Dazzler's text can be longer.
		// Also, the physical size of strings that use multibyte encoding can go multiple times over the chatbox capacity.
		// Neither the official client nor server place any restriction on the length of the data in the packet,
		// but we'll only allow reasonably long strings here. This also makes sure that they fit into the `chatlog` table.
		ShowWarning("clif_process_message: Player '%s' sent a message too long ('%.*s')!\n", sd->status.name, CHAT_SIZE_MAX-1, message);
		return false;
	}

	// If it is not a whisper message, set the name to the fakename of the player
	if (whisperFormat == false && sd->fakename[0] != '\0') {
		strcpy(out_name, sd->fakename);
	}
	else {
		safestrncpy(out_name, name, nameLength + 1);
	}
	safestrncpy(out_message, message, messageLength);

	if (whisperFormat) {
		sprintf(out_full_message, "%-24s%s", out_name, out_message);
		out_full_message[nameLength] = '\0';
	}
	else {
		sprintf(out_full_message, "%s%s%s", out_name, seperator, out_message);
	}

	if (is_atcommand(fd, sd, out_message, 1))
		return false;

	if (battle_config.min_chat_delay)
	{	//[Skotlex]
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return false;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	if (sd->sc.data[SC_BERSERK] || sd->sc.data[SC_DEEPSLEEP] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT)
		|| (sd->state.block_action & PCBLOCK_CHAT))
		return false; //no "chatting" while muted.

	return true;
}

// ---------------------
// clif_guess_PacketVer
// ---------------------
// Parses a WantToConnection packet to try to identify which is the packet version used. [Skotlex]
// error codes:
// 0 - Success
// 1 - Unknown packet_ver
// 2 - Invalid account_id
// 3 - Invalid char_id
// 4 - Invalid login_id1 (reserved)
// 5 - Invalid client_tick (reserved)
// 6 - Invalid sex
// Only the first 'invalid' error that appears is used.
static int clif_guess_PacketVer(int fd, int get_previous, int *error)
{
	static int err = 1;
	static int packet_ver = -1;
	int cmd, packet_len, value; //Value is used to temporarily store account/char_id/sex

	if (get_previous)
	{//For quick reruns, since the normal code flow is to fetch this once to identify the packet version, then again in the wanttoconnect function. [Skotlex]
		if( error )
			*error = err;
		return packet_ver;
	}

	//By default, start searching on the default one.
	err = 1;
	packet_ver = clif_config.packet_db_ver;
	cmd = clif_parse_cmd(fd, NULL);
	packet_len = RFIFOREST(fd);

#ifdef LOG_ALL_PACKETS
	ShowDebug("clif_guess_PacketVer: packet_ver: %d cmd: 0x%04X len: %d\n", packet_ver, cmd, packet_len);
#endif

#define SET_ERROR(n) \
	if( err == 1 )\
		err = n;\
//define SET_ERROR

	// FIXME: If the packet is not received at once, this will FAIL.
	// Figure out, when it happens, that only part of the packet is
	// received, or fix the function to be able to deal with that
	// case.
#define CHECK_PACKET_VER() \
	if( cmd != clif_config.connect_cmd[packet_ver] || packet_len != packet_db[packet_ver][cmd].len )\
		;/* not wanttoconnection or wrong length */\
	else if( (value=(int)RFIFOL(fd, packet_db[packet_ver][cmd].pos[0])) < START_ACCOUNT_NUM || value > END_ACCOUNT_NUM )\
	{ SET_ERROR(2); }/* invalid account_id */\
	else if( (value=(int)RFIFOL(fd, packet_db[packet_ver][cmd].pos[1])) <= 0 )\
	{ SET_ERROR(3); }/* invalid char_id */\
	/*                   RFIFOL(fd, packet_db[packet_ver][cmd].pos[2]) - don't care about login_id1 */\
	/*                   RFIFOL(fd, packet_db[packet_ver][cmd].pos[3]) - don't care about client_tick */\
	else if( (value=(int)RFIFOB(fd, packet_db[packet_ver][cmd].pos[4])) != 0 && value != 1 )\
	{ SET_ERROR(6); }/* invalid sex */\
	else\
	{\
		err = 0;\
		if( error )\
			*error = 0;\
		return packet_ver;\
	}\
//define CHECK_PACKET_VER

	CHECK_PACKET_VER();//Default packet version found.
	
	for (packet_ver = MAX_PACKET_VER; packet_ver > 0; packet_ver--)
	{	//Start guessing the version, giving priority to the newer ones. [Skotlex]
		CHECK_PACKET_VER();
	}
	if( error )
		*error = err;
	packet_ver = -1;
	return -1;
#undef SET_ERROR
#undef CHECK_PACKET_VER
}

// ------------
// clif_parse_*
// ------------
// パケット読み取って色々操作

/// Refuse login.
/// 006a <error code>.B <block date>.20B (ZC_REFUSE_LOGIN)
/// 083e <error code>.L <block date>.20B (ZC_REFUSE_LOGIN2)
void clif_auth_error(int fd, int errorCode)
{
#if PACKETVER >= 20101123
	WFIFOHEAD(fd, 26);
	WFIFOW(fd, 0) = 0x83e;
	WFIFOL(fd, 2) = errorCode;
	memset(WFIFOP(fd, 6), '\0', 20);
	WFIFOSET(fd, 26);
#else
	WFIFOHEAD(fd, 23);
	WFIFOW(fd, 0) = 0x6a;
	WFIFOB(fd, 2) = (uint8)errorCode;
	memset(WFIFOP(fd, 3), '\0', 20);
	WFIFOSET(fd, 23);
#endif
}

/// Request to connect to map-server.
/// 0072 <account id>.L <char id>.L <auth code>.L <client time>.L <gender>.B (CZ_ENTER)
/// 0436 <account id>.L <char id>.L <auth code>.L <client time>.L <gender>.B (CZ_ENTER2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_WantToConnection(int fd, struct map_session_data* sd)
{
	struct block_list* bl;
	struct auth_node* node;
	int cmd, account_id, char_id;
	uint32 login_id1;
	uint8 sex;
	unsigned int client_tick; //The client tick is a tick, therefore it needs be unsigned. [Skotlex]
	int packet_ver;	// 5: old, 6: 7july04, 7: 13july04, 8: 26july04, 9: 9aug04/16aug04/17aug04, 10: 6sept04, 11: 21sept04, 12: 18oct04, 13: 25oct04 (by [Yor])

	if (sd) {
		ShowError("clif_parse_WantToConnection : invalid request (character already logged in)\n");
		return;
	}

	// Only valid packet version get here
	packet_ver = clif_guess_PacketVer(fd, 1, NULL);

	cmd = RFIFOW(fd,0);
	account_id  = RFIFOL(fd, packet_db[packet_ver][cmd].pos[0]);
	char_id     = RFIFOL(fd, packet_db[packet_ver][cmd].pos[1]);
	login_id1   = RFIFOL(fd, packet_db[packet_ver][cmd].pos[2]);
	client_tick = RFIFOL(fd, packet_db[packet_ver][cmd].pos[3]);
	sex         = RFIFOB(fd, packet_db[packet_ver][cmd].pos[4]);

	if( packet_ver < 5 || // reject really old client versions
		(packet_ver <= 9 && (battle_config.packet_ver_flag & 1) == 0) || // older than 6sept04
		(packet_ver > 9 && packet_ver < 40 && (battle_config.packet_ver_flag & 1 << (packet_ver - 9)) == 0) || // version not allowed
		(packet_ver >= 40 && packet_ver <= 45 && (battle_config.packet_ver_flag2 & 1 << (packet_ver - 40)) == 0))
	{// packet version rejected
		ShowInfo("Rejected connection attempt, forbidden packet version (AID/CID: '"CL_WHITE"%d/%d"CL_RESET"', Packet Ver: '"CL_WHITE"%d"CL_RESET"', IP: '"CL_WHITE"%s"CL_RESET"').\n", account_id, char_id, packet_ver, ip2str(session[fd]->client_addr, NULL));
		clif_auth_error(fd, 5); // Your Game's EXE file is not the latest version
		set_eof(fd);
		return;
	}

	if( runflag != SERVER_STATE_RUN )
	{// not allowed
		clif_authfail_fd(fd,1);// server closed
		return;
	}

	//Check for double login.
	bl = map_id2bl(account_id);
	if(bl && bl->type != BL_PC) {
		ShowError("clif_parse_WantToConnection: a non-player object already has id %d, please increase the starting account number\n", account_id);
		clif_auth_error(fd, 3);  // Rejected by server
		set_eof(fd);
		return;
	}

	if (bl || 
		((node=chrif_search(account_id)) && //An already existing node is valid only if it is for this login.
			!(node->account_id == account_id && node->char_id == char_id && node->state == ST_LOGIN)))
	{
		clif_authfail_fd(fd, 8); //Still recognizes last connection
		return;
	}

	CREATE(sd, TBL_PC, 1);
	sd->fd = fd;
	sd->packet_ver = packet_ver;
#ifdef PACKET_OBFUSCATION
	sd->cryptKey = (((((clif_cryptKey[0] * clif_cryptKey[1]) + clif_cryptKey[2]) & 0xFFFFFFFF) * clif_cryptKey[1]) + clif_cryptKey[2]) & 0xFFFFFFFF;
#endif
	session[fd]->session_data = sd;

	pc_setnewpc(sd, account_id, char_id, login_id1, client_tick, sex, fd);

#if PACKETVER < 20070521
	WFIFOHEAD(fd,4);
	WFIFOL(fd,0) = sd->bl.id;
	WFIFOSET(fd,4);
#else
	WFIFOHEAD(fd,packet_len(0x283));
	WFIFOW(fd,0) = 0x283;
	WFIFOL(fd,2) = sd->bl.id;
	WFIFOSET(fd,packet_len(0x283));
#endif

	chrif_authreq(sd);
}


/// Notification from the client, that it has finished map loading and is about to display player's character (CZ_NOTIFY_ACTORINIT).
/// 007d
void clif_parse_LoadEndAck(int fd,struct map_session_data *sd)
{
	bool guild_notice = false;

	if(sd->bl.prev != NULL)
		return;
	
	if (!sd->state.active && !sd->state.pc_loaded)
	{	//Character loading is not complete yet!
		//Let pc_reg_received or intif_parse_StorageReceived reinvoke this when ready.
		sd->state.connect_new = 0;
		return;
	}

	if (sd->state.rewarp)
	{	//Rewarp player.
		sd->state.rewarp = 0;
		clif_changemap(sd, sd->bl.m, sd->bl.x, sd->bl.y);
		return;
	}

	sd->state.warping = 0;

	// look
#if PACKETVER < 4
	clif_changelook(&sd->bl,LOOK_WEAPON,sd->status.weapon);
	clif_changelook(&sd->bl,LOOK_SHIELD,sd->status.shield);
#else
	clif_changelook(&sd->bl,LOOK_WEAPON,0);
#endif

	pc_set_costume_view(sd);

	if(sd->vd.cloth_color)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_CLOTHES_COLOR,sd->vd.cloth_color,SELF);
	if(sd->vd.body_style)
		clif_refreshlook(&sd->bl,sd->bl.id,LOOK_BODY2,sd->vd.body_style,SELF);

	// item
	clif_inventorylist(sd);  // inventory list first, otherwise deleted items in pc_checkitem show up as 'unknown item'
	pc_checkitem(sd);
	clif_equipswitch_list(sd);

	// cart
	if(pc_iscarton(sd)) {
		clif_cartlist(sd);
		clif_updatestatus(sd,SP_CARTINFO);
	}

	// weight
	clif_updatestatus(sd,SP_WEIGHT);
	clif_updatestatus(sd,SP_MAXWEIGHT);

	// guild
	// (needs to go before clif_spawn() to show guild emblems correctly)
	if(sd->status.guild_id)
		guild_send_memberinfoshort(sd,1);

	if(battle_config.pc_invincible_time > 0) {
		if(map_flag_gvg(sd->bl.m))
			pc_setinvincibletimer(sd,battle_config.pc_invincible_time<<1);
		else
			pc_setinvincibletimer(sd,battle_config.pc_invincible_time);
	}

	if( map[sd->bl.m].users++ == 0 && battle_config.dynamic_mobs )
		map_spawnmobs(sd->bl.m);
	if (!(sd->sc.option&OPTION_INVISIBLE))
	{// increment the number of pvp players on the map
		map[sd->bl.m].users_pvp++;
	}
	if( map[sd->bl.m].instance_id )
	{
		instance[map[sd->bl.m].instance_id].users++;
		instance_check_idle(map[sd->bl.m].instance_id);
	}
	sd->state.debug_remove_map = 0; // temporary state to track double remove_map's [FlavioJS]

	if (map_addblock(&sd->bl))
		return;

	clif_spawn(&sd->bl);

	// Party
	// (needs to go after clif_spawn() to show hp bars correctly)
	if(sd->status.party_id) {
		party_send_movemap(sd);
		clif_party_hp(sd); // Show hp after displacement [LuzZza]
	}

	if( sd->bg_id ) clif_bg_hp(sd); // BattleGround System

	if(map[sd->bl.m].flag.pvp && !(sd->sc.option&OPTION_INVISIBLE)) {
		if(!battle_config.pk_mode) { // remove pvp stuff for pk_mode [Valaris]
			if (!map[sd->bl.m].flag.pvp_nocalcrank)
				sd->pvp_timer = add_timer(gettick()+200, pc_calc_pvprank_timer, sd->bl.id, 0);
			sd->pvp_rank = 0;
			sd->pvp_lastusers = 0;
			sd->pvp_point = 5;
			sd->pvp_won = 0;
			sd->pvp_lost = 0;
		}
		clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF);
	} else if(sd->duel_group) // set flag, if it's a duel [LuzZza]
		clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF);
	else if (map[sd->bl.m].flag.gvg_dungeon)
		clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF); //TODO: Figure out the real packet to send here.
	else if( map_flag_gvg(sd->bl.m) )
		clif_map_property(&sd->bl, MAPPROPERTY_AGITZONE, SELF);
	else
		clif_map_property(&sd->bl, MAPPROPERTY_NOTHING, SELF);

	// info about nearby objects
	// must use foreachinarea (CIRCULAR_AREA interferes with foreachinrange)
	map_foreachinarea(clif_getareachar, sd->bl.m, sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE, sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE, BL_ALL, sd);

	// pet
	if( sd->pd )
	{
		if( battle_config.pet_no_gvg && map_flag_gvg(sd->bl.m) )
		{	//Return the pet to egg. [Skotlex]
			clif_displaymessage(sd->fd, "Pets are not allowed in Guild Wars.");
			pet_return_egg(sd, sd->pd);
		}
		else
		{
			if (map_addblock(&sd->pd->bl))
				return;

			clif_spawn(&sd->pd->bl);
			clif_send_petdata(sd,sd->pd,0,0);
			clif_send_petstatus(sd);
//			skill_unit_move(&sd->pd->bl,gettick(),1);
		}
	}

	//homunculus [blackhole89]
	if( hom_is_active(sd->hd) )
	{
		if (map_addblock(&sd->hd->bl))
			return;

		clif_spawn(&sd->hd->bl);
		clif_send_homdata(sd,SP_ACK,0);
		clif_hominfo(sd,sd->hd,1);
		clif_hominfo(sd,sd->hd,0); //for some reason, at least older clients want this sent twice
		clif_homskillinfoblock(sd);
		if( battle_config.hom_setting&0x8 )
			status_calc_bl(&sd->hd->bl, SCB_SPEED); //Homunc mimic their master's speed on each map change
		if( !(battle_config.hom_setting&0x2) )
			skill_unit_move(&sd->hd->bl,gettick(),1); // apply land skills immediately
	}

	if( sd->md ) {
		if (map_addblock(&sd->md->bl))
			return;

		clif_spawn(&sd->md->bl);
		clif_mercenary_info(sd);
		clif_mercenary_skillblock(sd);
		status_calc_bl(&sd->md->bl, SCB_SPEED); //Mercenary mimic their master's speed on each map change
	}

	if( sd->ed ) {
		if (map_addblock(&sd->ed->bl))
			return;

		clif_spawn(&sd->ed->bl);
		clif_elemental_info(sd);
		if (battle_config.elemental_masters_walk_speed)
			status_calc_bl(&sd->ed->bl, SCB_SPEED);
	}

	if(sd->state.connect_new) {
		int lv;
		guild_notice = true;
		clif_skillupdateinfoblock(sd);
		clif_hotkeys_send(sd);
		clif_updatestatus(sd,SP_BASEEXP);
		clif_updatestatus(sd,SP_NEXTBASEEXP);
		clif_updatestatus(sd,SP_JOBEXP);
		clif_updatestatus(sd,SP_NEXTJOBEXP);
		clif_updatestatus(sd,SP_SKILLPOINT);
		clif_initialstatus(sd);

		if (sd->sc.option&OPTION_FALCON)
			clif_status_load(&sd->bl, SI_FALCON, 1);

		else if (sd->sc.option&OPTION_RIDING || sd->sc.option&OPTION_DRAGON)
			clif_status_load(&sd->bl, SI_RIDING, 1);

		else if (sd->sc.option&OPTION_WUGRIDER)
			clif_status_load(&sd->bl, SI_WUGRIDER, 1);

		else if (&sd->sc && sd->sc.data[SC_ALL_RIDING])
			clif_status_load(&sd->bl, SI_ALL_RIDING, 1);

		if(sd->status.manner < 0)
			sc_start(&sd->bl,SC_NOCHAT,100,0,0);

		//Auron reported that This skill only triggers when you logon on the map o.O [Skotlex]
		if ((lv = pc_checkskill(sd,SG_KNOWLEDGE)) > 0) {
			if(sd->bl.m == sd->feel_map[0].m
				|| sd->bl.m == sd->feel_map[1].m
				|| sd->bl.m == sd->feel_map[2].m)
				sc_start(&sd->bl, SC_KNOWLEDGE, 100, lv, skill_get_time(SG_KNOWLEDGE, lv));
		}

		if(sd->pd && sd->pd->pet.intimate > 900)
			clif_pet_emotion(sd->pd,(sd->pd->pet.class_ - 100)*100 + 50 + pet_hungry_val(sd->pd));

		if(hom_is_active(sd->hd))
			hom_init_timers(sd->hd);

		if (night_flag && map[sd->bl.m].flag.nightenabled)
		{
			sd->state.night = 1;
			clif_status_load(&sd->bl, SI_NIGHT, 1);
		}

		// Notify everyone that this char logged in [Skotlex].
		map_foreachpc(clif_friendslist_toggle_sub, sd->status.account_id, sd->status.char_id, 1);

		// Set the initial idle time
		sd->idletime = last_tick;

#if PACKETVER >= 20180307
		if (battle_config.feature_attendance_system != 0 && battle_config.attendance_show_window != 0 && clif_attendance_timediff(sd) == true)
			clif_open_ui_req_sub(sd->fd, sd, IN_UI_ATTENDANCE);
#endif

		//Login Event
		npc_script_event(sd, NPCE_LOGIN);
	} else {
		//For some reason the client "loses" these on warp/map-change.
		clif_updatestatus(sd,SP_STR);
		clif_updatestatus(sd,SP_AGI);
		clif_updatestatus(sd,SP_VIT);
		clif_updatestatus(sd,SP_INT);
		clif_updatestatus(sd,SP_DEX);
		clif_updatestatus(sd,SP_LUK);
	
		// abort currently running script
		sd->state.using_fake_npc = 0;
		sd->state.menu_or_input = 0;
		sd->npc_menu = 0;

		if(sd->npc_id)
			npc_event_dequeue(sd);
	}

	if( sd->state.changemap )
	{// restore information that gets lost on map-change
#if PACKETVER >= 20070918
		clif_partyinvitationstate(sd, sd->status.allow_party);
		clif_equipcheckbox(sd);
#endif

#if PACKETVER >= 20141008
		if (battle_config.pet_autofeed_always) {
			// Always send ON or OFF
			if (sd->pd && battle_config.pet_autofeed) {
				clif_zc_config(sd, CZ_CONFIG_PET_AUTOFEEDING, sd->pd->pet.autofeed);
			}
			else {
				clif_zc_config(sd, CZ_CONFIG_PET_AUTOFEEDING, false);
			}
		}
		else {
			// Only send when enabled
			if (sd->pd && battle_config.pet_autofeed && sd->pd->pet.autofeed) {
				clif_zc_config(sd, CZ_CONFIG_PET_AUTOFEEDING, true);
			}
		}
#endif

		if (sd->status.guild_id && battle_config.guild_notice_changemap == 1){
			clif_guild_notice(sd);
			guild_notice = false; // Do not display it twice
		}

		if( (battle_config.bg_flee_penalty != 100 || battle_config.gvg_flee_penalty != 100) &&
			(map_flag_gvg(sd->state.pmap) || map_flag_gvg(sd->bl.m) || map[sd->state.pmap].flag.battleground || map[sd->bl.m].flag.battleground) )
			status_calc_bl(&sd->bl, SCB_FLEE); //Refresh flee penalty

		if( night_flag && map[sd->bl.m].flag.nightenabled )
		{	//Display night.
			if( !sd->state.night )
			{
				sd->state.night = 1;
				clif_status_load(&sd->bl, SI_NIGHT, 1);
			}
		}
		else if( sd->state.night )
		{ //Clear night display.
			sd->state.night = 0;
			clif_status_load(&sd->bl, SI_NIGHT, 0);
		}

		if( map[sd->bl.m].flag.battleground )
		{
			clif_map_type(sd, MAPTYPE_BATTLEFIELD); // Battleground Mode
			if( map[sd->bl.m].flag.battleground == 2 )
				clif_bg_updatescore_single(sd);
		}

		if( map[sd->bl.m].flag.allowks && !map_flag_ks(sd->bl.m) )
		{
			char output[128];
			sprintf(output, "[ Kill Steal Protection Disable. KS is allowed in this map ]");
			clif_broadcast(&sd->bl, output, strlen(output) + 1, BC_BLUE, SELF);
		}

		if ((pc_isGM(sd)) >= battle_config.disp_hpmeter) {
			map[sd->bl.m].hpmeter_visible++;
			sd->state.hpmeter_visible = 1;
		}

		status_change_clear_onChangeMap(&sd->bl, &sd->sc);
		map_iwall_get(sd); // Updates Walls Info on this Map to Client
		status_calc_pc(sd, SCO_NONE); // Some conditions are map-dependent so we must recalculate
	}

	if (sd->status.guild_id && (battle_config.guild_notice_changemap == 2 || guild_notice))
		clif_guild_notice(sd); // Displays at end
	
#ifndef TXT_ONLY
	mail_clear(sd);
#endif

	/* Guild Aura Init */
	if (sd->guild && sd->state.gmaster_flag) {
		guild_guildaura_refresh(sd, GD_LEADERSHIP, guild_checkskill(sd->guild, GD_LEADERSHIP));
		guild_guildaura_refresh(sd, GD_GLORYWOUNDS, guild_checkskill(sd->guild, GD_GLORYWOUNDS));
		guild_guildaura_refresh(sd, GD_SOULCOLD, guild_checkskill(sd->guild, GD_SOULCOLD));
		guild_guildaura_refresh(sd, GD_HAWKEYES, guild_checkskill(sd->guild, GD_HAWKEYES));
	}

	if (sd->state.vending) { /* show we have a vending */
		clif_openvending(sd, sd->bl.id, sd->vending);
		clif_showvendingboard(&sd->bl, sd->message, 0);
	}

	if(map[sd->bl.m].flag.loadevent) // Lance
		npc_script_event(sd, NPCE_LOADMAP);

	if (pc_checkskill(sd, SG_DEVIL) && ((sd->class_&MAPID_THIRDMASK) == MAPID_STAR_EMPEROR || sd->status.job_level >= 50))
		clif_status_load(&sd->bl, SI_DEVIL, 1);  //blindness [Komurka]

	if (sd->sc.opt2) //Client loses these on warp.
		clif_changeoption(&sd->bl);

	if ((sd->sc.data[SC_MONSTER_TRANSFORM] || sd->sc.data[SC_ACTIVE_MONSTER_TRANSFORM]) && battle_config.gvg_mon_trans_disable && map_flag_gvg2(sd->bl.m))
	{
		status_change_end(&sd->bl, SC_MONSTER_TRANSFORM, INVALID_TIMER);
		status_change_end(&sd->bl, SC_ACTIVE_MONSTER_TRANSFORM, INVALID_TIMER);
		clif_displaymessage(sd->fd, msg_txt(sd,739)); // Transforming into monster is not allowed in Guild Wars.
	}

	clif_weather_check(sd);
	
	// For automatic triggering of NPCs after map loading (so you don't need to walk 1 step first)
	if (map_getcell(sd->bl.m,sd->bl.x,sd->bl.y,CELL_CHKNPC))
		npc_touch_areanpc(sd,sd->bl.m,sd->bl.x,sd->bl.y);
	else
		sd->areanpc_id = 0;

	// it broke at some point (e.g. during a crash), so we make it visibly dead again.
	if( !sd->status.hp && !pc_isdead(sd) && status_isdead(&sd->bl) )
		pc_setdead(sd);

	// If player is dead, and is spawned (such as @refresh) send death packet. [Valaris]
	if(pc_isdead(sd))
		clif_clearunit_area(&sd->bl, CLR_DEAD);
	else {
		skill_usave_trigger(sd);
		if (battle_config.spawn_direction)
			clif_changed_dir(&sd->bl, SELF);
	}

//	Trigger skill effects if you appear standing on them
	if(!battle_config.pc_invincible_time)
		skill_unit_move(&sd->bl,gettick(),1);

// Show Questinfo-Icons
	pc_show_questinfo_reinit(sd);
	pc_show_questinfo(sd);

#if PACKETVER >= 20150513
	if( sd->mail.inbox.unread ){
		clif_Mail_new(sd, 0, NULL, NULL);
	}
#endif
	sd->state.connect_new = 0;
	sd->state.changemap = false;
}


/// Server's tick (ZC_NOTIFY_TIME).
/// 007f <time>.L
void clif_notify_time(struct map_session_data* sd, int64 time)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x7f));
	WFIFOW(fd,0) = 0x7f;
	WFIFOL(fd,2) = (uint32)time;
	WFIFOSET(fd,packet_len(0x7f));
}


/// Request for server's tick.
/// 007e <client tick>.L (CZ_REQUEST_TIME)
/// 0360 <client tick>.L (CZ_REQUEST_TIME2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_TickSend(int fd, struct map_session_data *sd)
{
	sd->client_tick = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	clif_notify_time(sd, gettick());
}


/// Sends hotkey bar.
/// 02b9 { <is skill>.B <id>.L <count>.W }*27 (ZC_SHORTCUT_KEY_LIST)
/// 07d9 { <is skill>.B <id>.L <count>.W }*36 (ZC_SHORTCUT_KEY_LIST_V2, PACKETVER >= 20090603)
/// 07d9 { <is skill>.B <id>.L <count>.W }*38 (ZC_SHORTCUT_KEY_LIST_V2, PACKETVER >= 20090617)
/// 0a00 <rotate>.B { <is skill>.B <id>.L <count>.W }*38 (ZC_SHORTCUT_KEY_LIST_V3, PACKETVER >= 20141022) 
void clif_hotkeys_send(struct map_session_data *sd) {
#ifdef HOTKEY_SAVING
	const int fd = sd->fd;
	int i;
	int offset = 2;
#if PACKETVER < 20090603
	const int cmd = 0x2b9;
#elif PACKETVER < 20141022
	const int cmd = 0x7d9;
#else
	char Rotate = sd->status.hotkey_rowshift;
	const int cmd = 0xa00;
	offset = 3;
#endif
	if (!fd) return;
	WFIFOHEAD(fd, offset + MAX_HOTKEYS * 7);
	WFIFOW(fd, 0) = cmd;
#if PACKETVER >= 20141022
	WFIFOB(fd, 2) = Rotate;
#endif
	for(i = 0; i < MAX_HOTKEYS; i++) {
		WFIFOB(fd, offset + 0 + i * 7) = sd->status.hotkeys[i].type; // type: 0: item, 1: skill
		WFIFOL(fd, offset + 1 + i * 7) = sd->status.hotkeys[i].id; // item or skill ID
		WFIFOW(fd, offset + 5 + i * 7) = sd->status.hotkeys[i].lv; // item qty or skill level
	}
	WFIFOSET(fd, packet_len(cmd));
#endif
}

/// Request to update a position on the hotkey row bar
void clif_parse_HotkeyRowShift(int fd, struct map_session_data *sd) {
	int cmd = RFIFOW(fd, 0);
	sd->status.hotkey_rowshift = RFIFOB(fd, packet_db[sd->packet_ver][cmd].pos[0]);
}

/// Request to update a position on the hotkey bar (CZ_SHORTCUT_KEY_CHANGE).
/// 02ba <index>.W <is skill>.B <id>.L <count>.W
void clif_parse_Hotkey(int fd, struct map_session_data *sd) {
#ifdef HOTKEY_SAVING
	unsigned short idx;
	int cmd;

	cmd = RFIFOW(fd, 0);
	idx = RFIFOW(fd, packet_db[sd->packet_ver][cmd].pos[0]);
	if (idx >= MAX_HOTKEYS) return;

	sd->status.hotkeys[idx].type = RFIFOB(fd, packet_db[sd->packet_ver][cmd].pos[1]);
	sd->status.hotkeys[idx].id = RFIFOL(fd, packet_db[sd->packet_ver][cmd].pos[2]);
	sd->status.hotkeys[idx].lv = RFIFOW(fd, packet_db[sd->packet_ver][cmd].pos[3]);
#endif
}


/// Displays cast-like progress bar (ZC_PROGRESS).
/// 02f0 <color>.L <time>.L
void clif_progressbar(struct map_session_data * sd, unsigned long color, unsigned int second)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x2f0));
	WFIFOW(fd,0) = 0x2f0;
	WFIFOL(fd,2) = color;
	WFIFOL(fd,6) = second;
	WFIFOSET(fd,packet_len(0x2f0));
}


/// Removes an ongoing progress bar (ZC_PROGRESS_CANCEL).
/// 02f2
void clif_progressbar_abort(struct map_session_data * sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x2f2));
	WFIFOW(fd,0) = 0x2f2;
	WFIFOSET(fd,packet_len(0x2f2));
}


/// Notification from the client, that the progress bar has reached 100%.
/// 02f1 (CZ_PROGRESS)
void clif_parse_progressbar(int fd, struct map_session_data * sd) {
	// No progressbar active, ignore it
	if (!sd->progressbar.npc_id) {
		return;
	}

	int npc_id = sd->progressbar.npc_id;
	bool closing = false;

	// Check if the progress was canceled
	if (gettick() < sd->progressbar.timeout && sd->st) {
		closing = true;
		sd->st->state = CLOSE; // will result in END in npc_scriptcont

		// If a message window was open, offer a close button to the user
		if (sd->st->mes_active) {
			clif_scriptclose(sd, npc_id);
		}
	}

	sd->progressbar.timeout = sd->state.workinprogress = sd->progressbar.npc_id = 0;
	npc_scriptcont(sd, npc_id, closing);
}


/// Request to walk to a certain position on the current map.
/// 0085 <dest>.3B (CZ_REQUEST_MOVE)
/// 035f <dest>.3B (CZ_REQUEST_MOVE2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_WalkToXY(int fd, struct map_session_data *sd) {
	short x, y;

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, CLR_DEAD);
		return;
	}

	if (sd->sc.opt1 > 0 && sd->sc.opt1 != OPT1_STONEWAIT && sd->sc.opt1 != OPT1_BURNING)
		; //You CAN walk on this OPT1 value.
	else if(sd->progressbar.npc_id) {
		clif_progressbar_abort(sd);
		return; // First walk attempt cancels the progress bar
	} else if (pc_cant_act(sd))
		return;

	if(sd->sc.data[SC_RUN] || sd->sc.data[SC_WUGDASH] )
		return;


	RFIFOPOS(fd, packet_db[sd->packet_ver][RFIFOW(fd, 0)].pos[0], &x, &y, NULL);
	//A move command one cell west is only valid if the target cell is free
	if (battle_config.official_cell_stack_limit > 0
		&& sd->bl.x == x + 1 && sd->bl.y == y && map_count_oncell(sd->bl.m, x, y, BL_CHAR | BL_NPC, 1))
		return;

	// Cloaking wall check is actually updated when you click to process next movement
	// not when you move each cell.  This is official behaviour.
	if (sd->sc.data[SC_CLOAKING])
		skill_check_cloaking(&sd->bl, sd->sc.data[SC_CLOAKING]);

	pc_delinvincibletimer(sd);

	//Set last idle time... [Skotlex]
	sd->idletime = last_tick;
	
	unit_walktoxy(&sd->bl, x, y, 4);
}


/// Notification about the result of a disconnect request (ZC_ACK_REQ_DISCONNECT).
/// 018b <result>.W
/// result:
///     0 = disconnect (quit)
///     1 = cannot disconnect (wait 10 seconds)
///     ? = ignored
void clif_disconnect_ack(struct map_session_data* sd, short result)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x18b));
	WFIFOW(fd,0) = 0x18b;
	WFIFOW(fd,2) = result;
	WFIFOSET(fd,packet_len(0x18b));
}


/// Request to disconnect from server (CZ_REQ_DISCONNECT).
/// 018a <type>.W
/// type:
///     0 = quit
void clif_parse_QuitGame(int fd, struct map_session_data *sd)
{
	/*	Rovert's prevent logout option fixed [Valaris]	*/
	if( !sd->sc.data[SC_CLOAKING] && !sd->sc.data[SC_HIDING] && 
		!sd->sc.data[SC_CHASEWALK] && !sd->sc.data[SC_CLOAKINGEXCEED] &&
		!sd->sc.data[SC__INVISIBILITY] && !sd->sc.data[SC_NEWMOON] && !sd->sc.data[SC_SUHIDE] &&
		(!battle_config.prevent_logout || DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout) )
	{
		pc_damage_log_clear(sd, 0);
		clif_disconnect_ack(sd, 0);
		flush_fifo(fd);
		if (battle_config.drop_connection_on_quit) {
			set_eof(fd);
		}
	} else {
		clif_disconnect_ack(sd, 1);
	}
}


/// Requesting unit's name.
/// 0094 <id>.L (CZ_REQNAME)
/// 0368 <id>.L (CZ_REQNAME2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_GetCharNameRequest(int fd, struct map_session_data *sd)
{
	int id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	struct block_list* bl;
	//struct status_change *sc;
	
	if( id < 0 && -id == sd->bl.id ) // for disguises [Valaris]
		id = sd->bl.id;

	bl = map_id2bl(id);
	if( bl == NULL )
		return;	// Lagged clients could request names of already gone mobs/players. [Skotlex]

	if( sd->bl.m != bl->m || !check_distance_bl(&sd->bl, bl, AREA_SIZE) )
		return; // Block namerequests past view range

	// 'see people in GM hide' cheat detection
	/* disabled due to false positives (network lag + request name of char that's about to hide = race condition)
	sc = status_get_sc(bl);
	if (sc && sc->option&OPTION_INVISIBLE && !disguised(bl) &&
		bl->type != BL_NPC && //Skip hidden NPCs which can be seen using Maya Purple
		pc_isGM(sd) < battle_config.hack_info_GM_level
	) {
		char gm_msg[256];
		sprintf(gm_msg, "Hack on NameRequest: character '%s' (account: %d) requested the name of an invisible target (id: %d).\n", sd->status.name, sd->status.account_id, id);
		ShowWarning(gm_msg);
		// information is sent to all online GMs
		intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, gm_msg);
		return;
	}
	*/

	clif_name(&sd->bl, bl, SELF);
}


/// Validates and processes global messages
/// 008c <packet len>.W <text>.?B (<name> : <message>) 00 (CZ_REQUEST_CHAT)
/// There are various variants of this packet.
void clif_parse_GlobalMessage(int fd, struct map_session_data* sd)
{
	char name[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	// validate packet and retrieve name and message
	if (!clif_process_message(sd, false, name, message, output))
		return;

	// send message to others
	clif_notify_chat(&sd->bl, output, sd->chatID ? CHAT_WOS : AREA_CHAT_WOC);

	// send back message to the speaker
	clif_notify_playerchat(sd, output);

#ifdef PCRE_SUPPORT
	// trigger listening npcs
	map_foreachinrange(npc_chat_sub, &sd->bl, AREA_SIZE, BL_NPC, output, strlen(output), &sd->bl);
#endif

	// Reset idle time when using normal chat.
	sd->idletime = last_tick;

	// Chat logging type 'O' / Global Chat
	log_chat(LOG_CHAT_GLOBAL, 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, NULL, message);
	//achievement_update_objective(sd, AG_CHAT, 1, sd->bl.m); //! TODO: What's the official use of this achievement type?
}


/// /mm /mapmove (as @rura GM command) (CZ_MOVETO_MAP).
/// Request to warp to a map on given coordinates.
/// 0140 <map name>.16B <x>.W <y>.W
void clif_parse_MapMove(int fd, struct map_session_data *sd)
{
	char output[MAP_NAME_LENGTH_EXT+15]; // Max length of a short: ' -6XXXX' -> 7 digits
	char message[MAP_NAME_LENGTH_EXT+15+5]; // "/mm "+output
	char* map_name;

	if (battle_config.atc_gmonly && !pc_isGM(sd))
		return;
	if(pc_isGM(sd) < get_atcommand_level(atcommand_mapmove))
		return;

	map_name = (char*)RFIFOP(fd,2);
	map_name[MAP_NAME_LENGTH_EXT-1]='\0';
	sprintf(output, "%s %d %d", map_name, RFIFOW(fd,18), RFIFOW(fd,20));
	atcommand_mapmove(fd, sd, "@mapmove", output);
	sprintf(message, "/mm %s", output);
	log_atcommand(sd, get_atcommand_level(atcommand_mapmove), message);
}


/// Updates body and head direction of an object (ZC_CHANGE_DIRECTION).
/// 009c <id>.L <head dir>.W <dir>.B
/// head dir:
///     0 = straight
///     1 = turned CW
///     2 = turned CCW
/// dir:
///     0 = north
///     1 = northwest
///     2 = west
///     3 = southwest
///     4 = south
///     5 = southeast
///     6 = east
///     7 = northeast
void clif_changed_dir(struct block_list *bl, enum send_target target)
{
	unsigned char buf[64];

	WBUFW(buf,0) = 0x9c;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = bl->type==BL_PC?((TBL_PC*)bl)->head_dir:0;
	WBUFB(buf,8) = unit_getdir(bl);

	clif_send(buf, packet_len(0x9c), bl, target);

	if (disguised(bl)) {
		WBUFL(buf,2) = -bl->id;
		WBUFW(buf,6) = 0;
		clif_send(buf, packet_len(0x9c), bl, SELF);
	}
}


/// Request to change own body and head direction.
/// 009b <head dir>.W <dir>.B (CZ_CHANGE_DIRECTION)
/// 0361 <head dir>.W <dir>.B (CZ_CHANGE_DIRECTION2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_ChangeDir(int fd, struct map_session_data *sd)
{
	unsigned char headdir, dir;

	headdir = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	dir = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	pc_setdir(sd, dir, headdir);

	clif_changed_dir(&sd->bl, AREA_WOS);
}


/// Request to show an emotion (CZ_REQ_EMOTION).
/// 00bf <type>.B
/// type:
///     @see enum emotion_type
void clif_parse_Emotion(int fd, struct map_session_data *sd)
{
	int emoticon = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	if (battle_config.basic_skill_check == 0 || pc_checkskill(sd, NV_BASIC) >= 2 || pc_checkskill(sd, SU_BASIC_SKILL) >= 1) {
		if (emoticon == E_MUTE) {// prevent use of the mute emote [Valaris]
			clif_skill_fail(sd, 1, USESKILL_FAIL_LEVEL, 1,0);
			return;
		}
		// fix flood of emotion icon (ro-proxy): flood only the hacker player
		if (sd->emotionlasttime + 1 >= time(NULL)) { // not more than 1 per second
			sd->emotionlasttime = time(NULL);
			clif_skill_fail(sd, 1, USESKILL_FAIL_LEVEL, 1,0);
			return;
		}
		sd->emotionlasttime = time(NULL);

		if (sd->state.block_action & PCBLOCK_EMOTION) {
			clif_skill_fail(sd, 1, USESKILL_FAIL_LEVEL, 1, 0);
			return;
		}

		if(battle_config.client_reshuffle_dice && emoticon>=E_DICE1 && emoticon<=E_DICE6)
		{// re-roll dice
			emoticon = rnd()%6+E_DICE1;
		}

		clif_emotion(&sd->bl, emoticon);
	} else
		clif_skill_fail(sd, 1, USESKILL_FAIL_LEVEL, 1, 0);
}


/// Amount of currently online players, reply to /w /who (ZC_USER_COUNT).
/// 00c2 <count>.L
void clif_user_count(struct map_session_data* sd, int count)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xc2));
	WFIFOW(fd,0) = 0xc2;
	WFIFOL(fd,2) = count;
	WFIFOSET(fd,packet_len(0xc2));
}


/// /w /who (CZ_REQ_USER_COUNT).
/// Request to display amount of currently connected players.
/// 00c1
void clif_parse_HowManyConnections(int fd, struct map_session_data *sd)
{
	clif_user_count(sd, map_getusers());
}


void clif_parse_ActionRequest_sub(struct map_session_data *sd, int action_type, int target_id, int64 tick)
{
	struct status_change *tsc = status_get_sc(map_id2bl(target_id));

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, CLR_DEAD);
		return;
	}

	if (sd->sc.count &&
		(sd->sc.data[SC_TRICKDEAD] ||
		(sd->sc.data[SC_AUTOCOUNTER] && action_type != 0x07) ||
		sd->sc.data[SC_BLADESTOP] ||
		sd->sc.data[SC_DEEPSLEEP] ||
		sd->sc.data[SC_CRYSTALIZE] ||
		sd->sc.data[SC_DEATHBOUND] ||
		sd->sc.data[SC__MANHOLE] ||
		sd->sc.data[SC_FALLENEMPIRE] ||
		sd->sc.data[SC_GRAVITYCONTROL] ||
		sd->sc.data[SC_NEWMOON] ||
		sd->sc.data[SC_SUHIDE]
	))
		return;

	// Statuses that don't let the player sit / stand / talk with NPCs (targeted)
	if (action_type != 0x00 && action_type != 0x07)
		pc_stop_walking(sd, 1);

	pc_stop_attack(sd);

	if(target_id<0 && -target_id == sd->bl.id) // for disguises [Valaris]
		target_id = sd->bl.id;

	switch(action_type)
	{
	case 0x00: // once attack
	case 0x07: // continuous attack

		if (pc_cant_act(sd) || sd->sc.option&OPTION_HIDE || sd->sc.option&OPTION_WUGRIDER)
			return;

		if( sd->sc.option&OPTION_COSTUME)
			return;

		// Can't attack
		if (sd->sc.data[SC_HEAT_BARREL_AFTER] ||
			sd->sc.data[SC_KINGS_GRACE] ||
			(sd->sc.data[SC_SIREN] && sd->sc.data[SC_SIREN]->val2 == target_id) ||
			(sd->sc.data[SC_GRAVITATION] && sd->sc.data[SC_GRAVITATION]->val3 == BCT_SELF) ||
			sd->sc.data[SC_ALL_RIDING])
			return;

		if ( tsc && (tsc->data[SC__MANHOLE] || tsc->data[SC_KINGS_GRACE]))
			return;
		
		if (!battle_config.sdelay_attack_enable && pc_checkskill(sd, SA_FREECAST) <= 0) {
			if (DIFF_TICK(tick, sd->ud.canact_tick) < 0) {
				clif_skill_fail(sd, 1, USESKILL_FAIL_SKILLINTERVAL, 0,0);
				return;
			}
		}

		pc_delinvincibletimer(sd);
		sd->idletime = last_tick;
		unit_attack(&sd->bl, target_id, action_type != 0);
	break;
	case 0x02: // sitdown
		if (battle_config.basic_skill_check && pc_checkskill(sd, NV_BASIC) < 3 && pc_checkskill(sd, SU_BASIC_SKILL) < 1) {
			clif_skill_fail(sd, 1, USESKILL_FAIL_LEVEL, 2,0);
			break;
		}

		if (pc_issit(sd))
		{
			//Bugged client? Just refresh them.
			clif_sitting(&sd->bl, false);
			return;
		}

		if (sd->ud.skilltimer != INVALID_TIMER || (sd->sc.opt1 && sd->sc.opt1 != OPT1_STONEWAIT && sd->sc.opt1 != OPT1_BURNING))
			break;

		if (sd->sc.count && (
			sd->sc.data[SC_DANCING] ||
			(sd->sc.data[SC_GRAVITATION] && sd->sc.data[SC_GRAVITATION]->val3 == BCT_SELF) ||
			sd->sc.data[SC_SITDOWN_FORCE] || sd->sc.data[SC_BANANA_BOMB_SITDOWN_POSTDELAY]
		)) //No sitting during these states either.
			break;

		if (sd->state.block_action & PCBLOCK_SITSTAND) {
			clif_displaymessage(sd->fd, msg_txt(sd, 794)); // This action is currently blocked.
			break;
		}

		sd->idletime = last_tick;
		pc_setsit(sd);
		skill_sit(sd,1);
		clif_sitting(&sd->bl, true);
	break;
	case 0x03: // standup		
		if (!pc_issit(sd))
		{
			//Bugged client? Just refresh them.
			clif_standing(&sd->bl, false);
			return;
		}

		if (sd->sc.opt1 && sd->sc.opt1 != OPT1_STONEWAIT && sd->sc.opt1 != OPT1_BURNING)
			break;

		if (sd->state.block_action & PCBLOCK_SITSTAND) {
			clif_displaymessage(sd->fd, msg_txt(sd, 794)); // This action is currently blocked.
			break;
		}

		sd->idletime = last_tick;
		if (pc_setstand(sd, false)) {
			skill_sit(sd, 0);
			clif_standing(&sd->bl, true);
		}
	break;
	}
}


/// Request for an action.
/// 0089 <target id>.L <action>.B (CZ_REQUEST_ACT)
/// 0437 <target id>.L <action>.B (CZ_REQUEST_ACT2)
/// action:
///     0 = attack
///     1 = pick up item
///     2 = sit down
///     3 = stand up
///     7 = continous attack
///     12 = (touch skill?)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_ActionRequest(int fd, struct map_session_data *sd)
{
	clif_parse_ActionRequest_sub(sd,
		RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]),
		RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]),
		gettick()
	);
}


/// Response to the death/system menu (CZ_RESTART).
/// 00b2 <type>.B
/// type:
///     0 = restart (respawn)
///     1 = char-select (disconnect)
void clif_parse_Restart(int fd, struct map_session_data *sd)
{
	switch(RFIFOB(fd,2)) {
	case 0x00:
		pc_respawn(sd, CLR_OUTSIGHT);
		break;
	case 0x01:
		/*	Rovert's Prevent logout option - Fixed [Valaris]	*/
		if( !sd->sc.data[SC_CLOAKING] && !sd->sc.data[SC_HIDING] && !sd->sc.data[SC_CHASEWALK] &&
			!sd->sc.data[SC_CLOAKINGEXCEED] && !sd->sc.data[SC__INVISIBILITY] && !sd->sc.data[SC_NEWMOON] && !sd->sc.data[SC_SUHIDE] &&
			(!battle_config.prevent_logout || DIFF_TICK(gettick(), sd->canlog_tick) > battle_config.prevent_logout) )
		{	//Send to char-server for character selection.
			pc_damage_log_clear(sd, 0);
			chrif_charselectreq(sd, session[fd]->client_addr);
		} else {
			clif_disconnect_ack(sd, 1);
		}
		break;
	}
}


/// Validates and processes whispered messages (CZ_WHISPER).
/// 0096 <packet len>.W <nick>.24B <message>.?B
void clif_parse_WisMessage(int fd, struct map_session_data* sd)
{
	struct map_session_data* dstsd;
	int i;

	char target[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	// validate packet and retrieve name and message
	if (!clif_process_message(sd, true, target, message, output))
		return;

	// Reset idle time when using whisper/main chat.
	sd->idletime = last_tick;

	// Chat logging type 'W' / Whisper
	log_chat(LOG_CHAT_WHISPER, 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, target, message);

	//-------------------------------------------------------//
	//   Lordalfa - Paperboy - To whisper NPC commands       //
	//-------------------------------------------------------//
	if (target[0] && (strncasecmp(target,"NPC:",4) == 0) && (strlen(target) > 4))
	{
		char* str = target+4; //Skip the NPC: string part.
		struct npc_data* npc;
		if ((npc = npc_name2id(str)))	
		{
			char split_data[NUM_WHISPER_VAR][CHAT_SIZE_MAX];
			char *split;
			char output[256];

			str = message;
			// skip codepage indicator, if detected
			if( str[0] == '|' && strlen(str) >= 4 )
				str += 3;
			for( i = 0; i < NUM_WHISPER_VAR; ++i )
			{// Splits the message using '#' as separators
				split = strchr(str,'#');
				if( split == NULL )
				{	// use the remaining string
					safestrncpy(split_data[i], str, ARRAYLENGTH(split_data[i]));
					for( ++i; i < NUM_WHISPER_VAR; ++i )
						split_data[i][0] = '\0';
					break;
				}
				*split = '\0';
				safestrncpy(split_data[i], str, ARRAYLENGTH(split_data[i]));
				str = split+1;
			}
			
			for( i = 0; i < NUM_WHISPER_VAR; ++i )
			{
				sprintf(output, "@whispervar%d$", i);
				set_var(sd,output,(char *) split_data[i]);
			}
			
			safesnprintf(output,sizeof(output),"%s::%s", npc->exname,script_config.onwhisper_event_name);
			npc_event(sd,output,0); // Calls the NPC label

			return;
		}
	}
	
	// Main chat [LuzZza]
	else if(strcmpi(target, main_chat_nick) == 0)
	{
		if(!sd->state.mainchat)
			clif_displaymessage(fd, msg_txt(sd,388)); // You should enable main chat with "@main on" command.
		else {
			// send the main message using inter-server system
			intif_main_message(sd, message);
		}
		return;
	}

	// searching destination character
	dstsd = map_nick2sd(target);

	if (dstsd == NULL || strcmp(dstsd->status.name, target) != 0)
	{
		// player is not on this map-server
		// At this point, don't send wisp/page if it's not exactly the same name, because (example)
		// if there are 'Test' player on an other map-server and 'test' player on this map-server,
		// and if we ask for 'Test', we must not contact 'test' player
		// so, we send information to inter-server, which is the only one which decide (and copy correct name).
		intif_wis_message(sd, target, message, strlen(message) + 1);
		return;
	}
	
	// if player ignores everyone
	if (dstsd->state.ignoreAll && pc_isGM(sd) <= pc_isGM(dstsd) )
	{
		if (dstsd->sc.option & OPTION_INVISIBLE && pc_isGM(sd) < pc_isGM(dstsd))
			clif_wis_end(fd, 1); // 1: target character is not loged in
		else
			clif_wis_end(fd, 3); // 3: everyone ignored by target
		return;
	}
	
	// if player is autotrading
	if( dstsd->state.autotrade == 1 )
	{
		char output[256];
		sprintf(output, "%s is in autotrade mode and cannot receive whispered messages.", dstsd->status.name);
		clif_wis_message(fd, wisp_server_name, output, strlen(output) + 1);
		return;
	}

	if (pc_isGM(sd) <= pc_isGM(dstsd)) {
		// if player ignores the source character
		ARR_FIND(0, MAX_IGNORE_LIST, i, dstsd->ignore[i].name[0] == '\0' || strcmp(dstsd->ignore[i].name, sd->status.name) == 0);
		if (i < MAX_IGNORE_LIST && dstsd->ignore[i].name[0] != '\0')
		{	// source char present in ignore list
			clif_wis_end(fd, 2); // 2: ignored by target
			return;
		}
	}
	
	// notify sender of success
	clif_wis_end(fd, 0); // 0: success to send wisper

	// Normal message
	clif_wis_message(dstsd->fd, sd->status.name, message, strlen(message) + 1);
}


/// /b /nb (CZ_BROADCAST).
/// Request to broadcast a message on whole server.
/// 0099 <packet len>.W <text>.?B 00
void clif_parse_Broadcast(int fd, struct map_session_data* sd)
{
	char* msg = (char*)RFIFOP(fd,4);
	unsigned int len = RFIFOW(fd,2)-4;
	int lv;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;
	if( pc_isGM(sd) < (lv=get_atcommand_level(atcommand_broadcast)) )
		return;

	// as the length varies depending on the command used, just block unreasonably long strings
	mes_len_check(msg, len, CHAT_SIZE_MAX);

	intif_broadcast(msg, len, 0);

	{
		char logmsg[CHAT_SIZE_MAX+4];
		sprintf(logmsg, "/b %s", msg);
		log_atcommand(sd, lv, logmsg);
	}
}


/// Request to pick up an item.
/// 009f <id>.L (CZ_ITEM_PICKUP)
/// 0362 <id>.L (CZ_ITEM_PICKUP2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_TakeItem(int fd, struct map_session_data *sd)
{
	struct flooritem_data *fitem;
	int map_object_id;

	map_object_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	
	fitem = (struct flooritem_data*)map_id2bl(map_object_id);

	do {
		if (pc_isdead(sd)) {
			clif_clearunit_area(&sd->bl, CLR_DEAD);
			break;
		}

		if (fitem == NULL || fitem->bl.type != BL_ITEM || fitem->bl.m != sd->bl.m)
			break;
	
		if (pc_cant_act(sd))
			break;

		if(sd->sc.count && (
			sd->sc.data[SC_HIDING] ||
			sd->sc.data[SC_CLOAKING] ||
			sd->sc.data[SC_TRICKDEAD] ||
			sd->sc.data[SC_BLADESTOP] ||
			sd->sc.data[SC_CLOAKINGEXCEED] ||
			sd->sc.data[SC_CURSEDCIRCLE_ATKER] ||
			sd->sc.data[SC_CURSEDCIRCLE_TARGET] ||
			sd->sc.data[SC_NEWMOON] ||
			sd->sc.data[SC_GRAVITYCONTROL] ||
			sd->sc.data[SC_SUHIDE] ||
			(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOITEM)
			))
			break;

		if (!pc_takeitem(sd, fitem))
			break;

		return;
	} while (0);
	// Client REQUIRES a fail packet or you can no longer pick items.
	clif_additem(sd,0,0,6);
}


/// Request to drop an item.
/// 00a2 <index>.W <amount>.W (CZ_ITEM_THROW)
/// 0363 <index>.W <amount>.W (CZ_ITEM_THROW2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_DropItem(int fd, struct map_session_data *sd)
{
	int item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	int item_amount = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);

	for(;;) {
		if (pc_isdead(sd))
			break;

		if (pc_cant_act2(sd) || sd->npc_id)
			break;

		if (sd->sc.count && (
			sd->sc.data[SC_AUTOCOUNTER] ||
			sd->sc.data[SC_DEATHBOUND] ||
			sd->sc.data[SC_BLADESTOP] ||
			sd->sc.data[SC_CURSEDCIRCLE_ATKER] ||
			sd->sc.data[SC_CURSEDCIRCLE_TARGET] ||
			(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOITEM)
		))
			break;

		if (!pc_dropitem(sd, item_index, item_amount))
			break;

		return;
	}

	//Because the client does not like being ignored.
	clif_dropitem(sd, item_index,0);
}


/// Request to use an item.
/// 00a7 <index>.W <account id>.L (CZ_USE_ITEM)
/// 0439 <index>.W <account id>.L (CZ_USE_ITEM2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_UseItem(int fd, struct map_session_data *sd)
{
	int n;
	int index = RFIFOW(fd, 2) - 2;

	if (pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl, CLR_DEAD);
		return;
	}
	
	if ((!sd->npc_id && pc_istrading(sd)) || sd->chatID || (sd->state.block_action & PCBLOCK_USEITEM)) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return;
	}

	//Whether the item is used or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;
	n = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	
	if(n <0 || n >= MAX_INVENTORY)
		return;
	if (!pc_useitem(sd,n))
		clif_useitemack(sd,n,0,false); //Send an empty ack packet or the client gets stuck.
}


/// Request to equip an item.
/// 00a9 <index>.W <position>.W (CZ_REQ_WEAR_EQUIP)
/// 0998 <index>.W <position>.L (CZ_REQ_WEAR_EQUIP_V5)
void clif_parse_EquipItem(int fd,struct map_session_data *sd) {
	int index;
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,CLR_DEAD);
		return;
	}
	index = RFIFOW(fd,info->pos[0])-2; 
	if (index < 0 || index >= MAX_INVENTORY)
		return; //Out of bounds check.
	
	if(sd->npc_id) {
		if (!sd->npc_item_flag)
			return;
	} else if (sd->state.storage_flag || (sd->sc.opt1 && sd->sc.opt1 != OPT1_BURNING))
		; //You can equip/unequip stuff while storage is open/under status changes
	else if (pc_cant_act2(sd))
		return;

	if(!sd->inventory.u.items_inventory[index].identify) {
		clif_equipitemack(sd,index,0, ITEM_EQUIP_ACK_FAIL);
		return;
	}

	if(!sd->inventory_data[index])
		return;

	if(sd->inventory_data[index]->type == IT_PETARMOR){
		pet_equipitem(sd,index);
		return;
	}

	//Client doesn't send the position for ammo.
	if( sd->inventory_data[index]->type == IT_AMMO )
		pc_equipitem(sd,index,EQP_AMMO,false);
	else {
	int req_pos;

#if PACKETVER  >= 20120925
		req_pos = RFIFOL(fd,info->pos[1]);
#else
		req_pos = (int)RFIFOW(fd,info->pos[1]);
#endif
		pc_equipitem(sd,index,req_pos,false);
	}
}


/// Request to take off an equip (CZ_REQ_TAKEOFF_EQUIP).
/// 00ab <index>.W
void clif_parse_UnequipItem(int fd,struct map_session_data *sd)
{
	int index;

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,CLR_DEAD);
		return;
	}

	if (sd->npc_id) {
		if (!sd->npc_item_flag)
			return;
	}
	else if (sd->state.storage_flag || sd->sc.opt1)
		; //You can equip/unequip stuff while storage is open/under status changes.
	else if (pc_cant_act2(sd))
		return;

	index = RFIFOW(fd,2)-2;

	pc_unequipitem(sd,index,1);
}


/// Request to start a conversation with an NPC (CZ_CONTACTNPC).
/// 0090 <id>.L <type>.B
/// type:
///     1 = click
void clif_parse_NpcClicked(int fd,struct map_session_data *sd)
{
	struct block_list *bl;

	if(pc_isdead(sd)) {
		clif_clearunit_area(&sd->bl,CLR_DEAD);
		return;
	}

	if (pc_cant_act2(sd) || sd->npc_id || pc_hasprogress(sd, WIP_DISABLE_NPC)) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return;
	}

	if( sd->state.mail_writing )
		return;

	bl = map_id2bl(RFIFOL(fd,2));
	if (!bl) return;
	switch (bl->type) {
		case BL_MOB:
		case BL_PC:
			clif_parse_ActionRequest_sub(sd, 0x07, bl->id, gettick());
			break;
		case BL_NPC:
			if (sd->ud.skill_id < RK_ENCHANTBLADE && sd->ud.skilltimer != INVALID_TIMER) { // Should only show an error message for non-3rd job skills with a running timer
				clif_msg(sd, WORK_IN_PROGRESS);
				break;
			}

			if( bl->m != -1 )// the user can't click floating npcs directly (hack attempt)
				npc_click(sd,(TBL_NPC*)bl);
			break;
	}
}


/// Selection between buy/sell was made (CZ_ACK_SELECT_DEALTYPE).
/// 00c5 <id>.L <type>.B
/// type:
///     0 = buy
///     1 = sell
void clif_parse_NpcBuySellSelected(int fd,struct map_session_data *sd)
{
	if (sd->state.trading)
		return;
	npc_buysellsel(sd,RFIFOL(fd,2),RFIFOB(fd,6));
}


/// Notification about the result of a purchase attempt from an NPC shop (ZC_PC_PURCHASE_RESULT).
/// 00ca <result>.B
/// result:
///     0 = "The deal has successfully completed."
///     1 = "You do not have enough zeny."
///     2 = "You are over your Weight Limit."
///     3 = "Out of the maximum capacity, you have too many items."
void clif_npc_buy_result(struct map_session_data* sd, unsigned char result)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xca));
	WFIFOW(fd,0) = 0xca;
	WFIFOB(fd,2) = result;
	WFIFOSET(fd,packet_len(0xca));
}


/// Request to buy chosen items from npc shop (CZ_PC_PURCHASE_ITEMLIST).
/// 00c8 <packet len>.W { <amount>.W <name id>.W }*
void clif_parse_NpcBuyListSend(int fd, struct map_session_data* sd) {
	const struct PACKET_CZ_PC_PURCHASE_ITEMLIST *p = (struct PACKET_CZ_PC_PURCHASE_ITEMLIST *)RFIFOP(fd, 0);

	uint16 n = (p->packetLength - sizeof(struct PACKET_CZ_PC_PURCHASE_ITEMLIST)) / sizeof(struct PACKET_CZ_PC_PURCHASE_ITEMLIST_sub);
	int result;
	
	if (sd->state.trading || !sd->npc_shopid)
		result = 1;
	else
	{
		struct s_npc_buy_list *items;
		CREATE(items, struct s_npc_buy_list, n+1);

		for (uint16 i = 0; i < n; i++) {
			items[i].nameid = p->items[i].itemId;
			items[i].qty = p->items[i].amount;
		}

		result = npc_buylist(sd, n, items);
	}

	sd->npc_shopid = 0; //Clear shop data.
	clif_npc_buy_result(sd, result);
}


/// Notification about the result of a sell attempt to an NPC shop (ZC_PC_SELL_RESULT).
/// 00cb <result>.B
/// result:
///     0 = "The deal has successfully completed."
///     1 = "The deal has failed."
void clif_npc_sell_result(struct map_session_data* sd, unsigned char result)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0xcb));
	WFIFOW(fd,0) = 0xcb;
	WFIFOB(fd,2) = result;
	WFIFOSET(fd,packet_len(0xcb));
}


/// Request to sell chosen items to npc shop (CZ_PC_SELL_ITEMLIST).
/// 00c9 <packet len>.W { <index>.W <amount>.W }*
void clif_parse_NpcSellListSend(int fd,struct map_session_data *sd)
{
	int fail=0,n;
	unsigned short *item_list;

	n = (RFIFOW(fd,2)-4) /4;
	item_list = (unsigned short*)RFIFOP(fd,4);

	if (sd->state.trading || !sd->npc_shopid)
		fail = 1;
	else
		fail = npc_selllist(sd,n,item_list);
	
	sd->npc_shopid = 0; //Clear shop data.

	clif_npc_sell_result(sd, fail);
}


/// Chatroom creation request (CZ_CREATE_CHATROOM).
/// 00d5 <packet len>.W <limit>.W <type>.B <passwd>.8B <title>.?B
/// type:
///     0 = private
///     1 = public
void clif_parse_CreateChatRoom(int fd, struct map_session_data* sd)
{
	int len = (int)RFIFOW(fd,2)-15;
	int limit = RFIFOW(fd,4);
	bool pub = (RFIFOB(fd,6) != 0);
	const char* password = (char*)RFIFOP(fd,7); //not zero-terminated
	const char* title = (char*)RFIFOP(fd,15); // not zero-terminated
	char s_password[CHATROOM_PASS_SIZE];
	char s_title[CHATROOM_TITLE_SIZE];

	if (sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOROOM)
		return;
	if(battle_config.basic_skill_check && pc_checkskill(sd,NV_BASIC) < 4 && pc_checkskill(sd, SU_BASIC_SKILL) < 1) {
		clif_skill_fail(sd,1,USESKILL_FAIL_LEVEL,3,0);
		return;
	}

	if (npc_isnear(&sd->bl)) {
		// uncomment to send msg_txt.
		//char output[150];
		//sprintf(output, msg_txt(sd,725), battle_config.min_npc_vendchat_distance);
		//clif_displaymessage(sd->fd, output);
		clif_skill_fail(sd, 1, USESKILL_FAIL_THERE_ARE_NPC_AROUND, 0, 0);
		return;
	}

	if( len <= 0 )
		return; // invalid input

	safestrncpy(s_password, password, CHATROOM_PASS_SIZE);
	safestrncpy(s_title, title, min(len+1,CHATROOM_TITLE_SIZE)); //NOTE: assumes that safestrncpy will not access the len+1'th byte

	chat_createpcchat(sd, s_title, s_password, limit, pub);
}


/// Chatroom join request (CZ_REQ_ENTER_ROOM).
/// 00d9 <chat ID>.L <passwd>.8B
void clif_parse_ChatAddMember(int fd, struct map_session_data* sd)
{
	int chatid = RFIFOL(fd,2);
	const char* password = (char*)RFIFOP(fd,6); // not zero-terminated

	chat_joinchat(sd,chatid,password);
}


/// Chatroom properties adjustment request (CZ_CHANGE_CHATROOM).
/// 00de <packet len>.W <limit>.W <type>.B <passwd>.8B <title>.?B
/// type:
///     0 = private
///     1 = public
void clif_parse_ChatRoomStatusChange(int fd, struct map_session_data* sd)
{
	int len = (int)RFIFOW(fd,2)-15;
	int limit = RFIFOW(fd,4);
	bool pub = (RFIFOB(fd,6) != 0);
	const char* password = (char*)RFIFOP(fd,7); // not zero-terminated
	const char* title = (char*)RFIFOP(fd,15); // not zero-terminated
	char s_password[CHATROOM_PASS_SIZE];
	char s_title[CHATROOM_TITLE_SIZE];

	if( len <= 0 )
		return; // invalid input

	safestrncpy(s_password, password, CHATROOM_PASS_SIZE);
	safestrncpy(s_title, title, min(len+1,CHATROOM_TITLE_SIZE)); //NOTE: assumes that safestrncpy will not access the len+1'th byte

	chat_changechatstatus(sd, s_title, s_password, limit, pub);
}


/// Request to change the chat room ownership (CZ_REQ_ROLE_CHANGE).
/// 00e0 <role>.L <nick>.24B
/// role:
///     0 = owner
///     1 = normal
void clif_parse_ChangeChatOwner(int fd, struct map_session_data* sd)
{
	chat_changechatowner(sd,(char*)RFIFOP(fd,6));
}


/// Request to expel a player from chat room (CZ_REQ_EXPEL_MEMBER).
/// 00e2 <name>.24B
void clif_parse_KickFromChat(int fd,struct map_session_data *sd)
{
	chat_kickchat(sd,(char*)RFIFOP(fd,2));
}


/// Request to leave the current chatroom (CZ_EXIT_ROOM).
/// 00e3
void clif_parse_ChatLeave(int fd, struct map_session_data* sd)
{
	chat_leavechat(sd,0);
}


//Handles notifying asker and rejecter of what has just ocurred.
//Type is used to determine the correct msg_txt to use:
//0: 
static void clif_noask_sub(struct map_session_data *sd, struct map_session_data *tsd, int type)
{
	const char* msg;
	char output[256];
	// Your request has been rejected by autoreject option.
	msg = msg_txt(sd,392);
	clif_disp_onlyself(sd, msg, strlen(msg));
	//Notice that a request was rejected.
	snprintf(output, 256, msg_txt(sd,393+type), sd->status.name, 256);
	clif_disp_onlyself(tsd, output, strlen(output));
}


/// Request to begin a trade (CZ_REQ_EXCHANGE_ITEM).
/// 00e4 <account id>.L
void clif_parse_TradeRequest(int fd,struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	t_sd = map_id2sd(RFIFOL(fd,2));

	if(!sd->chatID && pc_cant_act(sd))
		return; //You can trade while in a chatroom.

	if(t_sd){
		// @noask [LuzZza]
		if(t_sd->state.noask) {
			clif_noask_sub(sd, t_sd, 0);
			return;
		}

		if (t_sd->state.mail_writing) {
			int old = sd->trade_partner;

			// Fake trading
			sd->trade_partner = t_sd->status.account_id;
			clif_tradestart(sd, 5);
			// Restore old state
			sd->trade_partner = old;

			return;
		}
	}

	if( battle_config.basic_skill_check && pc_checkskill(sd,NV_BASIC) < 1 && pc_checkskill(sd, SU_BASIC_SKILL) < 1)
	{
		clif_skill_fail(sd,1,USESKILL_FAIL_LEVEL,0,0);
		return;
	}
	
	trade_traderequest(sd,t_sd);
}


/// Answer to a trade request (CZ_ACK_EXCHANGE_ITEM).
/// 00e6 <result>.B
/// result:
///     3 = accepted
///     4 = rejected
void clif_parse_TradeAck(int fd,struct map_session_data *sd)
{
	if( !sd->state.can_tradeack )
		return; // client isn't supposed to send this

	trade_tradeack(sd,RFIFOB(fd,2));
}


/// Request to add an item to current trade (CZ_ADD_EXCHANGE_ITEM).
/// 00e8 <index>.W <amount>.L
void clif_parse_TradeAddItem(int fd,struct map_session_data *sd)
{
	short index = RFIFOW(fd,2);
	int amount = RFIFOL(fd,4);

	if( index == 0 )
		trade_tradeaddzeny(sd, amount);
	else
		trade_tradeadditem(sd, index, (short)amount);
}


/// Request to lock items in current trade (CZ_CONCLUDE_EXCHANGE_ITEM).
/// 00eb
void clif_parse_TradeOk(int fd,struct map_session_data *sd)
{
	trade_tradeok(sd);
}


/// Request to cancel current trade (CZ_CANCEL_EXCHANGE_ITEM).
/// 00ed
void clif_parse_TradeCancel(int fd,struct map_session_data *sd)
{
	trade_tradecancel(sd);
}


/// Request to commit current trade (CZ_EXEC_EXCHANGE_ITEM).
/// 00ef
void clif_parse_TradeCommit(int fd,struct map_session_data *sd)
{
	trade_tradecommit(sd);
}


/// Request to stop chasing/attacking an unit (CZ_CANCEL_LOCKON).
/// 0118
void clif_parse_StopAttack(int fd,struct map_session_data *sd)
{
	pc_stop_attack(sd);
}


/// Request to move an item from inventory to cart (CZ_MOVE_ITEM_FROM_BODY_TO_CART).
/// 0126 <index>.W <amount>.L
void clif_parse_PutItemToCart(int fd,struct map_session_data *sd)
{
	if (pc_istrading(sd))
		return;
	if (!pc_iscarton(sd))
		return;
	pc_putitemtocart(sd,RFIFOW(fd,2)-2,RFIFOL(fd,4));
}


/// Request to move an item from cart to inventory (CZ_MOVE_ITEM_FROM_CART_TO_BODY).
/// 0127 <index>.W <amount>.L
void clif_parse_GetItemFromCart(int fd,struct map_session_data *sd)
{
	if (!pc_iscarton(sd))
		return;
	pc_getitemfromcart(sd,RFIFOW(fd,2)-2,RFIFOL(fd,4));
}


/// Request to remove cart/falcon/peco/dragon (CZ_REQ_CARTOFF).
/// 012a
void clif_parse_RemoveOption(int fd, struct map_session_data *sd)
{
	if (!(sd->sc.option&(OPTION_RIDING | OPTION_FALCON | OPTION_DRAGON | OPTION_MADOGEAR)) && sd->sc.data[SC_ON_PUSH_CART]) {
		pc_setcart(sd, 0);
	}
	else  // priority to remove this option before we can clear cart
		pc_setoption(sd, sd->sc.option&~(OPTION_RIDING | OPTION_FALCON | OPTION_DRAGON | OPTION_MADOGEAR));
}

/// Request to change cart's visual look (CZ_REQ_CHANGECART).
/// 01af <num>.W
void clif_parse_ChangeCart(int fd,struct map_session_data *sd)
{// TODO: State tracking?
	int type;

	if(!sd || pc_checkskill(sd, MC_CHANGECART) < 1)
		return;

	type = (int)RFIFOW(fd,2);

	if(
#if PACKETVER >= 20120410
		(type == 9 && sd->status.base_level > 130) ||
		(type == 8 && sd->status.base_level > 120) ||
		(type == 7 && sd->status.base_level > 110) ||
		(type == 6 && sd->status.base_level > 100) ||
#endif
		(type == 5 && sd->status.base_level > 90) ||
	    (type == 4 && sd->status.base_level > 80) ||
	    (type == 3 && sd->status.base_level > 65) ||
	    (type == 2 && sd->status.base_level > 40) ||
	    (type == 1))
		pc_setcart(sd,type);
}

/// Request to increase status (CZ_STATUS_CHANGE).
/// 00bb <status id>.W <amount>.B
/// status id:
///     SP_STR ~ SP_LUK
/// amount:
///     Old clients always send 1 for this, even when using /str+ and the like.
///     Newer clients (2013-12-23 and newer) send the correct amount.
void clif_parse_StatusUp(int fd,struct map_session_data *sd)
{
	int increase_amount = RFIFOB(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);

	if( increase_amount < 0 ) {
		ShowDebug("clif_parse_StatusUp: Negative 'increase' value sent by client! (fd: %d, value: %d)\n",
			fd, increase_amount);
	}
	pc_statusup(sd,RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]),increase_amount);
}

/// Request to increase level of a skill (CZ_UPGRADE_SKILLLEVEL).
/// 0112 <skill id>.W
void clif_parse_SkillUp(int fd,struct map_session_data *sd)
{
	pc_skillup(sd,RFIFOW(fd,2));
}


// TODO: Move clif_parse_UseSkillTo* helper functions to unit.c
static void clif_parse_UseSkillToId_homun(struct homun_data *hd, struct map_session_data *sd, int64 tick, short skill_id, short skill_lv, int target_id)
{
	int lv;

	if( !hd )
		return;
	if (skillnotok_homun(skill_id, hd))
	{
		clif_emotion(&hd->bl, E_DOTS);
		return;
	}
	if( hd->bl.id != target_id && skill_get_inf(skill_id)&INF_SELF_SKILL )
		target_id = hd->bl.id;
	if( sd->ud.skilltimer != INVALID_TIMER ){
		if( skill_id != SA_CASTCANCEL &&
		!(skill_id == SO_SPELLFIST && (sd->ud.skill_id == MG_FIREBOLT || sd->ud.skill_id == MG_COLDBOLT || sd->ud.skill_id == MG_LIGHTNINGBOLT)) )
 			return;
	}
	else if (DIFF_TICK(tick, hd->ud.canact_tick) < 0) {
		clif_emotion(&hd->bl, E_DOTS);
		if (hd->master)
			clif_skill_fail(hd->master, skill_id, USESKILL_FAIL_SKILLINTERVAL, 0, 0);
		return;
	}

	lv = hom_checkskill(hd, skill_id);
	if( skill_lv > lv )
		skill_lv = lv;
	if( skill_lv )
		unit_skilluse_id(&hd->bl, target_id, skill_id, skill_lv);
}

static void clif_parse_UseSkillToId_mercenary(struct mercenary_data *md, struct map_session_data *sd, int64 tick, short skill_id, short skill_lv, int target_id)
{
	int lv;

	if( !md )
		return;
	if( skillnotok_mercenary(skill_id, md) )
		return;
	if( md->bl.id != target_id && skill_get_inf(skill_id)&INF_SELF_SKILL )
		target_id = md->bl.id;
	if( md->ud.skilltimer != INVALID_TIMER )
	{
		if( skill_id != SA_CASTCANCEL ) return;
	}
	else if( DIFF_TICK(tick, md->ud.canact_tick) < 0 )
		return;

	lv = mercenary_checkskill(md, skill_id);
	if( skill_lv > lv )
		skill_lv = lv;
	if( skill_lv )
		unit_skilluse_id(&md->bl, target_id, skill_id, skill_lv);
}

static void clif_parse_UseSkillToPos_homun(struct homun_data *hd, struct map_session_data *sd, int64 tick, short skill_id, short skill_lv, short x, short y, int skillmoreinfo)
{
	int lv;
	if( !hd )
		return;
	if (skillnotok_homun(skill_id, hd))
	{
		clif_emotion(&hd->bl, E_DOTS);
		return;
	}
	if( hd->ud.skilltimer != INVALID_TIMER )
		return;
	else if( DIFF_TICK(tick, hd->ud.canact_tick) < 0 ) {
		clif_emotion(&hd->bl, E_DOTS);
		if (hd->master)
			clif_skill_fail(hd->master, skill_id, USESKILL_FAIL_SKILLINTERVAL, 0, 0);
		clif_skill_fail(hd->master, skill_id, 4, 0, 0);
		return;
	}

	if( hd->sc.data[SC_BASILICA] )
		return;
	lv = hom_checkskill(hd, skill_id);
	if( skill_lv > lv )
		skill_lv = lv;
	if( skill_lv )
		unit_skilluse_pos(&hd->bl, x, y, skill_id, skill_lv);
}

static void clif_parse_UseSkillToPos_mercenary(struct mercenary_data *md, struct map_session_data *sd, int64 tick, short skill_id, short skill_lv, short x, short y, int skillmoreinfo)
{
	int lv;
	if( !md )
		return;
	if( skillnotok_mercenary(skill_id, md) )
		return;
	if( md->ud.skilltimer != INVALID_TIMER )
		return;
	if( DIFF_TICK(tick, md->ud.canact_tick) < 0 )
	{
		if (md->master)
			clif_skill_fail(md->master, skill_id, USESKILL_FAIL_SKILLINTERVAL, 0,0);
		return;
	}

	if( md->sc.data[SC_BASILICA] )
		return;
	lv = mercenary_checkskill(md, skill_id);
	if( skill_lv > lv )
		skill_lv = lv;
	if( skill_lv )
		unit_skilluse_pos(&md->bl, x, y, skill_id, skill_lv);
}


void clif_parse_skill_toid(struct map_session_data* sd, uint16 skill_id, uint16 skill_lv, int target_id)
{
	int inf;
	int64 tick = gettick();

	if( skill_lv < 1 ) skill_lv = 1; //No clue, I have seen the client do this with guild skills :/ [Skotlex]

	inf = skill_get_inf(skill_id);
	if (inf&INF_GROUND_SKILL || !inf) {
		return; //Using a ground/passive skill on a target? WRONG.
	}

	if (sd->state.block_action & PCBLOCK_SKILL) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return;
	}

	if( skill_id >= HM_SKILLBASE && skill_id < HM_SKILLBASE + MAX_HOMUNSKILL )
	{
		clif_parse_UseSkillToId_homun(sd->hd, sd, tick, skill_id, skill_lv, target_id);
		return;
	}

	if( skill_id >= MC_SKILLBASE && skill_id < MC_SKILLBASE + MAX_MERCSKILL )
	{
		clif_parse_UseSkillToId_mercenary(sd->md, sd, tick, skill_id, skill_lv, target_id);
		return;
	}

	// Whether skill fails or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;

	if (sd->npc_id) {
		if (pc_hasprogress(sd, WIP_DISABLE_SKILLITEM) || !sd->npc_item_flag || !(inf & INF_SELF_SKILL)) {
			clif_msg(sd, WORK_IN_PROGRESS);
			return;
		}
	}

	if ((pc_cant_act2(sd) || sd->chatID) && skill_id != RK_REFRESH && skill_id != SR_GENTLETOUCH_CURE &&
		skill_id != SU_GROOMING && sd->state.storage_flag && !(inf&INF_SELF_SKILL)) //SELF skills can be used with the storage open
		return;

	if( pc_issit(sd) )
		return;

	if( skillnotok(skill_id, sd) )
		return;

	if( sd->bl.id != target_id && inf&INF_SELF_SKILL )
		target_id = sd->bl.id; // never trust the client
	
	if( target_id < 0 && -target_id == sd->bl.id ) // for disguises [Valaris]
		target_id = sd->bl.id;

	if( sd->sc.data[SC_SIREN] && sd->sc.data[SC_SIREN]->val2 == target_id )
		return;
	
	if( sd->ud.skilltimer != INVALID_TIMER )
	{
		if( skill_id != SA_CASTCANCEL )
			return;
	}
	else if( DIFF_TICK(tick, sd->ud.canact_tick) < 0 && skill_id != SO_SPELLFIST )
	{
		if( sd->skillitem != skill_id )
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_SKILLINTERVAL, 0,0);
			return;
		}
	}

	if( sd->sc.option&OPTION_COSTUME)
		return;

	if( sd->sc.data[SC_BASILICA] && (skill_id != HP_BASILICA || sd->sc.data[SC_BASILICA]->val4 != sd->bl.id) )
		return; // On basilica only caster can use Basilica again to stop it.

	if( sd->sc.data[SC__MANHOLE] )
		return;
	
	if( sd->menuskill_id )
	{
		if( sd->menuskill_id == SA_TAMINGMONSTER )
			sd->menuskill_id = sd->menuskill_val = 0; //Cancel pet capture.
		else if( sd->menuskill_id == SG_FEEL )
			sd->menuskill_id = sd->menuskill_val = 0; // Cancel selection
		else if( sd->menuskill_id != SA_AUTOSPELL )
			return; //Can't use skills while a menu is open.
	}
	if( sd->skillitem == skill_id )
	{
		if( skill_lv != sd->skillitemlv )
			skill_lv = sd->skillitemlv;
		if( !(inf&INF_SELF_SKILL) )
			pc_delinvincibletimer(sd); // Target skills thru items cancel invincibility. [Inkfish]
		unit_skilluse_id(&sd->bl, target_id, skill_id, skill_lv);
		return;
	}

	sd->skillitem = sd->skillitemlv = 0;

	if( skill_id >= GD_SKILLBASE )
	{
		if( sd->state.gmaster_flag )
		{
			skill_lv = guild_checkskill(sd->guild, skill_id);
		}
		else
			skill_lv = 0;
	}
	else
	{
		if (skill_id != ALL_EQSWITCH) {
			skill_lv = min(pc_checkskill(sd, skill_id), skill_lv); //never trust client
		}
	}

	pc_delinvincibletimer(sd);

	if( skill_lv )
		unit_skilluse_id(&sd->bl, target_id, skill_id, skill_lv);
}

/// Request to use a targeted skill.
/// 0113 <skill lv>.W <skill id>.W <target id>.L (CZ_USE_SKILL)
/// 0438 <skill lv>.W <skill id>.W <target id>.L (CZ_USE_SKILL2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_UseSkillToId(int fd, struct map_session_data *sd) {
	short skill_lv, skill_id;
	int target_id;
	
	skill_lv = RFIFOW(fd, packet_db[sd->packet_ver][RFIFOW(fd, 0)].pos[0]);
	skill_id = RFIFOW(fd, packet_db[sd->packet_ver][RFIFOW(fd, 0)].pos[1]);
	target_id = RFIFOL(fd, packet_db[sd->packet_ver][RFIFOW(fd, 0)].pos[2]);

	clif_parse_skill_toid(sd, skill_id, skill_lv, target_id);
}

 /*==========================================
  * Client tells server he'd like to use AoE skill id 'skill_id' of level 'skill_lv' on 'x','y' location
  *------------------------------------------*/
static void clif_parse_UseSkillToPosSub(int fd, struct map_session_data *sd, short skill_lv, short skill_id, short x, short y, int skillmoreinfo)
{
	int64 tick = gettick();

	if( !(skill_get_inf(skill_id)&INF_GROUND_SKILL) )
		return; //Using a target skill on the ground? WRONG.

	if (sd->state.block_action & PCBLOCK_SKILL) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return;
	}

	if( skill_id >= HM_SKILLBASE && skill_id < HM_SKILLBASE + MAX_HOMUNSKILL )
	{
		clif_parse_UseSkillToPos_homun(sd->hd, sd, tick, skill_id, skill_lv, x, y, skillmoreinfo);
		return;
	}

	if( skill_id >= MC_SKILLBASE && skill_id < MC_SKILLBASE + MAX_MERCSKILL )
	{
		clif_parse_UseSkillToPos_mercenary(sd->md, sd, tick, skill_id, skill_lv, x, y, skillmoreinfo);
		return;
	}

	if (pc_hasprogress(sd, WIP_DISABLE_SKILLITEM)) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return;
	}

	//Whether skill fails or not is irrelevant, the char ain't idle. [Skotlex]
	sd->idletime = last_tick;

	if( skillnotok(skill_id, sd) )
		return;
	if( skillmoreinfo != -1 )
	{
		if( pc_issit(sd) )
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0,0);
			return;
		}
		//You can't use Graffiti/TalkieBox AND have a vending open, so this is safe.
		safestrncpy(sd->message, (char*)RFIFOP(fd,skillmoreinfo), MESSAGE_SIZE);
	}

	if( sd->ud.skilltimer != INVALID_TIMER )
		return;

	if( DIFF_TICK(tick, sd->ud.canact_tick) < 0 )
	{
		if( sd->skillitem != skill_id )
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_SKILLINTERVAL, 0,0);
			return;
		}
	}

	if( sd->sc.option&OPTION_COSTUME)
		return;

	if( sd->sc.data[SC_BASILICA] && (skill_id != HP_BASILICA || sd->sc.data[SC_BASILICA]->val4 != sd->bl.id) )
		return; // On basilica only caster can use Basilica again to stop it.

	if( sd->sc.data[SC__MANHOLE] )
		return;

	if( sd->menuskill_id )
	{
		if( sd->menuskill_id == SA_TAMINGMONSTER )
			sd->menuskill_id = sd->menuskill_val = 0; //Cancel pet capture.
		else if( sd->menuskill_id == SG_FEEL )
			sd->menuskill_id = sd->menuskill_val = 0; // Cancel selection
		else if( sd->menuskill_id != SA_AUTOSPELL )
			return; //Can't use skills while a menu is open.
	}

	pc_delinvincibletimer(sd);

	if( sd->skillitem == skill_id )
	{
		if( skill_lv != sd->skillitemlv )
			skill_lv = sd->skillitemlv;
		unit_skilluse_pos(&sd->bl, x, y, skill_id, skill_lv);
	}
	else
	{
		int lv;

		sd->skillitem = sd->skillitemlv = 0;
		if( (lv = pc_checkskill(sd, skill_id)) > 0 )
		{
			if( skill_lv > lv )
				skill_lv = lv;
			unit_skilluse_pos(&sd->bl, x, y, skill_id,skill_lv);
		}
	}
}


/// Request to use a ground skill.
/// 0116 <skill lv>.W <skill id>.W <x>.W <y>.W (CZ_USE_SKILL_TOGROUND)
/// 0366 <skill lv>.W <skill id>.W <x>.W <y>.W (CZ_USE_SKILL_TOGROUND2)
/// 0AF4 <skill lv>.W <skill id>.W <x>.W <y>.W <unknown>.B (CZ_USE_SKILL_TOGROUND3)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_UseSkillToPos(int fd, struct map_session_data *sd)
{
	if (pc_cant_act(sd))
		return;
	if (pc_issit(sd))
		return;

	clif_parse_UseSkillToPosSub(fd, sd,
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]), //skill lv
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]), //skill num
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[2]), //pos x
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[3]), //pos y
		// TODO: find out what this is intended to do
		//RFIFOB(fd, info->pos[4])
		-1	//Skill more info.
	);
}


/// Request to use a ground skill with text.
/// 0190 <skill lv>.W <skill id>.W <x>.W <y>.W <contents>.80B (CZ_USE_SKILL_TOGROUND_WITHTALKBOX)
/// 0367 <skill lv>.W <skill id>.W <x>.W <y>.W <contents>.80B (CZ_USE_SKILL_TOGROUND_WITHTALKBOX2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_UseSkillToPosMoreInfo(int fd, struct map_session_data *sd)
{
	if (pc_cant_act(sd))
		return;
	if (pc_issit(sd))
		return;
	
	clif_parse_UseSkillToPosSub(fd, sd,
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]), //Skill lv
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]), //Skill num
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[2]), //pos x
		RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[3]), //pos y
		packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[4] //skill more info
	);
}


/// Answer to map selection dialog (CZ_SELECT_WARPPOINT).
/// 011b <skill id>.W <map name>.16B
void clif_parse_UseSkillMap(int fd, struct map_session_data* sd) {
	short skill_id = RFIFOW(fd,2);
	char map_name[MAP_NAME_LENGTH];

	mapindex_getmapname((char*)RFIFOP(fd,4), map_name);
	sd->state.workinprogress = WIP_DISABLE_NONE;

	if(skill_id != sd->menuskill_id) 
		return;

	if( sd->sc.data[SC__MANHOLE] )
		return;

	//It is possible to use teleport with the storage window open.
	if (pc_cant_act(sd) && !sd->state.storage_flag && skill_id != AL_TELEPORT)
	{
		sd->menuskill_id = sd->menuskill_val = 0;
		return;
	}

	pc_delinvincibletimer(sd);
	skill_castend_map(sd,skill_id,map_name);
	pc_itemskill_clear(sd);
}


/// Request to set a memo on current map (CZ_REMEMBER_WARPPOINT).
/// 011d
void clif_parse_RequestMemo(int fd,struct map_session_data *sd)
{
	if (!pc_isdead(sd))
		pc_memo(sd,-1);
}


/// Answer to pharmacy item selection dialog (CZ_REQMAKINGITEM).
/// 018e <name id>.W { <material id>.W }*3
void clif_parse_ProduceMix(int fd,struct map_session_data *sd)
{
	const struct PACKET_CZ_REQMAKINGITEM *p = (struct PACKET_CZ_REQMAKINGITEM*)RFIFOP(fd, 0);

	// -1 is used by produce script command.
	switch (sd->menuskill_id) 
	{
		case -1:
		case AM_PHARMACY:
		case SA_CREATECON:
		case RK_RUNEMASTERY:
		case GC_RESEARCHNEWPOISON:
			break;
		default:
			return;
	}

	if (pc_istrading(sd)) {
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skill_id,USESKILL_FAIL_LEVEL,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}

	if (skill_can_produce_mix(sd, p->itemId, sd->menuskill_val, 1))
		skill_produce_mix(sd, 0, p->itemId, p->material[0], p->material[1], p->material[2], 1);

	sd->menuskill_val = sd->menuskill_id = 0;
}

/*==========================================
* To SC_AUTOSHADOWSPELL
*------------------------------------------*/
void clif_parse_SkillSelectMenu(int fd, struct map_session_data *sd)
{
	if( sd->menuskill_id != SC_AUTOSHADOWSPELL )
		return;

	if( pc_istrading(sd) )
	{
		clif_skill_fail(sd,sd->ud.skill_id,USESKILL_FAIL_LEVEL,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	skill_select_menu(sd,RFIFOL(fd,2),RFIFOW(fd,6));
	sd->menuskill_val = sd->menuskill_id = 0;
}

/// Answer to mixing item selection dialog (CZ_REQ_MAKINGITEM).
/// 025b <mk type>.W <name id>.W
/// mk type:
///     1 = cooking
///     2 = arrow
///     3 = elemental
///     4 = GN_MIX_COOKING
///     5 = GN_MAKEBOMB
///     6 = GN_S_PHARMACY
void clif_parse_Cooking(int fd,struct map_session_data *sd)
{
	const struct PACKET_CZ_REQ_MAKINGITEM *p = (struct PACKET_CZ_REQ_MAKINGITEM *)RFIFOP(fd, 0);

	if (p->type == 6 && sd->menuskill_id != GN_MIX_COOKING && sd->menuskill_id != GN_S_PHARMACY)
		return;

	if (sd->menuskill_id != AM_PHARMACY)
		return;

	if (pc_istrading(sd))
	{
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd, sd->ud.skill_id, USESKILL_FAIL_LEVEL, 0, 0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	if (skill_can_produce_mix(sd, p->itemId, sd->menuskill_val, 1))
		skill_produce_mix(sd, (p->type > 1 ? sd->menuskill_id : 0), p->itemId, 0, 0, 0, sd->menuskill_itemused);
	sd->menuskill_val = sd->menuskill_id = sd->menuskill_itemused = 0;
}


/// Answer to repair weapon item selection dialog (CZ_REQ_ITEMREPAIR).
/// 01fd <index>.W <name id>.W <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W
void clif_parse_RepairItem(int fd, struct map_session_data *sd)
{
	const struct PACKET_CZ_REQ_ITEMREPAIR *p = (struct PACKET_CZ_REQ_ITEMREPAIR *)RFIFOP(fd, 0);

	if (sd->menuskill_id != BS_REPAIRWEAPON)
		return;
	if (pc_istrading(sd))
	{
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd, sd->ud.skill_id, USESKILL_FAIL_LEVEL, 0, 0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	skill_repairweapon(sd, p->index);
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Answer to refine weapon item selection dialog (CZ_REQ_WEAPONREFINE).
/// 0222 <index>.L
void clif_parse_WeaponRefine(int fd, struct map_session_data *sd)
{
	int idx;

	if (sd->menuskill_id != WS_WEAPONREFINE) //Packet exploit?
		return;
	if (pc_istrading(sd)) {
		//Make it fail to avoid shop exploits where you sell something different than you see.
		clif_skill_fail(sd,sd->ud.skill_id,USESKILL_FAIL_LEVEL,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}
	idx = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	skill_weaponrefine(sd, idx-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Answer to script menu dialog (CZ_CHOOSE_MENU).
/// 00b8 <npc id>.L <choice>.B
/// choice:
///     1~254 = menu item
///     255   = cancel
/// NOTE: If there were more than 254 items in the list, choice
///     overflows to choice%256.
void clif_parse_NpcSelectMenu(int fd,struct map_session_data *sd)
{
	int npc_id = RFIFOL(fd,2);
	uint8 select = RFIFOB(fd,6);

	if( (select > sd->npc_menu && select != 0xff) || select == 0 )
	{
		TBL_NPC* nd = map_id2nd(npc_id);
		ShowWarning("Invalid menu selection on npc %d:'%s' - got %d, valid range is [%d..%d] (player AID:%d, CID:%d, name:'%s')!\n", npc_id, (nd)?nd->name:"invalid npc id", select, 1, sd->npc_menu, sd->bl.id, sd->status.char_id, sd->status.name);
		clif_GM_kick(NULL,sd);
		return;
	}

	sd->npc_menu = select;
	npc_scriptcont(sd,npc_id,false);
}


/// NPC dialog 'next' click (CZ_REQ_NEXT_SCRIPT).
/// 00b9 <npc id>.L
void clif_parse_NpcNextClicked(int fd,struct map_session_data *sd)
{
	npc_scriptcont(sd,RFIFOL(fd,2),false);
}


/// NPC numeric input dialog value (CZ_INPUT_EDITDLG).
/// 0143 <npc id>.L <value>.L
void clif_parse_NpcAmountInput(int fd,struct map_session_data *sd)
{
	int npcid = RFIFOL(fd,2);
	int amount = (int)RFIFOL(fd,6);

	sd->npc_amount = amount;
	npc_scriptcont(sd, npcid, false);
}


/// NPC text input dialog value (CZ_INPUT_EDITDLGSTR).
/// 01d5 <packet len>.W <npc id>.L <string>.?B
void clif_parse_NpcStringInput(int fd, struct map_session_data* sd)
{
	int message_len = RFIFOW(fd,2)-8;
	int npcid = RFIFOL(fd,4);
	const char* message = (char*)RFIFOP(fd,8);
	
	if( message_len <= 0 )
		return; // invalid input

#if PACKETVER >= 20151029
	// October 2015 and newer needs its length adjusted.
	message_len += 1;
#endif

	safestrncpy(sd->npc_str, message, min(message_len,CHATBOX_SIZE));
	npc_scriptcont(sd, npcid, false);
}


/// NPC dialog 'close' click (CZ_CLOSE_DIALOG).
/// 0146 <npc id>.L
void clif_parse_NpcCloseClicked(int fd,struct map_session_data *sd)
{
	if (!sd->npc_id) //Avoid parsing anything when the script was done with. [Skotlex]
		return; 
	npc_scriptcont(sd,RFIFOL(fd,2),true);
}


/// Answer to identify item selection dialog (CZ_REQ_ITEMIDENTIFY).
/// 0178 <index>.W
/// index:
///     -1 = cancel
void clif_parse_ItemIdentify(int fd,struct map_session_data *sd) {
	short idx = RFIFOW(fd,2);

	if (sd->menuskill_id != MC_IDENTIFY)
		return;
	if (idx < 0 || idx >= MAX_INVENTORY || sd->inventory.u.items_inventory[idx].nameid == 0 || sd->inventory_data[idx] == NULL || sd->inventory.u.items_inventory[idx].identify)
	{// cancel pressed
		sd->menuskill_val = sd->menuskill_id = 0;
		sd->state.workinprogress = WIP_DISABLE_NONE;
		return;
	}
	skill_identify(sd,idx-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Answer to arrow crafting item selection dialog (CZ_REQ_MAKINGARROW).
/// 01ae <name id>.W
void clif_parse_SelectArrow(int fd,struct map_session_data *sd)
{
	switch( sd->menuskill_id )
	{
		case AC_MAKINGARROW:
		case SA_CREATECON:
		case GC_POISONINGWEAPON:
		case WL_READING_SB:
		case NC_MAGICDECOY:
			break;

		default:
			return;
	}

	if( pc_istrading(sd) )
	{ // Make it fail to avoid shop exploits where you sell something different than you see.
 		clif_skill_fail(sd,sd->ud.skill_id,0,0,0);
		sd->menuskill_val = sd->menuskill_id = 0;
		return;
	}

	struct PACKET_CZ_REQ_MAKINGARROW* p = (struct PACKET_CZ_REQ_MAKINGARROW*)RFIFOP(fd, 0);

	switch( sd->menuskill_id )
	{
		case AC_MAKINGARROW:
			skill_arrow_create(sd, p->itemId);
			break;
		case SA_CREATECON:
			skill_produce_mix(sd,SA_CREATECON,p->itemId,0,0,0,1);
			break;
		case GC_POISONINGWEAPON:
			skill_poisoningweapon(sd, p->itemId);
			break;
		case WL_READING_SB:
			skill_spellbook(sd, p->itemId);
			break;
		case NC_MAGICDECOY:
			skill_magicdecoy(sd, p->itemId);
			break;
	}

	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Answer to SA_AUTOSPELL skill selection dialog (CZ_SELECTAUTOSPELL).
/// 01ce <skill id>.L
void clif_parse_AutoSpell(int fd,struct map_session_data *sd) {
	if (sd->menuskill_id != SA_AUTOSPELL)
		return;
	sd->state.workinprogress = WIP_DISABLE_NONE;
	skill_autospell(sd,RFIFOL(fd,2));
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Request to display item carding/composition list (CZ_REQ_ITEMCOMPOSITION_LIST).
/// 017a <card index>.W
void clif_parse_UseCard(int fd,struct map_session_data *sd)
{
	if (sd->state.trading != 0)
		return;
	clif_use_card(sd,RFIFOW(fd,2)-2);
}


/// Answer to carding/composing item selection dialog (CZ_REQ_ITEMCOMPOSITION).
/// 017c <card index>.W <equip index>.W
void clif_parse_InsertCard(int fd,struct map_session_data *sd)
{
	if (sd->state.trading != 0)
		return;
	pc_insert_card(sd,RFIFOW(fd,2)-2,RFIFOW(fd,4)-2);
}


/// Request of character's name by char ID.
/// 0193 <char id>.L (CZ_REQNAME_BYGID)
/// 0369 <char id>.L (CZ_REQNAME_BYGID2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_SolveCharName(int fd, struct map_session_data *sd)
{
	int charid;

	charid = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);
	map_reqnickdb(sd, charid);
}


/// /resetskill /resetstate (CZ_RESET).
/// Request to reset stats or skills.
/// 0197 <type>.W
/// type:
///     0 = state
///     1 = skill
void clif_parse_ResetChar(int fd, struct map_session_data *sd)
{
	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	if( pc_isGM(sd) < get_atcommand_level(atcommand_reset) )
		return;

	if( RFIFOW(fd,2) )
		pc_resetskill(sd,1);
	else
	{
		pc_resetstate(sd);

		if (sd->mission_mobid) {
			sd->mission_mobid = 0;
			sd->mission_count = 0;
			pc_setglobalreg(sd, "TK_MISSION_ID", 0);
		}
	}

	log_atcommand(sd, get_atcommand_level(atcommand_reset), RFIFOW(fd,2) ? "/resetskill" : "/resetstate");
}


/// /lb /nlb (CZ_LOCALBROADCAST).
/// Request to broadcast a message on current map.
/// 019c <packet len>.W <text>.?B
void clif_parse_LocalBroadcast(int fd, struct map_session_data* sd)
{
	char command[CHAT_SIZE_MAX + 16];
	char* msg = (char*)RFIFOP(fd,4);
	unsigned int len = RFIFOW(fd,2)-4;
	int lv;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	if( pc_isGM(sd) < (lv=get_atcommand_level(atcommand_localbroadcast)) )
		return;

	// as the length varies depending on the command used, just block unreasonably long strings
	len = mes_len_check(msg, len, CHAT_SIZE_MAX);

	sprintf(command, "@lkami %s", msg);
	is_atcommand(fd, sd, command, 1);
}


/// Request to move an item from inventory to storage.
/// 00f3 <index>.W <amount>.L (CZ_MOVE_ITEM_FROM_BODY_TO_STORE)
/// 0364 <index>.W <amount>.L (CZ_MOVE_ITEM_FROM_BODY_TO_STORE2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_MoveToKafra(int fd, struct map_session_data *sd)
{
	int item_index, item_amount;

	if (pc_istrading(sd))
		return;
	
	item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-2;
	item_amount = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);
	if (item_index < 0 || item_index >= MAX_INVENTORY || item_amount < 1)
		return;
	if (sd->inventory.u.items_inventory[item_index].equipSwitch) {
		clif_msg(sd, C_ITEM_EQUIP_SWITCH);
		return;
	}

	if (sd->state.storage_flag == 1)
		storage_storageadd(sd, &sd->storage, item_index, item_amount);
	else if (sd->state.storage_flag == 2)
		storage_guild_storageadd(sd, item_index, item_amount);
	else if (sd->state.storage_flag == 3)
		storage_storageadd(sd, &sd->storage2, item_index, item_amount);
}


/// Request to move an item from storage to inventory.
/// 00f5 <index>.W <amount>.L (CZ_MOVE_ITEM_FROM_STORE_TO_BODY)
/// 0365 <index>.W <amount>.L (CZ_MOVE_ITEM_FROM_STORE_TO_BODY2)
/// There are various variants of this packet, some of them have padding between fields.
void clif_parse_MoveFromKafra(int fd,struct map_session_data *sd)
{
	int item_index, item_amount;

	item_index = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0])-1;
	item_amount = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1]);

	if (sd->state.storage_flag == 1)
		storage_storageget(sd, &sd->storage, item_index, item_amount);
	else if(sd->state.storage_flag == 2)
		storage_guild_storageget(sd, item_index, item_amount);
	else if (sd->state.storage_flag == 3)
		storage_storageget(sd, &sd->storage2, item_index, item_amount);
}


/// Request to move an item from cart to storage (CZ_MOVE_ITEM_FROM_CART_TO_STORE).
/// 0129 <index>.W <amount>.L
void clif_parse_MoveToKafraFromCart(int fd, struct map_session_data *sd)
{
	if( sd->state.vending )
		return;
	if (!pc_iscarton(sd))
		return;

	int idx = RFIFOW(fd, 2) - 2;
	int amount = RFIFOL(fd, 4);

	if (idx < 0 || idx >= MAX_INVENTORY || amount < 1)
		return;
	if (sd->inventory.u.items_inventory[idx].equipSwitch) {
		clif_msg(sd, C_ITEM_EQUIP_SWITCH);
		return;
	}

	if (sd->state.storage_flag == 1)
		storage_storageaddfromcart(sd, &sd->storage, idx, amount);
	else if (sd->state.storage_flag == 2)
		storage_guild_storageaddfromcart(sd, idx, amount);
	else if (sd->state.storage_flag == 3)
		storage_storageaddfromcart(sd, &sd->storage2, idx, amount);
}


/// Request to move an item from storage to cart (CZ_MOVE_ITEM_FROM_STORE_TO_CART).
/// 0128 <index>.W <amount>.L
void clif_parse_MoveFromKafraToCart(int fd, struct map_session_data *sd)
{
	int idx = RFIFOW(fd, 2) - 1;
	int amount = RFIFOL(fd, 4);

	if( sd->state.vending )
		return;
	if (!pc_iscarton(sd))
		return;

	if (sd->state.storage_flag == 1)
		storage_storagegettocart(sd, &sd->storage, idx, amount);
	else if (sd->state.storage_flag == 2)
		storage_guild_storagegettocart(sd, idx, amount);
	else if (sd->state.storage_flag == 3)
		storage_storagegettocart(sd, &sd->storage2, idx, amount);
}


/// Request to close storage (CZ_CLOSE_STORE).
/// 00f7
void clif_parse_CloseKafra(int fd, struct map_session_data *sd)
{
	if( sd->state.storage_flag == 1 )
		storage_storageclose(sd);
	else if( sd->state.storage_flag == 2 )
		storage_guild_storageclose(sd);
	else if (sd->state.storage_flag == 3)
		storage_storage2_close(sd);
}


/// Displays kafra storage password dialog (ZC_REQ_STORE_PASSWORD).
/// 023a <info>.W
/// info:
///     0 = password has not been set yet
///     1 = storage is password-protected
///     8 = too many wrong passwords
///     ? = ignored
/// NOTE: This packet is only available on certain non-kRO clients.
void clif_storagepassword(struct map_session_data* sd, short info)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x23a));
	WFIFOW(fd,0) = 0x23a;
	WFIFOW(fd,2) = info;
	WFIFOSET(fd,packet_len(0x23a));
}


/// Answer to the kafra storage password dialog (CZ_ACK_STORE_PASSWORD).
/// 023b <type>.W <password>.16B <new password>.16B
/// type:
///     2 = change password
///     3 = check password
/// NOTE: This packet is only available on certain non-kRO clients.
void clif_parse_StoragePassword(int fd, struct map_session_data *sd)
{
	//TODO
}


/// Result of kafra storage password validation (ZC_RESULT_STORE_PASSWORD).
/// 023c <result>.W <error count>.W
/// result:
///     4 = password change success
///     5 = password change failure
///     6 = password check success
///     7 = password check failure
///     8 = too many wrong passwords
///     ? = ignored
/// NOTE: This packet is only available on certain non-kRO clients.
void clif_storagepassword_result(struct map_session_data* sd, short result, short error_count)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x23c));
	WFIFOW(fd,0) = 0x23c;
	WFIFOW(fd,2) = result;
	WFIFOW(fd,4) = error_count;
	WFIFOSET(fd,packet_len(0x23c));
}


/// Party creation request
/// 00f9 <party name>.24B (CZ_MAKE_GROUP)
/// 01e8 <party name>.24B <item pickup rule>.B <item share rule>.B (CZ_MAKE_GROUP2)
void clif_parse_CreateParty(int fd, struct map_session_data *sd)
{
	char* name = (char*)RFIFOP(fd,2);
	name[NAME_LENGTH-1] = '\0';

	if( map[sd->bl.m].flag.partylock )
	{// Party locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}
	if( battle_config.basic_skill_check && pc_checkskill(sd,NV_BASIC) < 7 && pc_checkskill(sd, SU_BASIC_SKILL) < 1)
	{
		clif_skill_fail(sd,1,USESKILL_FAIL_LEVEL,4,0);
		return;
	}

	party_create(sd,name,0,0);
}

void clif_parse_CreateParty2(int fd, struct map_session_data *sd)
{
	char* name = (char*)RFIFOP(fd,2);
	int item1 = RFIFOB(fd,26);
	int item2 = RFIFOB(fd,27);
	name[NAME_LENGTH-1] = '\0';

	if( map[sd->bl.m].flag.partylock )
	{// Party locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}
	if( battle_config.basic_skill_check && pc_checkskill(sd,NV_BASIC) < 7 && pc_checkskill(sd, SU_BASIC_SKILL) < 1)
	{
		clif_skill_fail(sd,1,USESKILL_FAIL_LEVEL,4,0);
		return;
	}

	party_create(sd,name,item1,item2);
}


/// Party invitation request
/// 00fc <account id>.L (CZ_REQ_JOIN_GROUP)
void clif_parse_PartyInvite(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	if(map[sd->bl.m].flag.partylock)
	{// Party locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	if(t_sd && t_sd->state.noask)
	{// @noask [LuzZza]
		clif_noask_sub(sd, t_sd, 1);
		return;
	}
	
	party_invite(sd, t_sd);
}

/// Party invitation request
/// 02c4 <char name>.24B (CZ_PARTY_JOIN_REQ)
void clif_parse_PartyInvite2(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	char *name = (char*)RFIFOP(fd,2);
	name[NAME_LENGTH-1] = '\0';

	if(map[sd->bl.m].flag.partylock)
	{// Party locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}

	t_sd = map_nick2sd(name);

	if(t_sd && t_sd->state.noask)
	{// @noask [LuzZza]
		clif_noask_sub(sd, t_sd, 1);
		return;
	}
	
	party_invite(sd, t_sd);
}


/// Party invitation reply
/// 00ff <party id>.L <flag>.L (CZ_JOIN_GROUP)
/// flag:
///     0 = reject
///     1 = accept
void clif_parse_ReplyPartyInvite(int fd,struct map_session_data *sd)
{
	party_reply_invite(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}

/// Party invitation reply
/// 02c7 <party id>.L <flag>.B (CZ_PARTY_JOIN_REQ_ACK)
///     0 = reject
///     1 = accept
void clif_parse_ReplyPartyInvite2(int fd,struct map_session_data *sd)
{
	party_reply_invite(sd,RFIFOL(fd,2),RFIFOB(fd,6));
}


/// Request to leave party (CZ_REQ_LEAVE_GROUP).
/// 0100
void clif_parse_LeaveParty(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}
	party_leave(sd);
}


/// Request to expel a party member (CZ_REQ_EXPEL_GROUP_MEMBER).
/// 0103 <account id>.L <char name>.24B
void clif_parse_RemovePartyMember(int fd, struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.partylock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,227));
		return;
	}
	party_removemember(sd,RFIFOL(fd,2),(char*)RFIFOP(fd,6));
}


/// Request to change party options.
/// 0102 <exp share rule>.L (CZ_CHANGE_GROUPEXPOPTION)
/// 07d7 <exp share rule>.L <item pickup rule>.B <item share rule>.B (CZ_GROUPINFO_CHANGE_V2)
void clif_parse_PartyChangeOption(int fd, struct map_session_data *sd)
{
	struct party_data *p;
	int i;

	if( !sd->status.party_id )
		return;

	p = party_search(sd->status.party_id);
	if( p == NULL )
		return;

	ARR_FIND( 0, MAX_PARTY, i, p->data[i].sd == sd );
	if( i == MAX_PARTY )
		return; //Shouldn't happen

	if( !p->party.member[i].leader )
		return;

#if PACKETVER < 20090603
	//Client can't change the item-field
	party_changeoption(sd, RFIFOL(fd,2), p->party.item);
#else
	party_changeoption(sd, RFIFOL(fd,2), ((RFIFOB(fd,6)?1:0)|(RFIFOB(fd,7)?2:0)));
#endif
}


/// Validates and processes party messages (CZ_REQUEST_CHAT_PARTY).
/// 0108 <packet len>.W <text>.?B (<name> : <message>) 00
void clif_parse_PartyMessage(int fd, struct map_session_data* sd)
{
	char name[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	// validate packet and retrieve name and message
	if (!clif_process_message(sd, false, name, message, output))
		return;

	// Reset idle time when using party chat.
	sd->idletime = last_tick;

	party_send_message(sd, output, strlen(output) + 1);
}


/// Changes Party Leader (CZ_CHANGE_GROUP_MASTER).
/// 07da <account id>.L
void clif_parse_PartyChangeLeader(int fd, struct map_session_data* sd)
{
	party_changeleader(sd, map_id2sd(RFIFOL(fd,2)));
}


/// Party Booking in KRO [Spiria]
///

/// Request to register a party booking advertisment (CZ_PARTY_BOOKING_REQ_REGISTER).
/// 0802 <level>.W <map id>.W { <job>.W }*6
void clif_parse_PartyBookingRegisterReq(int fd, struct map_session_data* sd)
{
	short level = RFIFOW(fd,2);
	short mapid = RFIFOW(fd,4);
	short job[PARTY_BOOKING_JOBS];
	int i;
	
	for(i=0; i<PARTY_BOOKING_JOBS; i++)
		job[i] = RFIFOB(fd,6+i*2);

	party_booking_register(sd, level, mapid, job);
}


/// Result of request to register a party booking advertisment (ZC_PARTY_BOOKING_ACK_REGISTER).
/// 0803 <result>.W
/// result:
///     0 = success
///     1 = failure
///     2 = already registered
void clif_PartyBookingRegisterAck(struct map_session_data *sd, int flag)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x803));
	WFIFOW(fd,0) = 0x803;
	WFIFOW(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x803));
}


/// Request to search for party booking advertisments (CZ_PARTY_BOOKING_REQ_SEARCH).
/// 0804 <level>.W <map id>.W <job>.W <last index>.L <result count>.W
void clif_parse_PartyBookingSearchReq(int fd, struct map_session_data* sd)
{	
	short level = RFIFOW(fd,2);
	short mapid = RFIFOW(fd,4);
	short job = RFIFOW(fd,6);
	unsigned long lastindex = RFIFOL(fd,8);
	short resultcount = RFIFOW(fd,12);

	party_booking_search(sd, level, mapid, job, lastindex, resultcount);
}


/// Party booking search results (ZC_PARTY_BOOKING_ACK_SEARCH).
/// 0805 <packet len>.W <more results>.B { <index>.L <char name>.24B <expire time>.L <level>.W <map id>.W { <job>.W }*6 }*
/// more results:
///     0 = no
///     1 = yes
void clif_PartyBookingSearchAck(int fd, struct party_booking_ad_info** results, int count, bool more_result)
{
	int i, j;
	int size = sizeof(struct party_booking_ad_info); // structure size (48)
	struct party_booking_ad_info *pb_ad;
	WFIFOHEAD(fd,size*count + 5);
	WFIFOW(fd,0) = 0x805;
	WFIFOW(fd,2) = size*count + 5;
	WFIFOB(fd,4) = more_result;
	for(i=0; i<count; i++)
	{
		pb_ad = results[i];
		WFIFOL(fd,i*size+5) = pb_ad->index;
		safestrncpy(WFIFOP(fd,i*size+9),pb_ad->charname,NAME_LENGTH);
		WFIFOL(fd,i*size+33) = pb_ad->starttime;  // FIXME: This is expire time
		WFIFOW(fd,i*size+37) = pb_ad->p_detail.level;
		WFIFOW(fd,i*size+39) = pb_ad->p_detail.mapid;
		for(j=0; j<PARTY_BOOKING_JOBS; j++)
			WFIFOW(fd,i*size+41+j*2) = pb_ad->p_detail.job[j];
	}
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Request to delete own party booking advertisment (CZ_PARTY_BOOKING_REQ_DELETE).
/// 0806
void clif_parse_PartyBookingDeleteReq(int fd, struct map_session_data* sd)
{
	if(party_booking_delete(sd))
		clif_PartyBookingDeleteAck(sd, 0);
}


/// Result of request to delete own party booking advertisment (ZC_PARTY_BOOKING_ACK_DELETE).
/// 0807 <result>.W
/// result:
///     0 = success
///     1 = success (auto-removed expired ad)
///     2 = failure
///     3 = nothing registered
void clif_PartyBookingDeleteAck(struct map_session_data* sd, int flag)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x807));
	WFIFOW(fd,0) = 0x807;
	WFIFOW(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x807));
}


/// Request to update party booking advertisment (CZ_PARTY_BOOKING_REQ_UPDATE).
/// 0808 { <job>.W }*6
void clif_parse_PartyBookingUpdateReq(int fd, struct map_session_data* sd)
{
	short job[PARTY_BOOKING_JOBS];
	int i;
	
	for(i=0; i<PARTY_BOOKING_JOBS; i++)
		job[i] = RFIFOW(fd,2+i*2);

	party_booking_update(sd, job);
}


/// Notification about new party booking advertisment (ZC_PARTY_BOOKING_NOTIFY_INSERT).
/// 0809 <index>.L <char name>.24B <expire time>.L <level>.W <map id>.W { <job>.W }*6
void clif_PartyBookingInsertNotify(struct map_session_data* sd, struct party_booking_ad_info* pb_ad)
{
	int i;
	uint8 buf[38+PARTY_BOOKING_JOBS*2];

	if(pb_ad == NULL) return;

	WBUFW(buf,0) = 0x809;
	WBUFL(buf,2) = pb_ad->index;
	safestrncpy(WBUFP(buf,6),pb_ad->charname,NAME_LENGTH);
	WBUFL(buf,30) = pb_ad->starttime;  // FIXME: This is expire time
	WBUFW(buf,34) = pb_ad->p_detail.level;
	WBUFW(buf,36) = pb_ad->p_detail.mapid;
	for(i=0; i<PARTY_BOOKING_JOBS; i++)
		WBUFW(buf,38+i*2) = pb_ad->p_detail.job[i];
	
	clif_send(buf, packet_len(0x809), &sd->bl, ALL_CLIENT);
}


/// Notification about updated party booking advertisment (ZC_PARTY_BOOKING_NOTIFY_UPDATE).
/// 080a <index>.L { <job>.W }*6
void clif_PartyBookingUpdateNotify(struct map_session_data* sd, struct party_booking_ad_info* pb_ad)
{
	int i;
	uint8 buf[6+PARTY_BOOKING_JOBS*2];

	if(pb_ad == NULL) return;

	WBUFW(buf,0) = 0x80a;
	WBUFL(buf,2) = pb_ad->index;
	for(i=0; i<PARTY_BOOKING_JOBS; i++)
		WBUFW(buf,6+i*2) = pb_ad->p_detail.job[i];
	clif_send(buf,packet_len(0x80a),&sd->bl,ALL_CLIENT); // Now UPDATE all client.
}


/// Notification about deleted party booking advertisment (ZC_PARTY_BOOKING_NOTIFY_DELETE).
/// 080b <index>.L
void clif_PartyBookingDeleteNotify(struct map_session_data* sd, int index)
{
	uint8 buf[6];

	WBUFW(buf,0) = 0x80b;
	WBUFL(buf,2) = index;
	
	clif_send(buf, packet_len(0x80b), &sd->bl, ALL_CLIENT); // Now UPDATE all client.
}


/// Request to close own vending (CZ_REQ_CLOSESTORE).
/// 012e
void clif_parse_CloseVending(int fd, struct map_session_data* sd)
{
	vending_closevending(sd);
}


/// Request to open a vending shop (CZ_REQ_BUY_FROMMC).
/// 0130 <account id>.L
void clif_parse_VendingListReq(int fd, struct map_session_data* sd)
{
	if( sd->npc_id )
	{// using an NPC
		return;
	}
	vending_vendinglistreq(sd,RFIFOL(fd,2));
}


/// Shop item(s) purchase request (CZ_PC_PURCHASE_ITEMLIST_FROMMC).
/// 0134 <packet len>.W <account id>.L { <amount>.W <index>.W }*
void clif_parse_PurchaseReq(int fd, struct map_session_data* sd)
{
	int len = (int)RFIFOW(fd,2) - 8;
	int id = (int)RFIFOL(fd,4);
	const uint8* data = (uint8*)RFIFOP(fd,8);

	vending_purchasereq(sd, id, sd->vended_id, data, len/4);

	// whether it fails or not, the buy window is closed
	sd->vended_id = 0;
}


/// Shop item(s) purchase request (CZ_PC_PURCHASE_ITEMLIST_FROMMC2).
/// 0801 <packet len>.W <account id>.L <unique id>.L { <amount>.W <index>.W }*
void clif_parse_PurchaseReq2(int fd, struct map_session_data* sd) {
	int len = (int)RFIFOW(fd,2) - 12;
	int aid = (int)RFIFOL(fd,4);
	int uid = (int)RFIFOL(fd,8);
	const uint8* data = (uint8*)RFIFOP(fd,12);

	vending_purchasereq(sd, aid, uid, data, len/4);

	// whether it fails or not, the buy window is closed
	sd->vended_id = 0;
}


/// Confirm or cancel the shop preparation window.
/// 012f <packet len>.W <shop name>.80B { <index>.W <amount>.W <price>.L }* (CZ_REQ_OPENSTORE)
/// 01b2 <packet len>.W <shop name>.80B <result>.B { <index>.W <amount>.W <price>.L }* (CZ_REQ_OPENSTORE2)
/// result:
///     0 = canceled
///     1 = open
void clif_parse_OpenVending(int fd, struct map_session_data* sd) {
	int cmd = RFIFOW(fd, 0);
	struct s_packet_db* info = &packet_db[sd->packet_ver][cmd];
	short len = (short)RFIFOW(fd, info->pos[0]);
	const char* message = RFIFOCP(fd, info->pos[1]);
	const uint8* data = (uint8*)RFIFOP(fd, info->pos[3]);
	
	if (len < 0)
		return;

	if (cmd == 0x12f) { // (CZ_REQ_OPENSTORE)
		len -= 84;
	}
	else { //(CZ_REQ_OPENSTORE2)
		bool flag;

		len -= 85;
		flag = (bool)RFIFOB(fd, info->pos[2]);
		if (!flag) {
			sd->state.prevend = 0;
			sd->state.workinprogress = WIP_DISABLE_NONE;
		}
	}

	if (sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOROOM) {
		return;
	}
	if( map[sd->bl.m].flag.novending ) {
		clif_displaymessage (sd->fd, msg_txt(sd,276)); // "You can't open a shop on this map"
		return;
	}
	if( map_getcell(sd->bl.m,sd->bl.x,sd->bl.y,CELL_CHKNOVENDING) ) {
		clif_displaymessage (sd->fd, msg_txt(sd,204)); // "You can't open a shop on this cell."
		return;
	}

	if (message[0] == '\0') {// invalid input
		return;
	}

	vending_openvending(sd, message, data, len/8);
}


/// Guild creation request (CZ_REQ_MAKE_GUILD).
/// 0165 <char id>.L <guild name>.24B
void clif_parse_CreateGuild(int fd,struct map_session_data *sd)
{
	char* name = (char*)RFIFOP(fd,6);
	name[NAME_LENGTH-1] = '\0';

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}

	if (!(battle_config.guild_create&1)) {
		clif_disp_overheadcolor_self(fd, COLOR_RED, msg_txt(sd,717));
		return;
	}

	if (sd->clan) // Should display a clientside message "You are currently joined in Clan !!" so we ignore it
		return;

	guild_create(sd, name);
}


/// Request for guild window interface permissions (CZ_REQ_GUILD_MENUINTERFACE).
/// 014d
void clif_parse_GuildCheckMaster(int fd, struct map_session_data *sd)
{
	clif_guild_masterormember(sd);
}


/// Request for guild window information (CZ_REQ_GUILD_MENU).
/// 014f <type>.L
/// type:
///     0 = basic info
///     1 = member manager
///     2 = positions
///     3 = skills
///     4 = expulsion list
///     5 = unknown (GM_ALLGUILDLIST)
///     6 = notice
void clif_parse_GuildRequestInfo(int fd, struct map_session_data *sd)
{
	if( !sd->status.guild_id && !sd->bg_id )
		return;

	switch( RFIFOL(fd,2) )
	{
	case 0:	// ギルド基本情報、同盟敵対情報
		clif_guild_basicinfo(sd);
		clif_guild_allianceinfo(sd);
		break;
	case 1:	// メンバーリスト、役職名リスト
		clif_guild_positionnamelist(sd);
		clif_guild_memberlist(sd);
		break;
	case 2:	// 役職名リスト、役職情報リスト
		clif_guild_positionnamelist(sd);
		clif_guild_positioninfolist(sd);
		break;
	case 3:	// スキルリスト
		clif_guild_skillinfo(sd);
		break;
	case 4:	// 追放リスト
		clif_guild_expulsionlist(sd);
		break;
	default:
		ShowError("clif: guild request info: unknown type %d\n", RFIFOL(fd,2));
		break;
	}
}


/// Request to update guild positions (CZ_REG_CHANGE_GUILD_POSITIONINFO).
/// 0161 <packet len>.W { <position id>.L <mode>.L <ranking>.L <pay rate>.L <name>.24B }*
void clif_parse_GuildChangePositionInfo(int fd, struct map_session_data *sd)
{
	const unsigned int blocksize = 40;
	unsigned int packet_len, count, i;
	char name[NAME_LENGTH];
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	if( !sd->state.gmaster_flag )
		return;

	packet_len = RFIFOW(fd,info->pos[0]);
	packet_len-= info->pos[1];

	if( packet_len%blocksize )
	{
		ShowError("clif_parse_GuildChangePositionInfo: Unexpected position list size %u (account_id=%d, block size=%u)\n", packet_len, sd->bl.id, blocksize);
		return;
	}
	count = packet_len/blocksize;

	for( i = 0; i < count; i++ )
	{// FIXME: The list should be sent as a whole (see clif_guild_positionchanged)
		safestrncpy(name, (const char*)RFIFOP(fd,info->pos[1]+i*blocksize+16), sizeof(name));

		guild_change_position(sd->status.guild_id, RFIFOL(fd,info->pos[1]+i*blocksize), RFIFOL(fd,info->pos[1]+i*blocksize+4), RFIFOL(fd,info->pos[1]+i*blocksize+12), name);
	}
}


/// Request to update the position of guild members (CZ_REQ_CHANGE_MEMBERPOS).
/// 0155 <packet len>.W { <account id>.L <char id>.L <position id>.L }*
void clif_parse_GuildChangeMemberPosition(int fd, struct map_session_data *sd)
{
	int i;
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd, 0)];
	int len = RFIFOW(fd, info->pos[0]);

	if (!sd->state.gmaster_flag)
		return;

	// Guild leadership change.
	// A sent position change of 0 triggers a change in guild ownership.
	if (len == 16 && RFIFOL(fd, 12) == 0)
	{
		struct map_session_data *tsd = map_charid2sd(RFIFOL(fd, 8));

		if (!battle_config.guild_leaderchange_woe && is_agit_start()) {
			clif_msg(sd, GUILD_MASTER_WOE);
			return;
		}

		if (battle_config.guild_leaderchange_delay && DIFF_TICK(time(NULL), sd->guild->last_leader_change) < battle_config.guild_leaderchange_delay) {
			clif_msg(sd, GUILD_MASTER_DELAY);
			return;
		}

		if (map[sd->bl.m].flag.guildlock){
			clif_displaymessage(fd, "You can't change guild leaders on this map.");
			return;
		}

		if (tsd == NULL || tsd->status.guild_id != sd->status.guild_id){
			clif_displaymessage(fd, "Targeted character must be a online guildmate.");
			return;
		}

		guild_gm_change(sd->status.guild_id, RFIFOL(fd, 8));
		return;// End it here since position change already happened.
	}

	for (i = info->pos[1]; i<len; i += 12){
		int position = RFIFOL(fd, i + 8);

		if (position > 0)
			guild_change_memberposition(sd->status.guild_id, RFIFOL(fd, i), RFIFOL(fd, i + 4), position);
	}
}


/// Request for guild emblem data (CZ_REQ_GUILD_EMBLEM_IMG).
/// 0151 <guild id>.L
void clif_parse_GuildRequestEmblem(int fd,struct map_session_data *sd)
{
	struct guild* g;
	int guild_id = RFIFOL(fd,2);

	if( (g = guild_search(guild_id)) != NULL )
		clif_guild_emblem(sd,g);
}

/// Validates data of a guild emblem (compressed bitmap)
enum e_result_validate_emblem {	// Used as Result for clif_validate_emblem
	EMBVALIDATE_SUCCESS = 0,
	EMBVALIDATE_ERR_RAW_FILEFORMAT,	// Invalid File Format (Error in zlib/decompression or malformed BMP header)
	EMBVALIDATE_ERR_TRANSPARENCY	// uploaded emblem does not met the requirements of battle_config.emblem_transparency_limit
};

static enum e_result_validate_emblem clif_validate_emblem(const uint8* emblem, unsigned long emblem_len)
{
	uint8 buf[1800];  // no well-formed emblem bitmap is larger than 1782 (24 bit) / 1654 (8 bit) bytes
	unsigned long buf_len = sizeof(buf);
	int offset = 0;

	if (!((decode_zip(buf, &buf_len, emblem, emblem_len) == 0 && buf_len >= 18)  // sizeof(BITMAPFILEHEADER) + sizeof(biSize) of the following info header struct
		&& RBUFW(buf, 0) == 0x4d42   // BITMAPFILEHEADER.bfType (signature)
		&& RBUFL(buf, 2) == buf_len  // BITMAPFILEHEADER.bfSize (file size)
		&& (offset = RBUFL(buf, 10)) < buf_len  // BITMAPFILEHEADER.bfOffBits (offset to bitmap bits)
		))
		return EMBVALIDATE_ERR_RAW_FILEFORMAT;

	if (battle_config.emblem_transparency_limit != 100) {
		int i, transcount = 1, tmp[3];
		for (i = offset; i < buf_len - 1; i++) {
			int j = i % 3;
			tmp[j] = RBUFL(buf, i);
			if (j == 2 && (tmp[0] == 0xFFFF00FF) && (tmp[1] == 0xFFFF00) && (tmp[2] == 0xFF00FFFF)) //if pixel is transparent
				transcount++;
		}
		if (((transcount * 300) / (buf_len - offset)) > battle_config.emblem_transparency_limit) //convert in % to chk
			return EMBVALIDATE_ERR_TRANSPARENCY;
	}

	return EMBVALIDATE_SUCCESS;
}

/// Request to update the guild emblem (CZ_REGISTER_GUILD_EMBLEM_IMG).
/// 0153 <packet len>.W <emblem data>.?B
void clif_parse_GuildChangeEmblem(int fd, struct map_session_data *sd) {
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd, 0)];
	unsigned long emblem_len = RFIFOW(fd, info->pos[0]) - 4;
	const uint8* emblem = RFIFOP(fd, info->pos[1]);
	int emb_val = 0;

	if (!emblem_len || !sd->state.gmaster_flag)
		return;

	if (!(battle_config.emblem_woe_change) && is_agit_start()) {
		clif_displaymessagecolor(sd, msg_txt(sd, 411), COLOR_RED); //"You not allowed to change emblem during woe"
		return;
	}
	emb_val = clif_validate_emblem(emblem, emblem_len);
	if (emb_val == EMBVALIDATE_ERR_RAW_FILEFORMAT) {
		ShowWarning("clif_parse_GuildChangeEmblem: Rejected malformed guild emblem (size=%lu, accound_id=%d, char_id=%d, guild_id=%d).\n", emblem_len, sd->status.account_id, sd->status.char_id, sd->status.guild_id);
		clif_displaymessagecolor(sd, msg_txt(sd, 412), COLOR_RED); //"The chosen emblem was detected invalid"
		return;
	}
	else if (emb_val == EMBVALIDATE_ERR_TRANSPARENCY) {
		char output[128];
		safesnprintf(output, sizeof(output), msg_txt(sd, 413), battle_config.emblem_transparency_limit);
		clif_displaymessagecolor(sd, output, COLOR_RED); //"The chosen emblem was detected invalid as it contain too much transparency (limit=%d)"
		return;
	}

	guild_change_emblem(sd, emblem_len, (const char*)emblem);
}


/// Guild notice update request (CZ_GUILD_NOTICE).
/// 016e <guild id>.L <msg1>.60B <msg2>.120B
void clif_parse_GuildChangeNotice(int fd, struct map_session_data* sd)
{
	int guild_id = RFIFOL(fd,2);
	char* msg1 = (char*)RFIFOP(fd,6);
	char* msg2 = (char*)RFIFOP(fd,66);

	if(!sd->state.gmaster_flag)
		return;

	// compensate for some client defects when using multilanguage mode
	if (msg1[0] == '|' && msg1[3] == '|') msg1+= 3; // skip duplicate marker
	if (msg2[0] == '|' && msg2[3] == '|') msg2+= 3; // skip duplicate marker
	if (msg2[0] == '|') msg2[strnlen(msg2, MAX_GUILDMES2)-1] = '\0'; // delete extra space at the end of string

	guild_change_notice(sd, guild_id, msg1, msg2);
}

// Helper function for guild invite functions
int clif_sub_guild_invite(int fd, struct map_session_data *sd, struct map_session_data *t_sd) {
	if (t_sd == NULL) {// not online or does not exist
		return 1;
	}

	if (map[sd->bl.m].flag.guildlock) {//Guild locked.
		clif_displaymessage(fd, msg_txt(sd, 228));
		return 1;
	}

	if (t_sd && t_sd->state.noask) {// @noask [LuzZza]
		clif_noask_sub(sd, t_sd, 2);
		return 1;
	}

	// Players in a clan can not join a guild
	if (t_sd && t_sd->clan)
		return 1;

	guild_invite(sd, t_sd);
	return 0;
}

/// Guild invite request (CZ_REQ_JOIN_GUILD).
/// 0168 <account id>.L <inviter account id>.L <inviter char id>.L
void clif_parse_GuildInvite(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd = map_id2sd(RFIFOL(fd, 2));

	if (clif_sub_guild_invite(fd, sd, t_sd))
		return;
}

/// Guild invite request (/guildinvite)
/// 0916 <char name>.24B (CZ_REQ_JOIN_GUILD2)
void clif_parse_GuildInvite2(int fd, struct map_session_data *sd) {
	struct map_session_data *t_sd = map_nick2sd((char *)RFIFOP(fd, 2));

	// @noask [LuzZza]
	if (clif_sub_guild_invite(fd, sd, t_sd))
		return;
}


/// Answer to guild invitation (CZ_JOIN_GUILD).
/// 016b <guild id>.L <answer>.L
/// answer:
///     0 = refuse
///     1 = accept
void clif_parse_GuildReplyInvite(int fd,struct map_session_data *sd)
{
	guild_reply_invite(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}


/// Request to leave guild (CZ_REQ_LEAVE_GUILD).
/// 0159 <guild id>.L <account id>.L <char id>.L <reason>.40B
void clif_parse_GuildLeave(int fd,struct map_session_data *sd)
{
	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}
	if( sd->bg_id )
	{
		clif_displaymessage(fd, msg_txt(sd,520)); //"You can't leave battleground guilds."
		return;
	}

	guild_leave(sd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14));
}


/// Request to expel a member of a guild (CZ_REQ_BAN_GUILD).
/// 015b <guild id>.L <account id>.L <char id>.L <reason>.40B
void clif_parse_GuildExpulsion(int fd,struct map_session_data *sd)
{
	if( map[sd->bl.m].flag.guildlock || sd->bg_id )
	{ // Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}
	guild_expulsion(sd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14));
}


/// Validates and processes guild messages (CZ_GUILD_CHAT).
/// 017e <packet len>.W <text>.?B (<name> : <message>) 00
void clif_parse_GuildMessage(int fd, struct map_session_data* sd)
{
	char name[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	// validate packet and retrieve name and message
	if (!clif_process_message(sd, false, name, message, output))
		return;
	
	// Reset idle time when using guild chat.
	sd->idletime = last_tick;

	if (sd->bg_id)
		bg_send_message(sd, output, strlen(output));
	else
		guild_send_message(sd, output, strlen(output));
}


/// Guild alliance request (CZ_REQ_ALLY_GUILD).
/// 0170 <account id>.L <inviter account id>.L <inviter char id>.L
void clif_parse_GuildRequestAlliance(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;
	
	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 3);
		return;
	}
	
	guild_reqalliance(sd,t_sd);
}


/// Answer to a guild alliance request (CZ_ALLY_GUILD).
/// 0172 <inviter account id>.L <answer>.L
/// answer:
///     0 = refuse
///     1 = accept
void clif_parse_GuildReplyAlliance(int fd, struct map_session_data *sd)
{
	guild_reply_reqalliance(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}


/// Request to delete a guild alliance or opposition (CZ_REQ_DELETE_RELATED_GUILD).
/// 0183 <opponent guild id>.L <relation>.L
/// relation:
///     0 = Ally
///     1 = Enemy
void clif_parse_GuildDelAlliance(int fd, struct map_session_data *sd)
{
	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}
	guild_delalliance(sd,RFIFOL(fd,2),RFIFOL(fd,6));
}


/// Request to set a guild as opposition (CZ_REQ_HOSTILE_GUILD).
/// 0180 <account id>.L
void clif_parse_GuildOpposition(int fd, struct map_session_data *sd)
{
	struct map_session_data *t_sd;

	if(!sd->state.gmaster_flag)
		return;

	if(map[sd->bl.m].flag.guildlock)
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}

	t_sd = map_id2sd(RFIFOL(fd,2));

	// @noask [LuzZza]
	if(t_sd && t_sd->state.noask) {
		clif_noask_sub(sd, t_sd, 4);
		return;
	}
	
	guild_opposition(sd,t_sd);
}


/// Request to delete own guild (CZ_REQ_DISORGANIZE_GUILD).
/// 015d <key>.40B
/// key:
///     now guild name; might have been (intended) email, since the
///     field name and size is same as the one in CH_DELETE_CHAR.
void clif_parse_GuildBreak(int fd, struct map_session_data *sd)
{
	if( map[sd->bl.m].flag.guildlock )
	{	//Guild locked.
		clif_displaymessage(fd, msg_txt(sd,228));
		return;
	}
	if (!(battle_config.guild_break&1)) {
		clif_disp_overheadcolor_self(fd, COLOR_RED, msg_txt(sd,718));
		return;
	}
	guild_break(sd,(char*)RFIFOP(fd,2));
}

/// Sends a clan message to a player (ZC_NOTIFY_CLAN_CHAT)
/// 098e <length>.W <name>.24B <message>.?B 
void clif_clan_message(struct clan *clan, const char *mes, int len)
{
#if PACKETVER >= 20131223
	struct map_session_data *sd;
	uint8 buf[256];

	if (len == 0)
		return;
	else if (len > (sizeof(buf) - 5 - NAME_LENGTH))
	{
		ShowWarning("clif_clan_message: Truncated message '%s' (len=%d, max=%d, clan_id=%d).\n", mes, len, sizeof(buf) - 5, clan->id);
		len = sizeof(buf) - 5 - NAME_LENGTH;
	}

	WBUFW(buf, 0) = 0x98e;
	WBUFW(buf, 2) = len + 5 + NAME_LENGTH;
	
	// Offially the sender name should also be filled here, but it is not required by the client and since it's in the message too we do not fill it
	//safestrncpy(WBUFCP(buf,4), sendername, NAME_LENGTH);
	safestrncpy(WBUFCP(buf, 4 + NAME_LENGTH), mes, len + 1);

	if ((sd = clan_getavailablesd(clan)) != NULL)
		clif_send(buf, WBUFW(buf, 2), &sd->bl, CLAN);
#endif
}

/// Parses a clan message from a player. (CZ_CLAN_CHAT)
/// 098d <length>.W <text>.?B (<name> : <message>)
void clif_parse_clan_chat(int fd, struct map_session_data* sd)
{
#if PACKETVER >= 20131223
	char name[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	// validate packet and retrieve name and message
	if (!clif_process_message(sd, false, name, message, output))
		return;

	clan_send_message(sd, RFIFOCP(fd, 4), RFIFOW(fd, 2) - 4);
#endif
}

/// Sends the basic clan informations to the client. (ZC_CLANINFO)
/// 098a <length>.W <clan id>.L <clan name>.24B <clan master>.24B <clan map>.16B <alliance count>.B
///      <antagonist count>.B { <alliance>.24B } * alliance count { <antagonist>.24B } * antagonist count
void clif_clan_basicinfo(struct map_session_data *sd)
{
#if PACKETVER >= 20131223
	int fd, offset, length, i, flag;
	struct clan* clan;
	char mapname[MAP_NAME_LENGTH_EXT];
	
	nullpo_retv(sd);
	nullpo_retv(clan = sd->clan);
	
	// Check if the player has a valid session and is not autotrading
	if (!session_isValid(sd->fd))
		return;

	length = 8 + 2 * NAME_LENGTH + MAP_NAME_LENGTH_EXT + 2;
	fd = sd->fd;

	WFIFOHEAD(fd, length);

	memset(WFIFOP(fd, 0), 0, length);

	WFIFOW(fd, 0) = 0x98a;
	WFIFOL(fd, 4) = clan->id;
	offset = 8;
	safestrncpy(WFIFOCP(fd, offset), clan->name, NAME_LENGTH);
	offset += NAME_LENGTH;
	safestrncpy(WFIFOCP(fd, offset), clan->master, NAME_LENGTH);
	offset += NAME_LENGTH;
	mapindex_getmapname_ext(clan->map, mapname);
	safestrncpy( WFIFOCP(fd, offset), mapname, MAP_NAME_LENGTH_EXT);
	offset += MAP_NAME_LENGTH_EXT;

	WFIFOB(fd, offset++) = clan_get_alliance_count(clan, 0);
	WFIFOB(fd, offset++) = clan_get_alliance_count(clan, 1);

	for (flag = 0; flag < 2; flag++)
	{
		for (i = 0; i < MAX_CLANALLIANCE; i++)
		{
			if (clan->alliance[i].clan_id > 0 && clan->alliance[i].opposition == flag)
			{
				safestrncpy(WFIFOCP(fd, offset), clan->alliance[i].name, NAME_LENGTH);
				offset += NAME_LENGTH;
			}
		}
	}

	WFIFOW(fd, 2) = offset;
	WFIFOSET(fd, offset);
#endif
}

/// Updates the online and maximum player count of a clan. (ZC_NOTIFY_CLAN_CONNECTINFO)
/// 0988 <online count>.W <maximum member amount>.W
void clif_clan_onlinecount(struct clan* clan)
{
#if PACKETVER >= 20131223
	uint8 buf[6];
	struct map_session_data *sd;

	WBUFW(buf, 0) = 0x988;
	WBUFW(buf, 2) = clan->connect_member;
	WBUFW(buf, 4) = clan->max_member;

	if ((sd = clan_getavailablesd(clan)) != NULL)
		clif_send(buf, 6, &sd->bl, CLAN);
#endif
}

/// Notifies the client that the player has left his clan. (ZC_ACK_CLAN_LEAVE)
/// 0989
void clif_clan_leave(struct map_session_data* sd)
{
#if PACKETVER >= 20131223
	int fd;
	
	nullpo_retv(sd);
	
	if (!session_isValid(sd->fd))
		return;
	
	fd = sd->fd;

	WFIFOHEAD(fd, 2);
	WFIFOW(fd, 0) = 0x989;
	WFIFOSET(fd, 2);
#endif
}

/// Achievement System
/// Author: Luxuri, Aleos

/**
 * Send all of a session's achievement information to client.
 * Called only once on login/char-loading. (PACKET_ZC_ALL_ACH_LIST)
 * @packet [out] 0x0A23 <ID>.W <Length>.W <ach_count>.L <total_points>.L <rank>.W <current_rank_points>.L <next_rank_points>.L <struct ach_list_info *[]>.P
 * @param fd   socket descriptor
 * @param sd   pointer to map_session_data
 */
void clif_achievement_list_all(struct map_session_data *sd)
{
#if PACKETVER >= 20141016
	int i, j, len, fd, curr_exp_tmp = 0, rank = 0;
	uint16 count = 0;
	uint32 total_points = 0;
	struct ach_list_info ach[MAX_ACHIEVEMENT_DB] = {0};

	nullpo_retv(sd);

	if (!battle_config.feature_achievement) {
		clif_disp_overheadcolor_self(sd->fd, COLOR_RED, msg_txt(sd,747)); // Achievements are disabled.
		return;
	}

	fd = sd->fd;

	/* Browse through the session's achievement list and gather their values. */
	for (i = 0; i < VECTOR_LENGTH(sd->achievement); i++) {
		j = 0;
		struct achievement *a = &VECTOR_INDEX(sd->achievement, i);
		const struct achievement_data *ad = NULL;

		/* Sanity check for nonull pointers. */
		if (a == NULL || (ad = achievement_get(a->id)) == NULL)
			continue;

		ach[count].ach_id = a->id;

		for (j = 0; j < VECTOR_LENGTH(ad->objective); j++)
			ach[count].objective[j] = a->objective[j];

		if (a->completed) {
			ach[count].completed = 1;
			ach[count].completed_at = (uint32)a->completed;
			ach[count].reward = a->rewarded ? 1 : 0;
			total_points += ad->points;
		}

		count++;
	}

	curr_exp_tmp = total_points;
	for (i = 0; curr_exp_tmp && i < MAX_ACHIEVEMENT_RANKS && i < VECTOR_LENGTH(rank_exp); i++) {
		if (curr_exp_tmp >= VECTOR_INDEX(rank_exp, i)) {
			curr_exp_tmp -= VECTOR_INDEX(rank_exp, i);
			rank++;

			// Validate achievement rank type achievements.
			achievement_validate_achievement_rank(sd, rank);
		}
	}

	len = sizeof(struct ach_list_info) * count + 22;

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0xa23;
	WFIFOW(fd, 2) = len;
	WFIFOL(fd, 4) = count; // Amount of achievements the player has in their list (started/completed)
	WFIFOL(fd, 8) = total_points;
	WFIFOW(fd, 12) = rank;
	WFIFOL(fd, 14) = curr_exp_tmp;
	WFIFOL(fd, 18) = VECTOR_INDEX(rank_exp, rank);

	for (i = 0; i < count; i++) {
		WFIFOL(fd, i * 50 + 22) = (uint32)ach[i].ach_id;
		WFIFOB(fd, i * 50 + 26) = (uint32)ach[i].completed_at > 0;
		for (j = 0; j < MAX_ACHIEVEMENT_OBJECTIVES; j++)
			WFIFOL(fd, (i * 50) + 27 + (j * 4)) = (uint32)ach[i].objective[j];
		WFIFOL(fd, i * 50 + 67) = (uint32)ach[i].completed_at;
		WFIFOB(fd, i * 50 + 71) = ach[i].reward > 0;
	}
	WFIFOSET(fd, len);
#endif
}

/**
 * Sends achievement information for a single achievement.
 * Called on objective progress updates/completion. (PACKET_ZC_ACH_UPDATE)
 * @packet [out] 0x0A24 <ID>.W <total_points>.L <rank>.W <current_rank_points>.L <next_rank_points>.L <struct ach_list_info *>.P
 * @param sd    pointer to struct map_session_data
 * @param ad    const pointer to struct achievement_data from the achievement db.
 */
void clif_achievement_update(struct map_session_data *sd, const struct achievement_data *ad)
{
#if PACKETVER >= 20141016
	int fd, i = 0, points = 0, rank = 0, curr_rank_points = 0;
	struct achievement *a = NULL;
	struct ach_list_info ach = { 0 };

	nullpo_retv(sd);
	nullpo_retv(ad);

	if (!battle_config.feature_achievement) {
		clif_disp_overheadcolor_self(sd->fd, COLOR_RED, msg_txt(sd,747)); // Achievements are disabled.
		return;
	}

	fd = sd->fd;

	/* Get Session Achievement Data */
	if ((a = achievement_ensure(sd, ad)) == NULL)
		return;

	/* Get total points, current rank and current rank points from the session. */
	achievement_calculate_totals(sd, &points, NULL, &rank, &curr_rank_points);

	ach.ach_id = ad->id;
	ach.completed = (uint8)achievement_check_complete(sd, ad);

	for (i = 0; i < VECTOR_LENGTH(ad->objective); i++)
		ach.objective[i] = a->objective[i];

	ach.completed_at = (uint32)a->completed;

	ach.reward = a->rewarded ? 1 : 0;

	WFIFOHEAD(fd, packet_len(0xa24));
	WFIFOW(fd, 0) = 0xa24;
	WFIFOL(fd, 2) = points;
	WFIFOW(fd, 6) = rank;
	WFIFOL(fd, 8) = curr_rank_points;
	WFIFOL(fd, 12) = VECTOR_INDEX(rank_exp, rank);

	if (ach.ach_id > 0) {
		WFIFOL(fd, 16) = ach.ach_id; // Achievement ID
		WFIFOB(fd, 20) = ach.completed > 0; // Is it complete?
		for (i = 0; i < MAX_ACHIEVEMENT_OBJECTIVES; i++)
			WFIFOL(fd, 21 + (i * 4)) = (uint32)ach.objective[i]; // 1~10 pre-reqs
		WFIFOL(fd, 61) = (uint32)ach.completed_at; // Epoch time
		WFIFOB(fd, 65) = ach.reward > 0; // Got reward?
	}
	else
		memset(WFIFOP(fd, 16), 0, 40);
	WFIFOSET(fd, packet_len(0xa24));
#endif
}

/**
 * Parses achievement reward packet from session.
 * @packet [in] 0x0A25 <ach_id>.L
 * @param fd   socket descriptor.
 * @param sd   ptr to session data.
 */
void clif_parse_AchievementCheckReward(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20141016
	nullpo_retv(sd);

	int ach_id = RFIFOL(fd, 2);
	const struct achievement_data *ad = NULL;
	struct achievement *ach = NULL;

	if (ach_id <= 0 || (ad = achievement_get(ach_id)) == NULL)
		return;

	if ((ach = achievement_ensure(sd, ad)) == NULL)
		return;

	if (achievement_check_complete(sd, ad) && ach->completed && ach->rewarded == 0) {
		achievement_get_rewards(sd, ad);
	}
#endif
}

/**
* Returns the result of achievement_check_reward (ZC_REQ_AG_REWARD_ACK).
* 0a26 <packetType>.W <result>.W <achievementID>.L
*/
void clif_achievement_reward_ack(int fd, unsigned char result, int achievement_id)
{
#if PACKETVER >= 20141016
	WFIFOHEAD(fd, packet_len(0xa26));
	WFIFOW(fd, 0) = 0xa26;
	WFIFOB(fd, 2) = result;
	WFIFOL(fd, 3) = achievement_id;
	WFIFOSET(fd, packet_len(0xa26));
#endif
}

/**
 * [clif_change_title_ack description]
 * @packet [out] 0x0A2F <packet_id>.W <Result>.B <title_id>.L
 */
static void clif_change_title_ack(int fd, struct map_session_data *sd, int title_id)
{
#if PACKETVER >= 20141016
	unsigned char failed = 0;

	if (!achievement_check_title(sd, title_id)) {
		clif_displaymessage(fd, "Title is not yet earned.");
		failed = 1;
	}

	sd->status.title_id = title_id;

	WFIFOHEAD(fd, packet_len(0xa2f));
	WFIFOW(fd, 0) = 0xa2f;
	WFIFOB(fd, 2) = failed;
	WFIFOL(fd, 3) = sd->status.title_id;
	WFIFOSET(fd, packet_len(0xa2f));

	// Update names
	clif_name_self(&sd->bl);
	clif_name_area(&sd->bl);
#endif
}

/**
 * Sends achievement reward collection acknowledgement to the client.
 * @packet[in] 0x0A2E <packet_id>.W <title_id>.L
 */
static void clif_parse_change_title(int fd, struct map_session_data *sd)
{
	int title_id = RFIFOL(fd, 2);

	if (title_id == sd->status.title_id) { // Same Title
		return;
	}
	else if (title_id < 0) {
		title_id = 0;
	}

	clif_change_title_ack(fd, sd, title_id);
}

/// Pet
///

/// Request to invoke a pet menu action (CZ_COMMAND_PET).
/// 01a1 <type>.B
/// type:
///     0 = pet information
///     1 = feed
///     2 = performance
///     3 = return to egg
///     4 = unequip accessory
void clif_parse_PetMenu(int fd, struct map_session_data *sd)
{
	pet_menu(sd,RFIFOB(fd,2));
}


/// Attempt to tame a monster (CZ_TRYCAPTURE_MONSTER).
/// 019f <id>.L
void clif_parse_CatchPet(int fd, struct map_session_data *sd)
{
	pet_catch_process2(sd,RFIFOL(fd,2));
}


/// Answer to pet incubator egg selection dialog (CZ_SELECT_PETEGG).
/// 01a7 <index>.W
void clif_parse_SelectEgg(int fd, struct map_session_data *sd)
{
	if (sd->menuskill_id != SA_TAMINGMONSTER || sd->menuskill_val != -1)
	{
		return;
	}
	pet_select_egg(sd,RFIFOW(fd,2)-2);
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Request to display pet's emotion/talk (CZ_PET_ACT).
/// 01a9 <data>.L
/// data:
///     is either emotion (@see enum emotion_type) or a compound value
///     (((mob id)-100)*100+(act id)*10+(hunger)) that describes an
///     entry (given in parentheses) in data\pettalktable.xml
///     act id:
///         0 = feeding
///         1 = hunting
///         2 = danger
///         3 = dead
///         4 = normal (stand)
///         5 = special performance (perfor_s)
///         6 = level up (levelup)
///         7 = performance 1 (perfor_1)
///         8 = performance 2 (perfor_2)
///         9 = performance 3 (perfor_3)
///        10 = log-in greeting (connect)
///     hungry value:
///         0 = very hungry (hungry)
///         1 = hungry (bit_hungry)
///         2 = satisfied (noting)
///         3 = stuffed (full)
///         4 = full (so_full)
void clif_parse_SendEmotion(int fd, struct map_session_data *sd)
{
	if(sd->pd)
		clif_pet_emotion(sd->pd,RFIFOL(fd,2));
}


/// Request to change pet's name (CZ_RENAME_PET).
/// 01a5 <name>.24B
void clif_parse_ChangePetName(int fd, struct map_session_data *sd)
{
	pet_change_name(sd,(char*)RFIFOP(fd,2));
}

/// Request to Evolve the pet (CZ_PET_EVOLUTION) [Dastgir/Hercules]
/// 09fb <Length>.W <EvolvedPetEggID>.W {<index>.W <amount>.W}*items
void clif_parse_pet_evolution(int fd, struct map_session_data *sd)
{
	int i = 0, idx, petIndex;

	int16 EvolvedPetEggID = RFIFOW(fd,4);

	if (sd->status.pet_id == 0) {
		clif_pet_evolution_result(fd, PET_EVOL_NO_CALLPET);
		return;
	}

	ARR_FIND(0, MAX_INVENTORY, idx, sd->inventory.u.items_inventory[idx].card[0] == CARD0_PET &&
		sd->status.pet_id == MakeDWord(sd->inventory.u.items_inventory[idx].card[1], sd->inventory.u.items_inventory[idx].card[2]));

	if (idx == MAX_INVENTORY) {
		clif_pet_evolution_result(fd, PET_EVOL_NO_PETEGG);
		return;
	}

	// Not Loyal Yet
	if (sd->pd == NULL || sd->pd->pet.intimate < 900) {
		clif_pet_evolution_result(fd, PET_EVOL_RG_FAMILIAR);
		return;
	}

	ARR_FIND(0, MAX_PET_DB, petIndex, pet_db[petIndex].class_ == sd->pd->pet.class_);

	if (petIndex == MAX_PET_DB) {
		// Which error?
		clif_pet_evolution_result(fd, PET_EVOL_UNKNOWN);
		return;
	}

	// Client side validation is not done as it is insecure.
	for (i = 0; i < VECTOR_LENGTH(pet_db[petIndex].evolve_data); i++) {
		struct pet_evolve_data *ped = &VECTOR_INDEX(pet_db[petIndex].evolve_data, i);

		if (ped->petEggId == EvolvedPetEggID) {
			int j;
			int pet_id;

			if (VECTOR_LENGTH(ped->items) == 0) {
				clif_pet_evolution_result(fd, PET_EVOL_NO_RECIPE);
				return;
			}
			for (j = 0; j < VECTOR_LENGTH(ped->items); j++) {
				struct pet_itemlist_entry *list = &VECTOR_INDEX(ped->items, j);
				int n = pc_search_inventory(sd, list->id);

				if (n == -1) {
					clif_pet_evolution_result(fd, PET_EVOL_NO_MATERIAL);
					return;
				}
			}

			for (j = 0; j < VECTOR_LENGTH(ped->items); j++) {
				struct pet_itemlist_entry *list = &VECTOR_INDEX(ped->items, j);
				int n = pc_search_inventory(sd, list->id);

				if (pc_delitem(sd, n, list->amount, 0, 0, LOG_TYPE_OTHER) == 1) {
					clif_pet_evolution_result(fd, PET_EVOL_NO_MATERIAL);
					return;
				}
			}

			// Return to Egg
			pet_return_egg(sd, sd->pd);

			if (pc_delitem(sd, idx, 1, 0, 0, LOG_TYPE_OTHER) == 1) {
				clif_pet_evolution_result(fd, PET_EVOL_NO_PETEGG);
				return;
			}

			pet_id = search_petDB_index(ped->petEggId, PET_EGG);
			if (pet_id >= 0) {
				sd->catch_target_class = pet_db[pet_id].class_;

				intif_create_pet(
					sd->status.account_id, sd->status.char_id,
					(short)pet_db[pet_id].class_, (short)mob_db(pet_db[pet_id].class_)->lv,
					(short)pet_db[pet_id].EggID, 0, (short)pet_db[pet_id].intimate,
					100, 0, 1, pet_db[pet_id].jname);
				clif_pet_evolution_result(fd, PET_EVOL_SUCCESS);
			}
			else {
				clif_pet_evolution_result(fd, PET_EVOL_UNKNOWN);
			}
			return;
		}
	}

	clif_pet_evolution_result(fd, PET_EVOL_UNKNOWN);
	ShowDebug("clif_pet_evolution_result: No evolution data found...\n");
}

/**
 * Result of Pet Evolution (ZC_PET_EVOLUTION_RESULT)
 * 0x9fc <Result>.L
 */
void clif_pet_evolution_result(int fd, enum pet_evolution_result result) {
#if PACKETVER >= 20140122
	WFIFOHEAD(fd, packet_len(0x9fc));
	WFIFOW(fd, 0) = 0x9fc;
	WFIFOL(fd, 2) = result;
	WFIFOSET(fd, packet_len(0x9fc));
#endif
}

/// /kill (CZ_DISCONNECT_CHARACTER).
/// Request to disconnect a character.
/// 00cc <account id>.L
/// NOTE: Also sent when using GM right click menu "(name) force to quit"
void clif_parse_GMKick(int fd, struct map_session_data *sd)
{
	struct block_list *target;
	int tid,lv;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	tid = RFIFOL(fd,2);
	target = map_id2bl(tid);
	if (!target) {
		clif_GM_kickack(sd, 0);
		return;
	}

	switch (target->type) {
	case BL_PC:
	{
		struct map_session_data *tsd = (struct map_session_data *)target;
		if (pc_isGM(sd) <= pc_isGM(tsd))
		{
			clif_GM_kickack(sd, 0);
			return;
		}

		lv = get_atcommand_level(atcommand_kick);
		if( pc_isGM(sd) < lv )
		{
			clif_GM_kickack(sd, 0);
			return;
		}

		{
			char message[256];
			sprintf(message, "/kick %s (%d)", tsd->status.name, tsd->status.char_id);
			log_atcommand(sd, lv, message);
		}

		clif_GM_kick(sd, tsd);
	}
	break;
	case BL_MOB:
	{
		lv = get_atcommand_level(atcommand_killmonster);
		if( pc_isGM(sd) < lv )
		{
			clif_GM_kickack(sd, 0);
			return;
		}

		{
			char message[256];
			sprintf(message, "/kick %s (%d)", status_get_name(target), status_get_class(target));
			log_atcommand(sd, lv, message);
		}

		status_percent_damage(&sd->bl, target, 100, 0, true); // can invalidate 'target'
	}
	break;
	case BL_NPC:
	{
		struct npc_data* nd = (struct npc_data *)target;
		lv = get_atcommand_level(atcommand_unloadnpc);
		if( pc_isGM(sd) < lv )
		{
			clif_GM_kickack(sd, 0);
			return;
		}

		{
			char message[256];
			sprintf(message, "/kick %s (%d)", status_get_name(target), status_get_class(target));
			log_atcommand(sd, lv, message);
		}

		// copy-pasted from atcommand_unloadnpc
		npc_unload_duplicates(nd);
		npc_unload(nd, true); // invalidates 'target'
		npc_read_event_script();
	}
	break;
	default:
		clif_GM_kickack(sd, 0);
	}
}


/// /killall (CZ_DISCONNECT_ALL_CHARACTER).
/// Request to disconnect all characters.
/// 00ce
void clif_parse_GMKickAll(int fd, struct map_session_data* sd)
{
	is_atcommand(fd, sd, "@kickall", 1);
}


/// /remove (CZ_REMOVE_AID).
/// Request to warp to a character with given login ID.
/// 01ba <account name>.24B

/// /shift (CZ_SHIFT).
/// Request to warp to a character with given name.
/// 01bb <char name>.24B
void clif_parse_GMShift(int fd, struct map_session_data *sd)
{// FIXME: remove is supposed to receive account name for clients prior 20100803RE
	char *player_name;
	int lv;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;
	if( pc_isGM(sd) < (lv=get_atcommand_level(atcommand_jumpto)) )
		return;

	player_name = (char*)RFIFOP(fd,2);
	player_name[NAME_LENGTH-1] = '\0';
	atcommand_jumpto(fd, sd, "@jumpto", player_name); // as @jumpto
	{
		char message[NAME_LENGTH+7];
		sprintf(message, "/shift %s", player_name);
		log_atcommand(sd, lv, message);
	}
}


/// /remove (CZ_REMOVE_AID_SSO).
/// Request to warp to a character with given account ID.
/// 0843 <account id>.L
void clif_parse_GMRemove2(int fd, struct map_session_data* sd)
{
	uint32 account_id, lv;
	struct map_session_data* pl_sd;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
	{
		return;
	}

	if( pc_isGM(sd) < ( lv = get_atcommand_level(atcommand_jumpto) ) )
	{
		return;
	}

	account_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	if( ( pl_sd = map_id2sd(account_id) ) != NULL && pc_isGM(sd) >= pc_isGM(pl_sd) )
	{
		pc_warpto(sd, pl_sd);
	}

	{
		char message[32];

		sprintf(message, "/remove %d", account_id);
		log_atcommand(sd, lv, message);
	}
}


/// /recall (CZ_RECALL).
/// Request to summon a player with given login ID to own position.
/// 01bc <account name>.24B

/// /summon (CZ_RECALL_GID).
/// Request to summon a player with given name to own position.
/// 01bd <char name>.24B
void clif_parse_GMRecall(int fd, struct map_session_data *sd)
{// FIXME: recall is supposed to receive account name for clients prior 20100803RE
	char *player_name;
	int lv;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	if( pc_isGM(sd) < (lv=get_atcommand_level(atcommand_recall)) )
		return;

	player_name = (char*)RFIFOP(fd,2);
	player_name[NAME_LENGTH-1] = '\0';
	atcommand_recall(fd, sd, "@recall", player_name); // as @recall
	{
		char message[NAME_LENGTH+8];
		sprintf(message, "/recall %s", player_name);
		log_atcommand(sd, lv, message);
	}
}


/// /recall (CZ_RECALL_SSO).
/// Request to summon a player with given account ID to own position.
/// 0842 <account id>.L
void clif_parse_GMRecall2(int fd, struct map_session_data* sd)
{
	uint32 account_id, lv;
	struct map_session_data* pl_sd;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
	{
		return;
	}

	if( pc_isGM(sd) < ( lv = get_atcommand_level(atcommand_recall) ) )
	{
		return;
	}

	account_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	if( ( pl_sd = map_id2sd(account_id) ) != NULL && pc_isGM(sd) >= pc_isGM(pl_sd) )
	{
		pc_recall(sd, pl_sd);
	}

	{
		char message[32];

		sprintf(message, "/recall %d", account_id);
		log_atcommand(sd, lv, message);
	}
}


/// /item /monster (CZ_ITEM_CREATE).
/// Request to make items or spawn monsters.
/// 013f <item/mob name>.24B
void clif_parse_GM_Monster_Item(int fd, struct map_session_data *sd)
{
	char *monster_item_name;
	char message[NAME_LENGTH+10]; //For logging.
	int level;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	monster_item_name = (char*)RFIFOP(fd,2);
	monster_item_name[NAME_LENGTH-1] = '\0';

	// FIXME: Should look for item first, then for monster.
	// FIXME: /monster takes mob_db Sprite_Name as argument
	if( mobdb_searchname(monster_item_name) ) {
		if( pc_isGM(sd) < (level=get_atcommand_level(atcommand_monster)) )
			return;
		atcommand_monster(fd, sd, "@monster", monster_item_name); // as @monster
		{	//Log action. [Skotlex]
			snprintf(message, sizeof(message)-1, "@spawn %s", monster_item_name);
			log_atcommand(sd, level, message);
		}
		return;
	}
	// FIXME: Stackables have a quantity of 20.
	// FIXME: Equips are supposed to be unidentified.
	if( itemdb_searchname(monster_item_name) == NULL )
		return;
	if( pc_isGM(sd) < (level = get_atcommand_level(atcommand_item)) )
		return;
	atcommand_item(fd, sd, "@item", monster_item_name); // as @item
	{	//Log action. [Skotlex]
		sprintf(message, "@item %s", monster_item_name);
		log_atcommand(sd, level, message);
	}
}


/// /hide (CZ_CHANGE_EFFECTSTATE).
/// 019d <effect state>.L
/// effect state:
///     TODO: Any OPTION_* ?
void clif_parse_GMHide(int fd, struct map_session_data *sd)
{
	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	if( pc_isGM(sd) < get_atcommand_level(atcommand_hide) )
		return;

	if( sd->sc.option & OPTION_INVISIBLE ) {
		sd->sc.option &= ~OPTION_INVISIBLE;
		if (sd->disguise)
			status_set_viewdata(&sd->bl, sd->disguise);
		else
			status_set_viewdata(&sd->bl, sd->status.class_);
		clif_displaymessage(fd, "Invisible: Off.");
	} else {
		sd->sc.option |= OPTION_INVISIBLE;
		sd->vd.class_ = INVISIBLE_CLASS;
		clif_displaymessage(fd, "Invisible: On.");
		log_atcommand(sd, get_atcommand_level(atcommand_hide), "/hide");
	}
	clif_changeoption(&sd->bl);
}


/// Request to adjust player's manner points (CZ_REQ_GIVE_MANNER_POINT).
/// 0149 <account id>.L <type>.B <value>.W
/// type:
///     0 = positive points
///     1 = negative points
///     2 = self mute (+10 minutes)
void clif_parse_GMReqNoChat(int fd,struct map_session_data *sd)
{
	int id, type, value, level;
	struct map_session_data *dstsd;

	id = RFIFOL(fd,2);
	type = RFIFOB(fd,6);
	value = RFIFOW(fd,7);

	if( type == 0 )
		value = 0 - value;

	//If type is 2 and the ids don't match, this is a crafted hacked packet!
	//Disabled because clients keep self-muting when you give players public @ commands... [Skotlex]
	if (type == 2 /* && (pc_isGM(sd) > 0 || sd->bl.id != id)*/)
		return;

	dstsd = map_id2sd(id);
	if( dstsd == NULL )
		return;

	if( (level = pc_isGM(sd)) > pc_isGM(dstsd) && level >= get_atcommand_level(atcommand_mute) )
	{
		clif_manner_message(sd, 0);
		clif_manner_message(dstsd, 5);

		if( dstsd->status.manner < value ) {
			dstsd->status.manner -= value;
			sc_start(&dstsd->bl,SC_NOCHAT,100,0,0);
		} else {
			dstsd->status.manner = 0;
			status_change_end(&dstsd->bl, SC_NOCHAT, INVALID_TIMER);
		}

		if( type != 2 )
			clif_GM_silence(sd, dstsd, type);
	}
}


/// /rc (CZ_REQ_GIVE_MANNER_BYNAME).
/// GM adjustment of a player's manner value by -60.
/// 0212 <char name>.24B
void clif_parse_GMRc(int fd, struct map_session_data* sd)
{
	char* name = (char*)RFIFOP(fd,2);
	struct map_session_data* dstsd;
	name[23] = '\0';
	dstsd = map_nick2sd(name);
	if( dstsd == NULL )
		return;

	if( pc_isGM(sd) > pc_isGM(dstsd) && pc_isGM(sd) >= get_atcommand_level(atcommand_mute) )
	{
		clif_manner_message(sd, 0);
		clif_manner_message(dstsd, 3);

		dstsd->status.manner -= 60;
		sc_start(&dstsd->bl,SC_NOCHAT,100,0,0);

		clif_GM_silence(sd, dstsd, 1);
	}
}


/// Result of request to resolve account name (ZC_ACK_ACCOUNTNAME).
/// 01e0 <account id>.L <account name>.24B
void clif_account_name(struct map_session_data* sd, uint32 account_id, const char* accname)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x1e0));
	WFIFOW(fd,0) = 0x1e0;
	WFIFOL(fd,2) = account_id;
	safestrncpy((char*)WFIFOP(fd,6), accname, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x1e0));
}


/// GM requesting account name (for right-click gm menu) (CZ_REQ_ACCOUNTNAME).
/// 01df <account id>.L
void clif_parse_GMReqAccountName(int fd, struct map_session_data *sd)
{
	if (sd->bl.type&BL_PC) { // Only show for players
		char command[30];
		uint32 account_id = RFIFOL(fd, 2);

		//tmp get all display
		safesnprintf(command, sizeof(command), "%caccinfo %d", atcommand_symbol, account_id);
		is_atcommand(fd, sd, command, 1);
		//clif_account_name(sd, account_id, ""); //! TODO request to login-serv
	}
}

void clif_parse_GMFullStrip(int fd, struct map_session_data *sd) {
	char cmd[30];
	int t_aid = RFIFOL(fd, 2);
	safesnprintf(cmd, sizeof(cmd), "%cfullstrip %d", atcommand_symbol, t_aid);
	is_atcommand(fd, sd, cmd, 1);
}

/// /changemaptype <x> <y> <type> (CZ_CHANGE_MAPTYPE).
/// GM single cell type change request.
/// 0198 <x>.W <y>.W <type>.W
/// type:
///     0 = not walkable
///     1 = walkable
void clif_parse_GMChangeMapType(int fd, struct map_session_data *sd)
{
	int x,y,type;

	if( battle_config.atc_gmonly && !pc_isGM(sd) )
		return;

	if( pc_isGM(sd) < 99 ) //TODO: add proper check
		return;

	x = RFIFOW(fd,2);
	y = RFIFOW(fd,4);
	type = RFIFOW(fd,6);

	map_setgatcell(sd->bl.m,x,y,type);
	clif_changemapcell(0,sd->bl.m,x,y,type,ALL_SAMEMAP);
	//FIXME: once players leave the map, the client 'forgets' this information.
}


/// /in /ex (CZ_SETTING_WHISPER_PC).
/// Request to allow/deny whispers from a nick.
/// 00cf <nick>.24B <type>.B
/// type:
///     0 = (/ex nick) deny speech from nick
///     1 = (/in nick) allow speech from nick
void clif_parse_PMIgnore(int fd, struct map_session_data* sd)
{
	char output[512];
	char* nick;
	uint8 type;
	int i;

	nick = (char*)RFIFOP(fd,2); // speed up
	nick[NAME_LENGTH-1] = '\0'; // to be sure that the player name has at most 23 characters
	type = RFIFOB(fd,26);

	if( type == 0 )
	{	// Add name to ignore list (block)

		// Bot-check...
		if (strcmp(wisp_server_name, nick) == 0)
		{	// to find possible bot users who automaticaly ignore people
			sprintf(output, "Character '%s' (account: %d) has tried to block wisps from '%s' (wisp name of the server). Bot user?", sd->status.name, sd->status.account_id, wisp_server_name);
			intif_wis_message_to_gm(wisp_server_name, battle_config.hack_info_GM_level, output);
			clif_wisexin(sd, type, 1); // fail
			return;
		}

		// try to find a free spot, while checking for duplicates at the same time
		ARR_FIND( 0, MAX_IGNORE_LIST, i, sd->ignore[i].name[0] == '\0' || strcmp(sd->ignore[i].name, nick) == 0 );
		if( i == MAX_IGNORE_LIST )
		{// no space for new entry
			clif_wisexin(sd, type, 2); // too many blocks
			return;
		}
		if( sd->ignore[i].name[0] != '\0' )
		{// name already exists
			clif_wisexin(sd, type, 0); // Aegis reports success.
			return;
		}

		//Insert in position i
		safestrncpy(sd->ignore[i].name, nick, NAME_LENGTH);
	}
	else
	{	// Remove name from ignore list (unblock)

		// find entry
		ARR_FIND( 0, MAX_IGNORE_LIST, i, sd->ignore[i].name[0] == '\0' || strcmp(sd->ignore[i].name, nick) == 0 );
		if( i == MAX_IGNORE_LIST || sd->ignore[i].name[i] == '\0' )
		{ //Not found
			clif_wisexin(sd, type, 1); // fail
			return;
		}
		// move everything one place down to overwrite removed entry
		memmove(sd->ignore[i].name, sd->ignore[i+1].name, (MAX_IGNORE_LIST-i-1)*sizeof(sd->ignore[0].name));
		// wipe last entry
		memset(sd->ignore[MAX_IGNORE_LIST-1].name, 0, sizeof(sd->ignore[0].name));
	}

	clif_wisexin(sd, type, 0); // success
}


/// /inall /exall (CZ_SETTING_WHISPER_STATE).
/// Request to allow/deny all whispers.
/// 00d0 <type>.B
/// type:
///     0 = (/exall) deny all speech
///     1 = (/inall) allow all speech
void clif_parse_PMIgnoreAll(int fd, struct map_session_data *sd)
{
	int type = RFIFOB(fd,2), flag;

	if( type == 0 )
	{// Deny all
		if( sd->state.ignoreAll ) {
			flag = 1; // fail
		} else {
			sd->state.ignoreAll = 1;
			flag = 0; // success
		}
	}
	else
	{//Unblock everyone
		if( sd->state.ignoreAll ) {
			sd->state.ignoreAll = 0;
			flag = 0; // success
		} else {
			if (sd->ignore[0].name[0] != '\0')
			{  //Wipe the ignore list.
				memset(sd->ignore, 0, sizeof(sd->ignore));
				flag = 0; // success
			} else {
				flag = 1; // fail
			}
		}
	}

	clif_wisall(sd, type, flag);
}


/// Whisper ignore list (ZC_WHISPER_LIST).
/// 00d4 <packet len>.W { <char name>.24B }*
void clif_PMIgnoreList(struct map_session_data* sd)
{
	int i, fd = sd->fd;

	WFIFOHEAD(fd,4+ARRAYLENGTH(sd->ignore)*NAME_LENGTH);
	WFIFOW(fd,0) = 0xd4;

	for( i = 0; i < ARRAYLENGTH(sd->ignore) && sd->ignore[i].name[0]; i++ )
	{
		safestrncpy(WFIFOP(fd,4+i*NAME_LENGTH), sd->ignore[i].name, NAME_LENGTH);
	}

	WFIFOW(fd,2) = 4+i*NAME_LENGTH;
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Whisper ignore list request (CZ_REQ_WHISPER_LIST).
/// 00d3
void clif_parse_PMIgnoreList(int fd,struct map_session_data *sd)
{
	clif_PMIgnoreList(sd);
}


/// Request to invoke the /doridori recovery bonus (CZ_DORIDORI).
/// 01e7
void clif_parse_NoviceDoriDori(int fd, struct map_session_data *sd)
{
	if (sd->state.doridori) return;

	switch (sd->class_&MAPID_UPPERMASK)
	{
		case MAPID_SOUL_LINKER:
		case MAPID_STAR_GLADIATOR:
		case MAPID_TAEKWON:
			if (!sd->state.rest)
				break;
		case MAPID_SUPER_NOVICE:
		case MAPID_SUPER_BABY:
		case MAPID_SUPER_NOVICE_E:
		case MAPID_SUPER_BABY_E:
			sd->state.doridori=1;
			break;
	}
}


/// Request to invoke the effect of super novice's guardian angel prayer (CZ_CHOPOKGI).
/// 01ed
/// Note: This packet is caused by 7 lines of any text, followed by
///       the prayer and an another line of any text. The prayer is
///       defined by lines 790~793 in data\msgstringtable.txt
///       "Dear angel, can you hear my voice?"
///       "I am" (space separated player name) "Super Novice~"
///       "Help me out~ Please~ T_T"
void clif_parse_NoviceExplosionSpirits(int fd, struct map_session_data *sd)
{
	if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE)
	{
		unsigned int next = pc_nextbaseexp(sd);

		if( next )
		{
			int percent = (int)( ( (float)sd->status.base_exp/(float)next )*1000. );

			if( percent && ( percent%100 ) == 0 )
			{// 10.0%, 20.0%, ..., 90.0%
				sc_start(&sd->bl, status_skill2sc(MO_EXPLOSIONSPIRITS), 100, 17, skill_get_time(MO_EXPLOSIONSPIRITS, 5)); //Lv17-> +50 critical (noted by Poki) [Skotlex]
				clif_skill_nodamage(&sd->bl, &sd->bl, MO_EXPLOSIONSPIRITS, 5, 1);  // prayer always shows successful Lv5 cast and disregards noskill restrictions
			}
		}
	}
}


/// Friends List
///

/// Toggles a single friend online/offline [Skotlex] (ZC_FRIENDS_STATE).
/// 0206 <account id>.L <char id>.L <state>.B
/// 0206 <account id>.L <char id>.L <state>.B <name>.24B
/// state:
///     0 = online
///     1 = offline
void clif_friendslist_toggle(struct map_session_data *sd,uint32 account_id, uint32 char_id, int online)
{
	int i, fd = sd->fd;

	//Seek friend.
	for (i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id &&
		(sd->status.friends[i].char_id != char_id || sd->status.friends[i].account_id != account_id); i++);

	if(i == MAX_FRIENDS || sd->status.friends[i].char_id == 0)
		return; //Not found

	WFIFOHEAD(fd,packet_len(0x206));
	WFIFOW(fd, 0) = 0x206;
	WFIFOL(fd, 2) = sd->status.friends[i].account_id;
	WFIFOL(fd, 6) = sd->status.friends[i].char_id;
	WFIFOB(fd,10) = !online; //Yeah, a 1 here means "logged off", go figure... 
#if PACKETVER >= 20180221
	memcpy(WFIFOP(fd, 11), &sd->status.friends[i].name, NAME_LENGTH);
#endif
	WFIFOSET(fd, packet_len(0x206));
}


//Subfunction called from clif_foreachclient to toggle friends on/off [Skotlex]
int clif_friendslist_toggle_sub(struct map_session_data *sd,va_list ap)
{
	uint32 account_id, char_id, online;
	account_id = va_arg(ap, int);
	char_id = va_arg(ap, int);
	online = va_arg(ap, int);
	clif_friendslist_toggle(sd, account_id, char_id, online);
	return 0;
}


/// Sends the whole friends list (ZC_FRIENDS_LIST).
/// 0201 <packet len>.W { <account id>.L <char id>.L <name>.24B }*
/// 0201 <packet len>.W { <account id>.L <char id>.L }*
void clif_friendslist_send(struct map_session_data *sd)
{
#if PACKETVER < 20180221
	short offset = 32;
#else
	short offset = 8;
#endif

	int i = 0, n, fd = sd->fd;
	
	// Send friends list
	WFIFOHEAD(fd, MAX_FRIENDS * offset + 4);
	WFIFOW(fd, 0) = 0x201;
	for(i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id; i++)
	{
		WFIFOL(fd, 4 + offset * i + 0) = sd->status.friends[i].account_id;
		WFIFOL(fd, 4 + offset * i + 4) = sd->status.friends[i].char_id;
#if PACKETVER < 20180221
		safestrncpy(WFIFOP(fd, 4 + offset * i + 8), sd->status.friends[i].name, NAME_LENGTH);
#endif
	}

	if (i) {
		WFIFOW(fd, 2) = 4 + offset * i;
		WFIFOSET(fd, WFIFOW(fd,2));
	}
	
	for (n = 0; n < i; n++)
	{	//Sending the online players
		if (map_charid2sd(sd->status.friends[n].char_id))
			clif_friendslist_toggle(sd, sd->status.friends[n].account_id, sd->status.friends[n].char_id, 1);
	}
}


/// Notification about the result of a friend add request (ZC_ADD_FRIENDS_LIST).
/// 0209 <result>.W <account id>.L <char id>.L <name>.24B
/// result:
///     0 = MsgStringTable[821]="You have become friends with (%s)."
///     1 = MsgStringTable[822]="(%s) does not want to be friends with you."
///     2 = MsgStringTable[819]="Your Friend List is full."
///     3 = MsgStringTable[820]="(%s)'s Friend List is full."
void clif_friendslist_reqack(struct map_session_data *sd, struct map_session_data *f_sd, int type)
{
	int fd;
	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd,packet_len(0x209));
	WFIFOW(fd,0) = 0x209;
	WFIFOW(fd,2) = type;
	if (f_sd)
	{
		WFIFOL(fd,4) = f_sd->status.account_id;
		WFIFOL(fd,8) = f_sd->status.char_id;
		safestrncpy(WFIFOP(fd, 12), f_sd->status.name,NAME_LENGTH);
	}
	WFIFOSET(fd, packet_len(0x209));
}


/// Asks a player for permission to be added as friend (ZC_REQ_ADD_FRIENDS).
/// 0207 <req account id>.L <req char id>.L <req char name>.24B
void clif_friendlist_req(struct map_session_data* sd, uint32 account_id, uint32 char_id, const char* name)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x207));
	WFIFOW(fd,0) = 0x207;
	WFIFOL(fd,2) = account_id;
	WFIFOL(fd,6) = char_id;
	safestrncpy(WFIFOP(fd,10), name, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x207));
}


/// Request to add a player as friend (CZ_ADD_FRIENDS).
/// 0202 <name>.24B
void clif_parse_FriendsListAdd(int fd, struct map_session_data *sd)
{
	struct map_session_data *f_sd;
	int i;

	f_sd = map_nick2sd((char*)RFIFOP(fd,2));

	// ensure that the request player's friend list is not full
	ARR_FIND(0, MAX_FRIENDS, i, sd->status.friends[i].char_id == 0);

	if (i == MAX_FRIENDS) {
		clif_friendslist_reqack(sd, f_sd, 2);
		return;
	}

	// Friend doesn't exist (no player with this name)
	if (f_sd == NULL) {
		clif_displaymessage(fd, msg_txt(sd,3));
		return;
	}

	if( sd->bl.id == f_sd->bl.id )
	{// adding oneself as friend
		return;
	}

	// @noask [LuzZza]
	if(f_sd->state.noask) {
		clif_noask_sub(sd, f_sd, 5);
		return;
	}

	f_sd->friend_req = sd->status.char_id;
	sd->friend_req = f_sd->status.char_id;

	// Friend already exists
	for (i = 0; i < MAX_FRIENDS && sd->status.friends[i].char_id != 0; i++) {
		if (sd->status.friends[i].char_id == f_sd->status.char_id) {
			clif_displaymessage(fd, msg_txt(sd,521)); //"Friend already exists."
			return;
		}
	}

	clif_friendlist_req(f_sd, sd->status.account_id, sd->status.char_id, sd->status.name);
}


/// Answer to a friend add request (CZ_ACK_REQ_ADD_FRIENDS).
/// 0208 <inviter account id>.L <inviter char id>.L <result>.B
/// 0208 <inviter account id>.L <inviter char id>.L <result>.L (PACKETVER >= 6)
/// result:
///     0 = rejected
///     1 = accepted
void clif_parse_FriendsListReply(int fd, struct map_session_data *sd)
{
	struct map_session_data *f_sd;
	uint32 account_id;
	//uint32 char_id;
	char reply;

	account_id = RFIFOL(fd,2);
	//char_id = RFIFOL(fd,6);
#if PACKETVER < 6
	reply = RFIFOB(fd,10);
#else
	reply = RFIFOL(fd,10);
#endif

	if( sd->bl.id == account_id )
	{// adding oneself as friend
		return;
	}

	f_sd = map_id2sd(account_id); //The account id is the same as the bl.id of players.
	if (f_sd == NULL)
		return;
		
	if (reply == 0 || !(sd->friend_req == f_sd->status.char_id && f_sd->friend_req == sd->status.char_id))
		clif_friendslist_reqack(f_sd, sd, 1);
	else {
		int i;
		// Find an empty slot
		for (i = 0; i < MAX_FRIENDS; i++)
			if (f_sd->status.friends[i].char_id == 0)
				break;
		if (i == MAX_FRIENDS) {
			clif_friendslist_reqack(f_sd, sd, 2);
			return;
		}

		f_sd->status.friends[i].account_id = sd->status.account_id;
		f_sd->status.friends[i].char_id = sd->status.char_id;
		safestrncpy(f_sd->status.friends[i].name, sd->status.name, NAME_LENGTH);
		clif_friendslist_reqack(f_sd, sd, 0);

		achievement_validate_friend_add(f_sd); // Achievements [Smokexyz/Hercules]

		if (battle_config.friend_auto_add) {
			// Also add f_sd to sd's friendlist.
			for (i = 0; i < MAX_FRIENDS; i++) {
				if (sd->status.friends[i].char_id == f_sd->status.char_id)
					return; //No need to add anything.
				if (sd->status.friends[i].char_id == 0)
					break;
			}
			if (i == MAX_FRIENDS) {
				clif_friendslist_reqack(sd, f_sd, 2);
				return;
			}

			sd->status.friends[i].account_id = f_sd->status.account_id;
			sd->status.friends[i].char_id = f_sd->status.char_id;
			safestrncpy(sd->status.friends[i].name, f_sd->status.name, NAME_LENGTH);
			clif_friendslist_reqack(sd, f_sd, 0);
			
			achievement_validate_friend_add(sd); // Achievements [Smokexyz/Hercules]
		}
	}
}


/// Request to delete a friend (CZ_DELETE_FRIENDS).
/// 0203 <account id>.L <char id>.L
void clif_parse_FriendsListRemove(int fd, struct map_session_data *sd)
{
	struct map_session_data *f_sd = NULL;
	uint32 account_id, char_id;
	int i, j;

	account_id = RFIFOL(fd,2);
	char_id = RFIFOL(fd,6);

	// Search friend
	for (i = 0; i < MAX_FRIENDS &&
		(sd->status.friends[i].char_id != char_id || sd->status.friends[i].account_id != account_id); i++);

	if (i == MAX_FRIENDS) {
		clif_displaymessage(fd, msg_txt(sd,522)); //"Name not found in list."
		return;
	}
		
	//remove from friend's list first
	if ((f_sd = map_id2sd(account_id)) && f_sd->status.char_id == char_id) {
		for (i = 0; i < MAX_FRIENDS &&
			(f_sd->status.friends[i].char_id != sd->status.char_id || f_sd->status.friends[i].account_id != sd->status.account_id); i++);

		if (i != MAX_FRIENDS) {
			// move all chars up
			for (j = i + 1; j < MAX_FRIENDS; j++)
				memcpy(&f_sd->status.friends[j - 1], &f_sd->status.friends[j], sizeof(f_sd->status.friends[0]));

			memset(&f_sd->status.friends[MAX_FRIENDS - 1], 0, sizeof(f_sd->status.friends[MAX_FRIENDS - 1]));
			//should the guy be notified of some message? we should add it here if so
			WFIFOHEAD(f_sd->fd, packet_len(0x20a));
			WFIFOW(f_sd->fd, 0) = 0x20a;
			WFIFOL(f_sd->fd, 2) = sd->status.account_id;
			WFIFOL(f_sd->fd, 6) = sd->status.char_id;
			WFIFOSET(f_sd->fd, packet_len(0x20a));
		}

	}
	else { //friend not online -- ask char server to delete from his friendlist
		if (chrif_removefriend(char_id, sd->status.char_id)) { // char-server offline, abort
			clif_displaymessage(fd, "This action can't be performed at the moment. Please try again later.");
			return;
		}
	}

	// We can now delete from original requester
	for (i = 0; i < MAX_FRIENDS &&
		(sd->status.friends[i].char_id != char_id || sd->status.friends[i].account_id != account_id); i++);
	// move all chars up
	for (j = i + 1; j < MAX_FRIENDS; j++)
		memcpy(&sd->status.friends[j - 1], &sd->status.friends[j], sizeof(sd->status.friends[0]));

	memset(&sd->status.friends[MAX_FRIENDS - 1], 0, sizeof(sd->status.friends[MAX_FRIENDS - 1]));
	clif_displaymessage(fd, "Friend removed");
	
	WFIFOHEAD(fd,packet_len(0x20a));
	WFIFOW(fd,0) = 0x20a;
	WFIFOL(fd,2) = account_id;
	WFIFOL(fd,6) = char_id;
	WFIFOSET(fd, packet_len(0x20a));
}


/// /pvpinfo list (ZC_ACK_PVPPOINT).
/// 0210 <char id>.L <account id>.L <win point>.L <lose point>.L <point>.L
void clif_PVPInfo(struct map_session_data* sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x210));
	WFIFOW(fd,0) = 0x210;
	WFIFOL(fd,2) = sd->status.char_id;
	WFIFOL(fd,6) = sd->status.account_id;
	WFIFOL(fd,10) = sd->pvp_won;	// times won
	WFIFOL(fd,14) = sd->pvp_lost;	// times lost
	WFIFOL(fd,18) = sd->pvp_point;
	WFIFOSET(fd, packet_len(0x210));
}


/// /pvpinfo (CZ_REQ_PVPPOINT).
/// 020f <char id>.L <account id>.L
void clif_parse_PVPInfo(int fd,struct map_session_data *sd)
{
	// TODO: Is there a way to use this on an another player (char/acc id)?
	clif_PVPInfo(sd);
}

/// ranking pointlist { <name>.24B <point>.L }*10
void clif_ranklist_sub(unsigned char *buf, int type) {
	const char* name;
	struct fame_list* list;
	int i;
	
	switch( type ) {
		case RANKING_BLACKSMITH:	list = smith_fame_list; break;
		case RANKING_ALCHEMIST:		list = chemist_fame_list; break;
		case RANKING_TAEKWON:		list = taekwon_fame_list; break;
		default:					return; // Unsupported
	}

	// Packet size limits this list to 10 elements. [Skotlex]
	for( i = 0; i < 10 && i < MAX_FAME_LIST; i++ ) {
		if( list[i].id > 0 ) {
			if( strcmp(list[i].name, "-") == 0 && (name = map_charid2nick(list[i].id)) != NULL ) {
				strncpy((char *)(WBUFP(buf, 24 * i)), name, NAME_LENGTH);
			} else {
				strncpy((char *)(WBUFP(buf, 24 * i)), list[i].name, NAME_LENGTH);
			}
		} else {
			strncpy((char *)(WBUFP(buf, 24 * i)), "None", 5);
		}
		WBUFL(buf, 24 * 10 + i * 4) = list[i].fame; //points
	}
	for( ;i < 10; i++ ) { // In case the MAX is less than 10.
		strncpy((char *)(WBUFP(buf, 24 * i)), "Unavailable", 12);
		WBUFL(buf, 24 * 10 + i * 4) = 0;
	}
}

/// 097d <RankingType>.W {<CharName>.24B <point>L}*10 <mypoint>L (ZC_ACK_RANKING)
void clif_ranklist(struct map_session_data *sd, int type) {
	int fd = sd->fd;
	int mypoint = 0;
	int upperMask = sd->class_&MAPID_UPPERMASK;
	
	WFIFOHEAD(fd, 288);
	WFIFOW(fd, 0) = 0x97d;
	WFIFOW(fd, 2) = type;
	clif_ranklist_sub(WFIFOP(fd,4), type);

	if( (upperMask == MAPID_BLACKSMITH && type == RANKING_BLACKSMITH)
		|| (upperMask == MAPID_ALCHEMIST && type == RANKING_ALCHEMIST)
		|| (upperMask == MAPID_TAEKWON && type == RANKING_TAEKWON)
	) {
		mypoint = sd->status.fame;
	} else {
		mypoint = 0;
	}
	WFIFOL(fd, 284) = mypoint; //mypoint
	WFIFOSET(fd, 288);
}

/*
* 097c <type> (CZ_REQ_RANKING)
* */
void clif_parse_ranklist(int fd, struct map_session_data *sd) {
	int16 type = RFIFOW(fd, 2); //type

	switch( type ) {
		case RANKING_BLACKSMITH:
		case RANKING_ALCHEMIST:
		case RANKING_TAEKWON:
			clif_ranklist(sd, type); // pk_list unsupported atm
		break;
	}
}

// 097e <RankingType>.W <point>.L <TotalPoint>.L (ZC_UPDATE_RANKING_POINT)
void clif_update_rankingpoint(struct map_session_data *sd, int type, int points)
{
#if PACKETVER < 20120502
	switch( type ) {
		case RANKING_BLACKSMITH:	clif_fame_blacksmith(sd,points); break;
		case RANKING_ALCHEMIST:		clif_fame_alchemist(sd,points); break;
		case RANKING_TAEKWON:		clif_fame_taekwon(sd,points); break;
	}
#else
	int fd = sd->fd;
	int len = packet_len(0x97e);

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0x97e;
	WFIFOW(fd, 2) = type;
	WFIFOL(fd, 4) = points;
	WFIFOL(fd, 8) = sd->status.fame;
	WFIFOSET(fd, len);
#endif
}

/// /blacksmith list (ZC_BLACKSMITH_RANK).
/// 0219 { <name>.24B }*10 { <point>.L }*10
void clif_blacksmith(struct map_session_data* sd)
{
	int i, fd = sd->fd;
	const char* name;

	WFIFOHEAD(fd, packet_len(0x219));
	WFIFOW(fd, 0) = 0x219;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < min(10, MAX_FAME_LIST); i++) {
		if (smith_fame_list[i].id > 0) {
			if (strcmp(smith_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(smith_fame_list[i].id)) != NULL)
			{
				safestrncpy((char *)(WFIFOP(fd, 2 + 24 * i)), name, NAME_LENGTH);
			} else
				safestrncpy((char *)(WFIFOP(fd, 2 + 24 * i)), smith_fame_list[i].name, NAME_LENGTH);
		} else
			safestrncpy((char *)(WFIFOP(fd, 2 + 24 * i)), "None", 5);
		WFIFOL(fd, 242 + i * 4) = smith_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		safestrncpy((char *)(WFIFOP(fd, 2 + 24 * i)), "Unavailable", 12);
		WFIFOL(fd, 242 + i * 4) = 0;
	}

	WFIFOSET(fd, packet_len(0x219));
}


/// /blacksmith (CZ_BLACKSMITH_RANK).
/// 0217
void clif_parse_Blacksmith(int fd,struct map_session_data *sd)
{
	clif_blacksmith(sd);
}


/// Notification about backsmith points (ZC_BLACKSMITH_POINT).
/// 021b <points>.L <total points>.L
void clif_fame_blacksmith(struct map_session_data *sd, int points)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x21b));
	WFIFOW(fd,0) = 0x21b;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x21b));
}


/// /alchemist list (ZC_ALCHEMIST_RANK).
/// 021a { <name>.24B }*10 { <point>.L }*10
void clif_alchemist(struct map_session_data* sd)
{
	int i, fd = sd->fd;
	const char* name;

	WFIFOHEAD(fd,packet_len(0x21a));
	WFIFOW(fd,0) = 0x21a;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < min(10, MAX_FAME_LIST); i++) {
		if (chemist_fame_list[i].id > 0) {
			if (strcmp(chemist_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(chemist_fame_list[i].id)) != NULL)
			{
				safestrncpy(WFIFOP(fd, 2 + 24 * i), name, NAME_LENGTH);
			} else
				safestrncpy(WFIFOP(fd, 2 + 24 * i), chemist_fame_list[i].name, NAME_LENGTH);
		} else
			safestrncpy(WFIFOP(fd, 2 + 24 * i), "None", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = chemist_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		safestrncpy(WFIFOP(fd, 2 + 24 * i), "Unavailable", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = 0;
	}

	WFIFOSET(fd, packet_len(0x21a));
}


/// /alchemist (CZ_ALCHEMIST_RANK).
/// 0218
void clif_parse_Alchemist(int fd,struct map_session_data *sd)
{
	clif_alchemist(sd);
}


/// Notification about alchemist points (ZC_ALCHEMIST_POINT).
/// 021c <points>.L <total points>.L
void clif_fame_alchemist(struct map_session_data *sd, int points)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x21c));
	WFIFOW(fd,0) = 0x21c;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x21c));
}


/// /taekwon list (ZC_TAEKWON_RANK).
/// 0226 { <name>.24B }*10 { <point>.L }*10
void clif_taekwon(struct map_session_data* sd)
{
	int i, fd = sd->fd;
	const char* name;

	WFIFOHEAD(fd,packet_len(0x226));
	WFIFOW(fd,0) = 0x226;
	//Packet size limits this list to 10 elements. [Skotlex]
	for (i = 0; i < min(10, MAX_FAME_LIST); i++) {
		if (taekwon_fame_list[i].id > 0) {
			if (strcmp(taekwon_fame_list[i].name, "-") == 0 &&
				(name = map_charid2nick(taekwon_fame_list[i].id)) != NULL)
			{
				safestrncpy(WFIFOP(fd, 2 + 24 * i), name, NAME_LENGTH);
			} else
				safestrncpy(WFIFOP(fd, 2 + 24 * i), taekwon_fame_list[i].name, NAME_LENGTH);
		} else
			safestrncpy(WFIFOP(fd, 2 + 24 * i), "None", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = taekwon_fame_list[i].fame;
	}
	for(;i < 10; i++) { //In case the MAX is less than 10.
		safestrncpy(WFIFOP(fd, 2 + 24 * i), "Unavailable", NAME_LENGTH);
		WFIFOL(fd, 242 + i * 4) = 0;
	}
	WFIFOSET(fd, packet_len(0x226));
}


/// /taekwon (CZ_TAEKWON_RANK).
/// 0225
void clif_parse_Taekwon(int fd,struct map_session_data *sd)
{
	clif_taekwon(sd);
}


/// Notification about taekwon points (ZC_TAEKWON_POINT).
/// 0224 <points>.L <total points>.L
void clif_fame_taekwon(struct map_session_data *sd, int points)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x224));
	WFIFOW(fd,0) = 0x224;
	WFIFOL(fd,2) = points;
	WFIFOL(fd,6) = sd->status.fame;
	WFIFOSET(fd, packet_len(0x224));
}


/// /pk list (ZC_KILLER_RANK).
/// 0238 { <name>.24B }*10 { <point>.L }*10
void clif_ranking_pk(struct map_session_data* sd)
{
	int i, fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x238));
	WFIFOW(fd,0) = 0x238;
	for(i=0;i<10;i++){
		safestrncpy(WFIFOP(fd,i*24+2), "Unknown", NAME_LENGTH);
		WFIFOL(fd,i*4+242) = 0;
	}
	WFIFOSET(fd, packet_len(0x238));
}


/// /pk (CZ_KILLER_RANK).
/// 0237
void clif_parse_RankingPk(int fd,struct map_session_data *sd)
{
	clif_ranking_pk(sd);
}


/// SG Feel save OK [Komurka] (CZ_AGREE_STARPLACE).
/// 0254 <which>.B
/// which:
///     0 = sun
///     1 = moon
///     2 = star
void clif_parse_FeelSaveOk(int fd,struct map_session_data *sd)
{
	int i;
	if (sd->menuskill_id != SG_FEEL)
		return;
	i = sd->menuskill_val-1;
	if (i<0 || i >= MAX_PC_FEELHATE) return; //Bug?

	sd->feel_map[i].index = map_id2index(sd->bl.m);
	sd->feel_map[i].m = sd->bl.m;
	pc_setglobalreg(sd,sg_info[i].feel_var,sd->feel_map[i].index);

//Are these really needed? Shouldn't they show up automatically from the feel save packet?
//	clif_misceffect2(&sd->bl, 0x1b0);
//	clif_misceffect2(&sd->bl, 0x21f);
	clif_feel_info(sd, i, 0);
	sd->menuskill_val = sd->menuskill_id = 0;
}


/// Star Gladiator's Feeling map confirmation prompt (ZC_STARPLACE).
/// 0253 <which>.B
/// which:
///     0 = sun
///     1 = moon
///     2 = star
void clif_feel_req(int fd, struct map_session_data *sd, int skill_lv)
{
	WFIFOHEAD(fd,packet_len(0x253));
	WFIFOW(fd,0)=0x253;
	WFIFOB(fd,2)=TOB(skill_lv-1);
	WFIFOSET(fd, packet_len(0x253));
	sd->menuskill_id = SG_FEEL;
	sd->menuskill_val = skill_lv;
}


/// Request to change homunculus' name (CZ_RENAME_MER).
/// 0231 <name>.24B
void clif_parse_ChangeHomunculusName(int fd, struct map_session_data *sd)
{
	hom_change_name(sd,(char*)RFIFOP(fd,2));
}


/// Request to warp/move homunculus/mercenary to it's owner (CZ_REQUEST_MOVETOOWNER).
/// 0234 <id>.L
void clif_parse_HomMoveToMaster(int fd, struct map_session_data *sd)
{
	int id = RFIFOL(fd,2); // Mercenary or Homunculus
	struct block_list *bl = NULL;
	struct unit_data *ud = NULL;

	if( sd->md && sd->md->bl.id == id )
		bl = &sd->md->bl;
	else if( hom_is_active(sd->hd) && sd->hd->bl.id == id )
		bl = &sd->hd->bl; // Moving Homunculus
	else
		return;

	unit_calc_pos(bl, sd->bl.x, sd->bl.y, sd->ud.dir);
	ud = unit_bl2ud(bl);
	unit_walktoxy(bl, ud->to_x, ud->to_y, 4);
}


/// Request to move homunculus/mercenary (CZ_REQUEST_MOVENPC).
/// 0232 <id>.L <position data>.3B
void clif_parse_HomMoveTo(int fd, struct map_session_data *sd)
{
	int id = RFIFOL(fd,2); // Mercenary or Homunculus
	struct block_list *bl = NULL;
	short x, y;

	RFIFOPOS(fd, packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[1], &x, &y, NULL);

	if( sd->md && sd->md->bl.id == id )
		bl = &sd->md->bl; // Moving Mercenary
	else if( hom_is_active(sd->hd) && sd->hd->bl.id == id )
		bl = &sd->hd->bl; // Moving Homunculus
	else
		return;

	unit_walktoxy(bl, x, y, 4);
}


/// Request to do an action with homunculus/mercenary (CZ_REQUEST_ACTNPC).
/// 0233 <id>.L <target id>.L <action>.B
/// action:
///     always 0
void clif_parse_HomAttack(int fd,struct map_session_data *sd)
{
	struct block_list *bl = NULL;
	int id = RFIFOL(fd,2),
		target_id = RFIFOL(fd,6),
		action_type = RFIFOB(fd,10);

	if( hom_is_active(sd->hd) && sd->hd->bl.id == id )
		bl = &sd->hd->bl;
	else if( sd->md && sd->md->bl.id == id )
		bl = &sd->md->bl;
	else return;

	unit_stop_attack(bl);
	unit_attack(bl, target_id, action_type != 0);
}


/// Request to invoke a homunculus menu action (CZ_COMMAND_MER).
/// 022d <type>.W <command>.B
/// type:
///     always 0
/// command:
///     0 = homunculus information
///     1 = feed
///     2 = delete
void clif_parse_HomMenu(int fd, struct map_session_data *sd)
{	//[orn]
	int cmd;

	cmd = RFIFOW(fd,0);

	if(!hom_is_active(sd->hd))
		return;

	hom_menu(sd,RFIFOB(fd,packet_db[sd->packet_ver][cmd].pos[1]));
}


/// Request to resurrect oneself using Token of Siegfried (CZ_STANDING_RESURRECTION).
/// 0292
void clif_parse_AutoRevive(int fd, struct map_session_data *sd)
{
	pc_revive_item(sd);
}


/// Information about character's status values (ZC_ACK_STATUS_GM).
/// 0214 <str>.B <standardStr>.B <agi>.B <standardAgi>.B <vit>.B <standardVit>.B
///      <int>.B <standardInt>.B <dex>.B <standardDex>.B <luk>.B <standardLuk>.B
///      <attPower>.W <refiningPower>.W <max_mattPower>.W <min_mattPower>.W
///      <itemdefPower>.W <plusdefPower>.W <mdefPower>.W <plusmdefPower>.W
///      <hitSuccessValue>.W <avoidSuccessValue>.W <plusAvoidSuccessValue>.W
///      <criticalSuccessValue>.W <ASPD>.W <plusASPD>.W
void clif_check(int fd, struct map_session_data* pl_sd)
{
	WFIFOHEAD(fd,packet_len(0x214));
	WFIFOW(fd, 0) = 0x214;
	WFIFOB(fd, 2) = min(pl_sd->status.str, UINT8_MAX);
	WFIFOB(fd, 3) = pc_need_status_point(pl_sd, SP_STR, 1);
	WFIFOB(fd, 4) = min(pl_sd->status.agi, UINT8_MAX);
	WFIFOB(fd, 5) = pc_need_status_point(pl_sd, SP_AGI, 1);
	WFIFOB(fd, 6) = min(pl_sd->status.vit, UINT8_MAX);
	WFIFOB(fd, 7) = pc_need_status_point(pl_sd, SP_VIT, 1);
	WFIFOB(fd, 8) = min(pl_sd->status.int_, UINT8_MAX);
	WFIFOB(fd, 9) = pc_need_status_point(pl_sd, SP_INT, 1);
	WFIFOB(fd,10) = min(pl_sd->status.dex, UINT8_MAX);
	WFIFOB(fd,11) = pc_need_status_point(pl_sd, SP_DEX, 1);
	WFIFOB(fd,12) = min(pl_sd->status.luk, UINT8_MAX);
	WFIFOB(fd,13) = pc_need_status_point(pl_sd, SP_LUK, 1);
	WFIFOW(fd,14) = pl_sd->battle_status.batk+pl_sd->battle_status.rhw.atk+pl_sd->battle_status.lhw.atk;
	WFIFOW(fd,16) = pl_sd->battle_status.rhw.atk2+pl_sd->battle_status.lhw.atk2;
	WFIFOW(fd,18) = pl_sd->battle_status.matk_max;
	WFIFOW(fd,20) = pl_sd->battle_status.matk_min;
	WFIFOW(fd,22) = pl_sd->battle_status.def;
	WFIFOW(fd,24) = pl_sd->battle_status.def2;
	WFIFOW(fd,26) = pl_sd->battle_status.mdef;
	WFIFOW(fd,28) = pl_sd->battle_status.mdef2;
	WFIFOW(fd,30) = pl_sd->battle_status.hit;
	WFIFOW(fd,32) = pl_sd->battle_status.flee;
	WFIFOW(fd,34) = pl_sd->battle_status.flee2/10;
	WFIFOW(fd,36) = pl_sd->battle_status.cri/10;
	WFIFOW(fd,38) = (2000-pl_sd->battle_status.amotion)/10;  // aspd
	WFIFOW(fd,40) = 0;  // FIXME: What is 'plusASPD' supposed to be? Maybe adelay?
	WFIFOSET(fd,packet_len(0x214));
}


/// /check (CZ_REQ_STATUS_GM).
/// Request character's status values.
/// 0213 <char name>.24B
void clif_parse_Check(int fd, struct map_session_data *sd)
{
	char charname[NAME_LENGTH];
	struct map_session_data* pl_sd;

	if( pc_isGM(sd) < battle_config.gm_check_minlevel )
	{
		return;
	}

	safestrncpy(charname, (const char*)RFIFOP(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]), sizeof(charname));

	if( ( pl_sd = map_nick2sd(charname) ) == NULL || pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		return;
	}

	clif_check(fd, pl_sd);
}


#ifndef TXT_ONLY

/// MAIL SYSTEM
/// By Zephyrus
///

/// Notification about the result of adding an item to mail
/// 0255 <index>.W <result>.B (ZC_ACK_MAIL_ADD_ITEM)
/// result:
///     0 = success
///     1 = failure
/// 0a05 <result>.B <index>.W <amount>.W <item id>.W <item type>.B <identified>.B <damaged>.B <refine>.B (ZC_ACK_ADD_ITEM_TO_MAIL)
///		{ <card id>.W }*4 { <option index>.W <option value>.W <option parameter>.B }*5
/// result:
///		0 = success
///		1 = over weight
///		2 = fatal error
void clif_Mail_setattachment(struct map_session_data* sd, int index, int amount, uint8 flag) {
	int fd = sd->fd;

#if PACKETVER < 20150513
	WFIFOHEAD(fd, packet_len(0x255));
	WFIFOW(fd, 0) = 0x255;
	WFIFOW(fd, 2) = index;
	WFIFOB(fd, 4) = flag;
	WFIFOSET(fd, packet_len(0x255));
#else
	struct PACKET_ZC_ADD_ITEM_TO_MAIL p;

	if (flag) {
		memset(&p, 0, sizeof(p));
	}
	else {
		struct item* item = &sd->inventory.u.items_inventory[server_index(index)];

		p.index = index;
		p.count = amount;
		p.itemId = client_nameid(item->nameid);
		p.type = itemtype(item->nameid);
		p.IsIdentified = item->identify ? 1 : 0;
		p.IsDamaged = item->attribute ? 1 : 0;
		p.refiningLevel = item->refine;
		clif_addcards(&p.slot, item);
		clif_add_random_options(p.optionData, item);

		p.weight = 0;
		for (int i = 0; i < MAIL_MAX_ITEM; i++) {
			if (sd->mail.item[i].nameid == 0) {
				continue;
			}

			p.weight += sd->mail.item[i].amount * (sd->inventory_data[sd->mail.item[i].index]->weight / 10);
		}
		p.favorite = item->favorite;
		p.location = pc_equippoint(sd, server_index(index));
	}

	p.PacketType = 0x0A05;
	p.result = flag;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
#endif
}


/// Notification about the result of retrieving a mail attachment
/// 0245 <result>.B (ZC_MAIL_REQ_GET_ITEM)
/// result:
///     0 = success
///     1 = failure
///     2 = too many items
/// 09f2 <mail id>.Q <mail tab>.B <result>.B (ZC_ACK_ZENY_FROM_MAIL)
/// 09f4 <mail id>.Q <mail tab>.B <result>.B (ZC_ACK_ITEM_FROM_MAIL)
void clif_mail_getattachment(struct map_session_data* sd, struct mail_message *msg, uint8 result, enum mail_attachment_type type) {
	int fd = sd->fd;

#if PACKETVER < 20150513
	WFIFOHEAD(fd,packet_len(0x245));
	WFIFOW(fd,0) = 0x245;
	WFIFOB(fd,2) = result;
	WFIFOSET(fd,packet_len(0x245));
#else
	switch( type ){
		case MAIL_ATT_ITEM:
		case MAIL_ATT_ZENY:
			break;
		default:
			return;
	}

	WFIFOHEAD(fd, 12);
	WFIFOW(fd, 0) = type == MAIL_ATT_ZENY ? 0x9f2 : 0x9f4;
	WFIFOQ(fd, 2) = msg->id;
	WFIFOB(fd, 10) = msg->type;
	WFIFOB(fd, 11) = result;
	WFIFOSET(fd, 12);

	clif_Mail_refreshinbox( sd, msg->type, 0 );
#endif
}

/// Notification about the result of sending a mail
/// 0249 <result>.B (ZC_MAIL_REQ_SEND)
/// result:
///     0 = success
///     1 = recipinent does not exist
/// 09ed <result>.B (ZC_ACK_WRITE_MAIL)
void clif_Mail_send(struct map_session_data* sd, enum mail_send_result result){
	int fd = sd->fd;
#if PACKETVER < 20150513
	WFIFOHEAD(fd,packet_len(0x249));
	WFIFOW(fd,0) = 0x249;
	WFIFOB(fd,2) = WFIFOB(fd,2) = result != WRITE_MAIL_SUCCESS;
	WFIFOSET(fd,packet_len(0x249));
#else
	WFIFOHEAD(fd, 3);
	WFIFOW(fd, 0) = 0x9ed;
	WFIFOB(fd, 2) = result;
	WFIFOSET(fd, 3);
#endif
}


/// Notification about the result of deleting a mail.
/// 0257 <mail id>.L <result>.W (ZC_ACK_MAIL_DELETE)
/// result:
///     0 = success
///     1 = failure
// 09f6 <mail tab>.B <mail id>.Q (ZC_ACK_DELETE_MAIL)
void clif_mail_delete( struct map_session_data* sd, struct mail_message *msg, bool success ){
	int fd = sd->fd;

#if PACKETVER < 20150513
	WFIFOHEAD(fd, packet_len(0x257));
	WFIFOW(fd,0) = 0x257;
	WFIFOL(fd,2) = msg->id;
	WFIFOW(fd,6) = !success;
	WFIFOSET(fd, packet_len(0x257));
#else
	// This is only a success notification
	if( success ){
		WFIFOHEAD(fd, 11);
		WFIFOW(fd, 0) = 0x9f6;
		WFIFOB(fd, 2) = msg->type;
		WFIFOQ(fd, 3) = msg->id;
		WFIFOSET(fd, 11);
	}
#endif
}


/// Notification about the result of returning a mail (ZC_ACK_MAIL_RETURN).
/// 0274 <mail id>.L <result>.W
/// result:
///     0 = success
///     1 = failure
void clif_Mail_return(int fd, int mail_id, short fail)
{
	WFIFOHEAD(fd,packet_len(0x274));
	WFIFOW(fd,0) = 0x274;
	WFIFOL(fd,2) = mail_id;
	WFIFOW(fd,6) = fail;
	WFIFOSET(fd,packet_len(0x274));
}

/// Notification about new mail.
/// 024a <mail id>.L <title>.40B <sender>.24B (ZC_MAIL_RECEIVE)
/// 09e7 <result>.B (ZC_NOTIFY_UNREADMAIL)
void clif_Mail_new(struct map_session_data* sd, int mail_id, const char *sender, const char *title){
	int fd = sd->fd;
#if PACKETVER < 20150513
	WFIFOHEAD(fd,packet_len(0x24a));
	WFIFOW(fd,0) = 0x24a;
	WFIFOL(fd,2) = mail_id;
	safestrncpy((char*)WFIFOP(fd,6), title, MAIL_TITLE_LENGTH);
	safestrncpy((char*)WFIFOP(fd,46), sender, NAME_LENGTH);
	WFIFOSET(fd,packet_len(0x24a));
#else
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x9e7;
	WFIFOB(fd,2) = sd->mail.inbox.unread > 0 || sd->mail.inbox.unchecked > 0; // unread
	WFIFOSET(fd,3);
#endif
}

/// Opens/closes the mail window (ZC_MAIL_WINDOWS).
/// 0260 <type>.L
/// type:
///     0 = open
///     1 = close
void clif_Mail_window(int fd, int flag)
{
	WFIFOHEAD(fd,packet_len(0x260));
	WFIFOW(fd,0) = 0x260;
	WFIFOL(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x260));
}

/// Lists mails stored in inbox.
/// 0240 <packet len>.W <amount>.L { <mail id>.L <title>.40B <read>.B <sender>.24B <time>.L }*amount (ZC_MAIL_REQ_GET_LIST)
 /// read:
///     0 = unread
///     1 = read
/// 09f0 <packet len>.W <type>.B <amount>.B <last page>.B (ZC_ACK_MAIL_LIST)
///		{ <mail id>.Q <read>.B <type>.B <sender>.24B <received>.L <expires>.L <title length>.W <title>.?B }*
/// 0a7d <packet len>.W <type>.B <amount>.B <last page>.B (ZC_ACK_MAIL_LIST2)
///		{ <mail id>.Q <read>.B <type>.B <sender>.24B <received>.L <expires>.L <title length>.W <title>.?B }*
/// 0ac2 <packet len>.W <unknown>.B (ZC_ACK_MAIL_LIST3)
///		{ <type>.B <mail id>.Q <read>.B <type>.B <sender>.24B <expires>.L <title length>.W <title>.?B }*
void clif_Mail_refreshinbox(struct map_session_data *sd,enum mail_inbox_type type,int64 mailID){
#if PACKETVER < 20150513
	int fd = sd->fd;
	struct mail_data *md = &sd->mail.inbox;
	struct mail_message *msg;
	int len, i, j;

	len = 8 + (73 * md->amount);

	WFIFOHEAD(fd,len);
	WFIFOW(fd,0) = 0x240;
	WFIFOW(fd,2) = len;
	WFIFOL(fd,4) = md->amount;
	for( i = j = 0; i < MAIL_MAX_INBOX && j < md->amount; i++ )
	{
		msg = &md->msg[i];
		if (msg->id < 1)
			continue;

		WFIFOL(fd,8+73*j) = msg->id;
		memcpy(WFIFOP(fd,12+73*j), msg->title, MAIL_TITLE_LENGTH);
		WFIFOB(fd,52+73*j) = (msg->status != MAIL_UNREAD);
		safestrncpy(WFIFOP(fd,53+73*j), msg->send_name, NAME_LENGTH);
		WFIFOL(fd,77+73*j) = (uint32)msg->timestamp;
		j++;
	}
	WFIFOSET(fd,len);

	if( md->full )
	{// TODO: is this official?
		char output[100];
		sprintf(output, "Inbox is full (Max %d). Delete some mails.", MAIL_MAX_INBOX);
		clif_disp_onlyself(sd, output, strlen(output));
	}
#else
	int fd = sd->fd;
	struct mail_data *md = &sd->mail.inbox;
	struct mail_message *msg;
	int i, j, k, offset, titleLength;
	uint8 mailType, amount, remaining;
	uint32 now = (uint32)time(NULL);
#if PACKETVER >= 20170419
	int cmd = 0xac2;
#elif PACKETVER >= 20160601
	int cmd = 0xa7d;
#else
	int cmd = 0x9f0;
#endif

	if( battle_config.mail_daily_count ){
		mail_refresh_remaining_amount(sd);
	}

#if PACKETVER >= 20170419
	// Always send all
	i = md->amount;
#else
	// If a starting mail id was sent
	if( mailID != 0 ){
		ARR_FIND( 0, md->amount, i, md->msg[i].id == mailID );

		// Unknown mail
		if( i > md->amount ){
			// Ignore the request for now
			return; // TODO: Should we just show the first page instead?
		}

		// It was actually the oldest/first mail, there is no further page
		if( i < 0 ){
			return;
		}

		// We actually want the next/older mail
		// Enabling ths will break the "next" Button if only one Mail would be on the next page... [15peaces]
		//i -= 1;
	}else{
		i = md->amount;
	}
#endif
	
	// Count the remaining mails from the starting mail or the beginning
	// Only count mails of the target type and those that should not have been deleted already
	for( j = i, remaining = 0; j >= 0; j-- ){
		msg = &md->msg[j];

		if (msg->id < 1)
			continue;
#if PACKETVER < 20170419
		if (msg->type != type)
			continue;
#endif
		if (msg->scheduled_deletion > 0 && msg->scheduled_deletion <= now)
			continue;

		remaining++;
	}

#if PACKETVER >= 20170419
	// Always send all
	amount = remaining;
#else
	if (remaining > MAIL_PAGE_SIZE) {
		amount = MAIL_PAGE_SIZE;
	}
	else {
		amount = remaining;
	}
#endif

	WFIFOHEAD(fd, 7 + ((44 + MAIL_TITLE_LENGTH) * amount));
	WFIFOW(fd, 0) = cmd;
#if PACKETVER >= 20170419
	WFIFOB(fd, 4) = 1; // Unknown
	offset = 5;
#else
	WFIFOB(fd, 4) = type;
	WFIFOB(fd, 5) = amount;
	WFIFOB(fd, 6) = ( remaining < MAIL_PAGE_SIZE ); // last page
	offset = 7;
#endif

	for( amount = 0; i >= 0; i-- ){
		msg = &md->msg[i];

		if (msg->id < 1)
			continue;
#if PACKETVER < 20170419
		if (msg->type != type)
			continue;
#endif
		if (msg->scheduled_deletion > 0 && msg->scheduled_deletion <= now)
			continue;

#if PACKETVER >= 20170419
		WFIFOB(fd, offset) = msg->type;
		offset += 1;
#endif

		WFIFOQ(fd, offset + 0) = (uint64)msg->id;
		WFIFOB(fd, offset + 8) = (msg->status != MAIL_UNREAD);

		mailType = MAIL_TYPE_TEXT; // Normal letter

		if( msg->zeny > 0 ){
			mailType |= MAIL_TYPE_ZENY; // Mail contains zeny
		}

		for( k = 0; k < MAIL_MAX_ITEM; k++ ){
			if( msg->item[k].nameid > 0 ){
				mailType |= MAIL_TYPE_ITEM; // Mail contains items
				break;
			}
		}

#if PACKETVER >= 20170419
		// If it came from an npc?
		if (!msg->send_id) {
			mailType |= MAIL_TYPE_NPC;
		}
#endif

		WFIFOB(fd, offset + 9) = mailType;
		safestrncpy(WFIFOCP(fd, offset + 10), msg->send_name, NAME_LENGTH);

#if PACKETVER < 20170419
		// How much time has passed since you received the mail
		WFIFOL(fd, offset + 34 ) = now - (uint32)msg->timestamp;
		offset += 4;
#endif

		// If automatic return/deletion of mails is enabled, notify the client when it will kick in
		if( msg->scheduled_deletion > 0 ){
			WFIFOL(fd, offset + 34) = (uint32)msg->scheduled_deletion - now;
		}else{
			// Fake the scheduled deletion to one year in the future
			// Sadly the client always displays the scheduled deletion after 24 hours no matter how high this value gets [Lemongrass]
			WFIFOL(fd, offset + 34) = 365 * 24 * 60 * 60;
		}

		WFIFOW(fd, offset + 38) = titleLength = (int16)(strlen(msg->title) + 1);
		safestrncpy(WFIFOCP(fd, offset + 40), msg->title, titleLength);

		offset += 40 + titleLength;
	}
	WFIFOW(fd, 2) = (int16)offset;
	WFIFOSET(fd, offset);
#endif
}

/// Mail inbox list request.
/// 023f (CZ_MAIL_GET_LIST)
/// 09e8 <mail tab>.B <mail id>.Q (CZ_OPEN_MAILBOX)
/// 09ee <mail tab>.B <mail id>.Q (CZ_REQ_NEXT_MAIL_LIST)
/// 09ef <mail tab>.B <mail id>.Q (CZ_REQ_REFRESH_MAIL_LIST)
/// 0ac0 <mail id>.Q <unknown>.16B (CZ_OPEN_MAILBOX2)
/// 0ac1 <mail id>.Q <unknown>.16B (CZ_REQ_REFRESH_MAIL_LIST2)
void clif_parse_Mail_refreshinbox(int fd, struct map_session_data *sd){
#if PACKETVER < 20150513
	struct mail_data* md = &sd->mail.inbox;

	if( md->amount < MAIL_MAX_INBOX && (md->full || sd->mail.changed) )
		intif_Mail_requestinbox(sd->status.char_id, 1, MAIL_INBOX_NORMAL);
	else
		clif_Mail_refreshinbox(sd, MAIL_INBOX_NORMAL,0);

	mail_removeitem(sd, 0, sd->mail.item[0].index, sd->mail.item[0].amount);
	mail_removezeny(sd, false);
#else
	int cmd = RFIFOW(fd, 0);
#if PACKETVER < 20170419
	uint8 openType = RFIFOB(fd, 2);
	uint64 mailId = RFIFOQ(fd, 3);
#else
	uint8 openType;
	uint64 mailId = RFIFOQ(fd, 2);
	int i;

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mailId);

	if (i == MAIL_MAX_INBOX) {
		openType = MAIL_INBOX_NORMAL;
		mailId = 0;
	}
	else {
		openType = sd->mail.inbox.msg[i].type;
		mailId = 0;
	}
#endif

	switch( openType ){
		case MAIL_INBOX_NORMAL:
		case MAIL_INBOX_ACCOUNT:
		case MAIL_INBOX_RETURNED:
			break;
		default:
			// Unknown type: ignore request
			return;
	}

	if (sd->mail.changed || (cmd == 0x9ef || cmd == 0xac1)){
		intif_Mail_requestinbox(sd->status.char_id, 1, (enum mail_inbox_type)openType);
		return;
	}

	// If it is not a next page request
	if (cmd != 0x9ee){
		mailId = 0;
	}

	clif_Mail_refreshinbox(sd, (enum mail_inbox_type)openType, mailId);
#endif
}

/// Opens a mail
/// 0242 <packet len>.W <mail id>.L <title>.40B <sender>.24B <time>.L <zeny>.L (ZC_MAIL_REQ_OPEN)
///     <amount>.L <name id>.W <item type>.W <identified>.B <damaged>.B <refine>.B
///     <card1>.W <card2>.W <card3>.W <card4>.W <message>.?B
/// 09eb <packet len>.W <type>.B <mail id>.Q <message length>.W <zeny>.Q <item count>.B (ZC_ACK_READ_MAIL)
///		{  }*n
// TODO: Packet description => for repeated block
void clif_Mail_read(struct map_session_data *sd, int mail_id) {
	int i, fd = sd->fd;

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
	if (i == MAIL_MAX_INBOX) {
		clif_Mail_return(sd->fd, mail_id, 1); // Mail doesn't exist
		ShowWarning("clif_parse_Mail_read: char '%s' trying to read a message not the inbox.\n", sd->status.name);
		return;
	}
	else {
		struct mail_message *msg = &sd->mail.inbox.msg[i];
		struct item *item;
		struct item_data *data;
		int msg_len = strlen(msg->body);

		if (msg_len == 0) {
			strcpy(msg->body, "(no message)"); // TODO: confirm for RODEX
			msg_len = strlen(msg->body);
		}

#if PACKETVER < 20150513
		item = &msg->item[0];

		int len = 101 + msg_len;

		WFIFOHEAD(fd, len);
		WFIFOW(fd, 0) = 0x242;
		WFIFOW(fd, 2) = len;
		WFIFOL(fd, 4) = msg->id;
		safestrncpy(WFIFOCP(fd, 8), msg->title, MAIL_TITLE_LENGTH + 1);
		safestrncpy(WFIFOCP(fd, 48), msg->send_name, NAME_LENGTH + 1);
		WFIFOL(fd, 72) = 0;
		WFIFOL(fd, 76) = msg->zeny;

		if (item->nameid && (data = itemdb_exists(item->nameid)) != NULL) {
			WFIFOL(fd, 80) = item->amount;
			WFIFOW(fd, 84) = client_nameid(item->nameid);
			WFIFOW(fd, 86) = itemtype(item->nameid);
			WFIFOB(fd, 88) = item->identify;
			WFIFOB(fd, 89) = item->attribute;
			WFIFOB(fd, 90) = item->refine;
			WFIFOW(fd, 91) = item->card[0];
			WFIFOW(fd, 93) = item->card[1];
			WFIFOW(fd, 95) = item->card[2];
			WFIFOW(fd, 97) = item->card[3];
		}
		else // no item, set all to zero
			memset(WFIFOP(fd, 80), 0x00, 19);

		WFIFOB(fd, 99) = (unsigned char)msg_len;
		safestrncpy(WFIFOCP(fd, 100), msg->body, msg_len + 1);
		WFIFOSET(fd, len);
#else
		msg_len += 1; // Zero Termination

		int length = sizeof(struct PACKET_ZC_READ_MAIL) + MAIL_BODY_LENGTH + sizeof(struct mail_item) * MAIL_MAX_ITEM;
		WFIFOHEAD(fd, length);
		struct PACKET_ZC_READ_MAIL *p = (struct PACKET_ZC_READ_MAIL *)WFIFOP(fd, 0);

		p->PacketType = 0x09EB;
		p->PacketLength = length;
		p->opentype = msg->type;
		p->MailID = msg->id;
		p->TextcontentsLength = msg_len;
		p->zeny = msg->zeny;

		int offset = sizeof(struct PACKET_ZC_READ_MAIL);

		safestrncpy(WFIFOCP(fd, offset), msg->body, msg_len);

		offset += msg_len;

		int count = 0;
		for (int j = 0; j < MAIL_MAX_ITEM; j++) {
			item = &msg->item[j];

			if (item->nameid > 0 && item->amount > 0 && (data = itemdb_exists(item->nameid)) != NULL) {
				struct mail_item* mailitem = (struct mail_item *)WFIFOP(fd, offset);

				mailitem->ITID = client_nameid(item->nameid);
				mailitem->count = item->amount;
				mailitem->type = itemtype(item->nameid);
				mailitem->IsIdentified = item->identify ? 1 : 0;
				mailitem->IsDamaged = item->attribute ? 1 : 0;
				mailitem->refiningLevel = item->refine;
				mailitem->location = pc_equippoint_sub(sd, data);
				mailitem->viewSprite = data->look;
				mailitem->bindOnEquip = item->bound ? 2 : data->flag.bindOnEquip ? 1 : 0;
				clif_addcards(&mailitem->slot, item);
				clif_add_random_options(mailitem->optionData, item);

				offset += sizeof(struct mail_item);
				count++;
			}
		}

		p->ItemCnt = count;
		p->PacketLength = sizeof(struct PACKET_ZC_READ_MAIL) + p->TextcontentsLength + sizeof(struct mail_item) * p->ItemCnt;

		WFIFOSET(fd, p->PacketLength);
#endif

		if (msg->status == MAIL_UNREAD) {
			msg->status = MAIL_READ;
			intif_Mail_read(mail_id);
			clif_parse_Mail_refreshinbox(fd, sd);

			sd->mail.inbox.unread--;
			clif_Mail_new(sd, 0, msg->send_name, msg->title);
		}
	}
}

/// Request to open a mail.
/// 0241 <mail id>.L (CZ_MAIL_OPEN)
/// 09ea <mail tab>.B <mail id>.Q (CZ_REQ_READ_MAIL)
void clif_parse_Mail_read(int fd, struct map_session_data *sd){
#if PACKETVER < 20150513
	int mail_id = RFIFOL(fd,packet_db[RFIFOW(fd,0)]->pos[0]);
#else
	//uint8 openType = RFIFOB(fd, 2);
	int mail_id = (int)RFIFOQ(fd, 3);
#endif

	if( mail_id <= 0 )
		return;
	if( mail_invalid_operation(sd) )
		return;

	clif_Mail_read(sd, mail_id);
}

/// Allow a player to begin writing a mail
/// 0a12 <receiver>.24B <success>.B (ZC_ACK_OPEN_WRITE_MAIL)
void clif_send_Mail_beginwrite_ack( struct map_session_data *sd, char* name, bool success ){
	int fd = sd->fd;

	WFIFOHEAD(fd, 27);
	WFIFOW(fd, 0) = 0xa12;
	safestrncpy(WFIFOCP(fd, 2), name, NAME_LENGTH);
	WFIFOB(fd, 26) = success;
	WFIFOSET(fd, 27);
}

/// Request to start writing a mail
/// 0a08 <receiver>.24B (CZ_REQ_OPEN_WRITE_MAIL)
void clif_parse_Mail_beginwrite( int fd, struct map_session_data *sd ){
	char name[NAME_LENGTH];

	safestrncpy(name, RFIFOCP(fd, 2), NAME_LENGTH);

	if( sd->state.storage_flag || sd->state.mail_writing || sd->trade_partner || sd->npc_id > 0){
		clif_send_Mail_beginwrite_ack(sd, name, false);
		return;
	}

	mail_clear(sd);

	sd->state.mail_writing = true;
	clif_send_Mail_beginwrite_ack(sd, name,true);
}

/// Notification that the client cancelled writing a mail
/// 0a03 (CZ_REQ_CANCEL_WRITE_MAIL)
void clif_parse_Mail_cancelwrite( int fd, struct map_session_data *sd ){
	sd->state.mail_writing = false;
}

/// Give the client information about the recipient, if available
/// 0a14 <char id>.L <class>.W <base level>.W (ZC_CHECK_RECEIVE_CHARACTER_NAME)
/// 0a51 <char id>.L <class>.W <base level>.W <name>.24B (ZC_CHECK_RECEIVE_CHARACTER_NAME2)
void clif_Mail_Receiver_Ack( struct map_session_data* sd, uint32 char_id, short class_, uint32 level, const char* name ){
	int fd = sd->fd;
#if PACKETVER <= 20160302
	int cmd = 0xa14;
#else
	int cmd = 0xa51;
#endif

	WFIFOHEAD(fd, packet_len(cmd));
	WFIFOW(fd, 0) = cmd;
	WFIFOL(fd, 2) = char_id;
	WFIFOW(fd, 6) = class_;
	WFIFOW(fd, 8) = level;
#if PACKETVER >= 20160302
	strncpy(WFIFOCP(fd, 10), name, NAME_LENGTH);
#endif
	WFIFOSET(fd, packet_len(cmd));
}

/// Request information about the recipient
/// 0a13 <name>.24B (CZ_CHECK_RECEIVE_CHARACTER_NAME)
void clif_parse_Mail_Receiver_Check(int fd, struct map_session_data *sd) {
	static char name[NAME_LENGTH];

	safestrncpy(name, RFIFOCP(fd, 2), NAME_LENGTH);

	intif_mail_checkreceiver(sd, name);
}

/// Request to receive mail's attachment.
/// 0244 <mail id>.L (CZ_MAIL_GET_ITEM)
/// 09f1 <mail id>.Q <mail tab>.B (CZ_REQ_ZENY_FROM_MAIL)
/// 09f3 <mail id>.Q <mail tab>.B (CZ_REQ_ITEM_FROM_MAIL)
void clif_parse_Mail_getattach( int fd, struct map_session_data *sd ){
	int i;
	struct mail_message* msg;
#if PACKETVER < 20150513
	int mail_id = RFIFOL(fd, packet_db[sd->packet_ver][RFIFOW(fd, 0)].pos[0]);
	int attachment = MAIL_ATT_ALL;
#else
	uint16 packet_id = RFIFOW(fd, 0);
	int mail_id = (int)RFIFOQ(fd, 2);
	//int openType = RFIFOB(fd, 10);
	int attachment = packet_id == 0x9f1 ? MAIL_ATT_ZENY : packet_id == 0x9f3 ? MAIL_ATT_ITEM : MAIL_ATT_NONE;
#endif

	if (!chrif_isconnected())
		return;
	if( mail_id <= 0 )
		return;
	if( mail_invalid_operation(sd) )
		return;

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
	if( i == MAIL_MAX_INBOX )
		return;

	msg = &sd->mail.inbox.msg[i];

	if( attachment&MAIL_ATT_ZENY && msg->zeny < 1 ){
		attachment &= ~MAIL_ATT_ZENY;
	}

	if( attachment&MAIL_ATT_ITEM ){
		ARR_FIND(0, MAIL_MAX_ITEM, i, msg->item[i].nameid > 0 || msg->item[i].amount > 0);

		// No items were found
		if( i == MAIL_MAX_ITEM ){
			attachment &= ~MAIL_ATT_ITEM;
		}
	}

	// Either no attachment requested at all or there are no zeny or items in the mail
	if( attachment == MAIL_ATT_NONE ){
		return;
	}

	if( attachment&MAIL_ATT_ZENY ){
		if (msg->zeny > MAX_ZENY - sd->status.zeny){
			clif_mail_getattachment(sd, msg, 1, MAIL_ATT_ZENY); //too many zeny
			return;
		}else{
			// To make sure another request fails
			msg->zeny = 0;
		}
	}

	if( attachment&MAIL_ATT_ITEM ){
		int new_ = 0, totalweight = 0;

		for( i = 0; i < MAIL_MAX_ITEM; i++ ){
			struct item* item = &msg->item[i];

			if( item->nameid > 0 && item->amount > 0 ){
				struct item_data *data;

				if((data = itemdb_exists(item->nameid)) == NULL)
					continue;

				switch( pc_checkadditem(sd, item->nameid, item->amount) ){
					case CHKADDITEM_NEW:
						new_++;
						break;
					case CHKADDITEM_OVERAMOUNT:
						clif_mail_getattachment(sd, msg, 2, MAIL_ATT_ITEM);
						return;
				}

				totalweight += data->weight * item->amount;
			}
		}

		if( ( totalweight + sd->weight ) > sd->max_weight ){
			clif_mail_getattachment(sd, msg, 2, MAIL_ATT_ITEM);
			return;
		}else if( pc_inventoryblank(sd) < new_ ){
			clif_mail_getattachment(sd, msg, 2, MAIL_ATT_ITEM);
			return;
		}

		// To make sure another request fails
		memset(msg->item, 0, MAIL_MAX_ITEM*sizeof(struct item));
	}

	intif_mail_getattach(sd,msg,attachment);
	clif_Mail_read(sd, mail_id);
}


/// Request to delete a mail.
/// 0243 <mail id>.L (CZ_MAIL_DELETE)
/// 09f5 <mail tab>.B <mail id>.Q (CZ_REQ_DELETE_MAIL)
void clif_parse_Mail_delete(int fd, struct map_session_data *sd) {
#if PACKETVER < 20150513
	int mail_id = RFIFOL(fd,2);
#else
	int openType = RFIFOB(fd, 2);
	int mail_id = (int)RFIFOQ(fd, 3);
#endif
	int i, j;


	if (!chrif_isconnected())
		return;
	if( mail_id <= 0 )
		return;
	if( mail_invalid_operation(sd) )
		return;

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
	if (i < MAIL_MAX_INBOX)
	{
		struct mail_message *msg = &sd->mail.inbox.msg[i];

		// can't delete mail without removing zeny first
		if( msg->zeny > 0 ){
			clif_mail_delete(sd, msg, false);
			return;
		}
		
		// can't delete mail without removing attachment(s) first
		for( j = 0; j < MAIL_MAX_ITEM; j++ ){
			if( msg->item[j].nameid > 0 && msg->item[j].amount > 0 ){
				clif_mail_delete(sd, msg, false);
				return;
			}
		}

		if( intif_Mail_delete(sd->status.char_id, mail_id) && msg->status == MAIL_UNREAD ){
			sd->mail.inbox.unread--;
			clif_Mail_new(sd,0,NULL,NULL);
		}
	}
}


/// Request to return a mail (CZ_REQ_MAIL_RETURN).
/// 0273 <mail id>.L <receive name>.24B
void clif_parse_Mail_return(int fd, struct map_session_data *sd)
{
	int mail_id = RFIFOL(fd,2);
	int i;

	if( mail_id <= 0 )
		return;
	if( mail_invalid_operation(sd) )
		return;

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
	if( i < MAIL_MAX_INBOX && sd->mail.inbox.msg[i].send_id != 0 )
		intif_Mail_return(sd->status.char_id, mail_id);
	else
		clif_Mail_return(sd->fd, mail_id, 1);
}

/// Request to add an item or Zeny to mail.
/// 0247 <index>.W <amount>.L (CZ_MAIL_ADD_ITEM)
/// 0a04 <index>.W <amount>.W (CZ_REQ_ADD_ITEM_TO_MAIL)
void clif_parse_Mail_setattach(int fd, struct map_session_data *sd)
{
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
	int idx = RFIFOW(fd,info->pos[0]);

#if PACKETVER < 20150513
	int amount = RFIFOL(fd,info->pos[1]);
#else
	int amount = RFIFOW(fd,info->pos[1]);
#endif
	unsigned char flag;

	if (!chrif_isconnected())
		return;
	if (idx < 0 || amount < 0)
		return;

	flag = mail_setitem(sd, idx, amount);
	if (flag == MAIL_ATTACH_EQUIPSWITCH) {
		clif_msg(sd, C_ITEM_EQUIP_SWITCH);
	}
	else {
		clif_Mail_setattachment(sd, idx, amount, flag);
	}
}

/// Remove an item from a mail
/// 0a07 <result>.B <index>.W <amount>.W <weight>.W
void clif_mail_removeitem( struct map_session_data* sd, bool success, int index, int amount ){
	int fd = sd->fd;

	WFIFOHEAD(fd, 9);
	WFIFOW(fd, 0) = 0xa07;
	WFIFOB(fd, 2) = success;
	WFIFOW(fd, 3) = index;
	WFIFOW(fd, 5) = amount;
	WFIFOW(fd, 7) = 0; // TODO: which weight? item weight? removed weight? remaining weight?
	WFIFOSET(fd, 9);
}

/// Request to reset mail item and/or Zeny
/// 0246 <type>.W (CZ_MAIL_RESET_ITEM)
/// type:
///     0 = reset all
///     1 = remove item
///     2 = remove zeny
/// 0a06 <index>.W <amount>.W (CZ_REQ_REMOVE_ITEM_MAIL)
void clif_parse_Mail_winopen(int fd, struct map_session_data *sd)
{
#if PACKETVER < 20150513
	int flag = RFIFOW(fd,2);

	if (flag == 0 || flag == 1)
		mail_removeitem(sd, 0, sd->mail.item[0].index, sd->mail.item[0].amount);
	if (flag == 0 || flag == 2)
		mail_removezeny(sd, false);
#else
	uint16 index = RFIFOW(fd, 2);
	uint16 count = RFIFOW(fd, 4);

	mail_removeitem(sd,0,index,count);
#endif
}

/// Request to send mail
/// 0248 <packet len>.W <recipient>.24B <title>.40B <body len>.B <body>.?B (CZ_MAIL_SEND)
/// 09ec <packet len>.W <recipient>.24B <sender>.24B <zeny>.Q <title length>.W <body length>.W <title>.?B <body>.?B (CZ_REQ_WRITE_MAIL)
/// 0a6e <packet len>.W <recipient>.24B <sender>.24B <zeny>.Q <title length>.W <body length>.W <char id>.L <title>.?B <body>.?B (CZ_REQ_WRITE_MAIL2)
void clif_parse_Mail_send(int fd, struct map_session_data *sd){
	if (!chrif_isconnected())
		return;

#if PACKETVER < 20150513
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	if( RFIFOW(fd,info->pos[0]) < 69 ) {
		ShowWarning("Invalid Msg Len from account %d.\n", sd->status.account_id);
		return;
	}

	mail_send(sd, RFIFOCP(fd,info->pos[1]), RFIFOCP(fd,info->pos[2]), RFIFOCP(fd,info->pos[4]), RFIFOB(fd,info->pos[3]));
#else
	static char receiver[NAME_LENGTH];
	//static char sender[NAME_LENGTH];
	char *title;
	char *text;
	uint64 zeny;
	uint16 length, titleLength, textLength, realTitleLength, realTextLength;

	length = RFIFOW(fd, 2);

	if( length < 0x3e ){
		ShowWarning("Too short...\n");
		clif_Mail_send(sd, WRITE_MAIL_FAILED);
		return;
	}

	// Forged request without a begin writing packet?
	if( !sd->state.mail_writing ){
		return; // Ignore it
	}

	safestrncpy(receiver, RFIFOCP(fd, 4), NAME_LENGTH);
	//safestrncpy(sender, RFIFOCP(fd, 28), NAME_LENGTH);
	zeny = RFIFOQ(fd, 52);
	titleLength = RFIFOW(fd, 60);
	textLength = RFIFOW(fd, 62);

	realTitleLength = min(titleLength, MAIL_TITLE_LENGTH);
	realTextLength = min(textLength, MAIL_BODY_LENGTH);

	title = (char*)aMalloc(realTitleLength);
	text = (char*)aMalloc(realTextLength);

#if PACKETVER <= 20160330
	safestrncpy(title, RFIFOCP(fd, 64), realTitleLength);
	safestrncpy(text, RFIFOCP(fd, 64 + titleLength), realTextLength);
#else
	// 64 = <char id>.L
	safestrncpy(title, RFIFOCP(fd, 68), realTitleLength);
	safestrncpy(text, RFIFOCP(fd, 68 + titleLength), realTextLength);
#endif

	if( zeny > 0 ){
		if( mail_setitem(sd,0,(uint32)zeny) != MAIL_ATTACH_SUCCESS ){
			clif_Mail_send(sd,WRITE_MAIL_FAILED);
			return;
		}
	}

	mail_send(sd, receiver, title, text, textLength);

	aFree(title);
	aFree(text);
#endif
}


/// AUCTION SYSTEM
/// By Zephyrus
///

/// Opens/closes the auction window (ZC_AUCTION_WINDOWS).
/// 025f <type>.L
/// type:
///     0 = open
///     1 = close
void clif_Auction_openwindow(struct map_session_data *sd)
{
	int fd = sd->fd;

	if( sd->state.storage_flag || sd->state.vending || sd->state.buyingstore || sd->state.trading )
		return;

	if (!battle_config.feature_auction)
		return;

	WFIFOHEAD(fd,packet_len(0x25f));
	WFIFOW(fd,0) = 0x25f;
	WFIFOL(fd,2) = 0;
	WFIFOSET(fd,packet_len(0x25f));
}


/// Returns auction item search results (ZC_AUCTION_ITEM_REQ_SEARCH).
/// 0252 <packet len>.W <pages>.L <count>.L { <auction id>.L <seller name>.24B <name id>.W <type>.L <amount>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <now price>.L <max price>.L <buyer name>.24B <delete time>.L }*
void clif_Auction_results(struct map_session_data *sd, short count, short pages, uint8 *buf)
{
	int i, fd = sd->fd, len = sizeof(struct auction_data);
	struct auction_data auction;
	struct item_data *item;
	int k;

	WFIFOHEAD(fd,12 + (count * 83));
	WFIFOW(fd,0) = 0x252;
	WFIFOW(fd,2) = 12 + (count * 83);
	WFIFOL(fd,4) = pages;
	WFIFOL(fd,8) = count;

	for( i = 0; i < count; i++ )
	{
		memcpy(&auction, RBUFP(buf,i * len), len);
		k = 12 + (i * 83);

		WFIFOL(fd,k) = auction.auction_id;
		safestrncpy((char*)WFIFOP(fd,4+k), auction.seller_name, NAME_LENGTH);

		if( (item = itemdb_exists(auction.item.nameid)) != NULL && item->view_id > 0 )
			WFIFOW(fd,28+k) = item->view_id;
		else
			WFIFOW(fd,28+k) = auction.item.nameid;

		WFIFOL(fd,30+k) = auction.type;
		WFIFOW(fd,34+k) = auction.item.amount; // Always 1
		WFIFOB(fd,36+k) = auction.item.identify;
		WFIFOB(fd,37+k) = auction.item.attribute;
		WFIFOB(fd,38+k) = auction.item.refine;
		WFIFOW(fd,39+k) = auction.item.card[0];
		WFIFOW(fd,41+k) = auction.item.card[1];
		WFIFOW(fd,43+k) = auction.item.card[2];
		WFIFOW(fd,45+k) = auction.item.card[3];
		WFIFOL(fd,47+k) = auction.price;
		WFIFOL(fd,51+k) = auction.buynow;
		safestrncpy((char*)WFIFOP(fd,55+k), auction.buyer_name, NAME_LENGTH);
		WFIFOL(fd,79+k) = (uint32)auction.timestamp;
	}
	WFIFOSET(fd,WFIFOW(fd,2));
}


/// Result from request to add an item (ZC_ACK_AUCTION_ADD_ITEM).
/// 0256 <index>.W <result>.B
/// result:
///     0 = success
///     1 = failure
static void clif_Auction_setitem(int fd, int index, bool fail)
{
	WFIFOHEAD(fd,packet_len(0x256));
	WFIFOW(fd,0) = 0x256;
	WFIFOW(fd,2) = index;
	WFIFOB(fd,4) = fail;
	WFIFOSET(fd,packet_len(0x256));
}


/// Request to initialize 'new auction' data (CZ_AUCTION_CREATE).
/// 024b <type>.W
/// type:
///     0 = create (any other action in auction window)
///     1 = cancel (cancel pressed on register tab)
///     ? = junk, uninitialized value (ex. when switching between list filters)
void clif_parse_Auction_cancelreg(int fd, struct map_session_data *sd)
{
	if( sd->auction.amount > 0 )
		clif_additem(sd, sd->auction.index, sd->auction.amount, 0);

	sd->auction.amount = 0;
}


/// Request to add an item to the action (CZ_AUCTION_ADD_ITEM).
/// 024c <index>.W <count>.L
void clif_parse_Auction_setitem(int fd, struct map_session_data *sd)
{
	int idx = RFIFOW(fd,2) - 2;
	int amount = RFIFOL(fd,4); // Always 1
	struct item_data *item;

	if (!battle_config.feature_auction)
		return;

	if( sd->auction.amount > 0 )
		sd->auction.amount = 0;

	if( idx < 0 || idx >= MAX_INVENTORY )
	{
		ShowWarning("Character %s trying to set invalid item index in auctions.\n", sd->status.name);
		return;
	}

	if( amount != 1 || amount > sd->inventory.u.items_inventory[idx].amount )
	{ // By client, amount is always set to 1. Maybe this is a future implementation.
		ShowWarning("Character %s trying to set invalid amount in auctions.\n", sd->status.name);
		return;
	}

	if( (item = itemdb_exists(sd->inventory.u.items_inventory[idx].nameid)) != NULL && !(item->type == IT_ARMOR || item->type == IT_PETARMOR || item->type == IT_WEAPON || item->type == IT_CARD || item->type == IT_ETC || item->type == IT_SHADOWGEAR) )
	{ // Consumable or pets are not allowed
		clif_Auction_setitem(sd->fd, idx, true);
		return;
	}
	
	if(!pc_can_give_items(pc_isGM(sd)) || !sd->inventory.u.items_inventory[idx].identify ||
		!itemdb_canauction(&sd->inventory.u.items_inventory[idx], pc_isGM(sd)) || !itemdb_available(sd->inventory.u.items_inventory[idx].nameid))
	{ // Quest Item or something else
		clif_Auction_setitem(sd->fd, idx, true);
		return;
	}
	
	sd->auction.index = idx;
	sd->auction.amount = amount;
	clif_Auction_setitem(fd, idx + 2, false);
}


/// Result from an auction action (ZC_AUCTION_RESULT).
/// 0250 <result>.B
/// result:
///     0 = You have failed to bid into the auction
///     1 = You have successfully bid in the auction
///     2 = The auction has been canceled
///     3 = An auction with at least one bidder cannot be canceled
///     4 = You cannot register more than 5 items in an auction at a time
///     5 = You do not have enough Zeny to pay the Auction Fee
///     6 = You have won the auction
///     7 = You have failed to win the auction
///     8 = You do not have enough Zeny
///     9 = You cannot place more than 5 bids at a time
void clif_Auction_message(int fd, unsigned char flag)
{
	WFIFOHEAD(fd,packet_len(0x250));
	WFIFOW(fd,0) = 0x250;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x250));
}


/// Result of the auction close request (ZC_AUCTION_ACK_MY_SELL_STOP).
/// 025e <result>.W
/// result:
///     0 = You have ended the auction
///     1 = You cannot end the auction
///     2 = Auction ID is incorrect
void clif_Auction_close(int fd, unsigned char flag)
{
	WFIFOHEAD(fd,packet_len(0x25e));
	WFIFOW(fd,0) = 0x25d;  // BUG: The client identifies this packet as 0x25d (CZ_AUCTION_REQ_MY_SELL_STOP)
	WFIFOW(fd,2) = flag;
	WFIFOSET(fd,packet_len(0x25e));
}


/// Request to add an auction (CZ_AUCTION_ADD).
/// 024d <now money>.L <max money>.L <delete hour>.W
void clif_parse_Auction_register(int fd, struct map_session_data *sd)
{
	struct auction_data auction;
	struct item_data *item;

	if (!battle_config.feature_auction)
		return;

	auction.price = RFIFOL(fd,2);
	auction.buynow = RFIFOL(fd,6);
	auction.hours = RFIFOW(fd,10);

	// Invalid Situations...
	if( sd->auction.amount < 1 )
	{
		ShowWarning("Character %s trying to register auction without item.\n", sd->status.name);
		return;
	}

	if( auction.price >= auction.buynow )
	{
		ShowWarning("Character %s trying to alter auction prices.\n", sd->status.name);
		return;
	}

	if( auction.hours < 1 || auction.hours > 48 )
	{
		ShowWarning("Character %s trying to enter an invalid time for auction.\n", sd->status.name);
		return;
	}

	// Auction checks...
	if( sd->inventory.u.items_inventory[sd->auction.index].bound && !pc_can_give_bounded_items(sd->gmlevel) ) { 
		clif_displaymessage(sd->fd, msg_txt(sd,293)); 
		clif_Auction_message(fd, 2); // The auction has been canceled 
		return; 
	} 
	if( sd->status.zeny < (auction.hours * battle_config.auction_feeperhour) )
	{
		clif_Auction_message(fd, 5); // You do not have enough zeny to pay the Auction Fee.
		return;
	}

	if( auction.buynow > battle_config.auction_maximumprice )
	{ // Zeny Limits
		auction.buynow = battle_config.auction_maximumprice;
		if( auction.price >= auction.buynow )
			auction.price = auction.buynow - 1;
	}

	auction.auction_id = 0;
	auction.seller_id = sd->status.char_id;
	safestrncpy(auction.seller_name, sd->status.name, sizeof(auction.seller_name));
	auction.buyer_id = 0;
	memset(auction.buyer_name, '\0', sizeof(auction.buyer_name));

	if( sd->inventory.u.items_inventory[sd->auction.index].nameid == 0 || sd->inventory.u.items_inventory[sd->auction.index].amount < sd->auction.amount )
	{
		clif_Auction_message(fd, 2); // The auction has been canceled
		return;
	}

	if( (item = itemdb_exists(sd->inventory.u.items_inventory[sd->auction.index].nameid)) == NULL )
	{ // Just in case
		clif_Auction_message(fd, 2); // The auction has been canceled
		return;
	}

	safestrncpy(auction.item_name, item->jname, sizeof(auction.item_name));
	auction.type = item->type;
	memcpy(&auction.item, &sd->inventory.u.items_inventory[sd->auction.index], sizeof(struct item));
	auction.item.amount = 1;
	auction.timestamp = 0;

	if( !intif_Auction_register(&auction) )
		clif_Auction_message(fd, 4); // No Char Server? lets say something to the client
	else
	{
		int zeny = auction.hours*battle_config.auction_feeperhour;

		pc_delitem(sd, sd->auction.index, sd->auction.amount, 1, 6, LOG_TYPE_AUCTION);
		sd->auction.amount = 0;

		pc_payzeny(sd, zeny, LOG_TYPE_AUCTION, NULL);
	}
}


/// Cancels an auction (CZ_AUCTION_ADD_CANCEL).
/// 024e <auction id>.L
void clif_parse_Auction_cancel(int fd, struct map_session_data *sd)
{
	unsigned int auction_id = RFIFOL(fd,2);

	intif_Auction_cancel(sd->status.char_id, auction_id);
}


/// Closes an auction (CZ_AUCTION_REQ_MY_SELL_STOP).
/// 025d <auction id>.L
void clif_parse_Auction_close(int fd, struct map_session_data *sd)
{
	unsigned int auction_id = RFIFOL(fd,2);

	intif_Auction_close(sd->status.char_id, auction_id);
}


/// Places a bid on an auction (CZ_AUCTION_BUY).
/// 024f <auction id>.L <money>.L
void clif_parse_Auction_bid(int fd, struct map_session_data *sd)
{
	unsigned int auction_id = RFIFOL(fd,2);
	int bid = RFIFOL(fd,6);

	if( !pc_can_give_items(pc_isGM(sd)) )
	{ //They aren't supposed to give zeny [Inkfish]
		clif_displaymessage(sd->fd, msg_txt(sd,246));
		return;
	}

	if( bid <= 0 )
		clif_Auction_message(fd, 0); // You have failed to bid into the auction
	else if( bid > sd->status.zeny )
		clif_Auction_message(fd, 8); // You do not have enough zeny
	else if (CheckForCharServer()) // char server is down (bugreport:1138)
		clif_Auction_message(fd, 0); // You have failed to bid into the auction
	else {
		pc_payzeny(sd, bid, LOG_TYPE_AUCTION, NULL);
		intif_Auction_bid(sd->status.char_id, sd->status.name, auction_id, bid);
	}
}


/// Auction Search (CZ_AUCTION_ITEM_SEARCH).
/// 0251 <search type>.W <auction id>.L <search text>.24B <page number>.W
/// search type:
///     0 = armor
///     1 = weapon
///     2 = card
///     3 = misc
///     4 = name search
///     5 = auction id search
void clif_parse_Auction_search(int fd, struct map_session_data* sd)
{
	char search_text[NAME_LENGTH];
	short type = RFIFOW(fd,2), page = RFIFOW(fd,32);
	int price = RFIFOL(fd,4);  // FIXME: bug #5071

	if (!battle_config.feature_auction)
		return;

	clif_parse_Auction_cancelreg(fd, sd);
	
	safestrncpy(search_text, (char*)RFIFOP(fd,8), sizeof(search_text));
	intif_Auction_requestlist(sd->status.char_id, type, price, search_text, page);
}


/// Requests list of own currently active bids or auctions (CZ_AUCTION_REQ_MY_INFO).
/// 025c <type>.W
/// type:
///     0 = sell (own auctions)
///     1 = buy (own bids)
void clif_parse_Auction_buysell(int fd, struct map_session_data* sd)
{
	short type = RFIFOW(fd,2) + 6;

	if (!battle_config.feature_auction)
		return;

	clif_parse_Auction_cancelreg(fd, sd);

	intif_Auction_requestlist(sd->status.char_id, type, 0, "", 1);
}

#endif


/// CASH/POINT SHOP
///

/// List of items offered in a cash shop (ZC_PC_CASH_POINT_ITEMLIST).
/// 0287 <packet len>.W <cash point>.L { <sell price>.L <discount price>.L <item type>.B <name id>.W }*
/// 0287 <packet len>.W <cash point>.L <kafra point>.L { <sell price>.L <discount price>.L <item type>.B <name id>.W }* (PACKETVER >= 20070711)
void clif_cashshop_show(struct map_session_data *sd, struct npc_data *nd) {
	nullpo_retv(sd);
	nullpo_retv(nd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	sd->npc_shopid = nd->bl.id;

	uint16 len = sizeof(struct PACKET_ZC_PC_CASH_POINT_ITEMLIST) + nd->u.shop.count * sizeof(struct PACKET_ZC_PC_CASH_POINT_ITEMLIST_sub);
	WFIFOHEAD(fd, len);
	struct PACKET_ZC_PC_CASH_POINT_ITEMLIST* p = (struct PACKET_ZC_PC_CASH_POINT_ITEMLIST *)WFIFOP(fd, 0);

	p->packetType = 0x287;
	p->packetLength = len;
	p->cashPoints = sd->cashPoints;
#if PACKETVER >= 20070711
	p->kafraPoints = sd->kafraPoints;
#endif

	for (int i = 0; i < nd->u.shop.count; i++) {
		struct item_data* id = itemdb_search(nd->u.shop.shop_item[i].nameid);

		p->items[i].price = nd->u.shop.shop_item[i].value;
		p->items[i].discountPrice = nd->u.shop.shop_item[i].value; // Discount Price
		p->items[i].itemType = itemtype(id->nameid);
		p->items[i].itemId = client_nameid(id->nameid);
	}

	WFIFOSET(fd, len);
}


/// Cashshop Buy Ack (ZC_PC_CASH_POINT_UPDATE).
/// 0289 <cash point>.L <error>.W
/// 0289 <cash point>.L <kafra point>.L <error>.W (PACKETVER >= 20070711)
/// error:
///     0 = The deal has successfully completed. (ERROR_TYPE_NONE)
///     1 = The Purchase has failed because the NPC does not exist. (ERROR_TYPE_NPC)
///     2 = The Purchase has failed because the Kafra Shop System is not working correctly. (ERROR_TYPE_SYSTEM)
///     3 = You are over your Weight Limit. (ERROR_TYPE_INVENTORY_WEIGHT)
///     4 = You cannot purchase items while you are in a trade. (ERROR_TYPE_EXCHANGE)
///     5 = The Purchase has failed because the Item Information was incorrect. (ERROR_TYPE_ITEM_ID)
///     6 = You do not have enough Kafra Credit Points. (ERROR_TYPE_MONEY)
///     7 = You can purchase up to 10 items. (ERROR_TYPE_OVER_PRODUCT_TOTAL_CNT)
///     8 = Some items could not be purchased. (ERROR_TYPE_SOME_BUY_FAILURE)
///     9 = Unknwon. (ERROR_TYPE_INVENTORY_ITEMCNT)
void clif_cashshop_ack(struct map_session_data* sd, int error)
{
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x289));
	WFIFOW(fd,0) = 0x289;
	WFIFOL(fd,2) = sd->cashPoints;
#if PACKETVER < 20070711
	WFIFOW(fd,6) = TOW(error);
#else
	WFIFOL(fd,6) = sd->kafraPoints;
	WFIFOW(fd,10) = TOW(error);
#endif
	WFIFOSET(fd, packet_len(0x289));
}


/// Request to buy item(s) from cash shop (CZ_PC_BUY_CASH_POINT_ITEM).
/// 0288 <name id>.W <amount>.W
/// 0288 <name id>.W <amount>.W <kafra points>.L (PACKETVER >= 20070711)
/// 0288 <packet len>.W <kafra points>.L <count>.W { <amount>.W <name id>.W }.4B*count (PACKETVER >= 20100803)
void clif_parse_cashshop_buy(int fd, struct map_session_data *sd)
{
	int fail = 0;
	nullpo_retv(sd);

	if (sd->state.trading || !sd->npc_shopid)
		fail = 1;
	else
	{
		short nameid = RFIFOW(fd, 2);
		short amount = RFIFOW(fd, 4);
		int points = RFIFOL(fd, 6);

		fail = npc_cashshop_buy(sd, nameid, amount, points);
	}

	clif_cashshop_ack(sd, fail);
	}


/* [Ind/Hercules]
 * Rewrite by: 15peaces
 */

void clif_parse_CashShopOpen(int fd, struct map_session_data *sd) {
	sd->npc_shopid = -1; // Set npc_shopid when using cash shop from "cash shop" button

	WFIFOHEAD(fd, 10);
	WFIFOW(fd, 0) = 0x845;
	WFIFOL(fd, 2) = sd->cashPoints;
	WFIFOL(fd, 6) = sd->kafraPoints;
	WFIFOSET(fd, 10);
}

void clif_parse_CashShopClose(int fd, struct map_session_data *sd) {
	sd->npc_shopid = 0; // Reset npc_shopid when using cash shop from "cash shop" button
	// No need to do anything here
}

//08c0 <len>.W <openIdentity>.L <itemcount>.W (ZC_ACK_SE_CASH_ITEM_LIST2)
void clif_parse_CashShopReqTab(int fd, struct map_session_data *sd) {
// packet exists only in 2011 and was dropped/replaced by below packet after...
#if PACKETVER >= 20110222 && PACKETVER < 20120000
	short tab = RFIFOW(fd, 2);
	int j;

	if (tab < 0 || tab > CASHSHOP_TAB_MAX)
		return;

	WFIFOHEAD(fd, 10 + (cash_shop_items[tab].count * 6));
	WFIFOW(fd, 0) = 0x8c0;
	WFIFOW(fd, 2) = 10 + (cash_shop_items[tab].count * 6);
	WFIFOL(fd, 4) = tab;
	WFIFOW(fd, 8) = cash_shop_items[tab].count;

	for (j = 0; j < cash_shop_items[tab].count; j++) {
		WFIFOW(fd, 10 + (6 * j)) = cash_shop_items[tab].item[j]->id;
		WFIFOL(fd, 12 + (6 * j)) = cash_shop_items[tab].item[j]->price;
	}

	WFIFOSET(fd, 10 + (cash_shop_items[tab].count * 6));
#endif
}

//08ca <len>.W <itemcount> W <tabcode>.W (ZC_ACK_SCHEDULER_CASHITEM)
void clif_cashshop_list(int fd, struct map_session_data* sd) {
	nullpo_retv(sd);

	if (!session_isActive(fd)) {
		return;
	}

	for (int tab = CASHSHOP_TAB_NEW; tab < CASHSHOP_TAB_MAX; tab++) {
		// Skip empty tabs, the client only expects filled ones
		if (cash_shop_items[tab].count == 0) {
			continue;
		}

		int len = sizeof(struct PACKET_ZC_ACK_SCHEDULER_CASHITEM) + (cash_shop_items[tab].count * sizeof(struct PACKET_ZC_ACK_SCHEDULER_CASHITEM_sub));
		WFIFOHEAD(fd, len);
		struct PACKET_ZC_ACK_SCHEDULER_CASHITEM *p = (struct PACKET_ZC_ACK_SCHEDULER_CASHITEM *)WFIFOP(fd, 0);

		p->packetType = 0x8ca;
		p->packetLength = len;
		p->count = cash_shop_items[tab].count;
		p->tabNum = tab;

		for (int i = 0; i < cash_shop_items[tab].count; i++) {
			p->items[i].itemId = client_nameid(cash_shop_items[tab].item[i]->id);
			p->items[i].price = cash_shop_items[tab].item[i]->price;
		}

		WFIFOSET(fd, len);
	}
}

void clif_parse_CashShopBuy(int fd, struct map_session_data *sd) {
	unsigned short i, j;

	struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST* p = (struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST*)RFIFOP(fd, 0);

	for(i = 0; i < p->count ; i++) {
		int qty = p->items[i].amount;
		int id = p->items[i].itemId;
		short tab = p->items[i].tab;
		enum CASH_SHOP_BUY_RESULT result = CSBR_UNKNOWN;

		if( tab < 0 || tab > CASHSHOP_TAB_MAX )
			continue;

		for( j = 0; j < cash_shop_items[tab].count; j++ ) {
			if( cash_shop_items[tab].item[j]->id == id )
				break;
		}
		if( j < cash_shop_items[tab].count ) {
			struct item_data *data;
			if( sd->cashPoints < (cash_shop_items[tab].item[j]->price * qty) ) {
				result = CSBR_SHORTTAGE_CASH;
			} else if ( !( data = itemdb_exists(cash_shop_items[tab].item[j]->id) ) ) {
				result = CSBR_UNKONWN_ITEM;
			} else {
				int k, get_count;

				get_count = qty;

				if (!itemdb_isstackable2(data))
					get_count = 1;

				pc_paycash(sd, cash_shop_items[tab].item[j]->price * qty, 0, LOG_TYPE_CASH);/* kafra point support is missing */
				for (k = 0; k < qty; k += get_count) {
					unsigned short nameid = data->nameid;

					if (!pet_create_egg(sd, nameid)) {
						unsigned short get_amt = get_count, j;

						if (data->flag.guid)
							get_amt = 1;

						for (j = 0; j < get_count; j += get_amt) {
							struct item item_tmp = { 0 };

							item_tmp.nameid = nameid;
							item_tmp.identify = 1;

							switch (pc_additem(sd, &item_tmp, get_amt, LOG_TYPE_CASH)) {
								case 0:
									result = CSBR_SUCCESS;
									break;
								case 1:
									result = CSBR_EACHITEM_OVERCOUNT;
									break;
								case 2:
									result = CSBR_INVENTORY_WEIGHT;
									break;
								case 4:
									result = CSBR_INVENTORY_ITEMCNT;
									break;
								case 5:
									result = CSBR_EACHITEM_OVERCOUNT;
									break;
								case 7:
									result = CSBR_RUNE_OVERCOUNT;
									break;
							}

							if (result != CSBR_SUCCESS)
								pc_getcash(sd, cash_shop_items[tab].item[j]->price * get_amt, 0, LOG_TYPE_CASH);/* kafra point support is missing */
						}
					}
				}
			}
		} else {
			result = CSBR_UNKONWN_ITEM;
		}

		clif_cashshop_result(sd, id, result);
	}
}

void clif_cashshop_result(struct map_session_data *sd, t_itemid item_id, uint16 result) {
#if PACKETVER >= 20101123
	nullpo_retv(sd);

	struct PACKET_ZC_SE_PC_BUY_CASHITEM_RESULT packet;

	packet.packetType = 0x849;
	packet.itemId = client_nameid(item_id);
	packet.result = result;
	packet.cashPoints = sd->cashPoints;
	packet.kafraPoints = sd->kafraPoints;

	clif_send(&packet, sizeof(struct PACKET_ZC_SE_PC_BUY_CASHITEM_RESULT), &sd->bl, SELF);
#endif
}

/// Closes shop (CZ_NPC_TRADE_QUIT).
/// 09d4
void clif_parse_NPCShopClosed(int fd, struct map_session_data *sd) {
	nullpo_retv(sd);
	sd->npc_shopid = 0;
	sd->state.trading = 0;
}

/**
 * Presents list of items, that can be sold to a Market shop.
 * @author: Ind and Yommy
 **/
void clif_npc_market_open(struct map_session_data *sd, struct npc_data *nd) {
#if PACKETVER >= 20131223
	nullpo_retv(sd);
	nullpo_retv(nd);

	if (sd->state.trading)
		return;

	int fd = sd->fd;

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_NPC_MARKET_OPEN) + nd->u.shop.count * sizeof(struct PACKET_ZC_NPC_MARKET_OPEN_sub));
	struct PACKET_ZC_NPC_MARKET_OPEN *p = (struct PACKET_ZC_NPC_MARKET_OPEN *)WFIFOP(fd, 0);
	p->packetType = 0x09d5;

	int count = 0;
	for (int i = 0; i < nd->u.shop.count; i++) {
		struct npc_item_list *item = &nd->u.shop.shop_item[i];

		if (!item->nameid) {
			continue;
		}

		struct item_data *id = itemdb_exists(item->nameid);

		if (!id) {
			continue;
		}

		p->list[count].nameid = client_nameid(item->nameid);
		p->list[count].type = itemtype(item->nameid);
		p->list[count].price = item->value;
		p->list[count].qty = item->qty;
		p->list[count].weight = id->weight;
		count++;
	}

	p->packetLength = sizeof(struct PACKET_ZC_NPC_MARKET_OPEN) + count * sizeof(struct PACKET_ZC_NPC_MARKET_OPEN_sub);
	WFIFOSET(fd, p->packetLength);

	sd->state.trading = 1;
#endif
}

void clif_parse_NPCMarketClosed(int fd, struct map_session_data *sd) {
	nullpo_retv(sd);
	sd->npc_shopid = 0;
	sd->state.trading = 0;
}

/// Purchase item from Market shop.
void clif_npc_market_purchase_ack(struct map_session_data *sd, uint8 res, uint8 n, struct s_npc_buy_list *list) {
#if PACKETVER >= 20131223
	nullpo_retv(sd);
	nullpo_retv(list);

	struct npc_data *nd = map_id2nd(sd->npc_shopid);

	nullpo_retv(nd);

	int fd = sd->fd;

	WFIFOHEAD(fd, sizeof(struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT) + n * sizeof(struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT_sub));
	struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT *p = (struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT *)WFIFOP(fd, 0);
	p->PacketType = 0x09d7;
	p->result = (res == 0 ? 1 : 0);

	int count = 0;

	if (p->result) {
		for (int i = 0, j; i < n; i++) {
			ARR_FIND(0, nd->u.shop.count, j, list[i].nameid == nd->u.shop.shop_item[j].nameid);

			// Not found
			if (j == nd->u.shop.count) {
				continue;
			}

			p->list[count].ITID = client_nameid(list[i].nameid);
			p->list[count].qty = list[i].qty;
			p->list[count].price = nd->u.shop.shop_item[j].value;
			count++;
		}
	}

	p->PacketLength = sizeof(struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT) + count * sizeof(struct PACKET_ZC_NPC_MARKET_PURCHASE_RESULT_sub);
	WFIFOSET(fd, p->PacketLength);
#endif
}

/// Purchase item from Market shop.
void clif_parse_NPCMarketPurchase(int fd, struct map_session_data *sd) {
#if PACKETVER >= 20131223
	nullpo_retv(sd);

	if (!sd->npc_shopid)
		return;

	const struct packet_npc_market_purchase *p = (struct packet_npc_market_purchase *)RFIFOP(fd, 0);

	int count = (p->PacketLength - sizeof(struct packet_npc_market_purchase)) / sizeof(struct packet_npc_market_purchase_sub);

	struct s_npc_buy_list *list;

	CREATE(list, struct s_npc_buy_list, count);

	// Sadly order is reverse
	for (int i = 0; i < count; i++) {
		list[i].nameid = p->list[i].ITID;
		list[i].qty = p->list[i].qty;
	}

	uint8 res = npc_buylist(sd, count, list);
	clif_npc_market_purchase_ack(sd, res, count, list);

	aFree(list);
#endif
}

/// Adoption System
///

/// Adoption message (ZC_BABYMSG).
/// 0216 <msg>.L
/// msg:
///     ADOPT_REPLY_MORE_CHILDREN = "You cannot adopt more than 1 child."
///     ADOPT_REPLY_LEVEL_70 = "You must be at least character level 70 in order to adopt someone."
///     ADOPT_REPLY_MARRIED = "You cannot adopt a married person."
void clif_Adopt_reply(struct map_session_data *sd, int type)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,6);
	WFIFOW(fd,0) = 0x216;
	WFIFOL(fd,2) = type;
	WFIFOSET(fd,6);
}


/// Adoption confirmation (ZC_REQ_BABY).
/// 01f6 <account id>.L <char id>.L <name>.B
void clif_Adopt_request(struct map_session_data *sd, struct map_session_data *src, int p_id)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,34);
	WFIFOW(fd,0) = 0x1f6;
	WFIFOL(fd,2) = src->status.account_id;
	WFIFOL(fd,6) = p_id;
	safestrncpy(WFIFOP(fd,10), src->status.name, NAME_LENGTH);
	WFIFOSET(fd,34);
}


/// Request to adopt a player (CZ_REQ_JOIN_BABY).
/// 01f9 <account id>.L
void clif_parse_Adopt_request(int fd, struct map_session_data *sd)
{
	struct map_session_data *tsd = map_id2sd(RFIFOL(fd,2)), *p_sd = map_charid2sd(sd->status.partner_id);

	if(pc_try_adopt(sd, p_sd, tsd) == ADOPT_ALLOWED)
	{
		tsd->adopt_invite = sd->status.account_id;
		clif_Adopt_request(tsd, sd, p_sd->status.account_id);
	}
}


/// Answer to adopt confirmation (CZ_JOIN_BABY).
/// 01f7 <account id>.L <char id>.L <answer>.L
/// answer:
///     0 = rejected
///     1 = accepted
void clif_parse_Adopt_reply(int fd, struct map_session_data *sd)
{
	int p1_id = RFIFOL(fd,2);
	int p2_id = RFIFOL(fd,6);
	int result = RFIFOL(fd,10);
	struct map_session_data* p1_sd = map_id2sd(p1_id);
	struct map_session_data* p2_sd = map_id2sd(p2_id);

	int pid = sd->adopt_invite;
	sd->adopt_invite = 0;

	if( p1_sd == NULL || p2_sd == NULL )
		return; // Both players need to be online

	if( pid != p1_sd->status.account_id )
		return; // Incorrect values

	if( result == 0 )
		return; // Rejected

	pc_adoption(p1_sd, p2_sd, sd);
}


/// Convex Mirror (ZC_BOSS_INFO).
/// 0293 <infoType>.B <x>.L <y>.L <minHours>.W <minMinutes>.W <maxHours>.W <maxMinutes>.W <monster name>.51B
/// infoType:
///     0 = No boss on this map (BOSS_INFO_NOT).
///     1 = Boss is alive (position update) (BOSS_INFO_ALIVE).
///     2 = Boss is alive (initial announce) (BOSS_INFO_ALIVE_WITHMSG).
///     3 = Boss is dead (BOSS_INFO_DEAD).
void clif_bossmapinfo(int fd, struct mob_data *md, short flag)
{
	WFIFOHEAD(fd,70);
	memset(WFIFOP(fd,0),0,70);
	WFIFOW(fd,0) = 0x293;

	if( md != NULL )
	{
		if( md->bl.prev != NULL )
		{ // Boss on This Map
			if( flag )
			{
				WFIFOB(fd,2) = 1;
				WFIFOL(fd,3) = md->bl.x;
				WFIFOL(fd,7) = md->bl.y;
			}
			else
				WFIFOB(fd,2) = 2; // First Time
		}
		else if (md->spawn_timer != INVALID_TIMER)
		{ // Boss is Dead
			const struct TimerData * timer_data = get_timer(md->spawn_timer);
			unsigned int seconds;
			int hours, minutes;

			seconds = (unsigned int)(DIFF_TICK(timer_data->tick, gettick()) / 1000 + 60);
			hours = seconds / (60 * 60);
			seconds = seconds - (60 * 60 * hours);
			minutes = seconds / 60;

			WFIFOB(fd,2) = 3;
			WFIFOW(fd,11) = hours; // Hours
			WFIFOW(fd,13) = minutes; // Minutes
		}
		safestrncpy((char*)WFIFOP(fd,19), md->db->jname, NAME_LENGTH);
	}

	WFIFOSET(fd,70);
}


/// Requesting equip of a player (CZ_EQUIPWIN_MICROSCOPE).
/// 02d6 <account id>.L
void clif_parse_ViewPlayerEquip(int fd, struct map_session_data* sd) {
	int charid = RFIFOL(fd, 2);
	struct map_session_data* tsd = map_id2sd(charid);
	
	if (!tsd)
		return;

	if( tsd->status.show_equip || (battle_config.gm_viewequip_min_lv && pc_isGM(sd) >= battle_config.gm_viewequip_min_lv) )
		clif_viewequip_ack(sd, tsd);
	else
		clif_msg(sd, VIEW_EQUIP_FAIL);
}


/// Receive configurations (CZ_CONFIG).
/// 02d8 <type>.L <value>.L
/// type:
///     0 = open equip window
///     2 = pet autofeeding
///     value:
///         0 = disabled
///         1 = enabled
void clif_parse_cz_config(int fd, struct map_session_data *sd)
{
	if (pc_isdead(sd) || pc_istrading(sd))
		return;
	
	enum CZ_CONFIG type = RFIFOL(fd, 2);
	int flag = RFIFOL(fd, 6);

	switch (type) {
		case CZ_CONFIG_OPEN_EQUIPMENT_WINDOW:
			sd->status.show_equip = flag;
			break;
		case CZ_CONFIG_PET_AUTOFEEDING: 
		{
			struct pet_data *pd = sd->pd;
			nullpo_retv(pd);
			if (pd->petDB->autofeed == 0) {
				clif_displaymessage(fd, msg_txt(sd,205)); // "Autofeed is disabled for this pet."
				return;
			}
			pd->pet.autofeed = flag;
			break;
		}
		case CZ_CONFIG_HOMUNCULUS_AUTOFEEDING: // TODO
		{
			struct homun_data *hd = sd->hd;
			nullpo_retv(hd);
			hd->homunculus.autofeed = flag;
			break;
		}
		default:
			ShowWarning("clif_parse_cz_config: Unsupported type has been received (%u).\n", type);
			return;
	}
	clif_zc_config(sd, type, flag);
}


/// Questlog System [Kevin] [Inkfish]
///

/**
 * Safe check to init packet len & quest num for player.
 * @param def_len
 * @param info_len Length of each quest info.
 * @param avail_quests Avail quests that player has.
 * @param limit_out Limit for quest num, to make sure the quest num is still in allowed value.
 * @param len_out Packet length as initial set => def_len + limit_out * info_len.
 **/
static void clif_quest_len(int def_len, int info_len, int avail_quests, int *limit_out, int *len_out) {
	(*limit_out) = (0xFFFF - def_len) / info_len;
	(*limit_out) = (avail_quests > (*limit_out)) ? (*limit_out) : avail_quests;
	(*len_out) = ((*limit_out) * info_len) + def_len;
}

/// Sends list of all quest states (ZC_ALL_QUEST_LIST).
/// 02b1 <packet len>.W <num>.L { <quest id>.L <active>.B }*num
/// 097a <packet len>.W <num>.L { <quest id>.L <active>.B <remaining time>.L <time>.L <count>.W { <mob_id>.L <killed>.W <total>.W <mob name>.24B }*count }*num (ZC_ALL_QUEST_LIST2)
/// 09f8 <packet len>.W <num>.L { <quest id>.L <active>.B <remaining time>.L <time>.L <count>.W { <hunt identification>.L <mob type>.L <mob_id>.L <min level>.W <max level>.W <killed>.W <total>.W <mob name>.24B }*count }*num  (ZC_ALL_QUEST_LIST3)
void clif_quest_send_list(struct map_session_data *sd)
{
	int fd = sd->fd;
	int i;
	int offset = 8;
	int limit = 0;

#if PACKETVER >= 20141022
#if PACKETVER >= 20150513
	int size = 22 + NAME_LENGTH;
#else
	int size = 10 + NAME_LENGTH;
#endif
	clif_quest_len(offset, 15 + (size * MAX_QUEST_OBJECTIVES), sd->avail_quests, &limit, &i);
	WFIFOHEAD(fd,i);
#if PACKETVER >= 20150513
	WFIFOW(fd, 0) = 0x9f8;
#else
	WFIFOW(fd, 0) = 0x97a;
#endif
	WFIFOL(fd, 4) = limit;

	for (i = 0; i < limit; i++) {
		struct quest_db *qi = quest_db(sd->quest_log[i].quest_id);

		WFIFOL(fd, offset) = sd->quest_log[i].quest_id;
		offset += 4;
		WFIFOB(fd, offset) = sd->quest_log[i].state;
		offset++;
		WFIFOL(fd, offset) = sd->quest_log[i].time - qi->time;
		offset += 4;
		WFIFOL(fd, offset) = sd->quest_log[i].time;
		offset += 4;
		WFIFOW(fd, offset) = qi->objectives_count;
		offset += 2;
		
		if(qi->objectives_count > 0 ){
			int j;
			struct mob_db *mob;

			for( j = 0; j < qi->objectives_count; j++ ){
				mob = mob_db(qi->objectives[j].mob);

				// Dummy data for now...
				int8 race = qi->objectives[j].race;
				int8 size = qi->objectives[j].size;
				int8 element = qi->objectives[j].element;

#if PACKETVER >= 20150513
				WFIFOL(fd, offset) = sd->quest_log[i].quest_id * 1000 + j;
				offset += 4;
				WFIFOL(fd, offset) = (race ? race : (size ? size : (element ? element : 0)));
				offset += 4;
#endif

				WFIFOL(fd, offset) = (qi->objectives[j].mob)>0? qi->objectives[j].mob : MOBID_PORING;
				offset += 4;
#if PACKETVER >= 20150513
				WFIFOW(fd, offset) = qi->objectives[j].min_level;
				offset += 2;
				WFIFOW(fd, offset) = qi->objectives[j].max_level;
				offset += 2;
#endif
				WFIFOW(fd, offset) = sd->quest_log[i].count[j];
				offset += 2;
				WFIFOW(fd, offset) = qi->objectives[j].count;
				offset += 2;
				if (mob && qi->objectives[j].mob > 0)
					safestrncpy((char *)WFIFOP(fd, offset), mob->jname, NAME_LENGTH);
				else
					safestrncpy((char *)WFIFOP(fd, offset), "", NAME_LENGTH);
				offset += NAME_LENGTH;
			}
		}
	}

	WFIFOW(fd, 2) = offset;
	WFIFOSET(fd, offset);
#else
	clif_quest_len(offset, 5, sd->avail_quests, &limit, &i);
	WFIFOHEAD(fd,i);
	WFIFOW(fd, 0) = 0x2b1;
	WFIFOL(fd, 4) = limit;

	for (i = 0; i < limit; i++) {
		WFIFOL(fd, offset) = sd->quest_log[i].quest_id;
		offset += 4;
		WFIFOB(fd, offset) = sd->quest_log[i].state;
		offset += 1;
	}
	
	WFIFOW(fd, 2) = offset;
	WFIFOSET(fd, offset);
#endif
}

/// Sends list of all quest missions (ZC_ALL_QUEST_MISSION).
/// 02b2 <packet len>.W <num>.L { <quest id>.L <start time>.L <expire time>.L <mobs>.W { <mob id>.L <mob count>.W <mob name>.24B }*3 }*num
void clif_quest_send_mission(struct map_session_data * sd)
{
	int fd = sd->fd;
	int i, j;
	int len = sd->avail_quests*104+8;
	struct mob_db *mob;

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0x2b2;
	WFIFOW(fd, 2) = len;
	WFIFOL(fd, 4) = sd->avail_quests;

	for( i = 0; i < sd->avail_quests; i++ )
	{
		struct quest_db *qi = quest_db(sd->quest_log[i].quest_id);

		WFIFOL(fd, i*104+8) = sd->quest_log[i].quest_id;
		WFIFOL(fd, i*104+12) = sd->quest_log[i].time - qi->time;
		WFIFOL(fd, i*104+16) = sd->quest_log[i].time;
		WFIFOW(fd, i*104+20) = qi->objectives_count;

		for( j = 0 ; j < qi->objectives_count; j++ )
		{
			WFIFOL(fd, i*104+22+j*30) = qi->objectives[j].mob;
			WFIFOW(fd, i*104+26+j*30) = sd->quest_log[i].count[j];
			mob = mob_db(qi->objectives[j].mob);
			safestrncpy(WFIFOP(fd, i*104+28+j*30), mob->jname, NAME_LENGTH);
		}
	}

	WFIFOSET(fd, len);
}


/// Notification about a new quest.
/// 02b3 <quest id>.L <active>.B <start time>.L <expire time>.L <mobs>.W { <mob id>.L <mob count>.W <mob name>.24B }*3 (ZC_ADD_QUEST)
/// 08fe <packet len>.W  { <quest id>.L <mob id>.L <total count>.W <current count>.W }*3 (ZC_HUNTING_QUEST_INFO)
/// 09f9 <quest id>.L <active>.B <start time>.L <expire time>.L <mobs>.W { <hunt identification>.L <mob type>.L <mob id>.L <min level>.W <max level>.W <mob count>.W <mob name>.24B }*3 (ZC_ADD_QUEST_EX)
void clif_quest_add(struct map_session_data * sd, struct quest * qd)
{
	nullpo_retv(sd);
	nullpo_retv(qd);

	int fd = sd->fd;
	struct mob_db *mob;
	struct quest_db *qi = quest_db(qd->quest_id);

#if PACKETVER >= 20150513
	int cmd = 0x9f9;
#else
	int cmd = 0x2b3;
#endif

	WFIFOHEAD(fd, packet_len(cmd));
	WFIFOW(fd, 0) = cmd;
	WFIFOL(fd, 2) = qd->quest_id;
	WFIFOB(fd, 6) = qd->state;
	WFIFOB(fd, 7) = qi->time;
	WFIFOL(fd, 11) = qd->time;
	WFIFOW(fd, 15) = qi->objectives_count;

	for(int i = 0, offset = 17; i < qi->objectives_count; i++ )
	{
		mob = mob_db(qi->objectives[i].mob);

		// Dummy data for now...
		int8 race = qi->objectives[i].race;
		int8 size = qi->objectives[i].size;
		int8 element = qi->objectives[i].element;

#if PACKETVER >= 20150513
		WFIFOL(fd, offset) = qd->quest_id * 1000 + i;
		offset += 4;
		WFIFOL(fd, offset) = (race ? race : (size ? size : (element ? element : 0)));	// effect ?
		offset += 4;
#endif
		WFIFOL(fd, offset) = (qi->objectives[i].mob) > 0 ? qi->objectives[i].mob : MOBID_PORING;	// 0 can't be used as it displays "Novice" job regardless of the clif_mobtype_name
		offset += 4;
#if PACKETVER >= 20150513
		WFIFOW(fd, offset) = qi->objectives[i].min_level;
		offset += 2;
		WFIFOW(fd, offset) = qi->objectives[i].max_level;
		offset += 2;
#endif
		WFIFOW(fd, offset) = qd->count[i];
		offset += 2;
		if (mob && qi->objectives[i].mob > 0)
			safestrncpy((char *)WFIFOP(fd, offset), mob->jname, NAME_LENGTH);
		else
			safestrncpy((char *)WFIFOP(fd, offset), "", NAME_LENGTH);
		offset += NAME_LENGTH;
	}

	WFIFOSET(fd, packet_len(cmd));

#if PACKETVER >= 20150513
	int len = 4 + qi->objectives_count * 12;

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0x8fe;
	WFIFOW(fd, 2) = len;

	for (int i = 0, offset = 4; i < qi->objectives_count; i++, offset += 12) {
		WFIFOL(fd, offset) = qd->quest_id * 1000 + i;
		WFIFOL(fd, offset + 4) = qi->objectives[i].mob;
		WFIFOW(fd, offset + 8) = qi->objectives[i].count;
		WFIFOW(fd, offset + 10) = qd->count[i];
	}

	WFIFOSET(fd, len);

#endif
}


/// Notification about a quest being removed (ZC_DEL_QUEST).
/// 02b4 <quest id>.L
void clif_quest_delete(struct map_session_data * sd, int quest_id)
{
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x2b4));
	WFIFOW(fd, 0) = 0x2b4;
	WFIFOL(fd, 2) = quest_id;
	WFIFOSET(fd, packet_len(0x2b4));
}


/// Notification of an update to the hunting mission counter.
/// 02b5 <packet len>.W <mobs>.W { <quest id>.L <mob id>.L <total count>.W <current count>.W }*3 (ZC_UPDATE_MISSION_HUNT)
/// 09fa <packet len>.W <mobs>.W { <quest id>.L <hunt identification>.L <total count>.W <current count>.W }*3 (ZC_UPDATE_MISSION_HUNT_EX)
void clif_quest_update_objective(struct map_session_data * sd, struct quest * qd)
{
	int fd = sd->fd;
	int offset = 6;
	int i;
	struct quest_db *qi = quest_db(qd->quest_id);
	int len = qi->objectives_count * 12 + 6;

#if PACKETVER >= 20150513
	int cmd = 0x9fa;
#else
	int cmd = 0x2b5;
#endif

	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = cmd;
	WFIFOW(fd, 4) = qi->objectives_count;

	for( i = 0; i < qi->objectives_count; i++ )
	{
		WFIFOL(fd, offset) = qd->quest_id;
		offset += 4;
#if PACKETVER >= 20150513
		WFIFOL(fd, offset) = qd->quest_id * 1000 + i;
		offset += 4;
#else
		WFIFOL(fd, offset) = qi->objectives[i].mob;
		offset += 4;
#endif
		WFIFOW(fd, offset) = qi->objectives[i].count;
		offset += 2;
		WFIFOW(fd, offset) = qd->count[i];
		offset += 2;
	}

	WFIFOW(fd, 2) = offset;
	WFIFOSET(fd, len);
}


/// Request to change the state of a quest (CZ_ACTIVE_QUEST).
/// 02b6 <quest id>.L <active>.B
void clif_parse_questStateAck(int fd, struct map_session_data * sd)
{
	quest_update_status(sd, RFIFOL(fd,2), RFIFOB(fd,6)?Q_ACTIVE:Q_INACTIVE);
}


/// Notification about the change of a quest state (ZC_ACTIVE_QUEST).
/// 02b7 <quest id>.L <active>.B
void clif_quest_update_status(struct map_session_data * sd, int quest_id, bool active)
{
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x2b7));
	WFIFOW(fd, 0) = 0x2b7;
	WFIFOL(fd, 2) = quest_id;
	WFIFOB(fd, 6) = active;
	WFIFOSET(fd, packet_len(0x2b7));
}


/// Notification about an NPC's quest state (ZC_QUEST_NOTIFY_EFFECT).
/// 0446 <npc id>.L <x>.W <y>.W <effect>.W <type>.W
/// effect:
///     0 = none
///     1 = exclamation mark icon
///     2 = question mark icon
/// type:
///     0 = yellow
///     1 = orange
///     2 = green
///     3 = purple
void clif_quest_show_event(struct map_session_data *sd, struct block_list *bl, short state, short color)
{
#if PACKETVER >= 20090218
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x446));
	WFIFOW(fd, 0) = 0x446;
	WFIFOL(fd, 2) = bl->id;
	WFIFOW(fd, 6) = bl->x;
	WFIFOW(fd, 8) = bl->y;
	WFIFOW(fd, 10) = state;
	WFIFOW(fd, 12) = color;
	WFIFOSET(fd, packet_len(0x446));
#endif
}


/// Mercenary System
///

/// Notification about a mercenary status parameter change (ZC_MER_PAR_CHANGE).
/// 02a2 <var id>.W <value>.L
void clif_mercenary_updatestatus(struct map_session_data *sd, int type)
{
	struct mercenary_data *md;
	struct status_data *status;
	int fd;
	if( sd == NULL || (md = sd->md) == NULL )
		return;

	fd = sd->fd;
	status = &md->battle_status;
	WFIFOHEAD(fd,packet_len(0x2a2));
	WFIFOW(fd,0) = 0x2a2;
	WFIFOW(fd,2) = type;
	switch( type )
	{
		case SP_ATK1:
			{
				int atk = rnd()%(status->rhw.atk2 - status->rhw.atk + 1) + status->rhw.atk;
				WFIFOL(fd,4) = cap_value(atk, 0, INT16_MAX);
			}
			break;
		case SP_MATK1:
			WFIFOL(fd,4) = cap_value(status->matk_max, 0, INT16_MAX);
			break;
		case SP_HIT:
			WFIFOL(fd,4) = status->hit;
			break;
		case SP_CRITICAL:
			WFIFOL(fd,4) = status->cri/10;
			break;
		case SP_DEF1:
			WFIFOL(fd,4) = status->def;
			break;
		case SP_MDEF1:
			WFIFOL(fd,4) = status->mdef;
			break;
		case SP_MERCFLEE:
			WFIFOL(fd,4) = status->flee;
			break;
		case SP_ASPD:
			WFIFOL(fd,4) = status->amotion;
			break;
		case SP_HP:
			WFIFOL(fd,4) = status->hp;
			break;
		case SP_MAXHP:
			WFIFOL(fd,4) = status->max_hp;
			break;
		case SP_SP:
			WFIFOL(fd,4) = status->sp;
			break;
		case SP_MAXSP:
			WFIFOL(fd,4) = status->max_sp;
			break;
		case SP_MERCKILLS:
			WFIFOL(fd,4) = md->mercenary.kill_count;
			break;
		case SP_MERCFAITH:
			WFIFOL(fd,4) = mercenary_get_faith(md);
			break;
	}
	WFIFOSET(fd,packet_len(0x2a2));
}


/// Mercenary base status data (ZC_MER_INIT).
/// 029b <id>.L <atk>.W <matk>.W <hit>.W <crit>.W <def>.W <mdef>.W <flee>.W <aspd>.W
///     <name>.24B <level>.W <hp>.L <maxhp>.L <sp>.L <maxsp>.L <expire time>.L <faith>.W
///     <calls>.L <kills>.L <atk range>.W
void clif_mercenary_info(struct map_session_data *sd)
{
	int fd;
	struct mercenary_data *md;
	struct status_data *status;
	int atk;

	if( sd == NULL || (md = sd->md) == NULL )
		return;

	fd = sd->fd;
	status = &md->battle_status;

	WFIFOHEAD(fd,packet_len(0x29b));
	WFIFOW(fd,0) = 0x29b;
	WFIFOL(fd,2) = md->bl.id;

	// Mercenary shows ATK as a random value between ATK ~ ATK2
	atk = rnd()%(status->rhw.atk2 - status->rhw.atk + 1) + status->rhw.atk;
	WFIFOW(fd,6) = cap_value(atk, 0, INT16_MAX);
	WFIFOW(fd,8) = cap_value(status->matk_max, 0, INT16_MAX);
	WFIFOW(fd,10) = status->hit;
	WFIFOW(fd,12) = status->cri/10;
	WFIFOW(fd,14) = status->def;
	WFIFOW(fd,16) = status->mdef;
	WFIFOW(fd,18) = status->flee;
	WFIFOW(fd,20) = status->amotion;
	safestrncpy((char*)WFIFOP(fd,22), md->db->name, NAME_LENGTH);
	WFIFOW(fd,46) = md->db->lv;
	WFIFOL(fd,48) = status->hp;
	WFIFOL(fd,52) = status->max_hp;
	WFIFOL(fd,56) = status->sp;
	WFIFOL(fd,60) = status->max_sp;
	WFIFOL(fd,64) = (int)time(NULL) + (mercenary_get_lifetime(md) / 1000);
	WFIFOW(fd,68) = mercenary_get_faith(md);
	WFIFOL(fd,70) = mercenary_get_calls(md);
	WFIFOL(fd,74) = md->mercenary.kill_count;
	WFIFOW(fd,78) = md->battle_status.rhw.range;
	WFIFOSET(fd,packet_len(0x29b));
}


/// Mercenary skill tree (ZC_MER_SKILLINFO_LIST).
/// 029d <packet len>.W { <skill id>.W <type>.L <level>.W <sp cost>.W <attack range>.W <skill name>.24B <upgradable>.B }*
void clif_mercenary_skillblock(struct map_session_data *sd)
{
	struct mercenary_data *md;
	int fd, i, len = 4, id, j;

	if( sd == NULL || (md = sd->md) == NULL )
		return;
	
	fd = sd->fd;
	WFIFOHEAD(fd,4+37*MAX_MERCSKILL);
	WFIFOW(fd,0) = 0x29d;
	for( i = 0; i < MAX_MERCSKILL; i++ )
	{
		if( (id = md->db->skill[i].id) == 0 )
			continue;
		j = id - MC_SKILLBASE;
		WFIFOW(fd,len) = id;
		WFIFOL(fd,len+2) = skill_get_inf(id);
		WFIFOW(fd,len+6) = md->db->skill[j].lv;
		WFIFOW(fd,len+8) = skill_get_sp(id, md->db->skill[j].lv);
		WFIFOW(fd,len+10) = skill_get_range2(&md->bl, id, md->db->skill[j].lv, false);
		safestrncpy((char*)WFIFOP(fd,len+12), skill_get_name(id), NAME_LENGTH);
		WFIFOB(fd,len+36) = 0; // Skillable for Mercenary?
		len += 37;
	}

	WFIFOW(fd,2) = len;
	WFIFOSET(fd,len);
}


/// Request to invoke a mercenary menu action (CZ_MER_COMMAND).
/// 029f <command>.B
///     1 = mercenary information
///     2 = delete
void clif_parse_mercenary_action(int fd, struct map_session_data* sd)
{
	int option = RFIFOB(fd,2);
	if( sd->md == NULL )
		return;

	if( option == 2 ) mercenary_delete(sd->md, 2);
}


/// Mercenary Message
/// message:
///     0 = Mercenary soldier's duty hour is over.
///     1 = Your mercenary soldier has been killed.
///     2 = Your mercenary soldier has been fired.
///     3 = Your mercenary soldier has ran away.
void clif_mercenary_message(struct map_session_data* sd, int message) {
	clif_msg(sd, MSG_MER_FINISH + message);
}

/*==========================================
 * Elemental System
 *==========================================*/

/// Notification about a elemental status parameter change (ZC_EL_PAR_CHANGE).
/// 081e <var id>.W <value>.L
void clif_elemental_updatestatus(struct map_session_data *sd, int type) {
	struct elemental_data *ed;
	struct status_data *status;
	int fd;
	if( sd == NULL || (ed = sd->ed) == NULL )
		return;

	fd = sd->fd;
	status = &ed->battle_status;
	WFIFOHEAD(fd, packet_len(0x81e));
	WFIFOW(fd,0) = 0x81e;
	WFIFOW(fd,2) = type;
	switch( type ) {
		case SP_HP:
			WFIFOL(fd,4) = status->hp;
			break;
		case SP_MAXHP:
			WFIFOL(fd,4) = status->max_hp;
			break;
		case SP_SP:
			WFIFOL(fd,4) = status->sp;
			break;
		case SP_MAXSP:
			WFIFOL(fd,4) = status->max_sp;
			break;
	}
	WFIFOSET(fd, packet_len(0x81e));
}

/// Elemental base status data (ZC_EL_INIT).
/// 081d <id>.L <hp>.L <maxhp>.L <sp>.L <maxsp>.L
void clif_elemental_info(struct map_session_data *sd) {
	int fd;
	struct elemental_data *ed;
	struct status_data *status;

	if( sd == NULL || (ed = sd->ed) == NULL )
		return;

	fd = sd->fd;
	status = &ed->battle_status;

	WFIFOHEAD(fd, packet_len(0x81d));
	WFIFOW(fd, 0) = 0x81d;
	WFIFOL(fd, 2) = ed->bl.id;
	WFIFOL(fd, 6) = status->hp;
	WFIFOL(fd,10) = status->max_hp;
	WFIFOL(fd,14) = status->sp;
	WFIFOL(fd,18) = status->max_sp;
	WFIFOSET(fd, packet_len(0x81d));
}

/// Notification about the remaining time of a rental item (ZC_CASH_TIME_COUNTER).
/// 0298 <name id>.W <seconds>.L
void clif_rental_time(struct map_session_data* sd, t_itemid nameid, int seconds)
{ // '<ItemName>' item will disappear in <seconds/60> minutes.
	struct PACKET_ZC_CASH_TIME_COUNTER p;

	p.packetType = 0x298;
	p.itemId = client_nameid(nameid);
	p.seconds = seconds;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}


/// Deletes a rental item from client's inventory (ZC_CASH_ITEM_DELETE).
/// 0299 <index>.W <name id>.W
void clif_rental_expired(struct map_session_data* sd, int index, t_itemid nameid)
{ // '<ItemName>' item has been deleted from the Inventory
	struct PACKET_ZC_CASH_ITEM_DELETE p;

	p.packetType = 0x299;
	p.index = client_index(index);
	p.itemId = client_nameid(nameid);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}


/// Book Reading (ZC_READ_BOOK).
/// 0294 <book id>.L <page>.L
void clif_readbook(int fd, int book_id, int page)
{
	WFIFOHEAD(fd,packet_len(0x294));
	WFIFOW(fd,0) = 0x294;
	WFIFOL(fd,2) = book_id;
	WFIFOL(fd,6) = page;
	WFIFOSET(fd,packet_len(0x294));
}


/// Battlegrounds
///

/// Updates HP bar of a camp member.
/// 02e0 <account id>.L <name>.24B <hp>.W <max hp>.W (ZC_BATTLEFIELD_NOTIFY_HP)
/// 0a0e <account id>.L <hp>.L <max hp>.L (ZC_BATTLEFIELD_NOTIFY_HP2)
void clif_bg_hp(struct map_session_data *sd) {
#if PACKETVER < 20140613
	unsigned char buf[34];
	const int cmd = 0x2e0;
#else
	unsigned char buf[14];
	const int cmd = 0xa0e;
#endif
	nullpo_retv(sd);

	WBUFW(buf,0) = cmd;
	WBUFL(buf,2) = sd->status.account_id;
#if PACKETVER < 20140613
	safestrncpy(WBUFP(buf,6), sd->status.name, NAME_LENGTH);
    if( sd->battle_status.max_hp > INT16_MAX ) { // To correctly display the %hp bar. [Skotlex]
		WBUFW(buf,30) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
        WBUFW(buf,32) = 100;
    } else {
		WBUFW(buf,30) = sd->battle_status.hp;
		WBUFW(buf,32) = sd->battle_status.max_hp;
    }
#else
    if( sd->battle_status.max_hp > INT32_MAX ) {
		WBUFL(buf,6) = sd->battle_status.hp/(sd->battle_status.max_hp/100);
		WBUFL(buf,10) = 100;
    } else {
		WBUFL(buf,6) = sd->battle_status.hp;
		WBUFL(buf,10) = sd->battle_status.max_hp;
    }
#endif
	clif_send(buf, packet_len(cmd), &sd->bl, BG_AREA_WOS);
}


/// Updates the position of a camp member on the minimap (ZC_BATTLEFIELD_NOTIFY_POSITION).
/// 02df <account id>.L <name>.24B <class>.W <x>.W <y>.W
void clif_bg_xy(struct map_session_data *sd)
{
	unsigned char buf[36];
	nullpo_retv(sd);

	WBUFW(buf,0)=0x2df;
	WBUFL(buf,2)=sd->status.account_id;
	safestrncpy(WBUFP(buf,6), sd->status.name, NAME_LENGTH);
	WBUFW(buf,30)=sd->status.class_;
	WBUFW(buf,32)=sd->bl.x;
	WBUFW(buf,34)=sd->bl.y;

	clif_send(buf, packet_len(0x2df), &sd->bl, BG_SAMEMAP_WOS);
}

void clif_bg_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[36];
	nullpo_retv(sd);

	WBUFW(buf,0)=0x2df;
	WBUFL(buf,2)=sd->status.account_id;
	memset(WBUFP(buf,6), 0, NAME_LENGTH);
	WBUFW(buf,30)=0;
	WBUFW(buf,32)=-1;
	WBUFW(buf,34)=-1;

	clif_send(buf, packet_len(0x2df), &sd->bl, BG_SAMEMAP_WOS);
}


/// Notifies clients of a battleground message (ZC_BATTLEFIELD_CHAT).
/// 02dc <packet len>.W <account id>.L <name>.24B <message>.?B
void clif_bg_message(struct battleground_data *bg, int src_id, const char *name, const char *mes, int len)
{
	struct map_session_data *sd = bg_getavailablesd(bg);

	if( !bg->count || sd == NULL )
		return;

	// limit length
	len = min(len + 1, CHAT_SIZE_MAX);

	unsigned char buf[8 + NAME_LENGTH + CHAT_SIZE_MAX];

	WBUFW(buf,0) = 0x2dc;
	WBUFW(buf,2) = len + NAME_LENGTH + 8;
	WBUFL(buf,4) = src_id;
	safestrncpy(WBUFP(buf,8), name, NAME_LENGTH);
	safestrncpy(WBUFCP(buf, 8 + NAME_LENGTH), mes, len);

	clif_send(buf, WBUFW(buf, 2), &sd->bl, BG);
}


/// Validates and processes battlechat messages.
/// All messages that are sent after enabling battleground chat with /battlechat.
/// 02DB <packet len>.W <text>.?B (CZ_BATTLEFIELD_CHAT)
void clif_parse_BattleChat(int fd, struct map_session_data* sd)
{
	char name[NAME_LENGTH], message[CHAT_SIZE_MAX], output[CHAT_SIZE_MAX + NAME_LENGTH * 2];

	if (!clif_process_message(sd, false, name, message, output))
		return;

	// Reset idle time when using battleground chat.
	sd->idletime = last_tick;

	bg_send_message(sd, output, strlen(output));
}


/// Notifies client of a battleground score change (ZC_BATTLEFIELD_NOTIFY_POINT).
/// 02de <camp A points>.W <camp B points>.W
void clif_bg_updatescore(int m)
{
	struct block_list bl;
	unsigned char buf[6];

	bl.id = 0;
	bl.type = BL_NUL;
	bl.m = m;

	WBUFW(buf,0) = 0x2de;
	WBUFW(buf,2) = map[m].bgscore_lion;
	WBUFW(buf,4) = map[m].bgscore_eagle;
	clif_send(buf,packet_len(0x2de),&bl,ALL_SAMEMAP);
}

void clif_bg_updatescore_single(struct map_session_data *sd)
{
	int fd;
	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x2de));
	WFIFOW(fd,0) = 0x2de;
	WFIFOW(fd,2) = map[sd->bl.m].bgscore_lion;
	WFIFOW(fd,4) = map[sd->bl.m].bgscore_eagle;
	WFIFOSET(fd,packet_len(0x2de));
}


/// Battleground camp belong-information (ZC_BATTLEFIELD_NOTIFY_CAMPINFO).
/// 02dd <account id>.L <name>.24B <camp>.W
void clif_sendbgemblem_area(struct map_session_data *sd)
{
	unsigned char buf[33];
	nullpo_retv(sd);

	WBUFW(buf, 0) = 0x2dd;
	WBUFL(buf,2) = sd->bl.id;
	safestrncpy((char*)WBUFP(buf,6), sd->status.name, NAME_LENGTH); // name don't show in screen.
	WBUFW(buf,30) = sd->bg_id;
	clif_send(buf,packet_len(0x2dd), &sd->bl, AREA);
}

void clif_sendbgemblem_single(int fd, struct map_session_data *sd)
{
	nullpo_retv(sd);
	WFIFOHEAD(fd,32);
	WFIFOW(fd,0) = 0x2dd;
	WFIFOL(fd,2) = sd->bl.id;
	safestrncpy((char*)WFIFOP(fd,6), sd->status.name, NAME_LENGTH);
	WFIFOW(fd,30) = sd->bg_id;
	WFIFOSET(fd,packet_len(0x2dd));
}

/// Battleground Queue

void clif_bgqueue_notice_delete(struct map_session_data *sd, enum BATTLEGROUNDS_QUEUE_NOTICE_DELETED response, const char *name)
{	
	unsigned char buf[30];

	nullpo_retv(sd);

	WBUFW(buf,0) = 0x8db;
	WBUFW(buf,2) = response;
	safestrncpy((char*)WBUFCP(buf,6), name, NAME_LENGTH);

	clif_send(buf, packet_len(0x8db), &sd->bl, SELF);
}

void clif_parse_bgqueue_register(int fd, struct map_session_data *sd)
{
	unsigned int packet_len;
	int16 type;
	char bg_name[NAME_LENGTH];
	struct battleground_arena *arena = NULL;

	struct s_packet_db* p = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	if( !battle_config.bg_queue ) return;

	packet_len = RFIFOW(fd,p->pos[0]);
	type = RFIFOL(fd,p->pos[1]);
	safestrncpy(bg_name, (const char*)RFIFOP(fd,p->pos[2]), sizeof(bg_name));

	if( !(arena = bg_name2arena(bg_name)) ) {
		clif_bgqueue_ack(sd,BGQA_FAIL_BGNAME_INVALID,0);
		return;
	}

	switch( (enum bg_queue_types)type ) {
		case BGQT_INDIVIDUAL:
		case BGQT_PARTY:
		case BGQT_GUILD:
			break;
		default:
			clif_bgqueue_ack(sd,BGQA_FAIL_TYPE_INVALID, arena->bg_arena_id);
			return;
	}

	bg_queue_add(sd, arena, (enum bg_queue_types)type);
}

void clif_bgqueue_joined(struct map_session_data *sd, int pos) {
	unsigned char buf[30];

	nullpo_retv(sd);

	WBUFW(buf,0) = 0x8d9;
	safestrncpy((char*)WBUFCP(buf,2),sd->status.name,NAME_LENGTH);
	WBUFL(buf,26) = pos;

	clif_send(buf,packet_len(0x8d9), &sd->bl, BG_QUEUE);
}

void clif_bgqueue_pcleft(struct map_session_data *sd) {
	/* no idea */
	return;
}

void clif_parse_bgqueue_checkstate(int fd, struct map_session_data *sd)
{
	unsigned int packet_len;
	char bg_name[NAME_LENGTH];

	struct s_packet_db* p = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
	packet_len = RFIFOW(fd,p->pos[0]);
	safestrncpy(bg_name, (const char*)RFIFOP(fd,p->pos[1]), sizeof(bg_name));

	if (sd->bg_queue.arena && sd->bg_queue.type) {
		clif_bgqueue_update_info(sd,sd->bg_queue.arena->bg_arena_id,bg_id2pos(sd->bg_queue.arena->queue_id,sd->status.account_id));
	} else {
		clif_bgqueue_notice_delete(sd, BGQND_FAIL_NOT_QUEUING,bg_name);
	}
}

void clif_parse_bgqueue_revoke_req(int fd, struct map_session_data *sd)
{
	unsigned int packet_len;
	char bg_name[NAME_LENGTH];

	struct s_packet_db* p = &packet_db[sd->packet_ver][RFIFOW(fd,0)];
	packet_len = RFIFOW(fd,p->pos[0]);
	safestrncpy(bg_name, (const char*)RFIFOP(fd,p->pos[1]), sizeof(bg_name));

	if( sd->bg_queue.arena )
		bg_queue_player_cleanup(sd);
	else
		clif_bgqueue_notice_delete(sd, BGQND_FAIL_NOT_QUEUING,bg_name);
}

void clif_parse_bgqueue_battlebegin_ack(int fd, struct map_session_data *sd)
{
	struct battleground_arena *arena;
	unsigned int packet_len;
	uint8 result;
	char bg_name[NAME_LENGTH], game_name[NAME_LENGTH];

	struct s_packet_db* p = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	if( !battle_config.bg_queue ) return;

	packet_len = RFIFOW(fd,p->pos[0]);
	result = RFIFOB(fd, p->pos[1]);
	safestrncpy(bg_name, (const char*)RFIFOP(fd,p->pos[2]), sizeof(bg_name));
	safestrncpy(game_name, (const char*)RFIFOP(fd,p->pos[3]), sizeof(game_name));

	if( ( arena = bg_name2arena(bg_name) )  ) {
		bg_queue_ready_ack(arena,sd, ( result == 1 ) ? true : false);
	} else {
		clif_bgqueue_ack(sd,BGQA_FAIL_BGNAME_INVALID, 0);
	}
}

void clif_bgqueue_update_info(struct map_session_data *sd, unsigned char arena_id, int position) {
	struct battleground_data *bg;
	unsigned char buf[30];
	
	nullpo_retv(sd);

	if(!sd->bg_id)
		return;

	if((bg = bg_team_search(sd->bg_id)) == NULL)
		return;

	Assert_retv(arena_id < bg->arena.bg_arena_id);
	
	WBUFW(buf,0) = 0x8d9;
	safestrncpy((char*)WBUFCP(buf,2), bg->arena.name, NAME_LENGTH);
	WBUFL(buf,26) = position;

	sd->bg_queue.client_has_bg_data = true; // Client creates bg data when this packet arrives

	clif_send(buf,packet_len(0x8d9), &sd->bl, SELF);
}

// Sends BG ready req to all with same bg arena/type as sd
void clif_bgqueue_battlebegins(struct map_session_data *sd, unsigned char arena_id, enum send_target target) {
	struct battleground_data *bg;
	unsigned char buf[50];

	nullpo_retv(sd);

	if(!sd->bg_id)
		return;

	if((bg = bg_team_search(sd->bg_id)) == NULL)
		return;

	Assert_retv(arena_id < bg->arena.bg_arena_id);

	WBUFW(buf,0) = 0x8df;
	safestrncpy((char*)WBUFCP(buf,2), bg->arena.name, NAME_LENGTH); // BG-Name
	safestrncpy((char*)WBUFCP(buf,26), bg->arena.name, NAME_LENGTH); // Game-Name

	clif_send(buf, packet_len(0x8df), &sd->bl, target);
}

void clif_bgqueue_ack(struct map_session_data *sd, enum BATTLEGROUNDS_QUEUE_ACK response, unsigned char arena_id) 
{
	struct battleground_data *bg;
	unsigned char buf[30];

	if(!sd->bg_id)
		return;

	if((bg = bg_team_search(sd->bg_id)) == NULL)
		return;

	switch (response) {
		case BGQA_FAIL_COOLDOWN:
		case BGQA_FAIL_DESERTER:
		case BGQA_FAIL_TEAM_COUNT:
			break;
		default: {
			nullpo_retv(sd);
			
			WBUFW(buf,0) = 0x8d8;
			WBUFW(buf,2) = response;
			safestrncpy((char*)WBUFCP(buf,6), bg->arena.name, NAME_LENGTH);

			clif_send(buf,packet_len(0x8df), &sd->bl, SELF);
		}
			break;
	}
}

/// Custom Fonts (ZC_NOTIFY_FONT).
/// 02ef <account_id>.L <font id>.W
void clif_font(struct map_session_data *sd)
{
#if PACKETVER >= 20080102
	unsigned char buf[8];
	nullpo_retv(sd);
	WBUFW(buf,0) = 0x2ef;
	WBUFL(buf,2) = sd->bl.id;
	WBUFW(buf,6) = sd->status.font;
	clif_send(buf, packet_len(0x2ef), &sd->bl, AREA);
#endif
}


/*==========================================
 * Instancing Window
 *------------------------------------------*/
int clif_instance(int instance_id, int type, int flag)
{
	struct map_session_data *sd;
	struct party_data *p;
	unsigned char buf[255];

	if( (p = party_search(instance[instance_id].party_id)) == NULL || (sd = party_getavailablesd(p)) == NULL )
		return 0;

	switch( type )
	{
	case 1:
		// S 0x2cb <Instance name>.61B <Standby Position>.W
		// Required to start the instancing information window on Client
		// This window re-appear each "refresh" of client automatically until type 4 is send to client.
		WBUFW(buf,0) = 0x02CB;
		memcpy(WBUFP(buf,2),instance[instance_id].name,INSTANCE_NAME_LENGTH);
		WBUFW(buf,63) = flag;
		clif_send(buf,packet_len(0x02CB),&sd->bl,PARTY);
		break;
	case 2:
		// S 0x2cc <Standby Position>.W
		// To announce Instancing queue creation if no maps available
		WBUFW(buf,0) = 0x02CC;
		WBUFW(buf,2) = flag;
		clif_send(buf,packet_len(0x02CC),&sd->bl,PARTY);
		break;
	case 3:
	case 4:
		// S 0x2cd <Instance Name>.61B <Instance Remaining Time>.L <Instance Noplayers close time>.L
		WBUFW(buf,0) = 0x02CD;
		memcpy(WBUFP(buf,2),instance[instance_id].name,61);
		if( type == 3 )
		{
			WBUFL(buf,63) = (uint32)instance[instance_id].progress_timeout;
			WBUFL(buf,67) = 0;
		}
		else
		{
			WBUFL(buf,63) = 0;
			WBUFL(buf,67) = (uint32)instance[instance_id].idle_timeout;
		}
		clif_send(buf,packet_len(0x02CD),&sd->bl,PARTY);
		break;
	case 5:
		// S 0x2ce <Message ID>.L
		// 0 = Notification (EnterLimitDate update?)
		// 1 = The Memorial Dungeon expired; it has been destroyed
		// 2 = The Memorial Dungeon's entry time limit expired; it has been destroyed
		// 3 = The Memorial Dungeon has been removed.
		// 4 = Create failure (removes the instance window)
		WBUFW(buf,0) = 0x02CE;
		WBUFL(buf,2) = flag;
		//WBUFL(buf,6) = EnterLimitDate;
		clif_send(buf,packet_len(0x02CE),&sd->bl,PARTY);
		break;
	}
	return 0;
}

void clif_instance_join(int fd, int instance_id)
{
	if( instance[instance_id].idle_timer != INVALID_TIMER )
	{
		WFIFOHEAD(fd,packet_len(0x02CD));
		WFIFOW(fd,0) = 0x02CD;
		memcpy(WFIFOP(fd,2),instance[instance_id].name,61);
		WFIFOL(fd,63) = 0;
		WFIFOL(fd,67) = (uint32)instance[instance_id].idle_timeout;
		WFIFOSET(fd,packet_len(0x02CD));
	}
	else if( instance[instance_id].progress_timer != INVALID_TIMER )
	{
		WFIFOHEAD(fd,packet_len(0x02CD));
		WFIFOW(fd,0) = 0x02CD;
		memcpy(WFIFOP(fd,2),instance[instance_id].name,61);
		WFIFOL(fd,63) = (uint32)instance[instance_id].progress_timeout;;
		WFIFOL(fd,67) = 0;
		WFIFOSET(fd,packet_len(0x02CD));
	}
	else
	{
		WFIFOHEAD(fd,packet_len(0x02CB));
		WFIFOW(fd,0) = 0x02CB;
		memcpy(WFIFOP(fd,2),instance[instance_id].name,61);
		WFIFOW(fd,63) = 0;
		WFIFOSET(fd,packet_len(0x02CB));
	}
}

void clif_instance_leave(int fd)
{
	WFIFOHEAD(fd,packet_len(0x02CE));
	WFIFOW(fd,0) = 0x02ce;
	WFIFOL(fd,2) = 4;
	WFIFOSET(fd,packet_len(0x02CE));
}


/// Notifies clients about item picked up by a party member (ZC_ITEM_PICKUP_PARTY).
/// 02b8 <account id>.L <name id>.W <identified>.B <damaged>.B <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W <equip location>.W <item type>.B
void clif_party_show_picker(struct map_session_data* sd, struct item* item_data) {
#if PACKETVER >= 20071002
	nullpo_retv(sd);
	nullpo_retv(item_data);

	struct item_data* id = itemdb_search(item_data->nameid);

	struct PACKET_ZC_ITEM_PICKUP_PARTY p;

	p.packetType = 0x2b8;
	p.AID = sd->status.account_id;
	p.itemId = client_nameid(item_data->nameid);
	p.identified = item_data->identify;
	p.damaged = item_data->attribute;
	p.refine = item_data->refine;
	clif_addcards(&p.slot, item_data);
	p.location = id->equip; // equip location
	p.itemType = itemtype(id->nameid); // item type

	clif_send(&p, sizeof(p), &sd->bl, PARTY_SAMEMAP_WOS);
#endif
}

void clif_equip_damaged(struct map_session_data *sd, int equip_index)
{
#if PACKETVER >= 20070521
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x2bb));
	WFIFOW(fd,0) = 0x2bb;
	WFIFOW(fd,2) = equip_index;
	WFIFOL(fd,4) = sd->bl.id;
	WFIFOSET(fd,packet_len(0x2bb));
#endif
}

/// Display gained exp (ZC_NOTIFY_EXP).
/// 07f6 <account id>.L <amount>.L <var id>.W <exp type>.W (ZC_NOTIFY_EXP)
/// 0acc <account id>.L <amount>.Q <var id>.W <exp type>.W (ZC_NOTIFY_EXP2)
/// var id:
///     SP_BASEEXP, SP_JOBEXP
/// exp type:
///     0 = normal exp gain/loss
///     1 = quest exp gain/loss
/// @param sd Player
/// @param exp EXP value gained/loss
/// @param type SP_BASEEXP, SP_JOBEXP
/// @param quest False:Normal EXP; True:Quest EXP (displayed in purple color)
/// @param lost True:if lossing EXP
void clif_displayexp(struct map_session_data *sd, unsigned int exp, char type, bool quest, bool lost)
{
	int fd;
	int offset;
#if PACKETVER >= 20170830
	int cmd = 0xacc;
#else
	int cmd = 0x7f6;
#endif

	nullpo_retv(sd);

	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(cmd));
	WFIFOW(fd, 0) = cmd;
	WFIFOL(fd,2) = sd->bl.id;
#if PACKETVER >= 20170830
	WFIFOQ(fd, 6) = (int64)u64min((uint64)exp, INT_MAX) * (lost ? -1 : 1);
	offset = 4;
#else
	WFIFOL(fd,6) = (int)umin(exp, INT_MAX) * (lost ? -1 : 1);
	offset = 0;
#endif
	WFIFOW(fd, 10 + offset) = type;
	WFIFOW(fd, 12 + offset) = (quest && type != SP_JOBEXP) ? 1 : 0; // NOTE: Somehow JobEXP always in yellow color
	WFIFOSET(fd, packet_len(cmd));
}

/// S 07e4 <length>.w <option>.l <val>.l {<index>.w <amount>.w).4b* (CZ_ITEMLISTWIN_RES)
/// S 0945 <length>.w <option>.l <val>.l {<index>.w <amount>.w).4b* (CZ_* RagexeRE 2012-04-10a)
/// S 0281 <length>.w <option>.l <val>.l {<index>.w <amount>.w).4b* (CZ_* Ragexe 2013-08-07)
void clif_parse_ItemListWindowSelected(int fd, struct map_session_data* sd) {
	int n = (RFIFOW(fd,2)-12) / 4;
	int type = RFIFOL(fd,4);
	int flag = RFIFOL(fd,8); // Button clicked: 0 = Cancel, 1 = OK
	unsigned short* item_list = (unsigned short*)RFIFOP(fd,12);

	if( sd->state.trading || sd->npc_shopid )
		return;
	
	if( flag == 0 || n == 0) {
		sd->menuskill_id = sd->menuskill_val = sd->menuskill_itemused = 0;
		return; // Canceled by player.
	}

	if( sd->menuskill_id != SO_EL_ANALYSIS && sd->menuskill_id != GN_CHANGEMATERIAL ) {
		sd->menuskill_id = sd->menuskill_val = sd->menuskill_itemused = 0;
		return; // Prevent hacking.
	}

	switch( type ) {
		case 0: // Change Material
			skill_changematerial(sd,n,item_list);
			break;
		case 1:	// Level 1: Pure to Rough
		case 2:	// Level 2: Rough to Pure
			skill_elementalanalysis(sd,n,type,item_list);
			break;
	}
	sd->menuskill_id = sd->menuskill_val = sd->menuskill_itemused = 0;

	return;
}

/// Displays digital clock digits on top of the screen (ZC_SHOWDIGIT).
/// type:
///     0 = Displays 'value' for 5 seconds.
///     1 = Incremental counter (1 tick/second), negated 'value' specifies start value (e.g. using -10 lets the counter start at 10).
///     2 = Decremental counter (1 tick/second), negated 'value' specifies start value (does not stop when reaching 0, but overflows).
///     3 = Decremental counter (1 tick/second), 'value' specifies start value (stops when reaching 0, displays at most 2 digits).
/// value:
///     Except for type 3 it is interpreted as seconds for displaying as DD:HH:MM:SS, HH:MM:SS, MM:SS or SS (leftmost '00' is not displayed).
void clif_showdigit(struct map_session_data* sd, unsigned char type, int value)
{
	WFIFOHEAD(sd->fd, packet_len(0x1b1));
	WFIFOW(sd->fd,0) = 0x1b1;
	WFIFOB(sd->fd,2) = type;
	WFIFOL(sd->fd,3) = value;
	WFIFOSET(sd->fd, packet_len(0x1b1));
}


/// Notification of the state of client command /effect (CZ_LESSEFFECT).
/// 021d <state>.L
/// state:
///     0 = Full effects
///     1 = Reduced effects
///
/// NOTE:   The state is used on Aegis for sending skill unit packet
///         0x11f (ZC_SKILL_ENTRY) instead of 0x1c9 (ZC_SKILL_ENTRY2)
///         whenever possible. Due to the way the decision check is
///         constructed, this state tracking was rendered useless,
///         as the only skill unit, that is sent with 0x1c9 is
///         Graffiti.
void clif_parse_LessEffect(int fd, struct map_session_data* sd)
{
	int isLess = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	sd->state.lesseffect = ( isLess != 0 );
}


/// Buying Store System
///

/// Opens preparation window for buying store (ZC_OPEN_BUYING_STORE).
/// 0810 <slots>.B
void clif_buyingstore_open(struct map_session_data* sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x810));
	WFIFOW(fd,0) = 0x810;
	WFIFOB(fd,2) = sd->buyingstore.slots;
	WFIFOSET(fd,packet_len(0x810));
}


/// Request to create a buying store (CZ_REQ_OPEN_BUYING_STORE).
/// 0811 <packet len>.W <limit zeny>.L <result>.B <store name>.80B { <name id>.W <amount>.W <price>.L }*
/// result:
///     0 = cancel
///     1 = open
static void clif_parse_ReqOpenBuyingStore(int fd, struct map_session_data* sd) {
	const struct PACKET_CZ_REQ_OPEN_BUYING_STORE *p = (struct PACKET_CZ_REQ_OPEN_BUYING_STORE *)RFIFOP(fd, 0);

	// TODO: Make this check global for all variable length packets.
	// minimum packet length
	if (p->packetLength < sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE)) {
		ShowError("clif_parse_ReqOpenBuyingStore: Malformed packet (expected length=%" PRIdPTR ", length=%u, account_id=%d).\n", sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE), p->packetLength, sd->bl.id);
		return;
	}

	char storename[MESSAGE_SIZE];

	safestrncpy(storename, p->storeName, sizeof(storename));

	// so that buyingstore_create knows, how many elements it has access to
	int packet_len = p->packetLength - sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE);

	if (packet_len % sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub)) {
		ShowError("clif_parse_ReqOpenBuyingStore: Unexpected item list size %u (account_id=%d, block size=%" PRIdPTR ")\n", packet_len, sd->bl.id, sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub));
		return;
	}

	buyingstore_create(sd, p->zenyLimit, p->result, storename, p->items, packet_len / sizeof(struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub));
}


/// Notification, that the requested buying store could not be created (ZC_FAILED_OPEN_BUYING_STORE_TO_BUYER).
/// 0812 <result>.W <total weight>.L
/// result:
///     1 = "Failed to open buying store." (0x6cd, MSI_BUYINGSTORE_OPEN_FAILED)
///     2 = "Total amount of then possessed items exceeds the weight limit by <weight/10-maxweight*90%>. Please re-enter." (0x6ce, MSI_BUYINGSTORE_OVERWEIGHT)
///     8 = "No sale (purchase) information available." (0x705)
///     ? = nothing
void clif_buyingstore_open_failed(struct map_session_data* sd, unsigned short result, unsigned int weight)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x812));
	WFIFOW(fd,0) = 0x812;
	WFIFOW(fd,2) = result;
	WFIFOL(fd,4) = weight;
	WFIFOSET(fd,packet_len(0x812));
}

/// Notification, that the requested buying store was created (ZC_MYITEMLIST_BUYING_STORE).
/// 0813 <packet len>.W <account id>.L <limit zeny>.L { <price>.L <count>.W <type>.B <name id>.W }*
void clif_buyingstore_myitemlist(struct map_session_data* sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	int length = sizeof(struct PACKET_ZC_MYITEMLIST_BUYING_STORE) + sd->buyingstore.slots * sizeof(struct PACKET_ZC_MYITEMLIST_BUYING_STORE_sub);
	WFIFOHEAD(fd, length);
	struct PACKET_ZC_MYITEMLIST_BUYING_STORE *p = (struct PACKET_ZC_MYITEMLIST_BUYING_STORE *)WFIFOP(fd, 0);
	p->packetType = 0x813;
	p->packetLength = length;
	p->AID = sd->bl.id;
	p->zenyLimit = sd->buyingstore.zenylimit;

	for (int i = 0; i < sd->buyingstore.slots; i++) {
		p->items[i].price = sd->buyingstore.items[i].price;
		p->items[i].amount = sd->buyingstore.items[i].amount;
		p->items[i].itemType = itemtype(sd->buyingstore.items[i].nameid);
		p->items[i].itemId = client_nameid(sd->buyingstore.items[i].nameid);
	}

	WFIFOSET(fd, p->packetLength);
}

/// Notifies clients in area of a buying store (ZC_BUYING_STORE_ENTRY).
/// 0814 <account id>.L <store name>.80B
void clif_buyingstore_entry(struct map_session_data* sd)
{
	uint8 buf[MESSAGE_SIZE + 6];

	WBUFW(buf,0) = 0x814;
	WBUFL(buf,2) = sd->bl.id;
	safestrncpy(WBUFCP(buf, 6), sd->message, MESSAGE_SIZE);

	clif_send(buf, packet_len(0x814), &sd->bl, AREA_WOS);
}
void clif_buyingstore_entry_single(struct map_session_data* sd, struct map_session_data* pl_sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x814));
	WFIFOW(fd,0) = 0x814;
	WFIFOL(fd,2) = pl_sd->bl.id;
	safestrncpy(WFIFOCP(fd, 6), pl_sd->message, MESSAGE_SIZE);
	WFIFOSET(fd,packet_len(0x814));
}


/// Request to close own buying store (CZ_REQ_CLOSE_BUYING_STORE).
/// 0815
static void clif_parse_ReqCloseBuyingStore(int fd, struct map_session_data* sd)
{
	buyingstore_close(sd);
}


/// Notifies clients in area that a buying store was closed (ZC_DISAPPEAR_BUYING_STORE_ENTRY).
/// 0816 <account id>.L
void clif_buyingstore_disappear_entry(struct map_session_data* sd)
{
	uint8 buf[6];

	WBUFW(buf,0) = 0x816;
	WBUFL(buf,2) = sd->bl.id;

	clif_send(buf, packet_len(0x816), &sd->bl, AREA_WOS);
}
void clif_buyingstore_disappear_entry_single(struct map_session_data* sd, struct map_session_data* pl_sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x816));
	WFIFOW(fd,0) = 0x816;
	WFIFOL(fd,2) = pl_sd->bl.id;
	WFIFOSET(fd,packet_len(0x816));
}


/// Request to open someone else's buying store (CZ_REQ_CLICK_TO_BUYING_STORE).
/// 0817 <account id>.L
static void clif_parse_ReqClickBuyingStore(int fd, struct map_session_data* sd)
{
	uint32 account_id;

	account_id = RFIFOL(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]);

	buyingstore_open(sd, account_id);
}


/// Sends buying store item list (ZC_ACK_ITEMLIST_BUYING_STORE).
/// 0818 <packet len>.W <account id>.L <store id>.L <limit zeny>.L { <price>.L <amount>.W <type>.B <name id>.W }*
void clif_buyingstore_itemlist(struct map_session_data* sd, struct map_session_data* pl_sd) {
	nullpo_retv(sd);
	nullpo_retv(pl_sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
	}

	int length = sizeof(struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE) + pl_sd->buyingstore.slots * sizeof(struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE_sub);
	WFIFOHEAD(fd, length);
	struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE *p = (struct PACKET_ZC_ACK_ITEMLIST_BUYING_STORE *)WFIFOP(fd, 0);
	p->packetType = 0x818;
	p->packetLength = length;
	p->AID = pl_sd->bl.id;
	p->storeId = pl_sd->buyer_id;
	p->zenyLimit = pl_sd->buyingstore.zenylimit;

	for (int i = 0; i < pl_sd->buyingstore.slots; i++) {
		p->items[i].price = pl_sd->buyingstore.items[i].price;
		p->items[i].amount = pl_sd->buyingstore.items[i].amount;  // TODO: Figure out, if no longer needed items (amount == 0) are listed on official.
		p->items[i].itemType = itemtype(pl_sd->buyingstore.items[i].nameid);
		p->items[i].itemId = client_nameid(pl_sd->buyingstore.items[i].nameid);
	}

	WFIFOSET(fd, p->packetLength);
}

/// Request to sell items to a buying store (CZ_REQ_TRADE_BUYING_STORE).
/// 0819 <packet len>.W <account id>.L <store id>.L { <index>.W <name id>.W <amount>.W }*
static void clif_parse_ReqTradeBuyingStore(int fd, struct map_session_data* sd) {
	const struct PACKET_CZ_REQ_TRADE_BUYING_STORE *p = (struct PACKET_CZ_REQ_TRADE_BUYING_STORE *)RFIFOP(fd, 0);

	// minimum packet length
	if (p->packetLength < sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE)) {
		ShowError("clif_parse_ReqTradeBuyingStore: Malformed packet (expected length=%" PRIdPTR ", length=%u, account_id=%d).\n", sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE), p->packetLength, sd->bl.id);
		return;
	}

	// so that buyingstore_trade knows, how many elements it has access to
	int packet_len = p->packetLength - sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE);

	if (packet_len % sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub)) {
		ShowError("clif_parse_ReqTradeBuyingStore: Unexpected item list size %u (account_id=%d, buyer_id=%d, block size=%" PRIdPTR ")\n", packet_len, sd->bl.id, p->AID, sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub));
		return;
	}

	buyingstore_trade(sd, p->AID, p->storeId, p->items, packet_len / sizeof(struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub));
}

/// Notifies the buyer, that the buying store has been closed due to a post-trade condition (ZC_FAILED_TRADE_BUYING_STORE_TO_BUYER).
/// 081a <result>.W
/// result:
///     3 = "All items within the buy limit were purchased." (0x6cf, MSI_BUYINGSTORE_TRADE_OVERLIMITZENY)
///     4 = "All items were purchased." (0x6d0, MSI_BUYINGSTORE_TRADE_BUYCOMPLETE)
///     ? = nothing
void clif_buyingstore_trade_failed_buyer(struct map_session_data* sd, short result)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x81a));
	WFIFOW(fd,0) = 0x81a;
	WFIFOW(fd,2) = result;
	WFIFOSET(fd,packet_len(0x81a));
}


/// Updates the zeny limit and an item in the buying store item list.
/// 081b <name id>.W <amount>.W <limit zeny>.L (ZC_UPDATE_ITEM_FROM_BUYING_STORE)
/// 09e6 <name id>.W <amount>.W <zeny>.L <limit zeny>.L <GID>.L <Date>.L (ZC_UPDATE_ITEM_FROM_BUYING_STORE2)
void clif_buyingstore_update_item(struct map_session_data* sd, t_itemid nameid, unsigned short amount, uint32 char_id, int zeny) {
	struct PACKET_ZC_UPDATE_ITEM_FROM_BUYING_STORE p;

	p.packetType = buyingStoreUpdateItemType;
	p.itemId = client_nameid(nameid);
	p.amount = amount;
	p.zenyLimit = sd->buyingstore.zenylimit;
#if PACKETVER >= 20141016
	p.zeny = zeny;
	p.charId = char_id;  // GID
	p.updateTime = (int)time(NULL);
#endif

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Deletes item from inventory, that was sold to a buying store (ZC_ITEM_DELETE_BUYING_STORE).
/// 081c <index>.W <amount>.W <price>.L
/// message:
///     "%s (%d) were sold at %dz." (0x6d2, MSI_BUYINGSTORE_TRADE_SELLCOMPLETE)
///
/// NOTE:   This function has to be called _instead_ of clif_delitem/clif_dropitem.
void clif_buyingstore_delete_item(struct map_session_data* sd, short index, unsigned short amount, int price)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x81c));
	WFIFOW(fd,0) = 0x81c;
	WFIFOW(fd,2) = index+2;
	WFIFOW(fd,4) = amount;
	WFIFOL(fd,6) = price;  // price per item, client calculates total Zeny by itself
	WFIFOSET(fd,packet_len(0x81c));
}


/// Notifies the seller, that a buying store trade failed (ZC_FAILED_TRADE_BUYING_STORE_TO_SELLER).
/// 0824 <result>.W <name id>.W
/// result:
///     5 = "The deal has failed." (0x39, MSI_DEAL_FAIL)
///     6 = "The trade failed, because the entered amount of item %s is higher, than the buyer is willing to buy." (0x6d3, MSI_BUYINGSTORE_TRADE_OVERCOUNT)
///     7 = "The trade failed, because the buyer is lacking required balance." (0x6d1, MSI_BUYINGSTORE_TRADE_LACKBUYERZENY)
///     ? = nothing
void clif_buyingstore_trade_failed_seller(struct map_session_data* sd, short result, t_itemid nameid) {
	struct PACKET_ZC_FAILED_TRADE_BUYING_STORE_TO_SELLER p;

	p.packetType = 0x824;
	p.result = result;
	p.itemId = client_nameid(nameid);

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Search Store Info System
///

/// Request to search for stores (CZ_SEARCH_STORE_INFO).
/// 0835 <packet len>.W <type>.B <max price>.L <min price>.L <name id count>.B <card count>.B { <name id>.W }* { <card>.W }*
/// type:
///     0 = Vending
///     1 = Buying Store
///
/// NOTE:   The client determines the item ids by specifying a name and optionally,
///         amount of card slots. If the client does not know about the item it
///         cannot be searched.
static void clif_parse_SearchStoreInfo(int fd, struct map_session_data *sd) {
	const struct PACKET_CZ_SEARCH_STORE_INFO *p = (struct PACKET_CZ_SEARCH_STORE_INFO *)RFIFOP(fd, 0);

	// minimum packet length
	if (p->packetLength < sizeof(struct PACKET_CZ_SEARCH_STORE_INFO)) {
		ShowError("clif_parse_SearchStoreInfo: Malformed packet (expected length=%" PRIdPTR ", length=%u, account_id=%d).\n", sizeof(struct PACKET_CZ_SEARCH_STORE_INFO), p->packetLength, sd->bl.id);
		return;
	}

	// check, if there is enough data for the claimed count of items
	int packet_len = p->packetLength - sizeof(struct PACKET_CZ_SEARCH_STORE_INFO);

	if (packet_len % sizeof(struct PACKET_CZ_SEARCH_STORE_INFO_item)) {
		ShowError("clif_parse_SearchStoreInfo: Unexpected item list size %u (account_id=%d, block size=%" PRIdPTR ")\n", packet_len, sd->bl.id, sizeof(struct PACKET_CZ_SEARCH_STORE_INFO_item));
		return;
	}

	int count = packet_len / sizeof(struct PACKET_CZ_SEARCH_STORE_INFO_item);

	if (count < p->itemsCount + p->cardsCount) {
		ShowError("clif_parse_SearchStoreInfo: Malformed packet (expected count=%u, count=%u, account_id=%d).\n", p->itemsCount + p->cardsCount, count, sd->bl.id);
		return;
	}

	searchstore_query(sd, p->searchType, p->minPrice, p->maxPrice, &p->items[0], p->itemsCount, &p->items[p->itemsCount], p->cardsCount);
}

/// Results for a store search request (ZC_SEARCH_STORE_INFO_ACK).
/// 0836 <packet len>.W <is first page>.B <is next page>.B <remaining uses>.B { <store id>.L <account id>.L <shop name>.80B <nameid>.W <item type>.B <price>.L <amount>.W <refine>.B <card1>.W <card2>.W <card3>.W <card4>.W }*
/// is first page:
///     0 = appends to existing results
///     1 = clears previous results before displaying this result set
/// is next page:
///     0 = no "next" label
///     1 = "next" label to retrieve more results
void clif_search_store_info_ack(struct map_session_data* sd) {
	nullpo_retv(sd);

	int fd = sd->fd;

	if (!session_isActive(fd)) {
		return;
}

	unsigned int start = sd->searchstore.pages * SEARCHSTORE_RESULTS_PER_PAGE;
	unsigned int end = umin(sd->searchstore.count, start + SEARCHSTORE_RESULTS_PER_PAGE);
	int len = sizeof(struct PACKET_ZC_SEARCH_STORE_INFO_ACK) + (end - start) * sizeof(struct PACKET_ZC_SEARCH_STORE_INFO_ACK_sub);

	WFIFOHEAD(fd, len);

	struct PACKET_ZC_SEARCH_STORE_INFO_ACK *p = (struct PACKET_ZC_SEARCH_STORE_INFO_ACK *)WFIFOP(fd, 0);

	p->packetType = 0x836;
	p->packetLength = len;
	p->firstPage = !sd->searchstore.pages;
	p->nextPage = searchstore_querynext(sd);
	p->usesCount = (uint8)umin(sd->searchstore.uses, UINT8_MAX);

	for (int i = start; i < end; i++) {
		struct s_search_store_info_item* ssitem = &sd->searchstore.items[i];

		p->items[i].storeId = ssitem->store_id;
		p->items[i].AID = ssitem->account_id;
		safestrncpy(p->items[i].shopName, ssitem->store_name, MESSAGE_SIZE);
		p->items[i].itemId = client_nameid(ssitem->nameid);
		p->items[i].itemType = itemtype(ssitem->nameid);
		p->items[i].price = ssitem->price;
		p->items[i].amount = ssitem->amount;
		p->items[i].refine = ssitem->refine;

		// make-up an item for clif_addcards
		struct item it;

		memset(&it, 0, sizeof(it));
		memcpy(&it.card, &ssitem->card, sizeof(it.card));
		it.nameid = ssitem->nameid;
		it.amount = ssitem->amount;

		clif_addcards(&p->items[i].slot, &it);
#if PACKETVER >= 20150226
		clif_add_random_options(p->items[i].option_data, &it);
#endif
	}

	WFIFOSET(fd, len);
}

/// Notification of failure when searching for stores (ZC_SEARCH_STORE_INFO_FAILED).
/// 0837 <reason>.B
/// reason:
///     0 = "No matching stores were found." (0x70b)
///     1 = "There are too many results. Please enter more detailed search term." (0x6f8)
///     2 = "You cannot search anymore." (0x706)
///     3 = "You cannot search yet." (0x708)
///     4 = "No sale (purchase) information available." (0x705)
void clif_search_store_info_failed(struct map_session_data* sd, unsigned char reason)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x837));
	WFIFOW(fd,0) = 0x837;
	WFIFOB(fd,2) = reason;
	WFIFOSET(fd,packet_len(0x837));
}


/// Request to display next page of results (CZ_SEARCH_STORE_INFO_NEXT_PAGE).
/// 0838
static void clif_parse_SearchStoreInfoNextPage(int fd, struct map_session_data* sd)
{
	searchstore_next(sd);
}


/// Opens the search store window (ZC_OPEN_SEARCH_STORE_INFO).
/// 083a <type>.W <remaining uses>.B
/// type:
///     0 = Search Stores
///     1 = Search Stores (Cash), asks for confirmation, when clicking a store
void clif_open_search_store_info(struct map_session_data* sd)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x83a));
	WFIFOW(fd,0) = 0x83a;
	WFIFOW(fd,2) = sd->searchstore.effect;
#if PACKETVER > 20100701
	WFIFOB(fd,4) = (unsigned char)min(sd->searchstore.uses, UINT8_MAX);
#endif
	WFIFOSET(fd,packet_len(0x83a));
}


/// Request to close the store search window (CZ_CLOSE_SEARCH_STORE_INFO).
/// 083b
static void clif_parse_CloseSearchStoreInfo(int fd, struct map_session_data* sd)
{
	searchstore_close(sd);
}


/// Request to invoke catalog effect on a store from search results (CZ_SSILIST_ITEM_CLICK).
/// 083c <account id>.L <store id>.L <nameid>.W
static void clif_parse_SearchStoreInfoListItemClick(int fd, struct map_session_data* sd)
{
	unsigned short nameid;
	uint32 account_id, store_id;
	struct s_packet_db* info = &packet_db[sd->packet_ver][RFIFOW(fd,0)];

	account_id = RFIFOL(fd,info->pos[0]);
	store_id   = RFIFOL(fd,info->pos[1]);
	nameid     = RFIFOW(fd,info->pos[2]);

	searchstore_click(sd, account_id, store_id, nameid);
}


/// Notification of the store position on current map (ZC_SSILIST_ITEM_CLICK_ACK).
/// 083d <xPos>.W <yPos>.W
void clif_search_store_info_click_ack(struct map_session_data* sd, short x, short y)
{
	int fd = sd->fd;

	WFIFOHEAD(fd,packet_len(0x83d));
	WFIFOW(fd,0) = 0x83d;
	WFIFOW(fd,2) = x;
	WFIFOW(fd,4) = y;
	WFIFOSET(fd,packet_len(0x83d));
}

void clif_monster_hp_bar( struct mob_data* md, int fd )
{
	WFIFOHEAD(fd,packet_len(0x977));	
	WFIFOW(fd,0)  = 0x977;
	WFIFOL(fd,2)  = md->bl.id;
	WFIFOL(fd,6)  = md->status.hp;
	WFIFOL(fd,10) = md->status.max_hp;	
	WFIFOSET(fd,packet_len(0x977));
}

/*==========================================
* ZC_FASTMOVE = 0x8d2
* this+0x0 / short PacketType
* this+0x2 / unsigned long AID
* this+0x6 / short targetXpos
* this+0x8 / short targetYpos
*------------------------------------------*/
void clif_fast_movement(struct block_list *bl, short x, short y)
{
	unsigned char buf[10];

	WBUFW(buf,0) = 0x8d2;
	WBUFL(buf,2) = bl->id;
	WBUFW(buf,6) = x;
	WBUFW(buf,8) = y;

	clif_send(buf,packet_len(0x8d2),bl,AREA);
}

/**
 * 02d3 <index>.W (ZC_NOTIFY_BIND_ON_EQUIP)
 **/
void clif_notify_bindOnEquip(struct map_session_data *sd, int n) {
	struct s_packet_db *info = NULL;
	int cmd = 0;

	nullpo_retv(sd);

	cmd = packet_db_ack[sd->packet_ver][ZC_NOTIFY_BIND_ON_EQUIP];
	info = &packet_db[sd->packet_ver][cmd];
	if (!cmd || !info->len)
		return;

	WFIFOHEAD(sd->fd, info->len);
	WFIFOW(sd->fd, 0) = cmd;
	WFIFOW(sd->fd, info->pos[0]) = n + 2;
	WFIFOSET(sd->fd, info->len);
}

/// [Ind/Hercules]
void clif_showscript(struct block_list* bl, const char* message) {
	char buf[256];
	uint16 len;
	nullpo_retv(bl);

	if(!message)
		return;

	len = (uint16)strlen(message)+1;

	if( len > sizeof(buf)-8 ) {
		ShowWarning("clif_showscript: Truncating too long message '%s' (len=%d).\n", message, len);
		len = sizeof(buf)-8;
	}

	WBUFW(buf,0) = 0x8b3;
	WBUFW(buf,2) = (len+8);
	WBUFL(buf,4) = bl->id;
	safestrncpy((char *) WBUFP(buf,8), message, len);
	clif_send((unsigned char *) buf, WBUFW(buf,2), bl, AREA);
}

/**
 * Decrypt packet identifier for player
 * @param fd
 * @param sd
 * @param packet_ver
 * Orig author [Ind/Hercules]
 **/
static unsigned short clif_parse_cmd(int fd, struct map_session_data *sd) {
#ifndef PACKET_OBFUSCATION
	return RFIFOW(fd, 0);
#else
	unsigned short cmd = RFIFOW(fd,0); // Check if it is a player that tries to connect to the map server.
	ShowDebug("clif_parse_cmd: Raw packet ID: 0x%04x, %d bytes received.\n", cmd, RFIFOREST(fd));
	if (sd) {
		cmd = (cmd ^ ((sd->cryptKey >> 16) & 0x7FFF)); // Decrypt the current packet ID with the last key stored in the session.
		ShowDebug("clif_parse_cmd: Decrypted packet ID: 0x%04x, %d bytes received.\n", cmd, RFIFOREST(fd));
	} else {
		cmd = (cmd ^ ((((clif_cryptKey[0] * clif_cryptKey[1]) + clif_cryptKey[2]) >> 16) & 0x7FFF)); // A player tries to connect - use the initial keys for the decryption of the packet ID.
		ShowDebug("clif_parse_cmd: Decrypted packet ID (no Player): 0x%04x, %d bytes received.\n", cmd, RFIFOREST(fd));
	}
	return cmd; // Return the decrypted packet ID.
#endif
}

/// Parse function for packet debugging.
void clif_parse_debug(int fd,struct map_session_data *sd)
{
	int cmd, packet_len;

	// clif_parse ensures, that there is at least 2 bytes of data
	cmd = RFIFOW(fd,0);

	if( sd )
	{
		packet_len = packet_db[sd->packet_ver][cmd].len;

		if( packet_len == 0 )
		{// unknown
			packet_len = RFIFOREST(fd);
		}
		else if( packet_len == -1 )
		{// variable length
			packet_len = RFIFOW(fd,2);  // clif_parse ensures, that this amount of data is already received
		}
		ShowDebug("Packet debug of 0x%04X (length %d), %s session #%d, %d/%d (AID/CID)\n", cmd, packet_len, sd->state.active ? "authed" : "unauthed", fd, sd->status.account_id, sd->status.char_id);
	}
	else
	{
		packet_len = RFIFOREST(fd);
		ShowDebug("Packet debug of 0x%04X (length %d), session #%d\n", cmd, packet_len, fd);
	}

	ShowDump(RFIFOP(fd,0), packet_len);
}

/// Roulette System
/// Author: Yommy

/**
 * Opens Roulette window
 * @param fd
 * @param sd
 */
void clif_parse_RouletteOpen(int fd, struct map_session_data* sd)
{
	nullpo_retv(sd);

	if (!battle_config.feature_roulette) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,724)); //Roulette is disabled
		return;
	}

	WFIFOHEAD(fd,packet_len(0xa1a));
	WFIFOW(fd,0) = 0xa1a;
	WFIFOB(fd,2) = 0; // result
	WFIFOL(fd,3) = 0; // serial
	WFIFOB(fd,7) = (sd->roulette.claimPrize) ? sd->roulette.stage - 1 : 0;
	WFIFOB(fd,8) = (sd->roulette.claimPrize) ? sd->roulette.prizeIdx : -1;
	WFIFOW(fd,9) = -1; //! TODO: Display bonus item
	WFIFOL(fd,11) = sd->roulette_point.gold;
	WFIFOL(fd,15) = sd->roulette_point.silver;
	WFIFOL(fd,19) = sd->roulette_point.bronze;
	WFIFOSET(fd,packet_len(0xa1a));
}

/**
 * Generates information to be displayed
 * @param fd
 * @param sd
 */
void clif_parse_RouletteInfo(int fd, struct map_session_data* sd)
{
	unsigned short i, j, count = 0;
	int len = 8 + (42 * 8);

	nullpo_retv(sd);

	if (!battle_config.feature_roulette) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,724)); //Roulette is disabled
		return;
	}

	WFIFOHEAD(fd,len);
	WFIFOW(fd,0) = 0xa1c;
	WFIFOW(fd,2) = len;
	WFIFOL(fd,4) = 1; // serial

	for(i = 0; i < MAX_ROULETTE_LEVEL; i++) {
		for(j = 0; j < MAX_ROULETTE_COLUMNS - i; j++) {
			WFIFOW(fd,8 * count + 8) = i;
			WFIFOW(fd,8 * count + 10) = j;
			WFIFOW(fd,8 * count + 12) = rd.nameid[i][j];
			WFIFOW(fd,8 * count + 14) = rd.qty[i][j];
#if PACKETVER >= 20180523  // unknown real version
			WFIFOW(fd, 8 * count + 16) = 0; // unused
#endif
			count++;
		}
	}

	WFIFOSET(fd,len);
	return;
}

/**
 * Closes Roulette window
 * @param fd
 * @param sd
 */
void clif_parse_RouletteClose(int fd, struct map_session_data* sd)
{
	nullpo_retv(sd);

	if (!battle_config.feature_roulette) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,724)); //Roulette is disabled
		return;
	}

	/** What do we need this for? (other than state tracking), game client closes the window without our response. **/
	return;
}

 /**
 *
 **/
static void clif_roulette_recvitem_ack(struct map_session_data *sd, enum RECV_ROULETTE_ITEM_REQ type) {
#if PACKETVER >= 20141016
	uint16 cmd = 0xa22;
	unsigned char buf[5];

	nullpo_retv(sd);

	if (packet_db[sd->packet_ver][cmd].len == 0)
		return;

	WBUFW(buf,0) = cmd;
	WBUFB(buf,2) = type;
	WBUFW(buf,3) = 0; //! TODO: Additional item
	clif_send(buf, sizeof(buf), &sd->bl, SELF);
#endif
}

/**
 * Claim roulette reward
 * @param sd Player
 * @return 0:Success
 **/
static uint8 clif_roulette_getitem(struct map_session_data *sd) {
	struct item it;
	uint8 res = 1;

	nullpo_retr(1, sd);

	if (sd->roulette.prizeIdx < 0)
		return RECV_ITEM_FAILED;

	memset(&it, 0, sizeof(it));

	it.nameid = rd.nameid[sd->roulette.prizeStage][sd->roulette.prizeIdx];
	it.identify = 1;

	if ((res = pc_additem(sd, &it, rd.qty[sd->roulette.prizeStage][sd->roulette.prizeIdx], LOG_TYPE_OTHER)) == 0) {
		; // onSuccess
	}

	sd->roulette.claimPrize = false;
	sd->roulette.prizeStage = 0;
	sd->roulette.prizeIdx = -1;
	sd->roulette.stage = 0;
	return res;
}

/**
 * Process the stage and attempt to give a prize
 * @param fd
 * @param sd
 */
void clif_parse_RouletteGenerate(int fd, struct map_session_data* sd)
{
	enum GENERATE_ROULETTE_ACK result = GENERATE_ROULETTE_SUCCESS;
	short stage = sd->roulette.stage;

	nullpo_retv(sd);

	if (!battle_config.feature_roulette) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,724)); //Roulette is disabled
		return;
	}

	if (sd->roulette.stage >= MAX_ROULETTE_LEVEL)
		stage = sd->roulette.stage = 0;

	if (!stage) {
		if (sd->roulette_point.bronze <= 0 && sd->roulette_point.silver < 10 && sd->roulette_point.gold < 10)
			result = GENERATE_ROULETTE_NO_ENOUGH_POINT;
	}

	if (result == GENERATE_ROULETTE_SUCCESS) {
		if (!stage) {
			if (sd->roulette_point.bronze > 0) {
				sd->roulette_point.bronze -= 1;
				pc_setreg2(sd, ROULETTE_BRONZE_VAR, sd->roulette_point.bronze);
			} else if (sd->roulette_point.silver > 9) {
				sd->roulette_point.silver -= 10;
				stage = sd->roulette.stage = 2;
				pc_setreg2(sd, ROULETTE_SILVER_VAR, sd->roulette_point.silver);
			} else if (sd->roulette_point.gold > 9) {
				sd->roulette_point.gold -= 10;
				stage = sd->roulette.stage = 4;
				pc_setreg2(sd, ROULETTE_GOLD_VAR, sd->roulette_point.gold);
			}
		}

		sd->roulette.prizeStage = stage;
		sd->roulette.prizeIdx = rnd()%rd.items[stage];

		if (rd.flag[stage][sd->roulette.prizeIdx]&1) {
			clif_roulette_generate_ack(sd,GENERATE_ROULETTE_LOSING,stage,sd->roulette.prizeIdx,0);
			clif_roulette_getitem(sd);
			clif_roulette_recvitem_ack(sd, RECV_ITEM_SUCCESS);
			return;
		}
		else {
			sd->roulette.claimPrize = true;
			sd->roulette.stage++;
		}
	}

	clif_roulette_generate_ack(sd,result,stage,(sd->roulette.prizeIdx == -1 ? 0 : sd->roulette.prizeIdx),0); 
}

/**
 * Request to cash in prize
 * @param fd
 * @param sd
 */
void clif_parse_RouletteRecvItem(int fd, struct map_session_data* sd)
{
	enum RECV_ROULETTE_ITEM_REQ type = RECV_ITEM_FAILED;
	nullpo_retv(sd);

	if (!battle_config.feature_roulette) {
		clif_disp_overheadcolor_self(fd,COLOR_RED,msg_txt(sd,724)); //Roulette is disabled
		return;
	}

	if (sd->roulette.claimPrize && sd->roulette.prizeIdx != -1) {
		switch (clif_roulette_getitem(sd)) {
			case 0:
				type = RECV_ITEM_SUCCESS;
				break;
			case 1:
			case 4:
			case 5:
				type = RECV_ITEM_OVERCOUNT;
				break;
			case 2:
				type = RECV_ITEM_OVERWEIGHT;
				break;
			case 7:
			default:
				type = RECV_ITEM_FAILED;
				break;
		}
	}

	clif_roulette_recvitem_ack(sd,type);
	return;
}

/**
 * Update Roulette window with current stats
 * @param sd
 * @param result
 * @param stage
 * @param prizeIdx
 * @param bonusItemID
 */
void clif_roulette_generate_ack(struct map_session_data *sd, unsigned char result, short stage, short prizeIdx, t_itemid bonusItemID)
{
	nullpo_retv(sd);

	struct packet_roulette_generate_ack p;

	p.PacketType = 0xa20;
	p.Result = result;
	p.Step = stage;
	p.Idx = prizeIdx;
	p.AdditionItemID = bonusItemID;
	p.RemainGold = sd->roulette_point.gold;
	p.RemainSilver = sd->roulette_point.silver;
	p.RemainBronze = sd->roulette_point.bronze;

	clif_send(&p, sizeof(p), &sd->bl, SELF);
}

/// Move Item from or to Personal Tab (CZ_WHATSOEVER) [FE]
/// 0907 <index>.W
///
/// R 0908 <index>.w <type>.b
/// type:
/// 	0 = move item to personal tab
/// 	1 = move item to normal tab
void clif_parse_MoveItem(int fd, struct map_session_data *sd) {
#if PACKETVER >= 20111122
	int index;
		
	if(pc_isdead(sd)) {
		return;
	}
	index = RFIFOW(fd,2)-2; 
	if (index < 0 || index >= MAX_INVENTORY)
		return;
	if ( sd->inventory.u.items_inventory[index].favorite && RFIFOB(fd, 4) == 1 )
		sd->inventory.u.items_inventory[index].favorite = 0;
	else if( RFIFOB(fd, 4) == 0 )
		sd->inventory.u.items_inventory[index].favorite = 1;
	else
		return;/* nothing to do. */

	clif_favorite_item(sd, index);
#endif
}


/// Items that are in favorite tab of inventory (ZC_ITEM_FAVORITE).
/// 0900 <index>.W <favorite>.B
void clif_favorite_item(struct map_session_data* sd, unsigned short index) {
	int fd = sd->fd;
	
	WFIFOHEAD(fd,packet_len(0x908));
	WFIFOW(fd,0) = 0x908;
	WFIFOW(fd,2) = index+2;
	WFIFOB(fd,4) = (sd->inventory.u.items_inventory[index].favorite == 1) ? 0 : 1;
	WFIFOSET(fd,packet_len(0x908));
}

/**
 * 07fd <size>.W <type>.B <itemid>.W <charname_len>.B <charname>.24B <source_len>.B <containerid>.W (ZC_BROADCASTING_SPECIAL_ITEM_OBTAIN)
 * 07fd <size>.W <type>.B <itemid>.W <charname_len>.B <charname>.24B <source_len>.B <srcname>.24B (ZC_BROADCASTING_SPECIAL_ITEM_OBTAIN)
 * type: ITEMOBTAIN_TYPE_BOXITEM & ITEMOBTAIN_TYPE_MONSTER_ITEM "[playername] ... [surcename] ... [itemname]" -> MsgStringTable[1629]
 * type: ITEMOBTAIN_TYPE_NPC "[playername] ... [itemname]" -> MsgStringTable[1870]
 **/
void clif_broadcast_obtain_special_item(const char *char_name, t_itemid nameid, t_itemid container, enum BROADCASTING_SPECIAL_ITEM_OBTAIN type) {
	switch (type) {
	case ITEMOBTAIN_TYPE_BOXITEM:
	{
		struct packet_package_item_announce p;

		p.PacketType = 0x7fd;
		p.type = type;
		p.ItemID = client_nameid(nameid);
		p.len = NAME_LENGTH;
		safestrncpy(p.Name, char_name, sizeof(p.Name));
		p.unknown = 0x2; // some strange byte, IDA shows.. BYTE3(BoxItemIDLength) = 2;
		p.BoxItemID = client_nameid(container);

#if PACKETVER >= 20181121
		p.PacketLength = 15 + p.len;
#else
		p.PacketLength = 11 + p.len;
#endif

		clif_send(&p, p.PacketLength, NULL, ALL_CLIENT);
	}
	break;

	case ITEMOBTAIN_TYPE_MONSTER_ITEM:
	case ITEMOBTAIN_TYPE_NPC:
	{
		struct packet_item_drop_announce p;

		p.PacketType = 0x7fd;
		p.type = type;
		p.ItemID = client_nameid(nameid);
		p.len = NAME_LENGTH;
		safestrncpy(p.Name, char_name, sizeof(p.Name));

		if (type == ITEMOBTAIN_TYPE_MONSTER_ITEM) {
			struct mob_db *db = mob_db(container);

			p.monsterNameLen = NAME_LENGTH;
			safestrncpy(p.monsterName, db->name, NAME_LENGTH);
		}
		else {
			p.monsterNameLen = 0;
			p.monsterName[0] = '\0';
		}

#if PACKETVER >= 20181121
		p.PacketLength = 12 + p.len + p.monsterNameLen;
#else
		p.PacketLength = 8 + p.len + p.monsterNameLen;
#endif

		clif_send(&p, p.PacketLength, NULL, ALL_CLIENT);
	}
	break;
	}
}

/// Show body view windows (ZC_DRESSROOM_OPEN).
/// 0A02 <view>.W
/// Value <view> has the following effects:
/// 0: Close an open Dress Room window.
/// 1: Open a Dress Room window.
void clif_dressing_room(struct map_session_data *sd, int view) {
#if PACKETVER >= 20150513
	int fd = sd->fd;

	nullpo_retv(sd);

	fd = sd->fd;
	WFIFOHEAD(fd, packet_len(0xa02));
	WFIFOW(fd,0) = 0xa02;
	WFIFOW(fd,2) = view;
	WFIFOSET(fd, packet_len(0xa02));
#endif
}

/// Parsing a request from the client item identify oneclick (CZ_REQ_ONECLICK_ITEMIDENTIFY).
/// 0A35 <result>.W
void clif_parse_Oneclick_Itemidentify(int fd, struct map_session_data *sd) {
#if PACKETVER >= 20150513
	short idx = RFIFOW(fd,packet_db[sd->packet_ver][RFIFOW(fd,0)].pos[0]) - 2, magnifier_idx;

	// Ignore the request
	// - Invalid item index
	// - Invalid item ID or item doesn't exist
	// - Item is already identified
	if (idx < 0 || idx >= MAX_INVENTORY ||
		sd->inventory.u.items_inventory[idx].nameid == 0 || sd->inventory_data[idx] == NULL ||
		sd->inventory.u.items_inventory[idx].identify)
			return;

	// Ignore the request - No magnifiers in inventory
	if ((magnifier_idx = pc_search_inventory(sd, ITEMID_MAGNIFIER)) == -1 &&
		(magnifier_idx = pc_search_inventory(sd, ITEMID_NOVICE_MAGNIFIER)) == -1)
		return;
	
	// deleting magnifier failed, for whatever reason...
	if ( pc_delitem(sd, magnifier_idx, 1, 0, 0, LOG_TYPE_CONSUME) != 0 ) {
		return;
    }

	skill_identify(sd, idx);
#endif
}

/// Starts navigation to the given target on client side
void clif_navigateTo(struct map_session_data *sd, const char* map, uint16 x, uint16 y, uint8 flag, bool hideWindow, uint16 mob_id){
#if PACKETVER >= 20111010
	int fd = sd->fd;

	WFIFOHEAD(fd, 27);
	WFIFOW(fd, 0) = 0x08e2;

	// How detailed will our navigation be?
	if (mob_id > 0){
		x = 0;
		y = 0;
		WFIFOB(fd, 2) = 3; // monster with destination field
	}
	else if (x > 0 && y > 0){
		WFIFOB(fd, 2) = 0; // with coordinates
	}
	else{
		x = 0;
		y = 0;
		WFIFOB(fd, 2) = 1; // without coordinates(will fail if you are already on the map)
	}

	// Which services can be used for transportation?
	WFIFOB(fd, 3) = flag;
	// If this flag is set, the navigation window will not be opened up
	WFIFOB(fd, 4) = hideWindow;
	// Target map
	safestrncpy((char*)WFIFOP(fd, 5), map, MAP_NAME_LENGTH_EXT);
	// Target x
	WFIFOW(fd, 21) = x;
	// Target y
	WFIFOW(fd, 23) = y;
	// Target monster ID
	WFIFOW(fd, 25) = mob_id;
	WFIFOSET(fd, 27);
#endif
}

/// Send hat effects to the client (ZC_HAT_EFFECT).
/// 0A3B <Length>.W <AID>.L <Status>.B { <HatEffectId>.W }
void clif_hat_effects(struct map_session_data* sd, struct block_list* bl, enum send_target target){
#if PACKETVER >= 20150513
	unsigned char* buf;
	int len, i;
	struct map_session_data *tsd;
	struct block_list* tbl;

	if (target == SELF){
		tsd = BL_CAST(BL_PC, bl);
		tbl = &sd->bl;
	}
	else{
		tsd = sd;
		tbl = bl;
	}

	if (!tsd->hatEffectCount)
		return;

	len = 9 + tsd->hatEffectCount * 2;

	buf = (unsigned char*)aMalloc(len);

	WBUFW(buf, 0) = 0xa3b;
	WBUFW(buf, 2) = len;
	WBUFL(buf, 4) = tsd->bl.id;
	WBUFB(buf, 8) = 1;

	for (i = 0; i < tsd->hatEffectCount; i++){
		WBUFW(buf, 9 + i * 2) = tsd->hatEffectIDs[i];
	}

	clif_send(buf, len, tbl, target);

	aFree(buf);
#endif
}

void clif_hat_effect_single(struct map_session_data* sd, uint16 effectId, bool enable){
#if PACKETVER >= 20150513
	unsigned char buf[13];

	WBUFW(buf, 0) = 0xa3b;
	WBUFW(buf, 2) = 13;
	WBUFL(buf, 4) = sd->bl.id;
	WBUFB(buf, 8) = enable;
	WBUFL(buf, 9) = effectId;

	clif_send(buf, 13, &sd->bl, AREA);
#endif
}

/// Send the full list of items in the equip switch window
/// 0a9b <length>.W { <index>.W <position>.L }*
void clif_equipswitch_list(struct map_session_data* sd) {
#if PACKETVER >= 20170208
	int fd = sd->fd;
	int offset, i, position;

	WFIFOW(fd, 0) = 0xa9b;
	for (i = 0, offset = 4, position = 0; i < EQI_MAX; i++) {
		short index = sd->equip_switch_index[i];

		if (index >= 0 && !(position & equip_bitmask[i])) {
			WFIFOW(fd, offset) = index + 2;
			WFIFOL(fd, offset + 2) = sd->inventory.u.items_inventory[index].equipSwitch;
			position |= sd->inventory.u.items_inventory[index].equipSwitch;
			offset += 6;
		}
	}
	WFIFOW(fd, 2) = offset;
	WFIFOSET(fd, offset);
#endif
}

/// Acknowledgement for removing an equip to the equip switch window
/// 0a9a <index>.W <position.>.L <failure>.W
void clif_equipswitch_remove(struct map_session_data* sd, uint16 index, uint32 pos, bool failed) {
#if PACKETVER >= 20170208
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xa9a));
	WFIFOW(fd, 0) = 0xa9a;
	WFIFOW(fd, 2) = index + 2;
	WFIFOL(fd, 4) = pos;
	WFIFOW(fd, 8) = failed;
	WFIFOSET(fd, packet_len(0xa9a));
#endif
}

/// Request to remove an equip from the equip switch window
/// 0a99 <index>.W <position>.L <= 20170502
/// 0a99 <index>.W
void clif_parse_equipswitch_remove(int fd, struct map_session_data* sd) {
#if PACKETVER >= 20170208
	uint16 index = RFIFOW(fd, 2) - 2;
	bool removed = false;

	if (!battle_config.feature_equipswitch) {
		return;
	}

	// Check if the index is valid
	if (index >= MAX_INVENTORY) {
		return;
	}

	pc_equipswitch_remove(sd, index);
#endif
}

/// Acknowledgement for adding an equip to the equip switch window
/// 0a98 <index>.W <position.>.L <failure>.L  <= 20170502
/// 0a98 <index>.W <position.>.L <failure>.W
void clif_equipswitch_add(struct map_session_data* sd, uint16 index, uint32 pos, uint8 flag) {
#if PACKETVER >= 20170208
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xa98));
	WFIFOW(fd, 0) = 0xa98;
	WFIFOW(fd, 2) = index + 2;
	WFIFOL(fd, 4) = pos;
#if PACKETVER <= 20170502
	WFIFOL(fd, 8) = flag;
#else
	WFIFOW(fd, 8) = flag;
#endif
	WFIFOSET(fd, packet_len(0xa98));
#endif
}

/// Request to add an equip to the equip switch window
/// 0a97 <index>.W <position>.L
void clif_parse_equipswitch_add(int fd, struct map_session_data* sd) {
#if PACKETVER >= 20170208
	uint16 index = RFIFOW(fd, 2) - 2;
	uint32 position = RFIFOL(fd, 4);

	if (!battle_config.feature_equipswitch) {
		return;
	}

	if (index >= MAX_INVENTORY || sd->inventory_data[index] == NULL) {
		return;
	}

	if (sd->state.trading || sd->npc_shopid) {
		clif_equipswitch_add(sd, index, position, true);
		return;
	}

	if (sd->inventory_data[index]->type == IT_AMMO) {
		position = EQP_AMMO;
	}

	pc_equipitem(sd, index, position, true);
#endif
}

/// Acknowledgement packet for the full equip switch
/// 0a9d <failed>.W
void clif_equipswitch_reply(struct map_session_data* sd, bool failed) {
#if PACKETVER >= 20170208
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xa9d));
	WFIFOW(fd, 0) = 0xa9d;
	WFIFOW(fd, 2) = failed;
	WFIFOSET(fd, packet_len(0xa9d));
#endif
}

/// Request to do a full equip switch
/// 0a9c
void clif_parse_equipswitch_request(int fd, struct map_session_data* sd) {
#if PACKETVER >= 20170208
	int i;
	int64 tick = gettick();
	uint16 skill_id = ALL_EQSWITCH, skill_lv = 1;

	if (DIFF_TICK(tick, sd->equipswitch_tick) < 0) {
		// Client will not let you send a request
		return;
	}

	sd->equipswitch_tick = tick + skill_get_cooldown(skill_id, skill_lv);

	if (!battle_config.feature_equipswitch) {
		return;
	}

	ARR_FIND(0, EQI_MAX, i, sd->equip_switch_index[i] >= 0);

	if (i == EQI_MAX) {
		// client will show: "There is no item to replace." and should not even come here
		clif_equipswitch_reply(sd, false);
		return;
	}

	if (pc_issit(sd) && !pc_setstand(sd, false)) {
		return;
	}

	clif_parse_skill_toid(sd, skill_id, skill_lv, sd->bl.id);
#endif
}

/// Request to do a single equip switch
/// 0ace <index>.W
void clif_parse_equipswitch_request_single(int fd, struct map_session_data* sd) {
#if PACKETVER >= 20170502
	uint16 index = RFIFOW(fd, 2) - 2;

	if (!battle_config.feature_equipswitch) {
		return;
	}

	// Check if the index is valid
	if (index >= MAX_INVENTORY) {
		return;
	}

	// Check if the item exists
	if (sd->inventory_data[index] == NULL)
		return;

	// Check if the item was already added to equip switch
	if (sd->inventory.u.items_inventory[index].equipSwitch) {
		if (sd->npc_id) {
			if (pc_hasprogress(sd, WIP_DISABLE_SKILLITEM)) {
				clif_msg(sd, WORK_IN_PROGRESS);
				return;
			}
			if (!sd->npc_item_flag) {
				return;
			}
		}

		pc_equipswitch(sd, index);
		return;
	}

	pc_equipitem(sd, index, pc_equippoint(sd, index), true);
#endif
}

void clif_crimson_marker_xy(struct map_session_data *sd)
{
	unsigned char buf[16];

	nullpo_retv(sd);

	WBUFW(buf, 0) = 0x9c1;
	WBUFL(buf, 2) = sd->status.account_id;
	WBUFW(buf, 6) = sd->bl.x;
	WBUFW(buf, 8) = sd->bl.y;
	clif_send(buf, packet_len(0x9c1), &sd->bl, PARTY_SAMEMAP_WOS);
}

void clif_crimson_marker_xy_single(int fd, struct map_session_data *sd)
{
	WFIFOHEAD(fd, packet_len(0x9c1));
	WFIFOW(fd, 0) = 0x9c1;
	WFIFOL(fd, 2) = sd->status.account_id;
	WFIFOW(fd, 6) = sd->bl.x;
	WFIFOW(fd, 8) = sd->bl.y;
	WFIFOSET(fd, packet_len(0x9c1));
}

/// Crimson Marker mini map tracking (ZC_C_MARKERINFO).
/// 09c1 <AID>.L <xPos>.W <yPos>.W
void clif_crimson_marker_xy_all(struct map_session_data *sd, struct block_list *bl, short mark)
{
	unsigned char buf[10];

	nullpo_retv(sd);

	WBUFW(buf,0)=0x9c1;// ZC_C_MARKERINFO
	WBUFL(buf,2)=sd->status.account_id;// AID
	WBUFW(buf,6)=sd->bl.x;// xPos
	WBUFW(buf,8)=sd->bl.y;// yPos
	clif_send(buf,packet_len(0x9c1),&sd->bl,SELF);
}

void clif_crimson_marker_xy_remove(struct map_session_data *sd)
{
	unsigned char buf[16];
	nullpo_retv(sd);
	WBUFW(buf, 0) = 0x9c1;
	WBUFL(buf, 2) = sd->status.account_id;
	WBUFW(buf, 6) = -1;
	WBUFW(buf, 8) = -1;
	clif_send(buf, packet_len(0x9c1), &sd->bl, PARTY_SAMEMAP_WOS);
}

void clif_parse_changedress(int fd,struct map_session_data *sd)
{
	// Removes body costumes.
	// Wedding / Santa / Summer / Hanbok / Oktoberfest / Summer 2
	status_change_end(&sd->bl, SC_WEDDING, INVALID_TIMER);
	status_change_end(&sd->bl, SC_XMAS, INVALID_TIMER);
	status_change_end(&sd->bl, SC_SUMMER, INVALID_TIMER);
	status_change_end(&sd->bl, SC_HANBOK, INVALID_TIMER);
	status_change_end(&sd->bl, SC_OKTOBERFEST, INVALID_TIMER);
	status_change_end(&sd->bl, SC_SUMMER2, INVALID_TIMER);
}

/// Parses a request for a private airship
/// 0A49 <mapname>.16B <itemid>.W
void clif_parse_private_airship_request(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20180321
	// Check if the player is allowed to warp from the source map
	if (!map[sd->bl.m].flag.pairship_startable) {
		clif_private_airship_response(sd, P_AIRSHIP_INVALID_START_MAP);
		return;
	}

	const struct PACKET_CZ_PRIVATE_AIRSHIP_REQUEST *p = (struct PACKET_CZ_PRIVATE_AIRSHIP_REQUEST *)RFIFOP(fd, 0);

	char mapname[MAP_NAME_LENGTH_EXT];

	safestrncpy(mapname, p->mapName, MAP_NAME_LENGTH_EXT);

	int16 mapindex = mapindex_name2id(mapname);

	// Check if we know the mapname
	if (mapindex < 0) {
		clif_private_airship_response(sd, P_AIRSHIP_INVALID_END_MAP);
		return;
	}

	int16 mapid = map_mapindex2mapid(mapindex);

	// Check if the map is available on this server
	if (mapid < 0) {
		// TODO: add multi map-server support, cant validate the mapflags of the other server
		return;
	}

	// Check if the player tried to warp to the map he is on
	if (sd->bl.m == mapid) {
		// This is blocked by the client, but just to be sure
		return;
	}

	// This can only be a hack, so we prevent it
	if (map[sd->bl.m].instance_id) {
		// Ignore requests to warp directly into a running instance
		return;
	}

	// Check if the player is allowed to warp to the target map
	if (!map[mapid].flag.pairship_endable) {
		clif_private_airship_response(sd, P_AIRSHIP_INVALID_END_MAP);
		return;
	}

	// Check if the item sent by the client is known to us
	if (!itemdb_exists(p->ItemID)) {
		clif_private_airship_response(sd, P_AIRSHIP_ITEM_INVALID);
		return;
	}

	int idx = pc_search_inventory(sd, p->ItemID);

	// Check if the player has the item at all
	if (idx < 0) {
		clif_private_airship_response(sd, P_AIRSHIP_ITEM_NOT_ENOUGH);
		return;
	}

	// Delete the chosen item
	if (pc_delitem(sd, idx, 1, 0, 0, LOG_TYPE_OTHER)) {
		clif_private_airship_response(sd, P_AIRSHIP_RETRY);
		return;
	}

	// Warp the player to a random spot on the destination map
	pc_setpos(sd, mapindex, 0, 0, CLR_TELEPORT);
#else
	ShowWarning("clif_parse_private_airship_request: private airship is not supported in this client version, possible packet manipulation.\n");
#endif
}

/// Send out the response to a private airship request
/// 0A4A <response>.L
void clif_private_airship_response(struct map_session_data *sd, uint32 flag)
{
#if PACKETVER >= 20180321
	nullpo_retv(sd);
	int fd = sd->fd;

	if (flag > P_AIRSHIP_ITEM_INVALID) {
		ShowError("clif_private_airship_response: invalid flag given '%u', defaulting to 0.\n", flag);
		flag = 0;
	}

	WFIFOHEAD(fd, packet_len(0xA4A));
	WFIFOW(fd, 0) = 0xA4A;
	WFIFOL(fd, 2) = flag;
	WFIFOSET(fd, packet_len(0xA4A));
#else
	ShowWarning("clif_private_airship_response: private airship works only for clients >= 20180321.");
#endif
}

bool clif_attendance_timediff(struct map_session_data *sd)
{
	int64 timediff;

	nullpo_retr(false, sd);

	timediff = (time(NULL) / (60 * 60 * 24)) - (pc_readreg2(sd, ATTENDANCE_TIMER_VAR) / (60 * 60 * 24));

	if (timediff <= 0)
		return false;
	return true;
}

time_t clif_attendance_gettime(int time)
{
	time_t timestamp;
	struct tm tmtime = { 0 };
	int year = 0, month = 0, day = 0;
	char timestring[9];

	sprintf(timestring, "%8d", time);
	sscanf(timestring, "%4d%2d%2d", &year, &month, &day);

	tmtime.tm_year = year - 1900;
	tmtime.tm_mon = month - 1;
	tmtime.tm_mday = day;

	timestamp = mktime(&tmtime);

	return timestamp;
}
time_t clif_attendance_getstarttime(void) { return(clif_attendance_gettime(battle_config.attendance_starttime)); }
time_t clif_attendance_getendtime(void) { return(clif_attendance_gettime(battle_config.attendance_endtime)); }

/// Opens an UI window of the given type and initializes it with the given data
/// 0AE2 <type>.B <data>.L
void clif_ui_open(struct map_session_data *sd, enum out_ui_type ui_type, int32 data) {
	nullpo_retv(sd);

	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xae2));
	WFIFOW(fd, 0) = 0xae2;
	WFIFOB(fd, 2) = ui_type;
	WFIFOL(fd, 3) = data;
	WFIFOSET(fd, packet_len(0xae2));
}

void clif_open_ui_req_sub(int fd, struct map_session_data* sd, enum out_ui_type ui_type) {
#if PACKETVER >= 20180307
	int claimed = 0;

	switch (ui_type) {
	case IN_UI_ATTENDANCE:
		if (battle_config.feature_attendance_system == 1) {
			if (clif_attendance_timediff(sd) != true)
				++claimed;
			else if (pc_readreg2(sd, ATTENDANCE_COUNT_VAR) >= VECTOR_LENGTH(attendance_data))
				pc_setreg2(sd, ATTENDANCE_COUNT_VAR, 0);

			clif_ui_open(sd, OUT_UI_ATTENDANCE, (int)pc_readreg2(sd, ATTENDANCE_COUNT_VAR) * 10 + claimed);
		}
		else {
			clif_msg_color(sd, MSG_ATTENDANCE_UNAVAILABLE, COLOR_RED);
		}
		break;
	}
#else
	ShowWarning("Attendance System available only for PACKETVER >= 20180307.\n");
#endif
}

/// Request to open an UI window of the given type
/// 0A68 <type>.B
void clif_parse_open_ui_request(int fd, struct map_session_data *sd) {
#if PACKETVER >= 20180307
	clif_open_ui_req_sub(fd, sd, RFIFOB(fd, 2));
#endif
}

/// Response for attedance request
/// 0AF0 <type>.L <data>.L
void clif_ui_action(struct map_session_data *sd, int32 type, int32 data) {
	nullpo_retv(sd);

	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xAF0));
	WFIFOW(fd, 0) = 0xAF0;
	WFIFOL(fd, 2) = type;
	WFIFOL(fd, 6) = data;
	WFIFOSET(fd, packet_len(0xAF0));
}

/// Request from the client to retrieve today's attendance reward
/// 0AEF
void clif_parse_attendance_reward_request(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20180307
	struct mail_message msg = { 0 };
	struct attendance_entry *entry;
	char title[MAIL_TITLE_LENGTH], body[MAIL_BODY_LENGTH];

	int attendance_count = (int)pc_readreg2(sd, ATTENDANCE_COUNT_VAR);

	if (clif_attendance_getstarttime() > time(NULL) || clif_attendance_getendtime() < time(NULL)) {
		clif_msg_color(sd, MSG_ATTENDANCE_UNAVAILABLE, COLOR_RED);
		return;
	}

	if (battle_config.feature_attendance_system != 1)
		return;

	if (clif_attendance_timediff(sd) != true)
		return;

	if (attendance_count >= VECTOR_LENGTH(attendance_data))
		attendance_count = 0;

	++attendance_count;
	pc_setreg2(sd, ATTENDANCE_TIMER_VAR, time(NULL));

	msg.dest_id = sd->status.char_id;
	sprintf(title, msg_txt(sd,544), attendance_count + 1);
	sprintf(body, msg_txt(sd,545), attendance_count + 1);

	entry = &VECTOR_INDEX(attendance_data, attendance_count);
	msg.item[0].nameid = entry->nameid;
	msg.item[0].amount = entry->qty;
	msg.item[0].identify = 1;
	msg.type = MAIL_INBOX_NORMAL;

	safestrncpy(msg.send_name, msg_txt(sd,544), NAME_LENGTH);
	safestrncpy(msg.title, title, MAIL_TITLE_LENGTH);
	safestrncpy(msg.body, body, MAIL_BODY_LENGTH);
	msg.timestamp = time(NULL);

	pc_setreg2(sd, ATTENDANCE_COUNT_VAR, attendance_count);

	intif_Mail_send(0, &msg);
	clif_ui_action(sd, 0, attendance_count);
#else
	ShowWarning("Attendance System available only for PACKETVER >= 20180307.\n");
#endif
}

/// Ack world info (ZC_ACK_BEFORE_WORLD_INFO)
/// 0979 <world name>.24B <char name>.24B
void clif_ackworldinfo(struct map_session_data* sd) {
	int fd;

	nullpo_retv(sd);
	fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0x979));
	WFIFOW(fd, 0) = 0x979;
	//AID -> world name ?
	safestrncpy((char*)WFIFOP(fd, 2), "" /* World name */, 24);
	safestrncpy((char*)WFIFOP(fd, 26), sd->status.name, NAME_LENGTH);
	WFIFOSET(fd, packet_len(0x979));
}

/// req world info (CZ_REQ_BEFORE_WORLD_INFO)
/// 0978 <AID>.L
void clif_parse_reqworldinfo(int fd, struct map_session_data *sd) {
	//uint32 aid = RFIFOL(fd,2); //should we trust client ?
	if (sd) clif_ackworldinfo(sd);
}

/// (CZ_BLOCKING_PLAY_CANCEL)
/// 0447
void clif_parse_blocking_playcancel(int fd, struct map_session_data *sd) {
	return;
}

/// req world info (CZ_CLIENT_VERSION)
/// 044A <version>.L
void clif_parse_client_version(int fd, struct map_session_data *sd) {
	return;
}

/// MERGE ITEM

/**
 * Acknowledge the client about item merger result
 * ZC 096F <index>.W <total>.W <result>.B (ZC_ACK_MERGE_ITEM)
 * @param fd
 * @param sd
 **/
void clif_merge_item_ack(struct map_session_data *sd, unsigned short index, unsigned short count, enum MERGE_ITEM_ACK type) {
	unsigned char buf[9];
	struct s_packet_db *info = NULL;
	short cmd = 0;

	nullpo_retv(sd);

	if (!clif_session_isValid(sd))
		return;
	if (!(cmd = packet_db_ack[sd->packet_ver][ZC_ACK_MERGE_ITEM]))
		return;
	if (!(info = &packet_db[sd->packet_ver][cmd]) || info->len == 0)
		return;

	WBUFW(buf, 0) = cmd;
	WBUFW(buf, info->pos[0]) = index;
	WBUFW(buf, info->pos[1]) = count;
	WBUFB(buf, info->pos[2]) = type;
	clif_send(buf, info->len, &sd->bl, SELF);
}

/**
 * Check if item has the pair to be merged
 * @param sd
 * @param it Item
 * @return True if has pair, False if not
 **/
static bool clif_merge_item_has_pair(struct map_session_data *sd, struct item *it) {
	struct item *it_;
	unsigned short i;

	nullpo_retr(false, sd);

	ARR_FIND(0, MAX_INVENTORY, i, (it_ = &sd->inventory.u.items_inventory[i]) && it->nameid == it_->nameid && it->bound == it_->bound && memcmp(it_, it, sizeof(struct item)) != 0);
	if (i < MAX_INVENTORY)
		return true;

	return false;
}

/**
 * Check if item can be merged
 * @param id Item Data
 * @param it Item
 * @return True if can be merged, False if not
 **/
static bool clif_merge_item_check(struct item_data *id, struct item *it) {
	if (!id || !it)
		return false;
	if (id->type == IT_CASH)
		return false;
	if (!itemdb_isstackable2(id))
		return false;
	if (itemdb_isspecial(it->card[0]))
		return false;
	if (!it->unique_id)
		return false;
	return true;
}

/**
 * Open available item to be merged.
 * Only show 1 item in 1 process.
 * ZC 096D <size>.W { <index>.W }* (ZC_MERGE_ITEM_OPEN)
 * @param sd
 **/
void clif_merge_item_open(struct map_session_data *sd) {
	unsigned char buf[4 + MAX_INVENTORY * 2] = { 0 };
	unsigned short cmd = 0, n = 0, i = 0, indexes[MAX_INVENTORY] = { 0 };
	int len = 0;
	struct s_packet_db *info = NULL;
	struct item *it;

	nullpo_retv(sd);
	if (!clif_session_isValid(sd))
		return;
	if (!(cmd = packet_db_ack[sd->packet_ver][ZC_MERGE_ITEM_OPEN]))
		return;
	if (!(info = &packet_db[sd->packet_ver][cmd]) || info->len == 0)
		return;

	// Get entries
	for (i = 0; i < MAX_INVENTORY; i++) {
		if (!clif_merge_item_check(sd->inventory_data[i], (it = &sd->inventory.u.items_inventory[i])))
			continue;
		if (clif_merge_item_has_pair(sd, it))
			indexes[n++] = i;
	}

	if (n < 2) { // No item need to be merged
		clif_msg(sd, MERGE_ITEM_NOT_AVAILABLE);
		return;
	}

	WBUFW(buf, 0) = cmd;
	WBUFW(buf, info->pos[0]) = (len = 4 + n * 2);
	for (i = 0; i < n; i++) {
		WBUFW(buf, info->pos[1] + i * 2) = indexes[i] + 2;
	}

	clif_send(buf, len, &sd->bl, SELF);
}

/**
 * Process item merger
 * CZ 096E <size>.W { <index>.W }* (CZ_REQ_MERGE_ITEM)
 * @param fd
 * @param sd
 **/
void clif_parse_merge_item_req(int fd, struct map_session_data* sd) {
	struct s_packet_db *info = NULL;
	unsigned short n = 0, indexes[MAX_INVENTORY] = { 0 }, i, j;
	unsigned int count = 0;
	struct item_data *id = NULL;

	nullpo_retv(sd);
	if (!clif_session_isValid(sd))
		return;
	if (!(info = &packet_db[sd->packet_ver][RFIFOW(fd, 0)]) || info->len == 0)
		return;

	n = (RFIFOW(fd, info->pos[0]) - 4) / 2;

	if (n < 2) { // No item need to be merged
		clif_msg(sd, MERGE_ITEM_NOT_AVAILABLE);
		return;
	}

	for (i = 0, j = 0; i < n; i++) {
		unsigned short idx = RFIFOW(fd, info->pos[1] + i * 2) - 2;

		if (idx < 0 || idx >= MAX_INVENTORY) {
			return;
		}

		if (!clif_merge_item_check((id = sd->inventory_data[idx]), &sd->inventory.u.items_inventory[idx]))
			continue;
		indexes[j] = idx;
		if (j && id->nameid != sd->inventory_data[indexes[0]]->nameid) { // Only can merge 1 kind at once
			clif_merge_item_ack(sd, 0, 0, MERGE_ITEM_FAILED_NOT_MERGE);
			return;
		}
		count += sd->inventory.u.items_inventory[idx].amount;
		j++;
	}

	if (n != j || !(id = sd->inventory_data[indexes[0]])) {
		clif_msg(sd, MERGE_ITEM_NOT_AVAILABLE);
		return;
	}

	if (count >= MAX_AMOUNT) {
		clif_merge_item_ack(sd, 0, 0, MERGE_ITEM_FAILED_MAX_COUNT);
		return;
	}

	// Merrrrge!!!!
	for (i = 1; i < n; i++) {
		unsigned short idx = indexes[i], amt = sd->inventory.u.items_inventory[idx].amount;
		log_pick(&sd->bl, LOG_TYPE_MERGE_ITEM, sd->inventory.u.items_inventory[idx].nameid, -amt, &sd->inventory.u.items_inventory[idx]);
		memset(&sd->inventory.u.items_inventory[idx], 0, sizeof(sd->inventory.u.items_inventory[0]));
		sd->inventory_data[idx] = NULL;
		clif_delitem(sd, idx, amt, 0);
	}
	sd->inventory.u.items_inventory[indexes[0]].amount = count;

	clif_merge_item_ack(sd, indexes[0] + 2, count, MERGE_ITEM_SUCCESS);
}

/**
 * Cancel item merge
 * CZ 0974 (CZ_CANCEL_MERGE_ITEM)
 * @param fd
 * @param sd
 **/
void clif_parse_merge_item_cancel(int fd, struct map_session_data* sd) {
	return; // Nothing todo yet
}

/**
* [Ind/Hercules]
* 07fc <OldMasterAID>.L <NewMasterAID>.L (ZC_CHANGE_GROUP_MASTER)
**/
void clif_party_leaderchanged(struct map_session_data *sd, int prev_leader_aid, int new_leader_aid) {
	unsigned char buf[10];

	nullpo_retv(sd);

	WBUFW(buf, 0) = 0x7fc;
	WBUFL(buf, 2) = prev_leader_aid;
	WBUFL(buf, 6) = new_leader_aid;
	clif_send(buf, packet_len(0x7fc), &sd->bl, PARTY);
}

/// Activates the client camera info or updates the client camera with the given values.
/// 0A78 <type>.B <range>.F <rotation>.F <latitude>.F
void clif_camerainfo(struct map_session_data* sd, bool show, float range, float rotation, float latitude) {
#if PACKETVER >= 20160525
	int fd = sd->fd;

	WFIFOHEAD(fd, packet_len(0xa78));
	WFIFOW(fd, 0) = 0xa78;
	WFIFOB(fd, 2) = show;
	WFIFOF(fd, 3) = range;
	WFIFOF(fd, 7) = rotation;
	WFIFOF(fd, 11) = latitude;
	WFIFOSET(fd, packet_len(0xa78));
#endif
}

/// Activates or deactives the client camera info or updates the camera settings.
/// This packet is triggered by /viewpointvalue or /setcamera
/// 0A77 <type>.B <range>.F <rotation>.F <latitude>.F
void clif_parse_camerainfo(int fd, struct map_session_data* sd) {
	char command[CHAT_SIZE_MAX];

	// /viewpointvalue
	if (RFIFOB(fd, 2) == 1) {
		safesnprintf(command, sizeof(command), "%ccamerainfo", atcommand_symbol);
		// /setcamera
	}
	else {
		safesnprintf(command, sizeof(command), "%ccamerainfo %03.03f %03.03f %03.03f", atcommand_symbol, RFIFOF(fd, 3), RFIFOF(fd, 7), RFIFOF(fd, 11));
	}

	is_atcommand(fd, sd, command, 1);
}

bool clif_lapineDdukDdak_open(struct map_session_data *sd, int item_id)
{
#if PACKETVER >= 20160601
	nullpo_retr(false, sd);
	nullpo_retr(false, itemdb_exists(item_id));
	struct PACKET_ZC_LAPINEDDUKDDAK_OPEN p;

	p.packetType = 0x0a4e;
	p.itemId = item_id;
	clif_send(&p, sizeof(p), &sd->bl, SELF);

	sd->state.lapine_ui = 1;
	return true;
#else
	return false;
#endif // PACKETVER >= 20160601
}

static bool clif_lapineDdukDdak_result(struct map_session_data *sd, enum lapineddukddak_result result)
{
#if PACKETVER >= 20160601
	nullpo_retr(false, sd);
	struct PACKET_ZC_LAPINEDDUKDDAK_RESULT p;

	p.packetType = 0x0a50;
	p.result = result;
	clif_send(&p, sizeof(p), &sd->bl, SELF);
	return true;
#else
	return false;
#endif // PACKETVER >= 20160601
}

static void clif_parse_lapineDdukDdak_ack(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20160302
	if (sd->state.lapine_ui == 0)
		return;

	const struct PACKET_CZ_LAPINEDDUKDDAK_ACK *p = (struct PACKET_CZ_LAPINEDDUKDDAK_ACK *)RFIFOP(fd, 0);
	struct item_data *it = itemdb_exists(p->itemId);

	if (it == NULL || it->lapineddukddak == NULL)
		return;
	if (pc_cant_act_except_lapine(sd))
		return;
	if (pc_search_inventory(sd, it->nameid) < 0)
		return;

	if (((p->packetLength - sizeof(struct PACKET_CZ_LAPINEDDUKDDAK_ACK)) / sizeof(struct PACKET_CZ_LAPINEDDUKDDAK_ACK_sub)) != it->lapineddukddak->NeedCount)
		return;

	for (int i = 0; i < it->lapineddukddak->NeedCount; ++i) {
		int16 idx = p->items[i].index - 2;

		struct item itr = sd->inventory.u.items_inventory[idx];
		int j = 0;
		for (j = 0; j < VECTOR_LENGTH(it->lapineddukddak->SourceItems); ++j) {
			if (itr.nameid == VECTOR_INDEX(it->lapineddukddak->SourceItems, j).id) {
				// Validate that the amount sent in the packet is matching the database
				if (p->items[i].count != VECTOR_INDEX(it->lapineddukddak->SourceItems, j).amount) {
					clif_lapineDdukDdak_result(sd, LAPINEDDKUKDDAK_INSUFFICIENT_AMOUNT);
					return;
				}

				// Validate that the player have enough of the item
				if (itr.amount < VECTOR_INDEX(it->lapineddukddak->SourceItems, j).amount) {
					clif_lapineDdukDdak_result(sd, LAPINEDDKUKDDAK_INSUFFICIENT_AMOUNT);
					return;
				}

				// Validate refine rate requirement
				if ((itemdb_type(itr.nameid) == IT_ARMOR || itemdb_type(itr.nameid) == IT_WEAPON)
					&& (itr.refine < it->lapineddukddak->NeedRefineMin || itr.refine > it->lapineddukddak->NeedRefineMax))
					return;

				// All requirements are met, move to the next one
				break;
			}
		}
		// The item is not in sources list
		if (j == VECTOR_LENGTH(it->lapineddukddak->SourceItems)) {
			clif_lapineDdukDdak_result(sd, LAPINEDDKUKDDAK_INVALID_ITEM);
			return;
		}
	}

	for (int i = 0; i < it->lapineddukddak->NeedCount; ++i)
		pc_delitem(sd, p->items[i].index - 2, p->items[i].count, 0, 0, LOG_TYPE_SCRIPT);
	if (it->lapineddukddak->script != NULL)
		script_run_item_lapineddukddak_script(sd, it, fake_nd->bl.id);
	clif_lapineDdukDdak_result(sd, LAPINEDDKUKDDAK_SUCCESS);
	return;
#endif // PACKETVER >= 20160302
}

static void clif_parse_lapineDdukDdak_close(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20160504
	sd->state.lapine_ui = 0;
#endif // PACKETVER >= 20160504
}

static bool clif_lapineUpgrade_open(struct map_session_data *sd, int item_id)
{
#if PACKETVER >= 20170726
	nullpo_retr(false, sd);
	nullpo_retr(false, itemdb_exists(item_id));
	struct PACKET_ZC_LAPINEUPGRADE_OPEN p;

	p.packetType = 0x0ab4;
	p.itemId = item_id;
	clif_send(&p, sizeof(p), &sd->bl, SELF);

	return true;
#else
	return false;
#endif  // PACKETVER >= 20170726
}

/// result: 0 = success; 1 = failed.
static bool clif_lapineUpgrade_result(struct map_session_data *sd, uint16 result)
{
#if PACKETVER >= 20170726
	nullpo_retr(false, sd);
	struct PACKET_ZC_LAPINEUPGRADE_RESULT p;

	p.packetType = 0x0ab7;
	p.result = result;
	clif_send(&p, sizeof(p), &sd->bl, SELF);

	return true;
#else
	return false;
#endif  // PACKETVER >= 20170726
}

static void clif_parse_lapineUpgrade_makeItem(int fd, struct map_session_data *sd)
{
#if PACKETVER >= 20170111
	ShowError("Lapin upgrade not implimented yet.\n");
	clif_lapineUpgrade_result(sd, 1); // send fail result for now...
#endif  // PACKETVER >= 20170111
}

#ifdef DUMP_UNKNOWN_PACKET
void DumpUnknownPacket(int fd, TBL_PC *sd, int cmd, int packet_len) {
	const char* packet_txt = "save/packet.txt";
	FILE* fp;
	time_t time_server;
	struct tm *datetime;
	char datestr[512];

	time(&time_server);  // get time in seconds since 1/1/1970
	datetime = localtime(&time_server); // convert seconds in structure
	// like sprintf, but only for date/time (Sunday, November 02 2003 15:12:52)
	strftime(datestr, sizeof(datestr) - 1, "%A, %B %d %Y %X.", datetime); // Server time (normal time): %A, %B %d %Y %X.


	if ((fp = fopen(packet_txt, "a")) != NULL) {
		if (sd) {
			fprintf(fp, "Unknown packet 0x%04X (length %d), %s session #%d, %d/%d (AID/CID) at %s \n", cmd, packet_len, sd->state.active ? "authed" : "unauthed", fd, sd->status.account_id, sd->status.char_id, datestr);
		}
		else {
			fprintf(fp, "Unknown packet 0x%04X (length %d), session #%d at %s\n", cmd, packet_len, fd, datestr);
		}
		WriteDump(fp, RFIFOP(fd, 0), packet_len);
		fprintf(fp, "\n");
		fclose(fp);
	}
	else {
		ShowError("Failed to write '%s'.\n", packet_txt);
		// Dump on console instead
		if (sd) {
			ShowDebug("Unknown packet 0x%04X (length %d), %s session #%d, %d/%d (AID/CID) at %s\n", cmd, packet_len, sd->state.active ? "authed" : "unauthed", fd, sd->status.account_id, sd->status.char_id, datestr);
		}
		else {
			ShowDebug("Unknown packet 0x%04X (length %d), session #%d at %s\n", cmd, packet_len, fd, datestr);
		}

		ShowDump(RFIFOP(fd, 0), packet_len);
	}
}
#endif

/* [Ind] placeholder for unsupported incoming packets (avoids server disconnecting client) */
void __attribute__((unused)) clif_parse_dull(int fd, struct map_session_data *sd) {
	return;
}

/// Main client packet processing function
static int clif_parse(int fd)
{
	int cmd, packet_ver, packet_len, err;
	TBL_PC* sd;
	int pnum;

	//TODO apply delays or disconnect based on packet throughput [FlavioJS]
	// Note: "click masters" can do 80+ clicks in 10 seconds

	for( pnum = 0; pnum < 3; ++pnum )// Limit max packets per cycle to 3 (delay packet spammers) [FlavioJS]  -- This actually aids packet spammers, but stuff like /str+ gets slow without it [Ai4rei]
	{ // begin main client packet processing loop

	sd = (TBL_PC *)session[fd]->session_data;
	if (session[fd]->flag.eof) {
		if (sd) {
			if (sd->state.autotrade) {
				//Disassociate character from the socket connection.
				session[fd]->session_data = NULL;
				sd->fd = 0;
				ShowInfo("%sCharacter '"CL_WHITE"%s"CL_RESET"' logged off (using @autotrade).\n", (pc_isGM(sd))?"GM ":"", sd->status.name);
			} else
			if (sd->state.active) {
				// Player logout display [Valaris]
				ShowInfo("%sCharacter '"CL_WHITE"%s"CL_RESET"' logged off.\n", (pc_isGM(sd))?"GM ":"", sd->status.name);
				clif_quitsave(fd, sd);
			} else {
				//Unusual logout (during log on/off/map-changer procedure)
				ShowInfo("Player AID:%d/CID:%d logged off.\n", sd->status.account_id, sd->status.char_id);
				map_quit(sd);
			}
		} else {
			ShowInfo("Closed connection from '"CL_WHITE"%s"CL_RESET"'.\n", ip2str(session[fd]->client_addr, NULL));
		}
		do_close(fd);
		return 0;
	}

	if (RFIFOREST(fd) < 2)
		return 0;

	cmd = clif_parse_cmd(fd, sd);

	// identify client's packet version
	if (sd) {
		packet_ver = sd->packet_ver;
	} else {
		// check authentification packet to know packet version
		packet_ver = clif_guess_PacketVer(fd, 0, &err);
		if( err )
		{// failed to identify packet version
			ShowInfo("clif_parse: Disconnecting session #%d with unknown packet version%s (p:0x%04x,l:%d).\n", fd, (
				err == 1 ? "" :
				err == 2 ? ", possibly for having an invalid account_id" :
				err == 3 ? ", possibly for having an invalid char_id." :
				/* Uncomment when checks are added in clif_guess_PacketVer. [FlavioJS]
				err == 4 ? ", possibly for having an invalid login_id1." :
				err == 5 ? ", possibly for having an invalid client_tick." :
				*/
				err == 6 ? ", possibly for having an invalid sex." :
				". ERROR invalid error code"), cmd, RFIFOREST(fd));
				clif_auth_error(fd, 3);  // Rejected by server
#ifdef DUMP_INVALID_PACKET
			ShowDump(RFIFOP(fd,0), RFIFOREST(fd));
#endif
			RFIFOSKIP(fd, RFIFOREST(fd));
			set_eof(fd);
			return 0;
		}
	}

	// filter out invalid / unsupported packets
	if (cmd > MAX_PACKET_DB || cmd < MIN_PACKET_DB || packet_db[packet_ver][cmd].len == 0) { 
		ShowWarning("clif_parse: Received unsupported packet (packet 0x%04x, %d bytes received), disconnecting session #%d.\n", cmd, RFIFOREST(fd), fd);
#ifdef DUMP_INVALID_PACKET
		ShowDump(RFIFOP(fd,0), RFIFOREST(fd));
#endif
		set_eof(fd);
		return 0;
	}

	// determine real packet length
	packet_len = packet_db[packet_ver][cmd].len;
	if (packet_len == -1) { // variable-length packet
		if (RFIFOREST(fd) < 4)
			return 0;

		packet_len = RFIFOW(fd,2);
		if (packet_len < 4 || packet_len > 32768) {
			ShowWarning("clif_parse: Received packet 0x%04x specifies invalid packet_len (%d), disconnecting session #%d.\n", cmd, packet_len, fd);
#ifdef DUMP_INVALID_PACKET
			ShowDump(RFIFOP(fd,0), RFIFOREST(fd));
#endif
			set_eof(fd);
			return 0;
		}
	}
	if ((int)RFIFOREST(fd) < packet_len) {
		ShowWarning("clif_parse: Received packet 0x%04x with expected packet length %d, but only %d bytes remaining, disconnecting session #%d.\n", cmd, packet_len, RFIFOREST(fd), fd);
#ifdef DUMP_INVALID_PACKET
		ShowDump(RFIFOP(fd, 0), RFIFOREST(fd));
#endif
		set_eof(fd);
		return 0; // not enough data received to form the packet
	}

#ifdef PACKET_OBFUSCATION
	RFIFOW(fd, 0) = cmd;
	if (sd)
		sd->cryptKey = ((sd->cryptKey * clif_cryptKey[1]) + clif_cryptKey[2]) & 0xFFFFFFFF; // Update key for the next packet
#endif

	if( packet_db[packet_ver][cmd].func == clif_parse_debug )
		packet_db[packet_ver][cmd].func(fd, sd);
	else
	if( packet_db[packet_ver][cmd].func != NULL )
	{
		if( !sd && packet_db[packet_ver][cmd].func != clif_parse_WantToConnection )
			; //Only valid packet when there is no session
		else
		if( sd && sd->bl.prev == NULL && packet_db[packet_ver][cmd].func != clif_parse_LoadEndAck )
			; //Only valid packet when player is not on a map
		else
		if( sd && session[sd->fd]->flag.eof )
			; //No more packets accepted
		else
			packet_db[packet_ver][cmd].func(fd, sd); 
	}
#ifdef DUMP_UNKNOWN_PACKET
	else DumpUnknownPacket(fd, sd, cmd, packet_len);
#endif

#ifdef LOG_ALL_PACKETS
	ShowDebug("clif_parse: Received packet 0x%04X (length %d), session #%d\n", cmd, packet_len, fd);
#endif

	RFIFOSKIP(fd, packet_len);

	}; // main loop end

	return 0;
}

/*==========================================
 * Reads packet_db.txt and setups its array reference
 *------------------------------------------*/
void packetdb_readdb(void)
{
	FILE *fp;
	char line[1024];
	int ln=0, entries = 0;
	int cmd,i,j,packet_ver;
	int max_cmd=-1;
	bool skip_ver = false;
	int warned = 0;
	char *str[64],*p,*str2[64],*p2,w1[128],w2[128];
#ifdef PACKET_OBFUSCATION
	bool key_defined = false;
	int last_key_defined = -1;
#endif
	int packet_len_table[MAX_PACKET_DB] = {
	   10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0040
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
#if PACKETVER <= 20081217
	    0,  0,  0,  0, 55, 17,  3, 37, 46, -1, 23, -1,  3,110,  3,  2,
#else
	    0,  0,  0,  0, 55, 17,  3, 37, 46, -1, 23, -1,  3,114,  3,  2,
#endif
#if PACKETVER < 2
	    3, 28, 19, 11,  3, -1,  9,  5, 52, 51, 56, 58, 41,  2,  6,  6,
#elif PACKETVER < 20071106	// 78-7b Lv99 effect for later Kameshima
	    3, 28, 19, 11,  3, -1,  9,  5, 54, 53, 58, 60, 41,  2,  6,  6,
#elif PACKETVER <= 20081217 // change in 0x78 and 0x7c
	    3, 28, 19, 11,  3, -1,  9,  5, 55, 53, 58, 60, 42,  2,  6,  6,
#else
	    3, 28, 19, 11,  3, -1,  9,  5, 55, 53, 58, 60, 44,  2,  6,  6,
#endif
	//#0x0080
	    7,  3,  2,  2,  2,  5, 16, 12, 10,  7, 29,  2, -1, -1, -1,  0, // 0x8b changed to 2 (was 23)
#if PACKETVER < 20181121
	    7, 22, 28,  2,  6, 30, -1, -1,  3, -1, -1,  5,  9, 17, 17,  6,
#else
		7, 22, 28,  2,  6, 30, -1, -1,  3, -1, -1,  5,  9, 19, 17,  6,
#endif
#if PACKETVER <= 20100622
	   23,  6,  6, -1, -1, -1, -1,  8,  7,  6,  7,  4,  7,  0, -1,  6,
#else
	   23,  6,  6, -1, -1, -1, -1,  8,  7,  6,  9,  4,  7,  0, -1,  6, // 0xaa changed to 9 (was 7)
#endif
	    8,  8,  3,  3, -1,  6,  6, -1,  7,  6,  2,  5,  6, 44,  5,  3,
	//#0x00C0
	    7,  2,  6,  8,  6,  7, -1, -1, -1, -1,  3,  3,  6,  3,  2, 27, // 0xcd change to 3 (was 6)
	    3,  4,  4,  2, -1, -1,  3, -1,  6, 14,  3, -1, 28, 29, -1, -1,
	   30, 30, 26,  2,  6, 26,  3,  3,  8, 19,  5,  2,  3,  2,  2,  2,
#if PACKETVER < 20181121
	    3,  2,  6,  8, 21,  8,  8,  2,  2, 26,  3, -1,  6, 27, 30, 10,
#else
		3,  2,  6,  8, 23,  8,  8,  2,  2, 26,  3, -1,  6, 27, 30, 10,
#endif
	//#0x0100
#if PACKETVER < 20181121
	    2,  6,  6, 30, 79, 31, 10, 10, -1, -1,  4,  6,  6,  2, 11, -1,
	   10, 39,  4, 10, 31, 35, 10, 18,  2, 13, 15, 20, 68,  2,  3, 16,
		6, 14, -1, -1, 21,  8,  8,  8,  8,  8,  2,  2,  3,  4,  2, -1,
#else
		2,  6,  6, 30, 79, 31, 10, 10, -1, -1,  6,  6,  6,  2, 11, -1,
	   12, 39,  4, 10, 31, 35, 10, 18,  2, 13, 15, 20, 68,  2,  3, 16,
		6, 14, -1, -1, 23,  8,  8,  8,  8,  8,  2,  2,  3,  4,  2, -1,
#endif
	    6, 86,  6, -1, -1,  7, -1,  6,  3, 16,  4,  4,  4,  6, 24, 26,
	//#0x0140
	   22, 14,  6, 10, 23, 19,  6, 39,  8,  9,  6, 27, -1,  2,  6,  6,
	  110,  6, -1, -1, -1, -1, -1,  6, -1, 54, 66, 54, 90, 42,  6, 42,
	   -1, -1, -1, -1, -1, 30, -1,  3, 14,  3, 30, 10, 43, 14,186,182,
	   14, 30, 10,  3, -1,  6,106, -1,  4,  5,  4, -1,  6,  7, -1, -1,
	//#0x0180
#if PACKETVER < 20181121
	    6,  3,106, 10, 10, 34,  0,  6,  8,  4,  4,  4, 29, -1, 10,  6,
#else
		6,  3,106, 10, 10, 34,  0,  6,  8,  4,  4,  4, 29, -1, 18,  8,
#endif
#if PACKETVER < 1
	   90, 86, 24,  6, 30,102,  8,  4,  8,  4, 14, 10, -1,  6,  2,  6,
#else	// 196 comodo icon status display for later
	   90, 86, 24,  6, 30,102,  9,  4,  8,  4, 14, 10, -1,  6,  2,  6,
#endif
#if PACKETVER < 20081126
	    3,  3, 35,  5, 11, 26, -1,  4,  4,  6, 10, 12,  6, -1,  4,  4,
#elif PACKETVER < 20181121 // 0x1a2 changed (35->37)
	    3,  3, 37,  5, 11, 26, -1,  4,  4,  6, 10, 12,  6, -1,  4,  4,
#else
		3,  3, 37,  5, 11, 26, -1,  4,  4,  6, 10, 12,  6, -1,  6,  4,
#endif
	   11,  7, -1, 67, 12, 18,114,  6,  3,  6, 26, 26, 26, 26,  2,  3,
	//#0x01C0,   Set 0x1d5=-1
	    2, 14, 10, -1, 22, 22,  4,  2, 13, 97,  3,  9,  9, 30,  6, 28,
	    8, 14, 10, 35,  6, -1,  4, 11, 54, 53, 60,  2, -1, 47, 33,  6,
	   30,  8, 34, 14,  2,  6, 26,  2, 28, 81,  6, 10, 26,  2, -1, -1,
#if PACKETVER < 20181121
	   -1, -1, 20, 10, 32,  9, 34, 14,  2,  6, 48, 56, -1,  4,  5, 10,
#else
	  - 1, -1, 20, 10, 32,  9, 34, 14,  2,  6, 48, 56, -1,  6,  5, 10,
#endif
	//#0x0200
#if PACKETVER < 20180221
	   26, -1, 26, 10, 18, 26, 11, 34, 14, 36, 10,  0,  0, -1, 32, 10, // 0x20c change to 0 (was 19)
#else
	   26, -1, 26, 10, 18, 26, 35, 34, 14, 36, 10,  0,  0, -1, 32, 10,
#endif
	   22,  0, 26, 26, 42,  6,  6,  2,  2,282,282, 10, 10, -1, -1, 66,
#if PACKETVER < 20071106
	   10, -1, -1,  8, 10,  2,282, 18, 18, 15, 58, 57, 64,  5, 71,  5,
#elif PACKETVER < 20180221 // 0x22c changed
	   10, -1, -1,  8, 10,  2,282, 18, 18, 15, 58, 57, 65,  5, 71,  5,
#else
	   10, -1, -1, 10, 10,  2,282, 18, 18, 15, 58, 57, 65,  5, 71,  5,
#endif
	   12, 26,  9, 11, -1, -1, 10,  2,282, 11,  4, 36,  6, -1,  4,  2,
	//#0x0240
	   -1, -1, -1, -1, -1,  3,  4,  8, -1,  3, 70,  4,  8, 12,  4, 10,
#if PACKETVER < 20181121
	    3, 32, -1,  3,  3,  5,  5,  8,  2,  3, -1,  6,  4,  6,  4,  6,
#else
		3, 32, -1,  3,  3,  5,  5,  8,  2,  3, -1,  8,  4,  6,  4,  6,
#endif
	    6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0280
#if PACKETVER < 20070711
	    0,  0,  0,  6, 14,  0,  0, -1,  6,  8, 18,  0,  0,  0,  0,  0,
#elif PACKETVER < 20100803
	    0,  0,  0,  6, 14,  0,  0, -1, 10, 12, 18,  0,  0,  0,  0,  0, // 0x288, 0x289 increase by 4 (kafra points)
 #else
	    0,  0,  0,  6, 14,  0,  0, -1, -1, 12, 18,  0,  0,  0,  0,  0, // 0x288 changed to -1
#endif
#if PACKETVER < 20181121
	    0,  4,  0, 70, 10,  0,  0,  0,  8,  6, 27, 80,  0, -1,  0,  0,
#else
	    0,  4,  0, 70, 10,  0,  0,  0, 10,  8, 27, 80,  0, -1,  0,  0,
#endif
	    0,  0,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
#if PACKETVER < 20181121
	   85, -1, -1,107,  6, -1,  7,  7, 22,191,  0,  8,  0,  0,  0,  0,
#else
	   85, -1, -1,107,  6, -1,  7,  7, 24,191,  0,  8,  0,  0,  0,  0,
#endif
	//#0x02C0
	    0, -1,  0,  0,  0, 30, 30,  0,  0,  3,  0, 65,  4, 71, 10,  0,
	   -1, -1, -1,  0, 29,  0,  6, -1, 10, 10,  3,  0, -1, 32,  6, 36,
	   34, 33,  0,  0,  0,  0,  0,  0, -1, -1, -1, 13, 67, 59, 60,  8,
	   10,  2,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0300
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0340
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0380
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x03C0
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0400
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8,  0, 25,
	//#0x0440
	   10,  4, -1,  0,  0,  0, 14,  0,  0,  0,  6,  0,  0,  0,  0,  0,
	    0,  0, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0480
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x04C0
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0500
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 25,
	//#0x0540
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0580
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x05C0
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0600
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 25,
	//#0x0640
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0680
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x06C0
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0700
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 25,
	//#0x0740
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x0780
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	//#0x07C0
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
#if PACKETVER < 20090617
	    6,  2, -1,  4,  4,  4,  4,  8,  8,254,  6,  8,  6, 54, 30, 54,
#else // 0x07d9 changed
	    6,  2, -1,  4,  4,  4,  4,  8,  8,268,  6,  8,  6, 54, 30, 54,
#endif
	    0, 15,  8,  6, -1,  8,  8, 32, -1,  5,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0, 14, -1, -1, -1,  8, 25, 10,  0, 26,  0,
	//#0x0800
#if PACKETVER < 20091229
	   -1, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 14, 20,
#else // for Party booking ( PACKETVER >= 20091229 )
	   -1, -1, 18,  4,  8,  6,  2,  4, 14, 50, 18,  6,  2,  3, 14, 20,
#endif
	    3, -1,  8, -1,  86, 2,  6,  6, -1, -1,  4, 10, 10, 22,  8,  0,
#if PACKETVER < 20181121
	    0,  0,  0,  0,  6,  0,  0,  0,  0,  0,  0,  0,  0, -1,  0,  0,
#else
	    0,  0,  0,  0,  8,  0,  0,  0,  0,  0,  0,  0,  0, -1,  0,  0,
#endif
	    0,  0,  0,  0,  0, -1, -1,  3,  2, 66,  5,  2, 12,  6,  0,  0,
	//#0x0840
#if PACKETVER < 20130000
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
#else
		0,  0,  0,  0,  2,  0,  4,  0, -1,  0,  2, 19,  0,  0,  0,  0,
#endif
	    0,  0,  0,  0,  0,  0, -1, -1, -1, -1,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
//#0x0880
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
//#0x08C0

		0,  0,  0,  0,  0,  0,  0, 20, 34,  2,  0,  0,  0,  0,  0, 10,
		9,	7, 10,	0,	0,	0,	6,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0, -1, 24,
//#0x0900
		0,  0,  0,  0,  0,  0,  0,  0,  5,  0,  0,  0,  0,  0,  0, -1,
		0,  0,  0,  0, -1, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0, 24,
//#0x0940
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,
		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0, -1, -1,  7,
		0,	0,	0,	0,	2,	0,	0, 14,	6, 50, -1,	0,	4,288, 12,	0,
//#0x0980
		0,  0,  0, 29, 28,  0,  0,  0,  6,  2, -1,  0,  0, -1, -1,  0,
		31, 0,  0,  0,  0,  0,  0, -1,  8, 11,  9,  8,  0, -1,  0, 22,
		6,  2,  0,  0,  0,  0, 12, 10, 14, 10, 14,  6,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  6,  4,  6,  4,  0,  0,  0,  0,  0,  0, 
//#0x09C0
		0, 10,  0,  0,  0,  0,  0,  0,  0,  0, 23, 17,  0,  8,102,  0,
		0,  0,  0,  0,  2,  0, -1, -1,  2,  0,  0, -1, -1, -1,  0,  7,
		0,  0,  0,  0,  0, 18, 22,  3, 11,  0, 11, -1,  0,  3, 11, 11,
	   -1, 11, 12, 11,  0,  0,  0, 75, -1,143, -1,  0,  6, -1, -1, -1,
//#0x0A00
#if PACKETVER >= 20141022
	  269,  3,  4,  2,  6, 49,  6,  9, 26, 45, 47, 47, 56, -1, 14, -1,
#else
	  269,  0,  0,  2,  6, 48,  6,  9, 26, 45, 47, 47, 56, -1, 14,  0,
#endif
		-1, 0,  0, 26, 10,  0,  0,  0, 14,  2, 23,  2, -1,  2,  3,  2,
#if PACKETVER < 20181121
	   21,  3,  5, -1, 66,  6,  7,  8,  3,  0,  0,  0,  0, -1,  6,  7,
#else
	   25,  3,  5, -1, 66,  6,  7,  8,  3,  0,  0,  0,  0, -1,  6,  7,
#endif
 	  106,  0,  0,  0,  0,  4,  0, 59,  3,  0,  0,  0,  0,  0,  0,  0,
//#0x0A40
 		0,  0,  0, 85, -1,  0, 14,  3,  2, 20,  6,  0,  0,  0,  0, -1,
		0, 34,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,  3,  0,  0,  0,  0, -1,  0,  0,
 		2,  0,  0,  0,  0,  0,  0, 15, 15,  0,  0,  0,  0, -1,  0,  0,
//#0x0A80
	    0,  0,  0,  0, 94,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  8, 10,  4, 10, -1,  2,  4,  4,  0,
	    0,  0,  0,  0,  0, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
#if PACKETVER < 20181121
	    0,  0,  7,  0,  0,  0,  6,  0,  0,  0,  0,  0,  0, 10,  0,  0,
#else
		0,  0,  7,  0,  0,  0,  8,  0,  0,  0,  0,  0,  0, 10,  0,  0,
#endif

//#0x0AC0
		26,26,  0,  0, -1,156,  0,  0,  0,  0,  0, 12, 18,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 22,  0,  0,
	    0,  0,  7,  0,  0,  0,  0,  0,  2,  0,  0,  0,  0,  0,  0,  2,
	   10,  0,  0,  0, 11,  0,  0, 32,  0,  0,  0,  0,  0,  0,  0,  0,
//#0x0B00
	    8, 40,  0,  0,  0,  0,  0,  0,  0, -1, -1,  4,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	    0,  0,  0,  0,  0,  0,  0,  0,  0, -1,  0,  0,  0,  0,  0,  0,
	};
	struct {
		void (*func)(int, struct map_session_data *);
		char *name;
	} clif_parse_func[]={
		{clif_parse_WantToConnection,"wanttoconnection"},
		{clif_parse_LoadEndAck,"loadendack"},
		{clif_parse_TickSend,"ticksend"},
		{clif_parse_WalkToXY,"walktoxy"},
		{clif_parse_QuitGame,"quitgame"},
		{clif_parse_GetCharNameRequest, "getcharnamerequest"},
		{clif_parse_GlobalMessage,"globalmessage"},
		{clif_parse_MapMove,"mapmove"},
		{clif_parse_ChangeDir,"changedir"},
		{clif_parse_Emotion,"emotion"},
		{clif_parse_HowManyConnections,"howmanyconnections"},
		{clif_parse_ActionRequest,"actionrequest"},
		{clif_parse_Restart,"restart"},
		{clif_parse_WisMessage,"wis"},
		{clif_parse_Broadcast,"broadcast"},
		{clif_parse_TakeItem,"takeitem"},
		{clif_parse_DropItem,"dropitem"},
		{clif_parse_UseItem,"useitem"},
		{clif_parse_EquipItem,"equipitem"},
		{clif_parse_UnequipItem,"unequipitem"},
		{clif_parse_NpcClicked,"npcclicked"},
		{clif_parse_NpcBuySellSelected,"npcbuysellselected"},
		{clif_parse_NpcBuyListSend,"npcbuylistsend"},
		{clif_parse_NpcSellListSend,"npcselllistsend"},
		{clif_parse_CreateChatRoom,"createchatroom"},
		{clif_parse_ChatAddMember,"chataddmember"},
		{clif_parse_ChatRoomStatusChange,"chatroomstatuschange"},
		{clif_parse_ChangeChatOwner,"changechatowner"},
		{clif_parse_KickFromChat,"kickfromchat"},
		{clif_parse_ChatLeave,"chatleave"},
		{clif_parse_TradeRequest,"traderequest"},
		{clif_parse_TradeAck,"tradeack"},
		{clif_parse_TradeAddItem,"tradeadditem"},
		{clif_parse_TradeOk,"tradeok"},
		{clif_parse_TradeCancel,"tradecancel"},
		{clif_parse_TradeCommit,"tradecommit"},
		{clif_parse_StopAttack,"stopattack"},
		{clif_parse_PutItemToCart,"putitemtocart"},
		{clif_parse_GetItemFromCart,"getitemfromcart"},
		{clif_parse_RemoveOption,"removeoption"},
		{clif_parse_ChangeCart,"changecart"},
		{clif_parse_StatusUp,"statusup"},
		{clif_parse_SkillUp,"skillup"},
		{clif_parse_UseSkillToId,"useskilltoid"},
		{clif_parse_UseSkillToPos,"useskilltopos"},
		{clif_parse_UseSkillToPosMoreInfo,"useskilltoposinfo"},
		{clif_parse_UseSkillMap,"useskillmap"},
		{clif_parse_RequestMemo,"requestmemo"},
		{clif_parse_ProduceMix,"producemix"},
		{clif_parse_Cooking,"cooking"},
		{clif_parse_NpcSelectMenu,"npcselectmenu"},
		{clif_parse_NpcNextClicked,"npcnextclicked"},
		{clif_parse_NpcAmountInput,"npcamountinput"},
		{clif_parse_NpcStringInput,"npcstringinput"},
		{clif_parse_NpcCloseClicked,"npccloseclicked"},
		{clif_parse_ItemIdentify,"itemidentify"},
		{clif_parse_SelectArrow,"selectarrow"},
		{clif_parse_AutoSpell,"autospell"},
		{clif_parse_UseCard,"usecard"},
		{clif_parse_InsertCard,"insertcard"},
		{clif_parse_RepairItem,"repairitem"},
		{clif_parse_WeaponRefine,"weaponrefine"},
		{clif_parse_SolveCharName,"solvecharname"},
		{clif_parse_ResetChar,"resetchar"},
		{clif_parse_LocalBroadcast,"localbroadcast"},
		{clif_parse_MoveToKafra,"movetokafra"},
		{clif_parse_MoveFromKafra,"movefromkafra"},
		{clif_parse_MoveToKafraFromCart,"movetokafrafromcart"},
		{clif_parse_MoveFromKafraToCart,"movefromkafratocart"},
		{clif_parse_CloseKafra,"closekafra"},
		{clif_parse_CreateParty,"createparty"},
		{clif_parse_CreateParty2,"createparty2"},
		{clif_parse_PartyInvite,"partyinvite"},
		{clif_parse_PartyInvite2,"partyinvite2"},
		{clif_parse_ReplyPartyInvite,"replypartyinvite"},
		{clif_parse_ReplyPartyInvite2,"replypartyinvite2"},
		{clif_parse_LeaveParty,"leaveparty"},
		{clif_parse_RemovePartyMember,"removepartymember"},
		{clif_parse_PartyChangeOption,"partychangeoption"},
		{clif_parse_PartyMessage,"partymessage"},
		{clif_parse_PartyChangeLeader,"partychangeleader"},
		{clif_parse_CloseVending,"closevending"},
		{clif_parse_VendingListReq,"vendinglistreq"},
		{clif_parse_PurchaseReq,"purchasereq"},
		{clif_parse_PurchaseReq2,"purchasereq2"},
		{clif_parse_OpenVending,"openvending"},
		{clif_parse_CreateGuild,"createguild"},
		{clif_parse_GuildCheckMaster,"guildcheckmaster"},
		{clif_parse_GuildRequestInfo,"guildrequestinfo"},
		{clif_parse_GuildChangePositionInfo,"guildchangepositioninfo"},
		{clif_parse_GuildChangeMemberPosition,"guildchangememberposition"},
		{clif_parse_GuildRequestEmblem,"guildrequestemblem"},
		{clif_parse_GuildChangeEmblem,"guildchangeemblem"},
		{clif_parse_GuildChangeNotice,"guildchangenotice"},
		{clif_parse_GuildInvite,"guildinvite"},
		{clif_parse_GuildReplyInvite,"guildreplyinvite"},
		{clif_parse_GuildLeave,"guildleave"},
		{clif_parse_GuildExpulsion,"guildexpulsion"},
		{clif_parse_GuildMessage,"guildmessage"},
		{clif_parse_GuildRequestAlliance,"guildrequestalliance"},
		{clif_parse_GuildReplyAlliance,"guildreplyalliance"},
		{clif_parse_GuildDelAlliance,"guilddelalliance"},
		{clif_parse_GuildOpposition,"guildopposition"},
		{clif_parse_GuildBreak,"guildbreak"},
		{clif_parse_PetMenu,"petmenu"},
		{clif_parse_CatchPet,"catchpet"},
		{clif_parse_SelectEgg,"selectegg"},
		{clif_parse_SendEmotion,"sendemotion"},
		{clif_parse_ChangePetName,"changepetname"},

		{clif_parse_GMKick,"gmkick"},
		{clif_parse_GMHide,"gmhide"},
		{clif_parse_GMReqNoChat,"gmreqnochat"},
		{clif_parse_GMReqAccountName,"gmreqaccname"},
		{clif_parse_GMKickAll,"killall"},
		{clif_parse_GMRecall,"recall"},
		{clif_parse_GMRecall,"summon"},
		{clif_parse_GM_Monster_Item,"itemmonster"},
		{clif_parse_GMShift,"remove"},
		{clif_parse_GMShift,"shift"},
		{clif_parse_GMChangeMapType,"changemaptype"},
		{clif_parse_GMRc,"rc"},
		{clif_parse_GMRecall2,"recall2"},
		{clif_parse_GMRemove2,"remove2"},

		{clif_parse_NoviceDoriDori,"sndoridori"},
		{clif_parse_NoviceExplosionSpirits,"snexplosionspirits"},
		{clif_parse_PMIgnore,"wisexin"},
		{clif_parse_PMIgnoreList,"wisexlist"},
		{clif_parse_PMIgnoreAll,"wisall"},
		{clif_parse_FriendsListAdd,"friendslistadd"},
		{clif_parse_FriendsListRemove,"friendslistremove"},
		{clif_parse_FriendsListReply,"friendslistreply"},
		{clif_parse_ranklist,"pranklist"},
		{clif_parse_Blacksmith,"blacksmith"},
		{clif_parse_Alchemist,"alchemist"},
		{clif_parse_Taekwon,"taekwon"},
		{clif_parse_RankingPk,"rankingpk"},
		{clif_parse_FeelSaveOk,"feelsaveok"},
		{clif_parse_debug,"debug"},
		{clif_parse_ChangeHomunculusName,"changehomunculusname"},
		{clif_parse_HomMoveToMaster,"hommovetomaster"},
		{clif_parse_HomMoveTo,"hommoveto"},
		{clif_parse_HomAttack,"homattack"},
		{clif_parse_HomMenu,"hommenu"},
		{clif_parse_StoragePassword,"storagepassword"},
		{clif_parse_Hotkey,"hotkey"},
		{clif_parse_AutoRevive,"autorevive"},
		{clif_parse_Check,"check"},
		{clif_parse_Adopt_request,"adoptrequest"},
		{clif_parse_Adopt_reply,"adoptreply"},
#ifndef TXT_ONLY
		// MAIL SYSTEM
		{clif_parse_Mail_refreshinbox,"mailrefresh"},
		{clif_parse_Mail_read,"mailread"},
		{clif_parse_Mail_getattach,"mailgetattach"},
		{clif_parse_Mail_delete,"maildelete"},
		{clif_parse_Mail_return,"mailreturn"},
		{clif_parse_Mail_setattach,"mailsetattach"},
		{clif_parse_Mail_winopen,"mailwinopen"},
		{clif_parse_Mail_send,"mailsend"},
		{clif_parse_Mail_beginwrite,"mailbegin"},
		{clif_parse_Mail_cancelwrite,"mailcancel"},
		{clif_parse_Mail_Receiver_Check,"mailreceiver"},
		// AUCTION SYSTEM
		{clif_parse_Auction_search,"auctionsearch"},
		{clif_parse_Auction_buysell,"auctionbuysell"},
		{clif_parse_Auction_setitem,"auctionsetitem"},
		{clif_parse_Auction_cancelreg,"auctioncancelreg"},
		{clif_parse_Auction_register,"auctionregister"},
		{clif_parse_Auction_cancel,"auctioncancel"},
		{clif_parse_Auction_close,"auctionclose"},
		{clif_parse_Auction_bid,"auctionbid"},
		// Quest Log System
		{clif_parse_questStateAck,"queststate"},
#endif
#if PACKETVER < 20100803
		{clif_parse_cashshop_buy,"cashshopbuy"},
#else
		{clif_parse_CashShopBuy,"cashshopbuy"},
#endif
		{clif_parse_ViewPlayerEquip,"viewplayerequip"},
		{clif_parse_cz_config,"p_cz_config"},
		{clif_parse_BattleChat,"battlechat"},
		{clif_parse_mercenary_action,"mermenu"},
		{clif_parse_progressbar,"progressbar"},
		{clif_parse_SkillSelectMenu,"skillselectmenu"},
		{clif_parse_ItemListWindowSelected,"itemlistwindowselected"},
#if PACKETVER >= 20091229
		{clif_parse_PartyBookingRegisterReq,"bookingregreq"},
		{clif_parse_PartyBookingSearchReq,"bookingsearchreq"},
		{clif_parse_PartyBookingUpdateReq,"bookingupdatereq"},
		{clif_parse_PartyBookingDeleteReq,"bookingdelreq"},
#endif
		{clif_parse_BankCheck,"bankcheck"},
		{clif_parse_BankDeposit,"bankdeposit"},
		{clif_parse_BankWithdraw,"bankwithdrawal"},
		{clif_parse_BankOpen,"bankopen"},
		{clif_parse_BankClose,"bankclose"},

		{clif_parse_PVPInfo,"pvpinfo"},
		{clif_parse_LessEffect,"lesseffect"},
		// Buying Store
		{clif_parse_ReqOpenBuyingStore,"reqopenbuyingstore"},
		{clif_parse_ReqCloseBuyingStore,"reqclosebuyingstore"},
		{clif_parse_ReqClickBuyingStore,"reqclickbuyingstore"},
		{clif_parse_ReqTradeBuyingStore,"reqtradebuyingstore"},
		// Store Search
		{clif_parse_SearchStoreInfo,"searchstoreinfo"},
		{clif_parse_SearchStoreInfoNextPage,"searchstoreinfonextpage"},
		{clif_parse_CloseSearchStoreInfo,"closesearchstoreinfo"},
		{clif_parse_SearchStoreInfoListItemClick,"searchstoreinfolistitemclick"},
		{ clif_parse_MoveItem , "moveitem" },
		// CashShop
		{clif_parse_CashShopOpen,"pCashShopOpen"},
		{clif_parse_CashShopClose,"pCashShopClose"},
		{clif_parse_CashShopReqTab, "cashshopreqtab" },
		{clif_cashshop_list,"pCashShopSchedule"},
		{clif_parse_CashShopBuy,"pCashShopBuy"},
		{clif_parse_NPCShopClosed,"npcshopclosed"},
		// Market NPC
		{ clif_parse_NPCMarketClosed, "npcmarketclosed" },
		{ clif_parse_NPCMarketPurchase, "npcmarketpurchase" },
		// Roulette
		{ clif_parse_RouletteOpen, "rouletteopen" },
		{ clif_parse_RouletteInfo, "rouletteinfo" },
		{ clif_parse_RouletteClose, "rouletteclose" },
		{ clif_parse_RouletteGenerate, "roulettegenerate" },
		{ clif_parse_RouletteRecvItem, "rouletterecvitem" },
		// HotkeyRowShift
		{ clif_parse_HotkeyRowShift, "hotkeyrowshift"},
		// Clan System
		{clif_parse_clan_chat, "clanchat"},
		{clif_parse_AchievementCheckReward, "pAchivementCheckReward" },
		{clif_parse_change_title, "pChangeTitle" },
		// Battleground Queue
		{clif_parse_bgqueue_register, "pBGQueueRegister"},
		{clif_parse_bgqueue_checkstate, "pBGQueueCheckState"},
		{clif_parse_bgqueue_revoke_req, "pBGQueueRevokeReq"},
		{clif_parse_bgqueue_battlebegin_ack, "pBGQueueBattleBeginAck"},
		// OneClick Item Identify
		{ clif_parse_Oneclick_Itemidentify, "oneclick_itemidentify" },
		// Equip switch
		{clif_parse_equipswitch_add, "equipswitch_add"},
		{clif_parse_equipswitch_remove, "equipswitch_remove"},
		{clif_parse_equipswitch_request, "equipswitch_request"},
		{clif_parse_equipswitch_request_single, "equipswitch_request_single"},
		{clif_parse_changedress, "changedress"},
		{clif_parse_private_airship_request, "pPrivateAirshipRequest"},
		{clif_parse_open_ui_request, "pOpenUIRequest"},
		{clif_parse_attendance_reward_request, "pAttendanceRewardRequest"},
		{clif_parse_pet_evolution, "petevolution"},
		{clif_parse_reqworldinfo, "reqworldinfo"},
		{clif_parse_GuildInvite2, "guildinvite2"},
		{clif_parse_client_version, "clientversion"},
		{clif_parse_blocking_playcancel, "booking_playcancel"},
		{clif_parse_GMFullStrip, "gmfullstrip"},
		// Merge Item
		{ clif_parse_merge_item_req, "mergeitem_req" },
		{ clif_parse_merge_item_cancel, "mergeitem_cancel" },
		{ clif_parse_PartyTick, "partytick" },
		{ clif_parse_camerainfo, "pcamerainfo" },
		{ clif_parse_lapineDdukDdak_ack, "plapineDdukDdak_ack" },
		{ clif_parse_lapineDdukDdak_close, "plapineDdukDdak_close" },
		{ clif_parse_lapineUpgrade_makeItem, "pLapineUpgrade_makeItem" },
		{ clif_parse_dull, "dull" },
		{NULL,NULL}
	};

	struct 
	{
		char *name; //function name
		int funcidx; //
	} 
	clif_ack_func[] =
	{ //hash
		{"ZC_ACK_OPEN_BANKING", ZC_ACK_OPEN_BANKING},
		{"ZC_ACK_BANKING_DEPOSIT", ZC_ACK_BANKING_DEPOSIT},
		{"ZC_ACK_BANKING_WITHDRAW", ZC_ACK_BANKING_WITHDRAW},
		{"ZC_BANKING_CHECK", ZC_BANKING_CHECK},
		{"ZC_WEAR_EQUIP_ACK",ZC_WEAR_EQUIP_ACK},
		{"ZC_BROADCASTING_SPECIAL_ITEM_OBTAIN",ZC_BROADCASTING_SPECIAL_ITEM_OBTAIN},
		{"ZC_CLEAR_DIALOG", ZC_CLEAR_DIALOG},
		{"ZC_NOTIFY_BIND_ON_EQUIP", ZC_NOTIFY_BIND_ON_EQUIP},
		{ "ZC_MERGE_ITEM_OPEN", ZC_MERGE_ITEM_OPEN },
		{ "ZC_ACK_MERGE_ITEM", ZC_ACK_MERGE_ITEM },

	}; 

	memset(packet_db_ack, 0, sizeof(packet_db_ack));

	// initialize packet_db[SERVER] from hardcoded packet_len_table[] values
	memset(packet_db, 0, sizeof(packet_db));
	for (i = 0; i < ARRAYLENGTH(packet_len_table); ++i)
		packet_len(i) = packet_len_table[i];
	
	entries = 0;
	clif_config.packet_db_ver = MAX_PACKET_VER;

	sprintf(line, "%s/packet_db.txt", db_path);
	if ((fp = fopen(line, "r")) == NULL)
	{
		ShowFatalError("can't read %s\n", line);
		exit(EXIT_FAILURE);
	}

	packet_ver = MAX_PACKET_VER;	// read into packet_db's version by default
	while( fgets(line, sizeof(line), fp) )
	{
		ln++;
		if(line[0]=='/' && line[1]=='/')
			continue;
		if (sscanf(line,"%255[^:]: %255[^\r\n]",w1,w2) == 2)
		{
			if(strcmpi(w1,"packet_ver")==0) {
				int prev_ver = packet_ver;
				skip_ver = false;
				packet_ver = atoi(w2);
				if ( packet_ver > MAX_PACKET_VER )
				{	//Check to avoid overflowing. [Skotlex]
					if( (warned&1) == 0 )
						ShowWarning("The packet_db table only has support up to version %d.\n", MAX_PACKET_VER);
					warned &= 1;
					skip_ver = true;
				}
				else if( packet_ver < 0 )
				{
					if( (warned&2) == 0 )
						ShowWarning("Negative packet versions are not supported.\n");
					warned &= 2;
					skip_ver = true;
				}
				else if( packet_ver == SERVER )
				{
					if( (warned&4) == 0 )
						ShowWarning("Packet version %d is reserved for server use only.\n", SERVER);
					warned &= 4;
					skip_ver = true;
				}

				if( skip_ver )
				{
					ShowWarning("Skipping packet version %d.\n", packet_ver);
					packet_ver = prev_ver;
					continue;
				}
				// copy from previous version into new version and continue
				// - indicating all following packets should be read into the newer version
				memcpy(&packet_db[packet_ver], &packet_db[prev_ver], sizeof(packet_db[0]));
				memcpy(&packet_db_ack[packet_ver], &packet_db_ack[prev_ver], sizeof(packet_db_ack[0])); 
				continue;
			} else if(strcmpi(w1,"packet_db_ver")==0) {
				if(strcmpi(w2,"default")==0) //This is the preferred version.
					clif_config.packet_db_ver = MAX_PACKET_VER;
				else // to manually set the packet DB version
					clif_config.packet_db_ver = cap_value(atoi(w2), 0, MAX_PACKET_VER);
				
				continue;
			}
#ifdef PACKET_OBFUSCATION
			else if (strcmpi(w1,"packet_keys") == 0) {
				char key1[12] = { 0 }, key2[12] = { 0 }, key3[12] = { 0 };
				trim(w2);
				if (sscanf(w2, "%11[^,],%11[^,],%11[^ \r\n/]", key1, key2, key3) == 3) {
					CREATE(packet_keys[packet_ver], struct s_packet_keys, 1);
					packet_keys[packet_ver]->keys[0] = strtol(key1, NULL, 0);
					packet_keys[packet_ver]->keys[1] = strtol(key2, NULL, 0);
					packet_keys[packet_ver]->keys[2] = strtol(key3, NULL, 0);
					last_key_defined = packet_ver;
					if (battle_config.etc_log)
						ShowInfo("Packet Ver:%d -> Keys: 0x%08X, 0x%08X, 0x%08X\n", packet_ver, packet_keys[packet_ver]->keys[0], packet_keys[packet_ver]->keys[1], packet_keys[packet_ver]->keys[2]);
				}
				continue;
			} else if (strcmpi(w1,"packet_keys_use") == 0) {
				char key1[12] = { 0 }, key2[12] = { 0 }, key3[12] = { 0 };
				trim(w2);
				if (strcmpi(w2,"default") == 0)
					continue;
				if (sscanf(w2, "%11[^,],%11[^,],%11[^ \r\n/]", key1, key2, key3) == 3) {
					clif_cryptKey[0] = strtol(key1, NULL, 0);
					clif_cryptKey[1] = strtol(key2, NULL, 0);
					clif_cryptKey[2] = strtol(key3, NULL, 0);
					key_defined = true;
					if (battle_config.etc_log)
						ShowInfo("Defined keys: 0x%08X, 0x%08X, 0x%08X\n", clif_cryptKey[0], clif_cryptKey[1], clif_cryptKey[2]);
				}
				continue;
			}
#endif
		}

		if( skip_ver != 0 )
			continue; // Skipping current packet version

		memset(str,0,sizeof(str));
		for(j=0,p=line;j<4 && p; ++j)
		{
			str[j]=p;
			p=strchr(p,',');
			if(p) *p++=0;
		}
		if(str[0]==NULL)
			continue;
		cmd=strtol(str[0],(char **)NULL,0);
		if(max_cmd < cmd)
			max_cmd = cmd;
		if(cmd <= 0 || cmd > MAX_PACKET_DB)
			continue;
		if(str[1]==NULL){
			ShowError("packet_db: packet len error\n");
			continue;
		}

		packet_db[packet_ver][cmd].len = (short)atoi(str[1]);

		if(str[2]==NULL){
			packet_db[packet_ver][cmd].func = NULL;
			ln++;
			continue;
		}

		// look up processing function by name
		ARR_FIND( 0, ARRAYLENGTH(clif_parse_func), j, clif_parse_func[j].name != NULL && strcmp(str[2],clif_parse_func[j].name)==0 );
		if( j < ARRAYLENGTH(clif_parse_func) )
			packet_db[packet_ver][cmd].func = clif_parse_func[j].func;
		 else { //search if it's a mapped ack func
			ARR_FIND( 0, ARRAYLENGTH(clif_ack_func), j, clif_ack_func[j].name != NULL && strcmp(str[2],clif_ack_func[j].name)==0 );
			if( j < ARRAYLENGTH(clif_ack_func)) {
				int fidx = clif_ack_func[j].funcidx;
				packet_db_ack[packet_ver][fidx] = cmd;
#ifdef NOISY
				ShowInfo("Added %s, <=> %X i=%d for v=%d\n",clif_ack_func[j].name,cmd,fidx,packet_ver);
#endif
			}
		} 

		// set the identifying cmd for the packet_db version
		if (strcmp(str[2],"wanttoconnection")==0)
			clif_config.connect_cmd[packet_ver] = cmd;
			
		if(str[3]==NULL){
			ShowError("packet_db: packet error\n");
			exit(EXIT_FAILURE);
		}
		for(j=0,p2=str[3];p2;j++){
			short k;
			str2[j]=p2;
			p2=strchr(p2,':');
			if(p2) *p2++=0;
			k = atoi(str2[j]);
			// if (packet_db[packet_ver][cmd].pos[j] != k && clif_config.prefer_packet_db)	// not used for now

			if( j >= MAX_PACKET_POS )
			{
				ShowError("Too many positions found for packet 0x%04x (max=%d).\n", cmd, MAX_PACKET_POS);
				break;
			}

			packet_db[packet_ver][cmd].pos[j] = k;
		}
		entries++;
	}
	fclose(fp);
	if(max_cmd > MAX_PACKET_DB)
	{
		ShowWarning("Found packets up to 0x%X, ignored 0x%X and above.\n", max_cmd, MAX_PACKET_DB);
		ShowWarning("Please increase MAX_PACKET_DB and recompile.\n");
	}
	if (!clif_config.connect_cmd[clif_config.packet_db_ver])
	{	//Locate the nearest version that we still support. [Skotlex]
		for(j = clif_config.packet_db_ver; j >= 0 && !clif_config.connect_cmd[j]; j--);
		
		clif_config.packet_db_ver = j?j:MAX_PACKET_VER;
	}
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entries, "packet_db.txt");
	ShowStatus("Using default packet version: "CL_WHITE"%d"CL_RESET".\n", clif_config.packet_db_ver);

#ifdef PACKET_OBFUSCATION
	if (!key_defined && !clif_cryptKey[0] && !clif_cryptKey[1] && !clif_cryptKey[2]) { // Not defined
		int use_key = last_key_defined;
		
		if (last_key_defined == -1)
			ShowError("Can't find packet obfuscation keys!\n");
		else {
			if (packet_keys[clif_config.packet_db_ver])
				use_key = clif_config.packet_db_ver;

			ShowInfo("Using default packet obfuscation keys for packet_db_ver: %d\n", use_key);
			memcpy(&clif_cryptKey, &packet_keys[use_key]->keys, sizeof(packet_keys[use_key]->keys));
		}
	}
	ShowStatus("Packet Obfuscation: "CL_GREEN"Enabled"CL_RESET". Keys: "CL_WHITE"0x%08X, 0x%08X, 0x%08X"CL_RESET"\n", clif_cryptKey[0], clif_cryptKey[1], clif_cryptKey[2]);

	for (i = 0; i < ARRAYLENGTH(packet_keys); i++) {
		if (packet_keys[i]) {
			aFree(packet_keys[i]);
			packet_keys[i] = NULL;
		}
	}
#endif
}

/*
 * Destroys cashshop class.
 * Closes all and cleanup.
 */
void do_final_cashshop( void ){
	int tab, i;

	for( tab = CASHSHOP_TAB_NEW; tab < CASHSHOP_TAB_MAX; tab++ ){
		for( i = 0; i < cash_shop_items[tab].count; i++ ){
			aFree( cash_shop_items[tab].item[i] );
		}
		aFree( cash_shop_items[tab].item );
	}
	memset( cash_shop_items, 0, sizeof( cash_shop_items ) );
}

/*==========================================
 *
 *------------------------------------------*/
void do_init_clif(void)
{
	clif_config.packet_db_ver = -1; // the main packet version of the DB
	memset(clif_config.connect_cmd, 0, sizeof(clif_config.connect_cmd)); //The default connect command will be determined after reading the packet_db [Skotlex]
#ifdef PACKET_OBFUSCATION
	memset(clif_cryptKey, 0, sizeof(clif_cryptKey));
#endif

	//Using the packet_db file is the only way to set up packets now [Skotlex]
	packetdb_readdb();

	set_defaultparse(clif_parse);
	if( make_listen_bind(bind_ip,map_port) == -1 )
	{
		ShowFatalError("Failed to bind to port '"CL_WHITE"%d"CL_RESET"'\n", map_port);
		exit(EXIT_FAILURE);
	}

	add_timer_func_list(clif_clearunit_delayed_sub, "clif_clearunit_delayed_sub");
	add_timer_func_list(clif_delayquit, "clif_delayquit");

	delay_clearunit_ers = ers_new(sizeof(struct block_list), "clif.c::delay_clearunit_ers", ERS_OPT_CLEAR);
}

void do_final_clif(void) {
	ers_destroy(delay_clearunit_ers);
}
