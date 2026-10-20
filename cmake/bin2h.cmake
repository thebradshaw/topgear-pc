# cmake -DIN=file -DOUT=header.h -DNAME=kSymbol -P bin2h.cmake: a file as a C byte array.
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n  " bytes "${bytes}")
file(WRITE "${OUT}" "// Generated from ${IN} by cmake/bin2h.cmake.\nstatic const unsigned char ${NAME}[] = {\n  ${bytes}\n};\n")
