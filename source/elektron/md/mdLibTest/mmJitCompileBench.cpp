// JIT compile benchmark for the Monomachine (iOS port, IOS_PORT_STATUS 2026-11-27): boots a fresh MM each round for
// one second of emulated time, in which the JIT compiles about 290 KB of the real firmware's DSP code, and prints the
// compile time per block from dsp56kEmu/jitstats.h. (Discarding blocks of a running DSP instead breaks live loops.)
// Use: GEARMULATOR_MM_FIRMWARE_BIN=<rom> mmJitCompileBench [rounds]

#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"

#include "baseLib/filesystem.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitstats.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
	void advance(md::Hardware& _hw, uint32_t _frames)
	{
		while(_frames)
		{
			const auto chunk = std::min<uint32_t>(256, _frames);
			_hw.advance(chunk);
			_frames -= chunk;
		}
	}

	struct Snapshot
	{
		uint64_t emitNs, blocks, codegenNs, finalizeNs, addNs, codeBytes, createNs, creates;
		static Snapshot take()
		{
			const auto& s = dsp56k::g_jitStats;
			return {s.emitNs.load(), s.emitBlocks.load(), s.codegenNs.load(), s.finalizeNs.load(), s.addNs.load(), s.codeBytes.load(), s.createNs.load(), s.creates.load()};
		}
	};
}

int main(int _argc, char** _argv)
{
	const char* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::printf("mmJitCompileBench: SKIP (MM firmware not supplied)\n");
		return 77;
	}
	const int rounds = _argc > 1 ? std::atoi(_argv[1]) : 20;

	std::vector<uint8_t> rom;
	if(!baseLib::filesystem::readFile(rom, path) || !md::RomLoader::isRomForModel(rom, md::MachineModel::Monomachine))
	{
		std::printf("mmJitCompileBench: not an MM OS 1.32b image\n");
		return 1;
	}
	const auto first = Snapshot::take();
	const auto t0 = std::chrono::steady_clock::now();
	for(int r = 0; r < rounds; ++r)
	{
		auto hw = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Monomachine);
		advance(*hw, md::g_samplerate);
	}
	const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	const auto last = Snapshot::take();
	const double blocks = double(last.blocks - first.blocks);
	std::printf("rounds=%d wall=%.1fms compile=%.1fms (%.1f%%) blocks=%.0f code_bytes=%llu per_block_us: emit=%.2f codegen=%.2f finalize=%.2f add=%.2f | creates=%llu create_total=%.1fms (emit included)\n",
		rounds, wallMs, double(last.emitNs - first.emitNs) * 1e-6, 100.0 * double(last.emitNs - first.emitNs) * 1e-6 / wallMs, blocks, (unsigned long long)(last.codeBytes - first.codeBytes),
		double(last.emitNs - first.emitNs) * 1e-3 / blocks, double(last.codegenNs - first.codegenNs) * 1e-3 / blocks,
		double(last.finalizeNs - first.finalizeNs) * 1e-3 / blocks, double(last.addNs - first.addNs) * 1e-3 / blocks,
		(unsigned long long)(last.creates - first.creates), double(last.createNs - first.createNs) * 1e-6);
	return 0;
}
