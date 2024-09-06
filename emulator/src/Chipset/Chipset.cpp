#include "Chipset.hpp"

#include "../Data/HardwareId.hpp"
#include "../Emulator.hpp"
#include "../Logger.hpp"
#include "CPU.hpp"
#include "Coprocessor.hpp"
#include "MMU.hpp"
#include "InterruptSource.hpp"

#include "../Peripheral/ROMWindow.hpp"
#include "../Peripheral/BatteryBackedRAM.hpp"
#include "../Peripheral/Screen.hpp"
#include "../Peripheral/Keyboard.hpp"
#include "../Peripheral/StandbyControl.hpp"
#include "../Peripheral/Miscellaneous.hpp"
#include "../Peripheral/Timer.hpp"
#include "../Peripheral/BCDCalc.hpp"
#include "../Peripheral/PowerSupply.hpp"
#include "../Peripheral/TimerBaseCounter.hpp"
#include "../Peripheral/RealTimeClock.hpp"
#include "../Peripheral/WatchdogTimer.hpp"
#include "../Peripheral/ExternalInterrupts.hpp"
#include "../Peripheral/IOPorts.hpp"
#include "../Peripheral/Flash.hpp"

#include "../Gui/ui.hpp"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace casioemu
{
	Chipset::Chipset(Emulator &_emulator) : emulator(_emulator), cpu(*new CPU(emulator)), coprocessor(*new Coprocessor(emulator)), mmu(*new MMU(emulator))
	{
	}

	void Chipset::Setup()
	{
		for (size_t ix = 0; ix != INT_COUNT; ++ix)
			interrupts_active[ix] = false;
		pending_interrupt_count = 0;

		cpu.SetMemoryModel(CPU::MM_LARGE);
		cpu.SetCPUModel(emulator.hardware_id == HW_CLASSWIZ || emulator.hardware_id == HW_CLASSWIZ_II ? CPU::CM_NX_U16 : CPU::CM_NX_U8);

		//Add the segment index here if you want to register a MMURegion in a certain data segment; Read from unregistered data segments will return 0.
		std::initializer_list<int> segments_es_plus{ 0, 1, 8 }, segments_classwiz{ 0, 1, 2, 3, 4, 5, 31 },
		                           segments_classwiz_ii{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 31 },
								   segments_fx_5800p{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
		for (auto segment_index : emulator.hardware_id == HW_ES_PLUS ? segments_es_plus : emulator.hardware_id == HW_CLASSWIZ ? segments_classwiz :
		                          emulator.hardware_id == HW_CLASSWIZ_II ? segments_classwiz_ii : segments_fx_5800p)
			mmu.GenerateSegmentDispatch(segment_index);

		real_hardware = emulator.GetModelInfo("real_hardware");

		ConstructPeripherals();
	}

	Chipset::~Chipset()
	{
		DestructPeripherals();
		DestructClockGenerator();
		DestructInterruptSFR();

		delete &mmu;
		delete &cpu;
	}

	void Chipset::ConstructInterruptSFR()
	{
		EffectiveMICount = emulator.hardware_id == HW_ES_PLUS ? 12 : emulator.hardware_id == HW_CLASSWIZ ? 17 : emulator.hardware_id == HW_CLASSWIZ_II ? 21 : 10;
		MaskableInterrupts = new InterruptSource[EffectiveMICount];
		for(size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].Setup(i + INT_MASKABLE, emulator);
		}

		//WDTINT is unused
		region_int_mask.Setup(0xF010, 4, "Chipset/InterruptMask", this, [](MMURegion *region, size_t offset) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			return (uint8_t)((chipset->data_int_mask >> (offset * 8)) & 0xFF);
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			//size_t mask = (1 << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
			size_t mask = (1 << (chipset->EffectiveMICount + 1)) - 2;
			chipset->data_int_mask = (chipset->data_int_mask & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
			chipset->data_int_mask &= mask;
			for(size_t i = 0; i < chipset->EffectiveMICount; i++) {
				chipset->MaskableInterrupts[i].SetEnabled(chipset->data_int_mask & (1 << (i + 1)));
			}
			/*if(chipset->data_int_mask & 1) {
				if(chipset->GetInterruptPendingSFR(4))
					chipset->RaiseNonmaskable();
			} else {
				chipset->ResetNonmaskable();
			}*/
		}, emulator);

		region_int_pending.Setup(0xF014, 4, "Chipset/InterruptPending", this, [](MMURegion *region, size_t offset) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			return (uint8_t)((chipset->data_int_pending >> (offset * 8)) & 0xFF);
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			//size_t mask = (1 << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
			size_t mask = (1 << (chipset->EffectiveMICount + 1)) - 1;
			chipset->data_int_pending = (chipset->data_int_pending & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
			chipset->data_int_pending &= mask;
			for(size_t i = 0; i < chipset->EffectiveMICount; i++) {
				if(chipset->data_int_pending & (1 << (i + 1)))
					chipset->MaskableInterrupts[i].TryRaise();
				else
					chipset->MaskableInterrupts[i].ResetInt();
			}
			if(chipset->data_int_pending & 1) {
				/*if(chipset->data_int_mask & 1)
					chipset->RaiseNonmaskable();*/
				chipset->RaiseNonmaskable();
			} else {
				chipset->ResetNonmaskable();
			}
		}, emulator);
	}

	void Chipset::ResetInterruptSFR() {
		data_int_mask = 0;
		data_int_pending = 0;
		for(size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].SetEnabled(false);
			MaskableInterrupts[i].ResetInt();
		}
		ResetNonmaskable();
	}

	void Chipset::DestructInterruptSFR()
	{
		region_int_pending.Kill();
		region_int_mask.Kill();
	}

	void Chipset::ConstructClockGenerator() {
		LSCLKFreq = 16384;

		ResetClockGenerator();

		region_FCON.Setup(0xF00A, 1, "ClockGenerator/FCON", this, [](MMURegion *region, size_t) {
			Chipset* chipset = (Chipset*)region->userdata;
			return chipset->data_FCON;
		}, [](MMURegion *region, size_t, uint8_t data) {
			Chipset* chipset = (Chipset*)region->userdata;
			uint8_t OSCLK = (data & 0x70) >> 4;
			chipset->data_FCON = data & 0x73;
			chipset->ClockDiv = std::pow(2, OSCLK == 0 ? OSCLK : OSCLK - 1);
			chipset->LSCLKMode = (chipset->data_FCON & 0x03) == 1 ? true : false;
		}, emulator);
		region_LTBR.Setup(0xF00C, 1, "TimerBaseCounter/LTBR", this, [](MMURegion *region, size_t) {
			Chipset* chipset = (Chipset*)region->userdata;
			return chipset->data_LTBR;
		}, [](MMURegion *region, size_t, uint8_t data) {
			Chipset* chipset = (Chipset*)region->userdata;
			chipset->data_LTBR = 0;
			chipset->LTBCReset = true;
			chipset->LSCLKTick = true;
			chipset->LSCLKTickCounter = 0;
			chipset->LSCLKTimeCounter = 0;
			chipset->LSCLKFreqAddition = 0;
		}, emulator);
		region_HTBR.Setup(0xF00D, 1, "ClockGenerator/HTBR", this, [](MMURegion *region, size_t) {
			Chipset* chipset = (Chipset*)region->userdata;
			return chipset->data_HTBR;
		}, [](MMURegion *region, size_t, uint8_t data) {
			Chipset* chipset = (Chipset*)region->userdata;
			chipset->data_HTBR = 0;
			chipset->HSCLK_output = 0xFF;
			chipset->HTBCReset = true;
			chipset->HSCLKTick = true;
			chipset->HSCLKTickCounter = 0;
		}, emulator);
		region_LTBADJ.Setup(0xF006, 2, "TimerBaseCounter/LTBADJ", this, [](MMURegion *region, size_t offset) {
			Chipset* chipset = (Chipset*)region->userdata;
			offset -= region->base;
			return (uint8_t)((chipset->data_LTBADJ & 0x7FF) >> offset * 8);
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			Chipset* chipset = (Chipset*)region->userdata;
			offset -= region->base;
			chipset->data_LTBADJ = (chipset->data_LTBADJ & (~(0xFF << offset * 8))) | (data << offset * 8);
			chipset->data_LTBADJ &= 0x7FF;
			if(chipset->data_LTBADJ != 0)
				chipset->LSCLKThresh = (chipset->LSCLKFreq * (1 + 2097152 / (short)chipset->data_LTBADJ)) / chipset->emulator.GetCyclesPerSecond();
			else
				chipset->LSCLKThresh = 0;
		}, emulator);
	}

	void Chipset::GenerateTickForClock() {
		//Generate HSCLK Tick
		if(run_mode != RM_STOP) {
			if(++HSCLKTickCounter >= ClockDiv) {
				HSCLKTick = true;
				HSCLKTickCounter = 0;
				if(HTBCReset) {
					HTBCReset = false;
				} else {
					HSCLK_output = 0;
					if(++HSCLKTimeCounter >= HTBROutputCount) {
						data_HTBR++;
						HSCLK_output = (data_HTBR - 1) & (~data_HTBR);
						HSCLKTimeCounter = 0;
					}
				}
			}
		}

		//Generate LSCLK Tick
		if(LSCLKMode) {
			if(++LSCLKTickCounter >= emulator.GetCyclesPerSecond() / LSCLKFreq + LSCLKFreqAddition) {
				LSCLKTick = true;
				LSCLKTickCounter = 0;
				if(LSCLKFreqAddition != 0) {
					LSCLKFreqAddition = 0;
					LSCLKTimeCounter = 0;
				}
				if(LSCLKThresh > 0) {
					if(++LSCLKTimeCounter >= LSCLKThresh)
						LSCLKFreqAddition = 1;
				} else if(LSCLKThresh < 0) {
					if(++LSCLKTimeCounter >= -LSCLKThresh)
						LSCLKFreqAddition = -1;
				}
			}
		}
	}

	void Chipset::ResetClockGenerator() {
		data_FCON = 0;
		data_LTBR = 0;
		LTBC_H = 0;
		data_HTBR = 0;
		LSCLK_output = 0;
		LSCLK_output_H = 0;
		HSCLK_output = 0;
		data_LTBADJ = 0;

		ClockDiv = 1;
		LSCLKMode = false;
		
		LSCLKTick = false;
		HSCLKTick = false;
		LTBCReset = false;
		HTBCReset = false;

		LSCLKTickCounter = 0;
		LSCLKTimeCounter = 0;
		LSCLKFreqAddition = 0;
		LSCLKThresh = 0;
		HSCLKTickCounter = 0;
		HSCLKTimeCounter = 0;
	}

	void Chipset::DestructClockGenerator() {
		region_FCON.Kill();
		region_LTBR.Kill();
		region_HTBR.Kill();
		region_LTBADJ.Kill();
	}

	void Chipset::ConstructPeripherals()
	{
		//Only tested on fx-991cnx
		BLKCON_mask = emulator.hardware_id == HW_CLASSWIZ ? 0x1F : 0xFF;
		region_BLKCON.Setup(0xF028, 1, "Chipset/BLKCON0", this, [](MMURegion* region, size_t) {
			Chipset* chipset = (Chipset*)region->userdata;
			return (uint8_t)(chipset->data_BLKCON & chipset->BLKCON_mask);
		}, [](MMURegion* region, size_t, uint8_t data) {
			Chipset* chipset = (Chipset*)region->userdata;
			data &= chipset->BLKCON_mask;
			chipset->data_BLKCON = data;
			for(auto peripheral : chipset->peripherals) {
				int block_bit = peripheral->GetBlockBit();
				if(block_bit == -1)
					continue;
				if((1 << block_bit) > chipset->BLKCON_mask)
					PANIC("Invalid BLKCON0 bit %d\n", block_bit);
				if(data & (1 << block_bit))
					peripheral->Uninitialise();
				else
					peripheral->Initialise();
			}
		}, emulator);

		flash = new Flash(emulator);

		ioport = new IOPorts(emulator);
		EXIhandle = new ExternalInterrupts(emulator);

		peripherals.push_front(new ROMWindow(emulator));
		peripherals.push_front(new BatteryBackedRAM(emulator));
		peripherals.push_front(CreateScreen(emulator));
		peripherals.push_front(ioport);
		peripherals.push_front(EXIhandle);
		peripherals.push_front(new Keyboard(emulator));
		peripherals.push_front(new StandbyControl(emulator));
		peripherals.push_front(new Miscellaneous(emulator));
		peripherals.push_front(new Timer(emulator));
		peripherals.push_front(new PowerSupply(emulator));
		peripherals.push_front(new TimerBaseCounter(emulator));
		peripherals.push_front(new RealTimeClock(emulator));
		peripherals.push_front(new WatchdogTimer(emulator));
		if (emulator.hardware_id == HW_CLASSWIZ_II)
			peripherals.push_front(new BCDCalc(emulator));
		if (emulator.hardware_id != HW_ES_PLUS)
			peripherals.push_front(flash);
	}

	void Chipset::DestructPeripherals()
	{
		region_BLKCON.Kill();

		for (auto &peripheral : peripherals)
		{
			peripheral->Uninitialise();
			delete peripheral;
		}
	}

	void Chipset::SetupInternals()
	{
		std::ifstream rom_handle(emulator.GetModelFilePath(emulator.GetModelInfo("rom_path")), std::ifstream::binary);
		if (rom_handle.fail())
			PANIC("std::ifstream failed: %s\n", std::strerror(errno));
		rom_data = std::vector<unsigned char>((std::istreambuf_iterator<char>(rom_handle)), std::istreambuf_iterator<char>());

		for (auto &peripheral : peripherals)
			peripheral->Initialise();

		ConstructInterruptSFR();
		ConstructClockGenerator();

		cpu.SetupInternals();
		mmu.SetupInternals();
	}

	void Chipset::Reset()
	{
		ResetInterruptSFR();

		ResetClockGenerator();

		HWRemap = false;

		SegmentAccess = false;
		data_BLKCON = 0;

		for (auto &peripheral : peripherals)
			peripheral->Reset();

		cpu.Reset();

		interrupts_active[INT_RESET] = true;
		pending_interrupt_count = 1;

		run_mode = RM_RUN;
	}

	void Chipset::Break()
	{
		if (cpu.GetExceptionLevel() > 1)
		{
			cpu.Reset();

			interrupts_active[INT_RESET] = true;
			pending_interrupt_count++;
			return;
		}

		if (interrupts_active[INT_BREAK])
			return;
		interrupts_active[INT_BREAK] = true;
		pending_interrupt_count++;
	}

	void Chipset::Halt()
	{
		run_mode = RM_HALT;
	}

	void Chipset::Stop()
	{
		run_mode = RM_STOP;
	}

	bool Chipset::GetRunningState()
	{
		if(run_mode == RM_RUN)
			return true;
		return false;
	}
	
	void Chipset::RaiseEmulator()
	{
		if (interrupts_active[INT_EMULATOR])
			return;
		interrupts_active[INT_EMULATOR] = true;
		pending_interrupt_count++;
	}

	void Chipset::RequestNonmaskable() {
		SetInterruptPendingSFR(INT_NONMASKABLE, true);
		/*if (data_int_mask & 1)
			RaiseNonmaskable();*/
		RaiseNonmaskable();
	}

	void Chipset::RaiseNonmaskable()
	{
		if (interrupts_active[INT_NONMASKABLE])
			return;
		interrupts_active[INT_NONMASKABLE] = true;
		pending_interrupt_count++;
	}

	void Chipset::ResetNonmaskable() {
		if (!interrupts_active[INT_NONMASKABLE])
			return;
		interrupts_active[INT_NONMASKABLE] = false;
		pending_interrupt_count--;
	}

	void Chipset::RaiseMaskable(size_t index)
	{
		if (index < INT_MASKABLE || index >= INT_SOFTWARE)
			PANIC("%zu is not a valid maskable interrupt index\n", index);
		if (interrupts_active[index])
			return;
		interrupts_active[index] = true;
		pending_interrupt_count++;
	}

	void Chipset::ResetMaskable(size_t index) {
		if (index < INT_MASKABLE || index >= INT_SOFTWARE)
			PANIC("%zu is not a valid maskable interrupt index\n", index);
		if (!interrupts_active[index])
			return;
		interrupts_active[index] = false;
		pending_interrupt_count--;
	}

	void Chipset::RaiseSoftware(size_t index)
	{
		index += 0x40;
		if (interrupts_active[index])
			return;
		interrupts_active[index] = true;
		pending_interrupt_count++;
	}

	void Chipset::AcceptInterrupt()
	{
		size_t old_exception_level = cpu.GetExceptionLevel();

		size_t index = 0;
		bool acceptable = true;
		// * Reset has priority over everything.
		if (interrupts_active[INT_RESET])
			index = INT_RESET;
		// * Software interrupts are immediately accepted.
		if (!index)
			for (size_t ix = INT_SOFTWARE; ix != INT_COUNT; ++ix)
				if (interrupts_active[ix])
				{
					if (old_exception_level > 1)
						logger::Info("software interrupt while exception level was greater than 1\n");//test on real hardware shows that SWI seems to be raised normally when ELEVEL=2
					index = ix;
					break;
				}
		// * No need to check the old exception level as NMICI has an exception level of 3.
		if (!index && interrupts_active[INT_EMULATOR])
			index = INT_EMULATOR;
		// * No need to check the old exception level as BRK initiates a reset if
		//   the currect exception level is greater than 1.
		if (!index && interrupts_active[INT_BREAK])
			index = INT_BREAK;
		if (!index && interrupts_active[INT_NONMASKABLE]) {
			index = INT_NONMASKABLE;
			if(old_exception_level > 2) {
				acceptable = false;
			}
		}
		if (!index){
			for (size_t ix = INT_MASKABLE; ix != INT_SOFTWARE; ++ix){
				if (interrupts_active[ix])
				{
					index = ix;
					if(old_exception_level > 1) {
						acceptable = false;
					}
					break;
				}
			}
		}

		size_t exception_level;
		switch (index)
		{
		case INT_RESET:
			exception_level = 0;
			break;

		case INT_BREAK:
		case INT_NONMASKABLE:
			exception_level = 2;
			break;

		case INT_EMULATOR:
			exception_level = 3;
			break;

		default:
			exception_level = 1;
			break;
		}

		if (index >= INT_MASKABLE && index < INT_SOFTWARE)
		{
			if (cpu.GetMasterInterruptEnable() && acceptable) {
				SetInterruptPendingSFR(index, false);
				cpu.Raise(exception_level, index);

				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
			
		}
		else if(index == INT_NONMASKABLE)
		{
			if(acceptable) {
				cpu.Raise(exception_level, index);
				SetInterruptPendingSFR(INT_NONMASKABLE, false);
				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
		} else {
			cpu.Raise(exception_level, index);
			interrupts_active[index] = false;
			pending_interrupt_count--;
		}
	}

	bool Chipset::GetInterruptPendingSFR(size_t index)
	{
		return data_int_pending & (1 << (index - managed_interrupt_base));
	}

	void Chipset::SetInterruptPendingSFR(size_t index, bool val)
	{
		if(val)
			data_int_pending |= (1 << (index - managed_interrupt_base));
		else
			data_int_pending &= ~(1 << (index - managed_interrupt_base));
	}

	void Chipset::HandleInterrupts() {
		if (pending_interrupt_count) {
			AcceptInterrupt();
			for (auto peripheral : peripherals)
				peripheral->TickAfterInterrupts();
		}
	}

	void Chipset::InstCallBack() {
		coprocessor.Refresh();
	}

	bool Chipset::GetRequireFrame()
	{
		return std::any_of(peripherals.begin(), peripherals.end(), [](Peripheral *peripheral){
			return peripheral->GetRequireFrame();
		});
	}

	void Chipset::InputToPort(int port, int pin, bool value) {
		if(port == 0) {
			if(pin < 1 || pin > 3)
				PANIC("Trying to input to invalid pin %d of Port0!", pin);
			UserInput_level_Port0[pin - 1] = value;
			UserInput_state_Port0[pin - 1] = true;
			ioport->AcceptInput(0, pin - 1);
		} else if(port == 1) {
			if(pin < 0 || pin > 6)
				PANIC("Trying to input to invalid pin %d of Port1!", pin);
			UserInput_level_Port1[pin] = value;
			UserInput_state_Port1[pin] = true;
			ioport->AcceptInput(1, pin);
		} else {
			PANIC("Trying to input to invalid port %d!", port);
		}
	}

	void Chipset::RemovePortInput(int port, int pin) {
		if(port == 0) {
			if(pin < 1 || pin > 3)
				PANIC("Trying to remove input from invalid pin %d of Port0!", pin);
			UserInput_level_Port0[pin - 1] = false;
			UserInput_state_Port0[pin - 1] = false;
			ioport->AcceptInput(0, pin - 1);
		} else if(port == 1) {
			if(pin < 0 || pin > 6)
				PANIC("Trying to remove input from invalid pin %d of Port1!", pin);
			UserInput_level_Port1[pin] = false;
			UserInput_state_Port1[pin] = false;
			ioport->AcceptInput(1, pin);
		} else {
			PANIC("Trying to remove input from invalid port %d!", port);
		}
	}

	void Chipset::Frame()
	{
		for (auto peripheral : peripherals)
			peripheral->Frame();
	}

	void Chipset::Tick()
	{
		// * TODO: decrement delay counter, return if it's not 0

		GenerateTickForClock();

		for (auto peripheral : peripherals) {
			switch (peripheral->GetClockType())
			{
			case CLOCK_UNDEFINED:
				peripheral->Tick();
				break;
			case CLOCK_LSCLK:
				if(LTBCReset)
					peripheral->ResetLSCLK();
				if(LSCLKTick)
					peripheral->Tick();
				break;
			case CLOCK_HSCLK:
				if(HSCLKTick)
					peripheral->Tick();
				break;
			default:
				break;
			}
		}

		if (pending_interrupt_count)
			run_mode = RM_RUN;

		//cpu always follows the high speed clock.
		if (run_mode == RM_RUN && HSCLKTick) {
			cpu.Next();
			coprocessor.Tick();
		}

		LSCLKTick = false;
		LTBCReset = false;
		HSCLKTick = false;
	}

	void Chipset::UIEvent(SDL_Event &event)
	{
		for (auto peripheral : peripherals)
			peripheral->UIEvent(event);
	}
}

