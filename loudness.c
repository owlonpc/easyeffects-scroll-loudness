#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "config.h"

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (*fmt && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

static pid_t
findpid(const char *name)
{
	DIR *d = opendir("/proc");
	if (!d)
		return -1;

	struct dirent *ent;
	while ((ent = readdir(d))) {
		if (ent->d_type != DT_DIR)
			continue;

		pid_t pid = atoi(ent->d_name);
		if (pid <= 0)
			continue;

		char path[64];
		snprintf(path, sizeof path, "/proc/%d/comm", pid);

		FILE *f = fopen(path, "r");
		if (!f)
			continue;

		char comm[32];
		if (fgets(comm, sizeof comm, f)) {
			comm[strcspn(comm, "\n")] = 0;
			if (strcmp(comm, name) == 0) {
				fclose(f);
				closedir(d);
				return pid;
			}
		}

		fclose(f);
	}

	closedir(d);
	return -1;
}

static uintptr_t
findpattern(pid_t pid, size_t patternlen, const uint8_t pattern[patternlen])
{
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/maps", pid);

	FILE *f = fopen(path, "r");
	if (!f)
		return 0;

	char line[256];
	while (fgets(line, sizeof line, f)) {
		if (!strstr(line, "[heap]"))
			continue;

		uintptr_t start, end;
		sscanf(line, "%lx-%lx", &start, &end);

		size_t bufsize = 4096;
		uint8_t buf[bufsize];

		for (uintptr_t addr = start; addr < end; addr += bufsize) {
			struct iovec lvec = { buf, bufsize };
			struct iovec rvec = { (void *)addr, bufsize };

			ssize_t n = process_vm_readv(pid, &lvec, 1, &rvec, 1, 0);
			if (n <= 0)
				continue;

			for (size_t i = 0; i <= n - patternlen; i++)
				if (!memcmp(buf + i, pattern, patternlen)) {
					fclose(f);
					return addr + i;
				}
		}

		break;
	}

	fclose(f);
	return 0;
}

static int
checkalive(pid_t pid)
{
	char path[64], comm[32];
	snprintf(path, sizeof path, "/proc/%d/comm", pid);

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;

	ssize_t n = read(fd, comm, sizeof comm - 1);
	close(fd);

	if (n <= 0)
		return false;

	comm[n - 1] = '\0';
	return !strcmp(comm, "easyeffects");
}

static bool
readf32(pid_t pid, uintptr_t addr, float *val)
{
	struct iovec lvec = { val, sizeof *val };
	struct iovec rvec = { (void *)addr, sizeof *val };

	return process_vm_readv(pid, &lvec, 1, &rvec, 1, 0) >= 0;
}

static bool
writef32(pid_t pid, uintptr_t addr, float val)
{
	struct iovec lvec = { &val, sizeof val };
	struct iovec rvec = { (void *)addr, sizeof val };

	return process_vm_writev(pid, &lvec, 1, &rvec, 1, 0) >= 0;
}

static float
clamp(float x, float lo, float hi)
{
	return x < lo ? lo : (hi < x ? hi : x);
}

int
main(void)
{
	int fd = open(mousepath, O_RDONLY);
	if (fd < 0)
		die("could not open mouse device:");

retry:;
	pid_t pid = findpid("easyeffects");
	if (pid < 0) {
		usleep(100 * 1000);    // 100ms
		goto retry;
	}

	static const uint8_t pattern[] = { 0x76, 0x6F, 0x6C, 0x75, 0x6D, 0x65, 0x00, 0x00,
		                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	uintptr_t addr = findpattern(pid, sizeof pattern, pattern) + 0x10;
	while (addr == 0x10) {
		usleep(100 * 1000);    // 100 ms
		addr = findpattern(pid, sizeof pattern, pattern) + 0x10;
	}

	for (;;) {
		struct input_event ev;
		read(fd, &ev, sizeof ev);

		if (!(ev.type == EV_REL && ev.code == REL_WHEEL))
			continue;

		float volume;
		if (!checkalive(pid) || !readf32(pid, addr, &volume) ||
		    !writef32(pid, addr, clamp(volume + ev.value, -83.f, 7.f)))
			goto retry;
	}

	__builtin_unreachable();
}
