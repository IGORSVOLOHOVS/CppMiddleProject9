#pragma once

#include "mandelbrot_sender.hpp"
#include "types.hpp"
#include <exec/static_thread_pool.hpp>
#include <ranges>
#include <stdexec/execution.hpp>

class MandelbrotRenderer {
private:
    exec::static_thread_pool thread_pool_;

public:
    explicit MandelbrotRenderer(std::uint32_t num_threads = std::thread::hardware_concurrency())
        : thread_pool_{num_threads} {}

    template <size_t N>
    [[nodiscard]] auto RenderAsync(mandelbrot::ViewPort viewport, RenderSettings settings) {
        const std::uint32_t rows_per_task = settings.height / N;

        auto render_strips = [=, this]<std::size_t... Is>(std::index_sequence<Is...>) {
            auto create_strip_sender = [=, this](size_t i) {
                PixelRegion region{.start_row = static_cast<uint32_t>(i * rows_per_task),
                                   .end_row =
                                       static_cast<uint32_t>((i == N - 1) ? settings.height : (i + 1) * rows_per_task),
                                   .start_col = 0,
                                   .end_col = settings.width};

                return stdexec::schedule(thread_pool_.get_scheduler()) |
                       stdexec::let_value([=]() { return MandelbrotSender{viewport, settings, region}; }) |
                       stdexec::then([=](PixelMatrix pixel_matrix) {
                           ColorMatrix color_matrix(region.end_row - region.start_row,
                                                    std::vector<mandelbrot::RgbColor>(settings.width));

                           auto y_range = std::views::iota(0ul, color_matrix.size());
                           auto x_range = std::views::iota(0ul, color_matrix[0].size());

                           for (const auto &[y, x] : std::views::cartesian_product(y_range, x_range)) {
                               color_matrix[y][x] =
                                   mandelbrot::IterationsToColor(pixel_matrix[y][x], settings.max_iterations);
                           }
                           return std::make_pair(region, std::move(color_matrix));
                       });
            };
            return stdexec::when_all(create_strip_sender(Is)...);
        }(std::make_index_sequence<N>{});

        return std::move(render_strips) | stdexec::then([=](auto... results) {
                   auto start_time = std::chrono::steady_clock::now();
                   ColorMatrix final_image(settings.height, std::vector<mandelbrot::RgbColor>(settings.width));

                   auto process_result = [&](const auto &result_pair) {
                       const auto &[region, colors] = result_pair;
                       for (uint32_t y = 0; y < colors.size(); ++y) {
                           if (!colors.empty() && !colors[y].empty() && (region.start_row + y) < final_image.size()) {
                                std::ranges::copy(colors[y], final_image[region.start_row + y].begin());
                           }
                       }
                   };

                   (process_result(results), ...);

                   auto end_time = std::chrono::steady_clock::now();
                   auto render_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                   return RenderResult{.color_data = std::move(final_image),
                                       .viewport = viewport,
                                       .settings = settings,
                                       .render_time = render_time};
               });
    }
};

