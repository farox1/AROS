Nouveau driver and NVIDIA cards on AROS - the "Optimus" case
=============================================================

This driver (nouveau.hidd) provides experimental support for NVIDIA
graphics cards in AROS.

On most systems (desktop cards, non-switchable laptops) the NVIDIA
card has its own display output and can be used normally.

However, on many laptops with switchable graphics (NVIDIA "Optimus"
technology) the discrete NVIDIA GPU has NO display of its own:

  - The built-in LCD is wired to the primary (integrated) GPU, which
    is usually Intel, but could be AMD or NVIDIA depending on the
    laptop.
  - The HDMI/VGA ports are usually wired to that primary GPU as well.

In this configuration AROS detects the NVIDIA card, but cannot use it
as a display adapter, because there is simply no monitor connected to
it. The driver therefore DISABLES this secondary NVIDIA card by default
and lets the primary card (whatever it is: Intel, AMD or NVIDIA) drive
the display, so the system boots normally. You will see a message like:

  [Nouveau] Found Nvidia card 0x10de/0x134d (gm108), disabled as
            secondary card. To enable it, set NouveauEnable=YES

This is expected behaviour, NOT an error. A primary NVIDIA card (the
only display controller on the system) is probed automatically and
works normally. On a muxless Optimus laptop enabling the secondary card
will not help, because the card still has no display of its own - the
driver will report "No connected connector".


Can I still use the NVIDIA card?
--------------------------------

Only if it can actually drive a display:

  - Desktop NVIDIA card                -> yes, works normally
  - Laptop with muxed/switchable port  -> yes, if the NVIDIA GPU is
                                          wired to an external output
  - Optimus (muxless) laptop           -> NO, no output on this GPU

On a muxless Optimus laptop the only reason to enable the driver is for
experimentation / development of the driver itself.


How to test / enable the driver (experimental)
----------------------------------------------

The driver behaves differently depending on whether the NVIDIA card is
the primary or a secondary card:

  - PRIMARY card (the only display controller on the system, e.g. a
    desktop card): nouveau probes it automatically, nothing to do.

  - SECONDARY card (another display controller is present, e.g. an
    Optimus laptop with an integrated GPU): the card is disabled by
    default, and the driver is opt-in. To make it probe the card, set
    the environment variable before the monitors are loaded. There are
    two equivalent ways:

    - Add this line to S:Startup-Sequence, AFTER the
      "Copy ENVARC: ENV:" line:

          SetEnv NouveauEnable YES

    - or create a file ENVARC:NouveauEnable containing the text YES
      (ENVARC is copied to ENV: automatically at boot).

To DISABLE a secondary card again, BOTH must be removed:

  - comment out / delete the "SetEnv NouveauEnable YES" line, AND
  - delete the ENVARC:NouveauEnable file if you created it.

If either is still present, the driver will keep probing the card.
Note that on a muxless Optimus laptop enabling the secondary card will
still not display anything, because the card has no output of its own.


VBIOS (firmware) location
-------------------------

Many laptop dGPUs cannot provide their VBIOS to the driver (the card's
EEPROM is locked and there is no VBIOS in the PCI ROM), so the driver
can load it from a file. Place the VBIOS dump as:

  DEVS:Firmware/NVidia/<chipfamily>/<name>.rom

For example, for a GeForce 940MX (chip family "gm108"):

  DEVS:Firmware/NVidia/gm108/vbios.rom

The chip family name is printed in the log (e.g. "NVIDIA GM108").
The VBIOS dump must be a complete raw ROM image (usually 64 KB for
modern cards, starting with the bytes 55 AA). Files in this directory
are only used when the card cannot supply its own VBIOS.

Cards that have their VBIOS in an on-board chip work without any file.


Status
------

Experimental, work in progress. Even when the driver initialises the
NVIDIA card fully (VRAM detected, etc.), it still needs a connected
display to be useful. On a muxless Optimus laptop it cannot display
anything by itself.
