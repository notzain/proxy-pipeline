#include "LuaCondition.hpp"
#include "core/net/UdpMetadata.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <string_view>

#if defined(PROXY_HAS_LUA) && PROXY_HAS_LUA
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Proxy::Handler {

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
            Proxy::Log::error("[LuaCondition] Failed to compile Lua script: {}", err.what());
            return;
        }

        sol::protected_function pf = loadResult;
        auto execResult = pf();
        if (!execResult.valid()) {
            sol::error err = execResult;
            Proxy::Log::error("[LuaCondition] Failed to initialize Lua condition function: {}", err.what());
            return;
        }

        if (execResult.get_type() == sol::type::function) {
            func = execResult.get<sol::function>();
            valid = true;
        } else {
            // Check if global `route` or `matches` exists
            if (lua["route"].valid() && lua["route"].get_type() == sol::type::function) {
                func = lua["route"];
                valid = true;
            } else if (lua["matches"].valid() && lua["matches"].get_type() == sol::type::function) {
                func = lua["matches"];
                valid = true;
            } else {
                Proxy::Log::error("[LuaCondition] Lua snippet must return a function(msg) or define 'route(msg)'");
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

        const auto udpMeta = message.getMetadata<Core::Net::UdpMetadata>();
        if (udpMeta) {
            msg["port"] = ntohs(udpMeta->srcPort);
            msg["dst_port"] = ntohs(udpMeta->dstPort);
        } else {
            msg["port"] = sol::lua_nil;
            msg["dst_port"] = sol::lua_nil;
        }

        sol::protected_function_result result = m_impl->func(msg);
        if (!result.valid()) {
            sol::error err = result;
            Proxy::Log::error("[LuaCondition] Error evaluating Lua rule: {}", err.what());
            return false;
        }

        if (result.get_type() == sol::type::boolean) {
            return result.get<bool>();
        }
        if (result.get_type() == sol::type::string) {
            // Non-empty string counts as truthy
            std::string str = result.get<std::string>();
            return !str.empty();
        }
    } catch (const std::exception& e) {
        Proxy::Log::error("[LuaCondition] Exception during Lua evaluation: {}", e.what());
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
    Proxy::Log::warn("[LuaCondition] Lua support is disabled in this build (PROXY_ENABLE_LUA=OFF)");
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

