// SPDX-License-Identifier: MIT
#include <mach/kmod.h>
extern kern_return_t PM9C1aReadOnly_kern_start(kmod_info_t *, void *);
extern kern_return_t PM9C1aReadOnly_kern_stop(kmod_info_t *, void *);
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(com.stationk9.diagnostic.PM9C1aReadOnly, "0.1.0", _start, _stop)
kmod_start_func_t *_realmain = PM9C1aReadOnly_kern_start;
kmod_stop_func_t *_antimain = PM9C1aReadOnly_kern_stop;
