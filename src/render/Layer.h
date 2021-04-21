#ifndef NULLSPACE_RENDER_LAYER_H_
#define NULLSPACE_RENDER_LAYER_H_

namespace null {

enum class Layer {
  BelowAll,
  Background,
  AfterBackground,
  Tiles,
  AfterTiles,
  Weapons,
  AfterWeapons,
  Ships,
  AfterShips,
  Explosions,
  Gauges,
  AfterGauges,
  Chat,
  AfterChat,
  TopMost,

  Count
};

}  // namespace null

#endif
