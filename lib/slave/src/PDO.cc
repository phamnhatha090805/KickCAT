#include "kickcat/PDO.h"
#include "kickcat/debug.h"
#include "kickcat/CoE/protocol.h"
#include "protocol.h"
#include <iostream>

#include <cstring>
#include <iostream>
#include <iomanip>

namespace kickcat
{
    int32_t PDO::configure()
    {
        // A slave may have only inputs (e.g. a digital input terminal) or only
        // outputs. findSm throws when a direction's SM is absent, so resolve each
        // direction independently and leave the missing one Unused.
        sm_input_  = SyncManagerConfig{0, 0, 0, 0, SyncManager::Unused};
        sm_output_ = SyncManagerConfig{0, 0, 0, 0, SyncManager::Unused};

        try
        {
            auto [indexIn, pdoIn] = esc_->findSm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
            if (pdoIn.length > 0)
            {
                sm_input_ = SYNC_MANAGER_PI_IN(indexIn, pdoIn.start_address, pdoIn.length);
            }
        }
        catch (std::exception const&)
        {
        }

        try
        {
            auto [indexOut, pdoOut] = esc_->findSm(SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE);
            if (pdoOut.length > 0)
            {
                sm_output_ = SYNC_MANAGER_PI_OUT(indexOut, pdoOut.start_address, pdoOut.length);
            }
        }
        catch (std::exception const&)
        {
        }

        return 0;
    }

    StatusCode PDO::isConfigOk()
    {
        if (sm_input_.type != SyncManager::Unused and not esc_->isSmValid(sm_input_))
        {
            return StatusCode::INVALID_INPUT_CONFIGURATION;
        }
        if (sm_output_.type != SyncManager::Unused and not esc_->isSmValid(sm_output_))
        {
            return StatusCode::INVALID_OUTPUT_CONFIGURATION;
        }

        return StatusCode::ECAT_NO_ERROR;
    }

    void PDO::activateOutput(bool is_activated)
    {
        if (sm_output_.type != SyncManager::Unused)
        {
            esc_->setSmActivate({sm_output_}, is_activated);
        }
    }

    void PDO::activateInput(bool is_activated)
    {
        if (sm_input_.type != SyncManager::Unused)
        {
            esc_->setSmActivate({sm_input_}, is_activated);
        }
    }

    void PDO::setInput(void *buffer, uint32_t size)
    {
        input_ = buffer;
        input_size_ = size;
    }

    void PDO::setOutput(void *buffer, uint32_t size)
    {
        output_ = buffer;
        output_size_ = size;
    }

    void PDO::updateInput()
    {
        if (input_ == nullptr or sm_input_.type == SyncManager::Unused)
        {
            return;
        }

        int32_t written = esc_->write(sm_input_.start_address, input_, sm_input_.length);

        if (written != sm_input_.length)
        {
            slave_error("PDO::updateInput write error\n");
        }
    }

    void PDO::updateOutput()
    {
        if (output_ == nullptr or sm_output_.type == SyncManager::Unused)
        {
            return;
        }

        int32_t read = esc_->read(sm_output_.start_address, output_, sm_output_.length);

        if (read != sm_output_.length)
        {
            slave_error("PDO::updateOutput read error\n");
            return;
        }
    }

    std::vector<uint16_t> PDO::parseAssignment(CoE::Dictionary &dict, uint16_t assign_idx)
    {
        std::vector<uint16_t> pdo_indices;

        auto [obj0, entry0] = CoE::findObject(dict, assign_idx, 0);
        if (entry0)
        {
            uint8_t count = *static_cast<uint8_t *>(entry0->data);

            std::cerr << "[PDO CONFIG] assignment object=0x"
                      << std::hex << assign_idx
                      << " count=" << std::dec << int(count)
                      << std::endl;

            for (uint8_t i = 1; i <= count; ++i)
            {
                auto [obj, entry] = CoE::findObject(dict, assign_idx, i);
                if (entry)
                {
                    uint16_t pdo_index = *static_cast<uint16_t *>(entry->data);

                    std::cerr << "[PDO CONFIG] assignment object=0x"
                              << std::hex << assign_idx
                              << " sub=" << std::dec << int(i)
                              << " value=0x" << std::hex << pdo_index
                              << std::dec << std::endl;

                    pdo_indices.push_back(pdo_index);
                }
            }
        }

        return pdo_indices;
    }

    bool PDO::parsePdoMap(CoE::Dictionary &dict, uint16_t pdo_idx, void *buffer, uint16_t &bit_offset, uint32_t max_size)
    {
        auto [obj0, entry0] = CoE::findObject(dict, pdo_idx, 0);
        if (not entry0)
        {
            std::cerr << "[PDO MAP] cannot find PDO object 0x"
                      << std::hex << pdo_idx << std::dec << std::endl;
            return {};
        }

        uint8_t count = *static_cast<uint8_t *>(entry0->data);

        std::cerr << "[PDO MAP] PDO object=0x"
                  << std::hex << pdo_idx
                  << " count=" << std::dec << int(count)
                  << std::endl;

        for (uint8_t i = 1; i <= count; ++i)
        {
            auto [obj, entry] = CoE::findObject(dict, pdo_idx, i);
            if (not entry)
            {
                return false;
            }

            uint32_t mapping = *static_cast<uint32_t *>(entry->data);

            uint16_t index = static_cast<uint16_t>((mapping & CoE::PDO::MAPPING_INDEX_MASK) >> CoE::PDO::MAPPING_INDEX_SHIFT);
            uint8_t sub = static_cast<uint8_t>((mapping & CoE::PDO::MAPPING_SUB_MASK) >> CoE::PDO::MAPPING_SUB_SHIFT);
            uint8_t bits = static_cast<uint8_t>(mapping & CoE::PDO::MAPPING_LENGTH_MASK);

            std::cerr << "PDO map 0x" << std::hex << pdo_idx
                      << " -> 0x" << index
                      << ":" << std::dec << (int)sub
                      << " bits=" << (int)bits
                      << " bit_offset=" << bit_offset
                      << "\n";

            if (max_size > 0 and static_cast<uint32_t>((bit_offset + bits + 7) / 8) > max_size)
            {
                slave_error("PDO::parsePdoMap mapping size exceeds buffer size\n");
                return false;
            }

            // ETG.1000.6 Tables 74/75: index 0 is a gap (no mapped object).
            if (index == 0)
            {
                bit_offset += bits;
                continue;
            }

            auto [od_obj, od_entry] = CoE::findObject(dict, index, sub);
            if (not od_entry)
            {
                std::cerr << "[PDO MAP] cannot find mapped entry 0x"
                          << std::hex << index
                          << ":" << std::dec << int(sub)
                          << std::endl;
                return false;
            }

            std::cout << "[PDO DEBUG] target OD entry FOUND: 0x"
                        << std::hex << index
                        << ":" << std::dec << int(sub)
                        << " desc='" << od_entry->description << "'"
                        << " bitlen=" << od_entry->bitlen
                        << std::endl;

            // Aliasing logic
            void *old_data = od_entry->data;
            bool old_is_mapped = od_entry->is_mapped;

            uint8_t *new_ptr = static_cast<uint8_t *>(buffer) + (bit_offset / 8);

            od_entry->data = new_ptr;
            od_entry->is_mapped = true; // data has been remapped/aliased

            if (old_data)
            {
                std::memcpy(new_ptr, old_data, (bits + 7) / 8);  // sub-byte entries occupy 1 byte

                if (not old_is_mapped) // if the old data was not mapped, we allocated it, so free it
                {
                    std::free(old_data);
                }
            }

            bit_offset += bits;
        }

        return true;
    }

    StatusCode PDO::configureMapping(CoE::Dictionary &dict)
    {
        printf("PDO configureMapping dict address = %p\n", (void *)&dict);
        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C13);

            std::cerr << "TxPDO assignment 0x1C13:\n";
            for (auto pdo : pdo_indices)
            {
                std::cerr << "  0x" << std::hex << pdo << std::dec << "\n";
            }

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, input_, bit_offset, input_size_))
                {
                    return StatusCode::INVALID_INPUT_CONFIGURATION;
                }
            }
        }

        {
            uint16_t bit_offset = 0;
            std::vector<uint16_t> pdo_indices = parseAssignment(dict, 0x1C12);

            std::cerr << "RxPDO assignment 0x1C12:\n";
            for (auto pdo : pdo_indices)
            {
                std::cerr << "  0x" << std::hex << pdo << std::dec << "\n";
            }

            for (auto pdo : pdo_indices)
            {
                if (not parsePdoMap(dict, pdo, output_, bit_offset, output_size_))
                {
                    return StatusCode::INVALID_OUTPUT_CONFIGURATION;
                }
            }
        }

        return StatusCode::ECAT_NO_ERROR;
    }
}
