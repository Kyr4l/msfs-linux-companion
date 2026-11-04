# Turns a binary file into a C++ source with its bytes, at build time:
#   cmake -DIN=<file> -DOUT=<file.cpp> -DNAME=<symbol> -P EmbedFile.cmake
# defines `extern const unsigned char NAME[]` and `extern const unsigned int NAME_size` in namespace lr::embedded.
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "// generated from ${IN} by cmake/EmbedFile.cmake\nnamespace lr::embedded {\nextern const unsigned char ${NAME}[] = {${bytes}};\nextern const unsigned int ${NAME}_size = ${size};\n}\n")
