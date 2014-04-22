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

class KStatReader : node::ObjectWrap {
public:
	static void Initialize(Handle<Object> target);

protected:
	static Persistent<FunctionTemplate> templ;

	KStatReader(string *module, string *classname,
	    string *name, int instance);
	void close();
	Handle<Value> error(const char *fmt, ...);
	Handle<Value> read(kstat_t *);
	bool matches(kstat_t *, string *, string *, string *, int64_t);
	int update();
	~KStatReader();

	static Handle<Value> Close(const Arguments& args);
	static Handle<Value> New(const Arguments& args);
	static Handle<Value> Read(const Arguments& args);


private:
	static string *stringMember(Local<Value>, char *, char *);
	static int64_t intMember(Local<Value>, char *, int64_t);
	Handle<Object> data_raw_cpu_stat(kstat_t *);
	Handle<Object> data_raw_var(kstat_t *);
	Handle<Object> data_raw_ncstats(kstat_t *);
	Handle<Object> data_raw_sysinfo(kstat_t *);
	Handle<Object> data_raw_vminfo(kstat_t *);
	Handle<Object> data_raw_mntinfo(kstat_t *);
	Handle<Object> data_raw(kstat_t *);
	Handle<Object> data_named(kstat_t *);
	Handle<Object> data_intr(kstat_t *);
	Handle<Object> data_io(kstat_t *);
	Handle<Object> data_timer(kstat_t *);

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
KStatReader::Initialize(Handle<Object> target)
{
	HandleScope scope;

	Local<FunctionTemplate> k = FunctionTemplate::New(KStatReader::New);

	templ = Persistent<FunctionTemplate>::New(k);
	templ->InstanceTemplate()->SetInternalFieldCount(1);
	templ->SetClassName(String::NewSymbol("Reader"));

	NODE_SET_PROTOTYPE_METHOD(templ, "read", KStatReader::Read);
	NODE_SET_PROTOTYPE_METHOD(templ, "close", KStatReader::Close);

	target->Set(String::NewSymbol("Reader"), templ->GetFunction());
}

string *
KStatReader::stringMember(Local<Value> value, char *member, char *deflt)
{
	if (!value->IsObject())
		return (new string(deflt));

	Local<Object> o = Local<Object>::Cast(value);
	Local<Value> v = o->Get(String::New(member));

	if (!v->IsString())
		return (new string(deflt));

	String::AsciiValue val(v);
	return (new string(*val));
}

int64_t
KStatReader::intMember(Local<Value> value, char *member, int64_t deflt)
{
	int64_t rval = deflt;

	if (!value->IsObject())
		return (rval);

	Local<Object> o = Local<Object>::Cast(value);
	value = o->Get(String::New(member));

	if (!value->IsNumber())
		return (rval);

	Local<Integer> i = Local<Integer>::Cast(value);

	return (i->Value());
}

Handle<Value>
KStatReader::New(const Arguments& args)
{
	HandleScope scope;

	KStatReader *k = new KStatReader(stringMember(args[0], "module", ""),
	    stringMember(args[0], "class", ""),
	    stringMember(args[0], "name", ""),
	    intMember(args[0], "instance", -1));

	k->Wrap(args.Holder());

	return (args.This());
}

Handle<Value>
KStatReader::error(const char *fmt, ...)
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

	return (ThrowException(Exception::Error(String::New(err))));
}

Handle<Object>
KStatReader::data_raw_cpu_stat(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (cpu_stat_t));

	cpu_stat_t	  *stat;
	cpu_sysinfo_t	*sysinfo;
	cpu_syswait_t	*syswait;
	cpu_vminfo_t	*vminfo;

	stat    = (cpu_stat_t *)(ksp->ks_data);
	sysinfo = &stat->cpu_sysinfo;
	syswait = &stat->cpu_syswait;
	vminfo  = &stat->cpu_vminfo;

	data->Set(String::New("idle"), Number::New(sysinfo->cpu[CPU_IDLE]));
	data->Set(String::New("user"), Number::New(sysinfo->cpu[CPU_USER]));
	data->Set(String::New("kernel"), Number::New(sysinfo->cpu[CPU_KERNEL]));
	data->Set(String::New("wait"), Number::New(sysinfo->cpu[CPU_WAIT]));
	data->Set(String::New("wait_io"), Number::New(sysinfo->wait[W_IO]));
	data->Set(String::New("wait_swap"), Number::New(sysinfo->wait[W_SWAP]));
	data->Set(String::New("wait_pio"), Number::New(sysinfo->wait[W_PIO]));
	data->Set(String::New("bread"), Number::New(sysinfo->bread));
	data->Set(String::New("bwrite"), Number::New(sysinfo->bwrite));
	data->Set(String::New("lread"), Number::New(sysinfo->lread));
	data->Set(String::New("lwrite"), Number::New(sysinfo->lwrite));
	data->Set(String::New("phread"), Number::New(sysinfo->phread));
	data->Set(String::New("phwrite"), Number::New(sysinfo->phwrite));
	data->Set(String::New("pswitch"), Number::New(sysinfo->pswitch));
	data->Set(String::New("trap"), Number::New(sysinfo->trap));
	data->Set(String::New("intr"), Number::New(sysinfo->intr));
	data->Set(String::New("syscall"), Number::New(sysinfo->syscall));
	data->Set(String::New("sysread"), Number::New(sysinfo->sysread));
	data->Set(String::New("syswrite"), Number::New(sysinfo->syswrite));
	data->Set(String::New("sysfork"), Number::New(sysinfo->sysfork));
	data->Set(String::New("sysvfork"), Number::New(sysinfo->sysvfork));
	data->Set(String::New("sysexec"), Number::New(sysinfo->sysexec));
	data->Set(String::New("readch"), Number::New(sysinfo->readch));
	data->Set(String::New("writech"), Number::New(sysinfo->writech));
	data->Set(String::New("rcvint"), Number::New(sysinfo->rcvint));
	data->Set(String::New("xmtint"), Number::New(sysinfo->xmtint));
	data->Set(String::New("mdmint"), Number::New(sysinfo->mdmint));
	data->Set(String::New("rawch"), Number::New(sysinfo->rawch));
	data->Set(String::New("canch"), Number::New(sysinfo->canch));
	data->Set(String::New("outch"), Number::New(sysinfo->outch));
	data->Set(String::New("msg"), Number::New(sysinfo->msg));
	data->Set(String::New("sema"), Number::New(sysinfo->sema));
	data->Set(String::New("namei"), Number::New(sysinfo->namei));
	data->Set(String::New("ufsiget"), Number::New(sysinfo->ufsiget));
	data->Set(String::New("ufsdirblk"), Number::New(sysinfo->ufsdirblk));
	data->Set(String::New("ufsipage"), Number::New(sysinfo->ufsipage));
	data->Set(String::New("ufsinopage"), Number::New(sysinfo->ufsinopage));
	data->Set(String::New("inodeovf"), Number::New(sysinfo->inodeovf));
	data->Set(String::New("fileovf"), Number::New(sysinfo->fileovf));
	data->Set(String::New("procovf"), Number::New(sysinfo->procovf));
	data->Set(String::New("intrthread"), Number::New(sysinfo->intrthread));
	data->Set(String::New("intrblk"), Number::New(sysinfo->intrblk));
	data->Set(String::New("idlethread"), Number::New(sysinfo->idlethread));
	data->Set(String::New("inv_swtch"), Number::New(sysinfo->inv_swtch));
	data->Set(String::New("nthreads"), Number::New(sysinfo->nthreads));
	data->Set(String::New("cpumigrate"), Number::New(sysinfo->cpumigrate));
	data->Set(String::New("xcalls"), Number::New(sysinfo->xcalls));
	data->Set(String::New("mutex_adenters"),
	    Number::New(sysinfo->mutex_adenters));
	data->Set(String::New("rw_rdfails"), Number::New(sysinfo->rw_rdfails));
	data->Set(String::New("rw_wrfails"), Number::New(sysinfo->rw_wrfails));
	data->Set(String::New("modload"), Number::New(sysinfo->modload));
	data->Set(String::New("modunload"), Number::New(sysinfo->modunload));
	data->Set(String::New("bawrite"), Number::New(sysinfo->bawrite));
#ifdef	STATISTICS	/* see header file */
	data->Set(String::New("rw_enters"), Number::New(sysinfo->rw_enters));
	data->Set(String::New("win_uo_cnt"), Number::New(sysinfo->win_uo_cnt));
	data->Set(String::New("win_uu_cnt"), Number::New(sysinfo->win_uu_cnt));
	data->Set(String::New("win_so_cnt"), Number::New(sysinfo->win_so_cnt));
	data->Set(String::New("win_su_cnt"), Number::New(sysinfo->win_su_cnt));
	data->Set(String::New("win_suo_cnt"),
	    Number::New(sysinfo->win_suo_cnt));
#endif

	data->Set(String::New("iowait"), Number::New(syswait->iowait));
	data->Set(String::New("swap"), Number::New(syswait->swap));
	data->Set(String::New("physio"), Number::New(syswait->physio));

	data->Set(String::New("pgrec"), Number::New(vminfo->pgrec));
	data->Set(String::New("pgfrec"), Number::New(vminfo->pgfrec));
	data->Set(String::New("pgin"), Number::New(vminfo->pgin));
	data->Set(String::New("pgpgin"), Number::New(vminfo->pgpgin));
	data->Set(String::New("pgout"), Number::New(vminfo->pgout));
	data->Set(String::New("pgpgout"), Number::New(vminfo->pgpgout));
	data->Set(String::New("swapin"), Number::New(vminfo->swapin));
	data->Set(String::New("pgswapin"), Number::New(vminfo->pgswapin));
	data->Set(String::New("swapout"), Number::New(vminfo->swapout));
	data->Set(String::New("pgswapout"), Number::New(vminfo->pgswapout));
	data->Set(String::New("zfod"), Number::New(vminfo->zfod));
	data->Set(String::New("dfree"), Number::New(vminfo->dfree));
	data->Set(String::New("scan"), Number::New(vminfo->scan));
	data->Set(String::New("rev"), Number::New(vminfo->rev));
	data->Set(String::New("hat_fault"), Number::New(vminfo->hat_fault));
	data->Set(String::New("as_fault"), Number::New(vminfo->as_fault));
	data->Set(String::New("maj_fault"), Number::New(vminfo->maj_fault));
	data->Set(String::New("cow_fault"), Number::New(vminfo->cow_fault));
	data->Set(String::New("prot_fault"), Number::New(vminfo->prot_fault));
	data->Set(String::New("softlock"), Number::New(vminfo->softlock));
	data->Set(String::New("kernel_asflt"),
	    Number::New(vminfo->kernel_asflt));
	data->Set(String::New("pgrrun"), Number::New(vminfo->pgrrun));
	data->Set(String::New("execpgin"), Number::New(vminfo->execpgin));
	data->Set(String::New("execpgout"), Number::New(vminfo->execpgout));
	data->Set(String::New("execfree"), Number::New(vminfo->execfree));
	data->Set(String::New("anonpgin"), Number::New(vminfo->anonpgin));
	data->Set(String::New("anonpgout"), Number::New(vminfo->anonpgout));
	data->Set(String::New("anonfree"), Number::New(vminfo->anonfree));
	data->Set(String::New("fspgin"), Number::New(vminfo->fspgin));
	data->Set(String::New("fspgout"), Number::New(vminfo->fspgout));
	data->Set(String::New("fsfree"), Number::New(vminfo->fsfree));

	return (data);
}

Handle<Object>
KStatReader::data_raw_var(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (struct var));

	struct var	*var = (struct var *)(ksp->ks_data);

	data->Set(String::New("v_buf"), Number::New(var->v_buf));
	data->Set(String::New("v_call"), Number::New(var->v_call));
	data->Set(String::New("v_proc"), Number::New(var->v_proc));
	data->Set(String::New("v_maxupttl"), Number::New(var->v_maxupttl));
	data->Set(String::New("v_nglobpris"), Number::New(var->v_nglobpris));
	data->Set(String::New("v_maxsyspri"), Number::New(var->v_maxsyspri));
	data->Set(String::New("v_clist"), Number::New(var->v_clist));
	data->Set(String::New("v_maxup"), Number::New(var->v_maxup));
	data->Set(String::New("v_hbuf"), Number::New(var->v_hbuf));
	data->Set(String::New("v_hmask"), Number::New(var->v_hmask));
	data->Set(String::New("v_pbuf"), Number::New(var->v_pbuf));
	data->Set(String::New("v_sptmap"), Number::New(var->v_sptmap));
	data->Set(String::New("v_maxpmem"), Number::New(var->v_maxpmem));
	data->Set(String::New("v_autoup"), Number::New(var->v_autoup));
	data->Set(String::New("v_bufhwm"), Number::New(var->v_bufhwm));

	return (data);
}

Handle<Object>
KStatReader::data_raw_ncstats(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (struct ncstats));

	struct ncstats	*ncstats = (struct ncstats *)(ksp->ks_data);

	data->Set(String::New("hits"), Number::New(ncstats->hits));
	data->Set(String::New("misses"), Number::New(ncstats->misses));
	data->Set(String::New("enters"), Number::New(ncstats->enters));
	data->Set(String::New("dbl_enters"), Number::New(ncstats->dbl_enters));
	data->Set(String::New("long_enter"), Number::New(ncstats->long_enter));
	data->Set(String::New("long_look"), Number::New(ncstats->long_look));
	data->Set(String::New("move_to_front"),
	    Number::New(ncstats->move_to_front));
	data->Set(String::New("purges"), Number::New(ncstats->purges));

	return (data);
}

Handle<Object>
KStatReader::data_raw_sysinfo(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (sysinfo_t));

	sysinfo_t	*sysinfo = (sysinfo_t *)(ksp->ks_data);

	data->Set(String::New("updates"), Number::New(sysinfo->updates));
	data->Set(String::New("runque"), Number::New(sysinfo->runque));
	data->Set(String::New("runocc"), Number::New(sysinfo->runocc));
	data->Set(String::New("swpque"), Number::New(sysinfo->swpque));
	data->Set(String::New("swpocc"), Number::New(sysinfo->swpocc));
	data->Set(String::New("waiting"), Number::New(sysinfo->waiting));

	return (data);
}

Handle<Object>
KStatReader::data_raw_vminfo(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (vminfo_t));

	vminfo_t	*vminfo = (vminfo_t *)(ksp->ks_data);

	data->Set(String::New("freemem"), Number::New(vminfo->freemem));
	data->Set(String::New("swap_resv"), Number::New(vminfo->swap_resv));
	data->Set(String::New("swap_alloc"), Number::New(vminfo->swap_alloc));
	data->Set(String::New("swap_avail"), Number::New(vminfo->swap_avail));
	data->Set(String::New("swap_free"), Number::New(vminfo->swap_free));
	data->Set(String::New("updates"), Number::New(vminfo->updates));

	return (data);
}

Handle<Object>
KStatReader::data_raw_mntinfo(kstat_t *ksp)
{
	Handle<Object> data = Object::New();

	assert(ksp->ks_data_size == sizeof (struct mntinfo_kstat));

	struct mntinfo_kstat *mntinfo = (struct mntinfo_kstat *)(ksp->ks_data);

	data->Set(String::New("mntinfo"),
	    String::New(mntinfo->mik_proto));
	data->Set(String::New("mik_vers"),
	    Number::New(mntinfo->mik_vers));
	data->Set(String::New("mik_flags"),
	    Number::New(mntinfo->mik_flags));
	data->Set(String::New("mik_secmod"),
	    Number::New(mntinfo->mik_secmod));
	data->Set(String::New("mik_curread"),
	    Number::New(mntinfo->mik_curread));
	data->Set(String::New("mik_curwrite"),
	    Number::New(mntinfo->mik_curwrite));
	data->Set(String::New("mik_timeo"),
	    Number::New(mntinfo->mik_timeo));
	data->Set(String::New("mik_retrans"),
	    Number::New(mntinfo->mik_retrans));
	data->Set(String::New("mik_acregmin"),
	    Number::New(mntinfo->mik_acregmin));
	data->Set(String::New("mik_acregmax"),
	    Number::New(mntinfo->mik_acregmax));
	data->Set(String::New("mik_acdirmin"),
	    Number::New(mntinfo->mik_acdirmin));
	data->Set(String::New("mik_acdirmax"),
	    Number::New(mntinfo->mik_acdirmax));
	data->Set(String::New("lookup_srtt"),
	    Number::New(mntinfo->mik_timers[0].srtt));
	data->Set(String::New("lookup_deviate"),
	    Number::New(mntinfo->mik_timers[0].deviate));
	data->Set(String::New("lookup_rtxcur"),
	    Number::New(mntinfo->mik_timers[0].rtxcur));
	data->Set(String::New("read_srtt"),
	    Number::New(mntinfo->mik_timers[1].srtt));
	data->Set(String::New("read_deviate"),
	    Number::New(mntinfo->mik_timers[1].deviate));
	data->Set(String::New("read_rtxcur"),
	    Number::New(mntinfo->mik_timers[1].rtxcur));
	data->Set(String::New("write_srtt"),
	    Number::New(mntinfo->mik_timers[2].srtt));
	data->Set(String::New("write_deviate"),
	    Number::New(mntinfo->mik_timers[2].deviate));
	data->Set(String::New("write_rtxcur"),
	    Number::New(mntinfo->mik_timers[2].rtxcur));
	data->Set(String::New("mik_noresponse"),
	    Number::New(mntinfo->mik_noresponse));
	data->Set(String::New("mik_failover"),
	    Number::New(mntinfo->mik_failover));
	data->Set(String::New("mik_remap"),
	    Number::New(mntinfo->mik_remap));
	data->Set(String::New("mntinfo"),
	    String::New(mntinfo->mik_curserver));

	return (data);
}

Handle<Object>
KStatReader::data_raw(kstat_t *ksp)
{
	Handle<Object> data;

	assert(ksp->ks_type == KSTAT_TYPE_RAW);

	if (strcmp(ksp->ks_name, "cpu_stat") == 0) {
		data = data_raw_cpu_stat(ksp);
	} else if (strcmp(ksp->ks_name, "var") == 0) {
		data = data_raw_var(ksp);
	} else if (strcmp(ksp->ks_name, "ncstats") == 0) {
		data = data_raw_ncstats(ksp);
	} else if (strcmp(ksp->ks_name, "sysinfo") == 0) {
		data = data_raw_sysinfo(ksp);
	} else if (strcmp(ksp->ks_name, "vminfo") == 0) {
		data = data_raw_vminfo(ksp);
	} else if (strcmp(ksp->ks_name, "mntinfo") == 0) {
		data = data_raw_mntinfo(ksp);
	} else {
		data = Object::New();
	}

	return (data);
}

Handle<Object>
KStatReader::data_named(kstat_t *ksp)
{
	Handle<Object> data = Object::New();
	kstat_named_t *nm = KSTAT_NAMED_PTR(ksp);
	unsigned int i;

	assert(ksp->ks_type == KSTAT_TYPE_NAMED);

	for (i = 0; i < ksp->ks_ndata; i++, nm++) {
		Handle<Value> val;

		switch (nm->data_type) {
		case KSTAT_DATA_CHAR:
			val = Number::New(nm->value.c[0]);
			break;

		case KSTAT_DATA_INT32:
			val = Number::New(nm->value.i32);
			break;

		case KSTAT_DATA_UINT32:
			val = Number::New(nm->value.ui32);
			break;

		case KSTAT_DATA_INT64:
			val = Number::New(nm->value.i64);
			break;

		case KSTAT_DATA_UINT64:
			val = Number::New(nm->value.ui64);
			break;

		case KSTAT_DATA_STRING:
			val = String::New(KSTAT_NAMED_STR_PTR(nm));
			break;

		default:
			throw (error("unrecognized data type %d for member "
			    "\"%s\" in instance %d of stat \"%s\" (module "
			    "\"%s\", class \"%s\")\n", nm->data_type,
			    nm->name, ksp->ks_instance, ksp->ks_name,
			    ksp->ks_module, ksp->ks_class));
		}

		data->Set(String::New(nm->name), val);
	}

	return (data);
}

Handle<Object>
KStatReader::data_intr(kstat_t *ksp)
{
	Handle<Object> data = Object::New();
	kstat_intr_t *intr = KSTAT_INTR_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_INTR);

	data->Set(String::New("KSTAT_INTR_HARD"),
	    Number::New(intr->intrs[KSTAT_INTR_HARD]));
	data->Set(String::New("KSTAT_INTR_SOFT"),
	    Number::New(intr->intrs[KSTAT_INTR_SOFT]));
	data->Set(String::New("KSTAT_INTR_WATCHDOG"),
	    Number::New(intr->intrs[KSTAT_INTR_WATCHDOG]));
	data->Set(String::New("KSTAT_INTR_SPURIOUS"),
	    Number::New(intr->intrs[KSTAT_INTR_SPURIOUS]));
	data->Set(String::New("KSTAT_INTR_MULTSVC"),
	    Number::New(intr->intrs[KSTAT_INTR_MULTSVC]));

	return (data);
}

Handle<Object>
KStatReader::data_io(kstat_t *ksp)
{
	Handle<Object> data = Object::New();
	kstat_io_t *io = KSTAT_IO_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_IO);

	data->Set(String::New("nread"), Number::New(io->nread));
	data->Set(String::New("nwritten"), Number::New(io->nwritten));
	data->Set(String::New("reads"), Integer::New(io->reads));
	data->Set(String::New("writes"), Integer::New(io->writes));

	data->Set(String::New("wtime"), Number::New(io->wtime));
	data->Set(String::New("wlentime"), Number::New(io->wlentime));
	data->Set(String::New("wlastupdate"), Number::New(io->wlastupdate));

	data->Set(String::New("rtime"), Number::New(io->rtime));
	data->Set(String::New("rlentime"), Number::New(io->rlentime));
	data->Set(String::New("rlastupdate"), Number::New(io->rlastupdate));

	data->Set(String::New("wcnt"), Integer::New(io->wcnt));
	data->Set(String::New("rcnt"), Integer::New(io->rcnt));

	return (data);
}

Handle<Object>
KStatReader::data_timer(kstat_t *ksp)
{
	Handle<Object> data = Object::New();
	kstat_timer_t *timer = KSTAT_TIMER_PTR(ksp);

	assert(ksp->ks_type == KSTAT_TYPE_TIMER);

	data->Set(String::New("name"), String::New(timer->name));
	data->Set(String::New("num_events"), Number::New(timer->num_events));
	data->Set(String::New("elapsed_time"),
	    Number::New(timer->elapsed_time));
	data->Set(String::New("min_time"), Number::New(timer->min_time));
	data->Set(String::New("max_time"), Number::New(timer->max_time));
	data->Set(String::New("start_time"), Number::New(timer->start_time));
	data->Set(String::New("stop_time"), Number::New(timer->stop_time));

	return (data);
}

Handle<Value>
KStatReader::read(kstat_t *ksp)
{
	Handle<Object> rval = Object::New();
	Handle<Object> data;

	rval->Set(String::New("class"), String::New(ksp->ks_class));
	rval->Set(String::New("module"), String::New(ksp->ks_module));
	rval->Set(String::New("name"), String::New(ksp->ks_name));
	rval->Set(String::New("instance"), Integer::New(ksp->ks_instance));

	if (kstat_read(ksr_ctl, ksp, NULL) == -1) {
		/*
		 * It is deeply annoying, but some kstats can return errors
		 * under otherwise routine conditions.  (ACPI is one
		 * offender; there are surely others.)  To prevent these
		 * fouled kstats from completely ruining our day, we assign
		 * an "error" member to the return value that consists of
		 * the strerror().
		 */
		rval->Set(String::New("error"), String::New(strerror(errno)));
		return (rval);
	}

	rval->Set(String::New("instance"), Integer::New(ksp->ks_instance));
	rval->Set(String::New("snaptime"), Number::New(ksp->ks_snaptime));
	rval->Set(String::New("crtime"), Number::New(ksp->ks_crtime));

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
			data = data_raw(ksp);
			break;

		case KSTAT_TYPE_NAMED:
			data = data_named(ksp);
			break;

		case KSTAT_TYPE_INTR:
			data = data_intr(ksp);
			break;

		case KSTAT_TYPE_IO:
			data = data_io(ksp);
			break;

		case KSTAT_TYPE_TIMER:
			data = data_timer(ksp);
			break;

		default:
			return (rval);
	}

	rval->Set(String::New("data"), data);

	return (rval);
}

Handle<Value>
KStatReader::Close(const Arguments& args)
{
	KStatReader *k = ObjectWrap::Unwrap<KStatReader>(args.Holder());
	HandleScope scope;

	if (k->ksr_ctl == NULL)
		return (k->error("kstat reader has already been closed\n"));

	k->close();
	return (Undefined());
}

Handle<Value>
KStatReader::Read(const Arguments& args)
{
	KStatReader *k = ObjectWrap::Unwrap<KStatReader>(args.Holder());
	Handle<Array> rval;
	HandleScope scope;
	unsigned int i, j;

	if (k->ksr_ctl == NULL)
		return (k->error("kstat reader has already been closed\n"));

	if (k->update() == -1)
		return (k->error("failed to update kstat chain"));

	string *rmodule = stringMember(args[0], "module", "");
	string *rclass = stringMember(args[0], "class", "");
	string *rname = stringMember(args[0], "name", "");
	int64_t rinstance = intMember(args[0], "instance", -1);

	rval = Array::New();

	try {
		for (i = 0, j = 0; i < k->ksr_kstats.size(); i++) {
			if (!k->matches(k->ksr_kstats[i],
			    rmodule, rclass, rname, rinstance))
				continue;

			rval->Set(j++, k->read(k->ksr_kstats[i]));
		}
	} catch (Handle<Value> err) {
		delete rmodule;
		delete rclass;
		delete rname;
		return (err);
	}

	delete rmodule;
	delete rclass;
	delete rname;
	return (rval);
}

extern "C" void
init(Handle<Object> target)
{
	KStatReader::Initialize(target);
}

NODE_MODULE(kstat, init);
