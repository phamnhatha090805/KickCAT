#ifndef KICKCAT_ENIPARSER_H
#define KICKCAT_ENIPARSER_H

#include <tinyxml2.h>
#include <vector>
#include <string>
#include <cstdint>

namespace kickcat {

struct EniSlave {
    std::string name;
    uint16_t physAddr;
    uint32_t vendorId;
    uint32_t productCode;
    uint32_t revision;
    uint16_t autoIncAddr;
};

class EniParser {
public:
    EniParser() = default;
    bool load(const std::string& path);
    const std::vector<EniSlave>& getSlaves() const { return slaves_; }

private:
    std::vector<EniSlave> slaves_;
};

} // namespace kickcat

#endif