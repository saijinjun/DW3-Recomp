#include "dw3/texrep/upscaler.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace dw3::texrep {

namespace {

constexpr int kPrePad = 10;     // context given to the network around each tile, as the GPU program does
constexpr int kLanes = 16;      // output channels computed together; every layer's output count is a multiple
constexpr int kPixels = 4;      // neighbouring pixels computed together
constexpr uint32_t kFp16Tag = 0x01306B47;

bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

float half_to_float(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 31, man = h & 1023;
    float v;
    if (exp == 0) v = std::ldexp(static_cast<float>(man), -24);
    else if (exp == 31) v = man ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(man | 1024), static_cast<int>(exp) - 25);
    return sign ? -v : v;
}

// Reads the weight blobs in the order the layers name them.
class Blob {
public:
    explicit Blob(const std::string& data) : data_(data) {}
    bool take(std::size_t count, bool tagged, std::vector<float>& out, std::string& error) {
        out.resize(count);
        if (tagged) {
            uint32_t tag = 0;
            if (!bytes(&tag, 4, error)) return false;
            if (tag == kFp16Tag) {
                std::vector<uint16_t> half(count);
                if (!bytes(half.data(), 2 * count, error)) return false;
                for (std::size_t i = 0; i < count; ++i) out[i] = half_to_float(half[i]);
                pos_ += (4 - pos_ % 4) % 4;
                return true;
            }
            if (tag != 0) {
                error = "unsupported weight storage";
                return false;
            }
        }
        return bytes(out.data(), 4 * count, error);
    }
    bool at_end() const { return pos_ == data_.size(); }

private:
    bool bytes(void* dst, std::size_t n, std::string& error) {
        if (pos_ + n > data_.size()) {
            error = "the .bin is shorter than the .param needs";
            return false;
        }
        std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return true;
    }
    const std::string& data_;
    std::size_t pos_ = 0;
};

#if defined(__GNUC__) || defined(__clang__)
typedef float f32x4 __attribute__((vector_size(16)));
#define DW3_VECTOR 1
#endif

// One 3x3 convolution, stride 1, over a w x h picture held with a border of zeros (a row is `stride` pixels; `in` has
// cin channels a pixel and `out` cout). Writes only the inside of `out`, so its border stays zero.
void convolve(const CompactUpscaler::Layer& layer, const float* in, float* out, int w, int h, int stride) {
    const int cin = layer.cin, cout = layer.cout;
    for (int y = 0; y < h; ++y)
        for (int x0 = 0; x0 < w; x0 += kPixels) {
            const int count = std::min(kPixels, w - x0);
            for (int cb = 0; cb < cout / kLanes; ++cb) {
                const float* wp = layer.weights.data() + static_cast<std::size_t>(cb) * 9 * cin * kLanes;
#if DW3_VECTOR
                f32x4 acc[kPixels][kLanes / 4];
                for (int p = 0; p < kPixels; ++p)
                    for (int l = 0; l < kLanes / 4; ++l) std::memcpy(&acc[p][l], layer.bias.data() + cb * kLanes + 4 * l, 16);
                for (int dy = 0; dy < 3; ++dy)
                    for (int dx = 0; dx < 3; ++dx) {
                        const float* row = in + (static_cast<std::size_t>(y + dy) * stride + x0 + dx) * cin;
                        for (int ci = 0; ci < cin; ++ci, wp += kLanes) {
                            f32x4 wv[kLanes / 4];
                            std::memcpy(wv, wp, sizeof wv);
                            for (int p = 0; p < kPixels; ++p) {
                                const float a = row[p * cin + ci];
                                for (int l = 0; l < kLanes / 4; ++l) acc[p][l] += a * wv[l];
                            }
                        }
                    }
                for (int p = 0; p < count; ++p) {
                    float* dst = out + ((static_cast<std::size_t>(y) + 1) * stride + x0 + p + 1) * cout + cb * kLanes;
                    std::memcpy(dst, acc[p], kLanes * sizeof(float));
                }
#else
                float acc[kPixels][kLanes];
                for (int p = 0; p < kPixels; ++p) std::memcpy(acc[p], layer.bias.data() + cb * kLanes, sizeof acc[p]);
                for (int dy = 0; dy < 3; ++dy)
                    for (int dx = 0; dx < 3; ++dx) {
                        const float* row = in + (static_cast<std::size_t>(y + dy) * stride + x0 + dx) * cin;
                        for (int ci = 0; ci < cin; ++ci, wp += kLanes)
                            for (int p = 0; p < kPixels; ++p) {
                                const float a = row[p * cin + ci];
                                for (int l = 0; l < kLanes; ++l) acc[p][l] += a * wp[l];
                            }
                    }
                for (int p = 0; p < count; ++p) {
                    float* dst = out + ((static_cast<std::size_t>(y) + 1) * stride + x0 + p + 1) * cout + cb * kLanes;
                    std::memcpy(dst, acc[p], sizeof acc[p]);
                }
#endif
                if (!layer.slope.empty())
                    for (int p = 0; p < count; ++p) {
                        float* dst = out + ((static_cast<std::size_t>(y) + 1) * stride + x0 + p + 1) * cout + cb * kLanes;
                        for (int l = 0; l < kLanes; ++l)
                            if (dst[l] < 0.0f) dst[l] *= layer.slope[cb * kLanes + l];
                    }
            }
        }
}

}  // namespace

std::unique_ptr<CompactUpscaler> CompactUpscaler::load(const std::filesystem::path& dir, const std::string& name, std::string& error) {
    const std::filesystem::path param_path = dir / (name + ".param"), bin_path = dir / (name + ".bin");
    std::string text, blob_data;
    if (!read_file(param_path, text)) {
        error = "cannot read " + param_path.string();
        return nullptr;
    }
    if (!read_file(bin_path, blob_data)) {
        error = "cannot read " + bin_path.string();
        return nullptr;
    }
    auto fail = [&](const std::string& what) {
        error = param_path.string() + ": " + what;
        return nullptr;
    };
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.find("7767517") != 0) return fail("not an ncnn param file");
    std::getline(lines, line);  // layer and blob counts

    auto model = std::unique_ptr<CompactUpscaler>(new CompactUpscaler());
    Blob blob(blob_data);
    std::string blob_error;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string kind, layer_name;
        int inputs = 0, outputs = 0;
        if (!(fields >> kind >> layer_name >> inputs >> outputs)) continue;
        std::string word;
        for (int i = 0; i < inputs + outputs; ++i) fields >> word;
        std::map<std::string, std::string> params;
        while (fields >> word)
            if (const auto eq = word.find('='); eq != std::string::npos) params[word.substr(0, eq)] = word.substr(eq + 1);
        auto get = [&](const char* key, const char* fallback) { return params.count(key) ? params[key] : std::string(fallback); };
        if (kind == "Input" || kind == "Split" || kind == "Interp" || kind == "BinaryOp") continue;
        if (kind == "Convolution") {
            if (get("1", "0") != "3" || get("2", "1") != "1" || get("3", "1") != "1" || get("4", "0") != "1")
                return fail("only 3x3 stride 1 pad 1 convolutions are supported");
            Layer layer;
            layer.cout = std::atoi(get("0", "0").c_str());
            const std::size_t count = std::strtoull(get("6", "0").c_str(), nullptr, 10);
            if (layer.cout <= 0 || count % (static_cast<std::size_t>(layer.cout) * 9) != 0)
                return fail("unexpected convolution size");
            layer.cin = static_cast<int>(count / (static_cast<std::size_t>(layer.cout) * 9));
            std::vector<float> w;  // [cout][cin][3][3]
            if (!blob.take(count, true, w, blob_error)) return fail(blob_error);
            if (get("5", "0") == "1") {
                if (!blob.take(static_cast<std::size_t>(layer.cout), false, layer.bias, blob_error)) return fail(blob_error);
            } else {
                layer.bias.assign(static_cast<std::size_t>(layer.cout), 0.0f);
            }
            // Channels are held in groups of kLanes: a layer with fewer gets zero weights for the rest (and the next
            // layer zero weights for them as inputs).
            const int cin = layer.cin;
            const int in_stride = model->layers_.empty() ? cin : model->layers_.back().cout;
            const int cpad = (layer.cout + kLanes - 1) / kLanes * kLanes;
            layer.weights.assign(static_cast<std::size_t>(cpad) * 9 * in_stride, 0.0f);
            for (int co = 0; co < layer.cout; ++co)
                for (int ci = 0; ci < cin; ++ci)
                    for (int k = 0; k < 9; ++k)
                        layer.weights[((static_cast<std::size_t>(co / kLanes) * 9 + k) * in_stride + ci) * kLanes + co % kLanes] =
                            w[(static_cast<std::size_t>(co) * cin + ci) * 9 + k];
            if (!model->layers_.empty() && cin != model->real_cout_.back()) return fail("the layers do not connect");
            model->real_cout_.push_back(layer.cout);
            layer.bias.resize(static_cast<std::size_t>(cpad), 0.0f);
            layer.cin = in_stride;
            layer.cout = cpad;
            model->layers_.push_back(std::move(layer));
        } else if (kind == "PReLU") {
            if (model->layers_.empty()) return fail("PReLU before any convolution");
            Layer& last = model->layers_.back();
            if (!blob.take(static_cast<std::size_t>(std::atoi(get("0", "0").c_str())), false, last.slope, blob_error)) return fail(blob_error);
            if (last.slope.size() != static_cast<std::size_t>(model->real_cout_.back())) return fail("PReLU size does not match its convolution");
            last.slope.resize(static_cast<std::size_t>(last.cout), 0.0f);
        } else if (kind == "PixelShuffle") {
            model->scale_ = std::atoi(get("0", "0").c_str());
        } else {
            return fail("layer " + kind + " is not part of the compact Real-ESRGAN network");
        }
    }
    const int s = model->scale_;
    if (s <= 0 || model->layers_.empty() || model->layers_.front().cin != 3 || model->real_cout_.back() != 3 * s * s ||
        !model->layers_.back().slope.empty())
        return fail("not a compact Real-ESRGAN network");
    if (!blob.at_end()) {
        error = bin_path.string() + ": bytes left over; the .bin does not match the .param";
        return nullptr;
    }
    return model;
}

Image CompactUpscaler::upscale(const Image& in, int tile) const {
    const int s = scale_;
    Image out;
    out.w = in.w * s;
    out.h = in.h * s;
    out.rgba.assign(static_cast<std::size_t>(out.w) * out.h, 0xFF000000u);
    if (in.w <= 0 || in.h <= 0) return out;
    tile = std::max(tile, 16);
    int widest = 0;
    for (const Layer& l : layers_) widest = std::max({widest, l.cin, l.cout});
    std::vector<float> a, b;
    for (int ty = 0; ty < in.h; ty += tile)
        for (int tx = 0; tx < in.w; tx += tile) {
            const int th = std::min(tile, in.h - ty), tw = std::min(tile, in.w - tx);
            const int y0 = std::max(0, ty - kPrePad), x0 = std::max(0, tx - kPrePad);
            const int y1 = std::min(in.h, ty + th + kPrePad), x1 = std::min(in.w, tx + tw + kPrePad);
            const int w = x1 - x0, h = y1 - y0;
            // Rows padded so that the last group of kPixels may read past the picture into zeros.
            const int stride = (w + kPixels - 1) / kPixels * kPixels + 2;
            const std::size_t size = static_cast<std::size_t>(stride) * (h + 2) * widest;
            a.assign(size, 0.0f);
            b.assign(size, 0.0f);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint32_t p = in.rgba[static_cast<std::size_t>(y0 + y) * in.w + x0 + x];
                    float* dst = a.data() + (static_cast<std::size_t>(y + 1) * stride + x + 1) * 3;
                    for (int c = 0; c < 3; ++c) dst[c] = static_cast<float>((p >> (8 * c)) & 255) / 255.0f;
                }
            for (const Layer& layer : layers_) {
                // The output's border must be zero for the next layer: clear what an earlier, wider layer left there.
                const std::size_t used = static_cast<std::size_t>(stride) * (h + 2) * layer.cout;
                std::fill(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(used), 0.0f);
                convolve(layer, a.data(), b.data(), w, h, stride);
                std::swap(a, b);
            }
            // Pixel shuffle (channel c * s * s + i * s + j is output pixel (i, j) of colour c) plus the input, enlarged.
            const int c3 = layers_.back().cout;  // 3 * s * s, rounded up to a group of channels
            for (int y = ty; y < ty + th; ++y)
                for (int x = tx; x < tx + tw; ++x) {
                    const float* net = a.data() + (static_cast<std::size_t>(y - y0 + 1) * stride + x - x0 + 1) * c3;
                    const uint32_t p = in.rgba[static_cast<std::size_t>(y) * in.w + x];
                    for (int i = 0; i < s; ++i)
                        for (int j = 0; j < s; ++j) {
                            uint32_t q = 0xFF000000u;
                            for (int c = 0; c < 3; ++c) {
                                const float v = net[c * s * s + i * s + j] + static_cast<float>((p >> (8 * c)) & 255) / 255.0f;
                                const int byte = static_cast<int>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
                                q |= static_cast<uint32_t>(byte) << (8 * c);
                            }
                            out.rgba[static_cast<std::size_t>(y * s + i) * out.w + x * s + j] = q;
                        }
                }
        }
    return out;
}

BatchUpscaler builtin_batch_upscaler(std::shared_ptr<const CompactUpscaler> model, unsigned threads, std::function<bool()> cancelled) {
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    return [model = std::move(model), threads, cancelled = std::move(cancelled)](const std::vector<Image>& in, std::vector<Image>& out,
                                                                                 std::string& error) {
        out.assign(in.size(), Image{});
        std::atomic<std::size_t> next{0};
        std::atomic<bool> stopped{false};
        auto work = [&] {
            for (std::size_t i; (i = next++) < in.size();) {
                if (stopped || (cancelled && cancelled())) {
                    stopped = true;
                    return;
                }
                out[i] = model->upscale(in[i]);
            }
        };
        std::vector<std::thread> pool;
        const unsigned n = static_cast<unsigned>(std::min<std::size_t>(threads, in.size()));
        for (unsigned t = 1; t < n; ++t) pool.emplace_back(work);
        work();
        for (std::thread& t : pool) t.join();
        if (stopped) {
            error = "Cancelled.";
            return false;
        }
        return true;
    };
}

}  // namespace dw3::texrep
