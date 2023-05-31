// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/db.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"

#include "map.h" // struct map_session_data
#include "storage.h"
#include "chrif.h"
#include "itemdb.h"
#include "clif.h"
#include "intif.h"
#include "pc.h"
#include "guild.h"
#include "battle.h"
#include "atcommand.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static DBMap* guild_storage_db; ///Databases of guild_storage : int guild_id -> struct guild_storage*

/*==========================================
 * Storage item comparator (for qsort)
 * sort by itemid and amount
 * @param _i1 : item a
 * @param _i2 : item b
 * @return i1<=>i2
 *------------------------------------------*/
static int storage_comp_item(const void *_i1, const void *_i2)
{
	struct item *i1 = (struct item *)_i1;
	struct item *i2 = (struct item *)_i2;

	if (i1->nameid == i2->nameid)
		return 0;
	else if (!(i1->nameid) || !(i1->amount))
		return 1;
	else if (!(i2->nameid) || !(i2->amount))
		return -1;
	return i1->nameid - i2->nameid;
}

/*==========================================
 * Sort item by storage_comp_item (nameid)
 * used when we open up our storage or guild_storage
 * @param items : list of items to sort
 * @param size : number of item in list
 *------------------------------------------*/
static void storage_sortitem(struct item* items, unsigned int size)
{
	nullpo_retv(items);

	if( battle_config.client_sort_storage )
	{
		qsort(items, size, sizeof(struct item), storage_comp_item);
	}
}

/*==========================================
 * Initiate storage module
 * Called from map.c::do_init()
 * @return 1
 *------------------------------------------*/
int do_init_storage(void)
{
	guild_storage_db=idb_alloc(DB_OPT_RELEASE_DATA);
	return 1;
}

/*==========================================
 * Destroy storage module
 * @author : [MC Cameri]
 * Called from map.c::do_final()
 *------------------------------------------*/
void do_final_storage(void)
{
	guild_storage_db->destroy(guild_storage_db,NULL);
}

/*==========================================
 * Parses storage and saves 'dirty' ones upon reconnect.
 * @author [Skotlex]
 * @see DBApply
 * @return 0
 *------------------------------------------*/
static int storage_reconnect_sub(DBKey key,DBData *data,va_list ap)
{ //Parses storage and saves 'dirty' ones upon reconnect. [Skotlex]

	struct s_storage* stor = db_data2ptr(data);
	if (stor->dirty && stor->status == 0) //Save closed storages.
		storage_guild_storagesave(0, stor->id, 0);

	return 0;
}

/*==========================================
 *Function to be invoked upon server reconnection to char. To save all 'dirty' storages [Skotlex]
 *------------------------------------------*/
void do_reconnect_storage(void)
{
	guild_storage_db->foreach(guild_storage_db, storage_reconnect_sub);
}

/*==========================================
 * Opens a storage. Returns:
 * 0 - success
 * 1 - fail
 *------------------------------------------*/
int storage_storageopen(struct map_session_data *sd)
{
	nullpo_ret(sd);

	if(sd->state.storage_flag)
		return 1; //Already open?
	
	if( !pc_can_give_items(pc_isGM(sd)) )
  	{ //check is this GM level is allowed to put items to storage
		clif_displaymessage(sd->fd, msg_txt(sd,246));
		return 1;
	}
	
	sd->state.storage_flag = 1;
	storage_sortitem(sd->storage.u.items_storage, MAX_STORAGE);
	clif_storagelist(sd, sd->storage.u.items_storage, MAX_STORAGE);
	clif_updatestorageamount(sd, sd->storage.amount, MAX_STORAGE);
	return 0;
}

/*==========================================
 * check if 2 item a and b have same values
 * @param a : item 1
 * @param b : item 2
 * @return 1:same, 0 : are different
 *------------------------------------------*/
int compare_item(struct item *a, struct item *b)
{
	if( a->nameid == b->nameid &&
		a->identify == b->identify &&
		a->refine == b->refine &&
		a->attribute == b->attribute &&
		a->expire_time == b->expire_time && 
		a->bound == b->bound) 
	{
		int i;
		for (i = 0; i < MAX_SLOTS && (a->card[i] == b->card[i]); i++);
		return (i == MAX_SLOTS);
	}
	return 0;
}

/*==========================================
 * Make a player add an item to his storage
 * @param sd : player
 * @param item_data : item to add
 * @param amount : quantity of items
 * @return 0:success, 1:failed
 *------------------------------------------*/
static int storage_additem(struct map_session_data* sd, struct item* item_data, int amount)
{
	struct s_storage* stor = &sd->storage;
	struct item_data *data;
	int i;

	if( item_data->nameid <= 0 || amount <= 0 )
		return 1;
	
	data = itemdb_search(item_data->nameid);

	if( !itemdb_canstore(item_data, pc_isGM(sd)) )
	{	//Check if item is storable. [Skotlex]
		clif_displaymessage (sd->fd, msg_txt(sd,264));
		return 1;
	}

	if( (item_data->bound > BOUND_ACCOUNT) && !pc_can_give_bounded_items(sd->gmlevel) ) {
		clif_displaymessage(sd->fd, msg_txt(sd,294)); 
		return 1; 
	}
	
	if( itemdb_isstackable2(data) )
	{//Stackable
		for( i = 0; i < MAX_STORAGE; i++ )
		{
			if( compare_item(&stor->u.items_storage[i], item_data) )
			{// existing items found, stack them
				if( amount > MAX_AMOUNT - stor->u.items_storage[i].amount )
					return 1;
				stor->u.items_storage[i].amount += amount;
				stor->dirty = true;
				clif_storageitemadded(sd,&stor->u.items_storage[i],i,amount);
				log_pick(&sd->bl, LOG_TYPE_STORAGE, item_data->nameid, -amount, item_data);
				return 0;
			}
		}
	}

	// find free slot
	ARR_FIND( 0, MAX_STORAGE, i, stor->u.items_storage[i].nameid == 0 );
	if( i >= MAX_STORAGE )
		return 1;

	// add item to slot
	memcpy(&stor->u.items_storage[i],item_data,sizeof(stor->u.items_storage[0]));
	stor->amount++;
	stor->u.items_storage[i].amount = amount;
	stor->dirty = true;
	clif_storageitemadded(sd,&stor->u.items_storage[i],i,amount);
	clif_updatestorageamount(sd, stor->amount, MAX_STORAGE);

	return 0;
}

/*==========================================
 * Make a player delete an item from his storage
 * @param sd : player
 * @param n : idx on storage to remove the item from
 * @param amount :number of item to remove
 * @return 0:sucess, 1:fail
 *------------------------------------------*/
int storage_delitem(struct map_session_data* sd, int n, int amount)
{
	if( sd->storage.u.items_storage[n].nameid == 0 || sd->storage.u.items_storage[n].amount < amount )
		return 1;

	sd->storage.u.items_storage[n].amount -= amount;
	sd->storage.dirty = true;

	if( sd->storage.u.items_storage[n].amount == 0 )
	{
		memset(&sd->storage.u.items_storage[n],0,sizeof(sd->storage.u.items_storage[0]));
		sd->storage.amount--;
		if( sd->state.storage_flag == 1 ) clif_updatestorageamount(sd, sd->storage.amount, MAX_STORAGE);
	}
	if( sd->state.storage_flag == 1 ) clif_storageitemremoved(sd,n,amount);
	return 0;
}

/**
 * Check if item can be added to storage
 * @param stor Storage data
 * @param idx Index item from inventory/cart
 * @param items List of items from inventory/cart
 * @param amount Amount of item will be added
 * @param max_num Max inventory/cart
 * @return @see enum e_storage_add
 **/
static enum e_storage_add storage_canAddItem(struct map_session_data* sd, int idx, struct item items[], int amount, int max_num) {
	if( sd->storage.amount > MAX_STORAGE )
		return STORAGE_ADD_NOROOM; // storage full

	if (idx < 0 || idx >= max_num)
		return STORAGE_ADD_INVALID;

	if (items[idx].nameid <= 0)
		return STORAGE_ADD_INVALID; // No item on that spot

	if (amount < 1 || amount > items[idx].amount)
		return STORAGE_ADD_INVALID;

	if (itemdb_ishatched_egg(&items[idx]))
		return STORAGE_ADD_INVALID;

	return STORAGE_ADD_OK;
}

/**
 * Check if item can be moved from storage
 * @param stor Storage data
 * @param idx Index from storage
 * @param amount Number of item
 * @return @see enum e_storage_add
 **/
static enum e_storage_add storage_canGetItem(struct map_session_data *sd, int idx, int amount) {
	if (idx < 0 || idx >= MAX_STORAGE)
		return STORAGE_ADD_INVALID;

	if (sd->storage.u.items_storage[idx].nameid <= 0)
		return STORAGE_ADD_INVALID; //Nothing there

	if (amount < 1 || amount > sd->storage.u.items_storage[idx].amount)
		return STORAGE_ADD_INVALID;

	return STORAGE_ADD_OK;
}

/*==========================================
 * Add an item to the storage from the inventory.
 * @param sd : player
 * @param index : inventory index to take the item from
 * @param amount : number of item to take
 * @return 0:fail, 1:success
 *------------------------------------------*/
void storage_storageadd(struct map_session_data* sd, int index, int amount)
{
	enum e_storage_add result;

	nullpo_retv(sd);

	result = storage_canAddItem(sd, index, sd->inventory.u.items_inventory, amount, MAX_INVENTORY);
	if (result == STORAGE_ADD_INVALID)
		return;
	else if (result == STORAGE_ADD_OK) {
		switch( storage_additem(sd, &sd->inventory.u.items_inventory[index], amount) ){
			case 0:
				pc_delitem(sd,index,amount,0,4,LOG_TYPE_STORAGE);
				return;
			case 1:
				break;
			case 2:
				result = STORAGE_ADD_NOROOM;
				break;
		}
	}

	clif_storageitemremoved(sd,index,0);
	clif_dropitem(sd,index,0);
}

/*==========================================
 * Retrieve an item from the storage.
 * @param sd : player
 * @param index : storage index to take the item from
 * @param amount : number of item to take
 * @return 0:fail, 1:success
 *------------------------------------------*/
void storage_storageget(struct map_session_data* sd, int index, int amount)
{	
	unsigned char flag = 0;
	enum e_storage_add result;

	nullpo_retv(sd);

	result = storage_canGetItem(sd, index, amount);
	if (result != STORAGE_ADD_OK)
		return;

	if ((flag = pc_additem(sd, &sd->storage.u.items_storage[index], amount, LOG_TYPE_STORAGE)) == ADDITEM_SUCCESS)
		storage_delitem(sd, index, amount);
	else {
		clif_storageitemremoved(sd, index, 0);
		clif_additem(sd, 0, 0, flag);
	}
}

/*==========================================
 * Move an item from cart to storage.
 * @param sd : player
 * @param index : cart index to take the item from
 * @param amount : number of item to take
 * @return 0:fail, 1:success
 *------------------------------------------*/
void storage_storageaddfromcart(struct map_session_data* sd, int index, int amount)
{
	enum e_storage_add result;

	nullpo_retv(sd);

	if (sd->state.prevend) {
		return;
	}

	result = storage_canAddItem(sd, index, sd->cart.u.items_cart, amount, MAX_CART);
	if (result == STORAGE_ADD_INVALID)
		return;
	else if (result == STORAGE_ADD_OK) {
		switch (storage_additem(sd, &sd->cart.u.items_cart[index], amount)) {
		case 0:
			pc_cart_delitem(sd, index, amount, 0, LOG_TYPE_STORAGE);
			return;
		case 1:
			break;
		case 2:
			result = STORAGE_ADD_NOROOM;
			break;
		}
	}

	clif_storageitemremoved(sd, index, 0);
	clif_dropitem(sd, index, 0);
}

/*==========================================
 * Get from Storage to the Cart
 * @param sd : player
 * @param index : storage index to take the item from
 * @param amount : number of item to take
 * @return 0:fail, 1:success
 *------------------------------------------*/
void storage_storagegettocart(struct map_session_data* sd, int index, int amount)
{
	unsigned char flag = 0;
	enum e_storage_add result;

	nullpo_retv(sd);

	if (sd->state.prevend) {
		return;
	}

	result = storage_canGetItem(sd, index, amount);
	if (result != STORAGE_ADD_OK)
		return;

	if ((flag = pc_cart_additem(sd, &sd->storage.u.items_storage[index], amount, LOG_TYPE_STORAGE)) == 0)
		storage_delitem(sd, index, amount);
	else {
		clif_storageitemremoved(sd, index, 0);
		clif_cart_additem_ack(sd, (flag == 1) ? ADDITEM_TO_CART_FAIL_WEIGHT : ADDITEM_TO_CART_FAIL_COUNT);
	}
}


/*==========================================
 * Make player close his storage
 * @author : [massdriller] / modified by [Valaris]
 * @param sd : player
 *------------------------------------------*/
void storage_storageclose(struct map_session_data* sd)
{
	nullpo_retv(sd);

	if (sd->storage.dirty) {
		if (save_settings&4)
			chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);
		else
			intif_storage_save(sd, &sd->storage);
	}

	if (sd->state.storage_flag == 1) {
		sd->state.storage_flag = 0;
		clif_storageclose(sd);
	}
}

/*==========================================
 * Force closing the storage for player without displaying result
 * (exemple when quitting the game)
 * @param sd : player to close storage
 * @param flag :
 *  1: Character is quitting
 *	2(x): Character is changing map-servers
 *------------------------------------------*/
void storage_storage_quit(struct map_session_data* sd, int flag)
{
	nullpo_retv(sd);
	
	if (save_settings&4)
		chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);
	else
		intif_storage_save(sd, &sd->storage);
}

/*==========================================
 * Create a guild_storage stucture and add it into the db
 * @see DBCreateData
 * @param key
 * @param args
 * @return
 *------------------------------------------*/
static DBData create_guildstorage(DBKey key, va_list args)
{
	struct s_storage *gs = NULL;
	gs = (struct s_storage *) aCallocA(sizeof(struct s_storage), 1);
	gs->type = TABLE_GUILD_STORAGE;
	gs->id = key.i;
	return db_ptr2data(gs);
}

/*==========================================
 * Retrieve the guild_storage of a guild
 * will create a new storage if none found for the guild
 * @param guild_id : id of the guild
 * @return guild_storage
 *------------------------------------------*/
struct s_storage *guild2storage(int guild_id)
{
	struct s_storage *gs = NULL;
	if(guild_search(guild_id) != NULL)
		gs = idb_ensure(guild_storage_db,guild_id,create_guildstorage);
	return gs;
}

/*==========================================
 * See if the guild_storage exist in db and fetch it if it,s the case
 * @author : [Skotlex]
 * @param guild_id : guild_id to search the storage
 * @return guild_storage or NULL
 *------------------------------------------*/
struct s_storage *guild2storage2(int guild_id)
{	//For just locating a storage without creating one. [Skotlex]
	return (struct s_storage*)idb_get(guild_storage_db,guild_id);
}

/*==========================================
 * Delete a guild_storage and remove it from db
 * @param guild_id : guild to remove the storage from
 *------------------------------------------*/
void storage_guild_delete(int guild_id)	
{
	idb_remove(guild_storage_db,guild_id);
}

/*==========================================
 * Attempt to open guild storage for player
 * @param sd : player
 * @return * 	0 : success, 1 : fail, 2 : no guild found
 *------------------------------------------*/
char storage_guild_storageopen(struct map_session_data* sd)
{
	struct s_storage *gstor;

	nullpo_ret(sd);

	if(sd->status.guild_id <= 0)
		return 2;

	if(sd->state.storage_flag)
		return 1; //Can't open both storages at a time.
	
	if( !pc_can_give_items(pc_isGM(sd)) ) { //check is this GM level can open guild storage and store items [Lupus]
		clif_displaymessage(sd->fd, msg_txt(sd,246));
		return 1;
	}

	if((gstor = guild2storage2(sd->status.guild_id)) == NULL) {
		intif_request_guild_storage(sd->status.account_id,sd->status.guild_id);
		return 0;
	}
	if(gstor->status)
		return 1;

	if( gstor->lock )
 		return 1;
	
	gstor->status = true;
	sd->state.storage_flag = 2;
	storage_sortitem(gstor->u.items_guild, ARRAYLENGTH(gstor->u.items_guild));
	clif_storagelist(sd, gstor->u.items_guild, ARRAYLENGTH(gstor->u.items_guild));
	clif_updatestorageamount(sd, gstor->amount, MAX_GUILD_STORAGE);
	return 0;
}

/*==========================================
 * Attempt to add an item in guild storage, then refresh it
 * @param sd : player attempting to open the guild_storage
 * @param stor : guild_storage
 * @param item_data : item to add
 * @param amount : number of item to add
 * @return 0 : success, 1 : fail
 *------------------------------------------*/
bool storage_guild_additem(struct map_session_data* sd, struct s_storage* stor, struct item* item_data, int amount)
{
	struct item_data *data;
	int i;

	nullpo_retr(1, sd);
	nullpo_retr(1, stor);
	nullpo_retr(1, item_data);

	if(item_data->nameid == 0 || amount <= 0)
		return false;

	data = itemdb_search(item_data->nameid);

	if( !itemdb_canguildstore(item_data, pc_isGM(sd)) || item_data->expire_time )
	{	//Check if item is storable. [Skotlex]
		clif_displaymessage (sd->fd, msg_txt(sd,264));
		return false;
	}

	if( (item_data->bound == 1 || item_data->bound > 2) && !pc_can_give_bounded_items(sd->gmlevel) ) { 
		clif_displaymessage(sd->fd, msg_txt(sd,294)); 
		return false; 
	} 

	if(itemdb_isstackable2(data)){ //Stackable
		for(i=0;i<MAX_GUILD_STORAGE;i++){
			if(compare_item(&stor->u.items_guild[i], item_data)) {
				if(stor->u.items_guild[i].amount+amount > MAX_AMOUNT)
					return false;
				stor->u.items_guild[i].amount+=amount;
				clif_storageitemadded(sd,&stor->u.items_guild[i],i,amount);
				stor->dirty = true;
				return true;
			}
		}
	}
	//Add item
	for(i=0;i<MAX_GUILD_STORAGE && stor->u.items_guild[i].nameid;i++);
	
	if(i>=MAX_GUILD_STORAGE)
		return false;
	
	memcpy(&stor->u.items_guild[i],item_data,sizeof(stor->u.items_guild[0]));
	stor->u.items_guild[i].amount=amount;
	stor->amount++;
	clif_storageitemadded(sd,&stor->u.items_guild[i],i,amount);
	clif_updatestorageamount(sd, stor->amount, MAX_GUILD_STORAGE);
	stor->dirty = true;
	return true;
}

/*==========================================
 * Attempt to delete an item in guild storage, then refresh it
 * @param sd : player
 * @param stor : guild_storage
 * @param n : index of item in guild storage
 * @param amount : number of item to delete
 * @return true : success, false : fail
 *------------------------------------------*/
bool storage_guild_delitem(struct map_session_data* sd, struct s_storage* stor, int n, int amount)
{
	nullpo_retr(1, sd);
	nullpo_retr(1, stor);

	if(stor->u.items_guild[n].nameid==0 || stor->u.items_guild[n].amount<amount)
		return false;

	stor->u.items_guild[n].amount-=amount;
	if(stor->u.items_guild[n].amount==0){
		memset(&stor->u.items_guild[n],0,sizeof(stor->u.items_guild[0]));
		stor->amount--;
		clif_updatestorageamount(sd, stor->amount, MAX_GUILD_STORAGE);
	}
	clif_storageitemremoved(sd,n,amount);
	stor->dirty = true;
	return true;
}

/*==========================================
 * Attempt to add an item in guild storage from inventory, then refresh it
 * @param sd : player
 * @param amount : number of item to delete
 *------------------------------------------*/
void storage_guild_storageadd(struct map_session_data* sd, int index, int amount)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));
		
	if( !stor->status || stor->amount > MAX_GUILD_STORAGE )
		return;
	
	if( index<0 || index>=MAX_INVENTORY )
		return;

	if( sd->inventory.u.items_inventory[index].nameid == 0 )
		return;
	
	if( amount < 1 || amount > sd->inventory.u.items_inventory[index].amount )
		return;

	if(storage_guild_additem(sd,stor,&sd->inventory.u.items_inventory[index],amount))
		pc_delitem(sd,index,amount,0,4,LOG_TYPE_GSTORAGE);

	return;
}

/*==========================================
 * Attempt to retrieve an item from guild storage to inventory, then refresh it
 * @param sd : player
 * @param index : index of item in storage
 * @param amount : number of item to get
 *------------------------------------------*/
void storage_guild_storageget(struct map_session_data* sd, int index, int amount)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));

	if(!stor->status)
  		return;
	
	if(index<0 || index>=MAX_GUILD_STORAGE)
		return;

	if(stor->u.items_guild[index].nameid == 0)
		return;
	
	if(amount < 1 || amount > stor->u.items_guild[index].amount)
	  	return;

	if( stor->lock ) {
		storage_guild_storageclose(sd);
 		return;
 	}

	enum e_additem_result flag = pc_additem(sd, &stor->u.items_guild[index], amount, LOG_TYPE_GSTORAGE);

	if (flag == ADDITEM_SUCCESS)
		storage_guild_delitem(sd, stor, index, amount);
	else {
		clif_storageitemremoved(sd, index, 0);
		clif_additem(sd, 0, 0, flag);
	}

	return;
}

/*==========================================
 * Attempt to add an item in guild storage from cart, then refresh it
 * @param sd : player
 * @param index : index of item in cart
 * @param amount : number of item to transfer
 *------------------------------------------*/
void storage_guild_storageaddfromcart(struct map_session_data* sd, int index, int amount)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));

	if( !stor->status || stor->amount > MAX_GUILD_STORAGE )
		return;

	if( index < 0 || index >= MAX_CART )
		return;

	if( sd->cart.u.items_cart[index].nameid == 0 )
		return;
	
	if( amount < 1 || amount > sd->cart.u.items_cart[index].amount )
		return;

	if(storage_guild_additem(sd,stor,&sd->cart.u.items_cart[index],amount))
		pc_cart_delitem(sd,index,amount,0,LOG_TYPE_GSTORAGE);
 	else {
 		clif_storageitemremoved(sd,index,0);
 		clif_dropitem(sd,index,0);
	}

	return;
}

/*==========================================
 * Attempt to retrieve an item from guild storage to cart, then refresh it
 * @param sd : player
 * @param index : index of item in storage
 * @param amount : number of item to transfer
 *------------------------------------------*/
void storage_guild_storagegettocart(struct map_session_data* sd, int index, int amount)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));

	if(!stor->status)
	  	return;

	if(index<0 || index>=MAX_GUILD_STORAGE)
	  	return;
	
	if(stor->u.items_guild[index].nameid == 0)
		return;
	
	if(amount < 1 || amount > stor->u.items_guild[index].amount)
		return;

	enum e_additem_result flag = pc_cart_additem(sd, &stor->u.items_guild[index], amount, LOG_TYPE_GSTORAGE);

	if (flag == ADDITEM_SUCCESS)
		storage_guild_delitem(sd,stor,index,amount);
	else {
		clif_storageitemremoved(sd, index, 0);
		clif_cart_additem_ack(sd, (flag == 1) ? ADDITEM_TO_CART_FAIL_WEIGHT : ADDITEM_TO_CART_FAIL_COUNT);
	}

	return;
}

/*==========================================
 * Request to save guild storage
 * @param account_id : account requesting the save
 * @param guild_id : guild to take the guild_storage
 * @param flag : ?
 * @return 0 : fail (no storage), 1 : success (requested)
 *------------------------------------------*/
bool storage_guild_storagesave(uint32 account_id, int guild_id, int flag)
{
	struct s_storage *stor = guild2storage2(guild_id);

	if(stor)
	{
		if (flag) //Char quitting, close it.
			stor->status = false;
	 	if (stor->dirty)
			intif_send_guild_storage(account_id,stor);
		return 1;
	}
	return 0;
}

/*==========================================
 * ACK save of guild storage
 * @param guild_id : guild to use the storage
 *------------------------------------------*/
void storage_guild_storagesaved(int guild_id)
{
	struct s_storage *stor;

	if((stor=guild2storage2(guild_id)) != NULL) {
		if (stor->dirty && !stor->status)
		{	//Storage has been correctly saved.
			stor->dirty = false;
		}
		return;
	}
	return;
}

/*==========================================
 * Close storage for player then save it
 * @param sd : player
 *------------------------------------------*/
void storage_guild_storageclose(struct map_session_data* sd)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));

	clif_storageclose(sd);
	if (stor->status)
	{
		if (save_settings&4)
			chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART); //This one also saves the storage. [Skotlex]
		else
			storage_guild_storagesave(sd->status.account_id, sd->status.guild_id,0);
		stor->status = false;
	}
	sd->state.storage_flag = 0;

	return;
}

/*==========================================
 * Close storage for player then save it
 * @param sd
 * @param flag
 *------------------------------------------*/
void storage_guild_storage_quit(struct map_session_data* sd, int flag)
{
	struct s_storage *stor;

	nullpo_retv(sd);
	nullpo_retv(stor=guild2storage2(sd->status.guild_id));
	
	if(flag)
	{	//Only during a guild break flag is 1 (don't save storage)
		clif_storageclose(sd);

		if (save_settings&4)
			chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);

		sd->state.storage_flag = 0;
		stor->status = false;
		return;
	}

	if(stor->status) {
		if (save_settings&4)
			chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);
		else
			storage_guild_storagesave(sd->status.account_id,sd->status.guild_id,1);
	}
	sd->state.storage_flag = 0;
	stor->status = false;

	return;
}
