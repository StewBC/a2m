// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define TARGET_FPS              60                          // (cycles/sec) / (updates/sec) = cycles/update
#define OPS_PER_SLICE_FREE_RUN  20000                       // Maybe 20k..200k

int main(int argc, char *argv[]) {
    int quit = 0;
    APPLE2 m;                                               // The Apple II machine
    VIEWPORT v;                                             // The view that will display the Apple II machine
    Uint64 start_time, end_time, update_time = 0;
    Uint64 ticks_per_clock_cycle = SDL_GetPerformanceFrequency() / CPU_FREQUENCY; // Ticks per microsecond

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

    // Start running the sim loop
    while (!quit) {
        uint64_t start_time = SDL_GetPerformanceCounter();

        int ops = m.free_run && !m.step ? OPS_PER_SLICE_FREE_RUN : 1;
        int c0 = m.cpu.cycles;
        for (int i = 0; i < ops && (!m.stopped | m.step); ++i) {
            machine_run_opcode(&m);
            // See if a breakpoint was hit (will set m.stopped)
            viewdbg_update(&m);
        }
        int cycles = m.cpu.cycles - c0;
        speaker_on_cycles(&m.speaker, cycles);
        speaker_pump(&m.speaker);

        quit = viewport_process_events(&m);

        // draw if stopped or at desired fps
        if (SDL_GetPerformanceCounter() >= update_time || m.stopped) {
            v.shadow_screen_mode = m.screen_mode;
            v.shadow_active_page = m.active_page;
            viewport_show(&m);
            viewapl2_screen_apple2(&m);
            viewport_update(&m);
            update_time = SDL_GetPerformanceCounter() + SDL_GetPerformanceFrequency()/TARGET_FPS;
        }

        // 1 MHz pacing only when NOT in free-run
        if (!m.free_run) {
            uint64_t end_time = SDL_GetPerformanceCounter();
            while ((end_time - start_time) < (ticks_per_clock_cycle * cycles)) {
                end_time = SDL_GetPerformanceCounter();
            }
        }
    }

    // Cleanup, Apple II first and then the viewport for it
    apple2_shutdown(&m);
    viewport_shutdown(&v);
    errlog_shutdown();

    return 0;
}
