#include "CNA/CNAException.hpp"

namespace CNA {

    CNAException::CNAException(const char* msg)
        : System::Exception(msg) {
    }

    CNAException::CNAException(const std::string& msg)
        : System::Exception(msg) {
    }

} // CNA