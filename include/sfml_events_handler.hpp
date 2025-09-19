#pragma once

#include <SFML/Graphics.hpp>
#include <stdexec/execution.hpp>

#include "types.hpp"

class SfmlEventHandler {
public:

    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        sf::RenderWindow &window_;
        RenderSettings render_settings_;
        AppState &state_;
        sf::Clock &zoom_clock_;

        static constexpr float ZOOM_INTERVAL_MS = 100.0f;

        friend void tag_invoke(stdexec::start_t, OperationState &self) noexcept {
            try {
                if (self.state_.should_exit) {
                    stdexec::set_stopped(std::move(self.receiver_));
                    return;
                }

                self.HandleEvents();
                self.HandleContinuousZoom();

                if (self.state_.should_exit) {
                    stdexec::set_stopped(std::move(self.receiver_));
                } else {
                    stdexec::set_value(std::move(self.receiver_));
                }
            } catch (...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }

    private:
        void HandleEvents() {
            sf::Event event;
            while (window_.pollEvent(event)) {
                switch (event.type) {
                case sf::Event::Closed:
                    state_.should_exit = true;
                    break;
                case sf::Event::MouseButtonPressed:
                    if (event.mouseButton.button == sf::Mouse::Left) state_.left_mouse_pressed = true;
                    if (event.mouseButton.button == sf::Mouse::Right) state_.right_mouse_pressed = true;
                    break;
                case sf::Event::MouseButtonReleased:
                    if (event.mouseButton.button == sf::Mouse::Left) state_.left_mouse_pressed = false;
                    if (event.mouseButton.button == sf::Mouse::Right) state_.right_mouse_pressed = false;
                    break;
                default:
                    break;
                }
            }
        }

        void HandleContinuousZoom() {
            if ((state_.left_mouse_pressed || state_.right_mouse_pressed) &&
                zoom_clock_.getElapsedTime().asMilliseconds() >= ZOOM_INTERVAL_MS) {

                sf::Vector2i mouse_pos = sf::Mouse::getPosition(window_);

                if (mouse_pos.x >= 0 && mouse_pos.x < static_cast<int>(render_settings_.width) && mouse_pos.y >= 0 &&
                    mouse_pos.y < static_cast<int>(render_settings_.height)) {

                    ZoomToPoint(mouse_pos.x, mouse_pos.y, state_.left_mouse_pressed);
                    zoom_clock_.restart();
                }
            }
        }

        void ZoomToPoint(int pixel_x, int pixel_y, bool zoom_in, double factor = 0.9) {
            const double target_x = state_.viewport.x_min +
                                    (static_cast<double>(pixel_x) / render_settings_.width) * state_.viewport.width();
            const double target_y = state_.viewport.y_min +
                                    (static_cast<double>(pixel_y) / render_settings_.height) * state_.viewport.height();

            const double zoom_factor = zoom_in ? factor : (1.0 / factor);
            const double new_width = state_.viewport.width() * zoom_factor;
            const double new_height = state_.viewport.height() * zoom_factor;

            state_.viewport.x_min = target_x - (static_cast<double>(pixel_x) / render_settings_.width) * new_width;
            state_.viewport.x_max = state_.viewport.x_min + new_width;
            state_.viewport.y_min = target_y - (static_cast<double>(pixel_y) / render_settings_.height) * new_height;
            state_.viewport.y_max = state_.viewport.y_min + new_height;
            state_.need_rerender = true;
        }
    };

    using sender_concept = stdexec::sender_t;

    SfmlEventHandler(sf::RenderWindow &window, RenderSettings render_settings, AppState &state, sf::Clock &zoom_clock)
        : window_{window}, render_settings_{render_settings}, state_{state}, zoom_clock_{zoom_clock} {}

    template <class Env>
    auto get_completion_signatures(Env &&) const -> stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()> {
        return {};
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver &&receiver) && -> OperationState<std::decay_t<Receiver>> {
        return {std::forward<Receiver>(receiver), window_, render_settings_, state_, zoom_clock_};
    }
private:
    sf::RenderWindow &window_;
    RenderSettings render_settings_;
    AppState &state_;
    sf::Clock &zoom_clock_;
};