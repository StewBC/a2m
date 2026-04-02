// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

enum {
    AY38910_REG_TONE_A_FINE   = 0x0,
    AY38910_REG_TONE_A_COARSE = 0x1,
    AY38910_REG_TONE_B_FINE   = 0x2,
    AY38910_REG_TONE_B_COARSE = 0x3,
    AY38910_REG_TONE_C_FINE   = 0x4,
    AY38910_REG_TONE_C_COARSE = 0x5,
    AY38910_REG_NOISE_PERIOD  = 0x6,
    AY38910_REG_MIXER         = 0x7,
    AY38910_REG_VOLUME_A      = 0x8,
    AY38910_REG_VOLUME_B      = 0x9,
    AY38910_REG_VOLUME_C      = 0xA,
    AY38910_REG_ENV_FINE      = 0xB,
    AY38910_REG_ENV_COARSE    = 0xC,
    AY38910_REG_ENV_SHAPE     = 0xD,
    AY38910_REG_PORT_A        = 0xE,
    AY38910_REG_PORT_B        = 0xF,
};

typedef struct {
    double weighted_sum;
    uint32_t weight;
} AY38910_RENDER_ACCUM;

typedef struct {
    uint8_t regs[16];
    uint8_t selected_reg;
    uint8_t selected_reg_valid;
    uint8_t active;
    uint8_t chip_rate_identity;

    double cpu_hz;
    double chip_hz;
    double chip_cycles_per_cpu_cycle;
    double chip_cycle_accum;

    uint16_t tone_period[3];
    uint16_t tone_counter[3];
    uint8_t tone_output[3];
    uint32_t noise_period;
    uint32_t noise_counter;
    uint32_t noise_lfsr;
    uint8_t noise_output;
    uint32_t env_period;
    uint32_t env_counter;
    uint8_t env_level;
    uint8_t env_continue;
    uint8_t env_hold;
    uint8_t env_alternate;
    int8_t env_delta;
    uint8_t env_holding;
    float sample;
} AY38910;

void ay38910_reset(AY38910 *ay, double cpu_hz, double chip_hz);
void ay38910_select_register(AY38910 *ay, uint8_t reg);
uint8_t ay38910_read_selected(const AY38910 *ay);
void ay38910_write_selected(AY38910 *ay, uint8_t value);
uint8_t ay38910_is_active(const AY38910 *ay);
uint8_t ay38910_get_channel_mixer_gate(const AY38910 *ay, int channel);
uint8_t ay38910_get_channel_amplitude_level(const AY38910 *ay, int channel);
float ay38910_get_channel_output_level(const AY38910 *ay, int channel);
void ay38910_render_accum_reset(AY38910_RENDER_ACCUM *accum);
void ay38910_render_accum_add_current(const AY38910 *ay, AY38910_RENDER_ACCUM *accum, uint32_t chip_cycles);
float ay38910_render_accum_output(const AY38910_RENDER_ACCUM *accum);
void ay38910_step_cycles_render(AY38910 *ay, uint32_t cycles, AY38910_RENDER_ACCUM *accum);
void ay38910_step_cycles(AY38910 *ay, uint32_t cycles);
float ay38910_get_sample(const AY38910 *ay);
