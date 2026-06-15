#ifndef ELIPS_TEXT_ENGINE_TEXT_EMBEDDER_PORT_HPP
#define ELIPS_TEXT_ENGINE_TEXT_EMBEDDER_PORT_HPP

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "elips/domain/Vector.hpp"

namespace elips {

class TextEmbedderPort {
public:
    virtual ~TextEmbedderPort() = default;

    [[nodiscard]] virtual Vector embed(std::string_view text) const = 0;

    [[nodiscard]] virtual std::vector<Vector> embed_batch(
        const std::vector<std::string>& texts) const {
        std::vector<Vector> embedded;
        embedded.reserve(texts.size());
        for (const auto& text : texts) {
            embedded.push_back(embed(text));
        }
        return embedded;
    }

    [[nodiscard]] virtual std::string_view provider_name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view model_name() const noexcept = 0;
};

using TextEmbedderPtr = std::shared_ptr<TextEmbedderPort>;

}  // namespace elips

#endif  // ELIPS_TEXT_ENGINE_TEXT_EMBEDDER_PORT_HPP
