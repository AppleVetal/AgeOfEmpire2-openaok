#include "Grid.h"

#include <algorithm>

namespace Physics {

namespace {
uint32_t g_next_serial = 1;
} // namespace

Grid::Grid(int w, int h) : width(w), height(h), serial(g_next_serial++) {
    tiles.resize(static_cast<size_t>(width * height));
    out_of_bounds_tile.is_walkable = false;
    out_of_bounds_tile.terrain = Core::TerrainType::DeepWater;
}

void Grid::Resize(int w, int h) {
    width = w;
    height = h;
    for (uint32_t& revision : walk_revision) ++revision;
    tiles.clear();
    tiles.resize(static_cast<size_t>(width * height));
}


void Grid::SetWalkable(int x, int y, bool walkable) {
    if (!InBounds(x, y)) return;
    tiles[static_cast<size_t>(y * width + x)].blocked = !walkable;
    Refresh(x, y);
}

bool Grid::IsBlocked(int x, int y) const {
    if (!InBounds(x, y)) return true;
    const auto& tile = tiles[static_cast<size_t>(y * width + x)];
    return tile.blocked || tile.earthBlocked;
}

void Grid::SetLandPassability(const std::vector<bool>& by_terrain) {
    land_passable = by_terrain;
}

bool Grid::LandPassable(Core::TerrainType terrain) const {
    const size_t index = static_cast<size_t>(terrain);
    if (index < land_passable.size()) return land_passable[index];
    switch (terrain) {
    case Core::TerrainType::DeepWater:
    case Core::TerrainType::MediumWater:
    case Core::TerrainType::Swamp:
        return false;
    default:
        return true;
    }
}

void Grid::SetLayers(const std::vector<std::vector<bool>>& grounds,
                     const std::vector<int8_t>& layer_of_restriction) {
    layer_count = 1;
    layer_grounds.assign(1, {});
    if (!grounds.empty()) {
        land_passable = grounds[0];
        for (size_t l = 1; l < grounds.size() && l < static_cast<size_t>(kMaxLayers); ++l) {
            layer_grounds.push_back(grounds[l]);
            ++layer_count;
        }
    }
    restriction_layer = layer_of_restriction;
    for (int8_t& layer : restriction_layer) {
        if (layer >= layer_count) layer = -1;
    }
}

Layer Grid::LayerOf(int restriction) const {
    if (restriction < 0 || restriction >= static_cast<int>(restriction_layer.size())) return kLand;
    const int8_t layer = restriction_layer[static_cast<size_t>(restriction)];
    return layer > 0 ? static_cast<Layer>(layer) : kLand;
}

bool Grid::GroundPasses(Layer layer, Core::TerrainType terrain) const {
    layer = BaseLayer(layer);
    if (layer == kLand) return LandPassable(terrain);
    if (layer >= layer_count) return false;
    const auto& grounds = layer_grounds[layer];
    const size_t index = static_cast<size_t>(terrain);
    return index < grounds.size() && grounds[index];
}

void Grid::SetGate(int x, int y, Core::PlayerId owner) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    const uint8_t gate = static_cast<uint8_t>(owner & kGateOwnerBits);
    if (tile.gate == gate) return;
    tile.gate = gate;
    // Regions run through a gate that is not locked: see Pathfinder.
    ++walk_revision[kLand];
}

void Grid::SetGateLocked(int x, int y, bool locked) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.gate == 0) return;
    const uint8_t gate = static_cast<uint8_t>(locked ? (tile.gate | kGateLocked)
                                                     : (tile.gate & ~kGateLocked));
    if (gate == tile.gate) return;
    tile.gate = gate;
    ++walk_revision[kLand];
}

void Grid::SetGateFriends(Core::PlayerId owner, uint16_t friends) {
    if (owner >= 16) return;
    if (gate_friends[owner] == friends) return;
    gate_friends[owner] = friends;
    ++walk_revision[kLand];
}

bool Grid::GateOpensFor(int x, int y, Core::PlayerId player) const {
    if (!InBounds(x, y) || player == 0) return false;
    const Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.gate == 0 || (tile.gate & kGateLocked) != 0) return false;
    if (tile.earthBlocked || !LandPassable(tile.terrain)) return false;
    const Core::PlayerId owner = static_cast<Core::PlayerId>(tile.gate & kGateOwnerBits);
    return owner == player || ((gate_friends[owner] >> player) & 1u) != 0;
}

uint8_t Grid::LayerBits(const Tile& tile) const {
    uint8_t bits = 0;
    if (tile.blocked || tile.earthBlocked) return bits;
    for (int l = 1; l < layer_count; ++l) {
        if (GroundPasses(static_cast<Layer>(l), tile.terrain)) bits |= static_cast<uint8_t>(1u << l);
    }
    return bits;
}

void Grid::StoreBits(Tile& tile, uint8_t bits) {
    const uint8_t changed = static_cast<uint8_t>(tile.passable ^ bits);
    if (changed == 0) return;
    for (int l = 1; l < kMaxLayers; ++l) {
        if ((changed >> l) & 1u) ++walk_revision[l];
    }
    tile.passable = bits;
}

void Grid::Refresh(int x, int y) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    const bool walkable = !tile.blocked && !tile.earthBlocked && LandPassable(tile.terrain);
    if (walkable != tile.is_walkable) ++walk_revision[kLand];
    tile.is_walkable = walkable;
    StoreBits(tile, LayerBits(tile));
}

bool Grid::RestoreTiles(const std::vector<Tile>& saved) {
    if (saved.size() != tiles.size()) return false;
    for (const auto& tile : saved)
        if (tile.elevation < earthworkMinLevel || tile.elevation > 255 ||
            tile.terrain >= Core::TerrainType::COUNT || tile.originalTerrain >= Core::TerrainType::COUNT ||
            tile.earthwork > Earthwork::WaterMoat || tile.pinnedCorners > 15 || tile.m_flowDirection > 8 ||
            (tile.earthwork == Earthwork::River ? tile.m_flowDirection == 0 : tile.m_flowDirection != 0) ||
            (tile.earthwork != Earthwork::None && (tile.elevation > earthworkMaxLevel ||
                tile.originalElevation < earthworkMinLevel || tile.originalElevation > earthworkMaxLevel))) return false;
    for (const auto& tile : saved)
        for (const auto corner : tile.cornerHeights)
            if (corner < earthworkMinLevel * 4 || corner > 255 * 4) return false;
    tiles = saved;
    // Everything that keeps something of the ground looks at it again.
    terrain_log.clear();
    ++terrain_revision;
    WalkabilityChanged();
    return true;
}

void Grid::WalkabilityChanged() {
    ++walk_revision[kLand];
    for (Tile& tile : tiles) StoreBits(tile, LayerBits(tile));
    for (int l = 1; l < kMaxLayers; ++l) ++walk_revision[l];
}

void Grid::SetTerrain(int x, int y, Core::TerrainType terrain) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.terrain == terrain) return;
    tile.terrain = terrain;
    const bool walkable = !tile.blocked && !tile.earthBlocked && LandPassable(terrain);
    if (walkable != tile.is_walkable) ++walk_revision[kLand];
    tile.is_walkable = walkable;
    StoreBits(tile, LayerBits(tile));
    LogTerrain(x, y);
}

void Grid::LogTerrain(int x, int y)
{
    ++terrain_revision;
    if (terrain_log.size() >= kTerrainLogSize) {
        terrain_log.erase(terrain_log.begin(),
                          terrain_log.begin() + static_cast<long>(kTerrainLogSize / 2));
    }
    terrain_log.push_back(TerrainChange{terrain_revision, x, y});
}

int Grid::CornerHeight(int x, int y) const
{
    int sum = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
        {
            const int tx = std::clamp(x - 1 + dx, 0, width - 1);
            const int ty = std::clamp(y - 1 + dy, 0, height - 1);
            const auto& tile = GetTile(tx, ty);
            const int corner = (1 - dy) * 2 + 1 - dx;
            if ((tile.pinnedCorners & (1 << corner)) != 0) return tile.cornerHeights[corner];
            sum += tile.elevation;
        }
    return sum;
}

void Grid::PinCorners(int x, int y, uint8_t mask)
{
    auto& tile = GetTileMut(x, y);
    for (int corner = 0; corner < 4; ++corner)
        if ((mask & (1 << corner)) != 0 && (tile.pinnedCorners & (1 << corner)) == 0)
        {
            tile.cornerHeights[corner] = static_cast<int16_t>(CornerHeight(x + corner % 2, y + corner / 2));
            tile.pinnedCorners = static_cast<uint8_t>(tile.pinnedCorners | (1 << corner));
        }
}

bool Grid::EarthworkBlocks(int x, int y) const
{
    const auto& tile = GetTile(x, y);
    if (tile.earthwork == Earthwork::Ditch || tile.earthwork == Earthwork::WaterMoat) return true;
    if (tile.earthwork == Earthwork::Lake || tile.earthwork == Earthwork::Channel || tile.earthwork == Earthwork::River) return false;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (!InBounds(x + dx, y + dy)) continue;
            const auto& other = GetTile(x + dx, y + dy);
            if (other.earthwork == Earthwork::Ditch) continue;
            if ((tile.earthwork == Earthwork::Raised || other.earthwork == Earthwork::Raised) &&
                tile.elevation > other.elevation + 1) return true;
        }
    return false;
}

bool Grid::SetEarthwork(int x, int y, int level, Earthwork kind, uint8_t flowDirection)
{
    if (!InBounds(x, y) || level < earthworkMinLevel || level > earthworkMaxLevel ||
        kind > Earthwork::WaterMoat || flowDirection > 8 || (kind != Earthwork::River && flowDirection != 0)) return false;
    if (kind == Earthwork::River && flowDirection == 0) flowDirection = 1;
    auto& tile = GetTileMut(x, y);
    if (kind == Earthwork::None)
    {
        if (tile.earthwork == Earthwork::None || level != tile.originalElevation) return false;
        tile.terrain = tile.originalTerrain;
        tile.originalElevation = 0;
        tile.originalTerrain = Core::TerrainType::Grass1;
        tile.pinnedCorners = 0;
        tile.cornerHeights = {};
    }
    else
    {
        if (tile.earthwork == kind && tile.elevation == level && tile.m_flowDirection == flowDirection) return false;
        const bool wasWater = tile.terrain == Core::TerrainType::DeepWater || tile.terrain == Core::TerrainType::MediumWater ||
            tile.terrain == Core::TerrainType::ShallowWater || tile.terrain == Core::TerrainType::Swamp;
        if (tile.earthwork == Earthwork::None)
        {
            tile.originalElevation = tile.elevation;
            tile.originalTerrain = tile.terrain;
        }
        if (kind == Earthwork::Ditch) tile.terrain = Core::TerrainType::Dirt2;
        if (kind == Earthwork::Channel) tile.terrain = Core::TerrainType::Dirt3;
        if (kind == Earthwork::Lake || kind == Earthwork::River || kind == Earthwork::WaterMoat)
            tile.terrain = Core::TerrainType::MediumWater;
        else if (wasWater) tile.terrain = Core::TerrainType::Sand;
    }
    tile.elevation = level;
    tile.earthwork = kind;
    tile.m_flowDirection = flowDirection;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (InBounds(x + dx, y + dy))
            {
                GetTileMut(x + dx, y + dy).earthBlocked = EarthworkBlocks(x + dx, y + dy);
                Refresh(x + dx, y + dy);
            }
    LogTerrain(x, y);
    return true;
}

const Tile& Grid::GetTile(int x, int y) const {
    if (!InBounds(x, y)) return out_of_bounds_tile;
    return tiles[static_cast<size_t>(y * width + x)];
}

Tile& Grid::GetTileMut(int x, int y) {
    return tiles[static_cast<size_t>(y * width + x)];
}

} // namespace Physics
