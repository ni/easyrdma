// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include <exception>
#include <gtest/gtest.h>
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <stdio.h>
#include <thread>
#include <chrono>
#include "tests/utility/Utility.h"


#ifdef _WIN32
int __cdecl main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    try {

        boost::program_options::options_description desc;
        std::string adapterAddressString;

        desc.add_options()
        ("help", "produce help message")
        ("wait", "Waits on test exe launch, to allow debugger attach")
        ;

        boost::program_options::variables_map vm;

        boost::program_options::store(
            boost::program_options::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm);
        boost::program_options::notify(vm);

        if (vm.count("wait")) {
            std::cout << "Waiting. Press enter to continue... " << std::endl;
            std::cin.get();
        }

        testing::InitGoogleTest(&argc, argv);

        if (vm.count("help")) {
            std::cout << std::endl << "** Additional options **" << std::endl;
            std::cout << desc << "\n";
            return 1;
        }

#ifdef __linux__
        // Linux RT defaults to 1024 max file descriptors
        if(system("ulimit -n 65536") != 0) {
            std::cerr << "Unable to change max number of filesystem handles.\n";
        }

        // Using physical loopback between two ports on Linux requires some settings
        // for ARP to work properly.
        // https://github.com/linux-rdma/rdma-easyrdma/core/blob/master/Documentation/librdmacm.md
        TestAndFixIpv4Loopback();
#endif

        return RUN_ALL_TESTS();
    }
    catch(const std::exception& e) {
        std::cout << "Error " << e.what() << std::endl;
        return 1;
    }
};
