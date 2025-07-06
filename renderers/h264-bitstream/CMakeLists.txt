cmake_minimum_required(VERSION 3.10.0)
aux_source_directory(. h264_bitstream_src)
set(DIR_SRCS ${h264_bitstream_src})
add_library( h264-bitstream
        STATIC
        ${DIR_SRCS})