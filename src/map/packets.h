// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef PACKETS_H
#define PACKETS_H

// Required for MESSAGE_SIZE
#include "map.h"

#define UNAVAILABLE_STRUCT int8 _____unavailable

/* packet size constant for itemlist */
#if MAX_INVENTORY > MAX_STORAGE && MAX_INVENTORY > MAX_CART
#define MAX_ITEMLIST MAX_INVENTORY
#elif MAX_CART > MAX_INVENTORY && MAX_CART > MAX_STORAGE
#define MAX_ITEMLIST MAX_CART
#else
#define MAX_ITEMLIST MAX_STORAGE
#endif

#include "packets_struct.h"

const int16 MAX_INVENTORY_ITEM_PACKET_NORMAL = ((INT16_MAX - (sizeof(struct packet_itemlist_normal) - (sizeof(struct NORMALITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct NORMALITEM_INFO));
const int16 MAX_INVENTORY_ITEM_PACKET_EQUIP = ((INT16_MAX - (sizeof(struct packet_itemlist_equip) - (sizeof(struct EQUIPITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct EQUIPITEM_INFO));

const int16 MAX_STORAGE_ITEM_PACKET_NORMAL = ((INT16_MAX - (sizeof(struct ZC_STORE_ITEMLIST_NORMAL) - (sizeof(struct NORMALITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct NORMALITEM_INFO));
const int16 MAX_STORAGE_ITEM_PACKET_EQUIP = ((INT16_MAX - (sizeof(struct ZC_STORE_ITEMLIST_EQUIP) - (sizeof(struct EQUIPITEM_INFO) * MAX_ITEMLIST))) / sizeof(struct EQUIPITEM_INFO));

#undef UNAVAILABLE_STRUCT

#undef DEFINE_PACKET_HEADER

#endif /* PACKETS_H */