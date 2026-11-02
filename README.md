# rrd-convert

**go from any bag to Rerun**

Offline converter for robotics and autonomous-driving recordings. One command turns a stored bag into a file you can open in a visualizer. Input formats and visualizer backends are both pluggable. v1 reads a ROS 2 bag (SQLite storage) and writes a Rerun `.rrd`.

> **Status:** definition locked, implementation not started. The handoff notes in [`doc/`](doc/README.md) are the source of truth for the next change.

## Quick start (target UX)

```bash
# Prebuilt package from GitHub Releases (Ubuntu 20.04 x86_64 first).
pip install rerun-sdk==0.28.2
rrd-convert convert path/to/bag_dir -o run.rrd --topics /camera,/lidar,/tf
rerun run.rrd
```

A newcomer with the sample bag should see images, point clouds, and coordinate frames in Rerun within about **10 minutes**. The core command does not require a ROS 2 distro.

## Why not just MCAP?

The Rerun Viewer already opens MCAP and maps common ROS 2 sensor messages. rrd-convert focuses on what that path does not cover:

- `visualization_msgs/Marker` and `MarkerArray`.
- Semantic mapping of in-house or vendor messages into points, lines, shapes, and text, driven by message definitions instead of hand-written parsers.
- Swappable visualizer backends behind a shared intermediate representation.
- CI-friendly conversion: exit codes, a JSON summary, and no half-written output.

## Pipeline

```text
stores (pluggable)
  → Format Adapter
  → Input Record          topic, type, log time, serialization, bytes
  → Decoder               registered by message type
  → Visual Primitive      image, video frame, points, lines, shapes, text, transform, clear, ...
  → Sink                  registered by engine
  → .rrd today; Foxglove, socket, or a custom format later
```

Decoders do not know which store the bytes came from. Sinks do not know the original serialization or any ROS type. v1 ships one adapter and one sink. One command selects one sink.

## What v1 covers

| Area | Scope |
|------|--------|
| Language | C++17, CMake, one `rrd-convert` binary |
| Input | A `.db3` file or a bag directory with split files; compressed bags are rejected |
| Output | `.rrd` for Rerun Viewer 0.28.x / 0.29.x (SDK pinned to 0.28.2) |
| CLI | `convert` with topic filters, root frame, `.msg` search paths, JSON summary |
| Built-ins | `Image`, `CompressedImage` (JPEG, PNG, H.264, H.265), `foxglove_msgs/CompressedVideo`, `PointCloud2`, `TFMessage`, `Marker` / `MarkerArray` |
| Custom messages | Generic CDR decoding from embedded message definitions or user `.msg` files |
| Extension | Static registration through an exported CMake package |
| Distribution | Prebuilt package on GitHub Releases, Ubuntu 20.04 x86_64 first |

`inspect`, time trim, and multi-bag batch commands are v1.1.

Video frames are written as-is without transcoding. Playing them in the Viewer requires FFmpeg 5.1 or newer, and streams must not contain B-frames.

## Non-goals (v1)

- Covering every vendor or in-house message
- Radar SDKs, video transcoding, camera projection overlays, business dashboards
- Reading compressed bags
- Simulators, replay/eval engines, or on-vehicle playback control
- Replacing the official MCAP toolchain, or a live ROS 2 bridge
- Requiring a ROS 2 distro to run the core path
- Writing more than one sink in a single command

## Roadmap

| Stage | Focus |
|-------|--------|
| **v1** | ROS 2 SQLite adapter with splits and topic filters, built-in messages, generic CDR, Rerun save sink, exported plugin package, synthetic sample, Ubuntu 20.04 release |
| **v1.1** | `inspect`, time trim, batch; optional `connect` |
| **v2** | ROS 1 `.bag` and MCAP input adapters; another sink such as Foxglove; compressed bags |
| Later | More adapters and primitive templates; optional hooks for eval or replay pipelines |

## Related tools

| Project | Relation | How we differ |
|---------|----------|----------------|
| `mcap convert`, then open the MCAP in Rerun | Official path, no ROS install needed | Markers, semantic mapping of custom messages, swappable backends, CI summary |
| [rosbag2_storage_rrd](https://github.com/rerun-io/rosbag2_storage_rrd) | Experimental recording plugin | Converts bags that already exist; not tied to a distro recording stack |
| [bag2rrd](https://github.com/rerun-io/bag2rrd) | Similar shape, ROS 1 leaning | Both the store and the visualizer backend are replaceable |

## Contributing

Read [`doc/architecture.md`](doc/architecture.md) before adding a format, a message type, or a sink. Register the new piece. Do not add a topic-name branch to the convert loop.

## License

TBD. It will be set before the first release.

---

**Repo:** [github.com/Rany010/rrd-convert](https://github.com/Rany010/rrd-convert)
