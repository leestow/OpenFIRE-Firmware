#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"

#include "hm01b0_parallel.pio.h"
#include "hm01b0_regs.h"

// -----------------------------------------------------------------------------
// Arducam HM01B0 / Pico wiring used by the public Arducam RP2040 reference demo.
// Verify the labels on your exact module before powering it.
// -----------------------------------------------------------------------------
#define PIN_CAM_RESET   2u
#define PIN_CAM_MCLK    3u
#define PIN_CAM_SDA     4u
#define PIN_CAM_SCL     5u

#define PIN_CAM_D0      6u   // D0..D7 must be GP6..GP13
#define PIN_CAM_PCLK   14u
#define PIN_CAM_LVLD   15u   // HREF / line valid
#define PIN_CAM_FVLD   16u   // VSYNC / frame valid

#define HM01B0_I2C      i2c0
#define HM01B0_ADDR     0x24u

// HM01B0 binned array including border pixels.
#define FRAME_WIDTH     162u
#define FRAME_HEIGHT    162u
#define FRAME_BYTES     (FRAME_WIDTH * FRAME_HEIGHT)

#ifndef IR_THRESHOLD
#define IR_THRESHOLD    220u
#endif

#ifndef REPORT_EVERY_FRAMES
#define REPORT_EVERY_FRAMES 30u
#endif

#ifndef MCLK_TARGET_HZ
#define MCLK_TARGET_HZ  24000000u
#endif

static uint8_t frame_buffer[FRAME_BYTES];

static PIO camera_pio = pio0;
static uint camera_sm = 0;
static int camera_dma = -1;

static bool hm01b0_write_reg(uint16_t reg, uint8_t value) {
    uint8_t tx[3] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        value
    };

    int written = i2c_write_blocking(HM01B0_I2C, HM01B0_ADDR, tx, 3, false);
    return written == 3;
}

static bool hm01b0_read_reg(uint16_t reg, uint8_t *value) {
    uint8_t addr[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF)
    };

    if (i2c_write_blocking(HM01B0_I2C, HM01B0_ADDR, addr, 2, true) != 2) {
        return false;
    }

    return i2c_read_blocking(HM01B0_I2C, HM01B0_ADDR, value, 1, false) == 1;
}

static bool hm01b0_write_table(const hm01b0_reg_t *table) {
    for (size_t i = 0; table[i].reg != HM01B0_REG_END; ++i) {
        if (!hm01b0_write_reg(table[i].reg, table[i].value)) {
            printf("I2C write failed: reg=0x%04X value=0x%02X\n",
                   table[i].reg, table[i].value);
            return false;
        }
    }
    return true;
}

static void start_mclk(uint32_t target_hz) {
    gpio_set_function(PIN_CAM_MCLK, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(PIN_CAM_MCLK);
    uint channel = pwm_gpio_to_channel(PIN_CAM_MCLK);

    // A wrap value of 1 produces a 50% duty square wave with two PWM counts
    // per output period. Use a fractional divider to get close to target_hz.
    const float sys_hz = (float)clock_get_hz(clk_sys);
    float divider = sys_hz / (2.0f * (float)target_hz);

    if (divider < 1.0f) {
        divider = 1.0f;
    }

    pwm_set_clkdiv(slice, divider);
    pwm_set_wrap(slice, 1);
    pwm_set_chan_level(slice, channel, 1);
    pwm_set_enabled(slice, true);

    const float actual_hz = sys_hz / (divider * 2.0f);
    printf("MCLK target: %" PRIu32 " Hz, requested divider %.3f, approx %.0f Hz\n",
           target_hz, divider, actual_hz);
}

static void reset_camera(void) {
    gpio_init(PIN_CAM_RESET);
    gpio_set_dir(PIN_CAM_RESET, GPIO_OUT);

    gpio_put(PIN_CAM_RESET, 0);
    sleep_ms(20);
    gpio_put(PIN_CAM_RESET, 1);
    sleep_ms(100);
}

static bool init_i2c_and_probe(void) {
    i2c_init(HM01B0_I2C, 400 * 1000);

    gpio_set_function(PIN_CAM_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_CAM_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_CAM_SDA);
    gpio_pull_up(PIN_CAM_SCL);

    uint8_t id_h = 0;
    uint8_t id_l = 0;
    uint8_t revision = 0;

    bool ok = hm01b0_read_reg(0x0000, &id_h) &&
              hm01b0_read_reg(0x0001, &id_l) &&
              hm01b0_read_reg(0x0002, &revision);

    if (!ok) {
        printf("HM01B0 did not answer at I2C address 0x%02X.\n", HM01B0_ADDR);
        return false;
    }

    printf("Sensor ID: 0x%02X%02X, silicon revision: 0x%02X\n",
           id_h, id_l, revision);

    if (id_h != 0x01 || id_l != 0xB0) {
        printf("WARNING: expected HM01B0 ID 0x01B0.\n");
    }

    return true;
}

static bool init_sensor(void) {
    // Software reset. The datasheet allows either 0 or 1 on SW_RESET[0].
    if (!hm01b0_write_reg(0x0103, 0x00)) {
        return false;
    }
    sleep_ms(10);

    if (!hm01b0_write_table(hm01b0_base_init)) {
        return false;
    }

    if (!hm01b0_write_table(hm01b0_binned_2x2_mode)) {
        return false;
    }

    sleep_ms(50);

    uint8_t mode = 0;
    uint8_t frame_count = 0;
    hm01b0_read_reg(0x0100, &mode);
    hm01b0_read_reg(0x0005, &frame_count);

    printf("Mode register 0x0100: 0x%02X\n", mode);
    printf("Initial sensor frame counter: %u\n", frame_count);
    printf("Configured for 2x2 monochrome binning, 8-bit parallel RAW8.\n");
    printf("Expected DMA payload: %u x %u = %u bytes/frame.\n",
           FRAME_WIDTH, FRAME_HEIGHT, FRAME_BYTES);

    return true;
}

static void init_capture_engine(void) {
    // FVLD/VSYNC is watched directly by the CPU.
    gpio_init(PIN_CAM_FVLD);
    gpio_set_dir(PIN_CAM_FVLD, GPIO_IN);

    uint offset = pio_add_program(camera_pio, &hm01b0_parallel8_program);
    hm01b0_parallel8_program_init(camera_pio, camera_sm, offset, PIN_CAM_D0);

    camera_dma = dma_claim_unused_channel(true);
}

static bool wait_for_fvld_edge(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    // First leave any frame that is already active.
    while (gpio_get(PIN_CAM_FVLD)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }
        tight_loop_contents();
    }

    // Then wait for the next frame start.
    while (!gpio_get(PIN_CAM_FVLD)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }
        tight_loop_contents();
    }

    return true;
}

static bool capture_frame(uint32_t timeout_ms) {
    pio_sm_set_enabled(camera_pio, camera_sm, false);
    pio_sm_clear_fifos(camera_pio, camera_sm);
    pio_sm_restart(camera_pio, camera_sm);

    dma_channel_abort(camera_dma);

    dma_channel_config cfg = dma_channel_get_default_config(camera_dma);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_dreq(
        &cfg,
        pio_get_dreq(camera_pio, camera_sm, false)
    );

    dma_channel_configure(
        camera_dma,
        &cfg,
        frame_buffer,
        &camera_pio->rxf[camera_sm],
        FRAME_BYTES,
        false
    );

    if (!wait_for_fvld_edge(timeout_ms)) {
        return false;
    }

    dma_channel_start(camera_dma);
    pio_sm_set_enabled(camera_pio, camera_sm, true);

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (dma_channel_is_busy(camera_dma)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            dma_channel_abort(camera_dma);
            pio_sm_set_enabled(camera_pio, camera_sm, false);
            return false;
        }
        tight_loop_contents();
    }

    pio_sm_set_enabled(camera_pio, camera_sm, false);
    return true;
}

typedef struct {
    uint8_t min_value;
    uint8_t max_value;
    uint32_t mean_value;
    uint32_t pixels_over_threshold;
    uint32_t max_x;
    uint32_t max_y;
    uint64_t weighted_x;
    uint64_t weighted_y;
    uint64_t weight_sum;
} frame_stats_t;

static frame_stats_t analyze_frame(const uint8_t *frame) {
    frame_stats_t s = {
        .min_value = 255,
        .max_value = 0,
        .mean_value = 0,
        .pixels_over_threshold = 0,
        .max_x = 0,
        .max_y = 0,
        .weighted_x = 0,
        .weighted_y = 0,
        .weight_sum = 0
    };

    uint64_t sum = 0;

    for (uint32_t y = 0; y < FRAME_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FRAME_WIDTH; ++x) {
            uint8_t v = frame[y * FRAME_WIDTH + x];
            sum += v;

            if (v < s.min_value) {
                s.min_value = v;
            }

            if (v > s.max_value) {
                s.max_value = v;
                s.max_x = x;
                s.max_y = y;
            }

            if (v >= IR_THRESHOLD) {
                ++s.pixels_over_threshold;

                // Weight only the amount above threshold. This is a primitive
                // one-blob centroid that will be useful for the IR LED test.
                uint32_t w = (uint32_t)v - IR_THRESHOLD + 1u;
                s.weighted_x += (uint64_t)x * w;
                s.weighted_y += (uint64_t)y * w;
                s.weight_sum += w;
            }
        }
    }

    s.mean_value = (uint32_t)(sum / FRAME_BYTES);
    return s;
}

static void print_sample_pixels(const uint8_t *frame) {
    printf("First 16 pixels:");
    for (uint32_t i = 0; i < 16; ++i) {
        printf(" %3u", frame[i]);
    }
    printf("\n");

    const uint32_t cy = FRAME_HEIGHT / 2;
    const uint32_t cx = FRAME_WIDTH / 2;
    printf("Center row sample:");
    for (int dx = -4; dx < 4; ++dx) {
        printf(" %3u", frame[cy * FRAME_WIDTH + (cx + dx)]);
    }
    printf("\n");
}

int main(void) {
    stdio_init_all();

    // Give USB serial time to enumerate. The camera does not depend on this.
    sleep_ms(2000);

    printf("\n");
    printf("========================================\n");
    printf("OpenFIRE HM01B0 stand-alone bring-up\n");
    printf("8-bit parallel + PIO + DMA\n");
    printf("========================================\n");

    start_mclk(MCLK_TARGET_HZ);
    reset_camera();

    if (!init_i2c_and_probe()) {
        printf("STOP: camera probe failed.\n");
        while (true) {
            sleep_ms(1000);
        }
    }

    if (!init_sensor()) {
        printf("STOP: camera register configuration failed.\n");
        while (true) {
            sleep_ms(1000);
        }
    }

    init_capture_engine();

    uint64_t report_start_us = time_us_64();
    uint64_t capture_time_sum_us = 0;
    uint32_t frames_in_report = 0;
    uint32_t total_frames = 0;

    while (true) {
        uint64_t capture_start_us = time_us_64();

        if (!capture_frame(1000)) {
            printf("CAPTURE TIMEOUT. Check FVLD/PCLK/LVLD/data wiring and mode settings.\n");
            sleep_ms(500);
            continue;
        }

        uint64_t capture_end_us = time_us_64();
        capture_time_sum_us += capture_end_us - capture_start_us;
        ++frames_in_report;
        ++total_frames;

        if (frames_in_report >= REPORT_EVERY_FRAMES) {
            uint64_t now_us = time_us_64();
            uint64_t elapsed_us = now_us - report_start_us;

            const double measured_fps =
                ((double)frames_in_report * 1000000.0) / (double)elapsed_us;

            const double avg_capture_ms =
                ((double)capture_time_sum_us / (double)frames_in_report) / 1000.0;

            frame_stats_t stats = analyze_frame(frame_buffer);

            printf("\nFrame %" PRIu32 "\n", total_frames);
            printf("Measured loop/capture rate: %.2f FPS\n", measured_fps);
            printf("Average capture call: %.3f ms\n", avg_capture_ms);
            printf("Brightness min/mean/max: %u / %" PRIu32 " / %u\n",
                   stats.min_value, stats.mean_value, stats.max_value);
            printf("Brightest pixel: (%" PRIu32 ", %" PRIu32 ") value=%u\n",
                   stats.max_x, stats.max_y, stats.max_value);
            printf("Pixels >= %u: %" PRIu32 "\n",
                   IR_THRESHOLD, stats.pixels_over_threshold);

            if (stats.weight_sum > 0) {
                const double cx =
                    (double)stats.weighted_x / (double)stats.weight_sum;
                const double cy =
                    (double)stats.weighted_y / (double)stats.weight_sum;

                printf("Threshold-weighted centroid: (%.2f, %.2f)\n", cx, cy);
            } else {
                printf("Threshold-weighted centroid: none\n");
            }

            print_sample_pixels(frame_buffer);

            uint8_t sensor_frame_count = 0;
            if (hm01b0_read_reg(0x0005, &sensor_frame_count)) {
                printf("Sensor FRAME_COUNT register: %u\n", sensor_frame_count);
            }

            frames_in_report = 0;
            capture_time_sum_us = 0;
            report_start_us = now_us;
        }
    }

    return 0;
}
