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

// svs
#include "svs/concepts/data.h"
#include "svs/core/io.h"

#include "svs/lib/array.h"
#include "svs/lib/exception.h"
#include "svs/lib/memory.h"
#include "svs/lib/meta.h"
#include "svs/lib/misc.h"

namespace svs::io {

// Dispatch tags to control the loading and saving pipeline.
constexpr lib::PriorityTag<2> default_populate_tag = lib::PriorityTag<2>();

// Some specialized datasets (namely LVQ) may need to perform aritrary computation
// or rearrangement of the saved data prior to committing it to the dataset.
//
// These datasets are expected to provide their own custom accessor in this case.
//
// The class defined below is meant for the case when we are loading simple, uncompressed
// data.
struct DefaultWriteAccessor {
    template <data::MemoryDataset Data, typename File>
    typename File::template reader_type<typename Data::element_type>
    reader(const Data& SVS_UNUSED(data), const File& file) const {
        using T = typename Data::element_type;
        return file.reader(lib::Type<T>());
    }

    template <data::MemoryDataset Data, lib::AnySpanLike Span>
    void set(Data& data, size_t i, Span span) const {
        data.set_datum(i, span);
    }
};

// Similar to writing TO a dataset, when reading FROM a dataset, we also provide an option
// for injecting an arbitrary accessor to transform data.
struct DefaultReadAccessor {
    template <data::ImmutableMemoryDataset Data>
    size_t serialized_dimensions(const Data& data) const {
        return data.dimensions();
    }

    template <data::ImmutableMemoryDataset Data>
    typename Data::const_value_type get(const Data& data, size_t i) const {
        return data.get_datum(i);
    }
};

// TODO (long term): Constrain this to vector-based datasets?
template <data::MemoryDataset Data, typename WriteAccessor, typename File>
void populate_impl(
    Data& data, WriteAccessor& accessor, const File& file, lib::PriorityTag<0> /*tag*/
) {
    auto reader = accessor.reader(data, file);
    size_t i = 0;
    for (auto v : reader) {
        accessor.set(data, i, v);
        ++i;
    }
}

// Intercept the native file to perform dispatch on the actual file type.
template <data::MemoryDataset Data, typename WriteAccessor>
void populate_impl(
    Data& data, WriteAccessor& accessor, const NativeFile& file, lib::PriorityTag<1> tag
) {
    file.resolve([&](const auto& resolved_file) {
        populate_impl(data, accessor, resolved_file, decltype(tag)::next());
    });
}

///
/// @brief Populate the entries of `data` with the contents of `file`.
///
template <data::MemoryDataset Data, typename WriteAccessor, typename File>
void populate(Data& data, WriteAccessor& accessor, const File& file) {
    populate_impl(data, accessor, file, default_populate_tag);
}

/////
///// Saving
/////

template <data::ImmutableMemoryDataset Dataset, typename ReadAccessor, typename File>
void save(
    const Dataset& data,
    ReadAccessor& accessor,
    const File& file,
    const lib::UUID& uuid = lib::ZeroUUID
) {
    auto writer = file.writer(accessor.serialized_dimensions(data), uuid);
    for (size_t i = 0; i < data.size(); ++i) {
        writer << accessor.get(data, i);
    }
}

template <data::ImmutableMemoryDataset Dataset, typename File>
void save(const Dataset& data, const File& file, const lib::UUID& uuid = lib::ZeroUUID) {
    auto accessor = DefaultReadAccessor();
    return save(data, accessor, file, uuid);
}

///
/// @brief Save the dataset as a "*vecs" file.
///
/// @param data The dataset to save.
/// @param path The file path to where the data will be saved.
///
template <data::ImmutableMemoryDataset Dataset>
void save_vecs(const Dataset& data, const std::filesystem::path& path) {
    auto file = vecs::VecsFile{path};
    auto writer = file.writer(data.dimensionsions());
    for (size_t i = 0; i < data.size(); ++i) {
        writer << data.get_datum(i);
    }
}

/////
///// Dataset Loading
/////

// Generic dataset loading.
template <typename File, typename WriteAccessor, lib::LazyInvocable<size_t, size_t> F>
lib::lazy_result_t<F, size_t, size_t>
load_impl(const File& file, WriteAccessor& accessor, const F& lazy) {
    auto [vectors_to_read, ndims] = file.get_dims();
    auto data = lazy(vectors_to_read, ndims);
    populate(data, accessor, file);
    return data;
}

namespace detail {
// Promote untyped string-like arguments to a `NativeFile`.
template <typename T> const T& to_native(const T& x) { return x; }
inline NativeFile to_native(const std::string& x) { return NativeFile(x); }
inline NativeFile to_native(const std::string_view& x) { return to_native(std::string(x)); }
inline NativeFile to_native(const std::filesystem::path& x) { return NativeFile(x); }
} // namespace detail

template <typename File, typename WriteAccessor, lib::LazyInvocable<size_t, size_t> F>
lib::lazy_result_t<F, size_t, size_t>
load_dataset(const File& file, WriteAccessor& accessor, const F& lazy) {
    return load_impl(detail::to_native(file), accessor, lazy);
}

template <typename File, lib::LazyInvocable<size_t, size_t> F>
lib::lazy_result_t<F, size_t, size_t> load_dataset(const File& file, const F& lazy) {
    auto default_accessor = DefaultWriteAccessor();
    return load_impl(detail::to_native(file), default_accessor, lazy);
}

///
/// @brief Load a dataset from file. Automcatically detect the file type based on extension.
///
/// @tparam T The element type of the vector components in the file.
/// @tparam F A ``svs::lib::Lazy`` callable to construct the destination data type.
///
/// @param filename The path to the file on disk.
/// @param construct The deferred constructor for the dataset.
///
/// The lazy callable `F` must take two `size_t` arguments: (1) the number of elements in
/// the dataset and (2) the number of dimensions for each vector and return an allocated
/// dataset capable of holding a dataset with those dimensions.
///
/// Recognized file extentions:
/// * .svs: The native file format for this library.
/// * .vecs: The usual [f/b/i]vecs form.
/// * .bin: Files generated by DiskANN.
///
template <typename T, lib::LazyInvocable<size_t, size_t> F>
lib::lazy_result_t<F, size_t, size_t>
auto_load(const std::string& filename, const F& construct) {
    if (filename.ends_with("svs")) {
        return load_dataset(io::NativeFile(filename), construct);
    }
    if (filename.ends_with("vecs")) {
        return load_dataset(io::vecs::VecsFile<T>(filename), construct);
    }
    if (filename.ends_with("bin")) {
        return load_dataset(io::binary::BinaryFile(filename), construct);
    }
    throw ANNEXCEPTION("Unknown file extension for input file: {}.", filename);
}

} // namespace svs::io
