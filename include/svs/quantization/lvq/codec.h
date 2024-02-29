/**
 *    Copyright (C) 2023-present, Intel Corporation
 *
 *    You can redistribute and/or modify this software under the terms of the
 *    GNU Affero General Public License version 3.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    version 3 along with this software. If not, see
 *    <https://www.gnu.org/licenses/agpl-3.0.en.html>.
 */

#pragma once

#include "svs/lib/misc.h"
#include "svs/quantization/lvq/compressed.h"
#include "svs/quantization/lvq/vectors.h"

#include <algorithm>
#include <numeric>
#include <tuple>
#include <type_traits>

namespace svs::quantization::lvq {

///
/// Perform the operation
/// ```
/// clamp(round(scale * value), min, max)
/// ```
///
template <typename T> T crunch(T scale, T value, T min, T max) {
    return std::clamp(std::round(scale * value), min, max);
}

template <size_t Bits, size_t Extent, LVQPackingStrategy Strategy = Sequential>
class MinRange : public CVStorage {
  public:
    using encoding_type = Encoding<Unsigned, Bits>;
    using return_type = ScaledBiasedVector<Bits, Extent, Strategy>;
    static constexpr float min_s = encoding_type::min();
    static constexpr float max_s = encoding_type::max();

    ///
    /// Construct a MinRange encoder that uses per-vector constants for its scaling
    /// parameters.
    ///
    MinRange(lib::MaybeStatic<Extent> size = {})
        : CVStorage{}
        , size_{size} {}

    ///
    /// Construct a MinRange encoder that uses pre-determined minimum and maximum values
    /// for its scaling parameters.
    ///
    MinRange(float min, float max, lib::MaybeStatic<Extent> size = {})
        : CVStorage{}
        , discover_extrema_{false}
        , min_{min}
        , max_{max}
        , size_{size} {}

    size_t size() const { return size_; }

    std::pair<float, float> extrema(lib::AnySpanLike auto data) {
        if (!discover_extrema_) {
            return std::make_pair(min_, max_);
        }

        float min{std::numeric_limits<float>::max()};
        float max{std::numeric_limits<float>::lowest()};
        for (size_t i = 0; i < data.size(); ++i) {
            float val = static_cast<float>(data[i]);
            min = std::min(min, val);
            max = std::max(max, val);
        }
        return std::make_pair(min, max);
    }

    // Compression Operator.
    return_type operator()(lib::AnySpanLike auto data, selector_t selector = 0) {
        // Find the maximum absolute value of the data to encode.
        auto [min, max] = extrema(data);

        float bias = min;
        float decompressor = 1;
        float compressor = 1;

        // Only compress if the division won't cause catastrophic overflow.
        float range = max - min;

        // Explanation: The minimum positive number representable as a non-subnormal float16
        // is 2^−14 ~= 6.10e-5. We chose an epsilon for "almost constant" vectors such that
        // the decompressor constant is not flushed to zero when converted to float16
        // for storage.
        constexpr float epsilon = 7e-5f * max_s;
        if (range > epsilon) {
            decompressor = range / max_s;
            compressor = max_s / range;
        }

        auto cv = view<Unsigned, Bits, Extent, Strategy>(size_);
        for (size_t i = 0; i < data.size(); ++i) {
            auto v = lib::narrow<float>(data[i]);
            auto compressed = crunch(compressor, v - bias, min_s, max_s);
            cv.set(compressed, i);
        }

        // Make sure we aren't truncating the decompressor to 0 after conversion to float16.
        assert(Float16(decompressor) != 0.0f);
        return return_type{decompressor, bias, selector, cv};
    }

  private:
    bool discover_extrema_ = true;
    float min_ = 0;
    float max_ = 0;
    [[no_unique_address]] lib::MaybeStatic<Extent> size_ = {};
};

template <size_t Residual> class ResidualEncoder : public CVStorage {
  public:
    using encoding_type = Encoding<Signed, Residual>;
    static constexpr float min_s = encoding_type::min();
    static constexpr float max_s = encoding_type::max();

    ResidualEncoder()
        : CVStorage{} {}

    // Compression Operator.
    template <LVQCompressedVector Primary>
    CompressedVector<Signed, Residual, Primary::extent, Sequential>
    operator()(const Primary& primary, lib::AnySpanLike auto data) {
        // Compute the scaling factor for the residual.
        float decompressor = primary.get_scale() / (std::pow(2, Residual) - 1);
        float compressor = 1.0f / decompressor;

        // Round the difference between the primary compression and the
        auto cv = view<Signed, Residual, Primary::extent, Sequential>(
            lib::MaybeStatic<Primary::extent>(primary.size())
        );
        for (size_t i = 0; i < primary.size(); ++i) {
            float difference = static_cast<float>(data[i]) - primary.get(i);
            auto v = crunch(compressor, difference, min_s, max_s);
            cv.set(v, i);
        }

        return cv;
    }
};

} // namespace svs::quantization::lvq
