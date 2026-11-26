// Fixed, headless firmware scenario for comparing emulation engines and builds.
//
//   trigPressFirmwareTest md|mm [--render <file>] [--counters <file>]
//                               [--boot-seconds N] [--hold-seconds N] [--tail-seconds N]
//
// Cold boot (no home path, so no user storage is read or written), then press
// Trigger 1 out of record mode, hold it, release it, and keep rendering. The
// machine is driven with Hardware::processAudio() in fixed 256-frame blocks and
// the six raw 24-bit DSP output words per frame are hashed, so the result does
// not depend on float conversion or host resampling.
//
// --render writes those words as little-endian int32, six channels interleaved.
// --counters writes one CSV line every 0.25 s of emulated time: UC cycles and
// each DSP's instruction count, cycle count and PC. Two runs are identical only
// if every line matches; the first differing line locates a divergence.
//
// The summary line on stdout starts with "RENDER" and is parsed by
// scripts/verify-interp.sh. Firmware comes from GEARMULATOR_MD_FIRMWARE_BIN /
// GEARMULATOR_MM_FIRMWARE_BIN; without it the test skips (exit 77).

#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	constexpr uint32_t g_blockFrames = 256;
	constexpr uint32_t g_checkpointFrames = md::g_samplerate / 4;

	class Recorder
	{
	public:
		Recorder(md::Hardware& _hardware, FILE* _render, FILE* _counters)
			: m_hardware(_hardware), m_render(_render), m_counters(_counters)
		{
			if(m_counters)
				std::fprintf(m_counters, "frame,uc_cycles,dsp0_instr,dsp0_cycles,dsp0_pc,dsp1_instr,dsp1_cycles,dsp1_pc\n");
		}

		// Render _frames frames, in whole blocks, and return the peak in that span.
		double run(const uint32_t _frames)
		{
			double peak = 0.0;
			uint32_t remaining = _frames;
			while(remaining)
			{
				const uint32_t n = std::min(remaining, g_blockFrames);
				m_hardware.processAudio(n, 0);
				const auto& outputs = m_hardware.getAudioOutputs();
				for(uint32_t i = 0; i < n; ++i)
				{
					for(size_t ch = 0; ch < outputs.size(); ++ch)
					{
						const uint32_t word = static_cast<uint32_t>(outputs[ch][i]) & 0xffffffu;
						const int32_t sample = static_cast<int32_t>(word << 8) >> 8;
						for(int b = 0; b < 3; ++b)
						{
							m_hash ^= (word >> (8 * b)) & 0xffu;
							m_hash *= 0x100000001b3ull;
						}
						peak = std::max(peak, std::abs(static_cast<double>(sample)) / 8388608.0);
						if(m_render)
						{
							const uint32_t le = static_cast<uint32_t>(sample);
							const uint8_t bytes[4] = {static_cast<uint8_t>(le), static_cast<uint8_t>(le >> 8),
								static_cast<uint8_t>(le >> 16), static_cast<uint8_t>(le >> 24)};
							std::fwrite(bytes, 1, 4, m_render);
						}
					}
					++m_frames;
					if((m_frames % g_checkpointFrames) == 0)
						checkpoint();
				}
				remaining -= n;
			}
			m_peak = std::max(m_peak, peak);
			return peak;
		}

		uint64_t hash() const { return m_hash; }
		uint64_t frames() const { return m_frames; }
		uint64_t checkpoints() const { return m_checkpoints; }
		double peak() const { return m_peak; }

	private:
		void checkpoint()
		{
			++m_checkpoints;
			if(!m_counters)
				return;
			auto& mixer = m_hardware.getDspMixer().dsp();
			auto& producer = m_hardware.getDspProducer().dsp();
			std::fprintf(m_counters, "%llu,%llu,%llu,%llu,%06x,%llu,%llu,%06x\n",
				static_cast<unsigned long long>(m_frames),
				static_cast<unsigned long long>(m_hardware.hostCurrentCycle()),
				static_cast<unsigned long long>(mixer.getInstructionCounter()),
				static_cast<unsigned long long>(mixer.getCycles()),
				static_cast<unsigned>(mixer.getPC().toWord()),
				static_cast<unsigned long long>(producer.getInstructionCounter()),
				static_cast<unsigned long long>(producer.getCycles()),
				static_cast<unsigned>(producer.getPC().toWord()));
			// One line per checkpoint reaches the file at once, so a stalled run still
			// shows how far it got.
			std::fflush(m_counters);
		}

		md::Hardware& m_hardware;
		FILE* m_render;
		FILE* m_counters;
		uint64_t m_hash = 0xcbf29ce484222325ull;
		uint64_t m_frames = 0;
		uint64_t m_checkpoints = 0;
		double m_peak = 0.0;
	};

	struct File
	{
		explicit File(const char* _path) : f(_path ? std::fopen(_path, "wb") : nullptr)
		{
			if(_path)
				require(f != nullptr, "cannot open output file");
		}
		~File() { if(f) std::fclose(f); }
		FILE* f;
	};
}

int main(const int _argc, char** _argv)
{
	if(_argc < 2 || (std::string_view(_argv[1]) != "md" && std::string_view(_argv[1]) != "mm"))
	{
		std::cerr << "usage: trigPressFirmwareTest md|mm [--render f] [--counters f] [--boot-seconds n] [--hold-seconds n] [--tail-seconds n]\n";
		return 2;
	}
	const auto model = std::string_view(_argv[1]) == "md" ? md::MachineModel::Machinedrum : md::MachineModel::Monomachine;
	const char* renderPath = nullptr;
	const char* countersPath = nullptr;
	uint32_t bootSeconds = 25, holdSeconds = 2, tailSeconds = 10;
	for(int i = 2; i + 1 < _argc; i += 2)
	{
		const std::string_view arg(_argv[i]);
		if(arg == "--render") renderPath = _argv[i + 1];
		else if(arg == "--counters") countersPath = _argv[i + 1];
		else if(arg == "--boot-seconds") bootSeconds = static_cast<uint32_t>(std::strtoul(_argv[i + 1], nullptr, 10));
		else if(arg == "--hold-seconds") holdSeconds = static_cast<uint32_t>(std::strtoul(_argv[i + 1], nullptr, 10));
		else if(arg == "--tail-seconds") tailSeconds = static_cast<uint32_t>(std::strtoul(_argv[i + 1], nullptr, 10));
		else { std::cerr << "unknown argument " << arg << '\n'; return 2; }
	}

	const char* path = std::getenv(model == md::MachineModel::Machinedrum
		? "GEARMULATOR_MD_FIRMWARE_BIN" : "GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path) { std::cout << "SKIP: firmware not supplied\n"; return 77; }

	try
	{
		synthLib::DeviceCreateParams params;
		require(baseLib::filesystem::readFile(params.romData, path), "cannot read firmware");
		require(md::RomLoader::isRomForModel(params.romData, model), "wrong firmware model");
		params.romName = path;
		params.customData = md::deviceCustomData(model);
		// No homePath: never load or overwrite the user's machine storage.
		auto device = std::make_unique<md::Device>(params);
		require(device->isValid(), "firmware did not create a valid device");
		auto& hardware = device->getHardware();

		File render(renderPath);
		File counters(countersPath);
		Recorder recorder(hardware, render.f, counters.f);

		const auto wallStart = std::chrono::steady_clock::now();

		recorder.run(md::g_samplerate * bootSeconds);
		const bool ready = hardware.isAudioReady() && hardware.isFirmwareMidiReady();

		md::PanelRowState rows;
		const auto trig = md::panelPacket(model, md::PanelControl::Trigger1);
		require(trig.has_value(), "missing Trigger1 mapping");
		const auto down = rows.press(*trig);
		require(hardware.trySendPanelEvent(down.row, down.mask), "trig press rejected");
		const double holdPeak = recorder.run(md::g_samplerate * holdSeconds);
		const auto up = rows.release(*trig);
		require(hardware.trySendPanelEvent(up.row, up.mask), "trig release rejected");
		const double tailPeak = recorder.run(md::g_samplerate * tailSeconds);

		const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
		const double emulated = static_cast<double>(recorder.frames()) / md::g_samplerate;
		auto& mixer = hardware.getDspMixer().dsp();
		auto& producer = hardware.getDspProducer().dsp();

		char hash[17];
		std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(recorder.hash()));
		std::cout << "RENDER model=" << _argv[1]
			<< " frames=" << recorder.frames()
			<< " checkpoints=" << recorder.checkpoints()
			<< " hash=" << hash
			<< " boot_ready=" << (ready ? 1 : 0)
			<< " peak=" << recorder.peak()
			<< " hold_peak=" << holdPeak
			<< " tail_peak=" << tailPeak
			<< " uc_cycles=" << hardware.hostCurrentCycle()
			<< " dsp0_instr=" << mixer.getInstructionCounter()
			<< " dsp0_cycles=" << mixer.getCycles()
			<< " dsp1_instr=" << producer.getInstructionCounter()
			<< " dsp1_cycles=" << producer.getCycles()
			<< " wall_s=" << wall
			<< " rtf=" << (wall > 0.0 ? emulated / wall : 0.0)
			<< '\n';
		if(!ready)
		{
			std::cerr << "boot incomplete after " << bootSeconds << " s\n";
			return 1;
		}
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "trigPressFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
