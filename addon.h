/*
 * Copyright 2023 MNX Cloud, Inc.
 */

/*
 * Note the deliberate use of addon.h instead of kstat.h as we need to avoid
 * the system kstat.h.
 */
#ifndef	ADDON_H
#define	ADDON_H

#include <kstat.h>
#include <libnvpair.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct kstatjs_stat {
	kstat_t *ksjs_ksp;		/* underlying kstat */
	struct kstatjs_stat *ksjs_next;	/* next stat of interest */
} kstatjs_stat_t;

typedef struct kstatjs {
	kstat_ctl_t *ksj_ctl;		/* kstat handle */
	kid_t ksj_kid;			/* kstat chain ID */
	kstatjs_stat_t *ksj_stats;	/* statistics of interest */
	char *ksj_module;		/* module name to match, if any */
	char *ksj_class;		/* class name to match, if any */
	char *ksj_name;			/* name to match, if any */
	int ksj_instance;		/* instance to match, or -1 if any */
} kstatjs_t;

kstatjs_t *kstatjs_init(nvlist_t *);
nvlist_t *kstatjs_read(kstatjs_t *, nvlist_t *);
int kstatjs_close(kstatjs_t *);
const char *kstatjs_errmsg(void);

#if defined(__cplusplus)
}
#endif

#endif
