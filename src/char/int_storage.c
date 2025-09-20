// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/db.h"
#include "../common/lock.h"
#include "../common/showmsg.h"
#include "../common/utils.h"
#include "char.h"
#include "inter.h"
#include "int_storage.h"
#include "int_pet.h"
#include "int_guild.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ファイル名のデフォルト
// inter_config_read()で再設定される
char storage_txt[1024]="save/storage.txt";
char guild_storage_txt[1024]="save/g_storage.txt";

static DBMap* storage_db = NULL; // uint32 account_id -> struct storage_data*
static DBMap* guild_storage_db = NULL; // int guild_id -> struct guild_storage*

/**
* Save inventory entries to TXT
* @param char_id: Character ID to save
* @param p: Inventory entries
* @return 0 if success, or error count
*/
static int inventory_totxt(uint32 char_id, struct s_storage* p)
{
	return char_memitemdata_to_txt(p->u.items_inventory, MAX_INVENTORY, char_id, TABLE_INVENTORY);
}

/**
* Save storage entries to TXT
* @param char_id: Character ID to save
* @param p: Storage entries
* @return 0 if success, or error count
*/
static int storage_totxt(uint32 account_id, struct s_storage* p)
{
	return char_memitemdata_to_txt(p->u.items_storage, MAX_STORAGE, account_id, TABLE_STORAGE);
}

#ifndef TXT_SQL_CONVERT
/**
* Save cart entries to TXT
* @param char_id: Character ID to save
* @param p: Cart entries
* @return 0 if success, or error count
*/
static int cart_totxt(uint32 char_id, struct s_storage* p)
{
	return char_memitemdata_to_txt(p->u.items_cart, MAX_CART, char_id, TABLE_CART);
}

/**
* Fetch inventory entries from TXT
* @param char_id: Character ID to fetch
* @param p: Inventory list to save the entries
* @return True if success, False if failed
*/
static bool inventory_fromtxt(uint32 char_id, struct s_storage* p)
{
	return char_memitemdata_from_txt(p, MAX_INVENTORY, char_id, TABLE_INVENTORY);
}

/**
* Fetch cart entries from TXT
* @param char_id: Character ID to fetch
* @param p: Cart list to save the entries
* @return True if success, False if failed
*/
static bool cart_fromtxt(uint32 char_id, struct s_storage* p)
{
	return char_memitemdata_from_txt(p, MAX_CART, char_id, TABLE_CART);
}

/**
* Fetch storage entries from TXT
* @param char_id: Character ID to fetch
* @param p: Storage list to save the entries
* @return True if success, False if failed
*/
static bool storage_fromtxt(uint32 account_id, struct s_storage* p)
{
	return char_memitemdata_from_txt(p, MAX_STORAGE, account_id, TABLE_STORAGE);
}

/**
* Save guild_storage data to TXT
* @param guild_id: Character ID to save
* @param p: Guild Storage list to save the entries
* @return 0 if success, or error count
*/
bool guild_storage_totxt(int guild_id, struct s_storage* p)
{
	//ShowInfo("Guild Storage has been saved (GID: %d)\n", guild_id);
	return char_memitemdata_to_txt(p->u.items_guild, MAX_GUILD_STORAGE, guild_id, TABLE_GUILD_STORAGE);
}

/**
* Fetch guild_storage entries from TXT
* @param char_id: Character ID to fetch
* @param p: Storage list to save the entries
* @return True if success, False if failed
*/
bool guild_storage_fromtxt(int guild_id, struct s_storage* p)
{
	return char_memitemdata_from_sql(p, MAX_GUILD_STORAGE, guild_id, TABLE_GUILD_STORAGE);
}

static void* create_storage(DBKey key, va_list args)
{
	return (struct storage_data *) aCalloc(1, sizeof(struct storage_data));
}

static void* create_guildstorage(DBKey key, va_list args)
{
	struct guild_storage* gs = NULL;
	gs = (struct guild_storage *) aCalloc(1, sizeof(struct guild_storage));
	gs->guild_id=key.i;
	return gs;
}

/// Loads storage data into the provided data structure.
/// If data doesn't exist, the destination is zeroed and false is returned.
bool storage_load(uint32 account_id, struct storage_data* storage)
{
	struct storage_data* s = (struct storage_data*)idb_get(storage_db, account_id);

	if( s != NULL )
		memcpy(storage, s, sizeof(*storage));
	else
		memset(storage, 0x00, sizeof(*storage));

	return( s != NULL );
}

/// Writes provided data into storage cache.
/// If data contains 0 items, any existing entry in cache is destroyed.
/// If data contains 1+ items and no cache entry exists, a new one is created.
bool storage_save(uint32 account_id, struct storage_data* storage)
{
	if( storage->storage_amount > 0 )
	{
		struct storage_data* s = (struct storage_data*)idb_ensure(storage_db, account_id, create_storage);
		memcpy(s, storage, sizeof(*storage));
	}
	else
	{
		idb_remove(storage_db, account_id);
	}

	return true;
}

//---------------------------------------------------------
// 倉庫データを読み込む
int inter_storage_init()
{
	char line[65536];
	int c = 0;
	FILE *fp;

	storage_db = idb_alloc(DB_OPT_RELEASE_DATA);

	fp=fopen(storage_txt,"r");
	if(fp==NULL){
		ShowError("can't read : %s\n",storage_txt);
		return 1;
	}
	while( fgets(line, sizeof(line), fp) )
	{
		uint32 account_id;
		struct storage_data *s;

		s = (struct storage_data*)aCalloc(1, sizeof(struct storage_data));
		if( s == NULL )
		{
			ShowFatalError("int_storage: out of memory!\n");
			exit(EXIT_FAILURE);
		}

		if( storage_fromstr(line,&account_id,s) )
		{
			idb_put(storage_db,account_id,s);
		}
		else{
			ShowError("int_storage: broken data in [%s] line %d\n",storage_txt,c);
			aFree(s);
		}
		c++;
	}
	fclose(fp);

	c = 0;
	guild_storage_db = idb_alloc(DB_OPT_RELEASE_DATA);

	fp=fopen(guild_storage_txt,"r");
	if(fp==NULL){
		ShowError("can't read : %s\n",guild_storage_txt);
		return 1;
	}
	while(fgets(line, sizeof(line), fp))
	{
		int tmp_int;
		struct guild_storage *gs;

		sscanf(line,"%d",&tmp_int);
		gs = (struct guild_storage*)aCalloc(1, sizeof(struct guild_storage));
		if(gs==NULL){
			ShowFatalError("int_storage: out of memory!\n");
			exit(EXIT_FAILURE);
		}
//		memset(gs,0,sizeof(struct guild_storage)); aCalloc...
		gs->guild_id=tmp_int;
		if(gs->guild_id > 0 && guild_storage_fromstr(line,gs) == 0) {
			idb_put(guild_storage_db,gs->guild_id,gs);
		}
		else{
			ShowError("int_storage: broken data [%s] line %d\n",guild_storage_txt,c);
			aFree(gs);
		}
		c++;
	}
	fclose(fp);

	return 0;
}

void inter_storage_final() {
	if(storage_db)
	{
		storage_db->destroy(storage_db, NULL);
	}
	if(guild_storage_db)
	{
		guild_storage_db->destroy(guild_storage_db, NULL);
	}
	return;
}

//---------------------------------------------------------
// 倉庫データを書き込む
int inter_storage_save()
{
	struct DBIterator* iter;
	DBKey key;
	struct storage_data* data;
	FILE *fp;
	int lock;
	if( (fp=lock_fopen(storage_txt,&lock))==NULL ){
		ShowError("int_storage: can't write [%s] !!! data is lost !!!\n",storage_txt);
		return 1;
	}

	iter = storage_db->iterator(storage_db);
	for( data = (struct storage_data*)iter->first(iter,&key); iter->exists(iter); data = (struct storage_data*)iter->next(iter,&key) )
	{
		uint32 account_id = key.i;
		char line[65536];
		storage_tostr(line,account_id,data);
		fprintf(fp,"%s\n",line);
 	}
	iter->destroy(iter);

	lock_fclose(fp,storage_txt,&lock);
	return 0;
}

//---------------------------------------------------------
// 倉庫データを書き込む
int inter_guild_storage_save()
{
	struct DBIterator* iter;
	struct guild_storage* data;
	FILE *fp;
	int  lock;
	if( (fp=lock_fopen(guild_storage_txt,&lock))==NULL ){
		ShowError("int_storage: can't write [%s] !!! data is lost !!!\n",guild_storage_txt);
		return 1;
	}

	iter = guild_storage_db->iterator(guild_storage_db);
	for( data = (struct guild_storage*)iter->first(iter,NULL); iter->exists(iter); data = (struct guild_storage*)iter->next(iter,NULL) )
	{
		char line[65536];
		if(inter_guild_search(data->guild_id) != NULL)
		{
			guild_storage_tostr(line,data);
			if(*line)
				fprintf(fp,"%s\n",line);
		}
	}
	iter->destroy(iter);

	lock_fclose(fp,guild_storage_txt,&lock);
	return 0;
}

// 倉庫データ削除
int inter_storage_delete(uint32 account_id)
{
	struct storage_data *s = (struct storage_data*)idb_get(storage_db,account_id);
	if(s) {
		int i;
		for(i=0;i<s->storage_amount;i++){
			if(s->items[i].card[0] == (short)0xff00)
				inter_pet_delete( MakeDWord(s->items[i].card[1],s->items[i].card[2]) );
		}
		idb_remove(storage_db,account_id);
	}
	return 0;
}

// ギルド倉庫データ削除
int inter_guild_storage_delete(int guild_id)
{
	struct guild_storage *gs = (struct guild_storage*)idb_get(guild_storage_db,guild_id);
	if(gs) {
		int i;
		for(i=0;i<gs->storage_amount;i++){
			if(gs->items[i].card[0] == (short)0xff00)
				inter_pet_delete( MakeDWord(gs->items[i].card[1],gs->items[i].card[2]) );
		}
		idb_remove(guild_storage_db,guild_id);
	}
	return 0;
}

struct guild_storage *guild2storage(int guild_id)
{
	struct guild_storage* gs = NULL;
	if(inter_guild_search(guild_id) != NULL)
		gs = (struct guild_storage*)idb_ensure(guild_storage_db, guild_id, create_guildstorage);
	return gs;
}

//---------------------------------------------------------
// map serverへの通信

int mapif_load_guild_storage(int fd,uint32 account_id,int guild_id)
{
	struct guild_storage *gs=guild2storage(guild_id);
	WFIFOHEAD(fd, sizeof(struct guild_storage)+12);
	WFIFOW(fd,0)=0x3818;
	if(gs) {
		WFIFOW(fd,2)=sizeof(struct guild_storage)+12;
		WFIFOL(fd,4)=account_id;
		WFIFOL(fd,8)=guild_id;
		memcpy(WFIFOP(fd,12),gs,sizeof(struct guild_storage));
	}
	else {
		WFIFOW(fd,2)=12;
		WFIFOL(fd,4)=account_id;
		WFIFOL(fd,8)=0;
	}
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}

int mapif_save_guild_storage_ack(int fd,uint32 account_id,int guild_id,int fail)
{
	WFIFOHEAD(fd,11);
	WFIFOW(fd,0)=0x3819;
	WFIFOL(fd,2)=account_id;
	WFIFOL(fd,6)=guild_id;
	WFIFOB(fd,10)=fail;
	WFIFOSET(fd,11);
	return 0;
}

//---------------------------------------------------------
// map serverからの通信

int mapif_parse_LoadGuildStorage(int fd)
{
	mapif_load_guild_storage(fd,RFIFOL(fd,2),RFIFOL(fd,6));
	return 0;
}

int mapif_parse_SaveGuildStorage(int fd)
{
	struct guild_storage *gs;
	int guild_id, len;

	guild_id=RFIFOL(fd,8);
	len=RFIFOW(fd,2);
	if(sizeof(struct guild_storage)!=len-12){
		ShowError("inter storage: data size error %d %d\n",sizeof(struct guild_storage),len-12);
	}
	else {
		gs=guild2storage(guild_id);
		if(gs) {
			memcpy(gs,RFIFOP(fd,12),sizeof(struct guild_storage));
			mapif_save_guild_storage_ack(fd,RFIFOL(fd,4),guild_id,0);
		}
		else
			mapif_save_guild_storage_ack(fd,RFIFOL(fd,4),guild_id,1);
	}
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
	WFIFOHEAD(fd, 8);
	WFIFOW(fd, 0) = 0x388b;
	WFIFOL(fd, 2) = account_id;
	WFIFOB(fd, 6) = sucess;
	WFIFOB(fd, 7) = type;
	WFIFOSET(fd, 8);
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
	type = RFIFOB(fd, 2);
	aid = RFIFOL(fd, 3);
	cid = RFIFOL(fd, 7);

	memset(&stor, 0, sizeof(struct s_storage));

	//ShowInfo("Loading storage for AID=%d.\n", aid);
	switch (type)
	{
	case TABLE_INVENTORY:
		res = inventory_fromtxt(cid, &stor);
		break;
	case TABLE_STORAGE:
		res = storage_fromtxt(aid, &stor);
		break;
	case TABLE_CART:
		res = cart_fromtxt(cid, &stor);
		break;
	default:
		return false;
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
	switch (type){
	case TABLE_INVENTORY:	inventory_totxt(cid, &stor); break;
	case TABLE_STORAGE:		storage_totxt(aid, &stor); break;
	case TABLE_CART:		cart_totxt(cid, &stor); break;
	default: return false;
	}
	mapif_storage_saved(fd, aid, cid, true, type);
	return false;
}


/*==========================================
* Parse packet from map-server
*------------------------------------------*/
int inter_storage_parse_frommap(int fd)
{
	switch(RFIFOW(fd,0)){
	case 0x3018: mapif_parse_LoadGuildStorage(fd); break;
	case 0x3019: mapif_parse_SaveGuildStorage(fd); break;
	case 0x308a: mapif_parse_StorageLoad(fd); break;
	case 0x308b: mapif_parse_StorageSave(fd); break;
	default:
		return 0;
	}
	return 1;
}
#endif //TXT_SQL_CONVERT
