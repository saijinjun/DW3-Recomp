#pragma once

// A built-in upscaler for the pack generator: the compact Real-ESRGAN network (SRVGGNetCompact: 3x3 convolutions with
// PReLU, a pixel shuffle and the input added back, nearest-upscaled) on the CPU, read from the same ncnn .param/.bin files
// realesrgan-ncnn-vulkan uses. It is for platforms where that program can't run: phones, where an app may not start a
// downloaded program, and computers without a Vulkan GPU. tools/models/realesrgan_cpu.py is the reference it follows.

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dw3/texrep/generate.h"
#include "dw3/texrep/pack.h"

namespace dw3::texrep {

class CompactUpscaler {
public:
    // Reads <dir>/<name>.param and <dir>/<name>.bin. Other network layouts are refused.
    static std::unique_ptr<CompactUpscaler> load(const std::filesystem::path& dir, const std::string& name, std::string& error);

    int scale() const { return scale_; }
    // Returns `in` enlarged scale() times, alpha opaque. Runs tile by tile, each tile with 10 pixels of the image around
    // it (repeating the edge past the image's borders is the caller's business, as with the GPU program). Thread-safe.
    Image upscale(const Image& in, int tile = 128) const;

    struct Layer {
        int cin = 0, cout = 0;       // channels a pixel in the buffers; cout is a multiple of 16
        std::vector<float> weights;  // [cout / 16][3][3][cin][16]
        std::vector<float> bias;     // cout
        std::vector<float> slope;    // PReLU slope per output channel; empty for none
    };

private:
    std::vector<Layer> layers_;
    std::vector<int> real_cout_;  // the network's own output channel counts
    int scale_ = 0;
};

// A BatchUpscaler for generate_pack that runs `model` on `threads` threads (0: one per core). `cancelled`, when set, is
// polled between images; the batch then fails with the error "Cancelled.".
BatchUpscaler builtin_batch_upscaler(std::shared_ptr<const CompactUpscaler> model, unsigned threads,
                                     std::function<bool()> cancelled = {});

}  // namespace dw3::texrep
