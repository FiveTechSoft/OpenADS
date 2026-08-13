# ADI Tag Directory Ordering: Prepend vs Append

## Overview

The Advantage Database Index (ADI) format stores a tag directory on page 2 that maps ordinal positions to individual tags. When a new tag is added, there are two conventions for how it's inserted into this directory:

- **Prepend**: New tag is inserted at position 0, shifting existing tags down
- **Append**: New tag is added at the end, preserving creation order

## Behavior Differences

| Aspect | SAP Advantage Data Architect | OpenADS (default) |
|--------|------------------------------|-------------------|
| Ordering | Prepend | Append |
| Ordinal 1 | Last tag created | First tag created |
| CDX alignment | No | Yes |
| Harbour rddads alignment | No | Yes |

## Impact

When a `.ADI` file created by OpenADS (append mode) is opened by SAP's Advantage Data Architect:

1. SAP's internal validation assumes prepend ordering
2. SAP reports "duplicate index name" error
3. The file is **not corrupt** — OpenADS reads it correctly
4. All tags are accessible and functional in OpenADS

## Configurable Ordering

OpenADS now supports configurable ordering via `CreateParams::prepend_tag_dir`:

```cpp
openads::drivers::adi::AdiIndex::CreateParams params;
params.field_num = 1;
params.field_name = "LASTNAME";
// ... other params ...

// For SAP compatibility (prepend):
params.prepend_tag_dir = true;
auto result = AdiIndex::add_tag("test.adi", params);

// For CDX/Harbour compatibility (append, default):
params.prepend_tag_dir = false;
auto result = AdiIndex::add_tag("test.adi", params);
```

## ADI Inspection Tool

Use the `adi_inspect` tool to examine ADI files and determine their ordering mode:

```bash
# Build the tool
cmake --build build --target adi_inspect --config Release

# Inspect an ADI file
tools/adi_inspect/Release/adi_inspect.exe test.adi
```

Sample output:
```
ADI Tag Directory Inspection
===========================
File:       test.adi
File size:  3584 bytes
Page level: 3 (expected 3 for tag dir)
Tag count:  3

Tags (ordinal = position in directory):
----------------------------------------
  Ordinal  1: field=LASTNAME       hdr_pg=3    root=5    
  Ordinal  2: field=FIRSTNAME      hdr_pg=6    root=8    
  Ordinal  3: field=CITY           hdr_pg=9    root=11   

Ordering Analysis:
------------------
  Mode: APPEND (ordinals follow creation order)
  Note: SAP Advantage Data Architect uses PREPEND (reversed ordinals)
        OpenADS uses APPEND (matches CDX/Harbour behavior)
```

## Recommendation

- **New applications**: Use append mode (default) for CDX/Harbour compatibility
- **SAP integration**: Use prepend mode when files must be validated by SAP Data Architect
- **Mixed environments**: Use the inspection tool to verify ordering before troubleshooting

## References

- Issue #166: ADI tag directory ordering divergence
- Commit e511310: Changed from prepend to append for CDX alignment
- `src/drivers/adi/adi_index.cpp`: ADI index implementation
- `tools/adi_inspect/`: ADI inspection tool
