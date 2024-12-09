// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define TARGET_FPS      (1000000/60)                        // (cycles/sec) / (updates/sec) = cycles/update

int main(int argc, char *argv[]) {
    int quit = 0;
    APPLE2 m;                                               // The Apple II machine
    VIEWPORT v;                                             // The view that will display the Apple II machine
    Uint64 start_time, end_time;
    Uint64 ticks_per_clock_cycle = SDL_GetPerformanceFrequency() / CPU_FREQUENCY; // Ticks per microsecond

    // Init the assembler error log
    errlog_init();

    // Init the viewport
    if(A2_OK != viewport_init(&v, 1120, 840)) {
        return A2_ERR;
    }

    // Make this machine an Apple II
    if(A2_OK != apple2_configure(&m)) {
        free(m.RAM_MAIN);
        free(m.RAM_WATCH);
        return A2_ERR;
    }

    // Give the Apple II a viewport
    m.viewport = &v;

    // Set up a table to speed up rendering HGR
    viewapl2_init_color_table(&m);

    // Enable text input since it handles the keys better
    SDL_StartTextInput();

    // Start the audio (ie, un-pause)
    SDL_PauseAudio(0);

    // Start running the sim loop
    while(!quit) {
        // Take note of the time to help sync to Apple II speed
        start_time = SDL_GetPerformanceCounter();

        // Process the input for this view (if apple2 stopped, this won't return asap)
        quit = viewport_process_events(&m);

        // Step the sim one full instruction - several cycles
        int cycles = 0;
        do {
            machine_step(&m);
            cycles++;
            viewapl2_speaker_update(&m);
        } while(m.cpu.instruction_cycle != -1);

        // Give debugger a chance to process the new state and set m->step
        viewdbg_update(&m);

        // Force an update of the current page at the desired frame rate
        if(++m.screen_updated >= TARGET_FPS || m.stopped) {
            // Assume hardware drives the display
            v.shadow_screen_mode = m.screen_mode;
            v.shadow_active_page = m.active_page;
            viewport_show(&m);
            viewapl2_screen_apple2(&m);
            viewport_update(&m);
        }

        if(!m.free_run) {
            // Try to lock the SIM to the Apple II 1.023 MHz
            end_time = SDL_GetPerformanceCounter();

            // Sleep is only ms but I want us delays here
            while((end_time - start_time) < (ticks_per_clock_cycle * cycles)) {
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
