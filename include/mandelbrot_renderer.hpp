#pragma once

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#include <numeric>
#include <array>
#include <utility>

#include "mandelbrot_sender.hpp"
#include "types.hpp"
#include "mandelbrot_fractal_utils.hpp"

// Класс-фабрика, создающий сложный асинхронный сендер для рендеринга всего фрактала
class MandelbrotRenderer {
private:
    exec::static_thread_pool thread_pool_;

public:
    explicit MandelbrotRenderer(std::uint32_t num_threads = std::thread::hardware_concurrency())
        : thread_pool_{num_threads} {}

    template <size_t N>
    [[nodiscard]] auto RenderAsync(mandelbrot::ViewPort viewport, RenderSettings settings) {
        // 1. Разделяем экран на N горизонтальных полос для параллельной обработки
        std::array<PixelRegion, N> regions;
        const std::uint32_t rows_per_task = settings.height / N;
        for (size_t i = 0; i < N; ++i) {
            regions[i] = {
                .start_row = static_cast<std::uint32_t>(i * rows_per_task),
                .end_row = static_cast<std::uint32_t>((i == N - 1) ? settings.height : (i + 1) * rows_per_task),
                .start_col = 0,
                .end_col = settings.width};
        }

        // Лямбда для обработки одной полосы
        auto process_region = [&](const PixelRegion &region) {
            // 2. Запланировать (schedule) выполнение...
            return stdexec::schedule(thread_pool_.get_scheduler()) |
                   // ...нового асинхронного действия (let_value), которое...
                   stdexec::let_value([=]() {
                       // ...возвращает сендер для вычисления итераций в данной области
                       return MandelbrotSender{viewport, settings, region};
                   }) |
                   // 3. После того как MandelbrotSender завершится, преобразовать его результат (PixelMatrix)...
                   stdexec::then([=](PixelMatrix pixel_matrix) {
                       // ...в матрицу цветов
                       ColorMatrix color_matrix(region.end_row - region.start_row,
                                                std::vector<mandelbrot::RgbColor>(region.end_col - region.start_col));
                       for (size_t y = 0; y < color_matrix.size(); ++y) {
                           for (size_t x = 0; x < color_matrix[0].size(); ++x) {
                               color_matrix[y][x] =
                                   mandelbrot::IterationsToColor(pixel_matrix[y][x], settings.max_iterations);
                           }
                       }
                       // Возвращаем пару: область и матрицу цветов для нее
                       return std::make_pair(region, std::move(color_matrix));
                   });
        };

        // 4. Используем std::apply и fold expression для создания и объединения сендеров для всех регионов
        auto all_tasks_sender = std::apply(
            [&](auto... regions_pack) { return stdexec::when_all(process_region(regions_pack)...); }, regions);

        // 5. После завершения всех параллельных задач, объединяем результаты
        return std::move(all_tasks_sender) | stdexec::then([=](auto... results) {
                   RenderResult final_result;
                   final_result.settings = settings;
                   final_result.viewport = viewport;
                   final_result.color_data.resize(settings.height, std::vector<mandelbrot::RgbColor>(settings.width));

                   // Распаковываем результаты от when_all и собираем итоговое изображение
                   auto unpacker = [&](const auto &result_pair) {
                       const auto &[region, color_matrix] = result_pair;
                       for (size_t y = 0; y < color_matrix.size(); ++y) {
                           for (size_t x = 0; x < color_matrix[0].size(); ++x) {
                               final_result.color_data[region.start_row + y][region.start_col + x] = color_matrix[y][x];
                           }
                       }
                   };
                   (unpacker(results), ...);

                   return final_result;
               });
    }
};

