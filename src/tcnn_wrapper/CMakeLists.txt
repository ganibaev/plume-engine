add_library(tcnn_wrapper STATIC
            tcnn_wrapper.cu
            tcnn_wrapper.h)

###############################################################################
# CUDA compiler setup
###############################################################################

set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CUDA_LINK_LIBRARIES_KEYWORD PUBLIC)
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)


###############################################################################

target_link_libraries(tcnn_wrapper tiny-cuda-nn)