#include "bin_path_info.hpp"

namespace odgi {
namespace algorithms {

std::string BinSerializer::get_path_prefix(const std::string& path_name) {
    if (aggregate_delim || path_delim.empty()) {
        return "NA";
    } else {
        return path_name.substr(0, path_name.find(path_delim));
    }
}

std::string BinSerializer::get_path_suffix(const std::string& path_name) {
    if (aggregate_delim || path_delim.empty()) {
        return "NA";
    } else {
        return path_name.substr(path_name.find(path_delim)+1);
    }
}

BinSerializer::BinSerializer(const std::string& path_delim, bool aggregate_delim) :
    path_delim(path_delim), aggregate_delim(aggregate_delim)
{
}

BinSerializer::~BinSerializer() {}


void bin_path_info(const PathHandleGraph& graph,
                   const std::string& prefix_delimiter,
                   std::shared_ptr<BinSerializer>& serializer,
                   const std::function<void(const string&)>& handle_fasta,
                   uint64_t num_bins,
                   uint64_t bin_width,
                   bool drop_gap_links) {
    // the graph must be compacted for this to work
    std::vector<uint64_t> position_map(graph.get_node_count()+1);
    uint64_t len = 0;
    std::string graph_seq;
    graph.for_each_handle([&](const handle_t& h) {
            position_map[number_bool_packing::unpack_number(h)] = len;
            uint64_t hl = graph.get_length(h);
            graph_seq.append(graph.get_sequence(h));
            len += hl;
        });
    if (!num_bins) {
        num_bins = len / bin_width + (len % bin_width ? 1 : 0);
    } else if (!bin_width) {
        bin_width = len / num_bins;
        num_bins = len / bin_width + (len % bin_width ? 1 : 0);
    }
    position_map[position_map.size()-1] = len;
    // write header
    serializer->write_header(len, bin_width);
    // collect bin sequences
    for (uint64_t i = 0; i < num_bins; ++i) {
        serializer->write_seq(i+1, graph_seq.substr(i*bin_width, bin_width));
    }
    // write out pangenome sequence if wished so
    handle_fasta(graph_seq);
    graph_seq.clear(); // clean up
    std::unordered_map<path_handle_t, uint64_t> path_length;
    uint64_t gap_links_removed = 0;
    graph.for_each_path_handle([&](const path_handle_t& path) {
            std::vector<std::pair<uint64_t, uint64_t>> links;
            std::map<uint64_t, path_info_t> bins;
            // walk the path and aggregate
            uint64_t path_pos = 0;
            int64_t last_bin = 0; // flag meaning "null bin"
            uint64_t last_pos_in_bin = 0;
            uint64_t nucleotide_count = 0;
            bool last_is_rev = false;
            graph.for_each_step_in_path(path, [&](const step_handle_t& occ) {
                    handle_t h = graph.get_handle_of_step(occ);
                    bool is_rev = graph.get_is_reverse(h);
                    uint64_t p = position_map[number_bool_packing::unpack_number(h)];
                    uint64_t hl = graph.get_length(h);
                    // detect bin crossings
                    // make contects for the bases in the node
                    for (uint64_t k = 0; k < hl; ++k) {
                        int64_t curr_bin = (p+k) / bin_width + 1;
                        uint64_t curr_pos_in_bin = (p+k) - (curr_bin * bin_width);
                        if (curr_bin != last_bin && std::abs(curr_bin-last_bin) > 1 || last_bin == 0) {
                            // bin cross!
                            links.push_back(std::make_pair(last_bin,curr_bin));
                        }
                        ++bins[curr_bin].mean_cov;
                        if (is_rev) {
                            ++bins[curr_bin].mean_inv;
                        }
                        bins[curr_bin].mean_pos += path_pos++;
                        nucleotide_count += 1;
                        if(bins[curr_bin].first_nucleotide == 0){
                            bins[curr_bin].first_nucleotide = nucleotide_count;
                        }
                        bins[curr_bin].last_nucleotide = nucleotide_count;
                        last_bin = curr_bin;
                        last_is_rev = is_rev;
                        last_pos_in_bin = curr_pos_in_bin;
                    }
                });
            links.push_back(std::make_pair(last_bin,0));
            uint64_t path_length = path_pos;
            uint64_t end_nucleotide = nucleotide_count;
            for (auto& entry : bins) {
                auto& v = entry.second;
                v.mean_inv /= (v.mean_cov ? v.mean_cov : 1);
                v.mean_cov /= bin_width;
                v.mean_pos /= bin_width * path_length * v.mean_cov;
            }

            if (drop_gap_links) {
                std::vector<uint64_t> bin_ids;
                for (const auto &entry: bins) {
                    bin_ids.push_back(entry.first);
                }
                std::sort(bin_ids.begin(), bin_ids.end());

                uint64_t fill_pos = 0;

                for (uint64_t i = 0; i < links.size(); ++i) {
                    auto link = links[i];

                    if (link.first == 0 || link.second == 0)
                        continue;

                    if (link.first > link.second) {
                        links[fill_pos++] = link;
                        continue;
                    }

                    auto left_it = std::lower_bound(bin_ids.begin(), bin_ids.end(), link.first + 1);
                    auto right_it = std::lower_bound(bin_ids.begin(), bin_ids.end(), link.second);
                    if (right_it > left_it) {
                        links[fill_pos++] = link;
                    }
                }

                gap_links_removed += links.size() - fill_pos;
                links.resize(fill_pos);
            }

            serializer->write_path(graph.get_path_name(path), links, bins);
        });

    if (drop_gap_links) {
        std::cerr << "Gap links removed: " << gap_links_removed << std::endl;
    }
}

}
}
