#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rcr(uint16_t *bp, const char *addr, const char *reg = "bp");
int rcl(uint16_t *bp, const char *addr, const char *reg = "bp");
int shr(uint16_t *bp, const char *addr, const char *reg = "bp");

int carry;

uint16_t unpack(const uint8_t *ds, uint8_t *es)
{
	if (memcmp(ds, "FAB", 3)) {
		printf("Not a FAB section\n");
		return -1;
	}

	if (ds[3] < 0x0a || ds[3] > 0x0d) {
		printf("PANIC! don't know how to handle this FAB type. (0x%02x)\n", ds[3]);
		exit(EXIT_FAILURE);
	}

	uint16_t cx = 0x10 - ds[0x03]; //cx is set as 0x10 - byte following FAB
	assert(cx == 0x04);

	uint16_t dx = 0x10; //XXX dx is set as 0x10 (first occurance in coloview: 0f47:04c8)

	uint16_t si = 4; //skipping FAB\x0c
	uint16_t di = 0;
	carry = 1; //XXX where does this come from?

	uint16_t bp = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:04fc NEW bp(%04x) %d\n", bp, carry);

	for (;;) {
		if (--dx == 0) {
			//fprintf(stderr, "11c8:04e4 dx(%02x)\n", dx);
			dx = 0x10; //11c8:04e8
			shr(&bp, "11c8:04ea");
			bp = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:04ec NEW bp(%04x) %d\n", bp, carry);
			rcl(&bp, "11c8:04ee");
		} else {
			//fprintf(stderr, "11c8:04e4 dx(%02x)\n", dx);
		}

		if (rcr(&bp, "11c8:04f0")) {
			es[di++] = ds[si++]; //fprintf(stderr, "11c8:04f4 es[%02x]: %02x\n", di-1, es[di-1]);
			//assert(es[di - 1] == terrain_s1[di - 1]);
			continue;
		}

		cx = 0; //11c8:04f7

		if (--dx == 0) { //11c8:04f9
			//fprintf(stderr, "11c8:04f9 dx(%02x)\n", dx);

			dx = 0x10; //11c8:04fd
			shr(&bp, "11c8:04ff");
			bp = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:04fc NEW bp(%04x) %d\n", bp, carry);
			rcl(&bp, "11c8:0503");
		} else {
			//fprintf(stderr, "11c8:04f9 dx(%02x)\n", dx);
		}

		uint16_t bx;
	
		if (rcr(&bp, "11c8:0505")) {  //THIS IS WHERE IT GETS WEIRD
			uint8_t cs[] = {0x04, 0x0f, 0xf0}; //11c8:{045e,045f,0460}

			uint16_t ax = bx = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:0533 NEW bx(%04x) %d\n", bx, carry);
			cx  = cs[0];
			bx  = (bx & 0x00ff) + (((bx & 0xff00) >> (cx & 0x00ff)) & 0xff00); //fprintf(stderr, "11c8:053b shr bx(%04x)\n", bx);
			bx |= (cs[2] << 8); //fprintf(stderr, "11c8:053d bx(%04x)\n", bx);
			ax = ((ax & 0xff00) & (cs[1] << 8)) + (ax & 0x00ff); //fprintf(stderr, "11c8:0542 ax(%04x)\n", ax);

			if (0 == (ax & 0xff00)) { //11c8:0547
				ax = ds[si++];
				if (ax == 0) {
					//printf("\nStage 1 complete!\n");
					return di;
				}
				if (ax == 1) {
					printf("\nStage 1 error!\n");
					exit(EXIT_FAILURE);
				}
				cx = ax; //fprintf(stderr, "11c8:055e cx(%04x)\n", cx);
				++cx; //11c8:0560
			} else {
				cx = (ax >> 8);// fprintf(stderr, "11c8:0549 cx(%04x)\n", cx);
				cx += 2;
			}
		} else {
			if (--dx == 0) { //11c8:0509
				//fprintf(stderr, "11c8:0509 dx(%02x)\n", dx);
				dx = 0x10; //11c8:050d
				shr(&bp, "11c8:050f");
				bp = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:0511 NEW bp(%04x) %d\n", bp, carry);
				rcl(&bp, "11c8:0513");
			} else {
				//fprintf(stderr, "11c8:0509 dx(%02x)\n", dx);
			}
		
			rcr(&bp, "11c8:0515");
			rcl(&cx, "11c8:0517", "cx");

			if (--dx == 0) { //11c8:0519
				//fprintf(stderr, "11c8:0519 dx(%02x)\n", dx);
				dx = 0x10;
				shr(&bp, "11c8:051f");
				bp = *(uint16_t *) &ds[si]; si += 2; //fprintf(stderr, "11c8:0521 NEW bp(%04x) %d\n", bp, carry);
				rcl(&bp, "11c8:0523");
			} else {
				//fprintf(stderr, "11c8:0519 dx(%02x)\n", dx);
			}
		
			rcr(&bp, "11c8:0525");
			rcl(&cx, "11c8:0527", "cx");

			cx += 2; //fprintf(stderr, "11c8:052a cx(%02x)\n", cx);
			bx = 0xff00; bx += ds[si++];
		}

		int tmp_di = bx + di - 0x10000; //11c8:054d
		for (cx; cx--; ) {
			es[di++] = es[tmp_di++]; //fprintf(stderr, "11c8:0550 es[%02x]: %02x\n", di-1, es[di-1]);
			//assert(es[di - 1] == terrain_s1[di - 1]);
		}
	}	

	return -1;
}


int rcr(uint16_t *bp, const char *addr, const char *reg)
{
	int c = 0;

	if (*bp & 1)
		c = 1;

	*bp = *bp >> 1;

	if (carry)
		*bp += 0x8000;

	//fprintf(stderr, "%s rcr %s(%04x) %d\n", addr, reg, *bp, c);

	return carry = c;
}

int rcl(uint16_t *bp, const char *addr, const char *reg)
{
	int c = 0;

	if (*bp & 0x8000)
		c = 1;

	*bp = *bp << 1;

	if (carry)
		*bp += 1;

	//fprintf(stderr, "%s rcl %s(%04x) %d\n", addr, reg, *bp, c);

	return carry = c;
}

int shr(uint16_t *bp, const char *addr, const char *reg)
{
	int c = 0;

	if (*bp & 1)
		c = 1;

	*bp = *bp >> 1;

	//fprintf(stderr, "%s shr %s(%04x) %d\n", addr, reg, *bp, c);

	return carry = c;
}


/*
const char *binary(uint16_t bp)
{
	static char binary[17];
	if (bp & 0x8000)
		itoa(bp, binary, 2);
	else {
		itoa(bp + 0x8000, binary, 2);
		binary[0] = '0';
	}

	return binary;
}

printf("\033[22;31m%c\033[0m", binary[i]);
*/
