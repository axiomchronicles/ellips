#ifndef ELIPS_DOMAIN_RECORD_HPP
#define ELIPS_DOMAIN_RECORD_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

#include "elips/domain/RecordID.hpp"
#include "elips/domain/Vector.hpp"

namespace elips {

// A single metadata value. Dynamic schema (no upfront declaration).
using MetaValue = std::variant<std::int64_t, double, bool, std::string>;

// Metadata payload attached to a record: key -> typed value.
using Payload = std::map<std::string, MetaValue>;

struct DocumentAttachment {
    std::string text;
    std::string uri;
    std::string mime_type{"text/plain"};
};

struct ChunkInfo {
    std::string document_key;
    std::uint32_t ordinal{0};
    std::uint32_t char_start{0};
    std::uint32_t char_end{0};
};

struct EmbeddingLineage {
    std::string provider;
    std::string model;
    std::string revision;
    Payload attributes;
};

// A vector with identity and payload.
struct Record {
    RecordID id;
    Vector vector;
    Payload payload;
    std::optional<DocumentAttachment> document;
    std::optional<ChunkInfo> chunk;
    std::optional<EmbeddingLineage> lineage;
};

}  // namespace elips

#endif  // ELIPS_DOMAIN_RECORD_HPP
