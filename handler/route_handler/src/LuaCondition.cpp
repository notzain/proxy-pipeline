#include "LuaCondition.hpp"
#include "core/metadata/MetadataCatalog.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <string_view>

#if defined(PROXY_HAS_LUA) && PROXY_HAS_LUA
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Proxy::Handler {

namespace {

    /**
     * @brief Recursively converts an intermediate representation Value into a Lua object.
     */
    sol::object irToLua(lua_State* L, const Core::Serialization::Ir::Value& val)
    {
        if (val.isNull()) {
            return sol::make_object(L, sol::lua_nil);
        }
        if (val.isBool()) {
            return sol::make_object(L, val.asBool().value_or(false));
        }
        if (val.isInt()) {
            return sol::make_object(L, val.asInt().value_or(0));
        }
        if (val.isUInt()) {
            return sol::make_object(L, val.asUInt().value_or(0));
        }
        if (val.isDouble()) {
            return sol::make_object(L, val.asDouble().value_or(0.0));
        }
        if (val.isString()) {
            auto sv = val.asStringView().value_or("");
            return sol::make_object(L, sv);
        }
        if (val.isArray()) {
            sol::state_view lua(L);
            sol::table tbl = lua.create_table();
            std::size_t idx = 1;
            for (const auto& elem : *val.asArray()) {
                tbl[idx++] = irToLua(L, elem);
            }
            return tbl;
        }
        if (val.isObject()) {
            sol::state_view lua(L);
            sol::table tbl = lua.create_table();
            for (const auto& [k, v] : *val.asObject()) {
                tbl[k] = irToLua(L, v);
            }
            return tbl;
        }
        return sol::make_object(L, sol::lua_nil);
    }

} // namespace

struct LuaCondition::Impl {
    std::string script;
    sol::state lua;
    sol::function func;
    bool valid { false };

    explicit Impl(std::string scriptCode)
        : script(std::move(scriptCode))
    {
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

        // Wrap the snippet into a function taking `msg` if not already defining one
        std::string wrappedScript;
        if (script.find("function") != std::string::npos) {
            wrappedScript = script;
        } else {
            wrappedScript = "return function(msg)\n" + script + "\nend";
        }

        auto loadResult = lua.load(wrappedScript);
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            PROXY_LOG_ERROR("Failed to compile Lua script: {}", err.what());
            return;
        }

        sol::protected_function pf = loadResult;
        auto execResult = pf();
        if (!execResult.valid()) {
            sol::error err = execResult;
            PROXY_LOG_ERROR("Failed to initialize Lua condition function: {}", err.what());
            return;
        }

        if (execResult.get_type() == sol::type::function) {
            func = execResult.get<sol::function>();
            valid = true;
        } else {
            if (lua["route"].valid() && lua["route"].get_type() == sol::type::function) {
                func = lua["route"];
                valid = true;
            } else if (lua["matches"].valid() && lua["matches"].get_type() == sol::type::function) {
                func = lua["matches"];
                valid = true;
            } else {
                PROXY_LOG_ERROR("Lua snippet must return a function(msg) or define 'route(msg)'");
            }
        }
    }
};

LuaCondition::LuaCondition(std::string scriptCode)
    : m_impl(std::make_unique<Impl>(std::move(scriptCode)))
{
}

LuaCondition::~LuaCondition() = default;

LuaCondition::LuaCondition(const LuaCondition& other)
    : m_impl(other.m_impl ? std::make_unique<Impl>(other.m_impl->script) : nullptr)
{
}

LuaCondition& LuaCondition::operator=(const LuaCondition& other)
{
    if (this != &other) {
        m_impl = other.m_impl ? std::make_unique<Impl>(other.m_impl->script) : nullptr;
    }
    return *this;
}

bool LuaCondition::matches(const Backend::MessageView& message) const noexcept
{
    if (!m_impl || !m_impl->valid || !m_impl->func.valid()) {
        return false;
    }

    try {
        sol::table msg = m_impl->lua.create_table();
        auto payload = message.payload();
        std::string_view payloadStr(reinterpret_cast<const char*>(payload.data()), payload.size());
        msg["payload"] = payloadStr;
        msg["protocol"] = static_cast<int>(message.protocol().value());

        // Attach lazy metadata metatable on msg.meta and msg fallback
        // Resolves fields on demand via MetadataCatalog without coupling to concrete structs
        sol::table metaTable = m_impl->lua.create_table();
        sol::table metaMetatable = m_impl->lua.create_table();

        bool populated = false;
        auto populateFields = [&message, &metaTable, &populated, this]() {
            if (populated) {
                return;
            }
            populated = true;
            auto optIr = Core::Metadata::MetadataCatalog::toIr(message.protocol(), message.metadata());
            if (optIr && optIr->isObject()) {
                lua_State* L = m_impl->lua.lua_state();
                for (const auto& [k, v] : *optIr->asObject()) {
                    metaTable.raw_set(k, irToLua(L, v));
                }
            }
        };

        metaMetatable[sol::meta_function::index] = [populateFields, &metaTable](sol::table, const std::string& key) -> sol::object {
            populateFields();
            return metaTable.raw_get<sol::object>(key);
        };

        metaTable[sol::metatable_key] = metaMetatable;
        msg["meta"] = metaTable;

        // Also allow top-level fallback into meta fields (e.g. msg.src_port or msg.port)
        sol::table msgMetatable = m_impl->lua.create_table();
        msgMetatable[sol::meta_function::index] = [populateFields, &metaTable](sol::table self, const std::string& key) -> sol::object {
            populateFields();
            auto rawVal = metaTable.raw_get<sol::object>(key);
            if (rawVal.valid() && rawVal.get_type() != sol::type::lua_nil) {
                return rawVal;
            }
            // Convenience aliases for network ports in network byte order:
            if (key == "port" || key == "srcPort") {
                auto portVal = metaTable.raw_get<sol::object>("src_port");
                if (portVal.valid() && portVal.get_type() == sol::type::number) {
                    auto rawPort = portVal.as<std::uint16_t>();
                    return sol::make_object(self.lua_state(), ntohs(rawPort));
                }
            }
            if (key == "dst_port" || key == "dstPort") {
                auto portVal = metaTable.raw_get<sol::object>("dst_port");
                if (portVal.valid() && portVal.get_type() == sol::type::number) {
                    auto rawPort = portVal.as<std::uint16_t>();
                    return sol::make_object(self.lua_state(), ntohs(rawPort));
                }
            }
            return sol::make_object(self.lua_state(), sol::lua_nil);
        };
        msg[sol::metatable_key] = msgMetatable;

        sol::protected_function_result result = m_impl->func(msg);
        if (!result.valid()) {
            sol::error err = result;
            PROXY_LOG_ERROR("Error evaluating Lua rule: {}", err.what());
            return false;
        }

        if (result.get_type() == sol::type::boolean) {
            return result.get<bool>();
        }
        if (result.get_type() == sol::type::string) {
            std::string str = result.get<std::string>();
            return !str.empty();
        }
    } catch (const std::exception& e) {
        PROXY_LOG_ERROR("Exception during Lua evaluation: {}", e.what());
        return false;
    }

    return false;
}

std::unique_ptr<IRouteCondition> LuaCondition::clone() const
{
    return std::make_unique<LuaCondition>(*this);
}

const std::string& LuaCondition::scriptCode() const noexcept
{
    static const std::string empty;
    return m_impl ? m_impl->script : empty;
}

} // namespace Proxy::Handler

#else

// Lua disabled via PROXY_ENABLE_LUA=OFF
namespace Proxy::Handler {

struct LuaCondition::Impl {
    std::string script;
};

LuaCondition::LuaCondition(std::string scriptCode)
    : m_impl(std::make_unique<Impl>(Impl { std::move(scriptCode) }))
{
    PROXY_LOG_WARN("Lua support is disabled in this build (PROXY_ENABLE_LUA=OFF)");
}

LuaCondition::~LuaCondition() = default;

LuaCondition::LuaCondition(const LuaCondition& other)
    : m_impl(other.m_impl ? std::make_unique<Impl>(*other.m_impl) : nullptr)
{
}

LuaCondition& LuaCondition::operator=(const LuaCondition& other)
{
    if (this != &other) {
        m_impl = other.m_impl ? std::make_unique<Impl>(*other.m_impl) : nullptr;
    }
    return *this;
}

bool LuaCondition::matches(const Backend::MessageView&) const noexcept
{
    return false;
}

std::unique_ptr<IRouteCondition> LuaCondition::clone() const
{
    return std::make_unique<LuaCondition>(*this);
}

const std::string& LuaCondition::scriptCode() const noexcept
{
    static const std::string empty;
    return m_impl ? m_impl->script : empty;
}

} // namespace Proxy::Handler

#endif
