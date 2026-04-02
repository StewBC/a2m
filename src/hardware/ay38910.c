// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "hardware_lib.h"

static const float ay38910_volume_table[16] = {
    0.000000f, 0.007813f, 0.011049f, 0.015625f,
    0.022097f, 0.031250f, 0.044194f, 0.062500f,
    0.088388f, 0.125000f, 0.176777f, 0.250000f,
    0.353553f, 0.500000f, 0.707107f, 1.000000f,
};

static uint8_t ay38910_mask_register_value(uint8_t reg, uint8_t value) {
    switch(reg) {
        case AY38910_REG_TONE_A_COARSE:
        case AY38910_REG_TONE_B_COARSE:
        case AY38910_REG_TONE_C_COARSE:
        case AY38910_REG_ENV_SHAPE:
            return value & 0x0F;

        case AY38910_REG_NOISE_PERIOD:
        case AY38910_REG_VOLUME_A:
        case AY38910_REG_VOLUME_B:
        case AY38910_REG_VOLUME_C:
            return value & 0x1F;

        default:
            return value;
    }
}

static uint32_t ay38910_noise_period_from_regs(const AY38910 *ay) {
    uint32_t period = (uint32_t)(ay->regs[AY38910_REG_NOISE_PERIOD] & 0x1F);
    if(!period) {
        period = 1;
    }
    return period * 16u;
}

static uint32_t ay38910_env_period_from_regs(const AY38910 *ay) {
    uint32_t period = (uint32_t)(((uint32_t)ay->regs[AY38910_REG_ENV_COARSE] << 8) |
                                 ay->regs[AY38910_REG_ENV_FINE]);
    if(!period) {
        period = 1;
    }
    return period * 256u;
}

static uint16_t ay38910_tone_period_from_regs(const AY38910 *ay, int channel) {
    int reg = channel * 2;
    uint16_t fine = ay->regs[reg];
    uint16_t coarse = ay->regs[reg + 1] & 0x0F;
    uint16_t period = (uint16_t)((coarse << 8) | fine);
    if(!period) {
        period = 1;
    }
    return (uint16_t)(period * 16u);
}

static void ay38910_refresh_tone_periods(AY38910 *ay) {
    for(int ch = 0; ch < 3; ch++) {
        ay->tone_period[ch] = ay38910_tone_period_from_regs(ay, ch);
        if(ay->tone_counter[ch] == 0 || ay->tone_counter[ch] > ay->tone_period[ch]) {
            ay->tone_counter[ch] = ay->tone_period[ch];
        }
    }
}

static void ay38910_reload_tone_period(AY38910 *ay, int channel) {
    ay->tone_period[channel] = ay38910_tone_period_from_regs(ay, channel);
    ay->tone_counter[channel] = ay->tone_period[channel];
    ay->tone_output[channel] = 1;
}

static void ay38910_refresh_noise_period(AY38910 *ay) {
    ay->noise_period = ay38910_noise_period_from_regs(ay);
    if(!ay->noise_counter || ay->noise_counter > ay->noise_period) {
        ay->noise_counter = ay->noise_period;
    }
}

static void ay38910_reload_noise_period(AY38910 *ay) {
    ay->noise_period = ay38910_noise_period_from_regs(ay);
    ay->noise_counter = ay->noise_period;
    ay->noise_lfsr = 0x1FFFFu;
}

static void ay38910_refresh_env_period(AY38910 *ay) {
    ay->env_period = ay38910_env_period_from_regs(ay);
    if(!ay->env_counter || ay->env_counter > ay->env_period) {
        ay->env_counter = ay->env_period;
    }
}

static void ay38910_reload_env_period(AY38910 *ay) {
    ay->env_period = ay38910_env_period_from_regs(ay);
    ay->env_counter = ay->env_period;
}

static void ay38910_restart_envelope(AY38910 *ay) {
    uint8_t shape = ay->regs[AY38910_REG_ENV_SHAPE] & 0x0F;
    uint8_t attack = (shape >> 2) & 0x01;

    // Writing the envelope shape register restarts the envelope using the
    // current shape bits and the current envelope period. The fuller
    // shape/cycle semantics remain isolated to the envelope state machine.
    ay->env_continue = (shape >> 3) & 0x01;
    ay->env_alternate = (shape >> 1) & 0x01;
    ay->env_hold = shape & 0x01;
    ay->env_delta = attack ? 1 : -1;
    ay->env_level = attack ? 0u : 15u;
    ay->env_holding = 0;
    ay->env_counter = ay->env_period ? ay->env_period : ay38910_env_period_from_regs(ay);
}

static uint8_t ay38910_envelope_initial_level(const AY38910 *ay) {
    return (uint8_t)((ay->regs[AY38910_REG_ENV_SHAPE] & 0x04u) ? 0u : 15u);
}

static uint8_t ay38910_envelope_terminal_level(const AY38910 *ay) {
    return (uint8_t)((ay->regs[AY38910_REG_ENV_SHAPE] & 0x04u) ? 15u : 0u);
}

static void ay38910_finish_envelope_cycle(AY38910 *ay) {
    if(!ay->env_continue) {
        ay->env_level = 0u;
        ay->env_holding = 1;
        return;
    }

    if(ay->env_hold) {
        ay->env_level = ay->env_alternate ? ay38910_envelope_initial_level(ay)
                                          : ay38910_envelope_terminal_level(ay);
        ay->env_holding = 1;
        return;
    }

    if(ay->env_alternate) {
        ay->env_delta = (int8_t)-ay->env_delta;
    }

    ay->env_level = ay->env_delta > 0 ? 0u : 15u;
}

static void ay38910_step_envelope_level(AY38910 *ay) {
    int next_level;

    if(ay->env_holding) {
        return;
    }

    next_level = (int)ay->env_level + ay->env_delta;
    if(next_level >= 0 && next_level <= 15) {
        ay->env_level = (uint8_t)next_level;
        return;
    }

    ay38910_finish_envelope_cycle(ay);
}

uint8_t ay38910_get_channel_mixer_gate(const AY38910 *ay, int channel) {
    uint8_t mixer = ay->regs[AY38910_REG_MIXER];
    uint8_t tone_gate = (uint8_t)((mixer & (1u << channel)) ? 1u : ay->tone_output[channel]);
    uint8_t noise_gate = (uint8_t)((mixer & (1u << (channel + 3))) ? 1u : ay->noise_output);

    assert(channel >= 0 && channel < 3);
    return (uint8_t)(tone_gate & noise_gate);
}

uint8_t ay38910_get_channel_amplitude_level(const AY38910 *ay, int channel) {
    uint8_t volume_reg = ay->regs[AY38910_REG_VOLUME_A + channel];
    uint8_t level;

    assert(channel >= 0 && channel < 3);
    if(volume_reg & 0x10) {
        return (uint8_t)(ay->env_level & 0x0F);
    }

    level = volume_reg & 0x0F;
    return level;
}

float ay38910_get_channel_output_level(const AY38910 *ay, int channel) {
    uint8_t channel_gate = ay38910_get_channel_mixer_gate(ay, channel);
    uint8_t level = ay38910_get_channel_amplitude_level(ay, channel);

    if(!channel_gate) {
        return 0.0f;
    }

    return ay38910_volume_table[level];
}

static void ay38910_refresh_sample(AY38910 *ay) {
    // This core computes the chip's current logical output level from already-modeled AY
    // state. Treat this as a chip-state generator, not as the final host reconstruction
    // stage. Fidelity work above this layer must not rewrite AY register semantics in order
    // to "improve" the sound.
    float mixed = 0.0f;

    uint8_t mixer = ay->regs[AY38910_REG_MIXER];
    uint8_t noise_output = ay->noise_output;
    uint8_t env_level = ay->env_level & 0x0F;

    for(int ch = 0; ch < 3; ch++) {
        uint8_t tone_gate = (uint8_t)((mixer & (1u << ch)) ? 1u : ay->tone_output[ch]);
        uint8_t noise_gate = (uint8_t)((mixer & (1u << (ch + 3))) ? 1u : noise_output);

        if(tone_gate & noise_gate) {
            uint8_t volume_reg = ay->regs[AY38910_REG_VOLUME_A + ch];
            uint8_t level = (uint8_t)((volume_reg & 0x10) ? env_level : (volume_reg & 0x0F));
            mixed += ay38910_volume_table[level];
        }
    }
    ay->sample = mixed / 3.0f;
}

void ay38910_reset(AY38910 *ay, double cpu_hz, double chip_hz) {
    memset(ay, 0, sizeof(*ay));

    ay->cpu_hz = (cpu_hz > 0.0) ? cpu_hz : CPU_FREQUENCY;
    // Assumption for this isolated module: the caller provides the AY chip clock
    // directly, and cycle stepping converts CPU cycles to AY chip cycles using
    // a fixed ratio. No board-level divider is modeled here yet.
    ay->chip_hz = (chip_hz > 0.0) ? chip_hz : ay->cpu_hz;
    ay->chip_cycles_per_cpu_cycle = ay->chip_hz / ay->cpu_hz;
    ay->chip_rate_identity = (uint8_t)(ay->chip_hz == ay->cpu_hz);

    for(int ch = 0; ch < 3; ch++) {
        ay->tone_output[ch] = 1;
    }
    ay38910_refresh_tone_periods(ay);
    ay->noise_lfsr = 0x1FFFFu;
    ay->noise_output = 1;
    ay38910_refresh_noise_period(ay);
    ay38910_refresh_env_period(ay);
    ay38910_restart_envelope(ay);
    ay38910_refresh_sample(ay);
}

void ay38910_render_accum_reset(AY38910_RENDER_ACCUM *accum) {
    if(!accum) {
        return;
    }

    accum->weighted_sum = 0.0;
    accum->weight = 0;
}

void ay38910_render_accum_add_current(const AY38910 *ay, AY38910_RENDER_ACCUM *accum, uint32_t chip_cycles) {
    if(!ay || !accum || !chip_cycles) {
        return;
    }

    // This accumulation API is intentionally separate from chip stepping. It lets higher
    // layers build integrated or sub-sampled renders from already-emulated chip state
    // without redefining AY register/timer behavior.
    accum->weighted_sum += (double)ay->sample * (double)chip_cycles;
    accum->weight += chip_cycles;
}

float ay38910_render_accum_output(const AY38910_RENDER_ACCUM *accum) {
    if(!accum || !accum->weight) {
        return 0.0f;
    }

    return (float)(accum->weighted_sum / (double)accum->weight);
}

static uint32_t ay38910_next_event_chip_cycles(const AY38910 *ay) {
    uint32_t next = ay->env_counter;

    if(ay->tone_counter[0] < next) {
        next = ay->tone_counter[0];
    }
    if(ay->tone_counter[1] < next) {
        next = ay->tone_counter[1];
    }
    if(ay->tone_counter[2] < next) {
        next = ay->tone_counter[2];
    }
    if(ay->noise_counter < next) {
        next = ay->noise_counter;
    }

    return next ? next : 1u;
}

static inline void ay38910_advance_no_event(AY38910 *ay, uint32_t chip_cycles, AY38910_RENDER_ACCUM *accum) {
    if(accum) {
        ay38910_render_accum_add_current(ay, accum, chip_cycles);
    }

    ay->tone_counter[0] = (uint16_t)(ay->tone_counter[0] - chip_cycles);
    ay->tone_counter[1] = (uint16_t)(ay->tone_counter[1] - chip_cycles);
    ay->tone_counter[2] = (uint16_t)(ay->tone_counter[2] - chip_cycles);
    ay->env_counter -= chip_cycles;
    ay->noise_counter -= chip_cycles;
}

static void ay38910_advance_chip_cycles(AY38910 *ay, uint32_t chip_cycles, AY38910_RENDER_ACCUM *accum) {
    while(chip_cycles > 0) {
        uint32_t step = ay38910_next_event_chip_cycles(ay);
        uint8_t changed = 0;

        if(chip_cycles < step) {
            ay38910_advance_no_event(ay, chip_cycles, accum);
            return;
        }

        if(step > chip_cycles) {
            step = chip_cycles;
        }

        if(accum) {
            ay38910_render_accum_add_current(ay, accum, step);
        }

        if(step < ay->tone_counter[0]) {
            ay->tone_counter[0] = (uint16_t)(ay->tone_counter[0] - step);
        } else {
            ay->tone_counter[0] = ay->tone_period[0];
            ay->tone_output[0] ^= 1;
            changed = 1;
        }

        if(step < ay->tone_counter[1]) {
            ay->tone_counter[1] = (uint16_t)(ay->tone_counter[1] - step);
        } else {
            ay->tone_counter[1] = ay->tone_period[1];
            ay->tone_output[1] ^= 1;
            changed = 1;
        }

        if(step < ay->tone_counter[2]) {
            ay->tone_counter[2] = (uint16_t)(ay->tone_counter[2] - step);
        } else {
            ay->tone_counter[2] = ay->tone_period[2];
            ay->tone_output[2] ^= 1;
            changed = 1;
        }

        if(step < ay->env_counter) {
            ay->env_counter -= step;
        } else {
            ay->env_counter = ay->env_period;
            ay38910_step_envelope_level(ay);
            changed = 1;
        }

        if(step < ay->noise_counter) {
            ay->noise_counter -= step;
        } else {
            uint32_t feedback = (ay->noise_lfsr ^ (ay->noise_lfsr >> 3)) & 0x01u;
            ay->noise_counter = ay->noise_period;
            ay->noise_lfsr = (ay->noise_lfsr >> 1) | (feedback << 16);
            ay->noise_output = (uint8_t)(ay->noise_lfsr & 0x01u);
            changed = 1;
        }

        chip_cycles -= step;

        if(changed) {
            ay38910_refresh_sample(ay);
        }
    }
}

void ay38910_step_cycles_render(AY38910 *ay, uint32_t cycles, AY38910_RENDER_ACCUM *accum) {
    uint32_t chip_cycles;

    if(ay->chip_rate_identity) {
        chip_cycles = cycles;
    } else {
        ay->chip_cycle_accum += (double)cycles * ay->chip_cycles_per_cpu_cycle;
        chip_cycles = (uint32_t)ay->chip_cycle_accum;
        ay->chip_cycle_accum -= (double)chip_cycles;
    }

    if(!chip_cycles) {
        return;
    }

    ay38910_advance_chip_cycles(ay, chip_cycles, accum);
}

void ay38910_select_register(AY38910 *ay, uint8_t reg) {
    assert((reg & 0x0F) == reg);
    ay->selected_reg = reg & 0x0F;
    ay->selected_reg_valid = 1;
    ay->active = 1;
}

uint8_t ay38910_read_selected(const AY38910 *ay) {
    if(!ay->selected_reg_valid) {
        return 0;
    }
    assert(ay->selected_reg < 16);
    // Register readback returns the current stored register contents. AY I/O port
    // input-follow behavior remains deferred until a modeled external port source
    // exists in this core.
    return ay->regs[ay->selected_reg];
}

void ay38910_write_selected(AY38910 *ay, uint8_t value) {
    uint8_t refresh_sample = 1;

    if(!ay->selected_reg_valid) {
        return;
    }
    assert(ay->selected_reg < 16);
    ay->active = 1;
    ay->regs[ay->selected_reg] = ay38910_mask_register_value(ay->selected_reg, value);

    switch(ay->selected_reg) {
        case AY38910_REG_TONE_A_FINE:
        case AY38910_REG_TONE_A_COARSE:
        case AY38910_REG_TONE_B_FINE:
        case AY38910_REG_TONE_B_COARSE:
        case AY38910_REG_TONE_C_FINE:
        case AY38910_REG_TONE_C_COARSE:
            ay38910_reload_tone_period(ay, (int)(ay->selected_reg >> 1));
            break;

        case AY38910_REG_MIXER:
        case AY38910_REG_VOLUME_A:
        case AY38910_REG_VOLUME_B:
        case AY38910_REG_VOLUME_C:
            break;

        case AY38910_REG_NOISE_PERIOD:
            ay38910_reload_noise_period(ay);
            break;

        case AY38910_REG_ENV_FINE:
        case AY38910_REG_ENV_COARSE:
            ay38910_reload_env_period(ay);
            break;

        case AY38910_REG_ENV_SHAPE:
            ay38910_refresh_env_period(ay);
            ay38910_restart_envelope(ay);
            break;

        case AY38910_REG_PORT_A:
        case AY38910_REG_PORT_B:
            // External I/O ports exist on the AY-3-8910 but are unused on the Mockingboard.
            // Writes are stored as register contents, but no external device is attached.
            refresh_sample = 0;
            break;
    }

    if(refresh_sample) {
        ay38910_refresh_sample(ay);
    }
}

uint8_t ay38910_is_active(const AY38910 *ay) {
    return ay->active;
}

void ay38910_step_cycles(AY38910 *ay, uint32_t cycles) {
    ay38910_step_cycles_render(ay, cycles, NULL);
}

float ay38910_get_sample(const AY38910 *ay) {
    return ay->sample;
}
