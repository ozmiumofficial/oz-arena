/* Offsets into the game module's gentity_t (g_local.h), which the engine only sees as its
 * sharedEntity_t head: the item timers read whether an entity is in use and when a taken
 * item comes back. Built from the same (patched) headers as qagame, so the layout matches. */
#include <stddef.h>

#include "../../thirdparty/ioq3/code/game/g_local.h"

int q3_gentity_inuse_offset(void) { return (int)offsetof(gentity_t, inuse); }
int q3_gentity_nextthink_offset(void) { return (int)offsetof(gentity_t, nextthink); }
