// ELIPS v2 getting started (C++).
// Build with the project and run the compiled example binary.

#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

#include "elips/elips.hpp"
#include "elips/text_engine/TextEmbedderPort.hpp"

namespace {

class ToyEmbedder final : public elips::TextEmbedderPort {
public:
    [[nodiscard]] elips::Vector embed(std::string_view text) const override {
        const bool has_alpha = text.find("alpha") != std::string_view::npos;
        const bool has_beta = text.find("beta") != std::string_view::npos;
        return elips::Vector{{has_alpha ? 1.0F : 0.0F,
                              has_beta ? 1.0F : 0.0F}};
    }

    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "example";
    }

    [[nodiscard]] std::string_view model_name() const noexcept override {
        return "toy";
    }
};

}  // namespace

int main() {
    auto db = elips::open(
        ":memory:",
        elips::Config{}
            .dimension(2)
            .metric(elips::Metric::cosine)
            .text_embedder(std::make_shared<ToyEmbedder>()));

    auto& docs = db->vault("documents");

    elips::ChunkInfo chunk;
    chunk.document_key = "doc-alpha";
    chunk.ordinal = 0;
    chunk.char_start = 0;
    chunk.char_end = 17;

    docs.place_document("alpha design note",
                        {{"kind", std::string{"design"}}},
                        std::nullopt,
                        chunk);
    docs.place_document("beta incident runbook",
                        {{"kind", std::string{"ops"}}});

    std::cout << "text probe:\n";
    for (const auto& hit : docs.seek_text("alpha", 2)) {
        const auto title = hit.document.has_value() ? hit.document->text : "";
        std::cout << "  " << title << " distance=" << hit.distance << '\n';
    }

    std::cout << "\nhybrid probe:\n";
    for (const auto& hit :
         docs.seek_hybrid(elips::Vector{{0.0F, 1.0F}}, "alpha", 2)) {
        const auto kind = std::get<std::string>(hit.data.at("kind"));
        std::cout << "  " << kind << " distance=" << hit.distance << '\n';
    }

    const auto filter =
        elips::Filter().field("kind").equals(std::string{"design"});
    const auto plan = docs.explain_seek(elips::Vector{{1.0F, 0.0F}},
                                        1,
                                        filter,
                                        std::nullopt,
                                        true);
    std::cout << "\nplanner: candidates=" << plan.candidate_count
              << " metadata_accelerated=" << plan.metadata_accelerated
              << '\n';
    return 0;
}
