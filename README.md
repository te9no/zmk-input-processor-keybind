# ZMK Input Processor: Keybind

> ⚠️ **Development Notice**  
> This module is currently under active development and **somewhat works**.  
> Use at your own risk. API and behavior may change significantly.

A ZMK firmware module that quantizes XY pointer device inputs (like trackballs or touchpads) into discrete keypress events.

## Installation

Include this module on your ZMK's west manifest in `config/west.yml`:

```yaml
manifest:
  remotes:
    #...
    # START #####
    - name: zettaface
      url-base: https://github.com/zettaface
    # END #######
    #...
  projects:
    #...
    # START #####
    - name: zmk-input-processor-keybind
      remote: zettaface
      revision: main
    # END #######
    #...
```

Roughly, `overlay` of the split-peripheral trackball should look like below.

```

#include <input/processors/zmk-input-processor-keybind.dtsi>

&trackball_listener {
    status = "okay";
    device = <&trackball>;

    input-processors = <&zip_keybind_keys>;
};

```

Custom processor example:

```

/ {
    zip_keybind_keys: zip_keybind_keys {
        compatible = "zmk,input-processor-keybind";
        #input-processor-cells = <0>;
        type = <INPUT_EV_REL>;
        track_remainders;
        bindings = <&kp RIGHT>,
                  <&kp LEFT>,
                  <&kp UP>,
                  <&kp DOWN>;
        tick = <40>;
        wait-ms = <0>;
        tap-ms = <10>;
        // mode = <0>
        // threshold = <10>
        // max_threshold = <200>
        // max_pending_activations = <10>
    };
};

```
