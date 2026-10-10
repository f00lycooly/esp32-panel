// media-rooms.h — panel-side cache of the 7 compile-time Sonos room slots.
//
// Pulled in by page-media.yaml (`esphome: includes:`). Each room slot
// (media-room.yaml, one package per slot) binds media_player.<slug> directly
// and writes what HA pushes into rooms()[idx]; page-media's scripts read the
// cache back to paint now-playing and resolve the playback target. This
// replaces the per-panel HA mirror sensor + input_text selection helper.
//
// Plain C++ with no ESPHome dependencies, so the logic can be unit-tested on
// the host (g++ -std=c++17).
#pragma once

#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace media_rooms {

constexpr int kRooms = 7;

// One configured room slot, as last reported by HA.
struct Room {
  std::string slug;                  // entity id minus "media_player." (set at boot)
  std::string caption;               // panel caption (set at boot)
  std::string state;                 // raw HA state: playing/paused/idle/...
  std::string title, artist, art;    // media_title / media_artist / entity_picture
  std::string source, content_id;    // source / media_content_id (TV detection)
  std::vector<std::string> members;  // parsed group_members, coordinator first
  float volume = NAN;                // volume_level 0..1, NaN = unknown
};

// The single cache instance (function-local static: one definition however
// many translation units include this header).
inline std::array<Room, kRooms> &rooms() {
  static std::array<Room, kRooms> r;
  return r;
}

inline std::string entity_of(const Room &r) { return "media_player." + r.slug; }

// group_members arrives from HA's ESPHome bridge as the Python repr of a list,
// e.g. "['media_player.a', 'media_player.b']". Be liberal: take every
// identifier-shaped token that contains a dot (so JSON, CSV, "[]", "None",
// "unknown" and "" all parse sensibly), keep order, drop duplicates.
inline std::vector<std::string> parse_members(const std::string &s) {
  std::vector<std::string> out;
  std::string tok;
  auto flush = [&]() {
    if (!tok.empty() && std::isalpha((unsigned char) tok[0]) && tok.back() != '.' &&
        tok.find('.') != std::string::npos) {
      bool dup = false;
      for (auto &e : out) if (e == tok) { dup = true; break; }
      if (!dup) out.push_back(tok);
    }
    tok.clear();
  };
  for (char c : s) {
    if (std::isalnum((unsigned char) c) || c == '_' || c == '.') tok += c;
    else flush();
  }
  flush();
  return out;
}

// Group coordinator = group_members[0]; a room with no group_members is its
// own coordinator (HA's `group_members or [e]`).
inline std::string coord_of(const Room &r) {
  return r.members.empty() ? entity_of(r) : r.members[0];
}

// External input (TV / line-in / Sonos home-theatre stream). A room in that
// state reports `tv` instead of `playing` so now-playing and auto-follow
// ignore TV audio.
inline bool is_tv(const Room &r) {
  std::string src;
  for (char c : r.source) src += (char) std::tolower((unsigned char) c);
  return src == "tv" || src == "line-in" || src == "line in" ||
         r.content_id.rfind("x-sonos-htastream", 0) == 0;
}

// State as the panel shows it: `tv` replaces `playing` on an external input.
inline std::string display_state(const Room &r, bool tv) {
  return (r.state == "playing" && tv) ? std::string("tv") : r.state;
}

// Index of the configured room whose entity is `entity`, or -1.
inline int find_room(const std::string &entity) {
  auto &rs = rooms();
  for (int i = 0; i < kRooms; i++)
    if (!rs[i].slug.empty() && entity_of(rs[i]) == entity) return i;
  return -1;
}

// Room whose data stands in for `selected`: the configured room itself, else
// the first configured room grouped under it (a coordinator that is not one of
// the 7 slots, e.g. adopted from topology). -1 when nothing matches.
inline int resolve_room(const std::string &selected) {
  int i = find_room(selected);
  if (i >= 0) return i;
  auto &rs = rooms();
  for (int j = 0; j < kRooms; j++)
    if (!rs[j].slug.empty() && coord_of(rs[j]) == selected) return j;
  return -1;
}

}  // namespace media_rooms
