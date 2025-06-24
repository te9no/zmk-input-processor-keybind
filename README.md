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
        track_remainders;
        bindings = <&kp RIGHT>,
                  <&kp LEFT>,
                  <&kp UP>,
                  <&kp DOWN>;
        tick = <40>;
        wait-ms = <0>;
        tap-ms = <10>;
        // mode = <1>
        // threshold = <10>
        // max_threshold = <200>
        // max_pending_activations = <10>
    };
};

```

## Configuration

### Required Parameters
- **`bindings`** *(phandle-array)*  
  The key behaviors to trigger when input is detected. This is mandatory.

### Optional Parameters

- **`mode`** *(int, default: 0)*  
  How input translates to key events:
  - `0` = Raw direct movement
  - `1` = 4-directional (up/down/left/right) 
  - `2` = 8-directional (includes diagonals) 
    *Example: Diagonal movement (up+right) will press both up and right keys simultaneously*
- **`track_remainders`** *(boolean, default: false)*  
  When enabled, saves partial movement between activations
- **`threshold`** *(int, default: 1)*  
  Minimum movement required to trigger (must be positive)
- **`max_threshold`** *(int, default: 200)*  
  Upper limit for threshold (caps sensitivity)
- **`tick`** *(int, default: 10)*  
  Movement units needed per activation (higher = less sensitive)
- **`wait-ms`** *(int, default: 0)*  
  Delay before next activation (milliseconds)
- **`tap-ms`** *(int, default: 20)*  
  Press-to-release timing (milliseconds)
- **`max_pending_activations`** *(int, default: 5)*  
  Maximum queued actions per axis
