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

#undef UNAVAILABLE_STRUCT

#undef DEFINE_PACKET_HEADER

#endif /* PACKETS_H */