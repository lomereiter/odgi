#include "subcommand.hpp"
#include "odgi.hpp"
#include "args.hxx"
#include "algorithms/bin_path_info.hpp"

#include <regex>

namespace odgi {

using namespace odgi::subcommand;

struct TsvSerializer : public algorithms::BinSerializer {
    TsvSerializer(const std::string& path_delim, bool aggregate_delim) :
        algorithms::BinSerializer(path_delim, aggregate_delim)
    {}

    void write_header(const uint64_t pangenome_length, const uint64_t bin_width) override {
        std::cout << "path.name" << "\t"
                  << "path.prefix" << "\t"
                  << "path.suffix" << "\t"
                  << "bin" << "\t"
                  << "mean.cov" << "\t"
                  << "mean.inv" << "\t"
                  << "mean.pos" << "\t"
                  << "first.nucl" << "\t"
                  << "last.nucl" << std::endl;
    }

    void write_seq(const uint64_t& bin_id, const std::string& seq) override {}

    void write_path(const std::string& path_name, const link_vec_t& links, const bin_map_t &bins) override {
        std::string name_prefix = this->get_path_prefix(path_name);
        std::string name_suffix = this->get_path_suffix(path_name);
        for (auto& entry : bins) {
            auto& bin_id = entry.first;
            auto& info = entry.second;
            if (info.mean_cov) {
                std::cout << path_name << "\t"
                          << name_prefix << "\t"
                          << name_suffix << "\t"
                          << bin_id << "\t"
                          << info.mean_cov << "\t"
                          << info.mean_inv << "\t"
                          << info.mean_pos << "\t"
                          << info.first_nucleotide << "\t"
                          << info.last_nucleotide << std::endl;
            }
        }
    }
};

struct JsonSerializer : public algorithms::BinSerializer {
    static const uint64_t ODGI_JSON_VERSION = 10;

    bool write_seqs;

    JsonSerializer(const std::string& path_delim, bool aggregate_delim, bool write_seqs) :
        algorithms::BinSerializer(path_delim, aggregate_delim),
        write_seqs(write_seqs)
    {}

    void write_header(const uint64_t pangenome_length, const uint64_t bin_width) override {
        std::cout << "{\"odgi_version\": " << ODGI_JSON_VERSION << ",";
        std::cout << "\"bin_width\": " << bin_width << ",";
        std::cout << "\"pangenome_length\": " << pangenome_length << "}" << std::endl;
    };

    void write_seq(const uint64_t& bin_id, const std::string& seq) override {
        if (!this->write_seqs) {
            std::cout << "{\"bin_id\":" << bin_id << "}" << std::endl;
        } else {
            std::cout << "{\"bin_id\":" << bin_id << ","
                      << "\"sequence\":\"" << seq << "\"}" << std::endl;
        }
    }

    void write_path(const std::string& path_name, const link_vec_t& links, const bin_map_t& bins) override {
        std::string name_prefix = this->get_path_prefix(path_name);
        std::string name_suffix = this->get_path_suffix(path_name);
        std::cout << "{\"path_name\":\"" << path_name << "\",";
        if (!this->path_delim.empty()) {
            std::cout << "\"path_name_prefix\":\"" << name_prefix << "\","
                      << "\"path_name_suffix\":\"" << name_suffix << "\",";
        }
        std::cout << "\"bins\":[";
        auto entry_it = bins.begin();
        for (uint64_t i = 0; i < bins.size(); ++i) {
            auto& bin_id = entry_it->first;
            auto& info = entry_it->second;
            std::cout << "[" << bin_id << ","
                      << info.mean_cov << ","
                      << info.mean_inv << ","
                      << info.mean_pos << ","
                      << info.first_nucleotide << ","
                      << info.last_nucleotide << "]";
            if (i+1 != bins.size()) {
                std::cout << ",";
            }
            ++entry_it;
        }
        std::cout << "],";
        std::cout << "\"links\":[";
        for (uint64_t i = 0; i < links.size(); ++i) {
            auto& link = links[i];
            std::cout << "[" << link.first << "," << link.second << "]";
            if (i+1 < links.size()) std::cout << ",";
        }
        std::cout << "]}" << std::endl;
    }
};

int main_bin(int argc, char** argv) {

    for (uint64_t i = 1; i < argc-1; ++i) {
        argv[i] = argv[i+1];
    }
    std::string prog_name = "odgi bin";
    argv[0] = (char*)prog_name.c_str();
    --argc;

    args::ArgumentParser parser("binning of path information in the graph");
    args::HelpFlag help(parser, "help", "display this help summary", {'h', "help"});
    args::ValueFlag<std::string> dg_out_file(parser, "FILE", "store the graph in this file", {'o', "out"});
    args::ValueFlag<std::string> dg_in_file(parser, "FILE", "load the graph from this file", {'i', "idx"});
    args::ValueFlag<std::string> fa_out_file(parser, "FILE", "store the pangenome sequence in FASTA format in this file", {'f', "fasta"});
    args::ValueFlag<std::string> path_delim(parser, "path-delim", "annotate rows by prefix and suffix of this delimiter", {'D', "path-delim"});
    args::Flag output_json(parser, "write-json", "write JSON format output including additional path positional information", {'j', "json"});
    args::Flag aggregate_delim(parser, "aggregate-delim", "aggregate on path prefix delimiter", {'a', "aggregate-delim"});
    args::ValueFlag<uint64_t> num_bins(parser, "N", "number of bins", {'n', "num-bins"});
    args::ValueFlag<uint64_t> bin_width(parser, "bp", "width of each bin in basepairs along the graph vector", {'w', "bin-width"});
    args::Flag write_seqs_not(parser, "write-seqs-not", "don't write out the sequences for each bin", {'s', "no-seqs"});
    args::Flag drop_gap_links(parser, "drop-gap-links", "don't include gap links in the output", {'g', "no-gap-links"});
    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    if (argc==1) {
        std::cout << parser;
        return 1;
    }

    graph_t graph;
    assert(argc > 0);
    std::string infile = args::get(dg_in_file);
    if (infile.size()) {
        if (infile == "-") {
            graph.deserialize(std::cin);
        } else {
            ifstream f(infile.c_str());
            graph.deserialize(f);
            f.close();
        }
    }

    if (args::get(num_bins) + args::get(bin_width) == 0) {
        std::cerr << "[odgi bin] error: a bin width or a bin count is required" << std::endl;
        return 1;
    }

    auto write_fasta = [&](const std::string& nuc_seq) {
        if (fa_out_file) {
            std::ofstream out(args::get(fa_out_file));
            std::string fa_out_name = args::get(fa_out_file).c_str();
            std::regex regex("/");
            auto token_it = std::sregex_token_iterator(fa_out_name.begin(), fa_out_name.end(), regex, -1);
            std::vector<std::string> splitted(token_it, std::sregex_token_iterator());
            fa_out_name = splitted[splitted.size() - 1];
            // Write header
            out << ">" << fa_out_name << std::endl;
            // Write the actual sequences, 80 nucleotides per line
            for (unsigned i = 0; i < nuc_seq.length(); i += 80) {
                std:: string sub_nuc_seq = nuc_seq.substr(i, 80);
                out << sub_nuc_seq << std::endl;
            }
        }
    };

    std::string delim = args::get(path_delim);
    bool agg_delim = args::get(aggregate_delim);

    std::shared_ptr<algorithms::BinSerializer> serializer;
    bool skip_seqs = args::get(write_seqs_not) || fa_out_file;
    if (args::get(output_json)) {
        serializer = std::make_shared<JsonSerializer>(delim, agg_delim, !skip_seqs);
    } else {
        serializer = std::make_shared<TsvSerializer>(delim, agg_delim);
    }

    algorithms::bin_path_info(graph, (args::get(aggregate_delim) ? args::get(path_delim) : ""),
                              serializer, write_fasta,
                              args::get(num_bins), args::get(bin_width), args::get(drop_gap_links));
    return 0;
}

static Subcommand odgi_bin("bin", "bin path information across the graph",
                              PIPELINE, 3, main_bin);


}
