#include "FepGatewayApplication.hpp"
#include <hypernet/core/ConfigLoader.hpp>
#include <hypernet/runtime/ServerBuilder.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        auto cfg = hypernet::core::ConfigLoader::load(argc, argv);

        auto server = hypernet::runtime::ServerBuilder{}
                          .config(cfg.engine)
                          .makeApplication<fep::FepGatewayApplication>(cfg.fep)
                          .build();

        server.run();
        return 0;
    }
    catch (const std::exception &e)
    {
        return 1;
    }
}
