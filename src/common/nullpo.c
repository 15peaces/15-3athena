// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "nullpo.h"
#include "showmsg.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/**
 * Reports failed assertions or NULL pointers
 *
 * @param file       Source file where the error was detected
 * @param line       Line
 * @param func       Function
 * @param targetname Name of the checked symbol
 * @param title      Message title to display (i.e. failed assertion or nullpo info)
 */
void nullpo_assert_report(const char *file, int line, const char *func, const char *targetname, const char *title) {
	if (file == NULL)
		file = "??";

	if (func == NULL || *func == '\0')
		func = "unknown";

	ShowError("--- %s --------------------------------------------\n", title);
	ShowError("%s:%d: '%s' in function `%s'\n", file, line, targetname, func);
	ShowError("--- end %s ----------------------------------------\n", title);
}

/// Checks for and reports NULL pointers.
/// Used by nullpo_ret* macros.
int nullpo_chk(const char* file, int line, const char* func, const void* target, const char* targetname)
{
	if( target )
	{
		return 0;
	}

	file =
		( file == NULL ) ? "??" : file;

	func =
		( func == NULL ) ? "unknown" :
		( func[0] == 0 ) ? "unknown" : func;

	ShowDebug("--- nullpo info --------------------------------------------\n");
	ShowDebug("%s:%d: target '%s' in func '%s'\n", file, line, targetname, func);
	ShowDebug("--- end nullpo info ----------------------------------------\n");
	return 1;
}
