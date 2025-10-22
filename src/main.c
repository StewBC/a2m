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

    // Reverse and inverse the //e character rom, so it matches the II+ rom style
    viewapl2_init_character_rom_2e(&m);

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
    const double ms_per_frame = (1000.0 / TARGET_FPS);
    const double clock_cycles_per_tick = CPU_FREQUENCY / (double)pfreq;
    const uint64_t ticks_per_frame = pfreq / TARGET_FPS; // ms_per_frame * ticks_per_ms;
    uint64_t overhead_ticks = ticks_per_ms / 2 ; // Assume 1/2 ms to render
    while(!quit) {
        uint64_t frame_start_ticks = SDL_GetPerformanceCounter();
        uint64_t desired_frame_end_ticks = frame_start_ticks + ticks_per_frame;

        // If not going at max speed
        if(m.turbo_active > 0.0f) {
            double cycles_per_ms = (CPU_FREQUENCY * m.turbo_active) / 1000.0;
            double cycles_per_frame = ms_per_frame * cycles_per_ms;
            // uint64_t emulation_window = max(0, cycles_per_frame - overhead_ticks * clock_cycles_per_tick);
            double overhead_cycles = (double)overhead_ticks * clock_cycles_per_tick;
            uint64_t emulation_window = ((cycles_per_frame > overhead_cycles) ? (uint64_t)(cycles_per_frame - overhead_cycles) : 0);
            uint64_t cycles = 0;

            while(cycles < emulation_window && (!m.stopped || m.step)) {
                size_t opcode_cycles = machine_run_opcode(&m);
                speaker_on_cycles(&m.speaker, opcode_cycles);
                cycles += opcode_cycles;
                // See if a breakpoint was hit (will set m.stopped)
                viewdbg_update(&m);
            }
        } else {
            uint64_t emulation_window = max(0, frame_start_ticks + ticks_per_frame - overhead_ticks);
            while(SDL_GetPerformanceCounter() < emulation_window && (!m.stopped || m.step)) {
                size_t opcode_cycles = machine_run_opcode(&m);
                speaker_on_cycles(&m.speaker, opcode_cycles);
                // See if a breakpoint was hit (will set m.stopped)
                viewdbg_update(&m);
            }
        }

        uint64_t overhead_start_ticks = SDL_GetPerformanceCounter();
        quit = viewport_process_events(&m);
        v.shadow_screen_mode = m.screen_mode;
        v.shadow_active_page = m.active_page;
        viewport_show(&m);
        viewapl2_screen_apple2(&m);
        viewport_update(&m);
        // Delay for the right MHz emulation
        uint64_t frame_end_ticks = SDL_GetPerformanceCounter();
        if(desired_frame_end_ticks > frame_end_ticks) {
            uint32_t sleep_ms = (desired_frame_end_ticks - frame_end_ticks) / ticks_per_ms;
            if(sleep_ms > 0) {
                SDL_Delay(sleep_ms);
            }
        }
        while(SDL_GetPerformanceCounter() < desired_frame_end_ticks) {
        }
        // Overhead does not include the delay
        overhead_ticks = frame_end_ticks - overhead_start_ticks;
    }

    // Cleanup, Apple II first and then the viewport for it
    apple2_shutdown(&m);
    viewport_shutdown(&v);
    errlog_shutdown();

    return 0;
}
