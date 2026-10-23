/* The small, self-contained calls of q3_api.h: the item table and the error test.
 * The host (server, game module, botlib) is in engine/q3_host.c. */
#include <setjmp.h>

#include "../thirdparty/ioq3/code/qcommon/q_shared.h"
#include "../thirdparty/ioq3/code/qcommon/qcommon.h"
#include "../thirdparty/ioq3/code/game/bg_public.h"
#include "q3_api.h"
#include "ioq3_commit.h"

extern jmp_buf abortframe;   /* common.c: Com_Error(ERR_DROP) longjmps here */

const char *q3_source_version(void) { return IOQ3_COMMIT; }

/* bg_misc.c's showevents debug cvar. */
void trap_Cvar_VariableStringBuffer(const char *var_name, char *buffer, int bufsize) {
	Cvar_VariableStringBuffer(var_name, buffer, bufsize);
}

/* Pmove's velocity snap (cgame's trap_SnapVector): the engine's SSE round-to-nearest. */
void trap_SnapVector(float *v) {
	Q_SnapVector(v);
}

int q3_item_count(void) { return bg_numItems; }

const char *q3_item_classname(int index) {
	if (index < 0 || index >= bg_numItems || !bg_itemlist[index].classname)
		return "";
	return bg_itemlist[index].classname;
}

int q3_item_quantity(int index) {
	return (index < 0 || index >= bg_numItems) ? 0 : bg_itemlist[index].quantity;
}

/* Needs the host up (Com_Error shuts the server down before it longjmps). */
int q3_test_error(void) {
	if (!q3_host_ready())
		return 0;
	if (setjmp(abortframe))
		return 1;
	BG_FindItemForHoldable(HI_NUM_HOLDABLE + 1);   /* not in the table: Com_Error(ERR_DROP) */
	return 0;
}
