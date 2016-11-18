/*
 * Copyright 2016 Joyent, Inc.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <thread.h>
#include <errno.h>
#include <kstat.h>

#include <node_version.h>
#include "v8plus_glue.h"

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

#define	KSTATJS_ASSERT(cond) \
	if (!(cond)) { \
		v8plus_panic("kstat assertion failed in %s at line %d: %s\n", \
		    __FILE__, __LINE__, #cond); \
	}

static void
kstatjs_finalize(kstatjs_t *ksj)
{
	kstatjs_stat_t *ksjs, *next;

	free(ksj->ksj_module);
	ksj->ksj_module = NULL;

	free(ksj->ksj_class);
	ksj->ksj_class = NULL;

	free(ksj->ksj_name);
	ksj->ksj_name = NULL;

	ksj->ksj_instance = -1;

	if (ksj->ksj_ctl != NULL) {
		kstat_close(ksj->ksj_ctl);
		ksj->ksj_ctl = NULL;
	}

	for (ksjs = ksj->ksj_stats; ksjs != NULL; ksjs = next) {
		next = ksjs->ksjs_next;
		free(ksjs);
	}

	ksj->ksj_stats = NULL;
}

static void
kstatjs_dtor(void *op)
{
	kstatjs_t *ksj = op;

	kstatjs_finalize(ksj);
	free(ksj);
}

static int
kstatjs_lookup_nvpair(const nvlist_t *arg, const char *str, nvpair_t **nvpp)
{
	int err;

	if ((err = nvlist_lookup_nvpair((nvlist_t *)arg, str, nvpp)) != 0) {
		/*
		 * Disappointingly, nvlist_lookup_nvpair() conflates the
		 * EINVAL and ENOENT case -- so we must check for EINVAL here
		 * and assume that it was actually ENOENT that was intended.
		 */
		if (err == EINVAL) {
			*nvpp = NULL;
			return (0);
		}

		(void) v8plus_nverr(err, str);
		return (-1);
	}

	return (0);
}

static int
kstatjs_parsearg_string(const nvlist_t *arg, const char *str, char **p)
{
	nvpair_t *nvp;
	char *val;

	if (kstatjs_lookup_nvpair(arg, str, &nvp) != 0)
		return (-1);

	if (nvp == NULL)
		return (0);

	if (nvpair_type(nvp) != DATA_TYPE_STRING) {
		(void) v8plus_throw_exception("TypeError",
		    "illegal kstat specifier (expected string)",
		    V8PLUS_TYPE_STRING, "field", str,
		    V8PLUS_TYPE_NONE);

		return (-1);
	}

	(void) nvpair_value_string(nvp, &val);

	if ((*p = strdup(val)) == NULL)
		v8plus_panic("kstats_arg_string(): %s", strerror(errno));

	return (0);
}

static int
kstatjs_parsearg_int(const nvlist_t *arg, const char *str, int *p)
{
	nvpair_t *nvp;
	double val;

	if (kstatjs_lookup_nvpair(arg, str, &nvp) != 0)
		return (-1);

	if (nvp == NULL)
		return (0);

	if (nvpair_type(nvp) != DATA_TYPE_DOUBLE) {
		(void) v8plus_throw_exception("TypeError",
		    "illegal kstat specifier (expected number)",
		    V8PLUS_TYPE_STRING, "field", str,
		    V8PLUS_TYPE_NONE);

		return (-1);
	}

	(void) nvpair_value_double(nvp, &val);

	*p = (int)val;

	return (0);
}

static int
kstatjs_parsearg(const nvlist_t *args, char **modulep,
    char **classp, char **namep, int *instancep)
{
	nvpair_t *arg0, *arg1;
	nvlist_t *arg;

	*modulep = *classp = *namep = NULL;
	*instancep = -1;

	if (kstatjs_lookup_nvpair(args, "0", &arg0) != 0)
		return (-1);

	if (arg0 == NULL)
		return (0);

	if (nvpair_type(arg0) != DATA_TYPE_NVLIST) {
		(void) v8plus_throw_exception("TypeError",
		    "illegal kstat specifier (expected object)",
		    V8PLUS_TYPE_NONE);

		return (-1);
	}

	(void) nvpair_value_nvlist(arg0, &arg);

	if (nvlist_lookup_nvpair((nvlist_t *)args, "1", &arg1) == 0) {
		(void) v8plus_throw_exception("Error",
		    "illegal kstat specifier (spurious argument)",
		    V8PLUS_TYPE_NONE);
		return (-1);
	}

	if (kstatjs_parsearg_string(arg, "module", modulep) != 0 ||
	    kstatjs_parsearg_string(arg, "class", classp) != 0 ||
	    kstatjs_parsearg_string(arg, "name", namep) != 0) {
		return (-1);
	}

	if (kstatjs_parsearg_int(arg, "instance", instancep) != 0)
		return (-1);

	return (0);
}

static nvlist_t *
kstatjs_ctor(const nvlist_t *args, void **opp)
{
	kstatjs_t *ksj;

	if ((ksj = malloc(sizeof (kstatjs_t))) == NULL)
		v8plus_panic("malloc(): %s", strerror(errno));

	bzero(ksj, sizeof (kstatjs_t));
	ksj->ksj_kid = -1;

	*opp = NULL;

	if (kstatjs_parsearg(args, &ksj->ksj_module, &ksj->ksj_class,
	    &ksj->ksj_name, &ksj->ksj_instance) != 0) {
		kstatjs_dtor(ksj);
		return (NULL);
	}

	if ((ksj->ksj_ctl = kstat_open()) == NULL) {
		kstatjs_dtor(ksj);
		return (v8plus_syserr(errno, V8PLUS_TYPE_NONE));
	}

	*opp = ksj;

	return (v8plus_void());
}

#define	KSTATJS_READSTAT_IO_ADD(width, field) \
	if ((err = nvlist_add_uint##width(nvl, #field, io->field)) != 0) { \
		(void) v8plus_nverr(err, #field); \
		nvlist_free(nvl); \
		return (NULL); \
	}

static nvlist_t *
kstatjs_readstat_io(kstat_t *ksp)
{
	nvlist_t *nvl;
	int err;
	kstat_io_t *io = KSTAT_IO_PTR(ksp);

	KSTATJS_ASSERT(ksp->ks_type == KSTAT_TYPE_IO);

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		(void) v8plus_nverr(err, NULL);
		return (NULL);
	}

	KSTATJS_READSTAT_IO_ADD(64, nread);
	KSTATJS_READSTAT_IO_ADD(64, nwritten);
	KSTATJS_READSTAT_IO_ADD(32, reads);
	KSTATJS_READSTAT_IO_ADD(32, writes);
	KSTATJS_READSTAT_IO_ADD(64, wtime);
	KSTATJS_READSTAT_IO_ADD(64, wlentime);
	KSTATJS_READSTAT_IO_ADD(64, wlastupdate);
	KSTATJS_READSTAT_IO_ADD(64, rtime);
	KSTATJS_READSTAT_IO_ADD(64, rlentime);
	KSTATJS_READSTAT_IO_ADD(64, rlastupdate);
	KSTATJS_READSTAT_IO_ADD(64, wcnt);
	KSTATJS_READSTAT_IO_ADD(64, rcnt);

	return (nvl);
}

#undef	KSTATJS_READSTAT_IO_ADD

static nvlist_t *
kstatjs_readstat_named(kstat_t *ksp)
{
	nvlist_t *nvl;
	kstat_named_t *nm = KSTAT_NAMED_PTR(ksp);
	int err;
	unsigned i;

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		(void) v8plus_nverr(err, NULL);
		return (NULL);
	}

	for (i = 0; i < ksp->ks_ndata; i++, nm++) {
		char buf[256];

		switch (nm->data_type) {
		case KSTAT_DATA_CHAR:
			err = nvlist_add_uint8(nvl, nm->name, nm->value.c[0]);
			break;

		case KSTAT_DATA_INT32:
			err = nvlist_add_int32(nvl, nm->name, nm->value.i32);
			break;

		case KSTAT_DATA_UINT32:
			err = nvlist_add_uint32(nvl, nm->name, nm->value.ui32);
			break;

		case KSTAT_DATA_INT64:
			err = nvlist_add_int64(nvl, nm->name, nm->value.i64);
			break;

		case KSTAT_DATA_UINT64:
			err = nvlist_add_uint64(nvl, nm->name, nm->value.ui64);
			break;

		case KSTAT_DATA_STRING:
			err = nvlist_add_string(nvl, nm->name,
			    KSTAT_NAMED_STR_PTR(nm));
			break;

		default:
			(void) snprintf(buf, sizeof (buf), "unrecognized data "
			    "type %d for member \"%s\" in instance %d of stat "
			    "\"%s\" (module \"%s\", class \"%s\")\n",
			    nm->data_type, nm->name, ksp->ks_instance,
			    ksp->ks_name, ksp->ks_module, ksp->ks_class);

			(void) v8plus_throw_exception("Error",
			    buf, V8PLUS_TYPE_NONE);
			nvlist_free(nvl);

			return (NULL);
		}

		if (err != 0) {
			(void) v8plus_nverr(err, nm->name);
			nvlist_free(nvl);
			return (NULL);
		}
	}

	return (nvl);
}

static nvlist_t *
kstatjs_readstat(kstatjs_t *ksj, kstatjs_stat_t *ksjs)
{
	kstat_t *ksp = ksjs->ksjs_ksp;
	nvlist_t *nvl, *data;
	int err;

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		(void) v8plus_nverr(err, "nvlist_alloc");
		return (NULL);
	}

	if ((err = nvlist_add_string(nvl, "class", ksp->ks_class)) != 0 ||
	    (err = nvlist_add_string(nvl, "module", ksp->ks_module)) != 0 ||
	    (err = nvlist_add_string(nvl, "name", ksp->ks_name)) != 0 ||
	    (err = nvlist_add_int32(nvl, "instance", ksp->ks_instance))) {
		goto err;
	}

	if (kstat_read(ksj->ksj_ctl, ksp, NULL) == -1) {
		/*
		 * It is deeply annoying, but some kstats can return errors
		 * under otherwise routine conditions.  (ACPI is one
		 * offender; there are surely others.)  To prevent these
		 * fouled kstats from completely ruining our day, we assign
		 * an "error" member to the return value that consists of
		 * the strerror().
		 */
		if ((err = nvlist_add_string(nvl,
		    "error", strerror(errno))) != 0) {
			goto err;
		}

		return (nvl);
	}

	if ((err = nvlist_add_int32(nvl, "instance", ksp->ks_instance)) != 0 ||
	    (err = nvlist_add_uint64(nvl, "snaptime", ksp->ks_snaptime)) != 0 ||
	    (err = nvlist_add_uint64(nvl, "crtime", ksp->ks_crtime)) != 0) {
		goto err;
	}

	if (ksp->ks_type == KSTAT_TYPE_NAMED) {
		data = kstatjs_readstat_named(ksp);
	} else if (ksp->ks_type == KSTAT_TYPE_IO) {
		data = kstatjs_readstat_io(ksp);
	} else {
		return (nvl);
	}

	if (data == NULL) {
		nvlist_free(nvl);
		return (NULL);
	}

	if ((err = nvlist_add_nvlist(nvl, "data", data)) != 0) {
		nvlist_free(data);
		goto err;
	}

	nvlist_free(data);

	return (nvl);
err:
	(void) v8plus_nverr(err, NULL);
	nvlist_free(nvl);
	return (NULL);
}

static boolean_t
kstatjs_match(kstat_t *ksp, char *module, char *class,
    char *name, int instance)
{
	if (module != NULL && strcmp(ksp->ks_module, module) != 0)
		return (B_FALSE);

	if (class != NULL && strcmp(ksp->ks_class, class) != 0)
		return (B_FALSE);

	if (name != NULL && strcmp(ksp->ks_name, name) != 0)
		return (B_FALSE);

	if (instance != -1 && ksp->ks_instance != instance)
		return (B_FALSE);

	return (B_TRUE);
}

static int
kstatjs_update(kstatjs_t *ksj)
{
	kstat_ctl_t *ctl = ksj->ksj_ctl;
	kstatjs_stat_t *freelist, *ksjs, *tail = NULL;
	kstat_t *ksp;
	kid_t kid;

	if ((kid = kstat_chain_update(ctl)) == 0 && ksj->ksj_kid != -1)
		return (0);

	if (kid == -1) {
		(void) v8plus_syserr(errno, "kstat_chain_update()");
		return (-1);
	}

	ksj->ksj_kid = kid;

	/*
	 * After a chain update, we need to update all of our kstat headers.
	 */
	freelist = ksj->ksj_stats;
	ksj->ksj_stats = NULL;

	for (ksp = ctl->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
		if (!kstatjs_match(ksp, ksj->ksj_module,
		    ksj->ksj_class, ksj->ksj_name, ksj->ksj_instance)) {
			continue;
		}

		if (freelist == NULL) {
			if ((ksjs = malloc(sizeof (kstatjs_stat_t))) == NULL) {
				/*
				 * To increase the odds of actually surviving
				 * memory allocation failure enough to be able
				 * throw an exception, we'll free all of our
				 * kstats and set our kid to -1 to force any
				 * subsequent read to re-process the kstat
				 * header chain.
				 */
				ksjs = ksj->ksj_stats;

				while ((ksjs = ksj->ksj_stats) != NULL) {
					ksj->ksj_stats = ksjs->ksjs_next;
					free(ksjs);
				}

				(void) v8plus_syserr(errno, "malloc()");
				return (-1);
			}
		} else {
			ksjs = freelist;
			freelist = freelist->ksjs_next;
		}

		ksjs->ksjs_ksp = ksp;
		ksjs->ksjs_next = NULL;

		if (tail != NULL) {
			tail->ksjs_next = ksjs;
		} else {
			ksj->ksj_stats = ksjs;
		}

		tail = ksjs;
	}

	/*
	 * If there's anything left on the freelist, we need to free it.
	 */
	while ((ksjs = freelist) != NULL) {
		freelist = ksjs->ksjs_next;
		free(ksjs);
	}

	return (0);
}

static nvlist_t *
kstatjs_read(void *op, const nvlist_t *args)
{
	kstatjs_t *ksj = op;
	kstatjs_stat_t *ksjs;
	nvlist_t *nvl, *data, *rval = NULL;
	int nstats = 0, err;
	char *module = NULL, *class = NULL, *name = NULL;
	int instance = -1;

	if (kstatjs_parsearg(args, &module, &class, &name, &instance) != 0)
		return (NULL);

	if (ksj->ksj_ctl == NULL) {
		rval = v8plus_throw_exception("Error",
		    "kstat reader has already been closed", V8PLUS_TYPE_NONE);
		goto out;
	}

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0) {
		(void) v8plus_nverr(err, "nvlist_alloc");
		goto out;
	}

	/*
	 * One indicates to v8plus that an nvlist is actually of an array type
	 * by setting a magic property.  No, it's not documented -- and yes, a
	 * better interface is sorely needed!  (Indeed, it would be difficult
	 * to have a worse one.)
	 */
	if ((err = nvlist_add_string(nvl, ".__v8plus_type", "Array")) != 0) {
		(void) v8plus_nverr(err, NULL);
		nvlist_free(nvl);
		goto out;
	}

	if (kstatjs_update(ksj) != 0) {
		nvlist_free(nvl);
		goto out;
	}

	for (ksjs = ksj->ksj_stats; ksjs != NULL; ksjs = ksjs->ksjs_next) {
		char buf[12];

		if (!kstatjs_match(ksjs->ksjs_ksp, module,
		    class, name, instance)) {
			continue;
		}

		if ((data = kstatjs_readstat(ksj, ksjs)) == NULL) {
			nvlist_free(nvl);
			goto out;
		}

		(void) snprintf(buf, sizeof (buf), "%d", nstats++);
		KSTATJS_ASSERT(nstats > 0);

		if (nvlist_add_nvlist(nvl, buf, data) != 0) {
			(void) v8plus_nverr(err, NULL);
			nvlist_free(nvl);
			goto out;
		}

		nvlist_free(data);
	}

	rval = v8plus_obj(V8PLUS_TYPE_OBJECT, "res", nvl, V8PLUS_TYPE_NONE);
	nvlist_free(nvl);

out:
	free(module);
	free(class);
	free(name);

	return (rval);
}

static nvlist_t *
kstatjs_close(void *op, const nvlist_t *args __UNUSED)
{
	kstatjs_t *ksj = op;

	if (ksj->ksj_ctl == NULL) {
		return (v8plus_throw_exception("Error",
		    "kstat reader has already been closed", V8PLUS_TYPE_NONE));
	}

	kstatjs_finalize(ksj);

	return (v8plus_void());
}

static v8plus_module_defn_t vmd = {
	.vmd_version = V8PLUS_MODULE_VERSION,
	.vmd_js_class_name = "Reader",
	.vmd_js_factory_name = "Reader",
	.vmd_methods = (v8plus_method_descr_t[]) {
		{
			.md_name = "read",
			.md_c_func = kstatjs_read
		}, {
			.md_name = "close",
			.md_c_func = kstatjs_close
		}
	},
	.vmd_method_count = 2,
	.vmd_ctor = kstatjs_ctor,
	.vmd_dtor = kstatjs_dtor
};

__attribute__((constructor))
static void
_init(void)
{
	v8plus_module_register(&vmd);
}
