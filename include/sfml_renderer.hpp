#pragma once

#include <SFML/Graphics.hpp>
#include <print>
#include <stdexec/execution.hpp>

#include "types.hpp"

// Этот сендер является "потребителем" (sink), он принимает результат и отрисовывает его
class SFMLRender {
public:
    using sender_concept = stdexec::sender_t;

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
                // Если есть данные для отрисовки (т.е. фрактал был пересчитан)
                if (!self.render_result_.color_data.empty()) {
                    // Заполняем sf::Image пикселями
                    for (size_t y = 0; y < self.render_result_.settings.height; ++y) {
                        for (size_t x = 0; x < self.render_result_.settings.width; ++x) {
                            const auto &color = self.render_result_.color_data[y][x];
                            self.image_.setPixel(x, y, sf::Color(color.r, color.g, color.b));
                        }
                    }
                    // Обновляем текстуру и спрайт
                    self.texture_.update(self.image_);
                    self.sprite_.setTexture(self.texture_);
                }

                // Отрисовываем кадр
                self.window_.clear();
                self.window_.draw(self.sprite_);
                self.window_.display();
                
                // Передаем управление дальше по пайплайну
                stdexec::set_value(std::move(self.receiver_));
            } catch (...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }
    };
    
    // Данные, необходимые для рендеринга
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

    // Сигнатуры: успех без значения или ошибка
    template <class Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t, const SFMLRender &, Env)
        -> stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr),
                                          stdexec::set_stopped_t()> {
        return {};
    }

    template <typename Receiver>
    friend auto tag_invoke(stdexec::connect_t, SFMLRender &&self, Receiver receiver) -> OperationState<Receiver> {
        return {std::move(receiver), std::move(self.render_result_), self.image_, self.texture_, self.sprite_,
                self.window_};
    }
};
