# ZMK Input Processor: Keybind

> ⚠️ **Development Notice**  
> This module is currently under active development and **not yet in a working state**.  
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
