#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "config.h"

static float
clamp(float x, float lo, float hi)
{
	return x < lo ? lo : (hi < x ? hi : x);
}

static int
connectsocket(void)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, "/tmp/EasyEffectsServer", sizeof addr.sun_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static bool
getvolume(int sockfd, float *vol)
{
	const char msg[] = "get_property:output:loudness:0:volume\n";
	if (write(sockfd, msg, sizeof msg) < 0)
		return false;

	char buf[256];
	ssize_t n = read(sockfd, buf, sizeof buf - 1);
	if (n <= 0)
		return false;

	buf[n] = '\0';
	*vol = atof(buf);
	return true;
}

static bool
setvolume(int sockfd, float vol)
{
	char msg[256];
	snprintf(msg, sizeof msg, "set_property:output:loudness:0:volume:%.2f\n", vol);
	return write(sockfd, msg, strlen(msg)) > 0;
}

int
main(void)
{
	int mousefd = -1, sockfd = -1;

	for (;;) {
		if (mousefd < 0 && (mousefd = open(mousepath, O_RDONLY)) < 0) {
			usleep(100 * 1000);
			continue;
		}

		struct input_event ev;
		if (read(mousefd, &ev, sizeof ev) < 0) {
			close(mousefd);
			mousefd = -1;
			continue;
		}

		if (!(ev.type == EV_REL && ev.code == REL_WHEEL))
			continue;

		if (sockfd < 0 && (sockfd = connectsocket()) < 0) {
			usleep(100 * 1000);
			continue;
		}

		float vol;
		if (!getvolume(sockfd, &vol) || !setvolume(sockfd, clamp(vol + ev.value, -83.f, 7.f))) {
			close(sockfd);
			sockfd = -1;
		}
	}

	__builtin_unreachable();
}
