#pragma once

#include <stdexcept>
#include <string>

class CameraError : public std::runtime_error
{
public:
    explicit CameraError(const std::string& message)
        : std::runtime_error(message) {}
};
