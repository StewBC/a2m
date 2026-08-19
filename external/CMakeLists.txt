add_library(a2m_stb_ds STATIC
    stb/stb_ds_impl.c
)
target_include_directories(a2m_stb_ds PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb
)
target_compile_features(a2m_stb_ds PUBLIC c_std_99)

add_library(a2m_logc STATIC
    logc/log.c
)
target_include_directories(a2m_logc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/logc
)
target_compile_features(a2m_logc PUBLIC c_std_99)

add_library(a2m_argparse STATIC
    argparse/argparse.c
)
target_include_directories(a2m_argparse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/argparse
)
target_compile_features(a2m_argparse PUBLIC c_std_99)

add_library(a2m_inih STATIC
    inih/ini.c
)
target_include_directories(a2m_inih PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inih
)
target_compile_features(a2m_inih PUBLIC c_std_99)

add_library(a2m_whereami STATIC
    whereami/whereami.c
)
target_include_directories(a2m_whereami PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/whereami
)
target_compile_features(a2m_whereami PUBLIC c_std_99)

add_library(a2m_stb_image STATIC
    stb/stb_image_impl.c
)
target_include_directories(a2m_stb_image PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb
)
target_compile_features(a2m_stb_image PUBLIC c_std_99)

add_library(a2m_tiny_regex STATIC
    tiny-regex-c/re.c
)
target_include_directories(a2m_tiny_regex PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tiny-regex-c
)
target_compile_features(a2m_tiny_regex PUBLIC c_std_99)
