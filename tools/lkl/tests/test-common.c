#include "test-common.h"

void perror_msg(char *to_perror, char *str)
{
	/* No error handling here because we can't recover from it
	 * anyway */
	char *err_msg = strerror(errno);
	snprintf(str, MAX_MSG_LEN, "%s: error %d (%s)", to_perror, errno, err_msg);
}

int do_test(char *name, int (*fn)(char *, int))
{
	char str[MAX_MSG_LEN];
	int result;

	result = fn(str, sizeof(str));
	printf("%-20s %s [%s]\n", name,
		result == TEST_SUCCESS ? "passed" : "failed", str);
	return result;
}
