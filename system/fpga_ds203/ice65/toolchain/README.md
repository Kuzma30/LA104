# iCE65 L04 patches for icestorm and nextpnr

Experimental support of the iCE65L04 (VQ100) as device `l04`, derived from
the vendor-built FPGA image V2.61 of the DS203 and the board schematic
(no iCEcube). See `../../readme.md`.

```sh
git clone https://github.com/YosysHQ/icestorm && cd icestorm
git checkout 00da9c3 && git apply ../icestorm-ice65-l04.patch
make -j4 && make install PREFIX=$HOME/ice65      # chipdb-l04.txt, timings_l04.txt
cd .. && git clone --recurse-submodules https://github.com/YosysHQ/nextpnr && cd nextpnr
git checkout 5e2718b && git apply ../nextpnr-ice65-l04.patch
cmake -B build -DARCH=ice40 -DICE40_DEVICES=l04 -DICESTORM_INSTALL_PREFIX=$HOME/ice65 \
      -DBUILD_PYTHON=OFF -DBUILD_GUI=OFF
cmake --build build -j4
```

Yosys is used unchanged (`../flow/synth_l04.ys.in` replaces the RAM step:
the iCE65 block RAM is 256 x 16 only).

- icestorm: `iceunpack -6` reads a 656 x 176 image as `l04` (the geometry
  is the same as iCE40 LM4K, the image has no device ID); icebox device
  `l04`, package `l04-vq100`, `icebox_chipdb.py -6`, carry-in bits of the
  logic tile B1[1] / B1[0], RAM tiles as iCE40 1k, no column buffers, own
  IO cell of bank 3; timing estimate `timings_l04.txt` (iCE40 LP8K x 1.3).
- nextpnr: `--l04 --package vq100`; only the confirmed global buffers
  (fabric input of networks 3, 4, 6, pad of network 1 = pin 15), all eight
  with `ICE65_UNVERIFIED=1` when the chip database is built.
