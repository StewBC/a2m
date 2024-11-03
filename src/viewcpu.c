// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

int viewcpu_process_event(APPLE2 *m, SDL_Event *e) {
    if(e->type != SDL_KEYDOWN) {
        return 0;
    }

    SDL_Keymod mod = SDL_GetModState();
    VIEWPORT *v = m->viewport;
    DEBUGGER *d = &v->debugger;

    return 0;
}

void viewcpu_show(APPLE2 *m) {
    unsigned int value;
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    DEBUGGER *d = &v->debugger;
    VIEWCPU *vcpu = &v->viewcpu;

    int w = m->viewport->full_window_rect.w - m->viewport->target_rect.w;
    if(nk_begin(ctx, "CPU", nk_rect(m->viewport->target_rect.w, 0, w, 89), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        if(m->stopped) {
            // I don't know what I have to set this
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 10);
            nk_layout_row_push(ctx, 0.05f);
            nk_label(ctx, "PC", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
            nk_layout_row_push(ctx, 0.15f);
            if(NK_EDIT_COMMITED &
               nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[0],
                              &vcpu->register_lengths[0], 5, nk_filter_hex)) {
                vcpu->registers[0][vcpu->register_lengths[0]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->registers[0], "%x", &value);
                m->cpu.pc = value;
            }
            nk_layout_row_push(ctx, 0.05f);
            nk_label(ctx, "SP", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
            nk_layout_row_push(ctx, 0.15f);
            if(NK_EDIT_COMMITED &
               nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[1],
                              &vcpu->register_lengths[1], 5, nk_filter_hex)) {
                vcpu->registers[1][vcpu->register_lengths[1]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->registers[1], "%x", &value);
                m->cpu.sp = value;
            }
            nk_layout_row_push(ctx, 0.05f);
            nk_label(ctx, "A", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
            nk_layout_row_push(ctx, 0.10f);
            if(NK_EDIT_COMMITED &
               nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[2],
                              &vcpu->register_lengths[2], 3, nk_filter_hex)) {
                vcpu->registers[2][vcpu->register_lengths[2]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->registers[2], "%x", &value);
                m->cpu.A = value;
            }
            nk_layout_row_push(ctx, 0.05f);
            nk_label(ctx, "X", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
            nk_layout_row_push(ctx, 0.10f);
            if(NK_EDIT_COMMITED &
               nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[3],
                              &vcpu->register_lengths[3], 3, nk_filter_hex)) {
                vcpu->registers[3][vcpu->register_lengths[3]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->registers[3], "%x", &value);
                m->cpu.X = value;
            }
            nk_layout_row_push(ctx, 0.05f);
            nk_label(ctx, "Y", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
            nk_layout_row_push(ctx, 0.10f);
            if(NK_EDIT_COMMITED &
               nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->registers[4],
                              &vcpu->register_lengths[4], 3, nk_filter_hex)) {
                vcpu->registers[4][vcpu->register_lengths[4]] = 0;
                ctx->current->edit.active = 0;
                sscanf(vcpu->registers[4], "%x", &value);
                m->cpu.Y = value;
            }
            nk_layout_row_end(ctx);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 10);
            // nk_layout_row_dynamic(ctx, 13, 1);
            for(int i = 0; i < 8; i++) {
                nk_layout_row_push(ctx, 0.025f);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, "%c", (i)["NVEBDIZC"]);
                nk_layout_row_push(ctx, 0.075f);
                if(NK_EDIT_COMMITED &
                   nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, vcpu->flags[i], &vcpu->flag_lengths[i],
                                  2, nk_filter_binary)) {
                    vcpu->flags[i][vcpu->flag_lengths[i]] = 0;
                    ctx->current->edit.active = 0;
                    sscanf(vcpu->flags[i], "%d", &value);
                    m->cpu.flags &= ~(1 << (7 - i));
                    m->cpu.flags |= (value << (7 - i));
                }
            }
        } else {
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 1);
            nk_layout_row_push(ctx, 1.0f);
            nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE,
                      "PC %04X   SP %04X   A %02X   X %02X   Y %02X", m->cpu.pc, m->cpu.sp, m->cpu.A, m->cpu.X, m->cpu.Y);
            nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE,
                      "N %d  V %d  E %d  B %d  D %d  I %d  Z %d  C %d ",
                      (m->cpu.flags & 0x80) >> 7,
                      (m->cpu.flags & 0x40) >> 6,
                      (m->cpu.flags & 0x20) >> 5,
                      (m->cpu.flags & 0x10) >> 4,
                      (m->cpu.flags & 0x08) >> 3, (m->cpu.flags & 0x04) >> 2, (m->cpu.flags & 0x02) >> 1, (m->cpu.flags & 0x01) >> 0);
            nk_layout_row_end(ctx);
        }
    }
    nk_end(ctx);
}

void viewcpu_update(APPLE2 *m) {
    VIEWCPU *vcpu = &m->viewport->viewcpu;
    vcpu->register_lengths[0] = sprintf(vcpu->registers[0], "%04X", m->cpu.pc);
    vcpu->register_lengths[1] = sprintf(vcpu->registers[1], "%04X", m->cpu.sp);
    vcpu->register_lengths[2] = sprintf(vcpu->registers[2], "%02X", m->cpu.A);
    vcpu->register_lengths[3] = sprintf(vcpu->registers[3], "%02X", m->cpu.X);
    vcpu->register_lengths[4] = sprintf(vcpu->registers[4], "%02X", m->cpu.Y);
    for(int i = 0; i < 8; i++) {
        vcpu->flag_lengths[i] = sprintf(vcpu->flags[i], "%d", (m->cpu.flags & (1 << (7 - i))) != 0);
    }
}
