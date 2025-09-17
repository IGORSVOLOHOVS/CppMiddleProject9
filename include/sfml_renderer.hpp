#pragma once

#include <SFML/Graphics.hpp>
#include <print>
#include <stdexec/execution.hpp>
#include <ranges>

#include "types.hpp"

class SFMLRender {
private:
    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        RenderResult render_result_;
        sf::Image &image_;
        sf::Texture &texture_;
        sf::Sprite &sprite_;
        sf::RenderWindow &window_;

        friend void tag_invoke(stdexec::start_t, OperationState &self) noexcept {
            try {
                if (!self.render_result_.color_data.empty()) {
                    const auto& colors = self.render_result_.color_data;
                    auto height = colors.size();
                    auto width = colors[0].size();

                    auto y_range = std::views::iota(0u, (uint32_t)height);
                    auto x_range = std::views::iota(0u, (uint32_t)width);

                    for (const auto& [y, x] : std::views::cartesian_product(y_range, x_range)) {
                        const auto &color = colors[y][x];
                        self.image_.setPixel(x, y, sf::Color(color.r, color.g, color.b));
                    }
                }

                self.window_.clear();
                self.texture_.update(self.image_);
                self.sprite_.setTexture(self.texture_);
                self.window_.draw(self.sprite_);
                self.window_.display();
                stdexec::set_value(std::move(self.receiver_));
            } catch (...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }
    };

public:
    using sender_concept = stdexec::sender_t;

    RenderResult render_result_;
    sf::Image &image_;
    sf::Texture &texture_;
    sf::Sprite &sprite_;
    sf::RenderWindow &window_;
    RenderSettings render_settings_;

    SFMLRender(RenderResult render_result, sf::Image &image, sf::Texture &texture, sf::Sprite &sprite,
               sf::RenderWindow &window, RenderSettings render_settings)
        : render_result_(std::move(render_result)), image_{image}, texture_{texture}, sprite_{sprite}, window_{window},
          render_settings_{render_settings} {}

    template <class Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t, const SFMLRender &, Env)
        -> stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr),
                                          stdexec::set_stopped_t()> {
        return {};
    }

    template <typename Receiver>
    friend auto tag_invoke(stdexec::connect_t, SFMLRender &&self, Receiver receiver) -> OperationState<Receiver> {
        return {std::move(receiver), std::move(self.render_result_), self.image_, self.texture_, self.sprite_, self.window_};
    }
};

