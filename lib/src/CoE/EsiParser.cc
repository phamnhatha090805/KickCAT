#include <algorithm>
#include <stdexcept>
#include "kickcat/debug.h"

#include "kickcat/CoE/EsiParser.h"

using namespace tinyxml2;

namespace kickcat::CoE
{

    const std::unordered_map<std::string, DataType> EsiParser::BASIC_TYPES
    {
        {"BOOL",   DataType::BOOLEAN     },
        {"BYTE",   DataType::BYTE        },
        {"WORD",   DataType::WORD        },
        {"DWORD",  DataType::DWORD       },
        {"SINT",   DataType::INTEGER8    },
        {"INT",    DataType::INTEGER16   },
        {"INT24",  DataType::INTEGER24   },
        {"DINT",   DataType::INTEGER32   },
        {"INT40",  DataType::INTEGER40   },
        {"INT48",  DataType::INTEGER48   },
        {"INT56",  DataType::INTEGER56   },
        {"LINT",   DataType::INTEGER64   },
        {"USINT",  DataType::UNSIGNED8   },
        {"UINT",   DataType::UNSIGNED16  },
        {"UINT24", DataType::UNSIGNED24  },
        {"UDINT",  DataType::UNSIGNED32  },
        {"UINT40", DataType::UNSIGNED40  },
        {"UINT48", DataType::UNSIGNED48  },
        {"UINT56", DataType::UNSIGNED56  },
        {"ULINT",  DataType::UNSIGNED64  },
        {"REAL",   DataType::REAL32      },
        {"LREAL",  DataType::REAL64      },
        {"BIT2",   DataType::BIT2        },
        {"BIT3",   DataType::BIT3        },
        {"BIT4",   DataType::BIT4        },
        {"BIT5",   DataType::BIT5        },
        {"BIT6",   DataType::BIT6        },
        {"BIT7",   DataType::BIT7        },
        {"BIT8",   DataType::BIT8        },
    };

    const std::unordered_map<std::string, uint8_t> EsiParser::SM_CONF
    {
        {"MBoxOut",  1},
        {"MBoxIn",   2},
        {"Outputs",  3},
        {"Inputs",   4},
    };

    Dictionary EsiParser::loadFirstDictionaryFromFile  (std::string const& file) 
    {
        return std::move(loadDevicesFromFile(file).front().dictionary);
    }

    std::vector<Device> EsiParser::loadDevicesFromFile(std::string const& file)
    {
        XMLError result = doc_.LoadFile(file.c_str());
        if (result != XML_SUCCESS)
        {
            throw std::runtime_error(doc_.ErrorIDToName(result));
        }

        return parse();
    }

    std::vector<Device> EsiParser::loadString(std::string const& xml)
    {
        XMLError result = doc_.Parse(xml.c_str());
        if (result != XML_SUCCESS)
        {
            throw std::runtime_error(doc_.ErrorIDToName(result));
        }

        return parse();
    }

    // Helper to find and check a child element, throw if not found
    XMLElement* firstChildElement(XMLNode* node, char const* name)
    {
        auto element = node->FirstChildElement(name);
        if (element == nullptr)
        {
            std::string desc = "Cannot find child element <";
            desc += node->Value();
            desc += "> -> ";
            desc += name;
            throw std::invalid_argument(desc);
        }
        return element;
    };

    std::vector<Device> EsiParser::parse()
    {
        root_ = doc_.RootElement();

        // Position handler on main entry points
        vendor_ = firstChildElement(root_, "Vendor");
        uint32_t vendorId = toNumber<uint32_t>(firstChildElement(vendor_, "Id"));
        desc_   = firstChildElement(root_, "Descriptions");
        devices_ = firstChildElement(desc_, "Devices");

        // Loop through devices to find one with a Profile and Dictionary
        device_ = devices_->FirstChildElement("Device");

       std::vector<Device> devices;

        while (device_)
        {
            type_ = device_->FirstChildElement("Type");
            std::string product_code_str = type_->Attribute("ProductCode");
            std::string revision_number_str = type_->Attribute("RevisionNo");
            uint32_t productCode = toNumber<uint32_t>(product_code_str);
            uint32_t revision_number = toNumber<uint32_t>(revision_number_str);
            
            Device new_device;
            new_device.vendor_id = vendorId;
            new_device.product_code = productCode;
            new_device.revision_number = revision_number;
            printf("Found device with vendor id 0x%08x, product code 0x%08x, revision number 0x%08x\n", new_device.vendor_id, new_device.product_code, new_device.revision_number);
            profile_ = device_->FirstChildElement("Profile");
            dictionary_ = nullptr;
            dtypes_ = nullptr;
            objects_ = nullptr;
            if (profile_)
            {
                dictionary_ = profile_->FirstChildElement("Dictionary");
                if (dictionary_)
                {
                    // If we find the dictionary, set the remaining pointers and break
                    dtypes_  = dictionary_->FirstChildElement("DataTypes");
                    objects_ = dictionary_->FirstChildElement("Objects");
                }
            }
            new_device.dictionary = loadDictionary();
            devices.push_back(std::move(new_device));
            // Move to the next <Device> in the XML file (e.g., skip EL1002)
            device_ = device_->NextSiblingElement("Device");
        }

        return devices;
    }

    Dictionary EsiParser::loadDictionary() 
    {
        Dictionary dictionary;

        if (objects_) 
        {
            auto node_object = objects_->FirstChildElement();
            // loop over dictionary
            while (node_object)
            {
                CoE::Object obj = create(node_object);
                dictionary.push_back(std::move(obj));
                node_object = node_object->NextSiblingElement();
            }
        }

        loadPdos(dictionary);

        // load sync managers type object
        CoE::Object sms_type;
        sms_type.index = 0x1c00;
        sms_type.code = ObjectCode::ARRAY;
        sms_type.name = "Sync manager type";

        // create first entry (array size)
        sms_type.entries.push_back(CoE::Entry{0, 8, 0, Access::READ, DataType::UNSIGNED8, "Subindex 0"});

        auto sm = device_->FirstChildElement("Sm");
        while (sm)
        {
            CoE::Entry entry;
            entry.subindex = sms_type.entries.size();
            entry.access = Access::READ;
            entry.bitlen = 8;
            entry.bitoff = sms_type.entries.size() * 8 + 8; // + 8 for padding of the first entry
            entry.description = "Subindex " + std::to_string(sms_type.entries.size());
            entry.type = DataType::UNSIGNED8;
            entry.data = malloc(1);

            uint8_t sm_type = SM_CONF.at(sm->GetText());
            std::memcpy(entry.data, &sm_type, 1);

            sms_type.entries.push_back(std::move(entry));
            sm = sm->NextSiblingElement("Sm");
        }
        auto& subindex0 = sms_type.entries.at(0);
        subindex0.data = malloc(1);
        uint8_t array_size = sms_type.entries.size() - 1;
        std::memcpy(subindex0.data, &array_size, 1);
        dictionary.push_back(std::move(sms_type));

        return dictionary;
    }

    std::vector<uint8_t> EsiParser::loadHexBinary(XMLElement* node)
    {
        std::string field = node->GetText();
        std::vector<uint8_t> data;
        data.reserve(field.size() / 2); // 2 ascii character for one byte

        // Extract hex, data is already LE
        for (std::size_t i = 0; i < field.size(); i += 2)
        {
            std::string hex = field.substr(i, 2);
            uint8_t byte = std::stoi(hex, nullptr, 16);
            data.push_back(byte);
        }

        return data;
    }

    std::vector<uint8_t> EsiParser::loadString(XMLElement* node)
    {
        auto data = loadHexBinary(node);
        std::reverse(data.begin(), data.end());
        return data;
    }

    void EsiParser::loadDefaultData(XMLNode* node, Object& obj, Entry& entry)
    {
        auto node_info = node->FirstChildElement("Info");
        if (node_info == nullptr)
        {
            return;
        }

        auto node_default_data = node_info->FirstChildElement("DefaultData");
        if (node_default_data != nullptr)
        {
            std::vector<uint8_t> data;
            if(entry.type == DataType::VISIBLE_STRING)
            {
                data = loadString(node_default_data);
            }
            else
            {
                data = loadHexBinary(node_default_data);
            }

            if (data.size() != (entry.bitlen / 8))
            {
                esi_warning("Cannot load default data for 0x%04x.%d, expected size mismatch.\n"
                        "-> Got %ld bits, expected: %d bit\n"
                        "==> Skipping entry\n",
                    obj.index, entry.subindex,
                    data.size() * 8, entry.bitlen);
                return;
            }
            entry.data = malloc(entry.bitlen / 8);
            std::memcpy(entry.data, data.data(), data.size());
            return;
        }

        auto node_default_value = node_info->FirstChildElement("DefaultValue");
        if (node_default_value != nullptr)
        {
            std::string text = node_default_value->GetText();
            int64_t value;
            if (text.rfind("#x", 0) == 0)
            {
                text[0] = '0';
                value = std::stoll(text, nullptr, 16);
            }
            else
            {
                value = std::stoll(text, nullptr, 10);
            }

            uint32_t size = entry.bitlen / 8;
            entry.data = malloc(size);
            std::memcpy(entry.data, &value, size);
        }
    }

    uint16_t EsiParser::loadAccess(XMLNode* node)
    {
        uint16_t flags = 0;

        auto node_flags = node->FirstChildElement("Flags");
        if (node_flags == nullptr)
        {
            return flags;
        }

        auto node_access = node_flags->FirstChildElement("Access");
        if (node_access != nullptr)
        {
            std::string access = node_access->GetText();

            // global read rule
            if (access == "rw" or access == "ro")
            {
                flags |= Access::READ;
            }

            // global write rule
            if (access == "rw" or access == "wo")
            {
                flags |= Access::WRITE;
            }

            auto parseRestrictions = [](char const* raw_restrictions) -> uint16_t
            {
                if (raw_restrictions == nullptr)
                {
                    return Access::READ;
                }

                // lower the string
                std::string restrictions{raw_restrictions};
                std::transform(restrictions.begin(), restrictions.end(), restrictions.begin(),
                    [](char c){ return std::tolower(c); });

                uint16_t result = 0;
                if (restrictions.find("preop")  != std::string::npos) { result |= Access::READ_PREOP;  }
                if (restrictions.find("safeop") != std::string::npos) { result |= Access::READ_SAFEOP; }
                if (restrictions.find("_op")    != std::string::npos) { result |= Access::READ_OP;     }
                if (restrictions.find("op") == 0)                     { result |= Access::READ_OP;     }

                return result;
            };

            // restrictions
            uint16_t restrictions_mask = 0;
            restrictions_mask |= (parseRestrictions(node_access->Attribute("ReadRestrictions"))  << 0);
            restrictions_mask |= (parseRestrictions(node_access->Attribute("WriteRestrictions")) << 3);

            flags &= restrictions_mask;
        }
        else
        {
            flags |= Access::READ; // Default value
        }

        auto node_pdo_mapping = node_flags->FirstChildElement("PdoMapping");
        if (node_pdo_mapping != nullptr)
        {
            std::string mapping = node_pdo_mapping->GetText();
            for (auto const& c : mapping)
            {
                if (std::tolower(c) == 'r') { flags |= Access::RxPDO; }
                if (std::tolower(c) == 't') { flags |= Access::TxPDO; }
            }
        }

        auto node_backup = node_flags->FirstChildElement("Backup");
        if (node_backup != nullptr)
        {
            if (node_backup->GetText()[0] == '1') { flags |= Access::BACKUP; }
        }

        auto node_setting = node_flags->FirstChildElement("Setting");
        if (node_setting != nullptr)
        {
            if (node_setting->GetText()[0] == '1') { flags |= Access::SETTING; }
        }

        return flags;
    }

    DataType EsiParser::resolveType(std::string const& type_name)
    {
        auto it = BASIC_TYPES.find(type_name);
        if (it != BASIC_TYPES.end())
        {
            return it->second;
        }

        if (type_name.find("STRING") != std::string::npos)
        {
            return DataType::VISIBLE_STRING;
        }

        auto dtype = dtypes_->FirstChildElement();
        while (dtype)
        {
            auto name_elem = dtype->FirstChildElement("Name");
            if (name_elem and type_name == name_elem->GetText())
            {
                if (dtype->FirstChildElement("SubItem") or dtype->FirstChildElement("ArrayInfo"))
                {
                    return DataType::UNKNOWN;
                }

                auto base = dtype->FirstChildElement("BaseType");
                if (base)
                {
                    return resolveType(base->GetText());
                }

                break;
            }
            dtype = dtype->NextSiblingElement();
        }

        return DataType::UNKNOWN;
    }

    std::tuple<DataType, uint16_t, uint16_t> EsiParser::parseType(XMLNode* node)
    {
        auto node_type = node->FirstChildElement("Type");
        if (not node_type)
        {
            node_type = node->FirstChildElement("BaseType");
        }

        if (not node_type)
        {
            return {DataType::UNKNOWN, 0, 0};
        }

        DataType type = resolveType(node_type->GetText());
        if (type == DataType::UNKNOWN)
        {
            return {DataType::UNKNOWN, 0, 0};
        }

        uint16_t bitlen = toNumber<uint16_t>(node->FirstChildElement("BitSize"));
        uint16_t bitoff = 0;
        auto node_bitoff = node->FirstChildElement("BitOffs");
        if (node_bitoff)
        {
            bitoff = toNumber<uint16_t>(node_bitoff);
        }

        return {type, bitlen, bitoff};
    }


    XMLNode* EsiParser::findNodeType(XMLNode* node)
    {
        std::string raw_type = node->FirstChildElement("Type")->GetText();

        auto dtype = dtypes_->FirstChildElement();
        while (dtype)
        {
            if (raw_type == dtype->FirstChildElement("Name")->GetText())
            {
                break;
            }
            dtype = dtype->NextSiblingElement();
        }

        return dtype;
    }

    Object EsiParser::create(XMLNode* node)
    {
        Object object;
        object.index = toNumber<uint16_t>(node->FirstChildElement("Index"));
        object.name  = node->FirstChildElement("Name")->GetText();
        auto [type, bitlen, bitoff] = parseType(node);
        if (isBasic(type))
        {
            // Basic type: no subindex in the ESI file because it is defined directly in the object node.
            object.code = ObjectCode::VAR;
            object.entries.resize(1);
            auto& entry = object.entries.at(0);
            entry.subindex = 0;
            entry.bitlen = bitlen;
            entry.bitoff = bitoff;
            entry.type = type;
            entry.access = loadAccess(node);

            loadDefaultData(node, object, entry);

            return object;
        }

        auto node_type = findNodeType(node);
        auto node_subitem = node_type->FirstChildElement("SubItem");
        while (node_subitem)
        {
            Entry entry;
            auto node_name = node_subitem->FirstChildElement("Name");
            if (node_name)
            {
                entry.description = node_name->GetText();
            }

            auto [subitem_type, subitem_bitlen, subitem_bitoff] = parseType(node_subitem);
            if (isBasic(subitem_type))
            {
                object.code  = ObjectCode::RECORD;

                entry.type   = subitem_type;
                entry.bitlen = subitem_bitlen;
                entry.bitoff = subitem_bitoff;
                entry.subindex = toNumber<uint8_t>(node_subitem->FirstChildElement("SubIdx"));
                entry.access = loadAccess(node_subitem);

                object.entries.push_back(std::move(entry));
            }
            else
            {
                object.code = ObjectCode::ARRAY;

                auto node_array_type = findNodeType(node_subitem);
                auto [array_type, array_bitlen, array_bitoff] = parseType(node_array_type);
                entry.type   = array_type;
                entry.bitlen = array_bitlen;
                entry.bitoff = array_bitoff;
                entry.access = loadAccess(node_subitem);

                auto node_array_info = node_array_type->FirstChildElement("ArrayInfo");
                uint8_t lbound = toNumber<uint8_t>(node_array_info->FirstChildElement("LBound"));
                if (lbound == 0)
                {
                    // one big entry which is an array:
                    // - bitlen shall be updated accordingly
                    // - elements cannot be used because it represents the internal elements, not the elements accessible
                    //   through subindex
                    entry.subindex = 1;
                    entry.bitlen = toNumber<uint16_t>(node_array_type->FirstChildElement("BitSize"));
                    object.entries.push_back(std::move(entry));
                }
                else
                {
                    // array entries are the subindex starting from 1, 0 is the array size
                    uint8_t elements = toNumber<uint8_t>(node_array_info->FirstChildElement("Elements"));
                    uint16_t element_bitlen = toNumber<uint16_t>(node_subitem->FirstChildElement("BitSize")) / elements;
                    uint16_t element_bitoff = toNumber<uint16_t>(node_subitem->FirstChildElement("BitOffs"));

                    for (uint8_t i = 1; i <= elements; ++i)
                    {
                        entry.bitlen = element_bitlen;
                        entry.bitoff = element_bitoff + element_bitlen * (i - 1);
                        entry.subindex = i;
                        object.entries.push_back(std::move(entry));
                    }
                }
            }

            node_subitem = node_subitem->NextSiblingElement("SubItem");
        }


        // Set default data value
        // Update name if possible by using the object node
        auto node_info = node->FirstChildElement("Info");
        if (node_info == nullptr)
        {
            return object;
        }
        auto object_subitem = node_info->FirstChildElement("SubItem");
        auto entry = object.entries.begin();
        for (auto& object_entry : object.entries)
        {
            if (object_subitem)
            {
                auto object_subitem_name = object_subitem->FirstChildElement("Name");
                if (object_subitem_name)
                {
                    object_entry.description = object_subitem_name->GetText();
                }

                loadDefaultData(object_subitem, object, *entry);

                entry++;
                object_subitem = object_subitem->NextSiblingElement();
            }
        }
        return object;
    }

    DataType EsiParser::dataTypeFromBitLen(uint16_t bitlen)
    {
        switch (bitlen)
        {
            case 1:  return DataType::BOOLEAN;
            case 2:  return DataType::BIT2;
            case 3:  return DataType::BIT3;
            case 4:  return DataType::BIT4;
            case 5:  return DataType::BIT5;
            case 6:  return DataType::BIT6;
            case 7:  return DataType::BIT7;
            case 8:  return DataType::UNSIGNED8;
            case 16: return DataType::UNSIGNED16;
            case 32: return DataType::UNSIGNED32;
            case 64: return DataType::UNSIGNED64;
            default: return DataType::UNKNOWN;
        }
    }

    Object* EsiParser::findOrCreateObject(Dictionary& dictionary, uint16_t index, std::string const& name)
    {
        for (auto& object : dictionary)
        {
            if (object.index == index)
            {
                if (object.name.empty())
                {
                    object.name = name;
                }
                return &object;
            }
        }

        Object object;
        object.index = index;
        object.code = ObjectCode::RECORD;
        object.name = name.empty() ? ("Object 0x" + std::to_string(index)) : name;

        dictionary.push_back(std::move(object));
        return &dictionary.back();
    }

    Entry* EsiParser::findOrCreateEntry(Object& object, uint8_t subindex, uint16_t bitlen, uint16_t bitoff,
                                        uint16_t access, DataType type, std::string const& description)
    {
        for (auto& entry : object.entries)
        {
            if (entry.subindex == subindex)
            {
                if (entry.description.empty() && !description.empty())
                {
                    entry.description = description;
                }

                if (entry.bitlen == 0)
                {
                    entry.bitlen = bitlen;
                }

                entry.access |= access;
                return &entry;
            }
        }

        Entry entry;
        entry.subindex = subindex;
        entry.bitlen = bitlen;
        entry.bitoff = bitoff;
        entry.access = access;
        entry.type = type;
        entry.description = description.empty() ? ("SubIndex " + std::to_string(subindex)) : description;

        object.entries.push_back(std::move(entry));
        return &object.entries.back();
    }

    void EsiParser::loadPdos(Dictionary& dictionary)
    {
        for (auto tx = device_->FirstChildElement("TxPdo"); tx; tx = tx->NextSiblingElement("TxPdo"))
        {
            loadPdo(dictionary, tx, true);
        }

        for (auto rx = device_->FirstChildElement("RxPdo"); rx; rx = rx->NextSiblingElement("RxPdo"))
        {
            loadPdo(dictionary, rx, false);
        }
    }

    void EsiParser::loadPdo(Dictionary& dictionary, XMLElement* pdo, bool tx)
    {
        auto pdo_index_node = pdo->FirstChildElement("Index");
        if (!pdo_index_node || !pdo_index_node->GetText())
        {
            return;
        }

        uint16_t pdo_index = toNumber<uint16_t>(pdo_index_node);

        std::string pdo_name = tx ? "TxPDO" : "RxPDO";
        if (auto name = pdo->FirstChildElement("Name"); name && name->GetText())
        {
            pdo_name = name->GetText();
        }

        // Create PDO map object.
        findOrCreateObject(dictionary, pdo_index, pdo_name);

        uint8_t count = 0;
        uint16_t pdo_map_bitoff = 16;
        uint16_t process_bitoff = 0;

        for (auto entry_node = pdo->FirstChildElement("Entry");
            entry_node;
            entry_node = entry_node->NextSiblingElement("Entry"))
        {
            auto index_node = entry_node->FirstChildElement("Index");
            auto sub_node   = entry_node->FirstChildElement("SubIndex");
            auto bit_node   = entry_node->FirstChildElement("BitLen");

            if (!index_node || !sub_node || !bit_node ||
                !index_node->GetText() || !sub_node->GetText() || !bit_node->GetText())
            {
                continue;
            }

            uint16_t index = toNumber<uint16_t>(index_node);
            uint8_t subindex = toNumber<uint8_t>(sub_node);
            uint16_t bitlen = toNumber<uint16_t>(bit_node);

            std::string entry_name;
            if (auto name = entry_node->FirstChildElement("Name"); name && name->GetText())
            {
                entry_name = name->GetText();
            }

            uint32_t mapping_value =
                (static_cast<uint32_t>(index) << 16) |
                (static_cast<uint32_t>(subindex) << 8) |
                static_cast<uint32_t>(bitlen);

            ++count;

            // Re-find PDO object every time. Do not reuse old pointer.
            Object* pdo_object = findOrCreateObject(dictionary, pdo_index, pdo_name);

            Entry* map_entry = findOrCreateEntry(
                *pdo_object,
                count,
                32,
                pdo_map_bitoff,
                Access::READ,
                DataType::PDO_MAPPING,
                "SubIndex " + std::to_string(count)
            );

            if (!map_entry->data)
            {
                map_entry->data = malloc(sizeof(uint32_t));
            }

            std::memcpy(map_entry->data, &mapping_value, sizeof(uint32_t));
            map_entry->bitlen = 32;
            map_entry->bitoff = pdo_map_bitoff;
            map_entry->type = DataType::PDO_MAPPING;

            pdo_map_bitoff += 32;

            if (index != 0x0000 || subindex != 0)
            {
                Object* real_object = findOrCreateObject(dictionary, index, pdo_name);

                findOrCreateEntry(
                    *real_object,
                    subindex,
                    bitlen,
                    process_bitoff,
                    Access::READ | (tx ? Access::TxPDO : Access::RxPDO),
                    dataTypeFromBitLen(bitlen),
                    entry_name
                );
            }

            process_bitoff += bitlen;
        }

        // Re-find PDO object again at the end.
        Object* pdo_object = findOrCreateObject(dictionary, pdo_index, pdo_name);

        Entry* sub0 = findOrCreateEntry(
            *pdo_object,
            0,
            8,
            0,
            Access::READ,
            DataType::UNSIGNED8,
            "SubIndex 000"
        );

        if (!sub0->data)
        {
            sub0->data = malloc(sizeof(uint8_t));
        }

        std::memcpy(sub0->data, &count, sizeof(uint8_t));
        sub0->bitlen = 8;
        sub0->type = DataType::UNSIGNED8;
    }
}
