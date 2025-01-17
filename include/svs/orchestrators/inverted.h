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

#include "svs/index/inverted/memory_based.h"
#include "svs/orchestrators/manager.h"

namespace svs {

class InvertedInterface : public manager::ManagerInterface {
  public:
    using search_parameters_type = svs::index::inverted::InvertedSearchParameters;

    ///// Parameter Intefrace
    virtual search_parameters_type get_search_parameters() const = 0;
    virtual void set_search_parameters(const search_parameters_type& search_parameters) = 0;

    ///// Beckend Information Inteface
    virtual std::string experimental_backend_string() const = 0;

    ///// Saving
    virtual void save_primary_index(
        const std::filesystem::path& primary_config,
        const std::filesystem::path& primary_data,
        const std::filesystem::path& primary_graph
    ) = 0;
};

template <typename QueryType, typename Impl, typename IFace = InvertedInterface>
class InvertedImpl : public manager::ManagerImpl<QueryType, Impl, IFace> {
  private:
    // Null-terminated array of characters.
    static constexpr auto typename_impl = lib::generate_typename<Impl>();

  public:
    using base_type = manager::ManagerImpl<QueryType, Impl, IFace>;
    using base_type::impl;
    using search_parameters_type = typename IFace::search_parameters_type;

    explicit InvertedImpl(Impl impl)
        : base_type{std::move(impl)} {}

    ///// Parameter Interface
    [[nodiscard]] search_parameters_type get_search_parameters() const override {
        return impl().get_search_parameters();
    }

    void set_search_parameters(const search_parameters_type& search_parameters) override {
        impl().set_search_parameters(search_parameters);
    }

    ///// Backend Information Interface
    [[nodiscard]] std::string experimental_backend_string() const override {
        return std::string{typename_impl.begin(), typename_impl.end() - 1};
    }

    ///// Saving
    void save_primary_index(
        const std::filesystem::path& primary_config,
        const std::filesystem::path& primary_data,
        const std::filesystem::path& primary_graph
    ) override {
        impl().save_primary_index(primary_config, primary_data, primary_graph);
    }
};

/////
///// InvertedManager
/////

class Inverted : public manager::IndexManager<InvertedInterface, InvertedImpl> {
    // Type Alises
  public:
    using base_type = manager::IndexManager<InvertedInterface, InvertedImpl>;
    using search_parameters_type = typename InvertedInterface::search_parameters_type;

    // Constructors
  public:
    template <typename Impl>
    Inverted(std::unique_ptr<Impl> impl)
        : base_type{std::move(impl)} {}

    template <typename QueryType, typename Impl>
    Inverted(std::in_place_t, lib::Type<QueryType> SVS_UNUSED(type), Impl&& impl)
        : base_type{std::make_unique<InvertedImpl<QueryType, Impl>>(SVS_FWD(impl))} {}

    ///// Parameters
    search_parameters_type get_search_parameters() const {
        return impl_->get_search_parameters();
    }

    Inverted& set_search_parameters(const search_parameters_type& search_parameters) {
        impl_->set_search_parameters(search_parameters);
        return *this;
    }

    ///// Backend String
    std::string experimental_backend_string() const {
        return impl_->experimental_backend_string();
    }

    ///// Saving
    void save_primary_index(
        const std::filesystem::path& primary_config,
        const std::filesystem::path& primary_data,
        const std::filesystem::path& primary_graph
    ) {
        impl_->save_primary_index(primary_config, primary_data, primary_graph);
    }

    ///// Building
    template <
        typename QueryType,
        typename DataProto,
        typename Distance,
        typename ThreadpoolProto,
        index::inverted::StorageStrategy Strategy = index::inverted::SparseStrategy,
        typename CentroidPicker = svs::tag_t<index::inverted::pick_centroids_randomly>,
        typename ClusteringOp = svs::tag_t<index::inverted::no_clustering_post_op>>
    static Inverted build(
        const index::inverted::InvertedBuildParameters& build_parameters,
        DataProto data_proto,
        Distance distance,
        ThreadpoolProto threadpool_proto,
        Strategy strategy = {},
        CentroidPicker centroid_picker = {},
        ClusteringOp clustering_post_op = {}
    ) {
        // Forward the results of `auto_build`.
        return Inverted{
            std::in_place,
            lib::Type<QueryType>(),
            index::inverted::auto_build(
                build_parameters,
                std::move(data_proto),
                std::move(distance),
                std::move(threadpool_proto),
                std::move(strategy),
                std::move(centroid_picker),
                std::move(clustering_post_op)
            )};
    }

    ///// Assembling
    template <
        typename QueryType,
        typename DataProto,
        typename Distance,
        typename StorageStrategy = index::inverted::SparseStrategy>
    static Inverted assemble_from_clustering(
        const std::filesystem::path& clustering_path,
        DataProto data_proto,
        Distance distance,
        const std::filesystem::path& index_config,
        const std::filesystem::path& graph,
        size_t num_threads,
        StorageStrategy strategy = {}
    ) {
        return Inverted{
            std::in_place,
            lib::Type<QueryType>(),
            index::inverted::assemble_from_clustering(
                clustering_path,
                std::move(data_proto),
                std::move(distance),
                std::move(strategy),
                index_config,
                graph,
                num_threads
            )};
    }
};

} // namespace svs
