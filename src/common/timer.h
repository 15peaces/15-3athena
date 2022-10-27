// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_TIMER_H_
#define	_TIMER_H_

#include "cbasetypes.h"

#define DIFF_TICK(a,b) ((a)-(b))
#define DIFF_TICK32(a,b) ((int32)((a)-(b))) 

#define INVALID_TIMER (-1)

// timer flags
#define TIMER_ONCE_AUTODEL 0x01
#define TIMER_INTERVAL     0x02
#define TIMER_REMOVE_HEAP  0x10

// Struct declaration

typedef int (*TimerFunc)(int tid, int64 tick, int id, intptr_t data);

struct TimerData {
	int64 tick;
	TimerFunc func;
	unsigned int type;
	int interval;

	// general-purpose storage
	int id; 
	intptr_t data;
};

// Function prototype declaration

int64 gettick(void);
int64 gettick_nocache(void);

int add_timer(int64 tick, TimerFunc func, int id, intptr_t data);
int add_timer_interval(int64 tick, TimerFunc func, int id, intptr_t data, int interval);
const struct TimerData* get_timer(int tid);
int delete_timer(int tid, TimerFunc func);

int64 timer_addtick(int tid, int64 tick);
int64 timer_settick(int tid, int64 tick);

int add_timer_func_list(TimerFunc func, char* name);

unsigned long get_uptime(void);

int do_timer(int64 tick);
void timer_init(void);
void timer_final(void);

#endif /* _TIMER_H_ */
