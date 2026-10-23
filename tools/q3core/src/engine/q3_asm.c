/* ioquake3's asm/ftola.asm and asm/snapvector.asm (x86_64 versions) as SSE intrinsics, so the
 * build needs no MASM step; same instructions, same results. Plus the QVM JIT entry points,
 * which q3core leaves out: the game runs as a native DLL, and a QVM falls back to the interpreter. */
#include <emmintrin.h>

#include "../../thirdparty/ioq3/code/qcommon/vm_local.h"

/* cvttss2si: truncate toward zero */
long qftolsse(float f) {
	return _mm_cvtt_ss2si(_mm_set_ss(f));
}

/* Only the JIT calls this (it reads the VM's opStack registers); never reached here. */
int qvmftolsse(void) {
	return 0;
}

/* cvtps2dq + cvtdq2ps on x, y, z: round to nearest (even) under the default MXCSR, the rounding
 * Pmove's trap_SnapVector depends on. */
void qsnapvectorsse(vec3_t vec) {
	__m128 v = _mm_set_ps(0.0f, vec[2], vec[1], vec[0]);
	v = _mm_cvtepi32_ps(_mm_cvtps_epi32(v));
	vec[0] = _mm_cvtss_f32(v);
	vec[1] = _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)));
	vec[2] = _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2)));
}

void VM_Compile(vm_t *vm, vmHeader_t *header) {
	(void)header;
	vm->compiled = qfalse;   /* vm.c then interprets the bytecode */
}

int VM_CallCompiled(vm_t *vm, int *args) {
	(void)vm;
	(void)args;
	Com_Error(ERR_DROP, "q3core has no QVM compiler");
	return 0;
}
