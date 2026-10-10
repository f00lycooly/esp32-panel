// media-rooms.h — panel-side cache of the 7 compile-time Sonos room slots.
//
// Pulled in by page-media.yaml (`esphome: includes:`). Each room slot
// (media-room.yaml, one package per slot) binds media_player.<slug> directly
// and writes what HA pushes into rooms()[idx]; page-media's scripts read the
// cache back to paint now-playing, resolve the playback target and rebuild the
// group topology. This replaces the per-panel HA mirror sensor + input_text
// selection helper and the polled sensor.sonos_topology template.
//
// Plain C++ with no ESPHome dependencies, so the logic can be unit-tested on
// the host (g++ -std=c++17).
#pragma once

#include <algorithm>
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
  std::string content_id;            // media_content_id (TV / line-in detection)
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

// External input: Sonos home-theatre stream (TV) or line-in. A room in that
// state reports `tv` instead of `playing` so now-playing and auto-follow
// ignore TV audio. Judged on media_content_id alone: Sonos drops the `source`
// attribute while streaming, and HA never pushes an attribute's removal, so a
// cached `source: TV` would stick after switching back to music. Every Sonos
// room always reports media_content_id, so it is always current.
inline bool is_tv(const Room &r) {
  return r.content_id.rfind("x-sonos-htastream", 0) == 0 ||
         r.content_id.rfind("x-rincon-stream:", 0) == 0;
}

// Cache updates that drop stale metadata. HA never pushes an attribute's
// removal (the TV input has no entity_picture, for example), so the old
// track's title/artist/art would otherwise linger.
//   • New media_content_id → the track changed; forget the old metadata (the
//     new title/artist/art follow in the same push).
//   • State not playing/paused (idle, off, unavailable, ...) → nothing is
//     loaded; forget the metadata and content id.
inline void set_content(Room &r, const std::string &x) {
  if (x != r.content_id) { r.title.clear(); r.artist.clear(); r.art.clear(); }
  r.content_id = x;
}
inline void set_state(Room &r, const std::string &x) {
  r.state = x;
  if (x != "playing" && x != "paused") {
    r.title.clear(); r.artist.clear(); r.art.clear(); r.content_id.clear();
  }
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

// ═══════ Topology (replaces the HA sonos_topology package) ═══════
// Rebuilt from the cache above in the formats the old sensor.sonos_topology
// published, so the Rooms modal / Rooms Overview / auto-follow / Group code
// reads the same strings. Semantics follow the old Jinja: a room is a
// coordinator when group_members[0] is itself; a group on an external input
// (judged on its coordinator) is `tv`, never `playing`.
//
// Limit vs the old template: only the 7 configured rooms are seen. A
// coordinator outside the 7 is inferred from a configured member's
// group_members (its state / volume / track come from that member, which
// Sonos keeps in step with the coordinator); a group made only of
// unconfigured rooms is invisible.

inline std::string slug_of(const std::string &entity) {
  size_t d = entity.rfind('.');
  return d == std::string::npos ? entity : entity.substr(d + 1);
}

// "living_room" -> "Living Room" (label for a member outside the 7 slots).
inline std::string pretty_slug(const std::string &slug) {
  std::string out;
  bool up = true;
  for (char c : slug) {
    if (c == '_') { out += ' '; up = true; continue; }
    out += up ? (char) std::toupper((unsigned char) c) : c;
    up = false;
  }
  return out;
}

inline std::string label_of(const std::string &entity) {
  int i = find_room(entity);
  if (i >= 0 && !rooms()[i].caption.empty()) return rooms()[i].caption;
  return pretty_slug(slug_of(entity));
}

// volume_level 0..1 -> rounded percent; unknown -> 0 (Jinja float(0)).
inline int vol_pct(float v) {
  if (std::isnan(v)) return 0;
  return (int) std::lround(v * 100.0f);
}

// '|' separates blob fields; keep it out of free text (track titles).
inline std::string clean(const std::string &s) {
  std::string out = s;
  for (auto &c : out) if (c == '|') c = '/';
  return out;
}

// Per-room row: what the old `rooms` attribute carried for one speaker.
struct RoomStatus {
  std::string state;  // HA state, `tv` instead of `playing` on an external input
  int coord = 0;      // 1 when this room is its group's coordinator
  int size = 1;       // group size (1 = standalone)
  int vol = 0;        // volume %
  // "state|isCoord|size" — the apply_room_status contract.
  std::string info() const {
    return state + "|" + std::to_string(coord) + "|" + std::to_string(size);
  }
};

inline RoomStatus room_status(int i) {
  const Room &r = rooms()[i];
  std::string c = coord_of(r);
  int ci = find_room(c);
  RoomStatus s;
  // TV is judged on the coordinator; fall back to the room itself when the
  // coordinator is not one of the 7 slots.
  s.state = display_state(r, is_tv(ci >= 0 ? rooms()[ci] : r));
  s.coord = (c == entity_of(r)) ? 1 : 0;
  s.size = r.members.empty() ? 1 : (int) r.members.size();
  s.vol = vol_pct(r.volume);
  return s;
}

// Every PLAYING (non-TV) group, one blob per coordinator, sorted by
// coordinator entity id (the old g0..g3 order; callers keep the first 4, and
// size() is the old g_count):
//   "coordSlug|playing|vol|label|membersCsv|title|artist|art"
// label = member captions joined " + "; membersCsv = member slugs.
inline std::vector<std::string> playing_groups() {
  auto &rs = rooms();
  struct G { std::string coord; int rep; };
  std::vector<G> gs;
  for (int i = 0; i < kRooms; i++) {
    if (rs[i].slug.empty()) continue;
    std::string c = coord_of(rs[i]);
    bool seen = false;
    for (auto &g : gs) if (g.coord == c) { seen = true; break; }
    if (seen) continue;
    int rep = find_room(c);
    if (rep >= 0) {
      // A configured coordinator must agree it leads (group_members[0] == self).
      if (coord_of(rs[rep]) != c) continue;
    } else {
      rep = i;  // unconfigured coordinator: first configured member stands in
    }
    const Room &r = rs[rep];
    if (r.state != "playing" || is_tv(r)) continue;
    gs.push_back({c, rep});
  }
  std::sort(gs.begin(), gs.end(), [](const G &a, const G &b) { return a.coord < b.coord; });
  std::vector<std::string> out;
  for (auto &g : gs) {
    const Room &r = rs[g.rep];
    std::vector<std::string> ms = r.members;
    if (ms.empty()) ms.push_back(g.coord);
    std::string label, csv;
    for (size_t k = 0; k < ms.size(); k++) {
      if (k) { label += " + "; csv += ","; }
      label += label_of(ms[k]);
      csv += slug_of(ms[k]);
    }
    out.push_back(slug_of(g.coord) + "|playing|" + std::to_string(vol_pct(r.volume)) + "|" +
                  clean(label) + "|" + csv + "|" + clean(r.title) + "|" + clean(r.artist) + "|" + r.art);
  }
  return out;
}

}  // namespace media_rooms
