#pragma once

#include "types.hpp"
#include <ranges>
#include <stdexec/execution.hpp>

class MandelbrotSender {
public:

    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        mandelbrot::ViewPort viewport_;
        RenderSettings settings_;
        PixelRegion region_;

        friend void tag_invoke(stdexec::start_t, OperationState &self) noexcept {
            try {
                PixelMatrix pixel_matrix(self.region_.end_row - self.region_.start_row,
                                         std::vector<std::uint32_t>(self.region_.end_col - self.region_.start_col));

                auto y_range = std::views::iota(self.region_.start_row, self.region_.end_row);
                auto x_range = std::views::iota(self.region_.start_col, self.region_.end_col);

                for (const auto &[y, x] : std::views::cartesian_product(y_range, x_range)) {
                    const mandelbrot::Complex c =
                        mandelbrot::Pixel2DToComplex(x, y, self.viewport_, self.settings_.width, self.settings_.height);
                    pixel_matrix[y - self.region_.start_row][x - self.region_.start_col] =
                        mandelbrot::CalculateIterationsForPoint(c, self.settings_.max_iterations,
                                                                self.settings_.escape_radius);
                }

                stdexec::set_value(std::move(self.receiver_), std::move(pixel_matrix));
            } catch (...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }
    };

    using sender_concept = stdexec::sender_t;

    mandelbrot::ViewPort viewport_;
    RenderSettings settings_;
    PixelRegion region_;

    template <class Env>
    auto get_completion_signatures(Env &&) const -> stdexec::completion_signatures<
        stdexec::set_value_t(PixelMatrix),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()> {
        return {};
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver &&receiver) && -> OperationState<std::decay_t<Receiver>> {
        return {std::move(receiver), viewport_, settings_, region_};
    }
};