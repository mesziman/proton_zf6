/*
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_CPUFEATURE_H
#define __ASM_CPUFEATURE_H

#include <asm/cpucaps.h>
#include <asm/hwcap.h>
#include <asm/sysreg.h>

/*
 * In the arm64 world (as in the ARM world), elf_hwcap is used both internally
 * in the kernel and for user space to keep track of which optional features
 * are supported by the current system. So let's map feature 'x' to HWCAP_x.
 * Note that HWCAP_x constants are bit fields so we need to take the log.
 */

#define MAX_CPU_FEATURES	(8 * sizeof(elf_hwcap))
#define cpu_feature(x)		ilog2(HWCAP_ ## x)

#define ARM64_SSBD_UNKNOWN		-1
#define ARM64_SSBD_FORCE_DISABLE	0
#define ARM64_SSBD_KERNEL		1
#define ARM64_SSBD_FORCE_ENABLE		2
#define ARM64_SSBD_MITIGATED		3

#ifndef __ASSEMBLY__

#include <linux/bug.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>

/*
 * CPU feature register tracking
 *
 * The safe value of a CPUID feature field is dependent on the implications
 * of the values assigned to it by the architecture. Based on the relationship
 * between the values, the features are classified into 3 types - LOWER_SAFE,
 * HIGHER_SAFE and EXACT.
 *
 * The lowest value of all the CPUs is chosen for LOWER_SAFE and highest
 * for HIGHER_SAFE. It is expected that all CPUs have the same value for
 * a field when EXACT is specified, failing which, the safe value specified
 * in the table is chosen.
 */

enum ftr_type {
	FTR_EXACT,			/* Use a predefined safe value */
	FTR_LOWER_SAFE,			/* Smaller value is safe */
	FTR_HIGHER_SAFE,		/* Bigger value is safe */
	FTR_HIGHER_OR_ZERO_SAFE,	/* Bigger value is safe, but 0 is biggest */
};

#define FTR_STRICT	true	/* SANITY check strict matching required */
#define FTR_NONSTRICT	false	/* SANITY check ignored */

#define FTR_SIGNED	true	/* Value should be treated as signed */
#define FTR_UNSIGNED	false	/* Value should be treated as unsigned */

#define FTR_VISIBLE	true	/* Feature visible to the user space */
#define FTR_HIDDEN	false	/* Feature is hidden from the user */

struct arm64_ftr_bits {
	bool		sign;	/* Value is signed ? */
	bool		visible;
	bool		strict;	/* CPU Sanity check: strict matching required ? */
	enum ftr_type	type;
	u8		shift;
	u8		width;
	s64		safe_val; /* safe value for FTR_EXACT features */
};

/*
 * @arm64_ftr_reg - Feature register
 * @strict_mask		Bits which should match across all CPUs for sanity.
 * @sys_val		Safe value across the CPUs (system view)
 */
struct arm64_ftr_reg {
	const char			*name;
	u64				strict_mask;
	u64				user_mask;
	u64				sys_val;
	u64				user_val;
	const struct arm64_ftr_bits	*ftr_bits;
};

extern struct arm64_ftr_reg arm64_ftr_reg_ctrel0;

/* scope of capability check */
enum {
	SCOPE_SYSTEM,
	SCOPE_LOCAL_CPU,
};

struct arm64_cpu_capabilities {
	const char *desc;
	u16 capability;
	int def_scope;			/* default scope */
	bool (*matches)(const struct arm64_cpu_capabilities *caps, int scope);
	int (*enable)(void *);		/* Called on all active CPUs */
	union {
		struct {	/* To be used for erratum handling only */
			u32 midr_model;
			u32 midr_range_min, midr_range_max;
		};

		struct {	/* Feature register checking */
			u32 sys_reg;
			u8 field_pos;
			u8 min_field_value;
			u8 hwcap_type;
			bool sign;
			unsigned long hwcap;
		};
	};
};

extern DECLARE_BITMAP(cpu_hwcaps, ARM64_NCAPS);
extern struct static_key_false cpu_hwcap_keys[ARM64_NCAPS];
extern struct static_key_false arm64_const_caps_ready;

bool this_cpu_has_cap(unsigned int cap);

static inline bool cpu_have_feature(unsigned int num)
{
	return elf_hwcap & (1UL << num);
}

/* System capability check for constant caps */
static __always_inline bool __cpus_have_const_cap(int num)
{
	if (num >= ARM64_NCAPS)
		return false;
	return static_branch_unlikely(&cpu_hwcap_keys[num]);
}

static inline bool cpus_have_cap(unsigned int num)
{
	if (num >= ARM64_NCAPS)
		return false;
	return test_bit(num, cpu_hwcaps);
}

static __always_inline bool cpus_have_const_cap(int num)
{
	if (static_branch_likely(&arm64_const_caps_ready))
		return __cpus_have_const_cap(num);
	else
		return cpus_have_cap(num);
}

static inline void cpus_set_cap(unsigned int num)
{
	if (num >= ARM64_NCAPS) {
		pr_warn("Attempt to set an illegal CPU capability (%d >= %d)\n",
			num, ARM64_NCAPS);
	} else {
		__set_bit(num, cpu_hwcaps);
	}
}

static inline int __attribute_const__
cpuid_feature_extract_signed_field_width(u64 features, int field, int width)
{
	return (s64)(features << (64 - width - field)) >> (64 - width);
}

static inline int __attribute_const__
cpuid_feature_extract_signed_field(u64 features, int field)
{
	return cpuid_feature_extract_signed_field_width(features, field, 4);
}

static inline unsigned int __attribute_const__
cpuid_feature_extract_unsigned_field_width(u64 features, int field, int width)
{
	return (u64)(features << (64 - width - field)) >> (64 - width);
}

static inline unsigned int __attribute_const__
cpuid_feature_extract_unsigned_field(u64 features, int field)
{
	return cpuid_feature_extract_unsigned_field_width(features, field, 4);
}

static inline u64 arm64_ftr_mask(const struct arm64_ftr_bits *ftrp)
{
	return (u64)GENMASK(ftrp->shift + ftrp->width - 1, ftrp->shift);
}

static inline u64 arm64_ftr_reg_user_value(const struct arm64_ftr_reg *reg)
{
	return (reg->user_val | (reg->sys_val & reg->user_mask));
}

static inline int __attribute_const__
cpuid_feature_extract_field_width(u64 features, int field, int width, bool sign)
{
	return (sign) ?
		cpuid_feature_extract_signed_field_width(features, field, width) :
		cpuid_feature_extract_unsigned_field_width(features, field, width);
}

static inline int __attribute_const__
cpuid_feature_extract_field(u64 features, int field, bool sign)
{
	return cpuid_feature_extract_field_width(features, field, 4, sign);
}

static inline s64 arm64_ftr_value(const struct arm64_ftr_bits *ftrp, u64 val)
{
	return (s64)cpuid_feature_extract_field_width(val, ftrp->shift, ftrp->width, ftrp->sign);
}

static inline bool id_aa64mmfr0_mixed_endian_el0(u64 mmfr0)
{
	return cpuid_feature_extract_unsigned_field(mmfr0, ID_AA64MMFR0_BIGENDEL_SHIFT) == 0x1 ||
		cpuid_feature_extract_unsigned_field(mmfr0, ID_AA64MMFR0_BIGENDEL0_SHIFT) == 0x1;
}

static inline bool id_aa64pfr0_32bit_el0(u64 pfr0)
{
	u32 val = cpuid_feature_extract_unsigned_field(pfr0, ID_AA64PFR0_EL0_SHIFT);

	return val == ID_AA64PFR0_EL0_32BIT_64BIT;
}

void __init setup_cpu_features(void);

void update_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info);
void enable_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info);
void check_local_cpu_capabilities(void);

void update_cpu_errata_workarounds(void);
void __init enable_errata_workarounds(void);
void verify_local_cpu_errata_workarounds(void);

u64 read_sanitised_ftr_reg(u32 id);

static inline bool cpu_supports_mixed_endian_el0(void)
{
	return id_aa64mmfr0_mixed_endian_el0(read_cpuid(ID_AA64MMFR0_EL1));
}

static inline bool system_supports_32bit_el0(void)
{
	return cpus_have_const_cap(ARM64_HAS_32BIT_EL0);
}

static inline bool system_supports_mixed_endian_el0(void)
{
	return id_aa64mmfr0_mixed_endian_el0(read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1));
}

static inline bool system_supports_fpsimd(void)
{
	return !cpus_have_const_cap(ARM64_HAS_NO_FPSIMD);
}

static inline bool system_uses_ttbr0_pan(void)
{
	return IS_ENABLED(CONFIG_ARM64_SW_TTBR0_PAN) &&
		!cpus_have_const_cap(ARM64_HAS_PAN);
}

static inline int arm64_get_ssbd_state(void)
{
#ifdef CONFIG_ARM64_SSBD
	extern int ssbd_state;
	return ssbd_state;
#else
	return ARM64_SSBD_UNKNOWN;
#endif
}

#ifdef CONFIG_ARM64_SSBD
void arm64_set_ssbd_mitigation(bool state);
#else
static inline void arm64_set_ssbd_mitigation(bool state) {}
#endif

#endif /* __ASSEMBLY__ */

#endif
