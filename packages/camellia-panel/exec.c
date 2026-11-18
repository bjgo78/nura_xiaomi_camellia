#define _GNU_SOURCE
/* fork + execvp with stdout/stderr in a pipe, read from an LVGL timer.
 * No shell is involved, so SSIDs and passwords need no quoting. */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lvgl.h"
#include "exec.h"

#define OUT_MAX 65536

struct job {
	pid_t pid;
	int fd;
	char *out;
	size_t len;
	exec_cb cb;
	void *user;
};

static void job_timer(lv_timer_t *t)
{
	struct job *j = lv_timer_get_user_data(t);
	char buf[4096];
	ssize_t n;
	int status = -1;

	for (;;) {
		n = read(j->fd, buf, sizeof(buf));
		if (n > 0) {
			size_t take = (size_t)n;
			if (j->len + take > OUT_MAX - 1)
				take = OUT_MAX - 1 - j->len;
			memcpy(j->out + j->len, buf, take);
			j->len += take;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return; /* more later */
		break; /* EOF or error */
	}
	close(j->fd);
	if (waitpid(j->pid, &status, 0) > 0 && WIFEXITED(status))
		status = WEXITSTATUS(status);
	else
		status = -1;
	j->out[j->len] = 0;
	lv_timer_delete(t);
	if (j->cb)
		j->cb(status, j->out, j->user);
	free(j->out);
	free(j);
}

int exec_async(const char *const argv[], exec_cb cb, void *user)
{
	int p[2];
	struct job *j;
	pid_t pid;

	if (pipe2(p, O_CLOEXEC))
		return -1;
	pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		int null = open("/dev/null", O_RDONLY);
		dup2(null, 0);
		dup2(p[1], 1);
		dup2(p[1], 2);
		setenv("LC_ALL", "C", 1);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(p[1]);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	j = calloc(1, sizeof(*j));
	j->pid = pid;
	j->fd = p[0];
	j->out = malloc(OUT_MAX);
	j->cb = cb;
	j->user = user;
	lv_timer_create(job_timer, 100, j);
	return 0;
}
