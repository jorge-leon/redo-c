/* An implementation of the redo build system
   in portable C with zero dependencies

   Originally from: https://github.com/leahneukirchen/redo-c

   To the extent possible under law, Leah Neukirchen <leah@vuxu.org>
   has waived all copyright and related or neighboring rights to this work.
   http://creativecommons.org/publicdomain/zero/1.0/

   (Already significant) changes by Georg Lehner
   <jorge@at.anteris.net> are released under the same terms.

*/
/*
  Note: infinite dependency loops exhaust file descriptors.

  todo:
  test job server properly
*/

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char version[] = "0.6";

// ----------------------------------------------------------------------

int dir_fd = -1;
int dep_fd = -1;
int poolwr_fd = -1;
int poolrd_fd = -1;
int level = -1;
int implicit_jobs = 1;
int kflag, jflag, xflag, fflag, vflag, dflag;

//                                      1234567890123456
static const char redo_siphash_key[] = "redo siphash key";
static uint8_t siphash_zero[] = "\x92\x8f\xea\xaf\x8f\xb3\x39\x46\xcd\x28\x6e\x6f\x0b\xbd\x30\xc2";

// diagnostics
static void
die(const char *reason, int status)
{
    fprintf(stderr, "error: %s\n", reason);
    exit(status);
}
static void
err2(const char *reason, const char *argument)
{
    fprintf(stderr, "error: %s %s\n", reason, argument);
}
static void
die2(const char *reason, const char *argument, int status)
{
    err2(reason, argument);
    exit(status);
}

static void
err3(const char *reason, const char *arg1, const char *arg2)
{
    fprintf(stderr, "error: %s %s %s\n", reason, arg1, arg2);
}
static void
dprint2(const char *reason, const char *argument)
{
    if (!dflag)
	return;
    fputs(reason, stderr);
    fputs(argument, stderr);
    fputs("\n", stderr);
}
static void
dprint4(const char *reason, const char *arg1, const char* arg2, const char* arg3)
{
    if (!dflag)
	return;
    fputs(reason, stderr);
    fputs(arg1, stderr);
    fputs(arg2, stderr);
    fputs(arg3, stderr);
    fputs("\n", stderr);
}


// environment helpers

// get integer stored in environment variable
// -1 if not found
static int
envfd(const char *name)
{
    long fd;

    char *s = getenv(name);
    if (!s)
	return -1;

    fd = strtol(s, 0, 10);
    if (fd < 0 || fd > 255)
	fd = -1;

    return fd;
}

// set environment variable to integer
static void
setenvfd(const char *name, int i)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d", i);
    if(setenv(name, buf, 1)) die2("setenv", name, 100);
}

// file/file system helpers

static void
remove_temp(const char *path)
{
    if(remove(path)) err2("remove", path);
}

static void
rename_temp(const char *old, const char *new)
{
    if(rename(old, new)) err3("rename", old, new);
}

// retrieve "current directory" handle.  Stored into global dir_fd.
static int
keepdir()
{
    int fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
	perror("dir open");
	exit(-1);
    }
    return fd;
}


// job manager

void
vacate(int implicit)
{
    if (implicit)
	implicit_jobs++;
    else
	write(poolwr_fd, "\0", 1);
}

struct job {
    struct job *next;
    pid_t pid;
    int lock_fd;
    char *target, *temp_depfile, *temp_target;
    int implicit;
};
struct job *jobhead;

static void
insert_job(struct job *job)
{
    job->next = jobhead;
    jobhead = job;
}

static void
remove_job(struct job *job)
{
    if (jobhead == job)
	jobhead = jobhead->next;
    else {
	struct job *j = jobhead;
	while (j->next != job)
	    j = j->next;
	j->next = j->next->next;
    }
}

static struct job *
find_job(pid_t pid)
{
    struct job *j;

    for (j = jobhead; j; j = j->next) {
	if (j->pid == pid)
	    return j;
    }

    return 0;
}


static int
try_procure()
{
    if (implicit_jobs > 0) {
	implicit_jobs--;
	return 1;
    } else {
	if (poolrd_fd < 0)
	    return 0;

	fcntl(poolrd_fd, F_SETFL, O_NONBLOCK);

	char buf[1];
	return read(poolrd_fd, &buf, 1) > 0;
    }
}

static int
procure()
{
    if (implicit_jobs > 0) {
	implicit_jobs--;
	return 1;
    } else {
	fcntl(poolrd_fd, F_SETFL, 0);   // clear O_NONBLOCK

	char buf[1];
	return read(poolrd_fd, &buf, 1) > 0;
    }
}

void
create_pool()
{
    poolrd_fd = envfd("REDO_RD_FD");
    poolwr_fd = envfd("REDO_WR_FD");
    if (poolrd_fd < 0 || poolwr_fd < 0) {
	int jobs = envfd("JOBS");
	if (jobs > 1) {
	    int i, fds[2];
	    if(pipe(fds)) die("no pipes for pool", 100);
	    poolrd_fd = fds[0];
	    poolwr_fd = fds[1];

	    for (i = 0; i < jobs-1; i++)
		vacate(0);

	    setenvfd("REDO_RD_FD", poolrd_fd);
	    setenvfd("REDO_WR_FD", poolwr_fd);
	} else {
	    poolrd_fd = -1;
	    poolwr_fd = -1;
	}
    }
}

// orig redo-c uses 256 bit/64 chars sha
// we use 128 bit/32 chars siphash, 128 bit = 16 byte
#define HASH_CHARS 32

// https://github.com/veorq/SipHash, CC0 //

/* SipHash-2-4-128 */
#define cROUNDS 2
#define dROUNDS 4

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v) (p)[0] = (uint8_t)((v)); (p)[1] = (uint8_t)((v) >> 8); (p)[2] = (uint8_t)((v) >> 16); (p)[3] = (uint8_t)((v) >> 24);

#define U64TO8_LE(p, v) U32TO8_LE((p), (uint32_t)((v)));  U32TO8_LE((p) + 4, (uint32_t)((v) >> 32));

#define U8TO64_LE(p) (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) | ((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) | ((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) | ((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56))

#define SIPROUND do { v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32); v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2; v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0; v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32); } while (0)

// Note: HASH_CHARS=32 for 128 Bit hashes
uint8_t *siphash2_4_128(const void *in, const size_t inlen, const void *k) {
    static uint8_t out[16];
    const unsigned char *ni = (const unsigned char *)in;
    const unsigned char *kk = (const unsigned char *)k;

    uint64_t v0 = UINT64_C(0x736f6d6570736575);
    uint64_t v1 = UINT64_C(0x646f72616e646f6d);
    uint64_t v2 = UINT64_C(0x6c7967656e657261);
    uint64_t v3 = UINT64_C(0x7465646279746573);
    uint64_t k0 = U8TO64_LE(kk);
    uint64_t k1 = U8TO64_LE(kk + 8);
    uint64_t m;
    int i;
    const unsigned char *end = ni + inlen - (inlen % sizeof(uint64_t));
    const int left = inlen & 7;
    uint64_t b = ((uint64_t)inlen) << 56;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;
    v1 ^= 0xee;
    for (; ni != end; ni += 8) {
	m = U8TO64_LE(ni);
	v3 ^= m;
	for (i = 0; i < cROUNDS; ++i) SIPROUND;
	v0 ^= m;
    }
    switch (left) {
    case 7:	b |= ((uint64_t)ni[6]) << 48; __attribute__ ((fallthrough)); 
    case 6: b |= ((uint64_t)ni[5]) << 40; __attribute__ ((fallthrough)); 
    case 5: b |= ((uint64_t)ni[4]) << 32; __attribute__ ((fallthrough)); 
    case 4: b |= ((uint64_t)ni[3]) << 24; __attribute__ ((fallthrough)); 
    case 3:	b |= ((uint64_t)ni[2]) << 16; __attribute__ ((fallthrough)); 
    case 2:	b |= ((uint64_t)ni[1]) << 8; __attribute__ ((fallthrough)); 
    case 1:	b |= ((uint64_t)ni[0]);	break;
    case 0:	break;
    }
    v3 ^= b;
    for (i = 0; i < cROUNDS; ++i) SIPROUND;
    v0 ^= b;
    v2 ^= 0xee;
    for (i = 0; i < dROUNDS; ++i) SIPROUND;
    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);
    v1 ^= 0xdd;
    for (i = 0; i < dROUNDS; ++i) SIPROUND;
    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out + 8, b);
    return out;
}

// Note: HASH_CHARS
static char *
hashtohex(uint8_t *hash)
{
    static char hex[16] = "0123456789abcdef";
    static char asciihash[HASH_CHARS+1];
    char *a;
    int i;
    
    for (i = 0, a = asciihash; i < HASH_CHARS/2; i++) {
	*a++ = hex[hash[i] / 16];
	*a++ = hex[hash[i] % 16];
    }
    *a = 0;

    return asciihash;
}
static uint8_t *
hashfile(int fd)
{
    off_t off = 0;
    char buf[4096];
    ssize_t r;
    uint8_t *hash = siphash_zero;
    
    while ((r = pread(fd, buf, sizeof buf, off)) > 0) {
	hash = siphash2_4_128(buf, r, &redo_siphash_key);
	off += r;
    }
    return hash;
}

static char *
datefile(int fd)
{
    static char hexdate[17];
    struct stat st;

    fstat(fd, &st);
    snprintf(hexdate, sizeof hexdate, "%016" PRIx64, (uint64_t)st.st_ctime);

    return hexdate;
}

// find/register dofiles

static void
redo_ifcreate(int fd, char *target)
{
    dprintf(fd, "-%s\n", target);
}

static char *
check_dofile(const char *fmt, ...)
{
    static char dofile[PATH_MAX];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dofile, sizeof dofile, fmt, ap);
    va_end(ap);

    if (access(dofile, F_OK) == 0) {
	return dofile;
    } else {
	redo_ifcreate(dep_fd, dofile);
	return 0;
    }
}

/*
  dir/base.a.b
  will look for dir/base.a.b.do,
  dir/default.a.b.do, dir/default.b.do, dir/default.do,
  default.a.b.do, default.b.do, and default.do.

  this function assumes no / in target
*/
static char *
find_dofile(char *target)
{
    char updir[PATH_MAX];
    char *u = updir;
    char *dofile, *s;
    struct stat st, ost;

    dofile = check_dofile("./%s.do", target);
    if (dofile)
	return dofile;

    *u++ = '.';
    *u++ = '/';
    *u = 0;

    st.st_dev = ost.st_dev = st.st_ino = ost.st_ino = 0;

    while (1) {
	ost = st;

	if (stat(updir, &st) < 0)
	    return 0;
	if (ost.st_dev == st.st_dev && ost.st_ino == st.st_ino)
	    break;  // reached root dir, .. = .

	// also check ../target.do
	dofile = check_dofile("%s%s.do", updir, target);
	if (dofile)
	    return dofile;
	s = target;
	while (*s) {
	    if (*s++ == '.') {
		dofile = check_dofile("%sdefault.%s.do", updir, s);
		if (dofile)
		    return dofile;
	    }
	}

	dofile = check_dofile("%sdefault.do", updir);
	if (dofile)
	    return dofile;

	*u++ = '.';
	*u++ = '.';
	*u++ = '/';
	*u = 0;
    }

    return 0;
}

static char *
targetchdir(char *target)
{
    char *base = strrchr(target, '/');
    if (base) {
	int fd;
	*base = 0;
	fd = openat(dir_fd, target, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
	    perror("openat dir");
	    exit(111);
	}
	*base = '/';
	if (fchdir(fd) < 0) {
	    perror("chdir");
	    exit(111);
	}
	close(fd);
	return base+1;
    } else {
	fchdir(dir_fd);
	return target;
    }
}

// dependency handling, derived filenames

void
check_or_create_dir(const char *path)
{
    struct stat stats;
    if (!mkdir(path, 0755)) return;
    if (errno!=EEXIST) die2("failed to mkdir: ", path, 111);
    if (stat(path, &stats) && errno != ENOENT) die2("failed to stat: ", path, 111);
    if (!S_ISDIR(stats.st_mode)) die2("not a directory: ", path, 111);
    if (access(path, R_OK|W_OK|X_OK)<0) die2("insufficient rights: ", path, 111);
}

static const char *
redo_base(char *target)
{
    static const char *redo_base = ".redo";
    (void)target;
    return redo_base;
}

static char *
targetdep(char *target)
{
    static char buf[PATH_MAX];
    snprintf(buf, sizeof buf, ".redo/%s.dep", target);
    return buf;
}

static char *
targetlock(char *target)
{
    static char buf[PATH_MAX];
    snprintf(buf, sizeof buf, ".redo/%s.lock", target);
    return buf;
}

static char *
targettmp(const char *prefix, unsigned int id, const char *target)
{
    static char buf[PATH_MAX];
    snprintf(buf, sizeof buf, ".redo/%s.%u.%s", prefix, id, target);
    return buf;
}

// return false:
// - when target dependency files exists
// - or when dofile for target is not found
static int
sourcefile(char *target)
{
    // return 0 if target dependency file exists
    if (access(targetdep(target), F_OK) == 0)
	return 0;

    // Note: fflag.. REDO_FORCE, never set to negative value!
    // return 0 if target does not exist
    if (fflag < 0)
	return access(target, F_OK) == 0;

    // return 0 if dofile for target is not found
    return find_dofile(target) == 0;
}

// Note: HASH_CHARS depend on hash, and changes .dep file format
// return true when target does not need a rebuild:
// - if target is a sourcefile
// - no "false" check succeeds
// false (0) when:
// - FORCE_REDO is set (>0)
// - depfile cannot be opened for reading
// - error during reading of depfile
// - '-' line and dependency exists
// - '=' line:
//    - dependency cannot be opened for reading
//    - timestamp or hash does not match
//    - all dependencies are up-to date
// - '!' line
// - any other character on first position of line
static int
check_deps(char *target)
{
    char *depfile;
    FILE *f;
    int ok = 1;
    int fd;
    int old_dir_fd = dir_fd;

    target = targetchdir(target);

    if (sourcefile(target)) {
	dprint2("Not rebuilt, is sourcefile: ", target);
	return 1;
    }
    if (fflag > 0) {
	dprint2("Rebuild, force flag active: ", target);
	return 0;
    }
    
    depfile = targetdep(target);
    f = fopen(depfile, "r");
    if (!f) {
	dprint2("Rebuild, depfile cannot be opened: ", target);
	return 0;
    }
    dir_fd = keepdir();

    while (ok && !feof(f)) {
	char line[4096];
	char *hash = line + 1;
	char *timestamp = line + 1 + HASH_CHARS + 1;
	char *filename = line + 1 + HASH_CHARS + 1 + 16 + 1;

	if (fgets(line, sizeof line, f)) {
	    line[strlen(line)-1] = 0; // strip \n
	    switch (line[0]) {
	    case '-':  // must not exist
		if (access(line+1, F_OK) == 0) {
		    // Note: better message needed
		    dprint4("Rebuild, dependency ", line+1, " must not exist: ", target);
		    ok = 0;
		}
		break;
	    case '=':  // compare hash
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
		    dprint4("Rebuild, cannot open dependency ", filename, " for reading: ", target);
		    ok = 0;
		} else {
		    /* if (strncmp(timestamp, datefile(fd), 16) != 0 && */
		    /* 	strncmp(hash, hashtohex(hashfile(fd)), HASH_CHARS) != 0) */
		    /* 	ok = 0; */
		    if (strncmp(timestamp, datefile(fd), 16) != 0) {
			ok = 0;
			dprint4("Rebuild, timestamp mismatch for ", filename, ": ", target);
		    } else if (strncmp(hash, hashtohex(hashfile(fd)), HASH_CHARS) != 0) {
			ok = 0;
			dprint4("Rebuild, hash mismatch for ", filename, ": ", target);
		    }
		    close(fd);
		}
		// hash is good, recurse into dependencies
		if (ok && strcmp(target, filename) != 0) {
		    ok = check_deps(filename);
		    if (!ok)
			dprint4("Rebuild, dependency needs rebuild for ", filename, ": ", target);
		    fchdir(dir_fd);
		}
		break;
	    case '!':  // always rebuild
		// Note: better message needed
		ok = 0;
		dprint2("Rebuild, forced by ! line: ", target);
		break;
	    default:  // dep file broken, lets recreate it
		ok = 0;
		dprint2("Rebuild, invalid dep file line: ", target);
	    }
	} else {
	    if (!feof(f)) {
		ok = 0;
		dprint2("Rebuild, error while reading dep file: ", target);
		break;
	    }
	}
    }

    fclose(f);

    close(dir_fd);
    dir_fd = old_dir_fd;
    if (ok)
	dprint2("Not rebuilt, already up-to-date: ", target);
    return ok;
}

char uprel[PATH_MAX];

void
compute_uprel()
{
    char *u = uprel;
    char *dp = getenv("REDO_DIRPREFIX");

    *u = 0;
    while (dp && *dp) {
	*u++ = '.';
	*u++ = '.';
	*u++ = '/';
	*u = 0;
	dp = strchr(dp + 1, '/');
    }
}

static int
write_dep(int dep_fd, char *file)
{
    int fd = open(file, O_RDONLY);
    if (fd < 0)
	return 0;
    dprintf(dep_fd, "=%s %s %s%s\n",
	    hashtohex(hashfile(fd)), datefile(fd), (*file == '/' ? "" : uprel), file);
    close(fd);
    return 0;
}

// dofile doesn't contain /
// target can contain /
static char *
redo_basename(char *dofile, char *target)
{
    static char buf[PATH_MAX];
    int stripext = 0;
    char *s;

    if (strncmp(dofile, "default.", 8) == 0)
	for (stripext = -1, s = dofile; *s; s++)
	    if (*s == '.')
		stripext++;

    strncpy(buf, target, sizeof buf);
    while (stripext-- > 0) {
	if (strchr(buf, '.')) {
	    char *e = strchr(buf, '\0');
	    while (*--e != '.')
		*e = 0;
	    *e = 0;
	}
    }

    return buf;
}

pid_t
new_waitjob(int lock_fd, int implicit)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		perror("fork");
		vacate(implicit);
		exit(-1);
	}
	if (pid == 0) { // child
	    if (lockf(lock_fd, F_LOCK, 0)==-1) {
		perror("new_waitjob: lockf");
		exit(100);
	    }
	    close(lock_fd);
	    exit(0);
	}
	struct job *job = malloc(sizeof *job);
	if (!job)
	    exit(-1);
	job->target = 0;
	job->pid = pid;
	job->lock_fd = lock_fd;
	job->implicit = implicit;
	
	insert_job(job);

	return pid;
}

static void
run_script(char *target, int implicit)
{
    char temp_depfile[PATH_MAX];
    char temp_target[PATH_MAX-1]; // Just outwitting the compile string length check :-0
    char cwd[PATH_MAX], rel_target[PATH_MAX], rel_temp_target[PATH_MAX];
    char *orig_target = target;
    int old_dep_fd = dep_fd;
    int target_fd;
    char *dofile, *dirprefix;
    pid_t pid, my_pid=getpid();
    struct stat st;
    mode_t target_mode;

    target = targetchdir(target);
    
    dofile = find_dofile(target);
    if (!dofile) {
	fprintf(stderr, "no dofile for %s.\n", target);
	exit(1);
    }

    fprintf(stderr, "redo %s\n", target);
    
    // Testing: deadlock checking:
    //  On first instance we set an environment variable REDO_/HASH_OF_TARGET_ABSOLUTE_PATH/=PID
    //  This can only be seen by a child process and if we see it, we break the cycle.
    if (getcwd(cwd, sizeof cwd) == NULL) die2("getcwd", cwd, 100);
    strncat(cwd, target, PATH_MAX-strlen(target));
    char target_hash[PATH_MAX];
    snprintf(target_hash, sizeof target_hash, "REDO_%s",
	     hashtohex(siphash2_4_128(cwd, strlen(cwd), redo_siphash_key)));
    char * target_hash_env = getenv(target_hash);
    if (target_hash_env) {
	fprintf(stderr, "error: cyclic dependency %s [%s]\n", orig_target, target_hash_env);
	exit(-1);
    }
    // allow parallel building
    check_or_create_dir(redo_base(target));
    int lock_fd = open(targetlock(target), O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (lock_fd<0) die2("failed to create: ", targetlock(target), 111);
    if (lockf(lock_fd, F_TLOCK, 0) < 0) {
	if (errno == EAGAIN) {
	    pid = new_waitjob(lock_fd, implicit);
	    if (dflag) {
		fprintf(stderr, "%*.*s wait job %s [%d]\n",
			level, level, " ", orig_target, pid);
	    }
	    return;
	} else {
	    perror("run_script: lockf");
	    exit(111);
	}
    }
    
    // write dependencies
    strncpy(temp_depfile, targettmp(".dep", my_pid, target), sizeof temp_depfile);
    // dep_fd is global
    check_or_create_dir(redo_base(temp_depfile));
    dep_fd = open(temp_depfile, O_CREAT|O_WRONLY|O_EXCL, 0600);
    if (dep_fd==-1)
	die2("could not create temp_depfile: %s", temp_depfile, 100);
    write_dep(dep_fd, dofile);

    // prepare the $3 file
    strncpy(temp_target, targettmp(".tmp", my_pid, target), sizeof temp_target);
    if (stat(target, &st)==-1)
	target_mode = 0644;
    else
	target_mode = st.st_mode;
    check_or_create_dir(redo_base(temp_target));
    target_fd = open(temp_target, O_CREAT|O_RDWR|O_EXCL, target_mode);
    if (target_fd==-1)
	die2("could not create temp_targetfile: %s", temp_target, 100);
    
    // .do files are called from the directory they reside in, we need to
    // prefix the arguments with the path from the dofile to the target
    if (getcwd(cwd, sizeof cwd) == NULL) die2("getcwd", cwd, 100);
    dirprefix = strchr(cwd, '\0');
    dofile += 2;  // find_dofile starts with ./ always
    while (strncmp(dofile, "../", 3) == 0) {
	chdir("..");
	dofile += 3;
	while (*--dirprefix != '/')
	    ;
    }
    if (*dirprefix)
	dirprefix++;

    if (setenv("REDO_DIRPREFIX", dirprefix, 1)) die2("setenv REDO_DIRPREFIX", dirprefix, 100);

    snprintf(rel_target, sizeof rel_target,
	     "%s%s%s", dirprefix, (*dirprefix ? "/" : ""), target);
    
    snprintf(rel_temp_target, sizeof rel_temp_target,
	     "%s%s%s", dirprefix, (*dirprefix ? "/" : ""), temp_target);

    pid = fork();
    if (pid < 0) {
	perror("fork");
	vacate(implicit);
	exit(-1);
    } else if (pid == 0) { // child
	/*
	  djb-style default.o.do:
	  $1	   foo.o
	  $2	   foo
	  $3	   whatever.tmp

	  $1	   all
	  $2	   all (!!)
	  $3	   whatever.tmp

	  $1	   subdir/foo.o
	  $2	   subdir/foo
	  $3	   subdir/whatever.tmp
	*/
	char *basename = redo_basename(dofile, rel_target);
	if (old_dep_fd > 0) {
	    // Testing
	    if (dflag)
		fprintf(stderr, "warning run_script: global dep_fd > 0: %d\n", old_dep_fd);
	    close(old_dep_fd);
	}
	close(lock_fd);
	setenvfd("REDO_DEP_FD", dep_fd);
	setenvfd("REDO_LEVEL", level + 1);
	// Testing: deadlock checking
	setenvfd(target_hash, getpid());
	
	if (dup2(target_fd, 1)==-1) die("run_script, dup2", 100);
	if (access(dofile, X_OK) != 0)   // run -x files with /bin/sh
	    execl("/bin/sh", "/bin/sh", xflag > 0 ? "-ex" : "-e",
		  dofile, rel_target, basename, rel_temp_target, (char *)0);
	else
	    execl(dofile,
		  dofile, rel_target, basename, rel_temp_target, (char *)0);
	vacate(implicit);
	exit(-1);
    } else {
	struct job *job = malloc(sizeof *job);
	if (!job)
	    exit(-1);
	
	close(target_fd);
	close(dep_fd);
	dep_fd = old_dep_fd;

	job->pid = pid;
	job->lock_fd = lock_fd;
	job->target = orig_target;
	job->temp_depfile = strdup(temp_depfile);
	job->temp_target = strdup(temp_target);
	job->implicit = implicit;

	insert_job(job);
	
	if (vflag)
	    fprintf(stderr, "%*.*sredo %s # %s [%d]\n",
		    level, level, " ", orig_target, dofile, pid);
    }
}

static void
redo_ifchange(int targetc, char *targetv[])
{
    pid_t pid;
    int status;
    struct job *job;

    int targeti = 0;

    // XXX
    char skip[targetc];


    create_pool();

    // check all targets whether needing rebuild
    for (targeti = 0; targeti < targetc; targeti++)
	skip[targeti] = check_deps(targetv[targeti]);

    targeti = 0;
    while (1) {
	int procured = 0;
	if (targeti < targetc) {
	    char *target = targetv[targeti];

	    if (skip[targeti]) {
		targeti++;
		continue;
	    }

	    int implicit = implicit_jobs > 0;
	    if (try_procure()) {
		procured = 1;
		targeti++;
		run_script(target, implicit);
	    }
	}

	pid = waitpid(-1, &status, procured ? WNOHANG : 0);

	if (pid == 0)
	    continue;  // nohang

	if (pid < 0) {
	    if (errno == ECHILD && targeti < targetc)
		continue;   // no child yet???
	    else
		break;   // no child left
	}

	if (WIFEXITED(status))
	    status = WEXITSTATUS(status);

	job = find_job(pid);

	if (!job)
	    exit(-1);  // we're completely corrupted, go suicide
		
	remove_job(job);

	if (job->target) { // ToDo: what jobs don't have targets (or empty targets)?
	    // ToDo: what if job exit status < 0?
	    if (status > 0) {
		remove_temp(job->temp_depfile);
		remove_temp(job->temp_target);
	    } else {
		struct stat st;
		char *target = targetchdir(job->target);
		char *depfile = targetdep(target);
		int dfd;

		// Note: what if.. we can't open it?
		dfd = open(job->temp_depfile, O_WRONLY | O_APPEND);
				
		if (stat(job->temp_target, &st)) {
		    // Ohh: can't access produced output!
		    perror(job->temp_target);
		    remove_temp(job->temp_target);
		    // ToDo: ahmmm, we leave old target alone and do as if it were not here?
		    redo_ifcreate(dfd, target);
		} else {
		    if (st.st_size) {
			rename_temp(job->temp_target, target);
			write_dep(dfd, target);
		    }
		    else {
			remove_temp(job->temp_target);
			dprintf(dfd, "!\n");
		    }
		}
		close(dfd);
		rename_temp(job->temp_depfile, depfile);
		remove_temp(targetlock(target));
	    }
	}

	if (!job->target)
	    job->target = (char*) "waiting..";
	if (dflag)
	    fprintf(stderr, "%*.*s finish %s [%d]\n",
		    level, level, " ", job->target, pid);

	close(job->lock_fd);
	
	vacate(job->implicit);

	if (kflag < 0 && status > 0) {
	    fprintf(stderr, "failed with status %d [%d]\n", status, pid);
	    exit(status);
	}
    }
}

static void
record_deps(int targetc, char *targetv[])
{
    int targeti = 0;
    int fd;

    dep_fd = envfd("REDO_DEP_FD");
    
    if (dep_fd < 0)
	return;

    fchdir(dir_fd);

    for (targeti = 0; targeti < targetc; targeti++) {
	fd = open(targetv[targeti], O_RDONLY);
	if (fd < 0)
	    continue;
	write_dep(dep_fd, targetv[targeti]);
	close(fd);
    }
}

int
main(int argc, char *argv[])
{
    char *program;
    int opt, i;

    level = envfd("REDO_LEVEL");
    if (level < 0)
	level = 0;

    if ((program = strrchr(argv[0], '/')))
	program++;
    else
	program = argv[0];

    /* jdebp:
       -s, --silent, --quiet .. Operate quietly.
       -k, --keep-going .. Continue with the next target if a .do script fails
       -d, --debug .. Output debugging information.
       --verbose, -p, --print .. Display information about the database.
       -j n, --jobs n .. Allow multiple jobs to run in parallel.
       -C, --directory .. Change to directory before doing anything.
       --jobserver-fds fd-list .. Provide the file descriptor numbers of the jobserver pipe.
       --redoparent-fd fd .. Provide the file descriptor number of the redo database current parent file.
       REDOFLAGS
       MAKEFFLAGS
       MFLAGS
       redo-c .. jdebp
       -d .. -d, --debug
       -f ..
       -k .. -k, --keep-going
       -v .. --verbose, -p, --print
       -V .. -s, --silent, --quiet
       -x ..
       -X ..
       -j n .. -j n, --jobs n
       -C path .. -C path , --directory path
    */

    opterr = 0;
    while ((opt = getopt(argc, argv, "+dfkvVxXj:C:")) != -1) {
	switch (opt) {
	case 'd':
	    setenvfd("REDO_DEBUG", 1);
	    break;
	case 'f':
	    setenvfd("REDO_FORCE", 1);
	    break;
	case 'k':
	    setenvfd("REDO_KEEP_GOING", 1);
	    break;
	case 'v':
	    setenvfd("REDO_VERBOSE", 1);
	    break;
	case 'V':
	    setenvfd("REDO_VERBOSE", 0);
	    break;
	case 'x':
	    setenvfd("REDO_TRACE", 1);
	    break;
	case 'X':
	    setenvfd("REDO_TRACE", 0);
	    break;
	case 'j':
	    if(setenv("JOBS", optarg, 1)) die("setenv JOBS", 100);
	    break;
	case 'C':
	    if (chdir(optarg) < 0) {
		perror("chdir");
		exit(-1);
	    }
	    break;
	case '?':
	    /* handle jdbp long options */
	    if(!strcmp(argv[optind-1],"--silent")||!strcmp(argv[optind-1],"--quiet")) {
		setenvfd("REDO_VERBOSE", 0);
		setenvfd("REDO_DEBUG", 0);
		break;
	    }
	    if(!strcmp(argv[optind-1],"--keep-going")) {
		setenvfd("REDO_KEEP_GOING", 1);
		break;
	    }
	    if(!strcmp(argv[optind-1],"--debug")) {
		setenvfd("REDO_DEBUG", 1);
		break;
	    }
	    if(!strcmp(argv[optind-1],"--verbose")||!strcmp(argv[optind-1],"--print")) {
		setenvfd("REDO_VERBOSE", 1);
		break;
	    }
	    if(!strcmp(argv[optind-1],"--jobs")&&argc-optind) {
		if(setenv("JOBS", argv[optind++], 1)) die("setenv JOBS", 100);
		break;
	    }
	    if(!strcmp(argv[optind-1],"--directory")&&argc-optind) {
		if (chdir(argv[optind++]) < 0) {
		    perror("chdir");
		    exit(-1);
		}
		break;
	    }
	    /* yes, we want to fall through here */
	default:
	    fprintf(stderr, "Usage: %s [-dfkvVxX] [-Cdir] [-jN] [TARGETS...]\n\n", program);
	    fprintf(stderr, "%s %s\n", program, version);
	    exit(1);
	}
    }
    argc -= optind;
    argv += optind;

    fflag = envfd("REDO_FORCE");
    kflag = envfd("REDO_KEEP_GOING");
    if ((xflag = envfd("REDO_TRACE"))==-1)
	xflag = 0;
    if ((vflag = envfd("REDO_VERBOSE"))==-1)
	vflag = 0;
    if ((dflag = envfd("REDO_DEBUG"))==-1)
	dflag = 0;

    dir_fd = keepdir();

    if (strcmp(program, "redo") == 0) {
	char all[] = "all";
	char *argv_def[] = { all };

	if (argc == 0) {
	    argc = 1;
	    argv = argv_def;
	}

	fflag = 1;
	redo_ifchange(argc, argv);
	procure();
    } else if (strcmp(program, "redo-ifchange") == 0) {
	compute_uprel();
	redo_ifchange(argc, argv);
	record_deps(argc,argv);
	procure();
    } else if (strcmp(program, "redo-ifcreate") == 0) {
	for (i = 0; i < argc; i++)
	    redo_ifcreate(dep_fd, argv[i]);
    } else if (strcmp(program, "redo-always") == 0) {
	if (dep_fd==-1) {
	    fprintf(stderr, "error: redo-always must be invoked from within .do file\n");
	    exit(-1);
	}
	dprintf(dep_fd, "!\n");
    } else if (strcmp(program, "redo-hash") == 0) {
	for (i = 0; i < argc; i++)
	    write_dep(1, argv[i]);
    } else {
	fprintf(stderr, "not implemented %s\n", program);
	exit(-1);
    }

    return 0;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * End:
 */
