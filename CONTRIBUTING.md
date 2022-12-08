# Contributing to easyRDMA

## Dependencies Needed

- CMake (tested with 3.23)
- Boost (tested with 1.73.0/1.79.0)
    - Found via standard CMake `find_package()` call
- NetworkDirect (Windows build)
    - Download: `nuget.exe install NetworkDirect -Version 2.0.1`
    - CMake expected to be configured with `NetDirect_HEADER_DIR` / `NetDirect_LIBRARYDIR` variables pointing to the NetworkDirect install location
- libibverbs/rdma_cm (Linux build)
    - Both part of rdma-core package
    - CMake can be configured with `VERBS_HEADER_DIR` / `VERBS_LIB_DIR` variables, if not available in default system search directories
- googletest
    - Retrieved automatically via CMake step

## Testing

- Unit tests (`easyrdma_unit_tests`) cover some basic internal classes
- System tests (`easyrdma_tests`) cover the full API surface and are intended to be run on a system with RDMA-capable hardware

System setup:

- Two RDMA-capable network ports connected directly together on the system in a loopback-manner
- Tests expect them to have compatible IP addresses to connect to each other (such as link-local). These ports may be on the same physical card.
- Tests can also run on a single port with internal loopback
- API tests a variety of edge conditions with the underlying vendor's implementation and might not pass on all hardware. Extensive testing only done on Mellanox/NVIDIA ConnectX-4 (and newer) hardware.
