#pragma once

#include <stdexec/execution.hpp>

#include "mandelbrot_fractal_utils.hpp"
#include "types.hpp"

// Состояние операции для MandelbrotSender
// Выполняет основную вычислительную работу
template <typename Receiver>
struct MandelbrotOperationState {
    // Храним получателя, которому отправим результат
    Receiver receiver_;
    // Параметры для вычислений
    mandelbrot::ViewPort viewport_;
    RenderSettings settings_;
    PixelRegion region_;

    // Метод start() - точка входа для запуска операции
    friend void tag_invoke(stdexec::start_t, MandelbrotOperationState &self) noexcept {
        try {
            // Создаем матрицу для хранения количества итераций для каждого пикселя
            PixelMatrix pixels(self.region_.end_row - self.region_.start_row,
                               std::vector<std::uint32_t>(self.region_.end_col - self.region_.start_col));

            // Основной цикл вычислений
            for (std::uint32_t y = self.region_.start_row; y < self.region_.end_row; ++y) {
                for (std::uint32_t x = self.region_.start_col; x < self.region_.end_col; ++x) {
                    // 1. Преобразуем экранные координаты в комплексное число
                    mandelbrot::Complex c = mandelbrot::Pixel2DToComplex(x, y, self.viewport_, self.settings_.width,
                                                                         self.settings_.height);
                    // 2. Вычисляем количество итераций для этой точки
                    std::uint32_t iterations = mandelbrot::CalculateIterationsForPoint(
                        c, self.settings_.max_iterations, self.settings_.escape_radius);
                    // 3. Сохраняем результат в матрицу
                    pixels[y - self.region_.start_row][x - self.region_.start_col] = iterations;
                }
            }
            // Отправляем результат (матрицу итераций) дальше по цепочке
            stdexec::set_value(std::move(self.receiver_), std::move(pixels));
        } catch (...) {
            // В случае ошибки отправляем исключение
            stdexec::set_error(std::move(self.receiver_), std::current_exception());
        }
    }
};

// Сам сендер, который является "фабрикой" для состояний операций
struct MandelbrotSender {
    // Объявляем, что это сендер
    using sender_concept = stdexec::sender_t;

    mandelbrot::ViewPort viewport_;
    RenderSettings settings_;
    PixelRegion region_;

    // get_completion_signatures сообщает компилятору, что этот сендер может вернуть:
    // 1. Успех (set_value) с результатом типа PixelMatrix
    // 2. Ошибку (set_error) с типом std::exception_ptr
    // 3. Отмену (set_stopped) без параметров
    template <class Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t, const MandelbrotSender &, Env)
        -> stdexec::completion_signatures<stdexec::set_value_t(PixelMatrix),
                                          stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()> {
        return {};
    }

    // connect - "соединяет" сендер с получателем, создавая состояние операции
    template <typename Receiver>
    friend auto tag_invoke(stdexec::connect_t, MandelbrotSender &&self, Receiver receiver)
        -> MandelbrotOperationState<Receiver> {
        // Создаем и возвращаем состояние операции, передавая ему все необходимые данные
        return {std::move(receiver), self.viewport_, self.settings_, self.region_};
    }
};
