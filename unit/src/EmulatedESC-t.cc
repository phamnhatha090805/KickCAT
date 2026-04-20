#include <gtest/gtest.h>

#include "kickcat/ESC/EmulatedESC.h"

using namespace kickcat;

TEST(EmulatedESC, init)
{
    EmulatedESC esc;
    ASSERT_EQ(esc.init(), 0);
}

TEST(EmulatedESC, pdi_read_write_registers)
{
    EmulatedESC esc;

    uint64_t const payload = 0xCAFE0000DECA0000;

    // Test that the PDI can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        uint64_t read_test;

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), sizeof(uint64_t));
        ASSERT_NE(read_test, payload);

        ASSERT_EQ(esc.write(address, &payload, sizeof(uint64_t)), sizeof(uint64_t));

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), sizeof(uint64_t));
        ASSERT_EQ(read_test, payload);
    }
}


TEST(EmulatedESC, pdi_read_write_ram_no_sm)
{
    EmulatedESC esc;

    uint64_t const payload = 0xCAFE0000DECA0000;

    // Test that the PDI cannot read/write any address after 0x1000 if no SM initialized
    for (uint16_t address = 0x1000; address < 0xF000; address += sizeof(uint64_t))
    {
        uint64_t read_test = 0;

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -EACCES);
        ASSERT_NE(read_test, payload);

        ASSERT_EQ(esc.write(address, &payload, sizeof(uint64_t)), -EACCES);

        ASSERT_EQ(esc.read(address, &read_test, sizeof(uint64_t)), -EACCES);
        ASSERT_NE(read_test, payload);
    }
}

TEST(EmulatedESC, ecat_aprd_apwr_registers)
{
    EmulatedESC esc;

    uint64_t payload = 0xCAFE0000DECA0000;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        uint64_t read_test = 0;
        uint16_t wkc = 0;

        header.command = Command::APRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_NE(read_test, payload);

        header.command = Command::APWR;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::APRW;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_EQ(read_test, payload+1);

        header.command = Command::APRD;
        header.address = createAddress(0, address);
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(header.address, createAddress(1, address)); // +1 on address position for each ESC processing
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_fprd_fpwr_registers)
{
    EmulatedESC esc;

    uint16_t slave_address = 0x1000;
    uint16_t wkc = 0;
    DatagramHeader header{Command::APWR, 0, 0, sizeof(uint16_t), 0, 0, 0, 0};

    header.address = createAddress(0, reg::STATION_ADDR);
    esc.processDatagram(&header, &slave_address, &wkc);
    ASSERT_EQ(wkc, 1);

    // Test that the ECAT FPxx can read/write any address below 0x1000 when addressed
    // Note that we skip the first 0x0020 area to avoid messing with the station address
    uint64_t payload = 0xCAFE0000DECA0000;
    for (uint16_t address = 0x0020; address < 0x1000; address += sizeof(uint64_t))
    {
        header.address = createAddress(slave_address, address);
        header.len = sizeof(uint64_t);
        uint64_t read_test = 0;
        wkc = 0;

        header.command = Command::FPRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_NE(read_test, payload);

        header.command = Command::FPWR;
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::FPRW;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(read_test, payload);

        header.command = Command::FPRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_apxx_fpxx_not_addressed)
{
    EmulatedESC esc;
    DatagramHeader header{Command::APRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT APxx and FPxx does nothing when the esc is not addressed
    uint16_t const address = reg::SYNC_MANAGER;
    uint64_t read_test = 0;
    uint16_t wkc = 0;
    header.address = createAddress(1, address);

    header.command = Command::APRD;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(2, address));

    header.command = Command::APWR;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(3, address));

    header.command = Command::APRW;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPRD;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPWR;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));

    header.command = Command::FPRW;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 0);
    ASSERT_EQ(header.address, createAddress(4, address));
}

TEST(EmulatedESC, ecat_bxx_registers)
{
    EmulatedESC esc;

    uint64_t payload = 0xCAFE0000DECA0000;
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};

    // Test that the ECAT Bxx can read/write any address below 0x1000
    for (uint16_t address = 0; address < 0x1000; address += sizeof(uint64_t))
    {
        header.address = createAddress(0, address);

        uint64_t read_test = 0;
        uint16_t wkc = 0;

        header.command = Command::BRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 1);
        ASSERT_NE(read_test, payload);

        header.command = Command::BWR;
        esc.processDatagram(&header, &payload, &wkc);
        ASSERT_EQ(wkc, 2);

        read_test = 0xDEAD0000BEEF0000;
        header.command = Command::BRW;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 5);
        ASSERT_EQ(read_test, payload);

        header.command = Command::BRD;
        esc.processDatagram(&header, &read_test, &wkc);
        ASSERT_EQ(wkc, 6);
        ASSERT_EQ(read_test, 0xDEAD0000BEEF0000);
    }
}

TEST(EmulatedESC, ecat_PDOs)
{
    EmulatedESC esc;


    //Note: the PDO conf from PDI is possible here because the emulator do not implement control accesses on a per register basis (yet)
    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    FMMU fmmu;
    memset(&fmmu, 0, sizeof(FMMU));

    fmmu.type  = 2;                // write access
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 10;
    fmmu.logical_start_bit  = 0;   // we map every bits
    fmmu.logical_stop_bit   = 0x7; // we map every bits
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(FMMU));

    fmmu.type  = 1;                 // read access
    fmmu.logical_address    = 0x200A;
    fmmu.length             = 10;
    fmmu.logical_start_bit  = 0;   // we map every bits
    fmmu.logical_stop_bit   = 0x7; // we map every bits
    fmmu.physical_address   = 0x300A;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x10, &fmmu, sizeof(FMMU));

    // run internal logic
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t read_test;
    uint16_t wkc;
    esc.processDatagram(&header, &read_test, &wkc);

    // slave -> master
    uint64_t payload = 0xCAFE0000DECA0000;
    esc.write(0x300A, &payload, sizeof(uint64_t));

    header.command = Command::LRD;
    header.address = 0x200A;
    wkc = 0;
    esc.processDatagram(&header, &read_test, &wkc);
    ASSERT_EQ(wkc, 1);
    ASSERT_EQ(read_test, payload);

    // master -> slave
    payload = 0xDEAD0000BEEF0000;

    header.command = Command::LWR;
    header.address = 0x2000;
    wkc = 0;
    esc.processDatagram(&header, &payload, &wkc);
    ASSERT_EQ(wkc, 1);

    esc.read(0x3000, &read_test, sizeof(uint64_t));
    ASSERT_EQ(read_test, payload);
}

TEST(EmulatedESC, ecat_PDOs_bit_aligned_single_bit)
{
    // Map a single physical bit to a single logical bit through an FMMU.
    // This reproduces the "mailbox status check" pattern used by the master.
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    // Read-access FMMU: physical bit 3 of byte 0x300A -> logical bit 5 of byte 0x2003.
    FMMU fmmu;
    memset(&fmmu, 0, sizeof(FMMU));
    fmmu.type               = 1;
    fmmu.logical_address    = 0x2003;
    fmmu.length             = 1;
    fmmu.logical_start_bit  = 5;
    fmmu.logical_stop_bit   = 5;
    fmmu.physical_address   = 0x300A;
    fmmu.physical_start_bit = 3;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(FMMU));

    // Trigger PDO configuration on PRE_OP -> SAFE_OP transition
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t scratch = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &scratch, &wkc);

    // Set physical bit 3 at 0x300A, other bits off
    uint8_t phys = (1 << 3);
    esc.write(0x300A, &phys, 1);

    // LRD of byte 0x2003: we expect only bit 5 to be set (bit 3 of physical -> bit 5 of logical)
    uint8_t frame = 0xAA; // non-trivial initial content to verify masking
    header.command = Command::LRD;
    header.address = 0x2003;
    header.len = 1;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);
    EXPECT_EQ(frame & (1 << 5), (1 << 5)); // bit 5 is set
    EXPECT_EQ(frame & ~uint8_t(1 << 5), 0xAA & ~uint8_t(1 << 5)); // other bits untouched

    // Clear physical bit, re-read, bit 5 must now be 0 and others untouched
    phys = 0;
    esc.write(0x300A, &phys, 1);
    frame = 0xFF;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);
    EXPECT_EQ(frame & (1 << 5), 0);
    EXPECT_EQ(frame | (1 << 5), 0xFF);
}

TEST(EmulatedESC, ecat_PDOs_bit_aligned_write)
{
    // Master -> slave bit mapping: writing a specific logical bit only updates
    // the mapped physical bit and leaves everything else in the physical byte intact.
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    FMMU fmmu;
    memset(&fmmu, 0, sizeof(FMMU));
    fmmu.type               = 2; // write access (master -> slave)
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 1;
    fmmu.logical_start_bit  = 2;
    fmmu.logical_stop_bit   = 2;
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 6;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(FMMU));

    // Second FMMU (byte-aligned read-back) so the PDI side is allowed to pre-fill
    // the physical byte at 0x3000 and to read it back after the LWR.
    memset(&fmmu, 0, sizeof(FMMU));
    fmmu.type               = 1; // read access (slave -> master)
    fmmu.logical_address    = 0x2100;
    fmmu.length             = 1;
    fmmu.logical_start_bit  = 0;
    fmmu.logical_stop_bit   = 7;
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x10, &fmmu, sizeof(FMMU));

    // Trigger PDO configuration
    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t scratch = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &scratch, &wkc);

    // Pre-fill physical byte with a known pattern to verify only one bit is modified
    uint8_t phys = 0xA5;
    ASSERT_EQ(esc.write(0x3000, &phys, 1), 1);

    // LWR byte with logical bit 2 set
    uint8_t frame = (1 << 2);
    header.command = Command::LWR;
    header.address = 0x2000;
    header.len = 1;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);

    esc.read(0x3000, &phys, 1);
    EXPECT_EQ(phys & (1 << 6), (1 << 6));    // mapped physical bit set
    EXPECT_EQ(phys & ~uint8_t(1 << 6), 0xA5 & ~uint8_t(1 << 6)); // other bits preserved

    // LWR byte with logical bit 2 cleared
    frame = 0;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    ASSERT_EQ(wkc, 1);

    esc.read(0x3000, &phys, 1);
    EXPECT_EQ(phys & (1 << 6), 0);
    EXPECT_EQ(phys | uint8_t(1 << 6), 0xA5 | uint8_t(1 << 6));
}

TEST(EmulatedESC, ecat_PDOs_bit_aligned_cross_byte)
{
    // 4-bit mapping wrapping to the next byte on both sides:
    //   logical  bits: byte 0x2000 bit 6, bit 7; byte 0x2001 bit 0, bit 1
    //   physical bits: byte 0x3000 bit 5, bit 6, bit 7; byte 0x3001 bit 0
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    FMMU fmmu;
    memset(&fmmu, 0, sizeof(FMMU));
    fmmu.type               = 1; // read access (slave -> master)
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 2; // wraps to second byte since stop < start
    fmmu.logical_start_bit  = 6;
    fmmu.logical_stop_bit   = 1;
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 5;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(FMMU));

    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t scratch = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &scratch, &wkc);

    // Set 4 consecutive physical bits starting at byte 0x3000 bit 5
    uint8_t phys[2] = { uint8_t(0xE0), uint8_t(0x01) }; // bits 5,6,7 of byte 0; bit 0 of byte 1
    esc.write(0x3000, phys, 2);

    // LRD two bytes starting at 0x2000; logical bits 6,7 of byte 0 and 0,1 of byte 1 must all be 1
    uint8_t frame[2] = { 0x00, 0x00 };
    header.command = Command::LRD;
    header.address = 0x2000;
    header.len = 2;
    wkc = 0;
    esc.processDatagram(&header, frame, &wkc);
    ASSERT_EQ(wkc, 1);
    EXPECT_EQ(frame[0] & 0xC0, 0xC0); // bits 6,7
    EXPECT_EQ(frame[0] & 0x3F, 0x00); // untouched low bits
    EXPECT_EQ(frame[1] & 0x03, 0x03); // bits 0,1
    EXPECT_EQ(frame[1] & 0xFC, 0x00); // untouched high bits
}

TEST(EmulatedESC, ecat_PDOs_malformed_fmmu_does_not_hang)
{
    // Regression: a malformed FMMU (stop_bit < start_bit and length too small to wrap)
    // would yield a negative bit count; if computed in unsigned arithmetic that wraps
    // to ~4 billion and processBitAlignedPDO never returns. Verify the slave just
    // produces no work for such an FMMU.
    EmulatedESC esc;

    uint8_t current = State::PRE_OP;
    esc.write(reg::AL_STATUS, &current, 1);

    uint8_t next = State::SAFE_OP;
    esc.write(reg::AL_CONTROL, &next, 1);

    FMMU fmmu;
    memset(&fmmu, 0, sizeof(FMMU));
    fmmu.type               = 1;
    fmmu.logical_address    = 0x2000;
    fmmu.length             = 1;
    fmmu.logical_start_bit  = 6;   // start > stop within a single-byte FMMU
    fmmu.logical_stop_bit   = 0;   // → degenerate, total_bits would underflow
    fmmu.physical_address   = 0x3000;
    fmmu.physical_start_bit = 0;
    fmmu.activate           = 1;
    esc.write(reg::FMMU + 0x00, &fmmu, sizeof(FMMU));

    DatagramHeader header{Command::BRD, 0, 0, sizeof(uint64_t), 0, 0, 0, 0};
    uint64_t scratch = 0;
    uint16_t wkc = 0;
    esc.processDatagram(&header, &scratch, &wkc); // PRE_OP -> SAFE_OP triggers configurePDOs

    // LRD on the FMMU's logical range must complete in finite time and write nothing.
    uint8_t frame = 0xAA;
    header.command = Command::LRD;
    header.address = 0x2000;
    header.len = 1;
    wkc = 0;
    esc.processDatagram(&header, &frame, &wkc);
    EXPECT_EQ(wkc, 0);          // no bits were transferred
    EXPECT_EQ(frame, 0xAA);     // frame untouched
}

TEST(EmulatedESC, watchdog)
{
    EmulatedESC esc;
    uint16_t wkc = 0;
    DatagramHeader header{Command::NOP, 0, 0, 0, 0, 0, 0, 0};

    // Configure Watchdog
    // Divider: 100us (2498 + 2) * 40ns = 2500 * 40ns = 100,000ns = 100us
    uint16_t divider = 2498;
    esc.write(reg::WDG_DIVIDER, &divider, 2);

    // Watchdog Time PDO: 100 units of 100us = 10ms
    uint16_t wdg_time = 100;
    esc.write(reg::WDG_TIME_PDO, &wdg_time, 2);

    // Trigger processInternalLogic
    esc.processDatagram(&header, nullptr, &wkc);

    uint16_t status = 0;
    esc.read(reg::WDOG_STATUS, &status, 2);

    // SPEC: Bit 0 of 0x0440 is 1 if OK, 0 if expired.
    EXPECT_EQ(status & 0x01, 1);

    // Advance time beyond 10ms
    // since_epoch() increments by 1ms per call in unit tests.
    for(int i = 0; i < 20; ++i)
    {
        esc.processDatagram(&header, nullptr, &wkc);
    }

    esc.read(reg::WDOG_STATUS, &status, 2);
    EXPECT_EQ(status & 0x01, 0);

    uint8_t counter = 0;
    esc.read(reg::WDOG_COUNTER_PDO, &counter, 1);
    EXPECT_GT(counter, 0);
}

