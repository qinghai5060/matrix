/*
 * syscall.c
 */

#include <syscall.h>
#include <types.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/stat.h>

/* Definition of the system calls */
DEFN_SYSCALL1(putstr, 0, const char *)
DEFN_SYSCALL3(open, 1, const char *, int, int)
DEFN_SYSCALL3(read, 2, int, char *, int)
DEFN_SYSCALL3(write, 3, int, char *, int)
DEFN_SYSCALL1(close, 4, int)
DEFN_SYSCALL1(exit, 5, int)
DEFN_SYSCALL2(gettimeofday, 6, void *, void *)
DEFN_SYSCALL2(settimeofday, 7, const void *, const void *)
DEFN_SYSCALL3(readdir, 8, int, int, void *)
DEFN_SYSCALL3(lseek, 9, int, int, int)
DEFN_SYSCALL2(lstat, 10, int, void *)
DEFN_SYSCALL1(chdir, 11, const char *)
DEFN_SYSCALL2(mkdir, 12, const char *, uint32_t)
DEFN_SYSCALL2(gethostname, 13, char *, size_t)
DEFN_SYSCALL2(sethostname, 14, const char *, size_t)
DEFN_SYSCALL0(getuid, 15)
DEFN_SYSCALL1(setuid, 16, uint32_t)
DEFN_SYSCALL0(getgid, 17)
DEFN_SYSCALL1(setgid, 18, uint32_t)
DEFN_SYSCALL0(getpid, 19)
DEFN_SYSCALL1(sleep, 20, uint32_t)
DEFN_SYSCALL4(create_process, 21, const char *, const char **, int, int)
DEFN_SYSCALL1(waitpid, 22, int)
DEFN_SYSCALL1(unit_test, 23, uint32_t)
DEFN_SYSCALL0(clear, 24)
DEFN_SYSCALL0(shutdown, 25)
DEFN_SYSCALL2(syslog, 26, char *, size_t)


int open(const char *file, int flags, int mode)
{
	return mtx_open(file, flags, mode);
}

int read(int fd, char *buf, int len)
{
	return mtx_read(fd, buf, len);
}

int write(int fd, char *buf, int len)
{
	return mtx_write(fd, buf, len);
}

int close(int fd)
{
	return mtx_close(fd);
}

int exit(int val)
{
	return mtx_exit(val);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return mtx_gettimeofday(tv, tz);
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	return mtx_settimeofday(tv, tz);
}

int readdir(int fd, int index, struct dirent *entry)
{
	return mtx_readdir(fd, index, entry);
}

int lseek(int fd, int offset, int whence)
{
	return mtx_lseek(fd, offset, whence);
}

int lstat(int fd, struct stat *s)
{
	return mtx_lstat(fd, s);
}

int chdir(const char *path)
{
	return mtx_chdir(path);
}

int mkdir(const char *path, int mode)
{
	return mtx_mkdir(path, mode);
}

int gethostname(char *name, size_t len)
{
	return mtx_gethostname(name, len);
}

int sethostname(const char *name, size_t len)
{
	return mtx_sethostname(name, len);
}

int getuid()
{
	return mtx_getuid();
}

int setuid(uid_t uid)
{
	return mtx_setuid(uid);
}

int getgid()
{
	return mtx_getgid();
}

int setgid(gid_t gid)
{
	return mtx_setgid(gid);
}

int getpid()
{
	return mtx_getpid();
}

int sleep(int ms)
{
	return mtx_sleep(ms);
}

int create_process(const char *path, const char *args[], int flags, int priority)
{
	return mtx_create_process(path, args, flags, priority);
}

pid_t wait(int *status)
{
	return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	return mtx_waitpid(pid);
}

int unit_test(uint32_t round)
{
	return mtx_unit_test(round);
}

int clear()
{
	return mtx_clear();
}

int shutdown()
{
	return mtx_shutdown();
}

int syslog(char *buf, size_t len)
{
	return mtx_syslog(buf, len);
}
