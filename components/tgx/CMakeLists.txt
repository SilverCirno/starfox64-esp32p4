set(TGX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/tgx/src")
idf_component_register(
    SRCS "${TGX_ROOT}/Color.cpp" "${TGX_ROOT}/Fonts.cpp" "${TGX_ROOT}/Renderer3D.cpp"
         "${TGX_ROOT}/font_tgx_Arial.cpp"
    INCLUDE_DIRS "${TGX_ROOT}")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
