#pragma once

#include "mandelbrot_renderer.hpp"

// Этот сендер решает, нужно ли запускать ресурсоемкий рендеринг
class CalculateMandelbrotAsyncSender {
private:
    // Вложенный класс состояния операции
    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        AppState &state_;
        RenderSettings render_settings_;
        MandelbrotRenderer &renderer_;

        friend void tag_invoke(stdexec::start_t, OperationState &self) noexcept {
            try {
                // Проверяем, нужна ли перерисовка
                if (self.state_.need_rerender) {
                    self.state_.need_rerender = false;
                    // Если да, получаем сендер для асинхронного рендеринга...
                    auto render_sender =
                        self.renderer_.template RenderAsync<THREAD_POOL_SIZE>(self.state_.viewport, self.render_settings_);
                    // ...соединяем его с нашим получателем и запускаем.
                    // Здесь происходит передача управления асинхронной части пайплайна
                    auto op = stdexec::connect(std::move(render_sender), std::move(self.receiver_));
                    stdexec::start(op);
                } else {
                    // Если нет, просто возвращаем пустой результат дальше по цепочке
                    stdexec::set_value(std::move(self.receiver_), RenderResult{});
                }
            } catch(...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }
    };

public:
    using sender_concept = stdexec::sender_t;

    explicit CalculateMandelbrotAsyncSender(AppState &state, RenderSettings render_settings,
                                            MandelbrotRenderer &renderer)
        : state_(state), render_settings_{render_settings}, renderer_{renderer} {}

    // Сигнатуры: возвращает либо RenderResult, либо ошибку
    template <class Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t, const CalculateMandelbrotAsyncSender &, Env)
        -> stdexec::completion_signatures<stdexec::set_value_t(RenderResult),
                                          stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()> {
        return {};
    }

    template <typename Receiver>
    friend auto tag_invoke(stdexec::connect_t, CalculateMandelbrotAsyncSender &&self, Receiver receiver)
        -> OperationState<Receiver> {
        return {std::move(receiver), self.state_, self.render_settings_, self.renderer_};
    }

private:
    RenderSettings render_settings_;
    MandelbrotRenderer &renderer_;
    AppState &state_;
};

