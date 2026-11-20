# wii-slide-controller

Use Wii Remotes as presentation clickers on Linux.

- **A** sends the Right arrow key (next slide)
- **B** sends the Left arrow key (previous slide)

Up to 15 remotes can be connected at the same time, all driving the same
virtual keyboard. Each remote shows its number (1 to 15) in binary on its
LEDs, so you can tell them apart.

It works with any program that moves slides with the arrow keys (LibreOffice
Impress, PDF viewers, reveal.js, Google Slides in a browser, ...), since the
keys come from a regular keyboard as far as the system is concerned.

## Requirements

- Linux 4.5 or newer, with the `uinput` module
- A Bluetooth adapter and BlueZ
- `libbluetooth` headers to build (`libbluetooth-dev` on Debian/Ubuntu,
  `bluez-libs` on Arch, `bluez-libs-devel` on Fedora)
- A C99 compiler and `make`

## Build

```sh
git clone --recurse-submodules https://github.com/hugoocoto/wii-slide-controller
cd wii-slide-controller
make
sudo make install   # optional, installs to /usr/local/bin
```

If you cloned without `--recurse-submodules`, run
`git submodule update --init` first.

## Usage

1. Press **1 + 2** (or the red sync button) on every remote you want to use,
   so their LEDs blink.
2. Run:

   ```sh
   sudo wii-slide-controller
   ```

3. The remotes are found during a scan of about 10 seconds. Each one that
   connects is printed with its number, and its LEDs show that number.
4. Press A or B on any remote to move the slides.

Remotes only connect at startup. If one disconnects (battery, out of range),
the others keep working; restart the program to add it back. The program
exits when no remote is left.

### Running without root

Root is needed to create the virtual keyboard (`/dev/uinput`) and, in some
setups, to read device names during the Bluetooth scan. To run it as your
user instead:

```sh
# access to /dev/uinput for the input group
echo 'KERNEL=="uinput", GROUP="input", MODE="0660"' | sudo tee /etc/udev/rules.d/99-uinput.rules
sudo udevadm control --reload && sudo udevadm trigger
sudo usermod -aG input "$USER"   # log out and back in

# raw Bluetooth access for the scan
sudo setcap cap_net_raw+ep "$(command -v wii-slide-controller)"
```

## How it works

The program talks to the remotes directly over L2CAP, without pairing, and
types the keys through a virtual uinput keyboard. It is built on two
single-header libraries, included as git submodules:

- [wiimote.h](https://github.com/hugoocoto/wiimote.h): Wii Remote over Bluetooth
- [virt.h](https://github.com/hugoocoto/virt.h): virtual keyboard and mouse

## License

Public domain or MIT, choose whichever you prefer. See [LICENSE](LICENSE).
