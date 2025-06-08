# ZMK Split Input Packet Compression Processor

This module is used to quantize X and Y value set to fit inside single payload of pointing device on split peripheral, to reduce the bluetooth connection loading between the peripherals and the central.

## What it does

It packs both X and Y axis value set (two `int16_t`) from pointing device driver into one `uint32_t` (as Z axis value code) on split peripheral. Only 8 byte will be transmitted, and then resurrected back into two new X/Y input events on central side. Roughly, shall save up to 50% packet size for one 2D coordination. The compression and decompression are handled in two predefined input prcessor config, `&zip_keybind` & `&zip_zxy`. They should be placed at the ends of chain of processors between central and peripheral.

## Installation

Include this module on your ZMK's west manifest in `config/west.yml`:

```yaml
manifest:
  remotes:
    #...
    # START #####
    - name: badjeff
      url-base: https://github.com/badjeff
    # END #######
    #...
  projects:
    #...
    # START #####
    - name: zmk-input-processor-keybind
      remote: badjeff
      revision: main
    # END #######
    #...
```

Roughly, `overlay` of the split-peripheral trackball should look like below.

```
/* Typical common split inputs node on central and peripheral(s) */
/{
  split_inputs {
    #address-cells = <1>;
    #size-cells = <0>;

    trackball_split: trackball@0 {
      compatible = "zmk,input-split";
      reg = <0>;
    };

  };
};

/* Add the compressor (zip_keybind) on peripheral(s) overlay */
#include <input/processors/keybind.dtsi>
&trackball_split {
  device = <&trackball>;
  /* append XY-to-Z compressor on transmitting side */
  input-processors = <&zip_keybind>;
};

/* Add the decompressor (zip_zxy) on central overlay */
#include <input/processors/keybind.dtsi>
tball1_mmv_il {
  compatible = "zmk,input-listener";
  device = <&trackball_split>;

  /* preppend Z-to-XY processor on receiving side */
  input-processors = <&zip_zxy>, <&zip_report_rate_limit 8>;
  /* NOTE: &zip_report_rate_limit is avaliable at https://github.com/badjeff/zmk-input-processor-report-rate-limit */
};
```
