cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(vdmpoke LANGUAGES C)

option(VDMP_INSTALL_HPMFRAUD "Install the HPMFraud library" OFF)

add_library(HPMFraud lib/HPMFraud.c)
target_include_directories(HPMFraud PUBLIC include)
target_compile_features(HPMFraud PUBLIC c_std_99)
target_compile_options(HPMFraud PRIVATE "-Wno-gcc-compat")
target_link_libraries(HPMFraud PRIVATE "-framework CoreFoundation")
target_link_libraries(HPMFraud PRIVATE "-framework IOKit")

add_executable(vdmpoke src/main.c)
target_link_libraries(vdmpoke PRIVATE HPMFraud)

install(TARGETS vdmpoke)

if (VDMP_INSTALL_HPMFRAUD)
    install(TARGETS HPMFraud)
    install(FILES include/HPMFraud.h DESTINATION include)
endif()
