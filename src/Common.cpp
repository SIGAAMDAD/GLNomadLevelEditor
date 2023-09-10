#include "Common.hpp"
#include "GUI.h"
#include "Editor.h"
#ifdef USE_ZONE
#include "Zone.cpp"
#else
#include "Heap.cpp"
#endif
#include <bzlib.h>
#include <zlib.h>

#include <backtrace.h>
#include <cxxabi.h> // for demangling C++ symbols

int parm_saveJsonMaps;
int parm_saveJsonTilesets;
int parm_useInternalTilesets;
int parm_useInternalMaps;
int parm_compression;

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

int myargc;
char **myargv;


static struct backtrace_state *bt_state = NULL;

static void bt_error_callback( void *data, const char *msg, int errnum )
{
    Error("libbacktrace ERROR: %d - %s", errnum, msg);
}

static void bt_syminfo_callback( void *data, uintptr_t pc, const char *symname,
								 uintptr_t symval, uintptr_t symsize )
{
	if (symname != NULL) {
		int status;
		// FIXME: sucks that __cxa_demangle() insists on using malloc().. but so does printf()
		char* name = abi::__cxa_demangle(symname, NULL, NULL, &status);
		if (name != NULL) {
			symname = name;
		}
		Printf("  %zu %s", pc, symname);
		free(name);
	} else {
        Printf("  %zu (unknown symbol)", pc);
	}
}

static int bt_pcinfo_callback( void *data, uintptr_t pc, const char *filename, int lineno, const char *function )
{
	if (data != NULL) {
		int* hadInfo = (int*)data;
		*hadInfo = (function != NULL);
	}

	if (function != NULL) {
		int status;
		// FIXME: sucks that __cxa_demangle() insists on using malloc()..
		char* name = abi::__cxa_demangle(function, NULL, NULL, &status);
		if (name != NULL) {
			function = name;
		}

		const char* fileNameNeo = strstr(filename, "/neo/");
		if (fileNameNeo != NULL) {
			filename = fileNameNeo+1; // I want "neo/bla/blub.cpp:42"
		}
        Printf("  %zu %s:%d %s", pc, filename, lineno, function);
		free(name);
	}

	return 0;
}

static void bt_error_dummy( void *data, const char *msg, int errnum )
{
	//CrashPrintf("ERROR-DUMMY: %d - %s\n", errnum, msg);
}

static int bt_simple_callback(void *data, uintptr_t pc)
{
	int pcInfoWorked = 0;
	// if this fails, the executable doesn't have debug info, that's ok (=> use bt_error_dummy())
	backtrace_pcinfo(bt_state, pc, bt_pcinfo_callback, bt_error_dummy, &pcInfoWorked);
	if (!pcInfoWorked) { // no debug info? use normal symbols instead
		// yes, it would be easier to call backtrace_syminfo() in bt_pcinfo_callback() if function == NULL,
		// but some libbacktrace versions (e.g. in Ubuntu 18.04's g++-7) don't call bt_pcinfo_callback
		// at all if no debug info was available - which is also the reason backtrace_full() can't be used..
		backtrace_syminfo(bt_state, pc, bt_syminfo_callback, bt_error_callback, NULL);
	}

	return 0;
}


static void do_backtrace(void)
{
    // can't use idStr here and thus can't use Sys_GetPath(PATH_EXE) => added Posix_GetExePath()
	const char* exePath = "mapeditor";
	bt_state = backtrace_create_state(exePath[0] ? exePath : NULL, 0, bt_error_callback, NULL);

    if (bt_state != NULL) {
		int skip = 1; // skip this function in backtrace
		backtrace_simple(bt_state, skip, bt_simple_callback, bt_error_callback, NULL);
	} else {
        Error("(No backtrace because libbacktrace state is NULL)");
	}
}


void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
#ifdef USE_ZONE
	return Z_Malloc(size, TAG_STATIC, NULL, "op new");
#else
	return Mem_Alloc(size);
#endif
}

uint64_t LoadFile(const char *filename, void **buffer)
{
	void *buf;
	uint64_t length;
	FILE *fp;

	fp = SafeOpenRead(BuildOSPath(Editor::GetPWD(), "Data/", filename));

	length = FileLength(fp);
	buf = Malloc(length);

	SafeRead(buf, length, fp);
	fclose(fp);

	*buffer = buf;

	return length;
}

uint64_t LittleLong(uint64_t l)
{
#ifdef __BIG_ENDIAN__
	byte b1, b2, b3, b4, b5, b6, b7;

	b1 = l & 0xff;
	b2 = (l >> 8) & 0xff;
	b3 = (l >> 16) & 0xff;
	b4 = (l >> 24) & 0xff;
	b5 = (l >> 32) & 0xff;
	b6 = (l >> 40) & 0xff;
	b7 = (l >> 48) & 0xff;

	return ((uint64_t)b1<<48) + ((uint64_t)b2<<40) + ((uint64_t)b3<<32) + ((uint64_t)b4<<24) + ((uint64_t)b5<<16) + ((uint64_t)b6<<8) + b7;
#else
	return l;
#endif
}

uint32_t LittleInt(uint32_t l)
{
#ifdef __BIG_ENDIAN__
	byte b1, b2, b3, b4;

	b1 = l & 0xff;
	b2 = (l >> 8) & 0xff;
	b3 = (l >> 16) & 0xff;
	b4 = (l >> 24) & 0xff;

	return ((uint32_t)b1<<24) + ((uint32_t)b2<<16) + ((uint32_t)b3<<8) + b4;
#else
	return l;
#endif
}

float LittleFloat(float f)
{
	typedef union {
	    float	f;
		uint32_t i;
	} _FloatByteUnion;

	const _FloatByteUnion *in;
	_FloatByteUnion out;

	in = (_FloatByteUnion *)&f;
	out.i = LittleInt(in->i);

	return out.f;
}

void Exit(void)
{
    Printf("Exiting app (code : 1)");
    exit(1);
}

void Error(const char *fmt, ...)
{
    va_list argptr;
    char buffer[4096];

    va_start(argptr, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);

	do_backtrace();

	GUI::Print("ERROR: %s", buffer);
	GUI::Print("Exiting app (code : -1)");
	
    spdlog::critical("ERROR: {}", buffer);
    spdlog::critical("Exiting app (code : -1)");

    exit(-1);
}

void Printf(const char *fmt, ...)
{
    va_list argptr;
    char buffer[4096];

    va_start(argptr, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);

    spdlog::info("{}", buffer);
	GUI::Print("%s", buffer);
}

static inline const char *bzip2_strerror(int err)
{
	switch (err) {
	case BZ_DATA_ERROR: return "(BZ_DATA_ERROR) buffer provided to bzip2 was corrupted";
	case BZ_MEM_ERROR: return "(BZ_MEM_ERROR) memory allocation request made by bzip2 failed";
	case BZ_DATA_ERROR_MAGIC: return "(BZ_DATA_ERROR_MAGIC) buffer was not compressed with bzip2, it did not contain \"BZA\"";
	case BZ_IO_ERROR: return va("(BZ_IO_ERROR) failure to read or write, file I/O error");
	case BZ_UNEXPECTED_EOF: return "(BZ_UNEXPECTED_EOF) unexpected end of data stream";
	case BZ_OUTBUFF_FULL: return "(BZ_OUTBUFF_FULL) buffer overflow";
	case BZ_SEQUENCE_ERROR: return "(BZ_SEQUENCE_ERROR) bad function call error, please report this bug";
	case BZ_OK:
		break;
	};
	return "No Error... How?";
}

static inline void CheckBZIP2(int errcode, uint64_t buflen, const char *action)
{
	switch (errcode) {
	case BZ_OK:
	case BZ_RUN_OK:
	case BZ_FLUSH_OK:
	case BZ_FINISH_OK:
	case BZ_STREAM_END:
		return; // all good
	case BZ_CONFIG_ERROR:
	case BZ_DATA_ERROR:
	case BZ_DATA_ERROR_MAGIC:
	case BZ_IO_ERROR:
	case BZ_MEM_ERROR:
	case BZ_PARAM_ERROR:
	case BZ_SEQUENCE_ERROR:
	case BZ_OUTBUFF_FULL:
	case BZ_UNEXPECTED_EOF:
		Error("Failure on %s of %lu bytes. BZIP2 error reason:\n\t%s", action, buflen, bzip2_strerror(errcode));
		break;
	};
}

static char *Compress_BZIP2(void *buf, uint64_t buflen, uint64_t *outlen)
{
	char *out, *newbuf;
	unsigned int len;
	int ret;

	Printf("Compressing %lu bytes with bzip2...", buflen);

	len = buflen;
	out = (char *)Malloc(buflen);
	ret = BZ2_bzBuffToBuffCompress(out, &len, (char *)buf, buflen, 9, 4, 50);
	CheckBZIP2(ret, buflen, "compression");

	Printf("Successful compression of %lu to %u bytes with zlib", buflen, len);
	newbuf = (char *)Malloc(len);
	memcpy(newbuf, out, len);
	Free(out);
	*outlen = len;

	return newbuf;
}

static inline const char *zlib_strerror(int err)
{
	switch (err) {
	case Z_DATA_ERROR: return "(Z_DATA_ERROR) buffer provided to zlib was corrupted";
	case Z_BUF_ERROR: return "(Z_BUF_ERROR) buffer overflow";
	case Z_STREAM_ERROR: return "(Z_STREAM_ERROR) bad params passed to zlib, please report this bug";
	case Z_MEM_ERROR: return "(Z_MEM_ERROR) memory allocation request made by zlib failed";
	case Z_OK:
		break;
	};
	return "No Error... How?";
}

static void *zalloc(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	(void)items;
	return Malloc(size);
}

static void zfree(voidpf opaque, voidpf address)
{
	(void)opaque;
	Free((void *)address);
}

static char *Compress_ZLIB(void *buf, uint64_t buflen, uint64_t *outlen)
{
	char *out, *newbuf;
	const uint64_t expectedLen = buflen / 2;
	int ret;

	out = (char *)Malloc(buflen);

#if 0
	stream.zalloc = zalloc;
	stream.zfree = zfree;
	stream.opaque = Z_NULL;
	stream.next_in = (Bytef *)buf;
	stream.avail_in = buflen;
	stream.next_out = (Bytef *)out;
	stream.avail_out = buflen;

	ret = deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 9, );
	if (ret != Z_OK) {
		Error("Failed to compress buffer of %lu bytes (inflateInit2)", buflen);
		return NULL;
	}
	inflate

	do {
		ret = inflate(&stream, Z_SYNC_FLUSH);

		switch (ret) {
		case Z_NEED_DICT:
		case Z_STREAM_ERROR:
			ret = Z_DATA_ERROR;
			break;
		case Z_DATA_ERRO:
		case Z_MEM_ERROR:
			inflateEnd(&stream);
			Error("Failed to compress buffer of %lu bytes (inflate)", buflen);
			return NULL;
		};
		
		if (ret != Z_STREAM_END) {
			newbuf = (char *)Malloc(buflen * 2);
			memcpy(newbuf, out, buflen * 2);
			Free(out);
			out = newbuf;

			stream.next_out = (Bytef *)(out + buflen);
			stream.avail_out = buflen;
			buflen *= 2;
		}
	} while (ret != Z_STREAM_END);
#endif
	Printf("Compressing %lu bytes with zlib", buflen);

	ret = compress2((Bytef *)out, (uLongf *)outlen, (const Bytef *)buf, buflen, Z_BEST_COMPRESSION);
	if (ret != Z_OK)
		Error("Failure on compression of %lu bytes. ZLIB error reason:\n\t%s", buflen, zError(ret));
	
	Printf("Successful compression of %lu to %lu bytes with zlib", buflen, *outlen);
	newbuf = (char *)Malloc(*outlen);
	memcpy(newbuf, out, *outlen);
	Free(out);

	return newbuf;
}

char *Compress(void *buf, uint64_t buflen, uint64_t *outlen, int compression)
{
	switch (compression) {
	case COMPRESS_BZIP2:
		return Compress_BZIP2(buf, buflen, outlen);
	case COMPRESS_ZLIB:
		return Compress_ZLIB(buf, buflen, outlen);
	default:
		break;
	};
	return (char *)buf;
}

static char *Decompress_BZIP2(void *buf, uint64_t buflen, uint64_t *outlen)
{
	char *out, *newbuf;
	unsigned int len;
	int ret;

	Printf("Decompressing %lu bytes with bzip2", buflen);
	len = buflen * 2;
	out = (char *)Malloc(buflen * 2);
	ret = BZ2_bzBuffToBuffDecompress(out, &len, (char *)buf, buflen, 0, 4);
	CheckBZIP2(ret, buflen, "decompression");

	Printf("Successful decompression of %lu to %u bytes with bzip2", buflen, len);
	newbuf = (char *)Malloc(len);
	memcpy(newbuf, out, len);
	Free(out);
	*outlen = len;

	return newbuf;
}

static char *Decompress_ZLIB(void *buf, uint64_t buflen, uint64_t *outlen)
{
	char *out, *newbuf;
	int ret;

	Printf("Decompressing %lu bytes with zlib...", buflen);
	out = (char *)Malloc(buflen * 2);
	*outlen = buflen * 2;
	ret = uncompress((Bytef *)out, outlen, (const Bytef *)buf, buflen);
	if (ret != Z_OK)
		Error("Failure on decompression of %lu bytes. ZLIB error reason:\n\t:%s", buflen * 2, zError(ret));
	
	Printf("Successful decompression of %lu bytes to %lu bytes with zlib", buflen, *outlen);
	newbuf = (char *)Malloc(*outlen);
	memcpy(newbuf, out, *outlen);
	Free(out);

	return newbuf;
}

char *Decompress(void *buf, uint64_t buflen, uint64_t *outlen, int compression)
{
	switch (compression) {
	case COMPRESS_BZIP2:
		return Decompress_BZIP2(buf, buflen, outlen);
	case COMPRESS_ZLIB:
		return Decompress_ZLIB(buf, buflen, outlen);
	default:
		break;
	};
	return (char *)buf;
}

bool IsAbsolutePath(const string_t& path)
{
	return strrchr(path.c_str(), PATH_SEP) == NULL;
}

int GetParm(const char *parm)
{
    int i;

    for (i = 1; i < myargc - 1; i++) {
        if (!N_stricmp(myargv[i], parm))
            return i;
    }
    return -1;
}

const char *GetFilename(const char *path)
{
	const char *dir;

	dir = strrchr(path, PATH_SEP);
	return dir ? dir + 1 : path;
}

/*
==================
COM_DefaultExtension

if path doesn't have an extension, then append
 the specified one (which should include the .)
==================
*/
void COM_DefaultExtension( char *path, uint64_t maxSize, const char *extension )
{
	const char *dot = strrchr(path, '.'), *slash;
	if (dot && ((slash = strrchr(path, '/')) == NULL || slash < dot))
		return;
	else
		N_strcat(path, maxSize, extension);
}


void COM_StripExtension(const char *in, char *out, uint64_t destsize)
{
	const char *dot = (char *)strrchr(in, '.'), *slash;

	if (dot && ((slash = (char *)strrchr(in, '/')) == NULL || slash < dot))
		destsize = (destsize < dot-in+1 ? destsize : dot-in+1);

	if ( in == out && destsize > 1 )
		out[destsize-1] = '\0';
	else
		N_strncpy(out, in, destsize);
}

const char *COM_GetExtension( const char *name )
{
	const char *dot = strrchr(name, '.'), *slash;
	if (dot && ((slash = strrchr(name, '/')) == NULL || slash < dot))
		return dot + 1;
	else
		return "";
}
#ifdef _WIN32
/*
=============
N_vsnprintf
 
Special wrapper function for Microsoft's broken _vsnprintf() function. mingw-w64
however, uses Microsoft's broken _vsnprintf() function.
=============
*/
int N_vsnprintf( char *str, size_t size, const char *format, va_list ap )
{
	int retval;
	
	retval = _vsnprintf( str, size, format, ap );

	if ( retval < 0 || (size_t)retval == size ) {
		// Microsoft doesn't adhere to the C99 standard of vsnprintf,
		// which states that the return value must be the number of
		// bytes written if the output string had sufficient length.
		//
		// Obviously we cannot determine that value from Microsoft's
		// implementation, so we have no choice but to return size.
		
		str[size - 1] = '\0';
		return size;
	}
	
	return retval;
}
#endif

int N_isprint( int c )
{
	if ( c >= 0x20 && c <= 0x7E )
		return ( 1 );
	return ( 0 );
}


int N_islower( int c )
{
	if (c >= 'a' && c <= 'z')
		return ( 1 );
	return ( 0 );
}


int N_isupper( int c )
{
	if (c >= 'A' && c <= 'Z')
		return ( 1 );
	return ( 0 );
}


int N_isalpha( int c )
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return ( 1 );
	return ( 0 );
}

bool N_isintegral(float f)
{
	return (int)f == f;
}


bool N_isanumber( const char *s )
{
#ifdef Q3_VM
    //FIXME: implement
    return qfalse;
#else
    char *p;

	if( *s == '\0' )
        return false;

	strtod( s, &p );

    return *p == '\0';
#endif
}

void N_itoa(char *buf, uint64_t bufsize, int i)
{
	snprintf(buf, bufsize, "%i", i);
}

void N_ftoa(char *buf, uint64_t bufsize, float f)
{
	snprintf(buf, bufsize, "%f", f);
}

void N_strcpy (char *dest, const char *src)
{
	char *d = dest;
	const char *s = src;
	while (*s)
		*d++ = *s++;
	
	*d++ = 0;
}

void N_strncpyz (char *dest, const char *src, size_t count)
{
	if (!dest)
		Error( "N_strncpyz: NULL dest");
	if (!src)
		Error( "N_strncpyz: NULL src");
	if (count < 1)
		Error( "N_strncpyz: bad count");
	
#if 1 
	// do not fill whole remaining buffer with zeros
	// this is obvious behavior change but actually it may affect only buggy QVMs
	// which passes overlapping or short buffers to cvar reading routines
	// what is rather good than bad because it will no longer cause overwrites, maybe
	while ( --count > 0 && (*dest++ = *src++) != '\0' );
	*dest = '\0';
#else
	strncpy( dest, src, count-1 );
	dest[ count-1 ] = '\0';
#endif
}

void N_strncpy (char *dest, const char *src, size_t count)
{
	while (*src && count--)
		*dest++ = *src++;

	if (count)
		*dest++ = 0;
}


char *N_strupr(char *s1)
{
	char *s;

	s = s1;
	while (*s) {
		if (*s >= 'a' && *s <= 'z')
			*s = *s - 'a' + 'A';
		s++;
	}
	return s1;
}

// never goes past bounds or leaves without a terminating 0
bool N_strcat(char *dest, size_t size, const char *src)
{
	size_t l1;

	l1 = strlen(dest);
	if (l1 >= size) {
		Printf( "N_strcat: already overflowed" );
		return false;
	}

	N_strncpy( dest + l1, src, size - l1 );
	return true;
}

char *N_stradd(char *dst, const char *src)
{
	char c;
	while ( (c = *src++) != '\0' )
		*dst++ = c;
	*dst = '\0';
	return dst;
}


/*
* Find the first occurrence of find in s.
*/
const char *N_stristr(const char *s, const char *find)
{
	char c, sc;
	size_t len;

	if ((c = *find++) != 0) {
		if (c >= 'a' && c <= 'z') {
	    	c -= ('a' - 'A');
		}
 	   	len = strlen(find);
    	do {
    		do {
        		if ((sc = *s++) == 0)
          			return NULL;
        		if (sc >= 'a' && sc <= 'z') {
          			sc -= ('a' - 'A');
        		}
      		} while (sc != c);
    	} while (N_stricmpn(s, find, len) != 0);
   		s--;
  	}
  	return s;
}

int N_replace(const char *str1, const char *str2, char *src, size_t max_len)
{
	size_t len1, len2, count;
	ssize_t d;
	const char *s0, *s1, *s2, *max;
	char *match, *dst;

	match = strstr(src, str1);

	if (!match)
		return 0;

	count = 0; // replace count

    len1 = strlen(str1);
    len2 = strlen(str2);
    d = len2 - len1;

    if (d > 0) { // expand and replace mode
        max = src + max_len;
        src += strlen(src);

        do { // expand source string
			s1 = src;
            src += d;
            if (src >= max)
                return count;
            dst = src;
            
            s0 = match + len1;

            while (s1 >= s0)
                *dst-- = *s1--;
			
			// replace match
            s2 = str2;
			while (*s2)
                *match++ = *s2++;
			
            match = strstr(match, str1);

            count++;
		} while (match);

        return count;
    } 
    else if (d < 0) { // shrink and replace mode
        do  { // shrink source string
            s1 = match + len1;
            dst = match + len2;
            while ( (*dst++ = *s1++) != '\0' );
			
			//replace match
            s2 = str2;
			while ( *s2 ) {
				*match++ = *s2++;
			}

            match = strstr( match, str1 );

            count++;
        } 
        while ( match );

        return count;
    }
    else {
	    do { // just replace match
    	    s2 = str2;
			while (*s2)
				*match++ = *s2++;

    	    match = strstr(match, str1);
    	    count++;
		}  while (match);
	}

	return count;
}

size_t N_strlen (const char *str)
{
	size_t count = 0;
    while (str[count]) {
        ++count;
    }
	return count;
}

char *N_strrchr(char *str, char c)
{
    char *s = str;
    size_t len = N_strlen(s);
    s += len;
    while (len--)
    	if (*--s == c) return s;
    return 0;
}

int N_strcmp (const char *str1, const char *str2)
{
    const char *s1 = str1;
    const char *s2 = str2;
	while (1) {
		if (*s1 != *s2)
			return -1;              // strings not equal    
		if (!*s1)
			return 1;               // strings are equal
		s1++;
		s2++;
	}
	
	return 0;
}

bool N_streq(const char *str1, const char *str2)
{
	const char *s1 = str1;
	const char *s2 = str2;
	
	while (*s2 && *s1) {
		if (*s1++ != *s2++)
			return false;
	}
	return true;
}

bool N_strneq(const char *str1, const char *str2, size_t n)
{
	const char *s1 = str1;
	const char *s2 = str2;

	while (*s1 && n) {
		if (*s1++ != *s2++)
			return false;
		n--;
	}
	return true;
}

int N_strncmp( const char *s1, const char *s2, size_t n )
{
	int c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}
		
		if (c1 != c2) {
			return c1 < c2 ? -1 : 1;
		}
	} while (c1);
	
	return 0;		// strings are equal
}

int N_stricmpn (const char *str1, const char *str2, size_t n)
{
	int c1, c2;

	// bk001129 - moved in 1.17 fix not in id codebase
    if (str1 == NULL) {
    	if (str2 == NULL )
            return 0;
        else
            return -1;
    }
    else if (str2 == NULL)
        return 1;


	
	do {
		c1 = *str1++;
		c2 = *str2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}
		
		if (c1 != c2) {
			if (c1 >= 'a' && c1 <= 'z') {
				c1 -= ('a' - 'A');
			}
			if (c2 >= 'a' && c2 <= 'z') {
				c2 -= ('a' - 'A');
			}
			if (c1 != c2) {
				return c1 < c2 ? -1 : 1;
			}
		}
	} while (c1);
	
	return 0;		// strings are equal
}

int N_stricmp( const char *s1, const char *s2 ) 
{
	unsigned char c1, c2;

	if (s1 == NULL)  {
		if (s2 == NULL)
			return 0;
		else
			return -1;
	}
	else if (s2 == NULL)
		return 1;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 != c2) {
			if ( c1 <= 'Z' && c1 >= 'A' )
				c1 += ('a' - 'A');

			if ( c2 <= 'Z' && c2 >= 'A' )
				c2 += ('a' - 'A');

			if ( c1 != c2 ) 
				return c1 < c2 ? -1 : 1;
		}
	} while ( c1 != '\0' );

	return 0;
}

char* BuildOSPath(const path_t& curPath, const string_t& gamepath, const char *npath)
{
	static char ospath[MAX_OSPATH*2+1];
	char temp[MAX_OSPATH];

	if (npath)
		snprintf(temp, sizeof(temp), "%c%s%c%s", PATH_SEP, gamepath.c_str(), PATH_SEP, npath);
	else
		snprintf(temp, sizeof(temp), "%c%s%c", PATH_SEP, gamepath.c_str(), PATH_SEP);

	snprintf(ospath, sizeof(ospath), "%s%s", curPath.c_str(), temp);
	return ospath;
}

void *SafeMalloc(size_t size)
{
	void *p;

	Printf("Allocating %lu bytes with malloc()", size);

	p = Mem_Alloc(size);
	if (!p) {
		Error("malloc() failure on %lu bytes, strerror: %s", size, strerror(errno));
	}

	return p;
}


void SafeWrite(const void *buffer, size_t size, FILE *fp)
{
    Printf("Writing %lu bytes to file", size);
    if (!fwrite(buffer, size, 1, fp)) {
        Error("Failed to write %lu bytes to file", size);
    }
}

void SafeRead(void *buffer, size_t size, FILE *fp)
{
    Printf("Reading %lu bytes from file", size);
    if (!fread(buffer, size, 1, fp)) {
        Error("Failed to read %lu bytes from file", size);
    }
}

FILE *SafeOpenRead(const char *path)
{
    FILE *fp;
    
    Printf("Opening '%s' in read mode", path);
    fp = fopen(path, "rb");
    if (!fp) {
        Error("Failed to open file %s in read mode", path);
    }
    return fp;
}

FILE *SafeOpenWrite(const char *path)
{
    FILE *fp;
    
	Printf("Opening '%s' in write mode", path);
    fp = fopen(path, "wb");
    if (!fp) {
        Error("Failed to open file %s in write mode", path);
    }
    return fp;
}

bool FileExists(const char *path)
{
    FILE *fp;

    fp = fopen(path, "r");
    if (!fp)
        return false;
    
    fclose(fp);
    return true;
}

uint64_t FileLength(FILE *fp)
{
    uint64_t pos, end;

    pos = ftell(fp);
    fseek(fp, 0L, SEEK_END);
    end = ftell(fp);
    fseek(fp, pos, SEEK_SET);

    return end;
}

const char *FilterToString(uint32_t filter)
{
    switch (filter) {
    case GL_LINEAR: return "GL_LINEAR";
    case GL_NEAREST: return "GL_NEAREST";
    };

	// None
    return "None";
}

uint32_t StrToFilter(const char *str)
{
    if (!N_stricmp(str, "GL_LINEAR")) return GL_LINEAR;
    else if (!N_stricmp(str, "GL_NEAREST")) return GL_NEAREST;

	// None
    return 0;
}

const char *WrapToString(uint32_t wrap)
{
    switch (wrap) {
    case GL_REPEAT: return "GL_REPEAT";
    case GL_CLAMP_TO_EDGE: return "GL_CLAMP_TO_EDGE";
    case GL_CLAMP_TO_BORDER: return "GL_CLAMP_TO_BORDER";
    case GL_MIRRORED_REPEAT: return "GL_MIRRORED_REPEAT";
    };

	// None
    return "None";
}

uint32_t StrToWrap(const char *str)
{
    if (!N_stricmp(str, "GL_REPEAT")) return GL_REPEAT;
    else if (!N_stricmp(str, "GL_CLAMP_TO_EDGE")) return GL_CLAMP_TO_EDGE;
    else if (!N_stricmp(str, "GL_CLAMP_TO_BORDER")) return GL_CLAMP_TO_BORDER;
    else if (!N_stricmp(str, "GL_MIRRORED_REPEAT")) return GL_MIRRORED_REPEAT;

	// None
	return 0;
}

const char *FormatToString(uint32_t format)
{
    switch (format) {
    case GL_RGBA8: return "GL_RGBA8";
    case GL_RGBA12: return "GL_RGBA12";
    case GL_RGBA16: return "GL_RGBA16";
    };

	// None
	return "None";
}

uint32_t StrToFormat(const char *str)
{
    if (!N_stricmp(str, "GL_RGBA8")) return GL_RGBA8;
    else if (!N_stricmp(str, "GL_RGBA12")) return GL_RGBA12;
    else if (!N_stricmp(str, "GL_RGBA16")) return GL_RGBA16;
    
	// None
    return 0;
}

#define BIG_INFO_STRING 8192
#define MAX_STRING_TOKENS 1024
#define MAX_HISTORY 32
#define MAX_CMD_BUFFER  65536

#define arraylen(x) (sizeof((x))/sizeof((*x)))

static uint32_t numCommands = 0;
static uint32_t cmd_argc;
static char cmd_tokenized[BIG_INFO_STRING+MAX_STRING_TOKENS];
static char *cmd_argv[MAX_STRING_TOKENS];
static char cmd_cmd[BIG_INFO_STRING];

static char cmd_history[MAX_HISTORY][BIG_INFO_STRING];
static uint32_t cmd_historyused;

void *operator new[](size_t n, const char *, int, unsigned int, const char *, int)
{
    return new char[n];
}

const char *Argv(uint32_t index)
{
    if (index >= cmd_argc)
        return "";
    
    return cmd_argv[index];
}

uint32_t Argc(void)
{
    return cmd_argc;
}

void TokenizeString(const char *str, bool ignoreQuotes)
{
	const char *p;
	char *tok;

    memset(cmd_cmd, 0, sizeof(cmd_cmd));
    memset(cmd_tokenized, 0, sizeof(cmd_tokenized));
    cmd_argc = 0;

    strncpy(cmd_cmd, str, sizeof(cmd_cmd));
	p = str;
	tok = cmd_tokenized;

	while (1) {
		if (cmd_argc >= arraylen(cmd_argv)) {
			return; // usually something malicious
		}
		while (*p && *p <= ' ') {
			p++; // skip whitespace
		}
		if (!*p) {
			break; // end of string
		}
		// handle quoted strings
		if (!ignoreQuotes && *p == '\"') {
			cmd_argv[cmd_argc] = tok;
            cmd_argc++;
			p++;
			while (*p && *p != '\"') {
				*tok++ = *p++;
			}
			if (!*p) {
				return; // end of string
			}
			p++;
			continue;
		}

		// regular stuff
		cmd_argv[cmd_argc] = tok;
        cmd_argc++;

		// skip until whitespace, quote, or command
		while (*p > ' ') {
			if (!ignoreQuotes && p[0] == '\"') {
				break;
			}

			if (p[0] == '/' && p[1] == '/') {
				// accept protocol headers (e.g. http://) in command lines that match "*?[a-z]://" pattern
				if (p < cmd_cmd + 3 || p[-1] != ':' || p[-2] < 'a' || p[-2] > 'z') {
					break;
				}
			}

			// skip /* */ comments
			if (p[0] == '/' && p[1] == '*') {
				break;
			}

			*tok++ = *p++;
		}
		*tok++ = '\0';
		if (!*p) {
			return; // end of string
		}
	}
}
