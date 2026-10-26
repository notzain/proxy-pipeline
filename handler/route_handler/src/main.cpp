#include "RouteHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
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

    backend->configure(config);

    Proxy::Handler::RouteHandler handler(*backend);
    Core::Handler::HandlerRunner runner(handler);
    if (!runner.configure(config)) {
        std::cerr << "Failed to configure RouteHandler\n";
        return 1;
    }

    Proxy::Log::info("[Main] Starting RouteHandler loop...");
    runner.start();
    return 0;
}
