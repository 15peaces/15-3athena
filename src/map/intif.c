// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "map.h"
#include "battle.h"
#include "chrif.h"
#include "clan.h"
#include "clif.h"
#include "pc.h"
#include "intif.h"
#include "log.h"
#include "storage.h"
#include "party.h"
#include "guild.h"
#include "pet.h"
#include "atcommand.h"
#include "mercenary.h"
#include "elemental.h"
#include "homunculus.h"
#include "mail.h"
#include "quest.h"
#include "status.h"
#include "achievement.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>


/// Received packet Lengths from inter-server
static const int packet_len_table[] = {
	-1,-1,27,-1, -1, 0,37, 0,  0,-1, 0, 0,  0, 0,  0, 0, //0x3800-0x380f
	 0, 0, 0, 0,  0, 0, 0, 0, -1,11, 0, 0,  0, 0,  0, 0, //0x3810
	39,-1,15,15,15+NAME_LENGTH,21, 7,-1,  0, 0, 0, 0,  0, 0,  0, 0, //0x3820
	10,-1,15, 0, 79,23, 7,-1,  0,-1,-1,-1, 14,67,186,-1, //0x3830
	-1, 0, 0,18,  0, 0, 0, 0, -1,75,-1,11, 11,-1, 38, 0, //0x3840
	-1,-1, 7, 7,  7,11, 8,-1,  0, 0, 0, 0,  0, 0,  0, 0, //0x3850  Auctions [Zephyrus] itembound[Akinari] 
	-1, 7,-1, 0, 14, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0, //0x3860  Quests [Kevin] [Inkfish] / Achievements [Aleos]
	-1, 3, 3, 0,  0, 0, 0, 0, -1, 3, 3, 0,  0, 0,  0, 0, //0x3870  Mercenaries [Zephyrus] Elementals [Rytech]
	12,-1, 7, 3,  0, 0, 0, 0,  0, 0,-1, 9, -1, 0,  0, 0, //0x3880  Pet System,  Storages
	-1,-1, 7, 3,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0, //0x3890  Homunculus [albator]
	-1,-1, 8, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0, //0x38A0  Clans
};

extern int char_fd;		// inter serverのfdはchar_fdを使う
#define inter_fd char_fd	// エイリアス

//-----------------------------------------------------------------
// inter serverへの送信


/// Returns true if not connected to the char-server.
bool CheckForCharServer(void)
{
	return ((char_fd <= 0) || session[char_fd] == NULL || session[char_fd]->wdata == NULL);
}

/**
 * Get sd from pc_db (map_id2db) or auth_db (in case if parsing packet from inter-server when sd not added to pc_db yet)
 * @param account_id
 * @param char_id
 * @return sd Found sd or NULL if not found
 */
struct map_session_data *inter_search_sd(uint32 account_id, uint32 char_id)
{
	struct map_session_data *sd = NULL;
	struct auth_node *node = chrif_auth_check(account_id, char_id, ST_LOGIN);
	if (node)
		sd = node->sd;
	else
		sd = map_id2sd(account_id);
	return sd;
}

// pet
int intif_create_pet(uint32 account_id, uint32 char_id, short pet_class, short pet_lv, t_itemid pet_egg_id, t_itemid pet_equip, short intimate, short hungry, char rename_flag, char incubate, char *pet_name)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 28 + NAME_LENGTH);
	WFIFOW(inter_fd, 0) = 0x3080;
	WFIFOL(inter_fd, 2) = account_id;
	WFIFOL(inter_fd, 6) = char_id;
	WFIFOW(inter_fd, 10) = pet_class;
	WFIFOW(inter_fd, 12) = pet_lv;
	WFIFOL(inter_fd, 14) = pet_egg_id;
	WFIFOL(inter_fd, 18) = pet_equip;
	WFIFOW(inter_fd, 22) = intimate;
	WFIFOW(inter_fd, 24) = hungry;
	WFIFOB(inter_fd, 26) = rename_flag;
	WFIFOB(inter_fd, 27) = incubate;
	memcpy(WFIFOP(inter_fd, 28), pet_name, NAME_LENGTH);
	WFIFOSET(inter_fd, 28 + NAME_LENGTH);

	return 0;
}

int intif_request_petdata(uint32 account_id,uint32 char_id,int pet_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 14);
	WFIFOW(inter_fd,0) = 0x3081;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOL(inter_fd,10) = pet_id;
	WFIFOSET(inter_fd,14);

	return 0;
}

int intif_save_petdata(uint32 account_id,struct s_pet *p)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, sizeof(struct s_pet) + 8);
	WFIFOW(inter_fd,0) = 0x3082;
	WFIFOW(inter_fd,2) = sizeof(struct s_pet) + 8;
	WFIFOL(inter_fd,4) = account_id;
	memcpy(WFIFOP(inter_fd,8),p,sizeof(struct s_pet));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));

	return 0;
}

int intif_delete_petdata(int pet_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3083;
	WFIFOL(inter_fd,2) = pet_id;
	WFIFOSET(inter_fd,6);

	return 1;
}

int intif_rename(struct map_session_data *sd, int type, char *name)
{
	if (CheckForCharServer())
		return 1;

	WFIFOHEAD(inter_fd,NAME_LENGTH+12);
	WFIFOW(inter_fd,0) = 0x3006;
	WFIFOL(inter_fd,2) = sd->status.account_id;
	WFIFOL(inter_fd,6) = sd->status.char_id;
	WFIFOB(inter_fd,10) = type;  //Type: 0 - PC, 1 - PET, 2 - HOM
	memcpy(WFIFOP(inter_fd,11),name, NAME_LENGTH);
	WFIFOSET(inter_fd,NAME_LENGTH+12);
	return 0;
}

// GM Send a message
int intif_broadcast(const char* mes, int len, int type)
{
	int lp = (type | BC_COLOR_MASK) ? 4 : 0;

	// Send to the local players
	clif_broadcast(NULL, mes, len, type, ALL_CLIENT);

	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, 16 + lp + len);
	WFIFOW(inter_fd,0)  = 0x3000;
	WFIFOW(inter_fd,2)  = 16 + lp + len;
	WFIFOL(inter_fd,4)  = 0xFF000000; // 0xFF000000 color signals standard broadcast
	WFIFOW(inter_fd,8)  = 0; // fontType not used with standard broadcast
	WFIFOW(inter_fd,10) = 0; // fontSize not used with standard broadcast
	WFIFOW(inter_fd,12) = 0; // fontAlign not used with standard broadcast
	WFIFOW(inter_fd,14) = 0; // fontY not used with standard broadcast
	if (type | BC_BLUE)
		WFIFOL(inter_fd,16) = 0x65756c62; //If there's "blue" at the beginning of the message, game client will display it in blue instead of yellow.
	else if (type | BC_WOE)
		WFIFOL(inter_fd,16) = 0x73737373; //If there's "ssss", game client will recognize message as 'WoE broadcast'.
	memcpy(WFIFOP(inter_fd,16 + lp), mes, len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));
	return 0;
}

int intif_broadcast2(const char* mes, int len, unsigned long fontColor, short fontType, short fontSize, short fontAlign, short fontY)
{
	// Send to the local players
	if (fontColor == 0xFE000000) // This is main chat message [LuzZza]
		clif_MainChatMessage(mes);
	else
		clif_broadcast2(NULL, mes, len, fontColor, fontType, fontSize, fontAlign, fontY, ALL_CLIENT);

	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, 16 + len);
	WFIFOW(inter_fd,0)  = 0x3000;
	WFIFOW(inter_fd,2)  = 16 + len;
	WFIFOL(inter_fd,4)  = fontColor;
	WFIFOW(inter_fd,8)  = fontType;
	WFIFOW(inter_fd,10) = fontSize;
	WFIFOW(inter_fd,12) = fontAlign;
	WFIFOW(inter_fd,14) = fontY;
	memcpy(WFIFOP(inter_fd,16), mes, len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));
	return 0;
}

/// send a message using the main chat system
/// <sd>         the source of message
/// <message>    the message that was sent
int intif_main_message(struct map_session_data* sd, const char* message)
{
	char output[256];

	nullpo_ret(sd);

	// format the message for main broadcasting
	snprintf(output, sizeof(output), msg_txt(sd,386), sd->status.name, message);

	// send the message using the inter-server broadcast service
	intif_broadcast2(output, strlen(output) + 1, 0xFE000000, 0, 0, 0, 0);

	// log the chat message
	log_chat(LOG_CHAT_MAINCHAT, 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, NULL, message);

	return 0;
}

// The transmission of Wisp/Page to inter-server (player not found on this server)
int intif_wis_message(struct map_session_data *sd, char *nick, char *mes, int mes_len)
{
	nullpo_ret(sd);
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
	{	//Character not found.
		clif_wis_end(sd->fd, 1);
		return 0;
	}

	WFIFOHEAD(inter_fd,mes_len + 52);
	WFIFOW(inter_fd,0) = 0x3001;
	WFIFOW(inter_fd,2) = mes_len + 52;
	memcpy(WFIFOP(inter_fd,4), sd->status.name, NAME_LENGTH);
	memcpy(WFIFOP(inter_fd,4+NAME_LENGTH), nick, NAME_LENGTH);
	memcpy(WFIFOP(inter_fd,4+2*NAME_LENGTH), mes, mes_len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));

	if (battle_config.etc_log)
		ShowInfo("intif_wis_message from %s to %s (message: '%s')\n", sd->status.name, nick, mes);

	return 0;
}

// The reply of Wisp/page
int intif_wis_replay(int id, int flag)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,7);
	WFIFOW(inter_fd,0) = 0x3002;
	WFIFOL(inter_fd,2) = id;
	WFIFOB(inter_fd,6) = flag; // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
	WFIFOSET(inter_fd,7);

	if (battle_config.etc_log)
		ShowInfo("intif_wis_replay: id: %d, flag:%d\n", id, flag);

	return 0;
}

// The transmission of GM only Wisp/Page from server to inter-server
int intif_wis_message_to_gm(char *Wisp_name, int min_gm_level, char *mes)
{
	int mes_len;
	if (CheckForCharServer())
		return 0;
	mes_len = strlen(mes) + 1; // + null
	WFIFOHEAD(inter_fd, mes_len + 30);
	WFIFOW(inter_fd,0) = 0x3003;
	WFIFOW(inter_fd,2) = mes_len + 30;
	memcpy(WFIFOP(inter_fd,4), Wisp_name, NAME_LENGTH);
	WFIFOW(inter_fd,4+NAME_LENGTH) = (short)min_gm_level;
	memcpy(WFIFOP(inter_fd,6+NAME_LENGTH), mes, mes_len);
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));

	if (battle_config.etc_log)
		ShowNotice("intif_wis_message_to_gm: from: '%s', min level: %d, message: '%s'.\n", Wisp_name, min_gm_level, mes);

	return 0;
}

int intif_regtostr(char* str, struct global_reg *reg, int qty)
{
	int len =0, i;

	for (i = 0; i < qty; i++) {
		len+= sprintf(str+len, "%s", reg[i].str)+1; //We add 1 to consider the '\0' in place.
		len+= sprintf(str+len, "%s", reg[i].value)+1;
	}
	return len;
}

//Request for saving registry values.
int intif_saveregistry(struct map_session_data *sd, int type)
{
	struct global_reg *reg;
	int count;
	int i, p;

	if (CheckForCharServer())
		return -1;

	switch (type) {
	case 3: //Character reg
		reg = sd->save_reg.global;
		count = sd->save_reg.global_num;
		sd->state.reg_dirty &= ~0x4;
	break;
	case 2: //Account reg
		reg = sd->save_reg.account;
		count = sd->save_reg.account_num;
		sd->state.reg_dirty &= ~0x2;
	break;
	case 1: //Account2 reg
		reg = sd->save_reg.account2;
		count = sd->save_reg.account2_num;
		sd->state.reg_dirty &= ~0x1;
	break;
	default: //Broken code?
		ShowError("intif_saveregistry: Invalid type %d\n", type);
		return -1;
	}
	WFIFOHEAD(inter_fd, 288 * MAX_REG_NUM+13);
	WFIFOW(inter_fd,0)=0x3004;
	WFIFOL(inter_fd,4)=sd->status.account_id;
	WFIFOL(inter_fd,8)=sd->status.char_id;
	WFIFOB(inter_fd,12)=type;
	for( p = 13, i = 0; i < count; i++ ) {
		if (reg[i].str[0] != '\0' && reg[i].value[0] != '\0') {
			p+= sprintf((char*)WFIFOP(inter_fd,p), "%s", reg[i].str)+1; //We add 1 to consider the '\0' in place.
			p+= sprintf((char*)WFIFOP(inter_fd,p), "%s", reg[i].value)+1;
		}
	}
	WFIFOW(inter_fd,2)=p;
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}

//Request the registries for this player.
int intif_request_registry(struct map_session_data *sd, int flag)
{
	nullpo_ret(sd);

	sd->save_reg.account2_num = -1;
	sd->save_reg.account_num = -1;
	sd->save_reg.global_num = -1;

	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3005;
	WFIFOL(inter_fd,2) = sd->status.account_id;
	WFIFOL(inter_fd,6) = sd->status.char_id;
	WFIFOB(inter_fd,10) = (flag&1?1:0); //Request Acc Reg 2
	WFIFOB(inter_fd,11) = (flag&2?1:0); //Request Acc Reg
	WFIFOB(inter_fd,12) = (flag&4?1:0); //Request Char Reg
	WFIFOSET(inter_fd,13);

	return 0;
}

/**
 * Request to load guild storage from char-serv
 * @param account_id: Player account identification
 * @param guild_id: Guild of player
 * @return false - error, true - message sent
 */
bool intif_request_guild_storage(uint32 account_id, int guild_id)
{
	if (CheckForCharServer())
		return false;
	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x3018;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOL(inter_fd,6) = guild_id;
	WFIFOSET(inter_fd,10);
	return true;
}

/**
 * Request to save guild storage
 * @param account_id: account requesting the save
 * @param gstor: Guild storage struct to save
 * @return false - error, true - message sent
 */
bool intif_send_guild_storage(uint32 account_id,struct s_storage *gstor)
{
	if (CheckForCharServer())
		return false;
	WFIFOHEAD(inter_fd,sizeof(struct s_storage)+12);
	WFIFOW(inter_fd,0) = 0x3019;
	WFIFOW(inter_fd,2) = (unsigned short)sizeof(struct s_storage)+12;
	WFIFOL(inter_fd,4) = account_id;
	WFIFOL(inter_fd,8) = gstor->id;
	memcpy( WFIFOP(inter_fd,12),gstor, sizeof(struct s_storage) );
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return true;
}

/// Create a party.
/// Returns true if the request is sent.
bool intif_create_party(struct party_member* member, const char* name, int item, int item2) {
	if (CheckForCharServer())
		return false;
	nullpo_retr(false, member);

	WFIFOHEAD(inter_fd, 6+NAME_LENGTH+sizeof(struct party_member));
	WFIFOW(inter_fd,0) = 0x3020;
	WFIFOW(inter_fd,2) = 6+NAME_LENGTH+sizeof(struct party_member);
	memcpy(WFIFOP(inter_fd,4),name, NAME_LENGTH);
	WFIFOB(inter_fd,28) = item;
	WFIFOB(inter_fd,29) = item2;
	memcpy(WFIFOP(inter_fd,30), member, sizeof(struct party_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd, 2));
	return true;
}
// パーティ情報要求
int intif_request_partyinfo(int party_id, uint32 char_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x3021;
	WFIFOL(inter_fd,2) = party_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOSET(inter_fd,10);
	return 0;
}
// パーティ追加要求
int intif_party_addmember(int party_id,struct party_member *member)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,42);
	WFIFOW(inter_fd,0)=0x3022;
	WFIFOW(inter_fd,2)=8+sizeof(struct party_member);
	WFIFOL(inter_fd,4)=party_id;
	memcpy(WFIFOP(inter_fd,8),member,sizeof(struct party_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd, 2));
	return 1;
}
// パーティ設定変更
int intif_party_changeoption(int party_id,uint32 account_id,int exp,int item)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,14);
	WFIFOW(inter_fd,0)=0x3023;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOL(inter_fd,6)=account_id;
	WFIFOW(inter_fd,10)=exp;
	WFIFOW(inter_fd,12)=item;
	WFIFOSET(inter_fd,14);
	return 0;
}
// Ask the char-serv to make aid,cid quit party
int intif_party_leave(int party_id, uint32 account_id, uint32 char_id, char *name, enum e_party_member_withdraw type) {
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,15+NAME_LENGTH);
	WFIFOW(inter_fd,0) = 0x3024;
	WFIFOL(inter_fd,2) = party_id;
	WFIFOL(inter_fd,6) = account_id;
	WFIFOL(inter_fd,10) = char_id;
	memcpy((char *)WFIFOP(inter_fd,14), name, NAME_LENGTH);
	WFIFOB(inter_fd,14+NAME_LENGTH) = type;
	WFIFOSET(inter_fd,15+NAME_LENGTH);
	return 0;
}
// パーティ移動要求
int intif_party_changemap(struct map_session_data *sd,int online)
{
	int m, mapindex;
	
	if (CheckForCharServer())
		return 0;
	if(!sd)
		return 0;

	if( (m=map_mapindex2mapid(sd->mapindex)) >= 0 && map[m].instance_id )
		mapindex = map[map[m].instance_src_map].index;
	else
		mapindex = sd->mapindex;

	WFIFOHEAD(inter_fd,19);
	WFIFOW(inter_fd,0)=0x3025;
	WFIFOL(inter_fd,2)=sd->status.party_id;
	WFIFOL(inter_fd,6)=sd->status.account_id;
	WFIFOL(inter_fd,10)=sd->status.char_id;
	WFIFOW(inter_fd,14)=mapindex;
	WFIFOB(inter_fd,16)=online;
	WFIFOW(inter_fd,17)=sd->status.base_level;
	WFIFOW(inter_fd, 19) = sd->status.class_;
	WFIFOSET(inter_fd, 21);
	return 1;
}
// パーティー解散要求
int intif_break_party(int party_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0)=0x3026;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// パーティ会話送信
int intif_party_message(int party_id,uint32 account_id,const char *mes,int len)
{
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd,len + 12);
	WFIFOW(inter_fd,0)=0x3027;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=party_id;
	WFIFOL(inter_fd,8)=account_id;
	memcpy(WFIFOP(inter_fd,12),mes,len);
	WFIFOSET(inter_fd,len+12);
	return 0;
}

int intif_party_leaderchange(int party_id,uint32 account_id,uint32 char_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,14);
	WFIFOW(inter_fd,0)=0x3029;
	WFIFOL(inter_fd,2)=party_id;
	WFIFOL(inter_fd,6)=account_id;
	WFIFOL(inter_fd,10)=char_id;
	WFIFOSET(inter_fd,14);
	return 0;
}


// ギルド作成要求
int intif_guild_create(const char *name,const struct guild_member *master)
{
	if (CheckForCharServer())
		return 0;
	nullpo_ret(master);

	WFIFOHEAD(inter_fd,sizeof(struct guild_member)+(8+NAME_LENGTH));
	WFIFOW(inter_fd,0)=0x3030;
	WFIFOW(inter_fd,2)=sizeof(struct guild_member)+(8+NAME_LENGTH);
	WFIFOL(inter_fd,4)=master->account_id;
	memcpy(WFIFOP(inter_fd,8),name,NAME_LENGTH);
	memcpy(WFIFOP(inter_fd,8+NAME_LENGTH),master,sizeof(struct guild_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}
// ギルド情報要求
int intif_guild_request_info(int guild_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3031;
	WFIFOL(inter_fd,2) = guild_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// ギルドメンバ追加要求
int intif_guild_addmember(int guild_id,struct guild_member *m)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,sizeof(struct guild_member)+8);
	WFIFOW(inter_fd,0) = 0x3032;
	WFIFOW(inter_fd,2) = sizeof(struct guild_member)+8;
	WFIFOL(inter_fd,4) = guild_id;
	memcpy(WFIFOP(inter_fd,8),m,sizeof(struct guild_member));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}

int intif_guild_change_gm(int guild_id, const char* name, int len)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, len + 8);
	WFIFOW(inter_fd, 0)=0x3033;
	WFIFOW(inter_fd, 2)=len+8;
	WFIFOL(inter_fd, 4)=guild_id;
	memcpy(WFIFOP(inter_fd,8),name,len);
	WFIFOSET(inter_fd,len+8);
	return 0;
}

// ギルドメンバ脱退/追放要求
int intif_guild_leave(int guild_id,uint32 account_id,uint32 char_id,int flag,const char *mes)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 55);
	WFIFOW(inter_fd, 0) = 0x3034;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOL(inter_fd, 6) = account_id;
	WFIFOL(inter_fd,10) = char_id;
	WFIFOB(inter_fd,14) = flag;
	safestrncpy((char*)WFIFOP(inter_fd,15),mes,40);
	WFIFOSET(inter_fd,55);
	return 0;
}
// ギルドメンバのオンライン状況/Lv更新要求
int intif_guild_memberinfoshort(int guild_id,uint32 account_id,uint32 char_id,int online,int lv,int class_)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 19);
	WFIFOW(inter_fd, 0) = 0x3035;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOL(inter_fd, 6) = account_id;
	WFIFOL(inter_fd,10) = char_id;
	WFIFOB(inter_fd,14) = online;
	WFIFOW(inter_fd,15) = lv;
	WFIFOW(inter_fd,17) = class_;
	WFIFOSET(inter_fd,19);
	return 0;
}
// ギルド解散通知
int intif_guild_break(int guild_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 6);
	WFIFOW(inter_fd, 0) = 0x3036;
	WFIFOL(inter_fd, 2) = guild_id;
	WFIFOSET(inter_fd,6);
	return 0;
}
// ギルド会話送信
int intif_guild_message(int guild_id,uint32 account_id,const char *mes,int len)
{
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, len + 12);
	WFIFOW(inter_fd,0)=0x3037;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=account_id;
	memcpy(WFIFOP(inter_fd,12),mes,len);
	WFIFOSET(inter_fd,len+12);

	return 0;
}
// ギルド基本情報変更要求
int intif_guild_change_basicinfo(int guild_id,int type,const void* data,int len)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, len + 10);
	WFIFOW(inter_fd,0)=0x3039;
	WFIFOW(inter_fd,2)=len+10;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOW(inter_fd,8)=type;
	memcpy(WFIFOP(inter_fd,10),data,len);
	WFIFOSET(inter_fd,len+10);
	return 0;
}
// ギルドメンバ情報変更要求
int intif_guild_change_memberinfo(int guild_id,uint32 account_id,uint32 char_id,
	int type,const void* data,int len)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, len + 18);
	WFIFOW(inter_fd, 0)=0x303a;
	WFIFOW(inter_fd, 2)=len+18;
	WFIFOL(inter_fd, 4)=guild_id;
	WFIFOL(inter_fd, 8)=account_id;
	WFIFOL(inter_fd,12)=char_id;
	WFIFOW(inter_fd,16)=type;
	memcpy(WFIFOP(inter_fd,18),data,len);
	WFIFOSET(inter_fd,len+18);
	return 0;
}
// ギルド役職変更要求
int intif_guild_position(int guild_id,int idx,struct guild_position *p)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, sizeof(struct guild_position)+12);
	WFIFOW(inter_fd,0)=0x303b;
	WFIFOW(inter_fd,2)=sizeof(struct guild_position)+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=idx;
	memcpy(WFIFOP(inter_fd,12),p,sizeof(struct guild_position));
	WFIFOSET(inter_fd,WFIFOW(inter_fd,2));
	return 0;
}
// ギルドスキルアップ要求
int intif_guild_skillup(int guild_id, int skill_id, uint32 account_id, int max)
{
	if( CheckForCharServer() )
		return 0;
	WFIFOHEAD(inter_fd, 18);
	WFIFOW(inter_fd, 0)  = 0x303c;
	WFIFOL(inter_fd, 2)  = guild_id;
	WFIFOL(inter_fd, 6)  = skill_id;
	WFIFOL(inter_fd, 10) = account_id;
	WFIFOL(inter_fd, 14) = max;
	WFIFOSET(inter_fd, 18);
	return 0;
}
// ギルド同盟/敵対要求
int intif_guild_alliance(int guild_id1,int guild_id2,int account_id1,int account_id2,int flag)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,19);
	WFIFOW(inter_fd, 0)=0x303d;
	WFIFOL(inter_fd, 2)=guild_id1;
	WFIFOL(inter_fd, 6)=guild_id2;
	WFIFOL(inter_fd,10)=account_id1;
	WFIFOL(inter_fd,14)=account_id2;
	WFIFOB(inter_fd,18)=flag;
	WFIFOSET(inter_fd,19);
	return 0;
}
// ギルド告知変更要求
int intif_guild_notice(int guild_id,const char *mes1,const char *mes2)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,186);
	WFIFOW(inter_fd,0)=0x303e;
	WFIFOL(inter_fd,2)=guild_id;
	memcpy(WFIFOP(inter_fd,6),mes1,MAX_GUILDMES1);
	memcpy(WFIFOP(inter_fd,66),mes2,MAX_GUILDMES2);
	WFIFOSET(inter_fd,186);
	return 0;
}
// ギルドエンブレム変更要求
int intif_guild_emblem(int guild_id,int len,const char *data)
{
	if (CheckForCharServer())
		return 0;
	if(guild_id<=0 || len<0 || len>2000)
		return 0;
	WFIFOHEAD(inter_fd,len + 12);
	WFIFOW(inter_fd,0)=0x303f;
	WFIFOW(inter_fd,2)=len+12;
	WFIFOL(inter_fd,4)=guild_id;
	WFIFOL(inter_fd,8)=0;
	memcpy(WFIFOP(inter_fd,12),data,len);
	WFIFOSET(inter_fd,len+12);
	return 0;
}

/**
 * Requests guild castles data from char-server.
 * @param num Number of castles, size of castle_ids array.
 * @param castle_ids Pointer to array of castle IDs.
 */
int intif_guild_castle_dataload(int num, int *castle_ids)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 4 + num * sizeof(int));
	WFIFOW(inter_fd, 0) = 0x3040;
	WFIFOW(inter_fd, 2) = 4 + num * sizeof(int);
	memcpy(WFIFOP(inter_fd, 4), castle_ids, num * sizeof(int));
	WFIFOSET(inter_fd, WFIFOW(inter_fd, 2));
	return 1;
}

//ギルド城占領ギルド変更要求
int intif_guild_castle_datasave(int castle_id,int index, int value)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd,9);
	WFIFOW(inter_fd,0)=0x3041;
	WFIFOW(inter_fd,2)=castle_id;
	WFIFOB(inter_fd,4)=index;
	WFIFOL(inter_fd,5)=value;
	WFIFOSET(inter_fd,9);
	return 1;
}

//-----------------------------------------------------------------
// Homunculus Packets send to Inter server [albator]
//-----------------------------------------------------------------

int intif_homunculus_create(uint32 account_id, struct s_homunculus *sh)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, sizeof(struct s_homunculus)+8);
	WFIFOW(inter_fd,0) = 0x3090;
	WFIFOW(inter_fd,2) = sizeof(struct s_homunculus)+8;
	WFIFOL(inter_fd,4) = account_id;
	memcpy(WFIFOP(inter_fd,8),sh,sizeof(struct s_homunculus));
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));
	return 0;
}

int intif_homunculus_requestload(uint32 account_id, int homun_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 10);
	WFIFOW(inter_fd,0) = 0x3091;
	WFIFOL(inter_fd,2) = account_id;
	WFIFOL(inter_fd,6) = homun_id;
	WFIFOSET(inter_fd, 10);
	return 1;
}

int intif_homunculus_requestsave(uint32 account_id, struct s_homunculus* sh)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, sizeof(struct s_homunculus)+8);
	WFIFOW(inter_fd,0) = 0x3092;
	WFIFOW(inter_fd,2) = sizeof(struct s_homunculus)+8;
	WFIFOL(inter_fd,4) = account_id;
	memcpy(WFIFOP(inter_fd,8),sh,sizeof(struct s_homunculus));
	WFIFOSET(inter_fd, WFIFOW(inter_fd,2));
	return 0;

}

int intif_homunculus_requestdelete(int homun_id)
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 6);
	WFIFOW(inter_fd, 0) = 0x3093;
	WFIFOL(inter_fd,2) = homun_id;
	WFIFOSET(inter_fd,6);
	return 0;

}


//-----------------------------------------------------------------
// Packets receive from inter server

// Wisp/Page reception // rewritten by [Yor]
int intif_parse_WisMessage(int fd)
{ 
	struct map_session_data* sd;
	char *wisp_source;
	char name[NAME_LENGTH];
	int id, i;

	id=RFIFOL(fd,4);

	safestrncpy(name, (char*)RFIFOP(fd,32), NAME_LENGTH);
	sd = map_nick2sd(name);
	if(sd == NULL || strcmp(sd->status.name, name) != 0)
	{	//Not found
		intif_wis_replay(id,1);
		return 0;
	}
	if(sd->state.ignoreAll) {
		intif_wis_replay(id, 2);
		return 0;
	}
	wisp_source = (char *) RFIFOP(fd,8); // speed up [Yor]
	for(i=0; i < MAX_IGNORE_LIST &&
		sd->ignore[i].name[0] != '\0' &&
		strcmp(sd->ignore[i].name, wisp_source) != 0
		; i++);
	
	if (i < MAX_IGNORE_LIST && sd->ignore[i].name[0] != '\0')
	{	//Ignored
		intif_wis_replay(id, 2);
		return 0;
	}
	//Success to send whisper.
	clif_wis_message(sd->fd, wisp_source, (char*)RFIFOP(fd,56),RFIFOW(fd,2)-56);
	intif_wis_replay(id,0);   // 送信成功
	return 0;
}

// Wisp/page transmission result reception
int intif_parse_WisEnd(int fd)
{
	struct map_session_data* sd;

	if (battle_config.etc_log)
		ShowInfo("intif_parse_wisend: player: %s, flag: %d\n", RFIFOP(fd,2), RFIFOB(fd,26)); // flag: 0: success to send wisper, 1: target character is not loged in?, 2: ignored by target
	sd = (struct map_session_data *)map_nick2sd((char *) RFIFOP(fd,2));
	if (sd != NULL)
		clif_wis_end(sd->fd, RFIFOB(fd,26));

	return 0;
}

static int mapif_parse_WisToGM_sub(struct map_session_data* sd,va_list va)
{
	int min_gm_level = va_arg(va, int);
	char *wisp_name;
	char *message;
	int len;
	if (pc_isGM(sd) < min_gm_level) return 0;
	wisp_name = va_arg(va, char*);
	message = va_arg(va, char*);
	len = va_arg(va, int);
	clif_wis_message(sd->fd, wisp_name, message, len);
	return 1;
}

// Received wisp message from map-server via char-server for ALL gm
// 0x3003/0x3803 <packet_len>.w <wispname>.24B <min_gm_level>.w <message>.?B
int mapif_parse_WisToGM(int fd)
{
	int min_gm_level, mes_len;
	char Wisp_name[NAME_LENGTH];
	char *message;

	mes_len =  RFIFOW(fd,2) - 30;
	message = (char *)aMalloc(mes_len);

	min_gm_level = (int)RFIFOW(fd,28);
	safestrncpy(Wisp_name, (char*)RFIFOP(fd,4), NAME_LENGTH);
	safestrncpy(message, (char*)RFIFOP(fd,30), mes_len);
	// information is sended to all online GM
	map_foreachpc(mapif_parse_WisToGM_sub, min_gm_level, Wisp_name, message, mes_len);

	aFree(message);

	return 0;
}

// アカウント変数通知
int intif_parse_Registers(int fd)
{
	int j,p,len,max, flag;
	struct map_session_data *sd;
	struct global_reg *reg;
	int *qty;
	uint32 account_id = RFIFOL(fd,4), char_id = RFIFOL(fd,8);
	struct auth_node *node = chrif_auth_check(account_id, char_id, ST_LOGIN);
	if (node)
		sd = node->sd;
	else { //Normally registries should arrive for in log-in chars.
		sd = map_id2sd(account_id);
		if (sd && RFIFOB(fd,12) == 3 && sd->status.char_id != char_id)
			sd = NULL; //Character registry from another character.
	}
	if (!sd) return 1;

	flag = (sd->save_reg.global_num == -1 || sd->save_reg.account_num == -1 || sd->save_reg.account2_num == -1);

	switch (RFIFOB(fd,12)) {
		case 3: //Character Registry
			reg = sd->save_reg.global;
			qty = &sd->save_reg.global_num;
			max = GLOBAL_REG_NUM;
		break;
		case 2: //Account Registry
			reg = sd->save_reg.account;
			qty = &sd->save_reg.account_num;
			max = ACCOUNT_REG_NUM;
		break;
		case 1: //Account2 Registry
			reg = sd->save_reg.account2;
			qty = &sd->save_reg.account2_num;
			max = ACCOUNT_REG2_NUM;
		break;
		default:
			ShowError("intif_parse_Registers: Unrecognized type %d\n",RFIFOB(fd,12));
			return 0;
	}
	for(j=0,p=13;j<max && p<RFIFOW(fd,2);j++){
		sscanf((char*)RFIFOP(fd, p), "%31s%n", reg[j].str, &len);
		reg[j].str[len]='\0';
		p += len+1; //+1 to skip the '\0' between strings.
		sscanf((char*)RFIFOP(fd, p), "%255s%n", reg[j].value, &len);
		reg[j].value[len]='\0';
		p += len+1;
	}
	*qty = j;

	if (flag && sd->save_reg.global_num > -1 && sd->save_reg.account_num > -1 && sd->save_reg.account2_num > -1)
		pc_reg_received(sd); //Received all registry values, execute init scripts and what-not. [Skotlex]
	return 1;
}

int intif_parse_LoadGuildStorage(int fd)
{
	struct s_storage *gstor;
	struct map_session_data *sd;
	int guild_id, flag;
	
	guild_id = RFIFOL(fd,8);
	flag = RFIFOL(fd,12);
	if(guild_id <= 0)
		return 1;

	sd=map_id2sd( RFIFOL(fd,4) );
	if( flag ){ //If flag != 0, we attach a player and open the storage 
		if(sd==NULL){ 
			ShowError("intif_parse_LoadGuildStorage: user not found %d\n",RFIFOL(fd,4)); 
			return 1;
		}
	} 
	gstor=guild2storage(guild_id);
	if(!gstor) {
		ShowWarning("intif_parse_LoadGuildStorage: error guild_id %d not exist\n",guild_id);
		return 1;
	}
	if (gstor->status) { // Already open.. lets ignore this update
		ShowWarning("intif_parse_LoadGuildStorage: storage received for a client already open (User %d:%d)\n", flag?sd->status.account_id:1, flag?sd->status.char_id:1);
		return 1;
	}
	if (gstor->dirty) { // Already have storage, and it has been modified and not saved yet! Exploit! [Skotlex]
		ShowWarning("intif_parse_LoadGuildStorage: received storage for an already modified non-saved storage! (User %d:%d)\n", flag?sd->status.account_id:1, flag?sd->status.char_id:1);
		return 1;
	}
	if( RFIFOW(fd,2)-13 != sizeof(struct s_storage) ){
		ShowError("intif_parse_LoadGuildStorage: data size error %d %d\n",RFIFOW(fd,2)-13 , sizeof(struct s_storage));
		gstor->status = false;
		return 1;
	}
	if(battle_config.save_log)
		ShowInfo("intif_open_guild_storage: %d\n",RFIFOL(fd,4) );
	memcpy(gstor,RFIFOP(fd,13),sizeof(struct s_storage)); 
	if( flag ) 
		storage_guild_storageopen(sd); 

	return 0;
}
int intif_parse_SaveGuildStorage(int fd)
{
	if(battle_config.save_log) {
		ShowInfo("intif_save_guild_storage: done %d %d %d\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOB(fd,10) );
	}
	storage_guild_storagesaved(/*RFIFOL(fd,2), */RFIFOL(fd,6));
	return 0;
}

/// Party creation notification.
int intif_parse_PartyCreated(int fd)
{
	if(battle_config.etc_log)
		ShowInfo("intif: party created by account %d\n\n", RFIFOL(fd,2));
	party_created(RFIFOL(fd,2), RFIFOL(fd,6), RFIFOB(fd,10), RFIFOL(fd,11), (const char *)RFIFOP(fd,15));
	return 0;
}
// パーティ情報
int intif_parse_PartyInfo(int fd)
{
	if( RFIFOW(fd,2) == 12 ){
		ShowWarning("intif: party noinfo (char_id=%d party_id=%d)\n", RFIFOL(fd,4), RFIFOL(fd,8));
		party_recv_noinfo(RFIFOL(fd,8), RFIFOL(fd,4));
		return 0;
	}

	if( RFIFOW(fd,2) != 8+sizeof(struct party) )
		ShowError("intif: party info : data size error (char_id=%d party_id=%d packet_len=%d expected_len=%d)\n", RFIFOL(fd,4), RFIFOL(fd,8), RFIFOW(fd,2), 8+sizeof(struct party));
	party_recv_info((struct party *)RFIFOP(fd,8), RFIFOL(fd,4));
	return 0;
}
// パーティ追加通知
int intif_parse_PartyMemberAdded(int fd)
{
	if(battle_config.etc_log)
		ShowInfo("intif: party member added Party (%d), Account(%d), Char(%d)\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10));
	party_member_added(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10), RFIFOB(fd, 14));
	return 0;
}
// パーティ設定変更通知
int intif_parse_PartyOptionChanged(int fd)
{
	party_optionchanged(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOW(fd,10),RFIFOW(fd,12),RFIFOB(fd,14));
	return 0;
}
// ACK member leaving party
int intif_parse_PartyMemberWithdraw(int fd) {
	if(battle_config.etc_log)
		ShowInfo("intif: party member withdraw: Type(%d) Party(%d), Account(%d), Char(%d), Name(%s)\n",RFIFOB(fd,14+NAME_LENGTH),RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14)); 
	party_member_withdraw(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),(char*)RFIFOP(fd,14),(enum e_party_member_withdraw)RFIFOB(fd,14+NAME_LENGTH)); 
	return 0;
}
// パーティ解散通知
int intif_parse_PartyBroken(int fd)
{
	party_broken(RFIFOL(fd,2));
	return 0;
}
// パーティ移動通知
int intif_parse_PartyMove(int fd)
{
	party_recv_movemap(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOW(fd,14),RFIFOB(fd,16),RFIFOW(fd,17),RFIFOW(fd, 19));
	return 0;
}
// パーティメッセージ
int intif_parse_PartyMessage(int fd)
{
	party_recv_message(RFIFOL(fd,4),RFIFOL(fd,8),(char *) RFIFOP(fd,12),RFIFOW(fd,2)-12);
	return 0;
}

// ギルド作成可否
int intif_parse_GuildCreated(int fd)
{
	guild_created(RFIFOL(fd,2),RFIFOL(fd,6));
	return 0;
}
// ギルド情報
int intif_parse_GuildInfo(int fd)
{
	if(RFIFOW(fd,2) == 8) {
		ShowWarning("intif: guild noinfo %d\n",RFIFOL(fd,4));
		guild_recv_noinfo(RFIFOL(fd,4));
		return 0;
	}

	if( RFIFOW(fd,2)!=sizeof(struct guild)+4 )
		ShowError("intif: guild info : data size error Gid: %d recv size: %d Expected size: %d\n",RFIFOL(fd,4),RFIFOW(fd,2),sizeof(struct guild)+4);
	guild_recv_info((struct guild *)RFIFOP(fd,4));
	return 0;
}
// ギルドメンバ追加通知
int intif_parse_GuildMemberAdded(int fd)
{
	if(battle_config.etc_log)
		ShowInfo("intif: guild member added %d %d %d %d\n",RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14));
	guild_member_added(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14));
	return 0;
}
// ギルドメンバ脱退/追放通知
int intif_parse_GuildMemberWithdraw(int fd)
{
	guild_member_withdraw(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14),(char *)RFIFOP(fd,55),(char *)RFIFOP(fd,15));
	return 0;
}

// ギルドメンバオンライン状態/Lv変更通知
int intif_parse_GuildMemberInfoShort(int fd)
{
	guild_recv_memberinfoshort(RFIFOL(fd, 2), RFIFOL(fd, 6), RFIFOL(fd, 10), RFIFOB(fd, 14), RFIFOW(fd, 15), RFIFOW(fd, 17), RFIFOL(fd, 19));
	return 0;
}
// ギルド解散通知
int intif_parse_GuildBroken(int fd)
{
	guild_broken(RFIFOL(fd,2),RFIFOB(fd,6));
	return 0;
}

// basic guild info change notice
// 0x3839 <packet len>.w <guild id>.l <type>.w <data>.?b
int intif_parse_GuildBasicInfoChanged(int fd)
{
	//int len = RFIFOW(fd,2) - 10;
	int guild_id = RFIFOL(fd,4);
	int type = RFIFOW(fd,8);
	//void* data = RFIFOP(fd,10);

	struct guild* g = guild_search(guild_id);
	if( g == NULL )
		return 0;

	switch(type) {
	case GBI_EXP:        g->exp = RFIFOQ(fd,10); break;
	case GBI_GUILDLV:    g->guild_lv = RFIFOW(fd,10); break;
	case GBI_SKILLPOINT: g->skill_point = RFIFOL(fd,10); break;
	}

	return 0;
}

// guild member info change notice
// 0x383a <packet len>.w <guild id>.l <account id>.l <char id>.l <type>.w <data>.?b
int intif_parse_GuildMemberInfoChanged(int fd)
{
	//int len = RFIFOW(fd,2) - 18;
	int guild_id = RFIFOL(fd,4);
	uint32 account_id = RFIFOL(fd,8);
	uint32 char_id = RFIFOL(fd,12);
	int type = RFIFOW(fd,16);
	//void* data = RFIFOP(fd,18);

	struct guild* g;
	int idx;

	g = guild_search(guild_id);
	if( g == NULL )
		return 0;

	idx = guild_getindex(g,account_id,char_id);
	if( idx == -1 )
		return 0;

	switch( type ) {
	case GMI_POSITION:   g->member[idx].position   = RFIFOW(fd,18); guild_memberposition_changed(g,idx,RFIFOW(fd,18)); break;
	case GMI_EXP:        g->member[idx].exp        = RFIFOQ(fd,18); break;
	case GMI_HAIR:       g->member[idx].hair       = RFIFOW(fd,18); break;
	case GMI_HAIR_COLOR: g->member[idx].hair_color = RFIFOW(fd,18); break;
	case GMI_GENDER:     g->member[idx].gender     = RFIFOW(fd,18); break;
	case GMI_CLASS:      g->member[idx].class_     = RFIFOW(fd,18); break;
	case GMI_LEVEL:      g->member[idx].lv         = RFIFOW(fd,18); break;
	case GMI_LAST_LOGIN: g->member[idx].last_login = RFIFOL(fd,18); break;
	}
	return 0;
}

// ギルド役職変更通知
int intif_parse_GuildPosition(int fd)
{
	if( RFIFOW(fd,2)!=sizeof(struct guild_position)+12 )
		ShowError("intif: guild info : data size error\n %d %d %d",RFIFOL(fd,4),RFIFOW(fd,2),sizeof(struct guild_position)+12);
	guild_position_changed(RFIFOL(fd,4),RFIFOL(fd,8),(struct guild_position *)RFIFOP(fd,12));
	return 0;
}
// ギルドスキル割り振り通知
int intif_parse_GuildSkillUp(int fd)
{
	guild_skillupack(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10));
	return 0;
}
// ギルド同盟/敵対通知
int intif_parse_GuildAlliance(int fd)
{
	guild_allianceack(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOL(fd,14),RFIFOB(fd,18),(char *) RFIFOP(fd,19),(char *) RFIFOP(fd,43));
	return 0;
}
// ギルド告知変更通知
int intif_parse_GuildNotice(int fd)
{
	guild_notice_changed(RFIFOL(fd,2),(char *) RFIFOP(fd,6),(char *) RFIFOP(fd,66));
	return 0;
}
// ギルドエンブレム変更通知
int intif_parse_GuildEmblem(int fd)
{
	guild_emblem_changed(RFIFOW(fd,2)-12,RFIFOL(fd,4),RFIFOL(fd,8), (char *)RFIFOP(fd,12));
	return 0;
}
// ギルド会話受信
int intif_parse_GuildMessage(int fd)
{
	guild_recv_message(RFIFOL(fd,4),RFIFOL(fd,8),(char *) RFIFOP(fd,12),RFIFOW(fd,2)-12);
	return 0;
}
// ギルド城データ要求返信
int intif_parse_GuildCastleDataLoad(int fd)
{
	return guild_castledataloadack(RFIFOW(fd, 2), (struct guild_castle *)RFIFOP(fd, 4));
}

int intif_parse_GuildMasterChanged(int fd)
{
	return guild_gm_changed(RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOL(fd, 14));
}

// pet
int intif_parse_CreatePet(int fd)
{
	pet_get_egg(RFIFOL(fd,2), RFIFOW(fd,6), RFIFOL(fd,8));
	return 0;
}

int intif_parse_RecvPetData(int fd)
{
	struct s_pet p;
	int len;
	len=RFIFOW(fd,2);
	if(sizeof(struct s_pet)!=len-9) {
		if(battle_config.etc_log)
			ShowError("intif: pet data: data size error %d %d\n",sizeof(struct s_pet),len-9);
	}
	else{
		memcpy(&p,RFIFOP(fd,9),sizeof(struct s_pet));
		pet_recv_petdata(RFIFOL(fd,4),&p,RFIFOB(fd,8));
	}

	return 0;
}
int intif_parse_SavePetOk(int fd)
{
	if(RFIFOB(fd,6) == 1)
		ShowError("pet data save failure\n");

	return 0;
}

int intif_parse_DeletePetOk(int fd)
{
	if(RFIFOB(fd,2) == 1)
		ShowError("pet data delete failure\n");

	return 0;
}

int intif_parse_ChangeNameOk(int fd)
{
	struct map_session_data *sd = NULL;
	if((sd=map_id2sd(RFIFOL(fd,2)))==NULL ||
		sd->status.char_id != (int)RFIFOL(fd,6))
		return 0;

	switch (RFIFOB(fd,10)) {
	case 0: //Players [NOT SUPPORTED YET]
		break;
	case 1: //Pets
		pet_change_name_ack(sd, (char*)RFIFOP(fd,12), RFIFOB(fd,11));
		break;
	case 2: //Hom
		hom_change_name_ack(sd, (char*)RFIFOP(fd,12), RFIFOB(fd,11));
		break;
	}
	return 0;
}

//----------------------------------------------------------------
// Homunculus recv packets [albator]

int intif_parse_CreateHomunculus(int fd)
{
	int len;
	len=RFIFOW(fd,2)-9;
	if(sizeof(struct s_homunculus)!=len) {
		if(battle_config.etc_log)
			ShowError("intif: create homun data: data size error %d != %d\n",sizeof(struct s_homunculus),len);
		return 0;
	}
	hom_recv_data(RFIFOL(fd,4), (struct s_homunculus*)RFIFOP(fd,9), RFIFOB(fd,8)) ;
	return 0;
}

int intif_parse_RecvHomunculusData(int fd)
{
	int len;

	len=RFIFOW(fd,2)-9;

	if(sizeof(struct s_homunculus)!=len) {
		if(battle_config.etc_log)
			ShowError("intif: homun data: data size error %d %d\n",sizeof(struct s_homunculus),len);
		return 0;
	}
	hom_recv_data(RFIFOL(fd,4), (struct s_homunculus*)RFIFOP(fd,9), RFIFOB(fd,8));
	return 0;
}

int intif_parse_SaveHomunculusOk(int fd)
{
	if(RFIFOB(fd,6) != 1)
		ShowError("homunculus data save failure for account %d\n", RFIFOL(fd,2));

	return 0;
}

int intif_parse_DeleteHomunculusOk(int fd)
{
	if(RFIFOB(fd,2) != 1)
		ShowError("Homunculus data delete failure\n");

	return 0;
}

/**************************************

QUESTLOG SYSTEM FUNCTIONS

***************************************/

void intif_request_questlog(TBL_PC *sd)
{
	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3060;
	WFIFOL(inter_fd,2) = sd->status.char_id;
	WFIFOSET(inter_fd,6);
}

void intif_parse_questlog(int fd)
{
	uint32 char_id = RFIFOL(fd, 4), num_received = (RFIFOW(fd, 2) - 8) / sizeof(struct quest);
	TBL_PC *sd = map_charid2sd(char_id);

	if (!sd) // User not online anymore
		return;

	sd->num_quests = sd->avail_quests = 0;

	if (num_received == 0) {
		if (sd->quest_log) {
			aFree(sd->quest_log);
			sd->quest_log = NULL;
		}
	}
	else {
		struct quest *received = (struct quest *)RFIFOP(fd, 8);
		int i, k = num_received;

		if (sd->quest_log)
			RECREATE(sd->quest_log, struct quest, num_received);
		else
			CREATE(sd->quest_log, struct quest, num_received);

		for (i = 0; i < num_received; i++) {
			if (quest_db(received[i].quest_id) == &quest_dummy) {
				ShowError("intif_parse_QuestLog: quest %d not found in DB.\n", received[i].quest_id);
				continue;
			}
			if (received[i].state != Q_COMPLETE) // Insert at the beginning
				memcpy(&sd->quest_log[sd->avail_quests++], &received[i], sizeof(struct quest));
			else // Insert at the end
				memcpy(&sd->quest_log[--k], &received[i], sizeof(struct quest));
			sd->num_quests++;
		}
		if (sd->avail_quests < k) {
			// sd->avail_quests and k didn't meet in the middle: some entries were skipped
			if (k < num_received) // Move the entries at the end to fill the gap
				memmove(&sd->quest_log[k], &sd->quest_log[sd->avail_quests], sizeof(struct quest) * (num_received - k));
			sd->quest_log = aRealloc(sd->quest_log, sizeof(struct quest) * sd->num_quests);
		}
	}

	quest_pc_login(sd);
}

/**
 * Parses the quest log save ack for a character from the inter server.
 *
 * Received in reply to the requests made by intif_quest_save.
 *
 * @see intif_parse
 */
void intif_parse_questsave(int fd)
{
	int cid = RFIFOL(fd, 2);
	TBL_PC *sd = map_id2sd(cid);

	if( !RFIFOB(fd, 6) )
		ShowError("intif_parse_questsave: Failed to save quest(s) for character %d!\n", cid);
	else if( sd )
		sd->save_quest = false;
}

/**
 * Requests to the inter server to save a character's quest log entries.
 *
 * @param sd Character's data
 * @return 0 in case of success, nonzero otherwise
 */
int intif_quest_save(TBL_PC *sd)
{
	int len = sizeof(struct quest) * sd->num_quests + 8;

	if(CheckForCharServer())
		return 1;

	WFIFOHEAD(inter_fd, len);
	WFIFOW(inter_fd,0) = 0x3061;
	WFIFOW(inter_fd,2) = len;
	WFIFOL(inter_fd,4) = sd->status.char_id;
	if( sd->num_quests )
		memcpy(WFIFOP(inter_fd,8), sd->quest_log, sizeof(struct quest)*sd->num_quests);
	WFIFOSET(inter_fd,  len);

	return 0;
}

#ifndef TXT_ONLY

/*==========================================
* Achievement System
*------------------------------------------*/

/**
* Requests a character's achievement log entries to the inter server.
* @param char_id: Character ID
*/
void intif_request_achievements(uint32 char_id)
{
	if (CheckForCharServer())
		return;

	WFIFOHEAD(inter_fd, 6);
	WFIFOW(inter_fd, 0) = 0x3062;
	WFIFOL(inter_fd, 2) = char_id;
	WFIFOSET(inter_fd, 6);
}

/**
 * Handles reception of achievement data for a character from the inter-server.
 * @packet [in] 0x3810 <packet_len>.W <char_id>.L <char_achievements[]>.P
 * @param fd socket descriptor.
 */
void intif_parse_achievements_load(int fd)
{
	int size = RFIFOW(fd, 2);
	uint32 char_id = RFIFOL(fd, 4);
	int payload_count = (size - 8) / sizeof(struct achievement);
	struct map_session_data *sd = map_charid2sd(char_id);
	int i = 0;

	if (sd == NULL) {
		ShowError("intif_parse_achievements_load: Parse request for achievements received but character is offline!\n");
		return;
	}

	if (VECTOR_LENGTH(sd->achievement) > 0) {
		ShowError("intif_parse_achievements_load: Achievements already loaded! Possible multiple calls from the inter-server received.\n");
		return;
	}

	VECTOR_ENSURE(sd->achievement, payload_count, 1);

	for (i = 0; i < payload_count; i++) {
		struct achievement t_ach = { 0 };

		memcpy(&t_ach, RFIFOP(fd, 8 + i * sizeof(struct achievement)), sizeof(struct achievement));

		if (achievement_get(t_ach.id) == NULL) {
			ShowError("intif_parse_achievements_load: Invalid Achievement %d received from character %d. Ignoring...\n", t_ach.id, char_id);
			continue;
		}

		VECTOR_PUSH(sd->achievement, t_ach);
	}

	clif_achievement_list_all(sd);
	achievement_init_titles(sd);
	sd->achievements_received = true;
}

/**
 * Send character's achievement data to the inter-server.
 * @packet 0x3063 <packet_len>.W <char_id>.L <achievements[]>.P
 * @param sd pointer to map session data.
 */
void intif_achievements_save(struct map_session_data *sd)
{
	int packet_len = 0, payload_size = 0, i = 0;

	nullpo_retv(sd);
	/* check for character server. */
	if (CheckForCharServer())
		return;
	/* Return if no data. */
	if (!(payload_size = VECTOR_LENGTH(sd->achievement) * sizeof(struct achievement)))
		return;

	packet_len = payload_size + 8;

	WFIFOHEAD(inter_fd, packet_len);
	WFIFOW(inter_fd, 0) = 0x3063;
	WFIFOW(inter_fd, 2) = packet_len;
	WFIFOL(inter_fd, 4) = sd->status.char_id;
	for (i = 0; i < VECTOR_LENGTH(sd->achievement); i++)
		memcpy(WFIFOP(inter_fd, 8 + i * sizeof(struct achievement)), &VECTOR_INDEX(sd->achievement, i), sizeof(struct achievement));
	WFIFOSET(inter_fd, packet_len);
}

/*==========================================
 * MAIL SYSTEM
 * By Zephyrus
 *==========================================*/

/*------------------------------------------
 * Inbox Request
 * flag: 0 Update Inbox | 1 OpenMail
 *------------------------------------------*/
int intif_Mail_requestinbox(uint32 char_id, unsigned char flag, enum mail_inbox_type type)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,8);
	WFIFOW(inter_fd,0) = 0x3048;
	WFIFOL(inter_fd,2) = char_id;
	WFIFOB(inter_fd,6) = flag;
	WFIFOB(inter_fd,7) = type;
	WFIFOSET(inter_fd,8);

	return 0;
}

int intif_parse_Mail_inboxreceived(int fd)
{
	struct map_session_data *sd;
	unsigned char flag = RFIFOB(fd,8);

	sd = map_charid2sd(RFIFOL(fd,4));

	if (sd == NULL)
	{
		ShowError("intif_parse_Mail_inboxreceived: char not found %d\n",RFIFOL(fd,4));
		return 1;
	}

	if (RFIFOW(fd,2) - 10 != sizeof(struct mail_data))
	{
		ShowError("intif_parse_Mail_inboxreceived: data size error %d %d\n", RFIFOW(fd,2) - 10, sizeof(struct mail_data));
		return 1;
	}

	//FIXME: this operation is not safe [ultramage]
	memcpy(&sd->mail.inbox, RFIFOP(fd,10), sizeof(struct mail_data));
	sd->mail.changed = false; // cache is now in sync

#if PACKETVER >= 20150513
	// Refresh top right icon
	clif_Mail_new(sd, 0, NULL, NULL);
#endif
	if (flag) {
		clif_Mail_refreshinbox(sd,RFIFOB(fd,9),0);
	}else if( battle_config.mail_show_status && ( battle_config.mail_show_status == 1 || sd->mail.inbox.unread ) )
	{
		char output[128];
		sprintf(output, msg_txt(sd,510), sd->mail.inbox.unchecked, sd->mail.inbox.unread + sd->mail.inbox.unchecked);
		clif_disp_onlyself(sd, output, strlen(output));
	}
	return 0;
}
/*------------------------------------------
 * Mail Read
 *------------------------------------------*/
int intif_Mail_read(int mail_id)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3049;
	WFIFOL(inter_fd,2) = mail_id;
	WFIFOSET(inter_fd,6);

	return 0;
}
/*------------------------------------------
 * Get Attachment
 *------------------------------------------*/
bool intif_mail_getattach( struct map_session_data* sd, struct mail_message *msg, enum mail_attachment_type type){ 
	if (CheckForCharServer())
		return false;

	WFIFOHEAD(inter_fd,11);
	WFIFOW(inter_fd,0) = 0x304a;
	WFIFOL(inter_fd,2) = sd->status.char_id;
	WFIFOL(inter_fd,6) = msg->id;
	WFIFOB(inter_fd,10) = (uint8)type;
	WFIFOSET(inter_fd, 11);

	return true;
}

/**
 * Receive the attachment from char-serv of a mail
 * @param fd : char-serv link
 * @return 0=error, 1=sucess
 */
int intif_parse_Mail_getattach(int fd)
{
	struct map_session_data *sd;
	struct item item[MAIL_MAX_ITEM];
	int i, mail_id, zeny;

	if (RFIFOW(fd, 2) - 16 != sizeof(struct item)*MAIL_MAX_ITEM)
	{
		ShowError("intif_parse_Mail_getattach: data size error %d %d\n", RFIFOW(fd, 2) - 16, sizeof(struct item));
		return 0;
	}

	sd = map_charid2sd( RFIFOL(fd,4) );

	if (sd == NULL)
	{
		ShowError("intif_parse_Mail_getattach: char not found %d\n",RFIFOL(fd,4));
		return 0;
	}

	mail_id = RFIFOL(fd, 8);

	ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
	if (i == MAIL_MAX_INBOX)
		return 0;

	zeny = RFIFOL(fd, 12);

	memcpy(item, RFIFOP(fd,16), sizeof(struct item)*MAIL_MAX_ITEM);

	mail_getattachment(sd, &sd->mail.inbox.msg[i], zeny, item);
	return 1;
}

/*------------------------------------------
 * Delete Message
 *------------------------------------------*/
int intif_Mail_delete(uint32 char_id, int mail_id)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x304b;
	WFIFOL(inter_fd,2) = char_id;
	WFIFOL(inter_fd,6) = mail_id;
	WFIFOSET(inter_fd,10);

	return 0;
}

int intif_parse_Mail_delete(int fd)
{
	uint32 char_id = RFIFOL(fd,2);
	int mail_id = RFIFOL(fd,6);
	bool failed = RFIFOB(fd,10);
	enum mail_inbox_type type;

	struct map_session_data *sd = map_charid2sd(char_id);
	if (sd == NULL)
	{
		ShowError("intif_parse_Mail_delete: char not found %d\n", char_id);
		return 1;
	}

	if (!failed)
	{
		int i;
		ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
		if( i < MAIL_MAX_INBOX )
		{
			clif_mail_delete(sd, &sd->mail.inbox.msg[i], !failed);
			type = sd->mail.inbox.msg[i].type;
			memset(&sd->mail.inbox.msg[i], 0, sizeof(struct mail_message));
			sd->mail.inbox.amount--;
		}

		if( sd->mail.inbox.full || sd->mail.inbox.unchecked > 0 )
			intif_Mail_requestinbox(sd->status.char_id, 1, type); // Free space is available for new mails
	}

	return 0;
}
/*------------------------------------------
 * Return Message
 *------------------------------------------*/
int intif_Mail_return(uint32 char_id, int mail_id)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x304c;
	WFIFOL(inter_fd,2) = char_id;
	WFIFOL(inter_fd,6) = mail_id;
	WFIFOSET(inter_fd,10);

	return 0;
}

int intif_parse_Mail_return(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	int mail_id = RFIFOL(fd,6);
	short fail = RFIFOB(fd,10);
	enum mail_inbox_type type;

	if( sd == NULL )
	{
		ShowError("intif_parse_Mail_return: char not found %d\n",RFIFOL(fd,2));
		return 1;
	}

	if( !fail )
	{
		int i;
		ARR_FIND(0, MAIL_MAX_INBOX, i, sd->mail.inbox.msg[i].id == mail_id);
		if( i < MAIL_MAX_INBOX )
		{
			type = sd->mail.inbox.msg[i].type;
			memset(&sd->mail.inbox.msg[i], 0, sizeof(struct mail_message));
			sd->mail.inbox.amount--;
		}

		if( sd->mail.inbox.full )
			intif_Mail_requestinbox(sd->status.char_id, 1, type); // Free space is available for new mails
	}

	clif_Mail_return(sd->fd, mail_id, fail);
	return 0;
}
/*------------------------------------------
 * Send Mail
 *------------------------------------------*/
int intif_Mail_send(uint32 account_id, struct mail_message *msg)
{
	int len = sizeof(struct mail_message) + 8;

	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,len);
	WFIFOW(inter_fd,0) = 0x304d;
	WFIFOW(inter_fd,2) = len;
	WFIFOL(inter_fd,4) = account_id;
	memcpy(WFIFOP(inter_fd,8), msg, sizeof(struct mail_message));
	WFIFOSET(inter_fd,len);

	return 1;
}

/**
 * Received the ack of a mail send request
 * @param fd L char-serv link
 */
static void intif_parse_Mail_send(int fd)
{
	struct mail_message msg;
	struct map_session_data *sd;
	bool fail;

	if( RFIFOW(fd,2) - 4 != sizeof(struct mail_message) )
	{
		ShowError("intif_parse_Mail_send: data size error %d %d\n", RFIFOW(fd,2) - 4, sizeof(struct mail_message));
		return;
	}

	memcpy(&msg, RFIFOP(fd,4), sizeof(struct mail_message));
	fail = (msg.id == 0);

	// notify sender
	sd = map_charid2sd(msg.send_id);
	if( sd != NULL )
	{
		if( fail )
			mail_deliveryfail(sd, &msg);
		else
		{
			clif_Mail_send(sd, WRITE_MAIL_SUCCESS);
			if( save_settings&16 )
				chrif_save(sd, CSAVE_INVENTORY);
		}
	}
}

static void intif_parse_Mail_new(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	int mail_id = RFIFOL(fd,6);
	const char* sender_name = (char*)RFIFOP(fd,10);
	const char* title = (char*)RFIFOP(fd,34);

	if( sd == NULL )
		return;

	sd->mail.changed = true;
	sd->mail.inbox.unread++;
	clif_Mail_new(sd, mail_id, sender_name, title);
#if PACKETVER >= 20150513
	// Make sure the window gets refreshed when its open
	intif_Mail_requestinbox(sd->status.char_id, 1, RFIFOB(fd,74));
#endif
}

static void intif_parse_Mail_receiver( int fd ){
	struct map_session_data *sd;

	sd = map_charid2sd( RFIFOL( fd, 2 ) );

	// Only f the player is online
	if( sd ){
		clif_Mail_Receiver_Ack( sd, RFIFOL( fd, 6 ), RFIFOW( fd, 10 ), RFIFOW( fd, 12 ), RFIFOCP( fd, 14 ) );
	}
}

bool intif_mail_checkreceiver( struct map_session_data* sd, char* name ){
	struct map_session_data *tsd;

	tsd = map_nick2sd( name );

	// If the target player is online on this map-server
	if( tsd != NULL ){
		clif_Mail_Receiver_Ack( sd, tsd->status.char_id, tsd->status.class_, tsd->status.base_level, name );
		return true;
	}

	if( CheckForCharServer() )
		return false;

	WFIFOHEAD(inter_fd, 6 + NAME_LENGTH);
	WFIFOW(inter_fd, 0) = 0x304e;
	WFIFOL(inter_fd, 2) = sd->status.char_id;
	safestrncpy(WFIFOCP(inter_fd, 6), name, NAME_LENGTH);
	WFIFOSET(inter_fd, 6 + NAME_LENGTH);

	return true;
}

/*==========================================
 * AUCTION SYSTEM
 * By Zephyrus
 *==========================================*/
int intif_Auction_requestlist(uint32 char_id, short type, int price, const char* searchtext, short page)
{
	int len = NAME_LENGTH + 16;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,len);
	WFIFOW(inter_fd,0) = 0x3050;
	WFIFOW(inter_fd,2) = len;
	WFIFOL(inter_fd,4) = char_id;
	WFIFOW(inter_fd,8) = type;
	WFIFOL(inter_fd,10) = price;
	WFIFOW(inter_fd,14) = page;
	memcpy(WFIFOP(inter_fd,16), searchtext, NAME_LENGTH);
	WFIFOSET(inter_fd,len);

	return 0;
}

static void intif_parse_Auction_results(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,4));
	short count = RFIFOW(fd,8);
	short pages = RFIFOW(fd,10);
	uint8* data = RFIFOP(fd,12);

	if( sd == NULL )
		return;

	clif_Auction_results(sd, count, pages, data);
}

int intif_Auction_register(struct auction_data *auction)
{
	int len = sizeof(struct auction_data) + 4;
	
	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,len);
	WFIFOW(inter_fd,0) = 0x3051;
	WFIFOW(inter_fd,2) = len;
	memcpy(WFIFOP(inter_fd,4), auction, sizeof(struct auction_data));
	WFIFOSET(inter_fd,len);
	
	return 1;
}

static void intif_parse_Auction_register(int fd)
{
	struct map_session_data *sd;
	struct auction_data auction;

	if( RFIFOW(fd,2) - 4 != sizeof(struct auction_data) )
	{
		ShowError("intif_parse_Auction_register: data size error %d %d\n", RFIFOW(fd,2) - 4, sizeof(struct auction_data));
		return;
	}

	memcpy(&auction, RFIFOP(fd,4), sizeof(struct auction_data));
	if( (sd = map_charid2sd(auction.seller_id)) == NULL )
		return;

	if( auction.auction_id > 0 )
	{
		clif_Auction_message(sd->fd, 1); // Confirmation Packet ??
		if( save_settings&32 )
			chrif_save(sd, CSAVE_INVENTORY);
	}
	else
	{
		int zeny = auction.hours*battle_config.auction_feeperhour;

		clif_Auction_message(sd->fd, 4);
		pc_additem(sd, &auction.item, auction.item.amount, LOG_TYPE_AUCTION);

		pc_getzeny(sd, zeny, LOG_TYPE_AUCTION, NULL);
	}
}

int intif_Auction_cancel(uint32 char_id, unsigned int auction_id)
{
	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x3052;
	WFIFOL(inter_fd,2) = char_id;
	WFIFOL(inter_fd,6) = auction_id;
	WFIFOSET(inter_fd,10);

	return 0;
}

static void intif_parse_Auction_cancel(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	int result = RFIFOB(fd,6);

	if( sd == NULL )
		return;

	switch( result )
	{
	case 0: clif_Auction_message(sd->fd, 2); break;
	case 1: clif_Auction_close(sd->fd, 2); break;
	case 2: clif_Auction_close(sd->fd, 1); break;
	case 3: clif_Auction_message(sd->fd, 3); break;
	}
}

int intif_Auction_close(uint32 char_id, unsigned int auction_id)
{
	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x3053;
	WFIFOL(inter_fd,2) = char_id;
	WFIFOL(inter_fd,6) = auction_id;
	WFIFOSET(inter_fd,10);

	return 0;
}

static void intif_parse_Auction_close(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	unsigned char result = RFIFOB(fd,6);

	if( sd == NULL )
		return;

	clif_Auction_close(sd->fd, result);
	if( result == 0 )
	{
		// FIXME: Leeching off a parse function
		clif_parse_Auction_cancelreg(fd, sd);
		intif_Auction_requestlist(sd->status.char_id, 6, 0, "", 1);
	}
}

int intif_Auction_bid(uint32 char_id, const char* name, unsigned int auction_id, int bid)
{
	int len = 16 + NAME_LENGTH;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,len);
	WFIFOW(inter_fd,0) = 0x3055;
	WFIFOW(inter_fd,2) = len;
	WFIFOL(inter_fd,4) = char_id;
	WFIFOL(inter_fd,8) = auction_id;
	WFIFOL(inter_fd,12) = bid;
	memcpy(WFIFOP(inter_fd,16), name, NAME_LENGTH);
	WFIFOSET(inter_fd,len);

	return 0;
}

static void intif_parse_Auction_bid(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	int bid = RFIFOL(fd,6);
	unsigned char result = RFIFOB(fd,10);

	if( sd == NULL )
		return;

	clif_Auction_message(sd->fd, result);
	if( bid > 0 )
	{
		pc_getzeny(sd, bid, LOG_TYPE_AUCTION, NULL);
	}
	if( result == 1 )
	{ // To update the list, display your buy list
		clif_parse_Auction_cancelreg(fd, sd);
		intif_Auction_requestlist(sd->status.char_id, 7, 0, "", 1);
	}
}

// Used to send 'You have won the auction' and 'You failed to won the auction' messages
static void intif_parse_Auction_message(int fd)
{
	struct map_session_data *sd = map_charid2sd(RFIFOL(fd,2));
	unsigned char result = RFIFOB(fd,6);

	if( sd == NULL )
		return;

	clif_Auction_message(sd->fd, result);
}

#endif

/*==========================================
 * Mercenary's System
 *------------------------------------------*/
int intif_mercenary_create(struct s_mercenary *merc)
{
	int size = sizeof(struct s_mercenary) + 4;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,size);
	WFIFOW(inter_fd,0) = 0x3070;
	WFIFOW(inter_fd,2) = size;
	memcpy(WFIFOP(inter_fd,4), merc, sizeof(struct s_mercenary));
	WFIFOSET(inter_fd,size);
	return 0;
}

int intif_parse_mercenary_received(int fd)
{
	int len = RFIFOW(fd,2) - 5;
	if( sizeof(struct s_mercenary) != len )
	{
		if( battle_config.etc_log )
			ShowError("intif: create mercenary data size error %d != %d\n", sizeof(struct s_mercenary), len);
		return 0;
	}

	mercenary_recv_data((struct s_mercenary*)RFIFOP(fd,5), RFIFOB(fd,4));
	return 0;
}

int intif_mercenary_request(int merc_id, uint32 char_id)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd,0) = 0x3071;
	WFIFOL(inter_fd,2) = merc_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOSET(inter_fd,10);
	return 0;
}

int intif_mercenary_delete(int merc_id)
{
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x3072;
	WFIFOL(inter_fd,2) = merc_id;
	WFIFOSET(inter_fd,6);
	return 0;
}

int intif_parse_mercenary_deleted(int fd)
{
	if( RFIFOB(fd,2) != 1 )
		ShowError("Mercenary data delete failure\n");

	return 0;
}

int intif_mercenary_save(struct s_mercenary *merc)
{
	int size = sizeof(struct s_mercenary) + 4;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,size);
	WFIFOW(inter_fd,0) = 0x3073;
	WFIFOW(inter_fd,2) = size;
	memcpy(WFIFOP(inter_fd,4), merc, sizeof(struct s_mercenary));
	WFIFOSET(inter_fd,size);
	return 0;
}

int intif_parse_mercenary_saved(int fd)
{
	if( RFIFOB(fd,2) != 1 )
		ShowError("Mercenary data save failure\n");

	return 0;
}

/*==========================================
 * Elemental's System
 *------------------------------------------*/
int intif_elemental_create(struct s_elemental *elem) {
	int size = sizeof(struct s_elemental) + 4;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,size);
	WFIFOW(inter_fd, 0) = 0x3078;
	WFIFOW(inter_fd,2) = size;
	memcpy(WFIFOP(inter_fd,4), elem, sizeof(struct s_elemental));
	WFIFOSET(inter_fd,size);
	return 0;
}

int intif_parse_elemental_received(int fd) {
	int len = RFIFOW(fd,2) - 5;
	if( sizeof(struct s_elemental) != len ) {
		if( battle_config.etc_log )
			ShowError("intif: create elemental data size error %d != %d\n", sizeof(struct s_elemental), len);
		return 0;
	}

	elem_data_received((struct s_elemental*)RFIFOP(fd,5), RFIFOB(fd,4));
	return 0;
}

int intif_elemental_request(int elem_id, uint32 char_id) {
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,10);
	WFIFOW(inter_fd, 0) = 0x3079;
	WFIFOL(inter_fd, 2) = elem_id;
	WFIFOL(inter_fd,6) = char_id;
	WFIFOSET(inter_fd,10);
	return 0;
}

int intif_elemental_delete(int elem_id) {
	if (CheckForCharServer())
		return 0;

	WFIFOHEAD(inter_fd,6);
	WFIFOW(inter_fd,0) = 0x307a;
	WFIFOL(inter_fd,2) = elem_id;
	WFIFOSET(inter_fd,6);
	return 0;
}

int intif_parse_elemental_deleted(int fd) {
	if( RFIFOB(fd,2) != 1 )
		ShowError("Elemental data delete failure\n");

	return 0;
}

int intif_elemental_save(struct s_elemental *elem) {
	int size = sizeof(struct s_elemental) + 4;

	if( CheckForCharServer() )
		return 0;

	WFIFOHEAD(inter_fd,size);
	WFIFOW(inter_fd,0) = 0x307b;
	WFIFOW(inter_fd,2) = size;
	memcpy(WFIFOP(inter_fd,4), elem, sizeof(struct s_elemental));
	WFIFOSET(inter_fd,size);
	return 0;
}

int intif_parse_elemental_saved(int fd) {
	if( RFIFOB(fd,2) != 1 )
		ShowError("Elemental data save failure\n");

	return 0;
}

/// BROADCAST OBTAIN SPECIAL ITEM

/**
 * Request to send broadcast item to all servers
 * ZI 3009 <cmd>.W <len>.W <nameid>.W <source>.W <type>.B <name>.?B
 * @param sd Player who obtain the item
 * @param nameid Obtained item
 * @param sourceid Source of item, another item ID or monster ID
 * @param type Obtain type @see enum BROADCASTING_SPECIAL_ITEM_OBTAIN
 * @return
 **/
int intif_broadcast_obtain_special_item(struct map_session_data *sd, t_itemid nameid, t_itemid sourceid, unsigned char type) {
	nullpo_retr(0, sd);

	// Should not be here!
	if (type == ITEMOBTAIN_TYPE_NPC) {
		intif_broadcast_obtain_special_item_npc(sd, nameid);
		return 0;
	}

	// Send local
	clif_broadcast_obtain_special_item(sd->status.name, nameid, sourceid, (enum BROADCASTING_SPECIAL_ITEM_OBTAIN)type);

	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0;

	WFIFOHEAD(inter_fd, 11 + NAME_LENGTH);
	WFIFOW(inter_fd, 0) = 0x3009;
	WFIFOW(inter_fd, 2) = 11 + NAME_LENGTH;
	WFIFOL(inter_fd, 4) = nameid;
	WFIFOW(inter_fd, 8) = sourceid;
	WFIFOB(inter_fd, 10) = type;
	safestrncpy(WFIFOCP(inter_fd, 11), sd->status.name, NAME_LENGTH);
	WFIFOSET(inter_fd, WFIFOW(inter_fd, 2));

	return 1;
}

/**
 * Request to send broadcast item to all servers.
 * TODO: Confirm the usage. Maybe on getitem-like command?
 * ZI 3009 <cmd>.W <len>.W <nameid>.W <source>.W <type>.B <name>.24B <npcname>.24B
 * @param sd Player who obtain the item
 * @param nameid Obtained item
 * @param srcname Source name
 * @return
 **/
int intif_broadcast_obtain_special_item_npc(struct map_session_data *sd, t_itemid nameid) {
	nullpo_retr(0, sd);

	// Send local
	clif_broadcast_obtain_special_item(sd->status.name, nameid, 0, ITEMOBTAIN_TYPE_NPC);

	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0;

	WFIFOHEAD(inter_fd, 11 + NAME_LENGTH * 2);
	WFIFOW(inter_fd, 0) = 0x3009;
	WFIFOW(inter_fd, 2) = 11 + NAME_LENGTH * 2;
	WFIFOL(inter_fd, 4) = nameid;
	WFIFOW(inter_fd, 8) = 0;
	WFIFOB(inter_fd, 10) = ITEMOBTAIN_TYPE_NPC;
	safestrncpy(WFIFOCP(inter_fd, 11), sd->status.name, NAME_LENGTH);
	WFIFOSET(inter_fd, WFIFOW(inter_fd, 2));

	return 1;
}

/**
 * Received broadcast item and broadcast on local map.
 * IZ 3809 <cmd>.W <len>.W <nameid>.W <source>.W <type>.B <name>.24B <srcname>.24B
 * @param fd
 **/
void intif_parse_broadcast_obtain_special_item(int fd) {
	int type = RFIFOB(fd, 10);
	char name[NAME_LENGTH];

	safestrncpy(name, (char *)RFIFOP(fd, 11), NAME_LENGTH);
	if (type == ITEMOBTAIN_TYPE_NPC)
		safestrncpy(name, (char *)RFIFOP(fd, 11 + NAME_LENGTH), NAME_LENGTH);

	clif_broadcast_obtain_special_item(name, RFIFOL(fd, 4), RFIFOW(fd, 8), (enum BROADCASTING_SPECIAL_ITEM_OBTAIN)type);
}

/*========================================== 
* Item Bound System 
*------------------------------------------*/ 
 #ifdef BOUND_ITEMS 
/**
 * ZI 0x3056 <char_id>.L <account_id>.L <guild_id>.W
 * Request inter-serv to delete some bound item, for non connected cid
 * @param char_id : Char to delete item ID
 * @param aid : Account to delete item ID
 * @param guild_id : Guild of char
 */
void intif_itembound_guild_retrieve(uint32 char_id, uint32 account_id, int guild_id) {
	struct s_storage *gstor = guild2storage2(guild_id); 
	WFIFOHEAD(inter_fd,12); 
	WFIFOW(inter_fd,0) = 0x3056; 
	WFIFOL(inter_fd,2) = char_id; 
	WFIFOL(inter_fd,6) = account_id;
	WFIFOW(inter_fd,10) = guild_id; 
	WFIFOSET(inter_fd,12); 
	if (gstor)
		gstor->lock = true; //Lock for retrieval process
	ShowInfo("Request guild bound item(s) retrieval for CID = "CL_WHITE"%d"CL_RESET", AID = %d, Guild ID = "CL_WHITE"%d"CL_RESET".\n", char_id, account_id, guild_id);
} 
 
/**
 * Acknowledge the good deletion of the bound item
 * (unlock the guild storage)
 * @struct : 0x3856 <aid>.L <gid>.W
 * @param fd : Char-serv link
 */
void intif_parse_itembound_ack(int fd) { 
	struct s_storage *gstor; 
	int guild_id = RFIFOW(char_fd,6); 

	gstor = guild2storage2(guild_id); 
	if(gstor) gstor->lock = false; //Unlock now that operation is completed 
}

/**
* IZ 0x3857 <size>.W <count>.W <guild_id>.W { <item>.?B }.*MAX_INVENTORY
* Received the retrieved guild bound items from inter-server, store them to guild storage.
* @param fd
* @author [Cydh]
*/
void intif_parse_itembound_store2gstorage(int fd) {
	unsigned short i, failed = 0;
	short count = RFIFOW(fd, 4), guild_id = RFIFOW(fd, 6);
	struct s_storage *gstor = guild2storage(guild_id);
	if (!gstor) {
		ShowError("intif_parse_itembound_store2gstorage: Guild '%d' not found.\n", guild_id);
		return;
	}
	//@TODO: Gives some actions for item(s) that cannot be stored because storage is full or reach the limit of stack amount
	for (i = 0; i < count; i++) {
		struct item *item = (struct item*)RFIFOP(fd, 8 + i * sizeof(struct item));
		if (!item)
			continue;
		if (!storage_guild_additem2(gstor, item, item->amount))
			failed++;
	}
	ShowInfo("Retrieved '"CL_WHITE"%d"CL_RESET"' (failed: %d) guild bound item(s) for Guild ID = "CL_WHITE"%d"CL_RESET".\n", count, failed, guild_id);
	gstor->lock = false;
	gstor->status = false;
}
#endif

int intif_clan_requestclans()
{
	if (CheckForCharServer())
		return 0;
	WFIFOHEAD(inter_fd, 2);
	WFIFOW(inter_fd, 0) = 0x30A0;
	WFIFOSET(inter_fd, 2);
	return 1;
}

void intif_parse_clans(int fd)
{
	clan_load_clandata((RFIFOW(fd, 2) - 4) / sizeof(struct clan), (struct clan*)RFIFOP(fd, 4));
}

int intif_clan_message(int clan_id, uint32 account_id, const char *mes, int len)
{
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, len + 12);
	WFIFOW(inter_fd, 0) = 0x30A1;
	WFIFOW(inter_fd, 2) = len+12;
	WFIFOL(inter_fd, 4) = clan_id;
	WFIFOL(inter_fd, 8) = account_id;
	memcpy(WFIFOP(inter_fd, 12), mes, len);
	WFIFOSET(inter_fd, len + 12);

	return 1;
}

int intif_parse_clan_message(int fd)
{
	clan_recv_message(RFIFOL(fd, 4), RFIFOL(fd, 8), (char *) RFIFOP(fd, 12), RFIFOW(fd, 2) - 12);

	return 1;
}

int intif_clan_member_left(int clan_id)
{
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, 6);
	WFIFOW(inter_fd, 0) = 0x30A2;
	WFIFOL(inter_fd, 2) = clan_id;
	WFIFOSET(inter_fd, 6);
	
	return 1;
}

int intif_clan_member_joined(int clan_id)
{
	if (CheckForCharServer())
		return 0;

	if (other_mapserver_count < 1)
		return 0; //No need to send.

	WFIFOHEAD(inter_fd, 6);
	WFIFOW(inter_fd, 0) = 0x30A3;
	WFIFOL(inter_fd, 2) = clan_id;
	WFIFOSET(inter_fd, 6);
	
	return 1;
}

int intif_parse_clan_onlinecount(int fd)
{
	struct clan* clan = clan_search(RFIFOL(fd, 2));

	if(clan == NULL)
		return 0;

	clan->connect_member = RFIFOW(fd, 6);

	clif_clan_onlinecount(clan);

	return 1;
}

/**
 * Receive inventory/cart/storage data for player
 * IZ 0x388a <size>.W <type>.B <account_id>.L <result>.B <storage>.?B
 * @param fd
 */
static bool intif_parse_StorageReceived(int fd)
{
	char type =  RFIFOB(fd,4);
	uint32 account_id = RFIFOL(fd, 5);
	struct map_session_data *sd = map_id2sd(account_id);
	struct s_storage *stor, *p; //storage
	size_t sz_stor = sizeof(struct s_storage);

	if (!sd)
	{
		ShowError("intif_parse_StorageReceived: No player online for receiving inventory/cart/storage data (AID: %d)\n", account_id);
		return false;
	}

	if (!RFIFOB(fd, 9))
	{
		ShowError("intif_parse_StorageReceived: Failed to load! (AID: %d, type: %d)\n", account_id, type);
		return false;
	}

	p = (struct s_storage *)RFIFOP(fd, 10);

	switch (type)
	{ 
		case TABLE_INVENTORY:
			stor = &sd->inventory;
			break;
		case TABLE_STORAGE:
			if (p->stor_id == 0)
				stor = &sd->storage;
			else
				stor = &sd->storage2;
			break;
		case TABLE_CART:
			stor = &sd->cart;
			break;
		default: 
			return false;
	}

	if (stor->stor_id == p->stor_id) {
		if (stor->status) { // Already open.. lets ignore this update
			ShowWarning("intif_parse_StorageReceived: storage received for a client already open (User %d:%d)\n", sd->status.account_id, sd->status.char_id);
			return false;
		}
		if (stor->dirty) { // Already have storage, and it has been modified and not saved yet! Exploit!
			ShowWarning("intif_parse_StorageReceived: received storage for an already modified non-saved storage! (User %d:%d)\n", sd->status.account_id, sd->status.char_id);
			return false;
		}
	}
	if (RFIFOW(fd,2)-10 != sz_stor)
	{
		ShowError("intif_parse_StorageReceived: data size error %d %d\n",RFIFOW(fd,2)-10 , sz_stor);
		stor->status = false;
		return false;
	}

	memcpy(stor, p, sz_stor); //copy the items data to correct destination

	switch (type)
	{
		case TABLE_INVENTORY:
			{
#ifdef BOUND_ITEMS
				int j, idxlist[MAX_INVENTORY];
#endif
				pc_setinventorydata(sd);
				pc_setequipindex(sd);
#ifdef BOUND_ITEMS
				// Party bound item check
				if (sd->status.party_id == 0 && (j = pc_bound_chk(sd, BOUND_PARTY, idxlist))) 
				{ // Party was deleted while character offline
					int i;
					for (i = 0; i < j; i++)
						pc_delitem(sd, idxlist[i], sd->inventory.u.items_inventory[idxlist[i]].amount, 0, 1, LOG_TYPE_OTHER);
				}
#endif
				//Set here because we need the inventory data for weapon sprite parsing.
				status_set_viewdata(&sd->bl, sd->status.class_);
				status_calc_pc(sd, (enum e_status_calc_opt)(SCO_FIRST|SCO_FORCE));
				status_calc_weight(sd, 1|2); // Refresh item weight data
				chrif_scdata_request(sd->status.account_id, sd->status.char_id);
				break;
			}

		case TABLE_CART:
			if (sd->state.autotrade)
				clif_parse_LoadEndAck(sd->fd, sd);
			else if( sd->state.prevend )
			{
				clif_clearcart(sd->fd);
				clif_cartlist(sd);
				clif_openvendingreq(sd, sd->vend_skill_lv+2);
			}
			break;

		case TABLE_STORAGE:
			if (stor->stor_id)
				storage_storage2_open(sd);
			break;
	}
	return true;
}

/**
 * Save inventory/cart/storage data for a player
 * IZ 0x388b <account_id>.L <result>.B <type>.B
 * @param fd
 */
static void intif_parse_StorageSaved(int fd)
{
	TBL_PC *sd = map_id2sd(RFIFOL(fd,2));

	if (RFIFOB(fd, 6)) {
		switch (RFIFOB(fd, 7)) {
			case TABLE_INVENTORY: //inventory
				//ShowInfo("Inventory has been saved (AID: %d).\n", RFIFOL(fd, 2));
				break;
			case TABLE_STORAGE: //storage
				//ShowInfo("Storage has been saved (AID: %d).\n", RFIFOL(fd, 2));
				if (RFIFOB(fd, 8))
					storage_storage2_saved(map_id2sd(RFIFOL(fd, 2)));
				sd->storage.dirty = false;
				break;
			case TABLE_CART: // cart
				//ShowInfo("Cart has been saved (AID: %d).\n", RFIFOL(fd, 2));
				if( sd && sd->state.prevend ){
					intif_storage_request(sd,TABLE_CART,0);
				}
				break;
			default:
				break;
		}
	} else
		ShowError("Failed to save inventory/cart/storage data (AID: %d, type: %d).\n", RFIFOL(fd, 2), RFIFOB(fd, 7));
}

/**
  * IZ 0x388c <len>.W { <storage_table>.? }*?
  * Receive storage information
  **/
void intif_parse_StorageInfo_recv(int fd) {
	int i, size = sizeof(struct s_storage_table), count = (RFIFOW(fd, 2) - 4) / size;

	storage_count = 0;
	if (storage_db)
		aFree(storage_db);
	storage_db = NULL;

	for (i = 0; i < count; i++) {
		char name[NAME_LENGTH + 1];
		safestrncpy(name, (char *)RFIFOP(fd, 5 + size * i), NAME_LENGTH);
		if (!name || name[0] == '\0')
			continue;
		RECREATE(storage_db, struct s_storage_table, storage_count + 1);
		memcpy(&storage_db[storage_count], RFIFOP(fd, 4 + size * i), size);
		storage_count++;
	}

	if (battle_config.etc_log)
		ShowInfo("Received '"CL_WHITE"%d"CL_RESET"' storage info from inter-server.\n", storage_count);
}

/**
 * Request inventory/cart/storage data for a player
 * ZI 0x308a <type>.B <account_id>.L <char_id>.L <storage_id>.B
 * @param sd: Player data
 * @param type: Storage type
  * @param stor_id: Storage ID
 * @return false - error, true - message sent
 */
bool intif_storage_request(struct map_session_data *sd, enum storage_type type, uint8 stor_id)
{
	if (CheckForCharServer())
		return false;

	WFIFOHEAD(inter_fd, 12);
	WFIFOW(inter_fd, 0) = 0x308a;
	WFIFOB(inter_fd, 2) = type;
	WFIFOL(inter_fd, 3) = sd->status.account_id;
	WFIFOL(inter_fd, 7) = sd->status.char_id;
	WFIFOB(inter_fd, 11) = stor_id;
	WFIFOSET(inter_fd, 12);
	return true;
}

/**
 * Request to save inventory/cart/storage data from player
 * ZI 0x308b <size>.W <type>.B <account_id>.L <char_id>.L <entries>.?B
 * @param sd: Player data
 * @param type: Storage type
 * @ return false - error, true - message sent
 */
bool intif_storage_save(struct map_session_data *sd, struct s_storage *stor)
{
	int stor_size = sizeof(struct s_storage);

	nullpo_retr(false, sd);
	nullpo_retr(false, stor);

	if (CheckForCharServer())
		return false;

	WFIFOHEAD(inter_fd, stor_size+13);
	WFIFOW(inter_fd, 0) = 0x308b;
	WFIFOW(inter_fd, 2) = stor_size+13;
	WFIFOB(inter_fd, 4) = stor->type;
	WFIFOL(inter_fd, 5) = sd->status.account_id;
	WFIFOL(inter_fd, 9) = sd->status.char_id;
	memcpy(WFIFOP(inter_fd, 13), stor, stor_size);
	WFIFOSET(inter_fd, stor_size+13);
	return true;
}

/*========================================== 
 * Communication from the inter server, Main entry point interface (inter<=>map) 
 * @param fd : inter-serv link
 * @return
 *  0 (unknown packet).
 *  1 sucess (no error)
 *  2 invalid lenght of packet (not enough data yet)
 *------------------------------------------*/ 
int intif_parse(int fd)
{
	int packet_len, cmd;
	cmd = RFIFOW(fd,0);

	// Verify ID of the packet
	if(cmd<0x3800 || cmd>=0x3800+ARRAYLENGTH(packet_len_table) || packet_len_table[cmd-0x3800]==0){
		return 0;
	}
	// Check the length of the packet
	packet_len = packet_len_table[cmd-0x3800];
	if(packet_len==-1){
		if(RFIFOREST(fd)<4)
			return 2;
		packet_len = RFIFOW(fd,2);
	}
	if((int)RFIFOREST(fd)<packet_len){
		return 2;
	}

#ifdef LOG_ALL_PACKETS
	ShowDebug("intif_parse: Received packet 0x%04X (length %d), session #%d\n", cmd, packet_len, fd);
#endif

	// 処理分岐
	switch(cmd){
	case 0x3800:
		if (RFIFOL(fd,4) == 0xFF000000) //Normal announce.
			clif_broadcast(NULL, (char *) RFIFOP(fd,16), packet_len-16, BC_DEFAULT, ALL_CLIENT);
		else if (RFIFOL(fd,4) == 0xFE000000) //Main chat message [LuzZza]
			clif_MainChatMessage((char *)RFIFOP(fd,16));
		else //Color announce.
			clif_broadcast2(NULL, (char *) RFIFOP(fd,16), packet_len-16, RFIFOL(fd,4), RFIFOW(fd,8), RFIFOW(fd,10), RFIFOW(fd,12), RFIFOW(fd,14), ALL_CLIENT);
		break;
	case 0x3801:	intif_parse_WisMessage(fd); break;
	case 0x3802:	intif_parse_WisEnd(fd); break;
	case 0x3803:	mapif_parse_WisToGM(fd); break;
	case 0x3804:	intif_parse_Registers(fd); break;
	case 0x3806:	intif_parse_ChangeNameOk(fd); break;
	case 0x3809:	intif_parse_broadcast_obtain_special_item(fd); break;
	case 0x3818:	intif_parse_LoadGuildStorage(fd); break;
	
// Party System
	case 0x3819:	intif_parse_SaveGuildStorage(fd); break;
	case 0x3820:	intif_parse_PartyCreated(fd); break;
	case 0x3821:	intif_parse_PartyInfo(fd); break;
	case 0x3822:	intif_parse_PartyMemberAdded(fd); break;
	case 0x3823:	intif_parse_PartyOptionChanged(fd); break;
	case 0x3824:	intif_parse_PartyMemberWithdraw(fd); break;
	case 0x3825:	intif_parse_PartyMove(fd); break;
	case 0x3826:	intif_parse_PartyBroken(fd); break;
	case 0x3827:	intif_parse_PartyMessage(fd); break;
	
// Guild System
	case 0x3830:	intif_parse_GuildCreated(fd); break;
	case 0x3831:	intif_parse_GuildInfo(fd); break;
	case 0x3832:	intif_parse_GuildMemberAdded(fd); break;
	case 0x3834:	intif_parse_GuildMemberWithdraw(fd); break;
	case 0x3835:	intif_parse_GuildMemberInfoShort(fd); break;
	case 0x3836:	intif_parse_GuildBroken(fd); break;
	case 0x3837:	intif_parse_GuildMessage(fd); break;
	case 0x3839:	intif_parse_GuildBasicInfoChanged(fd); break;
	case 0x383a:	intif_parse_GuildMemberInfoChanged(fd); break;
	case 0x383b:	intif_parse_GuildPosition(fd); break;
	case 0x383c:	intif_parse_GuildSkillUp(fd); break;
	case 0x383d:	intif_parse_GuildAlliance(fd); break;
	case 0x383e:	intif_parse_GuildNotice(fd); break;
	case 0x383f:	intif_parse_GuildEmblem(fd); break;
	case 0x3840:	intif_parse_GuildCastleDataLoad(fd); break;
	case 0x3843:	intif_parse_GuildMasterChanged(fd); break;

#ifndef TXT_ONLY
// Mail System
	case 0x3848:	intif_parse_Mail_inboxreceived(fd); break;
	case 0x3849:	intif_parse_Mail_new(fd); break;
	case 0x384a:	intif_parse_Mail_getattach(fd); break;
	case 0x384b:	intif_parse_Mail_delete(fd); break;
	case 0x384c:	intif_parse_Mail_return(fd); break;
	case 0x384d:	intif_parse_Mail_send(fd); break;
	case 0x384e:	intif_parse_Mail_receiver(fd); break;
// Auction System
	case 0x3850:	intif_parse_Auction_results(fd); break;
	case 0x3851:	intif_parse_Auction_register(fd); break;
	case 0x3852:	intif_parse_Auction_cancel(fd); break;
	case 0x3853:	intif_parse_Auction_close(fd); break;
	case 0x3854:	intif_parse_Auction_message(fd); break;
	case 0x3855:	intif_parse_Auction_bid(fd); break;
#endif

//Bound items 
#ifdef BOUND_ITEMS 
	case 0x3856:    intif_parse_itembound_ack(fd); break;
	case 0x3857:	intif_parse_itembound_store2gstorage(fd); break;
#endif

// Quest system
	case 0x3860:	intif_parse_questlog(fd); break;
	case 0x3861:	intif_parse_questsave(fd); break;

// Achievement system
	case 0x3862:	intif_parse_achievements_load(fd); break;

// Mercenary System
	case 0x3870:	intif_parse_mercenary_received(fd); break;
	case 0x3871:	intif_parse_mercenary_deleted(fd); break;
	case 0x3872:	intif_parse_mercenary_saved(fd); break;

// Elemental System
	case 0x3878:	intif_parse_elemental_received(fd); break;
	case 0x3879:	intif_parse_elemental_deleted(fd); break;
	case 0x387a:	intif_parse_elemental_saved(fd); break;

// Pet System
	case 0x3880:	intif_parse_CreatePet(fd); break;
	case 0x3881:	intif_parse_RecvPetData(fd); break;
	case 0x3882:	intif_parse_SavePetOk(fd); break;
	case 0x3883:	intif_parse_DeletePetOk(fd); break;

// Storage
	case 0x388a:	intif_parse_StorageReceived(fd); break;
	case 0x388b:	intif_parse_StorageSaved(fd); break;
	case 0x388c:	intif_parse_StorageInfo_recv(fd); break;

// Homunculus System
	case 0x3890:	intif_parse_CreateHomunculus(fd); break;
	case 0x3891:	intif_parse_RecvHomunculusData(fd); break;
	case 0x3892:	intif_parse_SaveHomunculusOk(fd); break;
	case 0x3893:	intif_parse_DeleteHomunculusOk(fd); break;

// Clan system
	case 0x38A0:	intif_parse_clans(fd); break;
	case 0x38A1:	intif_parse_clan_message(fd); break;
	case 0x38A2:	intif_parse_clan_onlinecount(fd); break;

	default:
		ShowError("intif_parse : unknown packet %d %x\n", fd, RFIFOW(fd, 0));
		return 0;
	}

	// Skip packet
	RFIFOSKIP(fd, packet_len);

	return 1;
}
