// adi_inspect — OpenADS ADI index inspection utility.
//
// Reads an .adi file and displays the tag directory structure, including
// tag ordinals, field names, page numbers, and ordering mode (prepend/append).
//
// Usage:  adi_inspect <file.adi>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t PAGE_SIZE = 512;
constexpr std::uint32_t TAGDIR_ENTRY_START = 24;
constexpr std::uint32_t TAGDIR_ENTRY_SIZE = 6;
constexpr std::uint16_t LVL_TAGDIR = 3;

using Page = std::array<std::uint8_t, PAGE_SIZE>;

std::uint16_t u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct TagInfo {
    std::uint32_t header_page;
    std::uint32_t field_marker_page;
    std::uint32_t root_page;
    std::uint8_t  first_char;
    std::string   field_name;
    bool          unique;
    // v2 metadata
    bool          has_v2;
    std::string   tag_name;
    std::string   tag_expr;
    std::string   for_expr;
    std::uint16_t key_length;
};

std::string read_field_name_from_page(FILE* f, std::uint32_t page_no) {
    Page pg;
    std::fseek(f, static_cast<long>(page_no) * PAGE_SIZE, SEEK_SET);
    if (std::fread(pg.data(), 1, PAGE_SIZE, f) != PAGE_SIZE) return "";
    
    // Field marker page: field numbers encoded as 1-byte each, terminated by 0
    std::string name;
    for (std::size_t i = 12; i < PAGE_SIZE && pg[i] != 0; ++i) {
        if (pg[i] >= 32 && pg[i] < 127) {
            name += static_cast<char>(pg[i]);
        }
    }
    return name;
}

std::string read_v2_string(const std::uint8_t* pg, std::size_t offset, std::size_t max_len) {
    std::string s;
    for (std::size_t i = 0; i < max_len; ++i) {
        std::uint8_t ch = pg[offset + i];
        if (ch == 0) break;
        if (ch >= 32 && ch < 127) s += static_cast<char>(ch);
    }
    return s;
}

void inspect_adi(const fs::path& path) {
    FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "Error: cannot open '%s'\n", path.string().c_str());
        return;
    }

    // Read page 2 (tag directory)
    Page pg2;
    std::fseek(f, 2 * PAGE_SIZE, SEEK_SET);
    if (std::fread(pg2.data(), 1, PAGE_SIZE, f) != PAGE_SIZE) {
        std::fprintf(stderr, "Error: cannot read tag directory page\n");
        std::fclose(f);
        return;
    }

    std::uint16_t level = u16_le(pg2.data());
    std::uint16_t count = u16_le(pg2.data() + 2);

    std::printf("ADI Tag Directory Inspection\n");
    std::printf("===========================\n");
    std::printf("File:       %s\n", path.string().c_str());
    std::printf("File size:  %lld bytes\n", static_cast<long long>(fs::file_size(path)));
    std::printf("Page level: %u (expected %u for tag dir)\n", level, LVL_TAGDIR);
    std::printf("Tag count:  %u\n\n", count);

    if (level != LVL_TAGDIR) {
        std::fprintf(stderr, "Warning: page level %u != expected %u\n", level, LVL_TAGDIR);
    }

    std::vector<TagInfo> tags;
    tags.reserve(count);

    for (std::uint16_t i = 0; i < count; ++i) {
        std::size_t off = TAGDIR_ENTRY_START + i * TAGDIR_ENTRY_SIZE;
        if (off + TAGDIR_ENTRY_SIZE > PAGE_SIZE) break;

        std::uint32_t xx = u32_le(pg2.data() + off);
        std::uint8_t first_char = pg2.data()[off + 5];
        
        std::uint32_t fmk_pg = xx + 1u;
        std::uint32_t root_pg = fmk_pg + 1u;

        TagInfo tag{};
        tag.header_page = xx;
        tag.field_marker_page = fmk_pg;
        tag.root_page = root_pg;
        tag.first_char = first_char;
        tag.unique = false;
        tag.has_v2 = false;
        tag.key_length = 0;

        // Read field name from field marker page
        tag.field_name = read_field_name_from_page(f, fmk_pg);

        // Read per-tag header page for unique flag and v2 metadata
        Page hdr_pg;
        std::fseek(f, static_cast<long>(xx) * PAGE_SIZE, SEEK_SET);
        if (std::fread(hdr_pg.data(), 1, PAGE_SIZE, f) == PAGE_SIZE) {
            tag.unique = (hdr_pg[14] & 0x01u) != 0;
            
            // Check for v2 metadata (signature at offset 20-23)
            if (hdr_pg[20] == 'A' && hdr_pg[21] == 'D' && hdr_pg[22] == 'I' && hdr_pg[23] == '2') {
                tag.has_v2 = true;
                tag.tag_name = read_v2_string(hdr_pg.data(), 24, 32);
                tag.tag_expr = read_v2_string(hdr_pg.data(), 56, 64);
                tag.for_expr = read_v2_string(hdr_pg.data(), 120, 64);
                tag.key_length = u16_le(hdr_pg.data() + 184);
            }
        }

        tags.push_back(tag);
    }

    // Display tags
    std::printf("Tags (ordinal = position in directory):\n");
    std::printf("----------------------------------------\n");
    for (std::size_t i = 0; i < tags.size(); ++i) {
        const auto& tag = tags[i];
        std::printf("  Ordinal %2zu: ", i + 1);
        
        if (!tag.tag_name.empty()) {
            std::printf("name=%-16s ", tag.tag_name.c_str());
        } else if (!tag.field_name.empty()) {
            std::printf("field=%-15s ", tag.field_name.c_str());
        } else {
            std::printf("char='%c'              ", tag.first_char);
        }
        
        std::printf("hdr_pg=%-5u root=%-5u ",
            tag.header_page, tag.root_page);
        
        if (tag.unique) std::printf("UNIQUE ");
        if (tag.has_v2) {
            std::printf("v2(klen=%u)", tag.key_length);
            if (!tag.tag_expr.empty()) {
                std::printf(" expr=\"%s\"", tag.tag_expr.c_str());
            }
            if (!tag.for_expr.empty()) {
                std::printf(" for=\"%s\"", tag.for_expr.c_str());
            }
        }
        std::printf("\n");
    }

    // Ordering analysis
    std::printf("\nOrdering Analysis:\n");
    std::printf("------------------\n");
    
    // Check if ordinals follow creation order (append) or reverse (prepend)
    // by comparing header page numbers - in append mode, they should be
    // monotonically increasing (pages allocated sequentially)
    bool monotonic = true;
    for (std::size_t i = 1; i < tags.size(); ++i) {
        if (tags[i].header_page < tags[i-1].header_page) {
            monotonic = false;
            break;
        }
    }
    
    if (tags.size() <= 1) {
        std::printf("  Single tag - cannot determine ordering mode\n");
    } else if (monotonic) {
        std::printf("  Mode: APPEND (ordinals follow creation order)\n");
        std::printf("  Note: SAP Advantage Data Architect uses PREPEND (reversed ordinals)\n");
        std::printf("        OpenADS uses APPEND (matches CDX/Harbour behavior)\n");
    } else {
        std::printf("  Mode: PREPEND (ordinals are reversed from creation order)\n");
        std::printf("  Note: This matches SAP Advantage Data Architect behavior\n");
    }

    // Footer field names (page 2, bytes 500-511)
    std::printf("\nFooter (bytes 500-511 of page 2):\n");
    std::printf("----------------------------------\n");
    std::string footer;
    for (std::size_t i = 500; i < 512; ++i) {
        if (pg2[i] >= 32 && pg2[i] < 127) {
            footer += static_cast<char>(pg2[i]);
        }
    }
    std::printf("  Field chars: '%s'\n", footer.c_str());

    std::fclose(f);
}

void usage() {
    std::fprintf(stderr,
        "adi_inspect — OpenADS ADI index inspection utility\n"
        "\n"
        "Usage:  adi_inspect <file.adi>\n"
        "\n"
        "Displays tag directory structure, ordinals, field names,\n"
        "page numbers, and ordering mode (prepend vs append).\n");
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        usage();
        return 1;
    }

    fs::path path(argv[1]);
    if (!fs::exists(path)) {
        std::fprintf(stderr, "Error: file not found: '%s'\n", path.string().c_str());
        return 1;
    }

    inspect_adi(path);
    return 0;
}
