#include "WString.hpp"

String operator+(String a, String const &b) {
    a += b;
    return a;
}