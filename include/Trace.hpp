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

    enum class TraceOpType {
        Insert,
        Get,
        Erase,
        Custom,
    };

    inline constexpr const char* trace_op_type_to_string(TraceOpType to_type) {
        switch (to_type) {
            case TraceOpType::Insert:   return "insert";
            case TraceOpType::Get:      return "get";
            case TraceOpType::Erase:    return "erase";
            case TraceOpType::Custom:   return "custom";
            default: return "unknown";
        }
    }

    template<typename Key, typename Value>
    struct TraceOp {

        std::size_t index{};
        TraceOpType type;

        std::optional<Key> key;
        std::optional<Value> value;

        // Custom
        std::optional<uint32_t> custom_id;
        std::optional<Key> key1;
        std::optional<Key> key2;
        std::optional<std::size_t> size_val;

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
    }


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

        out << '\n';
    }

    template<typename Key, typename Value>
    void write_trace_op_newline(std::ostream& out, const TraceOp<Key, Value>& op) {
        write_trace_op<Key, Value>(out, op);
        out << '\n';
    }

    template<typename Key, typename Value>
    void write_op(std::ostream& out, std::size_t index, const Operation<Key, Value>& op) {
        write_trace_op<Key, Value>(out, to_trace_op(index, op));
    }

    namespace detail {

        inline void write_text(std::ostream& out, const std::string& text) {
            std::istringstream in(text);
            std::string line;

            while(std::getline(in, line)) {
                out << "# " << line << '\n';
            }
        }

        template<typename Key, typename Value>
        void write_header(std::ostream& out, uint64_t seed, std::size_t max_ops, 
                          const AbstractScenario<Key, Value>& scenario) {
            
            out << "# seed " << seed << '\n';

            if(max_ops == 0) {
                out << "# max ops all\n";
            } else {
                out << "# max ops " << max_ops << '\n';
            }

            out << "\n";

            out << "# scenario:\n";
            write_text(out, scenario.to_string());
            out << "\n";
        }

    } // namespace detail

    template<typename Key, typename Value>
    std::size_t dump_trace(AbstractScenario<Key, Value>& scenario, uint64_t seed, std::size_t max_ops,
    const std::string& filename) {

        std::ofstream fout(filename);

        if(!fout.is_open()) {
            throw std::runtime_error("Cannot open trace file: " + filename);
        }

        detail::write_header<Key, Value>(fout, seed, max_ops, scenario);
        scenario.reset(seed);

        Operation<Key, Value> op;
        std::size_t index = 0;

        while((max_ops == 0 || index < max_ops) && scenario.next(op)) {
            ++index;
            write_op(fout, index, op);
        }    
        
        return index;
    }


    //////// PARSING ////////


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
            std::runtime_error("Unknown trace operation");
        }
    }

    namespace detail {

        inline std::string strip_line(std::string line) {
            const auto pos = line.find("#");

            if(pos != std::string::npos) {
                line.erase(pos);
            }
            return line;
        }

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

        template<typename T>
        T read_token(std::istringstream& in, std::size_t line_num, const char* field_name) {
            T value{};

            if(!(in >> value)) {
                throw std::runtime_error("Cannot parse field '" + std::string(field_name) 
                                        + "' at line " + std::to_string(line_num));
            }

            return value;
        }

        inline void check_extra(std::istringstream& in, std::size_t line_num) {
            std::string extra;

            if(in >> extra) {
                throw std::runtime_error("Unexpected token: '" + extra 
                                        + "' at line " + std::to_string(line_num));
            }
        }

    } // namespace detail

    template<typename Key, typename Value>
    TraceOp<Key, Value> parse_trace_line(const std::string& line, std::size_t line_num) {
        std::istringstream in(line);

        TraceOp<Key, Value> res;

        res.index = detail::read_token<std::size_t>(in, line_num, "index");
        const std::string op_name = detail::read_token<std::string>(in, line_num, "op");
        res.type = parse_trace_line<Key, Value>(op_name);

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

    template<typename Key, typename Value> 
    struct TraceRecord {
        TraceOp<Key, Value> op;

        std::optional<OperationResult<Key, Value>> expected;
    };

    template<typename T>
    void write_optonal_value(std::ostream& out, const std::optional<T>& value) {
        if(value.has_value()) {
            out << *value;
        } else {
            out << "null";
        }
    }

    inline void write_optional_bool(std::ostream& out, const std::optional<bool>& value) {
        if(value.has_value()) {
            out << (*value ? "true" : "false");
        } else {
            out << "null";
        }
    }

    template<typename Key, typename Value>
    void write_operation_result(std::ostream& out, const OperationResult<Key, Value>& res) {
        std::visit([&](const auto& x) {
            using T = std::decay_t<decltype(x)>;

            if constexpr(std::is_same_v<T, KeyResult<Key>>) {
                out << "key ";
                write_optonal_value(out, x.res);
            } else if constexpr(std::is_same_v<T, ValueResult<Value>>) {
                out << "value ";
                write_optonal_value(out, x.res);
            } else if constexpr(std::is_same_v<T, BoolResult>) {
                out << "bool ";
                write_optonal_value(out, x.res);
            } else if constexpr(std::is_same_v<T, CountResult>) {
                out << "count ";
                write_optonal_value(out, x.res);
            } else {
                static_assert(sizeof(T) == 0, "Unknown OperatrionResult type");
            }
        });
    }

    template<typename Key, typename Value>
    void write_trace_record(std::ostream& out, const TraceRecord<Key, Value>& rec) {
        write_trace_op_newline<Key, Value>(out, rec.op);

        if(rec.expected.has_value()) {
            out << " => ";
            write_operation_result<Key, Value>(out, *rec.expected);
        }

        out << '\n';
    }

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

    template<typename Wrapper, typename Container, typename Key, typename Value>
    std::size_t dump_trace_record(AbstractScenario<Key, Value>& scenario, uint64_t seed, std::size_t max_ops, 
    const std::string& filename, std::function<Container()> cont_build = []{return Container{};}) {

        std::ofstream fout(filename);

        if(!fout.is_open()) {
            throw std::runtime_error("Cannot open trace record file: " + filename);
        }

        detail::write_header<Key, Value>(fout, seed, max_ops, scenario);
        scenario.reset();

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


} // namespace valid_framework
