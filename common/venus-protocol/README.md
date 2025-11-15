# Venus Protocol Headers

**Source**: venus-protocol project
**Location**: https://gitlab.freedesktop.org/virgl/venus-protocol
**Local path**: /home/ayman/venus-protocol/
**Generated**: 2025-11-15

These headers are auto-generated from the venus-protocol project using the vn_protocol.py script.

## Updating

To update these headers:
```bash
cd /home/ayman/venus-protocol
python3 vn_protocol.py --outdir /home/ayman/venus-plus/common/venus-protocol
```

## Purpose

The Venus protocol provides a complete serialization/deserialization mechanism for all Vulkan commands, enabling transparent forwarding of Vulkan API calls over a network or other transport mechanism.
