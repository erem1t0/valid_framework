/**
 * @file Logger.hpp
 * @brief Logger class for writing run logs in JSON format.
 */

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
    /// Time unit used by logger measurements.
    using time_unit_t = std::chrono::nanoseconds;

    /// Monotonic clock used for benchmark timing.
    using clock_t = std::chrono::steady_clock;

    /// @brief Error reported during validation.
    struct Error {
        std::size_t op_index;   ///< Operation index where the error was detected.
        std::string message;    ///< Human-readable error description.
    };
    
    /// @brief Chunk measurements.
    struct ChunkRecord {
        
        /// Index of the last operation included in this chunk.
        std::size_t op_index;
        
        /// Number of operations processed in this chunk.
        std::size_t ops_in_chunk;
        
        /// Total measured duration from the beginning of the run to the end of this chunk.
        time_unit_t cumulative;
        
        /// Duration of this chunk.
        time_unit_t chunk_duration;
        
        /// Memory usage reported at the end of this chunk, in bytes.
        std::size_t memory_bytes;
    };

    /**
     * @brief Collects run measurements and serializes them to a JSON log file.
     * 
     * Logger records metadata, per-chunk timing measurements, memory usage, and
     * validation errors.
     */
    class Logger {
    public:

        explicit Logger(const std::string& filename = "log.json")
            : filename_(filename)
        { }

        /**
         * @brief Starts a new logging session.
         *
         * Clears previously collected records and reserves storage for chunk records.
         *
         * @param[in] chunks Expected number of chunks used for reservation.
         */
        void start(std::size_t chunks) {
            errors_.clear();
            records_.clear();
            records_.reserve(chunks);
            op_count_       = 0;
            cumulative_     = time_unit_t::zero();
            paused_ = true;
        }

        /**
         * @brief Finishes the current logging session.
         *
         * If measurement is currently active, the current chunk is paused and recorded.
         */
        void finish() {
            if(!paused_) {
                pause();
            }
        }

        /**
         * @brief Stops timing the current chunk and records its measurements.
         *
         * @param[in] memory_bytes Memory usage to store for the completed chunk.
         */
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

        /// @brief Starts timing a new chunk if the logger is currently paused.
        void resume() {
            if(paused_) {
                chunk_start_ = clock_t::now();
                chunk_op_count_ = 0;
                paused_ = false;
            }
        }

        /// @brief Adds completed operations to the total and current chunk counters.
        /// @param count Count of completed ops.
        void add_count(std::size_t count) {
            op_count_        += count;
            chunk_op_count_  += count;
        }

        /// @brief Adds a metadata key-value pair to the log.
        /// @param key Metadata key.
        /// @param value Metadata value.
        void add_meta(const std::string& key, const std::string& value) {
            meta_.emplace_back(key, value);
        }

        /// @brief Records a validation error.
        /// @param index Index of operation where error occurred.
        /// @param message Message with information about error.
        void add_error(std::size_t index, const std::string& message) {
            errors_.emplace_back(index, message);
        }
        
        /**
         * @brief Serializes collected log data to JSON.
         *
         * The output contains metadata, time series records, errors, and summary
         * statistics.
         *
         * @return JSON representation of the current logger state.
         */
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

        /**
         * @brief Writes the current JSON log to the configured file.
         *
         * @return true if the file was opened and written successfully, false otherwise.
         */
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
        
        /**
         * @brief Escapes a string for JSON output.
         * 
         * @param[in] s Source string.
         * 
         * @return Escaped string suitable for JSON string values.
         */
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

        /// Name of log file.
        std::string filename_;

        /// Total completed operations count.
        std::size_t op_count_{0};
        /// Completed chunk operations count.
        std::size_t chunk_op_count_{0};
        /// Total duration from scenario begin to current time or end.
        time_unit_t cumulative_{time_unit_t::zero()};
        
        /// Variable to store time of chunk beginning.
        clock_t::time_point chunk_start_;
        /// Pause flag.
        bool paused_{true};

        /// Vector of metadata.
        std::vector<std::pair<std::string, std::string>> meta_;
        /// Vector of errors.
        std::vector<Error> errors_;
        /// Vector of chunks measurements.
        std::vector<ChunkRecord> records_;
    };


} // namespace valid_framework
