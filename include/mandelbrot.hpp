#pragma once

#include "mandelbrot_renderer.hpp" 
#include <stdexec/execution.hpp>

class CalculateMandelbrotAsyncSender {
public:

    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        AppState &state_;
        RenderSettings render_settings_;
        MandelbrotRenderer &renderer_;

        friend void tag_invoke(stdexec::start_t, OperationState &self) noexcept {
            try {
                if (self.state_.need_rerender) {
                    self.state_.need_rerender = false;

                    auto render_sender = self.renderer_.template RenderAsync<THREAD_POOL_SIZE>(self.state_.viewport,
                                                                                               self.render_settings_);
                    
                    auto result = stdexec::sync_wait(std::move(render_sender));
                    
                    if (result) {
                        auto [data] = std::move(*result);
                        stdexec::set_value(std::move(self.receiver_), std::move(data));
                    } else {
                        stdexec::set_stopped(std::move(self.receiver_));
                    }
                } else {
                    stdexec::set_value(std::move(self.receiver_), RenderResult{});
                }
            } catch (...) {
                stdexec::set_error(std::move(self.receiver_), std::current_exception());
            }
        }
    };

    using sender_concept = stdexec::sender_t;

    explicit CalculateMandelbrotAsyncSender(AppState &state, RenderSettings render_settings,
                                            MandelbrotRenderer &renderer)
        : state_(state), render_settings_{render_settings}, renderer_{renderer} {}

    template <class Env>
    auto get_completion_signatures(Env &&) const -> stdexec::completion_signatures<
        stdexec::set_value_t(RenderResult),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()> {
        return {};
    }
    
    template <stdexec::receiver Receiver>
    auto connect(Receiver &&receiver) && -> OperationState<std::decay_t<Receiver>> {
        return {std::forward<Receiver>(receiver), state_, render_settings_, renderer_};
    }


private:
    RenderSettings render_settings_;
    MandelbrotRenderer &renderer_;
    AppState &state_;
};