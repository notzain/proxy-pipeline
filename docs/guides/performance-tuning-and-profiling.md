# Performance Tuning & Low-Latency Operational Guide

## 1. Overview & Problem Statement

Achieving consistent sub-microsecond latency and handling millions of messages per second requires coordinated tuning across the operating system kernel, network stack, memory allocator, and application threads.
By default, standard Linux and macOS kernel parameters prioritize batch desktop workloads or web servers, introducing:
- Scheduling latency jitter from thread migration across CPU cores.
- Socket buffer overruns and packet drops during burst traffic.
- Page faults and TLB misses when allocating physical memory.

This guide provides actionable configuration recipes and profiling methodologies to achieve peak performance.

---

## 2. Linux Kernel & Operating System Tuning

### Network Socket Buffer Sizing (`sysctl`)
For UDP socket ingress/egress (`backend-socket` and `UdpIngressHandler`), default OS socket buffers (typically 212 KB) overflow during microbursts:

```bash
# Add to /etc/sysctl.d/99-proxy-latency.conf:

# Increase maximum and default socket receive buffer (64 MB)
net.core.rmem_max = 67108864
net.core.rmem_default = 33554432

# Increase maximum and default socket send buffer (64 MB)
net.core.wmem_max = 67108864
net.core.wmem_default = 33554432

# Increase max backlog of unprocessed network packets
net.core.netdev_max_backlog = 100000

# Enable busy polling for low-latency socket reads (microseconds)
net.core.busy_read = 50
net.core.busy_poll = 50
```
Apply settings:
```bash
sudo sysctl --system
```

### `io_uring` & Memlock Resource Limits (`limits.conf`)
The `IoUringSocketPoller` uses registered buffers in kernel shared memory rings. Ensure locked memory limits are unrestricted:

```ini
# In /etc/security/limits.d/99-proxy.conf:
*    soft    memlock    unlimited
*    hard    memlock    unlimited
```

### CPU Power & Frequency Governor
Disable dynamic CPU frequency scaling (which introduces microsecond latency ramps when waking from idle):

```bash
# Set performance governor across all cores
sudo cpupower frequency-set --governor performance
```

---

## 3. CPU Core Pinning & Thread Affinity

In multi-threaded pipelines, thread migration across NUMA nodes or CPU sockets destroys L1/L2 data cache locality.

### Dedicated Thread Pinning
When using `DedicatedThreadStrategy` or running multi-process pipelines via `chain_runner.py`, bind processes or threads to isolated CPU cores using `taskset` or `numactl`:

```bash
# Pin ingress handler to Core 2:
taskset -c 2 ./build/release/bin/udp_ingress_handler_shm --config ingress.conf

# Pin validation handler to Core 3:
taskset -c 3 ./build/release/bin/order_validation_handler_shm --config validator.conf

# Pin egress handler to Core 4:
taskset -c 4 ./build/release/bin/udp_egress_handler_shm --config egress.conf
```

---

## 4. Shared Memory & Memory Sizing (`/dev/shm`)

### Slot Size & Ring Capacity
The default `ShmSlot` is configured to 2048 bytes (cache-line aligned). When transferring larger payloads (such as large XML documents or batch packets):
1. In `chains/*.toml`, configure `shm.capacity` to a power of two (e.g. 2048, 4096, 8192):
   ```toml
   [config]
   "shm.capacity" = 4096
   ```
2. Verify available RAM in `/dev/shm`:
   ```bash
   df -h /dev/shm
   ```

### Tuning Adaptive Signaling (`ShmSignaler`)
In `backend/shm/sync/ShmSignaler.hpp`:
- **Under continuous heavy load**: Keep adaptive spin iterations high to avoid issuing kernel futex calls.
- **Under intermittent burst load**: Allow the signaler to yield or park threads to avoid pegging CPU cores at 100% when queues are empty.

---

## 5. Profiling & Performance Diagnostics

### Recipe 1: Generating Flame Graphs with Linux `perf`
```bash
# 1. Record stack samples at 99 Hz with call graphs
sudo perf record -F 99 -g -- ./build/release/bin/order_validation_handler_shm --config validator.conf

# 2. Generate Flame Graph
perf script | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg
```

### Recipe 2: Inspecting Micro-Benchmarks
Run Catch2 statistical microbenchmarks with release optimizations:

```bash
./build/release/bin/proxy_tests "[benchmark]" --benchmark-samples 250
```
Key metrics to monitor:
- **Mean Execution Time**: Should remain $< 2.0$ µs for order validation and $< 0.85$ µs for SHM transfers.
- **Standard Deviation & Outliers**: High variance indicates lock contention, cache misses, or background OS context switching.

### Recipe 3: Cache Misses & Hardware Branch Mispredictions
```bash
perf stat -e cache-misses,cache-references,L1-dcache-load-misses,branch-misses \
  ./build/release/bin/order_validation_handler_shm --config validator.conf
```
*Goal: `cache-misses` should be $< 2.0\%$ of cache-references on hot message loops.*

