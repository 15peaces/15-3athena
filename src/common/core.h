// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_CORE_H_
#define	_CORE_H_

#define UNKNOWN_VERSION '\x02'

#define ATHENA_SERVER_NONE	0	// not defined
#define ATHENA_SERVER_LOGIN	1	// login server
#define ATHENA_SERVER_CHAR	2	// char server
#define ATHENA_SERVER_INTER	4	// inter server
#define ATHENA_SERVER_MAP	8	// map server

// Comment to disable Guild/Party Bound item system 
// By default, we recover/remove Guild/Party Bound items automatically 
#define BOUND_ITEMS

/// Max number of items on @autolootid list
#define AUTOLOOTITEM_SIZE 10

/// Comment to disable the official packet obfuscation support.
/// When enabled, make sure there is value for 'packet_keys' of used packet version or
/// defined 'packet_keys_use' in db/packet_db.txt.
/// This requires PACKETVER 2011-08-17 or newer.
/// EXPERIMENTAL FEATURE [15peaces]
//#define PACKET_OBFUSCATION

// Show all packets (for debugging) [15peaces]
//#define LOG_ALL_PACKETS

// Let inter server display more informations (for debugging) (old eAthena define)
//#define NOISY

enum SERVER_STATE
{
	SERVER_STATE_STOP,
	SERVER_STATE_RUN,
	SERVER_STATE_SHUTDOWN,
};

extern int arg_c;
extern char **arg_v;
extern int runflag;
extern char *SERVER_NAME;
extern char SERVER_TYPE;

extern int parse_console(const char* buf);
const char *get_svn_revision(void);
const char *get_git_hash(void);
extern int do_init(int,char**);
extern void set_server_type(void);
extern void do_shutdown(void);
extern void do_abort(void);
extern void do_final(void);

#endif /* _CORE_H_ */
