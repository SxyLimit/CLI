#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace sj {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

class Value {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() : type_(Type::Null), number_(0.0), bool_(false) {}
  explicit Value(std::nullptr_t) : Value() {}
  explicit Value(bool b) : type_(Type::Bool), number_(0.0), bool_(b) {}
  explicit Value(double num) : type_(Type::Number), number_(num), bool_(false) {}
  explicit Value(int num) : type_(Type::Number), number_(static_cast<double>(num)), bool_(false) {}
  explicit Value(long long num) : type_(Type::Number), number_(static_cast<double>(num)), bool_(false) {}
  explicit Value(const std::string& s) : type_(Type::String), string_(s), number_(0.0), bool_(false) {}
  explicit Value(std::string&& s) : type_(Type::String), string_(std::move(s)), number_(0.0), bool_(false) {}
  explicit Value(const char* s) : type_(Type::String), string_(s ? s : ""), number_(0.0), bool_(false) {}
  explicit Value(const Array& arr) : type_(Type::Array), array_(arr), number_(0.0), bool_(false) {}
  explicit Value(Array&& arr) : type_(Type::Array), array_(std::move(arr)), number_(0.0), bool_(false) {}
  explicit Value(const Object& obj) : type_(Type::Object), object_(obj), number_(0.0), bool_(false) {}
  explicit Value(Object&& obj) : type_(Type::Object), object_(std::move(obj)), number_(0.0), bool_(false) {}

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool defaultValue = false) const {
    if(isBool()) return bool_;
    if(isNumber()) return number_ != 0.0;
    return defaultValue;
  }

  double asNumber(double defaultValue = 0.0) const {
    if(isNumber()) return number_;
    if(isBool()) return bool_ ? 1.0 : 0.0;
    return defaultValue;
  }

  long long asInteger(long long defaultValue = 0) const {
    if(isNumber()) return static_cast<long long>(number_);
    if(isBool()) return bool_ ? 1 : 0;
    return defaultValue;
  }

  const std::string& asString() const {
    if(!isString()) throw std::runtime_error("json value is not string");
    return string_;
  }

  const Array& asArray() const {
    if(!isArray()) throw std::runtime_error("json value is not array");
    return array_;
  }

  const Object& asObject() const {
    if(!isObject()) throw std::runtime_error("json value is not object");
    return object_;
  }

  Array& array() {
    if(!isArray()) throw std::runtime_error("json value is not array");
    return array_;
  }

  Object& object() {
    if(!isObject()) throw std::runtime_error("json value is not object");
    return object_;
  }

  const Value* find(const std::string& key) const {
    if(!isObject()) return nullptr;
    auto it = object_.find(key);
    if(it == object_.end()) return nullptr;
    return &it->second;
  }

private:
  Type type_;
  std::string string_;
  Array array_;
  Object object_;
  double number_;
  bool bool_;
};

class Parser {
public:
  Parser(const std::string& text) : text_(text), pos_(0) {}

  Value parse(){
    skipWhitespace();
    Value v = parseValue();
    skipWhitespace();
    if(pos_ != text_.size()){
      throw std::runtime_error("unexpected characters after JSON value");
    }
    return v;
  }

private:
  const std::string& text_;
  size_t pos_;

  void skipWhitespace(){
    while(pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))){
      ++pos_;
    }
  }

  Value parseValue(){
    if(pos_ >= text_.size()) throw std::runtime_error("unexpected end of JSON");
    char ch = text_[pos_];
    switch(ch){
      case 'n': return parseNull();
      case 't': return parseTrue();
      case 'f': return parseFalse();
      case '"': return parseString();
      case '[': return parseArray();
      case '{': return parseObject();
      default:
        if(ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parseNumber();
        throw std::runtime_error("invalid JSON value");
    }
  }

  Value parseNull(){
    expect("null");
    return Value();
  }

  Value parseTrue(){
    expect("true");
    return Value(true);
  }

  Value parseFalse(){
    expect("false");
    return Value(false);
  }

  Value parseNumber(){
    size_t start = pos_;
    if(text_[pos_] == '-') ++pos_;
    if(pos_ >= text_.size()) throw std::runtime_error("invalid number");
    if(text_[pos_] == '0'){
      ++pos_;
    }else{
      if(!std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("invalid number");
      while(pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))){ ++pos_; }
    }
    if(pos_ < text_.size() && text_[pos_] == '.'){
      ++pos_;
      if(pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("invalid number");
      while(pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))){ ++pos_; }
    }
    if(pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')){
      ++pos_;
      if(pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
      if(pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("invalid number");
      while(pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))){ ++pos_; }
    }
    std::string token = text_.substr(start, pos_ - start);
    double value = std::strtod(token.c_str(), nullptr);
    return Value(value);
  }

  Value parseString(){
    if(text_[pos_] != '"') throw std::runtime_error("expected string");
    ++pos_;
    std::string out;
    while(pos_ < text_.size()){
      char ch = text_[pos_++];
      if(ch == '"'){
        return Value(out);
      }
      if(ch == '\\'){
        if(pos_ >= text_.size()) throw std::runtime_error("invalid escape");
        char esc = text_[pos_++];
        switch(esc){
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u':{
            if(pos_ + 4 > text_.size()) throw std::runtime_error("invalid unicode escape");
            unsigned int code = 0;
            for(int i=0;i<4;++i){
              char hex = text_[pos_++];
              code <<= 4;
              if(hex >= '0' && hex <= '9') code += static_cast<unsigned int>(hex - '0');
              else if(hex >= 'a' && hex <= 'f') code += static_cast<unsigned int>(hex - 'a' + 10);
              else if(hex >= 'A' && hex <= 'F') code += static_cast<unsigned int>(hex - 'A' + 10);
              else throw std::runtime_error("invalid unicode escape");
            }
            if(code <= 0x7F){
              out.push_back(static_cast<char>(code));
            }else if(code <= 0x7FF){
              out.push_back(static_cast<char>(0xC0 | ((code >> 6) & 0x1F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }else{
              out.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default:
            throw std::runtime_error("invalid escape sequence");
        }
      }else{
        out.push_back(ch);
      }
    }
    throw std::runtime_error("unterminated string");
  }

  Value parseArray(){
    if(text_[pos_] != '[') throw std::runtime_error("expected array");
    ++pos_;
    Array arr;
    skipWhitespace();
    if(pos_ < text_.size() && text_[pos_] == ']'){ ++pos_; return Value(std::move(arr)); }
    while(true){
      skipWhitespace();
      arr.push_back(parseValue());
      skipWhitespace();
      if(pos_ >= text_.size()) throw std::runtime_error("unterminated array");
      char ch = text_[pos_++];
      if(ch == ']') break;
      if(ch != ',') throw std::runtime_error("expected comma in array");
    }
    return Value(std::move(arr));
  }

  Value parseObject(){
    if(text_[pos_] != '{') throw std::runtime_error("expected object");
    ++pos_;
    Object obj;
    skipWhitespace();
    if(pos_ < text_.size() && text_[pos_] == '}'){ ++pos_; return Value(std::move(obj)); }
    while(true){
      skipWhitespace();
      Value key = parseString();
      skipWhitespace();
      if(pos_ >= text_.size() || text_[pos_] != ':') throw std::runtime_error("expected colon in object");
      ++pos_;
      skipWhitespace();
      Value value = parseValue();
      obj.emplace(key.asString(), std::move(value));
      skipWhitespace();
      if(pos_ >= text_.size()) throw std::runtime_error("unterminated object");
      char ch = text_[pos_++];
      if(ch == '}') break;
      if(ch != ',') throw std::runtime_error("expected comma in object");
    }
    return Value(std::move(obj));
  }

  void expect(const char* token){
    size_t len = std::strlen(token);
    if(pos_ + len > text_.size() || text_.compare(pos_, len, token) != 0){
      throw std::runtime_error("unexpected token");
    }
    pos_ += len;
  }
};

inline Value parse(const std::string& text){
  Parser parser(text);
  return parser.parse();
}

inline std::string dumpString(const std::string& value){
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for(char ch : value){
    switch(ch){
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20){
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
          out += buf;
        }else{
          out.push_back(ch);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

inline std::string dump(const Value& value, int indent = -1, int level = 0){
  switch(value.type()){
    case Value::Type::Null:
      return "null";
    case Value::Type::Bool:
      return value.asBool() ? "true" : "false";
    case Value::Type::Number: {
      char buf[64];
      double num = value.asNumber();
      if(std::isfinite(num)){
        std::snprintf(buf, sizeof(buf), "%.15g", num);
      }else if(std::isnan(num)){
        return "null";
      }else{
        return num < 0 ? "-1e9999" : "1e9999";
      }
      std::string out(buf);
      if(out.find('.') == std::string::npos && out.find('e') == std::string::npos && out.find('E') == std::string::npos){
        // ensure integer formatting doesn't include trailing .0
      }
      return out;
    }
    case Value::Type::String:
      return dumpString(value.asString());
    case Value::Type::Array: {
      const auto& arr = value.asArray();
      if(arr.empty()) return "[]";
      std::string out;
      out.push_back('[');
      bool pretty = indent >= 0;
      for(size_t i=0;i<arr.size();++i){
        if(pretty){
          out.push_back('\n');
          out.append((level+1)*indent, ' ');
        }
        out += dump(arr[i], indent, level+1);
        if(i + 1 < arr.size()) out.push_back(',');
      }
      if(pretty){
        out.push_back('\n');
        out.append(level*indent, ' ');
      }
      out.push_back(']');
      return out;
    }
    case Value::Type::Object: {
      const auto& obj = value.asObject();
      if(obj.empty()) return "{}";
      std::string out;
      out.push_back('{');
      bool pretty = indent >= 0;
      size_t i = 0;
      for(const auto& kv : obj){
        if(pretty){
          out.push_back('\n');
          out.append((level+1)*indent, ' ');
        }
        out += dumpString(kv.first);
        out.push_back(':');
        if(pretty) out.push_back(' ');
        out += dump(kv.second, indent, level+1);
        if(++i < obj.size()) out.push_back(',');
      }
      if(pretty){
        out.push_back('\n');
        out.append(level*indent, ' ');
      }
      out.push_back('}');
      return out;
    }
  }
  return "null";
}

inline Value make_object(std::initializer_list<std::pair<const std::string, Value>> entries){
  Object obj;
  for(auto& kv : entries){
    obj.emplace(kv.first, kv.second);
  }
  return Value(std::move(obj));
}

inline Value make_array(std::initializer_list<Value> entries){
  Array arr(entries);
  return Value(std::move(arr));
}

} // namespace sj

