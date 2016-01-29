#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef __MINGW32__
#include <argp.h>
#endif
#include <lkl.h>
#include <lkl_host.h>
#ifndef __MINGW32__
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#else
#include <windows.h>
#endif

#include "test-common.h"

static struct cl_args {
	int printk;
	const char *disk_filename;
	const char *tap_ifname;
	const char *fstype;
} cla;

static struct cl_option {
	const char *long_name;
	char short_name;
	const char *help;
	int has_arg;
} options[] = {
	{"enable-printk", 'p', "show Linux printks", 0},
	{"disk-file", 'd', "disk file to use", 1},
	{"net-tap", 'n', "tap interface to use", 1},
	{"type", 't', "filesystem type", 1},
	{0},
};

static int parse_opt(int key, char *arg)
{
	switch (key) {
	case 'p':
		cla.printk = 1;
		break;
	case 'd':
		cla.disk_filename = arg;
		break;
	case 'n':
		cla.tap_ifname = arg;
		break;
	case 't':
		cla.fstype = arg;
		break;
	default:
		return -1;
	}

	return 0;
}

void printk(const char *str, int len)
{
	int ret __attribute__((unused));

	if (cla.printk)
		ret = write(STDOUT_FILENO, str, len);
}

#ifndef __MINGW32__

#define sleep_ns 87654321
int test_nanosleep(char *str, int len)
{
	struct lkl_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = sleep_ns,
	};
	struct timespec start, stop;
	long delta;
	long ret;

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = lkl_sys_nanosleep(&ts, NULL);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	delta = 1e9*(stop.tv_sec - start.tv_sec) +
		(stop.tv_nsec - start.tv_nsec);

	snprintf(str, len, "%ld", delta);

	if (ret == 0 && delta > sleep_ns * 0.9 && delta < sleep_ns * 1.1)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}
#endif

int test_getpid(char *str, int len)
{
	long ret;

	ret = lkl_sys_getpid();

	snprintf(str, len, "%ld", ret);

	if (ret == 1)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

#define access_rights 0721

int test_creat(char *str, int len)
{
	long ret;

	ret = lkl_sys_creat("/file", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_close(char *str, int len)
{
	long ret;

	ret = lkl_sys_close(0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_failopen(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file2", 0, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == -LKL_ENOENT)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_umask(char *str, int len)
{
	long ret, ret2;

	ret = lkl_sys_umask(0777);

	ret2 = lkl_sys_umask(0);

	snprintf(str, len, "%lo %lo", ret, ret2);

	if (ret > 0 && ret2 == 0777)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_open(char *str, int len)
{
	long ret;

	ret = lkl_sys_open("/file", LKL_O_RDWR, 0);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static const char write_test[] = "test";

int test_write(char *str, int len)
{
	long ret;

	ret = lkl_sys_write(0, write_test, sizeof(write_test));

	snprintf(str, len, "%ld", ret);

	if (ret == sizeof(write_test))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_lseek(char *str, int len)
{
	long ret;

	ret = lkl_sys_lseek(0, 0, LKL_SEEK_SET);

	snprintf(str, len, "%zd ", ret);

	if (ret >= 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_read(char *str, int len)
{
	char buf[10] = { 0, };
	long ret;

	ret = lkl_sys_read(0, buf, sizeof(buf));

	snprintf(str, len, "%ld %s", ret, buf);

	if (ret == sizeof(write_test) && strcmp(write_test, buf) == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_fstat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_fstat(0, &stat);

	snprintf(str, len, "%ld %o %zd", ret, stat.st_mode, stat.st_size);

	if (ret == 0 && stat.st_size == sizeof(write_test) &&
	    stat.st_mode == (access_rights | LKL_S_IFREG))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_mkdir(char *str, int len)
{
	long ret;

	ret = lkl_sys_mkdir("/mnt", access_rights);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_stat(char *str, int len)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_stat("/mnt", &stat);

	snprintf(str, len, "%ld %o", ret, stat.st_mode);

	if (ret == 0 && stat.st_mode == (access_rights | LKL_S_IFDIR))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static union lkl_disk disk;
static int disk_id = -1;

int test_disk_add(char *str, int len)
{
#ifdef __MINGW32__
	disk.handle = CreateFile(cla.disk_filename, GENERIC_READ | GENERIC_WRITE,
			       0, NULL, OPEN_EXISTING, 0, NULL);
	if (!disk.handle)
#else
	disk.fd = open(cla.disk_filename, O_RDWR);
	if (disk.fd < 0)
#endif
		goto out_unlink;

	disk_id = lkl_disk_add(disk);
	if (disk_id < 0)
		goto out_close;

	goto out;

out_close:
#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

out_unlink:
#ifdef __MINGW32__
	DeleteFile(cla.disk_filename);
#else
	unlink(cla.disk_filename);
#endif

out:
	snprintf(str, len, "%x %d", disk.fd, disk_id);

	if (disk_id >= 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

#ifndef __MINGW32__
static int netdev_id = -1;

int test_netdev_add(char *str, int len)
{
	union lkl_netdev netdev = { -1, };
	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};
	int ret;

	strncpy(ifr.ifr_name, cla.tap_ifname, IFNAMSIZ);

	ret = open("/dev/net/tun", O_RDWR|O_NONBLOCK);
	if (ret < 0)
		goto out;

	netdev.fd = ret;

	ret = ioctl(netdev.fd, TUNSETIFF, &ifr);
	if (ret < 0)
		goto out;

	ret = lkl_netdev_add(netdev, NULL);
	if (ret < 0)
		goto out;

	netdev_id = ret;

out:
	snprintf(str, len, "%d %d %d", ret, netdev.fd, netdev_id);
	return ret >= 0 ? TEST_SUCCESS : TEST_FAILURE;
}

static int test_netdev_ifup(char *str, int len)
{
	long ret;
	int ifindex = -1;

	ret = lkl_netdev_get_ifindex(netdev_id);
	if (ret < 0)
		goto out;
	ifindex = ret;

	ret = lkl_if_up(ifindex);

out:
	snprintf(str, len, "%ld %d", ret, ifindex);

	if (!ret)
		return TEST_SUCCESS;
	return TEST_FAILURE;
}

static int test_pipe2(char *str, int len)
{
	int pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	const char msg[] = "Hello world!";
	int msg_len_bytes = strlen(msg) + 1;
	int cmp_res = 0;

	if (lkl_sys_pipe2(pipe_fds, O_NONBLOCK)) {
		perror_msg("pipe2", str);
		return TEST_FAILURE;
	}

	if (lkl_sys_write(pipe_fds[WRITE_IDX], msg, msg_len_bytes) !=
		msg_len_bytes) {
		perror_msg("write", str);
		return TEST_FAILURE;
	}

	if (lkl_sys_read(pipe_fds[READ_IDX], str, msg_len_bytes) !=
		msg_len_bytes) {
		perror_msg("read", str);
		return TEST_FAILURE;
	}

	if ((cmp_res = memcmp(msg, str, msg_len_bytes))) {
		snprintf(str, MAX_MSG_LEN, "%d", cmp_res);
		return TEST_FAILURE;
	}

	if (lkl_sys_close(pipe_fds[0]) || lkl_sys_close(pipe_fds[1])) {
		perror_msg("close", str);
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

static int test_epoll(char *str, int len)
{
	int epoll_fd, pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	struct lkl_epoll_event wait_on, read_result;
	const char msg[] = "Hello world!";

	memset(&wait_on, 0, sizeof(wait_on));
	memset(&read_result, 0, sizeof(read_result));

	if (lkl_sys_pipe2(pipe_fds, O_NONBLOCK)) {
		perror_msg("pipe2", str);
		return TEST_FAILURE;
	}

	if ((epoll_fd = lkl_sys_epoll_create(1)) == -1) {
		perror_msg("lkl_sys_epoll_create", str);
		return TEST_FAILURE;
	}

	wait_on.events = EPOLLIN | EPOLLOUT;
	wait_on.data = pipe_fds[READ_IDX];

	if (lkl_sys_epoll_ctl(epoll_fd, LKL_EPOLL_CTL_ADD, pipe_fds[READ_IDX], &wait_on)) {
		perror_msg("epoll_ctl", str);
		return TEST_FAILURE;
	}

	/* Shouldn't be ready before we have written something */
	if (lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0)) {
		perror_msg("epoll_wait", str);
		return TEST_FAILURE;
	}

	if (lkl_sys_write(pipe_fds[WRITE_IDX], msg, strlen(msg) + 1) == -1) {
		perror_msg("write", str);
		return TEST_FAILURE;
	}

	/* We expect exactly 1 fd to be ready immediately */
	if (lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0) != 1) {
		perror_msg("epoll_wait", str);
		return TEST_FAILURE;
	}

	/* Already tested reading from pipe2 so no need to do it
	 * here */
	snprintf(str, MAX_MSG_LEN, "%s", msg);

	return TEST_SUCCESS;
}
#endif /* __MINGW32__ */

static char mnt_point[32];

static int test_mount(char *str, int len)
{
	long ret;

	ret = lkl_mount_dev(disk_id, cla.fstype, 0, NULL, mnt_point,
			    sizeof(mnt_point));

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_chdir(char *str, int len)
{
	long ret;

	ret = lkl_sys_chdir(mnt_point);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int dir_fd;

static int test_opendir(char *str, int len)
{
	dir_fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);

	snprintf(str, len, "%d", dir_fd);

	if (dir_fd > 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_getdents64(char *str, int len)
{
	long ret;
	char buf[1024], *pos;
	struct lkl_linux_dirent64 *de;
	int wr;

	de = (struct lkl_linux_dirent64 *)buf;
	ret = lkl_sys_getdents64(dir_fd, de, sizeof(buf));

	wr = snprintf(str, len, "%d ", dir_fd);
	str += wr;
	len -= wr;

	if (ret < 0)
		return TEST_FAILURE;

	for (pos = buf; pos - buf < ret; pos += de->d_reclen) {
		de = (struct lkl_linux_dirent64 *)pos;

		wr = snprintf(str, len, "%s ", de->d_name);
		str += wr;
		len -= wr;
	}

	return TEST_SUCCESS;
}

static int test_umount(char *str, int len)
{
	long ret, ret2, ret3;

	ret = lkl_sys_close(dir_fd);

	ret2 = lkl_sys_chdir("/");

	ret3 = lkl_umount_dev(disk_id, 0, 1000);

	snprintf(str, len, "%ld %ld %ld", ret, ret2, ret3);

	if (!ret && !ret2 && !ret3)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_lo_ifup(char *str, int len)
{
	long ret;

	ret = lkl_if_up(1);

	snprintf(str, len, "%ld", ret);

	if (!ret)
		return TEST_SUCCESS;
	return TEST_FAILURE;
}

static struct cl_option *find_short_opt(char name)
{
	struct cl_option *opt;

	for (opt = options; opt->short_name != 0; opt++) {
		if (opt->short_name == name)
			return opt;
	}

	return NULL;
}

static struct cl_option *find_long_opt(const char *name)
{
	struct cl_option *opt;

	for (opt = options; opt->long_name; opt++) {
		if (strcmp(opt->long_name, name) == 0)
			return opt;
	}

	return NULL;
}

static void print_help(void)
{
	struct cl_option *opt;

	printf("usage:\n");
	for (opt = options; opt->long_name; opt++)
		printf("-%c, --%-20s %s\n", opt->short_name, opt->long_name,
		       opt->help);
}

static int parse_opts(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		struct cl_option *opt = NULL;

		if (argv[i][0] == '-') {
			if (argv[i][1] != '-')
				opt = find_short_opt(argv[i][1]);
			else
				opt = find_long_opt(&argv[i][2]);
		}

		if (!opt) {
			print_help();
			return -1;
		}

		if (parse_opt(opt->short_name, argv[i + 1]) < 0) {
			print_help();
			return -1;
		}

		if (opt->has_arg)
			i++;
	}

	return 0;
}

int main(int argc, char **argv)
{
	if (parse_opts(argc, argv) < 0)
		return -1;

	lkl_host_ops.print = printk;

	TEST(disk_add);
#ifndef __MINGW32__
	if (cla.tap_ifname)
		TEST(netdev_add);
#endif /* __MINGW32__ */
	lkl_start_kernel(&lkl_host_ops, 16 * 1024 * 1024, "");

	TEST(getpid);
	TEST(umask);
	TEST(creat);
	TEST(close);
	TEST(failopen);
	TEST(open);
	TEST(write);
	TEST(lseek);
	TEST(read);
	TEST(fstat);
	TEST(mkdir);
	TEST(stat);
#ifndef __MINGW32__
	TEST(nanosleep);
	if (netdev_id >= 0)
		TEST(netdev_ifup);
	TEST(pipe2);
	TEST(epoll);
#endif  /* __MINGW32__ */
	TEST(mount);
	TEST(chdir);
	TEST(opendir);
	TEST(getdents64);
	TEST(umount);
	TEST(lo_ifup);

	lkl_sys_halt();

	close(disk.fd);

	return g_test_pass;
}
