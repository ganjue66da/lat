/**
 * @file opt-jmp.c
 * @author Hanlu Li <heuleehanlu@gmail.com>
 * @brief JRRA optimization
 */
#include <translate.h>
#include <include/exec/tb-lookup.h>
#include <accel/tcg/internal.h>
#include "opt-jmp.h"
#include "latx-options.h"
#include "reg-map.h"
#ifdef CONFIG_LATX_TU
#include "tu.h"
#endif
#include "ts.h"
#include "aot_recover_tb.h"

#ifdef CONFIG_LATX_JRRA
static bool tb_ending_with_call(TranslationBlock *tb)
{
    if (!tb) {
        return false;
    }
    return tb->next_86_pc ? true : false;
}

static TranslationBlock *get_next_tb(TranslationBlock *tb, CPUState *cpu,
                    target_ulong cs_base, uint32_t flags, uint32_t cflags)
{

    TranslationBlock *n;
#ifdef CONFIG_LATX_AOT
    if (!in_pre_translate) {
        n = tb_lookup(cpu, tb->next_86_pc, cs_base, flags, cflags);
    } else {
        if ((tb->pc & TARGET_PAGE_MASK) != (tb->next_86_pc & TARGET_PAGE_MASK)) {
            return NULL;
        }
        return aot_tb_lookup(tb->next_86_pc, cflags);
    }
#else
    n = tb_lookup(cpu, tb->next_86_pc, cs_base, flags, cflags);
#endif
    if (!n) {
        n = tb_gen_code(cpu, tb->next_86_pc, cs_base, flags, cflags);
    }
    return n;
}

static void patch_jrra(TranslationBlock *tb, TranslationBlock *next_tb)
{
    int temp0, temp1;
    uint64_t patch_pcalau12i, patch_ori, patch_scr1, patch_scr0;
    ptrdiff_t offset_high, offset_low;
    tb_page_addr_t tb_page;

    if (!option_jr_ra) {
        return;
    }

    tb_page = tb_page_addr1(tb) == -1 ? tb_page_addr0(tb) : tb_page_addr1(tb);
    if (tb->return_target_ptr &&
            ((tb_page & TARGET_PAGE_MASK) == (tb_page_addr0(next_tb) & TARGET_PAGE_MASK))) {
        offset_high = (((uint64_t)next_tb->tc.ptr >> 12) -
                    ((uintptr_t)tb->return_target_ptr >> 12)) & 0xfffff;
        offset_low = (uint64_t)next_tb->tc.ptr & 0xfff;

        temp0 = reg_itemp_map[ITEMP0];
        temp1 = reg_itemp_map[ITEMP1];
        /* pcalau12i itemp0, offset_high */
        patch_pcalau12i = 0x1a000000 | temp0 | (offset_high << 5);
        /* ori itemp0, itemp0, offset_low */
        patch_ori = 0x03800000 | (offset_low << 10) |
                    (temp0 << 5) | temp0;
        /* gr2scr scr1, itemp0 */
        patch_scr1 = 0x801 | (temp0 << 5);
        /* gr2scr scr0, itemp1 */
        patch_scr0 = 0x800 | (temp1 << 5);
#ifdef CONFIG_LATX_AOT
        tb->bool_flags |= IS_ENABLE_JRRA;
#endif
        *tb->return_target_ptr = (patch_pcalau12i | (patch_ori << 32));
        *(tb->return_target_ptr + 1) = (patch_scr1 | (patch_scr0 << 32));
    }
}

static void patch_jrra_stack(TranslationBlock *tb, TranslationBlock *next_tb)
{
    int temp1;
    uint64_t patch_pcalau12i, patch_ori;
    ptrdiff_t offset_high, offset_low;

    if (!option_jr_ra_stack || !tb->return_target_ptr) {
        return;
    }

    offset_high = (((uint64_t)next_tb->tc.ptr >> 12) -
                    ((uintptr_t)tb->return_target_ptr >> 12)) & 0xfffff;
    offset_low = (uint64_t)next_tb->tc.ptr & 0xfff;

    /* pcalau12i itemp0, offset_high */
    temp1 = reg_itemp_map[ITEMP1];
    patch_pcalau12i = 0x1a000000 | temp1 | (offset_high << 5);
    /* ori itemp0, itemp0, offset_low */
    patch_ori = 0x03800000 | (offset_low << 10) |
                (temp1 << 5) | temp1;
#ifdef CONFIG_LATX_AOT
    tb->bool_flags |= IS_ENABLE_JRRA;
#endif
    *tb->return_target_ptr = (patch_pcalau12i | (patch_ori << 32));
}
#endif /* ifdef CONFIG_LATX_JRRA */

void jrra_pre_translate(void** list, int num, CPUState *cpu,
                        target_ulong cs_base, uint32_t flags, uint32_t cflags)
{
#ifdef CONFIG_LATX_JRRA
    TranslationBlock *next = NULL;
    TranslationBlock *curr = NULL;

    while (num--) {
        curr = *(TranslationBlock**)list;
        while (tb_ending_with_call(curr)) {
            if (!(next = get_next_tb(curr, cpu, cs_base, flags, cflags))) {
                break;
            }
            patch_jrra(curr, next);
            patch_jrra_stack(curr, next);
            curr = next;
        }
        list++;
    }
#endif
    return;
}
