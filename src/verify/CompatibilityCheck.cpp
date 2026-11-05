#include "verify/Harness.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#include "core/ContentIdentity.h"
#include "core/Random.h"
#include "ecs/Digest.h"
#include "ecs/Simulation.h"
#include "ecs/components/Resource.h"
#include "net/Lobby.h"
#include "net/Wire.h"
#include "save/Recording.h"
#include "save/Save.h"

namespace Verify
{
  namespace
  {
    // Владеет временными файлами проверки и удаляет только созданные ею файлы.
    struct TestFiles
    {
      std::filesystem::path root;
      bool isCreated = false;
      ~TestFiles()
      {
        if (!isCreated) return;
        std::error_code error;
        for (const auto* name : {"a.json", "b.json", "record.mgl"})
          std::filesystem::remove(root / name, error);
        std::filesystem::remove(root, error);
      }
    };

    // Закрывает оба соединения независимо от результата сетевой проверки.
    struct Connections
    {
      Net::Session host;
      Net::Session client;
      ~Connections()
      {
        Net::Leave(client);
        Net::Leave(host);
      }
    };

    const char* checkRejectedConnection(bool isLobby, bool hasWrongRevision)
    {
      Connections pair;
      const bool hasHost = isLobby
        ? Net::OpenLobby(pair.host, 0, Core::DefaultSetup(), "compatibility", false)
        : Net::Host(pair.host, 0, Core::DefaultSetup(), 2);
      if (!hasHost) return "could not open the compatibility host";
      const auto port = HAL::Net::BoundPort(pair.host.listener);
      const bool hasClient = isLobby
        ? Net::JoinLobby(pair.client, "127.0.0.1", port, "other content")
        : Net::Join(pair.client, "127.0.0.1", port);
      if (!hasClient) return "could not connect the compatibility client";
      if (hasWrongRevision) ++pair.client.content.revision;
      else pair.client.content.dataHash ^= 1;
      for (int attempt = 0; attempt < 400 && pair.client.refused == 0; ++attempt)
      {
        Net::Pump(pair.host);
        Net::Pump(pair.client);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (pair.client.refused != Net::kWrongVersion || pair.client.started ||
          pair.client.welcomed || pair.host.started)
        return "incompatible content was admitted to a game";
      for (const auto& peer : pair.host.peers)
        if (peer.row >= 0 || peer.seat != 0) return "incompatible content took a player seat";
      return nullptr;
    }

    const char* checkIdentityFiles(TestFiles& files)
    {
      std::ofstream(files.root / "a.json", std::ios::binary) << "first";
      std::ofstream(files.root / "b.json", std::ios::binary) << "second";
      Core::content::Identity original, same, changed;
      std::string error;
      if (!Core::content::compute(files.root, {"a.json", "b.json"}, original, error) ||
          !Core::content::compute(files.root, {"b.json", "a.json"}, same, error) || original != same)
        return "content identity depends on file enumeration order";
      std::ofstream(files.root / "b.json", std::ios::binary) << "Second";
      if (!Core::content::compute(files.root, {"a.json", "b.json"}, changed, error) || changed == original)
        return "changed bytes did not change the content identity";
      if (Core::content::compute(files.root, {"missing.json"}, changed, error) || error.empty())
        return "missing content was accepted";
      if (Core::content::compute(files.root, {"a.json", "a.json"}, changed, error))
        return "duplicate content was accepted";
      Net::Wire::Writer out;
      Net::Wire::Write(out, original);
      Net::Wire::Reader in(out.bytes);
      if (!Net::Wire::Read(in, same) || !in.Done() || same != original)
        return "the content identity changed on the wire";
      out.bytes.pop_back();
      Net::Wire::Reader shortInput(out.bytes);
      if (Net::Wire::Read(shortInput, same)) return "a truncated identity was accepted";
      return nullptr;
    }

    const char* checkWorldDigest(TestFiles& files)
    {
      ECS::Simulation sim;
      sim.Init();
      auto& reg = sim.GetRegistry();
      const auto before = ECS::Digest::Of(sim);
      const auto legacy = ECS::Digest::Of(reg);
      auto& tile = sim.GetGrid().GetTileMut(0, 0);
      ++tile.elevation;
      if (ECS::Digest::Of(reg) != legacy || ECS::Digest::Of(sim) == before)
        return "a height change escaped the world digest";
      --tile.elevation;
      const auto resources = reg.view<ECS::Components::Resource>();
      if (resources.empty()) return "no resource node in the test world";
      auto& resource = reg.get<ECS::Components::Resource>(resources.front());
      --resource.current_amount;
      if (ECS::Digest::Of(sim) == before) return "resource depletion escaped the world digest";
      ++resource.current_amount;
      auto* random = reg.ctx().find<Core::Random>();
      if (random == nullptr) return "no random stream in the test world";
      const auto savedRandom = *random;
      random->Next();
      if (ECS::Digest::Of(sim) == before) return "random state escaped the world digest";
      *random = savedRandom;
      if (ECS::Digest::Of(sim) != before) return "digest inspection mutated the world";

      std::vector<uint8_t> bytes;
      if (!Save::Write(sim, bytes)) return "could not save the compatibility world";
      ECS::Simulation restored;
      std::string error;
      if (!Save::Read(restored, bytes, &error) || ECS::Digest::Of(restored) != before)
        return "terrain and resources did not survive a save";
      // Magic, format and component-layout fingerprint precede the content identity.
      constexpr std::size_t identityOffset = 8 + sizeof(uint32_t) + sizeof(uint64_t);
      bytes[identityOffset + sizeof(uint32_t)] ^= 1;
      Core::GameSetup setup;
      if (Save::ReadSetup(bytes, setup) || Save::Read(restored, bytes, &error))
        return "a save with different content was accepted";
      if (ECS::Digest::Of(restored) != before)
        return "rejecting incompatible content changed the current world";

      Save::Recording recording;
      recording.setup = sim.Setup();
      const auto path = files.root / "record.mgl";
      if (!Save::WriteRecording(recording, path.string())) return "could not write the test recording";
      Save::Recording read;
      if (!Save::ReadRecording(path.string(), read)) return "a matching recording was rejected";
      {
        std::fstream edit(path, std::ios::in | std::ios::out | std::ios::binary);
        constexpr std::streamoff hashOffset = 8 + sizeof(uint32_t);
        edit.seekg(hashOffset);
        char byte = 0;
        edit.get(byte);
        edit.seekp(hashOffset);
        edit.put(static_cast<char>(byte ^ 1));
        if (!edit) return "could not alter the test recording";
      }
      if (Save::ReadRecording(path.string(), read)) return "a recording with different content was accepted";
      return nullptr;
    }
  }

  bool CheckCompatibility()
  {
    TestFiles files;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    files.root = std::filesystem::temp_directory_path() / ("openaok-content-" + std::to_string(stamp));
    std::error_code error;
    files.isCreated = std::filesystem::create_directory(files.root, error);
    const char* verdict = files.isCreated ? nullptr : "could not create the test directory";
    if (verdict == nullptr) verdict = checkIdentityFiles(files);
    if (verdict == nullptr) verdict = checkWorldDigest(files);
    for (const bool isLobby : {false, true})
      for (const bool hasWrongRevision : {false, true})
        if (verdict == nullptr) verdict = checkRejectedConnection(isLobby, hasWrongRevision);
    std::printf("compatibility=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
  }
}
