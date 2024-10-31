/*
 * Copyright 2023 MNX Cloud, Inc.
 */

/*
 * This file contains all of the C++ support code required to interface node
 * with kstat.c.
 */

#include "addon.h"

#include <iostream>
#include <napi.h>

class KStat : public Napi::ObjectWrap<KStat> {
    public:
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	KStat(const Napi::CallbackInfo& info);

    private:
	Napi::Value Read(const Napi::CallbackInfo& info);
	Napi::Value Close(const Napi::CallbackInfo& info);
	nvlist_t * parse_options(const Napi::CallbackInfo& info);
	Napi::Value nvpair_to_value(Napi::Env, nvpair_t *);
	Napi::Object create_and_populate(Napi::Env, nvlist_t *);

	kstatjs_t *ksj;
};

Napi::Object KStat::Init(Napi::Env env, Napi::Object exports) {
	Napi::Function func = DefineClass(env, "KStat", {
		InstanceMethod("read", &KStat::Read),
		InstanceMethod("close", &KStat::Close)
	});

	exports.Set("Reader", func);

	return (exports);
}

/*
 * Parse an optional Object passed as the only argument to Reader() or read(),
 * and return a populated nvlist_t with valid entries added.
 *
 * Any invalid arguments throw a TypeError and return NULL.
 */
nvlist_t *
KStat::parse_options(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();
	Napi::Object args;
	nvlist_t *nvl;

	/*
	 * The only valid options are none, or a single object.
	 */
	if (info.Length() > 1) {
		Napi::Error::New(env,
		    "illegal kstat specifier (spurious argument)")
		    .ThrowAsJavaScriptException();
		return (NULL);
	}

	if (info.Length() == 1 && !info[0].IsObject()) {
		Napi::Error::New(env,
		    "illegal kstat specifier (expected object)")
		    .ThrowAsJavaScriptException();
		return (NULL);
	}

	nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0);

	/*
	 * If no object was provided just return the empty nvlist.
	 */
	if (!info[0].IsObject())
		return (nvl);

	args = info[0].ToObject();

	if (args.Has("class")) {
		if (!(args.Get("class").IsString())) {
			Napi::TypeError::New(env,
			    "illegal kstat specifier (expected string)")
			    .ThrowAsJavaScriptException();
			nvlist_free(nvl);
			return (NULL);
		}
		nvlist_add_string(nvl, "class",
		    args.Get("class").ToString().Utf8Value().c_str());
	}

	if (args.Has("name")) {
		if (!(args.Get("name").IsString())) {
			Napi::TypeError::New(env,
			    "illegal kstat specifier (expected string)")
			    .ThrowAsJavaScriptException();
			nvlist_free(nvl);
			return (NULL);
		}
		nvlist_add_string(nvl, "name",
		    args.Get("name").ToString().Utf8Value().c_str());
	}

	if (args.Has("module")) {
		if (!(args.Get("module").IsString())) {
			Napi::TypeError::New(env,
			    "illegal kstat specifier (expected string)")
			    .ThrowAsJavaScriptException();
			nvlist_free(nvl);
			return (NULL);
		}
		nvlist_add_string(nvl, "module",
		    args.Get("module").ToString().Utf8Value().c_str());
	}

	if (args.Has("instance")) {
		if (!(args.Get("instance").IsNumber())) {
			Napi::TypeError::New(env,
			    "illegal kstat specifier (expected double)")
			    .ThrowAsJavaScriptException();
			nvlist_free(nvl);
			return (NULL);
		}
		nvlist_add_double(nvl, "instance",
		    args.Get("instance").ToNumber());
	}

	return (nvl);

}

/*
 * Initialise our Reader class, parsing an optional object that provides a
 * specific class/name/module/instance to look at.
 */
KStat::KStat(const Napi::CallbackInfo& info) : Napi::ObjectWrap<KStat>(info) {
	Napi::Env env = info.Env();
	nvlist_t *nvl = NULL;

	if ((nvl = KStat::parse_options(info)) == NULL)
		return;

	if ((this->ksj = kstatjs_init(nvl)) == NULL) {
		Napi::Error::New(env, kstatjs_errmsg())
		    .ThrowAsJavaScriptException();
		return;
	}
}

/*
 * Based on similar functions in v8plus.
 */
/*
 * Given an nvlist, create an Object and populate it with each nvpair.  Note
 * that this can end up being called recursively if the nvlist contains another
 * nvlist, resulting in an Object containing an Object.
 */
Napi::Object
KStat::create_and_populate(Napi::Env env, nvlist_t *nvl)
{
	nvpair_t *pp = NULL;
	Napi::Object obj = Napi::Object::New(env);

	while ((pp = nvlist_next_nvpair(nvl, pp)) != NULL) {
		obj.Set(nvpair_name(pp), KStat::nvpair_to_value(env, pp));
	}

	return (obj);
}

#define	RETURN_JS(_env, _p, _jt, _ct, _xt, _pt) \
	do { \
		_ct _v; \
		(void) nvpair_value_##_pt(const_cast<nvpair_t *>(_p), &_v); \
		return (Napi::_jt::New(_env, (_xt)_v)); \
	} while (0)

/*
 * Convert an nvpair value to a Napi Value.
 */
Napi::Value
KStat::nvpair_to_value(Napi::Env env, nvpair_t *pp)
{
	nvlist_t *lp;

	switch (nvpair_type(pp)) {
	case DATA_TYPE_BOOLEAN:
		return (env.Undefined());
	case DATA_TYPE_BOOLEAN_VALUE:
		RETURN_JS(env, pp, Boolean, boolean_t, bool, boolean_value);
	case DATA_TYPE_BYTE:
		return (env.Null());
	case DATA_TYPE_INT8:
		RETURN_JS(env, pp, Number, int8_t, double, int8);
	case DATA_TYPE_UINT8:
		RETURN_JS(env, pp, Number, uint8_t, double, uint8);
	case DATA_TYPE_INT16:
		RETURN_JS(env, pp, Number, int16_t, double, int16);
	case DATA_TYPE_UINT16:
		RETURN_JS(env, pp, Number, uint16_t, double, uint16);
	case DATA_TYPE_INT32:
		RETURN_JS(env, pp, Number, int32_t, double, int32);
	case DATA_TYPE_UINT32:
		RETURN_JS(env, pp, Number, uint32_t, double, uint32);
	case DATA_TYPE_INT64:
		RETURN_JS(env, pp, Number, int64_t, double, int64);
	case DATA_TYPE_UINT64:
		RETURN_JS(env, pp, Number, uint64_t, double, uint64);
	case DATA_TYPE_DOUBLE:
		RETURN_JS(env, pp, Number, double, double, double);
	case DATA_TYPE_STRING:
		RETURN_JS(env, pp, String, char *, const char *, string);
	case DATA_TYPE_NVLIST:
		nvpair_value_nvlist(pp, &lp);
		return (KStat::create_and_populate(env, lp));
	default: {
		char errmsg[256];

		(void) snprintf(errmsg, sizeof (errmsg),
		    "invalid nvpair data type %d", nvpair_type(pp));
		Napi::Error::New(env, errmsg).ThrowAsJavaScriptException();
	}
	}

	/*NOTREACHED*/
	return (env.Undefined());
}

#undef RETURN_JS

Napi::Value
KStat::Read(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();
	Napi::Array arr;
	nvlist_t *ret, *nvl = NULL;
	nvpair_t *pp = NULL;
	int i = 0, len = 0;

	if ((nvl = KStat::parse_options(info)) == NULL)
		return (env.Undefined());

	if ((ret = kstatjs_read(this->ksj, nvl)) == NULL) {
		Napi::Error::New(env, kstatjs_errmsg())
		    .ThrowAsJavaScriptException();
		return (env.Undefined());
	}

	while ((pp = nvlist_next_nvpair(ret, pp)) != NULL)
		len++;

	arr = Napi::Array::New(env, len);

	while ((pp = nvlist_next_nvpair(ret, pp)) != NULL) {
		arr.Set(i++, KStat::nvpair_to_value(env, pp));
	}

	kstatjs_free(ret);
	return (arr);
}

Napi::Value
KStat::Close(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	if ((kstatjs_close(this->ksj)) != 0) {
		Napi::Error::New(env, kstatjs_errmsg())
		    .ThrowAsJavaScriptException();
		return (env.Undefined());
	}

	return (env.Undefined());
}

Napi::Object
Init(Napi::Env env, Napi::Object exports)
{
	KStat::Init(env, exports);
	return (exports);
}

NODE_API_MODULE(kstat, Init)
