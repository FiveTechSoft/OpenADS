#pragma once

#include "drivers/dbf_common.h"
#include "platform/file.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers {

enum class IndexOpenMode { ReadOnly, Shared, Exclusive };

enum class SeekHit { Exact, AfterKey, BeforeBegin, AfterEnd };

// How key bytes are encoded on disk. Text = raw/space-padded character
// bytes (default; ADI and character CDX/NTX). FoxNumeric = the 8-byte
// order-preserving encoding FoxPro/Harbour use for numeric and date CDX
// keys. NtxNumeric = the native DBFNTX numeric form: a zero-padded
// fixed-width magnitude, with every byte of a negative key complemented
// as (0x5c - byte) so negatives sort before positives. The engine builds
// the bytes; the B+tree itself only ever compares keys as opaque bytes,
// so this lives at the ABI/engine boundary.
enum class KeyEncoding { Text, FoxNumeric, NtxNumeric };

struct SeekOutcome {
    SeekHit       hit          = SeekHit::AfterEnd;
    std::uint32_t recno        = 0;
    bool          positioned   = false;
};

class IIndex {
public:
    virtual ~IIndex() = default;

    virtual util::Result<void>
        open(const std::string& path, IndexOpenMode mode) = 0;

    virtual std::string name()       const = 0;
    virtual std::string expression() const = 0;
    // On-disk index file path, for the management surface
    // (AdsMgGetOpenIndexes / sp_mgGet*Indexes). Empty when the driver
    // doesn't track one (e.g. purely in-memory orders).
    virtual std::string file_path()  const { return std::string{}; }
    // FOR-clause predicate (CDX conditional tag). Empty = unconditional.
    // Default empty; CdxIndex parses it from the on-disk sub-tag header so
    // (re)build paths can re-apply the same filter the tag was created with.
    virtual std::string condition()  const { return std::string{}; }

    // True when the tag provably has no on-disk keys (CDX: no root page).
    // Lets an opener detect a stale / never-built bag over a non-empty
    // table (every GotoTop on it lands in bof=eof=1 Limbo). Default false
    // means "unknown" — callers must not treat unknown as empty.
    virtual bool empty() { return false; }
    virtual bool        descending() const = 0;
    virtual bool        unique()     const = 0;
    virtual std::uint16_t key_length() const = 0;

    virtual util::Result<SeekOutcome> seek_first()   = 0;
    virtual util::Result<SeekOutcome> seek_last()    = 0;
    virtual util::Result<SeekOutcome>
        seek_key(const std::string& key, bool soft) = 0;
    virtual util::Result<SeekOutcome> next()         = 0;
    virtual util::Result<SeekOutcome> prev()         = 0;
    // Invalidate any cached cursor state so the next next() / prev()
    // doesn't try to resume from a boundary set by an earlier walk.
    // Default no-op; CdxIndex overrides to clear its CurState.
    virtual void invalidate_cursor() {}

    // Multiuser: re-read the on-disk bag header so peer appends / reindex
    // become visible. Default no-op; CdxIndex drops clean page-cache pages
    // when the sub-tag counter / root changes. Call before navigation.
    virtual void refresh_from_disk() {}

    virtual std::string current_key() const = 0;

    // On-disk key encoding for this index. Default Text; CdxIndex returns
    // FoxNumeric once the ABI marks a numeric/date key. The engine consults
    // this when building keys (see Table::compute_index_key_).
    virtual KeyEncoding key_encoding() const { return KeyEncoding::Text; }
    virtual void set_key_encoding(KeyEncoding) {}

    // Decimal places of a numeric key (NtxNumeric). Default 0; NtxIndex
    // overrides to return the count pinned from the field descriptor. The
    // engine needs it to format the same zero-padded key the index stored.
    virtual std::uint16_t key_decimals() const { return 0; }

    virtual util::Result<void> insert(std::uint32_t recno,
                                      const std::string& key) = 0;
    virtual util::Result<void> erase (std::uint32_t recno,
                                      const std::string& key) = 0;
    virtual util::Result<void> flush() = 0;

    // Reset the index to empty so a caller (REINDEX / PACK) can rebuild it.
    // Default: collect every entry then erase it (works for any IIndex).
    // CdxIndex / AdiIndex override with an O(1)-ish structural reset.
    virtual util::Result<void> clear_data() {
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto s = seek_first();
        while (s && s.value().positioned) {
            entries.emplace_back(s.value().recno, current_key());
            s = next();
        }
        for (auto& kv : entries) {
            if (auto e = erase(kv.first, kv.second); !e) return e.error();
        }
        return {};
    }

    // Bulk-load (key, recno) pairs into a freshly-cleared index. Default:
    // per-record insert. CdxIndex / AdiIndex override with a bottom-up bulk
    // build (~10x faster on a full REINDEX). Call clear_data() first.
    virtual util::Result<void>
    build_bulk(std::vector<std::pair<std::string, std::uint32_t>> keys) {
        for (auto& kv : keys) {
            if (auto e = insert(kv.second, kv.first); !e) return e.error();
        }
        return {};
    }
};

} // namespace openads::drivers
