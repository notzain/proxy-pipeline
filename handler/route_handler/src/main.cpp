#include "RouteHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: route_handler <config_file>\n";
        return 1;
    }

    auto config = Core::Config::Configuration::fromFile(argv[1]);
    auto backend = ::Backend::createBackend();
    if (!backend) {
        std::cerr << "Failed to instantiate backend transport\n";
        return 1;
    }

    if (!backend->configure(config)) {
        std::cerr << "Failed to configure backend\n";
        return 1;
    }

    Proxy::Handler::RouteHandler handler;
    if (!handler.configure(config, *backend)) {
        std::cerr << "Failed to configure RouteHandler\n";
        return 1;
    }

    Proxy::Log::info("[Main] Starting RouteHandler loop...");
    handler.start();
    return 0;
}

