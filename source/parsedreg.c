/* skylark@mips.for.ever */

/*
PSP Registry Parser v2 by Skylark (with minimal help from FreePlay :-P)
--------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <polarssl/sha1.h>
#include <pspsdk.h>

#include "types.h"

#define ET_HEADER  0
#define ET_STRING  1
#define ET_INTEGER 2
#define ET_SUBDIR  3

#define DREG_SIZE  131072

typedef struct ent {
	int type;
	char *name;
	union {
		struct {
			int idx;
			int paridx;
			int nkids;
			int fail;
			struct ent **kids;
			struct ent *nextheader;
			struct ent *parent;
		} header;
		struct {
			int secret;
			char *str;
		} string;
		struct {
			int val;
		} integer;
		struct {
			struct ent *header;
		} subdir;
	};
} ent;

typedef struct ireg {
	short _0;
	short _1;
	short parent;
	short _2;
	short _3;
	short nent;
	short nblk;
	char name[28];
	short _4;
	short fat[7];
} ireg_t;

static uint8_t *dreg;


static void freeheader(ent *e)
{
	if(e->type==ET_HEADER) {
		for(int i=0; i<e->header.nkids; i++)
			freeheader(e->header.kids[i]);

		free(e->header.kids);
	}
	if(e->type==ET_STRING) {
		free(e->string.str);
	}
	free(e->name);
	free(e);
}

static void getcheck(uint8_t *block, int len, uint8_t *check)
{
	uint8_t save[4];
	uint8_t res[20];

	memcpy(save, block+14, 4);
	memset(block+14, 0, 4);

	// Calculate SHA-1 hash of the block
	sha1(block, len, res);

	memcpy(block+14, save, 4);

	check[0] = res[4] ^ res[3] ^ res[2] ^ res[1] ^ res[0];
	check[1] = res[9] ^ res[8] ^ res[7] ^ res[6] ^ res[5];
	check[2] = res[14] ^ res[13] ^ res[12] ^ res[11] ^ res[10];
	check[3] = res[19] ^ res[18] ^ res[17] ^ res[16] ^ res[15];
}

static int checkcheck(uint8_t *block, int len)
{
	uint8_t res[4];

	getcheck(block, len, res);
	return(!memcmp(res, block+14, 4));
}

static void parse_string(int i, int s, short *f, ent *hdr)
{
	int l=*(short *)(dreg+i+28);
	ent *e=calloc(sizeof(ent),1);
	char x[l];
	int j,k;

	for(j=0; j<l; j++) {
		k=dreg[i+31]+(j>>5);
		x[j]=dreg[(j&31)+32*(k&15)+512*f[k>>4]];
	}

	x[l]=0;
	e->type=ET_STRING;
	e->name=strdup((char*)dreg+i+1);
	e->string.secret=s;
	e->string.str=strdup(x);
	hdr->header.kids[hdr->header.nkids++]=e;
}

static void parse(int i, short *f, ent *hdr)
{
	switch(dreg[i]) {
	case 0x01:
//		parse_subdir(i,f,hdr);
		break;
	case 0x02:
//		parse_int(i,f,hdr);
		break;
	case 0x03:
//		parse_string(i,0,f,hdr);
		break;
	case 0x04:
		parse_string(i,1,f,hdr);
		break;
	case 0x0f:
	case 0x10:
	case 0x1f:
	case 0x20:
	case 0x2f:
	case 0x30:
	case 0x3f:
//		parse_header(i,f,hdr);
		break;
	case 0x00:
		/* WTF? */
		break;
	default:
		LOG("UNKNOWN TYPE <%02x> [@%06x]", dreg[i], i);
	}
}

static ent* walk_fatents(ireg_t *a)
{
	int i,j;
	uint8_t *buf;
	ent *hdrlist = NULL;

	for(i=0; i<256; i++)
		if(a[i].nblk) {
			if (strcmp(a[i].name, "NP") != 0)
				continue;
			
			ent *e=calloc(sizeof(ent),1);
			e->type=ET_HEADER;
			e->name=strdup(a[i].name);
			e->header.idx=i;
			e->header.paridx=a[i].parent;
			e->header.kids=calloc(sizeof(ent *),a[i].nent);

			/* reassemble segments
			   it can be done better in-place
			   bleh i don't care foo */
			buf=malloc(a[i].nblk*512);
			for(j=0; j<a[i].nblk; j++)
				memcpy(buf+512*j,dreg+512*a[i].fat[j],512);

			free(buf);
			if(!checkcheck(buf,a[i].nblk*512))
				e->header.fail=1;

			/* walk the walk */
			for(j=0;j<=a[i].nent;j++)
				parse(32*(j&15)+512*a[i].fat[j>>4],a[i].fat,e);

			e->header.nextheader=hdrlist;
			hdrlist=e;
		}

	return hdrlist;
}

static uint64_t dump_account_id(const ent *h)
{
	uint64_t act = 0;

	for(int i=0; i<h->header.nkids; i++)
	{
		if (strcmp(h->header.kids[i]->name, "account_id") != 0)
			continue;
		
		if(h->header.kids[i]->type == ET_STRING)
			sscanf(h->header.kids[i]->string.str, "%" PRIx64, &act);
	}

	return act;
}

static uint64_t parse_registry(void)
{
	FILE *f;
	ireg_t ireg[256];
	ent *hdrlist, *i, *tmp = NULL;
	uint64_t act = 0;
	uint32_t k1;

	dreg = malloc(DREG_SIZE);
	if (!dreg)
		return 0;

	k1 = pspSdkSetK1(0);

	f = fopen("flash1:/registry/system.dreg", "rb");
	if (!f) {
		LOG("Failed to open registry");
		goto end;
	}

	fread(dreg, DREG_SIZE, 1, f);
	fclose(f);

	f = fopen("flash1:/registry/system.ireg", "rb");
	if (!f) {
		LOG("Failed to open registry");
		goto end;
	}

	fseek(f, 0x5C, SEEK_SET);
	fread(ireg, sizeof(ireg_t), 256, f);
	fclose(f);

	hdrlist = walk_fatents(ireg);

	for(i=hdrlist; i; i=i->header.nextheader)
		if(!i->header.parent)
			act = dump_account_id(i);

	LOG("Registry Account ID: %016" PRIX64, act);

	for(i=hdrlist; i; i=tmp)
	{
		tmp = i->header.nextheader;
		freeheader(i);
	}

end:
	free(dreg);
	pspSdkSetK1(k1);
	return act;
}

static uint64_t read_actdat(void)
{
	FILE *f;
	uint64_t buf[4];
	uint64_t account_id = 0;
	uint32_t k1 = pspSdkSetK1(0);

	f = fopen("flash2:/act.dat", "rb");
	if (!f) {
		LOG("Failed to open act.dat");
		goto end;
	}

	fread(buf, 1, sizeof(buf), f);
	fclose(f);

	account_id = buf[1];
	LOG("act.dat Account ID: %016" PRIX64, account_id);

end:
	pspSdkSetK1(k1);
	return account_id;
}

uint64_t get_account_id(void)
{
	FILE *fp;
	uint64_t account_id = read_actdat();

	if (!account_id)
		account_id = parse_registry();

	// Fallback to reading from account.txt
	fp = fopen("./account.txt", "r");
	if (fp) {
		fscanf(fp, "%" PRIx64, &account_id);
		fclose(fp);
		LOG("Account ID %016" PRIX64, account_id);
	}

	if (!account_id)
		LOG("No Account ID found");

	return account_id;
}
