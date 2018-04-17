// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h" // StringBuf
#include "../common/sql.h"
#include "char.h"
#include "inter.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define STORAGE_MEMINC	16

/**
 * Save inventory entries to SQL
 * @param char_id: Character ID to save
 * @param p: Inventory entries
 * @return 0 if success, or error count
 */
static int inventory_tosql(uint32 char_id, struct s_storage* p)
{
	return memitemdata_to_sql(p->u.items_inventory, MAX_INVENTORY, char_id, TABLE_INVENTORY);
}

/**
 * Save storage entries to SQL
 * @param char_id: Character ID to save
 * @param p: Storage entries
 * @return 0 if success, or error count
 */
static int storage_tosql(uint32 account_id, struct s_storage* p)
{
	return memitemdata_to_sql(p->u.items_storage, MAX_STORAGE, account_id, TABLE_STORAGE);
}

#ifndef TXT_SQL_CONVERT
/**
 * Save cart entries to SQL
 * @param char_id: Character ID to save
 * @param p: Cart entries
 * @return 0 if success, or error count
 */
static int cart_tosql(uint32 char_id, struct s_storage* p)
{
	return memitemdata_to_sql(p->u.items_cart, MAX_CART, char_id, TABLE_CART);
}

/**
 * Fetch inventory entries from table
 * @param char_id: Character ID to fetch
 * @param p: Inventory list to save the entries
 * @return True if success, False if failed
 */
static bool inventory_fromsql(uint32 char_id, struct s_storage* p)
{
	int i;
	StringBuf buf;
	SqlStmt* stmt;
	struct item tmp_item;

	memset(p, 0, sizeof(struct s_storage)); //clean up memory
	p->id = char_id;
	p->type = TABLE_INVENTORY;

	stmt = SqlStmt_Malloc(sql_handle);
	if (stmt == NULL) {
		SqlStmt_ShowDebug(stmt);
		return false;
	}

	// storage {`account_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`expire_time`/`favorite`/`bound`/`card0`/`card1`/`card2`/`card3`/`option_id0`/`option_val0`/`option_parm0`/`option_id1`/`option_val1`/`option_parm1`/`option_id2`/`option_val2`/`option_parm2`/`option_id3`/`option_val3`/`option_parm3`/`option_id4`/`option_val4`/`option_parm4`} 
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `favorite`, `bound`");
	for( i = 0; i < MAX_SLOTS; ++i )
		StringBuf_Printf(&buf, ", `card%d`", i);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`=? LIMIT %d", inventory_db, MAX_INVENTORY);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
		||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		||	SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return false;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,       &tmp_item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,    &tmp_item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,     &tmp_item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,      &tmp_item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,      &tmp_item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,      &tmp_item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,      &tmp_item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,      &tmp_item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,      &tmp_item.favorite,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 9, SQLDT_CHAR,      &tmp_item.bound,       0, NULL, NULL);
	for( i = 0; i < MAX_SLOTS; ++i ){
		SqlStmt_BindColumn(stmt, 10+i, SQLDT_USHORT, &tmp_item.card[i], 0, NULL, NULL);
	}
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 10+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].id, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].value, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 12+MAX_SLOTS+i*3, SQLDT_CHAR, &tmp_item.option[i].param, 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);
	}

	for( i = 0; i < MAX_INVENTORY && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->u.items_inventory[i], &tmp_item, sizeof(tmp_item));

	p->amount = i;
	ShowInfo("Loaded inventory data from DB - CID: %d (total: %d)\n", char_id, p->amount);

	SqlStmt_FreeResult(stmt);
	SqlStmt_Free(stmt);
	StringBuf_Destroy(&buf);
	return true;
}

/**
 * Fetch cart entries from table
 * @param char_id: Character ID to fetch
 * @param p: Cart list to save the entries
 * @return True if success, False if failed
 */
static bool cart_fromsql(uint32 char_id, struct s_storage* p)
{
	int i,j;
	StringBuf buf;
	SqlStmt* stmt;
	struct item tmp_item;

	memset(p, 0, sizeof(struct s_storage)); //clean up memory
	p->id = char_id;
	p->type = TABLE_CART;

	stmt = SqlStmt_Malloc(sql_handle);
	if (stmt == NULL) {
		SqlStmt_ShowDebug(stmt);
		return false;
	}

	// storage {`char_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`expire_time`/`bound`/`card0`/`card1`/`card2`/`card3`/`option_id0`/`option_val0`/`option_parm0`/`option_id1`/`option_val1`/`option_parm1`/`option_id2`/`option_val2`/`option_parm2`/`option_id3`/`option_val3`/`option_parm3`/`option_id4`/`option_val4`/`option_parm4`} 
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`=? ORDER BY `id` LIMIT %d", cart_db, MAX_CART);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
		||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		||	SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return false;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,         &tmp_item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,      &tmp_item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,       &tmp_item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,        &tmp_item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,        &tmp_item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,        &tmp_item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,        &tmp_item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,        &tmp_item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,        &tmp_item.bound,       0, NULL, NULL);
	for( i = 0; i < MAX_SLOTS; ++i )
		SqlStmt_BindColumn(stmt, 9+i, SQLDT_USHORT, &tmp_item.card[i], 0, NULL, NULL);

	for( i = 0; i < MAX_ITEM_RDM_OPT; i++ ) {
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 9+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].id, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].value, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11+MAX_SLOTS+i*3, SQLDT_CHAR, &tmp_item.option[i].param, 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);
	}

	for( i = 0; i < MAX_CART && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->u.items_cart[i], &tmp_item, sizeof(tmp_item));

	p->amount = i;
	ShowInfo("Loaded Cart data from DB - CID: %d (total: %d)\n", char_id, p->amount);

	SqlStmt_FreeResult(stmt);
	SqlStmt_Free(stmt);

	StringBuf_Destroy(&buf);
	return true;
}

/**
 * Fetch storage entries from table
 * @param char_id: Character ID to fetch
 * @param p: Storage list to save the entries
 * @return True if success, False if failed
 */
static bool storage_fromsql(uint32 account_id, struct s_storage* p)
{
	int i, j;
	StringBuf buf;
	SqlStmt* stmt;
	struct item tmp_item;

	memset(p, 0, sizeof(struct s_storage)); //clean up memory
	p->id = account_id;
	p->type = TABLE_STORAGE;

	stmt = SqlStmt_Malloc(sql_handle);
	if (stmt == NULL) {
		SqlStmt_ShowDebug(stmt);
		return false;
	}

	// storage {`char_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`expire_time`/`bound`/`card0`/`card1`/`card2`/`card3`/`option_id0`/`option_val0`/`option_parm0`/`option_id1`/`option_val1`/`option_parm1`/`option_id2`/`option_val2`/`option_parm2`/`option_id3`/`option_val3`/`option_parm3`/`option_id4`/`option_val4`/`option_parm4`} 
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	for( i = 0; i < MAX_ITEM_RDM_OPT; ++i ) {
		StringBuf_Printf(&buf, ", `option_id%d`", i);
		StringBuf_Printf(&buf, ", `option_val%d`", i);
		StringBuf_Printf(&buf, ", `option_parm%d`", i);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `account_id`=? ORDER BY `nameid` LIMIT %d", storage_db, account_id, MAX_STORAGE);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
		||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &account_id, 0)
		||	SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return false;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,           &tmp_item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,        &tmp_item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,         &tmp_item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,          &tmp_item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,          &tmp_item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,          &tmp_item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,          &tmp_item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,          &tmp_item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,          &tmp_item.bound,       0, NULL, NULL);
	for( i = 0; i < MAX_SLOTS; ++i )
		SqlStmt_BindColumn(stmt, 9+i, SQLDT_USHORT, &tmp_item.card[i],     0, NULL, NULL);
	for( i = 0; i < MAX_ITEM_RDM_OPT; i++ ) {
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 9+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].id, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].value, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11+MAX_SLOTS+i*3, SQLDT_CHAR, &tmp_item.option[i].param, 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);
	}

	for( i = 0; i < MAX_STORAGE && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->u.items_storage[i], &tmp_item, sizeof(tmp_item));

	p->amount = i;
	ShowInfo("Loaded Storage data from DB - AID: %d (total: %d)\n", account_id, p->amount);

	SqlStmt_FreeResult(stmt);
	SqlStmt_Free(stmt);
	StringBuf_Destroy(&buf);
	return true;
}
#endif //TXT_SQL_CONVERT

/**
 * Save guild_storage data to sql
 * @param guild_id: Character ID to save
 * @param p: Guild Storage list to save the entries
 * @return 0 if success, or error count
 */
bool guild_storage_tosql(int guild_id, struct s_storage* p)
{
	//ShowInfo("Guild Storage has been saved (GID: %d)\n", guild_id);
	return memitemdata_to_sql(p->u.items_guild, MAX_GUILD_STORAGE, guild_id, TABLE_GUILD_STORAGE);
}

#ifndef TXT_SQL_CONVERT
/**
 * Fetch guild_storage entries from table
 * @param char_id: Character ID to fetch
 * @param p: Storage list to save the entries
 * @return True if success, False if failed
 */
bool guild_storage_fromsql(int guild_id, struct s_storage* p)
{
	int i,j;
	StringBuf buf;
	SqlStmt* stmt;
	struct item tmp_item;

	memset(p, 0, sizeof(struct s_storage)); //clean up memory
	p->id = guild_id;
	p->type = TABLE_GUILD_STORAGE;

	stmt = SqlStmt_Malloc(sql_handle);
	if (stmt == NULL) {
		SqlStmt_ShowDebug(stmt);
		return false;
	}

	// storage {`guild_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`expire_time`/`bound`/`card0`/`card1`/`card2`/`card3`/`option_id0`/`option_val0`/`option_parm0`/`option_id1`/`option_val1`/`option_parm1`/`option_id2`/`option_val2`/`option_parm2`/`option_id3`/`option_val3`/`option_parm3`/`option_id4`/`option_val4`/`option_parm4`} 
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`,`nameid`,`amount`,`equip`,`identify`,`refine`,`attribute`,`expire_time`,`bound`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ",`card%d`", j);
	for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ) {
		StringBuf_Printf(&buf, ", `option_id%d`", j);
		StringBuf_Printf(&buf, ", `option_val%d`", j);
		StringBuf_Printf(&buf, ", `option_parm%d`", j);
	}

	StringBuf_Printf(&buf, " FROM `%s` WHERE `guild_id`='%d' ORDER BY `nameid`", guild_storage_db, guild_id);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
		||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &guild_id, 0)
		||	SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return false;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,          &tmp_item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,       &tmp_item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,        &tmp_item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,         &tmp_item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,         &tmp_item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,         &tmp_item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,         &tmp_item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,         &tmp_item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,         &tmp_item.bound,       0, NULL, NULL);
	tmp_item.expire_time = 0;
	for( i = 0; i < MAX_SLOTS; ++i )
		SqlStmt_BindColumn(stmt, 9+i, SQLDT_USHORT, &tmp_item.card[i],   0, NULL, NULL);
	for( i = 0; i < MAX_ITEM_RDM_OPT; i++ ) {
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 9+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].id, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10+MAX_SLOTS+i*3, SQLDT_SHORT, &tmp_item.option[i].value, 0, NULL, NULL)
		||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11+MAX_SLOTS+i*3, SQLDT_CHAR, &tmp_item.option[i].param, 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);
	}

	for( i = 0; i < MAX_GUILD_STORAGE && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->u.items_guild[i], &tmp_item, sizeof(tmp_item));

	p->amount = i;
	ShowInfo("Loaded Guild Storage data from DB - GID: %d (total: %d)\n", guild_id, p->amount);

	SqlStmt_FreeResult(stmt);
	SqlStmt_Free(stmt);
	StringBuf_Destroy(&buf);
	return true;
}

//---------------------------------------------------------
// storage data initialize
void inter_storage_sql_init(void)
{
	return;
}

// storage data finalize
void inter_storage_sql_final(void)
{
	return;
}

// q?f[^?
void inter_storage_delete(int account_id)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id`='%d'", storage_db, account_id) )
		Sql_ShowDebug(sql_handle);
	return;
}
void inter_guild_storage_delete(int guild_id)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `guild_id`='%d'", guild_storage_db, guild_id) )
		Sql_ShowDebug(sql_handle);
	return;
}

//---------------------------------------------------------
// packet from map server

bool mapif_load_guild_storage(int fd,int account_id,int guild_id, char flag)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `guild_id` FROM `%s` WHERE `guild_id`='%d'", guild_db, guild_id) )
		Sql_ShowDebug(sql_handle);
	else if( Sql_NumRows(sql_handle) > 0 )
	{// guild exists
		Sql_FreeResult(sql_handle);
		WFIFOHEAD(fd, sizeof(struct s_storage)+13);
		WFIFOW(fd,0) = 0x3818;
		WFIFOW(fd,2) = sizeof(struct s_storage)+13;
		WFIFOL(fd,4) = account_id;
		WFIFOL(fd,8) = guild_id;
		WFIFOB(fd,12) = flag; //1 open storage, 0 don't open 
		guild_storage_fromsql(guild_id, (struct s_storage*)WFIFOP(fd,13));
		WFIFOSET(fd, WFIFOW(fd,2));
		return false;
	}
	// guild does not exist
	Sql_FreeResult(sql_handle);
	WFIFOHEAD(fd, 12);
	WFIFOW(fd,0) = 0x3818;
	WFIFOW(fd,2) = 12;
	WFIFOL(fd,4) = account_id;
	WFIFOL(fd,8) = 0;
	WFIFOSET(fd, 12);
	return true;
}
void mapif_save_guild_storage_ack(int fd,int account_id,int guild_id,int fail)
{
	WFIFOHEAD(fd,11);
	WFIFOW(fd,0)=0x3819;
	WFIFOL(fd,2)=account_id;
	WFIFOL(fd,6)=guild_id;
	WFIFOB(fd,10)=fail;
	WFIFOSET(fd,11);
}

//---------------------------------------------------------
// packet from map server

void mapif_parse_LoadGuildStorage(int fd)
{
	mapif_load_guild_storage(fd,RFIFOL(fd,2),RFIFOL(fd,6),1);
}

bool mapif_parse_SaveGuildStorage(int fd)
{
	int guild_id;
	int len;

	guild_id = RFIFOL(fd,8);
	len = RFIFOW(fd,2);

	if( sizeof(struct s_storage) != len - 12 )
	{
		ShowError("inter storage: data size error %d != %d\n", sizeof(struct s_storage), len - 12);
	}
	else
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `guild_id` FROM `%s` WHERE `guild_id`='%d'", guild_db, guild_id) )
			Sql_ShowDebug(sql_handle);
		else if( Sql_NumRows(sql_handle) > 0 )
		{// guild exists
			Sql_FreeResult(sql_handle);
			guild_storage_tosql(guild_id, (struct s_storage*)RFIFOP(fd,12));
			mapif_save_guild_storage_ack(fd, RFIFOL(fd,4), guild_id, 0);
			return false;
		}
		Sql_FreeResult(sql_handle);
	}
	mapif_save_guild_storage_ack(fd, RFIFOL(fd,4), guild_id, 1);
	return true;
}

#ifdef BOUND_ITEMS 
int mapif_itembound_ack(int fd, int aid, int guild_id) 
{ 
	WFIFOHEAD(fd,8); 
	WFIFOW(fd,0) = 0x3856; 
	WFIFOL(fd,2) = aid; 
	WFIFOW(fd,6) = guild_id; 
	WFIFOSET(fd,8); 
	return 0; 
} 
#endif

//------------------------------------------------ 
//Guild bound items pull for offline characters [Akinari] 
//------------------------------------------------ 
int mapif_parse_itembound_retrieve(int fd) 
{ 
	StringBuf buf; 
	SqlStmt* stmt; 
	struct item item; 
	int j, i=0, s; 
	bool found=false; 
	struct item items[MAX_INVENTORY]; 
	int char_id = RFIFOL(fd,2); 
	int aid = RFIFOL(fd,6); 
	int guild_id = RFIFOW(fd,10); 

	StringBuf_Init(&buf); 
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`"); 
	for( j = 0; j < MAX_SLOTS; ++j ) 
		StringBuf_Printf(&buf, ", `card%d`", j);
	for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ) {
		StringBuf_Printf(&buf, ", `option_id%d`", j);
		StringBuf_Printf(&buf, ", `option_val%d`", j);
		StringBuf_Printf(&buf, ", `option_parm%d`", j);
	}
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`='%d'",inventory_db,char_id); 

	stmt = SqlStmt_Malloc(sql_handle); 
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf)) 
	||  SQL_ERROR == SqlStmt_Execute(stmt) ) 
	{ 
		SqlStmt_ShowDebug(stmt); 
		SqlStmt_Free(stmt); 
		StringBuf_Destroy(&buf); 
		return 1; 
	} 

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,       &item.id,          0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,    &item.nameid,      0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,     &item.amount,      0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,      &item.equip,       0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,      &item.identify,    0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,      &item.refine,      0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,      &item.attribute,   0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,      &item.expire_time, 0, NULL, NULL); 
	SqlStmt_BindColumn(stmt, 8, SQLDT_UINT,      &item.bound,       0, NULL, NULL); 
	for( j = 0; j < MAX_SLOTS; ++j ) 
		SqlStmt_BindColumn(stmt, 9+j, SQLDT_SHORT, &item.card[j], 0, NULL, NULL); 
	for( j = 0; j < MAX_ITEM_RDM_OPT; ++j ) {
		SqlStmt_BindColumn(stmt, 9+MAX_SLOTS+j*3, SQLDT_SHORT, &item.option[j].id, 0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 10+MAX_SLOTS+j*3, SQLDT_SHORT, &item.option[j].value, 0, NULL, NULL);
		SqlStmt_BindColumn(stmt, 11+MAX_SLOTS+j*3, SQLDT_CHAR, &item.option[j].param, 0, NULL, NULL);
	}

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) ) { 
		if(item.bound == 2) { 
			memcpy(&items[i],&item,sizeof(struct item)); 
			i++; 
		} 
	} 
     
	if(!i) //No items found - No need to continue 
		return 0; 

	//First we delete the character's items 
	StringBuf_Clear(&buf); 
	StringBuf_Printf(&buf, "DELETE FROM `%s` WHERE",inventory_db); 
	for(j=0; j<i; j++) { 
		if( found ) 
			StringBuf_AppendStr(&buf, " OR"); 
		else 
			found = true; 
		StringBuf_Printf(&buf, " `id`=%d",items[j].id); 
	} 

	stmt = SqlStmt_Malloc(sql_handle); 
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf)) 
	||  SQL_ERROR == SqlStmt_Execute(stmt) ) 
	{ 
		SqlStmt_ShowDebug(stmt); 
		SqlStmt_Free(stmt); 
		StringBuf_Destroy(&buf); 
		return 1; 
	} 

	//Now let's update the guild storage with those deleted items 
	found = false; 
	StringBuf_Clear(&buf); 
	StringBuf_Printf(&buf, "INSERT INTO `%s` (`guild_id`, `nameid`, `amount`, `identify`, `refine`, `attribute`, `expire_time`, `bound`", guild_storage_db); 
	for( j = 0; j < MAX_SLOTS; ++j ) 
		StringBuf_Printf(&buf, ", `card%d`", j); 
	StringBuf_AppendStr(&buf, ") VALUES "); 
       
	for( j = 0; j < i; ++j ) { 
		if( found ) 
			StringBuf_AppendStr(&buf, ","); 
		else 
			found = true; 

		StringBuf_Printf(&buf, "('%d', '%hu', '%d', '%d', '%d', '%d', '%d', '%d'", 
		guild_id, items[j].nameid, items[j].amount, items[j].identify, items[j].refine, items[j].attribute, items[j].expire_time, items[j].bound); 
		for( s = 0; s < MAX_SLOTS; ++s ) 
			StringBuf_Printf(&buf, ", '%hu'", items[j].card[s]); 
		StringBuf_AppendStr(&buf, ")"); 
	} 

	stmt = SqlStmt_Malloc(sql_handle); 
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf)) 
	||  SQL_ERROR == SqlStmt_Execute(stmt) ) 
	{ 
		SqlStmt_ShowDebug(stmt); 
		SqlStmt_Free(stmt); 
		StringBuf_Destroy(&buf); 
		return 1; 
	} 

	//Finally reload storage and tell map we're done 
	mapif_load_guild_storage(fd,aid,guild_id,0); 
	mapif_itembound_ack(fd,aid,guild_id); 
	return 0; 
} 

/*==========================================
 * Storages (Inventory/Storage/Cart)
 *------------------------------------------*/

/**
 * Sending inventory/cart/storage data to player
 * IZ 0x388a <size>.W <type>.B <account_id>.L <result>.B <inventory>.?B
 * @param fd
 * @param account_id
 * @param type
 * @param entries Inventory/cart/storage entries
 * @param result
 */
static void mapif_storage_data_loaded(int fd, uint32 account_id, char type, struct s_storage entries, bool result) {
	uint16 size = sizeof(struct s_storage) + 10;
	
	WFIFOHEAD(fd, size);
	WFIFOW(fd, 0) = 0x388a;
	WFIFOW(fd, 2) = size;
	WFIFOB(fd, 4) = type;
	WFIFOL(fd, 5) = account_id;
	WFIFOB(fd, 9) = result;
	memcpy(WFIFOP(fd, 10), &entries, sizeof(struct s_storage));
	WFIFOSET(fd, size);
}

/**
 * Tells player if inventory/cart/storage is saved
 * IZ 0x388b <account_id>.L <result>.B <type>.B
 * @param fd
 * @param account_id
 * @param type
 */
void mapif_storage_saved(int fd, uint32 account_id, uint32 char_id, bool sucess, char type) {
	WFIFOHEAD(fd,8);
	WFIFOW(fd, 0) = 0x388b;
	WFIFOL(fd, 2) = account_id;
	WFIFOB(fd, 6) = sucess;
	WFIFOB(fd, 7) = type;
	WFIFOSET(fd,8);

	if (type == TABLE_CART_)
	{
		struct s_storage stor;
		memset(&stor, 0, sizeof(struct s_storage));
		mapif_storage_data_loaded(fd, account_id, type, stor, cart_fromsql(char_id, &stor));
	}
}

/**
 * Requested inventory/cart/storage data for a player
 * ZI 0x308a <type>.B <account_id>.L <char_id>.L
 * @param fd
 */
bool mapif_parse_StorageLoad(int fd) {
	uint32 aid, cid;
	int type;
	struct s_storage stor;
	bool res = true;

	RFIFOHEAD(fd);
	type = RFIFOB(fd,2);
	aid = RFIFOL(fd,3);
	cid = RFIFOL(fd,7);

	memset(&stor, 0, sizeof(struct s_storage));

	//ShowInfo("Loading storage for AID=%d.\n", aid);
	switch (type) {
		case TABLE_INVENTORY: res = inventory_fromsql(cid, &stor); break;
		case TABLE_STORAGE:   res = storage_fromsql(aid, &stor);   break;
		case TABLE_CART:      res = cart_fromsql(cid, &stor);      break;
		default: return false;
	}
	mapif_storage_data_loaded(fd, aid, type, stor, res);
	return true;
}

/**
 * Asking to save player's inventory/cart/storage data
 * ZI 0x308b <size>.W <type>.B <account_id>.L <char_id>.L <entries>.?B
 * @param fd
 */
bool mapif_parse_StorageSave(int fd) {
	int aid, cid, type;
	struct s_storage stor;

	RFIFOHEAD(fd);
	type = RFIFOB(fd, 4);
	aid = RFIFOL(fd, 5);
	cid = RFIFOL(fd, 9);
	
	memset(&stor, 0, sizeof(struct s_storage));
	memcpy(&stor, RFIFOP(fd, 13), sizeof(struct s_storage));

	//ShowInfo("Saving storage data for AID=%d.\n", aid);
	switch(type){
		case TABLE_INVENTORY: inventory_tosql(cid, &stor); break;
		case TABLE_STORAGE:   storage_tosql(aid, &stor);   break;
		case TABLE_CART:
		case TABLE_CART_:
			cart_tosql(cid, &stor);
			break;
		default: return false;
	}
	mapif_storage_saved(fd, aid, cid, true, type);
	return false;
}


/*==========================================
 * Parse packet from map-server
 *------------------------------------------*/
bool inter_storage_parse_frommap(int fd)
{
	switch(RFIFOW(fd,0)){
		case 0x3018: mapif_parse_LoadGuildStorage(fd); break;
		case 0x3019: mapif_parse_SaveGuildStorage(fd); break;
#ifdef BOUND_ITEMS
		case 0x3056: mapif_parse_itembound_retrieve(fd); break;
#endif
		case 0x308a: mapif_parse_StorageLoad(fd); break;
		case 0x308b: mapif_parse_StorageSave(fd); break;
		default:
			return false;
	}
	return true;
}
#endif //TXT_SQL_CONVERT
