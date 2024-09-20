#pragma once
#include "../Config.hpp"

#include "Peripheral.hpp"
#include "../Chipset/MMURegion.hpp"

namespace casioemu
{
	class BCDCalc : public Peripheral
	{
		MMURegion region_BCDCMD, region_BCDCON, region_BCDMCN, region_BCDMCR, region_BCDFLG, region_BCDLLZ, region_BCDMLZ,
			region_BCDREGA, region_BCDREGB, region_BCDREGC, region_BCDREGD;

		uint8_t data_BCDCMD, data_BCDCON, data_BCDMCN, data_BCDLLZ, data_BCDMLZ;
		uint8_t BCDREG[4][12];

		uint8_t BCDMCN;
		bool C_flag, Z_flag, macro_running;

		uint8_t BCDCMD_req, BCDMCR_req;
		bool BCDCMD_pend, BCDMCR_pend;

		uint16_t* current_pgm;
		size_t pgm_counter;

		static uint8_t ReadReg(MMURegion* region, size_t offset) {
			offset -= region->base;
			return ((uint8_t*)region->userdata)[offset];
		}

		static void WriteReg(MMURegion* region, size_t offset, uint8_t data) {
			offset -= region->base;
			((uint8_t*)region->userdata)[offset] = data;
		}

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();

		void RunCommand(uint8_t);
		void StartMacro(uint8_t);
		void ShiftLeft(uint8_t, uint8_t, bool);
		void ShiftRight(uint8_t, uint8_t, bool);
	};
}

