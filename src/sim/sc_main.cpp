#include "platform/e203_soc.h"
#include "common/types.h"
#include "common/debug_logger.h"
#include "sim/sim_loader.h"
#include "models/memory/sram_ca.h"

using namespace sc_core;
  static bool check_u64(const char* name, uint64_t got, uint64_t expected)
  {
      if (got != expected) {
          std::cout << "[FAIL] " << name
                    << " got=0x" << std::hex << got
                    << " expected=0x" << expected
                    << std::dec << '\n';
          return false;
      }

      std::cout << "[PASS] " << name
                << " value=0x" << std::hex << got << std::dec << '\n';
      return true;
  }

   static bool check_read(sram_ca& sram, uint32_t addr, uint64_t expected, const char* name)
  {
      uint64_t got = 0;

      if (!sram.sram_read(addr, got)) {
          std::cout << "[FAIL] " << name << " read failed\n";
          return false;
      }

      return check_u64(name, got, expected);
  }

  static void test_sram_ca()
  {
      bool ok = true;

      {
          sram_ca sram32(32, 16);

          sram32.sram_write(0x0, 0x11223344, 0b1111);
          ok &= check_read(sram32, 0x0, 0x11223344, "sram32 word write/read");

          sram32.sram_write(0x2, 0xAABB0000, 0b1100);
          ok &= check_read(sram32, 0x0, 0xAABB3344, "sram32 halfword high write");

          sram32.sram_write(0x1, 0x0000CC00, 0b0010);
          ok &= check_read(sram32, 0x0, 0xAABBCC44, "sram32 byte write");
      }
      SIM_ERROR("dtcm_ca", "write accepted");

      {
          sram_ca sram64(64, 16);

          sram64.sram_write(0x0, 0x1122334455667788ULL, 0b11111111);
          ok &= check_read(sram64, 0x0, 0x1122334455667788ULL, "sram64 full lane write/read");

          sram64.sram_write(0x4, 0xAABBCCDD00000000ULL, 0b11110000);
          ok &= check_read(sram64, 0x0, 0xAABBCCDD55667788ULL, "sram64 high word write");

          sram64.sram_write(0x2, 0x00000000EEFF0000ULL, 0b00001100);
          ok &= check_read(sram64, 0x0, 0xAABBCCDDEEFF7788ULL, "sram64 halfword write");

          sram64.sram_write(0x8, 0x0102030405060708ULL, 0b11111111);
          ok &= check_read(sram64, 0x8, 0x0102030405060708ULL, "sram64 next lane write/read");
      }
      std::cout << (ok ? "sram_ca test passed\n" : "sram_ca test failed\n");
  }

int sc_main(int argc, char* argv[])
{
    test_sram_ca();
    // e203sim::sim_loader loader;
    // e203sim::args_option option;
    // loader.parse_args(argc, argv, option);
    // // option.print();
    // // 解析JSON配置
    // e203sim::sim_config cfg;
    // cfg.itcm = new e203sim::memory_config;
    // cfg.dtcm = new e203sim::memory_config;
    // cfg.clint = new e203sim::memory_config;
    // cfg.plic = new e203sim::memory_config;
    // cfg.ppi = new e203sim::memory_config;
    // cfg.fio = new e203sim::memory_config;
    // loader.load_config(option.config_path, cfg);
    // if (cfg.enable_debug) {
    //     e203sim::debug_logger::instance().enable(cfg.debug_path);
    // }
    // // 创建platform
    // e203_soc soc("e203_soc");
    // sc_start();
    return 0;
}
