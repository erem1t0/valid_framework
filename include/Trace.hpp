/**
 * @file Trace.hpp
 * @brief 
 */

#pragma once

#include <cstddef>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <functional>

#include "Scenario.hpp"
#include "Operations.hpp"
#include "OperationApplier.hpp"

namespace valid_framework {

    /**
     * @brief Enumeration of trace operation types.
     */
    enum class TraceOpType {
        Insert, ///< InsertOp operation in trace representation.
        Get,    ///< GetOp operation in trace representation.
        Erase,  ///< EraseOp operation in trace representation.
        Custom, ///< CustomOp operation in trace representation.
    };

    /**
     *  @brief Returns the textual name of a trace operation type.
     * 
     * @param[in] type Trace operation type.
     * @return String literal: "insert", "get", "erase", "custom" or "unknown".
     */
     inline constexpr const char* trace_op_type_to_string(TraceOpType type) {
        switch (type) {
            case TraceOpType::Insert:   return "insert";
            case TraceOpType::Get:      return "get";
            case TraceOpType::Erase:    return "erase";
            case TraceOpType::Custom:   return "custom";
            default: return "unknown";
        }
    }

    /**
     * @brief Serializable representation of a framework operation.
     * 
     * TraceOp stores an operation together with its position in a trace file.
     * Only fields required by the selected @c type are expected to contain values.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    struct TraceOp {

        std::size_t index{};    ///< Index of tracing operation.
        TraceOpType type;       ///< Type of tracing operation.

        std::optional<Key> key;     ///< Key argument used by insert, get and erase operations.
        std::optional<Value> value; ///< Value argument used by insert and custom operations.

        // Custom
        std::optional<uint32_t> custom_id;   ///< Custom operation identifier.
        std::optional<Key> key1;             ///< First key argument of a custom operation.
        std::optional<Key> key2;             ///< Second key argument of a custom operation.
        std::optional<std::size_t> size_val; ///< Positional or size-like argument of custom operation.

        /**
         * @brief Converts this trace operation to a framework operation.
         * 
         * @return Operation recostructed from the stored trace fields. 
         * 
         * @warning The required optional fields must be present for the current
         * operation type. Calling this function on an incomplete TraceOp is undefined
         * behavior at the framework level.
         * 
         * @throws @c std::runtime_error if @c type is unknown.
         */
        Operation<Key, Value> to_operation() const {
            switch(type) {
                case TraceOpType::Insert: {
                    return InsertOp<Key, Value>{*key, *value};
                }
                case TraceOpType::Get: {
                    return GetOp<Key>{*key};
                }
                case TraceOpType::Erase: {
                    return EraseOp<Key>{*key}; 
                }
                case TraceOpType::Custom: {
                    return CustomOp<Key, Value>{*custom_id, *key1, *key2, *value, *size_val};
                }
                default: throw std::runtime_error("Unknown TraceOpType");
            }
        }
    };

    /** 
     * @brief Converts a framework operation to its trace representation.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param[in] index Operation index in the trace.
     * @param[in] op Operation to convert.
     * 
     * @return Trace operation containing the operation type and serialized arguments.
     */
    template<typename Key, typename Value>
    TraceOp<Key, Value> to_trace_op(std::size_t index, const Operation<Key, Value>& op) {
        TraceOp<Key, Value> res;
        res.index = index;

        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;

                if constexpr (std::is_same_v<T, InsertOp<Key, Value>>) {
                    res.type = TraceOpType::Insert;
                    res.key = x.key;
                    res.value = x.value;
                } else if constexpr (std::is_same_v<T, GetOp<Key>>) {
                    res.type = TraceOpType::Get;
                    res.key = x.key;
                } else if constexpr (std::is_same_v<T, EraseOp<Key>>) {
                    res.type = TraceOpType::Erase;
                    res.key = x.key;
                } else if constexpr (std::is_same_v<T, CustomOp<Key, Value>>) {
                    res.type = TraceOpType::Custom;
                    res.custom_id = x.id;
                    res.key1 = x.key1;
                    res.key2 = x.key2;
                    res.value = x.value;
                    res.size_val = x.size_val;
                } else {
                    static_assert(sizeof(T) == 0, "Unknwon operation type");
                }
            },  op);
        return res;
    }

    /**
     * @brief Writes tracing operation to stream.
     * 
     * @tparam Key Operation key type. 
     * @tparam Value Operation value type.
     * 
     * @param out Output stream for writing.
     * @param op Target tracing operation object.
     */
     template<typename Key, typename Value>
    void write_trace_op(std::ostream& out, const TraceOp<Key, Value>& op) {

        out << op.index << ' ' << trace_op_type_to_string(op.type);

        switch(op.type) {
            case TraceOpType::Insert: {
                out << ' ' << *op.key << ' ' << *op.value;
                break;
            }
            case TraceOpType::Get: {
                out << ' ' << *op.key;
                break;
            }
            case TraceOpType::Erase: {
                out << ' ' << *op.key;
                break;
            }
            case TraceOpType::Custom: {
                out << ' ' << *op.custom_id << ' ' << *op.key1 << ' ' << *op.key2 << ' '
                << *op.value << ' ' << *op.size_val;
                break;
            }
        }
    }

    /**
     * @brief Writes operation to stream.
     * 
     * @tparam Key Operation key type. 
     * @tparam Value Operation value type.
     * 
     * @param out Output stream for writing.
     * @param index Index of target operation.
     * @param op Target operation object.
     */
     template<typename Key, typename Value>
    void write_op(std::ostream& out, std::size_t index, const Operation<Key, Value>& op) {
        write_trace_op<Key, Value>(out, to_trace_op(index, op));
        out << '\n';
    }

    namespace detail {

        /// @brief Helpful method for writing some information to stream.
        /// @param out Output stream for writing.
        /// @param text Target string object for writing.
        inline void write_text(std::ostream& out, const std::string& text) {
            std::istringstream in(text);
            std::string line;

            while(std::getline(in, line)) {
                out << "# " << line << '\n';
            }
        }

    } // namespace detail

    /**
     * @brief Writes the trace file header.
     * 
     * The header contains the seed, operation limit and scenario metadata.
     * Header lines are written as comments and are ignored by trace parsers.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param out Output stream for writing.
     * @param seed Seed of the scenario.
     * @param[in] max_ops Maximum number of operations to dump, or zero to dump all available operations.
     * @param scenario Scenario object.
     */
    template<typename Key, typename Value>
    void write_header(std::ostream& out, uint64_t seed, std::size_t max_ops, 
                        const AbstractScenario<Key, Value>& scenario) {
        
        out << "# seed " << seed << '\n';

        if(max_ops == 0) {
            out << "# max_ops all\n";
        } else {
            out << "# max_ops " << max_ops << '\n';
        }

        out << "\n";

        out << "# scenario:\n";
        detail::write_text(out, scenario.to_string());
        out << "\n";
    }

    /**
     * @brief Dumps operations produced by a scenario to a trace file.
     * 
     * Resets @p scenario with a @p seed, writes a trace header and serializes
     * generated operations until the scenario is exhausted or @p max_ops is reached.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param scenario Scenario used to generate operations.
     * @param seed Seed used to reset the scenario.
     * @param max_ops Maximum number of operations to dump, or zero to dump all operations.
     * @param filename Output trace file path.
     * 
     * @return Number of operations written to the trace file.
     * 
     * @throws @c std::runtime_error if the output file cannot be opened.
     */
    template<typename Key, typename Value>
    std::size_t dump_trace(AbstractScenario<Key, Value>& scenario, uint64_t seed, std::size_t max_ops,
    const std::string& filename) {

        std::ofstream fout(filename);

        if(!fout.is_open()) {
            throw std::runtime_error("Cannot open trace file: " + filename);
        }

        write_header<Key, Value>(fout, seed, max_ops, scenario);
        scenario.reset(seed);

        Operation<Key, Value> op;
        std::size_t index = 0;

        while((max_ops == 0 || index < max_ops) && scenario.next(op)) {
            ++index;
            write_op(fout, index, op);
        }    
        
        return index;
    }


//////////// PARSING ////////////


    /**
     * @brief Parses a textual trace operation type.
     * 
     * @param[in] name Operation name: "insert", "get", "erase", or "custom". 
     * 
     * @return Parsed trace operation type.
     * 
     * @throws @c std::runtime_error if the @c name is incorrect.
     */
     inline TraceOpType parse_trace_op_type(const std::string& name) {
        if(name == "insert") {
            return TraceOpType::Insert;
        } else if(name == "get") {
            return TraceOpType::Get;
        } else if(name == "erase") {
            return TraceOpType::Erase;
        } else if(name == "custom") {
            return TraceOpType::Custom;
        } else {
            throw std::runtime_error("Unknown trace operation");
        }
    }

    namespace detail {

        /**
         * @brief Removes the comment suffix from a trace lines.
         * 
         * Everything starting from the first @c # character is removed.
         * 
         * @param[in] line Target line to strip.
         * 
         * @return New stripped line.
         */
         inline std::string strip_line(std::string line) {
            const auto pos = line.find("#");

            if(pos != std::string::npos) {
                line.erase(pos);
            }
            return line;
        }

        /**
         * @brief Removes leading and trailing whitespaces.
         * 
         * @param[in] line Target line to trim.
         * 
         * @return New trimmed line.
         */
         inline std::string trim(std::string line) {
            auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
            
            while(!line.empty() && is_space(static_cast<unsigned char>(line.front()))) {
                line.erase(line.begin());
            }

            while(!line.empty() && is_space(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
            
            return line;
        }

        /** 
         * @brief Reads a typed token from an input stream.
         * 
         * @tparam T Token type.
         * 
         * @param[in,out] in Input stream.
         * @param[in] line_num Source line number used in diagnostics.
         * @param[in] field_name Field name used in diagnostics.
         * 
         * @return Parsed token value.
         * 
         * @throws @c std::runtime_error if the token cannot be read as @p T.
         */ 
        template<typename T>
        T read_token(std::istringstream& in, std::size_t line_num, const char* field_name) {
            T value{};

            if(!(in >> value)) {
                throw std::runtime_error("Cannot parse field '" + std::string(field_name) 
                                        + "' at line " + std::to_string(line_num));
            }

            return value;
        }

        /**
         * @brief Check input stream for extra tokens.
         * 
         * @param[in,out] in Input stream.
         * @param line_num Source line number used in diagnostics.
         */
         inline void check_extra(std::istringstream& in, std::size_t line_num) {
            std::string extra;

            if(in >> extra) {
                throw std::runtime_error("Unexpected token: '" + extra 
                                        + "' at line " + std::to_string(line_num));
            }
        }

    } // namespace detail

    /**
     * @brief Parses one operation line from a trace file
     * 
     * Expected formats:
     * - @c "<index> insert <key> <value>"
     * 
     * - @c "<index> get <key>"
     * 
     * - @c "<index> erase <key>"
     * 
     * - @c "<index> custom <id> <key1> <key2> <value> <size_val>"
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param[in] line Source line for parsing.
     * @param[in] line_num Index of source line for error handling.
     * 
     * @return Parsed trace opeartion.
     * 
     * @throws @c std::runtime_error if the line has invalid syntax.
     */
    template<typename Key, typename Value>
    TraceOp<Key, Value> parse_trace_line(const std::string& line, std::size_t line_num) {
        std::istringstream in(line);

        TraceOp<Key, Value> res;

        res.index = detail::read_token<std::size_t>(in, line_num, "index");
        const std::string op_name = detail::read_token<std::string>(in, line_num, "op");
        res.type = parse_trace_op_type(op_name);

        switch(res.type) {
            case TraceOpType::Insert: {
                res.key = detail::read_token<Key>(in, line_num, "key");
                res.value = detail::read_token<Value>(in, line_num, "value");
                break;
            }
            case TraceOpType::Get: {
                res.key = detail::read_token<Key>(in, line_num, "key");
                break;
            }
            case TraceOpType::Erase: {
                res.key = detail::read_token<Key>(in, line_num, "key");
                break;
            }
            case TraceOpType::Custom: {
                res.custom_id = detail::read_token<uint32_t>(in, line_num, "id");
                res.key1 = detail::read_token<Key>(in, line_num, "key1");
                res.key2 = detail::read_token<Key>(in, line_num, "key2");
                res.value = detail::read_token<Value>(in, line_num, "value");
                res.size_val = detail::read_token<std::size_t>(in, line_num, "size_val");
                break;
            }
        }

        detail::check_extra(in, line_num);

        return res;
    }

    /**
     * @brief Loads trace operations from a file.
     * 
     * Empty lines and comments are ignored. Each remaining line is parsed
     * as a trace operation.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param[in] filename Input trace file path.
     * 
     * @return Parsed trace operations in file order.
     * 
     * @throws @c std::runtime_error if the file cannot be opened or a line cannot be parsed.
     */
    template<typename Key, typename Value>
    std::vector<TraceOp<Key, Value>> load_trace(const std::string& filename) {
        std::ifstream fin(filename);

        if(!fin.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename);
        }

        std::vector<TraceOp<Key, Value>> res;

        std::string line;
        std::size_t line_num = 0;

        while(std::getline(fin, line)) {
            ++line_num;
            line = detail::trim(detail::strip_line(std::move(line)));
    
            if(line.empty()) {
                continue;
            }

            res.push_back(parse_trace_line<Key, Value>(line, line_num));
        }

        return res;
    }

    /**
     * @brief Loads tracing operations and converts it to framework operations.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param filename Input trace file path.
     * 
     * @return Vector of framework operations.
     * 
     * @throws @c std::runtime_error if line cannot be parsed.
     */
    template<typename Key, typename Value>
    std::vector<Operation<Key, Value>> load_ops(const std::string& filename) {
        auto trace = load_trace<Key, Value>(filename);

        std::vector<Operation<Key, Value>> res;
        res.reserve(trace.size());

        for(const auto& it : trace) {
            res.push_back(it.to_operation());
        }

        return res;
    }

    ///////////// TRACE RECORD ////////////

    /**
     * @brief Trace operation with an optional expected result.
     * 
     * TraceRecord is used by full trace dumps. The expected result can be 
     * used to replay a trace without a reference container.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value> 
    struct TraceRecord {
        TraceOp<Key, Value> op; ///< Trace operation stored in this record.

        std::optional<OperationResult<Key, Value>> expected; ///< Excpected operation result, if recorded.
    };

    /** 
     * @brief Writes an optional scalar value using trace result syntax.
     * 
     * Writes the contained value if present, or @c null otherwise.
     * 
     * @tparam T Type of the optional value.
     * 
     * @param out Target output stream.
     * @param value Target optional value for writing.
     */
     template<typename T>
    void write_optional_value(std::ostream& out, const std::optional<T>& value) {
        if(value.has_value()) {
            out << *value;
        } else {
            out << "null";
        }
    }

    /**
     * @brief Writes optional bool value to the output stream.
     * 
     * @param out Target output stream.
     * @param value Target optional bool value for writing.
     */
     inline void write_optional_bool(std::ostream& out, const std::optional<bool>& value) {
        if(value.has_value()) {
            out << (*value ? "true" : "false");
        } else {
            out << "null";
        }
    }

    /**
     * @brief Writes operationg result to the output stream.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param out Output stream for writing.
     * @param res Target operation result object.
     */
    template<typename Key, typename Value>
    void write_operation_result(std::ostream& out, const OperationResult<Key, Value>& res) {
        std::visit([&](const auto& x) {
            using T = std::decay_t<decltype(x)>;

            if constexpr(std::is_same_v<T, KeyResult<Key>>) {
                out << "key ";
                write_optional_value<Key>(out, x.res);
            } else if constexpr(std::is_same_v<T, ValueResult<Value>>) {
                out << "value ";
                write_optional_value<Value>(out, x.res);
            } else if constexpr(std::is_same_v<T, BoolResult>) {
                out << "bool ";
                write_optional_bool(out, x.res);
            } else if constexpr(std::is_same_v<T, CountResult>) {
                out << "count ";
                write_optional_value<std::size_t>(out, x.res);
            } else {
                static_assert(sizeof(T) == 0, "Unknown OperatrionResult type");
            }
        }, res);
    }

    /**
     * @brief Writes trace record to the output stream.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type
     * 
     * @param out Output stream for writing.
     * @param rec Target trace record object.
     */
    template<typename Key, typename Value>
    void write_trace_record(std::ostream& out, const TraceRecord<Key, Value>& rec) {
        write_trace_op<Key, Value>(out, rec.op);

        if(rec.expected.has_value()) {
            out << " => ";
            write_operation_result<Key, Value>(out, *rec.expected);
        }

        out << '\n';
    }

    /// @brief Parses boolean token from string.
    /// @param token Source string line.
    /// @param line_num Index of line for error handling.
    inline bool parse_bool_token(const std::string& token, std::size_t line_num) {
        if(token == "true") {
            return true;
        } else if(token == "false") {
            return false;
        } else {
            throw std::runtime_error("unexpected bool value: '" + token 
                                    + "' at line " + std::to_string(line_num));
        }
    }

    /**
     * @brief Parses token of T type from string.
     * 
     * @tparam T Type of token for parsing.
     * 
     * @param token Source line for parsing.
     * @param line_num Index of line for error handling.
     * @param type_name Name of the type for error handling.
     */
    template<typename T>
    T parse_value_token(const std::string& token, std::size_t line_num, const char* type_name) {
        std::istringstream in(token);

        T value{};

        if(!(in >> value)) {
            throw std::runtime_error("unexpected value with type '" + std::string(type_name) + "': '" 
                                    + token + "' at line " + std::to_string(line_num));
        }

        std::string extra;

        if(in >> extra) {
            throw std::runtime_error("unexpected token with type '" + std::string(type_name) + "': '" +
                                    extra + "' at line " + std::to_string(line_num));
        }

        return value;
    }

    /**
     * @brief Parses operation result from the string.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param line Source string line for parsing.
     * @param line_num Index of line for error handling.
     * 
     */
    template<typename Key, typename Value>
    OperationResult<Key, Value> parse_trace_result(const std::string& line, std::size_t line_num) {

        std::istringstream in(line);

        const std::string type = detail::read_token<std::string>(in, line_num, "res_type");
        const std::string token = detail::read_token<std::string>(in, line_num, "res_value");

        detail::check_extra(in, line_num);

        if(type == "bool") {
            if(token == "null") {
                return BoolResult{std::nullopt};
            }

            return BoolResult{parse_bool_token(token, line_num)};
        } else if(type == "key") {
            if(token == "null") {
                return KeyResult<Key>{std::nullopt};
            }
            
            return KeyResult<Key>{parse_value_token<Key>(token, line_num, "key")};
        } else if(type == "value") {
            if(token == "null") {
                return ValueResult<Value>{std::nullopt};
            }
            
            return ValueResult<Value>{parse_value_token<Value>(token, line_num, "value")};
        } else if(type == "count") {
            if(token == "null") {
                return CountResult{std::nullopt};
            }

            return CountResult{parse_value_token<std::size_t>(token, line_num, "count")};
        } else {
            throw std::runtime_error("Unknown res_type: '" + type + "' at line " + std::to_string(line_num));
        }
    }

    /**
     * @brief Parses full record line.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param line Source record line for parsing.
     * @param line_num Index of line for error handling.
     */
    template<typename Key, typename Value>
    TraceRecord<Key, Value> parse_trace_record_line(const std::string& line, std::size_t line_num) {
        const std::string separator = "=>";
        const auto separator_pos = line.find(separator);

        TraceRecord<Key, Value> rec;

        if(separator_pos == std::string::npos) {
            rec.op = parse_trace_line<Key, Value>(line, line_num);
            rec.expected = std::nullopt;
            return rec;
        }

        std::string op_part = line.substr(0, separator_pos);
        std::string res_part = line.substr(separator_pos + separator.size());

        if(op_part.empty()) {
            throw std::runtime_error("Missing operation before => at line " + std::to_string(line_num));
        }

        if(res_part.empty()) {
            throw std::runtime_error("Missing operation result after => at line " + std::to_string(line_num));
        }

        rec.op = parse_trace_line<Key, Value>(op_part, line_num);
        rec.expected = parse_trace_result<Key, Value>(res_part, line_num);

        return rec;
    }

    /**
     * @brief Loads tracing file and parses it to a vector of trace records.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param filename Source file for loading.
     */
    template<typename Key, typename Value>
    std::vector<TraceRecord<Key, Value>> load_trace_records(const std::string& filename) {
        
        std::ifstream fin(filename);

        if(!fin.is_open()) {
            throw std::runtime_error("Cannot open trace file: " + filename);
        }

        std::vector<TraceRecord<Key, Value>> res;
        
        std::string line;
        std::size_t line_num = 0;

        while(std::getline(fin, line)) {
            ++line_num;

            line = detail::trim(detail::strip_line(std::move(line)));

            if(line.empty()) {
                continue;
            }
            res.push_back(parse_trace_record_line<Key, Value>(line, line_num));
        }

        return res;
    }

    /**
     * @brief Dumps operations together with expected results.
     * 
     * The function runs generated operations against a container created by @p cont_build. 
     * Each operation is written together with the result returned by @c apply_op.
     * 
     * @tparam Wrapper Wrapper for tracing container.
     * @tparam Container Target tracing container type.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param scenario Scenario object.
     * @param seed Seed of the scenario.
     * @param max_ops Maximum count of operations for loading.
     * @param[in] filename Output trace record file path.
     * @param[in] cont_build Factory function used to create the container.
     */
    template<typename Wrapper, typename Container, typename Key, typename Value>
    std::size_t dump_trace_record(AbstractScenario<Key, Value>& scenario, uint64_t seed, std::size_t max_ops, 
    const std::string& filename, std::function<Container()> cont_build = []{return Container{};}) {

        std::ofstream fout(filename);

        if(!fout.is_open()) {
            throw std::runtime_error("Cannot open trace record file: " + filename);
        }

        write_header<Key, Value>(fout, seed, max_ops, scenario);
        scenario.reset(seed);

        Container cont = cont_build();

        Operation<Key, Value> op;
        std::size_t index = 0;

        while((max_ops == 0 || index < max_ops) && scenario.next(op)) {
            ++index;

            OperationResult<Key, Value> expected = apply_op<Wrapper, Container, Key, Value>(cont, op);
            TraceRecord<Key, Value> rec;
            rec.op = to_trace_op<Key, Value>(index, op);
            rec.expected = expected;

            write_trace_record<Key, Value>(fout, rec);
        }

        return index;
    }

    
    /// @brief Configuration for tracing.
    struct TraceDumpConfig {
        bool enabled{false};    ///< Enables trace dumping.

        bool dump_ops{true};    ///< Dumps operation-only trace files.
        bool dump_full{true};   ///< Dumps trace files with expected results.

        std::string filename_prefix{""}; ///< Prefix used for generated trace file names.
    };

} // namespace valid_framework
