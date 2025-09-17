#include <gtest/gtest.h>
#include <optional>
#include <stdexec/execution.hpp>
#include <variant>

#include <SFML/Graphics.hpp>

#include "mandelbrot.hpp"
#include "mandelbrot_renderer.hpp"
#include "mandelbrot_sender.hpp"
#include "sfml_events_handler.hpp"
#include "sfml_renderer.hpp"
#include "types.hpp"

template <typename... Values>
struct TestReceiver {
    using receiver_concept = stdexec::receiver_t;

    std::variant<std::monostate, std::tuple<Values...>, std::exception_ptr, std::string> result;

    friend void tag_invoke(stdexec::set_value_t, TestReceiver &&self, Values... values) noexcept {
        self.result.template emplace<1>(std::move(values)...);
    }

    friend void tag_invoke(stdexec::set_error_t, TestReceiver &&self, std::exception_ptr err) noexcept {
        self.result.template emplace<2>(std::move(err));
    }

    friend void tag_invoke(stdexec::set_stopped_t, TestReceiver &&self) noexcept {
        self.result.template emplace<3>("stopped");
    }

    friend auto tag_invoke(stdexec::get_env_t, const TestReceiver &) noexcept { return stdexec::empty_env{}; }
};

TEST(MandelbrotSenderTest, CalculatesCorrectIterationsForKnownPoints) {
    RenderSettings settings{.width = 2, .height = 2, .max_iterations = 100, .escape_radius = 2.0};
    PixelRegion region{.start_row = 0, .end_row = 2, .start_col = 0, .end_col = 2};
    mandelbrot::ViewPort viewport{.x_min = -1.0, .x_max = 1.0, .y_min = -1.0, .y_max = 1.0};

    MandelbrotSender sender{viewport, settings, region};

    auto [result_matrix] = stdexec::sync_wait(std::move(sender)).value();

    ASSERT_EQ(result_matrix.size(), 2);
    ASSERT_EQ(result_matrix[0].size(), 2);

    EXPECT_NE(result_matrix[0][0], settings.max_iterations);
    EXPECT_EQ(result_matrix[1][1], settings.max_iterations);
}

TEST(MandelbrotRendererTest, RenderAsyncProducesCorrectSizeOutput) {
    RenderSettings settings{.width = 16, .height = 16, .max_iterations = 50};
    mandelbrot::ViewPort viewport{};
    MandelbrotRenderer renderer(4);

    auto render_sender = renderer.RenderAsync<4>(viewport, settings);

    auto [result] = stdexec::sync_wait(std::move(render_sender)).value();

    ASSERT_EQ(result.color_data.size(), settings.height);
    ASSERT_EQ(result.color_data[0].size(), settings.width);
    EXPECT_EQ(result.settings.width, settings.width);
    EXPECT_EQ(result.viewport.x_min, viewport.x_min);

    const auto &center_color = result.color_data[settings.height / 2][settings.width / 2];
    EXPECT_EQ(center_color.r, 0);
    EXPECT_EQ(center_color.g, 0);
    EXPECT_EQ(center_color.b, 0);
}

TEST(SfmlEventHandlerTest, SetsShouldExitOnWindowClose) {
    sf::RenderWindow window{sf::VideoMode({100, 100}), "Test"};
    RenderSettings settings{};
    AppState state{};
    sf::Clock clock;

    state.should_exit = true;

    SfmlEventHandler handler{window, settings, state, clock};
    TestReceiver<> receiver;
    auto op = stdexec::connect(std::move(handler), std::move(receiver));
    stdexec::start(op);

    ASSERT_TRUE(std::holds_alternative<std::string>(receiver.result));
    EXPECT_EQ(std::get<3>(receiver.result), "stopped");
}

TEST(SfmlEventHandlerTest, SetsRerenderOnZoom) {
    sf::RenderWindow window{sf::VideoMode({100, 100}), "Test"};
    RenderSettings settings{.width = 100, .height = 100};
    AppState state{};
    state.left_mouse_pressed = true;
    sf::Clock clock;

    SfmlEventHandler handler{window, settings, state, clock};

    stdexec::sync_wait(std::move(handler));

    EXPECT_TRUE(state.need_rerender);
    EXPECT_LT(state.viewport.width(), 4.0);
}

TEST(SFMLRenderTest, RendersDataWhenProvided) {
    RenderResult result;
    result.settings = {.width = 2, .height = 1};
    result.color_data = {{mandelbrot::RgbColor{255, 0, 0}, mandelbrot::RgbColor{0, 255, 0}}};

    sf::Image image;
    image.create(2, 1, sf::Color::Black);
    sf::Texture texture;
    sf::Sprite sprite;
    sf::RenderWindow window{sf::VideoMode({2, 1}), "Test"};

    SFMLRender renderer{std::move(result), image, texture, sprite, window, result.settings};

    stdexec::sync_wait(std::move(renderer));

    EXPECT_EQ(image.getPixel(0, 0), sf::Color::Red);
    EXPECT_EQ(image.getPixel(1, 0), sf::Color::Green);
}

TEST(SFMLRenderTest, SkipsUpdateForEmptyData) {
    RenderResult result;
    RenderSettings settings{.width = 2, .height = 1};

    sf::Image image;
    image.create(2, 1, sf::Color::Blue);
    sf::Texture texture;
    sf::Sprite sprite;
    sf::RenderWindow window{sf::VideoMode({2, 1}), "Test"};

    SFMLRender renderer{std::move(result), image, texture, sprite, window, settings};

    stdexec::sync_wait(std::move(renderer));

    EXPECT_EQ(image.getPixel(0, 0), sf::Color::Blue);
    EXPECT_EQ(image.getPixel(1, 0), sf::Color::Blue);
}

TEST(PipelineIntegrationTest, FullRenderPipelineProducesValidResultWhenRerenderIsNeeded) {
    RenderSettings settings{.width = 32, .height = 32, .max_iterations = 50};
    AppState state;
    state.need_rerender = true;

    MandelbrotRenderer renderer(2);

    CalculateMandelbrotAsyncSender pipeline_sender{state, settings, renderer};

    auto [result] = stdexec::sync_wait(std::move(pipeline_sender)).value();

    EXPECT_FALSE(result.color_data.empty());
    ASSERT_EQ(result.color_data.size(), settings.height);
    EXPECT_FALSE(state.need_rerender);
}

TEST(PipelineIntegrationTest, FullRenderPipelineSkipsRenderWhenNotNeeded) {
    RenderSettings settings{.width = 32, .height = 32, .max_iterations = 50};
    AppState state;
    state.need_rerender = false;

    MandelbrotRenderer renderer(2);

    CalculateMandelbrotAsyncSender pipeline_sender{state, settings, renderer};

    auto [result] = stdexec::sync_wait(std::move(pipeline_sender)).value();

    EXPECT_TRUE(result.color_data.empty());
}
