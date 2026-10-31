// The built-in upscaler (dw3/texrep/upscaler.h): a small network of the compact Real-ESRGAN layout written as ncnn
// files, checked against a plain convolution, and the ncnn files it refuses.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dw3/texrep/upscaler.h"

using namespace dw3::texrep;
namespace fs = std::filesystem;

namespace {

fs::path temp_dir(const char* name) {
    const fs::path p = fs::temp_directory_path() / (std::string("dw3_upscaler_") + name);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

struct Conv {
    int cin, cout;
    std::vector<float> w, b, slope;  // w: [cout][cin][3][3]
};

uint16_t to_half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000;
    const int exp = static_cast<int>((x >> 23) & 255) - 127 + 15;
    const uint32_t man = x & 0x7FFFFF;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    return static_cast<uint16_t>(sign | (exp << 10) | (man >> 13));
}

float from_half(uint16_t h) {
    const int exp = (h >> 10) & 31;
    const float v = exp ? std::ldexp(static_cast<float>((h & 1023) | 1024), exp - 25) : 0.0f;
    return (h & 0x8000) ? -v : v;
}

// Writes the network as <dir>/net.param and net.bin; `half` stores the weights of the first convolution as fp16 (and
// rounds them so, for the reference).
void write_net(const fs::path& dir, std::vector<Conv>& convs, int scale, bool half) {
    std::ofstream param(dir / "net.param"), bin(dir / "net.bin", std::ios::binary);
    param << "7767517\n" << (2 + 2 * convs.size()) << " " << (3 + 2 * convs.size()) << "\n";
    param << "Input data 0 1 data\n";
    int blob = 0;
    for (std::size_t i = 0; i < convs.size(); ++i) {
        Conv& c = convs[i];
        param << "Convolution conv" << i << " 1 1 " << blob << " " << blob + 1 << " 0=" << c.cout << " 1=3 11=3 2=1 3=1 4=1 5=1 6="
              << c.w.size() << "\n";
        ++blob;
        if (half && i == 0) {
            const uint32_t tag = 0x01306B47;
            bin.write(reinterpret_cast<const char*>(&tag), 4);
            for (float& v : c.w) {
                const uint16_t h = to_half(v);
                v = from_half(h);
                bin.write(reinterpret_cast<const char*>(&h), 2);
            }
            if (c.w.size() % 2) bin.write("\0\0", 2);
        } else {
            const uint32_t tag = 0;
            bin.write(reinterpret_cast<const char*>(&tag), 4);
            bin.write(reinterpret_cast<const char*>(c.w.data()), static_cast<std::streamsize>(4 * c.w.size()));
        }
        bin.write(reinterpret_cast<const char*>(c.b.data()), static_cast<std::streamsize>(4 * c.b.size()));
        if (!c.slope.empty()) {
            param << "PReLU prelu" << i << " 1 1 " << blob << " " << blob + 1 << " 0=" << c.cout << "\n";
            ++blob;
            bin.write(reinterpret_cast<const char*>(c.slope.data()), static_cast<std::streamsize>(4 * c.slope.size()));
        }
    }
    param << "PixelShuffle shuffle 1 1 " << blob << " " << blob + 1 << " 0=" << scale << "\n";
}

std::vector<Conv> random_net(int feat, int scale, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    auto conv = [&](int cin, int cout, bool prelu) {
        Conv c{cin, cout, {}, {}, {}};
        c.w.resize(static_cast<std::size_t>(cin) * cout * 9);
        for (float& v : c.w) v = u(rng);
        c.b.resize(cout);
        for (float& v : c.b) v = u(rng) * 0.1f;
        if (prelu) {
            c.slope.resize(cout);
            for (float& v : c.slope) v = 0.25f + u(rng);
        }
        return c;
    };
    return {conv(3, feat, true), conv(feat, feat, true), conv(feat, 3 * scale * scale, false)};
}

// The network on the whole image at once, in doubles, straight from its definition.
Image reference(const std::vector<Conv>& convs, int scale, const Image& in) {
    const int w = in.w, h = in.h;
    std::vector<double> x(static_cast<std::size_t>(3) * w * h);
    for (int y = 0; y < h; ++y)
        for (int i = 0; i < w; ++i)
            for (int c = 0; c < 3; ++c) x[(static_cast<std::size_t>(c) * h + y) * w + i] = ((in.rgba[y * w + i] >> (8 * c)) & 255) / 255.0;
    for (const Conv& cv : convs) {
        std::vector<double> o(static_cast<std::size_t>(cv.cout) * w * h);
        for (int co = 0; co < cv.cout; ++co)
            for (int y = 0; y < h; ++y)
                for (int i = 0; i < w; ++i) {
                    double s = cv.b[co];
                    for (int ci = 0; ci < cv.cin; ++ci)
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int yy = y + dy, xx = i + dx;
                                if (yy < 0 || yy >= h || xx < 0 || xx >= w) continue;
                                s += cv.w[((static_cast<std::size_t>(co) * cv.cin + ci) * 3 + dy + 1) * 3 + dx + 1] *
                                     x[(static_cast<std::size_t>(ci) * h + yy) * w + xx];
                            }
                    if (!cv.slope.empty() && s < 0) s *= cv.slope[co];
                    o[(static_cast<std::size_t>(co) * h + y) * w + i] = s;
                }
        x = std::move(o);
    }
    Image out;
    out.w = w * scale;
    out.h = h * scale;
    out.rgba.assign(static_cast<std::size_t>(out.w) * out.h, 0xFF000000u);
    for (int y = 0; y < out.h; ++y)
        for (int i = 0; i < out.w; ++i)
            for (int c = 0; c < 3; ++c) {
                const int ch = c * scale * scale + (y % scale) * scale + i % scale;
                const double v = x[(static_cast<std::size_t>(ch) * h + y / scale) * w + i / scale] +
                                 ((in.rgba[(y / scale) * w + i / scale] >> (8 * c)) & 255) / 255.0;
                const int byte = static_cast<int>(std::fmin(std::fmax(v * 255.0 + 0.5, 0.0), 255.0));
                out.rgba[static_cast<std::size_t>(y) * out.w + i] |= static_cast<uint32_t>(byte) << (8 * c);
            }
    return out;
}

Image noise(int w, int h, unsigned seed) {
    std::mt19937 rng(seed);
    Image img;
    img.w = w;
    img.h = h;
    for (int i = 0; i < w * h; ++i) img.rgba.push_back(rng() | 0xFF000000u);
    return img;
}

int max_difference(const Image& a, const Image& b) {
    int worst = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i)
        for (int c = 0; c < 4; ++c)
            worst = std::max(worst, std::abs(static_cast<int>((a.rgba[i] >> (8 * c)) & 255) - static_cast<int>((b.rgba[i] >> (8 * c)) & 255)));
    return worst;
}

}  // namespace

TEST(BuiltinUpscaler, MatchesThePlainConvolution) {
    const fs::path dir = temp_dir("match");
    std::vector<Conv> net = random_net(16, 2, 7);
    write_net(dir, net, 2, true);
    std::string error;
    auto model = CompactUpscaler::load(dir, "net", error);
    ASSERT_TRUE(model) << error;
    EXPECT_EQ(model->scale(), 2);
    // Odd sizes, so that the last group of pixels in a row is partial.
    const Image in = noise(13, 9, 3);
    const Image out = model->upscale(in);
    ASSERT_EQ(out.w, 26);
    ASSERT_EQ(out.h, 18);
    EXPECT_LE(max_difference(out, reference(net, 2, in)), 1);
}

TEST(BuiltinUpscaler, TilesSeeTheirSurroundings) {
    // With 10 pixels of context per tile, a two-layer-deep network cannot tell tiles from the whole picture.
    const fs::path dir = temp_dir("tiles");
    std::vector<Conv> net = random_net(16, 4, 11);
    write_net(dir, net, 4, false);
    std::string error;
    auto model = CompactUpscaler::load(dir, "net", error);
    ASSERT_TRUE(model) << error;
    const Image in = noise(40, 23, 5);
    EXPECT_LE(max_difference(model->upscale(in, 16), reference(net, 4, in)), 1);
}

TEST(BuiltinUpscaler, BatchKeepsTheOrderAndCanBeCancelled) {
    const fs::path dir = temp_dir("batch");
    std::vector<Conv> net = random_net(16, 2, 13);
    write_net(dir, net, 2, false);
    std::string error;
    std::shared_ptr<const CompactUpscaler> model = CompactUpscaler::load(dir, "net", error);
    ASSERT_TRUE(model) << error;
    std::vector<Image> in = {noise(5, 4, 1), noise(8, 3, 2), noise(3, 7, 3), noise(6, 6, 4)};
    std::vector<Image> out;
    ASSERT_TRUE(builtin_batch_upscaler(model, 3)(in, out, error)) << error;
    ASSERT_EQ(out.size(), in.size());
    for (std::size_t i = 0; i < in.size(); ++i) EXPECT_EQ(out[i].rgba, model->upscale(in[i]).rgba);
    EXPECT_FALSE(builtin_batch_upscaler(model, 2, [] { return true; })(in, out, error));
    EXPECT_EQ(error, "Cancelled.");
}

TEST(BuiltinUpscaler, RefusesOtherNetworks) {
    const fs::path dir = temp_dir("refuse");
    std::vector<Conv> net = random_net(16, 2, 17);
    write_net(dir, net, 2, false);
    std::string error;
    // A layer the compact network does not have.
    {
        std::ifstream in(dir / "net.param");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream(dir / "other.param") << text << "Sigmoid extra 1 1 9 10\n";
        fs::copy_file(dir / "net.bin", dir / "other.bin");
    }
    EXPECT_FALSE(CompactUpscaler::load(dir, "other", error));
    EXPECT_NE(error.find("Sigmoid"), std::string::npos);
    // Weights that do not match the .param.
    fs::copy_file(dir / "net.param", dir / "short.param");
    {
        std::ofstream(dir / "short.bin", std::ios::binary) << "1234";
    }
    EXPECT_FALSE(CompactUpscaler::load(dir, "short", error));
    {
        std::ifstream in(dir / "net.bin", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        fs::copy_file(dir / "net.param", dir / "long.param");
        std::ofstream(dir / "long.bin", std::ios::binary) << bytes << "extra!!!";
    }
    EXPECT_FALSE(CompactUpscaler::load(dir, "long", error));
    EXPECT_FALSE(CompactUpscaler::load(dir, "missing", error));
}
