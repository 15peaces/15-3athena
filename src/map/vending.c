// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "clif.h"
#include "itemdb.h"
#include "atcommand.h"
#include "map.h"
#include "path.h"
#include "chrif.h"
#include "vending.h"
#include "pc.h"
#include "skill.h"
#include "battle.h"
#include "log.h"
#include "achievement.h"

#include <stdio.h>
#include <string.h>

struct vending_entry {
	int cartinventory_id;
	int amount;
	int price;
	int index;
};
struct vending {
	int account_id;
	int char_id;
	int vendor_id;
	int m;
	int x;
	int y;
	unsigned char sex;
	char title[MESSAGE_SIZE];
	uint32 count;
	struct vending_entry* entries;
	struct map_session_data *sd;
};

static int vending_nextid = 0;
static DBMap *vending_db;

DBMap * vending_getdb() {
	return vending_db;
}

/// Returns an unique vending shop id.
static int vending_getuid(void)
{
	return ++vending_nextid;
}

/*==========================================
 * Close shop
 *------------------------------------------*/
void vending_closevending(struct map_session_data* sd)
{
	nullpo_retv(sd);

	if( sd->state.vending )
	{
		if (Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE vending_id = %d;", vending_items_db, sd->vender_id) != SQL_SUCCESS ||
			Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `id` = %d;", vendings_db, sd->vender_id) != SQL_SUCCESS) {
			Sql_ShowDebug(mmysql_handle);
		}

		sd->state.vending = false;
		sd->vender_id = 0;
		clif_closevendingboard(&sd->bl, 0);
		idb_remove(vending_db, sd->status.char_id);
	}
}

/*==========================================
 * Request a shop's item list
 *------------------------------------------*/
void vending_vendinglistreq(struct map_session_data* sd, int id)
{
	struct map_session_data* vsd;
	nullpo_retv(sd);

	if( (vsd = map_id2sd(id)) == NULL )
		return;
	if( !vsd->state.vending )
		return; // not vending

	if ( !pc_can_give_items(pc_isGM(sd)) || !pc_can_give_items(pc_isGM(vsd)) ) //check if both GMs are allowed to trade
	{	// GM is not allowed to trade
		clif_displaymessage(sd->fd, msg_txt(sd,246));
		return;
	} 

	sd->vended_id = vsd->vender_id;  // register vending uid

	clif_vendinglist(sd, vsd);
}

/*==========================================
 * Purchase item(s) from a shop
 *------------------------------------------*/
void vending_purchasereq(struct map_session_data* sd, int aid, int uid, const uint8* data, int count)
{
	int i, j, cursor, w, new_ = 0, blank, vend_list[MAX_VENDING];
	double z;
	struct s_vending vending[MAX_VENDING]; // against duplicate packets
	struct map_session_data* vsd = map_id2sd(aid);

	nullpo_retv(sd);
	if( vsd == NULL || !vsd->state.vending || vsd->bl.id == sd->bl.id )
		return; // invalid shop

	if( vsd->vender_id != uid )
	{// shop has changed
		clif_buyvending(sd, 0, 0, 6);  // store information was incorrect
		return;
	}

	if( !searchstore_queryremote(sd, aid) && ( sd->bl.m != vsd->bl.m || !check_distance_bl(&sd->bl, &vsd->bl, AREA_SIZE) ) )
		return; // shop too far away

	searchstore_clearremote(sd);

	if( count < 1 || count > MAX_VENDING || count > vsd->vend_num )
		return; // invalid amount of purchased items

	blank = pc_inventoryblank(sd); //number of free cells in the buyer's inventory

	// duplicate item in vending to check hacker with multiple packets
	memcpy(&vending, &vsd->vending, sizeof(vsd->vending)); // copy vending list

	// some checks
	z = 0.; // zeny counter
	w = 0;  // weight counter
	for( i = 0; i < count; i++ )
	{
		short amount = *(uint16*)(data + 4*i + 0);
		short idx    = *(uint16*)(data + 4*i + 2);
		idx -= 2;

		if( amount <= 0 )
			return;

		// check of item index in the cart
		if( idx < 0 || idx >= MAX_CART )
			return;

		ARR_FIND( 0, vsd->vend_num, j, vsd->vending[j].index == idx );
		if( j == vsd->vend_num )
			return; //picked non-existing item
		else
			vend_list[i] = j;

		z += ((double)vsd->vending[j].value * (double)amount);
		if( z > (double)sd->status.zeny || z < 0. || z > (double)MAX_ZENY )
		{
			clif_buyvending(sd, idx, amount, 1); // you don't have enough zeny
			return;
		}
		if( z + (double)vsd->status.zeny > (double)MAX_ZENY && !battle_config.vending_over_max )
		{
			clif_buyvending(sd, idx, vsd->vending[j].amount, 4); // too much zeny = overflow
			return;

		}
		w += itemdb_weight(vsd->cart.u.items_cart[idx].nameid) * amount;
		if( w + sd->weight > sd->max_weight )
		{
			clif_buyvending(sd, idx, amount, 2); // you can not buy, because overweight
			return;
		}
		
		//Check to see if cart/vend info is in sync.
		if( vending[j].amount > vsd->cart.u.items_cart[idx].amount )
			vending[j].amount = vsd->cart.u.items_cart[idx].amount;
		
		// if they try to add packets (example: get twice or more 2 apples if marchand has only 3 apples).
		// here, we check cumulative amounts
		if( vending[j].amount < amount )
		{
			// send more quantity is not a hack (an other player can have buy items just before)
			clif_buyvending(sd, idx, vsd->vending[j].amount, 4); // not enough quantity
			return;
		}
		
		vending[j].amount -= amount;

		switch( pc_checkadditem(sd, vsd->cart.u.items_cart[idx].nameid, amount) ) {
		case CHKADDITEM_EXIST:
			break;	//We'd add this item to the existing one (in buyers inventory)
		case CHKADDITEM_NEW:
			new_++;
			if (new_ > blank)
				return; //Buyer has no space in his inventory
			break;
		case CHKADDITEM_OVERAMOUNT:
			return; //too many items
		}
	}

	pc_payzeny(sd, (int)z, LOG_TYPE_VENDING, vsd);

	//achievement_update_objective(sd, AG_SPEND_ZENY, 1, (int)z);
	if( battle_config.vending_tax )
		z -= z * (battle_config.vending_tax/10000.);
	
	pc_getzeny(vsd, (int)z, LOG_TYPE_VENDING, sd);

	for( i = 0; i < count; i++ )
	{
		short amount = *(uint16*)(data + 4*i + 0);
		short idx    = *(uint16*)(data + 4*i + 2);
		idx -= 2;
		z = 0.; // zeny counter

		// vending item
		pc_additem(sd, &vsd->cart.u.items_cart[idx], amount, LOG_TYPE_VENDING);
		vsd->vending[vend_list[i]].amount -= amount;
		z += ((double)vsd->vending[i].value * (double)amount);

		if (vsd->vending[vend_list[i]].amount) {
			if (Sql_Query(mmysql_handle, "UPDATE `%s` SET `amount` = %d WHERE `vending_id` = %d and `cartinventory_id` = %d", vending_items_db, vsd->vending[vend_list[i]].amount, vsd->vender_id, vsd->cart.u.items_cart[idx].id) != SQL_SUCCESS) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
		else {
			if (Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `vending_id` = %d and `cartinventory_id` = %d", vending_items_db, vsd->vender_id, vsd->cart.u.items_cart[idx].id) != SQL_SUCCESS) {
				Sql_ShowDebug(mmysql_handle);
			}
		}

		pc_cart_delitem(vsd, idx, amount, 0, LOG_TYPE_VENDING);
		if( battle_config.vending_tax )
			z -= z * (battle_config.vending_tax/10000.);
		clif_vendingreport(vsd, idx, amount, sd->status.char_id, (int)z);

		//print buyer's name
		if( battle_config.buyer_name )
		{
			char temp[256];
			sprintf(temp, msg_txt(sd,265), sd->status.name);
			clif_disp_onlyself(vsd,temp,strlen(temp));
		}
	}

	// compact the vending list
	for( i = 0, cursor = 0; i < vsd->vend_num; i++ )
	{
		if( vsd->vending[i].amount == 0 )
			continue;
		
		if( cursor != i ) // speedup
		{
			vsd->vending[cursor].index = vsd->vending[i].index;
			vsd->vending[cursor].amount = vsd->vending[i].amount;
			vsd->vending[cursor].value = vsd->vending[i].value;
		}

		cursor++;
	}
	vsd->vend_num = cursor;

	//Always save BOTH: buyer and customer
	if( save_settings&2 )
	{
		chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);
		chrif_save(vsd, CSAVE_INVENTORY | CSAVE_CART);
	}

	//check for @AUTOTRADE users [durf]
	if( vsd->state.autotrade )
	{
		//see if there is anything left in the shop
		ARR_FIND( 0, vsd->vend_num, i, vsd->vending[i].amount > 0 );
		if( i == vsd->vend_num )
		{
			//Close Vending (this was automatically done by the client, we have to do it manually for autovenders) [Skotlex]
			vending_closevending(vsd);
			map_quit(vsd);	//They have no reason to stay around anymore, do they?
		}
	}
}

/*==========================================
 * Open shop
 * data := {<index>.w <amount>.w <value>.l}[count]
 *------------------------------------------*/
void vending_openvending(struct map_session_data* sd, const char* message, const uint8* data, int count) {
	int i, j;
	int vending_skill_lvl;
	char message_sql[MESSAGE_SIZE * 2];

	nullpo_retv(sd);

	if (pc_isdead(sd) || !sd->state.prevend || pc_istrading(sd)) {
		return; // can't open vendings lying dead || didn't use via the skill (wpe/hack) || can't have 2 shops at once
	}

	vending_skill_lvl = pc_checkskill(sd, MC_VENDING);
	// skill level and cart check
	if( !vending_skill_lvl || !pc_iscarton(sd) )
	{
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0, 0);
		return;
	}

	// check number of items in shop
	if( count < 1 || count > MAX_VENDING || count > 2 + vending_skill_lvl )
	{	// invalid item count
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0, 0);
		return;
	}

	if (save_settings&CHARSAVE_VENDING) // Avoid invalid data from saving
		chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);

	// filter out invalid items
	i = 0;
	for( j = 0; j < count; j++ )
	{
		short index        = *(uint16*)(data + 8*j + 0);
		short amount       = *(uint16*)(data + 8*j + 2);
		unsigned int value = *(uint32*)(data + 8*j + 4);

		index -= 2; // offset adjustment (client says that the first cart position is 2)

		if( index < 0 || index >= MAX_CART // invalid position
		||  pc_cartitem_amount(sd, index, amount) < 0 // invalid item or insufficient quantity
		//NOTE: official server does not do any of the following checks!
		||  !sd->cart.u.items_cart[index].identify // unidentified item
		||  sd->cart.u.items_cart[index].attribute == 1 // broken item
		||  sd->cart.u.items_cart[index].expire_time // It should not be in the cart but just in case
		||  (sd->cart.u.items_cart[index].bound && !pc_can_give_bounded_items(sd->gmlevel)) // can't trade account bound items and has no permission
		||  !itemdb_cantrade(&sd->cart.u.items_cart[index], pc_isGM(sd), pc_isGM(sd)) ) // untradeable item
			continue;

		sd->vending[i].index = index;
		sd->vending[i].amount = amount;
		sd->vending[i].value = min(value, (unsigned int)battle_config.vending_max_value);

		i++; // item successfully added
	}

	if (i != j)
	{
		clif_displaymessage(sd->fd, msg_txt(sd,266)); //"Some of your items cannot be vended and were removed from the shop."
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0, 0); // custom reply packet
		return;
	}

	if( i == 0 )
	{	// no valid item found
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0, 0); // custom reply packet
		return;
	}

	sd->state.prevend = 0;
	sd->state.vending = true;
	sd->state.workinprogress = WIP_DISABLE_NONE;
	sd->vender_id = vending_getuid();
	sd->vend_num = i;
	safestrncpy(sd->message, message, MESSAGE_SIZE);

	Sql_EscapeString(mmysql_handle, message_sql, sd->message);

	if (Sql_Query(mmysql_handle, "INSERT INTO `%s`(`id`,`account_id`,`char_id`,`sex`,`map`,`x`,`y`,`title`) VALUES( %d, %d, %d, '%c', '%s', %d, %d, '%s');", vendings_db, sd->vender_id, sd->status.account_id, sd->status.char_id, sd->status.sex == 2 ? 'F' : 'M', map[sd->bl.m].name, sd->bl.x, sd->bl.y, message_sql) != SQL_SUCCESS) {
		Sql_ShowDebug(mmysql_handle);
	}

	for (i = 0; i < count; i++) {
		if (Sql_Query(mmysql_handle, "INSERT INTO `%s`(`vending_id`,`index`,`cartinventory_id`,`amount`,`price`) VALUES( %d, %d, %d, %d, %d );", vending_items_db, sd->vender_id, i, sd->cart.u.items_cart[sd->vending[i].index].id, sd->vending[i].amount, sd->vending[i].value) != SQL_SUCCESS) {
			Sql_ShowDebug(mmysql_handle);
		}
	}

	clif_openvending(sd,sd->bl.id,sd->vending);
	clif_showvendingboard(&sd->bl,message,0);

	idb_put(vending_db, sd->vender_id, sd);
}


/// Checks if an item is being sold in given player's vending.
bool vending_search(struct map_session_data* sd, t_itemid nameid)
{
	int i;

	if( !sd->state.vending )
	{// not vending
		return false;
	}

	ARR_FIND( 0, sd->vend_num, i, sd->cart.u.items_cart[sd->vending[i].index].nameid == nameid );
	if( i == sd->vend_num )
	{// not found
		return false;
	}

	return true;
}


/// Searches for all items in a vending, that match given ids, price and possible cards.
/// @return Whether or not the search should be continued.
bool vending_searchall(struct map_session_data* sd, const struct s_search_store_search* s)
{
	int i, c, slot;
	unsigned int idx, cidx;
	struct item* it;

	if( !sd->state.vending )
	{// not vending
		return true;
	}

	for( idx = 0; idx < s->item_count; idx++ )
	{
		ARR_FIND( 0, sd->vend_num, i, sd->cart.u.items_cart[sd->vending[i].index].nameid == (short)s->itemlist[idx].itemId);
		if( i == sd->vend_num )
		{// not found
			continue;
		}
		it = &sd->cart.u.items_cart[sd->vending[i].index];

		if( s->min_price && s->min_price > sd->vending[i].value )
		{// too low price
			continue;
		}

		if( s->max_price && s->max_price < sd->vending[i].value )
		{// too high price
			continue;
		}

		if( s->card_count )
		{// check cards
			if( itemdb_isspecial(it->card[0]) )
			{// something, that is not a carded
				continue;
			}
			slot = itemdb_slot(it->nameid);

			for( c = 0; c < slot && it->card[c]; c ++ )
			{
				ARR_FIND( 0, s->card_count, cidx, s->cardlist[cidx].itemId == it->card[c] );
				if( cidx != s->card_count )
				{// found
					break;
				}
			}

			if( c == slot || !it->card[c] )
			{// no card match
				continue;
			}
		}

		if( !searchstore_result(s->search_sd, sd->vender_id, sd->status.account_id, sd->message, it->nameid, sd->vending[i].amount, sd->vending[i].value, it->card, it->refine, it->option) )
		{// result set full
			return false;
		}
	}

	return true;
}

void do_final_vending(void) {
	db_destroy(vending_db);
}

void do_init_vending(void) {
	vending_db = idb_alloc(DB_OPT_BASE);
	vending_nextid = 0;
}
