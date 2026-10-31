/*
Oz: emoji in chat (oz_emoji.c). Chat crosses the network as ASCII (every Quake 3 engine turns a byte
above 127 into a dot), so an emoji goes as its code, ":fire:", which any other client shows as it is.
The HUD draws codes and the classic smileys as the emoji, from Windows' Segoe UI Emoji.
*/
#ifndef OZ_EMOJI_H
#define OZ_EMOJI_H

#ifdef __cplusplus
extern "C" {
#endif

#define OZ_EMOJI_MAX_NAME	80		// the longest code's name

int			OzEmoji_Lookup( const char *name, int len );	// a code's emoji (its code point), 0 for none
const char	*OzEmoji_Code( int codepoint );		// the name a message sends for an emoji, NULL for none
int			OzEmoji_Part( int codepoint );		// a piece of a sequence (skin tone, joiner, variation, flag letter)
int			OzEmoji_Utf8( int codepoint, char *out );	// writes 1-4 bytes, returns how many
int			OzEmoji_Decode( const char *s, int *codepoint );	// a UTF-8 character at s: returns its length (1 for a stray byte)

// codes and smileys as UTF-8 emoji; one with keep strictly inside it stays text, and *keepAt gets
// where keep ends up in out (both may be -1 / NULL); returns out's length
int			OzEmoji_Expand( const char *in, char *out, int size, int keep, int *keepAt );
int			OzEmoji_Before( const char *line, int at );	// the length of a code or smiley ending at line + at, 0 for none

// the emoji whose names start with prefix (2 letters at least), best first: short names, then shorter
int			OzEmoji_Suggest( const char *prefix, int len, int *codepoints, const char **names, int max );
int			OzEmoji_Typing( const char *line, int at );	// a code being typed before line + at (":fi"): where its name starts, -1 for none

#ifdef __cplusplus
}
#endif

#endif
