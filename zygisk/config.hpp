/*
 * RshMod 配置数据模型与 JSON 解析（C++ 侧）
 * 内置零依赖递归下降 JSON 解析器，覆盖配置所需语法。
 */
#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <cctype>

//------------------------------------------------------------------------------
// 极简 JSON 值
//------------------------------------------------------------------------------
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool        b = false;
    double      num = 0;
    std::string str;
    std::vector<JsonValue>            arr;
    std::map<std::string, JsonValue>  obj;

    bool isNull()   const { return type == Null; }
    bool isBool()   const { return type == Bool; }
    bool isNum()    const { return type == Number; }
    bool isString() const { return type == String; }
    bool isArray()  const { return type == Array; }
    bool isObject() const { return type == Object; }
    const std::string& strVal() const { static const std::string e; return type==String ? str : e; }
};

//------------------------------------------------------------------------------
// 递归下降 JSON 解析器
//------------------------------------------------------------------------------
namespace rj {
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s), i_(0) {}
    JsonValue parse() { skipWs(); JsonValue v = parseValue(); skipWs(); return v; }
private:
    const std::string& s_;
    size_t i_;
    void skipWs() {
        while (i_ < s_.size() && (s_[i_]==' '||s_[i_]=='\t'||s_[i_]=='\n'||s_[i_]=='\r')) ++i_;
    }
    char peek() { return i_ < s_.size() ? s_[i_] : '\0'; }
    char get()  { return i_ < s_.size() ? s_[i_++] : '\0'; }
    bool expect(char c) {
        if (peek() != c) return false;
        ++i_; return true;
    }
    JsonValue parseValue() {
        skipWs(); char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't') { get();get();get();get(); JsonValue v; v.type=JsonValue::Bool; v.b=true; return v; }
        if (c == 'f') { get();get();get();get();get(); JsonValue v; v.type=JsonValue::Bool; v.b=false; return v; }
        if (c == 'n') { get();get();get();get(); JsonValue v; v.type=JsonValue::Null; return v; }
        return parseNumber();
    }
    JsonValue parseObject() {
        JsonValue v; v.type = JsonValue::Object;
        if (!expect('{')) return v;
        skipWs();
        if (peek()=='}') { get(); return v; }
        while (true) {
            skipWs(); expect('"');
            JsonValue key = parseString();
            skipWs(); expect(':');
            JsonValue val = parseValue();
            v.obj[key.str] = val;
            skipWs();
            char c = get();
            if (c == ',') continue;
            if (c == '}') break;
            return v;
        }
        return v;
    }
    JsonValue parseArray() {
        JsonValue v; v.type = JsonValue::Array;
        if (!expect('[')) return v;
        skipWs();
        if (peek()==']') { get(); return v; }
        while (true) {
            skipWs();
            v.arr.push_back(parseValue());
            skipWs();
            char c = get();
            if (c == ',') continue;
            if (c == ']') break;
            return v;
        }
        return v;
    }
    JsonValue parseString() {
        JsonValue v; v.type = JsonValue::String;
        expect('"');
        std::string out;
        while (i_ < s_.size()) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    default: out += e;
                }
            } else out += c;
        }
        v.str = out;
        return v;
    }
    JsonValue parseNumber() {
        JsonValue v; v.type = JsonValue::Number;
        std::string n;
        while (i_ < s_.size() &&
               (isdigit((unsigned char)peek())||peek()=='-'||peek()=='+'||peek()=='.'||peek()=='e'||peek()=='E')) {
            n += get();
        }
        v.num = atof(n.c_str());
        return v;
    }
};
} // namespace rj

//------------------------------------------------------------------------------
// 配置模型
//------------------------------------------------------------------------------
struct RshConfig {
    bool                     enableGlobal = false;
    std::vector<std::string> scopes;
    std::map<std::string, std::string> props;
    std::map<std::string, std::string> ids;
    std::map<std::string, std::string> webview;
    std::map<std::string, std::string> wifi;
    std::map<std::string, std::string> bluetooth;
    std::map<std::string, std::string> drm;
    std::map<std::string, std::string> operatorInfo;
    std::map<std::string, float>       geo;
    std::map<std::string, bool>        switches;

    bool enableBuildProp=false, enableAndroidId=false, enableImei=false,
         enableGeo=false, enableWifi=false, enableBt=false, enableDrm=false,
         enableWebView=false, enableOperator=false;

    void applySwitch(const std::string& k, bool v) {
        if      (k=="build_prop") enableBuildProp=v;
        else if (k=="android_id") enableAndroidId=v;
        else if (k=="imei")       enableImei=v;
        else if (k=="geo")        enableGeo=v;
        else if (k=="wifi")       enableWifi=v;
        else if (k=="bluetooth")  enableBt=v;
        else if (k=="drm")        enableDrm=v;
        else if (k=="webview")    enableWebView=v;
        else if (k=="operator")   enableOperator=v;
    }
    std::string lookupProp(const std::string& name) const {
        auto it = props.find(name);
        if (it != props.end()) return it->second;
        return "";
    }
    static RshConfig fromJson(const std::string& raw) {
        RshConfig cfg;
        rj::Parser p(raw);
        JsonValue root = p.parse();
        if (!root.isObject()) return cfg;

        auto it = root.obj.find("global");
        if (it!=root.obj.end() && it->second.isBool()) cfg.enableGlobal = it->second.b;

        it = root.obj.find("scopes");
        if (it!=root.obj.end() && it->second.isArray())
            for (auto& e : it->second.arr)
                if (e.isString()) cfg.scopes.push_back(e.str);

        auto fill = [](JsonValue& root, const char* k, std::map<std::string,std::string>& m){
            auto i = root.obj.find(k);
            if (i != root.obj.end() && i->second.isObject())
                for (auto& kv : i->second.obj)
                    if (kv.second.isString()) m[kv.first] = kv.second.str;
        };
        fill(root, "props", cfg.props);
        fill(root, "ids", cfg.ids);
        fill(root, "webview", cfg.webview);
        fill(root, "wifi", cfg.wifi);
        fill(root, "bluetooth", cfg.bluetooth);
        fill(root, "drm", cfg.drm);
        fill(root, "operator", cfg.operatorInfo);

        it = root.obj.find("geo");
        if (it!=root.obj.end() && it->second.isObject())
            for (auto& kv : it->second.obj)
                if (kv.second.isNum()) cfg.geo[kv.first] = (float)kv.second.num;

        it = root.obj.find("switch");
        if (it!=root.obj.end() && it->second.isObject())
            for (auto& kv : it->second.obj)
                if (kv.second.isBool()) { cfg.switches[kv.first]=kv.second.b; cfg.applySwitch(kv.first,kv.second.b); }

        return cfg;
    }
};