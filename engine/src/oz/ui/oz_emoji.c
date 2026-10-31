/*
Oz: emoji in chat (oz_emoji.h). The codes are oz_emoji_table.h's (engine/tools/make_emoji_table.py):
each emoji's Unicode name in lower case, and short names as Discord and Slack spell them for the
common ones. A message sends the first short name, else the Unicode name. The classic smileys show as
emoji too, as whole words.
*/
#include "oz_emoji.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	const char	*name;
	int			codepoint;
	int			isShort;
} ozEmojiName_t;

typedef struct {
	int			codepoint;
	const char	*name;
} ozEmojiSend_t;

#include "oz_emoji_table.h"

#define NUM_NAMES	( (int)( sizeof( ozEmojiNames ) / sizeof( ozEmojiNames[0] ) ) )
#define NUM_SEND	( (int)( sizeof( ozEmojiSend ) / sizeof( ozEmojiSend[0] ) ) )

static const struct {
	const char	*text;
	int			codepoint;
} smileys[] = {
	{ ":)", 0x1f642 }, { ":-)", 0x1f642 }, { "=)", 0x1f642 }, { ":(", 0x1f641 }, { ":-(", 0x1f641 },
	{ ":D", 0x1f603 }, { ":-D", 0x1f603 }, { ";)", 0x1f609 }, { ";-)", 0x1f609 },
	{ ":P", 0x1f61b }, { ":p", 0x1f61b }, { ":-P", 0x1f61b }, { ":-p", 0x1f61b },
	{ ":O", 0x1f62e }, { ":o", 0x1f62e }, { ":-O", 0x1f62e }, { ":-o", 0x1f62e },
	{ ":'(", 0x1f622 }, { ":|", 0x1f610 }, { ":-|", 0x1f610 }, { ":/", 0x1f615 }, { ":-/", 0x1f615 },
	{ ":*", 0x1f618 }, { ":-*", 0x1f618 }, { ">:(", 0x1f620 }, { "xD", 0x1f606 }, { "XD", 0x1f606 },
	{ "<3", 0x2764 }, { "</3", 0x1f494 },
};

static int IsNameChar( int c ) {
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '+' || c == '-';
}

static void Lower( const char *s, int len, char *out ) {
	int i;

	for ( i = 0; i < len; i++ ) {
		out[i] = s[i] >= 'A' && s[i] <= 'Z' ? s[i] - 'A' + 'a' : s[i];
	}
	out[len] = 0;
}

int OzEmoji_Lookup( const char *name, int len ) {
	char	low[OZ_EMOJI_MAX_NAME + 1];
	int		lo = 0, hi = NUM_NAMES - 1;

	if ( len <= 0 || len > OZ_EMOJI_MAX_NAME ) {
		return 0;
	}
	Lower( name, len, low );
	while ( lo <= hi ) {
		const int mid = ( lo + hi ) / 2;
		const int c = strcmp( ozEmojiNames[mid].name, low );
		if ( !c ) {
			return ozEmojiNames[mid].codepoint;
		}
		if ( c < 0 ) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return 0;
}

const char *OzEmoji_Code( int codepoint ) {
	int lo = 0, hi = NUM_SEND - 1;

	while ( lo <= hi ) {
		const int mid = ( lo + hi ) / 2;
		if ( ozEmojiSend[mid].codepoint == codepoint ) {
			return ozEmojiSend[mid].name;
		}
		if ( ozEmojiSend[mid].codepoint < codepoint ) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return NULL;
}

int OzEmoji_Part( int c ) {
	return c == 0x200d || c == 0xfe0e || c == 0xfe0f || c == 0x20e3 || ( c >= 0x1f3fb && c <= 0x1f3ff )
		|| ( c >= 0x1f1e6 && c <= 0x1f1ff ) || ( c >= 0xe0020 && c <= 0xe007f ) || ( c >= 0x1f9b0 && c <= 0x1f9b3 );
}

int OzEmoji_Utf8( int c, char *out ) {
	if ( c < 0x80 ) {
		out[0] = (char)c;
		return 1;
	}
	if ( c < 0x800 ) {
		out[0] = (char)( 0xc0 | ( c >> 6 ) );
		out[1] = (char)( 0x80 | ( c & 0x3f ) );
		return 2;
	}
	if ( c < 0x10000 ) {
		out[0] = (char)( 0xe0 | ( c >> 12 ) );
		out[1] = (char)( 0x80 | ( ( c >> 6 ) & 0x3f ) );
		out[2] = (char)( 0x80 | ( c & 0x3f ) );
		return 3;
	}
	out[0] = (char)( 0xf0 | ( c >> 18 ) );
	out[1] = (char)( 0x80 | ( ( c >> 12 ) & 0x3f ) );
	out[2] = (char)( 0x80 | ( ( c >> 6 ) & 0x3f ) );
	out[3] = (char)( 0x80 | ( c & 0x3f ) );
	return 4;
}

int OzEmoji_Decode( const char *s, int *codepoint ) {
	const unsigned char *p = (const unsigned char *)s;
	int n, i;

	if ( p[0] < 0x80 ) {
		*codepoint = p[0];
		return 1;
	}
	n = ( p[0] & 0xe0 ) == 0xc0 ? 2 : ( p[0] & 0xf0 ) == 0xe0 ? 3 : ( p[0] & 0xf8 ) == 0xf0 ? 4 : 0;
	*codepoint = n ? p[0] & ( 0x7f >> n ) : 0xfffd;
	for ( i = 1; i < n; i++ ) {
		if ( ( p[i] & 0xc0 ) != 0x80 ) {
			*codepoint = 0xfffd;
			return i;
		}
		*codepoint = ( *codepoint << 6 ) | ( p[i] & 0x3f );
	}
	return n ? n : 1;
}

// a code at s, ":name:": its length, 0 for none ("^:" is a color code)
static int MatchCode( const char *start, const char *s, int *codepoint ) {
	const char *e;

	if ( *s != ':' || ( s > start && s[-1] == '^' ) ) {
		return 0;
	}
	for ( e = s + 1; IsNameChar( (unsigned char)*e ) && e - s <= OZ_EMOJI_MAX_NAME; e++ ) {
	}
	if ( *e != ':' || e == s + 1 ) {
		return 0;
	}
	*codepoint = OzEmoji_Lookup( s + 1, (int)( e - s - 1 ) );
	return *codepoint ? (int)( e - s + 1 ) : 0;
}

// a smiley at s, as a whole word: after the start, a space or a color code, before the end, a space,
// a color code or .,!?
static int MatchSmiley( const char *start, const char *s, int *codepoint ) {
	int i;

	if ( s > start && s[-1] != ' ' && !( s - start >= 2 && s[-2] == '^' && s[-1] != '^' ) ) {
		return 0;
	}
	for ( i = 0; i < (int)( sizeof( smileys ) / sizeof( smileys[0] ) ); i++ ) {
		const int n = (int)strlen( smileys[i].text );
		if ( !strncmp( s, smileys[i].text, n ) && ( !s[n] || strchr( " ^.,!?", s[n] ) ) ) {
			*codepoint = smileys[i].codepoint;
			return n;
		}
	}
	return 0;
}

int OzEmoji_Expand( const char *in, char *out, int size, int keep, int *keepAt ) {
	const char	*s = in;
	int			o = 0;

	if ( keepAt ) {
		*keepAt = -1;
	}
	while ( o < size - 1 ) {
		int cp = 0, n;
		if ( keepAt && s - in == keep ) {
			*keepAt = o;
		}
		if ( !*s ) {
			break;
		}
		n = MatchCode( in, s, &cp );
		if ( !n ) {
			n = MatchSmiley( in, s, &cp );
		}
		if ( n && !( keep > s - in && keep < s - in + n ) ) {
			char u[4];
			const int k = OzEmoji_Utf8( cp, u );
			if ( o + k > size - 1 ) {
				break;
			}
			memcpy( out + o, u, k );
			o += k;
			s += n;
			continue;
		}
		out[o++] = *s++;
	}
	out[o] = 0;
	return o;
}

int OzEmoji_Before( const char *line, int at ) {
	int i, cp;

	for ( i = at - 2; i >= 0 && at - i <= OZ_EMOJI_MAX_NAME + 2; i-- ) {
		if ( line[i] == ':' && MatchCode( line, line + i, &cp ) == at - i ) {
			return at - i;
		}
	}
	for ( i = 0; i < (int)( sizeof( smileys ) / sizeof( smileys[0] ) ); i++ ) {
		const int n = (int)strlen( smileys[i].text );
		if ( n <= at && MatchSmiley( line, line + at - n, &cp ) == n && !strncmp( line + at - n, smileys[i].text, n ) ) {
			return n;
		}
	}
	return 0;
}

int OzEmoji_Typing( const char *line, int at ) {
	int start = at;

	while ( start > 0 && IsNameChar( (unsigned char)line[start - 1] ) ) {
		start--;
	}
	if ( !start || line[start - 1] != ':' || ( start >= 2 && line[start - 2] == '^' ) || at - start < 2 ) {
		return -1;
	}
	return start;
}

static int CompareSuggestions( const void *a, const void *b ) {
	const ozEmojiName_t *x = *(const ozEmojiName_t *const *)a, *y = *(const ozEmojiName_t *const *)b;
	const int lx = (int)strlen( x->name ), ly = (int)strlen( y->name );

	if ( x->isShort != y->isShort ) {
		return y->isShort - x->isShort;
	}
	if ( lx != ly ) {
		return lx - ly;
	}
	return strcmp( x->name, y->name );
}

int OzEmoji_Suggest( const char *prefix, int len, int *codepoints, const char **names, int max ) {
	char					low[OZ_EMOJI_MAX_NAME + 1];
	const ozEmojiName_t		*found[256];
	int						lo = 0, hi = NUM_NAMES, n = 0, out = 0, i, j;

	if ( len < 2 || len > OZ_EMOJI_MAX_NAME ) {
		return 0;
	}
	Lower( prefix, len, low );
	// the first name not below the prefix; the ones that start with it follow
	while ( lo < hi ) {
		const int mid = ( lo + hi ) / 2;
		if ( strncmp( ozEmojiNames[mid].name, low, len ) < 0 ) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	for ( i = lo; i < NUM_NAMES && n < (int)( sizeof( found ) / sizeof( found[0] ) ) && !strncmp( ozEmojiNames[i].name, low, len ); i++ ) {
		found[n++] = &ozEmojiNames[i];
	}
	qsort( (void *)found, n, sizeof( found[0] ), CompareSuggestions );
	for ( i = 0; i < n && out < max; i++ ) {
		for ( j = 0; j < out && codepoints[j] != found[i]->codepoint; j++ ) {
		}
		if ( j == out ) {
			codepoints[out] = found[i]->codepoint;
			names[out++] = found[i]->name;
		}
	}
	return out;
}
