#ifndef _CBASETYPES_H_
#define _CBASETYPES_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*              +--------+-----------+--------+---------+
 *              | ILP32  |   LP64    |  ILP64 | (LL)P64 |
 * +------------+--------+-----------+--------+---------+
 * | ints       | 32-bit |   32-bit  | 64-bit |  32-bit |
 * | longs      | 32-bit |   64-bit  | 64-bit |  32-bit |
 * | long-longs | 64-bit |   64-bit  | 64-bit |  64-bit |
 * | pointers   | 32-bit |   64-bit  | 64-bit |  64-bit |
 * +------------+--------+-----------+--------+---------+
 * |    where   |   --   |   Tiger   |  Alpha | Windows |
 * |    used    |        |   Unix    |  Cray  |         |
 * |            |        | Sun & SGI |        |         |
 * +------------+--------+-----------+--------+---------+
 * Taken from http://developer.apple.com/macosx/64bit.html
 */

//////////////////////////////////////////////////////////////////////////
// basic include for all basics
// introduces types and global functions
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
// setting some defines on platforms
//////////////////////////////////////////////////////////////////////////
#if (defined(__WIN32__) || defined(__WIN32) || defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER) || defined(__BORLANDC__)) && !defined(WIN32)
#define WIN32
#endif

#if defined(__MINGW32__) && !defined(MINGW)
#define MINGW
#endif

#if (defined(__CYGWIN__) || defined(__CYGWIN32__)) && !defined(CYGWIN)
#define CYGWIN
#endif

// __APPLE__ is the only predefined macro on MacOS
#if defined(__APPLE__)
#define __DARWIN__
#endif

// 64bit OS
#if defined(_M_IA64) || defined(_M_X64) || defined(_WIN64) || defined(_LP64) || defined(_ILP64) || defined(__LP64__) || defined(__ppc64__)
#define __64BIT__
#endif

#if defined(_ILP64)
#error "this specific 64bit architecture is not supported"
#endif

// debug mode
#if defined(_DEBUG) && !defined(DEBUG)
#define DEBUG
#endif

// debug function name
#ifndef __NETBSD__
#if __STDC_VERSION__ < 199901L
#	if __GNUC__ >= 2 || ( defined(_MSC_VER) && defined(__FUNCTION__) )
#		define __func__ __FUNCTION__
#	else
#		define __func__ ""
#	endif
#endif
#endif


// disable attributed stuff on non-GNU
#if !defined(__GNUC__) && !defined(MINGW)
#  define  __attribute__(x)
#endif

//////////////////////////////////////////////////////////////////////////
// portable printf/scanf format macros and integer definitions
// NOTE: Visual C++ uses <inttypes.h> and <stdint.h> provided in /3rdparty
//////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <inttypes.h>
#include <stdint.h>
#include <limits.h>

// temporary fix for bugreport:4961 (unintended conversion from signed to unsigned)
// (-20 >= UCHAR_MAX) returns true
// (-20 >= USHRT_MAX) returns true
#if defined(__FreeBSD__) && defined(__x86_64)
#undef UCHAR_MAX
#define UCHAR_MAX ((unsigned char)0xff)
#undef USHRT_MAX
#define USHRT_MAX ((unsigned short)0xffff)
#endif

// ILP64 isn't supported, so always 32 bits?
#ifndef UINT_MAX
#define UINT_MAX 0xffffffff
#endif

//////////////////////////////////////////////////////////////////////////
// Integers with guaranteed _exact_ size.
//////////////////////////////////////////////////////////////////////////

typedef int8_t		int8;
typedef int16_t		int16;
typedef int32_t		int32;
typedef int64_t		int64;

typedef int8_t		sint8;
typedef int16_t		sint16;
typedef int32_t		sint32;
typedef int64_t		sint64;

typedef uint8_t		uint8;
typedef uint16_t	uint16;
typedef uint32_t	uint32;
typedef uint64_t	uint64;

#undef UINT8_MIN
#undef UINT16_MIN
#undef UINT32_MIN
#undef UINT64_MIN
#define UINT8_MIN	((uint8) UINT8_C(0x00))
#define UINT16_MIN	((uint16)UINT16_C(0x0000))
#define UINT32_MIN	((uint32)UINT32_C(0x00000000))
#define UINT64_MIN	((uint64)UINT64_C(0x0000000000000000))

#undef UINT8_MAX
#undef UINT16_MAX
#undef UINT32_MAX
#undef UINT64_MAX
#define UINT8_MAX	((uint8) UINT8_C(0xFF))
#define UINT16_MAX	((uint16)UINT16_C(0xFFFF))
#define UINT32_MAX	((uint32)UINT32_C(0xFFFFFFFF))
#define UINT64_MAX	((uint64)UINT64_C(0xFFFFFFFFFFFFFFFF))

#undef SINT8_MIN
#undef SINT16_MIN
#undef SINT32_MIN
#undef SINT64_MIN
#define SINT8_MIN	((sint8) INT8_C(0x80))
#define SINT16_MIN	((sint16)INT16_C(0x8000))
#define SINT32_MIN	((sint32)INT32_C(0x80000000))
#define SINT64_MIN	((sint32)INT64_C(0x8000000000000000))

#undef SINT8_MAX
#undef SINT16_MAX
#undef SINT32_MAX
#undef SINT64_MAX
#define SINT8_MAX	((sint8) INT8_C(0x7F))
#define SINT16_MAX	((sint16)INT16_C(0x7FFF))
#define SINT32_MAX	((sint32)INT32_C(0x7FFFFFFF))
#define SINT64_MAX	((sint64)INT64_C(0x7FFFFFFFFFFFFFFF))

//////////////////////////////////////////////////////////////////////////
// Integers with guaranteed _minimum_ size.
// These could be larger than you expect,
// they are designed for speed.
//////////////////////////////////////////////////////////////////////////
typedef          long int   ppint;
typedef          long int   ppint8;
typedef          long int   ppint16;
typedef          long int   ppint32;

typedef unsigned long int   ppuint;
typedef unsigned long int   ppuint8;
typedef unsigned long int   ppuint16;
typedef unsigned long int   ppuint32;


//////////////////////////////////////////////////////////////////////////
// integer with exact processor width (and best speed)
//////////////////////////////
#include <stddef.h> // size_t

#if defined(WIN32) && !defined(MINGW) // does not have a signed size_t
//////////////////////////////
#if defined(_WIN64)	// native 64bit windows platform
typedef __int64			ssize_t;
#else
typedef int				ssize_t;
#endif
//////////////////////////////
#endif
//////////////////////////////


//////////////////////////////////////////////////////////////////////////
// pointer sized integers
//////////////////////////////////////////////////////////////////////////
typedef intptr_t intptr;
typedef uintptr_t uintptr;


//////////////////////////////////////////////////////////////////////////
// some redefine of function redefines for some Compilers
//////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define strcasecmp			stricmp
#define strncasecmp			strnicmp
#define strncmpi			strnicmp
#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf			_snprintf
#endif
#if defined(_MSC_VER) && _MSC_VER < 1400
#define vsnprintf			_vsnprintf
#endif
#else
#define strcmpi				strcasecmp
#define stricmp				strcasecmp
#define strncmpi			strncasecmp
#define strnicmp			strncasecmp
#endif
#if defined(_MSC_VER) && _MSC_VER > 1200
#define strtoull	_strtoui64
#define strtoll     _strtoi64
#endif

// keyword replacement in windows
#ifdef _MSC_VER
#define inline __inline
#define forceinline __forceinline
#define ra_align(n) __declspec(align(n))
#define _chdir chdir
#endif

/////////////////////////////
// for those still not building c++
#ifndef __cplusplus
//////////////////////////////

//////////////////////////////
#endif // not __cplusplus
//////////////////////////////

// boolean types for C
#if !defined(_MSC_VER) || _MSC_VER >= 1800
// MSVC doesn't have stdbool.h yet as of Visual Studio 2012 (MSVC version 17.00)
// but it will support it in Visual Studio 2013 (MSVC version 18.00)
// http://blogs.msdn.com/b/vcblog/archive/2013/07/19/c99-library-support-in-visual-studio-2013.aspx
// GCC and Clang are assumed to be C99 compliant
#include <stdbool.h> // bool, true, false, __bool_true_false_are_defined
#endif // ! defined(_MSC_VER) || _MSC_VER >= 1800

#ifndef __bool_true_false_are_defined
// If stdbool.h is not available or does not define this
typedef char bool;
#define false (1==0)
#define true  (1==1)
#define __bool_true_false_are_defined
#endif // __bool_true_false_are_defined

//////////////////////////////////////////////////////////////////////////
// macro tools

#ifdef swap // just to be sure
#undef swap
#endif
// hmm only ints?
//#define swap(a,b) { int temp=a; a=b; b=temp;} 
// if using macros then something that is type independent
//#define swap(a,b) ((a == b) || ((a ^= b), (b ^= a), (a ^= b)))
// Avoid "value computed is not used" warning and generates the same assembly code
#define swap(a,b) if( (a) != (b) ) ( ( (a) ^= (b) ), ( (b) ^= (a) ), ( (a) ^= (b) ) )
#define swap_ptr(a,b) if ((a) != (b)) ((a) = (void*)((intptr_t)(a) ^ (intptr_t)(b)), (b) = (void*)((intptr_t)(a) ^ (intptr_t)(b)), (a) = (void*)((intptr_t)(a) ^ (intptr_t)(b)))

//////////////////////////////////////////////////////////////////////////
// should not happen
#ifndef NULL
#define NULL ((void *)0)
#endif

//////////////////////////////////////////////////////////////////////////
// Additional printf specifiers
#if defined(_MSC_VER)
#define PRIS_PREFIX "I"
#else // gcc
#define PRIS_PREFIX "z"
#endif
#define PRIdS PRIS_PREFIX "d"
#define PRIxS PRIS_PREFIX "x"
#define PRIuS PRIS_PREFIX "u"
#define PRIXS PRIS_PREFIX "X"
#define PRIoS PRIS_PREFIX "o"

//////////////////////////////////////////////////////////////////////////
// number of bits in a byte
#ifndef NBBY
#define	NBBY 8
#endif

//////////////////////////////////////////////////////////////////////////
// path separator

#if defined(WIN32)
#define PATHSEP '\\'
#define PATHSEP_STR "\\"
#elif defined(__APPLE__) && !defined(__MACH__)
// __MACH__ indicates OS X ( http://sourceforge.net/p/predef/wiki/OperatingSystems/ )
#define PATHSEP ':'
#define PATHSEP_STR ":"
#else
#define PATHSEP '/'
#define PATHSEP_STR "/"
#endif

//////////////////////////////////////////////////////////////////////////
// Assert

#if !defined(Assert)
#if defined(RELEASE)
#define Assert(EX)
#else
// extern "C" {
#include <assert.h>
// }
#if !defined(DEFCPP) && defined(WIN32) && !defined(MINGW)
#include <crtdbg.h>
#endif
#define Assert(EX) assert(EX)
#endif
#endif /* ! defined(Assert) */

//////////////////////////////////////////////////////////////////////////
// Has to be unsigned to avoid problems in some systems
// Problems arise when these functions expect an argument in the range [0,256[ and are fed a signed char.
#include <ctype.h>
#define ISALNUM(c) (isalnum((unsigned char)(c)))
#define ISALPHA(c) (isalpha((unsigned char)(c)))
#define ISCNTRL(c) (iscntrl((unsigned char)(c)))
#define ISDIGIT(c) (isdigit((unsigned char)(c)))
#define ISGRAPH(c) (isgraph((unsigned char)(c)))
#define ISLOWER(c) (islower((unsigned char)(c)))
#define ISPRINT(c) (isprint((unsigned char)(c)))
#define ISPUNCT(c) (ispunct((unsigned char)(c)))
#define ISSPACE(c) (isspace((unsigned char)(c)))
#define ISUPPER(c) (isupper((unsigned char)(c)))
#define ISXDIGIT(c) (isxdigit((unsigned char)(c)))
#define TOASCII(c) (toascii((unsigned char)(c)))
#define TOLOWER(c) (tolower((unsigned char)(c)))
#define TOUPPER(c) (toupper((unsigned char)(c)))

//////////////////////////////////////////////////////////////////////////
// length of a static array
#define ARRAYLENGTH(A) ( sizeof(A)/sizeof((A)[0]) )

//////////////////////////////////////////////////////////////////////////
// Make sure va_copy exists
#include <stdarg.h> // va_list, va_copy(?)
#if !defined(va_copy)
#if defined(__va_copy)
#define va_copy __va_copy
#else
#define va_copy(dst, src) ((void) memcpy(&(dst), &(src), sizeof(va_list)))
#endif
#endif


//////////////////////////////////////////////////////////////////////////
// Use the preprocessor to 'stringify' stuff (concert to a string).
// example:
//   #define TESTE blabla
//   QUOTE(TESTE) -> "TESTE"
//   EXPAND_AND_QUOTE(TESTE) -> "blabla"
#define QUOTE(x) #x
#define EXPAND_AND_QUOTE(x) QUOTE(x)


//////////////////////////////////////////////////////////////////////////
// Set a pointer variable to a pointer value.
#ifdef __cplusplus
template <typename T1, typename T2>
void SET_POINTER(T1*&var, T2* p)
{
	var = static_cast<T1*>(p);
}
template <typename T1, typename T2>
void SET_FUNCPOINTER(T1& var, T2 p)
{
	char ASSERT_POINTERSIZE[sizeof(T1) == sizeof(void*) && sizeof(T2) == sizeof(void*)?1:-1];// 1 if true, -1 if false
	union{ T1 out; T2 in; } tmp;// /!\ WARNING casting a pointer to a function pointer is against the C++ standard
	tmp.in = p;
	var = tmp.out;

	// unused variable
	(void)ASSERT_POINTERSIZE;
}
#else
#define SET_POINTER(var,p) (var) = (p)
#define SET_FUNCPOINTER(var,p) (var) = (p)
#endif

#ifndef max
static inline int max(int a, int b) { return (a > b) ? a : b; } //default is int
#endif
static inline int8 i8max(int8 a, int8 b) { return (a > b) ? a : b; }
static inline int16 i16max(int16 a, int16 b) { return (a > b) ? a : b; }
static inline int32 i32max(int32 a, int32 b) { return (a > b) ? a : b; }
static inline int64 i64max(int64 a, int64 b) { return (a > b) ? a : b; }
static inline uint32 umax(uint32 a, uint32 b) { return (a > b) ? a : b; }
static inline uint8 u8max(uint8 a, uint8 b) { return (a > b) ? a : b; }
static inline uint16 u16max(uint16 a, uint16 b) { return (a > b) ? a : b; }
static inline uint32 u32max(uint32 a, uint32 b) { return (a > b) ? a : b; }
static inline uint64 u64max(uint64 a, uint64 b) { return (a > b) ? a : b; }
static inline size_t zmax(size_t a, size_t b) { return (a > b) ? a : b; } //cause those varie

#ifndef min
static inline int min(int a, int b) { return (a < b) ? a : b; } //default is int
#endif
static inline int8 i8min(int8 a, int8 b) { return (a < b) ? a : b; }
static inline int16 i16min(int16 a, int16 b) { return (a < b) ? a : b; }
static inline int32 i32min(int32 a, int32 b) { return (a < b) ? a : b; }
static inline int64 i64min(int64 a, int64 b) { return (a < b) ? a : b; }
static inline uint32 umin(uint32 a, uint32 b) { return (a < b) ? a : b; }
static inline uint8 u8min(uint8 a, uint8 b) { return (a < b) ? a : b; }
static inline uint16 u16min(uint16 a, uint16 b) { return (a < b) ? a : b; }
static inline uint32 u32min(uint32 a, uint32 b) { return (a < b) ? a : b; }
static inline uint64 u64min(uint64 a, uint64 b) { return (a < b) ? a : b; }
static inline size_t zmin(size_t a, size_t b) { return (a < b) ? a : b; }

#endif /* _CBASETYPES_H_ */
