# Reset and transient-buffer stress

This test runs an offscreen bgfx scene while continuously resetting the
device. It checks both API robustness and actual framebuffer recovery:

- 200 `bgfx::reset` calls, injected before drawing, after the first submit,
  after a duplicate submit, and after invalid-handle submits.
- 200 transient VB/IB exhaustion cycles. Allocation is attempted until both
  transient handle tables are empty, then a frame is advanced and availability
  must return to normal.
- Duplicate submission of the same draw state.
- `BGFX_INVALID_HANDLE` submits through the public APIs that explicitly allow
  invalid handles.
- Pixel readback against two deterministic baseline images. Alternating clear
  colors make stale readback content visible.
- Allocation counting through a custom `bx::AllocatorI`; shutdown must leave
  zero live blocks.

Failures print the operations recorded since the last verified frame. The
Python runner also preserves the trace if the process crashes or asserts.

## Run

From the bgfx root:

```sh
python3 tests/reset-transient-stress/run.py --renderer vulkan
python3 tests/reset-transient-stress/run.py --renderer gl
```

The default is 200 iterations with transient exhaustion on every iteration.
The generated debug library is used so bgfx handle checks and fatal callbacks
are active.

## Last observed result

On this machine (`2026-11-18`, Mesa software Vulkan/OpenGL under Xvfb):

```text
Vulkan: PASS iterations=200 resets=200 exhaustions=200 duplicate_submits=400 invalid_submits=400
        alloc_blocks live=0 max=8656 allocs=1691435 frees=1691435
        frame=835

OpenGL: PASS iterations=200 resets=200 exhaustions=200 duplicate_submits=400 invalid_submits=400
        alloc_blocks live=0 max=8562 allocs=1688404 frees=1688404
        frame=835
```

Wall time was about 7 to 9 seconds per backend; peak RSS was about 140 MB.
The `librenderdoc.so` load warning emitted by the debug build is harmless.

## Negative controls

These are intentionally failing runs used to verify failure reporting. They
are not pass cases.

Reset without rebinding the framebuffer:

```sh
python3 tests/reset-transient-stress/run.py \
  --no-build --iterations 1 --exhaust-every 0 --renderer vulkan \
  --fault reset-no-rebind
```

Minimal sequence:

```text
reset
clear variant 0 without setViewFrameBuffer
frame + readback
```

The readback hash differs because the view still points at the default
backbuffer after reset.

Double destroy:

```sh
python3 tests/reset-transient-stress/run.py \
  --no-build --iterations 1 --exhaust-every 0 --renderer vulkan \
  --fault double-destroy
```

This triggers bgfx's "Vertex buffer handle is already destroyed" debug check.
The fatal callback prints the two destroy operations before aborting, and the
runner reports the same minimal replay from the last checkpoint.

Allocator leak:

```sh
python3 tests/reset-transient-stress/run.py \
  --no-build --iterations 1 --exhaust-every 0 --renderer vulkan \
  --fault leak
```

The allocator reports `live=1` after shutdown and prints:

```text
fault leak: allocate 64 bytes without free
```
