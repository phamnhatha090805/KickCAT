#include "EniParser.h"
#include <iostream>

namespace kickcat {

bool EniParser::load(const std::string& path) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) return false;

    auto config = doc.FirstChildElement("EtherCATConfig");
    if (!config) return false;

    for (auto* slaveNode = config->FirstChildElement("Slave"); slaveNode; slaveNode = slaveNode->NextSiblingElement("Slave")) {
        EniSlave s;
        auto* info = slaveNode->FirstChildElement("Info");
        if (!info) continue;

        s.name = slaveNode->FirstChildElement("Name")->GetText();
        s.physAddr = std::stoi(info->FirstChildElement("PhysAddr")->GetText());
        s.autoIncAddr = (uint16_t)std::stoi(info->FirstChildElement("AutoIncAddr")->GetText());
        
        s.vendorId = std::stoul(info->FirstChildElement("VendorId")->GetText());
        s.productCode = std::stoul(info->FirstChildElement("ProductCode")->GetText());
        
        slaves_.push_back(s);
    }
    return true;
}

} // namespace kickcat