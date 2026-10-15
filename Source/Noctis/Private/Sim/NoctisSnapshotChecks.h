// Engine-independent checks on render snapshots: a bitwise hash (determinism) and a consistency validator.
// Deliberately free of Unreal headers so the exact same code can be compiled against the standalone core
// (it was, during development: see Docs/UE5_INTEGRATION.md, "Ce qui a été vérifié ici").
#pragma once

#include "Noctis/Sim/WorldSimulation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace NoctisChecks
{
inline noctis::u64 FloatBits(float V)
{
	noctis::u32 Bits = 0;
	std::memcpy(&Bits, &V, sizeof(Bits));
	return Bits;
}

inline noctis::u64 DoubleBits(double V)
{
	noctis::u64 Bits = 0;
	std::memcpy(&Bits, &V, sizeof(Bits));
	return Bits;
}

inline noctis::u64 HashVec3(noctis::u64 H, const noctis::Vec3& V)
{
	H = noctis::hashCombine(H, FloatBits(V.x));
	H = noctis::hashCombine(H, FloatBits(V.y));
	return noctis::hashCombine(H, FloatBits(V.z));
}

/** Bitwise hash of everything a renderer consumes. Two runs are deterministic iff their hashes match step by step. */
inline noctis::u64 HashSnapshot(const noctis::RenderSnapshot& S)
{
	noctis::u64 H = 0x4E4F43544953ull; // "NOCTIS"
	H = noctis::hashCombine(H, DoubleBits(S.time));
	H = noctis::hashCombine(H, FloatBits(S.sky.sunElevationDeg));
	H = noctis::hashCombine(H, FloatBits(S.sky.illuminanceLux));
	H = noctis::hashCombine(H, static_cast<noctis::u64>(S.weather.regime));
	H = noctis::hashCombine(H, FloatBits(S.weather.temperatureC));
	H = noctis::hashCombine(H, FloatBits(S.weather.cloudCover));
	H = noctis::hashCombine(H, FloatBits(S.weather.fogExtinction));
	H = noctis::hashCombine(H, FloatBits(S.riverStageM));
	H = noctis::hashCombine(H, static_cast<noctis::u64>(S.creatures.size()));
	for (const noctis::CreatureRenderState& C : S.creatures)
	{
		H = noctis::hashCombine(H, C.id);
		H = noctis::hashCombine(H, static_cast<noctis::u64>(static_cast<noctis::u16>(C.speciesIndex)));
		H = noctis::hashCombine(H, static_cast<noctis::u64>(C.lod));
		H = HashVec3(H, C.position);
		H = noctis::hashCombine(H, FloatBits(C.heading));
		H = noctis::hashCombine(H, FloatBits(C.speed));
		H = noctis::hashCombine(H, static_cast<noctis::u64>(C.gait));
		H = noctis::hashCombine(H, static_cast<noctis::u64>(C.behavior));
		H = noctis::hashCombine(H, static_cast<noctis::u64>(C.posture));
		for (size_t F = 0; F < C.footCount && F < C.footTargets.size(); ++F)
		{
			H = HashVec3(H, C.footTargets[F]);
		}
		for (const float V : C.softTissue)
		{
			H = noctis::hashCombine(H, FloatBits(V));
		}
	}
	H = noctis::hashCombine(H, static_cast<noctis::u64>(S.carcasses.size()));
	for (const noctis::CarcassRenderState& K : S.carcasses)
	{
		H = noctis::hashCombine(H, K.id);
		H = noctis::hashCombine(H, FloatBits(K.softTissueFraction));
	}
	H = noctis::hashCombine(H, static_cast<noctis::u64>(S.newSounds.size()));
	for (const noctis::SoundSource& Snd : S.newSounds)
	{
		H = noctis::hashCombine(H, Snd.id);
		H = noctis::hashCombine(H, static_cast<noctis::u64>(Snd.kind));
		H = HashVec3(H, Snd.position);
	}
	return H;
}

inline bool Finite(const noctis::Vec3& V)
{
	return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z);
}

/** Checks a snapshot against the simulation it was built from. Returns false and a message on the first problem. */
inline bool ValidateSnapshot(const noctis::RenderSnapshot& S, const noctis::WorldSimulation& W, std::string& Error)
{
	char Buf[256];
	auto Fail = [&](const char* What, unsigned long long A, unsigned long long B) {
		std::snprintf(Buf, sizeof(Buf), "%s (%llu, %llu)", What, A, B);
		Error = Buf;
		return false;
	};
	if (S.time != W.clock().seconds())
	{
		return Fail("snapshot time differs from the simulation clock", static_cast<unsigned long long>(S.time), static_cast<unsigned long long>(W.clock().seconds()));
	}
	size_t Alive = 0;
	for (const noctis::Creature& C : W.creatures().creatures())
	{
		Alive += C.alive ? 1u : 0u;
	}
	if (S.creatures.size() != Alive)
	{
		return Fail("creature count differs from living individuals", S.creatures.size(), Alive);
	}
	const noctis::Rect2 Bounds = W.terrain().bounds();
	const float Margin = 200.0f;
	std::set<noctis::u32> Ids;
	for (const noctis::CreatureRenderState& C : S.creatures)
	{
		if (!Ids.insert(C.id).second)
		{
			return Fail("duplicate creature id", C.id, 0);
		}
		if (noctis::EntityId(C.id).kind() != noctis::EntityKind::Creature)
		{
			return Fail("creature id of the wrong entity kind", C.id, static_cast<unsigned long long>(noctis::EntityId(C.id).kind()));
		}
		if (C.speciesIndex < 0 || static_cast<size_t>(C.speciesIndex) >= W.species().size())
		{
			return Fail("creature species index out of range", C.id, static_cast<unsigned long long>(static_cast<noctis::u16>(C.speciesIndex)));
		}
		if (!Finite(C.position) || !std::isfinite(C.heading) || !std::isfinite(C.pitch) || !std::isfinite(C.roll) || !std::isfinite(C.speed))
		{
			return Fail("non-finite creature transform", C.id, 0);
		}
		if (C.position.x < Bounds.min.x - Margin || C.position.y < Bounds.min.y - Margin || C.position.x > Bounds.max.x + Margin ||
			C.position.y > Bounds.max.y + Margin)
		{
			return Fail("creature outside the terrain bounds", C.id, 0);
		}
		if (C.footCount > 4)
		{
			return Fail("more than four feet", C.id, C.footCount);
		}
		for (size_t F = 0; F < C.footCount; ++F)
		{
			if (!Finite(C.footTargets[F]) || C.footContact[F] > 1)
			{
				return Fail("invalid foot target", C.id, F);
			}
		}
		if (!(C.scale > 0.0f) || !std::isfinite(C.scale))
		{
			return Fail("invalid body scale", C.id, 0);
		}
	}
	for (const noctis::CarcassRenderState& K : S.carcasses)
	{
		if (K.speciesIndex >= 0 && static_cast<size_t>(K.speciesIndex) >= W.species().size())
		{
			return Fail("carcass species index out of range", K.id, static_cast<unsigned long long>(K.speciesIndex));
		}
		if (!Finite(K.position))
		{
			return Fail("non-finite carcass position", K.id, 0);
		}
	}
	std::set<noctis::u32> SoundIds;
	for (const noctis::SoundSource& Snd : S.newSounds)
	{
		if (!SoundIds.insert(Snd.id).second)
		{
			return Fail("sound published twice in one snapshot", Snd.id, 0);
		}
	}
	const float SunLen = S.sky.sunDirection.length();
	if (std::fabs(SunLen - 1.0f) > 1e-3f)
	{
		return Fail("sun direction not normalised (x1000)", static_cast<unsigned long long>(SunLen * 1000.0f), 1000);
	}
	if (S.weather.cloudCover < 0.0f || S.weather.cloudCover > 1.0f || S.weather.humidity < 0.0f || S.weather.humidity > 1.0f)
	{
		return Fail("weather fraction out of [0, 1] (x1000)", static_cast<unsigned long long>(S.weather.cloudCover * 1000.0f),
					static_cast<unsigned long long>(S.weather.humidity * 1000.0f));
	}
	Error.clear();
	return true;
}

/** Sounds must never be published twice across consecutive snapshots (the audio layer plays each once). */
inline bool NoSoundRepeated(const noctis::RenderSnapshot& Older, const noctis::RenderSnapshot& Newer)
{
	std::set<noctis::u32> Seen;
	for (const noctis::SoundSource& S : Older.newSounds)
	{
		Seen.insert(S.id);
	}
	for (const noctis::SoundSource& S : Newer.newSounds)
	{
		if (Seen.count(S.id) != 0)
		{
			return false;
		}
	}
	return true;
}
} // namespace NoctisChecks
