/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RKP_H__
#define __RKP_H__

#ifndef __ASSEMBLY__
#include <asm/stack_pointer.h>
#include <asm/thread_info.h>
#include <linux/spinlock.h>
#include <linux/uh.h>

#ifdef CONFIG_UH_PKVM
#define RKP_EARLY_MODULE	2
#endif
#define RKP_INIT_MAGIC		0x5afe0001
#define __rkp_ro __section(".rkp_ro")

enum __RKP_CMD_ID {
	RKP_START = 0x00,
	RKP_DEFERRED_START = 0x01,
	/* RKP robuffer cmds*/
	RKP_GET_RO_INFO = 0x2,
	RKP_CHG_RO = 0x03,
	RKP_CHG_RW = 0x04,
	RKP_PGD_RO = 0x05,
	RKP_PGD_RW = 0x06,
	RKP_ROBUFFER_ALLOC = 0x07,
	RKP_ROBUFFER_FREE = 0x08,
	/* module, binary load */
	RKP_DYNAMIC_LOAD = 0x09,
	RKP_MODULE_LOAD = 0x0A,
	RKP_BPF_LOAD = 0x0B,
	/* Log */
	RKP_LOG = 0x0C,
	RKP_KPROBE_PAGE = 0x11,
};

#define RKP_MODULE_PXN_CLEAR	0x1
#define RKP_MODULE_PXN_SET		0x2

struct rkp_init {
	u32 magic;
	u64 reserved1;
	u64 reserved2;
	u64 init_mm_pgd;
	u64 id_map_pgd;
	u64 zero_pg_addr;
	u64 rkp_pgt_bitmap;
	u64 rkp_dbl_bitmap;
	u32 rkp_bitmap_size;
	u32 reserved3;
	u64 reserved4;
	u64 _text;
	u64 _etext;
	u64 extra_memory_addr;
	u32 extra_memory_size;
	u64 reserved5;
	u64 _srodata;
	u64 _erodata;
	u32 reserved6;
	u64 tramp_pgd;
	u64 tramp_valias;
};

struct module_info {
	u64 base_va;
	u64 vm_size;
	u64 core_base_va;
	u64 core_text_size;
	u64 core_ro_size;
	u64 init_base_va;
	u64 init_text_size;
};

extern u64 module_direct_base;
extern u64 module_plt_base;

extern bool rkp_started;

#ifdef CONFIG_UH_PKVM
extern void rkp_init(void);
#else
extern void __init rkp_init(void);
#endif
extern void rkp_deferred_init(void);
extern void rkp_robuffer_init(void);

extern inline phys_addr_t rkp_ro_alloc_phys(int shift);
extern inline phys_addr_t rkp_ro_alloc_phys_for_text(void);
extern inline void *rkp_ro_alloc(void);
extern inline void rkp_ro_free(void *free_addr);
extern inline bool is_rkp_ro_buffer(u64 addr);

void *module_alloc_by_rkp(unsigned int core_layout_size, unsigned int core_text_size);
#endif //__ASSEMBLY__
#endif //__RKP_H__
