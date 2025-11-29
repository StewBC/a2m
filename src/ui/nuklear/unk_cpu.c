// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

void unk_cpu_show(UNK *v, int dirty) {
    APPLE2 *m = v->m;
    RUNTIME *rt = v->rt;
    VIEWDASM *dv = &v->viewdasm;
    VIEWCPU *vcpu = &v->viewcpu;
    unsigned int value;
    struct nk_context *ctx = v->ctx;

    int w = v->layout.cpu.w;
    if(dirty) {
        unk_cpu_update(v);
    }
    if(nk_begin(ctx, "CPU", v->layout.cpu, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 10);
        nk_layout_row_push(ctx, 0.05f);
        nk_label(ctx, "PC", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.15f);
        if(NK_EDIT_COMMITED &
                nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[0],
                                &vcpu->register_lengths[0], 5, nk_filter_hex)) {
            vcpu->registers[0][vcpu->register_lengths[0]] = 0;
            ctx->current->edit.active = 0;
            sscanf(vcpu->registers[0], "%x", &value);
            runtime_machine_set_pc(rt, value);
            // m->cpu.pc = value;
        }
        nk_layout_row_push(ctx, 0.05f);
        nk_label(ctx, "SP", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.15f);
        if(NK_EDIT_COMMITED &
                nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[1],
                                &vcpu->register_lengths[1], 5, nk_filter_hex)) {
            vcpu->registers[1][vcpu->register_lengths[1]] = 0;
            ctx->current->edit.active = 0;
            sscanf(vcpu->registers[1], "%x", &value);
            runtime_machine_set_sp(rt, value);
            // m->cpu.sp = value;
        }
        nk_layout_row_push(ctx, 0.05f);
        nk_label(ctx, "A", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.10f);
        if(NK_EDIT_COMMITED &
                nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[2],
                                &vcpu->register_lengths[2], 3, nk_filter_hex)) {
            vcpu->registers[2][vcpu->register_lengths[2]] = 0;
            ctx->current->edit.active = 0;
            sscanf(vcpu->registers[2], "%x", &value);
            runtime_machine_set_A(rt, value);
            // m->cpu.A = value;
        }
        nk_layout_row_push(ctx, 0.05f);
        nk_label(ctx, "X", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.10f);
        if(NK_EDIT_COMMITED &
                nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[3],
                                &vcpu->register_lengths[3], 3, nk_filter_hex)) {
            vcpu->registers[3][vcpu->register_lengths[3]] = 0;
            ctx->current->edit.active = 0;
            sscanf(vcpu->registers[3], "%x", &value);
            runtime_machine_set_X(rt, value);
            // m->cpu.X = value;
        }
        nk_layout_row_push(ctx, 0.05f);
        nk_label(ctx, "Y", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.10f);
        if(NK_EDIT_COMMITED &
                nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[4],
                                &vcpu->register_lengths[4], 3, nk_filter_hex)) {
            vcpu->registers[4][vcpu->register_lengths[4]] = 0;
            ctx->current->edit.active = 0;
            sscanf(vcpu->registers[4], "%x", &value);
            runtime_machine_set_Y(rt, value);
            // m->cpu.Y = value;
        }
        nk_layout_row_end(ctx);
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 10);
        // nk_layout_row_dynamic(ctx, 13, 1);
        for(int i = 0; i < 8; i++) {
            nk_layout_row_push(ctx, 0.025f);
            nk_labelf(ctx, NK_TEXT_LEFT, "%c", (i)["NVEBDIZC"]);
            nk_layout_row_push(ctx, 0.075f);
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->flags[i], &vcpu->flag_lengths[i],
                                    2, nk_filter_binary)) {
                vcpu->flags[i][vcpu->flag_lengths[i]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->flags[i], "%d", &value);
                uint8_t flags = m->cpu.flags;
                flags &= ~(1 << (7 - i));
                flags |= (value << (7 - i));
                runtime_machine_set_flags(rt, flags);
            }
        }
    }
    nk_end(ctx);
}

// SQW make sure this is called where needed only
void unk_cpu_update(UNK *v) {
    APPLE2 *m = v->m;
    VIEWCPU *vcpu = &v->viewcpu;
    vcpu->register_lengths[0] = sprintf(vcpu->registers[0], "%04X", m->cpu.pc);
    vcpu->register_lengths[1] = sprintf(vcpu->registers[1], "%04X", m->cpu.sp);
    vcpu->register_lengths[2] = sprintf(vcpu->registers[2], "%02X", m->cpu.A);
    vcpu->register_lengths[3] = sprintf(vcpu->registers[3], "%02X", m->cpu.X);
    vcpu->register_lengths[4] = sprintf(vcpu->registers[4], "%02X", m->cpu.Y);
    for(int i = 0; i < 8; i++) {
        vcpu->flag_lengths[i] = sprintf(vcpu->flags[i], "%d", (m->cpu.flags & (1 << (7 - i))) != 0);
    }
}
