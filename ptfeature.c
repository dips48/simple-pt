/* Check CPUID for different PT features
 * Without arguments print all.
 */

/*
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <cpuid.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sched.h>
#include <errno.h>

#define BIT(x) (1ULL << (x))

static int read_msr(int cpu, unsigned num, uint64_t *val)
{
	int ret = -1;
	char fn[100];
	snprintf(fn, sizeof fn, "/dev/cpu/%d/msr", cpu);
	int fd = open(fn, O_RDONLY);
	if (fd < 0)
		return -1;
	if (pread(fd, val, 8, num) == 8)
		ret = 0;
	close(fd);
	return ret;
}

static int read_platform_info(unsigned *ratio)
{
	cpu_set_t cpus;
	sched_getaffinity(0, sizeof(cpu_set_t), &cpus);
	int cpu;
	for (cpu = 0; cpu < __CPU_SETSIZE; cpu++) {
		uint64_t pinfo;
		if (read_msr(cpu, 0xce, &pinfo) == 0) {
			*ratio = (pinfo >> 8) & 0xff;
			break;
		}
		if (errno == EACCES || errno == ENOENT)
			break;
	}
	return 0;
}

static void print_bits(unsigned x)
{
	int i;
	for (i = 0; i < 32; i++)
		if (BIT(i) & x)
			printf("%d ", i);
}

int main(int ac, char **av)
{
	unsigned a, b, c, d;
	int addr_cfg_max = 0;
	int mtc_freq_mask = 0;
	int cyc_thresh_mask = 0;
	int psb_freq_mask = 0;
	int addr_range_num = 0;
	unsigned max_leaf;
	float bus_freq;
	unsigned fam, mod, stepping;

	max_leaf = __get_cpuid_max(0, NULL);
	if (max_leaf < 0x14) {
		printf("No PT support\n");
		return 1;
	}

	__cpuid(1, a, b, c, d);
	stepping = a & 0xf;
	mod = (a >> 4) & 0xf;
	fam = (a >> 8) & 0xf;
	if (fam == 0xf)
		fam += (a >> 20) & 0xff;
	if (fam == 6 || fam == 0xf)
		mod += ((a >> 16) & 0xf) << 4;

	/* check cpuid */
	__cpuid_count(0x07, 0, a, b, c, d);
	if ((b & BIT(25)) == 0) {
		printf("No PT support\n");
		return 1;
	}
	__cpuid_count(0x14, 0, a, b, c, d);
	if (b & BIT(2))
		addr_cfg_max = 2;
	if ((b & BIT(1)) && a >= 1) {
		unsigned a1, b1, c1, d1;
		__cpuid_count(0x14, 1, a1, b1, c1, d1);
		mtc_freq_mask = (a1 >> 16) & 0xffff;
		cyc_thresh_mask = b1 & 0xffff;
		psb_freq_mask = (b1 >> 16) & 0xffff;
		addr_range_num = a1 & 0x3;
	}

	unsigned a1 = 0, b1 = 0, c1 = 0, d1 = 0;
	if (max_leaf >= 0x15) {
		__cpuid(0x15, a1, b1, c1, d1);
		if (a1 && b1)
			bus_freq = 1. / ((float)a1 / (float)b1);
	}

	if (av[1] == NULL) {
		printf("Supports PT\n");
		printf("toPA output support:		%d\n", !!(c & BIT(0)));
		printf("multiple toPA entries:		%d\n", !!(c & BIT(1)));
		printf("single range:			%d\n", !!(c & BIT(2)));
		printf("trace transport output:		%d\n", !!(c & BIT(3)));
		printf("payloads are LIP:		%d\n", !!(c & BIT(31)));
		printf("cycle accurate mode / psb freq:	%d\n", !!(b & BIT(1)));
		printf("filtering / stop / mtc:		%d\n", !!(b & BIT(2)));
		printf("CR3 match:			%d\n", !!(b & BIT(0)));
		printf("Number of address ranges:	%d\n", addr_range_num);
		printf("Supports filter ranges:		%d\n", addr_cfg_max >= 1);
		printf("Supports stop ranges:		%d\n", addr_cfg_max >= 2);
		printf("Valid cycles thresholds:	");
		print_bits(cyc_thresh_mask);
		putchar('\n');
		printf("Valid PSB frequencies:		");
		print_bits(psb_freq_mask);
		putchar('\n');
		printf("Valid MTC frequencies:	        ");
		print_bits(mtc_freq_mask);
		putchar('\n');
		if (a1 && b1) {
			printf("TSC ratio:		        ");
			printf("%d %d\n", a1, b1);
		}
		if (bus_freq)
			printf("Bus frequency:			%f\n", bus_freq);
		unsigned pinfo;
		if (read_platform_info(&pinfo) == 0)
			printf("Max non Turbo Ratio:	        %u\n", pinfo);
		printf("Family:				%d\n", fam);
		printf("Model:				%d\n", mod);
		printf("Stepping:			%d\n", stepping);
		return 0;
	}

	while (*++av) {
		if (!strcmp(*av, "pt")) {
			continue; /* Already checked */
		} else if (!strcmp(*av, "filter")) {
			if (addr_range_num == 0 || addr_cfg_max < 1) {
				printf("No filter ranges\n");
				return 1;
			}
		} else if (!strcmp(*av, "stop")) {
			if (addr_range_num == 0 || addr_cfg_max < 2) {
				printf("No stop ranges\n");
				return 1;
			}
		} else if (!strcmp(*av, "cyc")) {
			if (cyc_thresh_mask == 0) {
				printf("No CYC support\n");
				return 1;
			}
		} else if (!strcmp(*av, "psb")) {
			if (psb_freq_mask == 0) {
				printf("No PSB support\n");
				return 1;
			}
		} else if (!strcmp(*av, "mtc")) {
			if (mtc_freq_mask == 0) {
				printf("No MTC support\n");
				return 1;
			}
		} else if (!strcmp(*av, "topa")) {
			if (!(c & BIT(0))) {
				printf("No toPA support\n");
				return 1;
			}
		} else if (!strcmp(*av, "multi_topa")) {
			if (!(c & BIT(0)) || !(c & BIT(1))) {
				printf("No multiple toPA support\n");
				return 1;
			}
		} else if (!strcmp(*av, "single_range")) {
			if (!(c & BIT(2))) {
				printf("No single range support\n");
				return 1;
			}
		} else if (!strcmp(*av, "lip")) {
			if (!(c & BIT(31))) {
				printf("Payloads are not LIP\n");
				return 1;
			}
		} else {
			fprintf(stderr, "Unknown match %s\n", *av);
			fprintf(stderr, "Valid matches: pt, filter, stop, cyc, psb, mtc, pt, topa, multi_topa, single_range, lip\n");
			return 1;
		}
	}
	return 0;
}
