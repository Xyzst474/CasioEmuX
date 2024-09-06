#pragma once
#include "../Config.hpp"

#include "Peripheral.hpp"
#include "../Chipset/MMURegion.hpp"

namespace casioemu
{
	class TimerBaseCounter : public Peripheral
	{
		size_t L256SINT = 5;
        size_t L1024SINT = 6;
        size_t L4096SINT = 7;
        size_t L16384SINT = 8;

        uint8_t current_output;
        bool LTBR_reset_tick;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();
        void ResetLSCLK();
	};
}
