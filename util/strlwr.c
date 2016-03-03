#include <ctype.h>
#include <string.h>

char *strlwr(char *str)
{
	int len = strlen(str);
	for (int i = 0; i < len; ++i)
		str[i] = tolower(str[i]);

	return str;
}

