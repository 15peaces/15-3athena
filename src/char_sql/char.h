// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _CHAR_SQL_H_
#define _CHAR_SQL_H_

#include "../common/core.h" // CORE_ST_LAST
#include "../common/msg_conf.h"

struct mmo_charstatus;

#define MAX_MAP_SERVERS 30
#define DEFAULT_AUTOSAVE_INTERVAL 300*1000

#define msg_config_read(cfgName) char_msg_config_read(cfgName)
#define msg_txt(msg_number) char_msg_txt(msg_number)
#define do_final_msg() char_do_final_msg()
int char_msg_config_read(char *cfgName);
const char* char_msg_txt(int msg_number);
void char_do_final_msg(void);

struct mmo_map_server {
	int fd;
	uint32 ip;
	uint16 port;
	int users;
	unsigned short map[MAX_MAP_PER_SERVER];
};
extern struct mmo_map_server server[MAX_MAP_SERVERS];

struct char_session_data {
	bool auth; // whether the session is authed or not
	uint32 account_id, login_id1, login_id2;
	uint8 sex;
	int found_char[MAX_CHARS]; // ids of chars on this account
	char email[40]; // e-mail (default: a@a.com) by [Yor]
	time_t expiration_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	int gmlevel;
	uint32 version;
	uint8 clienttype;
	char new_name[NAME_LENGTH];
	char birthdate[10+1];  // YYYY-MM-DD
	time_t unban_time[MAX_CHARS];
	int charblock_timer;
	unsigned int char_moves[MAX_CHARS]; // character moves left
	char pincode[PINCODE_LENGTH + 1];
	uint32 pincode_seed;
	time_t pincode_change;
	uint16 pincode_try;
	uint8 flag; // &1 - Retrieving guild bound items
};

enum e_char_delete {
	CHAR_DEL_EMAIL = 1,
	CHAR_DEL_BIRTHDATE
};

enum e_char_delete_restriction {
	CHAR_DEL_RESTRICT_PARTY = 1,
	CHAR_DEL_RESTRICT_GUILD,
	CHAR_DEL_RESTRICT_ALL
};

enum e_char_del_response {
	CHAR_DELETE_OK = 0,
	CHAR_DELETE_DATABASE,
	CHAR_DELETE_NOTFOUND,
	CHAR_DELETE_BASELEVEL,
	CHAR_DELETE_GUILD,
	CHAR_DELETE_PARTY,
	CHAR_DELETE_TIME,
};

unsigned int char_server_fd(uint32 account_id);

int char_memitemdata_to_sql(const struct item items[], int max, int id, enum storage_type tableswitch, uint8 stor_id);
bool char_memitemdata_from_sql( struct s_storage* p, int max, int id, enum storage_type tableswitch, uint8 stor_id);

int mapif_sendall(unsigned char *buf,unsigned int len);
int mapif_sendallwos(int fd,unsigned char *buf,unsigned int len);
int mapif_send(int fd,unsigned char *buf,unsigned int len);

int char_married(int pl1,int pl2);
int char_child(int parent_id, int child_id);
int char_family(int pl1,int pl2,int pl3);

void disconnect_player(uint32 account_id);

int char_parse_ackchangesex(int fd, struct char_session_data* sd);
int char_parse_ackchangecharsex(uint32 char_id, int sex);

int request_accreg2(uint32 account_id, uint32 char_id);
int save_accreg2(unsigned char* buf, int len);

void moveCharSlot(int fd, struct char_session_data* sd, unsigned short from, unsigned short to);
void moveCharSlotReply(int fd, struct char_session_data* sd, unsigned short index, short reason);

extern int char_name_option;
extern char char_name_letters[];
extern bool char_gm_read;
extern int autosave_interval;
extern int save_log;
extern char db_path[];
extern char char_db[256];
extern char scdata_db[256];
extern char cart_db[256];
extern char inventory_db[256];
extern char charlog_db[256];
extern char storage_db[256];
extern char storage2_db[256];
extern char interlog_db[256];
extern char reg_db[256];
extern char skill_db[256];
extern char memo_db[256];
extern char guild_db[256];
extern char guild_alliance_db[256];
extern char guild_castle_db[256];
extern char guild_expulsion_db[256];
extern char guild_member_db[256];
extern char guild_position_db[256];
extern char guild_skill_db[256];
extern char guild_storage_db[256];
extern char clan_db[256];
extern char clan_alliance_db[256];
extern char party_db[256];
extern char pet_db[256];
extern char mail_db[256];
extern char mail_attachment_db[256];
extern char auction_db[256];
extern char quest_db[256];
extern char homunculus_db[256];
extern char skill_homunculus_db[256];
extern char mercenary_db[256];
extern char mercenary_owner_db[256];
extern char elemental_db[256];
extern char ragsrvinfo_db[256];
extern char achievement_db[256];

extern int db_use_sqldbs; // added for sql item_db read for char server [Valaris]

extern int guild_exp_rate;
extern int log_inter;

extern int mail_return_days;
extern int mail_delete_days;

void char_set_session_flag_(int account_id, int val, bool set);
#define char_set_session_flag(account_id, val)   ( char_set_session_flag_((account_id), (val), true)  )
#define char_unset_session_flag(account_id, val) ( char_set_session_flag_((account_id), (val), false) )

//For use in packets that depend on an sd being present [Skotlex]
#define FIFOSD_CHECK(rest) { if(RFIFOREST(fd) < rest) return 0; if (sd==NULL || !sd->auth) { RFIFOSKIP(fd,rest); return 0; } }

//Exported for use in the TXT-SQL converter.
int mmo_char_tosql(uint32 char_id, struct mmo_charstatus *p);
void sql_config_read(const char *cfgName);

#endif /* _CHAR_SQL_H_ */
