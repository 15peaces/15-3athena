// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef __ACCOUNT_H_INCLUDED__
#define __ACCOUNT_H_INCLUDED__

#include "../common/cbasetypes.h"
#include "../common/mmo.h" // ACCOUNT_REG2_NUM

typedef struct AccountDB AccountDB;
typedef struct AccountDBIterator AccountDBIterator;


// standard engines
#ifdef WITH_TXT
AccountDB* account_db_txt(void);
#endif
#ifdef WITH_SQL
AccountDB* account_db_sql(void);
#endif
// extra engines (will probably use the other txt functions)
#define ACCOUNTDB_CONSTRUCTOR_(engine) account_db_##engine
#define ACCOUNTDB_CONSTRUCTOR(engine) ACCOUNTDB_CONSTRUCTOR_(engine)
#ifdef ACCOUNTDB_ENGINE_0
AccountDB* ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_0)(void);
#endif
#ifdef ACCOUNTDB_ENGINE_1
AccountDB* ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_1)(void);
#endif
#ifdef ACCOUNTDB_ENGINE_2
AccountDB* ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_2)(void);
#endif
#ifdef ACCOUNTDB_ENGINE_3
AccountDB* ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_3)(void);
#endif
#ifdef ACCOUNTDB_ENGINE_4
AccountDB* ACCOUNTDB_CONSTRUCTOR(ACCOUNTDB_ENGINE_4)(void);
#endif


struct mmo_account
{
	uint32 account_id;
	char userid[NAME_LENGTH];
	char pass[32+1];        // 23+1 for plaintext, 32+1 for md5-ed passwords
	char sex;               // gender (M/F/S)
	char email[40];         // e-mail (by default: a@a.com)
	int level;              // GM level
	unsigned int state;     // packet 0x006a value + 1 (0: compte OK)
	time_t unban_time;      // (timestamp): ban time limit of the account (0 = no ban)
	time_t expiration_time; // (timestamp): validity limit of the account (0 = unlimited)
	unsigned int logincount;// number of successful auth attempts
	char lastlogin[24];     // date+time of last successful login
	char last_ip[16];       // save of last IP of connection
	char birthdate[10+1];   // assigned birth date (format: YYYY-MM-DD, default: 0000-00-00)
	char pincode[PINCODE_LENGTH + 1];	// pincode system
	time_t pincode_change;	// (timestamp): last time of pincode change
	int account_reg2_num;
	struct global_reg account_reg2[ACCOUNT_REG2_NUM]; // account script variables (stored on login server)
};


struct AccountDBIterator
{
	/// Destroys this iterator, releasing all allocated memory (including itself).
	///
	/// @param self Iterator
	void (*destroy)(AccountDBIterator* self);

	/// Fetches the next account in the database.
	/// Fills acc with the account data.
	/// @param self Iterator
	/// @param acc Account data
	/// @return true if successful
	bool (*next)(AccountDBIterator* self, struct mmo_account* acc);
};


struct AccountDB
{
	/// Initializes this database, making it ready for use.
	/// Call this after setting the properties.
	///
	/// @param self Database
	/// @return true if successful
	bool (*init)(AccountDB* self);

	/// Destroys this database, releasing all allocated memory (including itself).
	///
	/// @param self Database
	void (*destroy)(AccountDB* self);

	/// Gets a property from this database.
	/// These read-only properties must be implemented:
	/// "engine.name" -> "txt", "sql", ...
	/// "engine.version" -> internal version
	/// "engine.comment" -> anything (suggestion: description or specs of the engine)
	///
	/// @param self Database
	/// @param key Property name
	/// @param buf Buffer for the value
	/// @param buflen Buffer length
	/// @return true if successful
	bool (*get_property)(AccountDB* self, const char* key, char* buf, size_t buflen);

	/// Sets a property in this database.
	///
	/// @param self Database
	/// @param key Property name
	/// @param value Property value
	/// @return true if successful
	bool (*set_property)(AccountDB* self, const char* key, const char* value);

	/// Creates a new account in this database.
	/// If acc->account_id is not -1, the provided value will be used.
	/// Otherwise the account_id will be auto-generated and written to acc->account_id.
	///
	/// @param self Database
	/// @param acc Account data
	/// @return true if successful
	bool (*create)(AccountDB* self, struct mmo_account* acc);

	/// Removes an account from this database.
	///
	/// @param self Database
	/// @param account_id Account id
	/// @return true if successful
	bool (*remove)(AccountDB* self, const uint32 account_id);

	/// Modifies the data of an existing account.
	/// Uses acc->account_id to identify the account.
	///
	/// @param self Database
	/// @param acc Account data
	/// @return true if successful
	bool (*save)(AccountDB* self, const struct mmo_account* acc);

	/// Finds an account with account_id and copies it to acc.
	///
	/// @param self Database
	/// @param acc Pointer that receives the account data
	/// @param account_id Target account id
	/// @return true if successful
	bool (*load_num)(AccountDB* self, struct mmo_account* acc, const uint32 account_id);

	/// Finds an account with userid and copies it to acc.
	///
	/// @param self Database
	/// @param acc Pointer that receives the account data
	/// @param userid Target username
	/// @return true if successful
	bool (*load_str)(AccountDB* self, struct mmo_account* acc, const char* userid);

	/// Returns a new forward iterator.
	///
	/// @param self Database
	/// @return Iterator
	AccountDBIterator* (*iterator)(AccountDB* self);
};


#endif // __ACCOUNT_H_INCLUDED__
