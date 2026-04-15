####
# F Prime CMakeLists for RadiationPayload
####

register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/RadiationPayload.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/RadiationPayload.cpp"
)