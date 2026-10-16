// export-level — algorithmic layout of the playable Hell Creek vertical-slice level.
//
//   noctis_sim export-level [--env hell_creek] [--seed 7] [--profile standard] [--scale 1]
//                           [--warmup-hours 6] [--out Output/level] [--json Data/Levels/<level>.json]
//
// Generates the same world as 'run' (same seed and SimConfig), runs a short warm-up of the live
// simulation, then selects points of interest, predicted game corridors, the on-foot route network,
// rover accessibility and gameplay masks, and writes the level JSON plus annotated PNG maps.
#pragma once

namespace noctistools
{
// Parses its own options from argv (the common ones of noctis_sim plus --warmup-hours and --json).
int cmdExportLevel(int argc, char** argv);
} // namespace noctistools
