#ifndef HM01B0_REGS_H
#define HM01B0_REGS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t reg;
    uint8_t value;
} hm01b0_reg_t;

#define HM01B0_REG_END 0xFFFFu

/*
 * Baseline tuning/register values based on the public Arducam RP2040 HM01B0
 * reference example, with the final interface configuration changed to
 * 8-bit parallel RAW8 for this experiment.
 *
 * The goal of this table is first bring-up, not final image-quality tuning.
 */
static const hm01b0_reg_t hm01b0_base_init[] = {
    {0x0100, 0x00},   // Standby

    {0x1003, 0x08},
    {0x1007, 0x08},
    {0x3044, 0x0A},
    {0x3045, 0x00},
    {0x3047, 0x0A},
    {0x3050, 0xC0},
    {0x3051, 0x42},
    {0x3052, 0x50},
    {0x3053, 0x00},
    {0x3054, 0x03},
    {0x3055, 0xF7},
    {0x3056, 0xF8},
    {0x3057, 0x29},
    {0x3058, 0x1F},
    {0x3059, 0x1E},
    {0x3064, 0x00},
    {0x3065, 0x04},

    {0x1000, 0x43},
    {0x1001, 0x40},
    {0x1002, 0x32},
    {0x0350, 0x7F},
    {0x1006, 0x01},
    {0x1008, 0x00},
    {0x1009, 0xA0},
    {0x100A, 0x60},
    {0x100B, 0x90},
    {0x100C, 0x40},
    {0x3022, 0x01},
    {0x1012, 0x01},

    {0x2000, 0x07},
    {0x2003, 0x00},
    {0x2004, 0x1C},
    {0x2007, 0x00},
    {0x2008, 0x58},
    {0x200B, 0x00},
    {0x200C, 0x7A},
    {0x200F, 0x00},
    {0x2010, 0xB8},
    {0x2013, 0x00},
    {0x2014, 0x58},
    {0x2017, 0x00},
    {0x2018, 0x9B},

    {0x2100, 0x01},
    {0x2101, 0x5F},
    {0x2102, 0x0A},
    {0x2103, 0x03},
    {0x2104, 0x05},
    {0x2105, 0x02},
    {0x2106, 0x14},
    {0x2107, 0x02},
    {0x2108, 0x03},
    {0x2109, 0x03},
    {0x210A, 0x00},
    {0x210B, 0x80},
    {0x210C, 0x40},
    {0x210D, 0x20},
    {0x210E, 0x03},
    {0x210F, 0x00},
    {0x2110, 0x85},
    {0x2111, 0x00},
    {0x2112, 0xA0},
    {0x2150, 0x03},

    // Start from full-array timing before applying 2x2 binning.
    {0x0340, 0x01},
    {0x0341, 0x7A},
    {0x0342, 0x01},
    {0x0343, 0x77},
    {0x3010, 0x00},   // QVGA window disabled
    {0x0383, 0x01},   // Full X readout
    {0x0387, 0x01},   // Full Y readout
    {0x0390, 0x00},   // Binning disabled
    {0x3011, 0x70},   // RAW8 (bit 0 remains 0)

    // 8-bit parallel interface, gated PCLK.
    // 0x3059[6:5] = 00 => 8-bit interface.
    {0x3059, 0x02},
    // 0x3060[5] = 1 => gated PCLK. Lower clock-divider bits stay at 0.
    {0x3060, 0x20},

    {HM01B0_REG_END, 0x00}
};

/*
 * Monochrome 2x2 binning mode.
 *
 * Datasheet:
 *   0x0383 = 3 -> horizontal BIN2 timing
 *   0x0387 = 3 -> vertical BIN2 timing
 *   0x0390[1:0] = 3 -> horizontal + vertical binning
 *
 * The HM01B0 array is 324x324 including border pixels, so the raw binned
 * capture is expected to be 162x162. The effective image area is about 160x160.
 *
 * The timing values below use the datasheet's minimum QQVGA timing:
 *   line_length_pck   = 0x00D7
 *   frame_length_lines = 0x0080
 *
 * This intentionally asks the sensor for its fastest binned timing so the test
 * can MEASURE what the real module and capture path achieve. It is not an
 * assumption that the result will be exactly 120.0 FPS.
 */
static const hm01b0_reg_t hm01b0_binned_2x2_mode[] = {
    {0x0100, 0x00},   // Standby
    {0x0104, 0x01},   // Hold grouped timing parameters

    {0x3010, 0x00},   // Full array source, then 2x2 bin
    {0x0383, 0x03},   // Horizontal BIN2 timing
    {0x0387, 0x03},   // Vertical BIN2 timing
    {0x0390, 0x03},   // H + V binning
    {0x1012, 0x03},   // Sync shift settings used by reference binning setup

    {0x0342, 0x00},   // LINE_LENGTH_PCK = 0x00D7
    {0x0343, 0xD7},
    {0x0340, 0x00},   // FRAME_LENGTH_LINES = 0x0080
    {0x0341, 0x80},

    {0x3011, 0x70},   // RAW8
    {0x3059, 0x02},   // 8-bit parallel
    {0x3060, 0x20},   // Gated PCLK

    {0x0104, 0x00},   // Consume grouped parameters
    {0x0100, 0x01},   // Streaming

    {HM01B0_REG_END, 0x00}
};

#endif
