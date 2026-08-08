#include <stdio.h>
#include <stdint.h>

void dump(const uint8_t *data, const size_t size, const char *filename)
{
	FILE *fp = fopen(filename, "w");
	fwrite(data, 1, size, fp);
	fclose(fp);
}
