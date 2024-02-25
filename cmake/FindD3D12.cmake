# Autodetect if D3D12 can be used.

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "")

check_cxx_source_compiles("
    #include <d3d12.h>
    int main() { return 0; }"
    D3D12_FOUND
)
