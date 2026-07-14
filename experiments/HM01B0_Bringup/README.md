# HM01B0 stand-alone bring-up test for OpenFIRE

This is **not yet integrated into OpenFIRE**. It is a deliberately isolated first test for the Arducam HM01B0 module.

The test is intended to answer these questions before we touch the OpenFIRE tracking pipeline:

1. Does the Pico/Pico 2 reliably detect the HM01B0 over I2C?
2. Can PIO + DMA capture the HM01B0's 8-bit pixel bus?
3. Does the module actually run its 2x2 monochrome binning mode?
4. What frame rate does the real module/capture path achieve?
5. How strongly do 850 nm and 940 nm IR LEDs appear in RAW8 data?

## What the program does

- Generates HM01B0 MCLK from a Pico PWM output.
- Probes the HM01B0 at I2C address `0x24`.
- Verifies the expected model ID `0x01B0`.
- Configures RAW8, 8-bit parallel output.
- Enables monochrome 2x2 binning.
- Uses the HM01B0 datasheet's minimum QQVGA timing values as a high-speed test.
- Captures a raw 162 x 162 frame with PIO + DMA.
- Prints:
  - measured frame/capture rate;
  - average capture time in milliseconds;
  - min/mean/max brightness;
  - brightest pixel location;
  - number of pixels above an adjustable IR threshold;
  - a simple threshold-weighted centroid;
  - several sample pixel values.

The HM01B0 has a 324 x 324 array including border pixels. 2x2 binning therefore produces a raw 162 x 162 capture; the effective image area is approximately 160 x 160.

## Suggested folder in the fork

Copy this whole folder to:

`experiments/HM01B0_Bringup/`

on the `hm01b0-camera-dev` branch.

## Wiring used by the test

This follows the pin mapping used by Arducam's public RP2040 HM01B0 reference code.

| Camera signal | Pico GPIO |
|---|---:|
| RESET | GP2 |
| MCLK / XCLK | GP3 |
| SDA | GP4 |
| SCL | GP5 |
| D0 | GP6 |
| D1 | GP7 |
| D2 | GP8 |
| D3 | GP9 |
| D4 | GP10 |
| D5 | GP11 |
| D6 | GP12 |
| D7 | GP13 |
| PCLK | GP14 |
| LVLD / HREF | GP15 |
| FVLD / VSYNC | GP16 |

**Important:** verify the labels and power requirements on your exact Arducam module before applying power. The sensor's internal rail voltages are not necessarily the same thing as the breakout board's VCC input.

The PIO program depends on `D0..D7`, `PCLK`, and `LVLD` being consecutive from GP6 through GP15.

## Build with the Raspberry Pi Pico SDK

Set `PICO_SDK_PATH` to your Pico SDK checkout.

### RP2040 / Pico

```bash
mkdir build
cd build
cmake -DPICO_BOARD=pico ..
cmake --build . -j
```

### RP2350 / Pico 2

```bash
mkdir build-pico2
cd build-pico2
cmake -DPICO_BOARD=pico2 ..
cmake --build . -j
```

The output file will include:

`hm01b0_bringup.uf2`

Hold BOOTSEL while connecting the Pico, then copy the UF2 to the USB mass-storage device.

Open a USB serial terminal after reboot.

## Expected first output

A successful probe should include something like:

```text
Sensor ID: 0x01B0
Configured for 2x2 monochrome binning, 8-bit parallel RAW8.
Expected DMA payload: 162 x 162 = 26244 bytes/frame.
```

Then periodic reports such as:

```text
Measured loop/capture rate: ...
Brightness min/mean/max: ...
Brightest pixel: ...
Pixels >= 220: ...
Threshold-weighted centroid: ...
```

## IR test

Start with one LED, not four.

1. Point an 850 nm LED toward the camera.
2. Compare `max`, `Pixels >= 220`, and the centroid with the LED off and on.
3. Repeat with a 940 nm LED at the same distance and similar drive current.
4. Do the first test with no external IR-pass filter.
5. Only after confirming the raw module can see the LED should we test visible-light blocking material.

Change the compile-time threshold if needed:

```bash
cmake -DPICO_BOARD=pico -DCMAKE_C_FLAGS="-DIR_THRESHOLD=180" ..
```

A lower number is more sensitive but will include more background.

## Important experimental note about 120 FPS

The HM01B0 datasheet states that the monochrome 2x2 binning mode can reach 120 FPS. This program does **not** hard-code the answer "120 FPS"; it uses the datasheet's minimum QQVGA line/frame timing and then measures what the actual sensor, clocking, module, wiring, PIO, and DMA path achieve.

That is intentional. The first trustworthy number is the one measured on the real hardware.

## If capture times out

Check in this order:

1. HM01B0 ID reads as `0x01B0`.
2. MCLK is present on GP3.
3. FVLD/VSYNC reaches GP16.
4. PCLK reaches GP14.
5. LVLD/HREF reaches GP15.
6. D0..D7 are wired in order to GP6..GP13.
7. Grounds are common.
8. Wiring is short enough for the camera clock/data rate.

For a difficult first bring-up, a logic analyzer on MCLK, FVLD, LVLD, and PCLK will save a lot of guessing.

## Next milestone after this works

Do **not** integrate the whole camera into OpenFIRE immediately.

Next:

1. Find one bright IR blob robustly.
2. Find up to four connected bright blobs.
3. Calculate subpixel/weighted centroids.
4. Scale the four points into OpenFIRE's expected coordinate space.
5. Replace only the existing camera-coordinate source, leaving OpenFIRE's perspective and calibration code intact.
6. 
