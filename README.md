# rrd-convert

**go from any bag to Rerun**

Offline converter for robotics and autonomous-driving recordings. One command turns a stored bag into a file you can open in a visualizer. Input formats and visualizer backends are both pluggable. v1 reads a standard ROS 2 `.db3` and writes a Rerun `.rrd`.

> **Status:** definition locked, implementation not started. The handoff notes in [`doc/`](doc/README.md) are the source of truth for the next change.

## Quick start (target UX)

```bash
# install (details land with the first working build)
rrd-convert convert run.db3 -o run.rrd
rerun run.rrd
```

A newcomer with the sample bag should see images, point clouds, and TF in Rerun within about **10 minutes**. The core command does not require a ROS 2 distro.

## Pipeline

```text
stores (pluggable)
  → Format Adapter
  → Input Record          topic, type, time, serialization, bytes
  → Decoder               registered by message type
  → Visual Primitive      image, point cloud, transform, marker
  → Sink                  registered by engine
  → .rrd today; Foxglove, socket, or a custom format later
```

Decoders do not know which store the bytes came from. Sinks do not know the original serialization. v1 ships one adapter and one sink: SQLite `.db3` in, Rerun `save` out. One command selects one sink.

## What v1 covers

| Area | Scope |
|------|--------|
| Language | C++17, CMake, one `rrd-convert` binary |
| Input | Standard ROS 2 SQLite `.db3` (common CDR) |
| Output | `.rrd` openable with `rerun` in save mode |
| CLI | `convert` |
| Built-ins | `Image`, JPEG/PNG `CompressedImage`, `PointCloud2`, `TFMessage`, basic `Marker` / `MarkerArray` |
| Extension | Static registration of a Format Adapter, Decoder, or Sink |
| Sample | Synthetic `.db3` generated in-tree |

`inspect`, topic filters, time trim, and batch conversion are v1.1. A small default blueprint comes after the first convert path works.

## Non-goals (v1)

- Covering every vendor or in-house message
- Radar SDKs, video transcoding, camera projection overlays, business dashboards
- Simulators, replay/eval engines, or on-vehicle playback control
- Replacing the official MCAP toolchain, or a live ROS 2 bridge
- Requiring a ROS 2 distro to run the core path
- Writing more than one sink in a single command

## Roadmap

| Stage | Focus |
|-------|--------|
| **v1** | `.db3` adapter, built-in messages, Rerun save sink, static plugin skeleton, synthetic sample |
| **v1.1** | `inspect`, topic filter, time trim, batch; optional `connect` |
| **v2** | ROS 1 `.bag` and MCAP input adapters; another sink such as Foxglove |
| Later | More adapters and primitive templates; optional hooks for eval or replay pipelines |

## Related tools

| Project | Relation | How we differ |
|---------|----------|----------------|
| Official db3 → MCAP → rrd | Authoritative two-step path | One command; no full ROS install; docs lead with the view |
| [rosbag2_storage_rrd](https://github.com/rerun-io/rosbag2_storage_rrd) | Experimental recording plugin | Converts bags that already exist; not tied to a distro recording stack |
| [bag2rrd](https://github.com/rerun-io/bag2rrd) | Similar shape, ROS 1 leaning | Both the store and the visualizer backend are replaceable; v1 is ROS 2 `.db3` to Rerun |

## Contributing

Read [`doc/architecture.md`](doc/architecture.md) before adding a format, a message type, or a sink. Register the new piece. Do not add a topic-name branch to the convert loop.

## License

TBD. It will be set before the first release.

---

**Repo:** [github.com/Rany010/rrd-convert](https://github.com/Rany010/rrd-convert)
