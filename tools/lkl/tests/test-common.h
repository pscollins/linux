#include <string.h>
#include <errno.h>
#include <stdio.h>

#define TEST_SUCCESS 1
#define TEST_FAILURE 0
#define MAX_MSG_LEN 60

#define TEST(name) {				\
	int ret = do_test(#name, test_##name);	\
	if (!ret) g_test_pass = -1;		\
	}

int g_test_pass = 0;

void perror_msg(char *to_perror, char *str);
int do_test(char *name, int (*fn)(char *, int));

