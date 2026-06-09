#pragma once

#include <string>
#include <ostream>
#include <cstdint>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>

#include "Operations.hpp"
#include "Scenario.hpp"


namespace valid_framework {

    struct Error {
        std::size_t op_index;
        std::string message;
    };
    
    struct ChunkRecord {
        std::size_t op_index;
        std::size_t ops_in_chunk;
        std::chrono::nanoseconds cumulative; // need to sync with time_unit_t in Logger
        std::chrono::nanoseconds chunk_duration; // same
        std::size_t memory_bytes;
    };

    class Logger {
    public:
        using time_unit_t = std::chrono::nanoseconds;
        using clock_t = std::chrono::steady_clock;

        explicit Logger(const std::string& filename = "log.json")
            : filename_(filename)
        { }

        void start(std::size_t chunks) {
            errors_.clear();
            records_.clear();
            records_.reserve(chunks);
            op_count_       = 0;
            cumulative_     = time_unit_t::zero();
            paused_ = true;
        }

        void finish() {
            if(!paused_) {
                pause();
            }
        }

        void pause(std::size_t memory_bytes = 0) {
            if(!paused_) {

                time_unit_t chunk_duration = std::chrono::duration_cast<time_unit_t>(
                    clock_t::now() - chunk_start_);
                cumulative_ += chunk_duration;

                records_.push_back(ChunkRecord{
                    op_count_,
                    chunk_op_count_,
                    cumulative_,
                    chunk_duration,
                    memory_bytes,
                });

                paused_ = true; 
            }
        }

        void resume() {
            if(paused_) {
                chunk_start_ = clock_t::now();
                chunk_op_count_ = 0;
                paused_ = false;
            }
        }

        void add_count(std::size_t count) {
            op_count_        += count;
            chunk_op_count_  += count;
        }

        void add_meta(const std::string& key, const std::string& value) {
            meta_.emplace_back(key, value);
        }

        void add_error(std::size_t index, const std::string& message) {
            errors_.emplace_back(index, message);
        }
        
        std::string to_json() const {

            std::ostringstream ss;
            ss << std::fixed;

            ss << "{\n";

            ss << "\t\"metadata\": {\n";
            for(std::size_t i = 0; i < meta_.size(); ++i) {
                ss << "\t\t\"" << special_json(meta_[i].first) << "\": \"" 
                               << special_json(meta_[i].second) << "\"";

                if(i + 1 != meta_.size()) {
                    ss << ",";
                }
                ss << "\n";
            }

            ss << "\t},\n";

            ss << "\t\"timeseries\": [\n";

            // time_series
            for(std::size_t i = 0; i < records_.size(); ++i) {
                const auto& rec = records_[i];

                double cummulative_ms = std::chrono::duration<double, std::milli>(rec.cumulative).count();
                double chunk_ms = std::chrono::duration<double, std::milli>(rec.chunk_duration).count();
                double chunk_sec = std::chrono::duration<double>(rec.chunk_duration).count();

                double ops_per_sec = (chunk_sec > 1e-15) 
                    ? (static_cast<double>(rec.ops_in_chunk) / chunk_sec)
                    : 0.0; 

                ss << std::fixed << "\t\t{\"op\": " << rec.op_index 
                            << ", \"cumulative_ms\": " << std::setprecision(3) << cummulative_ms 
                            << ", \"chunk_ms\": " << std::setprecision(3) << chunk_ms
                            << ", \"ops_per_sec\": " << static_cast<uint64_t>(ops_per_sec)
                            << ", \"memory_bytes\": " << rec.memory_bytes
                            << "}";

                if(i + 1 != records_.size()) {
                    ss << ",";
                }
                ss << "\n";
            }

            ss << "\n\t],\n";

            ss << "\t\"errors\": [\n";
            for(std::size_t i = 0; i < errors_.size(); ++i) {
                ss << "\t\t{\"op\": " << errors_[i].op_index <<
                ", \"message\": \"" << special_json(errors_[i].message) << "\"}";

                if(i + 1 != errors_.size()) {
                    ss << ",";
                }
                ss << "\n";
            }
            ss << "\t],\n";

            double total_ms = std::chrono::duration<double, std::milli>(cumulative_).count();
            double total_sec = std::chrono::duration<double>(cumulative_).count();
            double avg = (total_sec > 1e-15) 
                ? (static_cast<double>(op_count_) / total_sec) 
                : 0.0;

            ss  << "\t\"summary\": {\n" 
                << "\t\t\"total_ops\": " << op_count_ << ",\n"
                << "\t\t\"total_time_ms\": " << std::setprecision(3) << total_ms << ",\n"
                << "\t\t\"avg_ops_per_sec\": " << static_cast<uint64_t>(avg) << ",\n"
                << "\t\t\"failed_ops\": " << errors_.size() << "\n"
                << "\t}\n";

            ss << "}\n";

            return ss.str();
        }


        bool write() const {
            std::ofstream fout(filename_);
            if(!fout.is_open()) {
                return false;
            }

            fout << to_json();
            fout.close();
            return true;
        }

    private:

        static std::string special_json(const std::string& s) {
            std::string out;
            out.reserve(s.size() + 10);

            for(const auto& c : s) {
                switch(c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c;
                }
            }
            return out;
        }

        std::string filename_;

        std::size_t op_count_{0};
        std::size_t chunk_op_count_{0};
        time_unit_t cumulative_{time_unit_t::zero()};
        
        clock_t::time_point chunk_start_;
        bool paused_{true};

        std::vector<std::pair<std::string, std::string>> meta_;
        std::vector<Error> errors_;
        std::vector<ChunkRecord> records_;
    };


} // namespace valid_framework
