#ifndef KICKCAT_COE_ESI_PARSER_H
#define KICKCAT_COE_ESI_PARSER_H

#include <tinyxml2.h>
#include <unordered_map>

#include "kickcat/CoE/OD.h"

namespace kickcat::CoE
{
    class EsiParser
    {
    public:
        EsiParser() = default;
        ~EsiParser() = default;

        Dictionary loadFirstDictionaryFromFile  (std::string const& file);
        std::vector<Device> loadDevicesFromFile  (std::string const& file);
        std::vector<Device> loadString(std::string const& xml);

        char const* vendor() const  { return vendor_->FirstChildElement("Name")->GetText();       }
        char const* profile() const { return profile_->FirstChildElement("ProfileNo")->GetText(); }

    private:
    
        template<typename T>
        T toNumber(std::string& field)
        {
            if (field.rfind("#x", 0) == 0)
            {
                field[0] = '0';
            }
            return std::stoi(field, nullptr, 0);
        }

        template<typename T>
        T toNumber(tinyxml2::XMLElement* node)
        {
            std::string field = node->GetText();
            return toNumber<T>(field);
        }

        std::vector<Device> parse(); // main method
        Dictionary loadDictionary();
        std::vector<uint8_t> loadHexBinary(tinyxml2::XMLElement* node);
        std::vector<uint8_t> loadString(tinyxml2::XMLElement* node);

        void loadDefaultData(tinyxml2::XMLNode* node, Object& obj, Entry& entry);

        uint16_t loadAccess(tinyxml2::XMLNode* node);

        std::tuple<DataType, uint16_t, uint16_t> parseType(tinyxml2::XMLNode* node);

        DataType resolveType(std::string const& type_name);

        tinyxml2::XMLNode* findNodeType(tinyxml2::XMLNode* node);

        Object create(tinyxml2::XMLNode* node);

        void loadPdos(Dictionary& dictionary);
        void loadPdo(Dictionary& dictionary, tinyxml2::XMLElement* pdo, bool tx);
        Object* findOrCreateObject(Dictionary& dictionary, uint16_t index, std::string const& name);
        Entry* findOrCreateEntry(Object& object, uint8_t subindex, uint16_t bitlen, uint16_t bitoff,
                                uint16_t access, DataType type, std::string const& description);
        DataType dataTypeFromBitLen(uint16_t bitlen);

        // Manage XML entry point
        tinyxml2::XMLDocument doc_;
        tinyxml2::XMLElement* root_;

        // second level
        tinyxml2::XMLElement* vendor_;
        tinyxml2::XMLElement* desc_;

        // jump on profile and associated dictionnary
        tinyxml2::XMLElement* type_;
        tinyxml2::XMLElement* profile_;
        tinyxml2::XMLElement* devices_;
        tinyxml2::XMLElement* device_;
        tinyxml2::XMLElement* dictionary_;
        tinyxml2::XMLElement* dtypes_;
        tinyxml2::XMLElement* objects_;

        static const std::unordered_map<std::string, DataType> BASIC_TYPES;
        static const std::unordered_map<std::string, uint8_t> SM_CONF;
    };
}

#endif