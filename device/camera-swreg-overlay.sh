#!/bin/sh
# Overlay the (user-supplied) libswregistrationalgo.so into /odm/lib64 so the
# Qualcomm CamX camera HAL can dlopen it and stop crashing on video-record stop.
# See README "Fixing the camera-provider crash". /odm is read-only (dm-verity),
# so we overlay instead of copying; the lib lives in a writable upperdir.
UPPER=/data/odmlib/upper
WORK=/data/odmlib/work
LIB="$UPPER/libswregistrationalgo.so"

[ -f "$LIB" ] || { echo "camera-swreg: $LIB not provisioned, nothing to do"; exit 0; }

# Wait for the odm partition to be mounted (Android/hybris mounts it early,
# but ordering vs. this unit is not guaranteed).
i=0
while [ ! -d /odm/lib64 ] && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done
[ -d /odm/lib64 ] || { echo "camera-swreg: /odm/lib64 never appeared"; exit 0; }

if mountpoint -q /odm/lib64; then
    echo "camera-swreg: /odm/lib64 already overlaid"
    exit 0
fi

mkdir -p "$WORK"
mount -t overlay overlay \
    -o lowerdir=/odm/lib64,upperdir="$UPPER",workdir="$WORK" /odm/lib64 || {
        echo "camera-swreg: overlay mount failed"; exit 1; }
echo "camera-swreg: overlay mounted"

# Restart the camera provider so it re-scans and can dlopen the lib (covers the
# case where droid-hal-init already started it before this unit ran).
pkill -9 -f 'camera.provider@2.4-service' 2>/dev/null || true
