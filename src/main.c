// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define TARGET_FPS              60

int main(int argc, char *argv[]) {
    int quit = 0;
    APPLE2 m;                                               // The Apple II machine
    VIEWPORT v;                                             // The view that will display the Apple II machine

    // Init the assembler error log
    errlog_init();

    // Make this machine an Apple II
    if(A2_OK != apple2_configure(&m)) {
        free(m.RAM_MAIN);
        free(m.RAM_WATCH);
        return A2_ERR;
    }

    // Give the Apple II a viewport
    m.viewport = &v;

    // Init the viewport
    if(A2_OK != viewport_init(&m, 1120, 840)) {
        return A2_ERR;
    }

    // Set up a table to speed up rendering HGR
    viewapl2_init_color_table(&m);

    // Enable text input since it handles the keys better
    SDL_StartTextInput();

    // The speaker is in the machine but only a machine in view makes sounds
    if(A2_OK != speaker_init(&m.speaker, CPU_FREQUENCY, 48000, 2, 40.0f, 256)) {
        return A2_ERR;
    }

    // Start the audio (ie, un-pause)
    SDL_PauseAudio(0);

    const uint64_t pfreq = SDL_GetPerformanceFrequency();
    const uint64_t ticks_per_ms = pfreq / 1000;
    const double clock_cycles_per_tick = CPU_FREQUENCY / (double)pfreq;
    const uint64_t ticks_per_frame = pfreq / TARGET_FPS;
    uint64_t overhead_ticks = 0; // Assume emulation and rendering fit in 1 frame's time
    while(!quit) {
        uint64_t frame_start_ticks = SDL_GetPerformanceCounter();
        uint64_t desired_frame_end_ticks = frame_start_ticks + ticks_per_frame;

        // If not going at max speed
        if(m.turbo_active > 0.0) {
            double cycles_per_frame = max(1, (CPU_FREQUENCY * m.turbo_active) / TARGET_FPS - (overhead_ticks * clock_cycles_per_tick));
            uint64_t cycles = 0;
            while(cycles < cycles_per_frame && (!m.stopped || m.step)) {
                // See if a breakpoint was hit (will set m.stopped)
                if(viewdbg_update(&m)) {
                    continue;
                }
                size_t opcode_cycles = machine_run_opcode(&m);
                speaker_on_cycles(&m.speaker, opcode_cycles);
                cycles += opcode_cycles;
            }
        } else {
            uint64_t emulation_cycles = max(1, frame_start_ticks + ticks_per_frame - overhead_ticks);
            while(SDL_GetPerformanceCounter() < emulation_cycles && (!m.stopped || m.step)) {
                // See if a breakpoint was hit (will set m.stopped)
                if(viewdbg_update(&m)) {
                    continue;
                }
                size_t opcode_cycles = machine_run_opcode(&m);
                speaker_on_cycles(&m.speaker, opcode_cycles);
            }
        }
        // after a step, the pc the debugger will want to show should be the cpu pc
        v.debugger.cursor_pc = m.cpu.pc;

        quit = viewport_process_events(&m);
        if(v.display_override) {
            A2FLAGSPACK state;
            state.u32 = m.state_flags;
            state.b.col80set = v.shadow_flags.b.col80set;
            state.b.altcharset = v.shadow_flags.b.altcharset;
            state.b.text = v.shadow_flags.b.text;
            state.b.mixed = v.shadow_flags.b.mixed;
            state.b.page2set = v.shadow_flags.b.page2set;
            state.b.hires = v.shadow_flags.b.hires;
            state.b.dhires = v.shadow_flags.b.dhires;
            v.shadow_flags.u32 = state.u32;
        } else {
            v.shadow_flags.u32 = m.state_flags;
        }
        viewport_show(&m);
        viewapl2_screen_apple2(&m);
        viewport_update(&m);
        uint64_t frame_end_ticks = SDL_GetPerformanceCounter();

        // Delay to get the MHz emulation right for the desired FPS - no vsync
        if(frame_end_ticks < desired_frame_end_ticks) {
            uint32_t sleep_ms = (desired_frame_end_ticks - frame_end_ticks) / ticks_per_ms;
            // Sleep for coarse delay
            if(sleep_ms > 1) {
                SDL_Delay(sleep_ms - 1);
            }
            // The emulation + rendering fit in a frame, no compensation
            overhead_ticks = 0;
        } else {
            // Over-ran the frame
            // In MHz mode, do more emulation per frame to hit the MHz desired
            // In max turbo, do less emulation to try and keep the FPS target
            overhead_ticks = frame_end_ticks - desired_frame_end_ticks;
        }
        // Tail spin for accuracy
        while(SDL_GetPerformanceCounter() < desired_frame_end_ticks) {
        }
    }

    // Cleanup, Apple II first and then the viewport for it
    apple2_shutdown(&m);
    viewport_shutdown(&v);
    errlog_shutdown();

    return 0;
}
