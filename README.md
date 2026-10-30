# rrd-convert

**go from any bag to Rerun**

Offline converter for robotics / autonomous-driving toolchains: turn recorded bags into a `.rrd` you can open in [Rerun](https://rerun.io) with one command. Formats are pluggable; v1 starts with standard ROS 2 `.db3`. Common sensor messages work out of the box; custom messages plug in without forking the core loop.

> **Status:** early — product definition locked; implementation in progress. APIs and install paths below may change.

## Quick start (target UX)

```bash
# install (details TBD once the first release lands)
rrd-convert convert run.db3 -o run.rrd
rerun run.rrd
```

Goal: a newcomer with a sample bag should see images / point clouds / TF in Rerun within about **10 minutes**.

## Why this exists

Stock data often lives in ROS bags. Official paths can mean multiple tools, a full ROS workspace, or learning MCAP first. `rrd-convert` keeps the **user-facing** path short: install → one command → open `.rrd`. Whether anything goes through MCAP internally is an implementation detail, not a prerequisite for the README.

## What v1 aims to cover

| Area | Scope |
|------|--------|
| Input | Standard ROS 2 SQLite `.db3` (common CDR) |
| Output | `.rrd` openable with `rerun` (at least `save` mode) |
| CLI | `convert`; optional `inspect` (list topics / types) |
| Built-in maps | Image, PointCloud2, TF / TFMessage, common Markers, … |
| Extensibility | Format Adapter + Message Decoder / Mapper registration |
| Docs | 5-minute getting started + minimal plugin example |
| Optional | Simple default blueprint to help “see something fast” |

## Non-goals (v1)

- Guaranteeing every vendor / in-house message (that’s what plugins are for)
- Deep product suites (radar SDKs, video transcoding, camera projection overlays, business dashboards)
- Full simulators, replay/eval engines, or on-vehicle live playback control
- Replacing the official MCAP toolchain, or a live ROS 2 bridge
- Requiring a specific ROS 2 distro just to run the core path

## Architecture (high level)

```text
Input (multi-format)
    → Format Adapter   # open store, list topics, read in order
    → Decoder          # bytes → intermediate representation
    → Mapper           # built-in + plugins → Rerun entities
    → Rerun Writer     # .rrd
```

The intermediate representation is the only cross-layer object, so decoders stay decoupled from the Rerun API. Users always see one command; optional later paths (e.g. raw export to MCAP) must not block v1.

### Extension model

1. **New input format** — implement a Format Adapter (open, list topics, sequential read) and register it.
2. **New message type** — add intermediate struct + Decoder + Mapper; register by type name / config, not by hard-coding topic-name branches in the main loop.
3. **Deep domain viz** — ship as a separate plugin or private package; keep it out of the public repo’s default dependencies.

## Roadmap (sketch)

| Stage | Focus |
|-------|--------|
| **v1** | ROS 2 `.db3` adapter, built-in sensor maps, plugin skeleton, sample bag, optional simple blueprint |
| **v1.1** | `inspect`, topic filter, time trim, batch; optional `connect` mode |
| **v2** | ROS 1 `.bag` / MCAP input adapters; optional MCAP side export |
| Later | More format adapters and type templates; optional hooks for eval / replay pipelines |

## Related tools

| Project | Relation | How we differ |
|---------|----------|----------------|
| Official db3 → MCAP → rrd | Authoritative two-step path | One command outward; no full ROS required; docs lead with the final view |
| [rosbag2_storage_rrd](https://github.com/rerun-io/rosbag2_storage_rrd) | Experimental recording plugin | Offline conversion of **existing** bags; not tied to a distro’s recording stack |
| [bag2rrd](https://github.com/rerun-io/bag2rrd) | Similar shape (ROS 1–oriented) | Multi-format adapter story; v1 prioritizes ROS 2 `.db3` |

## Contributing

Plugin examples and design notes will land with the first working convert path. Until then, issues and discussion around formats / message types are welcome.

## License

TBD (will be set before the first release).

---

**Repo:** [github.com/Rany010/rrd-convert](https://github.com/Rany010/rrd-convert)
