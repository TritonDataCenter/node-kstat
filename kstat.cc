#include <v8.h>
#include <node.h>
#include <string.h>
#include <unistd.h>
#include <nfs/nfs_clnt.h>
#include <node_object_wrap.h>
#include <kstat.h>
#include <errno.h>
#include <string>
#include <vector>
#include <sys/dnlc.h>
#include <sys/varargs.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/var.h>

using namespace v8;
using std::string;
using std::vector;

class KStatReader : public node::ObjectWrap {
public:
	static void Initialize(Handle<Object> exports);

protected:
	static Persistent<FunctionTemplate> templ;

	KStatReader(string *module, string *classname,
	    string *name, int instance);
	void close();
	Handle<Value> error(Isolate *isolate, const char *fmt, ...);
	Handle<Value> read(Isolate *, kstat_t *);
	bool matches(kstat_t *, string *, string *, string *, int64_t);
	int update();
	~KStatReader();

	static void Close(const FunctionCallbackInfo<Value>& args);
	static void New(const FunctionCallbackInfo<Value>& args);
	static void Read(const FunctionCallbackInfo<Value>& args);


private:
	static string *stringMember(Isolate *, Local<Value>, char *, char *);
	static int64_t intMember(Isolate *, Local<Value>, char *, int64_t);
	Handle<Object> data_raw_cpu_stat(Isolate *, kstat_t *);
	Handle<Object> data_raw_var(Isolate *, kstat_t *);
	Handle<Object> data_raw_ncstats(Isolate *, kstat_t *);
	Handle<Object> data_raw_sysinfo(Isolate *, kstat_t *);
	Handle<Object> data_raw_vminfo(Isolate *, kstat_t *);
	Handle<Object> data_raw_mntinfo(Isolate *, kstat_t *);
	Handle<Object> data_raw(Isolate *, kstat_t *);
	Handle<Object> data_named(Isolate *, kstat_t *);
	Handle<Object> data_intr(Isolate *, kstat_t *);
	Handle<Object> data_io(Isolate *, kstat_t *);
	Handle<Object> data_timer(Isolate *, kstat_t *);

	string *ksr_module;
	string *ksr_class;
	string *ksr_name;
	int ksr_instance;
	kid_t ksr_kid;
	kstat_ctl_t *ksr_ctl;
	vector<kstat_t *> ksr_kstats;
};

Persistent<FunctionTemplate> KStatReader::templ;

KStatReader::KStatReader(string *module, string *classname,
    string *name, int instance)
    : node::ObjectWrap(), ksr_module(module), ksr_class(classname),
    ksr_name(name), ksr_instance(instance), ksr_kid(-1)
{
	if ((ksr_ctl = kstat_open()) == NULL)
		throw "could not open kstat";
};

KStatReader::~KStatReader()
{
	delete ksr_module;
	delete ksr_class;
	delete ksr_name;

	if (ksr_ctl != NULL)
		this->close();
}

void
KStatReader::close()
{
	kstat_close(ksr_ctl);
	ksr_ctl = NULL;
}

bool
KStatReader::matches(kstat_t *ksp, string* fmodule, string* fclass,
    string* fname, int64_t finstance)
{
	if (!fmodule->empty() && fmodule->compare(ksp->ks_module) != 0)
		return (false);

	if (!fclass->empty() && fclass->compare(ksp->ks_class) != 0)
		return (false);

	if (!fname->empty() && fname->compare(ksp->ks_name) != 0)
		return (false);

	return (finstance == -1 || ksp->ks_instance == finstance);
}

int
KStatReader::update()
{
	kstat_t *ksp;
	kid_t kid;

	if ((kid = kstat_chain_update(ksr_ctl)) == 0 && ksr_kid != -1)
		return (0);

	if (kid == -1)
		return (-1);

	ksr_kid = kid;
	ksr_kstats.clear();

	for (ksp = ksr_ctl->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
		if (!this->matches(ksp,
		    ksr_module, ksr_class, ksr_name, ksr_instance))
			continue;

		ksr_kstats.push_back(ksp);
	}

	return (0);
}

void
KStatReader::Initialize(Handle<Object> exports)
{
	v8::Isolate* isolate;
  isolate = exports->GetIsolate();

	Local<FunctionTemplate> localTempl = FunctionTemplate::New(isolate, KStatReader::New);
	localTempl->InstanceTemplate()->SetInternalFieldCount(1);
	localTempl->SetClassName(String::NewFromUtf8(isolate, "Reader", String::kInternalizedString));

	NODE_SET_PROTOTYPE_METHOD(localTempl, "read", KStatReader::Read);
	NODE_SET_PROTOTYPE_METHOD(localTempl, "close", KStatReader::Close);

	templ.Reset(isolate, localTempl);

	exports->Set(String::NewFromUtf8(isolate, "Reader", String::kInternalizedString), localTempl->GetFunction());
}

string *
KStatReader::stringMember(Isolate *isolate, Local<Value> value, char *member, char *deflt)
{
	if (!value->IsObject())
		return (new string(deflt));

	Local<Object> o = Local<Object>::Cast(value);
	Local<Value> v = o->Get(String::NewFromUtf8(isolate, member));

	if (!v->IsString())
		return (new string(deflt));

	String::Utf8Value val(v);
	return (new string(*val));
}

int64_t
KStatReader::intMember(Isolate *isolate, Local<Value> value, char *member, int64_t deflt)
{
	int64_t rval = deflt;

	if (!value->IsObject())
		return (rval);

	Local<Object> o = Local<Object>::Cast(value);
	value = o->Get(String::NewFromUtf8(isolate, member));

	if (!value->IsNumber())
		return (rval);

	Local<Integer> i = Local<Integer>::Cast(value);

	return (i->Value());
}

void
KStatReader::New(const FunctionCallbackInfo<Value>& args)
{
	Isolate *isolate = args.GetIsolate();
	KStatReader *k = new KStatReader(stringMember(isolate, args[0], "module", ""),
	    stringMember(isolate, args[0], "class", ""),
	    stringMember(isolate, args[0], "name", ""),
	    intMember(isolate, args[0], "instance", -1));

	k->Wrap(args.Holder());

	args.GetReturnValue().Set (args.This());
}

Handle<Value>
KStatReader::error(Isolate *isolate, const char *fmt, ...)
{
	char buf[1024], buf2[1024];
	char *err = buf;
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);

	if (buf[strlen(buf) - 1] != '\n') {
		/*
		 * If our error doesn't end in a new-line, we'll append the
		 * strerror of errno.
		 */
		(void) snprintf(err = buf2, sizeof (buf2),
		    "%s: %s", buf, strerror(errno));
	} else {
		buf[strlen(buf) - 1] = '\0';
	}

	return (isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, err))));
}

Handle<Object>
KStatReader::data_raw_cpu_stat(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (cpu_stat_t));

	cpu_stat_t	  *stat;
	cpu_sysinfo_t	*sysinfo;
	cpu_syswait_t	*syswait;
	cpu_vminfo_t	*vminfo;

	stat    = (cpu_stat_t *)(ksp->ks_data);
	sysinfo = &stat->cpu_sysinfo;
	syswait = &stat->cpu_syswait;
	vminfo  = &stat->cpu_vminfo;

	data->Set(String::NewFromUtf8(isolate, "idle"), Number::New(isolate, sysinfo->cpu[CPU_IDLE]));
	data->Set(String::NewFromUtf8(isolate, "user"), Number::New(isolate, sysinfo->cpu[CPU_USER]));
	data->Set(String::NewFromUtf8(isolate, "kernel"), Number::New(isolate, sysinfo->cpu[CPU_KERNEL]));
	data->Set(String::NewFromUtf8(isolate, "wait"), Number::New(isolate, sysinfo->cpu[CPU_WAIT]));
	data->Set(String::NewFromUtf8(isolate, "wait_io"), Number::New(isolate, sysinfo->wait[W_IO]));
	data->Set(String::NewFromUtf8(isolate, "wait_swap"), Number::New(isolate, sysinfo->wait[W_SWAP]));
	data->Set(String::NewFromUtf8(isolate, "wait_pio"), Number::New(isolate, sysinfo->wait[W_PIO]));
	data->Set(String::NewFromUtf8(isolate, "bread"), Number::New(isolate, sysinfo->bread));
	data->Set(String::NewFromUtf8(isolate, "bwrite"), Number::New(isolate, sysinfo->bwrite));
	data->Set(String::NewFromUtf8(isolate, "lread"), Number::New(isolate, sysinfo->lread));
	data->Set(String::NewFromUtf8(isolate, "lwrite"), Number::New(isolate, sysinfo->lwrite));
	data->Set(String::NewFromUtf8(isolate, "phread"), Number::New(isolate, sysinfo->phread));
	data->Set(String::NewFromUtf8(isolate, "phwrite"), Number::New(isolate, sysinfo->phwrite));
	data->Set(String::NewFromUtf8(isolate, "pswitch"), Number::New(isolate, sysinfo->pswitch));
	data->Set(String::NewFromUtf8(isolate, "trap"), Number::New(isolate, sysinfo->trap));
	data->Set(String::NewFromUtf8(isolate, "intr"), Number::New(isolate, sysinfo->intr));
	data->Set(String::NewFromUtf8(isolate, "syscall"), Number::New(isolate, sysinfo->syscall));
	data->Set(String::NewFromUtf8(isolate, "sysread"), Number::New(isolate, sysinfo->sysread));
	data->Set(String::NewFromUtf8(isolate, "syswrite"), Number::New(isolate, sysinfo->syswrite));
	data->Set(String::NewFromUtf8(isolate, "sysfork"), Number::New(isolate, sysinfo->sysfork));
	data->Set(String::NewFromUtf8(isolate, "sysvfork"), Number::New(isolate, sysinfo->sysvfork));
	data->Set(String::NewFromUtf8(isolate, "sysexec"), Number::New(isolate, sysinfo->sysexec));
	data->Set(String::NewFromUtf8(isolate, "readch"), Number::New(isolate, sysinfo->readch));
	data->Set(String::NewFromUtf8(isolate, "writech"), Number::New(isolate, sysinfo->writech));
	data->Set(String::NewFromUtf8(isolate, "rcvint"), Number::New(isolate, sysinfo->rcvint));
	data->Set(String::NewFromUtf8(isolate, "xmtint"), Number::New(isolate, sysinfo->xmtint));
	data->Set(String::NewFromUtf8(isolate, "mdmint"), Number::New(isolate, sysinfo->mdmint));
	data->Set(String::NewFromUtf8(isolate, "rawch"), Number::New(isolate, sysinfo->rawch));
	data->Set(String::NewFromUtf8(isolate, "canch"), Number::New(isolate, sysinfo->canch));
	data->Set(String::NewFromUtf8(isolate, "outch"), Number::New(isolate, sysinfo->outch));
	data->Set(String::NewFromUtf8(isolate, "msg"), Number::New(isolate, sysinfo->msg));
	data->Set(String::NewFromUtf8(isolate, "sema"), Number::New(isolate, sysinfo->sema));
	data->Set(String::NewFromUtf8(isolate, "namei"), Number::New(isolate, sysinfo->namei));
	data->Set(String::NewFromUtf8(isolate, "ufsiget"), Number::New(isolate, sysinfo->ufsiget));
	data->Set(String::NewFromUtf8(isolate, "ufsdirblk"), Number::New(isolate, sysinfo->ufsdirblk));
	data->Set(String::NewFromUtf8(isolate, "ufsipage"), Number::New(isolate, sysinfo->ufsipage));
	data->Set(String::NewFromUtf8(isolate, "ufsinopage"), Number::New(isolate, sysinfo->ufsinopage));
	data->Set(String::NewFromUtf8(isolate, "inodeovf"), Number::New(isolate, sysinfo->inodeovf));
	data->Set(String::NewFromUtf8(isolate, "fileovf"), Number::New(isolate, sysinfo->fileovf));
	data->Set(String::NewFromUtf8(isolate, "procovf"), Number::New(isolate, sysinfo->procovf));
	data->Set(String::NewFromUtf8(isolate, "intrthread"), Number::New(isolate, sysinfo->intrthread));
	data->Set(String::NewFromUtf8(isolate, "intrblk"), Number::New(isolate, sysinfo->intrblk));
	data->Set(String::NewFromUtf8(isolate, "idlethread"), Number::New(isolate, sysinfo->idlethread));
	data->Set(String::NewFromUtf8(isolate, "inv_swtch"), Number::New(isolate, sysinfo->inv_swtch));
	data->Set(String::NewFromUtf8(isolate, "nthreads"), Number::New(isolate, sysinfo->nthreads));
	data->Set(String::NewFromUtf8(isolate, "cpumigrate"), Number::New(isolate, sysinfo->cpumigrate));
	data->Set(String::NewFromUtf8(isolate, "xcalls"), Number::New(isolate, sysinfo->xcalls));
	data->Set(String::NewFromUtf8(isolate, "mutex_adenters"),
	    Number::New(isolate, sysinfo->mutex_adenters));
	data->Set(String::NewFromUtf8(isolate, "rw_rdfails"), Number::New(isolate, sysinfo->rw_rdfails));
	data->Set(String::NewFromUtf8(isolate, "rw_wrfails"), Number::New(isolate, sysinfo->rw_wrfails));
	data->Set(String::NewFromUtf8(isolate, "modload"), Number::New(isolate, sysinfo->modload));
	data->Set(String::NewFromUtf8(isolate, "modunload"), Number::New(isolate, sysinfo->modunload));
	data->Set(String::NewFromUtf8(isolate, "bawrite"), Number::New(isolate, sysinfo->bawrite));
#ifdef	STATISTICS	/* see header file */
	data->Set(String::NewFromUtf8(isolate, "rw_enters"), Number::New(isolate, sysinfo->rw_enters));
	data->Set(String::NewFromUtf8(isolate, "win_uo_cnt"), Number::New(isolate, sysinfo->win_uo_cnt));
	data->Set(String::NewFromUtf8(isolate, "win_uu_cnt"), Number::New(isolate, sysinfo->win_uu_cnt));
	data->Set(String::NewFromUtf8(isolate, "win_so_cnt"), Number::New(isolate, sysinfo->win_so_cnt));
	data->Set(String::NewFromUtf8(isolate, "win_su_cnt"), Number::New(isolate, sysinfo->win_su_cnt));
	data->Set(String::NewFromUtf8(isolate, "win_suo_cnt"),
	    Number::New(isolate, sysinfo->win_suo_cnt));
#endif

	data->Set(String::NewFromUtf8(isolate, "iowait"), Number::New(isolate, syswait->iowait));
	data->Set(String::NewFromUtf8(isolate, "swap"), Number::New(isolate, syswait->swap));
	data->Set(String::NewFromUtf8(isolate, "physio"), Number::New(isolate, syswait->physio));

	data->Set(String::NewFromUtf8(isolate, "pgrec"), Number::New(isolate, vminfo->pgrec));
	data->Set(String::NewFromUtf8(isolate, "pgfrec"), Number::New(isolate, vminfo->pgfrec));
	data->Set(String::NewFromUtf8(isolate, "pgin"), Number::New(isolate, vminfo->pgin));
	data->Set(String::NewFromUtf8(isolate, "pgpgin"), Number::New(isolate, vminfo->pgpgin));
	data->Set(String::NewFromUtf8(isolate, "pgout"), Number::New(isolate, vminfo->pgout));
	data->Set(String::NewFromUtf8(isolate, "pgpgout"), Number::New(isolate, vminfo->pgpgout));
	data->Set(String::NewFromUtf8(isolate, "swapin"), Number::New(isolate, vminfo->swapin));
	data->Set(String::NewFromUtf8(isolate, "pgswapin"), Number::New(isolate, vminfo->pgswapin));
	data->Set(String::NewFromUtf8(isolate, "swapout"), Number::New(isolate, vminfo->swapout));
	data->Set(String::NewFromUtf8(isolate, "pgswapout"), Number::New(isolate, vminfo->pgswapout));
	data->Set(String::NewFromUtf8(isolate, "zfod"), Number::New(isolate, vminfo->zfod));
	data->Set(String::NewFromUtf8(isolate, "dfree"), Number::New(isolate, vminfo->dfree));
	data->Set(String::NewFromUtf8(isolate, "scan"), Number::New(isolate, vminfo->scan));
	data->Set(String::NewFromUtf8(isolate, "rev"), Number::New(isolate, vminfo->rev));
	data->Set(String::NewFromUtf8(isolate, "hat_fault"), Number::New(isolate, vminfo->hat_fault));
	data->Set(String::NewFromUtf8(isolate, "as_fault"), Number::New(isolate, vminfo->as_fault));
	data->Set(String::NewFromUtf8(isolate, "maj_fault"), Number::New(isolate, vminfo->maj_fault));
	data->Set(String::NewFromUtf8(isolate, "cow_fault"), Number::New(isolate, vminfo->cow_fault));
	data->Set(String::NewFromUtf8(isolate, "prot_fault"), Number::New(isolate, vminfo->prot_fault));
	data->Set(String::NewFromUtf8(isolate, "softlock"), Number::New(isolate, vminfo->softlock));
	data->Set(String::NewFromUtf8(isolate, "kernel_asflt"),
	    Number::New(isolate, vminfo->kernel_asflt));
	data->Set(String::NewFromUtf8(isolate, "pgrrun"), Number::New(isolate, vminfo->pgrrun));
	data->Set(String::NewFromUtf8(isolate, "execpgin"), Number::New(isolate, vminfo->execpgin));
	data->Set(String::NewFromUtf8(isolate, "execpgout"), Number::New(isolate, vminfo->execpgout));
	data->Set(String::NewFromUtf8(isolate, "execfree"), Number::New(isolate, vminfo->execfree));
	data->Set(String::NewFromUtf8(isolate, "anonpgin"), Number::New(isolate, vminfo->anonpgin));
	data->Set(String::NewFromUtf8(isolate, "anonpgout"), Number::New(isolate, vminfo->anonpgout));
	data->Set(String::NewFromUtf8(isolate, "anonfree"), Number::New(isolate, vminfo->anonfree));
	data->Set(String::NewFromUtf8(isolate, "fspgin"), Number::New(isolate, vminfo->fspgin));
	data->Set(String::NewFromUtf8(isolate, "fspgout"), Number::New(isolate, vminfo->fspgout));
	data->Set(String::NewFromUtf8(isolate, "fsfree"), Number::New(isolate, vminfo->fsfree));

	return (data);
}

Handle<Object>
KStatReader::data_raw_var(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (struct var));

	struct var	*var = (struct var *)(ksp->ks_data);

	data->Set(String::NewFromUtf8(isolate, "v_buf"), Number::New(isolate, var->v_buf));
	data->Set(String::NewFromUtf8(isolate, "v_call"), Number::New(isolate, var->v_call));
	data->Set(String::NewFromUtf8(isolate, "v_proc"), Number::New(isolate, var->v_proc));
	data->Set(String::NewFromUtf8(isolate, "v_maxupttl"), Number::New(isolate, var->v_maxupttl));
	data->Set(String::NewFromUtf8(isolate, "v_nglobpris"), Number::New(isolate, var->v_nglobpris));
	data->Set(String::NewFromUtf8(isolate, "v_maxsyspri"), Number::New(isolate, var->v_maxsyspri));
	data->Set(String::NewFromUtf8(isolate, "v_clist"), Number::New(isolate, var->v_clist));
	data->Set(String::NewFromUtf8(isolate, "v_maxup"), Number::New(isolate, var->v_maxup));
	data->Set(String::NewFromUtf8(isolate, "v_hbuf"), Number::New(isolate, var->v_hbuf));
	data->Set(String::NewFromUtf8(isolate, "v_hmask"), Number::New(isolate, var->v_hmask));
	data->Set(String::NewFromUtf8(isolate, "v_pbuf"), Number::New(isolate, var->v_pbuf));
	data->Set(String::NewFromUtf8(isolate, "v_sptmap"), Number::New(isolate, var->v_sptmap));
	data->Set(String::NewFromUtf8(isolate, "v_maxpmem"), Number::New(isolate, var->v_maxpmem));
	data->Set(String::NewFromUtf8(isolate, "v_autoup"), Number::New(isolate, var->v_autoup));
	data->Set(String::NewFromUtf8(isolate, "v_bufhwm"), Number::New(isolate, var->v_bufhwm));

	return (data);
}

Handle<Object>
KStatReader::data_raw_ncstats(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (struct ncstats));

	struct ncstats	*ncstats = (struct ncstats *)(ksp->ks_data);

	data->Set(String::NewFromUtf8(isolate, "hits"), Number::New(isolate, ncstats->hits));
	data->Set(String::NewFromUtf8(isolate, "misses"), Number::New(isolate, ncstats->misses));
	data->Set(String::NewFromUtf8(isolate, "enters"), Number::New(isolate, ncstats->enters));
	data->Set(String::NewFromUtf8(isolate, "dbl_enters"), Number::New(isolate, ncstats->dbl_enters));
	data->Set(String::NewFromUtf8(isolate, "long_enter"), Number::New(isolate, ncstats->long_enter));
	data->Set(String::NewFromUtf8(isolate, "long_look"), Number::New(isolate, ncstats->long_look));
	data->Set(String::NewFromUtf8(isolate, "move_to_front"),
	    Number::New(isolate, ncstats->move_to_front));
	data->Set(String::NewFromUtf8(isolate, "purges"), Number::New(isolate, ncstats->purges));

	return (data);
}

Handle<Object>
KStatReader::data_raw_sysinfo(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (sysinfo_t));

	sysinfo_t	*sysinfo = (sysinfo_t *)(ksp->ks_data);

	data->Set(String::NewFromUtf8(isolate, "updates"), Number::New(isolate, sysinfo->updates));
	data->Set(String::NewFromUtf8(isolate, "runque"), Number::New(isolate, sysinfo->runque));
	data->Set(String::NewFromUtf8(isolate, "runocc"), Number::New(isolate, sysinfo->runocc));
	data->Set(String::NewFromUtf8(isolate, "swpque"), Number::New(isolate, sysinfo->swpque));
	data->Set(String::NewFromUtf8(isolate, "swpocc"), Number::New(isolate, sysinfo->swpocc));
	data->Set(String::NewFromUtf8(isolate, "waiting"), Number::New(isolate, sysinfo->waiting));

	return (data);
}

Handle<Object>
KStatReader::data_raw_vminfo(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (vminfo_t));

	vminfo_t	*vminfo = (vminfo_t *)(ksp->ks_data);

	data->Set(String::NewFromUtf8(isolate, "freemem"), Number::New(isolate, vminfo->freemem));
	data->Set(String::NewFromUtf8(isolate, "swap_resv"), Number::New(isolate, vminfo->swap_resv));
	data->Set(String::NewFromUtf8(isolate, "swap_alloc"), Number::New(isolate, vminfo->swap_alloc));
	data->Set(String::NewFromUtf8(isolate, "swap_avail"), Number::New(isolate, vminfo->swap_avail));
	data->Set(String::NewFromUtf8(isolate, "swap_free"), Number::New(isolate, vminfo->swap_free));
	data->Set(String::NewFromUtf8(isolate, "updates"), Number::New(isolate, vminfo->updates));

	return (data);
}

Handle<Object>
KStatReader::data_raw_mntinfo(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);

	assert(ksp->ks_data_size == sizeof (struct mntinfo_kstat));

	struct mntinfo_kstat *mntinfo = (struct mntinfo_kstat *)(ksp->ks_data);

	data->Set(String::NewFromUtf8(isolate, "mntinfo"),
	    String::NewFromUtf8(isolate, mntinfo->mik_proto));
	data->Set(String::NewFromUtf8(isolate, "mik_vers"),
	    Number::New(isolate, mntinfo->mik_vers));
	data->Set(String::NewFromUtf8(isolate, "mik_flags"),
	    Number::New(isolate, mntinfo->mik_flags));
	data->Set(String::NewFromUtf8(isolate, "mik_secmod"),
	    Number::New(isolate, mntinfo->mik_secmod));
	data->Set(String::NewFromUtf8(isolate, "mik_curread"),
	    Number::New(isolate, mntinfo->mik_curread));
	data->Set(String::NewFromUtf8(isolate, "mik_curwrite"),
	    Number::New(isolate, mntinfo->mik_curwrite));
	data->Set(String::NewFromUtf8(isolate, "mik_timeo"),
	    Number::New(isolate, mntinfo->mik_timeo));
	data->Set(String::NewFromUtf8(isolate, "mik_retrans"),
	    Number::New(isolate, mntinfo->mik_retrans));
	data->Set(String::NewFromUtf8(isolate, "mik_acregmin"),
	    Number::New(isolate, mntinfo->mik_acregmin));
	data->Set(String::NewFromUtf8(isolate, "mik_acregmax"),
	    Number::New(isolate, mntinfo->mik_acregmax));
	data->Set(String::NewFromUtf8(isolate, "mik_acdirmin"),
	    Number::New(isolate, mntinfo->mik_acdirmin));
	data->Set(String::NewFromUtf8(isolate, "mik_acdirmax"),
	    Number::New(isolate, mntinfo->mik_acdirmax));
	data->Set(String::NewFromUtf8(isolate, "lookup_srtt"),
	    Number::New(isolate, mntinfo->mik_timers[0].srtt));
	data->Set(String::NewFromUtf8(isolate, "lookup_deviate"),
	    Number::New(isolate, mntinfo->mik_timers[0].deviate));
	data->Set(String::NewFromUtf8(isolate, "lookup_rtxcur"),
	    Number::New(isolate, mntinfo->mik_timers[0].rtxcur));
	data->Set(String::NewFromUtf8(isolate, "read_srtt"),
	    Number::New(isolate, mntinfo->mik_timers[1].srtt));
	data->Set(String::NewFromUtf8(isolate, "read_deviate"),
	    Number::New(isolate, mntinfo->mik_timers[1].deviate));
	data->Set(String::NewFromUtf8(isolate, "read_rtxcur"),
	    Number::New(isolate, mntinfo->mik_timers[1].rtxcur));
	data->Set(String::NewFromUtf8(isolate, "write_srtt"),
	    Number::New(isolate, mntinfo->mik_timers[2].srtt));
	data->Set(String::NewFromUtf8(isolate, "write_deviate"),
	    Number::New(isolate, mntinfo->mik_timers[2].deviate));
	data->Set(String::NewFromUtf8(isolate, "write_rtxcur"),
	    Number::New(isolate, mntinfo->mik_timers[2].rtxcur));
	data->Set(String::NewFromUtf8(isolate, "mik_noresponse"),
	    Number::New(isolate, mntinfo->mik_noresponse));
	data->Set(String::NewFromUtf8(isolate, "mik_failover"),
	    Number::New(isolate, mntinfo->mik_failover));
	data->Set(String::NewFromUtf8(isolate, "mik_remap"),
	    Number::New(isolate, mntinfo->mik_remap));
	data->Set(String::NewFromUtf8(isolate, "mntinfo"),
	    String::NewFromUtf8(isolate, mntinfo->mik_curserver));

	return (data);
}

Handle<Object>
KStatReader::data_raw(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data;

	assert(ksp->ks_type == KSTAT_TYPE_RAW);

	if (strcmp(ksp->ks_name, "cpu_stat") == 0) {
		data = data_raw_cpu_stat(isolate, ksp);
	} else if (strcmp(ksp->ks_name, "var") == 0) {
		data = data_raw_var(isolate, ksp);
	} else if (strcmp(ksp->ks_name, "ncstats") == 0) {
		data = data_raw_ncstats(isolate, ksp);
	} else if (strcmp(ksp->ks_name, "sysinfo") == 0) {
		data = data_raw_sysinfo(isolate, ksp);
	} else if (strcmp(ksp->ks_name, "vminfo") == 0) {
		data = data_raw_vminfo(isolate, ksp);
	} else if (strcmp(ksp->ks_name, "mntinfo") == 0) {
		data = data_raw_mntinfo(isolate, ksp);
	} else {
		data = Object::New(isolate);
	}

	return (data);
}

Handle<Object>
KStatReader::data_named(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);
	kstat_named_t *nm = KSTAT_NAMED_PTR(ksp);
	unsigned int i;

	assert(ksp->ks_type == KSTAT_TYPE_NAMED);

	for (i = 0; i < ksp->ks_ndata; i++, nm++) {
		Handle<Value> val;

		switch (nm->data_type) {
		case KSTAT_DATA_CHAR:
			val = Number::New(isolate, nm->value.c[0]);
			break;

		case KSTAT_DATA_INT32:
			val = Number::New(isolate, nm->value.i32);
			break;

		case KSTAT_DATA_UINT32:
			val = Number::New(isolate, nm->value.ui32);
			break;

		case KSTAT_DATA_INT64:
			val = Number::New(isolate, nm->value.i64);
			break;

		case KSTAT_DATA_UINT64:
			val = Number::New(isolate, nm->value.ui64);
			break;

		case KSTAT_DATA_STRING:
			val = String::NewFromUtf8(isolate, KSTAT_NAMED_STR_PTR(nm));
			break;

		default:
			throw (error(isolate, "unrecognized data type %d for member "
			    "\"%s\" in instance %d of stat \"%s\" (module "
			    "\"%s\", class \"%s\")\n", nm->data_type,
			    nm->name, ksp->ks_instance, ksp->ks_name,
			    ksp->ks_module, ksp->ks_class));
		}

		data->Set(String::NewFromUtf8(isolate, nm->name), val);
	}

	return (data);
}

Handle<Object>
KStatReader::data_intr(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);
	kstat_intr_t *intr = KSTAT_INTR_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_INTR);

	data->Set(String::NewFromUtf8(isolate, "KSTAT_INTR_HARD"),
	    Number::New(isolate, intr->intrs[KSTAT_INTR_HARD]));
	data->Set(String::NewFromUtf8(isolate, "KSTAT_INTR_SOFT"),
	    Number::New(isolate, intr->intrs[KSTAT_INTR_SOFT]));
	data->Set(String::NewFromUtf8(isolate, "KSTAT_INTR_WATCHDOG"),
	    Number::New(isolate, intr->intrs[KSTAT_INTR_WATCHDOG]));
	data->Set(String::NewFromUtf8(isolate, "KSTAT_INTR_SPURIOUS"),
	    Number::New(isolate, intr->intrs[KSTAT_INTR_SPURIOUS]));
	data->Set(String::NewFromUtf8(isolate, "KSTAT_INTR_MULTSVC"),
	    Number::New(isolate, intr->intrs[KSTAT_INTR_MULTSVC]));

	return (data);
}

Handle<Object>
KStatReader::data_io(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);
	kstat_io_t *io = KSTAT_IO_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_IO);

	data->Set(String::NewFromUtf8(isolate, "nread"), Number::New(isolate, io->nread));
	data->Set(String::NewFromUtf8(isolate, "nwritten"), Number::New(isolate, io->nwritten));
	data->Set(String::NewFromUtf8(isolate, "reads"), Integer::New(isolate, io->reads));
	data->Set(String::NewFromUtf8(isolate, "writes"), Integer::New(isolate, io->writes));

	data->Set(String::NewFromUtf8(isolate, "wtime"), Number::New(isolate, io->wtime));
	data->Set(String::NewFromUtf8(isolate, "wlentime"), Number::New(isolate, io->wlentime));
	data->Set(String::NewFromUtf8(isolate, "wlastupdate"), Number::New(isolate, io->wlastupdate));

	data->Set(String::NewFromUtf8(isolate, "rtime"), Number::New(isolate, io->rtime));
	data->Set(String::NewFromUtf8(isolate, "rlentime"), Number::New(isolate, io->rlentime));
	data->Set(String::NewFromUtf8(isolate, "rlastupdate"), Number::New(isolate, io->rlastupdate));

	data->Set(String::NewFromUtf8(isolate, "wcnt"), Integer::New(isolate, io->wcnt));
	data->Set(String::NewFromUtf8(isolate, "rcnt"), Integer::New(isolate, io->rcnt));

	return (data);
}

Handle<Object>
KStatReader::data_timer(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> data = Object::New(isolate);
	kstat_timer_t *timer = KSTAT_TIMER_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_TIMER);

	data->Set(String::NewFromUtf8(isolate, "name"), String::NewFromUtf8(isolate, timer->name));
	data->Set(String::NewFromUtf8(isolate, "num_events"), Number::New(isolate, timer->num_events));
	data->Set(String::NewFromUtf8(isolate, "elapsed_time"),
	    Number::New(isolate, timer->elapsed_time));
	data->Set(String::NewFromUtf8(isolate, "min_time"), Number::New(isolate, timer->min_time));
	data->Set(String::NewFromUtf8(isolate, "max_time"), Number::New(isolate, timer->max_time));
	data->Set(String::NewFromUtf8(isolate, "start_time"), Number::New(isolate, timer->start_time));
	data->Set(String::NewFromUtf8(isolate, "stop_time"), Number::New(isolate, timer->stop_time));

	return (data);
}

Handle<Value>
KStatReader::read(Isolate *isolate, kstat_t *ksp)
{
	Handle<Object> rval = Object::New(isolate);
	Handle<Object> data;

	rval->Set(String::NewFromUtf8(isolate, "class"), String::NewFromUtf8(isolate, ksp->ks_class));
	rval->Set(String::NewFromUtf8(isolate, "module"), String::NewFromUtf8(isolate, ksp->ks_module));
	rval->Set(String::NewFromUtf8(isolate, "name"), String::NewFromUtf8(isolate, ksp->ks_name));
	rval->Set(String::NewFromUtf8(isolate, "instance"), Integer::New(isolate, ksp->ks_instance));

	if (kstat_read(ksr_ctl, ksp, NULL) == -1) {
		/*
		 * It is deeply annoying, but some kstats can return errors
		 * under otherwise routine conditions.  (ACPI is one
		 * offender; there are surely others.)  To prevent these
		 * fouled kstats from completely ruining our day, we assign
		 * an "error" member to the return value that consists of
		 * the strerror().
		 */
		rval->Set(String::NewFromUtf8(isolate, "error"), String::NewFromUtf8(isolate, strerror(errno)));
		return (rval);
	}

	rval->Set(String::NewFromUtf8(isolate, "instance"), Integer::New(isolate, ksp->ks_instance));
	rval->Set(String::NewFromUtf8(isolate, "snaptime"), Number::New(isolate, ksp->ks_snaptime));
	rval->Set(String::NewFromUtf8(isolate, "crtime"), Number::New(isolate, ksp->ks_crtime));

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
			data = data_raw(isolate, ksp);
			break;

		case KSTAT_TYPE_NAMED:
			data = data_named(isolate, ksp);
			break;

		case KSTAT_TYPE_INTR:
			data = data_intr(isolate, ksp);
			break;

		case KSTAT_TYPE_IO:
			data = data_io(isolate, ksp);
			break;

		case KSTAT_TYPE_TIMER:
			data = data_timer(isolate, ksp);
			break;

		default:
			return (rval);
	}

	rval->Set(String::NewFromUtf8(isolate, "data"), data);

	return (rval);
}

void
KStatReader::Close(const FunctionCallbackInfo<Value>& args)
{
	KStatReader *k = ObjectWrap::Unwrap<KStatReader>(args.Holder());
	Isolate *isolate = args.GetIsolate();

	if (k->ksr_ctl == NULL)
		args.GetReturnValue().Set (k->error(isolate, "kstat reader has already been closed\n"));

	k->close();
	args.GetReturnValue().SetUndefined();
}

void
KStatReader::Read(const FunctionCallbackInfo<Value>& args)
{
	KStatReader *k = ObjectWrap::Unwrap<KStatReader>(args.Holder());
	Handle<Array> rval;
	Isolate *isolate = args.GetIsolate();
	ReturnValue<Value> returnValue = args.GetReturnValue();
	unsigned int i, j;

	if (k->ksr_ctl == NULL)
		returnValue.Set (k->error(isolate, "kstat reader has already been closed\n"));

	if (k->update() == -1)
		returnValue.Set (k->error(isolate, "failed to update kstat chain"));

	string *rmodule = stringMember(isolate, args[0], "module", "");
	string *rclass = stringMember(isolate, args[0], "class", "");
	string *rname = stringMember(isolate, args[0], "name", "");
	int64_t rinstance = intMember(isolate, args[0], "instance", -1);

	rval = Array::New(isolate);

	try {
		for (i = 0, j = 0; i < k->ksr_kstats.size(); i++) {
			if (!k->matches(k->ksr_kstats[i],
			    rmodule, rclass, rname, rinstance))
				continue;

			rval->Set(j++, k->read(isolate, k->ksr_kstats[i]));
		}
	} catch (Handle<Value> err) {
		delete rmodule;
		delete rclass;
		delete rname;
		returnValue.Set (err);
	}

	delete rmodule;
	delete rclass;
	delete rname;
	returnValue.Set (rval);
}

extern "C" void
init(Handle<Object> exports)
{
	KStatReader::Initialize(exports);
}

NODE_MODULE(kstat, init);
