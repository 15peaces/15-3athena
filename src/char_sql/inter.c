// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"

#include "char.h"
#include "inter.h"
#include "int_party.h"
#include "int_guild.h"
#include "int_storage.h"
#include "int_pet.h"
#include "int_homun.h"
#include "int_mercenary.h"
#include "int_elemental.h"
#include "int_clan.h"
#include "int_mail.h"
#include "int_auction.h"
#include "int_quest.h"
#include "int_achievement.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WISDATA_TTL (60*1000)	// Wisデータの生存時間(60秒)
#define WISDELLIST_MAX 256			// Wisデータ削除リストの要素数


Sql* sql_handle = NULL;

int char_server_port = 3306;
char char_server_ip[32] = "127.0.0.1";
char char_server_id[32] = "ragnarok";
char char_server_pw[32] = "ragnarok";
char char_server_db[32] = "ragnarok";
char default_codepage[32] = ""; //Feature by irmin.

#ifndef TXT_SQL_CONVERT

static struct accreg *accreg_pt;
unsigned int party_share_level = 10;
char main_chat_nick[16] = "Main";

struct s_storage_table *int_storages = NULL; ///< Storage name & table information
uint8 int_storage_count=0;			  ///< Number of available storage

// recv. packet list
int inter_recv_packet_length[] = {
	-1,-1, 7,-1, -1,13,36, 0,  0,-1, 0, 0,  0, 0,  0, 0,	// 3000-
	 6,-1, 0, 0,  0, 0, 0, 0, 10,-1, 0, 0,  0, 0,  0, 0,	// 3010-
	-1,10,-1,14, 15+NAME_LENGTH,21, 6,-1, 14,14, 0, 0,  0, 0,  0, 0,	// 3020- Party
	-1, 6,-1,-1, 55,19, 6,-1, 14,-1,-1,-1, 18,19,186,-1,	// 3030-
	-1, 9, 0, 0,  0, 0, 0, 0,  8, 6,11,10, 10,-1,6+NAME_LENGTH, 0,	// 3040-
	-1,-1,10,10,  0,-1,12, 0,  0, 0, 0, 0,  0, 0,  0, 0,	// 3050-  Auction System [Zephyrus]
	 6,-1, 6,-1,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,	// 3060-  Quest system [Kevin] [Inkfish] / Achievements [Aleos]
	-1,10, 6,-1,  0, 0, 0, 0, -1,10, 6,-1,  0, 0,  0, 0,	// 3070-  Mercenary packets [Zephyrus] Elementals [Rytech]
	52,14,-1, 6,  0, 0, 0, 0,  0, 0,12,-1,  0, 0,  0, 0,	// 3080-  Pet System, Storage
	-1,10,-1, 6,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,	// 3090-  Homunculus packets [albator]
	 2,-1, 6, 6,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,	// 30A0-  Clan packets
};

struct WisData {
	int id, fd, count, len;
	int64 tick;
	unsigned char src[24], dst[24], msg[512];
};
static DBMap* wis_db = NULL; // int wis_id -> struct WisData*
static int wis_dellist[WISDELLIST_MAX], wis_delnum;

#endif //TXT_SQL_CONVERT
//--------------------------------------------------------
// Save registry to sql
int inter_accreg_tosql(uint32 account_id, uint32 char_id, struct accreg* reg, int type)
{
	struct global_reg* r;
	StringBuf buf;
	int i;

	if( account_id <= 0 )
		return 0;
	reg->account_id = account_id;
	reg->char_id = char_id;

	//`global_reg_value` (`type`, `account_id`, `char_id`, `str`, `value`)
	switch( type )
	{
	case 3: //Char Reg
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `type`=3 AND `char_id`='%d'", reg_db, char_id) )
			Sql_ShowDebug(sql_handle);
		account_id = 0;
		break;
	case 2: //Account Reg
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `type`=2 AND `account_id`='%d'", reg_db, account_id) )
			Sql_ShowDebug(sql_handle);
		char_id = 0;
		break;
	case 1: //Account2 Reg
		ShowError("inter_accreg_tosql: Char server shouldn't handle type 1 registry values (##). That is the login server's work!\n");
		return 0;
	default:
		ShowError("inter_accreg_tosql: Invalid type %d\n", type);
		return 0;
	}

	if( reg->reg_num <= 0 )
		return 0;

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s` (`type`,`account_id`,`char_id`,`str`,`value`) VALUES ", reg_db);

	for (i = 0; i < reg->reg_num; ++i) {
		r = &reg->reg[i];
		if (r->str[0] != '\0' && r->value[0] != '\0') {
			char str[32];
			char val[256];

			if (i > 0)
				StringBuf_AppendStr(&buf, ",");

			Sql_EscapeString(sql_handle, str, r->str);
			Sql_EscapeString(sql_handle, val, r->value);

			StringBuf_Printf(&buf, "('%d','%d','%d','%s','%s')", type, account_id, char_id, str, val);
		}
	}

	if (SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf))) {
		Sql_ShowDebug(sql_handle);
	}

	StringBuf_Destroy(&buf);

	return 1;
}
#ifndef TXT_SQL_CONVERT

// Load account_reg from sql (type=2)
int inter_accreg_fromsql(uint32 account_id,uint32 char_id, struct accreg *reg, int type)
{
	struct global_reg* r;
	char* data;
	size_t len;
	int i;

	if( reg == NULL)
		return 0;

	memset(reg, 0, sizeof(struct accreg));
	reg->account_id = account_id;
	reg->char_id = char_id;

	//`global_reg_value` (`type`, `account_id`, `char_id`, `str`, `value`)
	switch( type )
	{
	case 3: //char reg
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `str`, `value` FROM `%s` WHERE `type`=3 AND `char_id`='%d'", reg_db, char_id) )
			Sql_ShowDebug(sql_handle);
		break;
	case 2: //account reg
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `str`, `value` FROM `%s` WHERE `type`=2 AND `account_id`='%d'", reg_db, account_id) )
			Sql_ShowDebug(sql_handle);
		break;
	case 1: //account2 reg
		ShowError("inter_accreg_fromsql: Char server shouldn't handle type 1 registry values (##). That is the login server's work!\n");
		return 0;
	default:
		ShowError("inter_accreg_fromsql: Invalid type %d\n", type);
		return 0;
	}
	for( i = 0; i < MAX_REG_NUM && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		r = &reg->reg[i];
		// str
		Sql_GetData(sql_handle, 0, &data, &len);
		memcpy(r->str, data, min(len, sizeof(r->str)));
		// value
		Sql_GetData(sql_handle, 1, &data, &len);
		memcpy(r->value, data, min(len, sizeof(r->value)));
	}
	reg->reg_num = i;
	Sql_FreeResult(sql_handle);
	return 1;
}

// Initialize
int inter_accreg_sql_init(void)
{
	CREATE(accreg_pt, struct accreg, 1);
	return 0;

}
#endif //TXT_SQL_CONVERT

/*==========================================
 * read config file
 *------------------------------------------*/
static int inter_config_read(const char* cfgName)
{
	int i;
	char line[1024], w1[1024], w2[1024];
	FILE* fp;

	fp = fopen(cfgName, "r");
	if(fp == NULL) {
		ShowError("file not found: %s\n", cfgName);
		return 1;
	}

	ShowInfo("reading file %s...\n", cfgName);

	while(fgets(line, sizeof(line), fp))
	{
		i = sscanf(line, "%[^:]: %[^\r\n]", w1, w2);
		if(i != 2)
			continue;

		if(!strcmpi(w1,"char_server_ip")) {
			strcpy(char_server_ip,w2);
			ShowStatus ("set char_server_ip : %s\n", w2);
		} else
		if(!strcmpi(w1,"char_server_port")) {
			char_server_port = atoi(w2);
			ShowStatus ("set char_server_port : %s\n", w2);
		} else
		if(!strcmpi(w1,"char_server_id")) {
			strcpy(char_server_id,w2);
			ShowStatus ("set char_server_id : %s\n", w2);
		} else
		if(!strcmpi(w1,"char_server_pw")) {
			strcpy(char_server_pw,w2);
			ShowStatus ("set char_server_pw : %s\n", w2);
		} else
		if(!strcmpi(w1,"char_server_db")) {
			strcpy(char_server_db,w2);
			ShowStatus ("set char_server_db : %s\n", w2);
		} else
		if(!strcmpi(w1,"default_codepage")) {
			strcpy(default_codepage,w2);
			ShowStatus ("set default_codepage : %s\n", w2);
		}
#ifndef TXT_SQL_CONVERT
		else if(!strcmpi(w1,"party_share_level"))
			party_share_level = (unsigned int)atof(w2);
		else if(!strcmpi(w1,"log_inter"))
			log_inter = atoi(w2);
		else if(!strcmpi(w1,"main_chat_nick"))
			safestrncpy(main_chat_nick, w2, sizeof(main_chat_nick));
#endif //TXT_SQL_CONVERT
		else if(!strcmpi(w1,"import"))
			inter_config_read(w2);
	}
	fclose(fp);

	ShowInfo ("done reading %s.\n", cfgName);

	return 0;
}
#ifndef TXT_SQL_CONVERT

// Save interlog into sql
int inter_log(char* fmt, ...)
{
	char str[255];
	char esc_str[sizeof(str)*2+1];// escaped str
	va_list ap;

	va_start(ap,fmt);
	vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);

	Sql_EscapeStringLen(sql_handle, esc_str, str, strnlen(str, sizeof(str)));
	if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`time`, `log`) VALUES (NOW(),  '%s')", interlog_db, esc_str) )
		Sql_ShowDebug(sql_handle);

	return 0;
}

#endif //TXT_SQL_CONVERT

// initialize
int inter_init_sql(const char *file)
{
	//int i;

	ShowInfo ("interserver initialize...\n");
	inter_config_read(file);

	//DB connection initialized
	sql_handle = Sql_Malloc();
	ShowInfo("Connect Character DB server.... (Character Server)\n");
	if( SQL_ERROR == Sql_Connect(sql_handle, char_server_id, char_server_pw, char_server_ip, (uint16)char_server_port, char_server_db) )
	{
		ShowError("Couldn't connect with username = '%s', password = '%s', host = '%s', port = '%d', database = '%s'\n", 
			char_server_id, char_server_pw, char_server_ip, char_server_port, char_server_db);
		Sql_ShowDebug(sql_handle);
		Sql_Free(sql_handle);
		exit(EXIT_FAILURE);
	}

	if( *default_codepage ) {
		if( SQL_ERROR == Sql_SetEncoding(sql_handle, default_codepage) )
			Sql_ShowDebug(sql_handle);
	}

	ShowStatus("Connected to main database '%s'.\n", char_server_db);
	Sql_PrintExtendedInfo(sql_handle);

#ifndef TXT_SQL_CONVERT
	wis_db = idb_alloc(DB_OPT_RELEASE_DATA);
	inter_guild_sql_init();
	inter_storage_sql_init();
	inter_party_sql_init();
	inter_pet_sql_init();
	inter_homunculus_sql_init();
	inter_mercenary_sql_init();
	inter_elemental_sql_init();
	inter_accreg_sql_init();
	inter_mail_sql_init();
	inter_auction_sql_init();
	inter_clan_init();
	inter_achievement_sql_init();

#endif //TXT_SQL_CONVERT
	return 0;
}
#ifndef TXT_SQL_CONVERT

// finalize
void inter_final(void)
{
	wis_db->destroy(wis_db, NULL);

	inter_guild_sql_final();
	inter_storage_sql_final();
	inter_party_sql_final();
	inter_pet_sql_final();
	inter_homunculus_sql_final();
	inter_mercenary_sql_final();
	inter_elemental_sql_final();
	inter_mail_sql_final();
	inter_auction_sql_final();
	inter_clan_final();
	inter_achievement_sql_final();
	
	if (accreg_pt) aFree(accreg_pt);
	return;
}

//--------------------------------------------------------

// broadcast sending
int mapif_broadcast(unsigned char *mes, int len, unsigned long fontColor, short fontType, short fontSize, short fontAlign, short fontY, int sfd)
{
	unsigned char *buf = (unsigned char*)aMalloc((len)*sizeof(unsigned char));

	if (buf == NULL) return 1;

	WBUFW(buf,0) = 0x3800;
	WBUFW(buf,2) = len;
	WBUFL(buf,4) = fontColor;
	WBUFW(buf,8) = fontType;
	WBUFW(buf,10) = fontSize;
	WBUFW(buf,12) = fontAlign;
	WBUFW(buf,14) = fontY;
	memcpy(WBUFP(buf,16), mes, len - 16);
	mapif_sendallwos(sfd, buf, len);

	aFree(buf);

	return 0;
}

// Wis sending
int mapif_wis_message(struct WisData *wd)
{
	unsigned char buf[2048];
	if (wd->len > 2047-56) wd->len = 2047-56; //Force it to fit to avoid crashes. [Skotlex]

	WBUFW(buf, 0) = 0x3801;
	WBUFW(buf, 2) = 56 +wd->len;
	WBUFL(buf, 4) = wd->id;
	memcpy(WBUFP(buf, 8), wd->src, NAME_LENGTH);
	memcpy(WBUFP(buf,32), wd->dst, NAME_LENGTH);
	memcpy(WBUFP(buf,56), wd->msg, wd->len);
	wd->count = mapif_sendall(buf,WBUFW(buf,2));

	return 0;
}

// Wis sending result
int mapif_wis_end(struct WisData *wd, int flag)
{
	unsigned char buf[27];

	WBUFW(buf, 0)=0x3802;
	memcpy(WBUFP(buf, 2),wd->src,24);
	WBUFB(buf,26)=flag;
	mapif_send(wd->fd,buf,27);
	return 0;
}

// Account registry transfer to map-server
static void mapif_account_reg(int fd, unsigned char *src)
{
	WBUFW(src,0)=0x3804; //NOTE: writing to RFIFO
	mapif_sendallwos(fd, src, WBUFW(src,2));
}

// Send the requested account_reg
int mapif_account_reg_reply(int fd,uint32 account_id,uint32 char_id, int type)
{
	struct accreg *reg=accreg_pt;
	WFIFOHEAD(fd, 13 + 5000);
	inter_accreg_fromsql(account_id,char_id,reg,type);
	
	WFIFOW(fd,0)=0x3804;
	WFIFOL(fd,4)=account_id;
	WFIFOL(fd,8)=char_id;
	WFIFOB(fd,12)=type;
	if(reg->reg_num==0){
		WFIFOW(fd,2)=13;
	}else{
		int i,p;
		for (p=13,i = 0; i < reg->reg_num && p < 5000; i++) {
			p+= sprintf((char*)WFIFOP(fd,p), "%s", reg->reg[i].str)+1; //We add 1 to consider the '\0' in place.
			p+= sprintf((char*)WFIFOP(fd,p), "%s", reg->reg[i].value)+1;
		}
		WFIFOW(fd,2)=p;
		if (p>= 5000)
			ShowWarning("Too many acc regs for %d:%d, not all values were loaded.\n", account_id, char_id);
	}
	WFIFOSET(fd,WFIFOW(fd,2));
	return 0;
}

//Request to kick char from a certain map server. [Skotlex]
int mapif_disconnectplayer(int fd, uint32 account_id, uint32 char_id, int reason)
{
	if (fd >= 0)
	{
		WFIFOHEAD(fd,7);
		WFIFOW(fd,0) = 0x2b1f;
		WFIFOL(fd,2) = account_id;
		WFIFOB(fd,6) = reason;
		WFIFOSET(fd,7);
		return 0;
	}
	return -1;
}

//--------------------------------------------------------

// Existence check of WISP data
int check_ttl_wisdata_sub(DBKey key, DBData *data, va_list ap)
{
	int64 tick;
	struct WisData *wd = db_data2ptr(data);
	tick = va_arg(ap, int64);

	if (DIFF_TICK(tick, wd->tick) > WISDATA_TTL && wis_delnum < WISDELLIST_MAX)
		wis_dellist[wis_delnum++] = wd->id;

	return 0;
}

int check_ttl_wisdata(void)
{
	int64 tick = gettick();
	int i;

	do {
		wis_delnum = 0;
		wis_db->foreach(wis_db, check_ttl_wisdata_sub, tick);
		for(i = 0; i < wis_delnum; i++) {
			struct WisData *wd = (struct WisData*)idb_get(wis_db, wis_dellist[i]);
			ShowWarning("inter: wis data id=%d time out : from %s to %s\n", wd->id, wd->src, wd->dst);
			// removed. not send information after a timeout. Just no answer for the player
			//mapif_wis_end(wd, 1); // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
			idb_remove(wis_db, wd->id);
		}
	} while(wis_delnum >= WISDELLIST_MAX);

	return 0;
}

//--------------------------------------------------------

// broadcast sending
int mapif_parse_broadcast(int fd)
{
	mapif_broadcast(RFIFOP(fd,16), RFIFOW(fd,2), RFIFOL(fd,4), RFIFOW(fd,8), RFIFOW(fd,10), RFIFOW(fd,12), RFIFOW(fd,14), fd);
	return 0;
}

/**
 * Parse received item broadcast and sends it to all connected map-serves
 * ZI 3009 <cmd>.W <len>.W <nameid>.L <source>.W <type>.B <name>.24B <srcname>.24B
 * IZ 3809 <cmd>.W <len>.W <nameid>.L <source>.W <type>.B <name>.24B <srcname>.24B
 * @param fd
 * @return
 **/
int mapif_parse_broadcast_item(int fd) {
	unsigned char buf[11 + NAME_LENGTH * 2];

	memcpy(WBUFP(buf, 0), RFIFOP(fd, 0), RFIFOW(fd, 2));
	WBUFW(buf, 0) = 0x3809;
	mapif_sendallwos(fd, buf, RFIFOW(fd, 2));

	return 0;
}

// Wisp/page request to send
int mapif_parse_WisRequest(int fd)
{
	struct WisData* wd;
	static int wisid = 0;
	char name[NAME_LENGTH];
	char esc_name[NAME_LENGTH*2+1];// escaped name
	char* data;
	size_t len;


	if ( fd <= 0 ) {return 0;} // check if we have a valid fd

	if (RFIFOW(fd,2)-52 >= sizeof(wd->msg)) {
		ShowWarning("inter: Wis message size too long.\n");
		return 0;
	} else if (RFIFOW(fd,2)-52 <= 0) { // normaly, impossible, but who knows...
		ShowError("inter: Wis message doesn't exist.\n");
		return 0;
	}
	
	safestrncpy(name, (char*)RFIFOP(fd,28), NAME_LENGTH); //Received name may be too large and not contain \0! [Skotlex]

	Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `name` FROM `%s` WHERE `name`='%s'", char_db, esc_name) )
		Sql_ShowDebug(sql_handle);

	// search if character exists before to ask all map-servers
	if( SQL_SUCCESS != Sql_NextRow(sql_handle) )
	{
		unsigned char buf[27];
		WBUFW(buf, 0) = 0x3802;
		memcpy(WBUFP(buf, 2), RFIFOP(fd, 4), NAME_LENGTH);
		WBUFB(buf,26) = 1; // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
		mapif_send(fd, buf, 27);
	}
	else
	{// Character exists. So, ask all map-servers
		// to be sure of the correct name, rewrite it
		Sql_GetData(sql_handle, 0, &data, &len);
		memset(name, 0, NAME_LENGTH);
		memcpy(name, data, min(len, NAME_LENGTH));
		// if source is destination, don't ask other servers.
		if( strncmp((const char*)RFIFOP(fd,4), name, NAME_LENGTH) == 0 )
		{
			uint8 buf[27];
			WBUFW(buf, 0) = 0x3802;
			memcpy(WBUFP(buf, 2), RFIFOP(fd, 4), NAME_LENGTH);
			WBUFB(buf,26) = 1; // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
			mapif_send(fd, buf, 27);
		}
		else
		{

			CREATE(wd, struct WisData, 1);

			// Whether the failure of previous wisp/page transmission (timeout)
			check_ttl_wisdata();

			wd->id = ++wisid;
			wd->fd = fd;
			wd->len= RFIFOW(fd,2)-52;
			memcpy(wd->src, RFIFOP(fd, 4), NAME_LENGTH);
			memcpy(wd->dst, RFIFOP(fd,28), NAME_LENGTH);
			memcpy(wd->msg, RFIFOP(fd,52), wd->len);
			wd->tick = gettick();
			idb_put(wis_db, wd->id, wd);
			mapif_wis_message(wd);
		}
	}

	Sql_FreeResult(sql_handle);
	return 0;
}


// Wisp/page transmission result
int mapif_parse_WisReply(int fd)
{
	int id, flag;
	struct WisData *wd;

	id = RFIFOL(fd,2);
	flag = RFIFOB(fd,6);
	wd = (struct WisData*)idb_get(wis_db, id);
	if (wd == NULL)
		return 0;	// This wisp was probably suppress before, because it was timeout of because of target was found on another map-server

	if ((--wd->count) <= 0 || flag != 1) {
		mapif_wis_end(wd, flag); // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
		idb_remove(wis_db, id);
	}

	return 0;
}

// Received wisp message from map-server for ALL gm (just copy the message and resends it to ALL map-servers)
int mapif_parse_WisToGM(int fd)
{
	unsigned char buf[2048]; // 0x3003/0x3803 <packet_len>.w <wispname>.24B <min_gm_level>.w <message>.?B
	
	ShowDebug("Sent packet back!\n");
	memcpy(WBUFP(buf,0), RFIFOP(fd,0), RFIFOW(fd,2));
	WBUFW(buf, 0) = 0x3803;
	mapif_sendall(buf, RFIFOW(fd,2));

	return 0;
}

// Save account_reg into sql (type=2)
int mapif_parse_Registry(int fd)
{
	int j,p,len, max;
	struct accreg *reg=accreg_pt;
	
	memset(accreg_pt,0,sizeof(struct accreg));
	switch (RFIFOB(fd, 12)) {
	case 3: //Character registry
		max = GLOBAL_REG_NUM;
	break;
	case 2: //Account Registry
		max = ACCOUNT_REG_NUM;
	break;
	case 1: //Account2 registry, must be sent over to login server.
		return save_accreg2(RFIFOP(fd,4), RFIFOW(fd,2)-4);
	default:
		return 1;
	}
	for(j=0,p=13;j<max && p<RFIFOW(fd,2);j++){
		sscanf((char*)RFIFOP(fd, p), "%31s%n", reg->reg[j].str, &len);
		reg->reg[j].str[len]='\0';
		p +=len+1; //+1 to skip the '\0' between strings.
		sscanf((char*)RFIFOP(fd, p), "%255s%n", reg->reg[j].value, &len);
		reg->reg[j].value[len]='\0';
		p +=len+1;
	}
	reg->reg_num=j;

	inter_accreg_tosql(RFIFOL(fd,4),RFIFOL(fd,8),reg, RFIFOB(fd,12));
	mapif_account_reg(fd,RFIFOP(fd,0));	// Send updated accounts to other map servers.
	return 0;
}

// Request the value of all registries.
int mapif_parse_RegistryRequest(int fd)
{
	//Load Char Registry
	if (RFIFOB(fd,12)) mapif_account_reg_reply(fd,RFIFOL(fd,2),RFIFOL(fd,6),3);
	//Load Account Registry
	if (RFIFOB(fd,11)) mapif_account_reg_reply(fd,RFIFOL(fd,2),RFIFOL(fd,6),2);
	//Ask Login Server for Account2 values.
	if (RFIFOB(fd,10)) request_accreg2(RFIFOL(fd,2),RFIFOL(fd,6));
	return 1;
}

static void mapif_namechange_ack(int fd, uint32 account_id, uint32 char_id, int type, int flag, char *name)
{
	WFIFOHEAD(fd, NAME_LENGTH+13);
	WFIFOW(fd, 0) = 0x3806;
	WFIFOL(fd, 2) = account_id;
	WFIFOL(fd, 6) = char_id;
	WFIFOB(fd,10) = type;
	WFIFOB(fd,11) = flag;
	memcpy(WFIFOP(fd, 12), name, NAME_LENGTH);
	WFIFOSET(fd, NAME_LENGTH+13);
}

int mapif_parse_NameChangeRequest(int fd)
{
	uint32 account_id, char_id, type;
	char* name;
	int i;

	account_id = RFIFOL(fd,2);
	char_id = RFIFOL(fd,6);
	type = RFIFOB(fd,10);
	name = (char*)RFIFOP(fd,11);

	// Check Authorised letters/symbols in the name
	if (char_name_option == 1) { // only letters/symbols in char_name_letters are authorised
		for (i = 0; i < NAME_LENGTH && name[i]; i++)
		if (strchr(char_name_letters, name[i]) == NULL) {
			mapif_namechange_ack(fd, account_id, char_id, type, 0, name);
			return 0;
		}
	} else if (char_name_option == 2) { // letters/symbols in char_name_letters are forbidden
		for (i = 0; i < NAME_LENGTH && name[i]; i++)
		if (strchr(char_name_letters, name[i]) != NULL) {
			mapif_namechange_ack(fd, account_id, char_id, type, 0, name);
			return 0;
		}
	}
	//TODO: type holds the type of object to rename.
	//If it were a player, it needs to have the guild information and db information
	//updated here, because changing it on the map won't make it be saved [Skotlex]

	//name allowed.
	mapif_namechange_ack(fd, account_id, char_id, type, 1, name);
	return 0;
}

// Achievement System

/**
 * Sends achievement data of a character to the map server.
 * @packet[out] 0x3810  <packet_id>.W <payload_size>.W <char_id>.L <char_achievements[]>.P
 * @param[in]  fd      socket descriptor.
 * @param[in]  char_id Character ID.
 * @param[in]  cp      Pointer to character's achievement data vector.
 */
static void mapif_send_achievements_to_map(int fd, uint32 char_id, const struct char_achievements *cp)
{
	unsigned int i = 0;
	int data_size = 0;

	nullpo_retv(cp);

	data_size = sizeof(struct achievement) * VECTOR_LENGTH(*cp);

	/* Send to the map server. */
	WFIFOHEAD(fd, (8 + data_size));
	WFIFOW(fd, 0) = 0x3810;
	WFIFOW(fd, 2) = (8 + data_size);
	WFIFOL(fd, 4) = char_id;
	for (i = 0; i < VECTOR_LENGTH(*cp); i++)
		memcpy(WFIFOP(fd, 8 + i * sizeof(struct achievement)), &VECTOR_INDEX(*cp, i), sizeof(struct achievement));
	WFIFOSET(fd, 8 + data_size);
}

/**
 * Loads achievements and sends to the map server.
 * @param[in] fd       socket descriptor
 * @param[in] char_id  character Id.
 */
static void mapif_achievement_load(int fd, uint32 char_id)
{
	struct char_achievements *cp = NULL;

	/* Ensure data exists */
	cp = idb_ensure(char_achievements, char_id, inter_achievement_ensure_char_achievements);

	/* Load storage for char-server. */
	inter_achievement_fromsql(char_id, cp);

	/* Send Achievements to map server. */
	mapif_send_achievements_to_map(fd, char_id, cp);
}

/**
 * Parse achievement load request from the map server
 * @param[in] fd  socket descriptor.
 */
static void mapif_parse_load_achievements(int fd)
{
	uint32 char_id = 0;

	/* Read received information from map-server. */
	RFIFOHEAD(fd);
	char_id = RFIFOL(fd, 2);

	/* Load and send achievements to map */
	mapif_achievement_load(fd, char_id);
}

/**
 * Handles inter-server achievement db ensuring
 * and saves current achievements to sql.
 * @param[in]  char_id      character identifier.
 * @param[out] p            pointer to character achievements vector.
 */
static void mapif_achievement_save(uint32 char_id, struct char_achievements *p)
{
	struct char_achievements *cp = NULL;

	nullpo_retv(p);

	/* Get loaded achievements. */
	cp = idb_ensure(char_achievements, char_id, inter_achievement_ensure_char_achievements);

	if (VECTOR_LENGTH(*p)) /* Save current achievements. */
		inter_achievement_tosql(char_id, cp, p);
}

/**
 * Handles achievement request and saves data from map server.
 * @packet[in] 0x3013 <packet_size>.W <char_id>.L <char_achievement>.P
 * @param[in]  fd     session socket descriptor.
 */
static void mapif_parse_save_achievements(int fd)
{
	int size = 0, char_id = 0;
	unsigned int payload_count = 0, i = 0;
	struct char_achievements p = { 0 };

	RFIFOHEAD(fd);
	size = RFIFOW(fd, 2);
	char_id = RFIFOL(fd, 4);

	payload_count = (size - 8) / sizeof(struct achievement);

	VECTOR_INIT(p);
	VECTOR_ENSURE(p, payload_count, 1);

	for (i = 0; i < payload_count; i++) {
		struct achievement ach = { 0 };
		memcpy(&ach, RFIFOP(fd, 8 + i * sizeof(struct achievement)), sizeof(struct achievement));
		VECTOR_PUSH(p, ach);
	}

	mapif_achievement_save(char_id, &p);

	VECTOR_CLEAR(p);
}

/**
  * IZ 0x388c <len>.W { <storage_table>.? }*?
  * Sends storage information to map-server
  * @param fd
  **/
void inter_Storage_sendInfo(int fd) {
	int size = sizeof(struct s_storage_table), len = 4 + int_storage_count * size, i = 0;
	// Send storage table information
	WFIFOHEAD(fd, len);
	WFIFOW(fd, 0) = 0x388c;
	WFIFOW(fd, 2) = len;
	for (i = 0; i < int_storage_count; i++) {
		if (!&int_storages[i] || !int_storages[i].name)
			continue;
		memcpy(WFIFOP(fd, 4 + size * i), &int_storages[i], size);
	}
	WFIFOSET(fd, len);
}

//--------------------------------------------------------

/// Returns the length of the next complete packet to process,
/// or 0 if no complete packet exists in the queue.
///
/// @param length The minimum allowed length, or -1 for dynamic lookup
int inter_check_length(int fd, int length)
{
	if( length == -1 )
	{// variable-length packet
		if( RFIFOREST(fd) < 4 )
			return 0;
		length = RFIFOW(fd,2);
	}

	if( (int)RFIFOREST(fd) < length )
		return 0;

	return length;
}

int inter_parse_frommap(int fd)
{
	int cmd;
	int len = 0;
	cmd = RFIFOW(fd,0);

	// Check is valid packet entry
	if (cmd < 0x3000 || cmd >= 0x3000 + ARRAYLENGTH(inter_recv_packet_length) || inter_recv_packet_length[cmd - 0x3000] == 0)
		return 0;

	// Check packet length
	if ((len = inter_check_length(fd, inter_recv_packet_length[cmd - 0x3000])) == 0)
		return 2;

#ifdef LOG_ALL_PACKETS
	ShowDebug("inter_parse_frommap: Received packet 0x%04X (length %d), session #%d\n", cmd, len, fd);
#endif

	switch(cmd) {
	case 0x3000: mapif_parse_broadcast(fd); break;
	case 0x3001: mapif_parse_WisRequest(fd); break;
	case 0x3002: mapif_parse_WisReply(fd); break;
	case 0x3003: mapif_parse_WisToGM(fd); break;
	case 0x3004: mapif_parse_Registry(fd); break;
	case 0x3005: mapif_parse_RegistryRequest(fd); break;
	case 0x3006: mapif_parse_NameChangeRequest(fd); break;
	case 0x3009: mapif_parse_broadcast_item(fd); break;
	default:
		if(  inter_party_parse_frommap(fd)
		  || inter_guild_parse_frommap(fd)
		  || inter_storage_parse_frommap(fd)
		  || inter_pet_parse_frommap(fd)
		  || inter_homunculus_parse_frommap(fd)
		  || inter_mercenary_parse_frommap(fd)
		  || inter_elemental_parse_frommap(fd)
		  || inter_mail_parse_frommap(fd)
		  || inter_auction_parse_frommap(fd)
		  || inter_quest_parse_frommap(fd)
		  || inter_clan_parse_frommap(fd)
		  || inter_achievement_parse_frommap(fd)
		   )
			break;
		else
			return 0;
	}

	RFIFOSKIP( fd, len );
	return 1;
}

int inter_mapif_init(int fd)
{
	CREATE(int_storages, struct s_storage_table, 2);
	
	int_storages[0].id = 0;
	safestrncpy(int_storages[0].name, "Storage", NAME_LENGTH);
	safestrncpy(int_storages[0].table, storage_db, 512);

	int_storages[1].id = 1;
	safestrncpy(int_storages[1].name, "Storage", NAME_LENGTH);
	safestrncpy(int_storages[1].table, storage2_db, 512);

	int_storage_count=2;

	inter_Storage_sendInfo(fd);
	return 0;
}

#endif //TXT_SQL_CONVERT
