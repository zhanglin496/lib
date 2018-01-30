#include <stdio.h>
#include <string.h>


//util.c

//convert dns
//such as www.baidu.com to 3www5baidu3com
static int str2dns(const char *name, char *dns)
{
	#define MAX_TOKENS 64
	char *token[MAX_TOKENS];
	int i = 0, j = 0, k = 0;
	int len;

	strcpy(dns + 1, name);
	char *c = dns + 1;
	char **p = &c;

	while (*p && i < MAX_TOKENS) {
		token[i] = strsep(p, ".");
		i++;
	}

	/* host too long */
	if (i >= MAX_TOKENS)
		return -1;

	/* not have a . */
	if (i == 1)
		return -1; 

	for (j = 0; j < i; j++) {
		len = strlen(token[j]);

		/* DNS rfc require */
		if (len > 63) 
			return -1;
		/* illegal */
		if (!len)
			return -1;
		dns[k] = len;
		k += len + 1;
	}
	return 0;
}

static inline int atoi(char *s)
{
	int i = 0;
	while (isdigit(*s))
		i = i * 10 + *s++ - '0';
	return i;
}
