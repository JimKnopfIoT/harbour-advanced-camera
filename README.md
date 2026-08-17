# harbour-advanced-camera-ext

> # ‼️ READ THIS FIRST ‼️
> - This package contains **no proprietary library**.
> - On the Xperia 10 III, **video recording crashes the camera** until you
>   provide `libswregistrationalgo.so` yourself — see
>   [Fixing the camera-provider crash](#findings-fixing-the-camera-provider-crash-on-the-xperia-10-iii).
> - Photos, mic gain, compression and the histogram work **without** it.
> - That library is a proprietary blob: extract it from **your own** device's
>   firmware, never redistribute it.

Extended build of the [Advanced Camera](https://github.com/piggz/harbour-advanced-camera)
for Sailfish OS, developed on the Xperia 10 III. Installs next to the original
(own package `harbour-advanced-camera-ext`, icon "Advanced Camera Ext", own
settings under `/uk/co/piggz/harbour-advanced-camera-ext`). Adds:

- **Microphone gain** (50–600 %, in the "…" settings panel): raises the
  PulseAudio record-stream volume of the recording the moment it starts, live
  while it runs, and remembers it for the next ones. Fixes the far-too-quiet
  videos on ports whose Android audio HAL uses a tiny camcorder input gain
  (Xperia 10 III). Same mechanism as
  [harbour-micgain](https://github.com/JimKnopfIoT/harbour-micgain), built in.
- **Video bitrate / compression** slider down to 1 Mbit/s (upstream: 6.4) with a
  live "≈ MB/min" readout, so videos can be stored small right away instead of
  being re-encoded afterwards. The hardware H.264 encoder honours the target
  bitrate; combine with a lower resolution for the smallest files.
- **Live histogram** overlay (RGB + luma, clipping bars), top-right in the
  picture. Marked *experimental*. How the value is computed: a 160×90 grab of
  the rendered viewfinder every 250 ms; per pixel luma = `(R·77 + G·150 + B·29)
  / 256` (Rec.601); 64 bins for R/G/B and luma; pure-black `(0,0,0)` pixels are
  skipped (they are the viewfinder letterbox, not image data); clipping bars
  when >1 % of pixels sit in the top/bottom bin. It samples the **displayed
  preview** (auto-exposure and display gamma applied), so it is an exposure
  guide, not raw sensor data — for a true-exposure read use manual EV/ISO.
- **Pause / resume** while recording video. camerabin cannot pause an encoder,
  so each resume records a new (hidden) segment and **Stop joins them
  losslessly** into one file (no re-encoding). A single unpaused recording is
  just one file, no join. The finished file gets a fresh `VID_*.mp4` name that
  the media tracker indexes, so it shows up in the system gallery too.
- **Resolution list** without the duplicate entries the backend reports.

Build with the Sailfish Platform SDK:

```sh
mb2 -t SailfishOS-5.0.0.62-aarch64 build
```

---

# Findings: fixing the camera-provider crash on the Xperia 10 III

- Symptom (Xperia 10 III, Sailfish OS): stopping a video recording crashes the
  Android camera HAL; viewfinder blacks out, unhandled apps die.
- Documented: [sonyxperiadev/bug_tracker#761](https://github.com/sonyxperiadev/bug_tracker/issues/761)
  (2022), closed *stalled*, no fix; a missing library was suspected but never
  supplied.
- Fix: supply the missing library where the HAL looks for it — the crash is gone.
- **The fix is system-wide, not app-specific.** The bug lives in the shared
  Android camera HAL, so once the library is in place **every** camera app
  records video without crashing — the stock Jolla Camera, the original
  Advanced Camera, this fork, anything. You do not need this app to benefit;
  installing the library alone heals video recording device-wide. Recipe below.

## What actually crashes, and why

Recording a video and stopping it makes the Android service
`android.hardware.camera.provider@2.4-service_64` segfault (SIGSEGV, NULL
pointer). The Android tombstone (`/data/tombstones/tombstone_*`) shows a
NULL-mutex dereference deep in Qualcomm's **closed** CamX/CHI camera blobs,
during pipeline teardown on a `configure_streams` call (which happens on every
video start/stop):

```
pthread_mutex_lock(NULL)                       libc
CamX::Mutex::Lock()                            /odm/lib64/hw/camera.qcom.so
CamX::Node::Destroy()
CamX::Pipeline::DestroyNodes / ~Pipeline / Destroy
CamX::ChiContext::DestroyPipelineDescriptor
ExtensionModule::DestroyPipelineDescriptor     /odm/lib64/hw/com.qti.chi.override.bitra.so
ChiFeature2Base::~ChiFeature2Base
ChiFeature2HWmultiframe::~ChiFeature2HWmultiframe   /odm/lib64/com.qti.feature2.mfsr.bitra.so
... AdvancedCameraUsecase::Destroy ...
ExtensionModule::TeardownOverrideSession
CamX::HALDevice::ConfigureStreams               <- trigger
```

The root cause: the CamX component
`/odm/lib64/camera/components/com.qti.node.swregistration.so` **dlopens
`libswregistrationalgo.so` at runtime** (the string `/odm/lib64` sits right next
to `libswregistrationalgo` inside that blob — it looks **only in `/odm/lib64`**,
never `/vendor/lib64`). But that library is **missing** from the Sailfish `/odm`
partition — and from Sony's open-source (SODP) `odm` image of *any* version.
When the dlopen fails, the multi-frame node is left half-initialised (a NULL
mutex), and destroying it on the next `configure_streams` segfaults.

The library exists only in the closed stock `vendor` partition, which the SODP
binaries do not ship. Pull the full stock firmware and extract it.

## The recipe (everything on a Linux PC + the device over ssh)

`libswregistrationalgo.so` (aarch64) is a proprietary Qualcomm/Sony blob.
Extract it from **your own** device's firmware; do not redistribute it.

### 1. Download the stock firmware with XperiFirm (Windows)

[XperiFirm v5.8.1](https://xdaforums.com/t/tool-xperifirm-xperia-firmware-downloader-v5-8-1.2834142/)
(needs .NET; runs on Windows). Pick:

- Model **`XQ-BT52`** (Xperia 10 III, codename pdx213)
- A generic region, e.g. **Customized EEA** (no carrier bloat)
- The latest version, e.g. **`62.2.A.0.533`** (Android 13)

It downloads a folder `XQ-BT52_Customized_EEA_62.2.A.0.533/` with `.sin`
partition files. Copy that folder to the Linux PC.

This firmware uses **dynamic partitions**, so the camera blobs are *not* in a
`vendor.sin`/`odm.sin`; they live inside **`super_X-FLASH-ALL-*.sin`** (~3.2 GB).

### 2. Tools on the Linux PC

- `lpunpack`, `lpdump`, `simg2img` — from **android-tools** (your distro package)
- `debugfs` — from **e2fsprogs** (usually already installed)
- **sony_dump** — a `.sin` extractor, built from source:

```sh
git clone --depth 1 https://github.com/munjeni/anyxperia_dumper.git
cd anyxperia_dumper
# it bundles zlib; fetch the exact version it expects
wget -q https://zlib.net/fossils/zlib-1.2.11.tar.gz -O zlib.tgz && tar xzf zlib.tgz
gcc -w -O2 -Iinclude -Izlib-1.2.11 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE=1 \
    zlib-1.2.11/*.c lz4.c unpackbootimg.c untar.c sony_dump.c -o sony_dump
```

### 3. Extract the library

```sh
FW=XQ-BT52_Customized_EEA_62.2.A.0.533

# 3a. .sin -> raw super image (~5 GB)
./sony_dump super_out "$FW"/super_X-FLASH-ALL-*.sin
#   -> super_out/super_X-FLASH-ALL-*.bin

# 3b. list the logical partitions, then unpack vendor (and odm, for reference)
lpdump super_out/super_X-FLASH-ALL-*.bin | grep -A1 'Name:'
lpunpack --partition=vendor_a --partition=odm_a \
         super_out/super_X-FLASH-ALL-*.bin .
#   -> vendor_a.img (~800 MB), odm_a.img

# 3c. pull the lib out of the vendor image WITHOUT mounting (no root needed)
debugfs -R "dump /lib64/libswregistrationalgo.so ./libswregistrationalgo.so" vendor_a.img

# 3d. sanity check: aarch64 ELF, ~5.3 MB
file libswregistrationalgo.so
#   ELF 64-bit LSB shared object, ARM aarch64 ...
```

(On stock Android 13 the camera algo libs are in **vendor**, not odm — that's why
the Sailfish port, based on odm, lacks this one.)

### 4. Install it on the device

CamX hard-codes the path `/odm/lib64/libswregistrationalgo.so` (it looks only in
`/odm/lib64`, never `/vendor/lib64`), and `/odm` is mounted read-only. There are
**two ways** to get the lib there — both verified on the Xperia 10 III. The ssh
user is `root` here; adjust the IP.

```sh
DEV=root@PHONE_IP        # ssh target of your device
```

#### Variant A — overlay + systemd service (recommended)

Non-invasive: the real `/odm` partition is never touched. A writable copy lives
on `/data`, overlaid onto `/odm/lib64`, and a boot service re-applies it. This
survives reboots **and** system updates that reflash `/odm` (the copy on `/data`
persists and gets re-overlaid), and carries zero risk of an unbootable device.

```sh
# copy the lib into a writable, persistent location
ssh $DEV 'mkdir -p /data/odmlib/upper /data/odmlib/work'
scp libswregistrationalgo.so $DEV:/data/odmlib/upper/
ssh $DEV 'chmod 0644 /data/odmlib/upper/libswregistrationalgo.so'

# overlay it into /odm/lib64 now, and restart the camera provider
ssh $DEV 'mount -t overlay overlay \
    -o lowerdir=/odm/lib64,upperdir=/data/odmlib/upper,workdir=/data/odmlib/work \
    /odm/lib64
  pkill -9 -f camera.provider@2.4-service'

# make it persist across reboots (unit + script are in this repo's device/ folder)
scp device/camera-swreg-overlay.sh      $DEV:/usr/local/bin/
scp device/camera-swreg-overlay.service $DEV:/etc/systemd/system/
ssh $DEV 'chmod +x /usr/local/bin/camera-swreg-overlay.sh
          systemctl daemon-reload
          systemctl enable camera-swreg-overlay.service'
```

The service is a `oneshot` that runs before `basic.target`; it only acts if
`/data/odmlib/upper/libswregistrationalgo.so` exists (a no-op on devices where
you haven't provisioned the lib). Revert: `systemctl disable
camera-swreg-overlay.service`, `umount /odm/lib64`, `rm -rf /data/odmlib`.

#### Variant B — write it straight into the /odm partition

More "permanent" (bytes on the partition, no overlay, no service). On this
device `/odm` is a plain partition (not behind a runtime dm-verity target) and
the bootloader is `unlocked`, so AVB does **not** reject a modified `/odm` — the
device boots fine with the extra file. Downsides: a system update that reflashes
`/odm` wipes it, and there is a small in-principle bootloop risk — so **back up
the partition first** and keep a USB cable handy for `fastboot`.

```sh
# B0. FIRST: pull a byte-exact, flashable backup of the current /odm (~800 MB).
#     Everything here is over ssh/Wi-Fi; USB is only needed for fastboot recovery.
ssh $DEV 'sha256sum /dev/sda76'                    # note the hash
ssh $DEV 'dd if=/dev/sda76 bs=4M 2>/dev/null' > odm_b-backup.img
sha256sum odm_b-backup.img                         # must match the hash above
#   (find the odm block device with: ssh $DEV 'mount | grep " /odm "')

# B1. remount /odm read-write, drop the file in, restore read-only
ssh $DEV 'mount -o remount,rw /odm
  cp /data/odmlib/upper/libswregistrationalgo.so /odm/lib64/    # or scp it straight in
  chmod 0644 /odm/lib64/libswregistrationalgo.so
  chcon u:object_r:vendor_file:s0 /odm/lib64/libswregistrationalgo.so 2>/dev/null || true
  sync
  mount -o remount,ro /odm'

# B2. restart the camera provider (or just reboot)
ssh $DEV 'pkill -9 -f camera.provider@2.4-service'
```

Revert: `mount -o remount,rw /odm; rm /odm/lib64/libswregistrationalgo.so; mount
-o remount,ro /odm`. If the device ever won't boot, reflash the backup over USB:
`fastboot flash odm_b odm_b-backup.img` (use the slot your device is on —
`cat /proc/cmdline | tr ' ' '\n' | grep slot_suffix`).

#### Verify (either variant)

Record a video and stop it — **no crash, no black screen.** System-wide proof:
the stock **Jolla Camera** no longer blacks out on video stop either. SELinux is
permissive on this port, so the file's context is not critical (Variant B still
sets the correct `vendor_file` label).

### Notes / scope

- Verified only on Xperia 10 III (pdx213), SFOS 5.1.0.11 — both install
  variants, reboot persistence, lib from `62.2.A.0.533`. Not tested elsewhere.
- The crash is a genuine bug in Qualcomm's closed CamX blobs; we don't patch
  them, we just supply the library they expect. Config tweaks in
  `/vendor/etc/camera/camxoverridesettings.txt` (e.g. `advanceFeatureMask`,
  `overrideEnableMFNR`) do **not** avoid it without breaking video-start.
- Again: **don't redistribute `libswregistrationalgo.so`.** It is proprietary;
  each user extracts it from their own device's firmware as above.

---

# harbour-advanced-camera

This is a camera application for Sailfish which exposes all available camera paramters to the user.

The application is licensed under the GPLv2+, with some source files specifically licensed under the LGPLv2(.1)+ so they can be re-used.
