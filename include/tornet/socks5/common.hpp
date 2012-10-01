#pragma once
#include <iostream>

#define DEBUG_MESSAGE(X) std::cout << "Debug " << __FILE__ << ':' << __LINE__ << " " << X << std::endl


typedef uint8_t octet_t;
typedef uint16_t port_t;
typedef std::vector<octet_t> byte_array;
