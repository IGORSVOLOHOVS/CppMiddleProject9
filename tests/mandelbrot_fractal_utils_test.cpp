#include <gtest/gtest.h>
#include "mandelbrot_fractal_utils.hpp"

TEST(MandelbrotFractalUtilsTest, CalculateIterationsForPoint_InsideSet) {
    const mandelbrot::Complex point_inside{0.0, 0.0};
    const std::uint32_t max_iterations = 100;
    const double escape_radius = 2.0;

    const std::uint32_t iterations = mandelbrot::CalculateIterationsForPoint(point_inside, max_iterations, escape_radius);
    EXPECT_EQ(iterations, max_iterations);
}

TEST(MandelbrotFractalUtilsTest, CalculateIterationsForPoint_OutsideSet) {
    const mandelbrot::Complex point_outside{3.0, 3.0};
    const std::uint32_t max_iterations = 100;
    const double escape_radius = 2.0;

    const std::uint32_t iterations = mandelbrot::CalculateIterationsForPoint(point_outside, max_iterations, escape_radius);
    EXPECT_LT(iterations, max_iterations);
}

TEST(MandelbrotFractalUtilsTest, Pixel2DToComplex) {
    const mandelbrot::ViewPort viewport;
    const std::uint32_t screen_width = 800;
    const std::uint32_t screen_height = 600;

    const mandelbrot::Complex complex_point = mandelbrot::Pixel2DToComplex(0, 0, viewport, screen_width, screen_height);

    EXPECT_DOUBLE_EQ(complex_point.real(), viewport.x_min);
    EXPECT_DOUBLE_EQ(complex_point.imag(), viewport.y_min);
}

TEST(MandelbrotFractalUtilsTest, IterationsToColor_InsideSet) {
    const std::uint32_t max_iterations = 100;
    const mandelbrot::RgbColor color = mandelbrot::IterationsToColor(max_iterations, max_iterations);

    EXPECT_EQ(color.r, mandelbrot::RgbColors::BLACK.r);
    EXPECT_EQ(color.g, mandelbrot::RgbColors::BLACK.g);
    EXPECT_EQ(color.b, mandelbrot::RgbColors::BLACK.b);
}

TEST(MandelbrotFractalUtilsTest, IterationsToColor_OutsideSet) {
    const std::uint32_t max_iterations = 100;
    const mandelbrot::RgbColor color = mandelbrot::IterationsToColor(50, max_iterations);

    EXPECT_NE(color, mandelbrot::RgbColors::BLACK);
}